#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

# One-click Route-B camera launcher.
# Stable thesis/demo programs are intentionally left untouched.

MODEL="${MODEL:-/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_hard_v1_ft_640_20e/weights/best.end2end_false.op12.rk3588.fp.v232.hard_v1.rknn}"
DEVICE="${DEVICE:-/dev/video48}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FPS="${FPS:-20}"
CODEC="${CODEC:-h264}"
CONF="${CONF:-0.24}"
NMS="${NMS:-0.45}"
DETECT_EVERY_N="${DETECT_EVERY_N:-3}"
OUTPUT_MODE="${OUTPUT_MODE:-bgr}"
PORT="${PORT:-8564}"
MOUNT="${MOUNT:-/yolo_routeb_cam}"
RUN_SECONDS="${RUN_SECONDS:-0}"
BUILD_DIR="${BUILD_DIR:-${PROJECT_DIR}/build_mpp_dma_rtsp_experiment}"
RUN_ID="${RUN_ID:-route_b_camera_live_$(date +%Y%m%d_%H%M%S)}"
OUT_DIR="${OUT_DIR:-${PROJECT_DIR}/eval_runs/${RUN_ID}}"
STOP_EXISTING="${STOP_EXISTING:-1}"

export RK_YOLO_ZERO_COPY_INPUT="${RK_YOLO_ZERO_COPY_INPUT:-1}"
export RK_YOLO_RGA_LETTERBOX="${RK_YOLO_RGA_LETTERBOX:-1}"
export RK_YOLO_ROUTE_B_STABLE="${RK_YOLO_ROUTE_B_STABLE:-1}"
export RK_YOLO_ROUTE_B_DEDUP="${RK_YOLO_ROUTE_B_DEDUP:-1}"
export RK_YOLO_IGNORE_OSD="${RK_YOLO_IGNORE_OSD:-0}"
export RK_YOLO_ASYNC_INFER="${RK_YOLO_ASYNC_INFER:-0}"
export RK_YOLO_ASYNC_POOL="${RK_YOLO_ASYNC_POOL:-3}"
export RK_YOLO_CAMERA_TUNE="${RK_YOLO_CAMERA_TUNE:-1}"
export RK_YOLO_CAMERA_MATCH="${RK_YOLO_CAMERA_MATCH:-HBS Camera}"
export RK_YOLO_CAMERA_ZOOM="${RK_YOLO_CAMERA_ZOOM:-20}"
export RK_YOLO_CAMERA_FOCUS_AUTO="${RK_YOLO_CAMERA_FOCUS_AUTO:-1}"
export RK_YOLO_CAMERA_FOCUS="${RK_YOLO_CAMERA_FOCUS:-260}"
export RK_YOLO_CAMERA_SETTLE_MS="${RK_YOLO_CAMERA_SETTLE_MS:-300}"
export RK_YOLO_CAMERA_FOCUS_STARTUP_LOCK="${RK_YOLO_CAMERA_FOCUS_STARTUP_LOCK:-1}"
export RK_YOLO_CAMERA_FOCUS_LOCK_MS="${RK_YOLO_CAMERA_FOCUS_LOCK_MS:-1200}"
export RK_YOLO_CAMERA_REFOCUS_AFTER_ZOOM="${RK_YOLO_CAMERA_REFOCUS_AFTER_ZOOM:-1}"
export RK_YOLO_CAMERA_REFOCUS_ZOOM_DELTA="${RK_YOLO_CAMERA_REFOCUS_ZOOM_DELTA:-8}"
export RK_YOLO_CAMERA_REFOCUS_COOLDOWN="${RK_YOLO_CAMERA_REFOCUS_COOLDOWN:-80}"
export RK_YOLO_CAMERA_REFOCUS_SETTLE_MS="${RK_YOLO_CAMERA_REFOCUS_SETTLE_MS:-800}"
export RK_YOLO_AUTO_ZOOM="${RK_YOLO_AUTO_ZOOM:-1}"
export RK_YOLO_AUTO_ZOOM_MATCH="${RK_YOLO_AUTO_ZOOM_MATCH:-${RK_YOLO_CAMERA_MATCH}}"
export RK_YOLO_AUTO_ZOOM_MIN="${RK_YOLO_AUTO_ZOOM_MIN:-0}"
export RK_YOLO_AUTO_ZOOM_MAX="${RK_YOLO_AUTO_ZOOM_MAX:-60}"
export RK_YOLO_AUTO_ZOOM_STEP="${RK_YOLO_AUTO_ZOOM_STEP:-4}"
export RK_YOLO_AUTO_ZOOM_COOLDOWN="${RK_YOLO_AUTO_ZOOM_COOLDOWN:-12}"
export RK_YOLO_AUTO_ZOOM_LOST_FRAMES="${RK_YOLO_AUTO_ZOOM_LOST_FRAMES:-45}"
export RK_YOLO_AUTO_ZOOM_MIN_RATIO="${RK_YOLO_AUTO_ZOOM_MIN_RATIO:-0.055}"
export RK_YOLO_AUTO_ZOOM_MAX_RATIO="${RK_YOLO_AUTO_ZOOM_MAX_RATIO:-0.24}"
# mpph264enc keeps the output path hardware-oriented. If VLC compatibility is
# poor on a specific machine, run with RK_YOLO_RTSP_ENCODER=x264.
export RK_YOLO_RTSP_ENCODER="${RK_YOLO_RTSP_ENCODER:-mpp}"

