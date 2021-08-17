#!/bin/bash
TEST_DEVS=${TEST_DEVS}
THREADS="1 4 8 12 24"
BLK_SIZE=4

SETUP=$1
DURATION=60
TEST_MOUNT_POINT=/mnt/test
OUT_DIR=res/
MODULE_DIR=mod/

# prepare
mkdir ${OUT_DIR} > /dev/null
echo -n "" > ${OUT_DIR}/${SETUP}.out # clear output

for i in ${TEST_DEVS}
do

# insmod 
if [ ${SETUP} == "+ccnvme" ];then
insmod $MODULE_DIR/ccnvme_${SETUP}.ko cp_device=${i}p1 nr_streams=4
insmod $MODULE_DIR/mqfs_${SETUP}.ko 
elif [ ${SETUP} == "+mqjournal" -o ${SETUP} == "+metapaging" ];then
insmod $MODULE_DIR/ccnvme_${SETUP}.ko cp_device=${i}p1 nr_streams=24
insmod $MODULE_DIR/mqfs_${SETUP}.ko
fi

# mkfs
if [ ${SETUP} == "base" ];then
mkfs.ext4 -E lazy_itable_init=0,lazy_journal_init=0 ${i}p2 > /dev/null
else
mkfs.ext4 -O ^has_journal -E lazy_itable_init=0,lazy_journal_init=0 ${i}p2 > /dev/null
fi

# mount
if [ ${SETUP} == "+ccnvme" ];then
mount -t mqfs -o nr_streams=4 ${i}p2 $TEST_MOUNT_POINT
elif [ ${SETUP} == "+mqjournal" -o ${SETUP} == "+metapaging" ];then
mount -t mqfs -o nr_streams=24 ${i}p2 $TEST_MOUNT_POINT
else
mount -t ext4 ${i}p2 $TEST_MOUNT_POINT
fi

# test size
s=12000

echo $i "begin" >> ${OUT_DIR}/${SETUP}.out
# run fio
for j in $THREADS
do
    rm -rf $TEST_MOUNT_POINT/*
    sync
	echo 3 > /proc/sys/vm/drop_caches
	fio --name=seq-write --directory=${TEST_MOUNT_POINT} --rw=write --file_append=1 --bs=4k --direct=0 --numjobs=${j} --size=${s}M --time_based=1 --runtime=$DURATION --ioengine=psync --fsync=1 --group_reporting=1 >> ${OUT_DIR}/${SETUP}.out
    sleep 5
done
echo $i "end" >> ${OUT_DIR}/${SETUP}.out

umount $TEST_MOUNT_POINT

if [ ${SETUP} != "base" ];then
rmmod mqfs.ko
rmmod ccnvme.ko
fi

sleep 5
done
