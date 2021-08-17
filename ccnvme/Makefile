# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the ccNVMe.
#

ccflags-y += -I$(src) 
# ccflags-y += -Wno-unused-but-set-variable
# ccflags-y += -Wno-declaration-after-statement
# ccflags-y += -DOLAYER_DEBUG
ccflags-y += -DUSE_CMB

obj-m += ccnvme.o

ccnvme-m += horae.o checkpoint.o flush.o cmb.o submitter.o debug.o recovery.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
