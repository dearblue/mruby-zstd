#include <mruby.h>
#include <mruby/class.h>
#include <mruby/hash.h>
#include <mruby/string.h>
#include <mruby/value.h>
#include <mruby/data.h>
#include <mruby/variable.h>
#include <mruby/error.h>
#include <stdlib.h>
#include <string.h>
#include "extdefs.h"
#include "mrbx_kwargs.h"

#define ZSTD_STATIC_LINKING_ONLY 1
#include <zstd.h>
#include <common/zstd_errors.h>

#ifndef MRUBY_ZSTD_DEFAULT_PARTIAL_SIZE
#   ifdef MRB_INT16
                                                 /* 4 KiB */
#       define MRUBY_ZSTD_DEFAULT_PARTIAL_SIZE   (4 << 10)
#   else
                                                 /* 1 MiB */
#       define MRUBY_ZSTD_DEFAULT_PARTIAL_SIZE   (1 << 20)
#   endif
#endif

#define AUX_MALLOC_MAX (MRB_INT_MAX - 1)

#define CLAMP_MAX(n, max) ((n) > (max) ? (max) : (n))

#define id_fast     (mrb_intern_lit(mrb, "fast"))
#define id_dfast    (mrb_intern_lit(mrb, "dfast"))
#define id_greedy   (mrb_intern_lit(mrb, "greedy"))
#define id_lazy     (mrb_intern_lit(mrb, "lazy"))
#define id_lazy2    (mrb_intern_lit(mrb, "lazy2"))
#define id_btlazy2  (mrb_intern_lit(mrb, "btlazy2"))
#define id_btopt    (mrb_intern_lit(mrb, "btopt"))
#define id_btultra  (mrb_intern_lit(mrb, "btultra"))

static ZSTD_strategy
aux_to_strategy(MRB, VALUE astrategy)
{
    mrb_check_type(mrb, astrategy, MRB_TT_SYMBOL);
    mrb_sym strategy = mrb_symbol(astrategy);
    if (strategy == id_fast) {
        return ZSTD_fast;
    } else if (strategy == id_dfast) {
        return ZSTD_dfast;
    } else if (strategy == id_greedy) {
        return ZSTD_greedy;
    } else if (strategy == id_lazy) {
        return ZSTD_lazy;
    } else if (strategy == id_lazy2) {
        return ZSTD_lazy2;
    } else if (strategy == id_btlazy2) {
        return ZSTD_btlazy2;
    } else if (strategy == id_btopt) {
        return ZSTD_btopt;
    } else if (strategy == id_btultra) {
        return ZSTD_btultra;
    } else {
        mrb_raisef(mrb, E_ARGUMENT_ERROR,
                "wrong strategy (given %S, expect fast, dfast, greedy, lazy, lazy2, btlazy2, btopt or btultra)",
                astrategy);
    }
}

/* stdio.h */
int sprintf(char *str, const char *format, ...);

static void
aux_zstd_error(MRB, size_t status, const char *mesg)
{
    VALUE errcode = mrb_str_buf_new(mrb, 32);
    sprintf(RSTRING_PTR(errcode), "%d", (int)ZSTD_getErrorCode(status));
    RSTR_SET_LEN(RSTRING(errcode), strlen(RSTRING_PTR(errcode)));

    if (mesg) {
        mrb_raisef(mrb, E_RUNTIME_ERROR,
                   "%S failed - %S (code:%S)",
                   mrb_str_new_cstr(mrb, mesg),
                   mrb_str_new_cstr(mrb, ZSTD_getErrorName(status)),
                   errcode);
    } else {
        mrb_raisef(mrb, E_RUNTIME_ERROR,
                   "zstd error - %S (code:%S)",
                   mrb_str_new_cstr(mrb, ZSTD_getErrorName(status)),
                   errcode);
    }
}

static void
aux_check_error(MRB, size_t status, const char *mesg)
{
    if (!ZSTD_isError(status)) { return; }
    aux_zstd_error(mrb, status, mesg);
}

static ZSTD_customMem
aux_zstd_allocator(MRB)
{
    const ZSTD_customMem a = {
        .customAlloc = (void *(*)(void *, unsigned long))mrb_malloc_simple,
        .customFree = (void (*)(void *, void *))mrb_free,
        .opaque = mrb,
    };

    return a;
}


/*
 * class Zstd::Encoder
 */

