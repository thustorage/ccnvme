#!/bin/bash
TEST_DEV="/dev/nvme2n1"  # configure the device to intel P5800X, 745.2G

# check the device
size=`lsblk | grep ${TEST_DEV:5} | awk 'NR==1{print $4}' `
if [ $size == "2E" ];then
echo -e "Error! wrong test device. can not use the PMR SSD for test. \e[40;35m please change it in the file run.sh to intel P5800X whose size is nearly 745.2G. \e[40;37m"
exit 0
fi

# run each system
./test_classic_tx.sh ${TEST_DEV:5}
./test_horae_tx.sh ${TEST_DEV:5}
./test_ccnvme_tx.sh ${TEST_DEV:5}
./test_ccnvme_atomic_tx.sh ${TEST_DEV:5}

# report results
# final results are listed in the fig10{x}/overall.out. 
# raw results for individual systems are in the same folder
./report.sh