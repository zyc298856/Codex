# rk_yolo_live_rtsp

Standalone RK3588 live demo:

- capture from a USB UVC camera
- or replay from a local video file through the same pipeline
- run YOLOv10 through RKNN
- draw boxes on each frame
- publish an RTSP stream through `GStreamer RTSP Server`
- use a 3-stage pipeline: capture -> infer -> publish

This tool is intentionally independent from the existing Jetson encoder path.

For the new single-class drone detector trained in this workspace, do not start here first. Validate the drone RKNN model in `rk_yolo_video` before moving it into the live RTSP path. That reduces the number of moving parts during the first board-side bring-up.

## Build On RK3588

```bash
mkdir -p build
cd build
cmake ..
make -j4
```

## Run

```bash
./rk_yolo_live_rtsp [device] [model] [mount] [width] [height] [fps] [score] [nms] [port] [detect_every_n]
```

Example:

```bash
./rk_yolo_live_rtsp /dev/video48 ../../yolov10n.rknn /yolo 640 480 15 0.30 0.45 8554 2
./rk_yolo_live_rtsp /home/ubuntu/public_videos/sample.mp4 ../../yolov10n.rknn /yolo 640 480 15 0.30 0.45 8554 3
```

Drone-specific fixed-video RTSP smoke test:

```bash
RK_YOLO_INPUT_LOOP=1 RK_YOLO_BOX_SMOOTH=0 ./rk_yolo_live_rtsp \
  /home/ubuntu/public_videos/video01.mp4 \
  ../../training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn \
  /drone 640 480 15 0.35 0.45 8554 3
```

Then open this on your PC:

```text
rtsp://192.168.10.186:8554/yolo
rtsp://192.168.2.156:8554/drone
```

## Notes

- The tool requests `MJPG` from the USB camera to reduce CPU load during capture.
- If the first argument is not a `/dev/video*` path, it is treated as a local video file input and opened with OpenCV's default backend.
- For local video files, set `RK_YOLO_INPUT_LOOP=1` if you want the file to loop after reaching the end. This is useful for repeatable fixed-input RTSP experiments.
- `mpph264enc` is used through GStreamer for H.264 encoding on RK3588.
- The stream starts producing frames after a client connects to the RTSP URL.
- The internal queues are intentionally small and will drop old frames to keep latency bounded.
- Preprocessing defaults to OpenCV. When `librga` is available at build time, you can compare the optional hardware preprocessing paths without changing code:
  - `RK_YOLO_PREPROCESS=opencv` keeps the stable default path
  - `RK_YOLO_PREPROCESS=rga` uses OpenCV for BGR-to-RGB conversion and RGA for resize
  - `RK_YOLO_PREPROCESS=rga_cvt_resize` uses RGA for BGR-to-RGB conversion and resize
  - if `librga` is not detected, the program prints a fallback warning and continues with OpenCV
- `RK_YOLO_RGA_FRAME_RESIZE=1` enables an optional RGA path for resizing captured frames to the RTSP output size before inference and publishing. It is off by default and falls back to OpenCV if RGA rejects the frame.
- `RK_YOLO_RGA_PUBLISH_RESIZE=1` is accepted as a compatibility alias for the same frame-resize experiment.
- Runtime logs include `rga_frame_resize_runs` and `opencv_frame_resize_runs` so this experiment can be verified from stdout.
- If the USB camera reconnects and its node changes from `/dev/video48` to a nearby node such as `/dev/video49`, the tool will probe nearby `/dev/video*` devices automatically.
- For the newly tested `HBS Camera` UVC module, the tool now applies a verified startup tune by default:
  - `zoom_absolute=20`
  - `focus_auto=0`
  - `focus_absolute=260`
  - the tune is best-effort and automatically skips non-matching cameras
- `detect_every_n=1` means every frame runs NPU inference; values above `1` run YOLO on keyframes and use lightweight optical-flow box tracking on the frames in between.
- `RK_YOLO_TRACK_MODE=motion` uses a lightweight box-level motion predictor between keyframes and is now the default.
- `RK_YOLO_TRACK_MODE=optflow` restores the older Lucas-Kanade optical-flow tracker if you want to compare behavior.
- `RK_YOLO_BOX_SMOOTH=1` enables lightweight temporal box smoothing on the displayed detections and is on by default.
- `RK_YOLO_BOX_SMOOTH_ALPHA=0.60` controls how strongly the new frame pulls the box toward the latest detection.
- `RK_YOLO_BOX_SMOOTH_IOU=0.10` is the minimum IoU used to match boxes across adjacent frames for smoothing.
- `RK_YOLO_ALARM_OVERLAY=1` enables a software alarm banner in the live RTSP image and is on by default.
- `RK_YOLO_ALARM_OVERLAY=0` disables only the banner while keeping detection boxes and RTSP output unchanged.
- `RK_YOLO_ALARM_HOLD_FRAMES=5` keeps the alarm active for a few missed frames to avoid flicker.
- Dynamic ROI is enabled by default for keyframes with existing detections: it crops a generous search window around the last boxes, and periodically falls back to full-frame inference to recover from drift.
- You can tune ROI behavior with environment variables:
  - `RK_YOLO_DYNAMIC_ROI=0` disables ROI cropping
  - `RK_YOLO_ROI_MARGIN=0.35` controls how much margin is added around the last detections
  - `RK_YOLO_ROI_MIN_COVERAGE=0.55` keeps the crop from becoming too small
  - `RK_YOLO_ROI_REFRESH=5` forces a full-frame keyframe every N inference runs
- You can override or disable the startup camera tune with environment variables:
  - `RK_YOLO_CAMERA_TUNE=0` disables the startup tune completely
  - `RK_YOLO_CAMERA_MATCH=HBS Camera` changes which camera name should receive the tune
  - `RK_YOLO_CAMERA_ZOOM=20` overrides the zoom value
  - `RK_YOLO_CAMERA_FOCUS_AUTO=0` selects manual focus mode
  - `RK_YOLO_CAMERA_FOCUS=260` overrides the manual focus value
  - `RK_YOLO_CAMERA_SETTLE_MS=350` controls how long to wait after applying UVC controls
  - `RK_YOLO_CAMERA_WARMUP_GRABS=6` controls how many post-tune frames are discarded before live capture begins
- You can enable experimental multi-context NPU inference with `RK_YOLO_INFER_WORKERS=2`, but it is only activated when `detect_every_n=1`.
- The multi-context path is aimed at throughput experiments and may increase end-to-end latency if you try to use it as a direct replacement for the current real-time viewing setup.
- Runtime logs print `stream_fps`, `npu_fps`, `roi_fps`, `work_ms`, and `alarm` so you can compare full-frame inference, ROI inference, tracking cost, and software-alarm state.
- Set `RK_YOLO_PROFILE=1` only during profiling runs if you need stage-level timing for `prepare`, `rknn_inputs_set`, `rknn_run`, output decode, and rendering. Keep it off for normal demonstrations.
- `RK_YOLO_ZERO_COPY_INPUT=1` enables an experimental RKNN input-memory path. It is disabled by default because the current float16-input model still moves most conversion cost into `rknn_run`.
- The live overlay automatically labels single-class RKNN models as `drone`; multi-class COCO-style models keep the original COCO label table.
- For the drone-specific model, start from the offline-validated threshold pair first; the current recommendation is `score=0.35`, `nms=0.45`.
