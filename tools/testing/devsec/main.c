// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ynl.h>
#include <linux/device-evidence.h>

#include "device-evidence-user.h"

static int usage(void)
{
	fprintf(stderr,
		"usage:\n"
		"  device-evidence dump -s <subsys> -d <dev> -t <type> -o <file|-> [-n <file|->]\n"
		"  device-evidence validate -s <subsys> -d <dev> -g <generation>\n"
		"\n"
		"  -s, --subsys      provider subsystem name (e.g. pci)\n"
		"  -d, --dev         device name (e.g. 0000:41:00.0)\n"
		"  -t, --type        evidence type name (e.g. measurements) or id (0..%u)\n"
		"  -o, --output      write raw evidence to file, or '-' for stdout\n"
		"  -n, --nonce       nonce to refresh evidence, from file or '-' for stdin\n"
		"  -g, --generation  evidence generation to validate\n",
		DEVICE_EVIDENCE_TYPE_MAX);
	return 2;
}

/* Read a nonce (at most @cap bytes) from @path, or stdin when @path is "-" */
static int read_nonce(const char *path, unsigned char *buf, size_t cap,
		      size_t *len)
{
	FILE *in = strcmp(path, "-") ? fopen(path, "rb") : stdin;
	unsigned char overflow;
	size_t n;

	if (!in) {
		perror(path);
		return 1;
	}

	n = fread(buf, 1, cap, in);
	if (ferror(in)) {
		perror(path);
		goto err;
	}
	/* Anything beyond @cap exceeds the ABI nonce limit */
	if (fread(&overflow, 1, 1, in) == 1) {
		fprintf(stderr, "nonce exceeds %zu bytes\n", cap);
		goto err;
	}

	if (in != stdin)
		fclose(in);
	*len = n;
	return 0;
err:
	if (in != stdin)
		fclose(in);
	return 1;
}

static int do_dump(struct ynl_sock *ys, const char *subsys, const char *dev,
		   unsigned int type, const char *outfile,
		   const char *noncefile)
{
	unsigned char nonce[DEVICE_EVIDENCE_MAX_NONCE_SIZE];
	struct device_evidence_read_req *req;
	struct device_evidence_read_list *list;
	size_t nonce_len = 0;
	int rc = 1;

	if (type > DEVICE_EVIDENCE_TYPE_MAX) {
		fprintf(stderr, "type %u out of range (max %u)\n", type,
			DEVICE_EVIDENCE_TYPE_MAX);
		return 1;
	}

	if (noncefile &&
	    read_nonce(noncefile, nonce, sizeof(nonce), &nonce_len))
		return 1;

	req = device_evidence_read_req_alloc();
	device_evidence_read_req_set_type_mask(req, 1U << type);
	device_evidence_read_req_set_subsys(req, subsys);
	device_evidence_read_req_set_dev_name(req, dev);
	if (nonce_len)
		device_evidence_read_req_set_nonce(req, nonce, nonce_len);

	list = device_evidence_read_dump(ys, req);
	device_evidence_read_req_free(req);
	if (!list) {
		fprintf(stderr, "read failed: %s\n", ys->err.msg);
		return 1;
	}

	ynl_dump_foreach(list, obj)
	{
		FILE *out;

		if (!obj->_present.type || obj->type != type)
			continue;

		fprintf(stderr,
			"{ \"type\" : %u, \"generation\" : %u, \"length\" : %u, \"received\" : %u }\n",
			obj->type,
			obj->_present.generation ? obj->generation : 0,
			obj->_present.length ? obj->length : 0, obj->_len.val);

		if (!strcmp(outfile, "-")) {
			out = stdout;
		} else {
			out = fopen(outfile, "wb");
			if (!out) {
				perror(outfile);
				goto out;
			}
		}

		if (obj->_len.val && fwrite(obj->val->data, 1, obj->_len.val,
					    out) != obj->_len.val) {
			perror("fwrite");
			if (out != stdout)
				fclose(out);
			goto out;
		}

		if (out != stdout)
			fclose(out);
		rc = 0;
		goto out;
	}

	fprintf(stderr, "evidence type %u not present for %s:%s\n", type,
		subsys, dev);
out:
	device_evidence_read_list_free(list);
	return rc;
}

