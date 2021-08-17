#!/bin/bash
tmux has-session -t crashmonkey-vm 2>/dev/null
if [ "$?" -eq 1 ];then
echo "no session found. creating VM"
tmux new-session -s crashmonkey-vm -d
# start the VM with a PMR-enabled NVMe SSD
tmux send-keys -t crashmonkey-vm "cd /home/lxj/VSSD/vm/; ./start-vm-nvme.sh" C-m 
else
echo "session found! exiting."
exit 1
fi