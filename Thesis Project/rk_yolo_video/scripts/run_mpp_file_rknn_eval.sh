#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build_mpp_dma_experiment}"
MODEL="${MODEL:-/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_hard_v1_ft_640_20e/weights/best.end2end_false.op12.rk3588.fp.v232.hard_v1.rknn}"
VIDEO="${VIDEO:-}"
H264="${H264:-}"
CONF="${CONF:-0.24}"
NMS="${NMS:-0.45}"
FRAMES="${FRAMES:-300}"
# Smaller H.264 feed chunks are safer for MPP parser draining on arbitrary Annex-B
# streams. 4096 was validated to recover the full 240/240 frames on a stream that
# only decoded 192/240 with 65536-byte chunks.
CHUNK_SIZE="${CHUNK_SIZE:-4096}"
OUT_VIDEO="${OUT_VIDEO:-}"
OUT_FPS="${OUT_FPS:-20}"
DETECT_EVERY_N="${DETECT_EVERY_N:-3}"
ROUTE_B_FORCE_LOW_LATENCY_H264="${ROUTE_B_FORCE_LOW_LATENCY_H264:-1}"
RKNN_INCLUDE_DIR="${RKNN_INCLUDE_DIR:-/home/ubuntu/eclipse-workspace/eclipse-workspace/encoder/include}"
RKNN_LIBRARY="${RKNN_LIBRARY:-/usr/lib/librknnrt.so}"

if [[ -z "${VIDEO}" && -z "${H264}" ]]; then
  echo "Set VIDEO=/path/to/input.mp4 or H264=/path/to/input.h264" >&2
  exit 1
fi

if [[ ! -s "${MODEL}" ]]; then
  FALLBACK_MODEL="/home/ubuntu/eclipse-workspace/eclipse-workspace_locked_demo/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn"
  if [[ -s "${FALLBACK_MODEL}" ]]; then
    echo "Route-B: MODEL not found, falling back to locked FP RKNN model: ${FALLBACK_MODEL}" >&2
    MODEL="${FALLBACK_MODEL}"
  else
    echo "MODEL not found: ${MODEL}" >&2
    exit 1
  fi
fi

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DRKNN_INCLUDE_DIR="${RKNN_INCLUDE_DIR}" \
  -DRKNN_LIBRARY="${RKNN_LIBRARY}"
cmake --build "${BUILD_DIR}" -j"$(nproc)" --target rk_yolo_mpp_file_demo

if [[ -z "${H264}" ]]; then
  if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "ffmpeg is required to extract an Annex-B H.264 elementary stream from VIDEO." >&2
    exit 1
  fi
  if ! command -v ffprobe >/dev/null 2>&1; then
    echo "ffprobe is required to inspect VIDEO codec before route-B conversion." >&2
    exit 1
  fi
  OUT_DIR="${OUT_DIR:-${ROOT_DIR}/eval_runs/route_b_file_inputs}"
  mkdir -p "${OUT_DIR}"
  base="$(basename "${VIDEO}")"
  if [[ "${ROUTE_B_FORCE_LOW_LATENCY_H264}" == "1" || \
        "${ROUTE_B_FORCE_LOW_LATENCY_H264}" == "true" || \
        "${ROUTE_B_FORCE_LOW_LATENCY_H264}" == "on" ]]; then
    H264="${OUT_DIR}/${base%.*}.low_latency.annexb.h264"
  else
    H264="${OUT_DIR}/${base%.*}.annexb.h264"
  fi
  if [[ ! -s "${H264}" ]]; then
    codec="$(ffprobe -v error -select_streams v:0 -show_entries stream=codec_name \
      -of default=nw=1:nk=1 "${VIDEO}" | head -n 1)"
    if [[ "${ROUTE_B_FORCE_LOW_LATENCY_H264}" == "1" || \
          "${ROUTE_B_FORCE_LOW_LATENCY_H264}" == "true" || \
          "${ROUTE_B_FORCE_LOW_LATENCY_H264}" == "on" ]]; then
      echo "Route-B: transcoding VIDEO to low-latency H.264 Annex-B (bframes=0)." >&2
      ffmpeg -nostdin -hide_banner -loglevel warning -y -i "${VIDEO}" \
        -an -c:v libx264 -preset ultrafast -tune zerolatency \
        -x264-params "bframes=0:keyint=30:min-keyint=30:scenecut=0" \
        -bf 0 -pix_fmt yuv420p -f h264 "${H264}"
    elif [[ "${codec}" == "h264" ]]; then
      echo "Route-B: extracting native H.264 Annex-B stream." >&2
      ffmpeg -nostdin -hide_banner -loglevel warning -y -i "${VIDEO}" \
        -an -c:v copy -bsf:v h264_mp4toannexb -f h264 "${H264}"
    else
      echo "VIDEO codec is ${codec}; transcoding to H.264 Annex-B for MPP." >&2
      ffmpeg -nostdin -hide_banner -loglevel warning -y -i "${VIDEO}" \
        -an -c:v libx264 -preset ultrafast -tune zerolatency -pix_fmt yuv420p \
        -f h264 "${H264}"
    fi
  fi
fi

if [[ -n "${OUT_VIDEO}" ]]; then
  mkdir -p "$(dirname "${OUT_VIDEO}")"
fi

"${BUILD_DIR}/rk_yolo_mpp_file_demo" \
  "${MODEL}" "${H264}" "${CONF}" "${NMS}" "${FRAMES}" "${CHUNK_SIZE}" \
  "${OUT_VIDEO}" "${OUT_FPS}" "${DETECT_EVERY_N}"
