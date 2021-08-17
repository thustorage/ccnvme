#!/bin/bash
SYS="async-write sync-write read"

# latency
printf "########################## Latency (ns) #################################\n" > res/overall.out
printf "%-14s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s\n" read-write 8 16 32 64 128 256 1K 4K 16K 64K >> res/overall.out
for i in ${SYS}
do
printf "%-14s" ${i} >> res/overall.out 
cat res/${i}.out | grep latency | cut -d "," -f 3 | cut -d " " -f 3 | awk '{printf(" %6s", $1)}' >> res/overall.out
printf "\n" >> res/overall.out
done

# bandwidth
printf "########################## Bandwidth (MB/s) #################################\n" >> res/overall.out
printf "%-14s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s\n" read-write 8 16 32 64 128 256 1K 4K 16K 64K >> res/overall.out
for i in ${SYS}
do
printf "%-14s" ${i} >> res/overall.out 
cat res/${i}.out | grep throughput | cut -d "," -f 2 | cut -d " " -f 3 | awk '{printf(" %6.2f", $1/1000000)}' >> res/overall.out
printf "\n" >> res/overall.out
done