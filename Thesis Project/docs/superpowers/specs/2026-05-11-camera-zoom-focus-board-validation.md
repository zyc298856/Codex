# RK3588 USB camera zoom/focus board validation

Date: 2026-05-11

Board location: grandmother's home network

SSH target: `ubuntu@192.168.10.186`

Camera node: `/dev/video48`

## Purpose

This validation checks whether the USB UVC camera can support a future detection-driven camera response loop:

`YOLO detection box -> target-size policy -> UVC zoom/focus control -> updated camera view`

This is not yet the final YOLO closed-loop test. The goal is to verify that the RK3588 board can write and read back real camera controls safely.

## Camera capability summary

The USB camera was detected as `27c2:0531 / HBS Camera`.

Main video node:

- `/dev/video48`: video capture node
- `/dev/video49`: metadata node

Supported capture formats on `/dev/video48`:

- `MJPG`: up to `1920x1080 @ 30 fps`
- `H264`: up to `1920x1080 @ 30 fps`
- `YUYV`: available, but high resolutions have lower frame rates

Relevant UVC controls:

- `zoom_absolute`: `0..99`
- `focus_auto`: `0/1`
- `focus_absolute`: `0..550`
- exposure, white balance, brightness, contrast, gain and other controls are also exposed

## Validation procedure

The board-side script used a conservative policy and limited zoom to a safe range instead of sweeping the full camera range.

Policy cases:

- Small target box: increase `zoom_absolute`
- Suitable target box: hold current zoom
- Large target box: decrease `zoom_absolute`
- Lost target: zoom out to recover a wider field of view
- After zoom changes: keep `focus_auto=1` for the practical demo path
- Manual fallback check: disable `focus_auto`, probe several `focus_absolute` values, compute a simple image sharpness score, then restore safe state

Output directory on the board:

`/tmp/rk_yolo_camera_control_validation_20260511_175458`

Local artifact directory:

`Thesis Project/eval_runs/camera_control_board_validation/rk_yolo_camera_control_validation_20260511_175458`

## Key results

The camera accepted all zoom commands and the readback values matched the requested values.

| Scenario | Reason | Zoom command | Zoom readback | Focus auto |
|---|---:|---:|---:|---:|
| far_small_1 | target_small_zoom_in | 8 | 8 | 1 |
| far_small_2 | target_small_zoom_in | 16 | 16 | 1 |
| target_good | hold | 16 | 16 | 1 |
| near_large | target_large_zoom_out | 8 | 8 | 1 |
| lost_1 | target_lost_zoom_out | 0 | 0 | 1 |
| lost_2 | target_lost_zoom_out | 0 | 0 | 1 |
| reacquire_small | target_small_zoom_in | 8 | 8 | 1 |

Manual focus fallback was also verified:

| Focus command | Focus readback | Sharpness score |
|---:|---:|---:|
| 215 | 215 | 33.78 |
| 275 | 275 | 33.11 |
| 335 | 335 | 31.27 |

In this scene, the simple sharpness metric selected `focus_absolute=215`, proving that the board can issue manual focus commands and evaluate a basic image-feedback signal. For the actual demonstration path, the safer default remains `focus_auto=1`.

## Final restored state

After the validation, the camera state was restored and checked again:

- `zoom_absolute: 0`
- `focus_auto: 1`
- `focus_absolute: 275`

## Conclusion

The camera-control part required by the future demo is technically feasible:

- The board can control optical/digital zoom through UVC.
- The board can switch between autofocus and manual focus.
- A detection-box-size policy can be mapped to real zoom commands.
- A manual focus fallback can be implemented by probing focus values and using image sharpness feedback.

The remaining task is to connect the live YOLO output to this same UVC control interface, so the control decisions come from real detection results rather than simulated target-box states.
