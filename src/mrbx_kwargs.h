/*
 * This code is under public domain (CC0)
 * <http://creativecommons.org/publicdomain/zero/1.0/>.
 *
 * To the extent possible under law, dearblue has waived all copyright
 * and related or neighboring rights to this work.
 *
 *     dearblue <dearblue@users.noreply.github.com>
 */

#ifndef MRBX_HASHARGS_H
#define MRBX_HASHARGS_H 1

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/hash.h>

#define MRBX_SCANHASH_ELEMENTOF(v) (sizeof((v)) / sizeof((v)[0]))
#define MRBX_SCANHASH_ENDOF(v) ((v) + MRBX_SCANHASH_ELEMENTOF(v))

#if defined(__cplusplus)
#   define MRBX_SCANHASH_CEXTERN         extern "C"
#   define MRBX_SCANHASH_CEXTERN_BEGIN   MRBX_SCANHASH_CEXTERN {
#   define MRBX_SCANHASH_CEXTERN_END     }
#else
#   define MRBX_SCANHASH_CEXTERN
#   define MRBX_SCANHASH_CEXTERN_BEGIN
#   define MRBX_SCANHASH_CEXTERN_END
#endif

MRBX_SCANHASH_CEXTERN_BEGIN

struct mrbx_scanhash_arg
{
    mrb_sym name;
    mrb_value *dest;
    mrb_value initval;
};


/**
 * メソッドが受け取るキーワード引数を解析します。
 *
 * マクロ引数はそれぞれが一度だけ評価されます (多重評価はされません)。
 *
 * 可変長部分の引数 (第3引数以降) の評価順は全ての環境で左から右に固定されます。
 *
 * 第1引数と第2引数の評価順は環境依存となります。
 *
 * @param hash
 *      解析対象のハッシュオブジェクト。nil の場合、受け取り変数を初期化するだけです。
 * @param rest
 *      解析後に残ったキーワードを受け取るハッシュオブジェクトを指定します。
 *      true を指定した場合、内部で新規ハッシュオブジェクトを用意します。
 *      NULL / false / nil の場合、任意キーワードの受け取りを認めません。
 * @param ...
 *      MRBX_SCANHASH_ARG[SI] が並びます。終端を表すものの記述は不要です。
 * @return
 *      受け取り対象外のハッシュオブジェクト (rest で与えたもの) が返ります。
 *
 * @sample
 *
 *  // 走査するハッシュオブジェクト
 *  mrb_value user_hash_object = mrb_hash_new(mrb);
 *
 *  mrb_value a, b, c, d, e, f; // これらの変数に受け取る
 *  MRBX_SCANHASH(mrb, user_hash_object, mrb_nil_value(),
 *          MRBX_SCANHASH_ARGS("a", &a, mrb_nil_value()),
 *          MRBX_SCANHASH_ARGS("b", &b, mrb_false_valse()),
 *          MRBX_SCANHASH_ARGS("c", &c, mrb_str_new_cstr(mrb, "abcdefg")),
 *          MRBX_SCANHASH_ARGS("d", &d, INT2FIX(5)));
 *
 * MRBX_SCANHASH_ARG 系の第2引数に NULL を与えると、名前の確認だけして、Cレベルの変数への代入は行わない。
 *          MRBX_SCANHASH_ARGS("e", NULL, mrb_nil_value())
 *
 * MRBX_SCANHASH_ARG 系の第3引数に Qundef を与えると、省略不可キーワード引数となる
 *          MRBX_SCANHASH_ARGS("f", &f, mrb_undef_value())
 */
#define MRBX_SCANHASH(mrb, hash, rest, ...)                              \
    ({                                                                   \
        struct mrbx_scanhash_arg MRBX_SCANHASH_argv[] = { __VA_ARGS__ }; \
        mrbx_scanhash(mrb, (hash), (rest), MRBX_SCANHASH_argv,           \
                MRBX_SCANHASH_ENDOF(MRBX_SCANHASH_argv));                \
    })                                                                   \

/*
 * 評価順は左から右に固定される。
 *
 * [name]   C の文字列を与える
 * [dest]   キーワード引数の代入先。NULL を指定した場合、名前の確認だけして、Cレベルの変数への代入は行わない
 * [vdefault] 規定値。Qundef を指定した場合、省略不可キーワードとなる
 */
#define MRBX_SCANHASH_ARGS(name, dest, vdefault) { mrb_intern_cstr(mrb, (name)), (dest), (vdefault), }

/*
 * 評価順は左から右に固定される。
 *
 * [name]   ruby の ID をあたえる。
 * [dest]   キーワード引数の代入先。NULL を指定した場合、名前の確認だけして、Cレベルの変数への代入は行わない
 * [vdefault] 規定値。Qundef を指定した場合、省略不可キーワードとなる
 */
#define MRBX_SCANHASH_ARGI(name, dest, vdefault) { (name), (dest), (vdefault), }


/*
 * private implementation
 */


#if MRUBY_RELEASE_NO <= 10200
#   define id_values mrb_intern_lit(mrb, "values")

    static mrb_value
    mrb_hash_values(mrb_state *mrb, mrb_value hash)
    {
        return mrb_funcall_argv(mrb, hash, id_values, 0, NULL);
    }
#endif


struct mrbx_scanhash_args
{
    struct mrbx_scanhash_arg *args;
    const struct mrbx_scanhash_arg *end;
    mrb_value rest;
};

