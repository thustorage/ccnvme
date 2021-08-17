#!/bin/bash
SYS="mqfs ext4-nj"
TEST_DEV="/dev/nvme1n1" # configure the device to Intel 905p SSD

# check the device
size=`lsblk | grep ${TEST_DEV:5} | awk 'NR==1{print $4}' `
if [ $size == "2E" ];then
echo -e "Error! wrong test device. can not use the PMR SSD for test. \e[40;35m please change it in the file run.sh to intel 905P whose size is nearly 447.1G. \e[40;37m"
exit 0
fi

# run
for i in ${SYS}
do
./test_latency_breakdown.sh ${i} ${TEST_DEV}
done

# report
./report.sh
