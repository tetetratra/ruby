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

#define TCL_MAX 100
#define SAVE_MAX 100

#define ESCAPE_SEQUENCES_RED    "\x1B[31;1m"
#define ESCAPE_SEQUENCES_GREEN  "\x1B[32;1m"
#define ESCAPE_SEQUENCES_YELLOW "\x1B[33;1m"
#define ESCAPE_SEQUENCES_BLUE   "\x1B[34;1m"
#define ESCAPE_SEQUENCES_RESET  "\x1B[37;m"

tcl_frame_t *tcl_frame_head = NULL,
            *tcl_frame_tail = NULL;
long tailcalls_size_sum = 0;
char *saved_commands[SAVE_MAX];
int saved_commands_size = 0;

static void log_delete(int* positions, int positions_size);
static void log_truncate(int* positions, int positions_size, char* command);
static void log_uniq();
static void free_tailcall(tcl_tailcall_t *t);
static void free_frame(tcl_frame_t *f);
static void connect_patern_lang_server(char *send_str, char *type_addr, char *save_addr, int *indexes_size_addr, int* indexes);
static void make_arguments(char* ret); // retが戻り値
static void print_log_full(void);
static void print_log_oneline(void);
static void prompt(void);
static void apply_saved(void);
static void print_saved_commands(void);
static void remove_saved_commands(int index);
void tcl_stack_push(rb_iseq_t *iseq, VALUE *pc, char *cfunc);
void tcl_stack_pop(void);
void tcl_stack_record(rb_iseq_t *iseq, VALUE *pc);
void tcl_stack_change_top(rb_iseq_t *iseq, VALUE *pc, char* cfunc);

static char* calc_method_name(rb_iseq_t *iseq) {
    return StringValuePtr(iseq->body->location.label);
}

