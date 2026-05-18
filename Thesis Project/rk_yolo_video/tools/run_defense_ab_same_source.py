#!/usr/bin/env python3
"""Run same-source OpenCV vs Route-B board evaluation and fetch artifacts.

This helper is intentionally kept outside the runtime sources. It uploads one
fixed MP4/H264 pair to the RK3588 board, runs the stable OpenCV path and the
Route-B 3-worker path, samples NPU load during each run, then downloads the
videos/logs for local dashboard rendering.
"""

from __future__ import annotations

import argparse
import os
import posixpath
import socket
import sys
import time
from pathlib import Path

import paramiko


BOARD_HOST = "192.168.2.156"
BOARD_USER = "ubuntu"
BOARD_PASS = "ubuntu"

REMOTE_BASE = "/home/ubuntu/eclipse-workspace/eclipse-workspace/Thesis Project/rk_yolo_video"
REMOTE_MODEL = (
    "/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/"
    "drone_hard_v1_ft_640_20e/weights/"
    "best.end2end_false.op12.rk3588.fp.v232.hard_v1.rknn"
)


def q(path: str) -> str:
    return "'" + path.replace("'", "'\"'\"'") + "'"


def connect(host: str, user: str, password: str) -> paramiko.SSHClient:
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(
        host,
        username=user,
        password=password,
        timeout=12,
        banner_timeout=12,
        auth_timeout=12,
        look_for_keys=False,
        allow_agent=False,
    )
    return client


def run(client: paramiko.SSHClient, command: str, timeout: int | None = None) -> tuple[int, str, str]:
    stdin, stdout, stderr = client.exec_command(command, timeout=timeout)
    del stdin
    out = stdout.read().decode("utf-8", "replace")
    err = stderr.read().decode("utf-8", "replace")
    code = stdout.channel.recv_exit_status()
    return code, out, err


def upload(sftp: paramiko.SFTPClient, local: Path, remote: str) -> None:
    print(f"[upload] {local.name} -> {remote}")
    sftp.put(str(local), remote)


def download_if_exists(sftp: paramiko.SFTPClient, remote: str, local: Path) -> None:
    try:
        sftp.stat(remote)
    except FileNotFoundError:
        print(f"[missing] {remote}")
        return
    local.parent.mkdir(parents=True, exist_ok=True)
    print(f"[download] {remote} -> {local}")
    sftp.get(remote, str(local))


