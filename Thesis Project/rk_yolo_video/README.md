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

Compare against the stable OpenCV path:

```bash
RK_YOLO_PROFILE=1 RK_YOLO_PREPROCESS=opencv ./rk_yolo_video input.mp4 output_opencv.mp4 ../../training_runs/drone_gpu_50e/weights/best.rk3588.fp.rknn 0.35 0.45 opencv.csv opencv.roi.jsonl
```

The RGA paths require `librga-dev` on the board. If RGA is not available, or if a specific RGA
operation fails, the program prints a warning and falls back to the stable OpenCV preprocessing path.
This keeps demonstrations and existing validation runs compatible with the known-good baseline.

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
- This phase uses OpenCV video I/O for simplicity. RGA resize is available as an optional experiment, while MPP/decode-side zero-copy remains future work.
