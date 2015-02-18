/*
 * OpenRISC translation
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
 *                         Feng Gao <gf91597@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "disas/disas.h"
#include "tcg-op.h"
#include "qemu-common.h"
#include "qemu/log.h"
#include "qemu/bitops.h"
#include "exec/cpu_ldst.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#include "trace-tcg.h"
#include "exec/log.h"

/* Set to 0 to completely disable.  */
#define OPENRISC_DISAS  CPU_LOG_TB_IN_ASM
#define LOG_DIS(...) qemu_log_mask(OPENRISC_DISAS, ## __VA_ARGS__)

typedef struct DisasContext {
    TranslationBlock *tb;
    target_ulong pc, ppc, npc;
    uint32_t tb_flags, synced_flags, flags;
    uint32_t is_jmp;
    uint32_t mem_idx;
    int singlestep_enabled;
    uint32_t delayed_branch;
} DisasContext;

static TCGv_env cpu_env;
static TCGv cpu_sr;
static TCGv cpu_R[32];
static TCGv cpu_pc;
static TCGv jmp_pc;            /* l.jr/l.jalr temp pc */
static TCGv cpu_npc;
static TCGv cpu_ppc;
static TCGv cpu_sr_f;           /* bf/bnf, F flag taken */
static TCGv_i32 fpcsr;
static TCGv machi, maclo;
static TCGv fpmaddhi, fpmaddlo;
static TCGv_i32 env_flags;
#include "exec/gen-icount.h"

void openrisc_translate_init(void)
{
    static const char * const regnames[] = {
        "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
        "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
        "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
    };
    int i;

    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");
    cpu_sr = tcg_global_mem_new(cpu_env,
                                offsetof(CPUOpenRISCState, sr), "sr");
    env_flags = tcg_global_mem_new_i32(cpu_env,
                                       offsetof(CPUOpenRISCState, flags),
                                       "flags");
    cpu_pc = tcg_global_mem_new(cpu_env,
                                offsetof(CPUOpenRISCState, pc), "pc");
    cpu_npc = tcg_global_mem_new(cpu_env,
                                 offsetof(CPUOpenRISCState, npc), "npc");
    cpu_ppc = tcg_global_mem_new(cpu_env,
                                 offsetof(CPUOpenRISCState, ppc), "ppc");
    jmp_pc = tcg_global_mem_new(cpu_env,
                                offsetof(CPUOpenRISCState, jmp_pc), "jmp_pc");
    cpu_sr_f = tcg_global_mem_new(cpu_env,
                                 offsetof(CPUOpenRISCState, sr_f), "sr_f");
    fpcsr = tcg_global_mem_new_i32(cpu_env,
                                   offsetof(CPUOpenRISCState, fpcsr),
                                   "fpcsr");
    machi = tcg_global_mem_new(cpu_env,
                               offsetof(CPUOpenRISCState, machi),
                               "machi");
    maclo = tcg_global_mem_new(cpu_env,
                               offsetof(CPUOpenRISCState, maclo),
                               "maclo");
    fpmaddhi = tcg_global_mem_new(cpu_env,
                                  offsetof(CPUOpenRISCState, fpmaddhi),
                                  "fpmaddhi");
    fpmaddlo = tcg_global_mem_new(cpu_env,
                                  offsetof(CPUOpenRISCState, fpmaddlo),
                                  "fpmaddlo");
    for (i = 0; i < 32; i++) {
        cpu_R[i] = tcg_global_mem_new(cpu_env,
                                      offsetof(CPUOpenRISCState, gpr[i]),
                                      regnames[i]);
    }
}

static inline int zero_extend(unsigned int val, int width)
{
    return val & ((1 << width) - 1);
}

static inline int sign_extend(unsigned int val, int width)
{
    int sval;

    /* LSL */
    val <<= TARGET_LONG_BITS - width;
    sval = val;
    /* ASR.  */
    sval >>= TARGET_LONG_BITS - width;
    return sval;
}

static inline void gen_sync_flags(DisasContext *dc)
{
    /* Sync the tb dependent flag between translate and runtime.  */
    if (dc->tb_flags != dc->synced_flags) {
        tcg_gen_movi_tl(env_flags, dc->tb_flags);
        dc->synced_flags = dc->tb_flags;
    }
}

static void gen_exception(DisasContext *dc, unsigned int excp)
{
    TCGv_i32 tmp = tcg_const_i32(excp);
    gen_helper_exception(cpu_env, tmp);
    tcg_temp_free_i32(tmp);
}

static void gen_illegal_exception(DisasContext *dc)
{
    tcg_gen_movi_tl(cpu_pc, dc->pc);
    gen_exception(dc, EXCP_ILLEGAL);
    dc->is_jmp = DISAS_UPDATE;
}

/* not used yet, open it when we need or64.  */
/*#ifdef TARGET_OPENRISC64
static void check_ob64s(DisasContext *dc)
{
    if (!(dc->flags & CPUCFGR_OB64S)) {
        gen_illegal_exception(dc);
    }
}

static void check_of64s(DisasContext *dc)
{
    if (!(dc->flags & CPUCFGR_OF64S)) {
        gen_illegal_exception(dc);
    }
}

static void check_ov64s(DisasContext *dc)
{
    if (!(dc->flags & CPUCFGR_OV64S)) {
        gen_illegal_exception(dc);
    }
}
#endif*/

static inline bool use_goto_tb(DisasContext *dc, target_ulong dest)
{
    if (unlikely(dc->singlestep_enabled)) {
        return false;
    }

#ifndef CONFIG_USER_ONLY
    return (dc->tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK);
#else
    return true;
#endif
}

static void gen_goto_tb(DisasContext *dc, int n, target_ulong dest)
{
    if (use_goto_tb(dc, dest)) {
        tcg_gen_movi_tl(cpu_pc, dest);
        tcg_gen_goto_tb(n);
        tcg_gen_exit_tb((uintptr_t)dc->tb + n);
    } else {
        tcg_gen_movi_tl(cpu_pc, dest);
        if (dc->singlestep_enabled) {
            gen_exception(dc, EXCP_DEBUG);
        }
        tcg_gen_exit_tb(0);
    }
}

