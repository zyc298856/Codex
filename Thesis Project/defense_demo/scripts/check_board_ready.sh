#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

load_env "${DEMO_ROOT}/config/live_safe.env"

echo "== Project =="
echo "PROJECT_ROOT=${PROJECT_ROOT}"

echo
echo "== Binaries =="
if LIVE_BIN="$(resolve_live_binary 2>/dev/null)"; then
  echo "live binary: ${LIVE_BIN}"
else
  echo "live binary: missing"
fi
if VIDEO_BIN="$(resolve_video_binary 2>/dev/null)"; then
  echo "video binary: ${VIDEO_BIN}"
else
  echo "video binary: missing"
fi

echo
echo "== Model =="
if MODEL="$(resolve_model 2>/dev/null)"; then
  ls -lh "${MODEL}"
else
  echo "model: missing"
fi

echo
echo "== Camera =="
if command -v v4l2-ctl >/dev/null 2>&1; then
  v4l2-ctl --list-devices || true
  echo
  echo "-- controls for ${SOURCE:-/dev/video48} --"
  v4l2-ctl -d "${SOURCE:-/dev/video48}" --list-ctrls || true
else
  echo "v4l2-ctl not found; install v4l-utils if camera controls need inspection."
fi

echo
echo "== GStreamer =="
if command -v gst-inspect-1.0 >/dev/null 2>&1; then
  gst-inspect-1.0 mpph264enc >/dev/null 2>&1 && echo "mpph264enc: ok" || echo "mpph264enc: missing"
  gst-inspect-1.0 rtspclientsink >/dev/null 2>&1 && echo "rtspclientsink: ok" || true
else
  echo "gst-inspect-1.0 not found"
fi

echo
echo "Ready check completed. If binaries, model, camera, and mpph264enc look OK, start scripts/run_live_safe.sh."

