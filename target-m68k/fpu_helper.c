/*
 *  m68k FPU helpers
 *
 *  Copyright (c) 2006-2007 CodeSourcery
 *  Written by Paul Brook
 *  Copyright (c) 2011-2016 Laurent Vivier
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include <math.h>

static const floatx80 fpu_rom[128] = {
    [0x00] = floatx80_pi,                                       /* Pi */

    [0x0b] = { .high = 0x3ffd, .low = 0x9a209a84fbcff798ULL },  /* Log10(2) */
    [0x0c] = floatx80_e,                                        /* e        */
    [0x0d] = floatx80_log2e,                                    /* Log2(e)  */
    [0x0e] = { .high = 0x3ffd, .low = 0xde5bd8a937287195ULL },  /* Log10(e) */
    [0x0f] = floatx80_zero,                                     /* Zero     */

    [0x30] = floatx80_ln2,                                      /* ln(2)    */
    [0x31] = { .high = 0x4000, .low = 0x935d8dddaaa8ac17ULL },  /* ln(10)   */
    [0x32] = floatx80_one,                                      /* 10^0     */
    [0x33] = floatx80_10,                                       /* 10^1     */
    [0x34] = { .high = 0x4005, .low = 0xc800000000000000ULL },  /* 10^2     */
    [0x35] = { .high = 0x400c, .low = 0x9c40000000000000ULL },  /* 10^4     */
    [0x36] = { .high = 0x4019, .low = 0xbebc200000000000ULL },  /* 10^8     */
    [0x37] = { .high = 0x4034, .low = 0x8e1bc9bf04000000ULL },  /* 10^16    */
    [0x38] = { .high = 0x4069, .low = 0x9dc5ada82b70b59eULL },  /* 10^32    */
    [0x39] = { .high = 0x40d3, .low = 0xc2781f49ffcfa6d5ULL },  /* 10^64    */
    [0x3a] = { .high = 0x41a8, .low = 0x93ba47c980e98ce0ULL },  /* 10^128   */
    [0x3b] = { .high = 0x4351, .low = 0xaa7eebfb9df9de8eULL },  /* 10^256   */
    [0x3c] = { .high = 0x46a3, .low = 0xe319a0aea60e91c7ULL },  /* 10^512   */
    [0x3d] = { .high = 0x4d48, .low = 0xc976758681750c17ULL },  /* 10^1024  */
    [0x3e] = { .high = 0x5a92, .low = 0x9e8b3b5dc53d5de5ULL },  /* 10^2048  */
    [0x3f] = { .high = 0x7525, .low = 0xc46052028a20979bULL },  /* 10^4096  */
};

static floatx80 FP0_to_floatx80(CPUM68KState *env)
{
    return env->fp0.d;
}

static void floatx80_to_FP0(CPUM68KState *env, floatx80 res)
{
    env->fp0.d = res;
}

static int32_t FP0_to_int32(CPUM68KState *env)
{
    return env->fp0.l.upper;
}

static void int32_to_FP0(CPUM68KState *env, int32_t val)
{
    env->fp0.l.upper = val;
}

static float32 FP0_to_float32(CPUM68KState *env)
{
    return *(float32 *)&env->fp0.l.upper;
}

static void float32_to_FP0(CPUM68KState *env, float32 val)
{
    env->fp0.l.upper = *(uint32_t *)&val;
}

static float64 FP0_to_float64(CPUM68KState *env)
{
    return *(float64 *)&env->fp0.l.lower;
}
static void float64_to_FP0(CPUM68KState *env, float64 val)
{
    env->fp0.l.lower = *(uint64_t *)&val;
}

static floatx80 FP1_to_floatx80(CPUM68KState *env)
{
    return env->fp1.d;
}

static void floatx80_to_FP1(CPUM68KState *env, floatx80 res)
{
    env->fp1.d = res;
}

void HELPER(exts32_FP0)(CPUM68KState *env)
{
    floatx80 res;

    res = int32_to_floatx80(FP0_to_int32(env), &env->fp_status);

    floatx80_to_FP0(env, res);
}

void HELPER(extf32_FP0)(CPUM68KState *env)
{
    floatx80 res;

    res = float32_to_floatx80(FP0_to_float32(env), &env->fp_status);

    floatx80_to_FP0(env, res);
}

void HELPER(extf64_FP0)(CPUM68KState *env)
{
    floatx80 res;

    res = float64_to_floatx80(FP0_to_float64(env), &env->fp_status);

    floatx80_to_FP0(env, res);
}

