#ifndef TAILCALL_H

typedef struct rb_iseq_struct rb_iseq_t;
typedef unsigned long VALUE;

typedef struct tcl_tailcall_struct {
    rb_iseq_t *iseq; // NULLなら... 扱い
    VALUE *pc; // NULLなら... 扱い
    char *truncated_by;
    int truncated_count;
    struct tcl_tailcall_struct *prev;
    struct tcl_tailcall_struct *next;
} tcl_tailcall_t;

typedef struct tcl_frame_struct {
    rb_iseq_t *iseq; // cfuncの場合もiseqやpcに値は入る
    VALUE *pc;
    char *cfunc; // NULLならruby method, 入っていればc func
    tcl_tailcall_t *tailcall_head;
    tcl_tailcall_t *tailcall_tail;
    int tailcalls_size;
    struct tcl_frame_struct *prev;
    struct tcl_frame_struct *next;
} tcl_frame_t;

extern tcl_frame_t *tcl_frame_tail;
extern long tailcalls_size_sum;
extern int saved_commands_size;

void apply_saved(void);
void tcl_stack_push(rb_iseq_t *iseq, VALUE *pc, char *cfunc);
void tcl_stack_pop(void);
void tcl_stack_record(rb_iseq_t *iseq, VALUE *pc);
void tcl_stack_change_top(rb_iseq_t *iseq, VALUE *pc, char* cfunc);
void Init_tailcall(void);

#define TAILCALL_H
#endif