static void gen_jump(DisasContext *dc, uint32_t imm, uint32_t reg, uint32_t op0)
{
    target_ulong tmp_pc;
    /* N26, 26bits imm */
    tmp_pc = sign_extend((imm<<2), 26) + dc->pc;

    switch (op0) {
    case 0x00:     /* l.j */
        tcg_gen_movi_tl(jmp_pc, tmp_pc);
        break;
    case 0x01:     /* l.jal */
        tcg_gen_movi_tl(cpu_R[9], (dc->pc + 8));
        tcg_gen_movi_tl(jmp_pc, tmp_pc);
        break;
    case 0x03:     /* l.bnf */
    case 0x04:     /* l.bf  */
        {
            TCGv t_next = tcg_const_tl(dc->pc + 8);
            TCGv t_true = tcg_const_tl(tmp_pc);
            TCGv t_zero = tcg_const_tl(0);

            tcg_gen_movcond_tl(op0 == 0x03 ? TCG_COND_EQ : TCG_COND_NE,
                               jmp_pc, cpu_sr_f, t_zero, t_true, t_next);

            tcg_temp_free(t_next);
            tcg_temp_free(t_true);
            tcg_temp_free(t_zero);
        }
        break;
    case 0x11:     /* l.jr */
        tcg_gen_mov_tl(jmp_pc, cpu_R[reg]);
        break;
    case 0x12:     /* l.jalr */
        tcg_gen_movi_tl(cpu_R[9], (dc->pc + 8));
        tcg_gen_mov_tl(jmp_pc, cpu_R[reg]);
        break;
    default:
        gen_illegal_exception(dc);
        break;
    }

    dc->delayed_branch = 2;
    dc->tb_flags |= D_FLAG;
    gen_sync_flags(dc);
}

static void gen_ove_cy(DisasContext *dc, TCGv cy)
{
    gen_helper_ove(cpu_env, cy);
}

static void gen_ove_ov(DisasContext *dc, TCGv ov)
{
    gen_helper_ove(cpu_env, ov);
}

static void gen_ove_cyov(DisasContext *dc, TCGv cy, TCGv ov)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_or_tl(t0, cy, ov);
    gen_helper_ove(cpu_env, t0);
    tcg_temp_free(t0);
}

static void gen_add(DisasContext *dc, TCGv dest, TCGv srca, TCGv srcb)
{
    TCGv t0 = tcg_const_tl(0);
    TCGv res = tcg_temp_new();
    TCGv sr_cy = tcg_temp_new();
    TCGv sr_ov = tcg_temp_new();

    tcg_gen_add2_tl(res, sr_cy, srca, t0, srcb, t0);
    tcg_gen_xor_tl(sr_ov, srca, srcb);
    tcg_gen_xor_tl(t0, res, srcb);
    tcg_gen_andc_tl(sr_ov, t0, sr_ov);
    tcg_temp_free(t0);

    tcg_gen_mov_tl(dest, res);
    tcg_temp_free(res);

    tcg_gen_shri_tl(sr_ov, sr_ov, TARGET_LONG_BITS - 1);
    tcg_gen_deposit_tl(cpu_sr, cpu_sr, sr_cy, ctz32(SR_CY), 1);
    tcg_gen_deposit_tl(cpu_sr, cpu_sr, sr_ov, ctz32(SR_OV), 1);

    gen_ove_cyov(dc, sr_ov, sr_cy);
    tcg_temp_free(sr_ov);
    tcg_temp_free(sr_cy);
}

static void gen_addc(DisasContext *dc, TCGv dest, TCGv srca, TCGv srcb)
{
    TCGv t0 = tcg_const_tl(0);
    TCGv res = tcg_temp_new();
    TCGv sr_cy = tcg_temp_new();
    TCGv sr_ov = tcg_temp_new();

    tcg_gen_shri_tl(sr_cy, cpu_sr, ctz32(SR_CY));
    tcg_gen_andi_tl(sr_cy, sr_cy, 1);

    tcg_gen_add2_tl(res, sr_cy, srca, t0, sr_cy, t0);
    tcg_gen_add2_tl(res, sr_cy, res, sr_cy, srcb, t0);
    tcg_gen_xor_tl(sr_ov, srca, srcb);
    tcg_gen_xor_tl(t0, res, srcb);
    tcg_gen_andc_tl(sr_ov, t0, sr_ov);
    tcg_temp_free(t0);

    tcg_gen_mov_tl(dest, res);
    tcg_temp_free(res);

    tcg_gen_shri_tl(sr_ov, sr_ov, TARGET_LONG_BITS - 1);
    tcg_gen_deposit_tl(cpu_sr, cpu_sr, sr_cy, ctz32(SR_CY), 1);
    tcg_gen_deposit_tl(cpu_sr, cpu_sr, sr_ov, ctz32(SR_OV), 1);

    gen_ove_cyov(dc, sr_ov, sr_cy);
    tcg_temp_free(sr_ov);
    tcg_temp_free(sr_cy);
}

static void gen_sub(DisasContext *dc, TCGv dest, TCGv srca, TCGv srcb)
{
    TCGv res = tcg_temp_new();
    TCGv sr_cy = tcg_temp_new();
    TCGv sr_ov = tcg_temp_new();

    tcg_gen_sub_tl(res, srca, srcb);
    tcg_gen_xor_tl(sr_cy, srca, srcb);
    tcg_gen_xor_tl(sr_ov, res, srcb);
    tcg_gen_and_tl(sr_ov, sr_ov, sr_cy);
    tcg_gen_setcond_tl(TCG_COND_LTU, sr_cy, srca, srcb);

    tcg_gen_mov_tl(dest, res);
    tcg_temp_free(res);

    tcg_gen_shri_tl(sr_ov, sr_ov, TARGET_LONG_BITS - 1);
    tcg_gen_deposit_tl(cpu_sr, cpu_sr, sr_cy, ctz32(SR_CY), 1);
    tcg_gen_deposit_tl(cpu_sr, cpu_sr, sr_ov, ctz32(SR_OV), 1);

    gen_ove_cyov(dc, sr_ov, sr_cy);
    tcg_temp_free(sr_ov);
    tcg_temp_free(sr_cy);
}

static void gen_mul(DisasContext *dc, TCGv dest, TCGv srca, TCGv srcb)
{
    TCGv sr_ov = tcg_temp_new();
    TCGv t0 = tcg_temp_new();

    tcg_gen_muls2_tl(dest, sr_ov, srca, srcb);
    tcg_gen_sari_tl(t0, dest, TARGET_LONG_BITS - 1);
    tcg_gen_setcond_tl(TCG_COND_NE, sr_ov, sr_ov, t0);
    tcg_temp_free(t0);

    tcg_gen_deposit_tl(cpu_sr, cpu_sr, sr_ov, ctz32(SR_OV), 1);

    gen_ove_ov(dc, sr_ov);
    tcg_temp_free(sr_ov);
}

static void gen_mulu(DisasContext *dc, TCGv dest, TCGv srca, TCGv srcb)
{
    TCGv sr_cy = tcg_temp_new();

    tcg_gen_muls2_tl(dest, sr_cy, srca, srcb);
    tcg_gen_setcondi_tl(TCG_COND_NE, sr_cy, sr_cy, 0);

    tcg_gen_deposit_tl(cpu_sr, cpu_sr, sr_cy, ctz32(SR_CY), 1);

    gen_ove_cy(dc, sr_cy);
    tcg_temp_free(sr_cy);
}

