.. SPDX-License-Identifier: GPL-2.0
.. include:: <isonum.txt>

========================================================
PCI Trusted Execution Environment Security Manager (TSM)
========================================================

Overview
========

A "TSM", as detailed by PCIe r7.0 section 11 "TEE Device Interface
Security Protocol (TDISP)", is an entity within the platform's Trusted
Computing Base (TCB) that enforces security policies on the host. It
serves to mitigate a threat model where devices may be under the control
of an adversary. The adversarial threats are:

- Identity: Device may be mimicking a legitimate device identity / firmware
- Physical: link may be under observation, or control (reorder / drop data)
- Virtual: Device MMIO presented to a guest may not actually map the
  device, device DMA may be redirected.

In Linux a "tsm" is a broader concept. It is a class device interface to
mitigate one or more of the above threats. A "tsm driver" registers a
tsm device that publishes either the 'tsm/connect' or
'tsm/{lock,accept}' set of attributes for the PCIe device. The typical
expectation is that 'tsm/{lock,accept}' is published by a guest "tsm
driver" to mitigate "Virtual" threats. The 'tsm/connect' interface is
published by a host "tsm driver" to mitigate "Identity" and/or
"Physical" threats.

Device Interface LOCK
=====================
The lock operation facilitated by tsm/lock (see
Documentation/ABI/testing/sysfs-bus-pci) places the device in a mode
where any security sensitive changes to the device configuration results
in the device transitioning to the ERROR state. The device presents
signed evidence of its LOCK state to the kernel through the tsm driver.
The relying party is responsible for verifying not only the evidence but
that the device is trusted to maintain those attested values while
locked. Accepting the locked configuration moves the device to the RUN
state where it will accept private (T=1) traffic to its MMIO interface.

Note that the RUN state is necessary, but not sufficient for the device
to access private host memory (send T=1 DMA). That is only enabled after
the device's trust level has been elevated to indicate it can operate
within the TCB.

A device in the RUN state is trusted to cease TCB interactions (send T=1
DMA / accept T=1 MMIO TLPs) when it is next unlocked or encounters a
security relevant error. The TSM is responsible for enforcing that the
device is not unlocked within the interval between evidence collection
and acceptance, by correlating the evidence from LOCK to the subsequent
RUN request.

Subsystem Interfaces
====================

.. kernel-doc:: include/linux/pci-ide.h
   :internal:

.. kernel-doc:: drivers/pci/ide.c
   :export:

.. kernel-doc:: include/linux/pci-tsm.h
   :internal:

.. kernel-doc:: drivers/pci/tsm.c
   :export:
