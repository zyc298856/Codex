# DMA/RGA/RKNN Experimental Path Design

## Goal

Build a side-path experiment for the RK3588 demo that explores a lower-copy video input pipeline:

`V4L2 DMA buffer -> RGA resize/color-convert/letterbox -> RKNN input memory -> NPU`

The existing stable programs remain untouched as the default demonstration path. This experiment is intended for performance exploration and thesis/defense discussion, not as an immediate replacement for the proven OpenCV-based demo.

## Non-goals

- Do not change the default behavior of `rk_yolo_video` or `rk_yolo_live_rtsp`.
- Do not remove or weaken the existing OpenCV, RGA, zero-copy, RTSP, and fixed-video paths.
- Do not claim a full MPP-to-NPU zero-copy pipeline unless board-side profiling proves it.

## Phase 1: V4L2 DMA Camera Path

The first implementation targets the USB camera because it is the primary live demonstration input. The tool requests `YUYV` frames from V4L2, exports the captured MMAP buffers as DMA buffer file descriptors with `VIDIOC_EXPBUF`, and passes those file descriptors to RGA.

RGA writes the resized RGB letterbox result into the RKNN input tensor memory created by `rknn_create_mem` and bound with `rknn_set_io_mem`. After RGA completes synchronously, RKNN runs inference directly from the bound input memory.

The experiment is exposed as a separate binary:

`rk_yolo_dma_demo`

Example:

```bash
RK_YOLO_ZERO_COPY_INPUT=1 ./rk_yolo_dma_demo /dev/video48 model.rknn 640 480 15 0.24 0.45 300
```

## Expected Data Flow

1. Open V4L2 camera device.
2. Set `YUYV 640x480` and target FPS.
3. Request MMAP capture buffers.
4. Export each buffer to DMA buffer fd.
5. Dequeue one captured buffer.
6. Fill RKNN input memory background with letterbox value `114`.
7. Use RGA `improcess` from source DMA fd to RKNN input memory fd.
8. Run RKNN inference without `rknn_inputs_set`.
9. Decode outputs with the existing YOLO post-processing code.
10. Requeue the camera buffer.

## Profiling

The tool prints per-run averages:

- capture/dequeue time
- RGA preprocess time
- RKNN run time
- output/decode time
- total frame time
- detection frame count

This lets us compare against the existing OpenCV path without mixing in RTSP display overhead.

## Safety

The experiment fails explicitly if DMA buffer export, RGA fd wrapping, or RKNN input memory binding fails. It should not silently fall back to OpenCV, because this path is meant to answer whether the hardware-memory pipeline works.

The stable demo remains:

- `rk_yolo_live_rtsp` for real-time display
- `rk_yolo_video` for fixed public-video input

## Board Smoke Test

Board-side smoke testing was completed on the RK3588 with the USB camera exposed as `/dev/video48`.

Command shape:

```bash
./build_dma_experiment/rk_yolo_dma_demo \
  best.end2end_false.op12.rk3588.fp.v232.hard_v1.rknn \
  /dev/video48 640 480 30 0.24 0.45 30
```

Observed key log lines for the first 30-frame smoke test:

```text
camera opened: /dev/video48 640x480 YUYV buffers=4
rga_api version 1.10.1_[10]
DMA fd -> RGA -> RKNN input mem path enabled: rknn_inputs_set skipped
summary frames=30 detected_frames=0 total_detections=0 wall_fps=9.65879
avg_prepare_ms=1.24882 avg_run_ms=94.422 avg_total_ms=100.607
```

Interpretation:

- The Phase 1 hardware-memory bridge is validated: `V4L2 YUYV DMA fd -> RGA -> RKNN input memory -> NPU`.
- The path skips `rknn_inputs_set`, so it is suitable for comparing input-copy overhead against the existing OpenCV path.
- No detections were expected in this smoke test because the camera was not pointed at a drone target.
- This is still an experimental performance probe. It does not yet replace the stable RTSP demonstration program.

A longer 300-frame test was then run with the same camera and model:

```text
summary frames=300 detected_frames=57 total_detections=57 wall_fps=10.2663
avg_prepare_ms=1.03828 avg_run_ms=91.2426 avg_total_ms=96.8745
```