static void
encode_kwargs(MRB, VALUE opts, VALUE src, ZSTD_parameters *params, mrb_int *pledgedsize, VALUE *dict)
{
    if (NIL_P(opts)) {
        if (NIL_P(src)) {
            *pledgedsize = 0;
        } else {
            *pledgedsize = RSTRING_LEN(src);
        }

        *params = ZSTD_getParams(0, *pledgedsize, 0);
        *dict = Qnil;
    } else {
        uint64_t estimatedsize;
        VALUE level, nocontentsize, nochecksum, nodictid, anestimatedsize, apledgedsize,
              windowlog, chainlog, hashlog, searchlog, searchlength, targetlength, strategy;
        struct mrbx_scanhash_arg args[] = {
            MRBX_SCANHASH_ARGS("level",         &level,             Qnil),
            MRBX_SCANHASH_ARGS("dict",          dict,               Qnil),
            MRBX_SCANHASH_ARGS("windowlog",     &windowlog,         Qnil),
            MRBX_SCANHASH_ARGS("chainlog",      &chainlog,          Qnil),
            MRBX_SCANHASH_ARGS("hashlog",       &hashlog,           Qnil),
            MRBX_SCANHASH_ARGS("searchlog",     &searchlog,         Qnil),
            MRBX_SCANHASH_ARGS("searchlength",  &searchlength,      Qnil),
            MRBX_SCANHASH_ARGS("targetlength",  &targetlength,      Qnil),
            MRBX_SCANHASH_ARGS("strategy",      &strategy,          Qnil),
            MRBX_SCANHASH_ARGS("nocontentsize", &nocontentsize,     Qnil),
            MRBX_SCANHASH_ARGS("nochecksum",    &nochecksum,        Qnil),
            MRBX_SCANHASH_ARGS("nodictid",      &nodictid,          Qnil),
            MRBX_SCANHASH_ARGS("estimatedsize", &anestimatedsize,   Qnil),
            MRBX_SCANHASH_ARGS("pledgedsize",   &apledgedsize,      Qnil),
        };

        if (NIL_P(src)) {
            mrbx_scanhash(mrb, opts, Qnil, args, MRBX_SCANHASH_ENDOF(args));
            *pledgedsize = (NIL_P(apledgedsize) ? 0 : mrb_int(mrb, apledgedsize));
            estimatedsize = (NIL_P(anestimatedsize) ? 0 : mrb_int(mrb, anestimatedsize));
            if (*pledgedsize != 0 && estimatedsize > *pledgedsize) {
                estimatedsize = *pledgedsize;
            }
        } else {
            /* NOTE: ~~~_ENDOF(args) - 2 によって estimatedsize と pledgedsize をないものと扱う */
            mrbx_scanhash(mrb, opts, Qnil, args, MRBX_SCANHASH_ENDOF(args) - 2);

            *pledgedsize = estimatedsize = RSTRING_LEN(src);
        }

        if (!NIL_P(*dict)) { mrb_check_type(mrb, *dict, MRB_TT_STRING); }

        *params = ZSTD_getParams(
                (NIL_P(level) ? 0 : mrb_int(mrb, level)),
                estimatedsize,
                (NIL_P(*dict) ? 0 : RSTRING_LEN(*dict)));

        if (!NIL_P(windowlog)) { params->cParams.windowLog = mrb_int(mrb, windowlog); }
        if (!NIL_P(chainlog)) { params->cParams.chainLog = mrb_int(mrb, chainlog); }
        if (!NIL_P(hashlog)) { params->cParams.hashLog = mrb_int(mrb, hashlog); }
        if (!NIL_P(searchlog)) { params->cParams.searchLog = mrb_int(mrb, searchlog); }
        if (!NIL_P(searchlength)) { params->cParams.searchLength = mrb_int(mrb, searchlength); }
        if (!NIL_P(targetlength)) { params->cParams.targetLength = mrb_int(mrb, targetlength); }
        if (!NIL_P(strategy)) { params->cParams.strategy = aux_to_strategy(mrb, strategy); }

        if (!NIL_P(nocontentsize)) { params->fParams.contentSizeFlag = (mrb_bool(nocontentsize) ? 1 : 0); }
        if (!NIL_P(nochecksum)) { params->fParams.checksumFlag = (mrb_bool(nochecksum) ? 1 : 0); }
        if (!NIL_P(nodictid)) { params->fParams.noDictIDFlag = (mrb_bool(nodictid) ? 0 : 1); }
    }
}

static void
enc_s_encode_args(MRB, VALUE *src, VALUE *dest, mrb_int *maxdest, ZSTD_parameters *params, mrb_int *pledgedsize, VALUE *dict)
{
    VALUE *argv;
    mrb_int argc;
    mrb_get_args(mrb, "*", &argv, &argc);
    VALUE opts;

    if (argc > 0 && mrb_hash_p(argv[argc - 1])) {
        opts = argv[argc - 1];
        argc --;
    } else {
        opts = Qnil;
    }

    switch (argc) {
    case 1:
        *src = argv[0];
        *maxdest = -1;
        *dest = Qnil;
        break;
    case 2:
        *src = argv[0];
        if (mrb_string_p(argv[1])) {
            *maxdest = -1;
            *dest = argv[1];
        } else {
            *maxdest = (NIL_P(argv[1]) ? -1 : mrb_int(mrb, argv[1]));
            *dest = Qnil;
        }
        break;
    case 3:
        *src = argv[0];
        *maxdest = (NIL_P(argv[1]) ? -1 : mrb_int(mrb, argv[1]));
        *dest = argv[2];
        break;
    default:
        mrb_raisef(mrb,
                   E_ARGUMENT_ERROR,
                   "wrong number of arguments (given %S, expect 1..3 with optional keywords)",
                   mrb_fixnum_value(argc));
        break;
    }

    mrb_check_type(mrb, *src, MRB_TT_STRING);

    if (*maxdest < 0) {
        *maxdest = ZSTD_compressBound(RSTRING_LEN(*src));
    }

    if (NIL_P(*dest)) {
        *dest = mrb_str_buf_new(mrb, *maxdest);
    } else {
        mrb_check_type(mrb, *dest, MRB_TT_STRING);
        mrb_str_resize(mrb, *dest, *maxdest);
    }

    RSTR_SET_LEN(RSTRING(*dest), 0);

    encode_kwargs(mrb, opts, *src, params, pledgedsize, dict);
}

