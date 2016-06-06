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


/* Note that while the words in the register file are stored in the
   correct memory order, the actual bytes are stored in host memory
   order.  Xor BYTE_SWAP with a byte index into the register file to
   get the bytes in target memory ordering.  */
#ifdef HOST_WORDS_BIGENDIAN
# define REG_BYTE_SWAP 0
#else
# define REG_BYTE_SWAP 3
#endif

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

void helper_shufb(void *vt, void *va, void *vb, void *vc)
{
    uint8_t *pa = va, *pb = vb, *pc = vc;
    uint8_t temp[16];
    unsigned i;

    for (i = 0; i < 16; ++i) {
        uint8_t val, sel = pc[i ^ REG_BYTE_SWAP];
        if ((sel & 0xc0) == 0x80) {
            val = 0;
        } else if ((sel & 0xe0) == 0xc0) {
            val = 0xff;
        } else if ((sel & 0xe0) == 0xe0) {
            val = 0x80;
        } else {
            val = (sel & 0x10 ? pb : pa)[(sel & 0xf) ^ REG_BYTE_SWAP];
        }
        temp[i ^ REG_BYTE_SWAP] = val;
    }

    memcpy(vt, temp, 16);
}

uint32_t helper_shlh(uint32_t a, uint32_t b)
{
    unsigned countl, counth, shifth, shiftl;

    countl = b & 0x1f;
    counth = (b >> 16) & 0x1f;

    shiftl = (a << countl) & 0xffff;
    shifth = (a & 0xffff0000) << counth;

    return shiftl | shifth;
}

void helper_shlqby(void *vt, void *va, uint32_t count)
{
    uint8_t *pa = va;
    unsigned i;
    uint8_t temp[16];

    count &= 0x1f;
    memset(temp, 0, 16);

    for (i = 0; i + count < 16; ++i) {
        temp[i ^ REG_BYTE_SWAP] = pa[(i + count) ^ REG_BYTE_SWAP];
    }

    memcpy(vt, temp, 16);
}

uint32_t helper_roth(uint32_t a, uint32_t b)
{
    unsigned countl, counth, roth, rotl;

    countl = b & 0xf;
    counth = (b >> 16) & 0xf;

    rotl  = (a << countl) & 0xffff;
    rotl |= (a & 0xffff) >> (16 - countl);

    roth  = (a & 0xffff0000) << counth;
    roth |= (a >> (16 - counth)) & 0xffff0000;

    return rotl | roth;
}

void helper_rotqby(void *vt, void *va, uint32_t count)
{
    uint8_t *pa = va;
    unsigned i;
    uint8_t temp[16];

    count &= 15;
    for (i = 0; i < 16; ++i) {
        temp[i ^ REG_BYTE_SWAP] = pa[((i + count) & 15) ^ REG_BYTE_SWAP];
    }

    memcpy(vt, temp, 16);
}

/* As indicated in the Programming Notes associated with all of the
   Rotate and Mask instructions, these are logical right shifts
   with the shift count in two's compliment form.  */

uint32_t helper_rothm(uint32_t a, uint32_t b)
{
    uint32_t countl, counth, shiftl, shifth;

    countl = -b & 0x1f;
    counth = -(b >> 16) & 0x1f;

    shiftl = (a & 0xffff) >> countl;
    shifth = (a >> counth) & 0xffff0000;

    return shiftl | shifth;
}

void helper_rotqmby(void *vt, void *va, uint32_t count)
{
    uint8_t *pa = va;
    unsigned i;
    uint8_t temp[16];

    count &= 0x1f;
    memset(temp, 0, 16);

    for (i = 0; i + count < 16; ++i) {
        temp[(i + count) ^ REG_BYTE_SWAP] = pa[i ^ REG_BYTE_SWAP];
    }

    memcpy(vt, temp, 16);
}

uint32_t helper_rotmah(uint32_t a, uint32_t b)
{
    uint32_t countl, counth, shiftl, shifth;

    countl = -b & 0x1f;
    counth = -(b >> 16) & 0x1f;

    shiftl = ((int16_t)a) >> countl;
    shifth = ((int32_t)a >> counth) & 0xffff0000;

    return shiftl | shifth;
}

void tlb_fill(CPUState *cs, target_ulong addr, int is_write,
              int mmu_idx, uintptr_t retaddr)
{
    abort();
}
