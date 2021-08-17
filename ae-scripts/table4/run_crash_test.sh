#!/bin/bash
FLAG_DEV=/dev/nvme0n1
CRASHMONKEY_DIR=/home/lxj/sosp21-ae/crashmonkey/
TESTS="create_delete generic_035_1 generic_035_2 generic_106 generic_321_1"

cd ${CRASHMONKEY_DIR};
rm -rf build/mqfs-tests/*
mkdir build/mqfs-tests/

for i in $TESTS
do
cp -i build/tests/${i}.so build/mqfs-tests/
done

insmod ${CRASHMONKEY_DIR}/../ccnvme/ccnvme.ko cp_device=/dev/nvme0n1p1 nr_streams=24
insmod ${CRASHMONKEY_DIR}/../mqfs/mqfs.ko

python xfsMonkey.py -f ${FLAG_DEV} -d /dev/cow_ram0 -t mqfs -e 10240000 -u build/mqfs-tests/

umount /mnt/snapshot
rmmod mqfs
rmmod ccnvme.ko