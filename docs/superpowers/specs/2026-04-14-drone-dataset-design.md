# Drone Dataset Design

Objective:
- build a dedicated single-class `drone` dataset without touching the already working RK3588 runtime path

First-stage design:
- create an isolated dataset workspace under `datasets/drone_single_class`
- start with a single class, `drone`
- use public drone-target datasets plus self-captured data later
- keep raw sources separate from processed YOLO training data

Structure:
- `raw_sources/` keeps original downloads and videos
- `images/{train,val,test}` keeps training images
- `labels/{train,val,test}` keeps YOLO label files
- `manifests/` stores split metadata

Why single-class first:
- the current deployment goal is precise drone recognition
- the existing COCO model does not contain a reliable drone class
- single-class training reduces annotation complexity and shortens the first iteration

Risk control:
- no changes to `rk_yolo_live_rtsp`
- no changes to `rk_yolo_video`
- dataset work happens in a separate tree and can be iterated independently

Next step:
- import the first real public dataset into `raw_sources/`
- then build a source-specific conversion and split script
