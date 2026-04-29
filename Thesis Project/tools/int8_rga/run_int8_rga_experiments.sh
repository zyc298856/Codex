#!/usr/bin/env bash
# Run repeatable rk_yolo_video experiments on RK3588.
#
# This script compares the stable FP RKNN path with optional INT8, RGA, and
# zero-copy variants. It does not modify the runtime program; every optimization
# path is enabled only through environment variables for the current command.

set -euo pipefail

SCRIPT_NAME="$(basename "$0")"

BINARY="./rk_yolo_video"
VIDEO=""
FP_MODEL=""
INT8_MODEL=""
OUT_DIR="./int8_rga_runs"
SCORE="0.35"
NMS="0.45"
CASE_FILTER="all"
DRY_RUN=0

usage() {
  cat <<USAGE
Usage:
  ${SCRIPT_NAME} --video VIDEO --fp-model FP_MODEL [options]

Required:
  --video PATH          Input video on the RK3588 board.
  --fp-model PATH       Stable FP RKNN model path.

Optional:
  --int8-model PATH     INT8 RKNN model path. INT8 cases are skipped if omitted.
  --binary PATH         rk_yolo_video binary. Default: ./rk_yolo_video
  --out-dir DIR         Output directory. Default: ./int8_rga_runs
  --score VALUE         Score threshold. Default: 0.35
  --nms VALUE           NMS threshold. Default: 0.45
  --cases VALUE         all, fp, int8, rga, zero_copy. Default: all
  --dry-run             Print commands without running them.
  -h, --help            Show this help.

Examples:
  ${SCRIPT_NAME} \\
    --video /home/ubuntu/eval/public_uav.mp4 \\
    --fp-model /home/ubuntu/models/best.end2end_false.op12.rk3588.fp.v220.rknn \\
    --int8-model /home/ubuntu/models/best.end2end_false.op12.rk3588.int8.v220.rknn \\
    --out-dir /home/ubuntu/eval/int8_rga_runs
USAGE
}

die() {
  echo "error: $*" >&2
  exit 1
}

quote_cmd() {
  printf "%q " "$@"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --video)
      VIDEO="${2:-}"
      shift 2
      ;;
    --fp-model)
      FP_MODEL="${2:-}"
      shift 2
      ;;
    --int8-model)
      INT8_MODEL="${2:-}"
      shift 2
      ;;
    --binary)
      BINARY="${2:-}"
      shift 2
      ;;
    --out-dir)
      OUT_DIR="${2:-}"
      shift 2
      ;;
    --score)
      SCORE="${2:-}"
      shift 2
      ;;
    --nms)
      NMS="${2:-}"
      shift 2
      ;;
    --cases)
      CASE_FILTER="${2:-}"
      shift 2
      ;;
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown argument: $1"
      ;;
  esac
done

[[ -n "${VIDEO}" ]] || die "--video is required"
[[ -n "${FP_MODEL}" ]] || die "--fp-model is required"
[[ -f "${VIDEO}" ]] || die "input video not found: ${VIDEO}"
[[ -f "${FP_MODEL}" ]] || die "FP model not found: ${FP_MODEL}"
if [[ -n "${INT8_MODEL}" && ! -f "${INT8_MODEL}" ]]; then
  die "INT8 model not found: ${INT8_MODEL}"
fi
if [[ ! -x "${BINARY}" ]]; then
  die "binary is not executable: ${BINARY}"
fi

case "${CASE_FILTER}" in
  all|fp|int8|rga|zero_copy) ;;
  *) die "--cases must be one of: all, fp, int8, rga, zero_copy" ;;
esac

mkdir -p "${OUT_DIR}"

MANIFEST="${OUT_DIR}/run_manifest.txt"
{
  echo "started_at=$(date -Iseconds)"
  echo "host=$(hostname)"
  echo "binary=${BINARY}"
  echo "video=${VIDEO}"
  echo "fp_model=${FP_MODEL}"
  echo "int8_model=${INT8_MODEL}"
  echo "score=${SCORE}"
  echo "nms=${NMS}"
  echo "case_filter=${CASE_FILTER}"
} > "${MANIFEST}"

should_run_case() {
  local case_name="$1"
  local model_kind="$2"
  case "${CASE_FILTER}" in
    all) return 0 ;;
    fp) [[ "${model_kind}" == "fp" ]] ;;
    int8) [[ "${model_kind}" == "int8" ]] ;;
    rga) [[ "${case_name}" == *"rga"* ]] ;;
    zero_copy) [[ "${case_name}" == *"zero_copy"* ]] ;;
  esac
}

