#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

RUN_SECONDS="${RUN_SECONDS:-18}"
BASE_PORT="${BASE_PORT:-8580}"
ASYNC_POOL="${ASYNC_POOL:-3}"
LOG_DIR="${LOG_DIR:-${PROJECT_DIR}/eval_runs/mpp_dma_rtsp_matrix_$(date +%Y%m%d_%H%M%S)}"

mkdir -p "${LOG_DIR}"

SUMMARY_FILE="${LOG_DIR}/summary.tsv"
printf "case\toutput_mode\tasync\tport\tmount\tprobe_ok\tsummary\tasync_summary\tlog\n" \
  > "${SUMMARY_FILE}"

run_case() {
  local label="$1"
  local output_mode="$2"
  local async_infer="$3"
  local port="$4"
  local mount="/${label}"
  local log_file="${LOG_DIR}/${label}.log"
  local probe_file="${LOG_DIR}/${label}.ffprobe.txt"

  echo "== ${label}: output_mode=${output_mode}, async=${async_infer}, port=${port}${mount} =="

  (
    cd "${PROJECT_DIR}"
    RUN_SECONDS="${RUN_SECONDS}" \
    OUTPUT_MODE="${output_mode}" \
    ASYNC_INFER="${async_infer}" \
    ASYNC_POOL="${ASYNC_POOL}" \
    PORT="${port}" \
    MOUNT="${mount}" \
      "${SCRIPT_DIR}/run_mpp_dma_rtsp_eval.sh"
  ) > "${log_file}" 2>&1 &

  local demo_pid="$!"
  for _ in $(seq 1 30); do
    if grep -q 'RTSP output ready' "${log_file}" 2>/dev/null; then
      break
    fi
    if ! kill -0 "${demo_pid}" 2>/dev/null; then
      break
    fi
    sleep 1
  done
  sleep 2

  local probe_ok="0"
  if ffprobe -v error -rtsp_transport tcp -select_streams v:0 \
      -show_entries stream=codec_name,width,height,r_frame_rate \
      -of default=noprint_wrappers=1 \
      "rtsp://127.0.0.1:${port}${mount}" > "${probe_file}" 2>&1; then
    probe_ok="1"
  fi

  wait "${demo_pid}" || true

  local summary_line=""
  local async_summary_line=""
  summary_line="$(grep '^summary ' "${log_file}" | tail -n 1 | tr '\t' ' ' || true)"
  async_summary_line="$(grep '^async_summary ' "${log_file}" | tail -n 1 | tr '\t' ' ' || true)"
  [[ -n "${summary_line}" ]] || summary_line="-"
  [[ -n "${async_summary_line}" ]] || async_summary_line="-"

  printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
    "${label}" "${output_mode}" "${async_infer}" "${port}" "${mount}" \
    "${probe_ok}" "${summary_line}" "${async_summary_line}" "${log_file}" \
    >> "${SUMMARY_FILE}"
}

run_case "direct-dmabuf" "dmabuf" "0" "$((BASE_PORT + 0))"
run_case "async-dmabuf" "dmabuf" "1" "$((BASE_PORT + 1))"
run_case "direct-bgr" "bgr" "0" "$((BASE_PORT + 2))"
run_case "async-bgr" "bgr" "1" "$((BASE_PORT + 3))"

echo "Matrix complete: ${SUMMARY_FILE}"
