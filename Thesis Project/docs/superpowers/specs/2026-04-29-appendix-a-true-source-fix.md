# 2026-04-29 Appendix A True-Source Fix

## Purpose

This record documents the fix applied after the Appendix A audit reported that the previous English text was a self-written summary rather than a true excerpt from the cited paper.

## Source

The Appendix A source was replaced with real excerpts from:

Li J, Ye J. Edge-YOLO: Lightweight Infrared Object Detection Method Deployed on Edge Devices[J]. Applied Sciences, 2023, 13(7): 4402. https://doi.org/10.3390/app13074402.

Local source files used:

- `C:\Users\Tony\Downloads\applsci-13-04402.xml`
- `C:\Users\Tony\Downloads\applsci-13-04402.pdf`

## Sections Selected

The English original now uses authentic excerpts from the following sections of the article:

- Section 1 Introduction
- Section 3.3 YOLOv5 Network Model Improvement
- Section 4.3 Model Lightweighting Experiment
- Section 4.7 Actual Edge Device Deployment Testing
- Section 5 Conclusions

These sections were selected because they cover lightweight detection, YOLO model improvement, edge-device deployment, RK3588, ONNX-to-RKNN conversion, NPU acceleration, and model quantization.

## Validation

- English original text in the DOCX: 1839 words.
- Chinese translation in the DOCX: 3050 CJK characters.
- Old self-written markers removed: `source article`, `For thesis work`, and `The article emphasizes` no longer appear in Appendix A.
- Appendix A and Appendix B were rendered through Word PDF export for visual QA.

## Output

Updated formal thesis:

- `Thesis Project/paper/full_thesis_latest_merged.docx`

QA render:

- `Thesis Project/docs/thesis_drafting/qa_appendix_a_true_source_check.pdf`
- `Thesis Project/docs/thesis_drafting/qa_appendix_a_true_source_last20_sheet.png`

Backup before replacement:

- `Thesis Project/docs/thesis_drafting/full_thesis_latest_merged_before_appendix_a_true_source_20260429_182720.docx`

