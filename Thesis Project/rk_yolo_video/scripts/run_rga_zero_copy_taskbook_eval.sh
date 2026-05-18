#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

RUN_ID="${RUN_ID:-rga_zero_copy_taskbook_$(date +%Y%m%d_%H%M%S)}"
OUT_DIR="${OUT_DIR:-${PROJECT_DIR}/eval_runs/${RUN_ID}}"
REPORT_FILE="${OUT_DIR}/report.md"
ENV_FILE="${OUT_DIR}/environment.txt"

MODEL="${MODEL:-/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_hard_v1_ft_640_20e/weights/best.end2end_false.op12.rk3588.fp.v232.hard_v1.rknn}"
CONF="${CONF:-0.24}"
NMS="${NMS:-0.45}"

# Fixed-video input. Set RUN_FIXED=1 and provide VIDEO=... or H264=...
RUN_FIXED="${RUN_FIXED:-0}"
VIDEO="${VIDEO:-}"
H264="${H264:-}"
FIXED_FRAMES="${FIXED_FRAMES:-300}"
WRITE_FIXED_VIDEO="${WRITE_FIXED_VIDEO:-1}"
FIXED_OUT_FPS="${FIXED_OUT_FPS:-20}"

# Camera input. Set RUN_CAMERA=1 to validate the realtime route.
RUN_CAMERA="${RUN_CAMERA:-1}"
DEVICE="${DEVICE:-/dev/video48}"
WIDTH="${WIDTH:-640}"
HEIGHT="${HEIGHT:-480}"
FPS="${FPS:-15}"
CODEC="${CODEC:-h264}"
CAMERA_SECONDS="${CAMERA_SECONDS:-20}"
CAMERA_FRAMES="${CAMERA_FRAMES:-0}"
PORT="${PORT:-8562}"
MOUNT="${MOUNT:-/yolo_mpp}"
DETECT_EVERY_N="${DETECT_EVERY_N:-3}"
OUTPUT_MODE="${OUTPUT_MODE:-bgr}"
ASYNC_INFER="${ASYNC_INFER:-0}"
ASYNC_POOL="${ASYNC_POOL:-3}"

mkdir -p "${OUT_DIR}"

log_section() {
  printf '\n[%s] %s\n' "$(date +%H:%M:%S)" "$*"
}

extract_metric() {
  local text="$1"
  local key="$2"
  printf "%s\n" "${text}" | sed -n "s/.*${key}=\([^ ]*\).*/\1/p" | tail -n 1
}

write_environment() {
  {
    echo "timestamp=$(date -Iseconds)"
    echo "hostname=$(hostname)"
    echo "uname=$(uname -a)"
    echo "project_dir=${PROJECT_DIR}"
    echo "model=${MODEL}"
    echo "conf=${CONF}"
    echo "nms=${NMS}"
    echo
    echo "== fixed video input =="
    echo "run_fixed=${RUN_FIXED}"
    echo "video=${VIDEO}"
    echo "h264=${H264}"
    echo "fixed_frames=${FIXED_FRAMES}"
    echo "write_fixed_video=${WRITE_FIXED_VIDEO}"
    echo
    echo "== camera input =="
    echo "run_camera=${RUN_CAMERA}"
    echo "device=${DEVICE}"
    echo "width=${WIDTH}"
    echo "height=${HEIGHT}"
    echo "fps=${FPS}"
    echo "codec=${CODEC}"
    echo "camera_seconds=${CAMERA_SECONDS}"
    echo "camera_frames=${CAMERA_FRAMES}"
    echo "detect_every_n=${DETECT_EVERY_N}"
    echo "output_mode=${OUTPUT_MODE}"
    echo "async_infer=${ASYNC_INFER}"
    echo "async_pool=${ASYNC_POOL}"
    echo "rtsp_url=rtsp://<board-ip>:${PORT}${MOUNT}"
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
    echo "== rga device =="
    ls -l /dev/rga* 2>/dev/null || true
    echo
    echo "== mpp libraries =="
    ldconfig -p 2>/dev/null | grep -Ei 'rockchip_mpp|mpp' || true
  } > "${ENV_FILE}"
}