static int do_validate(struct ynl_sock *ys, const char *subsys, const char *dev,
		       unsigned int generation)
{
	struct device_evidence_validate_req *req;
	struct device_evidence_validate_rsp *rsp;

	req = device_evidence_validate_req_alloc();
	device_evidence_validate_req_set_subsys(req, subsys);
	device_evidence_validate_req_set_dev_name(req, dev);
	device_evidence_validate_req_set_generation(req, generation);

	rsp = device_evidence_validate(ys, req);
	device_evidence_validate_req_free(req);
	if (!rsp) {
		fprintf(stderr, "validate failed: %s\n", ys->err.msg);
		return 1;
	}

	device_evidence_validate_rsp_free(rsp);
	printf("validated %s:%s at generation %u\n", subsys, dev, generation);
	return 0;
}

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
static const struct {
	const char *name;
	unsigned int type;
} evidence_types[] = {
	{ "cert0", DEVICE_EVIDENCE_TYPE_CERT0 },
	{ "cert1", DEVICE_EVIDENCE_TYPE_CERT1 },
	{ "cert2", DEVICE_EVIDENCE_TYPE_CERT2 },
	{ "cert3", DEVICE_EVIDENCE_TYPE_CERT3 },
	{ "cert4", DEVICE_EVIDENCE_TYPE_CERT4 },
	{ "cert5", DEVICE_EVIDENCE_TYPE_CERT5 },
	{ "cert6", DEVICE_EVIDENCE_TYPE_CERT6 },
	{ "cert7", DEVICE_EVIDENCE_TYPE_CERT7 },
	{ "vca", DEVICE_EVIDENCE_TYPE_VCA },
	{ "measurements", DEVICE_EVIDENCE_TYPE_MEASUREMENTS },
	{ "report", DEVICE_EVIDENCE_TYPE_REPORT },
};

/* Parse an evidence type given by name (e.g. "measurements") or numeric id. */
static long parse_type(const char *s)
{
	unsigned long id;
	char *end;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(evidence_types); i++)
		if (!strcmp(s, evidence_types[i].name))
			return evidence_types[i].type;

	id = strtoul(s, &end, 0);
	if (*s && !*end && id <= DEVICE_EVIDENCE_TYPE_MAX)
		return id;

	return -1;
}

static int cmd_dump(int argc, char **argv, struct ynl_sock *ys)
{
	static const struct option opts[] = {
		{ "subsys", required_argument, NULL, 's' },
		{ "dev", required_argument, NULL, 'd' },
		{ "type", required_argument, NULL, 't' },
		{ "output", required_argument, NULL, 'o' },
		{ "nonce", required_argument, NULL, 'n' },
		{}
	};
	const char *subsys = NULL, *dev = NULL, *output = NULL, *nonce = NULL;
	long type = -1;
	int c;

	while ((c = getopt_long(argc, argv, "s:d:t:o:n:", opts, NULL)) != -1) {
		switch (c) {
		case 's':
			subsys = optarg;
			break;
		case 'd':
			dev = optarg;
			break;
		case 't':
			type = parse_type(optarg);
			if (type < 0) {
				fprintf(stderr, "unknown evidence type '%s'\n",
					optarg);
				return usage();
			}
			break;
		case 'o':
			output = optarg;
			break;
		case 'n':
			nonce = optarg;
			break;
		default:
			return usage();
		}
	}

	if (!subsys)
		subsys = "pci";

	if (!dev || type < 0 || !output)
		return usage();

	return do_dump(ys, subsys, dev, type, output, nonce);
}

static int cmd_validate(int argc, char **argv, struct ynl_sock *ys)
{
	static const struct option opts[] = {
		{ "subsys", required_argument, NULL, 's' },
		{ "dev", required_argument, NULL, 'd' },
		{ "generation", required_argument, NULL, 'g' },
		{}
	};
	const char *subsys = NULL, *dev = NULL;
	long generation = -1;
	int c;

	while ((c = getopt_long(argc, argv, "s:d:g:", opts, NULL)) != -1) {
		switch (c) {
		case 's':
			subsys = optarg;
			break;
		case 'd':
			dev = optarg;
			break;
		case 'g':
			generation = strtol(optarg, NULL, 0);
			break;
		default:
			return usage();
		}
	}

	if (!subsys)
		subsys = "pci";

	if (!dev || generation < 0)
		return usage();

	return do_validate(ys, subsys, dev, generation);
}

static const struct ynl_family ynl_device_evidence = {
	.name = "device-evidence",
	.hdr_len = sizeof(struct genlmsghdr),
};

int main(int argc, char **argv)
{
	struct ynl_error yerr;
	struct ynl_sock *ys;
	int rc;

	if (argc < 2)
		return usage();

	ys = ynl_sock_create(&ynl_device_evidence, &yerr);
	if (!ys) {
		fprintf(stderr, "YNL: %s\n", yerr.msg);
		return 1;
	}

	/* Sub-command consumes argv[1]; getopt parses the remainder. */
	if (!strcmp(argv[1], "dump"))
		rc = cmd_dump(argc - 1, argv + 1, ys);
	else if (!strcmp(argv[1], "validate"))
		rc = cmd_validate(argc - 1, argv + 1, ys);
	else
		rc = usage();

	ynl_sock_destroy(ys);
	return rc;
}
