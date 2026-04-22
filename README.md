# Codex

This repository hosts my undergraduate thesis project workspace for:

**Research on an Embedded-Platform Object Detection System**

The main project lives inside:

- [`Thesis Project/`](./Thesis%20Project/)

## What This Repository Contains

The repository currently combines:

- a legacy Jetson/NVIDIA-oriented codebase kept as migration reference
- an RK3588 deployment and validation workspace
- C++ tools for offline video validation and live RTSP detection
- a drone-specific dataset / training / RKNN conversion workflow
- experiment notes and thesis-related technical documentation

## Current Focus

The current work centers on:

- deploying detection models on **RK3588**
- building a **C++ multithreaded real-time detection pipeline**
- studying **throughput-latency trade-offs** under RK3588 NPU multithreading / multi-context inference
- preparing a **drone-specific detection model** for embedded deployment

## Entry Points

- Main project overview:
  [`Thesis Project/README.md`](./Thesis%20Project/README.md)
- Offline RK3588 validation tool:
  [`Thesis Project/rk_yolo_video/README.md`](./Thesis%20Project/rk_yolo_video/README.md)
- Live RK3588 RTSP demo:
  [`Thesis Project/rk_yolo_live_rtsp/README.md`](./Thesis%20Project/rk_yolo_live_rtsp/README.md)
- Drone dataset / training scaffold:
  [`Thesis Project/datasets/drone_single_class/README.md`](./Thesis%20Project/datasets/drone_single_class/README.md)

## Repository Status

This is an active engineering workspace rather than a polished release repository.

What is already available:

- RK3588 live detection and RTSP streaming
- first-round NPU multithreading experiments
- public-video fixed-input evaluation
- drone-model training and ONNX / RKNN conversion workflow

What is still being improved:

- board-side compatibility for the latest drone-specific RKNN model
- closed-loop GPIO / alarm integration
- longer stability testing
- deeper deployment optimization such as INT8 quantization and RGA-first preprocessing
