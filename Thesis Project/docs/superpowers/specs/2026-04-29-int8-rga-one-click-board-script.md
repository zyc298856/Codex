# INT8/RGA 板端一键验证脚本记录

## 目标

为了减少下一次连接 RK3588 后的手工操作，本次新增板端一键实验脚本：

```text
Thesis Project/tools/int8_rga/run_int8_rga_experiments.sh
```

脚本用于对同一段固定视频依次运行 FP、INT8、RGA、zero-copy 等配置，自动保存输出视频、检测 CSV、ROI JSONL、报警事件 CSV 和运行日志。

## 设计原则

1. 不修改 `rk_yolo_video` 程序本体；
2. 不改变默认稳定配置；
3. 所有优化路径只通过环境变量在当前 case 中临时开启；
4. 每组实验独立保存输出文件，便于后续汇总和论文引用；
5. INT8 模型为可选参数，未提供时自动跳过 INT8 相关实验。

## 用法示例

```bash
chmod +x tools/int8_rga/run_int8_rga_experiments.sh

tools/int8_rga/run_int8_rga_experiments.sh \
  --video /home/ubuntu/eval/public_uav.mp4 \
  --fp-model /home/ubuntu/models/best.end2end_false.op12.rk3588.fp.v220.rknn \
  --int8-model /home/ubuntu/models/best.end2end_false.op12.rk3588.int8.v220.rknn \
  --binary ./rk_yolo_video \
  --out-dir /home/ubuntu/eval/int8_rga_runs \
  --score 0.35 \
  --nms 0.45
```

## 默认实验项

| Case | 模型 | 作用 |
| --- | --- | --- |
| `fp_opencv_baseline` | FP | 稳定基线 |
| `fp_rga_resize` | FP | RGA RGB resize 预处理 |
| `fp_rga_cvt_resize` | FP | RGA BGR-to-RGB 与 resize 融合 |
| `fp_rga_letterbox` | FP | RGA letterbox 比例保持输入 |
| `fp_zero_copy` | FP | zero-copy input 路径 |
| `int8_opencv_baseline` | INT8 | INT8 基线 |
| `int8_rga_cvt_resize` | INT8 | INT8 + RGA cvt+resize |
| `int8_rga_letterbox` | INT8 | INT8 + RGA letterbox |
| `int8_zero_copy` | INT8 | INT8 + zero-copy input |

## 输出文件

每个 case 生成：

| 文件 | 说明 |
| --- | --- |
| `<case>.mp4` | 带检测框和报警提示的视频 |
| `<case>.detections.csv` | 逐帧检测结果 |
| `<case>.roi.jsonl` | ROI 与跟踪状态记录 |
| `<case>.alarm_events.csv` | 软件报警事件 |
| `<case>.log` | 运行日志，包含 `profile_csv` 阶段耗时 |

脚本还会生成 `run_manifest.txt`，记录输入视频、模型、阈值和每个 case 的输出路径。

## 后续分析

将日志拷回 PC 后，可运行：

```bash
python tools/int8_rga/summarize_profile_csv.py \
  --logs "/path/to/int8_rga_runs/*.log" \
  --output eval_runs/int8_rga/profile_summary.csv
```

汇总结果可直接用于第 5 章实验表格，重点比较 `prepare_ms`、`rknn_run_ms`、`total_work_ms` 以及输出视频中的误检漏检情况。
