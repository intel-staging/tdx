#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2025-2026 Intel Corporation

# Checkout PCI/TSM sysfs and driver-core mechanics with the
# devsec_link_tsm and devsec_tsm sample modules from samples/devsec/.

set -ex

trap 'err $LINENO' ERR
err() {
        echo $(basename "$0"): failed at line "$1"
        [ -n "$2" ] && "$2"
        exit 1
}

DEVSEC_DIR=$(dirname "$0")
ORDER=""

setup_modules() {
	if [[ $ORDER == "bus" ]]; then
		modprobe devsec_bus
		modprobe devsec_link_tsm
		modprobe devsec_tsm
	else
		modprobe devsec_tsm
		modprobe devsec_link_tsm
		modprobe devsec_bus
	fi
}

teardown_modules() {
	if [[ $ORDER == "bus" ]]; then
		modprobe -r devsec_tsm
		modprobe -r devsec_link_tsm
		modprobe -r devsec_bus
	else
		modprobe -r devsec_bus
		modprobe -r devsec_link_tsm
		modprobe -r devsec_tsm
	fi
}

PCI_DEVS=(
"/sys/bus/pci/devices/10000:01:00.0"
"/sys/bus/pci/devices/10001:03:00.0"
)
FN_DEVS=(
"/sys/bus/pci/devices/10000:01:00.1"
"/sys/bus/pci/devices/10001:03:00.1"
)
tsm_devsec=""
tsm_link=""
devsec_pci="/sys/bus/pci/drivers/devsec_pci"

tdisp_test() {
	pci_dev=${PCI_DEVS[$1]}
	fn_dev=${FN_DEVS[$1]}
	host_bridge=$(dirname $(dirname $(readlink -f "$pci_dev")))

	# with the device disconnected from the devsec TSM validate that
	# the devsec_pci driver loads and honors the trust policy
	echo "devsec_pci" > "$pci_dev"/driver_override
	modprobe devsec_pci "trust=none"

	[[ -e $pci_dev/driver ]] && err "$LINENO"

	modprobe -r devsec_pci
	modprobe devsec_pci "trust=auto"
	expect="echo: write error: Device or resource busy"
	echo $(basename "$pci_dev") 2>&1 > $devsec_pci/bind | grep -q "$expect" || err "$LINENO"
	basename "$pci_dev" > $devsec_pci/unbind

	#TODO test trust=tcb after sample pci_dma_configure() enabled

# Disable MMIO resource checks until arch plumbing for encrypted_iomem lands
#	# grab the device's resource from /proc/iomem
#	resource=$(cat /proc/iomem | grep -m1 $(basename $pci_dev) | awk -F ' :' '{print $1}' | tr -d ' ')
#	[[ -n $resource ]] || err "$LINENO"
#
	# lock and accept the device, validate that the resource is now
	# marked encrypted
	basename "$tsm_devsec" > "$pci_dev"/tsm/lock
	echo 1 > "$pci_dev"/tsm/accept
#
#	cat /proc/iomem | grep "$resource" | grep -q -m1 "PCI MMIO Encrypted" || err "$LINENO"
#
#	# validate that the driver now fails with -EINVAL when trying to
#	# bind
#	expect="echo: write error: Invalid argument"
#	echo $(basename $pci_dev) 2>&1 > $devsec_pci/bind | grep -q "$expect" || err "$LINENO"
#
#	# unlock and validate that the encrypted mmio is removed
	basename "$tsm_devsec" > "$pci_dev"/tsm/unlock
#	cat /proc/iomem | grep "$resource" | grep -q "PCI MMIO Encrypted" && err "$LINENO"

	modprobe -r devsec_pci
}

