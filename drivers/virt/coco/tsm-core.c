// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2024-2025 Intel Corporation. All rights reserved. */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/tsm.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/cleanup.h>
#include <linux/pci-tsm.h>
#include <linux/pci-ide.h>

static ssize_t pci_mode_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct tsm_dev *tsm_dev = container_of(dev, struct tsm_dev, dev);
	const struct pci_tsm_ops *ops = tsm_dev->pci_ops;

	if (ops->connect)
		return sysfs_emit(buf, "link\n");
	if (ops->lock)
		return sysfs_emit(buf, "devsec\n");
	return sysfs_emit(buf, "none\n");
}
static DEVICE_ATTR_RO(pci_mode);

static umode_t tsm_pci_visible(struct kobject *kobj, struct attribute *attr, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct tsm_dev *tsm_dev = container_of(dev, struct tsm_dev, dev);

	if (tsm_dev->pci_ops)
		return attr->mode;
	return 0;
}

static struct attribute *tsm_pci_attrs[] = {
	&dev_attr_pci_mode.attr,
	NULL
};

static const struct attribute_group tsm_pci_group = {
	.attrs = tsm_pci_attrs,
	.is_visible = tsm_pci_visible,
};

static const struct attribute_group *tsm_pci_groups[] = {
	&tsm_pci_group,
	NULL
};

static void tsm_release(struct device *);
static const struct class tsm_class = {
	.name		= "tsm",
	.dev_release	= tsm_release,
	.dev_groups	= tsm_pci_groups,
};
static DEFINE_IDA(tsm_ida);

static int match_id(struct device *dev, const void *data)
{
	struct tsm_dev *tsm_dev = container_of(dev, struct tsm_dev, dev);
	int id = *(const int *)data;

	return tsm_dev->id == id;
}

struct tsm_dev *find_tsm_dev(int id)
{
	struct device *dev = class_find_device(&tsm_class, NULL, &id, match_id);

	if (!dev)
		return NULL;
	return container_of(dev, struct tsm_dev, dev);
}

static struct tsm_dev *alloc_tsm_dev(struct device *parent)
{
	struct device *dev;
	int id;

	struct tsm_dev *tsm_dev __free(kfree) =
		kzalloc_obj(*tsm_dev);
	if (!tsm_dev)
		return ERR_PTR(-ENOMEM);

	id = ida_alloc(&tsm_ida, GFP_KERNEL);
	if (id < 0)
		return ERR_PTR(id);

	tsm_dev->id = id;
	dev = &tsm_dev->dev;
	dev->parent = parent;
	dev->class = &tsm_class;
	device_initialize(dev);

	return no_free_ptr(tsm_dev);
}

static struct tsm_dev *tsm_register_pci_or_reset(struct tsm_dev *tsm_dev,
						 struct pci_tsm_ops *pci_ops)
{
	int rc;

	if (!pci_ops)
		return tsm_dev;

	tsm_dev->pci_ops = pci_ops;
	rc = pci_tsm_register(tsm_dev);
	if (rc) {
		dev_err(tsm_dev->dev.parent,
			"PCI/TSM registration failure: %d\n", rc);
		device_unregister(&tsm_dev->dev);
		return ERR_PTR(rc);
	}
	sysfs_update_group(&tsm_dev->dev.kobj, &tsm_pci_group);

	/* Notify TSM userspace that PCI/TSM operations are now possible */
	kobject_uevent(&tsm_dev->dev.kobj, KOBJ_CHANGE);
	return tsm_dev;
}

struct tsm_dev *tsm_register(struct device *parent, struct pci_tsm_ops *pci_ops)
{
	struct tsm_dev *tsm_dev __free(put_tsm_dev) = alloc_tsm_dev(parent);
	struct device *dev;
	int rc;

	if (IS_ERR(tsm_dev))
		return tsm_dev;

	dev = &tsm_dev->dev;
	rc = dev_set_name(dev, "tsm%d", tsm_dev->id);
	if (rc)
		return ERR_PTR(rc);

	rc = device_add(dev);
	if (rc)
		return ERR_PTR(rc);

	return tsm_register_pci_or_reset(no_free_ptr(tsm_dev), pci_ops);
}
EXPORT_SYMBOL_GPL(tsm_register);

