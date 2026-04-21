# rk_yolo_live_rtsp

Standalone RK3588 live demo:

- capture from a USB UVC camera
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
```

Then open this on your PC:

```text
rtsp://192.168.10.186:8554/yolo
```

## Notes

- The tool requests `MJPG` from the USB camera to reduce CPU load during capture.
- `mpph264enc` is used through GStreamer for H.264 encoding on RK3588.
- The stream starts producing frames after a client connects to the RTSP URL.
- The internal queues are intentionally small and will drop old frames to keep latency bounded.
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
- Runtime logs print `stream_fps`, `npu_fps`, `roi_fps`, and `work_ms` so you can compare full-frame inference, ROI inference, and tracking cost.
- If you later move the new drone-specific model into this live path, start from the offline-validated threshold pair first; the current recommendation is `score=0.35`, `nms=0.45`.
