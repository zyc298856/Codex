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

The script requires RKNN Toolkit2 in the active Python environment. Use `--dry-run` to validate arguments without importing RKNN Toolkit2.

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
