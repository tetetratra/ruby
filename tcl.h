// #include "method.h"

typedef struct tcl_tailcall_method_struct {
    rb_iseq_t *iseq;
    VALUE *pc;
    struct tcl_tailcall_method_struct *prev;
    struct tcl_tailcall_method_struct *next;
} tcl_tailcall_method_t;

typedef struct tcl_frame_struct {
    char *name;
    tcl_tailcall_method_t *tailcall_methods_head;
    tcl_tailcall_method_t *tailcall_methods_tail;
    int tailcall_methods_size;
    struct tcl_frame_struct *prev;
    struct tcl_frame_struct *next;
} tcl_frame_t;

