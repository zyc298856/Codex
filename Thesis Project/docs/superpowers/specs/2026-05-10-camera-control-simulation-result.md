# Camera auto-zoom policy simulation result

Date: 2026-05-10

## Purpose

This offline simulation checks the optional UVC auto-zoom policy without an RK3588 board or USB camera. It mirrors the live program's decision input: the largest side ratio of the displayed detection box.

The simulation does not modify `rk_yolo_live_rtsp`; it only produces local evaluation artifacts.

## Output files

Local output directory:

```text
Thesis Project/eval_runs/camera_control_sim/
```

Generated files:

- `auto_zoom_simulation.csv`
- `auto_zoom_simulation.svg`
- `auto_zoom_simulation_summary.md`

Note: `eval_runs/` is ignored by Git, so these files are local experiment outputs.

## Configuration

| Parameter | Value |
|---|---:|
| zoom range | 0..40 |
| initial zoom | 20 |
| step | 5 |
| cooldown frames | 20 |
| lost frames to zoom out | 60 |
| target side ratio | 0.06..0.22 |
| simulated frames | 360 |

## Key adjustment events

| Frame | Phase | Ratio before decision | Zoom before | Reason | Zoom after |
|---:|---|---:|---:|---|---:|
| 20 | far_small_target | 0.0450 | 20 | target_small | 25 |
| 40 | far_small_target | 0.0500 | 25 | target_small | 30 |
| 60 | far_small_target | 0.0550 | 30 | target_small | 35 |
| 165 | near_large_target | 0.4560 | 35 | target_large | 30 |
| 185 | near_large_target | 0.4180 | 30 | target_large | 25 |
| 205 | near_large_target | 0.3800 | 25 | target_large | 20 |
| 284 | target_lost | 0.0000 | 20 | target_lost | 15 |
| 304 | reacquired_small_target | 0.0480 | 15 | target_small | 20 |
| 324 | reacquired_small_target | 0.0540 | 20 | target_small | 25 |

## Result summary

- Simulated frames: 360
- Zoom range observed: 15..35
- Final zoom: 25
- Total adjustments: 9
- Adjustment reasons:
  - `target_small`: 5
  - `target_large`: 3
  - `target_lost`: 1

## Interpretation

The strategy is conservative enough for a live demonstration. It zooms in gradually when the target is too small, zooms out when the target becomes too large, and widens the field of view after sustained target loss. The cooldown window prevents frequent zoom changes, which should reduce visual shaking compared with an aggressive control loop.

For defense wording, this can be described as an offline verification of the detection-driven UVC zoom-control logic. Board-side validation is still needed before making it part of the live demonstration path.
