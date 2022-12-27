#include "tailcall.h"
#include "vm_backtrace.h"

#include "ruby/internal/config.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

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

#define TCL_MAX 100

#define ESCAPE_SEQUENCES_RED    "\x1B[31;1m"
#define ESCAPE_SEQUENCES_GREEN  "\x1B[32;1m"
#define ESCAPE_SEQUENCES_YELLOW "\x1B[33;1m"
#define ESCAPE_SEQUENCES_BLUE   "\x1B[34;1m"
#define ESCAPE_SEQUENCES_RESET  "\x1B[37;m"

tcl_frame_t *tcl_frame_head = NULL,
            *tcl_frame_tail = NULL;
long tailcall_methods_size_sum = 0;
# define SAVE_MAX 100
char *saved_commands[SAVE_MAX];
int saved_commands_size = 0;

tcl_frame_t* get_tcl_frame_tail(void) { return tcl_frame_tail; } // FIXME: 普通にグローバル変数にしたい

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
                            "        from "
                            ESCAPE_SEQUENCES_RED
                            "(... %d tailcalls truncated by `%s`...)"
                            ESCAPE_SEQUENCES_RESET"\n",
                            m_tmp->truncated_count,
                            m_tmp->truncated_by
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
            free(m_tmp->prev); // FIXME: 要素の中身もfreeするべき
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
                strcat(ret, m_tmp->iseq
                    ? calc_method_name(m_tmp->iseq)
                    : "_"); // ... は _ にして渡す
                strcat(ret, " ");
                if (m_tmp->next == NULL) { break; }
                m_tmp = m_tmp->next;
            }
        }
        strcat(ret, "@"); // non-tailcallの印
        strcat(ret, f_tmp->iseq ? calc_method_name(f_tmp->iseq) : "cfunc");
        strcat(ret, " ");

        if (f_tmp->next == NULL) { break; }
        f_tmp = f_tmp->next;
    }
    ret[strlen(ret) - 1] = '\0'; // trim last whitespace
}

void connect_patern_lang_server(char *send_str,
         char *type_addr, char *save_addr, int *indexes_size_addr,
         int* from_indexes, int* to_indexes) {

    int sock; // ソケットディスクリプタ
    sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    // サーバに接続
    struct sockaddr_in servAddr;
    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    servAddr.sin_port = htons(12345);
    connect(sock, (struct sockaddr*) &servAddr, sizeof(servAddr));

    // 送信
    /* printf("send_str: `%s`\n", send_str); */
    send(sock, send_str, strlen(send_str), 0);

    // 受信
    char buf[TCL_MAX * 100];
    memset(&buf, 0, sizeof(buf));
    recv(sock, buf, sizeof(buf), 0);
    /* printf("buf: `%s`\n", buf); */

    // パース 1行目
    sscanf(buf, "%c %c %d", type_addr, save_addr, indexes_size_addr);
    char type = *type_addr;
    char save = *save_addr;
    int indexes_size = *indexes_size_addr;
    /* printf("%c %c %d\n", type, save, indexes_size); */

    // パース 2行目以降
    char* pos;
    if ((pos = strchr(buf, '\n')) != NULL) {
        strcpy(buf, pos + 1);
    }
    /* printf("buf: `%s`", buf); */
    int index_from;
    int index_to;
    for (int i = 0; i < indexes_size; i++) {
        switch(type) {
          case 'd':
          case 'k':
            sscanf(buf, "%d", &index_from);
            from_indexes[i] = index_from;
            break;
          case 't':
            sscanf(buf, "%d %d", &index_from, &index_to);
            from_indexes[i] = index_from;
            to_indexes[i] = index_to;
            break;
        }
        if ((pos = strchr(buf, '\n')) != NULL) {
            strcpy(buf, pos + 1);
        }
    }
    close(sock);
}

