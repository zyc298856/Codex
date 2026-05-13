#!/usr/bin/env python3
"""Offline simulation for the RK YOLO UVC auto-zoom policy.

The live program uses the largest side ratio of the displayed detection box:

    max(box_width / frame_width, box_height / frame_height)

This script mirrors that policy without requiring an RK3588 board or camera. It
generates a synthetic target sequence, applies the same cooldown/lost-frame
logic, and writes CSV/SVG/Markdown outputs for later comparison with board logs.
"""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


@dataclass
class AutoZoomConfig:
    min_zoom: int = 0
    max_zoom: int = 40
    step: int = 5
    cooldown_frames: int = 20
    lost_frames_to_zoom_out: int = 60
    target_min_ratio: float = 0.06
    target_max_ratio: float = 0.22
    initial_zoom: int = 20
    zoom_ratio_gain: float = 1.6


@dataclass
class AutoZoomState:
    current_zoom: int
    last_adjust_frame: int = 0
    lost_frames: int = 0


@dataclass
class FrameInput:
    frame: int
    phase: str
    detected: bool
    base_ratio: float
    confidence: float


def clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


def synthetic_sequence(total_frames: int) -> Iterable[FrameInput]:
    """Generate a repeatable target sequence for strategy checks."""
    for frame in range(total_frames):
        if frame < 20:
            yield FrameInput(frame, "startup_no_target", False, 0.0, 0.0)
        elif frame < 120:
            yield FrameInput(frame, "far_small_target", True, 0.025, 0.72)
        elif frame < 165:
            yield FrameInput(frame, "middle_stable_target", True, 0.070, 0.80)
        elif frame < 225:
            yield FrameInput(frame, "near_large_target", True, 0.190, 0.84)
        elif frame < 295:
            yield FrameInput(frame, "target_lost", False, 0.0, 0.0)
        else:
            yield FrameInput(frame, "reacquired_small_target", True, 0.030, 0.76)


def observed_ratio(base_ratio: float, zoom: int, cfg: AutoZoomConfig) -> float:
    if base_ratio <= 0.0:
        return 0.0
    zoom_span = max(1, cfg.max_zoom - cfg.min_zoom)
    normalized_zoom = clamp((zoom - cfg.min_zoom) / zoom_span, 0.0, 1.0)
    return clamp(base_ratio * (1.0 + cfg.zoom_ratio_gain * normalized_zoom), 0.0, 0.95)


def update_auto_zoom(
    frame_index: int,
    detected: bool,
    largest_ratio: float,
    cfg: AutoZoomConfig,
    state: AutoZoomState,
) -> tuple[str, str]:
    in_cooldown = frame_index < state.last_adjust_frame + cfg.cooldown_frames
    desired_zoom = state.current_zoom
    reason = ""

    if not detected:
        state.lost_frames += 1
        if state.lost_frames >= cfg.lost_frames_to_zoom_out and not in_cooldown:
            desired_zoom = max(cfg.min_zoom, state.current_zoom - cfg.step)
            reason = "target_lost"
            state.lost_frames = 0
    else:
        state.lost_frames = 0
        if not in_cooldown and largest_ratio < cfg.target_min_ratio:
            desired_zoom = min(cfg.max_zoom, state.current_zoom + cfg.step)
            reason = "target_small"
        elif not in_cooldown and largest_ratio > cfg.target_max_ratio:
            desired_zoom = max(cfg.min_zoom, state.current_zoom - cfg.step)
            reason = "target_large"

    if desired_zoom == state.current_zoom or not reason:
        return "hold", "cooldown" if in_cooldown else "within_target_range"

    state.current_zoom = desired_zoom
    state.last_adjust_frame = frame_index
    return "adjust", reason


