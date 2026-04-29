# INT8/RGA Board Experiment Command Matrix

Generated commands for repeatable RK3588 fixed-video comparison.

## Inputs

- video: `/home/ubuntu/eval/public_uav.mp4`
- fp_model: `/home/ubuntu/models/best.end2end_false.op12.rk3588.fp.v220.rknn`
- int8_model: `/home/ubuntu/models/best.end2end_false.op12.rk3588.int8.v220.rknn`
- output_dir: `/home/ubuntu/eval/int8_rga_runs`
- score_threshold: `0.35`
- nms_threshold: `0.45`

## Prepare Output Directory

```bash
mkdir -p /home/ubuntu/eval/int8_rga_runs
```

## Cases

| Case | Model | Purpose |
| --- | --- | --- |
| `fp_opencv_baseline` | `fp` | FP RKNN baseline with OpenCV preprocessing. |
| `fp_rga_resize` | `fp` | FP RKNN with RGA RGB resize preprocessing. |
| `fp_rga_cvt_resize` | `fp` | FP RKNN with fused RGA BGR-to-RGB conversion and resize. |
| `fp_rga_letterbox` | `fp` | FP RKNN with RGA letterbox preprocessing. |
| `fp_zero_copy` | `fp` | FP RKNN with zero-copy input memory path. |
| `int8_opencv_baseline` | `int8` | INT8 RKNN baseline with OpenCV preprocessing. |
| `int8_rga_cvt_resize` | `int8` | INT8 RKNN with fused RGA BGR-to-RGB conversion and resize. |
| `int8_rga_letterbox` | `int8` | INT8 RKNN with RGA letterbox preprocessing. |
| `int8_zero_copy` | `int8` | INT8 RKNN with zero-copy input memory path. |

## Commands

### fp_opencv_baseline

```bash
RK_YOLO_PREPROCESS=opencv RK_YOLO_PROFILE=1 ./rk_yolo_video /home/ubuntu/eval/public_uav.mp4 /home/ubuntu/eval/int8_rga_runs/fp_opencv_baseline.mp4 /home/ubuntu/models/best.end2end_false.op12.rk3588.fp.v220.rknn 0.35 0.45 /home/ubuntu/eval/int8_rga_runs/fp_opencv_baseline.detections.csv /home/ubuntu/eval/int8_rga_runs/fp_opencv_baseline.roi.jsonl /home/ubuntu/eval/int8_rga_runs/fp_opencv_baseline.alarm_events.csv 2>&1 | tee /home/ubuntu/eval/int8_rga_runs/fp_opencv_baseline.log
```

### fp_rga_resize

```bash
RK_YOLO_PREPROCESS=rga RK_YOLO_PROFILE=1 ./rk_yolo_video /home/ubuntu/eval/public_uav.mp4 /home/ubuntu/eval/int8_rga_runs/fp_rga_resize.mp4 /home/ubuntu/models/best.end2end_false.op12.rk3588.fp.v220.rknn 0.35 0.45 /home/ubuntu/eval/int8_rga_runs/fp_rga_resize.detections.csv /home/ubuntu/eval/int8_rga_runs/fp_rga_resize.roi.jsonl /home/ubuntu/eval/int8_rga_runs/fp_rga_resize.alarm_events.csv 2>&1 | tee /home/ubuntu/eval/int8_rga_runs/fp_rga_resize.log
```

### fp_rga_cvt_resize

```bash
RK_YOLO_PREPROCESS=rga_cvt_resize RK_YOLO_PROFILE=1 ./rk_yolo_video /home/ubuntu/eval/public_uav.mp4 /home/ubuntu/eval/int8_rga_runs/fp_rga_cvt_resize.mp4 /home/ubuntu/models/best.end2end_false.op12.rk3588.fp.v220.rknn 0.35 0.45 /home/ubuntu/eval/int8_rga_runs/fp_rga_cvt_resize.detections.csv /home/ubuntu/eval/int8_rga_runs/fp_rga_cvt_resize.roi.jsonl /home/ubuntu/eval/int8_rga_runs/fp_rga_cvt_resize.alarm_events.csv 2>&1 | tee /home/ubuntu/eval/int8_rga_runs/fp_rga_cvt_resize.log
```

### fp_rga_letterbox

```bash
RK_YOLO_PREPROCESS=rga_cvt_resize RK_YOLO_PROFILE=1 RK_YOLO_RGA_LETTERBOX=1 ./rk_yolo_video /home/ubuntu/eval/public_uav.mp4 /home/ubuntu/eval/int8_rga_runs/fp_rga_letterbox.mp4 /home/ubuntu/models/best.end2end_false.op12.rk3588.fp.v220.rknn 0.35 0.45 /home/ubuntu/eval/int8_rga_runs/fp_rga_letterbox.detections.csv /home/ubuntu/eval/int8_rga_runs/fp_rga_letterbox.roi.jsonl /home/ubuntu/eval/int8_rga_runs/fp_rga_letterbox.alarm_events.csv 2>&1 | tee /home/ubuntu/eval/int8_rga_runs/fp_rga_letterbox.log
```

