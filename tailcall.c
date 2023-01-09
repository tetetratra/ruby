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
#define FRAME_MAX 2000 // Rubyのスタックの上限を，本来の10000強よりも小さく設定する
#define SAVE_MAX 100
#define METHOD_NAME_SIZE_MAX 20 // メソッド名が20文字以上のメソッドは無いはず
#define RUBY_STACK_MAX 20000 // Rubyの再帰呼び出しの上限は10100回程度みたいなので，余裕を持って20000回

#define ESCAPE_SEQUENCES_RED    "\x1B[31;1m"
#define ESCAPE_SEQUENCES_GREEN  "\x1B[32;1m"
#define ESCAPE_SEQUENCES_YELLOW "\x1B[33;1m"
#define ESCAPE_SEQUENCES_BLUE   "\x1B[34;1m"
#define ESCAPE_SEQUENCES_RESET  "\x1B[37;m"

bool enable_tailcall_log;
bool interactive = false;
tcl_frame_t *tcl_frame_head = NULL,
            *tcl_frame_tail = NULL;
long tailcalls_size_sum = 0;
char *saved_commands[SAVE_MAX];
int saved_commands_size = 0;

static void log_delete(int* positions, int positions_size, bool include_log);
static void log_truncate(int* positions, int positions_size, char* command, bool include_log);
static void log_uniq(void);
static void free_tailcall(tcl_tailcall_t *t);
static void free_frame(tcl_frame_t *f);
static void connect_patern_lang_server(char *send_str, char *type_addr, bool *save_addr, bool *include_log_addr, int *indexes_size_addr, int* indexes);
static void make_arguments(char* ret); // retが戻り値
static void print_log_full(void);
static void print_log_oneline(void);
static void prompt(void);
static void print_saved_commands(void);
static void remove_saved_commands(int index);
void apply_saved(void);
void tcl_stack_push(rb_iseq_t *iseq, VALUE *pc, char *cfunc);
void tcl_stack_pop(void);
void tcl_stack_record(rb_iseq_t *iseq, VALUE *pc);
void tcl_stack_change_top(rb_iseq_t *iseq, VALUE *pc, char* cfunc);
void Init_tailcall(void);

int frame_size(void) {
    tcl_frame_t *f = tcl_frame_head;
    if (f == NULL) { return 0; }

    int count = 0;
    while (true) {
        count++;
        if (f->next == NULL) { break; }
        f = f->next;
    }
    return count;
}

static char* calc_method_name(rb_iseq_t *iseq) {
    return StringValuePtr(iseq->body->location.label);
}

