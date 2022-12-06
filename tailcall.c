#include "tailcall.h"
#include "vm_backtrace.h"

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

#define TCL_MAX 10

tcl_frame_t *tcl_frame_head = NULL,
            *tcl_frame_tail = NULL;
tcl_frame_t* get_tcl_frame_tail(void) { return tcl_frame_tail; } // FIXME: 普通にグローバル変数にしたい
long tailcall_methods_size_sum = 0;

long tcl_log_size(void) { // FIXME: 「このフレーム以降のサイズ」を返せるようにする
    return tailcall_methods_size_sum;
}

char* calc_method_name(rb_iseq_t *iseq) {
  return StringValuePtr(ISEQ_BODY(iseq)->location.label);
}

void tcl_print(void) {
    tcl_frame_t *f_tmp = tcl_frame_tail;

    bool first_call = true;
    while (1) {
        printf(
            "%s%s:%d:in `%s'%s\n",
            first_call ? "" : "        from ",
            RSTRING_PTR(rb_iseq_path(f_tmp->iseq)),
            calc_lineno(f_tmp->iseq, f_tmp->pc),
            f_tmp->iseq ? calc_method_name(f_tmp->iseq) : "<cfunc>",
            first_call ? " (calling)" : ""
        );
        tcl_tailcall_method_t *m_tmp = f_tmp->tailcall_methods_tail;
        if (m_tmp != NULL) {
            while (1) {
                printf(
                    "        from %s:%d:in `%s' (tailcall)\n",
                    RSTRING_PTR(rb_iseq_path(m_tmp->iseq)),
                    calc_lineno(m_tmp->iseq, m_tmp->pc),
                    calc_method_name(m_tmp->iseq)
                );
                if (m_tmp->prev == NULL) { break; }
                m_tmp = m_tmp->prev;
            }
        }
        if (f_tmp->prev->prev == NULL) { break; }
        f_tmp = f_tmp->prev;
        first_call = false;
    }
}

void tcl_push(rb_iseq_t *iseq, VALUE *pc) {
    // allocate
    tcl_frame_t *new_frame = (tcl_frame_t*)malloc(sizeof(tcl_frame_t));
    *new_frame = (tcl_frame_t) {
        iseq,
        pc,
        NULL, // tailcall_methods_head
        NULL, // tailcall_methods_tail
        0, // tailcall_methods_size
        tcl_frame_tail, // prev
        NULL // next
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

void tcl_record(rb_iseq_t *iseq, VALUE *pc) {
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

    int i = 0;
    if (TCL_MAX <= tailcall_methods_size_sum) {
        printf("Reach limit!!! please enter expression to indicate what logs to discard.\n\n");
        tcl_print();
        printf("> ");
        scanf("%d", &i);
        printf("i: %d\n", i);
    }
}

void tcl_change_top(const rb_iseq_t *iseq, VALUE *pc) {
    tcl_frame_tail->iseq = iseq;
    tcl_frame_tail->pc = pc;
}

