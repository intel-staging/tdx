// SPDX-License-Identifier: GPL-2.0
/*
 * TDX Connect guest driver
 *
 * Copyright (C) 2026 Intel Corporation
 */

#define pr_fmt(fmt)		KBUILD_MODNAME ": tdx_connect: " fmt

#include <linux/pci.h>
#include <linux/pci-tsm.h>
#include <linux/tsm.h>
#include <asm/tdx.h>

#include "tdx-guest.h"

struct tdx_devsec {
	struct pci_tsm_devsec pci;
};

static struct tdx_devsec *to_tdx_devsec(struct pci_tsm *tsm)
{
	return container_of(tsm, struct tdx_devsec, pci.base_tsm);
}

static struct pci_tsm *tdx_devsec_lock(struct tsm_dev *tsm_dev, struct pci_dev *pdev)
{
	int ret;

	struct tdx_devsec *tdevsec __free(kfree) = kzalloc(sizeof(*tdevsec), GFP_KERNEL);
	if (!tdevsec)
		return ERR_PTR(-ENOMEM);

	ret = pci_tsm_devsec_constructor(pdev, &tdevsec->pci, tsm_dev);
	if (ret)
		return ERR_PTR(ret);

	return &no_free_ptr(tdevsec)->pci.base_tsm;
}

static void tdx_devsec_unlock(struct pci_tsm *tsm)
{
	struct tdx_devsec *tdevsec = to_tdx_devsec(tsm);

	kfree(tdevsec);
}

static int tdx_devsec_run(struct pci_dev *pdev)
{
	return -EOPNOTSUPP;
}

static struct pci_tsm_ops tdx_devsec_ops = {
	.lock = tdx_devsec_lock,
	.unlock = tdx_devsec_unlock,
	.run = tdx_devsec_run,
};

static void devsec_tsm_remove(void *tsm_dev)
{
	tsm_unregister(tsm_dev);
}

int tdx_connect_init(struct device *dev)
{
	struct tsm_dev *tsm_dev;
	u64 config, ret;

	ret = tdg_vm_rd(TDCS_CONFIG_FLAGS, &config);
	if (ret)
		return -EIO;

	if (!(config & TDCS_CONFIG_TDX_CONNECT))
		return 0;

	tsm_dev = tsm_register(dev, &tdx_devsec_ops);
	if (IS_ERR(tsm_dev))
		return PTR_ERR(tsm_dev);

	return devm_add_action_or_reset(dev, devsec_tsm_remove, tsm_dev);
}
