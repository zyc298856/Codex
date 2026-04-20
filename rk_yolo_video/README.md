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
./rk_yolo_video <input_video> <output_video> [model_path] [score_thresh] [nms_thresh] [detections_csv] [roi_jsonl]
```

If `model_path` is omitted, the binary will try common local paths such as `../../yolov10n.rknn`.

Example:

```bash
./rk_yolo_video input.mp4 output.mp4 ../../yolov10n.rknn 0.30 0.45 output.csv output.roi.jsonl
```

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
- The ROI JSONL file is meant to reduce the gap between this standalone validator and the legacy encoder's object output path.
- If the shipped `yolov10n.rknn` produces obviously wrong detections, regenerate it from `../yolov10n.onnx` and retry.
- This phase uses OpenCV video I/O for simplicity. MPP and RGA are intentionally deferred until functional validation succeeds.