void tsm_unregister(struct tsm_dev *tsm_dev)
{
	if (tsm_dev->pci_ops)
		pci_tsm_unregister(tsm_dev);
	device_unregister(&tsm_dev->dev);
}
EXPORT_SYMBOL_GPL(tsm_unregister);

static DEFINE_XARRAY(tsm_ide_streams);
static DEFINE_MUTEX(tsm_ide_streams_lock);

/* tracker for the bridge symlink when the bridge has any streams */
struct tsm_ide_stream {
	struct tsm_dev *tsm_dev;
	struct pci_host_bridge *bridge;
	struct kref kref;
};

static struct tsm_ide_stream *create_streams(struct tsm_dev *tsm_dev,
					    struct pci_host_bridge *bridge)
{
	int rc;

	struct tsm_ide_stream *streams __free(kfree) = kzalloc_obj(*streams);
	if (!streams)
		return NULL;

	streams->tsm_dev = tsm_dev;
	streams->bridge = bridge;
	kref_init(&streams->kref);
	rc = xa_insert(&tsm_ide_streams, (unsigned long)bridge, streams,
		       GFP_KERNEL);
	if (rc)
		return NULL;

	rc = sysfs_create_link(&tsm_dev->dev.kobj, &bridge->dev.kobj,
			       dev_name(&bridge->dev));
	if (rc) {
		xa_erase(&tsm_ide_streams, (unsigned long)bridge);
		return NULL;
	}

	return no_free_ptr(streams);
}

int tsm_ide_stream_register(struct pci_ide *ide)
{
	struct tsm_ide_stream *streams;
	struct pci_dev *pdev = ide->pdev;
	struct pci_tsm *tsm = pdev->tsm;
	struct tsm_dev *tsm_dev = tsm->tsm_dev;
	struct pci_host_bridge *bridge = pci_find_host_bridge(pdev->bus);

	guard(mutex)(&tsm_ide_streams_lock);
	streams = xa_load(&tsm_ide_streams, (unsigned long)bridge);
	if (streams)
		kref_get(&streams->kref);
	else
		streams = create_streams(tsm_dev, bridge);

	if (!streams)
		return -ENOMEM;
	ide->tsm_dev = tsm_dev;

	return 0;
}
EXPORT_SYMBOL_GPL(tsm_ide_stream_register);

static void destroy_streams(struct kref *kref)
{
	struct tsm_ide_stream *streams =
		container_of(kref, struct tsm_ide_stream, kref);
	struct tsm_dev *tsm_dev = streams->tsm_dev;
	struct pci_host_bridge *bridge = streams->bridge;

	lockdep_assert_held(&tsm_ide_streams_lock);
	sysfs_remove_link(&tsm_dev->dev.kobj, dev_name(&bridge->dev));
	xa_erase(&tsm_ide_streams, (unsigned long)bridge);
	kfree(streams);
}

void tsm_ide_stream_unregister(struct pci_ide *ide)
{
	struct tsm_ide_stream *streams;
	struct tsm_dev *tsm_dev = ide->tsm_dev;
	struct pci_dev *pdev = ide->pdev;
	struct pci_host_bridge *bridge = pci_find_host_bridge(pdev->bus);

	guard(mutex)(&tsm_ide_streams_lock);
	streams = xa_load(&tsm_ide_streams, (unsigned long)bridge);
	/* catch API abuse */
	if (dev_WARN_ONCE(&tsm_dev->dev,
			  !streams || streams->tsm_dev != tsm_dev,
			  "no IDE streams associated with %s\n",
			  dev_name(&bridge->dev)))
		return;
	kref_put(&streams->kref, destroy_streams);
	ide->tsm_dev = NULL;
}
EXPORT_SYMBOL_GPL(tsm_ide_stream_unregister);

static void tsm_release(struct device *dev)
{
	struct tsm_dev *tsm_dev = container_of(dev, typeof(*tsm_dev), dev);

	ida_free(&tsm_ida, tsm_dev->id);
	kfree(tsm_dev);
}

static int __init tsm_init(void)
{
	return class_register(&tsm_class);
}
module_init(tsm_init)

static void __exit tsm_exit(void)
{
	class_unregister(&tsm_class);
	xa_destroy(&tsm_ide_streams);
}
module_exit(tsm_exit)

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TEE Security Manager Class Device");
