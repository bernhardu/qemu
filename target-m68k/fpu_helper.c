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
