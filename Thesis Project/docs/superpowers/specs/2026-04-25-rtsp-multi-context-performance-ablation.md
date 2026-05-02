# RTSP Multi-Context NPU Performance Ablation

Date: 2026-04-25

## Purpose

Evaluate whether RK3588 multi-context NPU inference improves the real-time RTSP detection pipeline.

This experiment focuses on the thesis supervisor's preferred direction: hardware-oriented multi-thread and multi-context optimization on RK3588.

## Fixed Conditions

- Board: RK3588
- Input: `/home/ubuntu/public_videos/video01.mp4`
- Model: `best.end2end_false.op12.rk3588.fp.v220.rknn`
- Model output layout: `[1, 5, 8400]`
- Class mode: single-class `drone`
- RTSP output: `rtsp://192.168.2.156:8554/drone`
- Resolution: `640x480`
- Target FPS: `15`
- Score threshold: `0.35`
- NMS threshold: `0.45`
- Box smoothing: enabled
- Smooth alpha: `0.35`
- RTSP client: `gst-launch-1.0` over TCP, `fakesink`
- Test duration: about 70 seconds per configuration

Raw logs were saved under:

```text
Thesis Project/eval_runs/public_videos/rk3588_board/multictx_20260425/
```

## Tested Configurations

| ID | detect_every_n | infer workers | Mode |
|---|---:|---:|---|
| A | 2 | 1 | Recommended viewing baseline with frame reuse |
| B | 1 | 1 | Every-frame single-context inference |
| C | 1 | 2 | Every-frame dual-context inference |
| D | 1 | 3 | Every-frame triple-context inference |

## Quantitative Result

Values below are averaged from runtime logs after startup.

| ID | Avg stream FPS | Avg NPU FPS | Avg latency ms | Avg work ms | Avg CPU % | Avg RSS MB | Drops |
|---|---:|---:|---:|---:|---:|---:|---|
| A: N=2, W=1 | 10.37 | 5.18 | 109.06 | 108.52 | 121.5 | 205.5 | 0 capture / 0 publish |
| B: N=1, W=1 | 8.59 | 8.59 | 268.89 | 120.67 | 144.7 | 206.3 | 116 capture / 0 publish |
| C: N=1, W=2 | 10.63 | 10.64 | 173.58 | 108.46 | 143.4 | 233.7 | 0 capture / 0 publish |
| D: N=1, W=3 | 10.45 | 10.45 | 179.59 | 118.30 | 147.7 | 239.0 | 0 capture / 0 publish |

## Key Findings

1. Single-context every-frame inference is not suitable as the real-time default.

`N=1, W=1` increases NPU usage, but the RTSP stream drops from about `10.37 FPS` to `8.59 FPS`, and average end-to-end latency rises to about `268.89 ms`. The capture queue also drops frames, which means the pipeline cannot keep up with the input rate.

2. Dual-context inference significantly improves every-frame detection.

`N=1, W=2` increases the measured NPU throughput to about `10.64 FPS`, eliminates capture drops, and reduces latency from about `268.89 ms` to `173.58 ms` compared with `N=1, W=1`.

3. Triple-context inference does not improve this workload.

`N=1, W=3` is slightly worse than dual context in both stream FPS and latency. It also consumes more memory. This suggests that the current pipeline is no longer limited only by NPU context count after two contexts; scheduling, post-processing, RTSP output, and memory traffic likely become the bottlenecks.

4. The best viewing configuration is still the policy-based `N=2` mode.

`N=2, W=1` has the lowest average latency among the tested configurations while preserving good visual stability through box smoothing. It remains the best practical setting for demonstration.

## Current Recommendation

Use two different recommendations depending on the experimental goal.

For live demonstration:

```text
detect_every_n=2
infer_workers=1
box_smooth=on
dynamic_roi=on
```

For thesis hardware optimization discussion:

```text
detect_every_n=1
infer_workers=2
multi-context full-frame inference
```

This gives a clean thesis conclusion: multi-context inference is effective when the objective is every-frame detection throughput, but the policy-based mode is better for low-latency visual monitoring.

## Thesis-Friendly Conclusion

The RK3588 multi-context NPU experiment shows that increasing the number of inference contexts from one to two improves every-frame detection throughput from `8.59 FPS` to `10.64 FPS` and reduces average end-to-end latency from `268.89 ms` to `173.58 ms`. However, adding a third context does not provide further benefit, indicating that the system reaches a new bottleneck outside pure NPU execution.

Therefore, the system should use dual-context inference for hardware parallelism experiments, while retaining the `detect_every_n=2` policy-based configuration for stable low-latency RTSP demonstration.

## Remaining Work

- Repeat the multi-context comparison with the USB camera input.
- Add a longer 5 to 10 minute stability test for `N=1, W=2`.
- Profile CPU-side post-processing and RTSP publishing to identify the next bottleneck after dual-context inference.
- Consider separating post-processing from inference workers if the dual-context path becomes the main research branch.
