# Defense Demo Package

This folder collects the defense-day demo presets without changing the
known-good `rk_yolo_video` or `rk_yolo_live_rtsp` source code.

## Recommended Demo Order

1. Run the board readiness check:

   ```bash
   cd "Thesis Project/defense_demo"
   bash scripts/check_board_ready.sh
   ```

2. Start the main live demo on the RK3588 board:

   ```bash
   bash scripts/run_live_safe.sh
   ```

3. Open the RTSP stream on the PC:

   ```text
   rtsp://<board-ip>:8554/drone
   ```

4. If the target is too hard to detect, switch to the sensitive preset:

   ```bash
   bash scripts/run_live_sensitive.sh
   ```

5. If the classroom network or camera is unstable, use the fixed-video backup:

   ```bash
   bash scripts/run_video_backup.sh /home/ubuntu/public_videos/video01.mp4
   ```

6. If the teacher asks about the more aggressive hardware path, show it as a
   technical backup rather than the first live demo. The current route-B
   result uses MPP decoding, RGA preparation, RKNN memory binding, and a
   presentation-lock display strategy to reduce visible box jitter.

## Presets

- `config/live_safe.env`: stable classroom live demo. It disables dynamic ROI and multi-context, keeps box smoothing, and enables controlled auto zoom.
- `config/live_sensitive.env`: lower confidence threshold for harder targets. Use it when the model misses the phone-screen drone image.
- `config/video_backup.env`: fixed video validation path. Use it as a reliable fallback when the live camera path is affected by network or lighting.

## Why These Defaults

The live demo should prioritize visual stability over peak NPU throughput.
Therefore the default preset uses:

- FP RKNN model as the stable baseline
- `score=0.24`, `nms=0.45`
- `detect_every_n=3`
- `RK_YOLO_DYNAMIC_ROI=0` to avoid ROI drift on hand-held targets
- `RK_YOLO_BOX_SMOOTH=1` to reduce box jitter
- `RK_YOLO_TRACK_MODE=motion` for lightweight continuity between detections
- `RK_YOLO_AUTO_ZOOM=1` with rate limiting, so zoom changes are visible but not distracting

For the defense, do not enable RGA, zero-copy, or multi-context in the main
live demonstration unless the stable path has already been shown. Those paths
are useful for technical explanation and backup comparison, but the live demo
should not depend on them.

## Route-B Technical Backup

Route B is for explaining the performance-oriented direction:

```text
compressed video -> MPP decode -> RGA resize/color/letterbox -> RKNN input memory -> NPU -> boxed video
```

Use this only after the stable live chain has been demonstrated. Its value in
the defense is to show that the project has explored the RK3588 hardware path
beyond OpenCV, while the default live preset remains the safer choice for the
classroom environment.
