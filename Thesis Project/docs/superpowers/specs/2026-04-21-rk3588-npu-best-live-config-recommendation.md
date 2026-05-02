# RK3588 NPU Best Live Configuration Recommendation

Date: 2026-04-21

## Goal

This note narrows the current live RTSP anti-UAV system toward the most suitable configuration for practical real-time detection, rather than the highest raw throughput alone.

The recommendation is based on:

- first-round live comparison across baseline, multi-context, and policy-optimized modes
- a second-round policy sweep centered on `detect_every_n`
- the current live USB-camera scene available on the board

## Recommendation

The current recommended live configuration is still:

- single infer worker
- `detect_every_n=3`
- `track_mode=motion`
- `dynamic_roi=on`
- `box_smooth=off`
- camera tuning enabled for the current `HBS Camera`

In practical terms, this corresponds to the current `EXP-04L` family.

## Why This Is the Best Current Live Choice

### 1. It avoids the high latency of brute-force full-frame refresh

The first live results already showed:

- baseline full-frame inference: about `240.59 ms`
- dual-context full-frame inference: about `307.30 ms`
- policy-optimized inference: about `104.91 ms`

This means the system-level real-time experience improved more from policy optimization than from simply increasing raw NPU concurrency.

### 2. It remains the best balance inside the policy family

The second-round sweep compared:

- `detect_every_n=2`
- `detect_every_n=3`
- `detect_every_n=4`

Under the current scene, the main results were:

| Config | stream_fps | npu_fps | end_to_end_ms | CPU | Memory |
| --- | ---: | ---: | ---: | ---: | ---: |
| `N=2` | 10.02 | 6.68 | 175.20 | 43.0% | 132.37 MB |
| `N=3` | 10.91 | 4.96 | 107.18 | 37.1% | 127.36 MB |
| `N=4` | 9.54 | 3.82 | 95.54 | 33.5% | 127.97 MB |

This suggests:

- `N=2` keeps stronger refresh strength, but latency is still too high for the current real-time target
- `N=4` pushes latency slightly lower, but sacrifices stream smoothness and NPU refresh too much
- `N=3` remains the best middle point

## Working Interpretation

For the current system, the “best real-time configuration” should not be defined as:

- the highest `stream_fps`
- or the highest `npu_fps`
- or the lowest latency alone

Instead, it should be defined as the best compromise across:

- visible stream smoothness
- acceptable refresh strength
- low end-to-end latency
- moderate CPU and memory cost
- stable long-running behavior

Under that definition, `detect_every_n=3` is currently the strongest practical choice.

## Important Limitation

The current policy sweep was collected under a live no-target scene.

That means this recommendation is currently strongest for:

- system-level real-time tuning
- throughput-latency-resource trade-off analysis
- practical live-viewing configuration selection

It is **not yet sufficient** to finalize:

- target recall behavior
- target loss recovery quality
- moving-drone tracking consistency

Those still need a live target scene or a stronger recorded replay setup.

## Recommended Next Step

The next most valuable experiment is:

1. keep `detect_every_n=3` as the current real-time deployment default
2. move to a target-present scene
3. compare `N=2` and `N=3` specifically on:
   - target persistence
   - target loss recovery
   - visible lag
   - obvious false positives

That next step will tell us whether the current system recommendation also remains best when actual UAV targets are present.

## Supporting Files

- [policy_sweep_live_metrics.csv](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_live_rtsp/artifacts/experiments/policy_sweep_live_metrics.csv)
- [fig_policy_sweep_live_compare.svg](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_live_rtsp/artifacts/experiments/fig_policy_sweep_live_compare.svg)
- [2026-04-21-rk3588-npu-first-live-results-summary.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-21-rk3588-npu-first-live-results-summary.md)
