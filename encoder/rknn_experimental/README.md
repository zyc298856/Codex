# encoder_yolo_rknn_experimental

This directory is a safe staging area for RK3588 integration work.

What it does:

- builds an experimental static library for the RKNN-based YOLO bridge
- reuses the validated detector from `rk_yolo_video`
- keeps the current Jetson/Eclipse build untouched

What it does not do:

- it does not replace `encoder/yolo/yolov10.cpp`
- it does not modify the current `video_yolo()` call path
- it does not enable on-frame box drawing in the RK bridge yet

Build on RK3588:

```bash
mkdir -p build
cd build
cmake ..
make -j4
```

Smoke test:

```bash
./encoder_yolo_rknn_smoketest /home/ubuntu/test.mp4 5 0.30
```

Environment:

- optional `YOLOV10_RKNN_MODEL=/path/to/yolov10n.rknn`

Current bridge entry points:

- `open_yolo_rknn()`
- `yolo_infer_rknn()`
- `close_yolo_rknn()`

Optional main-pipeline switch:

- compile with `ENABLE_RKNN_YOLO_BRIDGE`
- run with `YOLOV10_BACKEND=rknn`

Without that runtime variable, the existing Jetson `open_yolo()/yolo_infer()/close_yolo()` path stays unchanged.

Input expectation for `yolo_infer_rknn()`:

- host-accessible planar `I420`
- `stride_y` is the luma plane stride

Output behavior:

- pushes ROI JSON into `video_object` using the same `{"pos":[...]}` structure as the legacy object path
- returns the number of detections for the frame
