# Production MPP/DMA/RGA/RKNN/RTSP Live Chain Design

Date: 2026-05-15

## Goal

Build an isolated experimental live-camera binary that moves the project closer to a production-grade RK3588 video AI pipeline:

```text
USB camera compressed stream
-> V4L2 capture
-> Rockchip MPP hardware decode
-> MppFrame DMA buffer
-> RGA resize / color convert / letterbox
-> RKNN input memory
-> NPU inference
-> RTSP output
```

The existing stable defense demo must remain untouched.

## Current Baseline

The project already has three validated building blocks:

- `rk_yolo_live_rtsp`: stable defense live demo, based on the safer OpenCV-oriented path.
- `rk_yolo_dma_rtsp_demo`: V4L2 YUYV DMA fd -> RGA -> RKNN input memory -> NPU -> RTSP.
- `rk_yolo_mpp_dma_demo`: V4L2 compressed packet -> MPP decode -> MppFrame DMA fd -> RGA -> RKNN input memory -> NPU.

The missing piece is a live RTSP binary that combines MPP camera decode with the existing RTSP visualization output.

## Implementation Strategy

Add a new CMake target:

```text
rk_yolo_mpp_dma_rtsp_demo
```

This target will reuse `src/mpp_dma_demo.cpp` with a compile-time macro:

```text
MPP_DMA_RTSP_DEMO=1
```

When the macro is not set, the existing `rk_yolo_mpp_dma_demo` behavior must remain unchanged.

## Success Criteria

Minimum acceptable success:

- The binary builds on RK3588.
- It opens the compressed camera stream, preferably H.264.
- It decodes with MPP and obtains `MppFrame` DMA fd.
- It calls `PrepareDmaFdToBoundInputStrided`.
- It runs `InferBoundInput`, skipping `rknn_inputs_set`.
- It publishes an RTSP stream that can be viewed on the PC.
- It prints stage timing: decode / prepare / run / total.

Production-oriented but honest boundary:

- The inference-side path is hardware-shared memory oriented.
- The BGR output mode performs an extra RGA DMA->BGR copy for drawing boxes and appsrc encoding.
- The DMABUF output mode keeps the RTSP output as NV12 DMA memory and removes the application-level BGR visualization copy.
- DMABUF mode is a performance path. It does not draw boxes on the NV12 DMA frame, because the current librga/kernel combination rejects or destabilizes color-fill based rectangle drawing on NV12 DMA buffers.

## Safety Rules

- Do not modify `rk_yolo_live_rtsp`.
- Do not change the stable defense scripts.
- Keep the new path under `rk_yolo_video`.
- Add a separate run script for the new binary.
- Keep logs explicit about which parts are zero-copy and which parts still use a visualization copy.

## Board Validation Notes

Validation board: RK3588 at `192.168.2.156`.

Camera device:

- `/dev/video48`
- H.264, MJPG and YUYV are exposed by UVC.
- H.264 640x480@15 was selected for the MPP decode path.

Implemented artifacts:

- Binary target: `rk_yolo_mpp_dma_rtsp_demo`
- Script: `scripts/run_mpp_dma_rtsp_eval.sh`
- Stable demo target `rk_yolo_live_rtsp` was not modified.

Verified runtime path:

```text
V4L2 H.264 packet
-> MPP decode
-> MppFrame DMA fd
-> RGA letterbox
-> RKNN input memory
-> NPU inference
-> RTSP output
```

Two RTSP output modes are available:

| output_mode | Purpose | Output path | Boundary |
|---|---|---|---|
| `bgr` | Boxed visualization / debugging | MppFrame DMA fd -> RGA BGR copy -> OpenCV drawing -> GStreamer RTSP | Easy to see detections, but not a pure output-memory path |
| `dmabuf` | Performance / low-copy exploration | MppFrame DMA fd -> RGA NV12 DMA output -> GstDmabufMemory -> GStreamer RTSP | CPU BGR visualization copy removed; detection boxes are not drawn in this mode |

Important log evidence:

- `zero_copy_input=on`
- `DMA fd -> RGA -> RKNN input mem path enabled: rknn_inputs_set skipped`
- PC-side `ffprobe` can read `rtsp://192.168.2.156:8562/yolo_mpp` as `H.264 640x480 15fps`.
- Board-side `ffprobe` can read `output_mode=dmabuf` RTSP output as `H.264 640x480 15fps`.
- `output_mode=dmabuf` completed a 30 s validation without RGA overlay errors after disabling NV12 DMA color-fill drawing.
- `output_mode=bgr` still completed a regression validation and remains the boxed RTSP visualization mode.

Resource lifecycle fix:

- Initial RTSP validation exposed MPP buffer release warnings at process exit.
- `MppDecoder::Release()` now drains remaining decoded frames before `reset()` and `mpp_destroy()`.
- Subsequent tests no longer showed `not released`, `Assertion`, or `cleaning leaked buffer` warnings.

Realtime policy result:

Using full inference on every decoded frame made the capture loop too slow and caused many MPP error frames. Adding `DETECT_EVERY_N` fixed the scheduling pressure.

Short comparison at 640x480 H.264:

| detect_every_n | inferred FPS | decoded FPS | Comment |
|---:|---:|---:|---|
| 3 | about 4.95 | about 14.73 | Recommended default for live visualization |
| 4 | about 3.70 | about 14.79 | Lower NPU pressure, slower detection refresh |
| 5 | about 2.99 | about 14.71 | Too slow for responsive target updates |

Recommended experimental default:

```bash
DETECT_EVERY_N=3 CONF=0.24 CODEC=h264 WIDTH=640 HEIGHT=480 FPS=15
```

## 2026-05-15 Additional Async Inference Validation

To explore whether the decode/output loop can be decoupled from the NPU latency, an optional asynchronous inference mode was added to the experimental binary only:

```bash
RK_YOLO_ASYNC_INFER=1
```

Implementation notes:

- The main loop keeps reading MPP decoded frames and publishing RTSP frames.
- A background inference worker owns a small DMA staging buffer pool and a single RKNN detector instance.
- When a frame should be inferred, the main loop submits the latest decoded frame to the worker.
- If inference falls behind, the pending old frame is replaced by a newer frame instead of building a backlog.
- The worker performs `DMA staging buffer -> RGA letterbox -> RKNN input memory -> NPU`.
- The latest available detection result is reused by the display path.

The buffer pool is controlled by:

```bash
RK_YOLO_ASYNC_POOL=3
```

Validation results on board `192.168.2.156`:

| mode | async | RTSP client result | Key log evidence |
|---|---:|---|---|
| `bgr` | 1 | `H.264 640x480 15fps` via `ffprobe` | `async_summary submitted=51 skipped_busy=1 copy_failed=0 inferred=51 detected=11 total_detections=20`, `visualized_frames=32` |
| `dmabuf` | 1 | `H.264 640x480 15fps` via `ffprobe` | `rtsp_dmabuf_output=on`, `zero_copy_input=on`, `async_summary submitted=51 skipped_busy=1 copy_failed=0 inferred=51` |
| `dmabuf` | 0 | `H.264 640x480 15fps` via `ffprobe` | Direct `MppFrame DMA fd -> RGA -> RKNN input memory`, `rknn_inputs_set skipped` |

Interpretation:

- `ASYNC_INFER=0 + OUTPUT_MODE=dmabuf` is the cleanest direct hardware-memory experiment: MPP decoded frame fd is used directly by RGA, then RGA writes into RKNN input memory.
- `ASYNC_INFER=1` improves scheduling isolation, but it intentionally adds one DMA staging copy so the worker can safely run after the decoder advances to later frames.
- Therefore, async mode is not claimed as a stricter zero-copy path. It is a low-blocking scheduling experiment.
- The most honest conclusion is that the project now has two experimental extremes: a direct low-copy path for performance analysis and an async low-blocking path for live scheduling analysis.

## 2026-05-16 DMA Buffer Pool and Matrix Benchmark

The async worker was upgraded from one staging DMA buffer to a configurable pool. A new matrix script now compares four modes under the same camera/model settings:

```bash
scripts/run_mpp_dma_rtsp_matrix.sh
```

The matrix runs:

