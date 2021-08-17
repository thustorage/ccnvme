#!/bin/bash
BASE_DIR=/home/lxj/sosp21-ae/
scp -r -P 8822 ../../ccnvme/ root@localhost:${BASE_DIR} > /dev/null
scp -r -P 8822 ../../mqfs/ root@localhost:${BASE_DIR} > /dev/null
ssh -p 8822 localhost "cd ${BASE_DIR}/ccnvme; make -j" > /dev/null
ssh -p 8822 localhost "cd ${BASE_DIR}/mqfs; make -j" > /dev/null

scp -P 8822 run_crash_test.sh root@localhost:/home/lxj/sosp21-ae/
ssh -t -p 8822 localhost "/home/lxj/sosp21-ae/run_crash_test.sh"