static void gen_div(DisasContext *dc, TCGv dest, TCGv srca, TCGv srcb)
{
    TCGv sr_ov = tcg_temp_new();
    TCGv t0 = tcg_temp_new();

    tcg_gen_setcondi_tl(TCG_COND_EQ, sr_ov, srcb, 0);
    /* The result of divide-by-zero is undefined.
       Supress the host-side exception by dividing by 1.  */
    tcg_gen_or_tl(t0, srcb, sr_ov);
    tcg_gen_div_tl(dest, srca, t0);
    tcg_temp_free(t0);

    tcg_gen_deposit_tl(cpu_sr, cpu_sr, sr_ov, ctz32(SR_OV), 1);

    gen_ove_ov(dc, sr_ov);
    tcg_temp_free(sr_ov);
}

static void gen_divu(DisasContext *dc, TCGv dest, TCGv srca, TCGv srcb)
{
    TCGv sr_cy = tcg_temp_new();
    TCGv t0 = tcg_temp_new();

    tcg_gen_setcondi_tl(TCG_COND_EQ, sr_cy, srcb, 0);
    /* The result of divide-by-zero is undefined.
       Supress the host-side exception by dividing by 1.  */
    tcg_gen_or_tl(t0, srcb, sr_cy);
    tcg_gen_divu_tl(dest, srca, t0);
    tcg_temp_free(t0);

    tcg_gen_deposit_tl(cpu_sr, cpu_sr, sr_cy, ctz32(SR_CY), 1);

    gen_ove_cy(dc, sr_cy);
    tcg_temp_free(sr_cy);
}

static void dec_calc(DisasContext *dc, uint32_t insn)
{
    uint32_t op0, op1, op2;
    uint32_t ra, rb, rd;
    op0 = extract32(insn, 0, 4);
    op1 = extract32(insn, 8, 2);
    op2 = extract32(insn, 6, 2);
    ra = extract32(insn, 16, 5);
    rb = extract32(insn, 11, 5);
    rd = extract32(insn, 21, 5);

    switch (op1) {
    case 0:
        switch (op0) {
        case 0x0: /* l.add */
            LOG_DIS("l.add r%d, r%d, r%d\n", rd, ra, rb);
            gen_add(dc, cpu_R[rd], cpu_R[ra], cpu_R[rb]);
            return;

        case 0x1: /* l.addc */
            LOG_DIS("l.addc r%d, r%d, r%d\n", rd, ra, rb);
            gen_addc(dc, cpu_R[rd], cpu_R[ra], cpu_R[rb]);
            return;

        case 0x2: /* l.sub */
            LOG_DIS("l.sub r%d, r%d, r%d\n", rd, ra, rb);
            gen_sub(dc, cpu_R[rd], cpu_R[ra], cpu_R[rb]);
            return;

        case 0x3: /* l.and */
            LOG_DIS("l.and r%d, r%d, r%d\n", rd, ra, rb);
            tcg_gen_and_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
            return;

        case 0x4: /* l.or */
            LOG_DIS("l.or r%d, r%d, r%d\n", rd, ra, rb);
            tcg_gen_or_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
            return;

        case 0x5: /* l.xor */
            LOG_DIS("l.xor r%d, r%d, r%d\n", rd, ra, rb);
            tcg_gen_xor_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
            return;

        case 0x8:
            switch (op2) {
            case 0: /* l.sll */
                LOG_DIS("l.sll r%d, r%d, r%d\n", rd, ra, rb);
                tcg_gen_shl_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
                return;
            case 1: /* l.srl */
                LOG_DIS("l.srl r%d, r%d, r%d\n", rd, ra, rb);
                tcg_gen_shr_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
                return;
            case 2: /* l.sra */
                LOG_DIS("l.sra r%d, r%d, r%d\n", rd, ra, rb);
                tcg_gen_sar_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
                return;
            case 3: /* l.ror */
                LOG_DIS("l.ror r%d, r%d, r%d\n", rd, ra, rb);
                tcg_gen_rotr_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
                return;
            }
            break;

        case 0xc:
            switch (op2) {
            case 0: /* l.exths */
                LOG_DIS("l.exths r%d, r%d\n", rd, ra);
                tcg_gen_ext16s_tl(cpu_R[rd], cpu_R[ra]);
                return;
            case 1: /* l.extbs */
                LOG_DIS("l.extbs r%d, r%d\n", rd, ra);
                tcg_gen_ext8s_tl(cpu_R[rd], cpu_R[ra]);
                return;
            case 2: /* l.exthz */
                LOG_DIS("l.exthz r%d, r%d\n", rd, ra);
                tcg_gen_ext16u_tl(cpu_R[rd], cpu_R[ra]);
                return;
            case 3: /* l.extbz */
                LOG_DIS("l.extbz r%d, r%d\n", rd, ra);
                tcg_gen_ext8u_tl(cpu_R[rd], cpu_R[ra]);
                return;
            }
            break;

        case 0xd:
            switch (op2) {
            case 0: /* l.extws */
                LOG_DIS("l.extws r%d, r%d\n", rd, ra);
                tcg_gen_ext32s_tl(cpu_R[rd], cpu_R[ra]);
                return;
            case 1: /* l.extwz */
                LOG_DIS("l.extwz r%d, r%d\n", rd, ra);
                tcg_gen_ext32u_tl(cpu_R[rd], cpu_R[ra]);
                return;
            }
            break;

        case 0xe: /* l.cmov */
            LOG_DIS("l.cmov r%d, r%d, r%d\n", rd, ra, rb);
            {
                TCGv zero = tcg_const_tl(0);
                tcg_gen_movcond_tl(TCG_COND_NE, cpu_R[rd], cpu_sr_f, zero,
                                   cpu_R[ra], cpu_R[rb]);
                tcg_temp_free(zero);
            }
            return;

        case 0xf: /* l.ff1 */
            LOG_DIS("l.ff1 r%d, r%d, r%d\n", rd, ra, rb);
            gen_helper_ff1(cpu_R[rd], cpu_R[ra]);
            return;
        }
        break;

    case 1:
        switch (op0) {
        case 0xf: /* l.fl1 */
            LOG_DIS("l.fl1 r%d, r%d, r%d\n", rd, ra, rb);
            gen_helper_fl1(cpu_R[rd], cpu_R[ra]);
            return;
        }
        break;

    case 2:
        break;

    case 3:
        switch (op0) {
        case 0x6: /* l.mul */
            LOG_DIS("l.mul r%d, r%d, r%d\n", rd, ra, rb);
            gen_mul(dc, cpu_R[rd], cpu_R[ra], cpu_R[rb]);
            return;

        case 0x9: /* l.div */
            LOG_DIS("l.div r%d, r%d, r%d\n", rd, ra, rb);
            gen_div(dc, cpu_R[rd], cpu_R[ra], cpu_R[rb]);
            return;

        case 0xa: /* l.divu */
            LOG_DIS("l.divu r%d, r%d, r%d\n", rd, ra, rb);
            gen_divu(dc, cpu_R[rd], cpu_R[ra], cpu_R[rb]);
            return;

        case 0xb: /* l.mulu */
            LOG_DIS("l.mulu r%d, r%d, r%d\n", rd, ra, rb);
            gen_mulu(dc, cpu_R[rd], cpu_R[ra], cpu_R[rb]);
            return;
        }
        break;
    }
    gen_illegal_exception(dc);
}

