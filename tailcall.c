#include "tailcall.h"

#include "ruby/internal/config.h"

#include <stdio.h>
#include <stdbool.h>

#include "vm_core.h"
#include "method.h"
#include "iseq.h"

#include "constant.h"
#include "debug_counter.h"
#include "internal.h"
#include "internal/class.h"
#include "internal/compar.h"
#include "internal/hash.h"
#include "internal/numeric.h"
#include "internal/proc.h"
#include "internal/random.h"
#include "internal/variable.h"
#include "internal/struct.h"
#include "variable.h"

VALUE rb_obj_equal(VALUE obj1, VALUE obj2);

tcl_frame_t *tcl_frame_head = NULL,
            *tcl_frame_tail = NULL;
tcl_frame_t* get_tcl_frame_tail(void) { return tcl_frame_tail; } // FIXME: 普通にグローバル変数にしたい
long tailcall_methods_size_sum = 0;

tcl_filter_t** tcl_filters = NULL; // 定数ではない rb_ary_new() による初期化はできないのでnilで初期化している
long tcl_filters_size = 0;

VALUE get_tcl_filters(void) {
    return Qnil; // TODO: ちゃんと返す
}

void set_tcl_filters(VALUE val) {
    // rb -> C の変換を行う
    if (tcl_filters) { free(tcl_filters); } // TODO: もっと内側もfreeする
    if (!rb_obj_is_kind_of(val, rb_cArray)) { rb_raise(rb_eTypeError, "class of `$tcl_filter' is not Array"); }

    tcl_filters_size = RARRAY_LEN(val);
    tcl_filters = malloc(sizeof(tcl_filter_t) * tcl_filters_size);
    for (long i = 0; i < tcl_filters_size; i++) {
        VALUE v = RARRAY_AREF(val, i);
        if (!rb_obj_is_kind_of(v, rb_cHash)) { rb_raise(rb_eTypeError, "class of `$tcl_filter[n]' is not Hash"); }

        VALUE name = rb_hash_aref(v, rb_str_intern(rb_str_new2("method")));
        if (!rb_obj_is_kind_of(name, rb_cString)) { rb_raise(rb_eTypeError, "class of `$tcl_filter[n][:name]' is not String"); }

        VALUE filter = rb_hash_aref(v, rb_str_intern(rb_str_new2("filter")));
        if (!rb_obj_is_kind_of(filter, rb_cSymbol)) { rb_raise(rb_eTypeError, "class of `$tcl_filter[n][:filter]' is not Symbol"); }

        tcl_filter_type filter_type = TCL_FILTER_TYPE_KEEP_NONE;
        if      (RB_TEST(rb_obj_equal(filter, rb_str_intern(rb_str_new2("keep_all")))          )) { filter_type = TCL_FILTER_TYPE_KEEP_ALL; }
        else if (RB_TEST(rb_obj_equal(filter, rb_str_intern(rb_str_new2("keep_begin")))        )) { filter_type = TCL_FILTER_TYPE_KEEP_BEGIN; }
        else if (RB_TEST(rb_obj_equal(filter, rb_str_intern(rb_str_new2("keep_end")))          )) { filter_type = TCL_FILTER_TYPE_KEEP_END;   }
        else if (RB_TEST(rb_obj_equal(filter, rb_str_intern(rb_str_new2("keep_begin_and_end"))))) { filter_type = TCL_FILTER_TYPE_KEEP_BEGIN_AND_END; }
        else if (RB_TEST(rb_obj_equal(filter, rb_str_intern(rb_str_new2("keep_none")))         )) { filter_type = TCL_FILTER_TYPE_KEEP_NONE; }
        else { rb_raise(rb_eTypeError, "`$tcl_filter[n][:filter]' is invalid"); }

        VALUE keep_size = rb_hash_aref(v, rb_str_intern(rb_str_new2("keep_size")));
        if (!rb_obj_is_kind_of(keep_size, rb_cInteger)) { rb_raise(rb_eTypeError, "class of `$tcl_filter[n][:keep_size]' is not Integer"); }

        char* s = malloc(sizeof(char) * strlen(RSTRING_PTR(name)) + 1);
        strcpy(s, RSTRING_PTR(name));
        tcl_filter_t *tcl_filter = (tcl_filter_t*)malloc(sizeof(tcl_filter_t));
        *tcl_filter = (tcl_filter_t) {
            s,
            filter_type,
            NUM2INT(keep_size)
        };
        tcl_filters[i] = tcl_filter;
    }
}