def remote_script(remote_run: str) -> str:
    base = q(REMOTE_BASE)
    model = q(REMOTE_MODEL)
    run_dir = q(remote_run)
    opencv_cmd = (
        "env RK_YOLO_PROFILE=1 RK_YOLO_PREPROCESS=opencv "
        "RK_YOLO_ALARM_OVERLAY=1 RK_YOLO_DYNAMIC_ROI=0 "
        f"{base}/build/rk_yolo_video "
        f"{run_dir}/same_source_90s.mp4 "
        f"{run_dir}/opencv_same_source.mp4 "
        f"{model} 0.24 0.45 "
        f"{run_dir}/opencv_detections.csv "
        f"{run_dir}/opencv_roi.jsonl "
        f"{run_dir}/opencv_alarm.csv"
    )
    routeb_cmd = (
        "env RK_YOLO_NPU_WORKERS=3 RK_YOLO_VIS_MULTI=0 "
        "RK_YOLO_ROUTE_B_STABLE=1 RK_YOLO_ROUTE_B_FAST_FOLLOW=1 "
        "RK_YOLO_ROUTE_B_DEDUP=1 "
        "RK_YOLO_IGNORE_OSD=1 "
        f"{base}/build_defense_route_b2/rk_yolo_mpp_file_demo "
        f"{model} "
        f"{run_dir}/same_source_90s.annexb.h264 "
        f"0.24 0.45 1800 4096 "
        f"{run_dir}/routeb_3worker_same_source.mp4 "
        "20 1"
    )
    return f"""set -e
RUN_DIR={run_dir}
cd {base}
mkdir -p "$RUN_DIR"
cat > "$RUN_DIR/sample_npu.sh" <<'SH'
#!/usr/bin/env bash
set +e
OUT="$1"
echo "ts,core0,core1,core2,devfreq_load,devfreq_hz" > "$OUT"
while true; do
  ts=$(date +%s.%3N)
  raw=$(echo ubuntu | sudo -S -p '' cat /sys/kernel/debug/rknpu/load 2>/dev/null | tr '\\n' ' ')
  c0=$(echo "$raw" | sed -n 's/.*Core0:[[:space:]]*\\([0-9]\\+\\)%.*/\\1/p')
  c1=$(echo "$raw" | sed -n 's/.*Core1:[[:space:]]*\\([0-9]\\+\\)%.*/\\1/p')
  c2=$(echo "$raw" | sed -n 's/.*Core2:[[:space:]]*\\([0-9]\\+\\)%.*/\\1/p')
  if [ -z "$c0$c1$c2" ]; then
    raw2=$(cat /sys/class/devfreq/fdab0000.npu/load 2>/dev/null)
    load=$(echo "$raw2" | awk '{{print $1}}')
  else
    load=0
  fi
  freq=$(cat /sys/class/devfreq/fdab0000.npu/cur_freq 2>/dev/null)
  echo "$ts,${{c0:-0}},${{c1:-0}},${{c2:-0}},${{load:-0}},${{freq:-0}}" >> "$OUT"
  sleep 0.25
done
SH
chmod +x "$RUN_DIR/sample_npu.sh"

echo "[board] checking binaries"
test -x {base}/build/rk_yolo_video
test -x {base}/build_defense_route_b2/rk_yolo_mpp_file_demo
test -f {model}

echo "[board] running OpenCV stable baseline"
"$RUN_DIR/sample_npu.sh" "$RUN_DIR/npu_load_opencv.csv" &
SAMPLE_PID=$!
set +e
{opencv_cmd} > "$RUN_DIR/opencv.log" 2>&1
OPENCV_CODE=$?
kill "$SAMPLE_PID" 2>/dev/null
wait "$SAMPLE_PID" 2>/dev/null
set -e
echo "$OPENCV_CODE" > "$RUN_DIR/opencv.exit"
if [ "$OPENCV_CODE" -ne 0 ]; then
  echo "[board] OpenCV run failed: $OPENCV_CODE"
  tail -120 "$RUN_DIR/opencv.log" || true
  exit "$OPENCV_CODE"
fi

sleep 1
echo "[board] running Route-B 3-worker/3-core path"
"$RUN_DIR/sample_npu.sh" "$RUN_DIR/npu_load_routeb.csv" &
SAMPLE_PID=$!
set +e
{routeb_cmd} > "$RUN_DIR/routeb.log" 2>&1
ROUTEB_CODE=$?
kill "$SAMPLE_PID" 2>/dev/null
wait "$SAMPLE_PID" 2>/dev/null
set -e
echo "$ROUTEB_CODE" > "$RUN_DIR/routeb.exit"
if [ "$ROUTEB_CODE" -ne 0 ]; then
  echo "[board] Route-B run failed: $ROUTEB_CODE"
  tail -160 "$RUN_DIR/routeb.log" || true
  exit "$ROUTEB_CODE"
fi

echo "[board] done"
ls -lh "$RUN_DIR"
"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-dir", required=True, type=Path)
    parser.add_argument("--host", default=BOARD_HOST)
    parser.add_argument("--user", default=BOARD_USER)
    parser.add_argument("--password", default=BOARD_PASS)
    args = parser.parse_args()

    local_run = args.run_dir.resolve()
    mp4 = local_run / "same_source_90s.mp4"
    h264 = local_run / "same_source_90s.annexb.h264"
    if not mp4.exists() or not h264.exists():
        raise SystemExit(f"missing input files in {local_run}")

    remote_run = posixpath.join(REMOTE_BASE, "eval_runs", local_run.name)
    socket.setdefaulttimeout(15)
    client = connect(args.host, args.user, args.password)
    try:
        sftp = client.open_sftp()
        code, out, err = run(client, f"mkdir -p {q(remote_run)}")
        if code:
            raise RuntimeError(err or out)
        upload(sftp, mp4, posixpath.join(remote_run, mp4.name))
        upload(sftp, h264, posixpath.join(remote_run, h264.name))

        print("[run] launching board evaluation")
        start = time.time()
        code, out, err = run(client, remote_script(remote_run), timeout=900)
        (local_run / "board_eval_stdout.txt").write_text(out, encoding="utf-8")
        (local_run / "board_eval_stderr.txt").write_text(err, encoding="utf-8")
        print(out)
        if err:
            print(err, file=sys.stderr)
        if code:
            raise SystemExit(code)
        print(f"[run] board evaluation finished in {time.time() - start:.1f}s")

        artifacts = [
            "opencv_same_source.mp4",
            "routeb_3worker_same_source.mp4",
            "opencv.log",
            "routeb.log",
            "npu_load_opencv.csv",
            "npu_load_routeb.csv",
            "opencv_detections.csv",
            "opencv_roi.jsonl",
            "opencv_alarm.csv",
            "opencv.exit",
            "routeb.exit",
        ]
        for name in artifacts:
            download_if_exists(sftp, posixpath.join(remote_run, name), local_run / name)
    finally:
        client.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
