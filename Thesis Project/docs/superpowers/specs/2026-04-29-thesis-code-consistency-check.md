# 2026-04-29 Thesis-Code Consistency Check

## Scope

This check compares the current thesis wording in `Thesis Project/paper/full_thesis_latest_merged.docx` with the implemented RK3588 code paths.

## Confirmed implemented paths

- `rk_yolo_video` and `rk_yolo_live_rtsp` provide fixed-video and real-time RTSP detection paths.
- `RK_YOLO_PROFILE` records stage timing fields for prepare, input update, RKNN run, output get, decode, output release, render, and total work.
- `RK_YOLO_ZERO_COPY_INPUT` provides an optional RKNN input memory path through `rknn_create_mem` and `rknn_set_io_mem`.
- `RK_YOLO_PREPROCESS=rga_cvt_resize`, `RK_YOLO_RGA_LETTERBOX`, `RK_YOLO_RGA_FRAME_RESIZE`, and `RK_YOLO_RGA_PUBLISH_NV12` provide optional RGA experiment paths.
- `RK_YOLO_INFER_WORKERS` enables multi-context inference when `detect_every_n=1`.
- `RK_YOLO_TRACK_MODE=motion/optflow`, `RK_YOLO_BOX_SMOOTH`, and dynamic ROI are implemented in the RTSP path.
- `RK_YOLO_ALARM_OVERLAY` and alarm event logging provide the software alarm substitute for GPIO hardware.

## Wording adjustment

The thesis originally used broad wording that could imply all RGA hardware preprocessing remained only future work. Because optional RGA experiments have been implemented and validated, the wording was narrowed to distinguish:

- completed: optional RGA experiment paths and switchable validation;
- not completed as stable default: full RGA hardware preprocessing closed loop with MPP/physically continuous memory/default RTSP path.

This keeps the thesis aligned with the code and avoids overstating INT8, complete RGA, GPIO, or zero-copy as stable default results.
