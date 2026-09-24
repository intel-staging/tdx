// SPDX-License-Identifier: GPL-2.0
/*
 * TDX Connect guest driver
 *
 * Copyright (C) 2026 Intel Corporation
 */

#define pr_fmt(fmt)		KBUILD_MODNAME ": tdx_connect: " fmt

#include <linux/iopoll.h>
#include <linux/pci.h>
#include <linux/pci-tsm.h>
#include <linux/set_memory.h>
#include <linux/tsm.h>
#include <asm/tdx.h>

#include "tdx-guest.h"

/**
 * struct tdcm_ctx - TEE-IO Device Configuration and Management (TDCM) context
 * @buf: Pointer to 4KB-aligned shared memory (>= one page) used as the command
 *       buffer on input and the response buffer on output.
 * @buf_size: Total size of the shared memory buffer.
 * @max_rsp_data_sz: Maximum expected size of the response data.
 * @rsp_data_sz: Actual size of the response data populated by the VMM.
 * @devid: Device Identifier.
 */
struct tdcm_ctx {
	struct tdvmcall_tdcm *buf;
	size_t buf_size;
	u32 max_rsp_data_sz;
	u32 rsp_data_sz;
	u16 devid;
};

#define to_tdcm_rsp_data(tdcm)	((tdcm)->buf->data)

#define TDCM_POLL_DELAY_US	10000
#define TDCM_POLL_TIMEOUT_US	(10 * USEC_PER_SEC)

static int tdx_tdcm_run(struct tdcm_ctx *tdcm)
{
	struct tdvmcall_tdcm *buf = tdcm->buf;
	u8 status;
	int ret;
	u64 r;

	r = tdx_hcall_tdcm(tdcm->devid, buf, tdcm->buf_size, 0);
	if (r)
		return -EFAULT;

	ret = read_poll_timeout(READ_ONCE, status, status != TDCM_STATUS_WAIT,
				TDCM_POLL_DELAY_US, TDCM_POLL_TIMEOUT_US, false, buf->status);
	if (ret) {
		pr_err("TDCM request %d timed out\n", buf->operation);
		return ret;
	}

	/* Ensure subsequent data reads are ordered after the status observation */
	virt_rmb();

	/*
	 * Cache VMM response data length to avoid referencing buf->data_length
	 * directly in case a malicious VMM changes buf->data_length between the
	 * validation below and the subsequent reads.
	 */
	tdcm->rsp_data_sz = READ_ONCE(buf->data_length);

	if (status != TDCM_STATUS_COMPLETED || tdcm->rsp_data_sz > tdcm->max_rsp_data_sz ||
	    (tdcm->max_rsp_data_sz && !tdcm->rsp_data_sz)) {
		pr_err("TDCM request %d failed: status 0x%x, error 0x%x, max_rsp_data_sz 0x%x, returned 0x%x\n",
		       buf->operation, status, buf->error, tdcm->max_rsp_data_sz,
		       tdcm->rsp_data_sz);
		return -EIO;
	}

	return 0;
}