void tcl_prompt(void) {
    char command[1024];

    while (1) {
        printf("pattern> ");
        fgets(command, sizeof(command), stdin);

        if (strcmp(command, "\n") == 0) {
            if (TCL_MAX <= tailcall_methods_size_sum) {
                printf("still over the limit. please enter pattern expression.\n");
                continue;
            } else {
                return;
            }
        }
        command[strlen(command) - 1] = '\0';

        char argument[TCL_MAX * 100] = "";
        tcl_arg(argument);
        strcat(argument, "\n");
        strcat(argument, command);
        /* printf("argument: `%s`\n", argument); */

        char type;
        char save;
        int positions_size;
        int from_positions[TCL_MAX * 100];
        memset(&from_positions, 0, sizeof(from_positions));
        int to_positions[TCL_MAX * 100];
        memset(&to_positions, 0, sizeof(to_positions));
        connect_patern_lang_server(argument, &type, &save, &positions_size, from_positions, to_positions);

        long prev_tailcall_methods_size_sum = tailcall_methods_size_sum;
        switch(type) {
          case 'd': // 消すものを受け取る
          case 'k':
            tcl_delete(from_positions, positions_size);
            break;
          case 't':
            tcl_truncate(from_positions, to_positions, positions_size, command);
            break;
          default:
            printf("bug in tcl_prompt. type: `%c`\n", type);
            exit(EXIT_FAILURE);
        }
        tcl_merge_same_truncated_calls();

        printf("backtrace updated.\n\n");
        printf("current backtrace:\n");
        tcl_print();
        if (type == 'k' || type == 'd') {
            printf("(%ld tailcalls are deleted.)\n", prev_tailcall_methods_size_sum - tailcall_methods_size_sum);
        }
        printf("\n");

        if (save == 's') {
            char *save_command = malloc(1024);
            strcpy(save_command, command);
            saved_commands[saved_commands_size] = save_command;
            saved_commands_size++;
            printf("command `"ESCAPE_SEQUENCES_YELLOW"%s"ESCAPE_SEQUENCES_RESET"` saved.\n", save_command);
            printf("saved commands:\n", save_command);
            print_saved_commands();
            printf("\n");
        }

        if (TCL_MAX <= tailcall_methods_size_sum) {
            printf("still over the limit. please enter pattern expression.\n");
        } else {
            printf("below the limit. press ENTER to resume program.\n");
        }
    }
    printf("\n");
}

void tcl_delete(int* positions, int positions_size) {
    if (positions_size == 0) { return; }

    char buf[64];
    int index = 0;
    tcl_frame_t *f_tmp = tcl_frame_head->next; // <main>の前に1個あるけれど飛ばす

    int position_index = 0;
    int position = positions[position_index];

    while (1) {
        tcl_tailcall_method_t *m_tmp = f_tmp->tailcall_methods_head;

        while (m_tmp != NULL) {
            /* printf("index: %d, position: %d\n", index, position); */
            if (index == position) {
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
                position_index++;
                position = positions[position_index];
            }

            if (m_tmp->next == NULL) { break; }
            m_tmp = m_tmp->next;
            index++;
        }

        /* printf("index: %d, position: %d\n", index, position); */
        if (index == position) {
            /* printf("match (not deleted)\n"); */
            position_index++;
            position = positions[position_index];
        }

        if (f_tmp->next == NULL) { break; }
        f_tmp = f_tmp->next;
        index++;
    }
}


void tcl_truncate(int* from_positions, int* to_positions,
                  int positions_size, char* command) {
    if (positions_size == 0) { return; }

    char buf[64];
    int index = 0;
    int positions_index = 0;
    int from_position = from_positions[positions_index];
    int to_position = to_positions[positions_index];
    /* printf("index: %d, from_position: %d, to_position: %d\n", index, from_position, to_position); */

    tcl_frame_t *f_tmp = tcl_frame_head->next; // <main>の前に1個あるけれど飛ばす
    tcl_tailcall_method_t *m_tmp_next = NULL;
    tcl_tailcall_method_t *from_position_m = NULL;
    tcl_tailcall_method_t *from_position_m_prev = NULL;
    tcl_tailcall_method_t *to_position_m_next = NULL;

    /* printf("index: %d, from_position: %d, to_position: %d\n", index, from_position, to_position); */
    while (1) {
        tcl_tailcall_method_t *m_tmp = f_tmp->tailcall_methods_head;
        while (1) {
            if (m_tmp == NULL) { break; }

            if (index == from_position) {
                from_position_m = m_tmp;
                from_position_m_prev = m_tmp->prev;
                // 全消し
                for (int i = from_position; i <= to_position; i++) {
                    m_tmp_next = m_tmp->next;
                    free(m_tmp);
                    tailcall_methods_size_sum--;
                    m_tmp = m_tmp_next;
                    index++;
                }
                // 置き換え
                char* truncated_by = malloc(1024);
                strcpy(truncated_by, command);
                truncated_by[strlen(truncated_by) - 1] = '\0'; // chop

                to_position_m_next = m_tmp;
                tcl_tailcall_method_t *m_truncated = malloc(sizeof(tcl_tailcall_method_t));
                *m_truncated = (tcl_tailcall_method_t) {
                    NULL,
                    NULL,
                    truncated_by,
                    (to_position - from_position) + 1, // truncated_count
                    NULL,
                    NULL
                };
                tailcall_methods_size_sum++;

                if (from_position_m_prev == NULL && to_position_m_next == NULL) {
                    f_tmp->tailcall_methods_head = m_truncated;
                    f_tmp->tailcall_methods_tail = m_truncated;
                } else if (from_position_m_prev == NULL) {
                    f_tmp->tailcall_methods_head = m_truncated;
                    m_truncated->next = to_position_m_next;
                    to_position_m_next->prev = m_truncated;
                } else if (to_position_m_next == NULL) {
                    f_tmp->tailcall_methods_tail = m_truncated;
                    m_truncated->prev = from_position_m_prev;
                    from_position_m_prev->next = m_truncated;
                } else {
                    m_truncated->prev = from_position_m_prev;
                    from_position_m_prev->next = m_truncated;
                    m_truncated->next = to_position_m_next;
                    to_position_m_next->prev = m_truncated;
                }
                // 更新
                positions_index++;
                from_position = from_positions[positions_index];
                to_position = to_positions[positions_index];

                if (m_tmp == NULL) {
                    index--; // 上げすぎた分を相殺
                    break;
                }
            } else {
                if (m_tmp->next == NULL) { break; }
                m_tmp = m_tmp->next;
                index++;
            }
            /* printf("index: %d, from_position: %d, to_position: %d\n", index, from_position, to_position); */
        }

        if (f_tmp->next == NULL) { break; }
        f_tmp = f_tmp->next;
        index++;
        /* printf("index: %d, from_position: %d, to_position: %d\n", index, from_position, to_position); */
    }
}

