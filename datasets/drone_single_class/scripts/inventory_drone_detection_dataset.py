#!/usr/bin/env python3

from __future__ import annotations

import csv
import json
from collections import Counter, defaultdict
from pathlib import Path
import re
import sys


PATTERN = re.compile(
    r"^(?P<modality>IR|V)_(?P<label>AIRPLANE|BIRD|DRONE|HELICOPTER)_(?P<index>\d{3})(?P<labels>_LABELS)?\.(?P<ext>mp4|mat)$"
)


def build_inventory(source_root: Path) -> dict:
    counts_by_modality = Counter()
    counts_by_label = Counter()
    drone_pairs: list[dict] = []
    pair_index: dict[tuple[str, str, str], dict] = {}

    for file_path in sorted(source_root.rglob("*")):
      if not file_path.is_file():
        continue

      match = PATTERN.match(file_path.name)
      if not match:
        continue

      info = match.groupdict()
      modality = info["modality"]
      label = info["label"]
      index = info["index"]
      ext = info["ext"]
      is_label_file = info["labels"] is not None

      counts_by_modality[modality] += 1
      counts_by_label[label] += 1

      key = (modality, label, index)
      record = pair_index.setdefault(
          key,
          {
              "modality": modality,
              "label": label,
              "index": index,
              "video": "",
              "labels_mat": "",
          },
      )
      if ext == "mp4" and not is_label_file:
        record["video"] = str(file_path)
      elif ext == "mat" and is_label_file:
        record["labels_mat"] = str(file_path)

    drone_pairs = [
        record
        for record in sorted(pair_index.values(), key=lambda item: (item["modality"], item["label"], item["index"]))
        if record["label"] == "DRONE"
    ]

    summary = {
        "source_root": str(source_root),
        "counts_by_modality": dict(counts_by_modality),
        "counts_by_label": dict(counts_by_label),
        "drone_pair_count": len(drone_pairs),
        "notes": [
            "This source uses MATLAB groundTruth objects stored in *_LABELS.mat files.",
            "Direct YOLO conversion will likely require MATLAB export or a source-specific parser.",
        ],
    }
    return {"summary": summary, "drone_pairs": drone_pairs}


def write_outputs(inventory: dict, output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)

    summary_path = output_dir / "drone_detection_dataset_inventory.json"
    summary_path.write_text(
        json.dumps(inventory["summary"], indent=2, ensure_ascii=False),
        encoding="utf-8",
    )

    pairs_path = output_dir / "drone_detection_dataset_drone_pairs.csv"
    with pairs_path.open("w", newline="", encoding="utf-8") as handle:
      writer = csv.DictWriter(
          handle,
          fieldnames=["modality", "label", "index", "video", "labels_mat"],
      )
      writer.writeheader()
      writer.writerows(inventory["drone_pairs"])

    print(f"wrote {summary_path}")
    print(f"wrote {pairs_path}")


def main() -> int:
    if len(sys.argv) != 3:
      print(
          "Usage: inventory_drone_detection_dataset.py <source_root> <output_dir>",
          file=sys.stderr,
      )
      return 1

    source_root = Path(sys.argv[1]).resolve()
    output_dir = Path(sys.argv[2]).resolve()
    if not source_root.is_dir():
      print(f"source root not found: {source_root}", file=sys.stderr)
      return 2

    inventory = build_inventory(source_root)
    write_outputs(inventory, output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