run_case() {
  local case_name="$1"
  local model_kind="$2"
  local model_path="$3"
  shift 3
  local env_args=("$@")

  if ! should_run_case "${case_name}" "${model_kind}"; then
    echo "skip ${case_name}: filtered by --cases ${CASE_FILTER}"
    return 0
  fi

  local output_video="${OUT_DIR}/${case_name}.mp4"
  local detections_csv="${OUT_DIR}/${case_name}.detections.csv"
  local roi_jsonl="${OUT_DIR}/${case_name}.roi.jsonl"
  local alarm_csv="${OUT_DIR}/${case_name}.alarm_events.csv"
  local log_path="${OUT_DIR}/${case_name}.log"

  local cmd=(env "${env_args[@]}" "${BINARY}" "${VIDEO}" "${output_video}" "${model_path}" \
    "${SCORE}" "${NMS}" "${detections_csv}" "${roi_jsonl}" "${alarm_csv}")

  echo
  echo "===== ${case_name} (${model_kind}) ====="
  echo "command:"
  quote_cmd "${cmd[@]}"
  echo "2>&1 | tee ${log_path}"

  {
    echo
    echo "[${case_name}]"
    echo "model_kind=${model_kind}"
    echo "model_path=${model_path}"
    echo "output_video=${output_video}"
    echo "detections_csv=${detections_csv}"
    echo "roi_jsonl=${roi_jsonl}"
    echo "alarm_csv=${alarm_csv}"
    echo "log=${log_path}"
    echo "env=${env_args[*]}"
  } >> "${MANIFEST}"

  if [[ "${DRY_RUN}" -eq 1 ]]; then
    return 0
  fi

  "${cmd[@]}" 2>&1 | tee "${log_path}"
}

COMMON_ENV=("RK_YOLO_PROFILE=1" "RK_YOLO_ALARM_OVERLAY=1")

run_case "fp_opencv_baseline" "fp" "${FP_MODEL}" \
  "${COMMON_ENV[@]}" "RK_YOLO_PREPROCESS=opencv"

run_case "fp_rga_resize" "fp" "${FP_MODEL}" \
  "${COMMON_ENV[@]}" "RK_YOLO_PREPROCESS=rga"

run_case "fp_rga_cvt_resize" "fp" "${FP_MODEL}" \
  "${COMMON_ENV[@]}" "RK_YOLO_PREPROCESS=rga_cvt_resize"

run_case "fp_rga_letterbox" "fp" "${FP_MODEL}" \
  "${COMMON_ENV[@]}" "RK_YOLO_PREPROCESS=rga_cvt_resize" "RK_YOLO_RGA_LETTERBOX=1"

run_case "fp_zero_copy" "fp" "${FP_MODEL}" \
  "${COMMON_ENV[@]}" "RK_YOLO_ZERO_COPY_INPUT=1"

if [[ -n "${INT8_MODEL}" ]]; then
  run_case "int8_opencv_baseline" "int8" "${INT8_MODEL}" \
    "${COMMON_ENV[@]}" "RK_YOLO_PREPROCESS=opencv"

  run_case "int8_rga_cvt_resize" "int8" "${INT8_MODEL}" \
    "${COMMON_ENV[@]}" "RK_YOLO_PREPROCESS=rga_cvt_resize"

  run_case "int8_rga_letterbox" "int8" "${INT8_MODEL}" \
    "${COMMON_ENV[@]}" "RK_YOLO_PREPROCESS=rga_cvt_resize" "RK_YOLO_RGA_LETTERBOX=1"

  run_case "int8_zero_copy" "int8" "${INT8_MODEL}" \
    "${COMMON_ENV[@]}" "RK_YOLO_ZERO_COPY_INPUT=1"
else
  echo
  echo "INT8 model omitted; INT8 cases skipped."
fi

{
  echo "finished_at=$(date -Iseconds)"
  echo "dry_run=${DRY_RUN}"
} >> "${MANIFEST}"

echo
echo "All requested cases completed."
echo "Manifest: ${MANIFEST}"
echo "Tip: copy logs back to the PC and run tools/int8_rga/summarize_profile_csv.py --logs '<out-dir>/*.log'."
