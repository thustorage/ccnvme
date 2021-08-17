THREADS="1 2 4 8 12"
NR_DATA_BLOCKS="1 2 4 8 16"
DURATION=30
OUT_DIR=io-util/
CCNVME_TEST=../../ccnvme-test/
HORAE_TEST=/home/lxj/horae+/olayer-test/

SYS=$1
FN_PTR=$2

TEST_DEV=$3 # test device should be Intel P5800X, size=745.2G
DEV_MAX_BW=33 # in 100MB. maxmium write bandwidth of the tested device, see Table 3.

let s=10+$DURATION

# prepare
mkdir fig10a
mkdir fig10b
mkdir fig10c
mkdir fig10d
if [ ${SYS} == "horae" ];then
insmod ${HORAE_TEST}/../olayerv2/olayerv2.ko cp_device=/dev/${TEST_DEV}p1 nr_streams=24
else 
insmod ../../ccnvme/ccnvme.ko cp_device=/dev/${TEST_DEV}p1 nr_streams=24
fi

# start Figure 10(a)-(b)
for i in 1
do
for j in $NR_DATA_BLOCKS
do

if [ ${SYS} == "horae" ];then
rmmod olayertest
else
rmmod ccnvmetest
fi
rm -f fig10b/${SYS}-${i}-${j}

tmux new-session -s iostat -d
tmux send-keys -t iostat "iostat -x 1 >> fig10b/${SYS}-${i}-${j}" C-m

if [ ${SYS} == "horae" ];then
insmod ${HORAE_TEST}/olayertest.ko threads=$i duration=$DURATION nr_data_blocks=$j test_devs_path=/dev/${TEST_DEV}p2
else
insmod ${CCNVME_TEST}/ccnvmetest.ko threads=$i duration=$DURATION nr_data_blocks=$j test_devs_path=/dev/${TEST_DEV}p2 test_fn_idx=${FN_PTR}
fi

sleep $s
tmux kill-session -t iostat
done
done

if [ ${SYS} == "horae" ];then
rmmod olayertest
else
rmmod ccnvmetest
fi

# save results
# fig 10(a)
dmesg -c | grep throughput > fig10a/${SYS}.out # new file 
# fig 10(b)
echo -n "" > fig10b/${SYS}.out # new file
for j in $NR_DATA_BLOCKS
do
cat fig10b/${SYS}-${i}-${j} | grep ${TEST_DEV} | awk 'NR>2{print line}{line=$7}' | awk '$1!=0'| awk '{sum+=$1} END {print sum/NR/1000/"'${DEV_MAX_BW}'"}' >> fig10b/${SYS}.out
done

#start Figure 10(c)-(d)
for i in $THREADS
do
for j in 1
do

if [ ${SYS} == "horae" ];then
rmmod olayertest
else
rmmod ccnvmetest
fi

rm -f fig10d/${SYS}-${i}-${j}
tmux new-session -s iostat -d
tmux send-keys -t iostat "iostat -x 1 >> fig10d/${SYS}-${i}-${j}" C-m

if [ ${SYS} == "horae" ];then
insmod ${HORAE_TEST}/olayertest.ko threads=$i duration=$DURATION nr_data_blocks=$j test_devs_path=/dev/${TEST_DEV}p2
else
insmod ${CCNVME_TEST}/ccnvmetest.ko threads=$i duration=$DURATION nr_data_blocks=$j test_devs_path=/dev/${TEST_DEV}p2 test_fn_idx=${FN_PTR}
fi

sleep $s
tmux kill-session -t iostat
done
done

if [ ${SYS} == "horae" ];then
rmmod olayertest
else
rmmod ccnvmetest
fi

# save results
# fig 10(c)
dmesg -c | grep throughput > fig10c/${SYS}.out
# fig 10(d)
echo -n "" > fig10d/${SYS}.out # new file
for i in $THREADS
do
cat fig10d/${SYS}-${i}-${j} | grep ${TEST_DEV} | awk 'NR>2{print line}{line=$7}' | awk '$1!=0'| awk '{sum+=$1} END {print sum/NR/1000/"'${DEV_MAX_BW}'"}' >> fig10d/${SYS}.out
done

if [ ${SYS} == "horae" ];then
rmmod olayerv2
else
rmmod ccnvme
fi