For comparison, the existing `rk_yolo_live_rtsp` OpenCV input path was run for a short profiling window with a local RTSP client connected. Representative logs showed:

```text
preprocess=opencv
zero_copy_input=off
prepare_ms=7.06 inputs_set_ms=76.66 rknn_run_ms=49.10 infer_total_ms=138.11
```

The comparison suggests that the DMA/RGA/RKNN path is meaningful for input-path exploration because it removes the large `rknn_inputs_set` stage. However, it is not yet a drop-in replacement for the stable demonstration path because it currently lacks rendering, RTSP publishing, and the existing camera-control/display logic.

An additional visual-output validation was added to avoid judging the path only from terminal logs. The demo can now write an annotated MP4 after inference while keeping the inference input path as `V4L2 DMA fd -> RGA -> RKNN input memory -> NPU`.

```text
summary frames=120 detected_frames=40 total_detections=53 wall_fps=6.87784
avg_prepare_ms=1.00031 avg_run_ms=97.5594 avg_total_ms=103.657
wrote output_video=/home/ubuntu/dma_rga_rknn_eval_20260514/dma_visual_120.mp4
```

The lower wall FPS in the visual-output run is expected because video writing is enabled for evidence capture. It should not be used as the pure input-path performance number. The 300-frame no-output run remains the cleaner timing comparison.

The reproducibility script was also verified from the home network on `ubuntu@192.168.2.156`:

```bash
FRAMES=30 OUT_DIR=/home/ubuntu/dma_rga_rknn_eval_script_smoke \
  bash scripts/run_dma_rga_rknn_eval.sh
```

Observed summary:

```text
summary frames=30 detected_frames=4 total_detections=4 wall_fps=9.61374
avg_prepare_ms=1.15179 avg_run_ms=93.9698 avg_total_ms=100.005
```

## Phase 1.5: RTSP-Enabled Aggressive Camera Path

The next safe integration step keeps the stable `rk_yolo_live_rtsp` program unchanged and adds a separate experimental target:

```text
rk_yolo_dma_rtsp_demo
```

Inference path:

```text
V4L2 YUYV DMA fd -> RGA resize/color convert/letterbox -> RKNN input memory -> NPU
```

Visualization path:

```text
YUYV mmap buffer -> BGR overlay -> GStreamer RTSP appsrc -> mpph264enc
```

This is closer to the task-book hardware pipeline because the detector input avoids the normal CPU memory upload through `rknn_inputs_set`. The RTSP publishing branch still uses a safe BGR appsrc copy so that the result can be viewed reliably from the PC. This limitation should be stated clearly: Phase 1.5 validates the aggressive NPU input path with live RTSP display, but it is not yet a full industrial zero-copy display pipeline.

Board-side command:

```bash
bash scripts/run_dma_rtsp_eval.sh
```

PC viewing URL:

```text
rtsp://<board-ip>:8561/yolo_dma
```

Home-network smoke validation on `ubuntu@192.168.2.156` passed. A local RTSP client on the board was able to probe the stream:

```text
codec_name=h264
width=640
height=480
r_frame_rate=15/1
```

Representative demo log lines:

```text
aggressive experimental path enabled
path=V4L2 YUYV DMA fd -> RGA letterbox -> RKNN input memory -> NPU
display path=YUYV mmap -> BGR overlay -> RTSP appsrc
rtsp path=rtsp://<board-ip>:8561/yolo_dma
DMA fd -> RGA -> RKNN input mem path enabled: rknn_inputs_set skipped
summary frames=45 detected_frames=28 total_detections=42 wall_fps=2.54177
avg_prepare_ms=1.3135 avg_run_ms=102.524 avg_total_ms=109.228
```

The wall FPS in this short run includes the period waiting for an RTSP client, so it should not be used as the clean throughput metric. The important validation points are that the H.264 RTSP stream is available, the RGA/RKNN input-memory path is active, and detection results are produced while the stream is being published.

## Phase 2: MPP Decode Path

After the V4L2 DMA path is validated, a second experiment can add:

`H.264/H.265 input -> MPP decode buffer -> RGA -> RKNN input memory`

This phase is more complex because it must handle encoded stream parsing, decoder buffer lifetime, timestamps, and possibly NV12/NV16 source formats. It should be implemented only after the Phase 1 camera path confirms the RKNN input memory bridge is reliable.