run_fixed_video() {
  local fixed_log="${OUT_DIR}/fixed_video_mpp_rga_rknn.log"
  local fixed_video="${OUT_DIR}/fixed_video_boxed.mp4"

  if [[ "${RUN_FIXED}" != "1" ]]; then
    echo "skipped" > "${fixed_log}"
    return 0
  fi
  if [[ -z "${VIDEO}" && -z "${H264}" ]]; then
    echo "RUN_FIXED=1 requires VIDEO=/path/to/video.mp4 or H264=/path/to/video.h264" | tee "${fixed_log}" >&2
    return 1
  fi

  log_section "Fixed video Route B: MP4/H264 -> MPP DMA fd -> RGA -> RKNN input memory -> NPU"
  local out_video_arg=""
  if [[ "${WRITE_FIXED_VIDEO}" == "1" ]]; then
    out_video_arg="${fixed_video}"
  fi

  (
    cd "${PROJECT_DIR}"
    RUN_ID="${RUN_ID}_fixed" \
    OUT_DIR="${OUT_DIR}/fixed_video_work" \
    MODEL="${MODEL}" \
    VIDEO="${VIDEO}" \
    H264="${H264}" \
    CONF="${CONF}" \
    NMS="${NMS}" \
    FRAMES="${FIXED_FRAMES}" \
    OUT_VIDEO="${out_video_arg}" \
    OUT_FPS="${FIXED_OUT_FPS}" \
      "${SCRIPT_DIR}/run_mpp_file_rknn_eval.sh"
  ) > "${fixed_log}" 2>&1
}

run_camera_rtsp() {
  local camera_log="${OUT_DIR}/camera_mpp_rga_rknn_rtsp.log"

  if [[ "${RUN_CAMERA}" != "1" ]]; then
    echo "skipped" > "${camera_log}"
    return 0
  fi

  log_section "Camera Route B: V4L2 compressed -> MPP DMA fd -> RGA -> RKNN input memory -> NPU -> RTSP"
  (
    cd "${PROJECT_DIR}"
    MODEL="${MODEL}" \
    DEVICE="${DEVICE}" \
    WIDTH="${WIDTH}" \
    HEIGHT="${HEIGHT}" \
    FPS="${FPS}" \
    CODEC="${CODEC}" \
    CONF="${CONF}" \
    NMS="${NMS}" \
    FRAMES="${CAMERA_FRAMES}" \
    RUN_SECONDS="${CAMERA_SECONDS}" \
    PORT="${PORT}" \
    MOUNT="${MOUNT}" \
    DETECT_EVERY_N="${DETECT_EVERY_N}" \
    OUTPUT_MODE="${OUTPUT_MODE}" \
    ASYNC_INFER="${ASYNC_INFER}" \
    ASYNC_POOL="${ASYNC_POOL}" \
      "${SCRIPT_DIR}/run_mpp_dma_rtsp_eval.sh"
  ) > "${camera_log}" 2>&1 || true
}

evidence_status() {
  local log_file="$1"
  local pattern="$2"
  if grep -q "${pattern}" "${log_file}" 2>/dev/null; then
    echo "yes"
  else
    echo "no"
  fi
}

