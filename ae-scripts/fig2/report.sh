#!/bin/bash
SYS="ext4 horaefs ext4-nj"
TEST_DEVS=${TEST_DEVS}

# Figure 2a-c
echo -n "" > fig2a-c/overall.out
for j in ${TEST_DEVS}
do
printf "############################## %10s #######################\n" ${j} >> fig2a-c/overall.out
printf "%-14s %10s %10s %10s %10s %10s %10s %10s\n" system 1 4 8 12 16 20 24 >> fig2a-c/overall.out
for i in ${SYS}
do
printf "%-14s" $i >> fig2a-c/overall.out
cat fig2a-c/${i}-${j:5}.out | grep iops | cut -d "=" -f 4  | cut -d "," -f 1 | awk '{printf(" %10s", $1)}' >> fig2a-c/overall.out
printf "\n" >> fig2a-c/overall.out
done
done