void tcl_record(rb_iseq_t *iseq, VALUE *pc) {
    if (TCL_MAX <= tailcall_methods_size_sum && saved_commands_size > 0) {
        tcl_apply_saved();
    }
    if (TCL_MAX <= tailcall_methods_size_sum) {
        printf("log size limit reached. please enter pattern expression what logs to discard.\n\ncurrent backtrace:\n");
        tcl_print();
        printf("\n");
        tcl_prompt();
    }

    tcl_frame_tail->tailcall_methods_size += 1;
    tailcall_methods_size_sum += 1;

    tcl_tailcall_method_t *new_method_name = malloc(sizeof(tcl_tailcall_method_t));
    *new_method_name = (tcl_tailcall_method_t) {
         iseq,
         pc,
         NULL, // truncated_by
         0, // truncated_count
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

void print_saved_commands(void) {
    for (int i = 0; i < saved_commands_size; i++) {
        printf("        %d: "ESCAPE_SEQUENCES_YELLOW"%s"ESCAPE_SEQUENCES_RESET"\n", i + 1, saved_commands[i]);
    }
}

void tcl_apply_saved(void) {
    char *command;
    char buf[1024];

    for (int i = 0; i < saved_commands_size; i++) {
        command = saved_commands[i];

        char argument[TCL_MAX * 100] = "";
        tcl_arg(argument);
        strcat(argument, "\n");
        strcat(argument, command);
        /* printf("argument: `%s`\n", argument); */

        char type;
        char save;
        int positions_size;
        int from_positions[TCL_MAX * 100];
        memset(&from_positions, 0, sizeof(from_positions));
        int to_positions[TCL_MAX * 100];
        memset(&to_positions, 0, sizeof(to_positions));
        connect_patern_lang_server(argument, &type, &save, &positions_size, from_positions, to_positions);

        long prev_tailcall_methods_size_sum = tailcall_methods_size_sum;
        switch(type) {
          case 'd': // 消すものを受け取る
          case 'k':
            tcl_delete(from_positions, positions_size);
            break;
          case 't':
            tcl_truncate(from_positions, to_positions, positions_size, command);
            break;
          default:
            printf("bug in tcl_apply_saved\n");
            exit(EXIT_FAILURE);
        }
        tcl_merge_same_truncated_calls();
    }
}

void tcl_merge_same_truncated_calls() {
    tcl_frame_t *f_tmp = tcl_frame_head->next; // <main>の前に1個あるけれど飛ばす
    while (1) {
        tcl_tailcall_method_t *m_tmp = f_tmp->tailcall_methods_head;
        while (1) {
            if (m_tmp == NULL) { break; }

            if (m_tmp->truncated_by  &&
                m_tmp->prev && m_tmp->prev->truncated_by &&
                strcmp(m_tmp->truncated_by, m_tmp->prev->truncated_by) == 0) {

                m_tmp->truncated_count += m_tmp->prev->truncated_count;
                // TODO: freeする
                if (m_tmp->prev->prev == NULL) {
                    f_tmp->tailcall_methods_head = m_tmp;
                    m_tmp->prev = NULL;
                } else {
                    m_tmp->prev->prev->next = m_tmp;
                    m_tmp->prev = m_tmp->prev->prev;
                }
                tailcall_methods_size_sum--;
            }

            if (m_tmp->next == NULL) { break; }
            m_tmp = m_tmp->next;
        }
        if (f_tmp->next == NULL) { break; }
        f_tmp = f_tmp->next;
    }
}

