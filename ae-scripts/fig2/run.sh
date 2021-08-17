#!/bin/bash
TEST_DEVS="/dev/nvme1n1 /dev/nvme2n1"  # configure the device to intel 905P (447.1G) and intel P5800X (745.2G)
SYS="ext4 horaefs ext4-nj"
DEV_MAX_BW=(23 33)

# check the device
for i in ${TEST_DEVS}
do
size=`lsblk | grep ${i:5} | awk 'NR==1{print $4}' `
if [ $size == "2E" ];then
echo -e "Error! wrong test device. can not use the PMR SSD for test. \e[40;35m please change it in the file run.sh to intel 905P whose size is nearly 447.1G. \e[40;37m"
exit 0
fi
done 

# run
for i in ${TEST_DEVS}
do
./test_motivation.sh ${i}
done

# report Figure 2a-c
export TEST_DEVS
./report.sh

# Figure 2d
m=0
for j in ${TEST_DEVS}
do
for k in ${SYS}
do
echo -n "" > fig2d/${k}-${j:5}.out
for i in 24
do
cat fig2d/${k}-${j:5}-${i} | grep ${j:5} | awk 'NR>2{print line}{line=$7}' | awk '$1!=0'| awk '{sum+=$1} END {print sum/NR/1000/"'${DEV_MAX_BW[m]}'"}' >> fig2d/${k}-${j:5}.out
done
done
m=$[m+1]
done
# print header
printf "%-14s" system > fig2d/overall.out
for i in ${TEST_DEVS}
do
printf " %10s" ${i:5} >> fig2d/overall.out
done 
printf "\n" >> fig2d/overall.out
# print content
for i in ${SYS}
do
printf "%-14s" ${i} >> fig2d/overall.out
for j in ${TEST_DEVS}
do
if [ $i == "horaefs" ];then
cat fig2d/$i-${j:5}.out | awk '{printf(" %10.2f", $1/2)}' >> fig2d/overall.out
else
cat fig2d/$i-${j:5}.out | awk '{printf(" %10.2f", $1)}' >> fig2d/overall.out
fi
done
printf "\n" >> fig2d/overall.out
done 