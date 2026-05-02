# INT8 and RGA Experiment Toolkit

This folder contains non-invasive helper tools for the RK3588 performance optimization line. The stable FP RKNN runtime is not changed by these scripts.

## Purpose

The current project has a stable FP RKNN deployment path. INT8 quantization and RGA preprocessing should be evaluated as controlled experiments before becoming the default path. This toolkit prepares the experiment loop:

1. Generate a calibration image list for RKNN INT8 quantization.
2. Convert ONNX models to FP or INT8 RKNN models.
3. Generate repeatable RK3588 command matrices for OpenCV, RGA, INT8, and zero-copy comparisons.
4. Summarize `RK_YOLO_PROFILE=1` logs into CSV tables for thesis experiments.

## Typical Workflow

Run from `Thesis Project` unless otherwise noted.

### 1. Generate calibration list

```powershell
python tools/int8_rga/make_calibration_list.py `
  --dataset-yaml datasets/drone_single_class/dataset.yaml `
  --split train `
  --limit 200 `
  --output training_runs/drone_gpu_50e/calibration/drone_calib_200.txt
```

Use representative images. For this project, start with 100 to 300 images from the single-class drone dataset, then increase only if INT8 accuracy is unstable.

For the current INT8 follow-up, use pinned public-video target frames so that the calibration set always includes difficult small-drone examples:

```powershell
python tools/int8_rga/make_calibration_list.py `
  --dataset-yaml datasets/drone_single_class/dataset.yaml `
  --split train `
  --split val `
  --pinned-image-dir datasets/drone_single_class/calibration/int8_public_targets_20260429/selected_frames `
  --limit 500 `
  --output training_runs/drone_gpu_50e/calibration/drone_calib_500_with_public_targets_windows.txt
```

Prepared calibration-list sizes:

- `drone_calib_230_with_public_targets_*`: first hybrid INT8 attempt.
- `drone_calib_500_with_public_targets_*`: medium calibration set, pinned public target frames plus train/val samples.
- `drone_calib_1000_with_public_targets_*`: larger calibration set for checking whether full INT8 confidence collapse can be reduced.

If RKNN Toolkit2 runs in WSL and the project path contains spaces, create a no-space symlink and keep symlink paths in the calibration file:

```bash
ln -sfn "/mnt/c/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis Project" /tmp/thesis_project
cd /tmp/thesis_project
python tools/int8_rga/make_calibration_list.py \
  --dataset-yaml datasets/drone_single_class/dataset.yaml \
  --split train \
  --limit 200 \
  --preserve-symlinks \
  --rewrite-prefix "/mnt/c/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis Project=/tmp/thesis_project" \
  --output training_runs/drone_gpu_50e/calibration/drone_calib_200_wsl_nospace.txt
```

### 2. Convert ONNX to RKNN

FP model:

```powershell
python tools/int8_rga/convert_onnx_to_rknn.py `
  --onnx training_runs/drone_gpu_50e/weights/best.end2end_false.op12.onnx `
  --output training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn `
  --dtype fp
```

INT8 model:

```powershell
python tools/int8_rga/convert_onnx_to_rknn.py `
  --onnx training_runs/drone_gpu_50e/weights/best.end2end_false.op12.onnx `
  --output training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.int8.v220.rknn `
  --dtype int8 `
  --dataset training_runs/drone_gpu_50e/calibration/drone_calib_200.txt
```

For the stronger calibration follow-up, replace `--dataset` with one of the pinned public-target lists, for example:

```bash
python tools/int8_rga/convert_onnx_to_rknn.py \
  --onnx training_runs/drone_gpu_50e/weights/best.end2end_false.op12.onnx \
  --output training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.int8.calib500.v220.rknn \
  --dtype int8 \
  --dataset training_runs/drone_gpu_50e/calibration/drone_calib_500_with_public_targets_wsl_nospace.txt
```

The script requires RKNN Toolkit2 in the active Python environment. Use `--dry-run` to validate arguments without importing RKNN Toolkit2.

### Task-book INT8 board validation

For the final thesis evidence chain, the preferred board-side entry is:

```bash
cd /home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_video
scripts/run_taskbook_int8_eval.sh \
  /home/ubuntu/public_videos/anti_uav_fig1.mp4 \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.int8.v220.rknn \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.int8.hybrid_head230.v220.rknn \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_video/artifacts/taskbook_int8_eval
```

This compares the stable FP model, the fully quantized INT8 model, and the manual hybrid INT8 model on the same fixed video. The expected interpretation is conservative: INT8 is considered validated as a quantization and board-side experiment, but it should only replace FP after detection consistency is comparable to the FP baseline.

If full INT8 has no detections at the default confidence threshold, run the threshold sweep:

```bash
cd /home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_video
RK_YOLO_INT8_SWEEP_THRESHOLDS="0.05 0.10 0.20 0.35" \
scripts/run_taskbook_int8_sweep.sh \
  /home/ubuntu/public_videos/anti_uav_fig1.mp4 \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.int8.v220.rknn \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.int8.hybrid_head230.v220.rknn \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_video/artifacts/taskbook_int8_sweep
