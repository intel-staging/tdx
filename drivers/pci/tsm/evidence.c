// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2026 NVIDIA Corporation & Affiliates */

#include <linux/device/evidence.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/pci-tsm.h>
#include <linux/tsm.h>

extern struct rw_semaphore pci_tsm_rwsem;

static bool evidence_available(struct pci_dev *pdev)
{
	return pdev->tsm && pdev->tsm->evidence;
}

static struct device *pci_tsm_evidence_find_device(const char *name)
{
	ACQUIRE(rwsem_read_intr, lock)(&pci_tsm_rwsem);
	if (ACQUIRE_ERR(rwsem_read_intr, &lock))
		return NULL;

	struct device *dev __free(put_device) =
		bus_find_device_by_name(&pci_bus_type, NULL, name);

	/*
	 * Bail evidence gathering early if we know at this point that
	 * the device has no valid evidence provider, but still need to
	 * revalidate the same in pci_tsm_evidence_read_begin().
	 */
	if (!dev || !evidence_available(to_pci_dev(dev)))
		return NULL;

	return no_free_ptr(dev);
}

static struct device_evidence *pci_tsm_evidence_read_begin(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct device_evidence *evidence;
	int rc;

	rc = down_read_interruptible(&pci_tsm_rwsem);
	if (rc)
		return ERR_PTR(rc);

	if (!evidence_available(pdev))
		goto err;

	/* Hold the evidence stable against conflicting refresh updates */
	evidence = pdev->tsm->evidence;
	rc = down_read_interruptible(&evidence->lock);
	if (rc)
		goto err;

	return evidence;
err:
	up_read(&pci_tsm_rwsem);
	return ERR_PTR(-ENXIO);
}

static void pci_tsm_evidence_read_end(struct device_evidence *evidence)
{
	up_read(&evidence->lock);
	up_read(&pci_tsm_rwsem);
}

static int pci_tsm_refresh_evidence(struct device *dev, const void *nonce,
				    size_t nonce_len)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct device_evidence *evidence;
	const struct pci_tsm_ops *ops;
	int rc;

	/* Sync against disconnect */
	ACQUIRE(rwsem_read_intr, lock)(&pci_tsm_rwsem);
	if ((rc = ACQUIRE_ERR(rwsem_read_intr, &lock)))
		return rc;

	if (!pdev->tsm)
		return -ENXIO;

	ops = to_pci_tsm_ops(pdev->tsm);
	if (!ops->refresh_evidence)
		return -EOPNOTSUPP;

	/* Sync against pci_tsm_evidence_read_begin */
	evidence = pdev->tsm->evidence;
	ACQUIRE(rwsem_write_kill, elock)(&evidence->lock);
	if ((rc = ACQUIRE_ERR(rwsem_write_kill, &elock)))
		return rc;

	return ops->refresh_evidence(pdev->tsm, nonce, nonce_len);
}

static const struct device_evidence_ops pci_tsm_evidence_ops = {
	.subsys_name = "pci",
	.find_device = pci_tsm_evidence_find_device,
	.evidence_read_begin = pci_tsm_evidence_read_begin,
	.evidence_read_end = pci_tsm_evidence_read_end,
	.refresh_evidence = pci_tsm_refresh_evidence,
};

static int __init pci_tsm_evidence_init(void)
{
	return device_evidence_register(&pci_tsm_evidence_ops);
}
subsys_initcall(pci_tsm_evidence_init);
