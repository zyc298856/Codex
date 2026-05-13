#!/usr/bin/env python3
"""Offline zoom/focus control simulation for future RK3588 camera tests.

This script is more hardware-oriented than ``simulate_auto_zoom.py``.  It models
three effects that matter on a real UVC zoom camera:

1. ``zoom_absolute`` does not take effect instantly.
2. Zoom changes can temporarily reduce image sharpness.
3. Focus should be adjusted from a measured sharpness signal, not directly from
   the detection box size.

The simulation is intentionally deterministic so that logs can be compared with
future board-side experiments.
"""

from __future__ import annotations

import argparse
import csv
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


@dataclass
class ControlConfig:
    frames: int = 480
    min_zoom: int = 0
    max_zoom: int = 40
    initial_zoom: int = 15
    zoom_step: int = 5
    zoom_slew_per_frame: float = 1.25
    zoom_settle_frames: int = 10
    min_focus: int = 0
    max_focus: int = 550
    initial_focus: int = 230
    focus_step: int = 18
    focus_slew_per_frame: float = 9.0
    focus_settle_frames: int = 4
    focus_probe_budget: int = 7
    focus_min_improvement: float = 0.015
    target_min_ratio: float = 0.07
    target_max_ratio: float = 0.20
    confidence_threshold: float = 0.35
    confirm_frames: int = 3
    lost_frames_to_zoom_out: int = 36


@dataclass
class SceneFrame:
    frame: int
    phase: str
    visible: bool
    distance_m: float
    occlusion: float
    base_confidence: float


@dataclass
class ControllerState:
    zoom_cmd: int
    zoom_actual: float
    focus_cmd: int
    focus_actual: float
    state: str = "SEARCHING"
    zoom_settle_left: int = 0
    focus_settle_left: int = 0
    focus_probe_left: int = 0
    focus_direction: int = 1
    focus_best_value: int = 0
    focus_best_sharpness: float = 0.0
    small_count: int = 0
    large_count: int = 0
    lost_count: int = 0
    stable_count: int = 0


def clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


def scene_sequence(total_frames: int) -> Iterable[SceneFrame]:
    """Generate a repeatable scene with approach, close pass, loss, and reacquire."""
    for frame in range(total_frames):
        if frame < 28:
            yield SceneFrame(frame, "startup_empty", False, 12.0, 0.0, 0.0)
        elif frame < 150:
            progress = (frame - 28) / 121.0
            distance = 13.5 - 3.5 * progress
            yield SceneFrame(frame, "far_small_uav", True, distance, 1.0, 0.78)
        elif frame < 235:
            progress = (frame - 150) / 84.0
            distance = 9.8 - 3.5 * progress
            yield SceneFrame(frame, "approaching_uav", True, distance, 1.0, 0.82)
        elif frame < 295:
            progress = (frame - 235) / 59.0
            distance = 5.9 - 2.1 * progress
            yield SceneFrame(frame, "near_large_uav", True, distance, 1.0, 0.84)
        elif frame < 350:
            yield SceneFrame(frame, "temporary_occlusion", False, 7.5, 0.0, 0.0)
        else:
            progress = (frame - 350) / max(1, total_frames - 351)
            distance = 12.5 - 2.0 * progress
            yield SceneFrame(frame, "reacquired_far_uav", True, distance, 1.0, 0.77)


def target_base_ratio(distance_m: float) -> float:
    # A small UAV becomes only a few percent of the frame at long distance.
    return clamp(0.33 / max(distance_m, 1.0), 0.012, 0.28)


def zoom_factor(zoom_actual: float, cfg: ControlConfig) -> float:
    norm = clamp((zoom_actual - cfg.min_zoom) / max(1, cfg.max_zoom - cfg.min_zoom), 0.0, 1.0)
    return 1.0 + 1.75 * norm


def observed_box_ratio(scene: SceneFrame, zoom_actual: float, cfg: ControlConfig) -> float:
    if not scene.visible:
        return 0.0
    # Deterministic jitter approximates detection-box shake and target attitude change.
    jitter = 1.0 + 0.035 * math.sin(scene.frame * 0.19) + 0.018 * math.sin(scene.frame * 0.53)
    return clamp(target_base_ratio(scene.distance_m) * zoom_factor(zoom_actual, cfg) * jitter, 0.0, 0.95)


