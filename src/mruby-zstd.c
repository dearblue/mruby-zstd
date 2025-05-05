#include <mruby.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/error.h>
#include <mruby/hash.h>
#include <mruby/string.h>
#include <mruby/value.h>
#include <mruby/variable.h>
#include <stdlib.h>
#include <string.h>
#include <mruby-aux.h>
#include <mruby-aux/scanhash.h>

//#define ZSTD_STATIC_LINKING_ONLY 1
#include <zstd.h>
#include <zstd_errors.h>

#ifndef MRUBY_ZSTD_DEFAULT_PARTIAL_SIZE
# ifdef MRB_INT16
                                                /* 4 KiB */
#  define MRUBY_ZSTD_DEFAULT_PARTIAL_SIZE       (4 << 10)
# else
                                                /* 1 MiB */
#  define MRUBY_ZSTD_DEFAULT_PARTIAL_SIZE       (1 << 20)
# endif
#endif

#define AUX_PP_JOIN(X, Y) AUX_PP_JOIN0(X, Y)
#define AUX_PP_JOIN0(X, Y) X ## Y
#define AUX_PP_COUNT_ARGS(...) AUX_PP_COUNT_ARGS0(__VA_ARGS__, 20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1, [*])
#define AUX_PP_COUNT_ARGS0(A20,A19,A18,A17,A16,A15,A14,A13,A12,A11,A10,A9,A8,A7,A6,A5,A4,A3,A2,A1, A, ...) A

#define AUX_MALLOC_MAX          (MRB_INT_MAX - 1)

#define CLAMP_MAX(n, max)       ((n) > (max) ? (max) : (n))

#define ID_op_lshift            mrb_intern_lit(mrb, "<<")
#define ID_read                 mrb_intern_lit(mrb, "read")

#define id_fast                 (mrb_intern_lit(mrb, "fast"))
#define id_dfast                (mrb_intern_lit(mrb, "dfast"))
#define id_greedy               (mrb_intern_lit(mrb, "greedy"))
#define id_lazy                 (mrb_intern_lit(mrb, "lazy"))
#define id_lazy2                (mrb_intern_lit(mrb, "lazy2"))
#define id_btlazy2              (mrb_intern_lit(mrb, "btlazy2"))
#define id_btopt                (mrb_intern_lit(mrb, "btopt"))
#define id_btultra              (mrb_intern_lit(mrb, "btultra"))
#define id_btultra2             (mrb_intern_lit(mrb, "btultra2"))

static mrb_value
aux_mrb_str_dup_freeze(mrb_state *mrb, mrb_value str)
{
  mrb_check_type(mrb, str, MRB_TT_STRING);
  if (!mrb_frozen_p(mrb_str_ptr(str))) {
    return mrb_obj_freeze(mrb, mrb_str_dup(mrb, str));
  } else {
    return str;
  }
}

static int64_t
aux_mrb_as_int64(mrb_state *mrb, mrb_value val)
{
  mrb_int n = mrb_int(mrb, val);

  return (int64_t)n;
}

static uint64_t
aux_mrb_as_uint64(mrb_state *mrb, mrb_value val)
{
  mrb_int n = mrb_int(mrb, val);
  if (n < 0) {
    mrb_raise(mrb, E_RANGE_ERROR, "wrong negative number");
  }

  return (uint64_t)n;
}

static int
aux_mrb_to_c_int(mrb_state *mrb, mrb_value val)
{
  mrb_int n = mrb_int(mrb, val);
#if MRB_INT_MAX > INT_MAX
  if (n < INT_MIN || n > INT_MAX) {
    mrb_raise(mrb, E_RANGE_ERROR, "out of range for c integer");
  }
#endif

  return (int)n;
}

static struct RProc *
aux_mrb_ensure_proc_or_nil_ptr(mrb_state *mrb, mrb_value proc)
{
  if (mrb_nil_p(proc)) {
    return NULL;
  }

  mrb_check_type(mrb, proc, MRB_TT_PROC);

  return mrb_proc_ptr(proc);
}

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
  } else if (strategy == id_btultra2) {
    return ZSTD_btultra2;
  } else {
    mrb_raisef(mrb, E_ARGUMENT_ERROR,
               "wrong strategy (given %S, expect fast, dfast, greedy, lazy, lazy2, btlazy2, btopt, btultra or btultra2)",
               astrategy);
  }
}

