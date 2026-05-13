# Camera auto zoom/focus preparation plan

Date: 2026-05-10

## Current status

The live RK3588 RTSP program already contains an optional UVC camera-control path. It is disabled by default and therefore does not affect the stable detection demo.

Validated earlier on the board:

- Camera: `HBS Camera: HBS Camera`
- Driver: `uvcvideo`
- Device used during validation: `/dev/video48`
- Exposed controls:
  - `zoom_absolute`, range `0..99`
  - `focus_auto`
  - `focus_absolute`, range `0..550`
- Startup camera tune:
  - `RK_YOLO_CAMERA_ZOOM`
  - `RK_YOLO_CAMERA_FOCUS_AUTO`
  - `RK_YOLO_CAMERA_FOCUS`
- Detection-driven zoom loop:
  - `RK_YOLO_AUTO_ZOOM=1`
  - `RK_YOLO_AUTO_ZOOM_MIN`
  - `RK_YOLO_AUTO_ZOOM_MAX`
  - `RK_YOLO_AUTO_ZOOM_STEP`
  - `RK_YOLO_AUTO_ZOOM_COOLDOWN`
  - `RK_YOLO_AUTO_ZOOM_LOST_FRAMES`
  - `RK_YOLO_AUTO_ZOOM_MIN_RATIO`
  - `RK_YOLO_AUTO_ZOOM_MAX_RATIO`

The earlier board smoke test proved that the program can call `v4l2-ctl` from the live RTSP process and update `zoom_absolute` during runtime. The test should be described as an optional UVC closed-loop experiment rather than the default stable detection mode.

## What can be prepared without the board

### 1. Prepare the validation checklist

When the board is available again, run the following checks in order.

```bash
v4l2-ctl --list-devices
v4l2-ctl -d /dev/video48 --all
v4l2-ctl -d /dev/video48 --list-ctrls
v4l2-ctl -d /dev/video48 --get-ctrl=zoom_absolute
v4l2-ctl -d /dev/video48 --get-ctrl=focus_auto
v4l2-ctl -d /dev/video48 --get-ctrl=focus_absolute
```

Manual control smoke test:

```bash
v4l2-ctl -d /dev/video48 -c zoom_absolute=0
v4l2-ctl -d /dev/video48 -c zoom_absolute=20
v4l2-ctl -d /dev/video48 -c zoom_absolute=40
v4l2-ctl -d /dev/video48 -c focus_auto=1
v4l2-ctl -d /dev/video48 -c focus_auto=0,focus_absolute=260
```

Acceptance criteria:

- `zoom_absolute` changes are accepted by the camera.
- The RTSP image visibly changes field of view after zoom changes.
- Focus commands do not crash or block the video stream.
- If manual focus causes blur, restore `focus_auto=1` or the verified stable value.

### 2. Prepare the automatic zoom demo command

Use a conservative zoom range first. The goal is to show the control loop, not to make the camera hunt aggressively.

```bash
RK_YOLO_AUTO_ZOOM=1 \
RK_YOLO_AUTO_ZOOM_MIN=0 \
RK_YOLO_AUTO_ZOOM_MAX=40 \
RK_YOLO_AUTO_ZOOM_STEP=5 \
RK_YOLO_AUTO_ZOOM_COOLDOWN=20 \
RK_YOLO_AUTO_ZOOM_LOST_FRAMES=60 \
RK_YOLO_AUTO_ZOOM_MIN_RATIO=0.06 \
RK_YOLO_AUTO_ZOOM_MAX_RATIO=0.22 \
RK_YOLO_CAMERA_ZOOM=20 \
RK_YOLO_CAMERA_FOCUS_AUTO=0 \
RK_YOLO_CAMERA_FOCUS=260 \
./rk_yolo_live_rtsp /dev/video48 <model.rknn> /drone 640 480 15 0.35 0.45 8554 3
```

PC-side viewing URL:

```text
rtsp://<board-ip>:8554/drone
```

Expected log signals:

```text
camera_tune=applied ...
auto_zoom=on ...
auto_zoom frame=... zoom=... reason=...
stream_fps=...
npu_fps=...
```

### 3. Prepare test targets

Since real UAV flight is inconvenient, use controlled targets for the first demonstration:

- A printed drone image on A4 paper.
- The same image shown on a monitor at several sizes.
- A short public UAV video played on a screen.
- A small object only as a camera-control fallback target, not as the main UAV detection proof.

Recommended order:

1. Use the printed or screen-displayed drone image to verify detection.
2. Move the target farther away or reduce its on-screen size.
3. Enable `RK_YOLO_AUTO_ZOOM=1`.
4. Observe whether the program increases zoom when the target box is too small.
5. Confirm that RTSP output remains smooth and the detection box remains visible.

### 4. Prepare result recording table

| Test item | Expected result | Pass/Fail | Notes |
|---|---|---|---|
| UVC controls listed | `zoom_absolute`, `focus_auto`, `focus_absolute` visible |  |  |
| Manual zoom | Image field of view changes |  |  |
| Startup tune | Program prints `camera_tune=applied` |  |  |
| Auto zoom enabled | Program prints `auto_zoom=on` |  |  |
| Target too small | Zoom value increases gradually |  |  |
| Target lost | Zoom value decreases to recover wider view |  |  |
| RTSP viewing | PC can view live stream |  |  |
| Detection stability | Detection box remains visible after zoom |  |  |

### 5. Prepare defense wording

Conservative wording:

> The current system has completed the real-time detection and RTSP display loop. For camera control, the UVC capability check confirmed that the selected USB camera exposes zoom and focus controls. Based on the detection result, the live program includes an optional closed-loop zoom strategy controlled by environment variables. This path is disabled by default to preserve the stable demonstration configuration, but it provides a verified interface for extending the system from target detection to camera response.

Chinese version:

> 当前系统已经完成了实时检测和 RTSP 显示闭环。针对摄像头控制，前期通过 UVC 控制项检查确认该 USB 摄像头开放了变焦和对焦接口；实时程序中也加入了基于检测框大小的可选自动变焦策略。该功能默认关闭，避免影响稳定演示，但已经为“检测到目标后调整摄像头视场”的闭环扩展预留并验证了软件接口。

If asked why autofocus is not emphasized:

> 自动变焦与检测框大小之间有明确对应关系，因此更适合做闭环控制。对焦控制虽然摄像头支持，但频繁动态对焦可能导致画面抖动和短时失焦，反而影响检测稳定性。因此当前方案采用固定或自动对焦作为基础设置，将检测驱动的闭环重点放在变焦控制上。

## Recommended next step when the board returns

1. Re-run `v4l2-ctl --list-ctrls` to confirm the camera node and control range.
2. Run manual zoom/focus commands and visually confirm the image response.
3. Run `rk_yolo_live_rtsp` with `RK_YOLO_AUTO_ZOOM=1`.
4. Record logs, RTSP screen capture, and whether detection remains stable.
5. If the loop is stable, add a short note to the defense demo script and thesis experiment record; if not stable, present it as an optional extension path with interface verification.

## Risk boundary

- Do not make automatic zoom the default demo path unless it is revalidated on the board.
- Do not claim full dynamic autofocus unless `focus_absolute` or `focus_auto` behavior is tested under live detection.
- The safest final demonstration remains the existing FP RKNN detection + RTSP output path.
- Auto zoom is a useful extension to show camera-control capability, but it should not replace the stable detection pipeline.