static void log_delete(int* positions, int positions_size) {
    if (positions_size == 0) { return; }

    int index = 0;
    int position_index = 0;
    int position = positions[position_index];
    tcl_frame_t *f = tcl_frame_head->next; // <main>の前に1個あるけれど飛ばす

    while (true) {
        tcl_tailcall_t *t = f->tailcall_head;
        while (true) {
            if (t == NULL) { break; }
            /* printf("\nindex: %d, position: %d, position_index: %d\n", index, position, position_index); */

            if (index == position) {
                /* printf("match\n"); */
                // delete tailcall
                if (t->next == NULL && t->prev == NULL) {
                    f->tailcall_head = NULL;
                    f->tailcall_tail = NULL;
                } else if (t->next == NULL) {
                    t->prev->next = NULL;
                    f->tailcall_tail = t->prev;
                } else if (t->prev == NULL) {
                    t->next->prev = NULL;
                    f->tailcall_head = t->next;
                } else {
                    t->prev->next = t->next;
                    t->next->prev = t->prev;
                }
                tailcalls_size_sum--;
                free_tailcall(t);
                // update index
                position_index++;
                position = positions[position_index];
            }
            index++;
            if (t->next == NULL) { break; }
            t = t->next;
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

static void log_truncate(int* positions, int positions_size, char* command) {
    if (positions_size == 0) { return; }

    int index = 0;
    int position_index = 0;
    int position = positions[position_index];
    tcl_frame_t *f = tcl_frame_head->next; // <main>の前に1個あるけれど飛ばす

    while (true) {
        tcl_tailcall_t *t = f->tailcall_head;
        while (true) {
            if (t == NULL) { break; }
            /* printf("\nindex: %d, position: %d, position_index: %d\n", index, position, position_index); */

            if (index == position) {
                /* printf("match\n"); */
                // replace
                char* truncated_by = malloc(1024);
                strcpy(truncated_by, command);
                truncated_by[strlen(truncated_by) - 1] = '\0'; // chop

                tcl_tailcall_t *m_truncated = malloc(sizeof(tcl_tailcall_t));
                *m_truncated = (tcl_tailcall_t) { NULL, NULL, truncated_by, 1 /* truncated_count */, NULL, NULL };

                if (t->next == NULL && t->prev == NULL) {
                    f->tailcall_head = m_truncated;
                    f->tailcall_tail = m_truncated;
                } else if (t->next == NULL) {
                    t->prev->next = m_truncated;
                    m_truncated->prev = t->prev;
                    f->tailcall_tail = m_truncated;
                } else if (t->prev == NULL) {
                    t->next->prev = m_truncated;
                    m_truncated->next = t->next;
                    f->tailcall_head = m_truncated;
                } else {
                    t->prev->next = m_truncated;
                    t->next->prev = m_truncated;
                    m_truncated->next = t->next;
                    m_truncated->prev = t->prev;
                }
                // update index
                position_index++;
                position = positions[position_index];
            }
            index++;
            if (t->next == NULL) { break; }
            t = t->next;
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

static void log_uniq() {
    tcl_frame_t *f = tcl_frame_head->next; // <main>の前に1個あるけれど飛ばす
    while (true) {
        tcl_tailcall_t *t = f->tailcall_head;
        while (true) {
            if (t == NULL) { break; }

            if (t->truncated_by  &&
                    t->prev && t->prev->truncated_by &&
                    strcmp(t->truncated_by, t->prev->truncated_by) == 0) {

                t->truncated_count += t->prev->truncated_count;
                free_tailcall(t->prev);
                if (t->prev->prev == NULL) {
                    f->tailcall_head = t;
                    t->prev = NULL;
                } else {
                    t->prev->prev->next = t;
                    t->prev = t->prev->prev;
                }
                tailcalls_size_sum--;
            }

            if (t->next == NULL) { break; }
            t = t->next;
        }
        if (f->next == NULL) { break; }
        f = f->next;
    }
}

static void free_tailcall(tcl_tailcall_t *t) {
    if (t->truncated_by != NULL) { free(t->truncated_by); }
    free(t);
}

static void free_frame(tcl_frame_t *f) {
    free(f);
}

static void connect_patern_lang_server(char *send_str,
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
    int indexes_size = *indexes_size_addr;

    // パース 2行目以降
    char* pos;
    char buf2[TCL_MAX * 100]; // 同一のchar*でstrcpyすると謎にバグるから、一旦buf2にstrcpyしてからbufに戻している
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

static void make_arguments(char* ret) { // retが戻り値
    tcl_frame_t *f = tcl_frame_head->next; // <main>の前に1個あるけれど飛ばす

    while (true) {
        tcl_tailcall_t *t = f->tailcall_head;
        if (t != NULL) {
            while (true) {
                strcat(ret, t->iseq
                        ? calc_method_name(t->iseq)
                        : "_"); // ... は _ にして渡す
                strcat(ret, "->");
                if (t->next == NULL) { break; }
                t = t->next;
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

static void print_log_full(void) {
    tcl_frame_t *f = tcl_frame_tail;

    bool first_call = true;
    while (true) {
        printf(
                "%s%s:%d:in `%s'%s\n",
                first_call ? "" : "        from ",
                RSTRING_PTR(rb_iseq_path(f->iseq)),
                calc_lineno(f->iseq, f->pc),
                f->cfunc ? f->cfunc : calc_method_name(f->iseq),
                first_call ? ESCAPE_SEQUENCES_BLUE" (calling)"ESCAPE_SEQUENCES_RESET : ""
              );
        tcl_tailcall_t *t = f->tailcall_tail;
        if (t != NULL) {
            while (true) {
                if (t->iseq) {
                    printf(
                            "        from %s:%d:in `%s' %s\n",
                            RSTRING_PTR(rb_iseq_path(t->iseq)),
                            calc_lineno(t->iseq, t->pc),
                            calc_method_name(t->iseq),
                            ESCAPE_SEQUENCES_GREEN"(tailcall)"ESCAPE_SEQUENCES_RESET
                          );
                } else { // ... の場合
                    printf(
                            "        from "
                            ESCAPE_SEQUENCES_RED
                            "(... %d tailcalls truncated by `%s`...)"
                            ESCAPE_SEQUENCES_RESET"\n",
                            t->truncated_count,
                            t->truncated_by
                          );
                }
                if (t->prev == NULL) { break; }
                t = t->prev;
            }
        }
        if (f->prev->prev == NULL) { break; }
        f = f->prev;
        first_call = false;
    }
}

static void print_log_oneline(void) {
    tcl_frame_t *f = tcl_frame_head->next;
    bool first_call = true;
    while (true) {
        if (!first_call) { printf(" -> "); }
        first_call = false;

        tcl_tailcall_t *t = f->tailcall_head;
        if (t != NULL) {
            while (true) {
                if (t->iseq) {
                    printf(
                            ESCAPE_SEQUENCES_GREEN"%s"ESCAPE_SEQUENCES_RESET,
                            calc_method_name(t->iseq)
                          );
                } else { // ... の場合
                    printf(
                            ESCAPE_SEQUENCES_RED
                            "(`%s` * %d)"
                            ESCAPE_SEQUENCES_RESET,
                            t->truncated_by,
                            t->truncated_count
                          );
                }
                printf(" => ");
                if (t->next == NULL) { break; }
                t = t->next;
            }
        }

        bool current_call = f->next == NULL;
        if (current_call) { printf(ESCAPE_SEQUENCES_BLUE); }
        printf("%s", f->cfunc ? f->cfunc : calc_method_name(f->iseq));
        if (current_call) { printf(ESCAPE_SEQUENCES_RESET); }

        if (f->next == NULL) { break; }
        f = f->next;
    }
    printf("\n");
}

static void prompt(void) {
    char command[1024];

    while (true) {
        printf("pattern> ");
        fgets(command, sizeof(command), stdin);

        if (strcmp(command, "\n") == 0) {
            if (TCL_MAX <= tailcalls_size_sum) {
                printf("still over the limit. please enter pattern expression.\n");
                continue;
            } else {
                return;
            }
        } else if (strcmp(command, "b\n") == 0) {
            printf("current backtrace:\n");
            print_log_oneline();
            printf("\n");
            continue;
        } else if (strcmp(command, "bt\n") == 0) {
            printf("current backtrace (full):\n");
            print_log_full();
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
        make_arguments(argument);
        strcat(argument, "\n");
        strcat(argument, command);
        /* printf("argument: `%s`\n", argument); */

        char type;
        char save;
        int positions_size;
        int positions[TCL_MAX * 100];
        memset(&positions, 0, sizeof(positions));
        connect_patern_lang_server(argument, &type, &save, &positions_size, positions);

        long prev_tailcalls_size_sum = tailcalls_size_sum;
        switch(type) {
            case 'd': // 消すものを受け取る
            case 'k':
                log_delete(positions, positions_size);
                break;
            case 't':
                log_truncate(positions, positions_size, command);
                break;
            default:
                printf("bug in prompt. type: `%c`\n", type);
                exit(EXIT_FAILURE);
        }
        log_uniq();

        printf("backtrace updated.\n\n");
        printf("current backtrace:\n");
        print_log_oneline();
        if (type == 'k' || type == 'd') {
            printf("(%ld tailcalls are deleted.)\n", prev_tailcalls_size_sum - tailcalls_size_sum);
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

        if (TCL_MAX <= tailcalls_size_sum) {
            printf("still over the limit. please enter pattern expression.\n");
        } else {
            printf("below the limit. press ENTER to resume program.\n");
        }
    }
    printf("\n");
}

static void apply_saved(void) {
    char *command;

    for (int i = 0; i < saved_commands_size; i++) {
        command = saved_commands[i];

        char argument[TCL_MAX * 100] = "";
        make_arguments(argument);
        strcat(argument, "\n");
        strcat(argument, command);
        /* printf("argument: `%s`\n", argument); */

        char type;
        char save;
        int positions_size;
        int positions[TCL_MAX * 100];
        memset(&positions, 0, sizeof(positions));
        connect_patern_lang_server(argument, &type, &save, &positions_size, positions);

        switch(type) {
            case 'd': // 消すものを受け取る
            case 'k':
                log_delete(positions, positions_size);
                break;
            case 't':
                log_truncate(positions, positions_size, command);
                break;
            default:
                printf("bug in apply_saved\n");
                exit(EXIT_FAILURE);
        }
        log_uniq();
    }
}

static void print_saved_commands(void) {
    for (int i = 0; i < saved_commands_size; i++) {
        printf("        %d: "ESCAPE_SEQUENCES_YELLOW"%s"ESCAPE_SEQUENCES_RESET"\n", i + 1, saved_commands[i]);
    }
}

static void remove_saved_commands(int index) {
    saved_commands[index] = NULL;
    for (int i = index + 1; i < saved_commands_size; i++) {
        strcpy(saved_commands[i - 1], saved_commands[i]);
    }
    saved_commands_size--;
}

void tcl_stack_push(rb_iseq_t *iseq, VALUE *pc, char *cfunc) {
    tcl_frame_t *new_frame = (tcl_frame_t*)malloc(sizeof(tcl_frame_t));
    *new_frame = (tcl_frame_t) {
        iseq,
            pc,
            cfunc,
            NULL, // tailcall_head
            NULL, // tailcall_tail
            0, // tailcalls_size
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

void tcl_stack_pop(void) {
    // pop
    tcl_frame_t *tail_frame = tcl_frame_tail;
    tcl_frame_tail = tcl_frame_tail->prev;
    tcl_frame_tail->next = NULL;
    // free
    tcl_tailcall_t* t = tail_frame->tailcall_head;
    if (t != NULL) {
        while (true) {
            if (t->next == NULL) break;
            t = t->next;
            free_tailcall(t->prev);
            tailcalls_size_sum--;
        }
        free_tailcall(t);
        tailcalls_size_sum--;
    }
    free_frame(tail_frame);
}

void tcl_stack_record(rb_iseq_t *iseq, VALUE *pc) {
    if (TCL_MAX <= tailcalls_size_sum && saved_commands_size > 0) {
        apply_saved();
    }
    if (TCL_MAX <= tailcalls_size_sum) {
        printf("log size limit reached. please enter pattern expression what logs to discard.\n\n");
        printf("current backtrace:\n");
        print_log_oneline();
        printf("\n");
        prompt();
    }

    tcl_frame_tail->tailcalls_size += 1;
    tailcalls_size_sum += 1;

    tcl_tailcall_t *new_tailcall = malloc(sizeof(tcl_tailcall_t));
    *new_tailcall = (tcl_tailcall_t) {
        iseq,
            pc,
            NULL, // truncated_by
            0, // truncated_count
            tcl_frame_tail->tailcall_tail, // prev
            NULL // next
    };

    if (tcl_frame_tail->tailcall_head == NULL) { // if Root
        tcl_frame_tail->tailcall_head = new_tailcall;
        tcl_frame_tail->tailcall_tail = new_tailcall;
    } else {
        tcl_frame_tail->tailcall_tail->next = new_tailcall;
        tcl_frame_tail->tailcall_tail = new_tailcall;
    }
}

void tcl_stack_change_top(rb_iseq_t *iseq, VALUE *pc, char* cfunc) {
    tcl_frame_tail->iseq = iseq;
    tcl_frame_tail->pc = pc;
    tcl_frame_tail->cfunc = cfunc;
}