static void dec_misc(DisasContext *dc, uint32_t insn)
{
    uint32_t op0, op1;
    uint32_t ra, rb, rd;
    uint32_t L6, K5;
    uint32_t I16, I5, I11, N26, tmp;
    TCGMemOp mop;
    TCGv t0;

    op0 = extract32(insn, 26, 6);
    op1 = extract32(insn, 24, 2);
    ra = extract32(insn, 16, 5);
    rb = extract32(insn, 11, 5);
    rd = extract32(insn, 21, 5);
    L6 = extract32(insn, 5, 6);
    K5 = extract32(insn, 0, 5);
    I16 = extract32(insn, 0, 16);
    I5 = extract32(insn, 21, 5);
    I11 = extract32(insn, 0, 11);
    N26 = extract32(insn, 0, 26);
    tmp = (I5<<11) + I11;

    switch (op0) {
    case 0x00:    /* l.j */
        LOG_DIS("l.j %d\n", N26);
        gen_jump(dc, N26, 0, op0);
        break;

    case 0x01:    /* l.jal */
        LOG_DIS("l.jal %d\n", N26);
        gen_jump(dc, N26, 0, op0);
        break;

    case 0x03:    /* l.bnf */
        LOG_DIS("l.bnf %d\n", N26);
        gen_jump(dc, N26, 0, op0);
        break;

    case 0x04:    /* l.bf */
        LOG_DIS("l.bf %d\n", N26);
        gen_jump(dc, N26, 0, op0);
        break;

    case 0x05:
        switch (op1) {
        case 0x01:    /* l.nop */
            LOG_DIS("l.nop %d\n", I16);
            break;

        default:
            gen_illegal_exception(dc);
            break;
        }
        break;

    case 0x11:    /* l.jr */
        LOG_DIS("l.jr r%d\n", rb);
         gen_jump(dc, 0, rb, op0);
         break;

    case 0x12:    /* l.jalr */
        LOG_DIS("l.jalr r%d\n", rb);
        gen_jump(dc, 0, rb, op0);
        break;

    case 0x13:    /* l.maci */
        LOG_DIS("l.maci %d, r%d, %d\n", I5, ra, I11);
        {
            TCGv_i64 t1 = tcg_temp_new_i64();
            TCGv_i64 t2 = tcg_temp_new_i64();
            TCGv_i32 dst = tcg_temp_new_i32();
            TCGv ttmp = tcg_const_tl(tmp);
            tcg_gen_mul_tl(dst, cpu_R[ra], ttmp);
            tcg_gen_ext_i32_i64(t1, dst);
            tcg_gen_concat_i32_i64(t2, maclo, machi);
            tcg_gen_add_i64(t2, t2, t1);
            tcg_gen_extrl_i64_i32(maclo, t2);
            tcg_gen_shri_i64(t2, t2, 32);
            tcg_gen_extrl_i64_i32(machi, t2);
            tcg_temp_free_i32(dst);
            tcg_temp_free(ttmp);
            tcg_temp_free_i64(t1);
            tcg_temp_free_i64(t2);
        }
        break;

    case 0x09:    /* l.rfe */
        LOG_DIS("l.rfe\n");
        {
#if defined(CONFIG_USER_ONLY)
            return;
#else
            if (dc->mem_idx == MMU_USER_IDX) {
                gen_illegal_exception(dc);
                return;
            }
            gen_helper_rfe(cpu_env);
            dc->is_jmp = DISAS_UPDATE;
#endif
        }
        break;

    case 0x1c:    /* l.cust1 */
        LOG_DIS("l.cust1\n");
        break;

    case 0x1d:    /* l.cust2 */
        LOG_DIS("l.cust2\n");
        break;

    case 0x1e:    /* l.cust3 */
        LOG_DIS("l.cust3\n");
        break;

    case 0x1f:    /* l.cust4 */
        LOG_DIS("l.cust4\n");
        break;

    case 0x3c:    /* l.cust5 */
        LOG_DIS("l.cust5 r%d, r%d, r%d, %d, %d\n", rd, ra, rb, L6, K5);
        break;

    case 0x3d:    /* l.cust6 */
        LOG_DIS("l.cust6\n");
        break;

    case 0x3e:    /* l.cust7 */
        LOG_DIS("l.cust7\n");
        break;

    case 0x3f:    /* l.cust8 */
        LOG_DIS("l.cust8\n");
        break;

/* not used yet, open it when we need or64.  */
/*#ifdef TARGET_OPENRISC64
    case 0x20:     l.ld
        LOG_DIS("l.ld r%d, r%d, %d\n", rd, ra, I16);
        check_ob64s(dc);
        mop = MO_TEQ;
        goto do_load;
#endif*/

    case 0x21:    /* l.lwz */
        LOG_DIS("l.lwz r%d, r%d, %d\n", rd, ra, I16);
        mop = MO_TEUL;
        goto do_load;

    case 0x22:    /* l.lws */
        LOG_DIS("l.lws r%d, r%d, %d\n", rd, ra, I16);
        mop = MO_TESL;
        goto do_load;

    case 0x23:    /* l.lbz */
        LOG_DIS("l.lbz r%d, r%d, %d\n", rd, ra, I16);
        mop = MO_UB;
        goto do_load;

    case 0x24:    /* l.lbs */
        LOG_DIS("l.lbs r%d, r%d, %d\n", rd, ra, I16);
        mop = MO_SB;
        goto do_load;

    case 0x25:    /* l.lhz */
        LOG_DIS("l.lhz r%d, r%d, %d\n", rd, ra, I16);
        mop = MO_TEUW;
        goto do_load;

    case 0x26:    /* l.lhs */
        LOG_DIS("l.lhs r%d, r%d, %d\n", rd, ra, I16);
        mop = MO_TESW;
        goto do_load;

    do_load:
        {
            TCGv t0 = tcg_temp_new();
            tcg_gen_addi_tl(t0, cpu_R[ra], sign_extend(I16, 16));
            tcg_gen_qemu_ld_tl(cpu_R[rd], t0, dc->mem_idx, mop);
            tcg_temp_free(t0);
        }
        break;

    case 0x27:    /* l.addi */
        LOG_DIS("l.addi r%d, r%d, %d\n", rd, ra, I16);
        t0 = tcg_const_tl(I16);
        gen_add(dc, cpu_R[rd], cpu_R[ra], t0);
        tcg_temp_free(t0);
        break;

    case 0x28:    /* l.addic */
        LOG_DIS("l.addic r%d, r%d, %d\n", rd, ra, I16);
        t0 = tcg_const_tl(I16);
        gen_addc(dc, cpu_R[rd], cpu_R[ra], t0);
        tcg_temp_free(t0);
        break;

    case 0x29:    /* l.andi */
        LOG_DIS("l.andi r%d, r%d, %d\n", rd, ra, I16);
        tcg_gen_andi_tl(cpu_R[rd], cpu_R[ra], zero_extend(I16, 16));
        break;

    case 0x2a:    /* l.ori */
        LOG_DIS("l.ori r%d, r%d, %d\n", rd, ra, I16);
        tcg_gen_ori_tl(cpu_R[rd], cpu_R[ra], zero_extend(I16, 16));
        break;

    case 0x2b:    /* l.xori */
        LOG_DIS("l.xori r%d, r%d, %d\n", rd, ra, I16);
        tcg_gen_xori_tl(cpu_R[rd], cpu_R[ra], sign_extend(I16, 16));
        break;

    case 0x2c:    /* l.muli */
        LOG_DIS("l.muli r%d, r%d, %d\n", rd, ra, I16);
        t0 = tcg_const_tl(I16);
        gen_mul(dc, cpu_R[rd], cpu_R[ra], t0);
        tcg_temp_free(t0);
        break;

    case 0x2d:    /* l.mfspr */
        LOG_DIS("l.mfspr r%d, r%d, %d\n", rd, ra, I16);
        {
#if defined(CONFIG_USER_ONLY)
            return;
#else
            TCGv_i32 ti = tcg_const_i32(I16);
            if (dc->mem_idx == MMU_USER_IDX) {
                gen_illegal_exception(dc);
                return;
            }
            gen_helper_mfspr(cpu_R[rd], cpu_env, cpu_R[rd], cpu_R[ra], ti);
            tcg_temp_free_i32(ti);
#endif
        }
        break;

    case 0x30:    /* l.mtspr */
        LOG_DIS("l.mtspr %d, r%d, r%d, %d\n", I5, ra, rb, I11);
        {
#if defined(CONFIG_USER_ONLY)
            return;
#else
            TCGv_i32 im = tcg_const_i32(tmp);
            if (dc->mem_idx == MMU_USER_IDX) {
                gen_illegal_exception(dc);
                return;
            }
            gen_helper_mtspr(cpu_env, cpu_R[ra], cpu_R[rb], im);
            tcg_temp_free_i32(im);
#endif
        }
        break;

/* not used yet, open it when we need or64.  */
/*#ifdef TARGET_OPENRISC64
    case 0x34:     l.sd
        LOG_DIS("l.sd %d, r%d, r%d, %d\n", I5, ra, rb, I11);
        check_ob64s(dc);
        mop = MO_TEQ;
        goto do_store;
#endif*/

    case 0x35:    /* l.sw */
        LOG_DIS("l.sw %d, r%d, r%d, %d\n", I5, ra, rb, I11);
        mop = MO_TEUL;
        goto do_store;

    case 0x36:    /* l.sb */
        LOG_DIS("l.sb %d, r%d, r%d, %d\n", I5, ra, rb, I11);
        mop = MO_UB;
        goto do_store;

    case 0x37:    /* l.sh */
        LOG_DIS("l.sh %d, r%d, r%d, %d\n", I5, ra, rb, I11);
        mop = MO_TEUW;
        goto do_store;

    do_store:
        {
            TCGv t0 = tcg_temp_new();
            tcg_gen_addi_tl(t0, cpu_R[ra], sign_extend(tmp, 16));
            tcg_gen_qemu_st_tl(cpu_R[rb], t0, dc->mem_idx, mop);
            tcg_temp_free(t0);
        }
        break;

    default:
        gen_illegal_exception(dc);
        break;
    }
}

