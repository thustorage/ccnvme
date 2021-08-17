#!/bin/bash
TEST_DEVS=${TEST_DEVS}
FS_TYPE="ext4 horaefs ext4-nj mqfs"

THREADS="24"
DE_BENCH_DIR=/home/lxj/horae+/rocksdb-master/
DB_DIR=/mnt/test
OUT_DIR=res/
HORAEFS_DIR=/home/lxj/horae+/

# prepare 
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
mkfs.ext4 -O ^has_journal -E lazy_itable_init=0,lazy_journal_init=0 ${j}p2
fi

# mount fs
if [ $i == "ext4" -o $i == "ext4-nj" ];then
mount -t ext4 ${j}p2 ${DB_DIR}
elif [ $i == "horaefs" ]; then
mount -t horaefsv2 ${j}p2 ${DB_DIR}
elif [ $i == "mqfs" ]; then
mount -t mqfs -o nr_streams=24 ${j}p2 ${DB_DIR}
fi

echo 3 > /proc/sys/vm/drop_caches

# start db bench
$DE_BENCH_DIR/db_bench --benchmarks "fillsync" \
--db $DB_DIR --duration 120 \
--num 10000000 \
--compression_type "none" --compression_level 0 \
--threads ${THREADS}  --value_size 1024 >> ${OUT_DIR}/$i.out

# post clean
sync
umount ${DB_DIR}
if [ $i == "horaefs" ]; then
rmmod horaefsv2
rmmod olayerv2
elif [ $i == "mqfs" ]; then
rmmod mqfs
rmmod ccnvme
fi

done
done
