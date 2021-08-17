# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux mqfs routines.
#
ccflags-y += -I$(src)/../ # include ccnvme
ccflags-y += -Wno-declaration-after-statement
#ccflags-y += -DFATOMIC_FILE

obj-m += mqfs.o

mqfs-m	:= balloc.o bitmap.o block_validity.o dir.o ext4_jbd2.o extents.o \
		extents_status.o file.o fsmap.o fsync.o hash.o ialloc.o \
		indirect.o inline.o inode.o ioctl.o mballoc.o migrate.o \
		mmp.o move_extent.o namei.o page-io.o readpage.o resize.o \
		super.o symlink.o sysfs.o xattr.o xattr_trusted.o xattr_user.o \
		filemap_compat.o page_sharing.o

mqfs-$(CONFIG_EXT4_FS_POSIX_ACL)	+= acl.o
mqfs-$(CONFIG_EXT4_FS_SECURITY)		+= xattr_security.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) KBUILD_EXTRA_SYMBOLS=$(PWD)/../ccnvme/Module.symvers modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
