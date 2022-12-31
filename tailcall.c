#include "tailcall.h"
#include "iseq.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

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
  return StringValuePtr(iseq->body->location.label);
}

void tcl_pretty_print(void) {
    tcl_frame_t *f = tcl_frame_tail;

    bool first_call = true;
    while (1) {
        printf(
            "%s%s:%d:in `%s'%s\n",
            first_call ? "" : "        from ",
            RSTRING_PTR(rb_iseq_path(f->iseq)),
            calc_lineno(f->iseq, f->pc),
            f->cfunc ? f->cfunc : calc_method_name(f->iseq),
            first_call ? ESCAPE_SEQUENCES_BLUE" (calling)"ESCAPE_SEQUENCES_RESET : ""
        );
        tcl_tailcall_method_t *m = f->tailcall_methods_tail;
        if (m != NULL) {
            while (1) {
                if (m->iseq) {
                    printf(
                            "        from %s:%d:in `%s' %s\n",
                            RSTRING_PTR(rb_iseq_path(m->iseq)),
                            calc_lineno(m->iseq, m->pc),
                            calc_method_name(m->iseq),
                            ESCAPE_SEQUENCES_GREEN"(tailcall)"ESCAPE_SEQUENCES_RESET
                          );
                } else { // ... の場合
                    printf(
                            "        from "
                            ESCAPE_SEQUENCES_RED
                            "(... %d tailcalls truncated by `%s`...)"
                            ESCAPE_SEQUENCES_RESET"\n",
                            m->truncated_count,
                            m->truncated_by
                          );
                }
                if (m->prev == NULL) { break; }
                m = m->prev;
            }
        }
        if (f->prev->prev == NULL) { break; }
        f = f->prev;
        first_call = false;
    }
}

void tcl_print(void) {
    tcl_frame_t *f = tcl_frame_head->next;
    bool first_call = true;
    while (1) {
        if (!first_call) { printf(" -> "); }
        first_call = false;

        tcl_tailcall_method_t *m = f->tailcall_methods_head;
        if (m != NULL) {
            while (1) {
                if (m->iseq) {
                    printf(
                            ESCAPE_SEQUENCES_GREEN"%s"ESCAPE_SEQUENCES_RESET,
                            calc_method_name(m->iseq)
                          );
                } else { // ... の場合
                    printf(
                            ESCAPE_SEQUENCES_RED
                            "(`%s` * %d)"
                            ESCAPE_SEQUENCES_RESET,
                            m->truncated_by,
                            m->truncated_count
                          );
                }
                printf(" => ");
                if (m->next == NULL) { break; }
                m = m->next;
            }
        }

        bool current_call = f->next == NULL;
        if (current_call) { printf(ESCAPE_SEQUENCES_BLUE); }
        printf(f->cfunc ? f->cfunc : calc_method_name(f->iseq));
        if (current_call) { printf(ESCAPE_SEQUENCES_RESET); }

        if (f->next == NULL) { break; }
        f = f->next;
    }
    printf("\n");
}

