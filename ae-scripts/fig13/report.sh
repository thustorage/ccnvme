#!/bin/bash
SYS="base +ccnvme +mqjournal +metapaging"

printf "### fig 13 (a) ####\n" > res/overall.out
printf "%-14s %10s %10s %10s %10s %10s\n" system 1 4 8 12 24 >> res/overall.out

for i in ${SYS}
do
printf "%-14s" $i >> res/overall.out
cat res/${i}.out | grep iops | awk 'NR<6' | cut -d "," -f 3 | cut -d "=" -f 2 | awk '{printf(" %10s", $1)}' >> res/overall.out
printf "\n" >> res/overall.out
done

printf "### fig 13 (b) ####\n" >> res/overall.out
printf "%-14s %10s %10s %10s %10s %10s\n" system 1 4 8 12 24 >> res/overall.out

for i in ${SYS}
do
printf "%-14s" $i >> res/overall.out
cat res/${i}.out | grep iops | awk 'NR>=6 && NR<=10' | cut -d "," -f 3 | cut -d "=" -f 2 | awk '{printf(" %10s", $1)}' >> res/overall.out
printf "\n" >> res/overall.out
done