static struct tdcm_ctx *tdx_tdcm_alloc(struct pci_dev *pdev, u8 operation,
				       size_t cmd_data_sz, size_t rsp_data_sz)
{
	struct tdvmcall_tdcm *buf;
	size_t buf_size;
	int ret;

	buf_size = max(cmd_data_sz, rsp_data_sz);
	buf_size = struct_size_t(struct tdvmcall_tdcm, data, buf_size);
	buf_size = PAGE_ALIGN(buf_size);

	struct tdcm_ctx *tdcm __free(kfree) = kzalloc(sizeof(*tdcm), GFP_KERNEL);
	if (!tdcm)
		return ERR_PTR(-ENOMEM);

	buf = alloc_pages_exact(buf_size, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	ret = set_memory_decrypted((unsigned long)buf, PHYS_PFN(buf_size));
	if (ret) {
		free_pages_exact(buf, buf_size);
		return ERR_PTR(ret);
	}

	memset(buf, 0, buf_size);
	buf->operation = operation;
	buf->status = TDCM_STATUS_WAIT;
	buf->data_length = cmd_data_sz;

	tdcm->buf = buf;
	tdcm->buf_size = buf_size;
	tdcm->max_rsp_data_sz = rsp_data_sz;
	tdcm->devid = pci_dev_id(pdev);

	return_ptr(tdcm);
}

static void tdx_tdcm_free(struct tdcm_ctx *tdcm)
{
	if (set_memory_encrypted((unsigned long)tdcm->buf, PHYS_PFN(tdcm->buf_size)))
		pr_err("Failed to encrypt TDCM buf, leak it\n");
	else
		free_pages_exact(tdcm->buf, tdcm->buf_size);

	kfree(tdcm);
}

DEFINE_FREE(tdx_tdcm_free, struct tdcm_ctx *,
	if (!IS_ERR_OR_NULL(_T)) tdx_tdcm_free(_T))

struct tdx_devsec {
	struct pci_tsm_devsec pci;
};

static struct tdx_devsec *to_tdx_devsec(struct pci_tsm *tsm)
{
	return container_of(tsm, struct tdx_devsec, pci.base_tsm);
}

static bool tdx_is_dev_teeio_support(struct tdx_devsec *tdevsec)
{
	struct pci_dev *pdev = tdevsec->pci.base_tsm.pdev;
	struct tdcm_rsp_check_teeio_supp *rsp;
	int ret;

	struct tdcm_ctx *tdcm __free(tdx_tdcm_free) =
		tdx_tdcm_alloc(pdev, TDCM_OP_CHECK_TEEIO_SUPP, 0, sizeof(*rsp));
	if (IS_ERR(tdcm))
		return false;

	ret = tdx_tdcm_run(tdcm);
	if (ret)
		return false;

	if (tdcm->rsp_data_sz != sizeof(*rsp))
		return false;

	rsp = (struct tdcm_rsp_check_teeio_supp *)to_tdcm_rsp_data(tdcm);
	return !!rsp->is_supported;
}

static struct tdx_devsec *tdx_tdi_bind_dev(struct tdx_devsec *tdevsec)
{
	struct pci_dev *pdev = tdevsec->pci.base_tsm.pdev;
	int ret;

	struct tdcm_ctx *tdcm __free(tdx_tdcm_free) = tdx_tdcm_alloc(pdev, TDCM_OP_BIND, 0, 0);
	if (IS_ERR(tdcm))
		return ERR_CAST(tdcm);

	ret = tdx_tdcm_run(tdcm);
	if (ret)
		return ERR_PTR(ret);

	return tdevsec;
}

static int tdx_tdi_unbind_dev(struct tdx_devsec *tdevsec)
{
	struct pci_dev *pdev = tdevsec->pci.base_tsm.pdev;

	struct tdcm_ctx *tdcm __free(tdx_tdcm_free) = tdx_tdcm_alloc(pdev, TDCM_OP_UNBIND, 0, 0);
	if (IS_ERR(tdcm))
		return PTR_ERR(tdcm);

	return tdx_tdcm_run(tdcm);
}

DEFINE_FREE(tdx_tdi_unbind_dev, struct tdx_devsec *,
	if (!IS_ERR_OR_NULL(_T)) tdx_tdi_unbind_dev(_T))

/*
 * TDI State value returned by TDG.TDI.RD.
 * Refer to section "TDG.TDI.RD leaf" in the TDX Connect ABI Specification.
 */
enum tdi_state {
	TDI_STATE_CONFIG_UNLOCKED	= 0x0,
	TDI_STATE_CONFIG_LOCKED		= 0x1,
	TDI_STATE_RUN			= 0x2,
	TDI_STATE_ERROR			= 0x3,
};

enum tdi_field_code {
	TDI_GET_TDISP_STATE		= 2,
};

static int tdx_tdi_read_state(struct tdx_devsec *tdevsec, u8 *state)
{
	struct pci_dev *pdev = tdevsec->pci.base_tsm.pdev;
	u64 value;
	int ret;

	ret = tdx_mcall_tdi_read(pci_dev_id(pdev), TDI_GET_TDISP_STATE, &value);
	if (!ret)
		*state = value;

	return ret;
}

static u8 *tdx_tdi_get_report(struct tdx_devsec *tdevsec, u32 *report_sz)
{
	struct pci_dev *pdev = tdevsec->pci.base_tsm.pdev;
	u8 *report;
	int ret;

	struct tdcm_ctx *tdcm __free(tdx_tdcm_free) =
		tdx_tdcm_alloc(pdev, TDCM_OP_GET_TDI_REPORT, 0, MAX_TDI_REPORT_SIZE);
	if (IS_ERR(tdcm))
		return ERR_CAST(tdcm);

	ret = tdx_tdcm_run(tdcm);
	if (ret)
		return ERR_PTR(ret);

	report = kmemdup(to_tdcm_rsp_data(tdcm), tdcm->rsp_data_sz, GFP_KERNEL);
	if (!report)
		return ERR_PTR(-ENOMEM);

	*report_sz = tdcm->rsp_data_sz;
	print_hex_dump_debug("tdi_report: ", DUMP_PREFIX_OFFSET, 16, 1, report, *report_sz, false);
	return report;
}

static struct tdx_devsec *tdx_tdi_mmio_setup(struct tdx_devsec *tdevsec)
{
	struct pci_tsm_devsec *devsec_tsm = &tdevsec->pci;
	struct pci_dev *pdev = devsec_tsm->base_tsm.pdev;
	int rc;

	struct pci_tsm_mmio *mmio __free(kfree) = pci_tsm_mmio_alloc(pdev, TDISP_OFFSET_RELATIVE);
	if (!mmio)
		return ERR_PTR(-EINVAL);

	rc = pci_tsm_mmio_setup(pdev, mmio);
	if (rc)
		return ERR_PTR(rc);

	devsec_tsm->mmio = no_free_ptr(mmio);
	return tdevsec;
}

static void tdx_tdi_mmio_teardown(struct tdx_devsec *tdevsec)
{
	struct pci_tsm_devsec *devsec_tsm = &tdevsec->pci;

	if (!devsec_tsm->mmio)
		return;

	pci_tsm_mmio_teardown(devsec_tsm->mmio);
	kfree(devsec_tsm->mmio);
	devsec_tsm->mmio = NULL;
}
DEFINE_FREE(tdx_tdi_mmio_teardown, struct tdx_devsec *,
	if (!IS_ERR_OR_NULL(_T)) tdx_tdi_mmio_teardown(_T))

static int tdx_tdi_mmio_accept(struct tdx_devsec *tdevsec)
{
	struct pci_dev *pdev = tdevsec->pci.base_tsm.pdev;
	const struct pci_tsm_mmio *mmio;
	u32 i;

	mmio = tdevsec->pci.mmio;

	for (i = 0; i < mmio->nr; i++) {
		const struct pci_tsm_mmio_entry *entry = &mmio->mmio[i];
		u32 pages = resource_size(&entry->res) >> PAGE_SHIFT;
		phys_addr_t gpa = entry->res.start;
		int ret;

		pci_dbg(pdev, "accept MMIO entry %d %pR gpa=0x%llx\n",
			entry->index, &entry->res, gpa);

		/* Accept the whole range */
		ret = tdx_mcall_mmio_accept(pci_dev_id(pdev), entry->index, 0, pages, gpa);
		if (ret) {
			pci_err(pdev, "Failed to accept MMIO entry %d (gpa=0x%llx), ret=%d\n",
				entry->index, gpa, ret);
			return ret;
		}
	}

	return 0;
}

static struct pci_tsm *tdx_devsec_lock(struct tsm_dev *tsm_dev, struct pci_dev *pdev)
{
	struct device_evidence_object *tsm_report;
	struct device_evidence *evidence;
	u32 report_sz;
	u8 state;
	int ret;

	struct tdx_devsec *tdevsec __free(kfree) = kzalloc(sizeof(*tdevsec), GFP_KERNEL);
	if (!tdevsec)
		return ERR_PTR(-ENOMEM);

	ret = pci_tsm_devsec_constructor(pdev, &tdevsec->pci, tsm_dev);
	if (ret)
		return ERR_PTR(ret);

	if (!tdx_is_dev_teeio_support(tdevsec))
		return ERR_PTR(-EOPNOTSUPP);

	struct tdx_devsec *tdevsec_bind __free(tdx_tdi_unbind_dev) = tdx_tdi_bind_dev(tdevsec);
	if (IS_ERR(tdevsec_bind))
		return ERR_CAST(tdevsec_bind);

	u8 *report __free(kfree) = tdx_tdi_get_report(tdevsec, &report_sz);
	if (IS_ERR(report))
		return ERR_CAST(report);

	ret = tdx_tdi_read_state(tdevsec, &state);
	if (ret)
		return ERR_PTR(ret);

	if (state != TDI_STATE_CONFIG_LOCKED)
		return ERR_PTR(-EIO);

	evidence = device_evidence_create(0, HASH_ALGO_SHA384);

	if (!evidence)
		return ERR_PTR(-ENOMEM);

	tsm_report = &evidence->obj[DEVICE_EVIDENCE_TYPE_REPORT];
	tsm_report->data = no_free_ptr(report);
	tsm_report->len = report_sz;

	tdevsec->pci.base_tsm.evidence = evidence;

	retain_and_null_ptr(tdevsec_bind);

	return &no_free_ptr(tdevsec)->pci.base_tsm;
}

static void tdx_devsec_unlock(struct pci_tsm *tsm)
{
	struct tdx_devsec *tdevsec = to_tdx_devsec(tsm);
	struct device_evidence *evidence = tsm->evidence;

	if (WARN_ON(tdx_tdi_unbind_dev(tdevsec)))
		return;

	tdx_tdi_mmio_teardown(tdevsec);

	kfree(evidence->obj[DEVICE_EVIDENCE_TYPE_REPORT].data);
	kfree(evidence);
	kfree(tdevsec);
}

static int tdx_devsec_run(struct pci_dev *pdev)
{
	struct tdx_devsec *tdevsec = to_tdx_devsec(pdev->tsm);
	int ret;

	struct tdx_devsec *tdevsec_mmio __free(tdx_tdi_mmio_teardown) =
		tdx_tdi_mmio_setup(tdevsec);
	if (IS_ERR(tdevsec_mmio))
		return PTR_ERR(tdevsec_mmio);

	ret = tdx_tdi_mmio_accept(tdevsec);
	if (ret)
		return ret;

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
