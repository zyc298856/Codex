# Route B: MPP Decode + DMA + RGA + RKNN Experimental Path

## Goal

Build an independent experimental path that is closer to the RK3588 hardware
pipeline:

```text
V4L2 compressed camera stream
  -> MPP hardware decode
  -> decoded MppFrame DMA buffer
  -> RGA resize / color convert / letterbox
  -> RKNN input memory
  -> NPU inference
```

This path is not a replacement for the stable demonstration program. It is used
to validate RGA hardware preprocessing and RKNN input-memory binding on top of
MPP decoded frames.

## Scope

The first implementation focuses on a headless verification binary:

- capture H.264 or MJPEG frames from the USB camera through V4L2;
- feed compressed packets to Rockchip MPP decoder;
- obtain decoded `MppFrame` buffer fd, width, height, stride, and format;
- use RGA to letterbox the decoded DMA frame into RKNN input memory;
- call RKNN without `rknn_inputs_set`;
- print per-stage timings and detection counts.

RTSP visualization is intentionally left outside this first binary. The existing
stable live demo remains responsible for presentation-quality output.

## Success Criteria

- The new binary builds independently as `rk_yolo_mpp_dma_demo`.
- The stable `rk_yolo_video` and `rk_yolo_dma_rtsp_demo` targets remain
  unchanged.
- Board logs show the route:
  `V4L2 compressed -> MPP decode -> MppFrame fd -> RGA -> RKNN input mem -> NPU`.
- At least one camera mode (`H264` preferred, `MJPEG` fallback) produces decoded
  frames and completes RKNN inference.

## Known Risks

- USB camera H.264 packet boundaries may require several packets before MPP
  produces the first decoded frame.
- MPP decoded frame stride can differ from visible width. The RGA wrapper must
  use visible width/height plus horizontal and vertical stride.
- Full end-to-end zero-copy visualization needs a separate output path. This
  first version validates the inference-side low-copy chain.

## Board Validation

Validation date: 2026-05-14

Board: RK3588, USB camera `/dev/video48`, H.264 640x480@15fps.

Binary:

```text
rk_yolo_mpp_dma_demo
```

Validated route:

```text
V4L2 H.264 packet
  -> Rockchip MPP decoder
  -> decoded MppFrame DMA fd
  -> RGA letterbox into RKNN input memory
  -> RKNN inference without rknn_inputs_set
  -> NPU
```

Representative 100-frame run:

```text
camera opened: /dev/video48 640x480 H264 buffers=4
rga_api version 1.10.1_[10]
DMA fd -> RGA -> RKNN input mem path enabled: rknn_inputs_set skipped
summary packets=469 decoded_frames=100 inferred_frames=100 detected_frames=0
total_detections=0 wall_fps=3.05 avg_decode_ms=0.20
avg_prepare_ms=1.04 avg_run_ms=93.65 avg_total_ms=95.95
ROUTE_B_H264_100_EXIT:0
```

Result:

- H.264 route B is functionally validated for inference-side low-copy data flow.
- MPP decoded frames provide DMA fds that can be consumed by RGA.
- RGA can write the letterboxed tensor directly into RKNN input memory.
- `rknn_inputs_set` is skipped in this path.
- The current binary is a headless validator, not the stable presentation path.
- The camera H.264 stream occasionally emits MPP error frames; the validator
  drains and skips them so the route can continue. This should be treated as a
  camera/stream robustness issue rather than a failure of the RGA/RKNN binding.

## Public Video File Validation

Validation date: 2026-05-14

Input video:

```text
pexels_18253602_drone_flying_18s.mp4
H.264, 1920x1080, about 20 s
```

The MP4 was converted to an Annex-B H.264 elementary stream with `ffmpeg
-nostdin -bsf:v h264_mp4toannexb`, then tested by the file-based route B
validator:

```text
MP4/H.264 public video
  -> Annex-B H.264 elementary stream
  -> Rockchip MPP decoder
  -> decoded MppFrame DMA fd
  -> RGA letterbox into RKNN input memory
  -> RKNN inference without rknn_inputs_set
  -> NPU
```

Representative 60-frame run:

```text
aggressive route B file validator enabled
path=H264 elementary stream -> MPP decode -> MppFrame fd -> RGA letterbox -> RKNN input memory -> NPU
rga_api version 1.10.1_[10]
DMA fd -> RGA -> RKNN input mem path enabled: rknn_inputs_set skipped
summary chunks=3 decoded_frames=60 inferred_frames=60 detected_frames=10
total_detections=10 wall_fps=9.55 avg_decode_ms=123.23
avg_prepare_ms=2.54 avg_run_ms=94.63 avg_total_ms=98.41
ROUTE_B_FILE_60_EXIT:0
```

Earlier 120-frame run on the same H.264 stream also completed successfully:

```text
decoded_frames=120 inferred_frames=120 detected_frames=25
total_detections=27 wall_fps=9.58 avg_prepare_ms=2.47
avg_run_ms=94.07 avg_total_ms=97.79
```

Result:

- Route B has been validated not only with the USB camera H.264 stream, but
  also with a public drone video input.
- The validator produced effective drone detections on the public video.
- The current file validator is still log/profiling oriented. It proves the
  hardware data path, but it does not yet render annotated video output.
- For presentation, the stable `rk_yolo_live_rtsp` path remains the default.
  Route B is kept as an optimization and technical-depth path.

## Public Video Annotated Output Validation

Validation date: 2026-05-15

The file-based route B validator was extended with an optional annotated video
output path. When an output file is provided, each decoded `MppFrame` is copied
through RGA into a BGR visualization frame, the NPU detections are drawn on the
original-resolution image, and OpenCV writes the result to MP4. This visualization
copy is intentionally separated from the inference path; the inference input
still uses `MppFrame fd -> RGA -> RKNN input memory`.

Representative 90-frame run:

```text
visual_output=.../route_b_public_drone_90f_boxed.mp4 fps=20
DMA fd -> RGA -> RKNN input mem path enabled: rknn_inputs_set skipped
summary chunks=5 decoded_frames=90 inferred_frames=90 detected_frames=64
total_detections=73 visualized_frames=90 wall_fps=5.06
avg_prepare_ms=2.14 avg_run_ms=86.37 avg_total_ms=89.33
ROUTE_B_VIS_EXIT:0
```

Generated output:

```text
rk_yolo_video/eval_runs/route_b_visual/route_b_public_drone_90f_boxed.mp4
codec=mpeg4, 1920x1080, 20 fps, 90 frames, 4.5 s
```

Result:

- Route B now has a visible annotated-video artifact for public video input.
- The generated MP4 demonstrates that the hardware optimized path can be
  connected to actual detection-box rendering.
- This file-output route is the safest visual validation layer before attempting
  a full low-copy RTSP presentation path.

## Display-Side Box Stabilization

Validation date: 2026-05-15

The first annotated route B video used raw frame-by-frame detections for drawing.
On the public drone video, the model occasionally selected boxes with noticeably
different size or center, so the rendered box appeared to zoom and jump even
when the detection path itself was working. To improve presentation quality, a
lightweight display-only stabilizer was added:

- choose the highest-confidence detection for visualization;
- smooth `x/y/w/h` with exponential interpolation;
- reduce the interpolation factor when the box area changes abruptly;
- keep the last display box for a few frames when the detector briefly misses.

This stabilizer does not change the raw NPU detections, CSV-style statistics, or
route B timing measurements. It only changes the box used by the optional MP4
visualization output.

Representative 90-frame smoothed run:

```text
visual_output=.../route_b_public_drone_90f_boxed_smooth.mp4 fps=20
DMA fd -> RGA -> RKNN input mem path enabled: rknn_inputs_set skipped
summary chunks=5 decoded_frames=90 inferred_frames=90 detected_frames=51
total_detections=56 visualized_frames=90 wall_fps=5.06
avg_prepare_ms=2.16 avg_run_ms=89.43 avg_total_ms=92.41
ROUTE_B_VIS_SMOOTH_EXIT:0
```

Generated output:

```text
rk_yolo_video/eval_runs/route_b_visual/route_b_public_drone_90f_boxed_smooth.mp4
codec=mpeg4, 1920x1080, 20 fps, 90 frames, 4.5 s
```

Result:

- The annotated public-video output is visibly steadier than the raw-detection
  rendering.
- The change is safe for presentation because it is isolated to the visualization
  layer and does not affect the verified route B hardware data path.

## Display Size-Clamp Update

Validation date: 2026-05-15

