# drone_single_class_hard_v2

This dataset is generated from `drone_single_class` without modifying the
original dataset. It is intended for a controlled fine-tuning experiment that
targets three weak cases observed during the demo:

- near_zoom: close-up or digitally zoomed drone appearance.
- motion_blur: fast camera/object motion causing blurred UAV contours.
- edge: drone near or partially clipped by the image border.

Generation settings:

- seed: `20260516`
- near_zoom: `320`
- motion_blur: `260`
- edge: `260`
- base dataset: `C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis Project/datasets/drone_single_class`

Training entry example:

```powershell
& "C:/Users/Tony/Desktop/eclipse-workspace-codex/.venv_pc/Scripts/python.exe" "C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis Project/training/drone_yolov10/train_drone_yolov10.py" `
  --model "C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis Project/training_runs/drone_gpu_50e/weights/best.pt" `
  --data "C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis Project/datasets/drone_single_class_hard_v2/dataset.yaml" `
  --name drone_single_class_hard_v2_ft `
  --epochs 30 --imgsz 640 --batch 8 --device cpu
```

Review `preview/contact_sheet.jpg` before formal training. The original stable
model and original dataset remain unchanged.
