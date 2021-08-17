#!/bin/bash
TEST_DEV="/dev/nvme1n1"  # configure the device to intel 905P, 447.1G

# check the device
size=`lsblk | grep ${TEST_DEV:5} | awk 'NR==1{print $4}' `
if [ $size == "2E" ];then
echo -e "Error! wrong test device. can not use the PMR SSD for test. \e[40;35m please change it in the file run.sh to intel 905P whose size is nearly 447.1G. \e[40;37m"
exit 0
fi

# run
./test_fs_perf.sh ${TEST_DEV}

# report 
./report.sh