static VALUE
enc_s_encode_main_body(MRB, VALUE args)
{
    struct args
    {
        ZSTD_CStream *zstd;
        VALUE src, dest;
        mrb_int maxdest;
        ZSTD_parameters *params;
        mrb_int pledgedsize;
        VALUE dict;
    } *p = (struct args *)mrb_cptr(args);

    size_t s = ZSTD_initCStream_advanced(p->zstd,
            (NIL_P(p->dict) ? NULL : RSTRING_PTR(p->dict)),
            (NIL_P(p->dict) ? 0 : RSTRING_LEN(p->dict)),
            *p->params, p->pledgedsize);
    aux_check_error(mrb, s, "ZSTD_initCStream_advanced");

    ZSTD_inBuffer input = {
        .src = RSTRING_PTR(p->src),
        .size = RSTRING_LEN(p->src),
        .pos = 0,
    };
    ZSTD_outBuffer output = {
        .dst = RSTRING_PTR(p->dest),
        .size = (p->maxdest < 0 ? RSTRING_CAPA(p->dest) : p->maxdest),
        .pos = 0,
    };

    for (;;) {
        size_t s = ZSTD_compressStream(p->zstd, &output, &input);
        if (input.pos >= input.size) { break; }
        aux_check_error(mrb, s, "ZSTD_compressStream");
        if (p->maxdest >= 0) {
            aux_zstd_error(mrb,
                    ZSTD_error_dstSize_tooSmall,
                    "ZSTD_compressStream");
        }

        /* expand dest */
        s = RSTRING_CAPA(p->dest);
        if (s >= AUX_MALLOC_MAX) {
            aux_zstd_error(mrb,
                    ZSTD_error_dstSize_tooSmall,
                    "ZSTD_compressStream");
        }
        s += MRUBY_ZSTD_DEFAULT_PARTIAL_SIZE;
        s = CLAMP_MAX(s, AUX_MALLOC_MAX);
        mrb_str_resize(mrb, p->dest, s);
        output.dst = RSTRING_PTR(p->dest);
        output.size = RSTRING_CAPA(p->dest);
    }

    for (;;) {
        size_t s = ZSTD_endStream(p->zstd, &output); /* 's' is Status */
        if (s == 0) { break; }
        aux_check_error(mrb, s, "ZSTD_endStream");
        if (p->maxdest >= 0) {
            aux_zstd_error(mrb,
                    ZSTD_error_dstSize_tooSmall,
                    "ZSTD_endStream");
        }

        /* expand dest */
        s = RSTRING_CAPA(p->dest); /* 's' is Size */
        if (s >= AUX_MALLOC_MAX) {
            aux_zstd_error(mrb,
                    ZSTD_error_dstSize_tooSmall,
                    "ZSTD_endStream");
        }
        s += MRUBY_ZSTD_DEFAULT_PARTIAL_SIZE;
        s = CLAMP_MAX(s, AUX_MALLOC_MAX);
        mrb_str_resize(mrb, p->dest, s);
        output.dst = RSTRING_PTR(p->dest);
        output.size = RSTRING_CAPA(p->dest);
    }

    RSTR_SET_LEN(RSTRING(p->dest), output.pos);

    return Qnil;
}

static VALUE
enc_s_encode_cleanup(MRB, VALUE args)
{
    struct args
    {
        ZSTD_CStream *zstd;
        VALUE src, dest;
        mrb_int maxdest;
        ZSTD_parameters *params;
        mrb_int pledgedsize;
        VALUE dict;
    } *p = (struct args *)mrb_cptr(args);

    ZSTD_freeCStream(p->zstd);

    return Qnil;
}

static void
enc_s_encode_main(MRB, VALUE src, VALUE dest, mrb_int maxdest, ZSTD_parameters *params, mrb_int pledgedsize, VALUE dict)
{
    ZSTD_customMem allocator = aux_zstd_allocator(mrb);
    ZSTD_CStream *zstd = ZSTD_createCStream_advanced(allocator);
    if (!zstd) { mrb_raise(mrb, E_RUNTIME_ERROR, "ZSTD_initCStream_advanced failed"); }

    struct args
    {
        ZSTD_CStream *zstd;
        VALUE src, dest;
        mrb_int maxdest;
        ZSTD_parameters *params;
        mrb_int pledgedsize;
        VALUE dict;
    } p = { zstd, src, dest, maxdest, params, pledgedsize, dict };

    VALUE argsp = mrb_cptr_value(mrb, &p);
    mrb_ensure(mrb, enc_s_encode_main_body, argsp, enc_s_encode_cleanup, argsp);
}

/*
 * call-seq:
 *  encode(source, buffer = "", opts = {}) -> buffer for zstd'd string
 *  encode(source, maxsize, buffer = "", opts = {}) -> buffer for zstd'd string
 *
 * [source (string)]
 *  Input data.
 *
 * [buffer (string OR nil)]
 *  Output buffer. Must give a string object.
 *
 * [maxsize (positive integer OR nil)]
 *  Maximum output buffer size.
 *
 *  Raise exception, if encoded data is over this size.
 *
 * [opts (hash)]
 *  level:: zstd compression level (1 .. 22)
 */
static VALUE
enc_s_encode(MRB, VALUE self)
{
    ZSTD_parameters params;
    mrb_int pledgedsize;
    VALUE src, dest, dict;
    mrb_int maxdest;
    enc_s_encode_args(mrb, &src, &dest, &maxdest, &params, &pledgedsize, &dict);

    enc_s_encode_main(mrb, src, dest, maxdest, &params, pledgedsize, dict);

    return dest;
}

