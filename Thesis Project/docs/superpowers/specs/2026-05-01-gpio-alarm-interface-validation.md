# GPIO-compatible alarm interface validation

Date: 2026-05-01

## Purpose

The thesis task book asks for a closed-loop path where detecting a UAV can trigger an alarm or countermeasure interface. Because no relay, buzzer, or GPIO peripheral is currently connected, the project now provides a software GPIO-compatible output path that can be validated without extra hardware and later mapped to a real GPIO value file.

## Implementation

Two executables now support the same optional environment variable:

- `rk_yolo_video`
- `rk_yolo_live_rtsp`

Environment variable:

```bash
RK_YOLO_GPIO_VALUE_PATH=/tmp/rk_yolo_gpio_value
```

Behavior:

- If the variable is unset, the external alarm interface is disabled and the existing video overlay, CSV, ROI, RGA, INT8 and RTSP behavior is unchanged.
- On startup, the configured value path is initialized to `0`.
- On an `alarm_on` transition, the value path is written as `1`.
- On an `alarm_off` transition, the value path is written as `0`.
- The mechanism is compatible with mock files under `/tmp` and sysfs-style GPIO value files if a real GPIO peripheral is added later.

## Board validation

Board:

- Host: `ubuntu@192.168.2.156`
- Project root: `/home/ubuntu/eclipse-workspace/eclipse-workspace`
- Test video: `/home/ubuntu/public_videos/anti_uav_fig1.mp4`
- Model: `training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn`

Build result:

- `rk_yolo_video`: built successfully on RK3588.
- `rk_yolo_live_rtsp`: built successfully on RK3588.
- Only GStreamer deprecation warnings were printed; no project compile errors were introduced.

Offline validation command pattern:

```bash
RK_YOLO_GPIO_VALUE_PATH=/tmp/rk_yolo_gpio_value ./rk_yolo_video \
  /home/ubuntu/public_videos/anti_uav_fig1.mp4 \
  out_alert.mp4 \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn \
  0.35 0.45 detections_alert.csv roi_alert.jsonl alarm_alert.csv
```

Validation output:

```text
normal_final_gpio=0
normal_alarm_events=14
noalert_final_gpio=0
noalert_alarm_events=0
```

Interpretation:

- In the normal detection run, the video produced 30 detected frames and 14 alarm transitions. The GPIO-compatible value file changed with the alarm state and ended at `0` after alarm release.
- In the high-threshold no-detection run, no alarm events were generated and the initialized GPIO-compatible value stayed at `0`.

## Task-book relevance

This closes the missing software side of the "detect UAV -> trigger alarm interface" requirement without requiring additional physical hardware. For the thesis, it should be described as an external alarm/GPIO-compatible interface that has been validated with a mock value file. If a relay or buzzer is later added, the same state-change logic can be connected to the real GPIO control path.
