#!/bin/bash

WRITE_NR_DATA_BLOCKS="1 2 4 8 16 32 128 512 2048 8192" # unit in 8 bytes, e.g., 1 means 8B
READ_NR_DATA_BLOCKS="1 2 4 8 16 32 128 512" # continuously read too large data blocks from the PMR will make the kernel stuck, as PMR read is a sync request that disables CPU scheduling 
DURATION=30
DEV=/dev/nvme1n1p1 # arbitrary usable device, does not take effect
let s=10+$DURATION

# prepare 
dmesg -C
mkdir res/
insmod ../../ccnvme/ccnvme.ko cp_device=${DEV} nr_streams=1

# async write 
for i in $WRITE_NR_DATA_BLOCKS
do
rmmod ccnvmetest
insmod ../../ccnvme-test/ccnvmetest.ko threads=1 duration=$DURATION nr_data_blocks=$i test_devs_path=${DEV} test_fn_idx=5 read=0 sync=0
sleep $s
done
rmmod ccnvmetest
# result
dmesg -c > res/async-write.out

# sync write 
for i in $WRITE_NR_DATA_BLOCKS
do
rmmod ccnvmetest
insmod ../../ccnvme-test/ccnvmetest.ko threads=1 duration=$DURATION nr_data_blocks=$i test_devs_path=${DEV} test_fn_idx=5 read=0 sync=1
sleep $s
done
rmmod ccnvmetest
# result
dmesg -c > res/sync-write.out

# raed 
for i in $READ_NR_DATA_BLOCKS
do
rmmod ccnvmetest
insmod ../../ccnvme-test/ccnvmetest.ko threads=1 duration=$DURATION nr_data_blocks=$i test_devs_path=${DEV} test_fn_idx=5 read=1
sleep $s
done
rmmod ccnvmetest
# result
dmesg -c > res/read.out
rmmod ccnvme