def ideal_focus(scene: SceneFrame, zoom_actual: float, cfg: ControlConfig) -> float:
    norm_zoom = clamp((zoom_actual - cfg.min_zoom) / max(1, cfg.max_zoom - cfg.min_zoom), 0.0, 1.0)
    # This is not a camera calibration curve. It is a monotonic stand-in for
    # "nearer target and higher zoom require a different focus setting".
    value = 95.0 + 520.0 / (scene.distance_m + 1.8) + 145.0 * norm_zoom
    return clamp(value, cfg.min_focus, cfg.max_focus)


def sharpness(scene: SceneFrame, state: ControllerState, cfg: ControlConfig) -> float:
    if not scene.visible:
        return 0.0
    norm_zoom = clamp((state.zoom_actual - cfg.min_zoom) / max(1, cfg.max_zoom - cfg.min_zoom), 0.0, 1.0)
    sigma = max(24.0, 76.0 - 34.0 * norm_zoom)
    error = state.focus_actual - ideal_focus(scene, state.zoom_actual, cfg)
    focus_score = math.exp(-(error * error) / (2.0 * sigma * sigma))
    settling_penalty = 0.72 if state.zoom_settle_left > 0 else 1.0
    return clamp(0.12 + 0.88 * focus_score * settling_penalty, 0.0, 1.0)


def confidence(scene: SceneFrame, ratio: float, sharp: float, cfg: ControlConfig) -> float:
    if not scene.visible:
        return 0.0
    if ratio <= 0.0:
        size_score = 0.0
    elif ratio < cfg.target_min_ratio:
        size_score = clamp(ratio / cfg.target_min_ratio, 0.0, 1.0)
    elif ratio > cfg.target_max_ratio * 1.8:
        size_score = clamp((cfg.target_max_ratio * 1.8) / ratio, 0.0, 1.0)
    else:
        size_score = 1.0
    noise = 0.025 * math.sin(scene.frame * 0.31)
    return clamp(scene.base_confidence * scene.occlusion * (0.35 + 0.65 * sharp) * size_score + noise, 0.0, 0.99)


def move_toward(current: float, target: float, step: float) -> float:
    if abs(target - current) <= step:
        return target
    return current + step if target > current else current - step


def command_zoom(state: ControllerState, cfg: ControlConfig, delta: int, reason: str) -> str:
    new_zoom = int(clamp(state.zoom_cmd + delta, cfg.min_zoom, cfg.max_zoom))
    if new_zoom == state.zoom_cmd:
        return "hold_zoom_limit"
    state.zoom_cmd = new_zoom
    state.zoom_settle_left = cfg.zoom_settle_frames
    state.focus_probe_left = 0
    state.state = "ZOOMING"
    return reason


def start_focus_scan(state: ControllerState, sharp: float, cfg: ControlConfig) -> str:
    state.state = "FOCUSING"
    state.focus_probe_left = cfg.focus_probe_budget
    state.focus_direction = 1
    state.focus_best_value = state.focus_cmd
    state.focus_best_sharpness = sharp
    state.focus_settle_left = cfg.focus_settle_frames
    return "start_focus_scan"


def update_focus_scan(state: ControllerState, sharp: float, cfg: ControlConfig) -> str:
    if state.focus_probe_left <= 0:
        state.focus_cmd = int(clamp(state.focus_best_value, cfg.min_focus, cfg.max_focus))
        state.state = "STABLE"
        return "focus_scan_done"

    if sharp > state.focus_best_sharpness + cfg.focus_min_improvement:
        state.focus_best_sharpness = sharp
        state.focus_best_value = state.focus_cmd
    else:
        state.focus_direction *= -1

    next_focus = state.focus_cmd + state.focus_direction * cfg.focus_step
    state.focus_cmd = int(clamp(next_focus, cfg.min_focus, cfg.max_focus))
    state.focus_probe_left -= 1
    state.focus_settle_left = cfg.focus_settle_frames
    return "probe_focus"


