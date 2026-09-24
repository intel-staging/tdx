// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2026 Intel Corporation */

#undef pr_fmt
#define pr_fmt(fmt)     "tdx_connect: " fmt

#include <linux/io.h>
#include <asm/tdx.h>
#include <linux/mm.h>

/*
 * tdx_hcall_tdcm - Send a TDCM command to the host via TDG.VP.VMCALL<TDCM>.
 *
 * @devid:  Device identifier of the target TEE-IO device.
 * @buf:    Shared, directly mapped buffer containing the TDCM command on
 *          input and the host response on output.
 * @size:   Size of @buf in bytes.
 * @vector: Event notification interrupt vector, or 0 for synchronous operation.
 *
 * The buffer physical address is passed to the host with decrypted/shared
 * mark.
 *
 * Returns 0 on success or a TDX VMCALL status code on failure. See
 * TDG.VP.VMCALL<TDCM> in the TDX GHCI v2.0 specification for status codes.
 */
u64 tdx_hcall_tdcm(u16 devid, void *buf, size_t size, u8 vector)
{
	WARN_ON_ONCE(!PAGE_ALIGNED(buf) || !PAGE_ALIGNED(size));

	return _tdx_hypercall(TDVMCALL_TDCM, devid, cc_mkdec(virt_to_phys(buf)), size, vector);
}
EXPORT_SYMBOL_FOR_MODULES(tdx_hcall_tdcm, "tdx-guest");
