/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/device-evidence.yaml */
/* YNL-GEN kernel header */
/* To regenerate run: tools/net/ynl/ynl-regen.sh */

#ifndef _LINUX_DEVICE_EVIDENCE_GEN_H
#define _LINUX_DEVICE_EVIDENCE_GEN_H

#include <net/netlink.h>
#include <net/genetlink.h>

#include <uapi/linux/device-evidence.h>

int device_evidence_nl_read_pre(struct netlink_callback *cb);
int device_evidence_nl_read_post(struct netlink_callback *cb);

int device_evidence_nl_read_dumpit(struct sk_buff *skb,
				   struct netlink_callback *cb);

extern struct genl_family device_evidence_nl_family;

#endif /* _LINUX_DEVICE_EVIDENCE_GEN_H */
