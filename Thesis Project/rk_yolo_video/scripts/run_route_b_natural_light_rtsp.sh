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
DETECT_EVERY_N="${DETECT_EVERY_N:-3}"
OUTPUT_MODE="${OUTPUT_MODE:-bgr}"
PORT="${PORT:-8562}"
MOUNT="${MOUNT:-/yolo_mpp}"
RUN_SECONDS="${RUN_SECONDS:-0}"

# Natural-light live preset. This keeps the exploratory Route-B chain enabled,
# while avoiding the thermal-video OSD filter and multi-box flicker.
export RK_YOLO_ROUTE_B_STABLE="${RK_YOLO_ROUTE_B_STABLE:-1}"
export RK_YOLO_VIS_MULTI="${RK_YOLO_VIS_MULTI:-0}"
export RK_YOLO_IGNORE_OSD="${RK_YOLO_IGNORE_OSD:-0}"
export RK_YOLO_ASYNC_INFER="${RK_YOLO_ASYNC_INFER:-0}"
export RK_YOLO_ASYNC_POOL="${RK_YOLO_ASYNC_POOL:-3}"
# x264 is used only for the experimental Route-B visualization stream because
# it is more tolerant in VLC than the current mpph264enc path.
export RK_YOLO_RTSP_ENCODER="${RK_YOLO_RTSP_ENCODER:-x264}"

echo "Starting natural-light Route-B live preset"
echo "RTSP URL: rtsp://<board-ip>:${PORT}${MOUNT}"
echo "Input: ${DEVICE} ${WIDTH}x${HEIGHT}@${FPS} codec=${CODEC}"
echo "Policy: conf=${CONF}, nms=${NMS}, detect_every_n=${DETECT_EVERY_N}, output_mode=${OUTPUT_MODE}"
echo "Stability: route_b_stable=${RK_YOLO_ROUTE_B_STABLE}, visual_multi=${RK_YOLO_VIS_MULTI}, ignore_osd=${RK_YOLO_IGNORE_OSD}"
echo "RTSP encoder: ${RK_YOLO_RTSP_ENCODER}"

MODEL="${MODEL}" \
DEVICE="${DEVICE}" \
WIDTH="${WIDTH}" \
HEIGHT="${HEIGHT}" \
FPS="${FPS}" \
CODEC="${CODEC}" \
CONF="${CONF}" \
NMS="${NMS}" \
DETECT_EVERY_N="${DETECT_EVERY_N}" \
OUTPUT_MODE="${OUTPUT_MODE}" \
PORT="${PORT}" \
MOUNT="${MOUNT}" \
RUN_SECONDS="${RUN_SECONDS}" \
ASYNC_INFER="${RK_YOLO_ASYNC_INFER}" \
ASYNC_POOL="${RK_YOLO_ASYNC_POOL}" \
  "${SCRIPT_DIR}/run_mpp_dma_rtsp_eval.sh"
