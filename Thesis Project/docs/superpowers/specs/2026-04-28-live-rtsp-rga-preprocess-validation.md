# 2026-04-28 Live RTSP RGA Preprocess Validation

## Purpose

This validation checks whether the optional RGA preprocessing path, previously verified in `rk_yolo_video`, can be enabled in the real-time RTSP program `rk_yolo_live_rtsp` without changing the stable default OpenCV path.

The change is intentionally conservative:

- `RK_YOLO_PREPROCESS=opencv` remains the default.
- `RK_YOLO_PREPROCESS=rga` and `RK_YOLO_PREPROCESS=rga_cvt_resize` are enabled only when `librga` is found at build time.
- The detection core remains shared through `rk_yolo_video/src/yolo_rknn.cpp`.

## Build Result

Board: RK3588 Linux board  
Project path: `/home/ubuntu/eclipse-workspace/eclipse-workspace`  
Program: `rk_yolo_live_rtsp`

Build command:

```bash
cd /home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_live_rtsp
mkdir -p build
cd build
cmake ..
cmake --build . -j2
```

CMake result:

```text
-- librga found: optional RK_YOLO_PREPROCESS=rga/rga_cvt_resize paths enabled
```

The program built successfully. Only GStreamer deprecation warnings from system headers were observed.

## Test Input

Video:

```text
/home/ubuntu/public_videos/anti_uav_fig1.mp4
```

Model:

```text
/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn
```

Output resolution and stream configuration:

```text
640 x 480, 15 fps, score=0.20, nms=0.45
```

## Test 1: Full-Frame Stress Mode

Purpose: maximize preprocessing frequency by running inference on every frame.

Common settings:

```bash
RK_YOLO_INPUT_LOOP=1
RK_YOLO_DYNAMIC_ROI=0
RK_YOLO_ALARM_OVERLAY=0
RK_YOLO_BOX_SMOOTH=1
RK_YOLO_TRACK_MODE=motion
detect_every_n=1
```

Artifact directory:

```text
/home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_live_rtsp/artifacts/rga_live_20260428_230046
```

| Mode | stream_fps avg | npu_fps avg | work_ms avg | end_to_end_ms avg | dropped_capture | dropped_publish |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `opencv` | 6.99 | 6.99 | 145.23 | 256.84 | 268 | 0 |
| `rga_cvt_resize` | 8.00 | 8.00 | 132.65 | 242.48 | 216 | 0 |

Observation:

`rga_cvt_resize` improves the full-frame stress case. Average stream/NPU throughput increased by about 14.4%, and average work time decreased by about 8.7%. This confirms that the live RTSP program is actually using the RGA path rather than silently falling back to OpenCV.

## Test 2: Real-Time Demo Mode

Purpose: verify the current real-time strategy with keyframe inference, motion reuse, and dynamic ROI.

Common settings:

```bash
RK_YOLO_INPUT_LOOP=1
RK_YOLO_DYNAMIC_ROI=1
RK_YOLO_ALARM_OVERLAY=0
RK_YOLO_BOX_SMOOTH=1
RK_YOLO_TRACK_MODE=motion
detect_every_n=3
```

Artifact directory:

```text
/home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_live_rtsp/artifacts/rga_live_n3_20260428_230455
```

| Mode | stream_fps avg | npu_fps avg | infer work_ms avg | infer end_to_end_ms avg | dropped_capture | dropped_publish |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `opencv` | 12.27 | 4.43 | 143.60 | 144.60 | 0 | 40 |
| `rga_cvt_resize` | 12.28 | 4.42 | 139.96 | 141.05 | 0 | 38 |

Observation:

In the current demonstration-oriented configuration, the throughput is almost unchanged because inference is intentionally skipped on non-keyframes. The RGA path still slightly reduces inference-frame work time and end-to-end latency, while preserving stable capture and RTSP publishing.

## Conclusion

The RGA preprocessing line is now implemented in both fixed-video and live-RTSP paths:

- `rk_yolo_video` can use `RK_YOLO_PREPROCESS=rga_cvt_resize`.
- `rk_yolo_live_rtsp` can also use the same environment variable after this build update.
- The default remains OpenCV, so the previously stable path is not affected.

Recommended project conclusion:

- For normal demonstration, keep the existing real-time strategy (`detect_every_n=3`, motion reuse, dynamic ROI).
- For hardware-acceleration comparison and thesis discussion, report `rga_cvt_resize` as an optional RGA preprocessing path that is functionally validated and shows measurable benefit in full-frame stress mode.
- RGA is useful, but it is not yet the dominant bottleneck under the final real-time policy because RKNN inference and scheduling still dominate the end-to-end latency.
