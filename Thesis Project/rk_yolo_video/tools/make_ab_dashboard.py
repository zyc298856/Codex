#!/usr/bin/env python3
"""Render a same-source A/B dashboard video from board logs.

The dashboard is generated offline, but every numeric panel is computed from
the board-side logs and NPU load samples produced during the actual run.
"""

from __future__ import annotations

import argparse
import csv
import math
import re
import statistics
import subprocess
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


OUT_W, OUT_H = 1920, 1080
FPS = 20
VIDEO_W, VIDEO_H = 880, 495
LEFT_X, RIGHT_X = 40, 1000
VIDEO_Y = 135
PANEL_Y = 650
PANEL_H = 370


@dataclass
class FrameMetric:
    frame: int
    run_ms: float
    total_ms: float
    prepare_ms: float = 0.0
    detections: int = 0
    best_score: float = 0.0


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    candidates = [
        r"C:\Windows\Fonts\msyhbd.ttc" if bold else r"C:\Windows\Fonts\msyh.ttc",
        r"C:\Windows\Fonts\simhei.ttf",
        r"C:\Windows\Fonts\simsun.ttc",
    ]
    for path in candidates:
        try:
            return ImageFont.truetype(path, size=size)
        except OSError:
            pass
    return ImageFont.load_default()


FONT_TITLE = font(34, True)
FONT_SUBTITLE = font(22, False)
FONT_LABEL = font(22, True)
FONT_TEXT = font(21, False)
FONT_SMALL = font(17, False)
FONT_NUMBER = font(28, True)


def mean(values: list[float], default: float = 0.0) -> float:
    vals = [v for v in values if v > 0]
    return statistics.fmean(vals) if vals else default


def parse_opencv_log(path: Path) -> dict[int, FrameMetric]:
    metrics: dict[int, FrameMetric] = {}
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if not line.startswith("profile_csv,"):
                continue
            parts = line.strip().split(",")
            if len(parts) < 12 or parts[1] == "frame":
                continue
            try:
                frame = int(parts[1])
                prepare = float(parts[3])
                input_set = float(parts[4])
                run_ms = float(parts[5])
                outputs_get = float(parts[6])
                decode_ms = float(parts[7])
                render_ms = float(parts[9])
                total = float(parts[10])
                det = int(float(parts[11])) if len(parts) > 11 else 0
            except ValueError:
                continue
            # Keep total_work as the actual sequential cost, but expose the
            # input-set heavy path in prepare_ms for the panel.
            metrics[frame] = FrameMetric(
                frame=frame,
                run_ms=run_ms,
                total_ms=total,
                prepare_ms=prepare + input_set + outputs_get + decode_ms + render_ms,
                detections=det,
            )
    return metrics


ROUTEB_RE = re.compile(
    r"stream_frame=(?P<frame>\d+)\s+worker=(?P<worker>\d+)\s+det=(?P<det>\d+).*?"
    r"prepare_ms=(?P<prepare>[\d.]+)\s+run_ms=(?P<run>[\d.]+)\s+"
    r"total_ms=(?P<total>[\d.]+)(?:\s+best_score=(?P<score>[\d.]+))?"
)


def parse_routeb_log(path: Path) -> tuple[dict[int, FrameMetric], int, float]:
    metrics: dict[int, FrameMetric] = {}
    workers = 3
    wall_fps = 0.0
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = ROUTEB_RE.search(line)
            if m:
                frame = int(m.group("frame"))
                metrics[frame] = FrameMetric(
                    frame=frame,
                    run_ms=float(m.group("run")),
                    total_ms=float(m.group("total")),
                    prepare_ms=float(m.group("prepare")),
                    detections=int(m.group("det")),
                    best_score=float(m.group("score") or 0.0),
                )
                continue
            if line.startswith("summary "):
                w = re.search(r"parallel_workers=(\d+)", line)
                fps = re.search(r"npu_fps=([\d.]+)", line)
                if w:
                    workers = int(w.group(1))
                if fps:
                    wall_fps = float(fps.group(1))
    return metrics, workers, wall_fps