struct encoder
{
    struct {
        ZSTD_CStream *context;
        ZSTD_customMem allocator;
        ZSTD_inBuffer bufin;
    } zstd;

    VALUE io;
    VALUE outbuf;
    size_t outbufsize;
};

static void
encoder_free(MRB, struct encoder *p)
{
    if (p->zstd.context) {
        ZSTD_freeCStream(p->zstd.context);
    }

    mrb_free(mrb, p);
}

static const mrb_data_type encoder_type = {
    .struct_name = "mruby_zstd.encoder",
    .dfree = (void (*)(mrb_state *, void *))encoder_free,
};

static struct encoder *
getencoder(MRB, VALUE self)
{
    struct encoder *p;
    Data_Get_Struct(mrb, self, &encoder_type, p);
    return p;
}

static VALUE
encoder_set_outport(MRB, VALUE self, struct encoder *p, VALUE port)
{
    mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "mruby-zstd.outport"), port);
    p->io = port;
    return port;
}

static VALUE
encoder_set_outbuf(MRB, VALUE obj, struct encoder *p, VALUE val)
{
    p->outbuf = val;
    mrb_iv_set(mrb, obj, mrb_intern_lit(mrb, "mruby-zstd.outbuf"), val);
    return val;
}

/*
 * call-seq:
 *  new(level = nil, prefs = {})
 */
static VALUE
enc_s_new(MRB, VALUE self)
{
    struct RClass *klass = mrb_class_ptr(self);
    struct RData *rd;
    struct encoder *p;
    Data_Make_Struct(mrb, klass, struct encoder, &encoder_type, p, rd);
    p->io = Qnil;
    p->outbufsize = ZSTD_CStreamOutSize();
    if (p->outbufsize > AUX_MALLOC_MAX) { p->outbufsize = AUX_MALLOC_MAX; }
    p->zstd.allocator = aux_zstd_allocator(mrb);
    p->zstd.context = ZSTD_createCStream_advanced(p->zstd.allocator);

    if (!p->zstd.context) {
        mrb_raise(mrb,
                  E_RUNTIME_ERROR,
                  "ZSTD_createCStream_advanced failed");
    }

    VALUE obj = mrb_obj_value(rd);
    mrb_int argc;
    mrb_value *argv;
    mrb_get_args(mrb, "*", &argv, &argc);
    mrb_funcall_argv(mrb, obj, mrb_intern_lit(mrb, "initialize"), argc, argv);

    return obj;
}

static void
enc_initialize_args(MRB, VALUE *outport, ZSTD_parameters *params, mrb_int *pledgedsize, VALUE *dict)
{
    mrb_int argc;
    VALUE *argv;
    mrb_get_args(mrb, "*", &argv, &argc);
    VALUE opts;

    if (argc > 0 && mrb_hash_p(argv[argc - 1])) {
        opts = argv[argc - 1];
        argc --;
    } else {
        opts = Qnil;
    }

    switch (argc) {
    case 1:
        break;
    default:
        mrb_raisef(mrb,
                   E_ARGUMENT_ERROR,
                   "wrong arguments (given %S, expect ``initialize(outport, opts = {})'')",
                   mrb_fixnum_value(argc));
    }

    *outport = argv[0];

    encode_kwargs(mrb, opts, Qnil, params, pledgedsize, dict);
}

/*
 * call-seq:
 *  initialize(outport, level = nil, opts = {})
 */
static VALUE
enc_initialize(MRB, VALUE self)
{
    ZSTD_parameters params;
    mrb_int pledgedsize;
    VALUE dict;
    VALUE port;
    enc_initialize_args(mrb, &port, &params, &pledgedsize, &dict);
    struct encoder *p = getencoder(mrb, self);

    size_t s = ZSTD_initCStream_advanced(p->zstd.context,
                                         (NIL_P(dict) ? NULL : RSTRING_PTR(dict)),
                                         (NIL_P(dict) ? 0 : RSTRING_LEN(dict)),
                                         params, pledgedsize);
    aux_check_error(mrb, s, "ZSTD_initCStream_advanced");

    encoder_set_outport(mrb, self, p, port);

    return self;
}

/*
 * call-seq:
 *  write(str) -> self
 */
static VALUE
enc_write(MRB, VALUE self)
{
    const char *inbuf;
    mrb_int insize;
    mrb_get_args(mrb, "s", &inbuf, &insize);
    struct encoder *p = getencoder(mrb, self);
    ZSTD_inBuffer input = { .src = inbuf, .size = insize, .pos = 0 };

    while (input.pos < input.size) {
        mrb_gc_arena_restore(mrb, 0);

        if (NIL_P(p->outbuf) || MRB_FROZEN_P(RSTRING(p->outbuf))) {
            encoder_set_outbuf(mrb, self, p, mrb_str_buf_new(mrb, p->outbufsize));
        } else {
            mrb_str_modify(mrb, RSTRING(p->outbuf));
        }
        mrb_str_resize(mrb, p->outbuf, p->outbufsize);
        ZSTD_outBuffer output = { .dst = RSTRING_PTR(p->outbuf), .size = RSTRING_CAPA(p->outbuf), .pos = 0 };
        size_t s = ZSTD_compressStream(p->zstd.context, &output, &input);
        aux_check_error(mrb, s, "ZSTD_compressStream");
        RSTR_SET_LEN(RSTRING(p->outbuf), output.pos);
        FUNCALLC(mrb, p->io, "<<", p->outbuf);
    }

    return self;
}