def update_controller(
    scene: SceneFrame,
    state: ControllerState,
    cfg: ControlConfig,
    ratio: float,
    sharp: float,
    conf: float,
    detected: bool,
) -> tuple[str, str]:
    action = "hold"
    reason = "no_change"

    if state.zoom_actual != state.zoom_cmd:
        state.zoom_actual = move_toward(state.zoom_actual, state.zoom_cmd, cfg.zoom_slew_per_frame)
    if state.focus_actual != state.focus_cmd:
        state.focus_actual = move_toward(state.focus_actual, state.focus_cmd, cfg.focus_slew_per_frame)

    if state.zoom_settle_left > 0:
        state.zoom_settle_left -= 1
        if state.zoom_settle_left == 0:
            action = "focus"
            reason = start_focus_scan(state, sharp, cfg)
        else:
            action = "wait"
            reason = "zoom_settling"
        return action, reason

    if not detected and state.state == "FOCUSING":
        state.lost_count += 1
        if state.lost_count >= cfg.confirm_frames:
            state.focus_probe_left = 0
            state.focus_settle_left = 0
            state.small_count = 0
            state.large_count = 0
            state.stable_count = 0
            state.state = "LOST"
            return "focus", "abort_focus_target_lost"
        return "wait", "focus_waiting_detection"

    if state.focus_settle_left > 0:
        state.focus_settle_left -= 1
        return "wait", "focus_settling"

    if state.state == "FOCUSING":
        return "focus", update_focus_scan(state, sharp, cfg)

    if not detected:
        state.lost_count += 1
        state.small_count = 0
        state.large_count = 0
        state.stable_count = 0
        state.state = "LOST" if state.lost_count >= cfg.confirm_frames else "SEARCHING"
        if state.lost_count >= cfg.lost_frames_to_zoom_out:
            state.lost_count = 0
            return "zoom", command_zoom(state, cfg, -cfg.zoom_step, "target_lost_zoom_out")
        return "hold", "target_not_confirmed"

    state.lost_count = 0
    state.state = "TRACKING"
    if ratio < cfg.target_min_ratio:
        state.small_count += 1
        state.large_count = 0
    elif ratio > cfg.target_max_ratio:
        state.large_count += 1
        state.small_count = 0
    else:
        state.small_count = 0
        state.large_count = 0
        state.stable_count += 1

    if state.small_count >= cfg.confirm_frames:
        state.small_count = 0
        return "zoom", command_zoom(state, cfg, cfg.zoom_step, "confirmed_target_small")
    if state.large_count >= cfg.confirm_frames:
        state.large_count = 0
        return "zoom", command_zoom(state, cfg, -cfg.zoom_step, "confirmed_target_large")
    if state.stable_count >= cfg.confirm_frames and sharp < 0.72:
        state.stable_count = 0
        return "focus", start_focus_scan(state, sharp, cfg)
    if state.stable_count >= cfg.confirm_frames:
        state.state = "STABLE"
    return action, reason


def run_simulation(cfg: ControlConfig) -> list[dict[str, object]]:
    state = ControllerState(
        zoom_cmd=cfg.initial_zoom,
        zoom_actual=float(cfg.initial_zoom),
        focus_cmd=cfg.initial_focus,
        focus_actual=float(cfg.initial_focus),
        focus_best_value=cfg.initial_focus,
    )
    rows: list[dict[str, object]] = []

    for scene in scene_sequence(cfg.frames):
        ratio = observed_box_ratio(scene, state.zoom_actual, cfg)
        sharp = sharpness(scene, state, cfg)
        conf = confidence(scene, ratio, sharp, cfg)
        detected = scene.visible and conf >= cfg.confidence_threshold
        action, reason = update_controller(scene, state, cfg, ratio, sharp, conf, detected)
        rows.append(
            {
                "frame": scene.frame,
                "phase": scene.phase,
                "visible": int(scene.visible),
                "state": state.state,
                "distance_m": f"{scene.distance_m:.2f}",
                "zoom_cmd": state.zoom_cmd,
                "zoom_actual": f"{state.zoom_actual:.2f}",
                "focus_cmd": state.focus_cmd,
                "focus_actual": f"{state.focus_actual:.2f}",
                "ideal_focus": f"{ideal_focus(scene, state.zoom_actual, cfg):.2f}",
                "sharpness": f"{sharp:.3f}",
                "box_ratio": f"{ratio:.4f}",
                "confidence": f"{conf:.3f}",
                "detected": int(detected),
                "action": action,
                "reason": reason,
                "small_count": state.small_count,
                "large_count": state.large_count,
                "lost_count": state.lost_count,
                "zoom_settle_left": state.zoom_settle_left,
                "focus_probe_left": state.focus_probe_left,
            }
        )
    return rows


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def line(points: list[tuple[float, float]], color: str, width: int = 2) -> str:
    data = " ".join(f"{x:.1f},{y:.1f}" for x, y in points)
    return f'<polyline points="{data}" fill="none" stroke="{color}" stroke-width="{width}"/>'


