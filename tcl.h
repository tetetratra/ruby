typedef struct tcl_tailcall_method_struct {
    rb_iseq_t *iseq;
    VALUE *pc;
    struct tcl_tailcall_method_struct *prev;
    struct tcl_tailcall_method_struct *next;
} tcl_tailcall_method_t;

typedef enum tcl_filter_enum { // そのフレームのログの残し方
  TCL_FILTER_TYPE_KEEP_ALL, // ALL以外は定数個。ALLの場合も(C言語的な)上限を設けたほうがいいかも?
  TCL_FILTER_TYPE_KEEP_NONE,
  TCL_FILTER_TYPE_KEEP_BEGIN,
  TCL_FILTER_TYPE_KEEP_END,
  TCL_FILTER_TYPE_KEEP_BEGIN_AND_END
} tcl_filter_type;

typedef struct tcl_frame_struct {
    char *name;
    tcl_tailcall_method_t *tailcall_methods_head;
    tcl_tailcall_method_t *tailcall_methods_tail;
    int tailcall_methods_size;
    struct tcl_frame_struct *prev;
    struct tcl_frame_struct *next;

    tcl_filter_type filter_type;
    int keep_size;
    bool truncated;
} tcl_frame_t;

typedef struct tcl_filter_struct {
  char *name;
  tcl_filter_type filter_type;
  int keep_size;
} tcl_filter_t;