static void dec_mac(DisasContext *dc, uint32_t insn)
{
    uint32_t op0;
    uint32_t ra, rb;
    op0 = extract32(insn, 0, 4);
    ra = extract32(insn, 16, 5);
    rb = extract32(insn, 11, 5);

    switch (op0) {
    case 0x0001:    /* l.mac */
        LOG_DIS("l.mac r%d, r%d\n", ra, rb);
        {
            TCGv_i32 t0 = tcg_temp_new_i32();
            TCGv_i64 t1 = tcg_temp_new_i64();
            TCGv_i64 t2 = tcg_temp_new_i64();
            tcg_gen_mul_tl(t0, cpu_R[ra], cpu_R[rb]);
            tcg_gen_ext_i32_i64(t1, t0);
            tcg_gen_concat_i32_i64(t2, maclo, machi);
            tcg_gen_add_i64(t2, t2, t1);
            tcg_gen_extrl_i64_i32(maclo, t2);
            tcg_gen_shri_i64(t2, t2, 32);
            tcg_gen_extrl_i64_i32(machi, t2);
            tcg_temp_free_i32(t0);
            tcg_temp_free_i64(t1);
            tcg_temp_free_i64(t2);
        }
        break;

    case 0x0002:    /* l.msb */
        LOG_DIS("l.msb r%d, r%d\n", ra, rb);
        {
            TCGv_i32 t0 = tcg_temp_new_i32();
            TCGv_i64 t1 = tcg_temp_new_i64();
            TCGv_i64 t2 = tcg_temp_new_i64();
            tcg_gen_mul_tl(t0, cpu_R[ra], cpu_R[rb]);
            tcg_gen_ext_i32_i64(t1, t0);
            tcg_gen_concat_i32_i64(t2, maclo, machi);
            tcg_gen_sub_i64(t2, t2, t1);
            tcg_gen_extrl_i64_i32(maclo, t2);
            tcg_gen_shri_i64(t2, t2, 32);
            tcg_gen_extrl_i64_i32(machi, t2);
            tcg_temp_free_i32(t0);
            tcg_temp_free_i64(t1);
            tcg_temp_free_i64(t2);
        }
        break;

    default:
        gen_illegal_exception(dc);
        break;
   }
}

