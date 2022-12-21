#include "tailcall.h"
#include "vm_backtrace.h"

#include "ruby/internal/config.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

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

#define ESCAPE_SEQUENCES_RED    "\x1B[31;1m"
#define ESCAPE_SEQUENCES_GREEN  "\x1B[32;1m"
#define ESCAPE_SEQUENCES_YELLOW "\x1B[33;1m"
#define ESCAPE_SEQUENCES_BLUE   "\x1B[34;1m"
#define ESCAPE_SEQUENCES_RESET  "\x1B[37;m"

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
            first_call ? ESCAPE_SEQUENCES_BLUE" (calling)"ESCAPE_SEQUENCES_RESET : ""
        );
        tcl_tailcall_method_t *m_tmp = f_tmp->tailcall_methods_tail;
        if (m_tmp != NULL) {
            while (1) {
                if (m_tmp->iseq) {
                    printf(
                            "        from %s:%d:in `%s' %s\n",
                            RSTRING_PTR(rb_iseq_path(m_tmp->iseq)),
                            calc_lineno(m_tmp->iseq, m_tmp->pc),
                            calc_method_name(m_tmp->iseq),
                            ESCAPE_SEQUENCES_GREEN"(tailcall)"ESCAPE_SEQUENCES_RESET
                          );
                } else { // ... の場合
                    printf(
                            "        from %s\n",
                            ESCAPE_SEQUENCES_RED"(... truncated tailcalls ...)"ESCAPE_SEQUENCES_RESET
                          );
                }
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

void tcl_arg(char* ret) { // retが戻り値
    tcl_frame_t *f_tmp = tcl_frame_head->next; // <main>の前に1個あるけれど飛ばす

    while (1) {
        tcl_tailcall_method_t *m_tmp = f_tmp->tailcall_methods_head;
        if (m_tmp != NULL) {
            while (1) {
                strcat(ret, calc_method_name(m_tmp->iseq));
                strcat(ret, " ");
                if (m_tmp->next == NULL) { break; }
                m_tmp = m_tmp->next;
            }
        }
        strcat(ret, "\\"); // non-tailcallの印
        strcat(ret, f_tmp->iseq ? calc_method_name(f_tmp->iseq) : "cfunc");
        strcat(ret, " ");

        if (f_tmp->next == NULL) { break; }
        f_tmp = f_tmp->next;
    }
    ret[strlen(ret) - 1] = '\0'; // trim last whitespace
}

void tcl_prompt(void) {
    FILE *fp_arg;
    char str_arg[1024];
    FILE *fp_res;
    char str_res[1024];

    while (1) {
        printf("pattern> ");
        fgets(str_arg, sizeof(str_arg), stdin);

        fp_arg = fopen("argument.txt", "w");
        char arg[TCL_MAX * 100] = "";
        tcl_arg(arg);
        fprintf(fp_arg, "%ld\n%s\n%s", time(NULL), arg, str_arg);
        fflush(fp_arg);
        fclose(fp_arg);
        fopen("result.txt", "w"); // clear contents

        str_res[0] = '\0';
        while (1) {
            fp_res = fopen("result.txt", "r");
            fgets(str_res, sizeof(str_res), fp_res);
            if (str_res[0] != '\0') { break; }

            sleep(1);
            // printf("sleeping\n");
            fclose(fp_res);
        }
        if (str_res[0] == '\n') {
            if (TCL_MAX <= tailcall_methods_size_sum) {
                printf("still over the limit. please enter pattern expression.\n");
            } else {
                printf("resume program.\n");
                return;
            }
        } else {
            long prev_tailcall_methods_size_sum = tailcall_methods_size_sum;
            tcl_overlimit(str_res[0], fp_res);
            printf("\ncurrent backtrace:\n");
            tcl_print();
            printf("\n");
            printf("(%ld tailcalls are deleted.)\n", prev_tailcall_methods_size_sum - tailcall_methods_size_sum);
            if (TCL_MAX <= tailcall_methods_size_sum) {
                printf("still over the limit. please enter pattern expression.\n");
            } else {
                printf("below the limit. press ENTER to complete.\n");
            }
        }
        fclose(fp_res); // while内でbreakした場合はfcloseを通らないので
    }
}

void tcl_overlimit(char type, FILE* fp) {
    char buf[64];

    int index = 0;
    int pos = -1;
    tcl_frame_t *f_tmp = tcl_frame_head->next; // <main>の前に1個あるけれど飛ばす

    switch(type) {
      case 'd': // 消すものを受け取る
      case 'k':
        if (fgets(buf, sizeof(buf), fp) == NULL) { return; }
        sscanf(buf, "%d", &pos);

        while (1) {
            tcl_tailcall_method_t *m_tmp = f_tmp->tailcall_methods_head;
            if (m_tmp != NULL) {
                while (1) {
                    /* printf("index: %d, pos: %d\n", index, pos); */
                    if (index == pos) {
                        /* printf("match (deleted)\n"); */
                        // delete tailcall_method
                        if (m_tmp->next == NULL && m_tmp->prev == NULL) {
                            f_tmp->tailcall_methods_head = NULL;
                            f_tmp->tailcall_methods_tail = NULL;
                        } else if (m_tmp->next == NULL) {
                            m_tmp->prev->next = NULL;
                            f_tmp->tailcall_methods_tail = m_tmp->prev;
                        } else if (m_tmp->prev == NULL) {
                            m_tmp->next->prev = NULL;
                            f_tmp->tailcall_methods_head = m_tmp->next;
                        } else {
                            m_tmp->prev->next = m_tmp->next;
                            m_tmp->next->prev = m_tmp->prev;
                        }
                        tailcall_methods_size_sum--;
                        // TODO free
                        if (fgets(buf, sizeof(buf), fp) == NULL) { return; }
                        sscanf(buf, "%d", &pos);
                    }

                    if (m_tmp->next == NULL) { break; }
                    m_tmp = m_tmp->next;
                    index++;
                }
            }

            /* printf("index: %d, pos: %d\n", index, pos); */
            if (index == pos) {
                /* printf("match (not deleted)\n"); */
                if (fgets(buf, sizeof(buf), fp) == NULL) { return; }
                sscanf(buf, "%d", &pos);
            }

            if (f_tmp->next == NULL) { break; }
            f_tmp = f_tmp->next;
            index++;
        }
        break;
      case 't': // truncate
        break;
      default:
        printf("bug\n");
        exit(EXIT_FAILURE);
    }
}


void tcl_record(rb_iseq_t *iseq, VALUE *pc) {
    if (TCL_MAX <= tailcall_methods_size_sum) {
        printf("log size limit reached. please enter pattern expression what logs to discard.\n\ncurrent backtrace:\n");
        tcl_print();
        printf("\n");
        tcl_prompt();
        printf("\n");
    }

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

void tcl_change_top(const rb_iseq_t *iseq, VALUE *pc) {
    tcl_frame_tail->iseq = iseq;
    tcl_frame_tail->pc = pc;
}

