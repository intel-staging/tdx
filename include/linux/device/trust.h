/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 NVIDIA Corporation & Affiliates */
#ifndef __DEVICE_TRUST_H__
#define __DEVICE_TRUST_H__

/**
 * enum device_trust - Level of restrictions and privileges for a
 * device. Trust is initially assigned by the bus, and the bus is
 * responsible for coordinating transitions between trust levels with
 * DMA/IOMMU and its own device security mechanisms.
 *
 * @DEVICE_TRUST_UNSET: Unregistered device object with no current bus
 * @DEVICE_TRUST_NONE: Blocked when idle, cannot bind
 * @DEVICE_TRUST_AUTO: All typical privileges granted
 */
enum device_trust {
	DEVICE_TRUST_UNSET,
	DEVICE_TRUST_NONE,
	DEVICE_TRUST_AUTO,
};

#define DEVICE_DEFAULT_TRUST                                        \
	(IS_ENABLED(CONFIG_DEVICE_TRUST_NONE) ? DEVICE_TRUST_NONE : \
						DEVICE_TRUST_AUTO)

struct device;
struct device_driver;

#ifdef CONFIG_DEVICE_TRUST
void module_driver_trust(struct module *mod, const char *val);
void module_driver_trust_init(struct module *mod, bool distrust);
#else
static inline void module_driver_trust(struct module *mod, const char *val)
{
	pr_warn("module: %s: trust= support disabled\n", mod->name);
}
static inline void module_driver_trust_init(struct module *mod, bool distrust)
{
}
#endif

#endif /* __DEVICE_TRUST_H__ */
