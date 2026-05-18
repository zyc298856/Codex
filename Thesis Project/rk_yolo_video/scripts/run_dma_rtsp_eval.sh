#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

MODEL="${MODEL:-/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_hard_v1_ft_640_20e/weights/best.end2end_false.op12.rk3588.fp.v232.hard_v1.rknn}"
DEVICE="${DEVICE:-/dev/video48}"
WIDTH="${WIDTH:-640}"
HEIGHT="${HEIGHT:-480}"
FPS="${FPS:-15}"
CONF="${CONF:-0.24}"
NMS="${NMS:-0.45}"
PORT="${PORT:-8561}"
MOUNT="${MOUNT:-/yolo_dma}"
BUILD_DIR="${BUILD_DIR:-${PROJECT_DIR}/build_dma_rtsp_experiment}"
RKNN_INCLUDE_DIR="${RKNN_INCLUDE_DIR:-/home/ubuntu/eclipse-workspace/eclipse-workspace/encoder/include}"
RKNN_LIBRARY="${RKNN_LIBRARY:-/usr/lib/librknnrt.so}"
RUN_SECONDS="${RUN_SECONDS:-0}"

cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" \
  -DRKNN_INCLUDE_DIR="${RKNN_INCLUDE_DIR}" \
  -DRKNN_LIBRARY="${RKNN_LIBRARY}"
cmake --build "${BUILD_DIR}" --target rk_yolo_dma_rtsp_demo -j"$(nproc)"

echo "Starting aggressive DMA/RGA/RKNN RTSP demo"
echo "RTSP URL: rtsp://<board-ip>:${PORT}${MOUNT}"
echo "Path: V4L2 YUYV DMA fd -> RGA letterbox -> RKNN input memory -> NPU -> RTSP"
echo "Display publishing still uses BGR appsrc for safe visual validation."

CMD=(
  "${BUILD_DIR}/rk_yolo_dma_rtsp_demo"
  "${MODEL}"
  "${DEVICE}"
  "${WIDTH}"
  "${HEIGHT}"
  "${FPS}"
  "${CONF}"
  "${NMS}"
  "${PORT}"
  "${MOUNT}"
)

if [[ "${RUN_SECONDS}" != "0" ]]; then
  timeout "${RUN_SECONDS}s" "${CMD[@]}"
else
  "${CMD[@]}"
fi
