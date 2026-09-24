// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2024 - 2026 Intel Corporation */

#include <linux/export.h>
#include <linux/pci.h>
#include <linux/pci-tsm.h>
#include <linux/vmalloc.h>
#include <uapi/linux/device-evidence.h>

#include "devsec.h"

/*
 * devsec_bus and devsec_tsm need a common location for this data to
 * avoid depending on each other. Enables load order testing
 */
struct devsec_sysdata *devsec_sysdata[NR_DEVSEC_HOST_BRIDGES];
EXPORT_SYMBOL_FOR_MODULES(devsec_sysdata, "devsec*");

static struct {
	void *certs;
	size_t certs_size;
	void *transcript;
	size_t transcript_size;
	int busy;
	struct mutex lock;
} devsec_evidence;

void devsec_init_evidence(struct device_evidence *evidence)
{
	struct device_evidence_object *obj;

	obj = &evidence->obj[DEVICE_EVIDENCE_TYPE_CERT0];
	obj->data = devsec_evidence.certs;
	obj->len = devsec_evidence.certs_size;

	obj = &evidence->obj[DEVICE_EVIDENCE_TYPE_MEASUREMENTS];
	obj->data = devsec_evidence.transcript;
	obj->len = devsec_evidence.transcript_size;
}
EXPORT_SYMBOL_FOR_MODULES(devsec_init_evidence, "devsec*");

static ssize_t certs_read(struct file *file, struct kobject *kobj,
			  const struct bin_attribute *bin_attr, char *buf,
			  loff_t off, size_t count)
{
	guard(mutex)(&devsec_evidence.lock);
	return memory_read_from_buffer(buf, count, &off, devsec_evidence.certs,
				       devsec_evidence.certs_size);
}

#define EVIDENCE_MAX_SIZE SZ_16M

static ssize_t evidence_write(char *buf, loff_t off, size_t count, void **data,
			      size_t *data_size)
{
	loff_t in_off = 0;

	if (off + count > EVIDENCE_MAX_SIZE)
		return -EFBIG;

	guard(mutex)(&devsec_evidence.lock);
	if (devsec_evidence.busy)
		return -EBUSY;
	if (off + count > *data_size) {
		void *new_data = kvrealloc(*data, off + count, GFP_KERNEL);

		if (!new_data)
			return -ENOMEM;
		*data = new_data;
		*data_size = off + count;
	}

	/* reset the buffer on a single byte write */
	if (off + count == 1) {
		kvfree(*data);
		*data = NULL;
		*data_size = 0;
		return 1;
	}

	return memory_read_from_buffer(*data + off, count, &in_off, buf, count);
}

static ssize_t certs_write(struct file *file, struct kobject *kobj,
			   const struct bin_attribute *bin_attr, char *buf,
			   loff_t off, size_t count)
{
	return evidence_write(buf, off, count, &devsec_evidence.certs,
			      &devsec_evidence.certs_size);
}

static ssize_t transcript_read(struct file *file, struct kobject *kobj,
			       const struct bin_attribute *bin_attr, char *buf,
			       loff_t off, size_t count)
{
	guard(mutex)(&devsec_evidence.lock);
	return memory_read_from_buffer(buf, count, &off,
				       devsec_evidence.transcript,
				       devsec_evidence.transcript_size);
}

static ssize_t transcript_write(struct file *file, struct kobject *kobj,
				const struct bin_attribute *bin_attr, char *buf,
				loff_t off, size_t count)
{
	return evidence_write(buf, off, count, &devsec_evidence.transcript,
			      &devsec_evidence.transcript_size);
}

static const BIN_ATTR_RW(certs, 0);
static const BIN_ATTR_RW(transcript, 0);

static const struct bin_attribute *devsec_evidence_attrs[] = {
	&bin_attr_certs,
	&bin_attr_transcript,
	NULL,
};

/*
 * Prevent evidence from changing while any sample device is connected or locked
 */
void devsec_evidence_busy(void)
{
	guard(mutex)(&devsec_evidence.lock);
	devsec_evidence.busy++;
}
EXPORT_SYMBOL_FOR_MODULES(devsec_evidence_busy, "devsec*");

void devsec_evidence_idle(void)
{
	guard(mutex)(&devsec_evidence.lock);
	if (devsec_evidence.busy-- <= 0) {
		WARN_ON_ONCE(1);
		devsec_evidence.busy = 0;
	}
}
EXPORT_SYMBOL_FOR_MODULES(devsec_evidence_idle, "devsec*");

const struct attribute_group devsec_evidence_group = {
	.bin_attrs = devsec_evidence_attrs,
};
EXPORT_SYMBOL_FOR_MODULES(devsec_evidence_group, "devsec*");

static int __init common_init(void)
{
	mutex_init(&devsec_evidence.lock);
	return 0;
}
module_init(common_init);

static void __exit common_exit(void)
{
	kvfree(devsec_evidence.certs);
	kvfree(devsec_evidence.transcript);
	mutex_destroy(&devsec_evidence.lock);
}
module_exit(common_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Device Security Sample Infrastructure: Shared data");