### fp_zero_copy

```bash
RK_YOLO_PROFILE=1 RK_YOLO_ZERO_COPY_INPUT=1 ./rk_yolo_video /home/ubuntu/eval/public_uav.mp4 /home/ubuntu/eval/int8_rga_runs/fp_zero_copy.mp4 /home/ubuntu/models/best.end2end_false.op12.rk3588.fp.v220.rknn 0.35 0.45 /home/ubuntu/eval/int8_rga_runs/fp_zero_copy.detections.csv /home/ubuntu/eval/int8_rga_runs/fp_zero_copy.roi.jsonl /home/ubuntu/eval/int8_rga_runs/fp_zero_copy.alarm_events.csv 2>&1 | tee /home/ubuntu/eval/int8_rga_runs/fp_zero_copy.log
```

### int8_opencv_baseline

```bash
RK_YOLO_PREPROCESS=opencv RK_YOLO_PROFILE=1 ./rk_yolo_video /home/ubuntu/eval/public_uav.mp4 /home/ubuntu/eval/int8_rga_runs/int8_opencv_baseline.mp4 /home/ubuntu/models/best.end2end_false.op12.rk3588.int8.v220.rknn 0.35 0.45 /home/ubuntu/eval/int8_rga_runs/int8_opencv_baseline.detections.csv /home/ubuntu/eval/int8_rga_runs/int8_opencv_baseline.roi.jsonl /home/ubuntu/eval/int8_rga_runs/int8_opencv_baseline.alarm_events.csv 2>&1 | tee /home/ubuntu/eval/int8_rga_runs/int8_opencv_baseline.log
```

### int8_rga_cvt_resize

```bash
RK_YOLO_PREPROCESS=rga_cvt_resize RK_YOLO_PROFILE=1 ./rk_yolo_video /home/ubuntu/eval/public_uav.mp4 /home/ubuntu/eval/int8_rga_runs/int8_rga_cvt_resize.mp4 /home/ubuntu/models/best.end2end_false.op12.rk3588.int8.v220.rknn 0.35 0.45 /home/ubuntu/eval/int8_rga_runs/int8_rga_cvt_resize.detections.csv /home/ubuntu/eval/int8_rga_runs/int8_rga_cvt_resize.roi.jsonl /home/ubuntu/eval/int8_rga_runs/int8_rga_cvt_resize.alarm_events.csv 2>&1 | tee /home/ubuntu/eval/int8_rga_runs/int8_rga_cvt_resize.log
```

### int8_rga_letterbox

```bash
RK_YOLO_PREPROCESS=rga_cvt_resize RK_YOLO_PROFILE=1 RK_YOLO_RGA_LETTERBOX=1 ./rk_yolo_video /home/ubuntu/eval/public_uav.mp4 /home/ubuntu/eval/int8_rga_runs/int8_rga_letterbox.mp4 /home/ubuntu/models/best.end2end_false.op12.rk3588.int8.v220.rknn 0.35 0.45 /home/ubuntu/eval/int8_rga_runs/int8_rga_letterbox.detections.csv /home/ubuntu/eval/int8_rga_runs/int8_rga_letterbox.roi.jsonl /home/ubuntu/eval/int8_rga_runs/int8_rga_letterbox.alarm_events.csv 2>&1 | tee /home/ubuntu/eval/int8_rga_runs/int8_rga_letterbox.log
```

### int8_zero_copy

```bash
RK_YOLO_PROFILE=1 RK_YOLO_ZERO_COPY_INPUT=1 ./rk_yolo_video /home/ubuntu/eval/public_uav.mp4 /home/ubuntu/eval/int8_rga_runs/int8_zero_copy.mp4 /home/ubuntu/models/best.end2end_false.op12.rk3588.int8.v220.rknn 0.35 0.45 /home/ubuntu/eval/int8_rga_runs/int8_zero_copy.detections.csv /home/ubuntu/eval/int8_rga_runs/int8_zero_copy.roi.jsonl /home/ubuntu/eval/int8_rga_runs/int8_zero_copy.alarm_events.csv 2>&1 | tee /home/ubuntu/eval/int8_rga_runs/int8_zero_copy.log
```

## Suggested Comparison Fields

- Stream FPS and output video continuity.
- `prepare_ms`, `input_set_or_update_ms`, `rknn_run_ms`, `outputs_get_ms`, `decode_ms`, `render_ms`, and `total_work_ms` from `profile_csv` logs.
- Obvious false positives and false negatives in the output video.
- Whether logs contain RGA fallback, zero-copy fallback, or model runtime errors.
