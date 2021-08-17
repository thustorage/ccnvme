# ccNVMe: Crash Consistent Non-Volatile Memory Express
ccNVMe is an extension of NVMe (Non-Volatile Memory Express) that provides atomicity and ordering guarantees at the device driver while reducing the traffic over the PCIe bus. This repository contains the source code of ccNVMe and instructions for reproducing the results in the ccNVMe paper, to appear in SOSP'21.

## Content of the repository 
* `ccnvme/`: ccNVMe kernel module
* `mqfs/`: the MQFS file system kernel module
* `nvme-host/`: NVMe over PCIe kernel module
* `ccnvme-test/`: tests for ccNVMe
* `patch/`: Linux kernel patch for MQFS, and CrashMonkey patch for testing MQFS atop ccNVMe
* `ae-scripts/`: instructions for reproducing the results of the paper (AE)

## Environment
* Operating system: Linux kernel 4.18.20
* Storage: at least one NVMe SSD that enables the PMR (persistent memory region) feature. Other alternatives of the PMR: use persistent memory or DRAM to store the submission queues; complies ccNVMe with `-DUSE_AEP` flag; WARNING: this emulation is not well tested.

## Getting started
This section introduces the instructions to compile and install of the kernel, ccNVMe and MQFS. Artifact evaluation committees can go to the `ae-scripts/` directory and login into the server, as we have provided the environment that contains all prerequisites.

### Install the kernel (with root user)
1. Download the vanilla Linux kernel 4.18.20.
    ```bash
    wget https://mirrors.edge.kernel.org/pub/linux/kernel/v4.x/linux-4.18.20.tar.gz
    tar xzvf linux-4.18.20.tar.gz
    ```
2. Apply the kernel patch.
    ```bash
    cd linux-4.18.20
    git clone https://github.com/thustorage/ccnvme.git
    git apply patch/mqfs.patch
    ```
3. Make and install the new kernel.
    ```bash
    cp /boot/config-`uname -r` .config
    make olddefconfig 
    make -j24
    make modules_install -j24
    make install 
    ```
4. Update the grub configuration in `/etc/default/grub` to enable default login of the new kernel as well as to disable `use_cmb_sqes`. For example:
    ```bash
    GRUB_DEFAULT="1>Ubuntu, with Linux 4.18.20"
    GRUB_CMDLINE_LINUX="nvme.use_cmb_sqes=0"
    ```
5. Reboot into the new kernel.
    ```bash
    update-grub && reboot 
    ```
### Build and install ccNVMe and MQFS 
1. Build and install NVMe.
    ```bash
    cd nvme-host
    make -j24
    ./reset_nvme.sh
    ```
2. Build and install ccNVMe.
    ```bash
    cd ccnvme
    make -j24
    insmod ccnvme.ko cp_device=/dev/nvme0n1p1 nr_streams=24
    ```
    > **NOTE:** `cp_device` specifies the file system journal device. Recommended to use a device partition whose size is larger than 1.01 GB (by default 1 GB journal size and 10 MB for PMR checkpoint). Theoretically, ccNVMe does not require an extra device. It is just an implementation issue here, as we originally tightly couple the journaling with ccNVMe. If applications or upper systems want to directly use ccNVMe and implement their own journaling mechanism, `cp_device` can be set to an arbitrary device (e.g., ramdisk /dev/ram0); no data will be stored on that `cp_device` in this case.
    `nr_streams` specifies the number of hardware queues. Recommended to set it to a number that equals to the number of physical cores.
    `journal_size` specifies the size of journal region of the MQFS. By default 1 GB.
3. Build, install and mount MQFS.
    ```bash
    cd mqfs
    make -j24
    insmod mqfs.ko
    mkfs.ext4 -O ^has_journal /dev/nvme0n1p2 
    mount -t mqfs /dev/nvme0n1p2 -o nr_streams=24 /dev/nvme0n1p2
    ```
    > **NOTE:** `/dev/nvme0n1p2` specifies the data device.
    `nr_streams` specifies the number of hardware queues, recommended to be equal to that of ccNVMe.

Now, an MQFS is mounted at /mnt/test, storing the journal to `/dev/nvme0n1p1` and the data to `/dev/nvme0n1p2`.