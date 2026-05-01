# UVC auto-zoom validation

Date: 2026-05-01

## Purpose

The USB camera exposes UVC controls for `zoom_absolute`, `focus_auto`, and `focus_absolute`.
To support the teacher's expected demonstration direction, `rk_yolo_live_rtsp` now includes an optional detection-driven UVC zoom loop.

The feature is conservative and disabled by default so it does not disturb the stable RTSP detection path.

## UVC capability check

Board:

- Host: `ubuntu@192.168.2.156`
- Camera node: `/dev/video48`
- Camera model: `HBS Camera: HBS Camera`
- Driver: `uvcvideo`

Relevant controls:

```text
focus_auto          default=1 value=1
focus_absolute      min=0 max=550 step=1 default=275 value=275
zoom_absolute       min=0 max=99 step=1 default=0 value=0
exposure_auto       Auto / Shutter Priority
exposure_absolute   min=10 max=330 default=300
```

Supported useful capture formats:

```text
MJPG 640x480 / 1280x720 / 1920x1080 @ 30 fps
H264 640x480 / 1280x720 / 1920x1080 @ 30 fps
```

## Implementation

Environment variable:

```bash
RK_YOLO_AUTO_ZOOM=1
```

Related controls:

- `RK_YOLO_AUTO_ZOOM_MIN=0`
- `RK_YOLO_AUTO_ZOOM_MAX=60`
- `RK_YOLO_AUTO_ZOOM_STEP=5`
- `RK_YOLO_AUTO_ZOOM_COOLDOWN=30`
- `RK_YOLO_AUTO_ZOOM_LOST_FRAMES=90`
- `RK_YOLO_AUTO_ZOOM_MIN_RATIO=0.06`
- `RK_YOLO_AUTO_ZOOM_MAX_RATIO=0.22`

Runtime behavior:

- The startup camera tune still sets the initial `zoom_absolute` and focus controls.
- If the largest displayed target box is smaller than the configured ratio, the program slowly increases `zoom_absolute`.
- If the target is too large, the program slowly decreases `zoom_absolute`.
- If the target is lost for enough frames, the program slowly zooms out to recover a wider field of view.
- Focus is not changed dynamically; this avoids repeated hunting and keeps the detection image stable.

## Board smoke test

Test command pattern:

```bash
RK_YOLO_AUTO_ZOOM=1 \
RK_YOLO_AUTO_ZOOM_LOST_FRAMES=5 \
RK_YOLO_AUTO_ZOOM_COOLDOWN=5 \
RK_YOLO_AUTO_ZOOM_MIN=0 \
RK_YOLO_AUTO_ZOOM_MAX=25 \
RK_YOLO_CAMERA_ZOOM=20 \
RK_YOLO_CAMERA_FOCUS_AUTO=0 \
RK_YOLO_CAMERA_FOCUS=260 \
./rk_yolo_live_rtsp /dev/video48 <model.rknn> /autoztest 640 480 15 0.99 0.45 8560 1
```

RTSP client check:

```text
rtsp://127.0.0.1:8560/autoztest
width=640
height=480
avg_frame_rate=15/1
```

Observed log:

```text
camera_tune=applied model="HBS Camera: HBS Camera" zoom=20 focus_auto=0 focus=260
auto_zoom=on model="HBS Camera: HBS Camera" range=[0,25] step=5 cooldown_frames=5 lost_frames=5
auto_zoom frame=8 zoom=15 reason=target_lost box_ratio=0.000
auto_zoom frame=19 zoom=10 reason=target_lost box_ratio=0.000
auto_zoom frame=30 zoom=5 reason=target_lost box_ratio=0.000
auto_zoom frame=42 zoom=0 reason=target_lost box_ratio=0.000
```

After the test, the camera was restored to:

```text
zoom_absolute: 20
focus_auto: 0
focus_absolute: 260
```

## Conclusion

The board and camera support the required UVC control path. The current implementation proves that the live RK3588 detection process can drive camera zoom through software based on detection state. It should be described as an optional closed-loop UVC control experiment rather than the default stable detection mode.
