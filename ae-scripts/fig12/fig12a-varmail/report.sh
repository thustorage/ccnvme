#!/bin/bash
SYS="ext4 horaefs ext4-nj mqfs"

printf "%-14s %10s %10s\n" system dev-A dev-B > res/overall.out

for i in ${SYS}
do
printf "%-14s" $i >> res/overall.out
cat res/${i}.out | grep Summary | cut -d " " -f 6 | awk '{printf(" %10s", $1)}' >> res/overall.out
printf "\n" >> res/overall.out
done