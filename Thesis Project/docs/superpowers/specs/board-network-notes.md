# RK3588 board network notes

This note records the board SSH address used in different locations.

## Locations

- home: `ubuntu@192.168.2.156`
  - Verified on 2026-05-14.
  - SSH banner: `SSH-2.0-OpenSSH_8.2p1 Ubuntu-4ubuntu0.13`.
  - USB camera capture device observed in current project tests: `/dev/video48`.
- grandma-home: `ubuntu@192.168.10.186`
  - Used in previous board-side tests.
  - When working from `home`, `192.168.10.186:22` may appear reachable but did not return a valid SSH banner through Paramiko. Prefer `192.168.2.156` at `home`.

Default credentials used during development:

```text
username: ubuntu
password: ubuntu
```

## 2026-05-14 home-network smoke test

The experimental DMA/RGA/RKNN script was verified from `home` on `192.168.2.156`:

```text
camera opened: /dev/video48 640x480 YUYV buffers=4
rga_api version 1.10.1_[10]
DMA fd -> RGA -> RKNN input mem path enabled: rknn_inputs_set skipped
summary frames=30 detected_frames=4 total_detections=4 wall_fps=9.61374
avg_prepare_ms=1.15179 avg_run_ms=93.9698 avg_total_ms=100.005
```
