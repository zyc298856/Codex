# Drone Single-Class Dataset

This dataset scaffold is intentionally isolated from the current RK3588 runtime tools.

Goal:
- train a dedicated single-class `drone` detector
- keep a clean path from raw sources to YOLO training
- avoid touching the already working RTSP and RKNN demo pipeline

Directory layout:

```text
datasets/drone_single_class/
  images/
    train/
    val/
    test/
  labels/
    train/
    val/
    test/
  raw_sources/
  manifests/
  scripts/
  dataset.yaml
  labeling_rules.md
  import_sources.md
```

Recommended data policy:
- `train`: about 70%
- `val`: about 20%
- `test`: about 10%
- keep clips from the same short video segment in only one split

Current class plan:
- class `0`: `drone`

Recommended source priority:
1. public drone-target datasets
2. your own real deployment videos
3. hard negative samples: birds, planes, kites, balloons, glare, distant black dots

Current bootstrap choice:
- primary source: Kaggle `Drone Object Detection` (`sshikamaru/drone-yolo-detection`)
- why this source first:
  - already YOLO-oriented
  - single-class `drone` task fits the current plan
  - includes negative samples
  - better aligned with the "first make the training loop work" goal
- backup sources kept for later:
  - `Drone-detection-dataset`
  - `Anti-UAV`
  - `UETT4K-Anti-UAV`

Next practical step:
- place the extracted Kaggle bundle under `raw_sources/public/kaggle_drone_object_detection/`
- run the source import script to normalize it into the standard `images/` and `labels/` layout
