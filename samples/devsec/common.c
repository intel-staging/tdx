// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2024 - 2026 Intel Corporation */

#include <linux/pci.h>
#include <linux/export.h>

#include "devsec.h"

/*
 * devsec_bus and devsec_tsm need a common location for this data to
 * avoid depending on each other. Enables load order testing
 */
struct devsec_sysdata *devsec_sysdata[NR_DEVSEC_HOST_BRIDGES];
EXPORT_SYMBOL_FOR_MODULES(devsec_sysdata, "devsec*");

static int __init common_init(void)
{
	return 0;
}
module_init(common_init);

static void __exit common_exit(void)
{
}
module_exit(common_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Device Security Sample Infrastructure: Shared data");
