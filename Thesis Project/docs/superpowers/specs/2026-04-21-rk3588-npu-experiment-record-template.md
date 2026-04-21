# RK3588 NPU Multithread Experiment Record Template

Date: 2026-04-21

## Purpose

This template standardizes how the RK3588 anti-UAV experiments should be recorded.

It is designed for:

- weekly reports
- mid-term presentations
- thesis experiment chapters
- later figure and table selection

The current research focus is:

`RTSP real-time anti-UAV detection on RK3588, with emphasis on NPU multithreading and throughput-latency trade-off analysis.`

## Core Experiment Layers

Recommended main experiment groups:

1. `Baseline`
- single-context or near-serial processing
- `detect_every_n=1`
- no extra policy optimization

2. `Pipeline`
- multi-thread pipeline with capture, infer, and publish decoupled
- `detect_every_n=1`
- one inference worker

3. `Multi-Context`
- dual RKNN context / dual inference worker
- `detect_every_n=1`

4. `Policy-Optimized`
- single-context pipeline plus policy optimization
- typical settings: `detect_every_n=3`, lightweight tracking, optional ROI

5. `Hybrid` (optional expansion)
- multi-thread + multi-context + policy optimization
- used only after the first four groups are stable

## Input Source Strategy

Use a dual-source strategy:

- **Primary experiment source**: fixed recorded video, for strict repeatability
- **Secondary validation source**: live USB camera + RTSP, for real deployment validation

This helps balance academic rigor and practical system relevance.

## Metrics

Every experiment should record the following metric groups.

### 1. Throughput

- `stream_fps`
- `npu_fps`

### 2. Latency

- average end-to-end latency
- maximum end-to-end latency
- average inference time
- average per-frame work time

### 3. Stability

- dropped capture frames
- queue overflow count
- whether long-duration run is stable

### 4. Resource Usage

- CPU utilization
- memory usage
- NPU-related observable runtime behavior

### 5. Detection Quality

- obvious correct detections
- obvious false positives
- obvious false negatives
- duplicate box behavior
- temporal box jitter

## Sheet A: Experiment Configuration Table

Use one row per experiment run.

| Field | Description |
| --- | --- |
| Experiment ID | Example: `EXP-01` |
| Date | Run date |
| Group | `Baseline / Pipeline / Multi-Context / Policy-Optimized / Hybrid` |
| Input Source | `fixed video / live USB camera` |
| Input Name | Video file name or camera identifier |
| Scene Type | Example: open sky, urban, mountain, backlight |
| Resolution | Example: `640x480`, `1280x720` |
| Model Name | Example: `yolov10n.rknn`, `best.rk3588.fp.rknn` |
| Model Type | `general detector / drone-specific detector` |
| Score Threshold | Example: `0.35` |
| NMS Threshold | Example: `0.45` |
| detect_every_n | Example: `1`, `2`, `3` |
| Infer Workers | Example: `1`, `2` |
| Track Mode | `off / motion / optflow` |
| Dynamic ROI | `on / off` |
| Camera Tune | `on / off` |
| RTSP Output | Example: `rtsp://.../yolo` |
| Test Duration | Example: `60 s`, `180 s` |
| Notes | Extra configuration remarks |

### Suggested Markdown Table Template

```text
| Experiment ID | Date | Group | Input Source | Input Name | Scene Type | Resolution | Model Name | Model Type | Score Threshold | NMS Threshold | detect_every_n | Infer Workers | Track Mode | Dynamic ROI | Camera Tune | Test Duration | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | --- | --- | --- | --- | --- |
| EXP-01 | 2026-04-XX | Baseline | fixed video | test.mp4 | open sky | 640x480 | yolov10n.rknn | general detector | 0.30 | 0.45 | 1 | 1 | off | off | off | 60 s | baseline reference |
```

## Sheet B: Performance Result Table

This table records system-level performance.

| Field | Description |
| --- | --- |
| Experiment ID | Must match Sheet A |
| stream_fps | Stream output FPS |
| npu_fps | NPU inference FPS |
| Avg Inference Time (ms) | Per-frame or per-keyframe |
| Avg Work Time (ms) | Average pipeline work time |
| Avg End-to-End Latency (ms) | Main latency indicator |
| Max End-to-End Latency (ms) | Stability upper bound |
| Dropped Capture | Number of dropped frames |
| Queue Overflow | Overflow count if available |
| CPU Usage (%) | Average CPU usage |
| Memory Usage (MB) | Average or peak |
| Runtime Stability | `stable / minor jitter / unstable / crash` |
| Notes | Observed runtime behavior |

### Suggested Markdown Table Template