/*
 * call-seq:
 *  flush -> self
 */
static VALUE
enc_flush(MRB, VALUE self)
{
    struct encoder *p = getencoder(mrb, self);
    ZSTD_outBuffer output = { 0 };

    do {
        mrb_gc_arena_restore(mrb, 0);

        if (NIL_P(p->outbuf) || MRB_FROZEN_P(RSTRING(p->outbuf))) {
            encoder_set_outbuf(mrb, self, p, mrb_str_buf_new(mrb, p->outbufsize));
        } else {
            mrb_str_modify(mrb, RSTRING(p->outbuf));
        }
        mrb_str_resize(mrb, p->outbuf, p->outbufsize);
        output.dst = RSTRING_PTR(p->outbuf);
        output.size = RSTRING_CAPA(p->outbuf);
        output.pos = 0;
        size_t s = ZSTD_flushStream(p->zstd.context, &output);
        aux_check_error(mrb, s, "ZSTD_flushStream");
        RSTR_SET_LEN(RSTRING(p->outbuf), output.pos);
        FUNCALLC(mrb, p->io, "<<", p->outbuf);
    } while (output.pos == output.size);

    return self;
}

/*
 * call-seq:
 *  close -> nil
 */
static VALUE
enc_close(MRB, VALUE self)
{
    struct encoder *p = getencoder(mrb, self);
    ZSTD_outBuffer output = { 0 };

    do {
        mrb_gc_arena_restore(mrb, 0);

        if (NIL_P(p->outbuf) || MRB_FROZEN_P(RSTRING(p->outbuf))) {
            encoder_set_outbuf(mrb, self, p, mrb_str_buf_new(mrb, p->outbufsize));
        } else {
            mrb_str_modify(mrb, RSTRING(p->outbuf));
        }
        mrb_str_resize(mrb, p->outbuf, p->outbufsize);
        output.dst = RSTRING_PTR(p->outbuf);
        output.size = RSTRING_CAPA(p->outbuf);
        output.pos = 0;
        size_t s = ZSTD_endStream(p->zstd.context, &output);
        aux_check_error(mrb, s, "ZSTD_endStream");
        RSTR_SET_LEN(RSTRING(p->outbuf), output.pos);
        FUNCALLC(mrb, p->io, "<<", p->outbuf);
    } while (output.pos == output.size);

    return Qnil;
}

/*
 * call-seq:
 *  get_port -> self
 */
static VALUE
enc_get_port(MRB, VALUE self)
{
    return getencoder(mrb, self)->io;
}

static void
init_encoder(MRB, struct RClass *mZstd)
{
    struct RClass *cEncoder = mrb_define_class_under(mrb, mZstd, "Encoder", mrb_cObject);
    mrb_define_class_method(mrb, cEncoder, "encode", enc_s_encode, MRB_ARGS_ANY());
    mrb_define_class_method(mrb, cEncoder, "new", enc_s_new, MRB_ARGS_ANY());
    mrb_define_method(mrb, cEncoder, "initialize", enc_initialize, MRB_ARGS_ANY());
    mrb_define_method(mrb, cEncoder, "write", enc_write, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, cEncoder, "flush", enc_flush, MRB_ARGS_NONE());
    mrb_define_method(mrb, cEncoder, "close", enc_close, MRB_ARGS_NONE());
    mrb_define_method(mrb, cEncoder, "get_port", enc_get_port, MRB_ARGS_NONE());

    mrb_define_alias(mrb, cEncoder, "<<", "write");
    mrb_define_alias(mrb, cEncoder, "finish", "close");

    mrb_define_const(mrb, cEncoder, "LEVEL_MIN", mrb_fixnum_value(1));
#ifdef ZSTD_CLEVEL_DEFAULT
    mrb_define_const(mrb, cEncoder, "LEVEL_DEFAULT", mrb_fixnum_value(ZSTD_CLEVEL_DEFAULT));
#else
    mrb_define_const(mrb, cEncoder, "LEVEL_DEFAULT", mrb_fixnum_value(3));
#endif
    mrb_define_const(mrb, cEncoder, "LEVEL_MAX", mrb_fixnum_value(ZSTD_maxCLevel()));
}

/*
 * class Zstd::Decoder
 */

static void
dec_s_decode_args(MRB, VALUE *src, VALUE *dest, mrb_int *maxsize, VALUE *dict)
{
    VALUE *argv;
    mrb_int argc;
    mrb_get_args(mrb, "S*", src, &argv, &argc);

    if (argc > 0 && mrb_hash_p(argv[argc - 1])) {
        MRBX_SCANHASH(mrb, argv[argc - 1], Qnil,
                MRBX_SCANHASH_ARGS("dict", dict, Qnil));
        if (!NIL_P(*dict)) { mrb_check_type(mrb, *dict, MRB_TT_STRING); }
        argc --;
    } else {
        *dict = Qnil;
    }

    switch (argc) {
    case 0:
        *maxsize = -1;
        *dest = Qnil;
        break;
    case 1:
        if (mrb_string_p(argv[0])) {
            *maxsize = -1;
            *dest = argv[0];
        } else {
            *maxsize = mrb_int(mrb, argv[0]);
            *dest = mrb_str_buf_new(mrb, *maxsize);
        }
        break;
    case 2:
        *maxsize = mrb_int(mrb, argv[0]);
        *dest = argv[1];
        break;
    default:
        mrb_raisef(mrb,
                   E_ARGUMENT_ERROR,
                   "wrong number of arguments (given %S, expect 1..3)",
                   mrb_fixnum_value(argc + 1));
        break;
    }

    size_t allocsize;
    if (*maxsize < 0) {
        allocsize = MRUBY_ZSTD_DEFAULT_PARTIAL_SIZE;
    } else {
        allocsize = *maxsize;
    }

    if (NIL_P(*dest)) {
        *dest = mrb_str_buf_new(mrb, allocsize);
    } else {
        mrb_check_type(mrb, *dest, MRB_TT_STRING);
        mrb_str_resize(mrb, *dest, allocsize);
    }
}

