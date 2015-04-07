/*
 * OpenRISC int helper routines
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
#include "exec/helper-proto.h"
#include "exception.h"
#include "qemu/host-utils.h"

target_ulong HELPER(ff1)(target_ulong x)
{
/*#ifdef TARGET_OPENRISC64
    return x ? ctz64(x) + 1 : 0;
#else*/
    return x ? ctz32(x) + 1 : 0;
/*#endif*/
}

target_ulong HELPER(fl1)(target_ulong x)
{
/* not used yet, open it when we need or64.  */
/*#ifdef TARGET_OPENRISC64
    return 64 - clz64(x);
#else*/
    return 32 - clz32(x);
/*#endif*/
}
