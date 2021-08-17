#!/bin/bash
TEST_DEV=$1
FS_TYPE="ext4 horaefs ext4-nj"
THREADS="1 4 8 12 16 20 24"

TEST_PATH=/mnt/test
HORAEFS_DIR=/home/lxj/horae+/

# prepare
mkdir fig2a-c/
mkdir fig2d/

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
fi

# mount fs
if [ $i == "ext4" -o $i == "ext4-nj" ];then
mount -t ext4 ${TEST_DEV}p2 ${TEST_PATH}
elif [ $i == "horaefs" ]; then
mount -t horaefsv2 ${TEST_DEV}p2 ${TEST_PATH}
fi

# init result file 
s=12000

# init result file 
echo ${TEST_DEV} "begin" > fig2a-c/${i}-${TEST_DEV:5}.out
# start Figure 2(a)-(c)
for k in ${THREADS}
do
rm -rf ${TEST_PATH}/*
sync
echo 3 > /proc/sys/vm/drop_caches
sleep 1

# Figure 2(d)
rm -f fig2d/${i}-${TEST_DEV:5}-${k}
tmux new-session -s iostat -d
tmux send-keys -t iostat "iostat -x 1 >> fig2d/${i}-${TEST_DEV:5}-${k}" C-m

fio --name=seq-write --directory=${TEST_PATH} --rw=write --file_append=1 --bs=4k --direct=0 --numjobs=${k} --size=${s}M --time_based=1 --runtime=40 --ioengine=psync --fsync=1 --group_reporting=1 >> fig2a-c/${i}-${TEST_DEV:5}.out

tmux kill-session -t iostat
done
echo ${TEST_DEV} "end" >> fig2a-c/${i}-${TEST_DEV:5}.out

# post clean
sync
umount ${TEST_PATH}
if [ $i == "horaefs" ]; then
rmmod horaefsv2
rmmod olayerv2
fi
done 