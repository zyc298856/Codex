# Public-video hard cases for hard_v2

This folder extends `drone_single_class_hard_v2` without modifying the original
stable dataset or model.

## What was added

- `images/train_public_neg/`: public-video hard negatives with empty YOLO labels.
  These are added to `train.txt`.
- `review/public_positive_candidates/`: candidate positive frames that must be
  manually labelled before training.
- `review/detected_false_positive_candidates/`: frames from detected/boxed videos.
  These are for visual diagnosis only and must not be used directly as labels.

## Safety rule

Do not train on model-predicted boxes from false-positive videos. If a frame
contains a real drone, draw the correct box manually first. If it is a background
false positive, keep the label empty and treat it as a hard negative.

## Current summary

```json
{
  "dataset": "C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis Project/datasets/drone_single_class_hard_v2",
  "negative_videos_found": 8,
  "review_videos_found": 5,
  "hard_negative_images": 144,
  "hard_negative_images_added_to_train": 144,
  "manual_review_candidate_images": 50,
  "train_txt": "C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis Project/datasets/drone_single_class_hard_v2/train.txt",
  "policy": {
    "hard_negatives": "empty labels are added to train.txt",
    "positive_candidates": "not used for training until manually labelled"
  }
}
```
