# Public Video Evaluation Bootstrap Summary

## Scope

This note records the first end-to-end public-video evaluation pass added to the RK3588 anti-UAV workflow. The goal is not to replace real flight-scene validation, but to give the project a repeatable fixed-input layer when real drone footage is unavailable.

The bootstrap source used here is:

- `anti-uav-official-gifs`

This source was intentionally chosen first because it is small, directly downloadable, and sufficient to validate the new import, manifest, PC evaluation, and RK3588 replay paths before spending more time on larger archives such as `DUT-Anti-UAV`.

The public-video layer now also includes a longer source:

- `dut-anti-uav-tracking`

## What Was Implemented

The following pieces are now in place:

- a public-video import script:
  [`import_public_uav_videos.py`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/datasets/drone_single_class/scripts/import_public_uav_videos.py)
- a fixed-input evaluation manifest:
  [`public_video_eval_manifest.json`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/datasets/drone_single_class/manifests/public_video_eval_manifest.json)
- a PC/WSL evaluation script for the current drone model:
  [`evaluate_public_videos.py`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/training/drone_yolov10/evaluate_public_videos.py)
- a local evaluation output root:
  [`eval_runs/public_videos`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos)

## PC/WSL Public-Video Result

The current drone-specific `best.pt` model was evaluated on:

- `anti_uav_fig1.mp4`

Output directory:

- [`anti-uav-official-gifs__anti_uav_fig1`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos/anti-uav-official-gifs__anti_uav_fig1)

Summary:

- `frame_count = 130`
- `frames_with_detections = 16`
- `total_detections = 18`
- `max_score = 0.6216`
- `conf = 0.35`
- `imgsz = 640`

This confirms that the drone-specific model is at least detecting target-like content on a new public anti-UAV video source in the PC/WSL path.

## RK3588 Fixed-Input RTSP Replay Result

The live RTSP tool was extended to accept a local video file as input and replay it at the requested FPS. This enables repeatable system-level experiments under the same pipeline used for live streaming.

Replay input:

- `/home/ubuntu/public_videos/anti_uav_fig1.mp4`

Metrics table:

- [`public_video_fixed_input_metrics.csv`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_live_rtsp/artifacts/experiments/public_video_fixed_input_metrics.csv)

Key results:

- `FIX-BASE`: `stream_fps = 6.49`, `npu_fps = 6.49`, `end_to_end = 239.87 ms`
- `FIX-MCTX`: `stream_fps = 12.95`, `npu_fps = 12.95`, `end_to_end = 426.05 ms`
- `FIX-POL2`: `stream_fps = 10.96`, `npu_fps = 6.48`, `end_to_end = 187.91 ms`
- `FIX-POL3`: `stream_fps = 9.60`, `npu_fps = 4.57`, `end_to_end = 104.78 ms`

Interpretation:

- multi-context replay again delivers the highest throughput
- policy optimization with `detect_every_n = 3` again delivers the best latency balance
- the fixed-input replay results are therefore consistent with the previous live-camera conclusion: throughput and latency move in opposite directions, and the practical real-time choice is not the same as the maximum-throughput choice

## RK3588 Offline Fixed-Video Validation

Board-side offline validation was also checked through:

- [`rk_yolo_video`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_video/README.md)

### Known-good comparison run

Using the existing generic model:

- `model = ../../yolov10n.rknn`
- `video = anti_uav_fig1.mp4`

Result:

- `frames = 130`
- `frames_with_detections = 1`
- `total_detections = 1`
- `avg_infer_ms = 137.94`

Artifacts pulled back locally:

- [`anti_uav_fig1_base_eval.mp4`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos/rk3588_board/anti_uav_fig1_base_eval.mp4)
- [`anti_uav_fig1_base_eval.csv`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos/rk3588_board/anti_uav_fig1_base_eval.csv)
- [`anti_uav_fig1_base_eval.roi.jsonl`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos/rk3588_board/anti_uav_fig1_base_eval.roi.jsonl)

This confirms that the public video itself is readable and the board-side offline validator still works on the same input.

### Drone-specific RKNN board recovery

The original drone-specific board model:

- `model = ../../training_runs/drone_gpu_50e/weights/best.rk3588.fp.rknn`

previously failed with:

- process exits with `status = 139`
- `gdb` shows a `SIGSEGV` inside `librknnrt.so`
- the crash happens during `rknn_run()`, before the first per-frame result is printed

