# RK YOLO Profile and Zero-copy Validation

## Context

The RK3588 board was connected from the user's grandmother's home network on 2026-04-28.

- SSH target: `ubuntu@192.168.10.186`
- Board project path: `/home/ubuntu/eclipse-workspace/eclipse-workspace`
- Validated program: `rk_yolo_video`
- Input video: `/home/ubuntu/public_videos/anti_uav_fig2.mp4`
- Stable RKNN model: `/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn`

## Implementation Status

Claude's earlier concern about missing technical implementation is now addressed on the board:

- `RK_YOLO_PROFILE` is supported by `rk_yolo_video/src/main.cpp`.
- Profiling rows are printed as `profile_csv`.
- `RK_YOLO_ZERO_COPY_INPUT` is supported by `rk_yolo_video/src/yolo_rknn.cpp`.
- The optional zero-copy path uses `rknn_create_mem` and `rknn_set_io_mem`.
- The default path remains `RK_YOLO_ZERO_COPY_INPUT=0`.

## Board Validation

The latest local `rk_yolo_video` source was synchronized to the RK3588 board and rebuilt successfully:

```bash
cd /home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_video
mkdir -p build
cd build
cmake ..
cmake --build . -j2
```

The executable was rebuilt successfully and printed the expected usage message.

## Profiling Results

Both tests used the same video, model, score threshold and NMS threshold. The first five frames were excluded from the average to reduce warm-up effects.

| Mode | Frames | Input update mean | RKNN run mean | Decode/NMS mean | Render mean | Total work mean |
|---|---:|---:|---:|---:|---:|---:|
| Standard `rknn_inputs_set` | 160 | 31.50 ms | 48.30 ms | 3.27 ms | 51.99 ms | 140.06 ms |
| Zero-copy input | 160 | 0.35 ms | 80.04 ms | 3.29 ms | 52.40 ms | 141.19 ms |

## Interpretation

The zero-copy switch is functional: the explicit input update stage drops from about 31.50 ms to 0.35 ms. However, `rknn_run` increases from about 48.30 ms to 80.04 ms, so the total work time does not improve. This supports the thesis conclusion that zero-copy is implemented and testable, but it should remain an experimental path rather than the current stable optimization route.

The likely reason is that the input conversion or synchronization cost is shifted from `rknn_inputs_set` into `rknn_run`, especially because the current FP RKNN model reports a float16 input tensor while the application-side image buffer is prepared as uint8 NHWC.

## Additional Finding

Models with direct output shape `[1, 300, 6]` still segfault in the fixed-video path, while the stable end2end-false model with output shape `[1, 5, 8400]` runs successfully. The current stable project route should therefore continue to use:

```text
best.end2end_false.op12.rk3588.fp.v220.rknn
```

This also matches the thesis wording that the restored stable route is based on the end2end-false RKNN model.
