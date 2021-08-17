#!/bin/bash
SYS="classic horae ccnvme ccnvme-atomic"

printf "%-14s %6s %6s %6s %6s %6s\n" system 4K 8K 16K 32K 64K > fig10a/overall.out
printf "%-14s %6s %6s %6s %6s %6s\n" system 4K 8K 16K 32K 64K > fig10b/overall.out
printf "%-14s %6s %6s %6s %6s %6s\n" system 1t 2t 4t 8t 12t > fig10c/overall.out
printf "%-14s %6s %6s %6s %6s %6s\n" system 1t 2t 4t 8t 12t > fig10d/overall.out

for i in ${SYS}
do
# fig 10a
printf "%-14s" $i >> fig10a/overall.out 
cat fig10a/$i.out | grep throughput | cut -d "," -f 2 | cut -d " " -f 3 | awk '{printf(" %6s", $1)}' >> fig10a/overall.out
printf "\n" >> fig10a/overall.out

# fig 10b
printf "%-14s" $i >> fig10b/overall.out 
cat fig10b/$i.out | awk '{printf(" %6.2f", $1)}' >> fig10b/overall.out
printf "\n" >> fig10b/overall.out

# fig 10c
printf "%-14s" $i >> fig10c/overall.out 
cat fig10c/$i.out | grep ops | cut -d "," -f 1 | cut -d " " -f 3 | awk '{printf(" %6s", $1)}' >> fig10c/overall.out
printf "\n" >> fig10c/overall.out

# fig 10d
printf "%-14s" $i >> fig10d/overall.out 
cat fig10d/$i.out | awk '{printf(" %6.2f", $1)}' >> fig10d/overall.out
printf "\n" >> fig10d/overall.out
done