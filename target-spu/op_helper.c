/*
 *  Synergistic Processor Unit (SPU) emulation
 *  Opcode helper functions.
 *
 *  Copyright (c) 2011  Richard Henderson
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
#include "qemu/host-utils.h"
#include "fpu/softfloat.h"
#include "exec/helper-proto.h"


void QEMU_NORETURN helper_debug(CPUSPUState *env)
{
    SPUCPU *cpu = spu_env_get_cpu(env);
    CPUState *cs = CPU(cpu);

    cs->exception_index = EXCP_DEBUG;
    env->error_code = 0;
    cpu_loop_exit(cs);
}

uint32_t helper_clz(uint32_t arg)
{
    return clz32(arg);
}

uint32_t helper_cntb(uint32_t val)
{
    /* Like ctpop32, but don't fold the bytes together.  */
    val = (val & 0x55555555) + ((val >> 1) & 0x55555555);
    val = (val & 0x33333333) + ((val >> 2) & 0x33333333);
    val = (val & 0x0f0f0f0f) + ((val >> 4) & 0x0f0f0f0f);
    return val;
}

uint32_t helper_fsmb(uint32_t arg)
{
    uint32_t ret = 0;
    ret |= (arg & 0x8000 ? 0xff000000 : 0);
    ret |= (arg & 0x4000 ? 0x00ff0000 : 0);
    ret |= (arg & 0x2000 ? 0x0000ff00 : 0);
    ret |= (arg & 0x1000 ? 0x000000ff : 0);
    return ret;
}

uint32_t helper_fsmh(uint32_t arg)
{
    uint32_t ret = 0;
    ret |= (arg & 0x80 ? 0xffff0000 : 0);
    ret |= (arg & 0x40 ? 0x0000ffff : 0);
    return ret;
}

static inline uint32_t gbb_1(uint32_t a)
{
    uint32_t ret = a & 1;
    ret |= (a >> (8 - 1)) & 2;
    ret |= (a >> (16 - 2)) & 4;
    ret |= (a >> (24 - 3)) & 8;
    return ret;
}

uint32_t helper_gbb(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t ret = 0;
    ret |= gbb_1(a0) << 12;
    ret |= gbb_1(a1) << 8;
    ret |= gbb_1(a2) << 4;
    ret |= gbb_1(a3) << 0;
    return ret;
}

static inline uint32_t gbh_1(uint32_t a)
{
    return (a & 1) | ((a >> (16 - 1)) & 2);
}

uint32_t helper_gbh(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t ret = 0;
    ret |= gbh_1(a0) << 6;
    ret |= gbh_1(a1) << 4;
    ret |= gbh_1(a2) << 2;
    ret |= gbh_1(a3) << 0;
    return ret;
}

uint32_t helper_gb(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t ret = a3 & 1;
    ret |= (a2 & 1) << 1;
    ret |= (a1 & 1) << 2;
    ret |= (a0 & 1) << 3;
    return ret;
}

uint32_t helper_avgb(uint32_t a, uint32_t b)
{
    uint32_t ret = 0, i;
    for (i = 0; i < 4; ++i) {
        uint32_t ab, bb, rb;

        ab = (a >> (i * 8)) & 0xff;
        bb = (b >> (i * 8)) & 0xff;

        rb = (ab + bb + 1) >> 1;

        ret |= (rb & 0xff) << (i * 8);
    }
    return ret;
}

uint32_t helper_absdb(uint32_t a, uint32_t b)
{
    uint32_t ret = 0, i;
    for (i = 0; i < 4; ++i) {
        uint32_t ab, bb, rb;

        ab = (a >> (i * 8)) & 0xff;
        bb = (b >> (i * 8)) & 0xff;

        rb = (bb > ab ? bb - ab : ab - bb);

        ret |= rb << (i * 8);
    }
    return ret;
}

static inline uint32_t sumb_1(uint32_t val)
{
    return ((val & 0xff)
            + ((val >> 8) & 0xff)
            + ((val >> 16) & 0xff)
            + (val >> 24));
}

uint32_t helper_sumb(uint32_t a, uint32_t b)
{
    return (sumb_1(b) << 16) + sumb_1(a);
}

void tlb_fill(CPUState *cs, target_ulong addr, int is_write,
              int mmu_idx, uintptr_t retaddr)
{
    abort();
}