long tcl_log_size(void) { // FIXME: 「このフレーム以降のサイズ」を返せるようにする
    return tailcall_methods_size_sum;
}

int tcl_truncated_size(void) {
    tcl_frame_t *f_tmp = tcl_frame_head;
    if (f_tmp->next == NULL) { return 0; } // 最初はスキップ(MRIにおけるダミー)
    f_tmp = f_tmp->next;

    int count = 0;
    while (1) {
        if (f_tmp->truncated_size > 0) { count += 1; }
        if (f_tmp->next == NULL) { break; }
        f_tmp = f_tmp->next;
    }
    return count;
}

void tcl_print(void) {
    tcl_frame_t *f_tmp = tcl_frame_tail;

    while (1) {
        printf("        from %s\n", f_tmp->name);
        tcl_tailcall_method_t *m_tmp = f_tmp->tailcall_methods_tail;
        if (m_tmp != NULL) {
            while (1) {
                printf(
                    "        from %s:%d:in `%s' (tailcall)\n",
                    RSTRING_PTR(rb_iseq_path(m_tmp->iseq)),
                    calc_lineno(m_tmp->iseq, m_tmp->pc),
                    StringValuePtr(ISEQ_BODY(m_tmp->iseq)->location.label)
                );
                if (m_tmp->prev == NULL) { break; }
                m_tmp = m_tmp->prev;
            }
        }
        if (f_tmp->prev == NULL) { break; }
        f_tmp = f_tmp->prev;
    }
}

void tcl_push(char *method_name) {
    char* prev_method_name = tcl_frame_tail // unless root
                             ? tcl_frame_tail->name
                             : "";
    tcl_filter_type filter_type = TCL_FILTER_TYPE_KEEP_NONE;
    int keep_size = 0;
    for (long i = 0; i < tcl_filters_size; i++) {
        if (strcmp(prev_method_name, tcl_filters[i]->name) == 0) {
            filter_type = tcl_filters[i]->filter_type;
            keep_size = tcl_filters[i]->keep_size;
        }
    }
    // allocate
    tcl_frame_t *new_frame = (tcl_frame_t*)malloc(sizeof(tcl_frame_t));
    *new_frame = (tcl_frame_t) {
        method_name, // name
        NULL, // tailcall_methods_head
        NULL, // tailcall_methods_tail
        0, // tailcall_methods_size
        tcl_frame_tail, // prev
        NULL, // next
        filter_type, // filter_type
        keep_size, // keep_size
        0 // truncated_size
    };
    // push
    if (tcl_frame_tail == NULL) { // if Root
        tcl_frame_tail = new_frame;
        tcl_frame_head = new_frame;
    } else {
        tcl_frame_tail->next = new_frame;
        tcl_frame_tail = new_frame;
    }
}

void tcl_pop(void) {
    // pop
    tcl_frame_t *tail_frame = tcl_frame_tail;
    tcl_frame_tail = tcl_frame_tail->prev;
    tcl_frame_tail->next = NULL;
    // free
    tcl_tailcall_method_t* m_tmp = tail_frame->tailcall_methods_head;
    if (m_tmp != NULL) {
        while (1) {
            if (m_tmp->next == NULL) break;
            m_tmp = m_tmp->next;
            free(m_tmp->prev); // FIXME: 要素の中身もfreeするべきかも
        }
        free(m_tmp);
    }
    free(tail_frame);
}

void tcl_record__filter_type_all(const rb_iseq_t *iseq, VALUE *pc);
void tcl_record__filter_type_end_size1(const rb_iseq_t *iseq, VALUE *pc);
void tcl_record__filter_type_end(const rb_iseq_t *iseq, VALUE *pc);
void tcl_record__filter_type_begin_end(const rb_iseq_t *iseq, VALUE *pc);

