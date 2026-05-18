# rk_yolo_video

Standalone RK3588 validation tool for `YOLOv10 + local video file + output video`.

## What It Does

- loads an RKNN YOLOv10 model
- reads a local video with OpenCV
- runs frame-by-frame detection
- draws boxes on the original frames
- writes a new output video

This project is intentionally isolated from the Jetson-specific `encoder` pipeline so phase 1 can be brought up quickly on RK3588.

It now also includes a non-invasive encoder adapter layer for future integration work:

- `include/yolo_encoder_adapter.h`
- `src/yolo_encoder_adapter.cpp`

That adapter is not wired into the current runtime path yet. It exists so future `encoder` migration work can reuse the working RKNN detector without replacing the known-good validation flow first.

## Expected Inputs

- model: `../yolov10n.rknn` or `../../yolov10n.rknn`
- video: any file OpenCV can decode on the target board

The default model is the WSL-regenerated `yolov10n.rknn` in the workspace root. The previous model is kept as `../yolov10n.pre_wsl_backup.rknn` in case you want to compare.

For the newer single-class drone detector trained in this workspace, the first recommended board-side validation path is also `rk_yolo_video`, not the live RTSP tool. That keeps the validation surface small before adding camera, tracking, and streaming variables.

Recommended first-pass settings for the drone-specific model:

- model: `../training_runs/drone_gpu_50e/weights/best.rk3588.fp.rknn`
- score: `0.35`
- nms: `0.45`

That recommendation comes from the offline threshold sweep documented in:

```text
docs/superpowers/specs/2026-04-21-drone-model-error-analysis.md
```

## Build On RK3588

Install dependencies first:

- OpenCV development package
- RKNN runtime library and headers

If RKNN is not installed into a standard path, export `RKNN_API_PATH` to the runtime package root before configuring:

```bash
export RKNN_API_PATH=/path/to/rknpu2/runtime/Linux/librknn_api
```

Build:

```bash
mkdir -p build
cd build
cmake ..
make -j4
```

## Convert ONNX To RKNN

If you need to regenerate the RKNN model from `../yolov10n.onnx`, use:

```bash
python3 tools/convert_yolov10_to_rknn.py ../yolov10n.onnx --target rk3588 --dtype fp
```

For an INT8 model, provide a calibration dataset txt file:

```bash
python3 tools/convert_yolov10_to_rknn.py ../yolov10n.onnx --target rk3588 --dtype i8 --dataset ./coco_subset_20.txt
```

## Run

```bash
./rk_yolo_video <input_video> <output_video> [model_path] [score_thresh] [nms_thresh] [detections_csv] [roi_jsonl] [alarm_csv]
```

If `model_path` is omitted, the binary will try common local paths such as `../../yolov10n.rknn`.

Example:

```bash
./rk_yolo_video input.mp4 output.mp4 ../../yolov10n.rknn 0.30 0.45 output.csv output.roi.jsonl output.alarm.csv
```

Drone-model example:

```bash
./rk_yolo_video input.mp4 output.mp4 ../../training_runs/drone_gpu_50e/weights/best.rk3588.fp.rknn 0.35 0.45 output.csv output.roi.jsonl output.alarm.csv
```

## Software Alarm Overlay

`rk_yolo_video` includes a software alarm path for demonstrations without external relays or buzzers.
When at least one target is displayed, the output video shows a red `UAV ALERT` banner. When no target
is present, it shows a green `NORMAL` banner. Alarm transitions are also written to an alarm CSV file.

Environment variables:

- `RK_YOLO_ALARM_OVERLAY=1` enables the visual banner and is on by default.
- `RK_YOLO_ALARM_OVERLAY=0` disables only the banner while keeping detection output unchanged.
- `RK_YOLO_ALARM_HOLD_FRAMES=5` keeps the alarm active for a few missed frames to avoid flicker.
- `RK_YOLO_GPIO_VALUE_PATH=/tmp/rk_yolo_gpio_value` writes a GPIO-compatible alarm value on state
  changes. The file contains `1` for `alarm_on` and `0` for `alarm_off`; if unset, this external
  alarm interface is disabled.

Alarm CSV format:

