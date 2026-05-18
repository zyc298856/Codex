#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

RUN_ID="${RUN_ID:-production_candidate_$(date +%Y%m%d_%H%M%S)}"
OUT_DIR="${OUT_DIR:-${PROJECT_DIR}/eval_runs/${RUN_ID}}"
MATRIX_SECONDS="${MATRIX_SECONDS:-12}"
STRESS_SECONDS="${STRESS_SECONDS:-10}"
BASE_PORT="${BASE_PORT:-8600}"
ASYNC_POOL="${ASYNC_POOL:-3}"
DEVICE="${DEVICE:-/dev/video48}"
WIDTH="${WIDTH:-640}"
HEIGHT="${HEIGHT:-480}"
FPS="${FPS:-15}"
CODEC="${CODEC:-h264}"
CONF="${CONF:-0.24}"
NMS="${NMS:-0.45}"
DETECT_EVERY_N="${DETECT_EVERY_N:-3}"
OUTPUT_MODE="${OUTPUT_MODE:-bgr}"
MODEL="${MODEL:-/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_hard_v1_ft_640_20e/weights/best.end2end_false.op12.rk3588.fp.v232.hard_v1.rknn}"

mkdir -p "${OUT_DIR}"

INFO_FILE="${OUT_DIR}/environment.txt"
REPORT_FILE="${OUT_DIR}/report.md"
MATRIX_DIR="${OUT_DIR}/matrix"
STRESS_LOG="${OUT_DIR}/async_pool_stress.log"

extract_metric() {
  local text="$1"
  local key="$2"
  printf "%s\n" "${text}" | sed -n "s/.*${key}=\([0-9.]*\).*/\1/p"
}

write_environment() {
  {
    echo "timestamp=$(date -Iseconds)"
    echo "hostname=$(hostname)"
    echo "uname=$(uname -a)"
    echo "project_dir=${PROJECT_DIR}"
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
    echo "async_pool=${ASYNC_POOL}"
    echo "matrix_seconds=${MATRIX_SECONDS}"
    echo "stress_seconds=${STRESS_SECONDS}"
    echo
    echo "== model file =="
    LC_ALL=C ls -lh "${MODEL}" 2>/dev/null || true
    echo
    echo "== camera formats =="
    if command -v v4l2-ctl >/dev/null 2>&1; then
      v4l2-ctl --list-formats-ext -d "${DEVICE}" 2>&1 || true
    else
      echo "v4l2-ctl not found"
    fi
    echo
    echo "== rknn runtime =="
    ldconfig -p 2>/dev/null | grep -i rknn || true
    echo
    echo "== rga =="
    ls -l /dev/rga* 2>/dev/null || true
  } > "${INFO_FILE}"
}

run_matrix() {
  echo "[1/3] Running four-mode MPP DMA/RGA/RKNN/RTSP matrix..."
  (
    cd "${PROJECT_DIR}"
    RUN_SECONDS="${MATRIX_SECONDS}" \
    BASE_PORT="${BASE_PORT}" \
    ASYNC_POOL="${ASYNC_POOL}" \
    LOG_DIR="${MATRIX_DIR}" \
    DEVICE="${DEVICE}" \
    WIDTH="${WIDTH}" \
    HEIGHT="${HEIGHT}" \
    FPS="${FPS}" \
    CODEC="${CODEC}" \
    CONF="${CONF}" \
    NMS="${NMS}" \
    DETECT_EVERY_N="${DETECT_EVERY_N}" \
    MODEL="${MODEL}" \
      "${SCRIPT_DIR}/run_mpp_dma_rtsp_matrix.sh"
  )
}

run_stress() {
  echo "[2/3] Running async DMA pool pressure test..."
  (
    cd "${PROJECT_DIR}"
    RUN_SECONDS="${STRESS_SECONDS}" \
    OUTPUT_MODE="dmabuf" \
    ASYNC_INFER="1" \
    ASYNC_POOL="${ASYNC_POOL}" \
    DETECT_EVERY_N="1" \
    PORT="$((BASE_PORT + 20))" \
    MOUNT="/async-stress" \
    DEVICE="${DEVICE}" \
    WIDTH="${WIDTH}" \
    HEIGHT="${HEIGHT}" \
    FPS="${FPS}" \
    CODEC="${CODEC}" \
    CONF="${CONF}" \
    NMS="${NMS}" \
    MODEL="${MODEL}" \
      "${SCRIPT_DIR}/run_mpp_dma_rtsp_eval.sh"
  ) > "${STRESS_LOG}" 2>&1 || true
}

