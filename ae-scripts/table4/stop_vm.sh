#!/bin/bash
tmux has-session -t crashmonkey-vm 2>/dev/null
if ["$?" -eq 1 ] ; then
ssh -t -p 8822 localhost "poweroff"
sleep 5
tmux kill-session -t crashmonkey-vm
fi