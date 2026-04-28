# RTSP 10-Minute Stability Validation

Date: 2026-04-28

## Purpose

This experiment validates whether the current RK3588 live RTSP detection pipeline can run continuously for about 10 minutes with the recommended real-time configuration, software alarm overlay, and public UAV video replay input.

The goal is to provide thesis-ready evidence for:

- real-time RTSP output stability;
- throughput and NPU scheduling behavior;
- end-to-end latency;
- CPU and memory usage;
- software alarm state output.

## Test Environment

Board:

```text
RK3588 Ubuntu board
SSH: ubuntu@192.168.10.186
```

Program:

```text
/home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_live_rtsp/build/rk_yolo_live_rtsp
```

Model:

```text
/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn
```

Input video:

```text
/home/ubuntu/public_videos/anti_uav_fig1.mp4
```

Valid result directory on board:

```text
/home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_live_rtsp/artifacts/stability_10min_20260428_195508_with_resource
```

## Runtime Configuration

The stability test used the current practical real-time configuration:

```bash
RK_YOLO_INPUT_LOOP=1
RK_YOLO_ALARM_OVERLAY=1
RK_YOLO_ALARM_HOLD_FRAMES=5
RK_YOLO_BOX_SMOOTH=1
RK_YOLO_DYNAMIC_ROI=1
RK_YOLO_TRACK_MODE=motion
RK_YOLO_PROFILE=0
```

Command:

```bash
timeout 620s ./build/rk_yolo_live_rtsp \
  /home/ubuntu/public_videos/anti_uav_fig1.mp4 \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn \
  /stability 640 480 15 0.25 0.45 8558 2
```

An `ffmpeg` RTSP client pulled the stream during the test:

```bash
timeout 610s ffmpeg -nostdin -hide_banner -loglevel error \
  -rtsp_transport tcp -i rtsp://127.0.0.1:8558/stability -f null -
```

The `timeout` return code was `124`, which is expected because both server and client were intentionally stopped by the timed wrapper.

## Output Artifacts

Generated on the board:

```text
summary.txt
computed_summary.txt
live.log
process_samples.csv
ffmpeg_pull.log
```

The `ffmpeg_pull.log` file is empty, indicating that the RTSP client did not report stream errors during the run.

## Summary Results

| Metric | Result |
|---|---:|
| Log metric rows | 294 |
| Process resource samples | 118 |
| Average stream FPS | 12.54 |
| Average NPU FPS | 6.71 |
| Average ROI FPS | 1.76 |
| Average end-to-end latency | 184.13 ms |
| P95 end-to-end latency | 259.15 ms |
| Max end-to-end latency | 309.09 ms |
| Captured frames at end | 8225 |
| Published frames at end | 7660 |
| NPU inference runs at end | 4100 |
| Reused/tracked frames at end | 3561 |
| ROI crop runs at end | 1073 |
| Dropped capture frames at end | 563 |
| Dropped publish frames at end | 0 |
| Alarm-on reports | 172 |
| Average process CPU | 107.73% |
| P95 process CPU | 112.00% |
| Max process CPU | 112.00% |
| Average RSS memory | 125.10 MB |
| Max RSS memory | 127.01 MB |

## Interpretation

The pipeline remained operational for the 10-minute test window. The RTSP stream was continuously pulled by a client, and no publish-queue drops were observed:

```text
dropped_publish=0
```

The capture-side dropped-frame count is expected in the current low-latency design. The capture queue intentionally discards old frames when inference and rendering cannot consume every input frame in real time. This prevents unlimited latency growth and keeps the displayed stream close to the latest available frame.

The measured average stream frame rate was about `12.54 FPS`, while NPU inference was executed at about `6.71 FPS`. This matches the `detect_every_n=2` strategy: not every displayed frame runs full NPU inference; intermediate frames reuse tracking or previous detection results. This supports the thesis argument that the final real-time configuration is a throughput-latency trade-off rather than simply maximizing NPU calls.

The average end-to-end latency was about `184 ms`, with a P95 value of about `259 ms`. For a visual demonstration and software-alarm loop, this is acceptable. The average process RSS stayed around `125 MB`, and no memory growth was observed within the test window.

## Thesis-Friendly Conclusion

The 10-minute RTSP stability test shows that the RK3588 live detection pipeline can continuously process a looping UAV video source, publish an H.264 RTSP stream, maintain software alarm output, and keep memory usage stable. Under the recommended `detect_every_n=2` configuration, the system achieves about `12.54 FPS` output stream rate and `184 ms` average end-to-end latency, while keeping publish-side frame drops at zero. The result supports the feasibility of using lightweight tracking and bounded queues to maintain stable real-time behavior on RK3588.

## Notes

One earlier 10-minute run produced valid RTSP metrics but missed CPU/RSS sampling because the script captured the timeout wrapper process instead of the real `rk_yolo_live_rtsp` process. The final run above fixed PID discovery by resolving the actual executable path under `/proc`.

The existing demonstration process on port `8554` was left untouched:

```text
./build/rk_yolo_live_rtsp ... /yolo ... 8554 ...
```
