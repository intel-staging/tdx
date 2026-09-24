// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2026 Intel Corporation */

#undef pr_fmt
#define pr_fmt(fmt)     "tdx_connect: " fmt

#include <linux/io.h>
#include <asm/tdx.h>
#include <linux/mm.h>

static inline int tdx_mcall_tdi_to_errno(u64 ret)
{
	switch (TDCALL_RETURN_CODE(ret)) {
	case TDCALL_TDI_NOT_PRESENT:
	case TDCALL_TDI_INVALID_METADATA:
		return -ENODEV;
	default:
		return tdx_mcall_to_errno(ret);
	}
}

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

/**
 * tdx_mcall_tdi_read() - Read field from a Trust Device Interface Control Structure (TDI_CS)
 * @func_id: Function identifier specifying the TDI_CS
 * @field: Field identifier within the specified TDI_CS
 * @value: Storage for the successfully retrieved field value
 *
 * Invokes the TDG.TDI.RD TDCALL to read the value of @field within the TDI_CS specified
 * by @func_id.
 *
 * Return 0 on success, -ENXIO for invalid operands, -EBUSY for busy operation,
 * -ENODEV for TDI not present or invalid metadata, or -EIO on other TDCALL failures.
 */
int tdx_mcall_tdi_read(u64 func_id, u64 field, u64 *value)
{
	struct tdx_module_args args = {
		.rcx = func_id,
		.rdx = field,
	};
	u64 ret;

	ret = __tdcall_ret(TDG_TDI_READ, &args);
	if (!ret) {
		*value = args.rcx;
		return 0;
	}

	return tdx_mcall_tdi_to_errno(ret);
}
EXPORT_SYMBOL_FOR_MODULES(tdx_mcall_tdi_read, "tdx-guest");