static void
aux_zstd_error(MRB, size_t status, const char *mesg)
{
  status = status >= 0 ? -status : status;
  int err = (int)ZSTD_getErrorCode(status);

  if (mesg) {
    mrb_raisef(mrb, E_RUNTIME_ERROR,
               "%S failed - %S (code:%S)",
               mrb_str_new_cstr(mrb, mesg),
               mrb_str_new_cstr(mrb, ZSTD_getErrorName(status)),
               mrb_fixnum_value(err));
  } else {
    mrb_raisef(mrb, E_RUNTIME_ERROR,
               "zstd error - %S (code:%S)",
               mrb_str_new_cstr(mrb, ZSTD_getErrorName(status)),
               mrb_fixnum_value(err));
  }
}

static void
aux_check_error(MRB, size_t status, const char *mesg)
{
  if (!ZSTD_isError(status)) { return; }
  aux_zstd_error(mrb, status, mesg);
}


/*
 * class Zstd::Encoder
 */

struct encode_worker
{
  ZSTD_CCtx *zstd;
  mrb_value src, dest, dict;
  int64_t maxdest;
};

static void
make_encoder(mrb_state *mrb, struct encode_worker *p, mrb_value opts)
{
  p->zstd = ZSTD_createCCtx();
  if (!p->zstd) {
    mrb_full_gc(mrb);
    p->zstd = ZSTD_createCCtx();
    if (!p->zstd) {
      mrb_raise(mrb, E_RUNTIME_ERROR, "ZSTD_createCCtx() failed (maybe out of memory)");
    }
  }

  if (mrb_nil_p(opts)) {
    if (!mrb_nil_p(p->src)) {
      size_t ret = ZSTD_CCtx_setPledgedSrcSize(p->zstd, (size_t)RSTRING_LEN(p->src));
      (void)ret;
    }

    p->dict = mrb_nil_value();
  } else {
    struct {
      mrb_value level, windowlog, hashlog, chainlog, searchlog, minmatch, targetlength, strategy,
                enablelongdistancematching, ldmhashlog, ldmminmatch, ldmbucketsizelog, ldmhashratelog,
                contentsizeflag, checksumflag, dictidflag, nbworkers, jobsize, overlaplog, pledgedsize;
      //targetcblocksize         // zstd-1.5.6+
    } a;
    struct mrbx_scanhash_arg args[] = {
      MRBX_SCANHASH_ARGS("level",                      &a.level,                      mrb_nil_value()),
      MRBX_SCANHASH_ARGS("dict",                       &p->dict,                      mrb_nil_value()),
      MRBX_SCANHASH_ARGS("windowlog",                  &a.windowlog,                  mrb_nil_value()),
      MRBX_SCANHASH_ARGS("hashlog",                    &a.hashlog,                    mrb_nil_value()),
      MRBX_SCANHASH_ARGS("chainlog",                   &a.chainlog,                   mrb_nil_value()),
      MRBX_SCANHASH_ARGS("searchlog",                  &a.searchlog,                  mrb_nil_value()),
      MRBX_SCANHASH_ARGS("minmatch",                   &a.minmatch,                   mrb_nil_value()),
      MRBX_SCANHASH_ARGS("targetlength",               &a.targetlength,               mrb_nil_value()),
      MRBX_SCANHASH_ARGS("strategy",                   &a.strategy,                   mrb_nil_value()),
      MRBX_SCANHASH_ARGS("enablelongdistancematching", &a.enablelongdistancematching, mrb_nil_value()),
      MRBX_SCANHASH_ARGS("ldmhashlog",                 &a.ldmhashlog,                 mrb_nil_value()),
      MRBX_SCANHASH_ARGS("ldmminmatch",                &a.ldmminmatch,                mrb_nil_value()),
      MRBX_SCANHASH_ARGS("ldmbucketsizelog",           &a.ldmbucketsizelog,           mrb_nil_value()),
      MRBX_SCANHASH_ARGS("ldmhashratelog",             &a.ldmhashratelog,             mrb_nil_value()),
      MRBX_SCANHASH_ARGS("contentsizeflag",            &a.contentsizeflag,            mrb_nil_value()),
      MRBX_SCANHASH_ARGS("checksumflag",               &a.checksumflag,               mrb_nil_value()),
      MRBX_SCANHASH_ARGS("dictidflag",                 &a.dictidflag,                 mrb_nil_value()),
      MRBX_SCANHASH_ARGS("nbworkers",                  &a.nbworkers,                  mrb_nil_value()),
      MRBX_SCANHASH_ARGS("jobsize",                    &a.jobsize,                    mrb_nil_value()),
      MRBX_SCANHASH_ARGS("overlaplog",                 &a.overlaplog,                 mrb_nil_value()),
      //zstd-1.5.6+
      //MRBX_SCANHASH_ARGS("targetcblocksize",           &p->targetcblocksize,           mrb_nil_value()),

      // このキーワード引数は一番最後に固定配置する必要がある
      MRBX_SCANHASH_ARGS("pledgedsize",                &a.pledgedsize,                mrb_nil_value())
    };

    if (mrb_nil_p(p->src)) {
      mrbx_scanhash(mrb, opts, mrb_nil_value(), ELEMENTOF(args), args);
      if (!mrb_nil_p(a.pledgedsize)) {
        size_t ret = ZSTD_CCtx_setPledgedSrcSize(p->zstd, aux_mrb_as_uint64(mrb, a.pledgedsize));
        (void)ret;
      }
    } else {
      mrbx_scanhash(mrb, opts, mrb_nil_value(), ELEMENTOF(args) - 1, args);
      size_t ret = ZSTD_CCtx_setPledgedSrcSize(p->zstd, (size_t)RSTRING_LEN(p->src));
      (void)ret;
    }

#define SET_ENCODER_PARAMETER(...) AUX_PP_JOIN(SET_ENCODER_PARAMETER_A, AUX_PP_COUNT_ARGS(__VA_ARGS__))(__VA_ARGS__)

#define SET_ENCODER_PARAMETER_A4(MRB, E, P, V) SET_ENCODER_PARAMETER_A5(MRB, E, P, V, aux_mrb_to_c_int)

#define SET_ENCODER_PARAMETER_A5(MRB, E, P, V, C)               \
    do {                                                        \
      if (!mrb_nil_p(V)) {                                      \
        size_t ret = ZSTD_CCtx_setParameter((E)->zstd, P, C(MRB, V)); \
        aux_check_error(MRB, ret, "wrong " #P " parameter");    \
      }                                                         \
    } while (0)                                                 \

    SET_ENCODER_PARAMETER(mrb, p, ZSTD_c_compressionLevel,            a.level);
    SET_ENCODER_PARAMETER(mrb, p, ZSTD_c_windowLog,                   a.windowlog);
    SET_ENCODER_PARAMETER(mrb, p, ZSTD_c_hashLog,                     a.hashlog);
    SET_ENCODER_PARAMETER(mrb, p, ZSTD_c_chainLog,                    a.chainlog);
    SET_ENCODER_PARAMETER(mrb, p, ZSTD_c_searchLog,                   a.searchlog);
    SET_ENCODER_PARAMETER(mrb, p, ZSTD_c_minMatch,                    a.minmatch);
    SET_ENCODER_PARAMETER(mrb, p, ZSTD_c_targetLength,                a.targetlength);
    SET_ENCODER_PARAMETER(mrb, p, ZSTD_c_strategy,                    a.strategy, aux_to_strategy);
    SET_ENCODER_PARAMETER(mrb, p, ZSTD_c_enableLongDistanceMatching,  a.enablelongdistancematching);
    SET_ENCODER_PARAMETER(mrb, p, ZSTD_c_ldmHashLog,                  a.ldmhashlog);
    SET_ENCODER_PARAMETER(mrb, p, ZSTD_c_ldmMinMatch,                 a.ldmminmatch);
    SET_ENCODER_PARAMETER(mrb, p, ZSTD_c_ldmBucketSizeLog,            a.ldmbucketsizelog);
    SET_ENCODER_PARAMETER(mrb, p, ZSTD_c_ldmHashRateLog,              a.ldmhashratelog);
    SET_ENCODER_PARAMETER(mrb, p, ZSTD_c_contentSizeFlag,             a.contentsizeflag);
    SET_ENCODER_PARAMETER(mrb, p, ZSTD_c_checksumFlag,                a.checksumflag);
    SET_ENCODER_PARAMETER(mrb, p, ZSTD_c_dictIDFlag,                  a.dictidflag);
    SET_ENCODER_PARAMETER(mrb, p, ZSTD_c_nbWorkers,                   a.nbworkers);
    SET_ENCODER_PARAMETER(mrb, p, ZSTD_c_jobSize,                     a.jobsize);
    SET_ENCODER_PARAMETER(mrb, p, ZSTD_c_overlapLog,                  a.overlaplog);

    // zstd-1.5.6+
    //SET_ENCODER_PARAMETER(mrb, p, ZSTD_c_targetCBlockSize,            a.targetcblocksize);

    if (!mrb_nil_p(p->dict)) {
      p->dict = aux_mrb_str_dup_freeze(mrb, p->dict);
      size_t ret = ZSTD_CCtx_loadDictionary(p->zstd, RSTRING_PTR(p->dict), (size_t)RSTRING_LEN(p->dict));
      aux_check_error(mrb, ret, "failed ZSTD_CCtx_loadDictionary()");
    }
  }
}

static void
enc_s_encode_setup(mrb_state *mrb, struct encode_worker *e)
{
  mrb_value *argv;
  mrb_int argc;
  mrb_get_args(mrb, "*", &argv, &argc);
  mrb_value opts;

  if (argc > 0 && mrb_hash_p(argv[argc - 1])) {
    opts = argv[argc - 1];
    argc--;
  } else {
    opts = mrb_nil_value();
  }

  switch (argc) {
  case 1:
    e->src = argv[0];
    e->maxdest = -1;
    e->dest = mrb_nil_value();
    break;
  case 2:
    e->src = argv[0];
    if (mrb_string_p(argv[1])) {
      e->maxdest = -1;
      e->dest = argv[1];
    } else {
      e->maxdest = (mrb_nil_p(argv[1]) ? -1 : aux_mrb_as_uint64(mrb, argv[1]));
      e->dest = mrb_nil_value();
    }
    break;
  case 3:
    e->src = argv[0];
    e->maxdest = (mrb_nil_p(argv[1]) ? -1 : aux_mrb_as_uint64(mrb, argv[1]));
    e->dest = argv[2];
    break;
  default:
    mrb_raisef(mrb,
               E_ARGUMENT_ERROR,
               "wrong number of arguments (given %i, expect 1..3)",
               argc);
    break;
  }

  mrb_check_type(mrb, e->src, MRB_TT_STRING);

  if (e->maxdest < 0) {
    e->maxdest = ZSTD_compressBound(RSTRING_LEN(e->src));
  }

  if (mrb_nil_p(e->dest)) {
    e->dest = mrb_str_buf_new(mrb, e->maxdest);
  } else {
    mrb_check_type(mrb, e->dest, MRB_TT_STRING);
    mrb_str_resize(mrb, e->dest, e->maxdest);
  }

  RSTR_SET_LEN(RSTRING(e->dest), 0);

  make_encoder(mrb, e, opts);
}

static mrb_value
enc_s_encode_main(mrb_state *mrb, void *ptr)
{
  struct encode_worker *e = (struct encode_worker *)ptr;
  enc_s_encode_setup(mrb, e);

  ZSTD_inBuffer input = {
    .src = RSTRING_PTR(e->src),
    .size = RSTRING_LEN(e->src),
    .pos = 0,
  };
  ZSTD_outBuffer output = {
    .dst = RSTRING_PTR(e->dest),
    .size = (e->maxdest < 0 ? RSTRING_CAPA(e->dest) : e->maxdest),
    .pos = 0,
  };

  for (;;) {
    size_t s = ZSTD_compressStream(e->zstd, &output, &input);
    if (input.pos >= input.size) {
      break;
    }
    aux_check_error(mrb, s, "ZSTD_compressStream");
    if (e->maxdest >= 0) {
      aux_zstd_error(mrb,
                     ZSTD_error_dstSize_tooSmall,
                     "ZSTD_compressStream");
    }

    /* expand dest */
    s = RSTRING_CAPA(e->dest);
    if (s >= AUX_MALLOC_MAX) {
      aux_zstd_error(mrb,
                     ZSTD_error_dstSize_tooSmall,
                     "ZSTD_compressStream");
    }
    s += MRUBY_ZSTD_DEFAULT_PARTIAL_SIZE;
    s = CLAMP_MAX(s, AUX_MALLOC_MAX);
    mrb_str_resize(mrb, e->dest, s);
    output.dst = RSTRING_PTR(e->dest);
    output.size = RSTRING_CAPA(e->dest);
  }

  for (;;) {
    size_t s = ZSTD_endStream(e->zstd, &output); /* 's' is Status */
    if (s == 0) {
      break;
    }
    aux_check_error(mrb, s, "ZSTD_endStream");
    if (e->maxdest >= 0) {
      aux_zstd_error(mrb,
                     ZSTD_error_dstSize_tooSmall,
                     "ZSTD_endStream");
    }

    /* expand dest */
    s = RSTRING_CAPA(e->dest); /* 's' is Size */
    if (s >= AUX_MALLOC_MAX) {
      aux_zstd_error(mrb,
                     ZSTD_error_dstSize_tooSmall,
                     "ZSTD_endStream");
    }
    s += MRUBY_ZSTD_DEFAULT_PARTIAL_SIZE;
    s = CLAMP_MAX(s, AUX_MALLOC_MAX);
    mrb_str_resize(mrb, e->dest, s);
    output.dst = RSTRING_PTR(e->dest);
    output.size = RSTRING_CAPA(e->dest);
  }

  RSTR_SET_LEN(RSTRING(e->dest), output.pos);

  return mrb_nil_value();
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
  struct encode_worker worker = { NULL };
  mrb_bool err;
  mrb_value ret = mrb_protect_error(mrb, enc_s_encode_main, &worker, &err);

  ZSTD_freeCCtx(worker.zstd);

  if (err) {
    mrb_exc_raise(mrb, ret);
  }

  return worker.dest;
}

struct encoder
{
  struct {
    ZSTD_CCtx *context;
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

struct encode_worker1
{
  struct encode_worker e;
  mrb_value self;
};

static void
enc_initialize_setup(mrb_state *mrb, struct encode_worker1 *e, mrb_value *outport)
{
  mrb_int argc;
  mrb_value *argv;
  mrb_value opts;
  mrb_get_args(mrb, "*", &argv, &argc);

  if (argc > 0 && mrb_hash_p(argv[argc - 1])) {
    opts = argv[argc - 1];
    argc--;
  } else {
    opts = mrb_nil_value();
  }

  switch (argc) {
  case 1:
    *outport = argv[0];
    break;
  default:
    mrb_raisef(mrb,
               E_ARGUMENT_ERROR,
               "wrong number of arguments (given %i, expect `initialize(outport, **opts)')",
               argc);
  }

  make_encoder(mrb, &e->e, opts);
}

static mrb_value
enc_initialize_main(mrb_state *mrb, void *opaque)
{
  struct encode_worker1 *e = (struct encode_worker1 *)opaque;

  mrb_value outport;
  enc_initialize_setup(mrb, e, &outport);

  if (DATA_PTR(e->self) != NULL) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "wrong twice re-initialization");
  }

  struct encoder *p = (struct encoder *)mrb_calloc(mrb, 1, sizeof(struct encoder));
  mrb_data_init(e->self, p, &encoder_type);
  p->io = Qnil;
  p->outbufsize = ZSTD_CStreamOutSize();
  if (p->outbufsize > AUX_MALLOC_MAX) { p->outbufsize = AUX_MALLOC_MAX; }

  encoder_set_outport(mrb, e->self, p, outport);
  encoder_set_outbuf(mrb, e->self, p, Qnil);
  p->zstd.context = e->e.zstd;
  e->e.zstd = NULL;

  return mrb_nil_value();
}

/*
 * call-seq:
 *  initialize(outport, **opts)
 */
static mrb_value
enc_initialize(mrb_state *mrb, mrb_value self)
{
  struct encode_worker1 e = { { NULL, mrb_nil_value(), mrb_nil_value(), mrb_nil_value(), -1 }, self };
  mrb_bool err;
  mrb_protect_error(mrb, enc_initialize_main, &e, &err);

  ZSTD_freeCCtx(e.e.zstd);
  if (err) {
    mrb_exc_raise(mrb, mrb_obj_value(mrb->exc));
  }

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
  int ai = mrb_gc_arena_save(mrb);

  while (input.pos < input.size) {
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
    FUNCALL(mrb, p->io, ID_op_lshift, p->outbuf);
    mrb_gc_arena_restore(mrb, ai);
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
  int ai = mrb_gc_arena_save(mrb);

  do {
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
    FUNCALL(mrb, p->io, ID_op_lshift, p->outbuf);
    mrb_gc_arena_restore(mrb, ai);
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
  int ai = mrb_gc_arena_save(mrb);

  do {
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
    FUNCALL(mrb, p->io, ID_op_lshift, p->outbuf);
    mrb_gc_arena_restore(mrb, ai);
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
  MRB_SET_INSTANCE_TT(cEncoder, MRB_TT_DATA);
  mrb_define_class_method(mrb, cEncoder, "encode", enc_s_encode, MRB_ARGS_ANY());
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

struct decode_worker
{
  ZSTD_DCtx *zstd;
  mrb_value src, dest, dict;
  struct RProc *skippable;
  intptr_t maxdest;
  mrb_bool concat:1;
  mrb_bool partial:1;
};

static void
dec_s_decode_setup(mrb_state *mrb, struct decode_worker *w)
{
  VALUE *argv;
  mrb_int argc;
  mrb_get_args(mrb, "S*", &w->src, &argv, &argc);

  if (argc > 0 && mrb_hash_p(argv[argc - 1])) {
    struct {
      mrb_value concat, partial, skippable;
    } opts;
    argc--;
    MRBX_SCANHASH(mrb, argv[argc], mrb_nil_value(),
                  MRBX_SCANHASH_ARGS("dict", &w->dict, mrb_nil_value()),
                  MRBX_SCANHASH_ARGS("concat", &opts.concat, mrb_nil_value()),
                  MRBX_SCANHASH_ARGS("partial", &opts.partial, mrb_nil_value()),
                  MRBX_SCANHASH_ARGS("skippable", &opts.skippable, mrb_nil_value()));
    if (!mrb_nil_p(w->dict)) {
      mrb_check_type(mrb, w->dict, MRB_TT_STRING);
    }
    w->skippable = aux_mrb_ensure_proc_or_nil_ptr(mrb, opts.skippable);
    w->concat = (mrb_nil_p(opts.concat) ? TRUE : mrb_bool(opts.concat));
    w->partial = (mrb_nil_p(opts.partial) ? FALSE : mrb_bool(opts.partial));
  } else {
    w->dict = mrb_nil_value();
  }

  switch (argc) {
  case 0:
    w->maxdest = -1;
    w->dest = mrb_nil_value();
    break;
  case 1:
    if (mrb_string_p(argv[0])) {
      w->maxdest = -1;
      w->dest = argv[0];
    } else {
      w->maxdest = mrb_int(mrb, argv[0]);
      w->dest = mrb_str_buf_new(mrb, w->maxdest);
    }
    break;
  case 2:
    w->maxdest = mrb_int(mrb, argv[0]);
    w->dest = argv[1];
    break;
  default:
    mrb_raisef(mrb,
               E_ARGUMENT_ERROR,
               "wrong number of arguments (given %S, expect 1..3)",
               mrb_fixnum_value(argc + 1));
    break;
  }

  intptr_t allocsize;
  if (w->maxdest < 0) {
    allocsize = MRUBY_ZSTD_DEFAULT_PARTIAL_SIZE;
  } else {
    allocsize = w->maxdest;
  }

  if (mrb_nil_p(w->dest)) {
    w->dest = mrb_str_buf_new(mrb, allocsize);
  } else {
    mrb_check_type(mrb, w->dest, MRB_TT_STRING);
    mrb_str_resize(mrb, w->dest, allocsize);
    RSTR_SET_LEN(mrb_str_ptr(w->dest), 0);
  }

  w->zstd = ZSTD_createDCtx();
  if (!w->zstd) {
    mrb_full_gc(mrb);
    w->zstd = ZSTD_createDCtx();
    if (!w->zstd) {
      mrb_raise(mrb, E_RUNTIME_ERROR, "failed ZSTD_createDCtx() (maybe out of memory)");
    }
  }

  // ? ZSTDLIB_API size_t ZSTD_DCtx_setParameter(ZSTD_DCtx* dctx, ZSTD_dParameter param, int value);

  if (!mrb_nil_p(w->dict)) {
    size_t s = ZSTD_DCtx_loadDictionary(w->zstd, RSTRING_PTR(w->dict), (size_t)RSTRING_LEN(w->dict));
    aux_check_error(mrb, s, "ZSTD_DCtx_loadDictionary");
  }
}

static mrb_value
dec_s_decode_main(mrb_state *mrb, void *opaque)
{
  struct decode_worker *w = (struct decode_worker *)opaque;
  dec_s_decode_setup(mrb, w);

  ZSTD_inBuffer bufin = {
    /* .src = */  RSTRING_PTR(w->src),
    /* .size = */ RSTRING_LEN(w->src),
    /* .pos = */  0
  };
  ZSTD_outBuffer bufout = {
    /* .dst =  */ RSTRING_PTR(w->dest),
    /* .size = */ (w->maxdest < 0 ? RSTRING_CAPA(w->dest) : w->maxdest),
    /* .pos =  */ 0
  };

  for (;;) {
    size_t s = ZSTD_decompressStream(w->zstd, &bufout, &bufin);
    //??w->pos = bufout.pos;
    aux_check_error(mrb, s, "ZSTD_decompressStream");

    if (s > 0 && w->maxdest == bufout.pos) {
      if (w->partial) {
        RSTR_SET_LEN(mrb_str_ptr(w->dest), bufout.pos);
        break;
      } else {
        aux_zstd_error(mrb, ZSTD_error_dstSize_tooSmall, "ZSTD_decompressStream");
      }
    }

    if (s == 0 || w->maxdest >= 0 || bufout.pos >= AUX_MALLOC_MAX) {
      RSTR_SET_LEN(mrb_str_ptr(w->dest), bufout.pos);
      break;
    }

    /* dest を拡張する */

    s = RSTRING_CAPA(w->dest);
    if (s >= AUX_MALLOC_MAX) {
      aux_zstd_error(mrb, ZSTD_error_dstSize_tooSmall, "ZSTD_decompressStream");
    }
    s += MRUBY_ZSTD_DEFAULT_PARTIAL_SIZE;
    s = CLAMP_MAX(s, AUX_MALLOC_MAX);
    mrb_str_resize(mrb, w->dest, s);
    RSTR_SET_LEN(mrb_str_ptr(w->dest), 0);
    bufout.dst = RSTRING_PTR(w->dest);
    bufout.size = RSTRING_CAPA(w->dest);
  }

  return w->dest;
}

/*
 *  call-seq:
 *    decode(zstd_sequence, maxsize, buffer = "", opts = {}) -> buffer
 *    decode(zstd_sequence, buffer, opts = {}) -> buffer
 *
 *  [opts (hash)]
 *    dict (string OR nil) (default: nil)::
 *      伸長に必要な辞書を指定する。
 *    concat (true OR false) (default: false)::
 *      真であれば、単一のストリーム中にある複数のフレームを連結して伸長する。
 *      偽であれば、最初のフレームのみを伸長する。
 *    partial (true OR false) (default: false)::
 *      真であれば、ストリームが maxsize を超えた場合に最初の部分だけを伸長する。
 *      偽であれば、maxsize を超える場合は例外が発生する。
 *    skippable (proc OR nil) (default: nil)::
 *      skippable frame に遭遇した場合に呼び出されるブロックを指定する。
 *      `proc { |controller| ... }`
 */
static mrb_value
dec_s_decode(mrb_state *mrb, mrb_value self)
{
  struct decode_worker w = { NULL, mrb_nil_value(), mrb_nil_value(), mrb_nil_value(), 0 };
  mrb_bool err;
  mrb_value ret = mrb_protect_error(mrb, dec_s_decode_main, &w, &err);

  ZSTD_freeDCtx(w.zstd);

  if (err) {
    mrb_exc_raise(mrb, ret);
  }

  return w.dest;
}

struct decoder
{
  struct {
    ZSTD_DCtx *context;
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

static void
dec_initialize_setup(mrb_state *mrb, struct decode_worker *w)
{
  VALUE *argv;
  mrb_int argc;
  mrb_get_args(mrb, "*", &argv, &argc);

  if (argc > 0 && mrb_hash_p(argv[argc - 1])) {
    MRBX_SCANHASH(mrb, argv[argc - 1], Qnil,
                  MRBX_SCANHASH_ARGS("dict", &w->dict, Qnil));
    if (!NIL_P(w->dict)) {
      mrb_check_type(mrb, w->dict, MRB_TT_STRING);
      w->dict = aux_mrb_str_dup_freeze(mrb, w->dict);
    }
    argc--;
  } else {
    w->dict = mrb_nil_value();
  }

  switch (argc) {
  case 1:
    w->src = argv[0];
    break;
  default:
    mrb_raisef(mrb,
               E_ARGUMENT_ERROR,
               "wrong number of arguments (given %S, expect #initialize(inport, **opts)",
               mrb_fixnum_value(argc));
    break;
  }

  w->zstd = ZSTD_createDCtx();
  if (!w->zstd) {
    mrb_full_gc(mrb);
    w->zstd = ZSTD_createDCtx();
    if (!w->zstd) {
      mrb_raise(mrb, E_RUNTIME_ERROR, "failed ZSTD_createDCtx() (maybe out of memory)");
    }
  }

  if (!mrb_nil_p(w->dict)) {
    size_t s = ZSTD_DCtx_loadDictionary(w->zstd, RSTRING_PTR(w->dict), (size_t)RSTRING_LEN(w->dict));
    aux_check_error(mrb, s, "ZSTD_DCtx_loadDictionary");
  }
}

struct dec_initialize_main
{
  struct decode_worker w;
  mrb_value self;
};

static mrb_value
dec_initialize_main(mrb_state *mrb, void *opaque)
{
  struct dec_initialize_main *w = (struct dec_initialize_main *)opaque;
  dec_initialize_setup(mrb, &w->w);

  if (DATA_PTR(w->self) != NULL) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "wrong twice re-initialization");
  }

  struct decoder *p = (struct decoder *)mrb_calloc(mrb, 1, sizeof(struct decoder));
  mrb_data_init(w->self, p, &decoder_type);

  decoder_set_inbuf(mrb, w->self, p, mrb_nil_value());
  decoder_set_inport(mrb, w->self, p, w->w.src);
  decoder_set_dict(mrb, w->self, p, w->w.dict);

  if (mrb_string_p(w->w.src)) {
    decoder_set_inbuf(mrb, w->self, p, mrb_nil_value());
    p->zstd.bufin.src = RSTRING_PTR(w->w.src);
    p->zstd.bufin.size = RSTRING_LEN(w->w.src);
    p->zstd.bufin.pos = 0;
  } else {
    decoder_set_inbuf(mrb, w->self, p, mrb_str_buf_new(mrb, ZSTD_DStreamInSize()));
    p->zstd.bufin.src = RSTRING_PTR(p->inbuf);
    p->zstd.bufin.size = RSTRING_LEN(p->inbuf);
    p->zstd.bufin.pos = 0;
  }

  p->zstd.context = w->w.zstd;

  return mrb_nil_value();
}

/*
 * call-seq:
 *  initialize(input_stream, dict: nil) -> self
 */
static mrb_value
dec_initialize(mrb_state *mrb, mrb_value self)
{
  struct dec_initialize_main w = { { NULL }, self };
  mrb_bool err;
  mrb_value ret = mrb_protect_error(mrb, dec_initialize_main, &w, &err);

  if (err) {
    ZSTD_freeDCtx(w.w.zstd);
    mrb_exc_raise(mrb, ret);
  }

  return self;
}

static void
dec_read_args(MRB, VALUE self, intptr_t *size, struct RString **dest)
{
  mrbx_get_read_args(mrb, size, dest);

  size_t allocsize = *size;

  if (allocsize == -1) {
    allocsize = ZSTD_DStreamOutSize() * 2;
  }

  if (allocsize > AUX_MALLOC_MAX) {
    *size = AUX_MALLOC_MAX;
    allocsize = AUX_MALLOC_MAX;
  }

  mrbx_str_reserve(mrb, *dest, allocsize);
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
  struct RString *dest;
  dec_read_args(mrb, self, &size, &dest);

  struct decoder *p = getdecoder(mrb, self);

  if (size == 0) { return mrb_obj_value(dest); }

  ZSTD_outBuffer bufout = {
    .dst = RSTR_PTR(dest),
    .size = (size < 0 ? RSTR_CAPA(dest) : size),
    .pos = 0,
  };

  while (size == -1 || bufout.pos < size) {
    if (p->zstd.bufin.pos - p->zstd.bufin.size < 1) {
      if (NIL_P(p->inbuf)) { break; }
      size_t readsize = ZSTD_DStreamInSize();
      if (readsize > AUX_MALLOC_MAX) { readsize = AUX_MALLOC_MAX; }
#if MRUBY_RELEASE_NO == 30200
      mrb_str_modify(mrb, mrb_str_ptr(p->inbuf));
      RSTR_SET_LEN(mrb_str_ptr(p->inbuf), 0);
#endif
      p->inbuf = FUNCALL(mrb, p->io, ID_read, mrb_fixnum_value(RSTRING_CAPA(p->inbuf)), p->inbuf);
#if MRUBY_RELEASE_NO == 30200
      if (mrb_string_p(p->inbuf) && RSTRING_LEN(p->inbuf) == 0) {
        p->inbuf = mrb_nil_value();
      }
#endif
      if (NIL_P(p->inbuf)) {
        decoder_set_inbuf(mrb, self, p, p->inbuf);
        break;
      } else if (!mrb_string_p(p->inbuf)) {
        decoder_set_inbuf(mrb, self, p, Qnil);
        mrb_check_type(mrb, p->inbuf, MRB_TT_STRING);
        // not reached
      } else {
        decoder_set_inbuf(mrb, self, p, p->inbuf);
        p->zstd.bufin.size = RSTRING_LEN(p->inbuf);
        p->zstd.bufin.pos = 0;
      }
    }

    if (bufout.pos - bufout.size < 1) {
      size_t s = RSTR_CAPA(dest);
      if (s == AUX_MALLOC_MAX) { aux_check_error(mrb, ZSTD_error_dstSize_tooSmall, "ZSTD_decompressStream"); }
      s *= 2;
      s = CLAMP_MAX(s, AUX_MALLOC_MAX);
      mrbx_str_reserve(mrb, dest, s);
      bufout.dst = RSTR_PTR(dest);
      bufout.size = RSTR_CAPA(dest);
    }

    {
      size_t s = ZSTD_decompressStream(p->zstd.context, &bufout, &p->zstd.bufin);
      aux_check_error(mrb, s, "ZSTD_decompressStream");
      if (s < 1) { break; }
    }
  }

  RSTR_SET_LEN(dest, bufout.pos);

  return (bufout.pos == 0 ? Qnil : mrb_obj_value(dest));
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
  MRB_SET_INSTANCE_TT(cDecoder, MRB_TT_DATA);
  mrb_define_class_method(mrb, cDecoder, "decode", dec_s_decode, MRB_ARGS_ANY());
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
  init_decoder(mrb, mZstd);
}

void
mrb_mruby_zstd_gem_final(MRB)
{
}
