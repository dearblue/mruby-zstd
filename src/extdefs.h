#ifndef XMRUBY_EXTDEFS_H
#define XMRUBY_EXTDEFS_H 1

#define ELEMENTOF(V) (sizeof(V) / sizeof(V[0]))

#define MRB mrb_state *mrb
#define mrb_cObject mrb->object_class
#define VALUE mrb_value
#define Qnil mrb_nil_value()
#define Qfalse mrb_false_value()
#define Qtrue mrb_true_value()
#define NIL_P(o) mrb_nil_p(o)

#define FUNCALL(MRB, RECV, ID, ...) \
    ({ \
        mrb_value args[] = { __VA_ARGS__ }; \
        mrb_funcall_argv(MRB, RECV, ID, ELEMENTOF(args), args); \
    }) \

#define FUNCALLC(MRB, RECV, NAME, ...) \
    FUNCALL(MRB, RECV, mrb_intern_cstr(MRB, NAME), __VA_ARGS__)

#if MRUBY_RELEASE_MAJOR == 1 && MRUBY_RELEASE_MINOR == 2
#   define MRB_FROZEN_P(o) RSTR_FROZEN_P(o)
#endif

#endif /* XMRUBY_EXTDEFS_H */
