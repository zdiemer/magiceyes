#!/bin/bash
cd "$HOME/pbtest" || exit 1
timeout 7 /mnt/e/Code/magiceyes/bin/me_unicorn "$HOME/pbtest/Payback_tmp" >/tmp/v.out 2>/tmp/v.err
echo "stdout cant/fail: $(grep -ic 'can.t\|fail' /tmp/v.out)  MMSP2 flips: $(grep -c 'MMSP2 flip' /tmp/v.err)"