```text
frame_index,event,active,detections,max_score
```

## Profiling And Zero-Copy Experiments

The default path is unchanged. Profiling and zero-copy input are controlled by environment variables and are disabled unless explicitly enabled.

Print per-frame stage timing to stdout:

```bash
RK_YOLO_PROFILE=1 ./rk_yolo_video input.mp4 output.mp4 ../../training_runs/drone_gpu_50e/weights/best.rk3588.fp.rknn 0.35 0.45 output.csv output.roi.jsonl
```

The profiling rows are emitted as `profile_csv` lines with fields matching the thesis stage analysis:

```text
frame,input_mode,prepare_ms,input_set_or_update_ms,rknn_run_ms,outputs_get_ms,decode_nms_ms,outputs_release_ms,render_ms,total_work_ms,detections
```

Compare the experimental zero-copy input path against the normal `rknn_inputs_set` path:

```bash
RK_YOLO_PROFILE=1 RK_YOLO_ZERO_COPY_INPUT=1 ./rk_yolo_video input.mp4 output_zero_copy.mp4 ../../training_runs/drone_gpu_50e/weights/best.rk3588.fp.rknn 0.35 0.45 zero_copy.csv zero_copy.roi.jsonl
```

If zero-copy setup fails, the tool prints the failure reason and falls back to the normal input path. Keep `RK_YOLO_ZERO_COPY_INPUT=0` for stable demonstrations unless a board-side comparison shows a benefit.

## Experimental DMA/RGA/RKNN Input Path

For low-copy input-path exploration, this folder also provides an isolated demo target:

```text
rk_yolo_dma_demo
```

Its intended data path is:

```text
V4L2 YUYV DMA fd -> RGA resize/color convert/letterbox -> RKNN input memory -> NPU
```

This target is separate from the stable `rk_yolo_video` and `rk_yolo_live_rtsp` flows. It is useful
for measuring whether camera DMA buffers can be passed through RGA into bound RKNN input memory
without using `rknn_inputs_set`.

Board-side reproducibility script:

```bash
bash scripts/run_dma_rga_rknn_eval.sh
```

Generate an additional annotated MP4 proof video:

```bash
WRITE_VIDEO=1 bash scripts/run_dma_rga_rknn_eval.sh
```

Common overrides:

```bash
DEVICE=/dev/video48 CONF=0.24 FRAMES=300 WRITE_VIDEO=1 bash scripts/run_dma_rga_rknn_eval.sh
```

Keep this as an experimental performance path until it is integrated with RTSP publishing and camera
control. The stable demonstration path remains `rk_yolo_live_rtsp`.

An RTSP-enabled experimental variant is also provided:

```text
rk_yolo_dma_rtsp_demo
```

Its intended inference path is:

```text
V4L2 YUYV DMA fd -> RGA resize/color convert/letterbox -> RKNN input memory -> NPU
```

and its visualization path is:

```text
YUYV mmap buffer -> BGR overlay -> GStreamer RTSP appsrc -> mpph264enc
```

This means the NPU input path avoids the normal `rknn_inputs_set` upload, while RTSP publishing still
uses a safe BGR appsrc copy for visual validation. It is an aggressive experimental path, not a
replacement for the stable `rk_yolo_live_rtsp` demonstration.

Build and run on the board:

```bash
bash scripts/run_dma_rtsp_eval.sh
```

Then open the stream on the PC:

```text
rtsp://<board-ip>:8561/yolo_dma
```

Common overrides:

```bash
DEVICE=/dev/video48 CONF=0.24 FPS=15 PORT=8561 MOUNT=/yolo_dma bash scripts/run_dma_rtsp_eval.sh
```

## Route B MPP/DMA/RGA/RKNN Validator

For the more aggressive zero-copy exploration, this folder also includes a headless
Route B validator:

```text
rk_yolo_mpp_dma_demo
```

Its intended inference path is:

```text
V4L2 H.264/MJPEG packet -> MPP hardware decode -> MppFrame DMA fd
  -> RGA resize/color convert/letterbox -> RKNN input memory -> NPU
```

