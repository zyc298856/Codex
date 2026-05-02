# RK3588 NPU First-Round Experiment Matrix

Date: 2026-04-21

## Purpose

This draft turns the experiment template into a concrete first-round matrix for the current anti-UAV RTSP system.

The goal of this round is not to exhaust every variable.
It is to establish a clean and reproducible first comparison across:

- `Baseline`
- `Pipeline`
- `Multi-Context`
- `Policy-Optimized`

The preferred evaluation strategy is:

- primary comparison on a fixed recorded input
- supplementary validation on live USB camera input

## Shared Conditions

Unless explicitly changed, keep these conditions fixed across the first round:

- model: current stable RTSP model in use on RK3588
- output: RTSP stream
- input resolution: `640x480`
- target FPS setting: `15`
- NMS threshold: `0.45`
- camera tuning: enabled when using the `HBS Camera`
- queue policy: bounded queue with old-frame drop

For the first recorded-input comparison, try to keep:

- the same source video
- the same run duration
- the same lighting and scene category

## Sheet A Draft: Experiment Configuration

| Experiment ID | Group | Input Source | Input Name | Resolution | Model Name | Score Threshold | NMS Threshold | detect_every_n | Infer Workers | Track Mode | Dynamic ROI | Camera Tune | Test Duration | Main Goal | Notes |
| --- | --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | --- | --- | --- | --- | --- | --- |
| EXP-01 | Baseline | fixed video | `test.mp4` or equivalent replay input | 640x480 | current stable RTSP RKNN model | 0.30 | 0.45 | 1 | 1 | off | off | off | 60 s | reference line | keep structure as simple as possible |
| EXP-02 | Pipeline | fixed video | same as EXP-01 | 640x480 | same as EXP-01 | 0.30 | 0.45 | 1 | 1 | off | off | off | 60 s | isolate pipeline threading effect | capture -> infer -> publish |
| EXP-03 | Multi-Context | fixed video | same as EXP-01 | 640x480 | same as EXP-01 | 0.30 | 0.45 | 1 | 2 | off | off | off | 60 s | measure dual-context throughput gain | expect higher latency risk |
| EXP-04 | Policy-Optimized | fixed video | same as EXP-01 | 640x480 | same as EXP-01 | 0.30 | 0.45 | 3 | 1 | motion | on | off | 60 s | optimize real-time smoothness | current strongest viewing-oriented policy set |

### Supplementary Live Validation

After the fixed-input runs are done, repeat the same four configurations under live USB camera input.

Suggested IDs:

- `EXP-01L`
- `EXP-02L`
- `EXP-03L`
- `EXP-04L`

Recommended differences:

- input source: live USB camera
- input name: actual `/dev/video*` or camera model
- camera tune: `on`
- test duration: `120 s`

## Sheet B Draft: Performance Result Table

Fill this after each run.

| Experiment ID | stream_fps | npu_fps | Avg Inference Time (ms) | Avg Work Time (ms) | Avg End-to-End Latency (ms) | Max End-to-End Latency (ms) | Dropped Capture | Queue Overflow | CPU Usage (%) | Memory Usage (MB) | Runtime Stability | Quick Note |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| EXP-01 |  |  |  |  |  |  |  |  |  |  |  |  |
| EXP-02 |  |  |  |  |  |  |  |  |  |  |  |  |
| EXP-03 |  |  |  |  |  |  |  |  |  |  |  |  |
| EXP-04 |  |  |  |  |  |  |  |  |  |  |  |  |
| EXP-01L | 6.56 | 6.56 | 152.49 | 152.49 | 240.59 | 403.57* | 80 | 0 | 51.5** | 125.78** | stable | baseline full-frame inference; note one later reconnect spike in the same log |
| EXP-02L | - | - | - | - | - | - | - | - |  |  | skipped | current implementation already runs the capture -> infer -> publish pipeline by default; no separate non-pipeline live command exists yet |
| EXP-03L | 13.98 | 13.98 | 180.16 | 180.16 | 307.30 | 307.30 | 0 | 0 | 63.3** | 163.23** | stable | dual-context full-frame inference; throughput nearly doubled, latency increased |
| EXP-04L | 10.55 | 5.04 | 0.00** | 0.00** | 104.91 | 121.95 | 1 | 0 | 41.4** | 126.98** | stable | policy-optimized mode; best practical latency among current live runs |

## Sheet C Draft: Detection Quality Observation Table

Fill this after viewing the result stream, saved clips, or screenshots.