validate_disconnected() {
	pci_dev=${PCI_DEVS[$1]}
	fn_dev=${FN_DEVS[$1]}
	host_bridge=$(dirname $(dirname $(readlink -f "$pci_dev")))

	# validate that the dsm is not yet detected and that the sub-function
	# is aware of any TSM capabilities
	dsm=$(cat "$pci_dev"/tsm/dsm) || err "$LINENO from $2"
	bound=$(cat "$pci_dev"/tsm/bound) || err "$LINENO from $2"
	[[ -z $dsm ]] || err "$LINENO from $2"
	[[ -z $bound ]] || err "$LINENO from $2"
	[[ ! -e $fn_dev/tsm/dsm ]] || err "$LINENO from $2"
	[[ ! -e $fn_dev/tsm/bound ]] || err "$LINENO from $2"
	[[ ! -e $fn_dev/tsm/connect ]] || err "$LINENO from $2"
	[[ ! -e $fn_dev/tsm/disconnect ]] || err "$LINENO from $2"
}

# check that all devices can be connected simultaneously
ide_multi_test() {
	for pci_dev in ${PCI_DEVS[@]}; do
		basename "$tsm_link" > "$pci_dev"/tsm/connect
	done

	#check stream links show up and point back to the pci_dev
	for pci_dev in ${PCI_DEVS[@]}; do
		host_bridge=$(dirname $(dirname $(readlink -f "$pci_dev")))
		hb=$(basename "$host_bridge")
		[[ -e $host_bridge/stream0.0.0 ]] || err "$LINENO"
		[[ -e $tsm_link/$hb/stream0.0.0 ]] || err "$LINENO"
		[[ $(readlink -f "$tsm_link/$hb/stream0.0.0") == $(readlink -f "$pci_dev") ]] || err "$LINENO"
	done

	for pci_dev in ${PCI_DEVS[@]}; do
		basename "$tsm_link" > "$pci_dev"/tsm/disconnect
	done
}

CERT_SIZE=$((16<<20))
MEASUREMENT_SIZE=$((4<<10))

declare -A evidence=(
	["cert0"]=$CERT_SIZE
	["cert1"]=0
	["cert2"]=0
	["cert3"]=0
	["cert4"]=0
	["cert5"]=0
	["cert6"]=0
	["cert7"]=0
	["vca"]=0
	["measurements"]=$MEASUREMENT_SIZE
	["report"]=0
)

check_evidence() {
	pdev=$(basename $1)
	dump="$DEVSEC_DIR/device-evidence dump"

	for blob in "${!evidence[@]}"
	do
		expect=${evidence[$blob]}
		size=$($dump -d $pdev -t $blob -o /dev/null 2>&1 | jq -r '.received')
		[[ $size == $expect ]] || err $LINENO
	done
}