static void dec_logic(DisasContext *dc, uint32_t insn)
{
    uint32_t op0;
    uint32_t rd, ra, L6;
    op0 = extract32(insn, 6, 2);
    rd = extract32(insn, 21, 5);
    ra = extract32(insn, 16, 5);
    L6 = extract32(insn, 0, 6);

    switch (op0) {
    case 0x00:    /* l.slli */
        LOG_DIS("l.slli r%d, r%d, %d\n", rd, ra, L6);
        tcg_gen_shli_tl(cpu_R[rd], cpu_R[ra], (L6 & 0x1f));
        break;

    case 0x01:    /* l.srli */
        LOG_DIS("l.srli r%d, r%d, %d\n", rd, ra, L6);
        tcg_gen_shri_tl(cpu_R[rd], cpu_R[ra], (L6 & 0x1f));
        break;

    case 0x02:    /* l.srai */
        LOG_DIS("l.srai r%d, r%d, %d\n", rd, ra, L6);
        tcg_gen_sari_tl(cpu_R[rd], cpu_R[ra], (L6 & 0x1f)); break;

    case 0x03:    /* l.rori */
        LOG_DIS("l.rori r%d, r%d, %d\n", rd, ra, L6);
        tcg_gen_rotri_tl(cpu_R[rd], cpu_R[ra], (L6 & 0x1f));
        break;

    default:
        gen_illegal_exception(dc);
        break;
    }
}

static void dec_M(DisasContext *dc, uint32_t insn)
{
    uint32_t op0;
    uint32_t rd;
    uint32_t K16;
    op0 = extract32(insn, 16, 1);
    rd = extract32(insn, 21, 5);
    K16 = extract32(insn, 0, 16);

    switch (op0) {
    case 0x0:    /* l.movhi */
        LOG_DIS("l.movhi  r%d, %d\n", rd, K16);
        tcg_gen_movi_tl(cpu_R[rd], (K16 << 16));
        break;

    case 0x1:    /* l.macrc */
        LOG_DIS("l.macrc  r%d\n", rd);
        tcg_gen_mov_tl(cpu_R[rd], maclo);
        tcg_gen_movi_tl(maclo, 0x0);
        tcg_gen_movi_tl(machi, 0x0);
        break;

    default:
        gen_illegal_exception(dc);
        break;
    }
}

