#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build_mpp_dma_experiment}"
MODEL="${MODEL:-/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_hard_v1_ft_640_20e/weights/best.end2end_false.op12.rk3588.fp.v232.hard_v1.rknn}"
DEVICE="${DEVICE:-/dev/video48}"
WIDTH="${WIDTH:-640}"
HEIGHT="${HEIGHT:-480}"
FPS="${FPS:-15}"
CODEC="${CODEC:-h264}"
CONF="${CONF:-0.24}"
NMS="${NMS:-0.45}"
FRAMES="${FRAMES:-300}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" -j"$(nproc)" --target rk_yolo_mpp_dma_demo

"${BUILD_DIR}/rk_yolo_mpp_dma_demo" \
  "${MODEL}" "${DEVICE}" "${WIDTH}" "${HEIGHT}" "${FPS}" "${CODEC}" \
  "${CONF}" "${NMS}" "${FRAMES}"
