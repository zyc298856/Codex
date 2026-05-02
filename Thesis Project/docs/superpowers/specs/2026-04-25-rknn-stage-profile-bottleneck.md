# RKNN Stage Profiling Bottleneck Analysis

Date: 2026-04-25

## Purpose

Identify the next performance bottleneck after the RK3588 dual-context RTSP experiment.

The previous experiment showed that dual-context inference improves every-frame detection throughput, while triple-context inference does not provide further gains. This profiling pass adds stage-level timing to determine whether the next bottleneck is CPU post-processing, drawing, RTSP output, RKNN input transfer, or NPU execution.

## Instrumentation

An optional profiling switch was added:

```bash
RK_YOLO_PROFILE=1
```

When enabled, the runtime log includes:

- `prepare_ms`: OpenCV resize/letterbox/color preparation
- `inputs_set_ms`: `rknn_inputs_set`
- `rknn_run_ms`: `rknn_run`
- `outputs_get_ms`: `rknn_outputs_get`
- `decode_ms`: YOLO output decode and NMS
- `outputs_release_ms`: `rknn_outputs_release`
- `infer_total_ms`: profiled inference total
- `render_ms`: box smoothing and drawing

The profiling switch is disabled by default, so normal demonstration logs remain unchanged.

## Fixed Conditions

- Board: RK3588
- Input: `/home/ubuntu/public_videos/video01.mp4`
- Model: `best.end2end_false.op12.rk3588.fp.v220.rknn`
- Resolution: `640x480`
- Score threshold: `0.35`
- NMS threshold: `0.45`
- Box smoothing: enabled
- RTSP client: `gst-launch-1.0`
- Test duration: about 55 seconds per configuration

Raw logs were saved under:

```text
Thesis Project/eval_runs/public_videos/rk3588_board/profile_20260425/
```

## Profiled Configurations

| ID | detect_every_n | infer workers | Meaning |
|---|---:|---:|---|
| B | 1 | 1 | Every-frame single-context baseline |
| C | 1 | 2 | Every-frame dual-context optimized path |

## Stage Timing Result

Average values after startup:

| Stage | B: N=1, W=1 | C: N=1, W=2 |
|---|---:|---:|
| Stream FPS | 8.57 | 10.53 |
| NPU FPS | 8.57 | 10.53 |
| End-to-end latency | 255.52 ms | 179.52 ms |
| Total inference work | 114.48 ms | 113.96 ms |
| Prepare | 3.62 ms | 1.42 ms |
| `rknn_inputs_set` | 62.01 ms | 63.78 ms |
| `rknn_run` | 45.82 ms | 45.72 ms |
| `rknn_outputs_get` | 0.49 ms | 0.54 ms |
| Decode + NMS | 2.40 ms | 2.48 ms |
| Draw/render | 0.15 ms | 0.13 ms |

## Key Findings

1. CPU post-processing is not the main bottleneck.

Decode and NMS take only about `2.4 ms`, and drawing takes about `0.1-0.2 ms`. Optimizing box drawing or label rendering will not meaningfully improve overall throughput.

2. RTSP rendering is not the dominant bottleneck in the measured stage timings.

The measured render stage is tiny, and the dual-context path can publish around `10.5 FPS` without capture drops. RTSP output still matters for end-to-end latency, but it is not the largest per-frame compute cost.

3. `rknn_inputs_set` is the largest single stage.

`rknn_inputs_set` takes about `62-64 ms`, which is larger than `rknn_run` itself. This indicates that input transfer, format conversion, memory copy, or synchronization before NPU execution is a major bottleneck.

4. `rknn_run` is the second-largest stage.

`rknn_run` takes about `45-46 ms`. This is expected because it represents the actual model execution on NPU.

5. Dual context improves pipeline overlap, not individual inference speed.

The per-inference `infer_total_ms` remains about `114 ms` in both single-context and dual-context runs. The throughput improvement comes from overlapping multiple inference contexts, reducing queueing and capture drops.

## Current Bottleneck Ranking

1. RKNN input set / input transfer: about `63 ms`
2. RKNN model execution: about `46 ms`
3. Queueing and scheduling latency: visible in end-to-end delay
4. Decode/NMS: about `2.4 ms`
5. Rendering/drawing: about `0.1-0.2 ms`

## Recommended Next Optimization Direction

The next hardware-oriented optimization should focus on reducing input transfer overhead:

- Investigate RKNN zero-copy input memory with `rknn_create_mem` and `rknn_set_io_mem`
- Evaluate whether pre-allocated reusable input buffers reduce allocation/copy overhead
- Explore RGA-based resize/color conversion and direct input buffer layout
- Keep dual-context inference as the best multi-context setting; do not spend more time on three contexts unless the input path is improved first

## Thesis-Friendly Conclusion

Stage-level profiling shows that the current RK3588 pipeline is not limited by YOLO post-processing or drawing. The dominant cost is input transfer into the RKNN runtime, followed by NPU model execution. Dual-context inference improves throughput by overlapping inference jobs, but it does not reduce the per-inference cost. Therefore, further hardware optimization should prioritize input memory handling and hardware-assisted preprocessing rather than CPU-side box rendering.

## Current Stable Demo Configuration

After profiling, the live service was restored to:

```text
detect_every_n=2
infer_workers=1
box_smooth=on
dynamic_roi=on
RK_YOLO_PROFILE=0
```

RTSP URL:

```text
rtsp://192.168.2.156:8554/drone
```
