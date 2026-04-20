# Codex

## 项目简介 | Overview

这个仓库包含两部分内容：一部分是原来面向 Jetson/NVIDIA 平台的旧工程，另一部分是正在进行中的 RK3588 迁移与验证工作，重点围绕基于 YOLOv10 的目标检测。

This repository contains two parts: the original Jetson/NVIDIA-oriented workspace and the ongoing RK3588 migration and validation work, with a focus on YOLOv10-based detection.

当前它还不是一个完全整理好的成品仓库，更像是一个持续推进中的工程工作区。目前主要包含：

This is not a fully polished release repository yet. It is a live engineering workspace that currently includes:

- 原始 Jetson 检测与推流路径：`encoder/`
- RK3588 离线视频检测验证工具：`rk_yolo_video/`
- RK3588 实时 RTSP 检测演示工具：`rk_yolo_live_rtsp/`
- 无人机单类别数据集脚手架：`datasets/drone_single_class/`

- the original Jetson-oriented detection and streaming path: `encoder/`
- a standalone RK3588 offline video validation tool: `rk_yolo_video/`
- a standalone RK3588 live RTSP demo: `rk_yolo_live_rtsp/`
- a single-class drone dataset scaffold: `datasets/drone_single_class/`

## 当前状态 | Current Status

目前已经在 RK3588 上跑通的内容：

What is already working on RK3588:

- `YOLOv10 RKNN + 本地视频输入 + 输出带框视频`
- `USB UVC 摄像头 + RKNN YOLOv10 + RTSP 实时推流`
- 新测试的 `HBS Camera` 摄像头启动自动调焦/变焦参数
- 从 RKNN 检测结果向旧版 encoder ROI 输出格式回接的第一版桥接

- `YOLOv10 RKNN + local video input + boxed output video`
- `USB UVC camera + RKNN YOLOv10 + RTSP live streaming`
- startup tuning for the tested `HBS Camera` UVC module
- a first-pass bridge from RKNN detections back to the legacy encoder-style ROI format

目前还没完成的内容：

What is not finished yet:

- 完整替换原始 Jetson encoder 主流程
- `UNet` 迁移到 RK3588
- 真正面向无人机识别的专用训练模型
- Jetson 平台相关依赖的彻底清理

- full replacement of the original Jetson encoder main pipeline
- `UNet` migration to RK3588
- a dedicated drone-trained detection model
- full cleanup of Jetson-specific dependencies in the legacy tree

## 目录结构 | Repository Layout

- `encoder/`
  原始 Jetson 主工程参考树，仍然保留 CUDA、TensorRT、Jetson multimedia、Live555 等平台相关实现。

- `encoder/`
  The original Jetson reference tree. It still contains CUDA, TensorRT, Jetson multimedia, and Live555-related platform code.

- `rk_yolo_video/`
  RK3588 最小离线验证路径，用于本地视频输入、RKNN 推理和输出带框视频。

- `rk_yolo_video/`
  The minimal RK3588 offline validation path for local video input, RKNN inference, and boxed output video.

- `rk_yolo_live_rtsp/`
  RK3588 最小实时演示路径，用于 USB 摄像头输入、实时检测、叠框和 RTSP 推流。

- `rk_yolo_live_rtsp/`
  The minimal RK3588 live demo path for USB camera input, real-time detection, overlay drawing, and RTSP streaming.

- `datasets/drone_single_class/`
  独立的数据集脚手架，用于后续训练单类别 `drone` 检测模型，不会影响当前已经跑通的运行链。

- `datasets/drone_single_class/`
  An isolated dataset scaffold for future single-class `drone` training, kept separate from the current working runtime path.

- `docs/superpowers/specs/`
  迁移和数据集设计过程中记录下来的工作规格说明和设计笔记。

- `docs/superpowers/specs/`
  Working specs and design notes written during the migration and dataset-planning process.

## 最快可运行路径 | Fastest Working Paths

### 1. RK3588 离线视频验证 | Offline RK3588 Validation

说明见：

See:

- [rk_yolo_video/README.md](rk_yolo_video/README.md)

这一条路径适合：

This path is meant for:

- 本地视频文件输入
- 验证 RKNN 推理是否正常
- 输出带检测框的新视频文件

- local video file input
- RKNN inference validation
- generation of a new video file with detection boxes

### 2. RK3588 实时 RTSP 演示 | Live RK3588 RTSP Demo

说明见：

See:

- [rk_yolo_live_rtsp/README.md](rk_yolo_live_rtsp/README.md)

这一条路径适合：

This path is meant for:

- USB UVC 摄像头输入
- 实时 RKNN 检测
- 视频画面叠框
- RTSP 推流到 PC 端播放器，例如 VLC

- USB UVC camera input
- real-time RKNN detection
- box overlay on live frames
- RTSP streaming to a PC player such as VLC

## 模型文件 | Model Files

当前工作区保留了几种模型形式，方便迁移、对照和重新生成：

The workspace currently keeps several model forms for migration, comparison, and regeneration:

- `yolov10n.pt`
- `yolov10n.onnx`
- `yolov10n.rknn`
- `yolov10n.wsl.rk3588.fp.rknn`
- `yolov10n.512.onnx`
- `yolov10n.512.rk3588.fp.rknn`

实际在 RK3588 上运行时，主要使用 `.rknn` 模型；`.pt` 和 `.onnx` 主要作为上游重导和重转换的来源。

In actual RK3588 runs, the `.rknn` models are the primary runtime artifacts, while the `.pt` and `.onnx` files are kept as upstream regeneration sources.

## 数据集方向 | Dataset Direction

后续提升无人机识别精度的主要方向，是从通用 COCO 检测模型逐步转向“无人机专用训练模型”。

The main direction for improving drone-recognition accuracy is to move away from generic COCO-only detection and toward a dedicated drone-trained model.

相关说明见：

See:

- [datasets/drone_single_class/README.md](datasets/drone_single_class/README.md)
- [docs/superpowers/specs/2026-04-14-drone-dataset-design.md](docs/superpowers/specs/2026-04-14-drone-dataset-design.md)

数据集目录是刻意独立出来的，这样数据整理和训练准备不会破坏当前已经跑通的 RK3588 演示路径。

The dataset tree is intentionally isolated so that data preparation and training work do not break the current known-good RK3588 demo paths.

## 说明 | Notes

- 仓库里仍然保留了不少 Jetson 相关旧代码，作为迁移参考。
- Git 已忽略构建产物、大型原始数据集下载内容以及 RTSP/视频测试产物。
- 当前仓库更接近“迁移进行中的工作区”，而不是最终清理完毕的发布分支。

- The repository still retains a large amount of Jetson-specific legacy code as migration reference material.
- Build outputs, large raw dataset downloads, and RTSP/video test artifacts are intentionally ignored by Git.
- The current repository reflects an in-progress migration workspace rather than a final cleaned release branch.
