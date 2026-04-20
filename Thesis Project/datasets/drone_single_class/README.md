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

Next practical step:
- place downloaded or extracted images/videos into `raw_sources/`
- then we can write the import and split script for your exact source format
