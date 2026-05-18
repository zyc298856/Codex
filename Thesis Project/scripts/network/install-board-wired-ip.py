#!/usr/bin/env python3
"""Install a persistent wired demo IP service on the RK3588 board."""

from __future__ import annotations

import argparse
import getpass
import posixpath
import shlex
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True, help="Board IP reachable over Wi-Fi")
    parser.add_argument("--user", default="ubuntu")
    parser.add_argument("--password", default=None)
    parser.add_argument("--ifname", default="eth1")
    parser.add_argument("--fixed-ip", default="192.168.50.2/24")
    parser.add_argument("--linklocal-ip", default="169.254.163.230/16")
    args = parser.parse_args()

    try:
        import paramiko
    except ImportError as exc:
        print("ERROR: paramiko is required. Install it or use the project .vendor path.", file=sys.stderr)
        raise SystemExit(2) from exc

    password = args.password or getpass.getpass(f"SSH password for {args.user}@{args.host}: ")

    helper_script = f"""#!/usr/bin/env bash
set -euo pipefail

IFNAME="${{RK_WIRED_IF:-{args.ifname}}}"
FIXED_IP="${{RK_WIRED_FIXED_IP:-{args.fixed_ip}}}"
LINKLOCAL_IP="${{RK_WIRED_LINKLOCAL_IP:-{args.linklocal_ip}}}"

if ! ip link show "$IFNAME" >/dev/null 2>&1; then
  exit 0
fi

ip link set "$IFNAME" up || true

if ! ip -4 addr show dev "$IFNAME" | grep -q "${{FIXED_IP%/*}}/"; then
  ip addr add "$FIXED_IP" dev "$IFNAME" 2>/dev/null || true
fi

if ! ip -4 addr show dev "$IFNAME" | grep -q "${{LINKLOCAL_IP%/*}}/"; then
  ip addr add "$LINKLOCAL_IP" dev "$IFNAME" 2>/dev/null || true
fi

ip -br addr show dev "$IFNAME" || true
"""

    service_unit = """[Unit]
Description=RK3588 thesis demo fixed wired IP
After=network-pre.target NetworkManager.service
Wants=network-pre.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/local/sbin/rk3588-wired-demo-ip.sh

[Install]
WantedBy=multi-user.target
"""

    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(args.host, username=args.user, password=password, timeout=10)

    tmp_helper = posixpath.join("/tmp", "rk3588-wired-demo-ip.sh")
    tmp_service = posixpath.join("/tmp", "rk3588-wired-demo-ip.service")
    with client.open_sftp() as sftp:
        with sftp.file(tmp_helper, "w") as f:
            f.write(helper_script)
        with sftp.file(tmp_service, "w") as f:
            f.write(service_unit)

    remote = " ".join(
        [
            "sudo",
            "-S",
            "sh",
            "-c",
            shlex.quote(
                "set -e; "
                f"install -m 755 {shlex.quote(tmp_helper)} /usr/local/sbin/rk3588-wired-demo-ip.sh; "
                f"install -m 644 {shlex.quote(tmp_service)} /etc/systemd/system/rk3588-wired-demo-ip.service; "
                "systemctl daemon-reload; "
                "systemctl enable rk3588-wired-demo-ip.service >/dev/null; "
                "systemctl restart rk3588-wired-demo-ip.service; "
                "systemctl --no-pager --full status rk3588-wired-demo-ip.service || true; "
                f"ip -br addr show dev {shlex.quote(args.ifname)} || true"
            ),
        ]
    )

    stdin, stdout, stderr = client.exec_command(remote, timeout=40)
    stdin.write(password + "\n")
    stdin.flush()
    out = stdout.read().decode("utf-8", "replace")
    err = stderr.read().decode("utf-8", "replace")
    client.close()
    if out:
        print(out)
    if err:
        print(err, file=sys.stderr)
    if "eth" not in out and "eth" not in err:
        print("WARNING: install command finished, but no Ethernet status was printed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
