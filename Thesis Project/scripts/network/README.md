# RK3588 wired demo connection scripts

These scripts prepare a direct Ethernet connection between the Windows laptop and
the RK3588 board for thesis defense demos where Wi-Fi may be unavailable.

Recommended fixed wired addresses:

- Laptop Ethernet: `192.168.50.1/24`
- RK3588 `eth1`: `192.168.50.2/24`
- RK3588 fallback link-local: `169.254.163.230/16`

## One-time board setup

Run this while the board is still reachable by Wi-Fi SSH:

```powershell
.\install-board-wired-ip.ps1 -BoardIp 192.168.2.156
```

The script installs a small systemd service on the board. After that, every boot
will add the wired demo IPs to `eth1`.

## Daily demo connection

Connect the Ethernet cable, wait a few seconds, then double-click or run:

```cmd
rk3588-wired-connect.cmd
```

or:

```powershell
.\rk3588-wired-connect.ps1
```

If Windows has not been given the fixed Ethernet address yet, run once as
administrator:

```powershell
.\rk3588-wired-connect.ps1 -ConfigurePcIp
```

Useful addresses after connection:

- SSH: `ssh ubuntu@192.168.50.2`
- RTSP: `rtsp://192.168.50.2:8554/drone`
- Fallback SSH: `ssh ubuntu@169.254.163.230`
- Fallback RTSP: `rtsp://169.254.163.230:8554/drone`

Verified on 2026-05-17:

- Wired adapter: Realtek USB GbE, 1 Gbps
- Board `eth1`: `192.168.50.2/24` and `169.254.163.230/16`
- SSH over wired link: passed
- RTSP port `8554` over wired link: passed