write_report() {
  echo "[3/3] Writing report..."
  local matrix_summary="${MATRIX_DIR}/summary.tsv"
  local stress_summary=""
  local stress_async_summary=""
  stress_summary="$(grep '^summary ' "${STRESS_LOG}" | tail -n 1 | tr '\t' ' ' || true)"
  stress_async_summary="$(grep '^async_summary ' "${STRESS_LOG}" | tail -n 1 | tr '\t' ' ' || true)"

  {
    echo "# RK3588 MPP DMA/RGA/RKNN/RTSP Production-Candidate Experiment"
    echo
    echo "Run ID: \`${RUN_ID}\`"
    echo
    echo "## Goal"
    echo
    echo "This run evaluates the most complete production-candidate path currently implemented in this project:"
    echo
    echo "\`\`\`text"
    echo "UVC camera compressed stream"
    echo "  -> V4L2 capture"
    echo "  -> MPP hardware decode"
    echo "  -> MppFrame DMA fd"
    echo "  -> RGA resize / color convert / letterbox"
    echo "  -> RKNN input memory"
    echo "  -> RK3588 NPU"
    echo "  -> RTSP output"
    echo "\`\`\`"
    echo
    echo "The stable thesis/demo program is not changed. This report is for performance exploration and engineering comparison."
    echo
    echo "## Environment"
    echo
    echo "\`\`\`text"
    sed -n '1,80p' "${INFO_FILE}"
    echo "\`\`\`"
    echo
    echo "## Four-Mode Matrix"
    echo
    if [[ -f "${matrix_summary}" ]]; then
      echo
      echo "| Case | Output | Async | RTSP Probe | Decoded FPS | Infer FPS | Avg Prepare ms | Avg Run ms | Avg Total ms | Detections | Notes |"
      echo "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|"
      tail -n +2 "${matrix_summary}" | while IFS=$'\t' read -r case_name output_mode async_infer port mount probe_ok summary_line async_summary_line log_file; do
        decoded_fps="$(extract_metric "${summary_line}" "decoded_wall_fps")"
        infer_fps="$(extract_metric "${summary_line}" "infer_wall_fps")"
        avg_prepare="$(extract_metric "${summary_line}" "avg_prepare_ms")"
        avg_run="$(extract_metric "${summary_line}" "avg_run_ms")"
        avg_total="$(extract_metric "${summary_line}" "avg_total_ms")"
        detections="$(extract_metric "${summary_line}" "total_detections")"
        [[ -n "${decoded_fps}" ]] || decoded_fps="-"
        [[ -n "${infer_fps}" ]] || infer_fps="-"
        [[ -n "${avg_prepare}" ]] || avg_prepare="-"
        [[ -n "${avg_run}" ]] || avg_run="-"
        [[ -n "${avg_total}" ]] || avg_total="-"
        [[ -n "${detections}" ]] || detections="-"
        local_note="log: ${log_file}"
        printf "| \`%s\` | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |\n" \
          "${case_name}" "${output_mode}" "${async_infer}" "${probe_ok}" \
          "${decoded_fps}" "${infer_fps}" "${avg_prepare}" "${avg_run}" \
          "${avg_total}" "${detections}" "${local_note}"
      done
    else
      echo
      echo "Matrix summary was not generated."
    fi
    echo
    echo "## Async DMA Pool Pressure Test"
    echo
    echo "\`\`\`text"
    echo "${stress_summary}"
    echo "${stress_async_summary}"
    echo "\`\`\`"
    echo
    echo "## Current Recommendation"
    echo
    echo "- Low-copy performance candidate: \`direct-dmabuf\`. It keeps the output stream in NV12 DMA form and is best for measuring the MPP/RGA/RKNN chain."
    echo "- Visual demonstration candidate: \`direct-bgr\` or \`async-bgr\`. These modes add a BGR visualization copy so the RTSP stream can show boxes."
    echo "- Real-time scheduling candidate: \`async + DMA pool\`. The DMA pool uses latest-frame-wins replacement to avoid latency accumulation under pressure."
    echo "- Stable thesis/demo version remains the FP RKNN \`rk_yolo_live_rtsp\` path. This experimental path should not replace it until it passes longer camera tests."
    echo
    echo "## Boundary"
    echo
    echo "This is the closest industrial chain currently implemented in the project, but it is still an experimental production-candidate path rather than a mathematically complete end-to-end zero-copy system. The remaining gap is a fully unified physical-memory pipeline across capture, decode, RGA, RKNN input, and visualization output without any visualization-side CPU copy."
  } > "${REPORT_FILE}"

  ln -sfn "${RUN_ID}" "${PROJECT_DIR}/eval_runs/production_candidate_latest" 2>/dev/null || true
}

write_environment
run_matrix
run_stress
write_report

echo "Production-candidate experiment complete."
echo "Output directory: ${OUT_DIR}"
echo "Report: ${REPORT_FILE}"