This target is separate from the stable demonstration program. It is meant to verify
whether decoded hardware buffers can be handed from MPP to RGA and then into bound
RKNN input memory without the normal `rknn_inputs_set` upload.

Validated board smoke result:

```text
/dev/video48 H264 640x480@15fps
summary packets=469 decoded_frames=100 inferred_frames=100
avg_decode_ms=0.20 avg_prepare_ms=1.04 avg_run_ms=93.65 avg_total_ms=95.95
```

Build and run on the board:

```bash
bash scripts/run_mpp_dma_rknn_eval.sh
```

Common overrides:

```bash
CODEC=mjpg DEVICE=/dev/video48 CONF=0.24 FRAMES=300 bash scripts/run_mpp_dma_rknn_eval.sh
```

For public drone videos, use the file validator. The script extracts an Annex-B
H.264 elementary stream from an MP4 file with `ffmpeg`, then sends that stream to
MPP:

```bash
VIDEO=/path/to/public_drone_video.mp4 CONF=0.20 FRAMES=300 \
  bash scripts/run_mpp_file_rknn_eval.sh
```

To also generate an annotated proof video, set `OUT_VIDEO`:

```bash
VIDEO=/path/to/public_drone_video.mp4 CONF=0.20 FRAMES=300 \
  OUT_VIDEO=eval_runs/route_b_visual/public_drone_boxed.mp4 \
  bash scripts/run_mpp_file_rknn_eval.sh
```

Its inference-side route is:

```text
MP4 video -> H.264 elementary stream -> MPP decode -> MppFrame DMA fd
  -> RGA letterbox -> RKNN input memory -> NPU
```

The annotated-video writer uses one extra RGA DMA-to-BGR copy for drawing boxes.
The inference input still uses the low-copy Route B path and skips
`rknn_inputs_set`.

## Taskbook RGA / Zero-Copy Verification Pack

Use this one-click verification pack when you need to show that both fixed-video
input and camera input can exercise the RGA / low-copy RKNN-input path without
touching the stable thesis demonstration program:

```bash
bash scripts/run_rga_zero_copy_taskbook_eval.sh
```

The script writes a timestamped report under:

```text
eval_runs/rga_zero_copy_taskbook_<timestamp>/
  environment.txt
  fixed_video_mpp_rga_rknn.log
  camera_mpp_rga_rknn_rtsp.log
  report.md
```

Fixed-video validation:

```bash
RUN_FIXED=1 RUN_CAMERA=0 \
VIDEO=/home/ubuntu/public_videos/quadcopter_20200202_10s.mp4 \
WRITE_FIXED_VIDEO=1 CONF=0.24 FIXED_FRAMES=300 \
  bash scripts/run_rga_zero_copy_taskbook_eval.sh
```

Camera validation with boxed RTSP visualization:

```bash
RUN_FIXED=0 RUN_CAMERA=1 OUTPUT_MODE=bgr \
DEVICE=/dev/video48 WIDTH=640 HEIGHT=480 FPS=15 CODEC=h264 \
CONF=0.24 DETECT_EVERY_N=3 CAMERA_SECONDS=30 \
PORT=8562 MOUNT=/yolo_mpp \
  bash scripts/run_rga_zero_copy_taskbook_eval.sh
```

Camera validation with the cleaner low-copy RTSP performance stream:

```bash
RUN_FIXED=0 RUN_CAMERA=1 OUTPUT_MODE=dmabuf \
DEVICE=/dev/video48 WIDTH=640 HEIGHT=480 FPS=15 CODEC=h264 \
CONF=0.24 DETECT_EVERY_N=3 CAMERA_SECONDS=30 \
PORT=8563 MOUNT=/yolo_mpp_dma \
  bash scripts/run_rga_zero_copy_taskbook_eval.sh
```

Expected evidence in `report.md`:

- `zero_copy_input=on`: RKNN input memory was created and bound.
- `DMA fd -> RGA -> RKNN input mem path enabled: rknn_inputs_set skipped`:
  RGA wrote the DMA-backed source frame into the bound RKNN input memory.
- `summary ... avg_prepare_ms=... avg_run_ms=... avg_total_ms=...`:
  the run reached the normal timing summary.

