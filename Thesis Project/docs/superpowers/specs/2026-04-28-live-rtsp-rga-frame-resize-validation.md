# 2026-04-28 Live RTSP RGA Frame Resize Validation

## Purpose

This validation extends the RGA line from model-input preprocessing to frame-size normalization in the live RTSP pipeline.

The newly added path is controlled by:

```bash
RK_YOLO_RGA_FRAME_RESIZE=1
```

It is disabled by default. `RK_YOLO_RGA_PUBLISH_RESIZE=1` is also accepted as a compatibility alias. If RGA rejects a frame, the program falls back to OpenCV resize.

## Implementation Scope

Program:

```text
rk_yolo_live_rtsp
```

Code path:

- The capture thread reads a frame from camera or video file.
- If the frame size differs from the configured RTSP output size, the program resizes it.
- With `RK_YOLO_RGA_FRAME_RESIZE=1`, this resize step uses RGA first.
- With the default setting, the resize step uses OpenCV.

Runtime logs now include:

```text
rga_frame_resize_runs
opencv_frame_resize_runs
```

These counters make the hardware path observable from stdout.

## Build Result

Board project path:

```text
/home/ubuntu/eclipse-workspace/eclipse-workspace
```

Build command:

```bash
cd /home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_live_rtsp/build
cmake ..
cmake --build . -j2
```

CMake result:

```text
-- librga found: optional RK_YOLO_PREPROCESS=rga/rga_cvt_resize paths enabled
```

The program built successfully.

## Test Input

Video:

```text
/home/ubuntu/public_videos/video01.mp4
```

Video properties:

```text
1920 x 1080, 20 fps, duration 52.5 s
```

RTSP output:

```text
640 x 480, 15 fps
```

Model:

```text
/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn
```

Common runtime settings:

```bash
RK_YOLO_INPUT_LOOP=1
RK_YOLO_PREPROCESS=opencv
RK_YOLO_DYNAMIC_ROI=1
RK_YOLO_ALARM_OVERLAY=0
RK_YOLO_BOX_SMOOTH=1
RK_YOLO_TRACK_MODE=motion
detect_every_n=3
score=0.20
nms=0.45
```

Artifact directory:

```text
/home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_live_rtsp/artifacts/rga_frame_resize_20260428_232302
```

## Results

The resize counters confirm that the intended paths were used.

| Mode | rga_frame_resize_runs | opencv_frame_resize_runs | stream_fps avg | npu_fps avg | work_ms avg | end_to_end_ms avg | dropped_capture | dropped_publish |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| OpenCV frame resize | 0 | 444 | 10.14 | 3.36 | 108.90 | 109.47 | 0 | 0 |
| RGA frame resize | 449 | 0 | 10.28 | 3.42 | 93.42 | 94.07 | 0 | 0 |

Inference-frame-only comparison:

| Mode | infer work_ms avg | infer end_to_end_ms avg |
| --- | ---: | ---: |
| OpenCV frame resize | 114.63 | 115.19 |
| RGA frame resize | 109.90 | 110.56 |

## Interpretation

RGA frame resize was functionally validated in the live RTSP path. On a 1920 x 1080 input resized to 640 x 480, the RGA path removed the OpenCV resize from the capture stage and produced a measurable latency reduction.

The improvement is clearer in the total pipeline average because non-keyframe reuse frames also benefit from cheaper frame preparation. On inference frames only, the benefit is smaller but still positive.

## Conclusion

This step strengthens the RGA hardware-optimization line:

- Model-input RGA preprocessing is available through `RK_YOLO_PREPROCESS=rga_cvt_resize`.
- Live RTSP frame-size normalization is available through `RK_YOLO_RGA_FRAME_RESIZE=1`.
- Both paths are optional and default to the stable OpenCV implementation.

Recommended use:

- Keep OpenCV as the default demonstration path unless a specific RGA comparison is needed.
- Use `RK_YOLO_RGA_FRAME_RESIZE=1` when the input source is higher resolution than the RTSP output resolution.
- Report this as a validated RGA acceleration experiment in the thesis rather than claiming a complete zero-copy video pipeline.
