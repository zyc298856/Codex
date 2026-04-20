# Import Sources

Planned source mix:

1. Public datasets
- Kaggle `Drone Object Detection` (`sshikamaru/drone-yolo-detection`) as the first bootstrap source
- Drone-detection-dataset
- Anti-UAV
- UETT4K-Anti-UAV
- optional challenge-style drone-vs-bird data if accessible

2. Your own videos
- RK3588 USB camera recordings
- field test videos from the real deployment scene

3. Hard negatives
- birds
- airplanes
- helicopters
- kites
- balloons
- empty sky
- rooftop clutter and distant dark objects

Suggested raw source organization:

```text
raw_sources/
  public/
  self_captured/
  hard_negatives/
```

Import rule:
- keep original files untouched in `raw_sources/`
- all converted training images and YOLO labels should go into `images/` and `labels/`
- store split manifests in `manifests/`

Current first-source decision:
- use Kaggle `Drone Object Detection` first because it is already YOLO-oriented and single-class friendly
- treat it as the fastest route to the first `pt -> onnx -> rknn` verification loop
- keep heavier or harder-to-convert sources for later quality improvements

Suggested first source landing path:

```text
raw_sources/
  public/
    kaggle_drone_object_detection/
```

Expected import flow for the first source:
- download and extract the Kaggle bundle into `raw_sources/public/kaggle_drone_object_detection/`
- run `scripts/import_kaggle_drone_object_detection.py`
- review the generated manifest in `manifests/`
