#!/bin/bash
SYS="ext4 horaefs ext4-nj mqfs mqfs-atomic"

# Figure 11a 
printf "%-14s %10s %10s %10s %10s %10s %10s\n" system 4 8 16 32 64 128 > fig11a/overall.out

for i in ${SYS}
do
printf "%-14s" $i >> fig11a/overall.out
cat fig11a/${i}.out | grep BW | cut -d "=" -f 3  | cut -d " " -f 1 | awk '{printf(" %10s", $1)}' >> fig11a/overall.out
printf "\n" >> fig11a/overall.out
done

# Figure 11b
## average latency
printf "####################   average latency  ############################\n" > fig11b/overall.out
printf "%-14s %16s %16s %16s %16s\n" system 4 16 64 128 >> fig11b/overall.out
for i in ${SYS}
do
printf "%-14s" $i >> fig11b/overall.out
for j in 1 3 5 6
do
# value 
cat fig11a/${i}.out | grep ".*sync.*avg=.*"  | sed -n ${j}p | cut -d "=" -f 4 | cut -d "," -f 1 | awk '{printf(" %12s", $1)}' >> fig11b/overall.out
# units
cat fig11a/${i}.out | grep ".*sync.*avg=.*"  | sed -n ${j}p | cut -d "(" -f 2| cut -d ")" -f 1 | awk '{printf $1}' >> fig11b/overall.out
done
printf "\n" >> fig11b/overall.out
done

# Figure 11b
## standard deviation of the latency
printf "############   standard deviation of the latency  #################\n" >> fig11b/overall.out
printf "%-14s %16s %16s %16s %16s\n" system 4 16 64 128 >> fig11b/overall.out
for i in ${SYS}
do
printf "%-14s" $i >> fig11b/overall.out
for j in 1 3 5 6 # 4 16 64 128 KB respectively
do
# value 
cat fig11a/${i}.out | grep ".*sync.*avg=.*"  | sed -n ${j}p | cut -d "=" -f 5 | cut -d " " -f 2 | awk '{printf(" %12s", $1)}' >> fig11b/overall.out
# units
cat fig11a/${i}.out | grep ".*sync.*avg=.*"  | sed -n ${j}p | cut -d "(" -f 2| cut -d ")" -f 1 | awk '{printf $1}' >> fig11b/overall.out
done
printf "\n" >> fig11b/overall.out
done

# Figure 11c
printf "%-14s %10s %10s %10s %10s %10s %10s %10s\n" system 1 4 8 12 16 20 24 > fig11c/overall.out
for i in ${SYS}
do
printf "%-14s" $i >> fig11c/overall.out
cat fig11c/${i}.out | grep BW | cut -d "=" -f 3  | cut -d " " -f 1 | awk '{printf(" %10s", $1)}' >> fig11c/overall.out
printf "\n" >> fig11c/overall.out
done

# Figure 11d
## average latency
printf "####################   average latency  ############################\n" > fig11d/overall.out
printf "%-14s %16s %16s %16s %16s\n" system 1 8 16 24 >> fig11d/overall.out
for i in ${SYS}
do
printf "%-14s" $i >> fig11d/overall.out
for j in 1 3 5 7 # 1 8 16 24 threads respectively
do
# value 
cat fig11c/${i}.out | grep ".*sync.*avg=.*"  | sed -n ${j}p | cut -d "=" -f 4 | cut -d "," -f 1 | awk '{printf(" %12s", $1)}' >> fig11d/overall.out
# units
cat fig11c/${i}.out | grep ".*sync.*avg=.*"  | sed -n ${j}p | cut -d "(" -f 2| cut -d ")" -f 1 | awk '{printf $1}' >> fig11d/overall.out
done
printf "\n" >> fig11d/overall.out
done

# Figure 11d
## standard deviation of the latency
printf "############   standard deviation of the latency  #################\n" >> fig11d/overall.out
printf "%-14s %16s %16s %16s %16s\n" system 1 8 16 24 >> fig11d/overall.out
for i in ${SYS}
do
printf "%-14s" $i >> fig11d/overall.out
for j in 1 3 5 7
do
# value 
cat fig11c/${i}.out | grep ".*sync.*avg=.*"  | sed -n ${j}p | cut -d "=" -f 5 | cut -d " " -f 2 | awk '{printf(" %12s", $1)}' >> fig11d/overall.out
# units
cat fig11c/${i}.out | grep ".*sync.*avg=.*"  | sed -n ${j}p | cut -d "(" -f 2| cut -d ")" -f 1 | awk '{printf $1}' >> fig11d/overall.out
done
printf "\n" >> fig11d/overall.out
done