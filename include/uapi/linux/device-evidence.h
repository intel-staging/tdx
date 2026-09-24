/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/device-evidence.yaml */
/* YNL-GEN uapi header */
/* To regenerate run: tools/net/ynl/ynl-regen.sh */

#ifndef _UAPI_LINUX_DEVICE_EVIDENCE_H
#define _UAPI_LINUX_DEVICE_EVIDENCE_H

#define DEVICE_EVIDENCE_FAMILY_NAME	"device-evidence"
#define DEVICE_EVIDENCE_FAMILY_VERSION	1

#define DEVICE_EVIDENCE_MAX_OBJECT_SIZE	16777216
#define DEVICE_EVIDENCE_MAX_NONCE_SIZE	32

/**
 * enum device_evidence_type - Device security evidence objects
 * @DEVICE_EVIDENCE_TYPE_CERT0: SPDM certificate chain from device slot0
 * @DEVICE_EVIDENCE_TYPE_CERT1: SPDM certificate chain from device slot1
 * @DEVICE_EVIDENCE_TYPE_CERT2: SPDM certificate chain from device slot2
 * @DEVICE_EVIDENCE_TYPE_CERT3: SPDM certificate chain from device slot3
 * @DEVICE_EVIDENCE_TYPE_CERT4: SPDM certificate chain from device slot4
 * @DEVICE_EVIDENCE_TYPE_CERT5: SPDM certificate chain from device slot5
 * @DEVICE_EVIDENCE_TYPE_CERT6: SPDM certificate chain from device slot6
 * @DEVICE_EVIDENCE_TYPE_CERT7: SPDM certificate chain from device slot7
 * @DEVICE_EVIDENCE_TYPE_VCA: SPDM version, capabilities, and algorithms
 *   transcript negotiated at session establishment. An implementation may not
 *   provide this separately and instead include it in the measurements
 *   transcript.
 * @DEVICE_EVIDENCE_TYPE_MEASUREMENTS: SPDM GET_MEASUREMENTS response
 * @DEVICE_EVIDENCE_TYPE_REPORT: A bus that implements a device interface
 *   security protocol like TDISP may convey an interface report that details
 *   interface settings and capabilities.
 */
enum device_evidence_type {
	DEVICE_EVIDENCE_TYPE_CERT0,
	DEVICE_EVIDENCE_TYPE_CERT1,
	DEVICE_EVIDENCE_TYPE_CERT2,
	DEVICE_EVIDENCE_TYPE_CERT3,
	DEVICE_EVIDENCE_TYPE_CERT4,
	DEVICE_EVIDENCE_TYPE_CERT5,
	DEVICE_EVIDENCE_TYPE_CERT6,
	DEVICE_EVIDENCE_TYPE_CERT7,
	DEVICE_EVIDENCE_TYPE_VCA,
	DEVICE_EVIDENCE_TYPE_MEASUREMENTS,
	DEVICE_EVIDENCE_TYPE_REPORT,

	/* private: */
	__DEVICE_EVIDENCE_TYPE_MAX,
	DEVICE_EVIDENCE_TYPE_MAX = (__DEVICE_EVIDENCE_TYPE_MAX - 1)
};

/*
 * Device security evidence request flags
 */
enum device_evidence_type_flag {
	DEVICE_EVIDENCE_TYPE_FLAG_CERT0 = 1,
	DEVICE_EVIDENCE_TYPE_FLAG_CERT1 = 2,
	DEVICE_EVIDENCE_TYPE_FLAG_CERT2 = 4,
	DEVICE_EVIDENCE_TYPE_FLAG_CERT3 = 8,
	DEVICE_EVIDENCE_TYPE_FLAG_CERT4 = 16,
	DEVICE_EVIDENCE_TYPE_FLAG_CERT5 = 32,
	DEVICE_EVIDENCE_TYPE_FLAG_CERT6 = 64,
	DEVICE_EVIDENCE_TYPE_FLAG_CERT7 = 128,
	DEVICE_EVIDENCE_TYPE_FLAG_VCA = 256,
	DEVICE_EVIDENCE_TYPE_FLAG_MEASUREMENTS = 512,
	DEVICE_EVIDENCE_TYPE_FLAG_REPORT = 1024,

	/* private: */
	DEVICE_EVIDENCE_TYPE_FLAG_MASK = 2047,
};

/**
 * enum device_evidence_flag - Flags to control evidence retrieval
 * @DEVICE_EVIDENCE_FLAG_DIGEST: Request a secure hash of objects like vca and
 *   measurements. The expectation is that this digest is produced by a
 *   responder within the TCB like a platform TSM. It validates a blob that may
 *   have traversed a transport without integrity protections.
 */
enum device_evidence_flag {
	DEVICE_EVIDENCE_FLAG_DIGEST = 1,

	/* private: */
	DEVICE_EVIDENCE_FLAG_MASK = 1,
};

enum {
	DEVICE_EVIDENCE_A_OBJECT_TYPE = 1,
	DEVICE_EVIDENCE_A_OBJECT_TYPE_MASK,
	DEVICE_EVIDENCE_A_OBJECT_FLAGS,
	DEVICE_EVIDENCE_A_OBJECT_SUBSYS,
	DEVICE_EVIDENCE_A_OBJECT_DEV_NAME,
	DEVICE_EVIDENCE_A_OBJECT_NONCE,
	DEVICE_EVIDENCE_A_OBJECT_GENERATION,
	DEVICE_EVIDENCE_A_OBJECT_COUNT,
	DEVICE_EVIDENCE_A_OBJECT_LENGTH,
	DEVICE_EVIDENCE_A_OBJECT_VAL,

	__DEVICE_EVIDENCE_A_OBJECT_MAX,
	DEVICE_EVIDENCE_A_OBJECT_MAX = (__DEVICE_EVIDENCE_A_OBJECT_MAX - 1)
};

enum {
	DEVICE_EVIDENCE_CMD_READ = 1,

	__DEVICE_EVIDENCE_CMD_MAX,
	DEVICE_EVIDENCE_CMD_MAX = (__DEVICE_EVIDENCE_CMD_MAX - 1)
};

#endif /* _UAPI_LINUX_DEVICE_EVIDENCE_H */