| Case | output_mode | async | Purpose |
|---|---|---:|---|
| `direct-dmabuf` | `dmabuf` | 0 | Cleanest low-copy performance path |
| `async-dmabuf` | `dmabuf` | 1 | Low-copy output plus non-blocking inference scheduling |
| `direct-bgr` | `bgr` | 0 | Boxed visualization baseline |
| `async-bgr` | `bgr` | 1 | Boxed visualization with async inference |

Short 10 s matrix validation on board `192.168.2.156`:

| Case | RTSP probe | decoded FPS | inferred FPS | Avg prepare / run / total |
|---|---:|---:|---:|---|
| `direct-dmabuf` | pass | 11.77 | 3.92 | 1.01 / 93.70 / 95.94 ms |
| `async-dmabuf` | pass | 6.87 | 2.29 | 1.12 / 97.38 / 99.89 ms |
| `direct-bgr` | pass | 6.54 | 2.25 | 1.17 / 101.93 / 104.39 ms |
| `async-bgr` | pass | 6.53 | 2.24 | 1.12 / 104.39 / 106.89 ms |

The matrix shows that `direct-dmabuf` is still the strongest low-copy performance experiment, while `bgr` remains the practical boxed visualization route.

A stress run with `DETECT_EVERY_N=1`, `OUTPUT_MODE=dmabuf`, `ASYNC_INFER=1`, and `ASYNC_POOL=3` verified that the pool-based latest-frame policy works:

```text
async_summary submitted=142 skipped_busy=0 replaced_pending=44 copy_failed=0 pool_size=3 inferred=98 detected=7 total_detections=7
summary decoded_frames=142 inferred_frames=98 decoded_wall_fps=14.51 infer_wall_fps=10.01 avg_prepare_ms=0.96 avg_run_ms=94.78 avg_total_ms=97.03
```

Interpretation:

- `replaced_pending=44` means the worker intentionally discarded older pending frames when inference could not keep up.
- This avoids queue buildup and is closer to industrial real-time video behavior: freshness is prioritized over processing every frame.
- `direct-dmabuf` remains the best candidate for pure low-copy performance measurement.
- `async + pool` remains the best candidate for latency-controlled live scheduling experiments.

Current production boundary:

- The inference-side path has reached the intended DMA/RGA/RKNN-input-memory form.
- The RTSP side now has two choices: BGR boxed visualization and experimental DMABUF/NV12 output.
- The project has not claimed mathematically complete end-to-end zero-copy. The camera decode frame, RKNN input tensor memory, and RTSP encoder input are still separate memory objects connected by RGA operations.
- The current result is better described as an application-level CPU-copy-reduced chain: it avoids `rknn_inputs_set`, avoids OpenCV decode, and can avoid the application BGR visualization copy in `output_mode=dmabuf`.
- This path is now a production-candidate experimental chain, while the stable defense/demo version remains the safer fallback.

## 2026-05-16 One-Click Production-Candidate Runner

To make this route easier to reproduce and review, a top-level board-side script
was added:

```bash
bash scripts/run_mpp_dma_rtsp_production_candidate.sh
```

It generates a timestamped folder under `eval_runs/` and writes:

```text
environment.txt
matrix/summary.tsv
async_pool_stress.log
report.md
```

The script performs three automated steps:

1. Records board, model, camera, RGA and runtime environment information.
2. Runs the four-mode matrix: `direct-dmabuf`, `async-dmabuf`, `direct-bgr`,
   and `async-bgr`.
3. Runs a stress configuration with `DETECT_EVERY_N=1`,
   `OUTPUT_MODE=dmabuf`, `ASYNC_INFER=1`, and `RK_YOLO_ASYNC_POOL=3`.

The generated `report.md` explicitly separates:

- low-copy performance candidate: `direct-dmabuf`;
- boxed visualization candidate: `direct-bgr` / `async-bgr`;
- scheduling experiment: async DMA pool with latest-frame-wins replacement;
- stable fallback: the original FP RKNN live RTSP demo path.

This turns the aggressive route into a reproducible experiment package rather
than a single manual run. It also keeps the conclusion honest: the current
implementation is the most complete industrial-style chain inside the project,
but it is still a production-candidate experiment until longer real-camera
validation confirms its stability.

