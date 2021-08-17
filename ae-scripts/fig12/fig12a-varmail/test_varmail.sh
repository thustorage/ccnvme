#!/bin/bash
TEST_DEVS=${TEST_DEVS} # intel 905P, 447.1G; intel P5800X, 745.2G
FS_TYPE="ext4 horaefs ext4-nj mqfs"

THREADS="16"
TEST_PATH=/mnt/test
OUT_DIR=res/
HORAEFS_DIR=/home/lxj/horae+/

# prepare
echo 0 | sudo tee /proc/sys/kernel/randomize_va_space > /dev/null
mkdir ${OUT_DIR} > /dev/null

# test
for i in $FS_TYPE
do
echo -n "" > ${OUT_DIR}/$i.out

for j in $TEST_DEVS
do
# insmod and mkfs
if [ $i == "ext4" ];then
mkfs.ext4 -E lazy_itable_init=0,lazy_journal_init=0 ${j}p2 > /dev/null
elif [ $i == "horaefs" ]; then
insmod ${HORAEFS_DIR}/olayerv2/olayerv2.ko cp_device=${j}p1 nr_streams=24
insmod ${HORAEFS_DIR}/horaefsv2/horaefsv2.ko
mkfs.ext4 -O ^has_journal -E lazy_itable_init=0,lazy_journal_init=0 ${j}p2 > /dev/null
elif [ $i == "ext4-nj" ]; then
mkfs.ext4 -O ^has_journal -E lazy_itable_init=0,lazy_journal_init=0 ${j}p2 > /dev/null
elif [ $i == "mqfs" ]; then
insmod ../../../ccnvme/ccnvme.ko cp_device=${j}p1 nr_streams=24
insmod ../../../mqfs/mqfs.ko 
mkfs.ext4 -O ^has_journal -E lazy_itable_init=0,lazy_journal_init=0 ${j}p2 > /dev/null
fi

# mount fs
if [ $i == "ext4" -o $i == "ext4-nj" ];then
mount -t ext4 ${j}p2 ${TEST_PATH}
elif [ $i == "horaefs" ]; then
mount -t horaefsv2 ${j}p2 ${TEST_PATH}
elif [ $i == "mqfs" ]; then
mount -t mqfs -o nr_streams=24 ${j}p2 ${TEST_PATH}
fi

echo 3 > /proc/sys/vm/drop_caches
sed -e "s,@NR_THREADS@,$THREADS,g" < varmail-tmp.f > /tmp/varmail-test

# start filebench varmail
filebench -f /tmp/varmail-test >> ${OUT_DIR}/$i.out

# post clean
sync
umount ${TEST_PATH}
if [ $i == "horaefs" ]; then
rmmod horaefsv2
rmmod olayerv2
elif [ $i == "mqfs" ]; then
rmmod mqfs
rmmod ccnvme
fi
done

done