The important boundary is unchanged: boxed MP4/RTSP output intentionally keeps a
visualization-side copy so boxes and labels can be drawn. For the cleanest
performance-path comparison, use `OUTPUT_MODE=dmabuf`; for demonstration, use
`OUTPUT_MODE=bgr`.

## Route-B Camera RTSP Launcher

For a quick live-camera check of the Route-B chain, use:

```bash
bash scripts/start_route_b_camera_rtsp.sh
```

Default input is `/dev/video48` at `1280x720@20fps` using the camera H.264
stream. The script keeps the experimental chain enabled:

```text
V4L2 compressed stream -> MPP decode -> MppFrame DMA fd
  -> RGA letterbox -> RKNN input memory -> NPU -> boxed RTSP output
```

Open the printed URL from the PC, for example:

```text
rtsp://<board-ip>:8564/yolo_routeb_cam
```

Useful overrides:

```bash
CONF=0.20 PORT=8568 MOUNT=/routeb_demo RUN_SECONDS=30 \
  bash scripts/start_route_b_camera_rtsp.sh
```

If VLC has trouble with the hardware RTSP encoder on a specific computer, run:

```bash
RK_YOLO_RTSP_ENCODER=x264 bash scripts/start_route_b_camera_rtsp.sh
```

This launcher is only for the Route-B exploration path and does not modify the
stable FP RKNN thesis demonstration flow.

## Production-Candidate One-Click Benchmark

For the most complete automated experiment in this project, use:

```bash
bash scripts/run_mpp_dma_rtsp_production_candidate.sh
```

This script does not touch the stable `rk_yolo_live_rtsp` demonstration path. It
creates a timestamped folder under `eval_runs/`, records board/camera/model
environment information, runs a four-mode RTSP matrix, runs an async DMA-pool
pressure test, and writes a `report.md` summary.

The evaluated chain is:

```text
UVC compressed stream -> V4L2 capture -> MPP hardware decode -> MppFrame DMA fd
  -> RGA resize/color convert/letterbox -> RKNN input memory -> NPU -> RTSP output
```

Common overrides:

```bash
MATRIX_SECONDS=18 STRESS_SECONDS=12 BASE_PORT=8600 ASYNC_POOL=3 \
  DEVICE=/dev/video48 CONF=0.24 FPS=15 CODEC=h264 \
  bash scripts/run_mpp_dma_rtsp_production_candidate.sh
```

Generated artifacts:

```text
eval_runs/production_candidate_<timestamp>/
  environment.txt
  matrix/summary.tsv
  async_pool_stress.log
  report.md
```

Interpretation guide:

- `direct-dmabuf`: cleanest low-copy performance candidate, but it outputs an
  NV12 performance stream without drawn boxes.
- `direct-bgr` / `async-bgr`: visualization candidates with drawn boxes; they
  intentionally add a BGR copy for overlay.
- `async + DMA pool`: latency-control experiment that replaces stale pending
  frames rather than accumulating delay.

This is the closest industrial-style chain currently implemented in the
project. It is still an experimental production candidate, not a replacement
for the stable thesis/demo flow until it passes longer camera validation.

## Optional RGA Preprocess Experiment

The stable default preprocessing path remains OpenCV. For board-side hardware-preprocess experiments,
`rk_yolo_video` can optionally use RK3588 RGA in two non-default modes while keeping letterbox
padding, RKNN input upload, and post-processing unchanged.

Enable the first-stage RGA resize path:

```bash
RK_YOLO_PROFILE=1 RK_YOLO_PREPROCESS=rga ./rk_yolo_video input.mp4 output_rga.mp4 ../../training_runs/drone_gpu_50e/weights/best.rk3588.fp.rknn 0.35 0.45 rga.csv rga.roi.jsonl
```

Enable the second-stage RGA color-convert plus resize experiment:

```bash
RK_YOLO_PROFILE=1 RK_YOLO_PREPROCESS=rga_cvt_resize ./rk_yolo_video input.mp4 output_rga_cvt_resize.mp4 ../../training_runs/drone_gpu_50e/weights/best.rk3588.fp.rknn 0.35 0.45 rga_cvt_resize.csv rga_cvt_resize.roi.jsonl
```

