# RKNN Zero-Copy Input Experiment

Date: 2026-04-25

## Purpose

Evaluate whether RKNN zero-copy input memory can reduce the large `rknn_inputs_set` cost observed in stage-level profiling.

The experiment was implemented as an optional switch to avoid breaking the stable RTSP path.

## Implementation

Experimental switch:

```bash
RK_YOLO_ZERO_COPY_INPUT=1
```

When enabled:

- `rknn_create_mem` allocates RKNN-managed input memory during model load.
- `rknn_set_io_mem` binds that memory to the model input tensor.
- Each inference copies the prepared NHWC uint8 input into the pre-allocated tensor memory.
- The default `rknn_inputs_set` path remains unchanged when the switch is off.

Stable default:

```bash
RK_YOLO_ZERO_COPY_INPUT=0
```

## Test Condition

- Board: RK3588
- Input: `/home/ubuntu/public_videos/video01.mp4`
- Model: `best.end2end_false.op12.rk3588.fp.v220.rknn`
- Mode: `detect_every_n=1`, `infer_workers=1`
- Profiling: `RK_YOLO_PROFILE=1`

Raw logs:

```text
Thesis Project/eval_runs/public_videos/rk3588_board/zerocopy_20260425/
```

## Runtime Evidence

Startup:

```text
zero-copy input mem size=2457600 attr_size=1228800 stride_size=2457600
zero_copy_input=on
```

The model still produced detections during the test, so the experimental path was functionally usable.

## Result

Average values from the zero-copy test:

| Metric | Zero-copy input |
|---|---:|
| Stream FPS | 8.57 |
| NPU FPS | 8.57 |
| End-to-end latency | 267.19 ms |
| Total inference work | 118.93 ms |
| Prepare | 3.82 ms |
| Input update | 0.43 ms |
| `rknn_run` | 111.36 ms |
| Output get | 0.53 ms |
| Decode + NMS | 2.64 ms |

Earlier standard path baseline:

| Metric | Standard input path |
|---|---:|
| Stream FPS | 8.57 |
| NPU FPS | 8.57 |
| End-to-end latency | 255.52 ms |
| Total inference work | 114.48 ms |
| `rknn_inputs_set` | 62.01 ms |
| `rknn_run` | 45.82 ms |

## Interpretation

The zero-copy input path reduced the measured input update time from about `62 ms` to less than `1 ms`, but `rknn_run` increased from about `46 ms` to about `111 ms`.

This means the total input conversion or synchronization cost was not eliminated. It appears to move from `rknn_inputs_set` into `rknn_run`.

The likely reason is that the model input tensor is `float16`, while the current application still writes `uint8` input data with `pass_through=0`. RKNN therefore still performs format/type conversion internally.

## Conclusion

The current zero-copy experiment is useful as a research result, but it is not a performance improvement yet.

Keep the stable demonstration path on:

```text
RK_YOLO_ZERO_COPY_INPUT=0
```

Use this experiment in the thesis as evidence that naive zero-copy memory binding alone is insufficient when the runtime still performs input type conversion.

## Next Optimization Options

1. Export or quantize a model whose input type better matches the camera/preprocess output.
2. Investigate true pass-through input with a correctly formatted `float16` or quantized tensor.
3. Combine RGA preprocessing with an input buffer layout that matches RKNN's expected tensor format.
4. Continue using dual-context inference because it already improves throughput without relying on this experimental zero-copy path.
