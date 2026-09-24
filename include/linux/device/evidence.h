/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 Intel Corporation */
/* Copyright (C) 2026, NVIDIA Corporation & Affiliates */
#ifndef __DEVICE_EVIDENCE_H
#define __DEVICE_EVIDENCE_H

#include <linux/errno.h>
#include <linux/rwsem.h>
#include <linux/types.h>
#include <uapi/linux/device-evidence.h>
#include <uapi/linux/hash_info.h>

struct device;
struct module;

/**
 * struct device_evidence_object - General device evidence blob descriptor
 * @data: pointer to the evidence data blob
 * @len: length of the evidence data blob
 * @digest: TSM generated digest of the data blob
 */
struct device_evidence_object {
	void *data;
	size_t len;
	void *digest;
};

/**
 * struct device_evidence - Retrieved device evidence
 * @slot: certificate slot used by a link TSM for connect
 * @generation: refresh_evidence() invocation detection
 * @validated_generation: generation last accepted by userspace via "validate"
 * @digest_algo: payload size of DEVICE_EVIDENCE_FLAG_DIGEST requests
 * @lock: synchronize dumps vs refresh_evidence()
 * @obj: array of evidence objects a TSM might populate
 *
 * An increment of @generation causes in flight dumps to fail with -EAGAIN and
 * implicitly revokes a prior userspace validation.
 */
struct device_evidence {
	int slot;
	u32 generation;
	u32 validated_generation;
	enum hash_algo digest_algo;
	struct rw_semaphore lock;
	struct device_evidence_object obj[DEVICE_EVIDENCE_TYPE_MAX + 1];
};

/**
 * struct device_evidence_ops - subsys-specific evidence lookup operations
 * @subsys_name: /sys/{bus,class}/@subsys_name
 * @find_device: fetch device object for ../@subsys_name/devices/@name
 * @evidence_read_begin: hold evidence stable over read
 * @evidence_read_end: hold evidence stable over read
 * @refresh_evidence: generate fresh device evidence
 *
 * Connect the generic "device-evidence" netlink transport to a source
 * of device evidence that conveys SPDM collateral and its extensions
 * like PCIe TDISP interface reports
 */
struct device_evidence_ops {
	const char *subsys_name;
	struct device *(*find_device)(const char *name);
	struct device_evidence *(*evidence_read_begin)(struct device *dev);
	void (*evidence_read_end)(struct device_evidence *evidence);
	int (*refresh_evidence)(struct device *dev, const void *nonce,
				size_t nonce_len);
};

#ifdef CONFIG_DEVICE_EVIDENCE
struct device_evidence *device_evidence_create(int slot,
					       enum hash_algo digest_algo);
int device_evidence_register(const struct device_evidence_ops *ops);
void device_evidence_unregister(const struct device_evidence_ops *ops);
#else
static inline int
device_evidence_register(const struct device_evidence_ops *ops)
{
	return -EOPNOTSUPP;
}

static inline void
device_evidence_unregister(const struct device_evidence_ops *ops)
{
}
#endif

#endif /* __DEVICE_EVIDENCE_H */
