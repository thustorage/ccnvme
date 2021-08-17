#!/bin/bash
TEST_DEVS="/dev/nvme1n1 /dev/nvme2n1" # configure the device to intel 905P (447.1G) and intel P5800X (745.2G)

# check the device
for i in ${TEST_DEVS}
do
size=`lsblk | grep ${i:5} | awk 'NR==1{print $4}' `
if [ $size == "2E" ];then
echo -e "Error! wrong test device. can not use the PMR SSD for test. \e[40;35m please change it in the file run.sh to intel 905P or intel P5800X whose size is nearly 447.1G or 745.2G. \e[40;37m"
exit 0
fi
done

# run
export TEST_DEVS
./test_varmail.sh

# report 
./report.sh