def write_svg(path: Path, rows: list[dict[str, object]], cfg: ControlConfig) -> None:
    width, height = 1180, 620
    left, right, top, bottom = 70, 50, 58, 60
    panel_gap = 38
    panel_h = (height - top - bottom - panel_gap) / 2
    plot_w = width - left - right
    max_frame = max(int(r["frame"]) for r in rows)

    def sx(frame: int) -> float:
        return left + frame / max_frame * plot_w

    def sy_top(value: float) -> float:
        return top + (1.0 - value) * panel_h

    def sy_bottom_norm(value: float) -> float:
        return top + panel_h + panel_gap + (1.0 - value) * panel_h

    ratio_pts = [(sx(int(r["frame"])), sy_top(min(float(r["box_ratio"]) / 0.35, 1.0))) for r in rows]
    conf_pts = [(sx(int(r["frame"])), sy_top(float(r["confidence"]))) for r in rows]
    sharp_pts = [(sx(int(r["frame"])), sy_top(float(r["sharpness"]))) for r in rows]
    zoom_pts = [
        (
            sx(int(r["frame"])),
            sy_bottom_norm(float(r["zoom_actual"]) / max(1, cfg.max_zoom)),
        )
        for r in rows
    ]
    focus_pts = [
        (
            sx(int(r["frame"])),
            sy_bottom_norm(float(r["focus_actual"]) / max(1, cfg.max_focus)),
        )
        for r in rows
    ]
    ideal_focus_pts = [
        (
            sx(int(r["frame"])),
            sy_bottom_norm(float(r["ideal_focus"]) / max(1, cfg.max_focus)),
        )
        for r in rows
    ]
    events = [r for r in rows if r["action"] in ("zoom", "focus")]

    grid = []
    for y0, h0 in ((top, panel_h), (top + panel_h + panel_gap, panel_h)):
        for i in range(5):
            y = y0 + i * h0 / 4
            grid.append(f'<line x1="{left}" y1="{y:.1f}" x2="{width-right}" y2="{y:.1f}" stroke="#e6edf3"/>')
        grid.append(f'<rect x="{left}" y="{y0}" width="{plot_w}" height="{h0}" fill="none" stroke="#94a3b8"/>')
    for frame in range(0, max_frame + 1, 60):
        x = sx(frame)
        grid.append(f'<line x1="{x:.1f}" y1="{top}" x2="{x:.1f}" y2="{height-bottom}" stroke="#f1f5f9"/>')
        grid.append(f'<text x="{x:.1f}" y="{height-24}" text-anchor="middle" font-size="12" fill="#475569">{frame}</text>')

    marks = []
    for r in events:
        x = sx(int(r["frame"]))
        if r["action"] == "zoom":
            y = sy_bottom_norm(float(r["zoom_actual"]) / max(1, cfg.max_zoom))
            color = "#d97706"
        else:
            y = sy_bottom_norm(float(r["focus_actual"]) / max(1, cfg.max_focus))
            color = "#2563eb"
        marks.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4.5" fill="{color}"/>')
        marks.append(f'<text x="{x+6:.1f}" y="{y-6:.1f}" font-size="10" fill="{color}">{r["reason"]}</text>')

    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
  <rect width="100%" height="100%" fill="white"/>
  <text x="{width/2}" y="30" text-anchor="middle" font-size="21" font-family="Arial, sans-serif" font-weight="700" fill="#0f2f4a">Zoom and focus control simulation for UVC camera</text>
  {"".join(grid)}
  {line(ratio_pts, "#0f766e", 3)}
  {line(conf_pts, "#9333ea", 2)}
  {line(sharp_pts, "#16a34a", 2)}
  {line(zoom_pts, "#f59e0b", 3)}
  {line(focus_pts, "#2563eb", 2)}
  {line(ideal_focus_pts, "#94a3b8", 2)}
  {"".join(marks)}
  <text x="{left}" y="{top-12}" font-size="13" fill="#334155">Top: box ratio / confidence / sharpness</text>
  <text x="{left}" y="{top+panel_h+panel_gap-12}" font-size="13" fill="#334155">Bottom: zoom and focus response</text>
  <text x="{left}" y="{height-8}" font-size="12" fill="#475569">frame</text>
  <text x="{left+20}" y="{top+20}" font-size="12" fill="#0f766e">box ratio</text>
  <text x="{left+120}" y="{top+20}" font-size="12" fill="#9333ea">confidence</text>
  <text x="{left+240}" y="{top+20}" font-size="12" fill="#16a34a">sharpness</text>
  <text x="{left+20}" y="{top+panel_h+panel_gap+20}" font-size="12" fill="#f59e0b">zoom actual</text>
  <text x="{left+140}" y="{top+panel_h+panel_gap+20}" font-size="12" fill="#2563eb">focus actual</text>
  <text x="{left+270}" y="{top+panel_h+panel_gap+20}" font-size="12" fill="#64748b">ideal focus</text>
