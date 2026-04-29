# INT8 与 RGA 离线实验准备设计

## 目标

在没有开发板的情况下，继续推进任务书中与性能优化相关的 INT8 量化和 RGA 硬件预处理要求。当前项目已经具备稳定的 FP RKNN 实时检测路径，因此本阶段采用“不改稳定主线、先补实验闭环”的方式推进。

本设计新增一组离线工具，用于生成 INT8 校准集、执行 ONNX 到 RKNN 的 FP/INT8 转换、生成板端固定视频对比命令，并汇总 `RK_YOLO_PROFILE=1` 输出的阶段耗时日志。

## 保守原则

1. 不修改已经跑通的 `rk_yolo_video` 与 `rk_yolo_live_rtsp` 主逻辑。
2. 不把 INT8 直接替换为默认模型，必须先完成板端固定视频对比。
3. RGA 仍通过环境变量显式开启，默认继续保持 OpenCV/FP 稳定路径。
4. 所有实验命令使用固定视频输入，保证结果可重复。

## 新增工具

| 工具 | 作用 |
| --- | --- |
| `tools/int8_rga/make_calibration_list.py` | 从 YOLO 数据集或图像目录生成 RKNN INT8 校准列表 |
| `tools/int8_rga/convert_onnx_to_rknn.py` | 调用 RKNN Toolkit2 将 ONNX 转换为 FP 或 INT8 RKNN |
| `tools/int8_rga/make_board_experiment_commands.py` | 生成 RK3588 固定视频实验命令矩阵 |
| `tools/int8_rga/summarize_profile_csv.py` | 汇总 `profile_csv` 日志，形成阶段耗时表 |

## 实验矩阵

下一次连接开发板后，建议至少验证以下配置：

| 配置 | 模型 | 预处理 | 目的 |
| --- | --- | --- | --- |
| FP OpenCV baseline | FP RKNN | OpenCV | 稳定基线 |
| FP RGA resize | FP RKNN | RGA resize | 验证 RGA 是否降低预处理耗时 |
| FP RGA cvt+resize | FP RKNN | RGA BGR-to-RGB + resize | 验证融合颜色转换与缩放的收益 |
| FP RGA letterbox | FP RKNN | RGA letterbox | 验证保持比例输入对检测稳定性的影响 |
| FP zero-copy | FP RKNN | OpenCV + zero-copy input | 验证输入拷贝开销是否可降低 |
| INT8 OpenCV baseline | INT8 RKNN | OpenCV | 验证 INT8 精度和推理耗时 |
| INT8 RGA cvt+resize | INT8 RKNN | RGA BGR-to-RGB + resize | 验证量化与 RGA 叠加效果 |
| INT8 RGA letterbox | INT8 RKNN | RGA letterbox | 验证量化模型在比例保持输入下的表现 |

## 成功标准

INT8 或 RGA 只有在满足以下条件后，才建议写成“主路径优化结果”：

1. 固定视频输出可以稳定生成检测视频、CSV、ROI JSONL 和日志。
2. `profile_csv` 中对应阶段耗时下降，且不是偶然单次下降。
3. 输出视频中没有明显增加误检、漏检或框位置异常。
4. 日志中没有 `fallback`、模型加载失败、RGA 调用失败或 zero-copy 初始化失败。
5. 至少完成一次与当前 FP 稳定路径的同源视频对比。

## 论文表述建议

在板端实验尚未完成前，论文中应将 INT8 和 RGA 描述为“已设计并部分实现的性能优化路径”或“后续实验验证方向”，不要写成已经全面优于 FP 主路径。

如果后续实验表明 INT8 精度下降明显，则可以保留 FP RKNN 作为最终演示方案，并把 INT8 写入不足与展望；如果 RGA 在部分阶段有收益但整体 FPS 改善有限，也应如实说明瓶颈可能已经转移到 NPU 推理、视频编码或线程同步。