static void log_delete(int* positions, int positions_size, bool include_log) {
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

            bool is_normal = t->truncated_by == NULL;
            if (index == position) {
                /* printf("match\n"); */
                if (include_log || is_normal) {
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

static void log_truncate(int* positions, int positions_size, char* command, bool include_log) {
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

            bool is_normal = t->truncated_by == NULL;
            if (index == position) {
                /* printf("match\n"); */
                if (include_log || is_normal) {
                    // replace
                    char* truncated_by = malloc(1024);
                    strcpy(truncated_by, command);

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

static void log_uniq(void) {
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
        char *type_addr, bool *save_addr, bool *include_log_addr, int *indexes_size_addr,
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
    char buf[(TCL_MAX + RUBY_STACK_MAX) * METHOD_NAME_SIZE_MAX];
    memset(&buf, 0, sizeof(buf));
    recv(sock, buf, sizeof(buf), 0);
    /* printf("buf: `%s`\n", buf); */

    // パース 1行目
    char save;
    char include_log;
    sscanf(buf, "%c %c %c %d", type_addr, &save, &include_log, indexes_size_addr);
    int indexes_size = *indexes_size_addr;
    *save_addr = save == 's';
    *include_log_addr = include_log == '_';

    // パース 2行目以降
    char* pos;
    char buf2[(TCL_MAX + RUBY_STACK_MAX) * METHOD_NAME_SIZE_MAX]; // 同一のchar*でstrcpyすると謎にバグるから、一旦buf2にstrcpyしてからbufに戻している
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
                            "(... %d tailcalls truncated by `%s'...)"
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
                            "(`%s' * %d)"
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
            continue;
        } else if (strcmp(command, "c\n") == 0) {
            if (TCL_MAX <= tailcalls_size_sum) {
                printf("still over the limit. please enter pattern expression.\n");
                continue;
            } else {
                printf("continue program.\n\n");
                return;
            }
        } else if (strcmp(command, "n\n") == 0) {
            if (TCL_MAX <= tailcalls_size_sum) {
                printf("still over the limit. please enter pattern expression.\n");
                continue;
            } else {
                interactive = true;
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
        } else if (strcmp(command, "help\n") == 0 || strcmp(command, "h\n") == 0) {
            printf(
                    "\n"
                    "[usage]\n"
                    "  help     : show this help\n"
                    "  c        : continue program\n"
                    "  b        : show current backtrace\n"
                    "  bt       : show full current backtrace\n"
                    "  ls       : show saved commands\n"
                    "  rm [1-9] : remove saved commands of the specified number\n"
                    "  (pattern expression) : (show below)\n"
                    "\n"
                    "[pattern expression]\n"
                    "  /(pattern)/(`d`,`k`,`t`)(`1` or empty)(`_` or empty)\n"
                    "\n"
                    "  descard (`d`) or keep (`k`) or truncate (`t`) logs matching the (pattern).\n"
                    "  if `1` is  specified, apply the pattern only once (otherwise, applied each time the upper limit is reached).\n"
                    "  if `_` is specified, matching logs created by `t` (displayed as `(`/foo/t' * 3)`) will also be deleted/keeped/truncated.\n"
                    "  range selection can be done by writing patterns side by side.\n"
                    );
            printf("\n");
            continue;
        } else if (strcmp(command, "q\n") == 0) {
            printf("quit\n");
            exit(EXIT_SUCCESS);
        }

        command[strlen(command) - 1] = '\0';

        char argument[(TCL_MAX + RUBY_STACK_MAX) * METHOD_NAME_SIZE_MAX] = "";
        make_arguments(argument);
        strcat(argument, "\n");
        strcat(argument, command);
        /* printf("argument: `%s`\n", argument); */

        char type;
        bool save;
        bool include_log;
        int positions_size;
        int positions[TCL_MAX + RUBY_STACK_MAX];
        memset(&positions, 0, sizeof(positions));
        connect_patern_lang_server(argument, &type, &save, &include_log, &positions_size, positions);

        long prev_tailcalls_size_sum = tailcalls_size_sum;
        switch(type) {
            case 'd': // 消すものを受け取る
            case 'k':
                log_delete(positions, positions_size, include_log);
                break;
            case 't':
                log_truncate(positions, positions_size, command, include_log);
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

        if (save) {
            char *save_command = malloc(1024);
            strcpy(save_command, command);
            saved_commands[saved_commands_size] = save_command;
            saved_commands_size++;
            printf("command `"ESCAPE_SEQUENCES_YELLOW"%s"ESCAPE_SEQUENCES_RESET"' saved.\n", save_command);
            printf("saved commands:\n");
            print_saved_commands();
            printf("\n");
        }

        if (TCL_MAX <= tailcalls_size_sum) {
            printf("still over the limit. please enter pattern expression.\n");
        } else {
            printf("below the limit. enter `c' to continue program.\n");
        }
    }
    printf("\n");
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

void apply_saved(void) {
    char *command;

    for (int i = 0; i < saved_commands_size; i++) {
        command = saved_commands[i];

        char argument[(TCL_MAX + RUBY_STACK_MAX) * METHOD_NAME_SIZE_MAX] = "";
        make_arguments(argument);
        strcat(argument, "\n");
        strcat(argument, command);
        /* printf("argument: `%s`\n", argument); */

        char type;
        bool save;
        bool include_log;
        int positions_size;
        int positions[TCL_MAX + RUBY_STACK_MAX];
        memset(&positions, 0, sizeof(positions));
        connect_patern_lang_server(argument, &type, &save, &include_log, &positions_size, positions);

        switch(type) {
            case 'd': // 消すものを受け取る
            case 'k':
                log_delete(positions, positions_size, include_log);
                break;
            case 't':
                log_truncate(positions, positions_size, command, include_log);
                break;
            default:
                printf("bug in apply_saved\n");
                exit(EXIT_FAILURE);
        }
        log_uniq();
    }
}

void tcl_stack_push(rb_iseq_t *iseq, VALUE *pc, char *cfunc) {
    if (frame_size() > FRAME_MAX) {
        if (saved_commands_size > 0) {
            apply_saved();
        }
        printf("current backtrace (full):\n");
        print_log_full();
        printf("\n");
        printf("stack over flow occurred.\n");
        printf("\n");
        exit(EXIT_FAILURE);
    }

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

    if (interactive) {
        interactive = false;
        printf("current backtrace:\n");
        print_log_oneline();
        printf("stack is pushed by normal call.\n");
        printf("\n");
        prompt();
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

    if (interactive) {
        interactive = false;
        printf("current backtrace:\n");
        print_log_oneline();
        printf("stack is poped by return.\n");
        printf("\n");
        prompt();
    }
}

void tcl_stack_record(rb_iseq_t *iseq, VALUE *pc) {
    if (!enable_tailcall_log) { return; }

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

    if (TCL_MAX <= tailcalls_size_sum && saved_commands_size > 0) {
        apply_saved();
    }
    if (TCL_MAX <= tailcalls_size_sum) {
        printf("log size limit reached. please enter pattern expression what logs to discard.\n\n");
        printf("current backtrace:\n");
        print_log_oneline();
        printf("\n");
        prompt();
    } else if (interactive) {
        interactive = false;
        printf("current backtrace:\n");
        print_log_oneline();
        printf("stack top is updated by tailcall\n");
        printf("\n");
        prompt();
    }
}

void tcl_stack_change_top(rb_iseq_t *iseq, VALUE *pc, char* cfunc) {
    tcl_frame_tail->iseq = iseq;
    tcl_frame_tail->pc = pc;
    tcl_frame_tail->cfunc = cfunc;
}

void Init_tailcall(void) {
    char* e = getenv("TCL");
    if (e == NULL) {
        enable_tailcall_log = true;
    } else if (strcmp(e, "0") == 0 || strcmp(e, "false") == 0) {
        enable_tailcall_log = false;
    } else if (strcmp(e, "1") == 0 || strcmp(e, "true") == 0) {
        enable_tailcall_log = true;
    } else {
        printf("the value of the environment variable `TCL' is invalid.");
        exit(EXIT_FAILURE);
    }
}