| Experiment ID | Test Scene | Obvious Correct Detections | Obvious False Positives | Obvious False Negatives | Duplicate Boxes | Box Jitter | Recovery After Loss | Real-Time Experience | Representative Issue | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| EXP-01 |  |  |  |  |  |  |  |  |  |  |
| EXP-02 |  |  |  |  |  |  |  |  |  |  |
| EXP-03 |  |  |  |  |  |  |  |  |  |  |
| EXP-04 |  |  |  |  |  |  |  |  |  |  |
| EXP-01L | live USB camera, RTSP probe connected | current scene has no drone target | no obvious false positives in sampled frame | current scene has no positive target | none observed in sampled frame | low, but stream is visibly less smooth | not assessable in current scene | acceptable | full-frame inference limits throughput | use as stability / false-positive reference under a no-target scene |
| EXP-02L | live USB camera, RTSP probe connected | - | - | - | - | - | - | - | no distinct live command path yet | treat as a methodological placeholder for now |
| EXP-03L | live USB camera, RTSP probe connected | current scene has no drone target | no obvious false positives in sampled frame | current scene has no positive target | none observed in sampled frame | low jitter in sampled frame, but lag is more likely | not assessable in current scene | smooth but likely laggier | latency rises as throughput increases | strongest raw live throughput so far; resource cost is also highest |
| EXP-04L | live USB camera, RTSP probe connected | current scene has no drone target | no obvious false positives in sampled frame | current scene has no positive target | none observed in sampled frame | moderate box stability under reuse mode | not assessable in current scene | smoother and more responsive | dropped_publish observed during reuse-heavy mode | best current live trade-off candidate; best latency with moderate resource usage |

## Screenshot Checklist By Experiment

Each experiment should keep at least these files:

### EXP-01 Baseline

- `fig_exp01_baseline_arch.png`
- `fig_exp01_baseline_result.png`
- `fig_exp01_baseline_perf.png`
- `fig_exp01_baseline_issue.png`

### EXP-02 Pipeline

- `fig_exp02_pipeline_arch.png`
- `fig_exp02_pipeline_result.png`
- `fig_exp02_pipeline_perf.png`
- `fig_exp02_pipeline_issue.png`

### EXP-03 Multi-Context

- `fig_exp03_multictx_arch.png`
- `fig_exp03_multictx_result.png`
- `fig_exp03_multictx_perf.png`
- `fig_exp03_multictx_issue.png`

### EXP-04 Policy-Optimized

- `fig_exp04_policy_arch.png`
- `fig_exp04_policy_result.png`
- `fig_exp04_policy_perf.png`
- `fig_exp04_policy_issue.png`

For live validation, use the same names with an `L` suffix:

- `fig_exp01L_baseline_result.png`
- `fig_exp03L_multictx_perf.png`

## Expected First-Round Questions

This round should answer:

1. How much does pure pipeline decoupling help over the baseline?
2. Does dual-context inference improve throughput enough to justify latency cost?
3. Does policy optimization produce a better practical trade-off than brute-force dual-context parallelism?
4. Are the trends consistent between fixed-input experiments and live-camera validation?

## Recommended First-Round Narrative

If results follow expectations, the thesis narrative can be:

- `Baseline` provides the reference system behavior.
- `Pipeline` improves system organization and stabilizes output.
- `Multi-Context` delivers the strongest raw throughput gain.
- `Policy-Optimized` often gives the better practical real-time experience under edge constraints.

This narrative is likely to fit both engineering implementation and experimental analysis.

## First Live Results Snapshot

The first live-camera round already shows a clear throughput-latency trade-off:

- `EXP-01L` baseline full-frame inference stayed around `6.56 FPS`, with end-to-end latency around `240.59 ms`
- `EXP-03L` dual-context inference reached about `13.98 FPS`, but latency rose to `307.30 ms`
- `EXP-04L` policy optimization reached about `10.55 stream FPS` with only `5.04 NPU FPS`, while reducing end-to-end latency to about `104.91 ms`

This is already enough to support an initial thesis-style conclusion:

- multi-context inference is the strongest path for throughput
- policy optimization is the better path for practical real-time responsiveness

## Notes

- `*` The baseline log later showed a reconnect-related latency spike, so `403.57 ms` should be treated as a provisional maximum rather than a clean long-run max.
- `**` In `EXP-04L`, the last logged frame was a reuse frame, so `work_ms=0.00` reflects reuse cost rather than true NPU inference time. Use `npu_fps` and `end_to_end_ms` as the more meaningful comparison anchors for now.
- `***` The `CPU Usage` and `Memory Usage` entries above come from a second short live snapshot under active RTSP client pull on 2026-04-21. They should be treated as practical resource snapshots rather than long-window averages.
- `****` The current camera scene used for the second observation pass did not contain a clear drone target, so the quality notes mainly support stability and false-positive analysis, not recall analysis.
