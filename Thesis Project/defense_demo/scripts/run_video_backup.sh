#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

load_env "${DEMO_ROOT}/config/video_backup.env"

INPUT_VIDEO="${1:-${SOURCE:-/home/ubuntu/public_videos/video01.mp4}}"
MODEL_ARG="$(resolve_model)"
VIDEO_BIN="$(resolve_video_binary)"
RUN_ID="$(date +%Y%m%d_%H%M%S)"
OUTPUT_DIR="$(resolve_path "${OUTPUT_ROOT:-eval_runs/defense_demo}")/${RUN_ID}"
mkdir -p "${OUTPUT_DIR}"

OUTPUT_VIDEO="${OUTPUT_DIR}/defense_video_output.mp4"
CSV_PATH="${OUTPUT_DIR}/defense_detections.csv"
ROI_PATH="${OUTPUT_DIR}/defense_roi.jsonl"
ALARM_PATH="${OUTPUT_DIR}/defense_alarm_events.csv"

cat <<EOF
fixed-video backup summary
  input=${INPUT_VIDEO}
  model=${MODEL_ARG}
  output=${OUTPUT_VIDEO}
  score=${SCORE:-0.24}
  nms=${NMS:-0.45}
EOF

"${VIDEO_BIN}" \
  "${INPUT_VIDEO}" \
  "${OUTPUT_VIDEO}" \
  "${MODEL_ARG}" \
  "${SCORE:-0.24}" \
  "${NMS:-0.45}" \
  "${CSV_PATH}" \
  "${ROI_PATH}" \
  "${ALARM_PATH}"

echo
echo "backup outputs:"
echo "  ${OUTPUT_VIDEO}"
echo "  ${CSV_PATH}"
echo "  ${ROI_PATH}"
echo "  ${ALARM_PATH}"

