# Import Sources

Planned source mix:

1. Public datasets
- Drone-detection-dataset
- Anti-UAV
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
