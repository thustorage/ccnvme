#!/bin/bash
TEST_DEV=$1
FS_TYPE="ext4 horaefs ext4-nj mqfs mqfs-atomic"
BLK_SIZE="4 8 16 32 64 128"
THREADS="1 4 8 12 16 20 24"

TEST_PATH=/mnt/test
HORAEFS_DIR=/home/lxj/horae+/

# prepare
mkdir fig11a
mkdir fig11b
mkdir fig11c
mkdir fig11d

for i in ${FS_TYPE}
do
# insmod and mkfs
if [ $i == "ext4" ];then
mkfs.ext4 -E lazy_itable_init=0,lazy_journal_init=0 ${TEST_DEV}p2 > /dev/null
elif [ $i == "horaefs" ]; then
insmod ${HORAEFS_DIR}/olayerv2/olayerv2.ko cp_device=${TEST_DEV}p1 nr_streams=24
insmod ${HORAEFS_DIR}/horaefsv2/horaefsv2.ko
mkfs.ext4 -O ^has_journal -E lazy_itable_init=0,lazy_journal_init=0 ${TEST_DEV}p2 > /dev/null
elif [ $i == "ext4-nj" ]; then
mkfs.ext4 -O ^has_journal -E lazy_itable_init=0,lazy_journal_init=0 ${TEST_DEV}p2 > /dev/null
elif [ $i == "mqfs" ]; then
insmod ../../ccnvme/ccnvme.ko cp_device=${TEST_DEV}p1 nr_streams=24
insmod ../../mqfs/mqfs.ko 
mkfs.ext4 -O ^has_journal -E lazy_itable_init=0,lazy_journal_init=0 ${TEST_DEV}p2 > /dev/null
elif [ $i == "mqfs-atomic" ]; then
insmod ../../ccnvme/ccnvme.ko cp_device=${TEST_DEV}p1 nr_streams=24
insmod ./mqfs-atomic.ko # ensure to use the atomic version of MQFS
mkfs.ext4 -O ^has_journal -E lazy_itable_init=0,lazy_journal_init=0 ${TEST_DEV}p2 > /dev/null
fi

# mount fs
if [ $i == "ext4" -o $i == "ext4-nj" ];then
mount -t ext4 ${TEST_DEV}p2 ${TEST_PATH}
elif [ $i == "horaefs" ]; then
mount -t horaefsv2 ${TEST_DEV}p2 ${TEST_PATH}
elif [ $i == "mqfs" -o $i == "mqfs-atomic" ]; then
mount -t mqfs -o nr_streams=24 ${TEST_DEV}p2 ${TEST_PATH}
fi

# init result file 
s=12000
echo ${TEST_DEV} "begin" > fig11a/${i}.out

# start Figure 11(a)-(b)
for j in ${BLK_SIZE}
do
rm -rf ${TEST_PATH}/*
sync
echo 3 > /proc/sys/vm/drop_caches
sleep 1
fio --name=seq-write --directory=${TEST_PATH} --rw=write --file_append=1 --bs=${j}k --direct=0 --numjobs=1 --size=${s}M --time_based=1 --runtime=40 --ioengine=psync --fsync=1 --group_reporting=1 >> fig11a/${i}.out
done
echo ${TEST_DEV} "end" >> fig11a/${i}.out
sleep 5

# init result file 
echo ${TEST_DEV} "begin" > fig11c/${i}.out
# start Figure 11(c)-(d)
for k in ${THREADS}
do
rm -rf ${TEST_PATH}/*
sync
echo 3 > /proc/sys/vm/drop_caches
sleep 1
fio --name=seq-write --directory=${TEST_PATH} --rw=write --file_append=1 --bs=4k --direct=0 --numjobs=${k} --size=${s}M --time_based=1 --runtime=40 --ioengine=psync --fsync=1 --group_reporting=1 >> fig11c/${i}.out
done
echo ${TEST_DEV} "end" >> fig11c/${i}.out

# post clean
sync
sleep 5
umount ${TEST_PATH}
if [ $i == "horaefs" ]; then
rmmod horaefsv2
rmmod olayerv2
elif [ $i == "mqfs" -o $i == "mqfs-atomic" ]; then
rmmod mqfs
rmmod ccnvme
fi
done 