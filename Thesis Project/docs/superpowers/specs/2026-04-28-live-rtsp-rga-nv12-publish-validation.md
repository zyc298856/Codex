# 2026-04-28 Live RTSP RGA NV12 Publish Validation

## Purpose

This validation checks whether the live RTSP publishing path can bypass GStreamer's `videoconvert` stage by converting the final BGR display frame to NV12 before feeding `mpph264enc`.

The experimental switch is:

```bash
RK_YOLO_RGA_PUBLISH_NV12=1
```

Default behavior is unchanged:

```text
BGR appsrc -> videoconvert -> mpph264enc
```

Experimental behavior:

```text
BGR display frame -> RGA BGR-to-NV12 -> NV12 appsrc -> mpph264enc
```

## Implementation Scope

Program:

```text
rk_yolo_live_rtsp
```

Runtime behavior:

- When `RK_YOLO_RGA_PUBLISH_NV12=0`, appsrc publishes BGR frames and the pipeline keeps `videoconvert`.
- When `RK_YOLO_RGA_PUBLISH_NV12=1`, appsrc publishes NV12 frames and the pipeline removes `videoconvert`.
- The program first tries RGA conversion.
- If RGA rejects a frame, OpenCV converts BGR to I420 and repacks it to NV12 as a fallback.

Runtime counters:

```text
rga_publish_nv12_runs
opencv_publish_nv12_runs
```

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

The build succeeded. CMake detected `librga`.

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
RK_YOLO_RGA_FRAME_RESIZE=0
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
/home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_live_rtsp/artifacts/rga_publish_nv12_20260428_234828
```

## Results

Both paths produced valid RTSP streams. `ffmpeg` successfully pulled H.264 video from both ports.

| Mode | publish format | rga_publish_nv12_runs | opencv_publish_nv12_runs | stream_fps avg | npu_fps avg | work_ms avg | end_to_end_ms avg | dropped_capture | dropped_publish |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Baseline | BGR + `videoconvert` | 0 | 0 | 10.35 | 3.45 | 70.62 | 71.19 | 0 | 0 |
| RGA NV12 publish | NV12 direct to encoder | 445 | 0 | 10.21 | 3.40 | 106.27 | 107.87 | 0 | 0 |

Inference-frame-only comparison:

| Mode | infer work_ms avg | infer end_to_end_ms avg |
| --- | ---: | ---: |
| Baseline | 108.64 | 109.18 |
| RGA NV12 publish | 106.27 | 107.87 |

## Interpretation

The RGA NV12 publish path is functionally valid:

- The program can publish NV12 frames directly to `mpph264enc`.
- `videoconvert` can be removed from the GStreamer pipeline.
- RGA handled all tested BGR-to-NV12 conversions; no OpenCV fallback was used.

However, the measured end-to-end benefit is not decisive in this test. The baseline path already performs reasonably well, and adding an explicit BGR-to-NV12 conversion before appsrc can offset the benefit of removing `videoconvert`. In the overall averages, the RGA NV12 path was slower; in inference-frame-only averages, it was slightly faster.

## Conclusion

This experiment should be reported as a validated optional path, not as the default recommendation.

Recommended project setting:

- Keep the default RTSP publishing path as `BGR + videoconvert`.
- Keep `RK_YOLO_RGA_PUBLISH_NV12=1` as an optional hardware-video-output experiment.
- Use this result in the thesis to show that RGA-related optimizations were investigated with measured evidence, including cases where hardware offload is functionally correct but not always the best system-level choice.
