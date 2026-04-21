# Scripts

Planned scripts for the next steps:
- source import
- train/val/test split generation
- label consistency checks
- dataset summary statistics

Current source-specific importer:
- `import_kaggle_drone_object_detection.py`
- `validate_yolo_dataset.py`

This importer is meant for the first bootstrap dataset:
- Kaggle `Drone Object Detection` (`sshikamaru/drone-yolo-detection`)

What it does:
- scans a YOLO-style extracted source bundle
- detects common train/val/test folder layouts
- copies images into the standard dataset scaffold
- normalizes all labels to class `0` (`drone`)
- preserves negative samples by generating empty label files when needed
- writes an import manifest to `manifests/`

It does not modify files inside `raw_sources/`.

Dataset validation helper:
- `validate_yolo_dataset.py`

What it does:
- scans `images/train|val|test` and matching labels
- verifies images can be decoded
- checks YOLO label formatting for the single-class `drone` setup
- writes a validation report into `manifests/`
- can optionally remove invalid derived image/label pairs
