#!/bin/bash
TEST_DEV=$2 # Intel 905p SSD
FXMARK_DIR=/home/lxj/horae+/fxmark/
SYS=$1

OUT_DIR=res/
MODULE_DIR=mod/
TEST_MOUNT_POINT=/mnt/test

# prepare
mkdir ${OUT_DIR} > /dev/null
echo -n "" > ${OUT_DIR}/${SYS}.out # clear output

# insmod 
if [ ${SYS} == "mqfs" ];then
insmod ../../ccnvme/ccnvme.ko cp_device=${TEST_DEV}p1 nr_streams=24
# make sure to enable performance analysis in MQFS code 
# see the macro PERF_DECOMPOSE in ext4.h
insmod ${MODULE_DIR}/mqfs_perf.ko
else 
# we add the functionarity of the performance analysis in the original ext4 code 
# see /home/lxj/horae+/ext4-debug/ for details
insmod ${MODULE_DIR}/ext4-debug.ko 
fi

# mkfs
mkfs.ext4 -O ^has_journal -E lazy_itable_init=0,lazy_journal_init=0 ${TEST_DEV}p2

# mount 
if [ ${SYS} == "mqfs" ];then
mount -t mqfs -o nr_streams=24 ${TEST_DEV}p2 ${TEST_MOUNT_POINT}
else
mount -t ext4-debug ${TEST_DEV}p2 ${TEST_MOUNT_POINT}
fi

# test
echo 3 > /proc/sys/vm/drop_caches
${FXMARK_DIR}/bin/fxmark --type MWCL --ncore 1 --root=${TEST_MOUNT_POINT} --times=1000000 --duration=120 --directio=0

# dump the results through sysfs
if [ ${SYS} == "mqfs" ];then
cat /sys/fs/mqfs/${TEST_DEV:5}p2/fsync_time_decompose >> ${OUT_DIR}/${SYS}.out
else
cat /sys/fs/ext4-debug/${TEST_DEV:5}p2/fsync_time_decompose >> ${OUT_DIR}/${SYS}.out
fi

# post test
umount ${TEST_MOUNT_POINT}

if [ ${SYS} == "mqfs" ];then
rmmod mqfs
rmmod ccnvme
else 
rmmod ext4-debug
fi