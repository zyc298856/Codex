#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEMO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
PROJECT_ROOT="$(cd "${DEMO_ROOT}/.." && pwd)"

load_env() {
  local env_file="$1"
  if [[ ! -f "${env_file}" ]]; then
    echo "missing env file: ${env_file}" >&2
    exit 2
  fi
  set -a
  # shellcheck disable=SC1090
  source "${env_file}"
  set +a
}

resolve_path() {
  local maybe_relative="$1"
  if [[ "${maybe_relative}" = /* ]]; then
    printf '%s\n' "${maybe_relative}"
  else
    printf '%s\n' "${PROJECT_ROOT}/${maybe_relative}"
  fi
}

resolve_model() {
  local configured="${MODEL_PATH:-}"
  local candidates=()
  if [[ -n "${configured}" ]]; then
    candidates+=("$(resolve_path "${configured}")")
  fi
  candidates+=(
    "${PROJECT_ROOT}/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn"
    "${PROJECT_ROOT}/training_runs/drone_gpu_50e/weights/best.rk3588.fp.rknn"
    "${PROJECT_ROOT}/training_runs/drone_hard_v1_ft_640_20e/weights/best.end2end_false.op12.rk3588.fp.v232.hard_v1.rknn"
    "${PROJECT_ROOT}/yolov10n.512.rk3588.fp.rknn"
  )

  local p
  for p in "${candidates[@]}"; do
    if [[ -f "${p}" ]]; then
      printf '%s\n' "${p}"
      return 0
    fi
  done

  echo "failed to find an RKNN model. Set MODEL_PATH in the selected .env file." >&2
  exit 3
}

resolve_live_binary() {
  local candidates=(
    "${PROJECT_ROOT}/rk_yolo_live_rtsp/build/rk_yolo_live_rtsp"
    "${PROJECT_ROOT}/rk_yolo_live_rtsp/rk_yolo_live_rtsp"
  )
  local p
  for p in "${candidates[@]}"; do
    if [[ -x "${p}" ]]; then
      printf '%s\n' "${p}"
      return 0
    fi
  done
  echo "failed to find rk_yolo_live_rtsp binary. Build it on the board first." >&2
  echo "  cd '${PROJECT_ROOT}/rk_yolo_live_rtsp' && mkdir -p build && cd build && cmake .. && make -j4" >&2
  exit 4
}

resolve_video_binary() {
  local candidates=(
    "${PROJECT_ROOT}/rk_yolo_video/build/rk_yolo_video"
    "${PROJECT_ROOT}/rk_yolo_video/rk_yolo_video"
  )
  local p
  for p in "${candidates[@]}"; do
    if [[ -x "${p}" ]]; then
      printf '%s\n' "${p}"
      return 0
    fi
  done
  echo "failed to find rk_yolo_video binary. Build it on the board first." >&2
  echo "  cd '${PROJECT_ROOT}/rk_yolo_video' && mkdir -p build && cd build && cmake .. && make -j4" >&2
  exit 5
}

print_live_summary() {
  local source="$1"
  local model="$2"
  cat <<EOF
live demo summary
  source=${source}
  model=${model}
  rtsp=rtsp://<board-ip>:${RTSP_PORT:-8554}${RTSP_MOUNT:-/drone}
  size=${WIDTH:-640}x${HEIGHT:-480}@${FPS:-15}
  score=${SCORE:-0.24}
  nms=${NMS:-0.45}
  detect_every_n=${DETECT_EVERY_N:-3}
  dynamic_roi=${RK_YOLO_DYNAMIC_ROI:-<default>}
  box_smooth=${RK_YOLO_BOX_SMOOTH:-<default>}
  auto_zoom=${RK_YOLO_AUTO_ZOOM:-0}
EOF
}