static void
mrbx_scanhash_error(mrb_state *mrb, mrb_sym given, struct mrbx_scanhash_arg *args, const struct mrbx_scanhash_arg *end)
{
    // 引数の数が㌧でもない数の場合、よくないことが起きそう。

    mrb_value names = mrb_ary_new(mrb);
    for (; args < end; args ++) {
        mrb_ary_push(mrb, names, mrb_symbol_value(args->name));
    }

    size_t namenum = RARRAY_LEN(names);
    if (namenum > 2) {
        mrb_value w = mrb_ary_pop(mrb, names);
        names = mrb_ary_join(mrb, names, mrb_str_new_cstr(mrb, ", "));
        names = mrb_ary_new_from_values(mrb, 1, &names);
        mrb_ary_push(mrb, names, w);
        names = mrb_ary_join(mrb, names, mrb_str_new_cstr(mrb, " or "));
    } else if (namenum > 1) {
        names = mrb_ary_join(mrb, names, mrb_str_new_cstr(mrb, " or "));
    }

    {
        mrb_value key = mrb_symbol_value(given);
        mrb_raisef(mrb, E_ARGUMENT_ERROR,
                "unknown keyword (%S for %S)",
                key, names);
    }
}

static int
mrbx_scanhash_foreach(mrb_state *mrb, mrb_value key, mrb_value value, struct mrbx_scanhash_args *args)
{
    struct mrbx_scanhash_arg *p = args->args;
    const struct mrbx_scanhash_arg *end = args->end;
    mrb_sym keyid = mrb_obj_to_sym(mrb, key);

    for (; p < end; p ++) {
        if (p->name == keyid) {
            if (p->dest) {
                *p->dest = value;
            }
            return 0;
        }
    }

    if (mrb_test(args->rest)) {
        mrb_hash_set(mrb, args->rest, key, value);
    } else {
        mrbx_scanhash_error(mrb, keyid, args->args, args->end);
    }

    return 0;
}

static mrb_value
mrbx_scanhash_to_hash(mrb_state *mrb, mrb_value hash)
{
    if (mrb_nil_p(hash)) { return mrb_nil_value(); }

    mrb_sym id_to_hash = mrb_intern_lit(mrb, "to_hash");
    mrb_value hash1 = mrb_funcall_argv(mrb, hash, id_to_hash, 0, 0);
    if (!mrb_hash_p(hash1)) {
        mrb_raisef(mrb, E_TYPE_ERROR,
                "converted object is not a hash (<#%S>)",
                hash);
    }
    return hash1;
}

static inline void
mrbx_scanhash_setdefaults(struct mrbx_scanhash_arg *args, struct mrbx_scanhash_arg *end)
{
    for (; args < end; args ++) {
        if (args->dest) {
            *args->dest = args->initval;
        }
    }
}


static inline void
mrbx_scanhash_check_missingkeys(mrb_state *mrb, struct mrbx_scanhash_arg *args, struct mrbx_scanhash_arg *end)
{
    for (; args < end; args ++) {
        if (args->dest && mrb_undef_p(*args->dest)) {
            mrb_value key = mrb_symbol_value(args->name);
            mrb_raisef(mrb, E_ARGUMENT_ERROR,
                    "missing keyword: `%S'",
                    key);
        }
    }
}

static void
mrbx_hash_foreach(mrb_state *mrb, mrb_value hash, int (*block)(mrb_state *, mrb_value, mrb_value, struct mrbx_scanhash_args *), struct mrbx_scanhash_args *args)
{
    mrb_value keys = mrb_hash_keys(mrb, hash);
    mrb_value values = mrb_hash_values(mrb, hash);
    const mrb_value *k = RARRAY_PTR(keys);
    const mrb_value *const kk = k + RARRAY_LEN(keys);
    const mrb_value *v = RARRAY_PTR(values);
    int arena = mrb_gc_arena_save(mrb);

    for (; k < kk; k ++, v ++) {
        block(mrb, *k, *v, args);
        mrb_gc_arena_restore(mrb, arena);
    }
}

static mrb_value
mrbx_scanhash(mrb_state *mrb, mrb_value hash, mrb_value rest, struct mrbx_scanhash_arg *args, struct mrbx_scanhash_arg *end)
{
    if (mrb_bool(rest)) {
        if (mrb_type(rest) == MRB_TT_TRUE) {
            rest = mrb_hash_new(mrb);
        } else if (!mrb_obj_is_kind_of(mrb, rest, mrb->hash_class)) {
            mrb_raise(mrb, E_ARGUMENT_ERROR,
                    "`rest' is not a hash");
        }
    } else {
        rest = mrb_nil_value();
    }

    mrbx_scanhash_setdefaults(args, end);

    hash = mrbx_scanhash_to_hash(mrb, hash);
    if (!mrb_nil_p(hash) && !mrb_bool(mrb_hash_empty_p(mrb, hash))) {
        struct mrbx_scanhash_args argset = { args, end, rest };
        mrbx_hash_foreach(mrb, hash, mrbx_scanhash_foreach, &argset);
    }

    mrbx_scanhash_check_missingkeys(mrb, args, end);

    return rest;
}

MRBX_SCANHASH_CEXTERN_END

#endif /* !defined(MRBX_HASHARGS_H) */
