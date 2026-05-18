# Demo Q&A Notes

## Why not enable multi-context in the live demo?

Multi-context improves throughput when every frame needs NPU inference.
However, the classroom live demo cares more about visible latency and display
stability. With `detect_every_n=3`, the NPU is already called less frequently,
so extra contexts add scheduling complexity without clearly improving the
viewer experience.

## Why disable dynamic ROI?

Dynamic ROI is useful when a target moves smoothly, because it searches around
the last detection area. In a hand-held phone demo, the target may move quickly
or leave the previous crop. If the crop drifts, the displayed box can jump.
Therefore the main demo uses full-frame inference plus lightweight tracking.

## Why use fixed focus instead of continuous autofocus?

Continuous autofocus can cause focus breathing: the camera repeatedly adjusts
focus even when the current image is already usable. For a short defense demo,
fixed focus after startup is more predictable. The zoom loop is kept active,
but focus is rate-limited by using a fixed manual value.

## Why not use RGA or INT8 in the main demo?

Both paths have been implemented or verified as engineering experiments.
For the final live demo, stability is more important than showing every
experimental optimization. FP RKNN plus OpenCV preprocessing is the stable
baseline; RGA, zero-copy and hybrid INT8 can be explained as explored hardware
optimization paths.

## What if the model misses the target?

Use the sensitive preset with `score=0.20`, increase phone brightness, reduce
motion speed, and place the target near the image center first. If live input
still fails due to classroom lighting or network issues, use the fixed-video
backup.