## 2026-05-16 Taskbook RGA / Zero-Copy Verification Pack

The production-candidate runner above focuses on camera RTSP comparison. To
match the task-book wording more directly, a second wrapper script was added:

```bash
bash scripts/run_rga_zero_copy_taskbook_eval.sh
```

This script keeps the stable FP RKNN demonstration unchanged and only exercises
the isolated Route B experimental targets. It records `environment.txt`,
captures logs for each input type, and writes a short `report.md` with evidence
checks.

Verified input modes:

| Input type | Target chain | Output |
|---|---|---|
| Fixed public/local video | MP4/H264 -> MPP decode -> MppFrame DMA fd -> RGA letterbox -> RKNN input memory -> NPU | Optional annotated MP4 proof video |
| USB camera | UVC compressed stream -> V4L2 capture -> MPP decode -> MppFrame DMA fd -> RGA letterbox -> RKNN input memory -> NPU -> RTSP | `bgr` boxed RTSP or `dmabuf` low-copy NV12 RTSP |

The generated report checks for these log markers:

```text
zero_copy_input=on
DMA fd -> RGA -> RKNN input mem path enabled: rknn_inputs_set skipped
summary ...
```

Interpretation:

- The fixed-video path is the reproducible input path for papers, audits and
  video-based comparisons.
- The camera path is the realtime input path for demonstration and live
  stability validation.
- Boxed visualization still uses an intentional display-side copy so labels and
  boxes can be drawn.
- `OUTPUT_MODE=dmabuf` is the cleaner low-copy performance stream, but it does
  not draw boxes.

This gives the project a single reviewable entry point for the task-book RGA
and zero-copy requirement while preserving the earlier stable demonstration
version as the fallback.

### Board Validation Record

Board-side validation was performed on 2026-05-16 at home network address
`192.168.2.156`. The stable FP RKNN demonstration route was not modified.

Combined fixed-video and camera evidence folder:

```text
/home/ubuntu/eclipse-workspace/eclipse-workspace/Thesis Project/rk_yolo_video/eval_runs/taskbook_combined_20260516_133634
```

The combined report shows all four evidence checks passing for both fixed video
and camera input:

| Check | Fixed video | Camera |
|---|---:|---:|
| `zero_copy_input=on` | yes | yes |
| `rknn_inputs_set skipped` | yes | yes |
| MPP/DMA path selected | yes | yes |
| normal summary produced | yes | yes |

Key fixed-video result:

```text
summary chunks=53 decoded_frames=130 inferred_frames=130 detected_frames=103 total_detections=293 visualized_frames=130 wall_fps=6.11 avg_prepare_ms=0.79 avg_run_ms=86.24 avg_total_ms=88.27
```

Key camera result:

```text
summary packets=146 decoded_frames=142 inferred_frames=48 detected_frames=4 total_detections=19 visualized_frames=0 infer_wall_fps=4.92 decoded_wall_fps=14.56 avg_prepare_ms=1.03 avg_run_ms=93.22 avg_total_ms=95.47
```

Additional RTSP client validation:

| Folder | Output mode | RTSP probe result | Notes |
|---|---|---|---|
| `taskbook_camera_rtsp_probe_20260516_133903` | `bgr` | `codec_name=h264`, `width=640`, `height=480`, `r_frame_rate=15/1` | boxed visualization path; `visualized_frames=32` |
| `taskbook_camera_dmabuf_probe_20260516_134022` | `dmabuf` | `codec_name=h264`, `width=640`, `height=480`, `r_frame_rate=15/1` | low-copy NV12 RTSP stream; log reports `rtsp_dmabuf_output=on` |

This confirms that Route B can now be exercised with both reproducible fixed
video input and realtime USB camera input. The inference-side path uses MPP
DMA-backed frames, RGA preprocessing, bound RKNN input memory, and NPU
inference. For human-visible boxes, the output stage still intentionally
performs a display-side conversion/copy; for a cleaner performance-oriented
stream, `OUTPUT_MODE=dmabuf` avoids the boxed overlay.