def run_simulation(cfg: AutoZoomConfig, total_frames: int) -> list[dict[str, object]]:
    state = AutoZoomState(current_zoom=clamp(cfg.initial_zoom, cfg.min_zoom, cfg.max_zoom))
    rows: list[dict[str, object]] = []

    for item in synthetic_sequence(total_frames):
        ratio_before = observed_ratio(item.base_ratio, state.current_zoom, cfg)
        zoom_before = state.current_zoom
        action, reason = update_auto_zoom(item.frame, item.detected, ratio_before, cfg, state)
        ratio_after = observed_ratio(item.base_ratio, state.current_zoom, cfg)
        rows.append(
            {
                "frame": item.frame,
                "phase": item.phase,
                "detected": int(item.detected),
                "confidence": f"{item.confidence:.2f}",
                "base_ratio": f"{item.base_ratio:.4f}",
                "observed_ratio_before": f"{ratio_before:.4f}",
                "zoom_before": zoom_before,
                "action": action,
                "reason": reason,
                "zoom_after": state.current_zoom,
                "observed_ratio_after": f"{ratio_after:.4f}",
                "lost_frames": state.lost_frames,
            }
        )

    return rows


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def svg_polyline(points: list[tuple[float, float]], color: str, width: int = 2) -> str:
    joined = " ".join(f"{x:.1f},{y:.1f}" for x, y in points)
    return f'<polyline points="{joined}" fill="none" stroke="{color}" stroke-width="{width}" />'


def write_svg(path: Path, rows: list[dict[str, object]], cfg: AutoZoomConfig) -> None:
    width, height = 1100, 520
    margin_l, margin_r, margin_t, margin_b = 70, 40, 50, 70
    plot_w = width - margin_l - margin_r
    plot_h = height - margin_t - margin_b
    max_frame = max(int(r["frame"]) for r in rows)

    def sx(frame: int) -> float:
        return margin_l + (frame / max_frame) * plot_w

    def sy_ratio(ratio: float) -> float:
        return margin_t + (1.0 - ratio / 0.35) * plot_h

    def sy_zoom(zoom: float) -> float:
        return margin_t + (1.0 - zoom / max(1, cfg.max_zoom)) * plot_h

    ratio_points = [
        (sx(int(r["frame"])), sy_ratio(float(r["observed_ratio_before"]))) for r in rows
    ]
    zoom_points = [(sx(int(r["frame"])), sy_zoom(float(r["zoom_after"]))) for r in rows]
    action_marks = [
        r for r in rows if str(r["action"]) == "adjust"
    ]

    grid = []
    for i in range(6):
        y = margin_t + i * plot_h / 5
        grid.append(f'<line x1="{margin_l}" y1="{y:.1f}" x2="{width-margin_r}" y2="{y:.1f}" stroke="#e6edf3" />')
    for frame in range(0, max_frame + 1, 60):
        x = sx(frame)
        grid.append(f'<line x1="{x:.1f}" y1="{margin_t}" x2="{x:.1f}" y2="{height-margin_b}" stroke="#f1f5f9" />')
        grid.append(f'<text x="{x:.1f}" y="{height-35}" text-anchor="middle" font-size="12" fill="#4b5563">{frame}</text>')

    marks = []
    for r in action_marks:
        x = sx(int(r["frame"]))
        y = sy_zoom(float(r["zoom_after"]))
        color = "#0b6bcb" if r["reason"] == "target_small" else "#cc6b00"
        if r["reason"] == "target_lost":
            color = "#6b7280"
        marks.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="5" fill="{color}" />')
        marks.append(
            f'<text x="{x+7:.1f}" y="{y-7:.1f}" font-size="11" fill="{color}">{r["reason"]}:{r["zoom_after"]}</text>'
        )

    min_y = sy_ratio(cfg.target_min_ratio)
    max_y = sy_ratio(cfg.target_max_ratio)
    target_band = (
        f'<rect x="{margin_l}" y="{max_y:.1f}" width="{plot_w}" height="{min_y-max_y:.1f}" '
        'fill="#dff3e7" opacity="0.45" />'
        f'<text x="{width-margin_r-8}" y="{max_y-8:.1f}" text-anchor="end" font-size="12" fill="#17623a">'
        f'target ratio {cfg.target_min_ratio:.2f}-{cfg.target_max_ratio:.2f}</text>'
    )

    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
  <rect width="100%" height="100%" fill="white"/>
  <text x="{width/2}" y="28" text-anchor="middle" font-size="22" font-family="Arial, sans-serif" font-weight="700" fill="#0f2f4a">Auto-zoom policy simulation</text>
  {target_band}
  {"".join(grid)}
  <rect x="{margin_l}" y="{margin_t}" width="{plot_w}" height="{plot_h}" fill="none" stroke="#94a3b8"/>
  {svg_polyline(ratio_points, "#1f77b4", 3)}
  {svg_polyline(zoom_points, "#f59e0b", 3)}
  {"".join(marks)}
  <text x="{margin_l}" y="{height-15}" font-size="13" fill="#334155">Frame index</text>
  <text x="22" y="{margin_t+20}" transform="rotate(-90 22,{margin_t+20})" font-size="13" fill="#1f77b4">Observed box side ratio</text>
  <text x="{width-28}" y="{margin_t+20}" transform="rotate(90 {width-28},{margin_t+20})" font-size="13" fill="#f59e0b">zoom_absolute</text>
  <rect x="{margin_l+16}" y="{margin_t+14}" width="16" height="4" fill="#1f77b4"/>
  <text x="{margin_l+40}" y="{margin_t+21}" font-size="13" fill="#334155">box ratio before decision</text>
  <rect x="{margin_l+240}" y="{margin_t+14}" width="16" height="4" fill="#f59e0b"/>
  <text x="{margin_l+264}" y="{margin_t+21}" font-size="13" fill="#334155">zoom after decision</text>