static VALUE
decode_main_body(MRB, VALUE args)
{
    struct args {
        ZSTD_DStream *zstd;
        VALUE src, dest;
        mrb_int maxsize;
        mrb_int pos;
    } *p = (struct args *)mrb_cptr(args);

    ZSTD_inBuffer bufin = { .src = RSTRING_PTR(p->src), .size = RSTRING_LEN(p->src), .pos = 0, };
    ZSTD_outBuffer bufout = { .dst = RSTRING_PTR(p->dest), .size = (p->maxsize < 0 ? RSTRING_CAPA(p->dest) : p->maxsize), .pos = 0, };

    for (;;) {
        size_t s = ZSTD_decompressStream(p->zstd, &bufout, &bufin);
        p->pos = bufout.pos;
        aux_check_error(mrb, s, "ZSTD_decompressStream");

        if (s == 0) { break; }
        if (p->maxsize >= 0) { break; }
        if (bufout.pos >= AUX_MALLOC_MAX) { break; }

        /* dest を拡張する */

        s = RSTRING_CAPA(p->dest);
        if (s >= AUX_MALLOC_MAX) { aux_zstd_error(mrb, ZSTD_error_dstSize_tooSmall, "ZSTD_decompressStream"); }
        s += MRUBY_ZSTD_DEFAULT_PARTIAL_SIZE;
        s = CLAMP_MAX(s, AUX_MALLOC_MAX);
        mrb_str_resize(mrb, p->dest, s);
        bufout.dst = RSTRING_PTR(p->dest);
        bufout.size = RSTRING_CAPA(p->dest);
    }

    return Qnil;
}

static VALUE
decode_main_ensure(MRB, VALUE args)
{
    struct args {
        ZSTD_DStream *zstd;
        VALUE src, dest;
        mrb_int maxsize;
        mrb_int pos;
    } *p = (struct args *)mrb_cptr(args);

    RSTR_SET_LEN(RSTRING(p->dest), p->pos);
    ZSTD_freeDStream(p->zstd);

    return Qnil;
}

static void
decode_main(MRB, ZSTD_DStream *zstd, VALUE src, VALUE dest, mrb_int maxsize)
{
    struct args {
        ZSTD_DStream *zstd;
        VALUE src, dest;
        mrb_int maxsize;
        mrb_int pos;
    } args = { zstd, src, dest, maxsize, 0 };

    VALUE argsp = mrb_cptr_value(mrb, &args);
    mrb_ensure(mrb, decode_main_body, argsp, decode_main_ensure, argsp);
}

/*
 * call-seq:
 *  decode(zstd_sequence, buffer = "", opts = {}) -> buffer
 *  decode(zstd_sequence, maxsize, buffer = "", opts = {}) -> buffer
 *
 * [opts (hash)]
 *  dict (nil OR string):: decompression with dictionary
 */
static VALUE
dec_s_decode(MRB, VALUE self)
{
    VALUE src, dest, dict;
    mrb_int maxsize;
    dec_s_decode_args(mrb, &src, &dest, &maxsize, &dict);

    ZSTD_customMem allocator = aux_zstd_allocator(mrb);
    ZSTD_DStream *zstd = ZSTD_createDStream_advanced(allocator);

    if (NIL_P(dict)) {
        size_t s = ZSTD_initDStream(zstd);
        aux_check_error(mrb, s, "ZSTD_initDStream");
    } else {
        size_t s = ZSTD_initDStream_usingDict(zstd, RSTRING_PTR(dict), RSTRING_LEN(dict));
        aux_check_error(mrb, s, "ZSTD_initDStream_usingDict");
    }

    decode_main(mrb, zstd, src, dest, maxsize);

    return dest;
}

struct decoder
{
    struct {
        ZSTD_DStream *context;
        ZSTD_customMem allocator;
        ZSTD_inBuffer bufin;
    } zstd;

    VALUE io;
    VALUE dict;
    VALUE inbuf;
};

static void
decoder_free(MRB, struct decoder *p)
{
    if (p->zstd.context) {
        ZSTD_freeDStream(p->zstd.context);
    }

    mrb_free(mrb, p);
}

static const mrb_data_type decoder_type = {
    .struct_name = "mruby_zstd.decoder",
    .dfree = (void (*)(mrb_state *, void *))decoder_free,
};

static struct decoder *
getdecoder(MRB, VALUE self)
{
    struct decoder *p;
    Data_Get_Struct(mrb, self, &decoder_type, p);
    return p;
}

