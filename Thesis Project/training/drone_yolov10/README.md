# Drone YOLOv10 Training Bootstrap

This directory holds the first training entrypoint for the single-class `drone` model.

Current goal:
- train a first working `drone` detector from the public bootstrap dataset
- export `best.pt` to `onnx`
- prepare the result for later RKNN conversion and RK3588 validation

Expected dataset:
- [datasets/drone_single_class/dataset.yaml](../../datasets/drone_single_class/dataset.yaml)

Recommended dependency baseline:
- Python 3.10 to 3.12 is safer than the current local Python 3.14 for ML packages
- `torch`
- `ultralytics`
- `onnx`

Typical next step:

```bash
python train_drone_yolov10.py --model ../../yolov10n.pt --epochs 100 --imgsz 640 --batch 16 --device 0
```

If GPU is not available, use:

```bash
python train_drone_yolov10.py --model ../../yolov10n.pt --epochs 50 --imgsz 640 --batch 8 --device cpu
```

Prediction analysis:

```bash
python analyze_drone_predictions.py --model ../../training_runs/drone_gpu_50e/weights/best.pt --split test --device cpu
```

Notes:
- this script does not touch the current RK3588 runtime path
- dataset images and labels are intentionally kept out of Git tracking
- after training, the next step is `pt -> onnx -> rknn`
