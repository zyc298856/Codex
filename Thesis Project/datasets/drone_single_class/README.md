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

Public video evaluation layer:
- raw public evaluation sources now live under `raw_sources/public_videos/`
- the canonical manifest for fixed-input evaluation is:
  - `manifests/public_video_eval_manifest.json`
- use `scripts/import_public_uav_videos.py` to:
  - download official public anti-UAV archives
  - extract them into the local raw-source area
  - reconstruct mp4 videos when the source is released as RGB image sequences
  - register all generated videos into the shared evaluation manifest

Recommended first evaluation source:
1. `dut-anti-uav-tracking`
2. `anti-uav300`

Typical usage from the WSL training environment:

```bash
python scripts/import_public_uav_videos.py --source dut-anti-uav-tracking --download --max-sequences 3
```

Next practical step:
- place the extracted Kaggle bundle under `raw_sources/public/kaggle_drone_object_detection/`
- run the source import script to normalize it into the standard `images/` and `labels/` layout
