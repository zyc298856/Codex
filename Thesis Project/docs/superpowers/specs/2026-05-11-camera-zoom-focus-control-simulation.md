# Camera zoom/focus control simulation for future board validation

Date: 2026-05-11

## Purpose

This record prepares the next real-camera control step for the RK3588 UAV
detection project. The goal is not to claim that live autofocus has already
been completed. Instead, this simulation checks a more realistic control policy
before the board and USB camera are available again.

Compared with the earlier `simulate_auto_zoom.py`, the new simulation is closer
to a real UVC camera because it models:

- delayed `zoom_absolute` response;
- temporary image blur after zoom changes;
- `focus_absolute` adjustment based on a sharpness score;
- confidence changes caused by focus quality;
- consecutive-frame confirmation before camera commands;
- target-loss recovery by zooming out.

## New script

```text
tools/camera_control/simulate_zoom_focus_control.py
```

The script is independent from the stable live detection programs. It does not
modify `rk_yolo_video`, `rk_yolo_live_rtsp`, or the existing simple auto-zoom
simulation.

## Output files

Local output directory:

```text
Thesis Project/eval_runs/camera_control_sim_advanced/
```

Generated files:

- `zoom_focus_control_simulation.csv`
- `zoom_focus_control_simulation.svg`
- `zoom_focus_control_simulation_summary.md`

`eval_runs/` is ignored by Git, so these files are local experiment artifacts.

## Simulated control logic

The simulation follows this loop:

```text
target distance / visibility
-> observed detection box ratio
-> current zoom and focus state
-> sharpness score
-> detection confidence
-> control state machine
-> optional zoom or focus command
```

The state machine contains the following main states:

```text
SEARCHING
TRACKING
ZOOMING
FOCUSING
STABLE
LOST
```

The policy avoids issuing a camera command from a single unstable frame. A zoom
command is issued only after the target is confirmed to be too small or too
large for several consecutive frames. If the target is lost during focus
probing, the focus scan is aborted and the system returns to the target-loss
logic.

## Configuration

| Item | Value |
|---|---:|
| simulated frames | 480 |
| zoom range | 0..40 |
| initial zoom | 15 |
| zoom step | 5 |
| zoom settle frames | 10 |
| focus range | 0..550 |
| initial focus | 230 |
| focus step | 18 |
| focus probe budget | 7 |
| target ratio band | 0.07..0.20 |
| confidence threshold | 0.35 |
| confirmation frames | 3 |

## Result summary

| Metric | Value |
|---|---:|
| detected frames | 394 / 480 |
| stable-state frames | 125 |
| zoom command range | 15..35 |
| focus command range | 230..284 |
| zoom events | 6 |
| focus events | 50 |
| mean confidence | 0.571 |
| mean sharpness | 0.744 |

Main event reasons:

- `confirmed_target_small`: 5
- `target_lost_zoom_out`: 1
- `start_focus_scan`: 7
- `probe_focus`: 36
- `focus_scan_done`: 5
- `abort_focus_target_lost`: 2

## Important behavior

The simulation shows three useful behaviors for future board validation.

First, the controller zooms in gradually when the target is persistently small.
It does not change the zoom value on every frame, which should reduce visual
oscillation in a live RTSP stream.

Second, after zoom changes, the controller enters a focus-search stage. This
models the practical issue that optical zoom can change the best focus position
and temporarily reduce image sharpness.

Third, when the target is occluded or lost, the controller aborts focus probing
and eventually zooms out. This is important for real scenes because continuing
to focus on an invisible target would waste time and make reacquisition harder.

## How to use this when the board returns

The future board-side validation should compare the simulation log fields with
live `rk_yolo_live_rtsp` logs:

| Simulation field | Board-side counterpart |
|---|---|
| `zoom_cmd` | `v4l2-ctl -c zoom_absolute=...` command value |
| `focus_cmd` | `v4l2-ctl -c focus_absolute=...` command value |
| `sharpness` | Laplacian or ROI sharpness computed from camera frame |
| `box_ratio` | largest detection-box side ratio |
| `confidence` | YOLO detection confidence |
| `state` | live camera-control state |
| `reason` | log reason for camera command |

Suggested real-camera validation order:

1. Confirm `zoom_absolute`, `focus_auto`, and `focus_absolute` using `v4l2-ctl`.
2. Run manual zoom/focus commands and check whether the RTSP image visibly
   responds.
3. Add ROI sharpness logging before enabling closed-loop focus changes.
4. Enable automatic zoom with conservative thresholds.
5. Enable focus probing only after zoom behavior is stable.
6. Record RTSP output and command logs for comparison with this simulation.

## Defense wording

Safe wording:

> The current stable system has completed real-time detection and RTSP output.
> For camera control, the project has verified that the USB camera exposes UVC
> zoom and focus controls. An optional detection-driven zoom strategy already
> exists, and this new simulation further prepares a future zoom/focus state
> machine. It should be described as control-strategy preparation rather than
> completed live autofocus validation.

Chinese wording:

> 当前稳定系统已经完成实时检测与 RTSP 输出。针对摄像头控制，前期已经确认 USB 摄像头开放了 UVC 变焦和对焦控制项，并在程序中预留了基于检测框大小的自动变焦策略。本次仿真进一步加入了变焦延迟、变焦后短暂失焦、清晰度评价和对焦搜索状态机，用于为后续真机闭环控制做准备。由于还没有在板端摄像头上重新验证，因此不能表述为已经完成实时自动对焦，而应表述为面向后续真机控制的策略仿真和接口准备。

## Boundary

This result is useful for planning, but it is not a substitute for real camera
validation. The final defense demo should still prioritize the stable detection
and RTSP stream. Automatic zoom/focus can be presented as an extension path if
time and board conditions allow live verification.
