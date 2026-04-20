# RK3588 YOLOv10 Video Validation Design

## Goal

Build the fastest possible first-phase RK3588 validation path for the existing drone recognition project:

- input: local video file
- model: YOLOv10 on RK3588 NPU
- output: a new video file with detection boxes drawn on frames

This phase explicitly does not migrate the Jetson encoder, RTSP, TensorRT, CUDA, or UNet pipeline.

## Why This Shape

The current project is tightly coupled to Jetson components such as TensorRT, CUDA, `NvVideoDecoder`, `NvVideoEncoder`, and `NvBufSurface`. Replacing those pieces inside the existing `encoder` pipeline would slow down first bring-up on RK3588.

To minimize risk and time-to-first-result, phase 1 uses an isolated C++ validation program that only depends on:

- RKNN runtime
- OpenCV video I/O

## Phase 1 Scope

Create a standalone `rk_yolo_video` project that:

1. reads frames from a local video file
2. letterboxes frames to the model input size
3. runs RKNN inference with `yolov10n.rknn`
4. decodes the `1x84x8400` detection head
5. applies confidence filtering and NMS
6. maps boxes back to original frame coordinates
7. writes an output video with boxes

## Model Strategy

Primary model:

- `yolov10n.rknn`

Fallback model source if runtime results are clearly wrong:

- `yolov10n.onnx`

The `.pt` model is kept only as the upstream source if a fresh ONNX export becomes necessary.

## Output Assumption

The added RKNN model reports an output layout equivalent to YOLO-style raw predictions instead of already-decoded `[x1, y1, x2, y2, score, class]` rows.

Phase 1 therefore includes custom post-processing in C++:

- select the best class score per candidate
- decode boxes from `xywh`
- remap boxes from letterboxed coordinates back to the source frame
- run class-aware NMS

## Deliverables

- standalone C++ source under `rk_yolo_video/`
- CMake-based build entry
- ONNX-to-RKNN conversion helper script
- run instructions for RK3588

## Non-Goals

- replace Jetson `encoder` directly
- support RTSP in phase 1
- optimize with RGA or MPP before functional validation
- integrate UNet in phase 1

## Phase 2 Direction

After the standalone program is validated on RK3588, reuse the detector interface and post-processing code to replace the Jetson YOLO module inside the larger pipeline.
