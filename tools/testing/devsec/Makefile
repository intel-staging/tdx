# SPDX-License-Identifier: GPL-2.0
YNL_DIR  := ../../net/ynl
KHDR_DIR := $(YNL_DIR)/../../..
SPEC     := $(KHDR_DIR)/Documentation/netlink/specs/device-evidence.yaml
GEN      := $(YNL_DIR)/pyynl/ynl_gen_c.py

CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter
CFLAGS += -I$(YNL_DIR)/lib -I. -idirafter $(KHDR_DIR)/include/uapi

PROG := device-evidence
OBJS := main.o device-evidence-user.o ynl.o

all: $(PROG)

device-evidence-user.h: $(SPEC) $(GEN)
	$(GEN) --mode user --header --spec $< -o $@
device-evidence-user.c: $(SPEC) $(GEN)
	$(GEN) --mode user --source --spec $< -o $@

ynl.o: $(YNL_DIR)/lib/ynl.c
	$(CC) $(CFLAGS) -c -o $@ $<

main.o: main.c device-evidence-user.h
device-evidence-user.o: device-evidence-user.c device-evidence-user.h

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lm

clean:
	rm -f $(PROG) $(OBJS) device-evidence-user.c device-evidence-user.h

.PHONY: all clean