void HELPER(reds32_FP0)(CPUM68KState *env)
{
    int32_t res;

    res = floatx80_to_int32(FP0_to_floatx80(env), &env->fp_status);

    int32_to_FP0(env, res);
}

void HELPER(redf32_FP0)(CPUM68KState *env)
{
    float32 res;

    res = floatx80_to_float32(FP0_to_floatx80(env), &env->fp_status);

    float32_to_FP0(env, res);
}

void HELPER(redf64_FP0)(CPUM68KState *env)
{
    float64 res;

    res = floatx80_to_float64(FP0_to_floatx80(env), &env->fp_status);

    float64_to_FP0(env, res);
}

void HELPER(iround_FP0)(CPUM68KState *env)
{
    floatx80 res;

    res = floatx80_round_to_int(FP0_to_floatx80(env), &env->fp_status);

    floatx80_to_FP0(env, res);
}

static inline void restore_precision_mode(CPUM68KState *env)
{
    int rounding_precision;

    rounding_precision = (env->fpcr >> 6) & 0x03;

    switch (rounding_precision) {
    case 0: /* extended */
        set_floatx80_rounding_precision(80, &env->fp_status);
        break;
    case 1: /* single */
        set_floatx80_rounding_precision(32, &env->fp_status);
        break;
    case 2: /* double */
        set_floatx80_rounding_precision(64, &env->fp_status);
        break;
    case 3: /* reserved */
    default:
        break;
    }
}

static inline void restore_rounding_mode(CPUM68KState *env)
{
    int rounding_mode;

    rounding_mode = (env->fpcr >> 4) & 0x03;

    switch (rounding_mode) {
    case 0: /* round to nearest */
        set_float_rounding_mode(float_round_nearest_even, &env->fp_status);
        break;
    case 1: /* round to zero */
        set_float_rounding_mode(float_round_to_zero, &env->fp_status);
        break;
    case 2: /* round toward minus infinity */
        set_float_rounding_mode(float_round_down, &env->fp_status);
        break;
    case 3: /* round toward positive infinity */
        set_float_rounding_mode(float_round_up, &env->fp_status);
        break;
    }
}

void HELPER(set_fpcr)(CPUM68KState *env, uint32_t val)
{
    env->fpcr = val & 0xffff;

    restore_precision_mode(env);
    restore_rounding_mode(env);
}

void HELPER(itrunc_FP0)(CPUM68KState *env)
{
    floatx80 res;

    set_float_rounding_mode(float_round_to_zero, &env->fp_status);
    res = floatx80_round_to_int(FP0_to_floatx80(env), &env->fp_status);
    restore_rounding_mode(env);

    floatx80_to_FP0(env, res);
}

void HELPER(sqrt_FP0)(CPUM68KState *env)
{
    floatx80 res;

    res = floatx80_sqrt(FP0_to_floatx80(env), &env->fp_status);

    floatx80_to_FP0(env, res);
}

void HELPER(abs_FP0)(CPUM68KState *env)
{
    floatx80 res;

    res = floatx80_abs(FP0_to_floatx80(env));

    floatx80_to_FP0(env, res);
}

void HELPER(chs_FP0)(CPUM68KState *env)
{
    floatx80 res;

    res = floatx80_chs(FP0_to_floatx80(env));

    floatx80_to_FP0(env, res);
}

void HELPER(add_FP0_FP1)(CPUM68KState *env)
{
    floatx80 res;

    res = floatx80_add(FP0_to_floatx80(env), FP1_to_floatx80(env),
                      &env->fp_status);

    floatx80_to_FP0(env, res);
}

void HELPER(sub_FP0_FP1)(CPUM68KState *env)
{
    floatx80 res;

    res = floatx80_sub(FP1_to_floatx80(env), FP0_to_floatx80(env),
                       &env->fp_status);

    floatx80_to_FP0(env, res);
}

void HELPER(mul_FP0_FP1)(CPUM68KState *env)
{
    floatx80 res;

    res = floatx80_mul(FP0_to_floatx80(env), FP1_to_floatx80(env),
                       &env->fp_status);

    floatx80_to_FP0(env, res);
}

void HELPER(div_FP0_FP1)(CPUM68KState *env)
{
    floatx80 res;

    res = floatx80_div(FP1_to_floatx80(env), FP0_to_floatx80(env),
                       &env->fp_status);

    floatx80_to_FP0(env, res);
}

