#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

RUN_ID="${RUN_ID:-route_b_natural_light_$(date +%Y%m%d_%H%M%S)}"
OUT_DIR="${OUT_DIR:-${PROJECT_DIR}/eval_runs/${RUN_ID}}"
MODEL="${MODEL:-/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_hard_v1_ft_640_20e/weights/best.end2end_false.op12.rk3588.fp.v232.hard_v1.rknn}"
VIDEO="${VIDEO:-/home/ubuntu/public_videos/pexels_18253602_drone_flying_18s_720p.mp4}"
CONF="${CONF:-0.24}"
NMS="${NMS:-0.45}"
FRAMES="${FRAMES:-240}"
OUT_FPS="${OUT_FPS:-20}"
OUT_VIDEO="${OUT_VIDEO:-${OUT_DIR}/route_b_natural_light.mp4}"

# Natural-light preset:
# - keep only one locked target for a clean demo view
# - keep Route-B box stabilizer on
# - keep thermal/OSD region filter off, because natural-light videos do not have telemetry overlays
export RK_YOLO_ROUTE_B_STABLE="${RK_YOLO_ROUTE_B_STABLE:-1}"
export RK_YOLO_VIS_MULTI="${RK_YOLO_VIS_MULTI:-0}"
export RK_YOLO_IGNORE_OSD="${RK_YOLO_IGNORE_OSD:-0}"

mkdir -p "${OUT_DIR}"

{
  echo "timestamp=$(date -Iseconds)"
  echo "run_id=${RUN_ID}"
  echo "mode=fixed-video-natural-light"
  echo "project_dir=${PROJECT_DIR}"
  echo "model=${MODEL}"
  echo "video=${VIDEO}"
  echo "conf=${CONF}"
  echo "nms=${NMS}"
  echo "frames=${FRAMES}"
  echo "out_fps=${OUT_FPS}"
  echo "rk_yolo_route_b_stable=${RK_YOLO_ROUTE_B_STABLE}"
  echo "rk_yolo_vis_multi=${RK_YOLO_VIS_MULTI}"
  echo "rk_yolo_ignore_osd=${RK_YOLO_IGNORE_OSD}"
} | tee "${OUT_DIR}/route_b_natural_light_env.txt"

VIDEO="${VIDEO}" \
MODEL="${MODEL}" \
CONF="${CONF}" \
NMS="${NMS}" \
FRAMES="${FRAMES}" \
OUT_VIDEO="${OUT_VIDEO}" \
OUT_FPS="${OUT_FPS}" \
OUT_DIR="${OUT_DIR}" \
  "${SCRIPT_DIR}/run_mpp_file_rknn_eval.sh" | tee "${OUT_DIR}/route_b_natural_light.log"

echo "Natural-light Route-B fixed-video run complete."
echo "Output video: ${OUT_VIDEO}"
echo "Output directory: ${OUT_DIR}"