void tcl_record(const rb_iseq_t *iseq, VALUE *pc) {
    int size = tcl_frame_tail->tailcall_methods_size;
    int keep_size = tcl_frame_tail->keep_size;

    switch(tcl_frame_tail->filter_type) {
      case TCL_FILTER_TYPE_KEEP_NONE:
        tcl_frame_tail->truncated_size++;
        break;
      case TCL_FILTER_TYPE_KEEP_BEGIN:
        if (keep_size == 0) { // TCL_FILTER_TYPE_KEEP_NONE と同じ
            tcl_frame_tail->truncated_size++;
        } else if (size >= keep_size) {
            tcl_frame_tail->truncated_size++;
        } else {
            tcl_record__filter_type_all(iseq, pc);
        }
        break;
      case TCL_FILTER_TYPE_KEEP_END:
        if (keep_size == 0) { // TCL_FILTER_TYPE_KEEP_NONE と同じ
            tcl_frame_tail->truncated_size++;
        } else if (size >= keep_size) {
            if (size == 1) { // 1個のときは付け替えが例外的な処理になる
                tcl_record__filter_type_end_size1(iseq, pc);
            } else {
                tcl_record__filter_type_end(iseq, pc);
            }
        } else {
          tcl_record__filter_type_all(iseq, pc);
        }
        break;
      case TCL_FILTER_TYPE_KEEP_BEGIN_AND_END:
        if (keep_size == 0) {
            tcl_frame_tail->truncated_size++;
        } else if (size >= keep_size * 2) {
            tcl_record__filter_type_begin_end(iseq, pc);
        } else {
          tcl_record__filter_type_all(iseq, pc);
        }
        break;
      case TCL_FILTER_TYPE_KEEP_ALL:
        tcl_record__filter_type_all(iseq, pc);
        break;
    }
}

void tcl_record__filter_type_all(const rb_iseq_t *iseq, VALUE *pc) {
    int size = tcl_frame_tail->tailcall_methods_size;

    tcl_frame_tail->tailcall_methods_size += 1;
    tailcall_methods_size_sum += 1;

    tcl_tailcall_method_t *new_method_name = malloc(sizeof(tcl_tailcall_method_t));
    *new_method_name = (tcl_tailcall_method_t) {
         iseq,
         pc,
         tcl_frame_tail->tailcall_methods_tail, // prev
         NULL // next
    };

    if (tcl_frame_tail->tailcall_methods_head == NULL) { // if Root
        tcl_frame_tail->tailcall_methods_head = new_method_name;
        tcl_frame_tail->tailcall_methods_tail = new_method_name;
    } else {
        tcl_frame_tail->tailcall_methods_tail->next = new_method_name;
        tcl_frame_tail->tailcall_methods_tail = new_method_name;
    }
}

void tcl_record__filter_type_end_size1(const rb_iseq_t *iseq, VALUE *pc) {
    free(tcl_frame_tail->tailcall_methods_head);

    tcl_tailcall_method_t *new_method_name = malloc(sizeof(tcl_tailcall_method_t));
    *new_method_name = (tcl_tailcall_method_t) {
        iseq,
        pc,
        tcl_frame_tail->tailcall_methods_tail, // prev
        NULL // next
    };

    tcl_frame_tail->tailcall_methods_head = new_method_name;
    tcl_frame_tail->tailcall_methods_tail = new_method_name;
    tcl_frame_tail->truncated_size++;
}

void tcl_record__filter_type_end(const rb_iseq_t *iseq, VALUE *pc) {
    // headを削除
    tcl_frame_tail->tailcall_methods_head = tcl_frame_tail->tailcall_methods_head->next;
    free(tcl_frame_tail->tailcall_methods_head->prev);
    tcl_frame_tail->tailcall_methods_head->prev = NULL;

    tcl_tailcall_method_t *new_method_name = malloc(sizeof(tcl_tailcall_method_t));
    *new_method_name = (tcl_tailcall_method_t) {
        iseq,
        pc,
        tcl_frame_tail->tailcall_methods_tail, // prev
        NULL // next
    };

    tcl_frame_tail->tailcall_methods_tail->next = new_method_name;
    tcl_frame_tail->tailcall_methods_tail = new_method_name;
    tcl_frame_tail->truncated_size++;
}

void tcl_record__filter_type_begin_end(const rb_iseq_t *iseq, VALUE *pc) {
    int keep_size = tcl_frame_tail->keep_size;

    tcl_tailcall_method_t *new_method_name = malloc(sizeof(tcl_tailcall_method_t));
    *new_method_name = (tcl_tailcall_method_t) {
        iseq,
        pc,
        tcl_frame_tail->tailcall_methods_tail, // prev
        NULL // next
    };
    tcl_frame_tail->tailcall_methods_tail->next = new_method_name;
    tcl_frame_tail->tailcall_methods_tail = new_method_name;
    // head+nを削除
    tcl_tailcall_method_t *delete_method = tcl_frame_tail->tailcall_methods_head;
    for (int i = 0; i < keep_size; i++) {
        delete_method = delete_method->next;
    }
    delete_method->prev->next = delete_method->next;
    delete_method->next->prev = delete_method->prev;
    free(delete_method);
    tcl_frame_tail->truncated_size++;
}

void tcl_change_top(char *method_name) {
    tcl_frame_tail->name = method_name;
}

