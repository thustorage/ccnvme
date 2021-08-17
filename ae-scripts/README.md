## Artifact evaluation for ccNVMe
In the figX or tableX directories, we have scripts and instructions for reproducing figures in our papers.
***Before evaluation, please run `w` to check if anyone else is using the server, to avoid resource contention. Please close ssh connection after evaluation.***
After login into the server, go to the directory **`/home/lxj/sosp21-ae/`** for AE.

### Hardware
The server has three NVMe SSDs, each identified by its capacity (not name) as the OS will rename the device during each machine reboot. For example, type `lsblk`, output: 
```
/dev/nvme0n1  2E // PMR SSD, please do not directly use it
/dev/nvme1n1  447.1G // Intel Optane 905P SSD
/dev/nvme2n1  745.2G // Intel Optane P5800X SSD
```
Please do not directly read/write the PMR SSD (i.e., /dev/nvme0n1); the ccNVMe will automatically detect and use the PMR SSD (see `ccnvme/cmb.c`).
***Before running each test for each figure, please associate the right device with the `TEST_DEVS` or `TEST_DEV` variable in the `run.sh` of each subdirectory.***
For example, Figure 10 of the paper uses P5800X, ensure `TEST_DEV` is correctly set to `/dev/nvme2n1` before running.
Partition the drives into two portions, one for journaling and another for normal file system region. For example:
```
/dev/nvme1n1p1 10G  // for journal
/dev/nvme1n1p2 437.1G   // for file system
```

### Main Figures
- Figure 10 (~20 mins). Run the following commands.The results are in `overall.out`. Each line represents the throughput (in MB/s or Tx/s) or I/O utilization of each system. Other files store the raw results from `dmesg` or `iostat`.
    ```bash
    cd fig10
    ./run.sh
    cat fig10a/overall.out
    cat fig10b/overall.out
    cat fig10c/overall.out
    cat fig10d/overall.out
    ```

- Figure 12 (~24 mins). Run the following commands. The results are in `overall.out`. Each line represents the throughput (in ops/s) of each system.  Other files store the raw results from `Filebench` or `RocksDB db_bench`.
    ```bash
    cd fig12/fig12a-varmail/
    ./run.sh
    cat res/overall.out
    cd ../fig12b-rocksdb/
    ./run.sh
    cat res/overall.out
    ```
    > NOTE: if some results are missing in the `overall.out` and you encounter the warning "WARNING! Run stopped early: flowop createfile2-1 could not obtain a file. Please reduce runtime, increase fileset entries ($nfiles), or switch modes.", you can re-run the tests for specific file systems by modifying the variable `FS_TYPE` in file `test_varmail.sh` (e.g., FS_TYPE="horaefs" to repeat the tests only for horaefs) or changing the `run 60` in `varmail-tmp.f` (e.g., `run 30` to run 30 seconds).

- Figure 13 (~50 mins). Run the following commands. The results are in `overall.out`. Each line represents the throughput (in ops/s) of each setup. Other files store the raw results from `FIO`. The `ccnvme_${SETUP}.ko` or `mqfs_${SETUP}.ko` files in folder `mod` contain the already compiled kernel module. `${SETUP}` is the legend from Figure 13. You can compile a specific setup by changing the macros in line 24 of `ccnvme/horae.h` and line 61 of `mqfs/ext4.h` and re-compiling the modules, followed by renaming the modules to `mod` folder with the aforementioned name format. If you do so, please restore the modules (in `ccnvme` and `mqfs` folders) to enable all designs, to avoid impacting other users and tests.
    ```bash
    cd fig13/
    ./run.sh
    cat res/overall.out
    ```

- Figure 14 (~5 mins). Run the following commands. The results are in `overall.out`, which stores two tables from Figure 14. The `mqfs_perf.ko` in `mod` folder is the intact MQFS that enables performance profiling (see macro `PERF_DECOMPOSE` in `mqfs/`). The `ext4-debug.ko` is the intact ext4 that uses the same approach to record latencies (see macro `PERF_DECOMPOSE` in `/home/lxj/horae+/ext4-debug/`).
    ```bash
    cd fig14/
    ./run.sh
    cat res/overall.out
    ```

### Other Figures 
-  Figure 2 (~30 mins). Run the following commands. The results are in `overall.out`. Each line represents the throughput (in ops/s) or bandwidth utilization of each system. The results of Intel 750 (i.e., Figure 2(a)) will not be shown since this SSD is used for other projects in this time.
    ```bash
    cd fig2
    ./run.sh
    cat fig2a-c/overall.out
    cat fig2d/overall.out
    ```

- Figure 5 (~20 mins). Run the following commands. The results are in `overall.out`. Note that for read test, the results beyond 4 KB MMIO are not shown (in both the figure and output file). This is because PMR read is a sync (and slow) request that disables CPU scheduling and preemption; continuously reading large data blocks from the PMR spends a lot of time, making the kernel stuck.
    ```bash
    cd fig5
    ./run.sh
    cat res/overall.out
    ```

- Figure 11 (~45 mins). Run the following commands. The results are in `overall.out`. Other files store the raw results from `FIO`. The `mqfs-atomic.ko` is MQFS that implements `fdataatomic` (see macro `FATOMIC_FILE` of `mqfs/` and the function `ext4_atomic_file`).
    ```bash
    cd fig11
    ./run.sh
    cat fig11a/overall.out
    cat fig11b/overall.out
    cat fig11c/overall.out
    cat fig11d/overall.out
    ```

- Table 4 (~5 mins). As CrashMonkey does not support Linux kernel 4.18.20 and does not understand MQFS/ccNVMe, we add our support to this version of kernel (see `patch/crashmonkey.patch`, apply this patch to commit 6987735). For artifact evaluation committees, we had already provided a complete version of CrashMonkey in a virtual machine for testing. Type the following commands to see the changes:
    ```bash
    ssh -p 8822 localhost
    cd /home/lxj/sosp21-ae/crashmonkey
    git diff
    ```
    Run CrashMonkey test in the virtual machine, as suggested by the document (https://github.com/utsaslab/crashmonkey/blob/master/docs/CrashMonkey.md). We also run CrashMonkey in the bare-metal machine but sometimes encounter unknown bugs that crash the machine.
    Before start, ensure the VM is launched by typing `ssh -p 8822 localhost`. If the VM is not started yet, start a VM by running `./start_vm_bg.sh`. After that, run the following commands to test. The raw results from CrashMonkey are printed in the screen. Each line prints the result for each workload. When all 1000 crash points pass, it prints "passed".
    ```bash
    cd table4
    ./run.sh
    ```