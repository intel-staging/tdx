// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2026 NVIDIA Corporation & Affiliates */

#include <crypto/hash_info.h>
#include <linux/cleanup.h>
#include <linux/device/evidence.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/find.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <net/genetlink.h>
#include <net/netlink.h>
#include "device-evidence-netlink.h"

#define DEVICE_EVIDENCE_NAME_LEN 32
#define DEVICE_EVIDENCE_START U32_MAX
#define DEVICE_EVIDENCE_OBJECT_START (U32_MAX - 1)

struct device_evidence_subsys {
	const struct device_evidence_ops *ops;
	struct list_head list;
	atomic_t busy;
};

static LIST_HEAD(device_evidence_subsystems);
static DEFINE_MUTEX(device_evidence_lock);
static DECLARE_WAIT_QUEUE_HEAD(device_evidence_waitqueue);

struct device_evidence_ctx {
	struct device_evidence_subsys *subsys;
	struct device *dev;
	void *nonce;
	u64 generation;
	u32 type_mask;
	u32 flags;
	u32 offset;
	u16 nonce_len;
};

struct device_evidence *device_evidence_create(int slot,
					       enum hash_algo digest_algo)
{
	struct device_evidence *evidence = kzalloc_obj(*evidence);

	if (!evidence)
		return NULL;

	evidence->slot = slot;
	evidence->generation = 1;
	evidence->digest_algo = digest_algo;
	init_rwsem(&evidence->lock);
	return evidence;
}
EXPORT_SYMBOL_GPL(device_evidence_create);

int device_evidence_register(const struct device_evidence_ops *ops)
{
	struct device_evidence_subsys *subsys;

	if (!ops || !ops->subsys_name || !ops->find_device ||
	    !ops->evidence_read_begin || !ops->evidence_read_end)
		return -EINVAL;

	struct device_evidence_subsys *new_subsys __free(kfree) =
		kzalloc_obj(*new_subsys);
	if (!new_subsys)
		return -ENOMEM;

	guard(mutex)(&device_evidence_lock);
	list_for_each_entry(subsys, &device_evidence_subsystems, list)
		if (subsys->ops == ops)
			return -EEXIST;

	new_subsys->ops = ops;
	list_add_tail(&no_free_ptr(new_subsys)->list, &device_evidence_subsystems);

	return 0;
}
EXPORT_SYMBOL_GPL(device_evidence_register);

static struct device_evidence_subsys *
block_subsys(const struct device_evidence_ops *ops)
{
	struct device_evidence_subsys *subsys;

	/* trusts callers do not re-register @ops while awaiting unregistration */
	guard(mutex)(&device_evidence_lock);
	list_for_each_entry(subsys, &device_evidence_subsystems, list) {
		if (subsys->ops == ops) {
			list_del(&subsys->list);
			return subsys;
		}
	}
	return NULL;
}

void device_evidence_unregister(const struct device_evidence_ops *ops)
{
	/* stop new requests */
	struct device_evidence_subsys *subsys = block_subsys(ops);

	if (!subsys)
		return;

	/* flush all usage of @ops */
	wait_event(device_evidence_waitqueue, atomic_read(&subsys->busy) == 0);
	kfree(subsys);
}
EXPORT_SYMBOL_GPL(device_evidence_unregister);

static struct device_evidence_subsys *
device_evidence_subsys(const char *subsys_name)
{
	struct device_evidence_subsys *subsys;

	list_for_each_entry(subsys, &device_evidence_subsystems, list) {
		const struct device_evidence_ops *ops = subsys->ops;

		if (!strcmp(subsys_name, ops->subsys_name))
			return subsys;
	}
	return ERR_PTR(-EOPNOTSUPP);
}

static void device_evidence_ctx_teardown(struct device_evidence_ctx *ctx)
{
	put_device(ctx->dev);
	kfree(ctx->nonce);
}

DEFINE_FREE(put_ctx, struct device_evidence_ctx *,
	    if (!IS_ERR_OR_NULL(_T)) device_evidence_ctx_teardown(_T))

static struct device_evidence_ctx *
device_evidence_ctx_setup(const struct genl_info *info,
			  struct device_evidence_ctx *ctx)
{
	char subsys[DEVICE_EVIDENCE_NAME_LEN];
	char name[DEVICE_EVIDENCE_NAME_LEN];
	struct nlattr *attr;

	*ctx = (struct device_evidence_ctx) { };

	if (GENL_REQ_ATTR_CHECK(info, DEVICE_EVIDENCE_A_OBJECT_SUBSYS)) {
		NL_SET_ERR_MSG(info->extack, "missing subsys name");
		return ERR_PTR(-EINVAL);
	}

	attr = info->attrs[DEVICE_EVIDENCE_A_OBJECT_SUBSYS];
	if (nla_strscpy(subsys, attr, sizeof(subsys)) < 0) {
		NL_SET_BAD_ATTR(info->extack, attr);
		return ERR_PTR(-EINVAL);
	}

