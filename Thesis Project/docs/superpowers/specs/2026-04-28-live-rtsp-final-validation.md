# Live RTSP Final Validation

## Context

Date: 2026-04-28  
Board location: grandmother's home network  
SSH target: `ubuntu@192.168.10.186`  
RTSP URL: `rtsp://192.168.10.186:8554/yolo`

The latest local `rk_yolo_live_rtsp` source was synchronized to the RK3588 board and rebuilt successfully.

Board path:

```text
/home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_live_rtsp
```

Stable model:

```text
/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn
```

## RTSP Smoke Test

The RTSP stream was verified from the PC with `ffprobe`:

```text
codec_name=h264
codec_type=video
width=640
height=480
avg_frame_rate=15/1
```

This confirms that the board can publish an H.264 RTSP stream and the PC can receive it over the current network.

## Recommended Live Viewing Configuration

Command configuration:

```text
RK_YOLO_INPUT_LOOP=1
RK_YOLO_INFER_WORKERS=1
RK_YOLO_BOX_SMOOTH=1
RK_YOLO_DYNAMIC_ROI=1
RK_YOLO_TRACK_MODE=motion
detect_every_n=2
```

Input video:

```text
/home/ubuntu/public_videos/anti_uav_fig1.mp4
```

Observed log samples:

```text
captured=32 inferred=24 published=24 npu_infer_runs=15 reused_frames=9 roi_crop_runs=12
detections=1 last_mode=infer_roi work_ms=144.54 stream_fps=10.79 npu_fps=6.57
roi_fps=5.63 end_to_end_ms=234.38 dropped_capture=6 dropped_publish=0
```

This configuration produced positive detections (`detections=3` and `detections=1`) and a valid RTSP stream. It is still the recommended demo configuration because it provides smoother viewing behavior through frame reuse, ROI, motion tracking and box smoothing.

## Multi-context Hardware Demonstration Configuration

Command configuration:

```text
RK_YOLO_INPUT_LOOP=1
RK_YOLO_INFER_WORKERS=2
RK_YOLO_BOX_SMOOTH=1
RK_YOLO_DYNAMIC_ROI=1
RK_YOLO_TRACK_MODE=motion
detect_every_n=1
```

The program entered:

```text
multi-context full-frame inference mode enabled
```

Observed log sample:

```text
captured=30 inferred=28 published=27 npu_infer_runs=28 reused_frames=0 roi_crop_runs=0
detections=1 last_mode=infer_full work_ms=101.48 stream_fps=12.96 npu_fps=13.46
roi_fps=0.00 end_to_end_ms=232.31 dropped_capture=0 dropped_publish=0
```

This configuration is useful for hardware optimization discussion because it demonstrates higher NPU inference throughput. However, it is not necessarily the best viewing mode because full-frame detection can still produce higher end-to-end latency than the policy-optimized viewing path.

## Conclusion

The live RTSP path is functional on the RK3588 board:

- The stable end2end-false RKNN model loads correctly.
- RTSP output is reachable from the PC.
- Recommended viewing configuration produces a valid stream and positive detections.
- Multi-context mode demonstrates higher NPU throughput and supports the thesis hardware-optimization argument.

Current recommended split:

- Demo/live viewing: `detect_every_n=2`, `infer_workers=1`, smoothing and dynamic ROI enabled.
- NPU multi-threading experiment: `detect_every_n=1`, `infer_workers=2`.
