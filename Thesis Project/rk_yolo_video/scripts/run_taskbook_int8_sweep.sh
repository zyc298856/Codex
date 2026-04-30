#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
WORKSPACE_DIR="$(cd "${PROJECT_DIR}/.." && pwd)"

VIDEO_PATH="${1:-/home/ubuntu/public_videos/anti_uav_fig1.mp4}"
FP_MODEL="${2:-${WORKSPACE_DIR}/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn}"
INT8_MODEL="${3:-${WORKSPACE_DIR}/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.int8.v220.rknn}"
HYBRID_MODEL="${4:-${WORKSPACE_DIR}/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.int8.hybrid_head230.v220.rknn}"
OUT_DIR="${5:-${PROJECT_DIR}/artifacts/taskbook_int8_sweep_$(date +%Y%m%d_%H%M%S)}"

# Use a threshold sweep to distinguish "quantized model is unusable" from
# "quantization changed the confidence scale".
THRESHOLDS="${RK_YOLO_INT8_SWEEP_THRESHOLDS:-0.05 0.10 0.20 0.35}"
NMS_THRESHOLD="${RK_YOLO_INT8_SWEEP_NMS:-0.45}"
PREPROCESS="${RK_YOLO_INT8_SWEEP_PREPROCESS:-opencv}"

MODEL_NAMES=("fp_baseline" "int8_full" "int8_hybrid_head230")
MODEL_PATHS=("${FP_MODEL}" "${INT8_MODEL}" "${HYBRID_MODEL}")

# Optional extra models, for example:
# RK_YOLO_INT8_EXTRA_MODELS="int8_calib500=/path/a.rknn int8_calib1000=/path/b.rknn"
for entry in ${RK_YOLO_INT8_EXTRA_MODELS:-}; do
  if [[ "${entry}" != *=* ]]; then
    echo "invalid RK_YOLO_INT8_EXTRA_MODELS entry, expected name=/path/model.rknn: ${entry}" >&2
    exit 1
  fi
  MODEL_NAMES+=("${entry%%=*}")
  MODEL_PATHS+=("${entry#*=}")
done

mkdir -p "${OUT_DIR}"

if [[ ! -x "${PROJECT_DIR}/build/rk_yolo_video" ]]; then
  cmake -S "${PROJECT_DIR}" -B "${PROJECT_DIR}/build"
  cmake --build "${PROJECT_DIR}/build" -j"$(nproc)"
fi

for required in "${VIDEO_PATH}" "${MODEL_PATHS[@]}"; do
  if [[ ! -f "${required}" ]]; then
    echo "missing required input: ${required}" >&2
    exit 1
  fi
done

run_case() {
  local model_name="$1"
  local model_path="$2"
  local conf="$3"
  local conf_tag
  conf_tag="$(printf '%s' "${conf}" | tr -d '.')"
  local run_name="${model_name}_conf${conf_tag}"

  echo "===== CASE ${run_name} ====="
  env RK_YOLO_PROFILE=1 RK_YOLO_ALARM_OVERLAY=1 RK_YOLO_PREPROCESS="${PREPROCESS}" \
    "${PROJECT_DIR}/build/rk_yolo_video" \
    "${VIDEO_PATH}" \
    "${OUT_DIR}/${run_name}.mp4" \
    "${model_path}" \
    "${conf}" \
    "${NMS_THRESHOLD}" \
    "${OUT_DIR}/${run_name}_detections.csv" \
    "${OUT_DIR}/${run_name}_roi.jsonl" \
    "${OUT_DIR}/${run_name}_alarm.csv" \
    | tee "${OUT_DIR}/${run_name}.log"
}

for conf in ${THRESHOLDS}; do
  for index in "${!MODEL_NAMES[@]}"; do
    run_case "${MODEL_NAMES[$index]}" "${MODEL_PATHS[$index]}" "${conf}"
  done
done

MODEL_NAMES_CSV="$(IFS=,; echo "${MODEL_NAMES[*]}")"

python3 - "${OUT_DIR}" "${THRESHOLDS}" "${NMS_THRESHOLD}" "${PREPROCESS}" "${MODEL_NAMES_CSV}" <<'PY'
import csv
import os
import re
import sys

out_dir = sys.argv[1]
thresholds = sys.argv[2].split()
nms_threshold = sys.argv[3]
preprocess = sys.argv[4]
models = sys.argv[5].split(",")
summary_path = os.path.join(out_dir, "taskbook_int8_sweep_summary.csv")

def tag(conf: str) -> str:
    return conf.replace(".", "")

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
        "model_case",
        "conf_threshold",
        "nms_threshold",
        "preprocess",
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

    for conf in thresholds:
        for model in models:
            run_name = f"{model}_conf{tag(conf)}"
            rows, done = read_profile(os.path.join(out_dir, run_name + ".log"))
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
                total_dets = str(sum(int(row[11]) for row in rows)) if rows else ""
                alarms = ""
                avg_infer = ""
            writer.writerow([
                model,
                conf,
                nms_threshold,
                preprocess,
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

echo "taskbook INT8 sweep artifacts: ${OUT_DIR}"