```

The sweep output `taskbook_int8_sweep_summary.csv` is used to determine whether the full INT8 model is truly unusable or whether quantization mainly shifts the confidence distribution lower.

After converting additional calibration variants, append them without editing the script:

```bash
RK_YOLO_INT8_EXTRA_MODELS="int8_calib500=/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.int8.calib500.v220.rknn int8_calib1000=/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.int8.calib1000.v220.rknn" \
RK_YOLO_INT8_SWEEP_THRESHOLDS="0.05 0.10 0.20 0.35" \
scripts/run_taskbook_int8_sweep.sh ...
```

Current board finding from the 2026-05-01 follow-up: lowering the confidence threshold to `0.05` and rebuilding full INT8 with the 500/1000 pinned calibration lists still produced zero detections on the fixed public UAV clip. Manual hybrid quantization around the sensitive output-head range remains the best INT8 compromise observed so far.

Current board finding from the 2026-05-02 refinement: narrower manual hybrid
models can preserve detections while leaving nearly all of the model quantized.
The strongest task-book-oriented result is currently
`best.end2end_false.op12.rk3588.int8.hybrid_sigmoid500.v220.rknn`, which protects
only the final confidence/output tail from `/model.23/Sigmoid_output_0_rs` to
`output0`. On `anti_uav_fig1.mp4`, it produced `23` frames with detections and
`31` total detections at threshold `0.35`, with mean `rknn_run_ms` around
`30.637 ms`. The same model also passed strict RGA validation with
`RK_YOLO_PREPROCESS=rga_cvt_resize`, `RK_YOLO_RGA_LETTERBOX=1`, and
`RK_YOLO_REQUIRE_RGA=1`. This is currently the closest working compromise to the
task-book INT8 + RGA optimization goal. It is still a hybrid INT8 model, not a
fully quantized replacement for the stable FP RKNN model.

The current strongest task-book-oriented validation is the combined strict path:

```bash
RK_YOLO_PIPELINE=1 RK_YOLO_PIPELINE_STAGED=1 \
RK_YOLO_PREPROCESS=rga_cvt_resize RK_YOLO_RGA_LETTERBOX=1 RK_YOLO_REQUIRE_RGA=1 \
./build/rk_yolo_video public_video.mp4 out.mp4 best.end2end_false.op12.rk3588.int8.hybrid_head230.v220.rknn
```

On `anti_uav_fig1.mp4`, this strict full-RGA + hybrid INT8 path preserved useful detections and reduced average inference time from roughly `147 ms` for FP full-RGA to roughly `46 ms`.

### 3. Generate board-side command matrix

```powershell
python tools/int8_rga/make_board_experiment_commands.py `
  --video /home/ubuntu/eval/public_uav.mp4 `
  --fp-model /home/ubuntu/models/best.end2end_false.op12.rk3588.fp.v220.rknn `
  --int8-model /home/ubuntu/models/best.end2end_false.op12.rk3588.int8.v220.rknn `
  --out-dir /home/ubuntu/eval/int8_rga_runs `
  --output docs/superpowers/specs/2026-04-29-int8-rga-board-command-matrix.md
```

Copy the generated commands to RK3588 and run them in the directory containing `rk_yolo_video`.

For real board-side execution, the one-click script is usually easier:

```bash
chmod +x tools/int8_rga/run_int8_rga_experiments.sh

tools/int8_rga/run_int8_rga_experiments.sh \
  --video /home/ubuntu/eval/public_uav.mp4 \
  --fp-model /home/ubuntu/models/best.end2end_false.op12.rk3588.fp.v220.rknn \
  --int8-model /home/ubuntu/models/best.end2end_false.op12.rk3588.int8.v220.rknn \
  --binary ./rk_yolo_video \
  --out-dir /home/ubuntu/eval/int8_rga_runs
```

Use `--dry-run` first if you only want to inspect the generated commands.

### 4. Summarize profiling logs

```powershell
python tools/int8_rga/summarize_profile_csv.py `
  --logs /path/to/*.log `
  --output eval_runs/int8_rga/profile_summary.csv
```

The summary columns match the `profile_csv` fields printed by `rk_yolo_video`:

- `prepare_ms`
- `input_set_or_update_ms`
- `rknn_run_ms`
- `outputs_get_ms`
- `decode_ms`
- `outputs_release_ms`
- `render_ms`
- `total_work_ms`

## Interpretation Rule

INT8 and RGA should only be promoted to the main deployment path after board-side evidence shows:

- no obvious accuracy regression compared with the FP baseline;
- stable output video and CSV generation;
- lower CPU usage or lower latency in repeated runs;
- no runtime fallback errors in logs.

Until then, the thesis should describe them as optimization experiments, while the stable FP RKNN path remains the main demonstrated deployment result.