```text
| Experiment ID | stream_fps | npu_fps | Avg Inference Time (ms) | Avg Work Time (ms) | Avg End-to-End Latency (ms) | Max End-to-End Latency (ms) | Dropped Capture | Queue Overflow | CPU Usage (%) | Memory Usage (MB) | Runtime Stability | Notes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| EXP-01 | 6.48 | 6.48 | 154.30 | 160.12 | 257.64 | 390.50 | 2 | 0 | 48.0 | 620 | stable | baseline full-frame inference |
```

## Sheet C: Detection Quality Observation Table

This table captures detection quality and visual behavior.

| Field | Description |
| --- | --- |
| Experiment ID | Must match Sheet A |
| Test Scene | Short description |
| Obvious Correct Detections | Count or qualitative note |
| Obvious False Positives | Count or note |
| Obvious False Negatives | Count or note |
| Duplicate Boxes | `none / mild / obvious / severe` |
| Box Jitter | `none / mild / obvious` |
| Recovery After Loss | `good / moderate / weak` |
| Real-Time Experience | `smooth / acceptable / laggy` |
| Representative Issue | Brief summary |
| Notes | Additional comments |

### Suggested Markdown Table Template

```text
| Experiment ID | Test Scene | Obvious Correct Detections | Obvious False Positives | Obvious False Negatives | Duplicate Boxes | Box Jitter | Recovery After Loss | Real-Time Experience | Representative Issue | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| EXP-01 | open sky, single drone | moderate | few | some | mild | mild | moderate | acceptable | duplicate boxes on distant drone | baseline result |
```

## Screenshot Checklist

Each main experiment group should keep the following materials.

### Required Materials Per Group

1. **Architecture / configuration figure**
- show thread layout, queue flow, and inference context count

2. **Normal detection result figure**
- one representative RTSP or processed-frame result

3. **Performance figure**
- one plot or table view showing FPS and latency

4. **Typical issue figure**
- false positive, false negative, duplicate box, or lag case

5. **One-sentence conclusion**
- concise conclusion for later report use

### Group-Specific File Suggestions

#### `EXP-01 Baseline`

- `fig_exp01_baseline_arch.png`
- `fig_exp01_baseline_result.png`
- `fig_exp01_baseline_perf.png`
- `fig_exp01_baseline_issue.png`

Suggested conclusion:
- `Baseline serves as the reference configuration with the simplest structure but limited throughput.`

#### `EXP-02 Pipeline`

- `fig_exp02_pipeline_arch.png`
- `fig_exp02_pipeline_result.png`
- `fig_exp02_pipeline_perf.png`
- `fig_exp02_pipeline_issue.png`

Suggested conclusion:
- `Pipeline multithreading improves stage decoupling and stabilizes real-time output.`

#### `EXP-03 Multi-Context`

- `fig_exp03_multictx_arch.png`
- `fig_exp03_multictx_result.png`
- `fig_exp03_multictx_perf.png`
- `fig_exp03_multictx_issue.png`

Suggested conclusion:
- `Dual RKNN contexts improve throughput significantly, but also increase end-to-end latency.`

#### `EXP-04 Policy-Optimized`

- `fig_exp04_policy_arch.png`
- `fig_exp04_policy_result.png`
- `fig_exp04_policy_perf.png`
- `fig_exp04_policy_issue.png`

Suggested conclusion:
- `Policy optimization improves perceived real-time smoothness without requiring extra NPU contexts.`

## Figure Style Guidance

To maintain academic consistency:

- use fixed output resolution whenever possible
- keep box colors and fonts consistent
- avoid full-desktop screenshots
- crop to the valid content area only
- annotate experiment group and core settings in the caption, not inside the image

Recommended caption style:

- `Figure X. Real-time detection result under the Baseline configuration.`
- `Figure X. Throughput and latency comparison under the Multi-Context configuration.`
- `Figure X. Typical false positive case under the Policy-Optimized configuration.`

## Recommended Final Figure Set For Thesis

Suggested thesis-ready figure/table list:

- `Figure 1` System overview diagram
- `Figure 2` Experiment pipeline and data flow
- `Figure 3` Architecture comparison of different thread/context strategies
- `Table 1` Experiment configuration table
- `Table 2` Throughput and latency comparison
- `Figure 4` FPS comparison across groups
- `Figure 5` End-to-end latency comparison across groups
- `Figure 6` Representative real-time detection results
- `Figure 7` False positive / false negative / duplicate-box cases

## Immediate Use Recommendation

For the next stage, use this recording order:

1. Fill Sheet A before running each experiment.
2. Fill Sheet B immediately after the run.
3. Fill Sheet C after watching the result video or RTSP recording.
4. Save the four required screenshot types.
5. Add a one-sentence conclusion for the run.

This will make later weekly reports and thesis writing much easier.