static void dec_comp(DisasContext *dc, uint32_t insn)
{
    uint32_t op0;
    uint32_t ra, rb;

    op0 = extract32(insn, 21, 5);
    ra = extract32(insn, 16, 5);
    rb = extract32(insn, 11, 5);

    /* unsigned integers  */
    tcg_gen_ext32u_tl(cpu_R[ra], cpu_R[ra]);
    tcg_gen_ext32u_tl(cpu_R[rb], cpu_R[rb]);

    switch (op0) {
    case 0x0:    /* l.sfeq */
        LOG_DIS("l.sfeq  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_sr_f, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x1:    /* l.sfne */
        LOG_DIS("l.sfne  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_NE, cpu_sr_f, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x2:    /* l.sfgtu */
        LOG_DIS("l.sfgtu  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_GTU, cpu_sr_f, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x3:    /* l.sfgeu */
        LOG_DIS("l.sfgeu  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_GEU, cpu_sr_f, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x4:    /* l.sfltu */
        LOG_DIS("l.sfltu  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_LTU, cpu_sr_f, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x5:    /* l.sfleu */
        LOG_DIS("l.sfleu  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_LEU, cpu_sr_f, cpu_R[ra], cpu_R[rb]);
        break;

    case 0xa:    /* l.sfgts */
        LOG_DIS("l.sfgts  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_GT, cpu_sr_f, cpu_R[ra], cpu_R[rb]);
        break;

    case 0xb:    /* l.sfges */
        LOG_DIS("l.sfges  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_GE, cpu_sr_f, cpu_R[ra], cpu_R[rb]);
        break;

    case 0xc:    /* l.sflts */
        LOG_DIS("l.sflts  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_LT, cpu_sr_f, cpu_R[ra], cpu_R[rb]);
        break;

    case 0xd:    /* l.sfles */
        LOG_DIS("l.sfles  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_LE, cpu_sr_f, cpu_R[ra], cpu_R[rb]);
        break;

    default:
        gen_illegal_exception(dc);
        break;
    }
}

static void dec_compi(DisasContext *dc, uint32_t insn)
{
    uint32_t op0;
    uint32_t ra, I16;

    op0 = extract32(insn, 21, 5);
    ra = extract32(insn, 16, 5);
    I16 = extract32(insn, 0, 16);

    I16 = sign_extend(I16, 16);

    switch (op0) {
    case 0x0:    /* l.sfeqi */
        LOG_DIS("l.sfeqi  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_sr_f, cpu_R[ra], I16);
        break;

    case 0x1:    /* l.sfnei */
        LOG_DIS("l.sfnei  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_NE, cpu_sr_f, cpu_R[ra], I16);
        break;

    case 0x2:    /* l.sfgtui */
        LOG_DIS("l.sfgtui  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_GTU, cpu_sr_f, cpu_R[ra], I16);
        break;

    case 0x3:    /* l.sfgeui */
        LOG_DIS("l.sfgeui  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_GEU, cpu_sr_f, cpu_R[ra], I16);
        break;

    case 0x4:    /* l.sfltui */
        LOG_DIS("l.sfltui  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_LTU, cpu_sr_f, cpu_R[ra], I16);
        break;

    case 0x5:    /* l.sfleui */
        LOG_DIS("l.sfleui  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_LEU, cpu_sr_f, cpu_R[ra], I16);
        break;

    case 0xa:    /* l.sfgtsi */
        LOG_DIS("l.sfgtsi  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_GT, cpu_sr_f, cpu_R[ra], I16);
        break;

    case 0xb:    /* l.sfgesi */
        LOG_DIS("l.sfgesi  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_GE, cpu_sr_f, cpu_R[ra], I16);
        break;

    case 0xc:    /* l.sfltsi */
        LOG_DIS("l.sfltsi  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_LT, cpu_sr_f, cpu_R[ra], I16);
        break;

    case 0xd:    /* l.sflesi */
        LOG_DIS("l.sflesi  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_LE, cpu_sr_f, cpu_R[ra], I16);
        break;

    default:
        gen_illegal_exception(dc);
        break;
    }
}

static void dec_sys(DisasContext *dc, uint32_t insn)
{
    uint32_t op0;
    uint32_t K16;

    op0 = extract32(insn, 16, 10);
    K16 = extract32(insn, 0, 16);

    switch (op0) {
    case 0x000:    /* l.sys */
        LOG_DIS("l.sys %d\n", K16);
        tcg_gen_movi_tl(cpu_pc, dc->pc);
        gen_exception(dc, EXCP_SYSCALL);
        dc->is_jmp = DISAS_UPDATE;
        break;

    case 0x100:    /* l.trap */
        LOG_DIS("l.trap %d\n", K16);
#if defined(CONFIG_USER_ONLY)
        return;
#else
        if (dc->mem_idx == MMU_USER_IDX) {
            gen_illegal_exception(dc);
            return;
        }
        tcg_gen_movi_tl(cpu_pc, dc->pc);
        gen_exception(dc, EXCP_TRAP);
#endif
        break;

    case 0x300:    /* l.csync */
        LOG_DIS("l.csync\n");
#if defined(CONFIG_USER_ONLY)
        return;
#else
        if (dc->mem_idx == MMU_USER_IDX) {
            gen_illegal_exception(dc);
            return;
        }
#endif
        break;

    case 0x200:    /* l.msync */
        LOG_DIS("l.msync\n");
#if defined(CONFIG_USER_ONLY)
        return;
#else
        if (dc->mem_idx == MMU_USER_IDX) {
            gen_illegal_exception(dc);
            return;
        }
#endif
        break;

    case 0x270:    /* l.psync */
        LOG_DIS("l.psync\n");
#if defined(CONFIG_USER_ONLY)
        return;
#else
        if (dc->mem_idx == MMU_USER_IDX) {
            gen_illegal_exception(dc);
            return;
        }
#endif
        break;

    default:
        gen_illegal_exception(dc);
        break;
    }
}

static void dec_float(DisasContext *dc, uint32_t insn)
{
    uint32_t op0;
    uint32_t ra, rb, rd;
    op0 = extract32(insn, 0, 8);
    ra = extract32(insn, 16, 5);
    rb = extract32(insn, 11, 5);
    rd = extract32(insn, 21, 5);

    switch (op0) {
    case 0x00:    /* lf.add.s */
        LOG_DIS("lf.add.s r%d, r%d, r%d\n", rd, ra, rb);
        gen_helper_float_add_s(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x01:    /* lf.sub.s */
        LOG_DIS("lf.sub.s r%d, r%d, r%d\n", rd, ra, rb);
        gen_helper_float_sub_s(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        break;


    case 0x02:    /* lf.mul.s */
        LOG_DIS("lf.mul.s r%d, r%d, r%d\n", rd, ra, rb);
        if (ra != 0 && rb != 0) {
            gen_helper_float_mul_s(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        } else {
            tcg_gen_ori_tl(fpcsr, fpcsr, FPCSR_ZF);
            tcg_gen_movi_i32(cpu_R[rd], 0x0);
        }
        break;

    case 0x03:    /* lf.div.s */
        LOG_DIS("lf.div.s r%d, r%d, r%d\n", rd, ra, rb);
        gen_helper_float_div_s(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x04:    /* lf.itof.s */
        LOG_DIS("lf.itof r%d, r%d\n", rd, ra);
        gen_helper_itofs(cpu_R[rd], cpu_env, cpu_R[ra]);
        break;

    case 0x05:    /* lf.ftoi.s */
        LOG_DIS("lf.ftoi r%d, r%d\n", rd, ra);
        gen_helper_ftois(cpu_R[rd], cpu_env, cpu_R[ra]);
        break;

    case 0x06:    /* lf.rem.s */
        LOG_DIS("lf.rem.s r%d, r%d, r%d\n", rd, ra, rb);
        gen_helper_float_rem_s(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x07:    /* lf.madd.s */
        LOG_DIS("lf.madd.s r%d, r%d, r%d\n", rd, ra, rb);
        gen_helper_float_muladd_s(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x08:    /* lf.sfeq.s */
        LOG_DIS("lf.sfeq.s r%d, r%d\n", ra, rb);
        gen_helper_float_eq_s(cpu_sr_f, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x09:    /* lf.sfne.s */
        LOG_DIS("lf.sfne.s r%d, r%d\n", ra, rb);
        gen_helper_float_ne_s(cpu_sr_f, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x0a:    /* lf.sfgt.s */
        LOG_DIS("lf.sfgt.s r%d, r%d\n", ra, rb);
        gen_helper_float_gt_s(cpu_sr_f, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x0b:    /* lf.sfge.s */
        LOG_DIS("lf.sfge.s r%d, r%d\n", ra, rb);
        gen_helper_float_ge_s(cpu_sr_f, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x0c:    /* lf.sflt.s */
        LOG_DIS("lf.sflt.s r%d, r%d\n", ra, rb);
        gen_helper_float_lt_s(cpu_sr_f, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x0d:    /* lf.sfle.s */
        LOG_DIS("lf.sfle.s r%d, r%d\n", ra, rb);
        gen_helper_float_le_s(cpu_sr_f, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

/* not used yet, open it when we need or64.  */
/*#ifdef TARGET_OPENRISC64
    case 0x10:     lf.add.d
        LOG_DIS("lf.add.d r%d, r%d, r%d\n", rd, ra, rb);
        check_of64s(dc);
        gen_helper_float_add_d(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x11:     lf.sub.d
        LOG_DIS("lf.sub.d r%d, r%d, r%d\n", rd, ra, rb);
        check_of64s(dc);
        gen_helper_float_sub_d(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x12:     lf.mul.d
        LOG_DIS("lf.mul.d r%d, r%d, r%d\n", rd, ra, rb);
        check_of64s(dc);
        if (ra != 0 && rb != 0) {
            gen_helper_float_mul_d(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        } else {
            tcg_gen_ori_tl(fpcsr, fpcsr, FPCSR_ZF);
            tcg_gen_movi_i64(cpu_R[rd], 0x0);
        }
        break;

    case 0x13:     lf.div.d
        LOG_DIS("lf.div.d r%d, r%d, r%d\n", rd, ra, rb);
        check_of64s(dc);
        gen_helper_float_div_d(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x14:     lf.itof.d
        LOG_DIS("lf.itof r%d, r%d\n", rd, ra);
        check_of64s(dc);
        gen_helper_itofd(cpu_R[rd], cpu_env, cpu_R[ra]);
        break;

    case 0x15:     lf.ftoi.d
        LOG_DIS("lf.ftoi r%d, r%d\n", rd, ra);
        check_of64s(dc);
        gen_helper_ftoid(cpu_R[rd], cpu_env, cpu_R[ra]);
        break;

    case 0x16:     lf.rem.d
        LOG_DIS("lf.rem.d r%d, r%d, r%d\n", rd, ra, rb);
        check_of64s(dc);
        gen_helper_float_rem_d(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x17:     lf.madd.d
        LOG_DIS("lf.madd.d r%d, r%d, r%d\n", rd, ra, rb);
        check_of64s(dc);
        gen_helper_float_muladd_d(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x18:     lf.sfeq.d
        LOG_DIS("lf.sfeq.d r%d, r%d\n", ra, rb);
        check_of64s(dc);
        gen_helper_float_eq_d(cpu_sr_f, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x1a:     lf.sfgt.d
        LOG_DIS("lf.sfgt.d r%d, r%d\n", ra, rb);
        check_of64s(dc);
        gen_helper_float_gt_d(cpu_sr_f, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x1b:     lf.sfge.d
        LOG_DIS("lf.sfge.d r%d, r%d\n", ra, rb);
        check_of64s(dc);
        gen_helper_float_ge_d(cpu_sr_f, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x19:     lf.sfne.d
        LOG_DIS("lf.sfne.d r%d, r%d\n", ra, rb);
        check_of64s(dc);
        gen_helper_float_ne_d(cpu_sr_f, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x1c:     lf.sflt.d
        LOG_DIS("lf.sflt.d r%d, r%d\n", ra, rb);
        check_of64s(dc);
        gen_helper_float_lt_d(cpu_sr_f, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x1d:     lf.sfle.d
        LOG_DIS("lf.sfle.d r%d, r%d\n", ra, rb);
        check_of64s(dc);
        gen_helper_float_le_d(cpu_sr_f, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;
#endif*/

    default:
        gen_illegal_exception(dc);
        break;
    }
}

static void disas_openrisc_insn(DisasContext *dc, OpenRISCCPU *cpu)
{
    uint32_t op0;
    uint32_t insn;
    insn = cpu_ldl_code(&cpu->env, dc->pc);
    op0 = extract32(insn, 26, 6);

    switch (op0) {
    case 0x06:
        dec_M(dc, insn);
        break;

    case 0x08:
        dec_sys(dc, insn);
        break;

    case 0x2e:
        dec_logic(dc, insn);
        break;

    case 0x2f:
        dec_compi(dc, insn);
        break;

    case 0x31:
        dec_mac(dc, insn);
        break;

    case 0x32:
        dec_float(dc, insn);
        break;

    case 0x38:
        dec_calc(dc, insn);
        break;

    case 0x39:
        dec_comp(dc, insn);
        break;

    default:
        dec_misc(dc, insn);
        break;
    }
}

void gen_intermediate_code(CPUOpenRISCState *env, struct TranslationBlock *tb)
{
    OpenRISCCPU *cpu = openrisc_env_get_cpu(env);
    CPUState *cs = CPU(cpu);
    struct DisasContext ctx, *dc = &ctx;
    uint32_t pc_start;
    uint32_t next_page_start;
    int num_insns;
    int max_insns;

    pc_start = tb->pc;
    dc->tb = tb;

    dc->is_jmp = DISAS_NEXT;
    dc->ppc = pc_start;
    dc->pc = pc_start;
    dc->flags = cpu->env.cpucfgr;
    dc->mem_idx = cpu_mmu_index(&cpu->env, false);
    dc->synced_flags = dc->tb_flags = tb->flags;
    dc->delayed_branch = !!(dc->tb_flags & D_FLAG);
    dc->singlestep_enabled = cs->singlestep_enabled;
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)) {
        qemu_log("-----------------------------------------\n");
        log_cpu_state(CPU(cpu), 0);
    }

    next_page_start = (pc_start & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
    num_insns = 0;
    max_insns = tb->cflags & CF_COUNT_MASK;

    if (max_insns == 0) {
        max_insns = CF_COUNT_MASK;
    }
    if (max_insns > TCG_MAX_INSNS) {
        max_insns = TCG_MAX_INSNS;
    }

    gen_tb_start(tb);

    do {
        tcg_gen_insn_start(dc->pc);
        num_insns++;

        if (unlikely(cpu_breakpoint_test(cs, dc->pc, BP_ANY))) {
            tcg_gen_movi_tl(cpu_pc, dc->pc);
            gen_exception(dc, EXCP_DEBUG);
            dc->is_jmp = DISAS_UPDATE;
            /* The address covered by the breakpoint must be included in
               [tb->pc, tb->pc + tb->size) in order to for it to be
               properly cleared -- thus we increment the PC here so that
               the logic setting tb->size below does the right thing.  */
            dc->pc += 4;
            break;
        }

        if (num_insns == max_insns && (tb->cflags & CF_LAST_IO)) {
            gen_io_start();
        }
        dc->ppc = dc->pc - 4;
        dc->npc = dc->pc + 4;
        tcg_gen_movi_tl(cpu_ppc, dc->ppc);
        tcg_gen_movi_tl(cpu_npc, dc->npc);
        disas_openrisc_insn(dc, cpu);
        dc->pc = dc->npc;
        /* delay slot */
        if (dc->delayed_branch) {
            dc->delayed_branch--;
            if (!dc->delayed_branch) {
                dc->tb_flags &= ~D_FLAG;
                gen_sync_flags(dc);
                tcg_gen_mov_tl(cpu_pc, jmp_pc);
                tcg_gen_mov_tl(cpu_npc, jmp_pc);
                tcg_gen_movi_tl(jmp_pc, 0);
                tcg_gen_exit_tb(0);
                dc->is_jmp = DISAS_JUMP;
                break;
            }
        }
    } while (!dc->is_jmp
             && !tcg_op_buf_full()
             && !cs->singlestep_enabled
             && !singlestep
             && (dc->pc < next_page_start)
             && num_insns < max_insns);

    if (tb->cflags & CF_LAST_IO) {
        gen_io_end();
    }
    if (dc->is_jmp == DISAS_NEXT) {
        dc->is_jmp = DISAS_UPDATE;
        tcg_gen_movi_tl(cpu_pc, dc->pc);
    }
    if (unlikely(cs->singlestep_enabled)) {
        if (dc->is_jmp == DISAS_NEXT) {
            tcg_gen_movi_tl(cpu_pc, dc->pc);
        }
        gen_exception(dc, EXCP_DEBUG);
    } else {
        switch (dc->is_jmp) {
        case DISAS_NEXT:
            gen_goto_tb(dc, 0, dc->pc);
            break;
        default:
        case DISAS_JUMP:
            break;
        case DISAS_UPDATE:
            /* indicate that the hash table must be used
               to find the next TB */
            tcg_gen_exit_tb(0);
            break;
        case DISAS_TB_JUMP:
            /* nothing more to generate */
            break;
        }
    }

    gen_tb_end(tb, num_insns);

    tb->size = dc->pc - pc_start;
    tb->icount = num_insns;

#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)
        && qemu_log_in_addr_range(pc_start)) {
        qemu_log("\n");
        log_target_disas(cs, pc_start, dc->pc - pc_start, 0);
        qemu_log("\nisize=%d osize=%d\n",
                 dc->pc - pc_start, tcg_op_buf_count());
    }
#endif
}

void openrisc_cpu_dump_state(CPUState *cs, FILE *f,
                             fprintf_function cpu_fprintf,
                             int flags)
{
    OpenRISCCPU *cpu = OPENRISC_CPU(cs);
    CPUOpenRISCState *env = &cpu->env;
    int i;

    cpu_fprintf(f, "PC=%08x\n", env->pc);
    for (i = 0; i < 32; ++i) {
        cpu_fprintf(f, "R%02d=%08x%c", i, env->gpr[i],
                    (i % 4) == 3 ? '\n' : ' ');
    }
}

void restore_state_to_opc(CPUOpenRISCState *env, TranslationBlock *tb,
                          target_ulong *data)
{
    env->pc = data[0];
}