</svg>
'''
    path.write_text(svg, encoding="utf-8")


def summarize(rows: list[dict[str, object]], cfg: AutoZoomConfig) -> str:
    adjusts = [r for r in rows if r["action"] == "adjust"]
    zoom_values = [int(r["zoom_after"]) for r in rows]
    reasons: dict[str, int] = {}
    for r in adjusts:
        reasons[str(r["reason"])] = reasons.get(str(r["reason"]), 0) + 1

    reason_lines = "\n".join(f"- `{k}`: {v}" for k, v in sorted(reasons.items())) or "- none"
    final_zoom = zoom_values[-1]
    min_zoom = min(zoom_values)
    max_zoom = max(zoom_values)
    return f"""# Auto zoom simulation summary

## Configuration

- zoom range: `{cfg.min_zoom}..{cfg.max_zoom}`
- initial zoom: `{cfg.initial_zoom}`
- step: `{cfg.step}`
- cooldown frames: `{cfg.cooldown_frames}`
- lost frames to zoom out: `{cfg.lost_frames_to_zoom_out}`
- target side ratio: `{cfg.target_min_ratio:.2f}..{cfg.target_max_ratio:.2f}`

## Result

- simulated frames: `{len(rows)}`
- zoom range observed: `{min_zoom}..{max_zoom}`
- final zoom: `{final_zoom}`
- adjustment count: `{len(adjusts)}`

Adjustment reasons:

{reason_lines}

## Interpretation

The policy behaves conservatively: zoom changes only after the cooldown window
has expired, so it avoids high-frequency left-right or in-out oscillation. The
far-small target phase increases zoom until the observed box side ratio reaches
the configured target band. The near-large phase decreases zoom to recover a
reasonable field of view. After sustained target loss, the policy gradually
zooms out so the camera can search a wider scene again.

For a live defense demo, this parameter set is safer than an aggressive zoom
loop. It can demonstrate detection-driven camera response while preserving the
existing stable RTSP detection path.
"""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path("eval_runs/camera_control_sim"))
    parser.add_argument("--frames", type=int, default=360)
    parser.add_argument("--min-zoom", type=int, default=0)
    parser.add_argument("--max-zoom", type=int, default=40)
    parser.add_argument("--initial-zoom", type=int, default=20)
    parser.add_argument("--step", type=int, default=5)
    parser.add_argument("--cooldown", type=int, default=20)
    parser.add_argument("--lost-frames", type=int, default=60)
    parser.add_argument("--min-ratio", type=float, default=0.06)
    parser.add_argument("--max-ratio", type=float, default=0.22)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    cfg = AutoZoomConfig(
        min_zoom=args.min_zoom,
        max_zoom=args.max_zoom,
        initial_zoom=args.initial_zoom,
        step=args.step,
        cooldown_frames=args.cooldown,
        lost_frames_to_zoom_out=args.lost_frames,
        target_min_ratio=args.min_ratio,
        target_max_ratio=args.max_ratio,
    )
    rows = run_simulation(cfg, args.frames)
    args.output_dir.mkdir(parents=True, exist_ok=True)

    write_csv(args.output_dir / "auto_zoom_simulation.csv", rows)
    write_svg(args.output_dir / "auto_zoom_simulation.svg", rows, cfg)
    (args.output_dir / "auto_zoom_simulation_summary.md").write_text(
        summarize(rows, cfg), encoding="utf-8"
    )

    print(f"wrote {args.output_dir / 'auto_zoom_simulation.csv'}")
    print(f"wrote {args.output_dir / 'auto_zoom_simulation.svg'}")
    print(f"wrote {args.output_dir / 'auto_zoom_simulation_summary.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