ide_test() {
	pci_dev=${PCI_DEVS[$1]}
	fn_dev=${FN_DEVS[$1]}
	host_bridge=$(dirname $(dirname $(readlink -f "$pci_dev")))

	# validate that all of the secure streams are idle by default
	hb=$(basename "$host_bridge")
	nr=$(cat "$host_bridge"/available_secure_streams)
	[[ $nr == 4 ]] || err "$LINENO"

	validate_disconnected "$1" $LINENO

	# connect a stream and validate that the stream link shows up at
	# the host bridge and the TSM
	basename "$tsm_link" > "$pci_dev"/tsm/connect
	nr=$(cat "$host_bridge"/available_secure_streams)
	[[ $nr == 3 ]] || err "$LINENO"

	[[ $(cat "$pci_dev"/tsm/connect) == $(basename "$tsm_link") ]] || err "$LINENO"
	[[ -e $host_bridge/stream0.0.0 ]] || err "$LINENO"
	[[ -e $tsm_link/$hb/stream0.0.0 ]] || err "$LINENO"

	# with the DSM connected (PF0), validate both it and its
	# sub-function (PF1) populate tsm/dsm with the PF0 device.
	dsm=$(cat "$pci_dev"/tsm/dsm)
	[[ $dsm == $(basename "$pci_dev") ]] || err "$LINENO"
	dsm=$(cat "$fn_dev"/tsm/dsm)
	[[ $dsm == $(basename "$pci_dev") ]] || err "$LINENO"

	check_evidence $pci_dev

	# bind both functions and validate that they display bound to
	# the TSM device
	basename "$pci_dev" > "$tsm_link"/device/tsm_bind
	bound=$(cat "$pci_dev"/tsm/bound)
	[[ $bound == $(basename "$tsm_link") ]] || err "$LINENO"
	basename "$fn_dev" > "$tsm_link"/device/tsm_bind
	bound=$(cat "$fn_dev"/tsm/bound)
	[[ $bound == $(basename "$tsm_link") ]] || err "$LINENO"

	# test manual unbind
	basename "$pci_dev" > "$tsm_link"/device/tsm_unbind
	bound=$(cat "$pci_dev"/tsm/bound)
	[[ -z $bound ]] || err "$LINENO"
	basename "$fn_dev" > "$tsm_link"/device/tsm_unbind
	bound=$(cat "$fn_dev"/tsm/bound)
	[[ -z $bound ]] || err "$LINENO"

	# rebind to test automatic unbind at disconnect
	basename "$pci_dev" > "$tsm_link"/device/tsm_bind
	basename "$fn_dev" > "$tsm_link"/device/tsm_bind

	# check that the links disappear at disconnect and the stream
	# pool is refilled
	basename "$tsm_link" > "$pci_dev"/tsm/disconnect
	nr=$(cat "$host_bridge"/available_secure_streams)
	[[ $nr == 4 ]] || err "$LINENO"

	validate_disconnected "$1" $LINENO

	[[ $(cat "$pci_dev"/tsm/connect) == "" ]] || err "$LINENO"
	[[ ! -e $host_bridge/stream0.0.0 ]] || err "$LINENO"
	[[ ! -e $tsm_link/$hb/stream0.0.0 ]] || err "$LINENO"
}

reconnect() {
	pci_dev=${PCI_DEVS[$1]}
	fn_dev=${FN_DEVS[$1]}
	host_bridge=$(dirname $(dirname $(readlink -f "$pci_dev")))

	# reconnect to prepare for surprise removal of the TSM or device
	basename "$tsm_link" > "$pci_dev"/tsm/connect
	[[ $(cat "$pci_dev"/tsm/connect) == $(basename "$tsm_link") ]] || err "$LINENO"
	[[ -e $host_bridge/stream0.0.0 ]] || err "$LINENO"
	[[ -e $tsm_link/$hb/stream0.0.0 ]] || err "$LINENO"
}

devsec_test() {
	teardown_modules
	setup_modules

	# find the tsm devices by personality
	for tsm in /sys/class/tsm/tsm*; do
		mode=$(cat "$tsm"/pci_mode)
		[[ $mode == "devsec" ]] && tsm_devsec=$tsm
		[[ $mode == "link" ]] && tsm_link=$tsm
	done
	[[ -n $tsm_devsec ]] || err "$LINENO"
	[[ -n $tsm_link ]] || err "$LINENO"

	# initialize evidence payloads
	dd if=/dev/zero of=/sys/bus/faux/devices/devsec_link_tsm/certs bs=$CERT_SIZE count=1
	dd if=/dev/zero of=/sys/bus/faux/devices/devsec_link_tsm/transcript bs=$MEASUREMENT_SIZE count=1

	# check that devsec bus loads correctly and the TSM is detected
	for i in ${!PCI_DEVS[@]}; do
		pci_dev=${PCI_DEVS[$i]}
		[[ -e $pci_dev ]] || err "$LINENO"
		[[ -e $pci_dev/tsm ]] || err "$LINENO"
	done

	ide_multi_test
	ide_test 0
	tdisp_test 0

	reconnect 0
	teardown_modules
}

ORDER="bus"
devsec_test
ORDER="tsm"
devsec_test