def parse_npu_csv(path: Path) -> list[tuple[float, float, float, float]]:
    rows: list[tuple[float, float, float, float]] = []
    with path.open("r", encoding="utf-8", errors="replace", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                rows.append(
                    (
                        float(row.get("ts") or 0.0),
                        float(row.get("core0") or 0.0),
                        float(row.get("core1") or 0.0),
                        float(row.get("core2") or 0.0),
                    )
                )
            except ValueError:
                continue
    return rows


def rolling(metrics: dict[int, FrameMetric], frame: int, window: int = 60) -> list[FrameMetric]:
    start = max(1, frame - window + 1)
    return [metrics[i] for i in range(start, frame + 1) if i in metrics]


def metric_at(metrics: dict[int, FrameMetric], frame: int) -> FrameMetric:
    if frame in metrics:
        return metrics[frame]
    # Fall back to nearest previous frame, because Route-B logs may arrive out
    # of order but visualization still emits every source frame.
    for i in range(frame - 1, max(0, frame - 90), -1):
        if i in metrics:
            return metrics[i]
    return FrameMetric(frame=frame, run_ms=0, total_ms=0)


def core_at(rows: list[tuple[float, float, float, float]], frame: int, total_frames: int) -> tuple[float, float, float]:
    if not rows:
        return (0.0, 0.0, 0.0)
    idx = min(len(rows) - 1, max(0, int((frame - 1) / max(1, total_frames - 1) * (len(rows) - 1))))
    _, c0, c1, c2 = rows[idx]
    return c0, c1, c2


def open_reader(video: Path) -> subprocess.Popen:
    return subprocess.Popen(
        [
            "ffmpeg",
            "-v",
            "error",
            "-i",
            str(video),
            "-vf",
            f"scale={VIDEO_W}:{VIDEO_H}",
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "-",
        ],
        stdout=subprocess.PIPE,
    )


def open_writer(output: Path) -> subprocess.Popen:
    return subprocess.Popen(
        [
            "ffmpeg",
            "-y",
            "-v",
            "error",
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "-s",
            f"{OUT_W}x{OUT_H}",
            "-r",
            str(FPS),
            "-i",
            "-",
            "-c:v",
            "libx264",
            "-preset",
            "veryfast",
            "-pix_fmt",
            "yuv420p",
            str(output),
        ],
        stdin=subprocess.PIPE,
    )


def read_frame(proc: subprocess.Popen) -> Image.Image | None:
    assert proc.stdout is not None
    size = VIDEO_W * VIDEO_H * 3
    data = proc.stdout.read(size)
    if len(data) != size:
        return None
    return Image.frombytes("RGB", (VIDEO_W, VIDEO_H), data)


def draw_card(draw: ImageDraw.ImageDraw, x: int, y: int, w: int, h: int, title: str, value: str, color: tuple[int, int, int]) -> None:
    draw.rounded_rectangle((x, y, x + w, y + h), radius=18, fill=(245, 248, 251), outline=(205, 216, 226), width=2)
    draw.text((x + 18, y + 12), title, font=FONT_SMALL, fill=(70, 82, 95))
    draw.text((x + 18, y + 40), value, font=FONT_NUMBER, fill=color)


def draw_core_bars(draw: ImageDraw.ImageDraw, x: int, y: int, cores: tuple[float, float, float], active_color: tuple[int, int, int]) -> None:
    labels = ["Core0", "Core1", "Core2"]
    for idx, (label, val) in enumerate(zip(labels, cores)):
        yy = y + idx * 38
        draw.text((x, yy - 2), label, font=FONT_SMALL, fill=(72, 81, 91))
        bar_x = x + 80
        bar_w = 260
        draw.rounded_rectangle((bar_x, yy, bar_x + bar_w, yy + 18), radius=9, fill=(224, 231, 238))
        fill_w = int(bar_w * max(0, min(100, val)) / 100)
        draw.rounded_rectangle((bar_x, yy, bar_x + fill_w, yy + 18), radius=9, fill=active_color)
        draw.text((bar_x + bar_w + 12, yy - 2), f"{val:.0f}%", font=FONT_SMALL, fill=(30, 45, 60))


def draw_panel(
    img: Image.Image,
    x: int,
    title: str,
    subtitle: str,
    metrics: dict[int, FrameMetric],
    frame: int,
    workers: int,
    wall_fps: float,
    cores: tuple[float, float, float],
    theme: tuple[int, int, int],
) -> None:
    draw = ImageDraw.Draw(img)
    draw.rounded_rectangle((x, PANEL_Y, x + VIDEO_W, PANEL_Y + PANEL_H), radius=22, fill=(255, 255, 255), outline=(205, 215, 224), width=2)
    draw.text((x + 24, PANEL_Y + 18), title, font=FONT_LABEL, fill=theme)
    draw.text((x + 24, PANEL_Y + 52), subtitle, font=FONT_SMALL, fill=(80, 91, 104))

    win = rolling(metrics, frame)
    run_avg = mean([m.run_ms for m in win], metric_at(metrics, frame).run_ms)
    total_avg = mean([m.total_ms for m in win], metric_at(metrics, frame).total_ms)
    npu_fps = (workers * 1000.0 / run_avg) if run_avg > 0 else 0.0
    est_120 = (120.0 / npu_fps) if npu_fps > 0 else 0.0
    total_fps = (workers * 1000.0 / total_avg) if total_avg > 0 else 0.0

    card_y = PANEL_Y + 90
    draw_card(draw, x + 24, card_y, 190, 95, "NPU FPS", f"{npu_fps:5.1f}", theme)
    draw_card(draw, x + 232, card_y, 215, 95, "平均 rknn_run", f"{run_avg:5.1f} ms", theme)
    draw_card(draw, x + 465, card_y, 210, 95, "120帧折算", f"{est_120:4.1f} s", theme)
    draw_card(draw, x + 694, card_y, 160, 95, "链路 FPS", f"{total_fps:4.1f}", theme)

    draw.text((x + 24, card_y + 120), "NPU核心使用率（板端 /sys/kernel/debug/rknpu/load 实采）", font=FONT_SMALL, fill=(62, 74, 88))
    draw_core_bars(draw, x + 30, card_y + 154, cores, theme)

    draw.text((x + 430, card_y + 155), "同源视频、同帧回放、同指标对比", font=FONT_TEXT, fill=(35, 47, 60))
    draw.text((x + 430, card_y + 195), f"并行 context：{workers}", font=FONT_TEXT, fill=(35, 47, 60))
    draw.text((x + 430, card_y + 235), f"当前帧：{frame}", font=FONT_TEXT, fill=(35, 47, 60))


def draw_header(draw: ImageDraw.ImageDraw, frame: int, total_frames: int) -> None:
    draw.rectangle((0, 0, OUT_W, OUT_H), fill=(238, 242, 246))
    draw.text((40, 28), "RK3588 固定视频同源对比：OpenCV 稳定版 vs Route B 三核并行方案", font=FONT_TITLE, fill=(24, 43, 64))
    draw.text(
        (40, 70),
        "指标来源：真实板端运行日志 + NPU 分核负载采样；用于答辩展示硬件优化差异",
        font=FONT_SUBTITLE,
        fill=(80, 95, 111),
    )
    pct = frame / max(1, total_frames)
    draw.rounded_rectangle((1320, 47, 1870, 68), radius=10, fill=(212, 221, 230))
    draw.rounded_rectangle((1320, 47, 1320 + int(550 * pct), 68), radius=10, fill=(34, 125, 205))
    draw.text((1320, 74), f"{frame}/{total_frames}  ({pct * 100:.1f}%)", font=FONT_SMALL, fill=(70, 82, 95))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-dir", required=True, type=Path)
    parser.add_argument("--output", default="opencv_vs_routeb_3core_same_source_dashboard_cn.mp4")
    parser.add_argument("--max-frames", type=int, default=1800)
    args = parser.parse_args()

    run = args.run_dir.resolve()
    opencv_video = run / "opencv_same_source.mp4"
    routeb_video = run / "routeb_3worker_same_source.mp4"
    opencv_metrics = parse_opencv_log(run / "opencv.log")
    routeb_metrics, routeb_workers, routeb_wall_fps = parse_routeb_log(run / "routeb.log")
    opencv_cores = parse_npu_csv(run / "npu_load_opencv.csv")
    routeb_cores = parse_npu_csv(run / "npu_load_routeb.csv")

    total_frames = min(args.max_frames, max(opencv_metrics), max(routeb_metrics))
    output = run / args.output

    left = open_reader(opencv_video)
    right = open_reader(routeb_video)
    writer = open_writer(output)
    assert writer.stdin is not None

    try:
        for frame in range(1, total_frames + 1):
            lf = read_frame(left)
            rf = read_frame(right)
            if lf is None or rf is None:
                total_frames = frame - 1
                break
            canvas = Image.new("RGB", (OUT_W, OUT_H), (238, 242, 246))
            draw = ImageDraw.Draw(canvas)
            draw_header(draw, frame, total_frames)
            draw.text((LEFT_X, VIDEO_Y - 35), "OpenCV 稳定版（单 context / CPU内存输入）", font=FONT_LABEL, fill=(30, 92, 145))
            draw.text((RIGHT_X, VIDEO_Y - 35), "Route B（MPP + RGA + 3 worker / 三核NPU）", font=FONT_LABEL, fill=(188, 100, 22))
            canvas.paste(lf, (LEFT_X, VIDEO_Y))
            canvas.paste(rf, (RIGHT_X, VIDEO_Y))
            draw.rectangle((LEFT_X, VIDEO_Y, LEFT_X + VIDEO_W, VIDEO_Y + VIDEO_H), outline=(80, 120, 160), width=2)
            draw.rectangle((RIGHT_X, VIDEO_Y, RIGHT_X + VIDEO_W, VIDEO_Y + VIDEO_H), outline=(210, 130, 45), width=2)

            draw_panel(
                canvas,
                LEFT_X,
                "OpenCV 稳定版",
                "稳定可视化基线：顺序输入设置 + 单 context NPU 推理",
                opencv_metrics,
                frame,
                workers=1,
                wall_fps=0.0,
                cores=core_at(opencv_cores, frame, total_frames),
                theme=(30, 107, 166),
            )
            draw_panel(
                canvas,
                RIGHT_X,
                "Route B 三核并行",
                "实验优化链路：MPP解码 + RGA准备 + 3 context 分核推理",
                routeb_metrics,
                frame,
                workers=routeb_workers,
                wall_fps=routeb_wall_fps,
                cores=core_at(routeb_cores, frame, total_frames),
                theme=(207, 117, 27),
            )
            writer.stdin.write(canvas.tobytes())
            if frame % 120 == 0:
                print(f"rendered {frame}/{total_frames}")
    finally:
        if left.stdout:
            left.stdout.close()
        if right.stdout:
            right.stdout.close()
        left.wait()
        right.wait()
        writer.stdin.close()
        writer.wait()

    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
