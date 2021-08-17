#!/bin/bash

# report mqfs
printf "#################### mqfs ##################\n" > res/overall.out
printf "%6s %6s %6s %6s %6s %6s %6s %6s %8s %8s\n" S-iD S-iM S-pM S-JH W-iM W-iD W-pM W-JH fatomic fsync >> res/overall.out

# S-iD, file data
cat res/mqfs.out | grep "file data submit" | cut -d ":" -f 2 | awk '{printf("%6s", $1)}' >> res/overall.out

# S-iM,  file and FS metadata
cat res/mqfs.out | grep -E "mapping submit|file inode submit" | cut -d ":" -f 2 | awk '{sum += $1};END {print sum}' | awk '{printf(" %6s", $1)}' >> res/overall.out

# S-pM, parent directory
cat res/mqfs.out | grep "parent submit" | cut -d ":" -f 2 | awk '{printf(" %6s", $1)}' >> res/overall.out

# S-JH, journal description block
cat res/mqfs.out | grep "file journal descirpt submit" | cut -d ":" -f 2 | awk '{printf(" %6s", $1)}' >> res/overall.out

## NOTE: the individual value of W-xx may not exactly match the number of Figure 14, but their sum will be consistent with the paper. This is because the SSD may reorder the requests, e.g., process the later S-JH ahead of S-iM. As a result of reordering, JH may finish before iM.

# W-iM
cat res/mqfs.out | grep -E "mapping wait|file inode wait" | cut -d ":" -f 2 | awk '{sum += $1};END {print sum}' | awk '{printf(" %6s", $1)}' >> res/overall.out

# W-iD
cat res/mqfs.out | grep "file data wait" | cut -d ":" -f 2 | awk '{printf(" %6s", $1)}' >> res/overall.out

# W-pM
cat res/mqfs.out | grep "parent wait" | cut -d ":" -f 2 | awk '{printf(" %6s", $1)}' >> res/overall.out

# W-JH
cat res/mqfs.out | grep "file journal descirpt wait" | cut -d ":" -f 2 | awk '{printf(" %6s", $1)}' >> res/overall.out

# fatomic
cat res/overall.out | sed -n '3p' | awk '{ for(i=1;i<=4;i++) sum+=$i; print sum}' | awk '{printf(" %8s", $1)}' >> res/overall.out

# fsync
cat res/overall.out | sed -n '3p' | awk '{ for(i=1;i<=8;i++) sum+=$i; print sum}' | awk '{printf(" %8s\n", $1)}' >> res/overall.out

# report ext4-nj
printf "#################### Ext4-NJ ##################\n" >> res/overall.out
printf "%10s %10s %10s %10s\n" S-iD+W-iD S-iM+W-iM S-pM+W-pM fsync >> res/overall.out

# S-iD + W-iD
cat res/ext4-nj.out | grep "file data submit" | cut -d ":" -f 2 | awk '{printf("%10s", $1)}' >> res/overall.out

# S-iM + W-iM
cat res/ext4-nj.out | grep -E "mapping submit|file inode submit" | cut -d ":" -f 2 | awk '{sum += $1};END {print sum}' | awk '{printf(" %10s", $1)}' >> res/overall.out

# S-pM + W-pM
cat res/ext4-nj.out | grep "parent submit" | cut -d ":" -f 2 | awk '{printf(" %10s", $1)}' >> res/overall.out

# fsync
cat res/overall.out | sed -n '6p' | awk '{ for(i=1;i<=NF;i++) sum+=$i; print sum}' | awk '{printf(" %10s\n", $1)}' >> res/overall.out