void tcl_push(rb_iseq_t *iseq, VALUE *pc, char *cfunc) {
    tcl_frame_t *new_frame = (tcl_frame_t*)malloc(sizeof(tcl_frame_t));
    *new_frame = (tcl_frame_t) {
        iseq,
        pc,
        cfunc,
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
    tcl_tailcall_method_t* m = tail_frame->tailcall_methods_head;
    if (m != NULL) {
        while (1) {
            if (m->next == NULL) break;
            m = m->next;
            free(m->prev); // FIXME: 要素の中身もfreeするべき
            tailcall_methods_size_sum--;
        }
        free(m);
        tailcall_methods_size_sum--;
    }
    free(tail_frame);
}

void tcl_arg(char* ret) { // retが戻り値
    tcl_frame_t *f = tcl_frame_head->next; // <main>の前に1個あるけれど飛ばす

    while (1) {
        tcl_tailcall_method_t *m = f->tailcall_methods_head;
        if (m != NULL) {
            while (1) {
                strcat(ret, m->iseq
                    ? calc_method_name(m->iseq)
                    : "_"); // ... は _ にして渡す
                strcat(ret, "->");
                if (m->next == NULL) { break; }
                m = m->next;
            }
        }
        strcat(ret, f->iseq ? calc_method_name(f->iseq) : "cfunc"); // FIXME: ちゃんと求める
        strcat(ret, "->");

        if (f->next == NULL) { break; }
        f = f->next;
    }
    ret[strlen(ret) - 1] = '\0'; // trim last `>`
    ret[strlen(ret) - 1] = '\0'; // trim last `-`
}

void connect_patern_lang_server(char *send_str,
         char *type_addr, char *save_addr, int *indexes_size_addr,
         int* indexes) {

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
    char buf2[TCL_MAX * 100]; // TODO: 同一のchar*でstrcpyすると謎にバグるから、一旦buf2にstrcpyしてからbufに戻す
    if ((pos = strchr(buf, '\n')) != NULL) {
        strcpy(buf2, pos + 1);
        strcpy(buf, buf2);
    }
    /* printf("buf: `%s`", buf); */
    for (int i = 0; i < indexes_size; i++) {
        sscanf(buf, "%d", &indexes[i]);

        if ((pos = strchr(buf, '\n')) != NULL) {
            strcpy(buf2, pos + 1);
            strcpy(buf, buf2);
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
        } else if (strcmp(command, "b\n") == 0) {
            printf("current backtrace:\n");
            tcl_print();
            printf("\n");
            continue;
        } else if (strcmp(command, "bt\n") == 0) {
            printf("current backtrace (full):\n");
            tcl_pretty_print();
            printf("\n");
            continue;
        } else if (strcmp(command, "ls\n") == 0) {
            printf("saved commands:\n");
            print_saved_commands();
            printf("\n");
            continue;
        } else if (strstr(command, "rm ") == command) {
            int n = 0;
            sscanf(command, "rm %d", &n);
            if (0 < n && n <= saved_commands_size) {
                remove_saved_commands(n - 1);
                printf("reomved saved command.\n");
                printf("saved commands:\n");
                print_saved_commands();
            } else {
                printf("invalid number.\n");
            }
            printf("\n");
            continue;
        } else if (strcmp(command, "q\n") == 0) {
            printf("quit\n");
            exit(EXIT_SUCCESS);
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
        int positions[TCL_MAX * 100];
        memset(&positions, 0, sizeof(positions));
        connect_patern_lang_server(argument, &type, &save, &positions_size, positions);

        long prev_tailcall_methods_size_sum = tailcall_methods_size_sum;
        switch(type) {
          case 'd': // 消すものを受け取る
          case 'k':
            tcl_delete(positions, positions_size);
            break;
          case 't':
            tcl_truncate(positions, positions_size, command);
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
            printf("saved commands:\n");
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
    int position_index = 0;
    int position = positions[position_index];
    tcl_frame_t *f = tcl_frame_head->next; // <main>の前に1個あるけれど飛ばす

    while (1) {
        tcl_tailcall_method_t *m = f->tailcall_methods_head;
        while (1) {
            if (m == NULL) { break; }
            /* printf("\nindex: %d, position: %d, position_index: %d\n", index, position, position_index); */

            if (index == position) {
                /* printf("match\n"); */
                // delete tailcall_method
                if (m->next == NULL && m->prev == NULL) {
                    f->tailcall_methods_head = NULL;
                    f->tailcall_methods_tail = NULL;
                } else if (m->next == NULL) {
                    m->prev->next = NULL;
                    f->tailcall_methods_tail = m->prev;
                } else if (m->prev == NULL) {
                    m->next->prev = NULL;
                    f->tailcall_methods_head = m->next;
                } else {
                    m->prev->next = m->next;
                    m->next->prev = m->prev;
                }
                tailcall_methods_size_sum--;
                // TODO free
                // update index
                position_index++;
                position = positions[position_index];
            }
            index++;
            if (m->next == NULL) { break; }
            m = m->next;
        }
        if (index == position) {
            /* printf("match (not deleted)\n"); */
            position_index++;
            position = positions[position_index];
        }
        /* printf("\nindex: %d, position: %d, position_index: %d\n", index, position, position_index); */
        index++;
        if (f->next == NULL) { break; }
        f = f->next;
    }
}


void tcl_truncate(int* positions, int positions_size, char* command) {
    if (positions_size == 0) { return; }

    char buf[64];
    int index = 0;
    int position_index = 0;
    int position = positions[position_index];
    tcl_frame_t *f = tcl_frame_head->next; // <main>の前に1個あるけれど飛ばす

    while (1) {
        tcl_tailcall_method_t *m = f->tailcall_methods_head;
        while (1) {
            if (m == NULL) { break; }
            /* printf("\nindex: %d, position: %d, position_index: %d\n", index, position, position_index); */

            if (index == position) {
                /* printf("match\n"); */
                // replace
                char* truncated_by = malloc(1024);
                strcpy(truncated_by, command);
                truncated_by[strlen(truncated_by) - 1] = '\0'; // chop

                tcl_tailcall_method_t *m_truncated = malloc(sizeof(tcl_tailcall_method_t));
                *m_truncated = (tcl_tailcall_method_t) { NULL, NULL, truncated_by, 1 /* truncated_count */, NULL, NULL };

                if (m->next == NULL && m->prev == NULL) {
                    f->tailcall_methods_head = m_truncated;
                    f->tailcall_methods_tail = m_truncated;
                } else if (m->next == NULL) {
                    m->prev->next = m_truncated;
                    m_truncated->prev = m->prev;
                    f->tailcall_methods_tail = m_truncated;
                } else if (m->prev == NULL) {
                    m->next->prev = m_truncated;
                    m_truncated->next = m->next;
                    f->tailcall_methods_head = m_truncated;
                } else {
                    m->prev->next = m_truncated;
                    m->next->prev = m_truncated;
                    m_truncated->next = m->next;
                    m_truncated->prev = m->prev;
                }
                // update index
                position_index++;
                position = positions[position_index];
            }
            index++;
            if (m->next == NULL) { break; }
            m = m->next;
        }
        if (index == position) {
            /* printf("match (not deleted)\n"); */
            position_index++;
            position = positions[position_index];
        }

        /* printf("\nindex: %d, position: %d, position_index: %d\n", index, position, position_index); */
        index++;
        if (f->next == NULL) { break; }
        f = f->next;
    }
}

void tcl_record(rb_iseq_t *iseq, VALUE *pc) {
    if (TCL_MAX <= tailcall_methods_size_sum && saved_commands_size > 0) {
        tcl_apply_saved();
    }
    if (TCL_MAX <= tailcall_methods_size_sum) {
        printf("log size limit reached. please enter pattern expression what logs to discard.\n\n");
        printf("current backtrace:\n");
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

void remove_saved_commands(int index) {
    saved_commands[index] = NULL;
    for (int i = index + 1; i < saved_commands_size; i++) {
        strcpy(saved_commands[i - 1], saved_commands[i]);
    }
    saved_commands_size--;
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
        int positions[TCL_MAX * 100];
        memset(&positions, 0, sizeof(positions));
        connect_patern_lang_server(argument, &type, &save, &positions_size, positions);

        long prev_tailcall_methods_size_sum = tailcall_methods_size_sum;
        switch(type) {
          case 'd': // 消すものを受け取る
          case 'k':
            tcl_delete(positions, positions_size);
            break;
          case 't':
            tcl_truncate(positions, positions_size, command);
            break;
          default:
            printf("bug in tcl_apply_saved\n");
            exit(EXIT_FAILURE);
        }
        tcl_merge_same_truncated_calls();
    }
}

void tcl_merge_same_truncated_calls() {
    tcl_frame_t *f = tcl_frame_head->next; // <main>の前に1個あるけれど飛ばす
    while (1) {
        tcl_tailcall_method_t *m = f->tailcall_methods_head;
        while (1) {
            if (m == NULL) { break; }

            if (m->truncated_by  &&
                m->prev && m->prev->truncated_by &&
                strcmp(m->truncated_by, m->prev->truncated_by) == 0) {

                m->truncated_count += m->prev->truncated_count;
                // TODO: freeする
                if (m->prev->prev == NULL) {
                    f->tailcall_methods_head = m;
                    m->prev = NULL;
                } else {
                    m->prev->prev->next = m;
                    m->prev = m->prev->prev;
                }
                tailcall_methods_size_sum--;
            }

            if (m->next == NULL) { break; }
            m = m->next;
        }
        if (f->next == NULL) { break; }
        f = f->next;
    }
}

