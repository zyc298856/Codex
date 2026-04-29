#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
WORKSPACE_DIR="$(cd "${PROJECT_DIR}/.." && pwd)"

VIDEO_PATH="${1:-/home/ubuntu/public_videos/anti_uav_fig1.mp4}"
MODEL_PATH="${2:-${WORKSPACE_DIR}/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn}"
OUT_DIR="${3:-${PROJECT_DIR}/artifacts/taskbook_pipeline_$(date +%Y%m%d_%H%M%S)}"

mkdir -p "${OUT_DIR}"

if [[ ! -x "${PROJECT_DIR}/build/rk_yolo_video" ]]; then
  cmake -S "${PROJECT_DIR}" -B "${PROJECT_DIR}/build"
  cmake --build "${PROJECT_DIR}/build" -j"$(nproc)"
fi

run_case() {
  local name="$1"
  shift
  echo "===== CASE ${name} ====="
  env "$@" "${PROJECT_DIR}/build/rk_yolo_video" \
    "${VIDEO_PATH}" \
    "${OUT_DIR}/${name}.mp4" \
    "${MODEL_PATH}" \
    0.35 \
    0.45 \
    "${OUT_DIR}/${name}_detections.csv" \
    "${OUT_DIR}/${name}_roi.jsonl" \
    "${OUT_DIR}/${name}_alarm.csv" \
    | tee "${OUT_DIR}/${name}.log"
}

run_case taskbook_full_rga_staged \
  RK_YOLO_PROFILE=1 \
  RK_YOLO_ALARM_OVERLAY=1 \
  RK_YOLO_PIPELINE=1 \
  RK_YOLO_PIPELINE_STAGED=1 \
  RK_YOLO_PIPELINE_QUEUE=4 \
  RK_YOLO_PREPROCESS=rga_cvt_resize \
  RK_YOLO_RGA_LETTERBOX=1 \
  RK_YOLO_REQUIRE_RGA=1

python3 - "${OUT_DIR}" <<'PY'
import csv
import os
import sys

out_dir = sys.argv[1]
summary_path = os.path.join(out_dir, "taskbook_pipeline_summary.csv")
case_name = "taskbook_full_rga_staged"
log_path = os.path.join(out_dir, case_name + ".log")

rows = []
done_line = ""
with open(log_path, "r", encoding="utf-8", errors="replace") as handle:
    for line in handle:
        if line.startswith("profile_csv,"):
            parts = line.strip().split(",")
            if len(parts) >= 12:
                rows.append(parts)
        elif line.startswith("done."):
            done_line = line.strip()

def avg(index):
    return sum(float(row[index]) for row in rows) / len(rows) if rows else 0.0

with open(summary_path, "w", newline="", encoding="utf-8") as handle:
    writer = csv.writer(handle)
    writer.writerow([
        "case",
        "frames",
        "detections",
        "mean_prepare_ms",
        "mean_input_update_ms",
        "mean_rknn_run_ms",
        "mean_outputs_get_ms",
        "mean_decode_ms",
        "mean_render_ms",
        "mean_total_work_ms",
        "done_line",
    ])
    writer.writerow([
        case_name,
        len(rows),
        sum(int(row[11]) for row in rows),
        f"{avg(3):.3f}",
        f"{avg(4):.3f}",
        f"{avg(5):.3f}",
        f"{avg(6):.3f}",
        f"{avg(7):.3f}",
        f"{avg(9):.3f}",
        f"{avg(10):.3f}",
        done_line,
    ])

print("summary=" + summary_path)
PY

echo "taskbook pipeline artifacts: ${OUT_DIR}"