After reviewing the first smoothed video, the box center was reasonably stable
but the box width and height still appeared to expand and shrink. The display
stabilizer was therefore updated to smooth center and size separately:

- center position follows the detection faster;
- width and height use a much smaller smoothing factor;
- each frame limits the maximum width/height change;
- abrupt area changes are treated more conservatively.

Representative 90-frame size-clamped run:

```text
visual_output=.../route_b_public_drone_90f_boxed_ultrasmooth.mp4 fps=20
DMA fd -> RGA -> RKNN input mem path enabled: rknn_inputs_set skipped
summary chunks=5 decoded_frames=90 inferred_frames=90 detected_frames=53
total_detections=63 visualized_frames=90 wall_fps=5.08
avg_prepare_ms=2.15 avg_run_ms=90.47 avg_total_ms=93.47
ROUTE_B_VIS_ULTRASMOOTH_EXIT:0
```

Generated output:

```text
rk_yolo_video/eval_runs/route_b_visual/route_b_public_drone_90f_boxed_ultrasmooth.mp4
codec=mpeg4, 1920x1080, 20 fps, 90 frames, 4.5 s
```

Result:

- Box breathing is reduced further compared with the first smoothed output.
- This remains a presentation-layer improvement only; raw detection counts and
  route B timing data remain based on the original per-frame NPU outputs.

## Target-Lock Display Update

Validation date: 2026-05-15

The size-clamped display output still showed noticeable movement when the model
produced several candidate boxes with different centers. The display selector
was therefore changed from highest-score-only selection to target association:

- after the first valid box, prefer detections with higher IoU to the previous
  display box;
- reject spatially implausible high-score candidates;
- keep the same smoothing and short hold strategy after the target is selected.

Representative 90-frame target-lock run:

```text
visual_output=.../route_b_public_drone_90f_boxed_targetlock.mp4 fps=20
DMA fd -> RGA -> RKNN input mem path enabled: rknn_inputs_set skipped
summary chunks=5 decoded_frames=90 inferred_frames=90 detected_frames=47
total_detections=55 visualized_frames=90 wall_fps=5.25
avg_prepare_ms=2.13 avg_run_ms=86.58 avg_total_ms=89.54
ROUTE_B_VIS_TARGETLOCK_EXIT:0
```

Generated output:

```text
rk_yolo_video/eval_runs/route_b_visual/route_b_public_drone_90f_boxed_targetlock.mp4
codec=mpeg4, 1920x1080, 20 fps, 90 frames, 4.5 s
```

Result:

- The display box is less likely to jump to another candidate detection.
- This version is preferred when presentation stability is more important than
  tightly following every raw detector output.

## Presentation-Lock Display Update

Validation date: 2026-05-15

After reviewing the target-lock and hard-lock outputs, the remaining visible
jitter mainly came from two display-side effects: temporary detector misses
caused the display state to reset, and low-confidence frames still changed the
box width and height too quickly. The route B visualization layer was therefore
updated again for presentation use:

- keep the last display box for up to 24 frames instead of resetting after a few
  missed frames;
- use a slower center update and a stricter per-frame center movement limit;
- make width and height adaptation much slower than center movement;
- penalize candidate boxes with large center or area changes more strongly.

This is still a visualization-only strategy. The MPP decode, DMA-fd RGA
preprocessing, RKNN input-memory binding, NPU inference, and raw detection
statistics are unchanged.

Representative 90-frame presentation-lock run:

```text
visual_output=.../route_b_public_drone_90f_boxed_presentlock.mp4 fps=20
DMA fd -> RGA -> RKNN input mem path enabled: rknn_inputs_set skipped
summary chunks=5 decoded_frames=90 inferred_frames=90 detected_frames=78
total_detections=80 visualized_frames=90 wall_fps=5.08
avg_decode_ms=113.82 avg_prepare_ms=2.16 avg_run_ms=90.54 avg_total_ms=93.60
```

Generated output:

```text
rk_yolo_video/eval_runs/route_b_visual/route_b_public_drone_90f_boxed_presentlock.mp4
codec=mpeg4, 1920x1080, 20 fps, 90 frames, 4.5 s
```

Result:

- The displayed box no longer resets abruptly during short detection gaps.
- Box breathing is reduced by making width/height updates very conservative.
- This version is the preferred route B file-output visualization for
  presentation, while the stable live demonstration route remains unchanged.