void HELPER(cmp_FP0_FP1)(CPUM68KState *env)
{
    floatx80 res;
    res = floatx80_sub(FP1_to_floatx80(env), FP0_to_floatx80(env),
                       &env->fp_status);
    if (floatx80_is_any_nan(res)) {
        /* +/-inf compares equal against itself, but sub returns nan.  */
        if (!floatx80_is_any_nan(FP0_to_floatx80(env))
            && !floatx80_is_any_nan(FP1_to_floatx80(env))) {
            res = floatx80_zero;
            if (floatx80_lt_quiet(FP1_to_floatx80(env),
                                  res, &env->fp_status)) {
                res = floatx80_chs(res);
            }
        }
    }

    floatx80_to_FP0(env, res);
}

uint32_t HELPER(compare_FP0)(CPUM68KState *env)
{
    return floatx80_compare_quiet(FP0_to_floatx80(env), floatx80_zero,
                                  &env->fp_status);
}

void HELPER(const_FP0)(CPUM68KState *env, uint32_t offset)
{
    env->fp0.d = fpu_rom[offset];
}

static long double floatx80_to_ldouble(floatx80 val)
{
    if (floatx80_is_infinity(val)) {
            if (floatx80_is_neg(val)) {
                    return -__builtin_infl();
            }
            return __builtin_infl();
    }
    if (floatx80_is_any_nan(val)) {
            char low[20];
            sprintf(low, "0x%016"PRIx64, val.low);

            return nanl(low);
    }

    return *(long double *)&val;
}

static floatx80 ldouble_to_floatx80(long double val)
{
    floatx80 res;

    if (isinf(val)) {
            res.high = floatx80_default_nan(NULL).high;
            res.low = 0;
    }
    if (isinf(val) < 0) {
            res.high |= 0x8000;
    }
    if (isnan(val)) {
            res.high = floatx80_default_nan(NULL).high;
            res.low = *(uint64_t *)((char *)&val + 4);
    }
    return *(floatx80 *)&val;
}

void HELPER(sinh_FP0)(CPUM68KState *env)
{
    floatx80 res;
    long double val;

    val = sinhl(floatx80_to_ldouble(FP0_to_floatx80(env)));
    res = ldouble_to_floatx80(val);

    floatx80_to_FP0(env, res);
}

void HELPER(lognp1_FP0)(CPUM68KState *env)
{
    floatx80 val;
    long double res;

    val = FP0_to_floatx80(env);
    res = logl(floatx80_to_ldouble(val) + 1.0);

    floatx80_to_FP0(env, ldouble_to_floatx80(res));
}

void HELPER(ln_FP0)(CPUM68KState *env)
{
    floatx80 val;
    long double res;

    val = FP0_to_floatx80(env);
    res = logl(floatx80_to_ldouble(val));

    floatx80_to_FP0(env, ldouble_to_floatx80(res));
}

void HELPER(log10_FP0)(CPUM68KState *env)
{
    floatx80 val;
    long double res;

    val = FP0_to_floatx80(env);
    res = log10l(floatx80_to_ldouble(val));

    floatx80_to_FP0(env, ldouble_to_floatx80(res));
}

void HELPER(atan_FP0)(CPUM68KState *env)
{
    floatx80 res;
    long double val;

    val = floatx80_to_ldouble(FP0_to_floatx80(env));

    val = atanl(val);
    res = ldouble_to_floatx80(val);
    floatx80_to_FP0(env, res);
}

void HELPER(asin_FP0)(CPUM68KState *env)
{
    floatx80 res;
    long double val;

    val = floatx80_to_ldouble(FP0_to_floatx80(env));

    val = asinl(val);
    res = ldouble_to_floatx80(val);
    floatx80_to_FP0(env, res);
}

void HELPER(atanh_FP0)(CPUM68KState *env)
{
    floatx80 res;
    long double val;

    val = floatx80_to_ldouble(FP0_to_floatx80(env));

    val = atanhl(val);
    res = ldouble_to_floatx80(val);
    floatx80_to_FP0(env, res);
}

void HELPER(sin_FP0)(CPUM68KState *env)
{
    floatx80 res;
    long double val;

    val = floatx80_to_ldouble(FP0_to_floatx80(env));

    val = sinl(val);
    res = ldouble_to_floatx80(val);
    floatx80_to_FP0(env, res);
}

void HELPER(tanh_FP0)(CPUM68KState *env)
{
    floatx80 res;
    long double val;

    val = floatx80_to_ldouble(FP0_to_floatx80(env));

    val = tanhl(val);
    res = ldouble_to_floatx80(val);
    floatx80_to_FP0(env, res);
}