static VALUE
decoder_set_inport(MRB, VALUE self, struct decoder *p, VALUE port)
{
    mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "mruby-zstd.inport"), port);
    p->io = port;
    return port;
}

static VALUE
decoder_set_dict(MRB, VALUE self, struct decoder *p, VALUE dict)
{
    mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "mruby-zstd.dictionary"), dict);
    p->dict = dict;
    return dict;
}

static VALUE
decoder_set_inbuf(MRB, VALUE obj, struct decoder *p, VALUE buf)
{
    mrb_iv_set(mrb, obj, mrb_intern_lit(mrb, "mruby-zstd.outbuf"), buf);
    p->inbuf = buf;
    return buf;
}

static VALUE
dec_s_new(MRB, VALUE self)
{
    struct RClass *klass = mrb_class_ptr(self);
    struct RData *rd;
    struct decoder *p;
    Data_Make_Struct(mrb, klass, struct decoder, &decoder_type, p, rd);
    p->zstd.allocator = aux_zstd_allocator(mrb);
    p->zstd.context = ZSTD_createDStream_advanced(p->zstd.allocator);

    if (!p->zstd.context) {
        mrb_raise(mrb,
                  E_RUNTIME_ERROR,
                  "ZSTD_createDStream_advanced failed");
    }

    VALUE obj = mrb_obj_value(rd);
    decoder_set_inport(mrb, obj, p, Qnil);
    decoder_set_inbuf(mrb, obj, p, Qnil);
    mrb_int argc;
    mrb_value *argv;
    mrb_get_args(mrb, "*", &argv, &argc);
    mrb_funcall_argv(mrb, obj, mrb_intern_lit(mrb, "initialize"), argc, argv);

    return obj;
}

static void
dec_initialize_args(MRB, VALUE *inport, VALUE *dict)
{
    VALUE *argv;
    mrb_int argc;
    mrb_get_args(mrb, "*", &argv, &argc);

    if (argc > 0 && mrb_hash_p(argv[argc - 1])) {
        MRBX_SCANHASH(mrb, argv[argc - 1], Qnil,
                MRBX_SCANHASH_ARGS("dict", dict, Qnil));
        if (!NIL_P(*dict)) {
            mrb_check_type(mrb, *dict, MRB_TT_STRING);
            *dict = mrb_str_dup(mrb, *dict);
        }
        argc --;
    } else {
        *dict = Qnil;
    }

    switch (argc) {
    case 1:
        *inport = argv[0];
        break;
    default:
        mrb_raisef(mrb,
                   E_ARGUMENT_ERROR,
                   "wrong number of arguments (given %S, expect 1 with optional keywords)",
                   mrb_fixnum_value(argc));
        break;
    }
}

/*
 * call-seq:
 *  initialize(input_stream, dict: nil) -> self
 */
static VALUE
dec_initialize(MRB, VALUE self)
{
    struct decoder *p = getdecoder(mrb, self);
    VALUE dict;
    dec_initialize_args(mrb, &p->io, &dict);
    decoder_set_inport(mrb, self, p, p->io);
    decoder_set_dict(mrb, self, p, dict);

    if (mrb_string_p(p->io)) {
        decoder_set_inbuf(mrb, self, p, Qnil);
        p->zstd.bufin.src = RSTRING_PTR(p->io);
        p->zstd.bufin.size = RSTRING_LEN(p->io);
        p->zstd.bufin.pos = 0;
    } else {
#if MRB_INT16
        decoder_set_inbuf(mrb, self, p, mrb_str_buf_new(mrb, MRUBY_ZSTD_DEFAULT_PARTIAL_SIZE));
#else
        decoder_set_inbuf(mrb, self, p, mrb_str_buf_new(mrb, ZSTD_DStreamInSize()));
#endif
        p->zstd.bufin.src = RSTRING_PTR(p->inbuf);
        p->zstd.bufin.size = RSTRING_LEN(p->inbuf);
        p->zstd.bufin.pos = 0;
    }

    if (NIL_P(dict)) {
        size_t s = ZSTD_initDStream(p->zstd.context);
        aux_check_error(mrb, s, "ZSTD_initDStream");
    } else {
        size_t s = ZSTD_initDStream_usingDict(p->zstd.context, RSTRING_PTR(dict), RSTRING_LEN(dict));
        aux_check_error(mrb, s, "ZSTD_initDStream_usingDict");
    }

    return self;
}

static void
dec_read_args(MRB, VALUE self, intptr_t *size, VALUE *dest)
{
    mrb_int argc;
    VALUE *argv;
    mrb_get_args(mrb, "*", &argv, &argc);

    switch (argc) {
    case 2:
        *size = (NIL_P(argv[0]) ? -1 : mrb_int(mrb, argv[0]));
        *dest = argv[1];
        break;
    case 1:
        *size = (NIL_P(argv[0]) ? -1 : mrb_int(mrb, argv[0]));
        *dest = Qnil;
        break;
    case 0:
        *size = -1;
        *dest = Qnil;
        break;
    default:
        mrb_raisef(mrb,
                   E_RUNTIME_ERROR,
                   "wrong number of arguments (given %S, expect 0..2)",
                   mrb_fixnum_value(argc));
        break;
    }

    size_t allocsize = *size;

    if (allocsize == -1) {
        allocsize = ZSTD_DStreamOutSize() * 2;
    }

    if (allocsize > AUX_MALLOC_MAX) {
        *size = AUX_MALLOC_MAX;
        allocsize = AUX_MALLOC_MAX;
    }

    if (NIL_P(*dest)) {
        *dest = mrb_str_buf_new(mrb, allocsize);
    } else {
        mrb_check_type(mrb, *dest, MRB_TT_STRING);
    }

    mrb_str_resize(mrb, *dest, allocsize);
    RSTR_SET_LEN(RSTRING(*dest), 0);
}

