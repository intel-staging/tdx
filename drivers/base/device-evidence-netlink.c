// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/device-evidence.yaml */
/* YNL-GEN kernel source */
/* To regenerate run: tools/net/ynl/ynl-regen.sh */

#include <net/netlink.h>
#include <net/genetlink.h>

#include "device-evidence-netlink.h"

#include <uapi/linux/device-evidence.h>

/* DEVICE_EVIDENCE_CMD_READ - dump */
static const struct nla_policy device_evidence_read_nl_policy[DEVICE_EVIDENCE_A_OBJECT_NONCE + 1] = {
	[DEVICE_EVIDENCE_A_OBJECT_TYPE_MASK] = { .type = NLA_U32, },
	[DEVICE_EVIDENCE_A_OBJECT_FLAGS] = { .type = NLA_U32, },
	[DEVICE_EVIDENCE_A_OBJECT_SUBSYS] = { .type = NLA_NUL_STRING, },
	[DEVICE_EVIDENCE_A_OBJECT_DEV_NAME] = { .type = NLA_NUL_STRING, },
	[DEVICE_EVIDENCE_A_OBJECT_NONCE] = NLA_POLICY_MAX_LEN(DEVICE_EVIDENCE_MAX_NONCE_SIZE),
};

/* Ops table for device_evidence */
static const struct genl_split_ops device_evidence_nl_ops[] = {
	{
		.cmd		= DEVICE_EVIDENCE_CMD_READ,
		.start		= device_evidence_nl_read_pre,
		.dumpit		= device_evidence_nl_read_dumpit,
		.done		= device_evidence_nl_read_post,
		.policy		= device_evidence_read_nl_policy,
		.maxattr	= DEVICE_EVIDENCE_A_OBJECT_NONCE,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DUMP,
	},
};

struct genl_family device_evidence_nl_family __ro_after_init = {
	.name		= DEVICE_EVIDENCE_FAMILY_NAME,
	.version	= DEVICE_EVIDENCE_FAMILY_VERSION,
	.netnsok	= true,
	.parallel_ops	= true,
	.module		= THIS_MODULE,
	.split_ops	= device_evidence_nl_ops,
	.n_split_ops	= ARRAY_SIZE(device_evidence_nl_ops),
};