	if (GENL_REQ_ATTR_CHECK(info, DEVICE_EVIDENCE_A_OBJECT_DEV_NAME)) {
		NL_SET_ERR_MSG(info->extack, "missing device name");
		return ERR_PTR(-EINVAL);
	}

	attr = info->attrs[DEVICE_EVIDENCE_A_OBJECT_DEV_NAME];
	if (nla_strscpy(name, attr, sizeof(name)) < 0) {
		NL_SET_BAD_ATTR(info->extack, attr);
		return ERR_PTR(-EINVAL);
	}

	ctx->subsys = device_evidence_subsys(subsys);
	if (IS_ERR(ctx->subsys)) {
		NL_SET_ERR_MSG_FMT(info->extack,
				   "no evidence provider for subsys '%s'", subsys);
		return ERR_CAST(ctx->subsys);
	}

	ctx->dev = ctx->subsys->ops->find_device(name);
	if (!ctx->dev) {
		NL_SET_ERR_MSG_FMT(info->extack,
				   "device '%s:%s' evidence not found", subsys,
				   name);
		return ERR_PTR(-ENODEV);
	}

	return ctx;
}

static struct device_evidence_ctx *to_ctx(struct netlink_callback *cb)
{
	return (struct device_evidence_ctx *)cb->ctx;
}

int device_evidence_nl_read_pre(struct netlink_callback *cb)
{
	const struct genl_info *info = genl_info_dump(cb);
	struct nlattr *attr;
	u32 unknown_types;
	int rc;

	NL_ASSERT_CTX_FITS(struct device_evidence_ctx);

	ACQUIRE(mutex_intr, lock)(&device_evidence_lock);
	if ((rc = ACQUIRE_ERR(mutex_intr, &lock)))
		return rc;

	struct device_evidence_ctx *ctx __free(put_ctx) =
		device_evidence_ctx_setup(info, to_ctx(cb));
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	if (GENL_REQ_ATTR_CHECK(info,
				DEVICE_EVIDENCE_A_OBJECT_TYPE_MASK)) {
		NL_SET_ERR_MSG(info->extack, "missing object request mask");
		return -EINVAL;
	}

	attr = info->attrs[DEVICE_EVIDENCE_A_OBJECT_TYPE_MASK];
	ctx->type_mask = nla_get_u32(attr);
	unknown_types = ctx->type_mask & ~DEVICE_EVIDENCE_TYPE_FLAG_MASK;
	if (unknown_types) {
		NL_SET_ERR_MSG_FMT(info->extack,
				   "unsupported object request %#x",
				   unknown_types);
		return -EINVAL;
	}

	if (!ctx->type_mask) {
		NL_SET_ERR_MSG(info->extack, "no evidence type requested");
		return -EINVAL;
	}

	attr = info->attrs[DEVICE_EVIDENCE_A_OBJECT_FLAGS];
	if (attr) {
		ctx->flags = nla_get_u32(attr);
		if (ctx->flags & ~DEVICE_EVIDENCE_FLAG_MASK) {
			NL_SET_BAD_ATTR(info->extack, attr);
			return -EINVAL;
		}
	}

	attr = info->attrs[DEVICE_EVIDENCE_A_OBJECT_NONCE];
	if (attr) {
		ctx->nonce = nla_memdup(attr, GFP_KERNEL);
		if (!ctx->nonce) {
			NL_SET_BAD_ATTR(info->extack, attr);
			return -ENOMEM;
		}
		ctx->nonce_len = nla_len(attr);
	}

	ctx->offset = DEVICE_EVIDENCE_START;
	atomic_inc(&ctx->subsys->busy);
	retain_and_null_ptr(ctx);

	return 0;
}

int device_evidence_nl_read_post(struct netlink_callback *cb)
{
	struct device_evidence_ctx *ctx = to_ctx(cb);

	device_evidence_ctx_teardown(ctx);

	guard(mutex)(&device_evidence_lock);
	if (atomic_dec_and_test(&ctx->subsys->busy))
		wake_up_all(&device_evidence_waitqueue);

	return 0;
}

static size_t evidence_len(struct device_evidence *evidence,
			   struct device_evidence_object *obj,
			   unsigned long flags)
{
	if (flags & DEVICE_EVIDENCE_FLAG_DIGEST) {
		if (obj->digest)
			return hash_digest_size[evidence->digest_algo];
		return 0;
	}
	return obj->len;
}

static void *evidence_data(struct device_evidence_object *obj,
			   unsigned long flags)
{
	if (flags & DEVICE_EVIDENCE_FLAG_DIGEST)
		return obj->digest;
	return obj->data;
}

static int current_type(struct device_evidence_ctx *ctx)
{
	return __ffs(ctx->type_mask);
}

static struct device_evidence_object *
current_obj(struct device_evidence_ctx *ctx, struct device_evidence *evidence)
{
	return &evidence->obj[current_type(ctx)];
}

