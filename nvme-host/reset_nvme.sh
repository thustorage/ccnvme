rmmod nvme
rmmod nvme-core
insmod nvme-core.ko
insmod nvme.ko use_cmb_sqes=0
