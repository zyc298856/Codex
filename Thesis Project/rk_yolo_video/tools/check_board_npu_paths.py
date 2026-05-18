#!/usr/bin/env python3
"""Print available NPU load nodes on the RK3588 board."""

from __future__ import annotations

import paramiko

HOST = "192.168.2.156"
USER = "ubuntu"
PASS = "ubuntu"


COMMAND = r"""
set +e
echo "whoami=$(whoami)"
echo "--- debug rknpu load"
cat /sys/kernel/debug/rknpu/load 2>&1
echo "--- sudo debug rknpu load"
echo ubuntu | sudo -S cat /sys/kernel/debug/rknpu/load 2>&1
echo "--- devfreq npu"
ls -l /sys/class/devfreq/fdab0000.npu 2>&1
echo "--- devfreq load"
cat /sys/class/devfreq/fdab0000.npu/load 2>&1
echo "--- npu related paths"
find /sys -iname '*rknpu*' -o -iname '*npu*' 2>/dev/null | head -80
"""


client = paramiko.SSHClient()
client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
client.connect(HOST, username=USER, password=PASS, look_for_keys=False, allow_agent=False, timeout=12)
stdin, stdout, stderr = client.exec_command(COMMAND)
del stdin
print(stdout.read().decode("utf-8", "replace"))
print(stderr.read().decode("utf-8", "replace"))
client.close()
