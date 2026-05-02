# RK3588 NPU First Live Results Summary

Date: 2026-04-21

## Scope

This note consolidates the first live USB-camera RTSP results for the current anti-UAV pipeline on RK3588.

It is intentionally narrower than the full experiment matrix.
The purpose is to capture the first stable quantitative conclusion before the next round of experiments begins.

## Compared Runs

The current live comparison includes:

- `EXP-01L` baseline full-frame inference
- `EXP-03L` dual-context full-frame inference
- `EXP-04L` policy-optimized live configuration

`EXP-02L` is not treated as a separate quantitative result at this stage because the current implementation already runs the capture-infer-publish pipeline by default, so there is no clean live command path that isolates a distinct non-pipeline variant.

## Quantitative Snapshot

| Experiment ID | stream_fps | npu_fps | Avg Inference / Work (ms) | Avg End-to-End Latency (ms) | Dropped Capture | Main Reading |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `EXP-01L` | 6.56 | 6.56 | 152.49 | 240.59 | 80 | baseline reference line |
| `EXP-03L` | 13.98 | 13.98 | 180.16 | 307.30 | 0 | strongest throughput |
| `EXP-04L` | 10.55 | 5.04 | 0.00* | 104.91 | 1 | best practical latency |

## Main Result

The first live round already shows a clear throughput-latency trade-off on RK3588:

- `EXP-03L` almost doubles throughput relative to the baseline, but its latency rises from about `240.59 ms` to about `307.30 ms`
- `EXP-04L` does not maximize raw NPU throughput, but reduces end-to-end latency to about `104.91 ms`, making it the best practical live-viewing configuration among the current tested options

This result is strong enough to support an initial thesis-style statement:

- multi-context inference is the strongest path when throughput is the main goal
- policy optimization is the stronger path when practical live responsiveness matters more

## Interpretation for Thesis Narrative

The current data supports a clean first-round narrative:

1. The baseline establishes the live reference point for full-frame single-worker inference.
2. Dual-context inference demonstrates that RK3588 NPU parallelism can substantially improve throughput.
3. Strategy-level optimization shows that better real-time experience does not always come from brute-force concurrency.
4. Therefore, the anti-UAV RTSP system is better described as a throughput-latency balancing problem rather than a single-metric optimization problem.

## Current Best Candidate for Live Demonstration

For practical live camera demonstration, the current best candidate remains:

- `EXP-04L`
- `detect_every_n=3`
- `RK_YOLO_INFER_WORKERS=1`
- `track_mode=motion`
- `dynamic_roi=on`

This configuration currently offers the best compromise between:

- visible real-time smoothness
- acceptable detection persistence
- low end-to-end latency

## Supporting Files

- metrics table:
  [first_round_live_metrics.csv](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_live_rtsp/artifacts/experiments/first_round_live_metrics.csv)
- performance figure:
  [fig_first_round_live_perf_compare.svg](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_live_rtsp/artifacts/experiments/fig_first_round_live_perf_compare.svg)
- policy sweep:
  [2026-04-21-rk3588-npu-best-live-config-recommendation.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-21-rk3588-npu-best-live-config-recommendation.md)
- live comparison matrix:
  [2026-04-21-rk3588-npu-first-round-experiment-matrix.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-21-rk3588-npu-first-round-experiment-matrix.md)

## Notes

- `*` In `EXP-04L`, the sampled log line corresponded to a reuse frame, so `0.00 ms` reflects reuse cost rather than a real NPU inference step.
- The present summary should be treated as the first stable live snapshot, not the final full experiment chapter.