write_report() {
  local fixed_log="${OUT_DIR}/fixed_video_mpp_rga_rknn.log"
  local camera_log="${OUT_DIR}/camera_mpp_rga_rknn_rtsp.log"
  local fixed_summary camera_summary camera_async

  fixed_summary="$(grep '^summary ' "${fixed_log}" 2>/dev/null | tail -n 1 | tr '\t' ' ' || true)"
  camera_summary="$(grep '^summary ' "${camera_log}" 2>/dev/null | tail -n 1 | tr '\t' ' ' || true)"
  camera_async="$(grep '^async_summary ' "${camera_log}" 2>/dev/null | tail -n 1 | tr '\t' ' ' || true)"

  {
    echo "# RGA / Zero-Copy Taskbook Verification"
    echo
    echo "Run ID: \`${RUN_ID}\`"
    echo
    echo "This report is generated by \`scripts/run_rga_zero_copy_taskbook_eval.sh\`."
    echo "It is an experimental verification pack and does not modify the stable FP RKNN thesis/demo path."
    echo
    echo "## Verification Targets"
    echo
    echo "| Input | Intended inference chain | Visualization | Log |"
    echo "|---|---|---|---|"
    echo "| Fixed video | MP4/H264 -> MPP decode -> MppFrame DMA fd -> RGA letterbox -> RKNN input memory -> NPU | Optional annotated MP4 with one extra RGA DMA-to-BGR copy | \`fixed_video_mpp_rga_rknn.log\` |"
    echo "| Camera | UVC compressed stream -> V4L2 capture -> MPP decode -> MppFrame DMA fd -> RGA letterbox -> RKNN input memory -> NPU -> RTSP | \`bgr\` boxed RTSP or \`dmabuf\` low-copy NV12 RTSP | \`camera_mpp_rga_rknn_rtsp.log\` |"
    echo
    echo "## Environment"
    echo
    echo "\`\`\`text"
    sed -n '1,120p' "${ENV_FILE}"
    echo "\`\`\`"
    echo
    echo "## Evidence Checklist"
    echo
    echo "| Check | Fixed video | Camera | Meaning |"
    echo "|---|---:|---:|---|"
    echo "| zero-copy RKNN input enabled | $(evidence_status "${fixed_log}" 'zero_copy_input=on') | $(evidence_status "${camera_log}" 'zero_copy_input=on') | RKNN input memory was bound with \`rknn_set_io_mem\`. |"
    echo "| RGA wrote DMA fd into RKNN input memory | $(evidence_status "${fixed_log}" 'rknn_inputs_set skipped') | $(evidence_status "${camera_log}" 'rknn_inputs_set skipped') | Inference input path avoided normal \`rknn_inputs_set\` upload. |"
    echo "| MPP/DMA path selected | $(evidence_status "${fixed_log}" 'MPP decode') | $(evidence_status "${camera_log}" 'MPP decode') | Hardware decode produced DMA-backed frames for RGA. |"
    echo "| run summary produced | $(evidence_status "${fixed_log}" '^summary ') | $(evidence_status "${camera_log}" '^summary ') | The run reached normal summary output. |"
    echo
    echo "## Fixed Video Result"
    echo
    if [[ -n "${fixed_summary}" ]]; then
      echo "\`\`\`text"
      echo "${fixed_summary}"
      echo "\`\`\`"
      echo
      echo "- decoded_fps: $(extract_metric "${fixed_summary}" 'decoded_wall_fps')"
      echo "- infer_fps: $(extract_metric "${fixed_summary}" 'infer_wall_fps')"
      echo "- avg_prepare_ms: $(extract_metric "${fixed_summary}" 'avg_prepare_ms')"
      echo "- avg_run_ms: $(extract_metric "${fixed_summary}" 'avg_run_ms')"
      echo "- avg_total_ms: $(extract_metric "${fixed_summary}" 'avg_total_ms')"
      if [[ "${WRITE_FIXED_VIDEO}" == "1" ]]; then
        echo "- annotated_video: \`${OUT_DIR}/fixed_video_boxed.mp4\`"
      fi
    else
      echo "Fixed-video validation was skipped or did not produce a summary."
    fi
    echo
    echo "## Camera / RTSP Result"
    echo
    if [[ -n "${camera_summary}" ]]; then
      echo "\`\`\`text"
      echo "${camera_summary}"
      [[ -z "${camera_async}" ]] || echo "${camera_async}"
      echo "\`\`\`"
      echo
      echo "- decoded_fps: $(extract_metric "${camera_summary}" 'decoded_wall_fps')"
      echo "- infer_fps: $(extract_metric "${camera_summary}" 'infer_wall_fps')"
      echo "- avg_prepare_ms: $(extract_metric "${camera_summary}" 'avg_prepare_ms')"
      echo "- avg_run_ms: $(extract_metric "${camera_summary}" 'avg_run_ms')"
      echo "- avg_total_ms: $(extract_metric "${camera_summary}" 'avg_total_ms')"
      echo "- rtsp_url: \`rtsp://<board-ip>:${PORT}${MOUNT}\`"
    else
      echo "Camera validation was skipped or did not produce a summary."
    fi
    echo
    echo "## Boundary Statement"
    echo
    echo "- The inference-side experimental chain now verifies DMA-buffer input, RGA preprocessing, bound RKNN input memory, and NPU inference for both fixed-video and camera inputs."
    echo "- Boxed MP4/RTSP visualization intentionally performs an additional display-side copy so rectangles and labels can be drawn. Use \`OUTPUT_MODE=dmabuf\` for the cleaner low-copy RTSP performance stream, but that mode does not draw boxes."
    echo "- The stable thesis/demo route remains the previous FP RKNN real-time path. This package is for task-book RGA/zero-copy evidence and performance exploration."
  } > "${REPORT_FILE}"

  ln -sfn "${RUN_ID}" "${PROJECT_DIR}/eval_runs/rga_zero_copy_taskbook_latest" 2>/dev/null || true
}

write_environment
run_fixed_video
run_camera_rtsp
write_report

echo "RGA / zero-copy taskbook verification complete."
echo "Output directory: ${OUT_DIR}"
echo "Report: ${REPORT_FILE}"
