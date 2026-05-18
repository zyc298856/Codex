#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

MODEL="${MODEL:-/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_hard_v1_ft_640_20e/weights/best.end2end_false.op12.rk3588.fp.v232.hard_v1.rknn}"
DEVICE="${DEVICE:-/dev/video48}"
WIDTH="${WIDTH:-640}"
HEIGHT="${HEIGHT:-480}"
FPS="${FPS:-15}"
CODEC="${CODEC:-h264}"
CONF="${CONF:-0.24}"
NMS="${NMS:-0.45}"
FRAMES="${FRAMES:-0}"
PORT="${PORT:-8562}"
MOUNT="${MOUNT:-/yolo_mpp}"
DETECT_EVERY_N="${DETECT_EVERY_N:-3}"
OUTPUT_MODE="${OUTPUT_MODE:-bgr}"
ASYNC_INFER="${ASYNC_INFER:-0}"
ASYNC_POOL="${ASYNC_POOL:-3}"
BUILD_DIR="${BUILD_DIR:-${PROJECT_DIR}/build_mpp_dma_rtsp_experiment}"
RKNN_INCLUDE_DIR="${RKNN_INCLUDE_DIR:-/home/ubuntu/eclipse-workspace/eclipse-workspace/encoder/include}"
RKNN_LIBRARY="${RKNN_LIBRARY:-/usr/lib/librknnrt.so}"
RUN_SECONDS="${RUN_SECONDS:-0}"

cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DRKNN_INCLUDE_DIR="${RKNN_INCLUDE_DIR}" \
  -DRKNN_LIBRARY="${RKNN_LIBRARY}"
cmake --build "${BUILD_DIR}" --target rk_yolo_mpp_dma_rtsp_demo -j"$(nproc)"

echo "Starting production-candidate MPP DMA/RGA/RKNN RTSP demo"
echo "RTSP URL: rtsp://<board-ip>:${PORT}${MOUNT}"
echo "Inference path: V4L2 compressed -> MPP decode -> MppFrame DMA fd -> RGA letterbox -> RKNN input memory -> NPU"
echo "RTSP output path: output_mode=${OUTPUT_MODE} (bgr=boxed visualization, dmabuf=NV12 DMA performance stream)"
echo "Realtime policy: detect_every_n=${DETECT_EVERY_N}"
echo "Async policy: RK_YOLO_ASYNC_INFER=${ASYNC_INFER} (1=decouple NPU from decode/output)"
echo "Async DMA pool: RK_YOLO_ASYNC_POOL=${ASYNC_POOL}"
export RK_YOLO_ASYNC_INFER="${ASYNC_INFER}"
export RK_YOLO_ASYNC_POOL="${ASYNC_POOL}"

CMD=(
  "${BUILD_DIR}/rk_yolo_mpp_dma_rtsp_demo"
  "${MODEL}"
  "${DEVICE}"
  "${WIDTH}"
  "${HEIGHT}"
  "${FPS}"
  "${CODEC}"
  "${CONF}"
  "${NMS}"
  "${FRAMES}"
  "${PORT}"
  "${MOUNT}"
  "${DETECT_EVERY_N}"
  "${OUTPUT_MODE}"
)

if [[ "${RUN_SECONDS}" != "0" ]]; then
  timeout "${RUN_SECONDS}s" "${CMD[@]}"
else
  "${CMD[@]}"
fi
