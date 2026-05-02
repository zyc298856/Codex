# RTSP Detect Interval and Box Smoothing Ablation

Date: 2026-04-25

## Purpose

Compare the real-time viewing stability and performance of different detection interval and box smoothing configurations on RK3588.

The experiment was designed after observing that the drone box oscillated horizontally during RTSP playback. The likely cause was the combination of sparse NPU detection, lightweight inter-frame motion tracking, and disabled box smoothing.

## Fixed Conditions

- Board: RK3588
- Input: `/home/ubuntu/public_videos/video01.mp4`
- Model: `best.end2end_false.op12.rk3588.fp.v220.rknn`
- Model output layout: `[1, 5, 8400]`
- Class mode: single-class `drone`
- RTSP output: `rtsp://192.168.2.156:8554/drone`
- Resolution: `640x480`
- Target FPS: `15`
- Score threshold: `0.35`
- NMS threshold: `0.45`
- Track mode: `motion`
- Dynamic ROI: enabled
- Test duration: about 90 seconds per configuration
- RTSP client: `gst-launch-1.0` with TCP RTSP input and `fakesink`

Raw logs were saved under:

```text
Thesis Project/eval_runs/public_videos/rk3588_board/rtsp_ablation_20260425/
```

## Tested Configurations

| ID | detect_every_n | box_smooth | smooth_alpha | Expected Behavior |
|---|---:|---|---:|---|
| A | 3 | off | - | Highest reuse ratio, more visible jitter |
| B | 3 | on | 0.35 | Same NPU load as A, smoother display |
| C | 2 | on | 0.35 | Higher NPU frequency, best visual stability |

## Quantitative Result

The following values are averaged from runtime log samples after startup.

| ID | Avg stream FPS | Avg NPU FPS | Avg latency ms | Avg work ms | Last published frames | Last NPU runs |
|---|---:|---:|---:|---:|---:|---:|
| A: N=3, smooth off | 9.95 | 3.32 | 102.60 | 101.99 | 880 | 294 |
| B: N=3, smooth on | 9.93 | 3.31 | 108.05 | 107.03 | 883 | 295 |
| C: N=2, smooth on | 10.36 | 5.18 | 111.41 | 110.76 | 925 | 463 |

## Qualitative Observation

The user observed that configuration C reduced the left-right oscillation of the detection box.

This supports the hypothesis that the visible jitter was mainly caused by sparse keyframe detection plus inter-frame tracking drift. Enabling smoothing and increasing the detection frequency from every 3 frames to every 2 frames improved visual stability.

## Current Recommendation

Use configuration C as the current real-time viewing default for drone RTSP demonstration:

```text
detect_every_n=2
box_smooth=on
box_smooth_alpha=0.35
box_smooth_iou=0.05
track_mode=motion
dynamic_roi=on
score=0.35
nms=0.45
```

Runtime command:

```bash
RK_YOLO_INPUT_LOOP=1 RK_YOLO_BOX_SMOOTH=1 RK_YOLO_BOX_SMOOTH_ALPHA=0.35 RK_YOLO_BOX_SMOOTH_IOU=0.05 ./rk_yolo_live_rtsp \
  /home/ubuntu/public_videos/video01.mp4 \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn \
  /drone 640 480 15 0.35 0.45 8554 2
```

## Thesis-Friendly Conclusion

Compared with `detect_every_n=3`, reducing the interval to `detect_every_n=2` increases NPU invocation frequency from about `3.3 FPS` to about `5.2 FPS`, while keeping the RTSP output around `10 FPS`. The average latency increases only moderately, from about `102.6 ms` to `111.4 ms`.

For visual real-time anti-UAV monitoring, this is a favorable tradeoff because the detection box is more stable and the output frame rate remains acceptable.

## Remaining Work

- Repeat the comparison with the USB camera input.
- Add a longer stability test, preferably 5 to 10 minutes.
- Test whether disabling dynamic ROI further reduces jitter in hard scenes.
- Evaluate whether multi-context inference can improve `detect_every_n=1` without unacceptable latency.
