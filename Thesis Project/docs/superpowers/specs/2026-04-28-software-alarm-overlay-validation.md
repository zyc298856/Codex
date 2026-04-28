# Software Alarm Overlay Validation

Date: 2026-04-28

## Purpose

The task book expects the detection system to provide an external alarm or closed-loop response. Since no relay, buzzer, or other external actuator is currently available, this project implements a software alarm output that can be demonstrated through the video stream itself.

This keeps the system visually demonstrable and thesis-defensible without adding unstable external wiring.

## Implemented Behavior

Both fixed-video validation and live RTSP detection now support a software alarm overlay:

- `rk_yolo_video`
- `rk_yolo_live_rtsp`

When at least one target is detected or displayed, the video frame shows:

```text
UAV ALERT | targets=<N> | max_score=<score>
```

When no target is present, the frame shows:

```text
NORMAL | no target
```

To avoid flicker when detections briefly disappear, the alarm state remains active for a configurable number of missed frames.

## Configuration

The overlay is enabled by default:

```bash
RK_YOLO_ALARM_OVERLAY=1
```

It can be disabled without changing detection or RTSP output:

```bash
RK_YOLO_ALARM_OVERLAY=0
```

The hold duration is controlled by:

```bash
RK_YOLO_ALARM_HOLD_FRAMES=5
```

## Fixed-Video Validation

Command:

```bash
RK_YOLO_ALARM_OVERLAY=1 RK_YOLO_ALARM_HOLD_FRAMES=5 \
  ./build/rk_yolo_video /home/ubuntu/public_videos/anti_uav_fig1.mp4 \
  artifacts/alarm_validation/fig1_alarm_overlay.mp4 \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn \
  0.25 0.45 \
  artifacts/alarm_validation/fig1_alarm_overlay.csv \
  artifacts/alarm_validation/fig1_alarm_overlay.roi.jsonl \
  artifacts/alarm_validation/fig1_alarm_events.csv
```

Generated files:

```text
fig1_alarm_overlay.mp4
fig1_alarm_overlay.csv
fig1_alarm_overlay.roi.jsonl
fig1_alarm_events.csv
fig1_alarm_overlay.log
```

Alarm CSV output:

```text
frame_index,event,active,detections,max_score
1,alarm_on,1,1,0.6147
26,alarm_off,0,0,0.0000
28,alarm_on,1,1,0.5815
75,alarm_off,0,0,0.0000
87,alarm_on,1,1,0.2634
96,alarm_off,0,0,0.0000
105,alarm_on,1,1,0.3674
113,alarm_off,0,0,0.0000
```

Representative frame:

```text
Thesis Project/eval_runs/alarm_validation/fig1_alarm_overlay_frame1.jpg
```

## Live RTSP Validation

Command:

```bash
RK_YOLO_INPUT_LOOP=1 RK_YOLO_ALARM_OVERLAY=1 RK_YOLO_ALARM_HOLD_FRAMES=5 RK_YOLO_BOX_SMOOTH=1 \
  timeout 18s ./build/rk_yolo_live_rtsp /home/ubuntu/public_videos/anti_uav_fig1.mp4 \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn \
  /alarmtest 640 480 15 0.25 0.45 8556 2
```

RTSP pull check:

```text
codec_name=h264
width=640
height=480
r_frame_rate=15/1
```

Runtime log evidence:

```text
alarm_overlay=on hold_frames=5
alarm_event frame=0 state=on detections=3
captured=31 inferred=29 published=29 ... detections=1 alarm=on ...
```

## Interpretation

The software alarm path satisfies the demonstration need for a closed-loop response without requiring external hardware. It also provides structured alarm evidence for fixed-video tests through `alarm_events.csv`.

For the thesis, this can be described as a software-defined alarm output and reserved external-control interface:

> After target detection, the system updates an alarm state machine and outputs the alarm state through visual overlay and event logs. This validates the detection-to-response closed loop in software. External devices such as relays, buzzers, or pan-tilt controllers can be connected to the same alarm state in future work.

## Current Recommendation

Use the software alarm overlay for demonstrations:

```bash
RK_YOLO_ALARM_OVERLAY=1 RK_YOLO_ALARM_HOLD_FRAMES=5
```

Keep physical GPIO or relay output as a future extension unless real hardware is required by the advisor.
