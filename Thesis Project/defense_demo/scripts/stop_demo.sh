#!/usr/bin/env bash
set -euo pipefail

pkill -f rk_yolo_live_rtsp || true
pkill -f rk_yolo_video || true
echo "stopped rk_yolo demo processes if they were running"

