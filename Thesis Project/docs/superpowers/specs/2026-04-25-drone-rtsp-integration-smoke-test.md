# Drone RKNN RTSP Integration Smoke Test

Date: 2026-04-25

## Purpose

Verify that the recovered single-class drone RKNN model can be used by the live RTSP pipeline on RK3588 without breaking the previously stable real-time path.

## Board Access

- Location profile: home
- SSH target: `ubuntu@192.168.2.156`
- Board hostname: `focal`
- Architecture: `aarch64`

## Code Change

`rk_yolo_live_rtsp` now detects when the loaded RKNN model has one class and switches overlay labels from the COCO class table to `drone`.

This prevents the single-class drone model from displaying `class_id=0` as `person`.

## Build Result

Board build command:

```bash
cd /home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_live_rtsp/build
cmake ..
make -j4
```

Result:

- Build passed.
- Only GStreamer system deprecation warnings were shown.

## Runtime Command

```bash
RK_YOLO_INPUT_LOOP=1 RK_YOLO_BOX_SMOOTH=0 ./rk_yolo_live_rtsp \
  /home/ubuntu/public_videos/video01.mp4 \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn \
  /drone 640 480 15 0.35 0.45 8554 3
```

PC viewing URL:

```text
rtsp://192.168.2.156:8554/drone
```

## Key Runtime Evidence

Startup log:

```text
output index=0 name=output0 dims=[1, 5, 8400]
resolved model classes: 1
source_type=video_file
input_loop=on
model_classes=1
label_mode=single-class-drone
rtsp path=rtsp://<board-ip>:8554/drone
detect_every_n=3
track_mode=motion
box_smooth=off
dynamic_roi=on
```

RTSP probe:

```text
codec_name=h264
width=640
height=480
avg_frame_rate=15/1
```

Live detection log excerpt:

```text
captured=23 inferred=23 published=22 npu_infer_runs=8 reused_frames=15 roi_crop_runs=6 detections=1 last_mode=infer_roi work_ms=118.37 stream_fps=9.98 npu_fps=3.33 end_to_end_ms=118.92
captured=44 inferred=44 published=43 npu_infer_runs=15 reused_frames=29 roi_crop_runs=12 detections=1 last_mode=infer_roi work_ms=105.39 stream_fps=10.29 npu_fps=3.43 end_to_end_ms=106.16
```

## Current Conclusion

The recovered drone-specific RKNN model is now integrated into the live RTSP pipeline at smoke-test level.

The current stable viewing configuration remains:

- `detect_every_n=3`
- `track_mode=motion`
- `dynamic_roi=on`
- `box_smooth=off`
- `score=0.35`
- `nms=0.45`

## Remaining Work

- Open the RTSP stream from the PC player and capture a visual screenshot.
- Run a longer fixed-video RTSP stability test.
- Compare `detect_every_n=2/3/4` under the same public video input.
- Only after the fixed-video RTSP path is stable, switch the input source back to the USB camera.