This was not a video I/O problem and not a general `rk_yolo_video` failure. The crash was traced to the exported end-to-end model path.

The recovery path that now works on the board is:

- re-export the trained drone model with `end2end = False`
- convert that ONNX to RKNN using the existing 2.2.0 conversion path
- extend the shared RKNN decoder so it recognizes a single-class raw head with `dims = [1, 5, 8400]`

Recovered board model:

- `model = ../../training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn`

Recovered result on the same public video:

- `frames = 130`
- `frames_with_detections = 30`
- `total_detections = 30`
- `avg_infer_ms = 109.43`
- `score range = 0.3501 ~ 0.6548`
- `mean score = 0.4762`

Recovered artifacts pulled back locally:

- [`anti_uav_fig1_drone_eval_v3.mp4`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos/rk3588_board/anti_uav_fig1_drone_eval_v3.mp4)
- [`anti_uav_fig1_drone_eval_v3.csv`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos/rk3588_board/anti_uav_fig1_drone_eval_v3.csv)
- [`anti_uav_fig1_drone_eval_v3.roi.jsonl`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos/rk3588_board/anti_uav_fig1_drone_eval_v3.roi.jsonl)

The board-side offline validator is therefore no longer blocked by `rknn_run()` for the drone-specific model. The remaining work is no longer runtime rescue, but task-level validation and later integration into the live pipeline.

## DUT-Anti-UAV Long-Sequence Extension

To move beyond short demo clips, the `DUT-Anti-UAV` tracking archive was downloaded and screened for longer RGB sequences.

Downloaded archive:

- [`Anti-UAV-Tracking-V0.zip`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/datasets/drone_single_class/raw_sources/public_videos/dut-anti-uav/Anti-UAV-Tracking-V0.zip)

Two longer sequences were extracted and converted to MP4:

- [`video01.mp4`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/datasets/drone_single_class/raw_sources/public_videos/dut-anti-uav/videos/video01.mp4)
- [`video11.mp4`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/datasets/drone_single_class/raw_sources/public_videos/dut-anti-uav/videos/video11.mp4)

Quick local screening with the current drone-specific `best.pt` model at `conf = 0.35` over the first `400` frames gave:

- `video01`: `258` frames with detections, `283` total detections, `max_score = 0.9124`
- `video11`: `159` frames with detections, `159` total detections, `max_score = 0.7842`

This made `video01` the best candidate for board-side validation because it is:

- much longer than the Anti-UAV demo clips
- clearly positive under the current model
- more suitable for later thesis figures and repeatable evaluation

## RK3588 Offline Validation on a Longer Public Video

The recovered drone-specific board model was then validated on:

- `/home/ubuntu/public_videos/video01.mp4`

using:

- `model = ../../training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn`
- `score threshold = 0.35`
- `nms threshold = 0.45`

Board-side result:

- `frames = 1050`
- `frames_with_detections = 846`
- `total_detections = 846`
- `avg_infer_ms = 67.61`

Artifacts pulled back locally:

- [`video01_drone_eval_v1.mp4`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos/rk3588_board/video01_drone_eval_v1.mp4)
- [`video01_drone_eval_v1.csv`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos/rk3588_board/video01_drone_eval_v1.csv)
- [`video01_drone_eval_v1.roi.jsonl`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos/rk3588_board/video01_drone_eval_v1.roi.jsonl)

This is a stronger milestone than the earlier short demo result because it shows that the board-side drone-specific model can:

- run stably on a longer public UAV sequence
- keep producing detections across most of the clip
- provide a realistic longer-form qualitative video for advisor review, thesis figures, and weekly logs

## Current Practical Conclusion

At this stage, the public-video layer is already useful in three ways:

- it gives the project a repeatable fixed-input benchmark for comparing `Baseline`, `Multi-Context`, and policy configurations under the RTSP pipeline
- it provides a new-task validation source for the drone-specific model in PC/WSL
- it now also gives the project a board-side offline validation path for the recovered drone-specific RKNN model
- it now includes a longer public UAV sequence that can be reused as a higher-value qualitative and quantitative validation sample

## Next Steps

1. Extend screening beyond `video01` and `video11` to decide whether another `DUT-Anti-UAV` sequence offers a better task-level challenge set.
2. Compare the recovered board-side drone result on `video01` directly with the PC/WSL result and summarize any precision / recall gap.
3. Decide whether the recovered drone RKNN should be promoted into the live RTSP path for the next round of task-level testing.
