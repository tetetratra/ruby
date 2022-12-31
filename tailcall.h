#ifndef TAILCALL_H

typedef struct rb_iseq_struct rb_iseq_t;
typedef unsigned long VALUE;

typedef struct tcl_tailcall_method_struct {
    rb_iseq_t *iseq; // NULLなら... 扱い
    VALUE *pc; // NULLなら... 扱い
    char *truncated_by;
    int truncated_count;
    struct tcl_tailcall_method_struct *prev;
    struct tcl_tailcall_method_struct *next;
} tcl_tailcall_method_t;

typedef struct tcl_frame_struct {
    rb_iseq_t *iseq; // cfuncの場合もiseqやpcに値は入る
    VALUE *pc;
    char *cfunc; // NULLならruby method, 入っていればc func
    tcl_tailcall_method_t *tailcall_methods_head;
    tcl_tailcall_method_t *tailcall_methods_tail;
    int tailcall_methods_size;
    struct tcl_frame_struct *prev;
    struct tcl_frame_struct *next;
} tcl_frame_t;

tcl_frame_t* get_tcl_frame_tail(void);

long tcl_log_size(void);
int tcl_truncated_size(void);
void tcl_print(void);
void tcl_push(rb_iseq_t *iseq, VALUE *pc, char *cfunc);
void tcl_pop(void);
void tcl_record(rb_iseq_t *iseq, VALUE *pc);
void tcl_change_top(const rb_iseq_t *iseq, VALUE *pc, char* cfunc);

#define TAILCALL_H
#endif
