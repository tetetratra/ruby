int calc_lineno(const rb_iseq_t *iseq, const VALUE *pc);

int calc_pos(const rb_iseq_t *iseq, const VALUE *pc, int *lineno, int *node_id);
