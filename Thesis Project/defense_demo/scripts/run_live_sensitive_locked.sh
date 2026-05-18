#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

load_env "${DEMO_ROOT}/config/live_sensitive_locked.env"

SOURCE_ARG="${1:-${SOURCE:-/dev/video48}}"
MODEL_ARG="$(resolve_model)"
LIVE_BIN="$(resolve_live_binary)"

print_live_summary "${SOURCE_ARG}" "${MODEL_ARG}"
echo "  target_lock=${RK_YOLO_TARGET_LOCK:-0}"
echo "  lock_alpha=${RK_YOLO_LOCK_ALPHA:-<default>}"
echo "  lock_max_center_step=${RK_YOLO_LOCK_MAX_CENTER_STEP:-<default>}"
echo "  lock_max_size_step=${RK_YOLO_LOCK_MAX_SIZE_STEP:-<default>}"

exec "${LIVE_BIN}" \
  "${SOURCE_ARG}" \
  "${MODEL_ARG}" \
  "${RTSP_MOUNT:-/drone}" \
  "${WIDTH:-640}" \
  "${HEIGHT:-480}" \
  "${FPS:-15}" \
  "${SCORE:-0.20}" \
  "${NMS:-0.45}" \
  "${RTSP_PORT:-8554}" \
  "${DETECT_EVERY_N:-3}"