void HELPER(tan_FP0)(CPUM68KState *env)
{
    floatx80 res;
    long double val;

    val = floatx80_to_ldouble(FP0_to_floatx80(env));

    val = tanl(val);
    res = ldouble_to_floatx80(val);
    floatx80_to_FP0(env, res);
}

void HELPER(exp_FP0)(CPUM68KState *env)
{
    floatx80 f;
    long double res;

    f = FP0_to_floatx80(env);

    res = expl(floatx80_to_ldouble(f));

    floatx80_to_FP0(env, ldouble_to_floatx80(res));
}

void HELPER(exp2_FP0)(CPUM68KState *env)
{
    floatx80 f;
    long double res;

    f = FP0_to_floatx80(env);

    res = exp2l(floatx80_to_ldouble(f));

    floatx80_to_FP0(env, ldouble_to_floatx80(res));
}

void HELPER(exp10_FP0)(CPUM68KState *env)
{
    floatx80 res;
    long double val;

    val = floatx80_to_ldouble(FP0_to_floatx80(env));

    val = exp10l(val);
    res = ldouble_to_floatx80(val);
    floatx80_to_FP0(env, res);
}

void HELPER(cosh_FP0)(CPUM68KState *env)
{
    floatx80 res;
    long double val;

    val = floatx80_to_ldouble(FP0_to_floatx80(env));

    val = coshl(val);
    res = ldouble_to_floatx80(val);
    floatx80_to_FP0(env, res);
}

void HELPER(acos_FP0)(CPUM68KState *env)
{
    floatx80 res;
    long double val;

    val = floatx80_to_ldouble(FP0_to_floatx80(env));

    val = acosl(val);
    res = ldouble_to_floatx80(val);
    floatx80_to_FP0(env, res);
}

void HELPER(cos_FP0)(CPUM68KState *env)
{
    floatx80 res;
    long double val;

    val = floatx80_to_ldouble(FP0_to_floatx80(env));

    val = cosl(val);
    res = ldouble_to_floatx80(val);
    floatx80_to_FP0(env, res);
}

void HELPER(getexp_FP0)(CPUM68KState *env)
{
    int32_t exp;
    floatx80 res;

    exp = (env->fp0.l.upper & 0x7fff) - 0x3fff;

    res = int32_to_floatx80(exp, &env->fp_status);

    floatx80_to_FP0(env, res);
}

void HELPER(scale_FP0_FP1)(CPUM68KState *env)
{
    int32_t scale;
    int32_t exp;

    scale = floatx80_to_int32(FP0_to_floatx80(env), &env->fp_status);

    exp = (env->fp1.l.upper & 0x7fff) + scale;

    env->fp0.l.upper = (env->fp1.l.upper & 0x8000) | (exp & 0x7fff);
    env->fp0.l.lower = env->fp1.l.lower;
}

void HELPER(mod_FP0_FP1)(CPUM68KState *env)
{
    floatx80 res;
    long double src, dst;

    src = floatx80_to_ldouble(FP0_to_floatx80(env));
    dst = floatx80_to_ldouble(FP1_to_floatx80(env));

    dst = fmodl(dst, src);

    res = ldouble_to_floatx80(dst);
    floatx80_to_FP0(env, res);
}

void HELPER(sincos_FP0_FP1)(CPUM68KState *env)
{
    floatx80 res;
    long double val, valsin, valcos;

    val = floatx80_to_ldouble(FP0_to_floatx80(env));

    sincosl(val, &valsin, &valcos);
    res = ldouble_to_floatx80(valsin);
    floatx80_to_FP0(env, res);
    res = ldouble_to_floatx80(valcos);
    floatx80_to_FP1(env, res);
}

void HELPER(update_fpsr)(CPUM68KState *env)
{
    uint32_t fpcc = 0;
    floatx80 val = FP0_to_floatx80(env);

    if (floatx80_is_any_nan(val)) {
        fpcc |= FCCF_A;
    }
    if (floatx80_is_infinity(val)) {
        fpcc |= FCCF_I;
    }
    if (floatx80_is_neg(val)) {
        fpcc |= FCCF_N;
    }
    if (floatx80_is_zero(val)) {
        fpcc |= FCCF_Z;
    }

    env->fpsr = (env->fpsr & ~FCCF_MASK) | fpcc;
}

void HELPER(fmovem)(CPUM68KState *env, uint32_t opsize,
                    uint32_t mode, uint32_t mask)
{
    fprintf(stderr, "MISSING HELPER fmovem\n");
}
