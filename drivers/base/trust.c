/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 NVIDIA Corporation & Affiliates */

#include <linux/device.h>
#include <linux/device/trust.h>
#include <linux/module.h>
#include "base.h"

void device_initialize_trust(struct device *dev)
{
	if (dev->p->trust == DEVICE_TRUST_UNSET)
		dev->p->trust = DEVICE_DEFAULT_TRUST;
}

/* Driver trust policy requires modules, builtin drivers always attach */
static enum device_trust builtin_driver_trust(void)
{
	return DEVICE_TRUST_AUTO;
}

static enum device_trust driver_trust(struct module *mod)
{
	if (!mod)
		return builtin_driver_trust();
	return mod->trust;
}

/*
 * @dev matches @drv and is locked for probe. Check if the driver has
 * policy on trusting devices it attaches, update the device's trust
 * level from that policy. Trust privileges beyond driver bind are
 * realized in a bus's ->dma_configure().
 */
bool device_trust_bind(const struct device_driver *drv, struct device *dev)
{
	enum device_trust drv_trust = driver_trust(drv->owner);

	if (drv_trust != DEVICE_TRUST_UNSET)
		dev->p->trust = drv_trust;
	return dev->p->trust > DEVICE_TRUST_NONE;
}

static const char * const device_trust_names[] = {
	[DEVICE_TRUST_NONE]	 = "none",
	[DEVICE_TRUST_AUTO]	 = "auto",
};

static enum device_trust device_trust_parse(const char *name)
{
	int i;

	if (!name)
		return DEVICE_TRUST_UNSET;

	for (i = 0; i < ARRAY_SIZE(device_trust_names); i++)
		if (device_trust_names[i] &&
		    sysfs_streq(name, device_trust_names[i]))
			return i;
	return DEVICE_TRUST_UNSET;
}

void module_driver_trust(struct module *mod, const char *val)
{
	mod->trust = device_trust_parse(val);
}

/*
 * Honor the module core forcing all modules to no trust by default,
 * otherwise fallback to the compile-time default for all devices
 */
void module_driver_trust_init(struct module *mod, bool require_trust)
{
	if (require_trust)
		mod->trust = DEVICE_TRUST_NONE;
	else
		mod->trust = DEVICE_DEFAULT_TRUST;
}
