#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
WORKSPACE_DIR="$(cd "${PROJECT_DIR}/.." && pwd)"

VIDEO_PATH="${1:-/home/ubuntu/public_videos/anti_uav_fig1.mp4}"
FP_MODEL="${2:-${WORKSPACE_DIR}/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn}"
INT8_MODEL="${3:-${WORKSPACE_DIR}/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.int8.v220.rknn}"
HYBRID_MODEL="${4:-${WORKSPACE_DIR}/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.int8.hybrid_head230.v220.rknn}"
OUT_DIR="${5:-${PROJECT_DIR}/artifacts/taskbook_int8_$(date +%Y%m%d_%H%M%S)}"

mkdir -p "${OUT_DIR}"

if [[ ! -x "${PROJECT_DIR}/build/rk_yolo_video" ]]; then
  cmake -S "${PROJECT_DIR}" -B "${PROJECT_DIR}/build"
  cmake --build "${PROJECT_DIR}/build" -j"$(nproc)"
fi

for required in "${VIDEO_PATH}" "${FP_MODEL}" "${INT8_MODEL}" "${HYBRID_MODEL}"; do
  if [[ ! -f "${required}" ]]; then
    echo "missing required input: ${required}" >&2
    exit 1
  fi
done

run_case() {
  local name="$1"
  local model="$2"
  echo "===== CASE ${name} ====="
  env RK_YOLO_PROFILE=1 RK_YOLO_ALARM_OVERLAY=1 RK_YOLO_PREPROCESS=opencv \
    "${PROJECT_DIR}/build/rk_yolo_video" \
    "${VIDEO_PATH}" \
    "${OUT_DIR}/${name}.mp4" \
    "${model}" \
    0.35 \
    0.45 \
    "${OUT_DIR}/${name}_detections.csv" \
    "${OUT_DIR}/${name}_roi.jsonl" \
    "${OUT_DIR}/${name}_alarm.csv" \
    | tee "${OUT_DIR}/${name}.log"
}

run_case fp_baseline "${FP_MODEL}"
run_case int8_full "${INT8_MODEL}"
run_case int8_hybrid_head230 "${HYBRID_MODEL}"

python3 - "${OUT_DIR}" <<'PY'
import csv
import os
import re
import sys

out_dir = sys.argv[1]
cases = ["fp_baseline", "int8_full", "int8_hybrid_head230"]
summary_path = os.path.join(out_dir, "taskbook_int8_summary.csv")

def read_profile(log_path):
    rows = []
    done = ""
    with open(log_path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if line.startswith("profile_csv,"):
                parts = line.strip().split(",")
                if len(parts) >= 12:
                    rows.append(parts)
            elif line.startswith("done."):
                done = line.strip()
    return rows, done

def avg(rows, index):
    return sum(float(row[index]) for row in rows) / len(rows) if rows else 0.0

with open(summary_path, "w", newline="", encoding="utf-8") as handle:
    writer = csv.writer(handle)
    writer.writerow([
        "case",
        "frames",
        "frames_with_detections",
        "total_detections",
        "alarm_events",
        "avg_infer_ms",
        "mean_prepare_ms",
        "mean_input_update_ms",
        "mean_rknn_run_ms",
        "mean_outputs_get_ms",
        "mean_decode_ms",
        "mean_render_ms",
        "mean_total_work_ms",
    ])
    for case in cases:
        rows, done = read_profile(os.path.join(out_dir, case + ".log"))
        match = re.search(
            r"frames=(\d+), frames_with_detections=(\d+), "
            r"total_detections=(\d+), alarm_events=(\d+), avg_infer_ms=([0-9.]+)",
            done,
        )
        if match:
            frames, frames_with_dets, total_dets, alarms, avg_infer = match.groups()
        else:
            frames = str(len(rows))
            frames_with_dets = ""
            total_dets = str(sum(int(row[11]) for row in rows))
            alarms = ""
            avg_infer = ""
        writer.writerow([
            case,
            frames,
            frames_with_dets,
            total_dets,
            alarms,
            avg_infer,
            f"{avg(rows, 3):.3f}",
            f"{avg(rows, 4):.3f}",
            f"{avg(rows, 5):.3f}",
            f"{avg(rows, 6):.3f}",
            f"{avg(rows, 7):.3f}",
            f"{avg(rows, 9):.3f}",
            f"{avg(rows, 10):.3f}",
        ])

print("summary=" + summary_path)
PY

echo "taskbook INT8 artifacts: ${OUT_DIR}"
