#!/usr/bin/env bash
set -euo pipefail

# Reproducible board-side experiment for the low-copy input path:
# V4L2 YUYV DMA fd -> RGA -> RKNN input memory -> NPU.
#
# This script is intentionally isolated from the stable RTSP demo. It builds and
# runs only rk_yolo_dma_demo, so it cannot change the known-good live path.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

MODEL="${MODEL:-${1:-${PROJECT_DIR}/../../training_runs/drone_hard_v1_ft_640_20e/weights/best.end2end_false.op12.rk3588.fp.v232.hard_v1.rknn}}"
DEVICE="${DEVICE:-${2:-/dev/video48}}"
WIDTH="${WIDTH:-640}"
HEIGHT="${HEIGHT:-480}"
FPS="${FPS:-30}"
CONF="${CONF:-0.24}"
NMS="${NMS:-0.45}"
FRAMES="${FRAMES:-${3:-300}}"
VISUAL_FRAMES="${VISUAL_FRAMES:-120}"
WRITE_VIDEO="${WRITE_VIDEO:-0}"
OUT_DIR="${OUT_DIR:-${HOME}/dma_rga_rknn_eval_$(date +%Y%m%d_%H%M%S)}"
BUILD_DIR="${BUILD_DIR:-${PROJECT_DIR}/build_dma_experiment}"

if [[ ! -f "${MODEL}" ]]; then
  echo "model not found: ${MODEL}" >&2
  exit 1
fi

if [[ ! -e "${DEVICE}" ]]; then
  echo "video device not found: ${DEVICE}" >&2
  exit 1
fi

if [[ -d "${PROJECT_DIR}/../../encoder/include" ]]; then
  DEFAULT_RKNN_INCLUDE="${PROJECT_DIR}/../../encoder/include"
else
  DEFAULT_RKNN_INCLUDE="${PROJECT_DIR}/../encoder/include"
fi

RKNN_INCLUDE_DIR="${RKNN_INCLUDE_DIR:-${DEFAULT_RKNN_INCLUDE}}"
RKNN_LIBRARY="${RKNN_LIBRARY:-/usr/lib/librknnrt.so}"

mkdir -p "${OUT_DIR}"

echo "project: ${PROJECT_DIR}"
echo "model: ${MODEL}"
echo "device: ${DEVICE}"
echo "output: ${OUT_DIR}"
echo "build: ${BUILD_DIR}"

cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" \
  -DRKNN_INCLUDE_DIR="${RKNN_INCLUDE_DIR}" \
  -DRKNN_LIBRARY="${RKNN_LIBRARY}"

cmake --build "${BUILD_DIR}" --target rk_yolo_dma_demo -j2

LOG_PATH="${OUT_DIR}/dma_${FRAMES}.log"

"${BUILD_DIR}/rk_yolo_dma_demo" \
  "${MODEL}" "${DEVICE}" "${WIDTH}" "${HEIGHT}" "${FPS}" "${CONF}" "${NMS}" "${FRAMES}" \
  > "${LOG_PATH}" 2>&1

echo "main log: ${LOG_PATH}"
grep -E "^(DMA fd|summary|avg_|camera opened|rga_api)" "${LOG_PATH}" || true

if [[ "${WRITE_VIDEO}" != "0" ]]; then
  VISUAL_LOG="${OUT_DIR}/dma_visual_${VISUAL_FRAMES}.log"
  VISUAL_MP4="${OUT_DIR}/dma_visual_${VISUAL_FRAMES}.mp4"

  "${BUILD_DIR}/rk_yolo_dma_demo" \
    "${MODEL}" "${DEVICE}" "${WIDTH}" "${HEIGHT}" "${FPS}" "${CONF}" "${NMS}" "${VISUAL_FRAMES}" "${VISUAL_MP4}" \
    > "${VISUAL_LOG}" 2>&1

  echo "visual log: ${VISUAL_LOG}"
  echo "visual mp4: ${VISUAL_MP4}"
  grep -E "^(summary|avg_|wrote output_video)" "${VISUAL_LOG}" || true
fi

echo "done"
