/* SPDX-License-Identifier: GPL-2.0 */
/*
 * TDX guest driver private declarations
 *
 * Copyright (C) 2026 Intel Corporation
 */

#ifndef __TDX_GUEST_H
#define __TDX_GUEST_H

#include <linux/kernel.h>
#include <linux/device.h>

#ifdef CONFIG_TDX_CONNECT_GUEST
extern int tdx_connect_init(struct device *dev);
#else
static inline int tdx_connect_init(struct device *dev) { return 0; }
#endif /* CONFIG_TDX_CONNECT_GUEST */

#endif /* __TDX_GUEST_H */