Enable the optional RGA letterbox experiment:

```bash
RK_YOLO_PROFILE=1 RK_YOLO_PREPROCESS=rga_cvt_resize RK_YOLO_RGA_LETTERBOX=1 ./rk_yolo_video input.mp4 output_rga_letterbox.mp4 ../../training_runs/drone_gpu_50e/weights/best.rk3588.fp.rknn 0.35 0.45 rga_letterbox.csv rga_letterbox.roi.jsonl
```

This path keeps the stable RKNN input upload and post-processing logic, but lets RGA write the
resized RGB image directly into the letterbox canvas. It is disabled by default and falls back to
the existing preprocessing path if RGA rejects the operation.

Compare against the stable OpenCV path:

```bash
RK_YOLO_PROFILE=1 RK_YOLO_PREPROCESS=opencv ./rk_yolo_video input.mp4 output_opencv.mp4 ../../training_runs/drone_gpu_50e/weights/best.rk3588.fp.rknn 0.35 0.45 opencv.csv opencv.roi.jsonl
```

The RGA paths require `librga-dev` on the board. If RGA is not available, or if a specific RGA
operation fails, the program prints a warning and falls back to the stable OpenCV preprocessing path.
This keeps demonstrations and existing validation runs compatible with the known-good baseline.

For task-book validation, use strict full-RGA mode so the run fails instead of silently falling back
to OpenCV if RGA is unavailable. This path asks RGA to handle color conversion, resize, and
letterbox canvas construction before RKNN NPU inference:

```bash
RK_YOLO_PROFILE=1 RK_YOLO_PIPELINE=1 RK_YOLO_PIPELINE_STAGED=1 RK_YOLO_PREPROCESS=rga_cvt_resize RK_YOLO_RGA_LETTERBOX=1 RK_YOLO_REQUIRE_RGA=1 ./rk_yolo_video input.mp4 output_taskbook_rga.mp4 ../../training_runs/drone_gpu_50e/weights/best.rk3588.fp.rknn 0.35 0.45 taskbook.csv taskbook.roi.jsonl taskbook.alarm.csv
```

The repository also provides a board-side reproducibility script for the task-book pipeline:

```bash
bash scripts/run_taskbook_pipeline_eval.sh /home/ubuntu/public_videos/anti_uav_fig1.mp4
```

This script runs `video capture -> strict full-RGA preprocessing -> NPU inference -> post-processing`
with profiling enabled and writes a `taskbook_pipeline_summary.csv` file beside the output video.

The CSV file records one line per detection:

```text
frame_index,class_id,class_name,score,left,top,width,height
```

The ROI JSONL sidecar writes one JSON object per frame using the same field names as the legacy `output_roi()` payload:

```json
{"pos":[{"prob":0.8123,"id":63,"x":120,"y":88,"w":214,"h":160}]}
```

## Safe Baseline

The current known-good baseline is recorded in:

```text
baselines/test_mp4_tuned_default.json
```

You can validate a generated CSV against that baseline with:

```bash
python3 tools/validate_detection_csv.py artifacts/test_rk_yolo_tuned_default.csv --baseline baselines/test_mp4_tuned_default.json
```

This is the guardrail for future migration work: keep the current `rk_yolo_video` path runnable, and build new RK3588 integration work in parallel instead of replacing the working path first.

## Notes

- The code assumes the RKNN output is a YOLO-style raw head such as `1x84x8400`.
- The current default threshold pair is `score=0.30` and `nms=0.45`, chosen from a quick on-board sweep against `test.mp4`.
- The new drone-specific model should start from `score=0.35` and `nms=0.45` during its first board-side validation pass.
- The ROI JSONL file is meant to reduce the gap between this standalone validator and the legacy encoder's object output path.
- If the shipped `yolov10n.rknn` produces obviously wrong detections, regenerate it from `../yolov10n.onnx` and retry.
- This phase uses OpenCV video I/O for simplicity. RGA resize is available as an optional experiment. For MPP/decode-side zero-copy exploration, use the isolated `rk_yolo_mpp_dma_demo` validator above.