BIN="${BUILD_DIR}/rk_yolo_mpp_dma_rtsp_demo"
FRAMES=0
if [[ "${RUN_SECONDS}" != "0" ]]; then
  FRAMES=$((RUN_SECONDS * FPS))
fi

if [[ ! -f "${MODEL}" ]]; then
  echo "Model not found: ${MODEL}" >&2
  exit 1
fi

if [[ ! -e "${DEVICE}" ]]; then
  echo "Camera device not found: ${DEVICE}" >&2
  echo "Tip: run 'v4l2-ctl --list-devices' on the board." >&2
  exit 1
fi

mkdir -p "${OUT_DIR}"

echo "=== Route-B camera RTSP live launcher ==="
echo "Project: ${PROJECT_DIR}"
echo "Run dir: ${OUT_DIR}"
echo "Model: ${MODEL}"
echo "Input: ${DEVICE} ${WIDTH}x${HEIGHT}@${FPS} codec=${CODEC}"
echo "Policy: conf=${CONF}, nms=${NMS}, detect_every_n=${DETECT_EVERY_N}, output_mode=${OUTPUT_MODE}"
echo "Route B: zero_copy=${RK_YOLO_ZERO_COPY_INPUT}, rga_letterbox=${RK_YOLO_RGA_LETTERBOX}, stable=${RK_YOLO_ROUTE_B_STABLE}, dedup=${RK_YOLO_ROUTE_B_DEDUP}"
echo "Camera tune: enabled=${RK_YOLO_CAMERA_TUNE}, match='${RK_YOLO_CAMERA_MATCH}', zoom=${RK_YOLO_CAMERA_ZOOM}, focus_auto=${RK_YOLO_CAMERA_FOCUS_AUTO}, focus=${RK_YOLO_CAMERA_FOCUS}, startup_lock=${RK_YOLO_CAMERA_FOCUS_STARTUP_LOCK}, refocus_after_zoom=${RK_YOLO_CAMERA_REFOCUS_AFTER_ZOOM}"
echo "Auto zoom: enabled=${RK_YOLO_AUTO_ZOOM}, range=[${RK_YOLO_AUTO_ZOOM_MIN},${RK_YOLO_AUTO_ZOOM_MAX}], step=${RK_YOLO_AUTO_ZOOM_STEP}, cooldown=${RK_YOLO_AUTO_ZOOM_COOLDOWN}, target_ratio=[${RK_YOLO_AUTO_ZOOM_MIN_RATIO},${RK_YOLO_AUTO_ZOOM_MAX_RATIO}]"
echo "RTSP encoder: ${RK_YOLO_RTSP_ENCODER}"
echo
echo "Open from PC:"
for ip in $(hostname -I 2>/dev/null || true); do
  echo "  rtsp://${ip}:${PORT}${MOUNT}"
done
echo "  rtsp://<board-ip>:${PORT}${MOUNT}"
echo

if [[ ! -x "${BIN}" ]]; then
  echo "Binary not found, building: ${BIN}"
  RKNN_INCLUDE_DIR="${RKNN_INCLUDE_DIR:-/home/ubuntu/eclipse-workspace/eclipse-workspace/encoder/include}" \
  RKNN_LIBRARY="${RKNN_LIBRARY:-/usr/lib/librknnrt.so}" \
    cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DRKNN_INCLUDE_DIR="${RKNN_INCLUDE_DIR:-/home/ubuntu/eclipse-workspace/eclipse-workspace/encoder/include}" \
      -DRKNN_LIBRARY="${RKNN_LIBRARY:-/usr/lib/librknnrt.so}"
  cmake --build "${BUILD_DIR}" --target rk_yolo_mpp_dma_rtsp_demo -j"$(nproc)"
fi

if [[ "${STOP_EXISTING}" == "1" ]]; then
  pgrep -f "[r]k_yolo_mpp_dma_rtsp_demo.*${PORT}.*${MOUNT}" | xargs -r kill 2>/dev/null || true
fi

