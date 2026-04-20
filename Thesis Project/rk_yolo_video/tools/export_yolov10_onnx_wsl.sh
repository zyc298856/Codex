#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 3 ]]; then
  echo "Usage: $0 <workspace_dir> <pt_model> <imgsz> [output_onnx]" >&2
  exit 1
fi

workspace_dir="$1"
pt_model="$2"
imgsz="$3"
output_onnx="${4:-}"

tmp_dir="$workspace_dir/.tmp_yolo_export_${imgsz}"
mkdir -p "$tmp_dir"

pt_basename="$(basename "$pt_model")"
pt_stem="${pt_basename%.*}"
tmp_pt="$tmp_dir/${pt_stem}_${imgsz}.pt"

cp "$pt_model" "$tmp_pt"

/home/openclaw/rknn_venv/bin/python - <<PY
from ultralytics import YOLO
model = YOLO(r"$tmp_pt")
result = model.export(format="onnx", imgsz=${imgsz}, opset=12, simplify=False)
print(result)
PY

generated_onnx="$tmp_dir/${pt_stem}_${imgsz}.onnx"
if [[ ! -f "$generated_onnx" ]]; then
  echo "Expected ONNX file not found: $generated_onnx" >&2
  exit 2
fi

if [[ -z "$output_onnx" ]]; then
  output_onnx="$workspace_dir/${pt_stem}.${imgsz}.onnx"
fi

mv "$generated_onnx" "$output_onnx"
ls -l "$output_onnx"