static int __device_evidence_read(struct sk_buff *skb,
				  struct netlink_callback *cb,
				  struct device_evidence *evidence)
{
	struct device_evidence_ctx *ctx = (struct device_evidence_ctx *)cb->ctx;
	struct device_evidence_object *obj = current_obj(ctx, evidence);
	size_t object_len = evidence_len(evidence, obj, ctx->flags);
	void *object_data = evidence_data(obj, ctx->flags);
	size_t available, overhead, len;
	void *hdr;
	void *val;
	int rc;

	hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			  &device_evidence_nl_family, NLM_F_MULTI,
			  DEVICE_EVIDENCE_CMD_READ);
	if (!hdr)
		return -EMSGSIZE;

	if (ctx->offset == DEVICE_EVIDENCE_OBJECT_START) {
		if (nla_put_u32(skb, DEVICE_EVIDENCE_A_OBJECT_TYPE,
				current_type(ctx)) ||
		    nla_put_u32(skb, DEVICE_EVIDENCE_A_OBJECT_GENERATION,
				ctx->generation) ||
		    nla_put_u32(skb, DEVICE_EVIDENCE_A_OBJECT_LENGTH,
				object_len))
			goto out_cancel;

		ctx->offset = 0;
	}

	available = skb_tailroom(skb);
	overhead = nla_total_size(0) + NLA_ALIGNTO;
	if (available <= overhead) {
		rc = -EMSGSIZE;
		goto out_cancel;
	}

	if (object_len)
		len = min(available - overhead, object_len - ctx->offset);
	else
		len = 0;

	val = len ? object_data + ctx->offset : NULL;
	rc = nla_put(skb, DEVICE_EVIDENCE_A_OBJECT_VAL, len, val);
	if (rc)
		goto out_end;

	ctx->offset += len;
	if (ctx->offset < object_len) {
		rc = 1;
		goto out_end;
	}

	/* Move to the next bit in the request mask */
	ctx->type_mask ^= 1U << current_type(ctx);

	/* no more evidence types requested */
	if (!ctx->type_mask) {
		rc = 0;
		goto out_end;
	}
	ctx->offset = DEVICE_EVIDENCE_OBJECT_START;
	rc = 1;

out_end:
	genlmsg_end(skb, hdr);
	if (rc > 0)
		return skb->len;
	return rc;

out_cancel:
	genlmsg_cancel(skb, hdr);
	return -EMSGSIZE;
}

static int device_evidence_read(struct sk_buff *skb,
				struct netlink_callback *cb)
{
	struct device_evidence_ctx *ctx = (struct device_evidence_ctx *)cb->ctx;
	const struct device_evidence_ops *ops = ctx->subsys->ops;
	const struct genl_info *info = genl_info_dump(cb);
	struct device_evidence *evidence;
	int rc;

	/* Sync against provider removing device evidence */
	evidence = ops->evidence_read_begin(ctx->dev);
	if (IS_ERR(evidence)) {
		NL_SET_ERR_MSG(info->extack,
			       "failed to acquire evidence context");
		return PTR_ERR(evidence);
	}

	/* Check that evidence stays consistent over multi-message dumps */
	if (ctx->offset == DEVICE_EVIDENCE_START) {
		ctx->generation = evidence->generation;
		ctx->offset = DEVICE_EVIDENCE_OBJECT_START;
	}

	if (ctx->generation == evidence->generation)
		rc = __device_evidence_read(skb, cb, evidence);
	else {
		NL_SET_ERR_MSG(info->extack, "evidence updated during read");
		rc = -EAGAIN;
	}

	ops->evidence_read_end(evidence);
	return rc;
}

static int device_evidence_refresh(struct device_evidence_ctx *ctx)
{
	const struct device_evidence_ops *ops = ctx->subsys->ops;

	if (!ops->refresh_evidence)
		return -EOPNOTSUPP;

	return ops->refresh_evidence(ctx->dev, ctx->nonce, ctx->nonce_len);
}

int device_evidence_nl_read_dumpit(struct sk_buff *skb,
				   struct netlink_callback *cb)
{
	struct device_evidence_ctx *ctx = (struct device_evidence_ctx *)cb->ctx;
	const struct genl_info *info = genl_info_dump(cb);

	/*
	 * When a nonce is provided, refresh the dynamic evidence, if
	 * specified by @ctx, before the dump operation.
	 */
	if (ctx->offset == DEVICE_EVIDENCE_START && ctx->nonce) {
		int rc = device_evidence_refresh(ctx);

		if (rc) {
			NL_SET_ERR_MSG_FMT(info->extack,
					   "evidence refresh failed: %pe", ERR_PTR(rc));
			return rc;
		}
		kfree(ctx->nonce);
		ctx->nonce = NULL;
	}
	return device_evidence_read(skb, cb);
}

static int __init device_evidence_nl_init(void)
{
	return genl_register_family(&device_evidence_nl_family);
}
subsys_initcall(device_evidence_nl_init);