{
  echo "run_id=${RUN_ID}"
  echo "model=${MODEL}"
  echo "device=${DEVICE}"
  echo "width=${WIDTH}"
  echo "height=${HEIGHT}"
  echo "fps=${FPS}"
  echo "codec=${CODEC}"
  echo "conf=${CONF}"
  echo "nms=${NMS}"
  echo "detect_every_n=${DETECT_EVERY_N}"
  echo "output_mode=${OUTPUT_MODE}"
  echo "port=${PORT}"
  echo "mount=${MOUNT}"
  echo "run_seconds=${RUN_SECONDS}"
  echo "frames=${FRAMES}"
  echo "RK_YOLO_ZERO_COPY_INPUT=${RK_YOLO_ZERO_COPY_INPUT}"
  echo "RK_YOLO_RGA_LETTERBOX=${RK_YOLO_RGA_LETTERBOX}"
  echo "RK_YOLO_ROUTE_B_STABLE=${RK_YOLO_ROUTE_B_STABLE}"
  echo "RK_YOLO_ROUTE_B_DEDUP=${RK_YOLO_ROUTE_B_DEDUP}"
  echo "RK_YOLO_IGNORE_OSD=${RK_YOLO_IGNORE_OSD}"
  echo "RK_YOLO_ASYNC_INFER=${RK_YOLO_ASYNC_INFER}"
  echo "RK_YOLO_CAMERA_TUNE=${RK_YOLO_CAMERA_TUNE}"
  echo "RK_YOLO_CAMERA_MATCH=${RK_YOLO_CAMERA_MATCH}"
  echo "RK_YOLO_CAMERA_ZOOM=${RK_YOLO_CAMERA_ZOOM}"
  echo "RK_YOLO_CAMERA_FOCUS_AUTO=${RK_YOLO_CAMERA_FOCUS_AUTO}"
  echo "RK_YOLO_CAMERA_FOCUS=${RK_YOLO_CAMERA_FOCUS}"
  echo "RK_YOLO_CAMERA_SETTLE_MS=${RK_YOLO_CAMERA_SETTLE_MS}"
  echo "RK_YOLO_CAMERA_FOCUS_STARTUP_LOCK=${RK_YOLO_CAMERA_FOCUS_STARTUP_LOCK}"
  echo "RK_YOLO_CAMERA_FOCUS_LOCK_MS=${RK_YOLO_CAMERA_FOCUS_LOCK_MS}"
  echo "RK_YOLO_CAMERA_REFOCUS_AFTER_ZOOM=${RK_YOLO_CAMERA_REFOCUS_AFTER_ZOOM}"
  echo "RK_YOLO_CAMERA_REFOCUS_ZOOM_DELTA=${RK_YOLO_CAMERA_REFOCUS_ZOOM_DELTA}"
  echo "RK_YOLO_CAMERA_REFOCUS_COOLDOWN=${RK_YOLO_CAMERA_REFOCUS_COOLDOWN}"
  echo "RK_YOLO_CAMERA_REFOCUS_SETTLE_MS=${RK_YOLO_CAMERA_REFOCUS_SETTLE_MS}"
  echo "RK_YOLO_AUTO_ZOOM=${RK_YOLO_AUTO_ZOOM}"
  echo "RK_YOLO_AUTO_ZOOM_MATCH=${RK_YOLO_AUTO_ZOOM_MATCH}"
  echo "RK_YOLO_AUTO_ZOOM_MIN=${RK_YOLO_AUTO_ZOOM_MIN}"
  echo "RK_YOLO_AUTO_ZOOM_MAX=${RK_YOLO_AUTO_ZOOM_MAX}"
  echo "RK_YOLO_AUTO_ZOOM_STEP=${RK_YOLO_AUTO_ZOOM_STEP}"
  echo "RK_YOLO_AUTO_ZOOM_COOLDOWN=${RK_YOLO_AUTO_ZOOM_COOLDOWN}"
  echo "RK_YOLO_AUTO_ZOOM_LOST_FRAMES=${RK_YOLO_AUTO_ZOOM_LOST_FRAMES}"
  echo "RK_YOLO_AUTO_ZOOM_MIN_RATIO=${RK_YOLO_AUTO_ZOOM_MIN_RATIO}"
  echo "RK_YOLO_AUTO_ZOOM_MAX_RATIO=${RK_YOLO_AUTO_ZOOM_MAX_RATIO}"
  echo "RK_YOLO_RTSP_ENCODER=${RK_YOLO_RTSP_ENCODER}"
} > "${OUT_DIR}/launch_env.txt"

CMD=(
  "${BIN}"
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

echo "Starting. Press Ctrl+C to stop when RUN_SECONDS=0."
echo "Log: ${OUT_DIR}/rtsp.log"
"${CMD[@]}" 2>&1 | tee "${OUT_DIR}/rtsp.log"