/*
 * call-seq:
 *  read -> string OR nil
 *  read(size) -> string OR nil
 *  read(size, buffer) -> buffer OR nil
 */
static VALUE
dec_read(MRB, VALUE self)
{
    intptr_t size;
    VALUE dest;
    dec_read_args(mrb, self, &size, &dest);

    struct decoder *p = getdecoder(mrb, self);

    if (size == 0) { return dest; }

    ZSTD_outBuffer bufout = {
        .dst = RSTRING_PTR(dest),
        .size = (size < 0 ? RSTRING_CAPA(dest) : size),
        .pos = 0,
    };

    while (size == -1 || bufout.pos < size) {
        if (p->zstd.bufin.pos - p->zstd.bufin.size < 1) {
            if (NIL_P(p->inbuf)) { break; }
            size_t readsize = ZSTD_DStreamInSize();
            if (readsize > AUX_MALLOC_MAX) { readsize = AUX_MALLOC_MAX; }
            p->inbuf = FUNCALLC(mrb, p->io, "read", mrb_fixnum_value(RSTRING_CAPA(p->inbuf)), p->inbuf);
            if (NIL_P(p->inbuf)) {
                decoder_set_inbuf(mrb, self, p, p->inbuf);
                break;
            }

            if (!mrb_string_p(p->inbuf)) {
                decoder_set_inbuf(mrb, self, p, Qnil);
                mrb_check_type(mrb, p->inbuf, MRB_TT_STRING);
            }

            decoder_set_inbuf(mrb, self, p, p->inbuf);
        }

        if (bufout.pos - bufout.size < 1) {
            size_t s = RSTRING_CAPA(dest);
            if (s == AUX_MALLOC_MAX) { aux_check_error(mrb, ZSTD_error_dstSize_tooSmall, "ZSTD_decompressStream"); }
            s *= 2;
            s = CLAMP_MAX(s, AUX_MALLOC_MAX);
            mrb_str_resize(mrb, dest, s);
            bufout.dst = RSTRING_PTR(dest);
            bufout.size = RSTRING_CAPA(dest);
        }

        {
            size_t s = ZSTD_decompressStream(p->zstd.context, &bufout, &p->zstd.bufin);
            aux_check_error(mrb, s, "ZSTD_decompressStream");
            if (s < 1) { break; }
        }
    }

    RSTR_SET_LEN(RSTRING(dest), bufout.pos);

    return (bufout.pos == 0 ? Qnil : dest);
}

/*
 * call-seq:
 *  close -> nil
 */
static VALUE
dec_close(MRB, VALUE self)
{
    return Qnil;
}

/*
 * call-seq:
 *  eof -> true OR false
 */
static VALUE
dec_eof(MRB, VALUE self)
{
    mrb_raise(mrb, E_NOTIMP_ERROR, "implement me!");
    return Qnil;
}

/*
 * call-seq:
 *  get_port -> port
 */
static VALUE
dec_get_port(MRB, VALUE self)
{
    return getdecoder(mrb, self)->io;
}

static void
init_decoder(MRB, struct RClass *mZstd)
{
    struct RClass *cDecoder = mrb_define_class_under(mrb, mZstd, "Decoder", mrb_cObject);
    mrb_define_class_method(mrb, cDecoder, "decode", dec_s_decode, MRB_ARGS_ANY());
    mrb_define_class_method(mrb, cDecoder, "new", dec_s_new, MRB_ARGS_ANY());
    mrb_define_method(mrb, cDecoder, "initialize", dec_initialize, MRB_ARGS_ANY());
    mrb_define_method(mrb, cDecoder, "read", dec_read, MRB_ARGS_ANY());
    mrb_define_method(mrb, cDecoder, "close", dec_close, MRB_ARGS_NONE());
    mrb_define_method(mrb, cDecoder, "eof", dec_eof, MRB_ARGS_NONE());
    mrb_define_method(mrb, cDecoder, "port", dec_get_port, MRB_ARGS_NONE());

    mrb_define_alias(mrb, cDecoder, "finish", "close");
    mrb_define_alias(mrb, cDecoder, "eof?", "eof");
}

/*
 * mruby_zstd initializer
 * module Zstd
 */

void
mrb_mruby_zstd_gem_init(MRB)
{
    struct RClass *mZstd = mrb_define_module(mrb, "Zstd");

    mrb_define_const(mrb, mZstd, "LIBRARY_VERSION", mrb_str_new_cstr(mrb, ZSTD_VERSION_STRING));

#ifdef ZSTD_LEGACY_SUPPORT
    mrb_define_const(mrb, mZstd, "LEGACY_SUPPORTED", mrb_bool_value(TRUE));
#else
    mrb_define_const(mrb, mZstd, "LEGACY_SUPPORTED", mrb_bool_value(FALSE));
#endif

    init_encoder(mrb, mZstd);
    mrb_gc_arena_restore(mrb, 0);
    init_decoder(mrb, mZstd);
}

void
mrb_mruby_zstd_gem_final(MRB)
{
}