</svg>
'''
    path.write_text(svg, encoding="utf-8")


def summarize(rows: list[dict[str, object]], cfg: ControlConfig) -> str:
    zoom_events = [r for r in rows if r["action"] == "zoom"]
    focus_events = [r for r in rows if r["action"] == "focus"]
    detections = sum(int(r["detected"]) for r in rows)
    stable = sum(1 for r in rows if r["state"] == "STABLE")
    mean_conf = sum(float(r["confidence"]) for r in rows) / len(rows)
    mean_sharp = sum(float(r["sharpness"]) for r in rows) / len(rows)
    reasons: dict[str, int] = {}
    for r in zoom_events + focus_events:
        reasons[str(r["reason"])] = reasons.get(str(r["reason"]), 0) + 1
    reason_lines = "\n".join(f"- `{k}`: {v}" for k, v in sorted(reasons.items())) or "- none"

    event_rows = "\n".join(
        f"| {r['frame']} | {r['phase']} | {r['action']} | {r['reason']} | {r['zoom_cmd']} | {r['focus_cmd']} | {r['confidence']} |"
        for r in sorted(zoom_events + focus_events, key=lambda item: int(item["frame"]))[:18]
    )
    if not event_rows:
        event_rows = "| - | - | - | - | - | - | - |"

    return f"""# Zoom/focus control simulation summary

## Configuration

| Item | Value |
|---|---:|
| frames | {cfg.frames} |
| zoom range | {cfg.min_zoom}..{cfg.max_zoom} |
| initial zoom | {cfg.initial_zoom} |
| zoom step | {cfg.zoom_step} |
| zoom settle frames | {cfg.zoom_settle_frames} |
| focus range | {cfg.min_focus}..{cfg.max_focus} |
| initial focus | {cfg.initial_focus} |
| focus step | {cfg.focus_step} |
| focus probe budget | {cfg.focus_probe_budget} |
| target ratio band | {cfg.target_min_ratio:.2f}..{cfg.target_max_ratio:.2f} |
| confidence threshold | {cfg.confidence_threshold:.2f} |
| confirmation frames | {cfg.confirm_frames} |

## Result

- detected frames: `{detections}/{len(rows)}`
- stable-state frames: `{stable}`
- zoom command range: `{min(int(r["zoom_cmd"]) for r in rows)}..{max(int(r["zoom_cmd"]) for r in rows)}`
- focus command range: `{min(int(r["focus_cmd"]) for r in rows)}..{max(int(r["focus_cmd"]) for r in rows)}`
- zoom events: `{len(zoom_events)}`
- focus events: `{len(focus_events)}`
- mean confidence: `{mean_conf:.3f}`
- mean sharpness: `{mean_sharp:.3f}`

Event reasons:

{reason_lines}

## First control events

| Frame | Phase | Action | Reason | Zoom cmd | Focus cmd | Confidence |
|---:|---|---|---|---:|---:|---:|
{event_rows}

## Interpretation

This simulation is intended for future board-side control strategy preparation.
It is closer to a real UVC camera than the simple auto-zoom simulation because
it models zoom settling, temporary blur after zoom changes, focus probing, and
consecutive-frame confirmation before issuing camera commands.

The result should not be claimed as real autofocus validation. It verifies the
control policy and the expected log fields. Real validation still requires the
RK3588 board, the USB camera, and `v4l2-ctl`/UVC command feedback.
"""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path("eval_runs/camera_control_sim_advanced"))
    parser.add_argument("--frames", type=int, default=480)
    parser.add_argument("--confidence-threshold", type=float, default=0.35)
    parser.add_argument("--initial-zoom", type=int, default=15)
    parser.add_argument("--initial-focus", type=int, default=230)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    cfg = ControlConfig(
        frames=args.frames,
        confidence_threshold=args.confidence_threshold,
        initial_zoom=args.initial_zoom,
        initial_focus=args.initial_focus,
    )
    rows = run_simulation(cfg)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    write_csv(args.output_dir / "zoom_focus_control_simulation.csv", rows)
    write_svg(args.output_dir / "zoom_focus_control_simulation.svg", rows, cfg)
    (args.output_dir / "zoom_focus_control_simulation_summary.md").write_text(
        summarize(rows, cfg),
        encoding="utf-8",
    )
    print(f"wrote {args.output_dir / 'zoom_focus_control_simulation.csv'}")
    print(f"wrote {args.output_dir / 'zoom_focus_control_simulation.svg'}")
    print(f"wrote {args.output_dir / 'zoom_focus_control_simulation_summary.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
