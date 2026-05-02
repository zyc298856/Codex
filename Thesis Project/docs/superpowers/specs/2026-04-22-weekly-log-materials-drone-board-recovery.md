# Weekly Log Materials: Drone Model Board Recovery and Long Public-Video Milestone

## Purpose

This note records the recent milestone in a format that can be reused for:

- weekly reports
- advisor updates
- thesis writing
- later experiment and result chapters

It focuses on two linked achievements:

1. the drone-specific RKNN model no longer crashes in the board-side offline validator
2. the recovered drone-specific model now produces stable detections on a longer public UAV video

## Short Summary

The project has moved past the earlier blocker where the drone-specific RKNN model crashed inside `rknn_run()` on the RK3588 board. By switching from the original end-to-end export path to a recovered `end2end = False` export path, reconverting the ONNX model, and extending the shared decoder to support a single-class `1x5x8400` output head, the drone-specific model can now run stably in the board-side offline validator. On the longer `DUT-Anti-UAV` public sequence `video01`, the recovered model completed all `1050` frames and produced detections in `846` frames with an average inference time of `67.61 ms`.

## What Was Done

### 1. Recovered the board-side drone-specific RKNN path

Earlier, the original drone-specific board model:

- [`best.rk3588.fp.rknn`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/training_runs/drone_gpu_50e/weights/best.rk3588.fp.rknn)

failed inside `librknnrt.so` during `rknn_run()` when used by:

- [`rk_yolo_video`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_video)

The recovery path was:

- re-export the trained drone model as a non-end-to-end ONNX
- reconvert the ONNX file into a new RK3588 RKNN model
- extend the shared RKNN decoder to recognize a single-class raw output head with shape `dims = [1, 5, 8400]`

Recovered board model:

- [`best.end2end_false.op12.rk3588.fp.v220.rknn`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn)

Relevant implementation files:

- [`yolo_rknn.cpp`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_video/src/yolo_rknn.cpp)
- [`yolo_rknn.h`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_video/include/yolo_rknn.h)
- [`main.cpp`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_video/src/main.cpp)

### 2. Confirmed that the recovered model works on board

The recovered model was first validated on the shorter public clip:

- [`anti_uav_fig1_drone_eval_v3.mp4`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos/rk3588_board/anti_uav_fig1_drone_eval_v3.mp4)

This confirmed that the board-side drone-specific path was no longer blocked by `rknn_run()`.

### 3. Extended the public-video layer to longer DUT-Anti-UAV sequences

To move beyond short official demo clips, the `DUT-Anti-UAV` tracking archive was downloaded and screened. Two candidate long sequences were extracted and converted into MP4:

- [`video01.mp4`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/datasets/drone_single_class/raw_sources/public_videos/dut-anti-uav/videos/video01.mp4)
- [`video11.mp4`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/datasets/drone_single_class/raw_sources/public_videos/dut-anti-uav/videos/video11.mp4)

### 4. Screened candidates locally before board deployment

The current drone-specific `best.pt` model was used to screen the first `400` frames of the two candidate sequences at `conf = 0.35`.

Local screening result:

- `video01`: `258` frames with detections, `283` total detections, `max_score = 0.9124`
- `video11`: `159` frames with detections, `159` total detections, `max_score = 0.7842`

This made `video01` the preferred candidate for board-side validation.

## Board-Side Long-Video Result

The recovered drone-specific board model was then tested on:

- `/home/ubuntu/public_videos/video01.mp4`

Board result:

- `frames = 1050`
- `frames_with_detections = 846`
- `total_detections = 846`
- `avg_infer_ms = 67.61`

Result artifacts:

- [`video01_drone_eval_v1.mp4`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos/rk3588_board/video01_drone_eval_v1.mp4)
- [`video01_drone_eval_v1.csv`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos/rk3588_board/video01_drone_eval_v1.csv)
- [`video01_drone_eval_v1.roi.jsonl`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos/rk3588_board/video01_drone_eval_v1.roi.jsonl)

## Why This Milestone Matters

This milestone is important because it shows that the project is no longer limited to:

- training a drone model on PC
- exporting an RKNN file
- hoping it will run on the board

Instead, the current project now has:

- a self-trained drone-specific model
- a recovered RKNN path that runs on RK3588
- board-side offline validation on a longer public UAV sequence
- stable qualitative output that can be shown to an advisor and later cited in the thesis

This makes the remaining gap much narrower. The project is no longer blocked by board-side runtime rescue. The next technical step is to integrate the recovered drone-specific model into the live RTSP path.

## Suggested Weekly Report Wording

This week, the board-side runtime problem of the drone-specific RKNN model was resolved. By replacing the previous end-to-end export path with a non-end-to-end ONNX export and updating the decoder to support a single-class raw output head, the new drone-specific RKNN model was able to run stably in the RK3588 offline validator. On the basis of this recovery, a longer public UAV sequence from DUT-Anti-UAV was introduced for further validation. After local screening of candidate sequences, `video01` was selected as the most suitable test video. The recovered board-side drone model completed all `1050` frames of this sequence and produced detections in `846` frames, with an average inference time of `67.61 ms`. This result shows that the project has moved beyond simple deployment bring-up and has begun to obtain stable task-level results on the embedded platform.

## Suggested Thesis Wording

To verify that the recovered drone-specific RKNN model was not only syntactically convertible but also practically deployable, a longer public UAV sequence from the DUT-Anti-UAV tracking dataset was introduced for board-side validation. After initial screening with the PyTorch model, `video01` was selected as a representative long sequence with stable positive detections. The recovered RK3588 model completed inference on all `1050` frames and generated detections in `846` frames, indicating that the drone-specific model had progressed from deployment compatibility repair to stable task-level execution on the embedded platform.

## Next Step

The most valuable next step is:

1. integrate the recovered drone-specific RKNN model into the live RTSP path
2. compare its live behavior against the current generic model
3. continue preparing longer public-video evaluation assets for later thesis figures and result analysis
