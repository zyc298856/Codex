# Claude 修复任务清单采纳/驳回对照表

## 背景

用户提供了 Claude Code 生成的 `Codex修复任务清单.md`。该清单认为当前仓库缺少 profiling、zero-copy、论文补充文字和参考文献等内容。经对当前本地仓库核查，清单中部分结论来自旧版本仓库或错误分支，不能直接照单修改。

本记录用于说明每条建议的当前状态、采纳策略和后续动作，避免重复实现或破坏已经稳定的功能。

## 总体判断

| 任务 | Claude 判断 | 当前真实状态 | 处理建议 |
| --- | --- | --- | --- |
| 任务 1：`RK_YOLO_PROFILE` 阶段计时 | 认为未实现 | 已实现 | 不按原建议改，仅保留现有实现 |
| 任务 2：`RK_YOLO_ZERO_COPY_INPUT` | 认为未实现 | 已实现 | 不按原建议改，仅继续用作实验路径 |
| 任务 3：第二章补充文字 | 方向合理 | 需对照论文最新版确认 | 可部分采纳 |
| 任务 4：补参考文献 | 方向合理，但含占位文献 | 需严谨筛选 | 只采纳真实可查文献 |
| 任务 5：总结与展望 | 方向合理，但内容过时 | INT8 已完成离线转换，RGA 已有实测记录 | 需改写为当前状态 |
| 任务 6：跟踪算法描述 | 方向合理，但环境变量写错 | 代码已有 motion/optical_flow | 采纳算法描述，修正变量名 |
| 任务 7：技术小修 | 大多合理 | 需对照 DOCX | 可低风险采纳 |

## 任务 1：`RK_YOLO_PROFILE` 阶段计时

### Claude 结论

Claude 清单称全仓库 `grep RK_YOLO_PROFILE` 零匹配，认为 `yolo_rknn.cpp` 未实现阶段计时。

### 当前核查结果

该结论与当前仓库不符。当前实现中：

- `rk_yolo_video/src/main.cpp` 已读取 `RK_YOLO_PROFILE`；
- `rk_yolo_video/src/main.cpp` 已输出 `profile_csv_header` 和 `profile_csv`；
- 输出字段包含 `prepare_ms`、`input_set_or_update_ms`、`rknn_run_ms`、`outputs_get_ms`、`decode_ms`、`outputs_release_ms`、`render_ms`、`total_work_ms`；
- `rk_yolo_live_rtsp/src/main.cpp` 也读取 `RK_YOLO_PROFILE` 并输出运行统计。

### 证据位置

```text
rk_yolo_video/src/main.cpp
rk_yolo_video/src/yolo_rknn.cpp
rk_yolo_live_rtsp/src/main.cpp
```

### 处理结论

不采纳原代码修改建议。原因是原建议比现有实现更粗糙，且没有覆盖 zero-copy input 更新耗时、输出释放耗时和 render 耗时等字段。重复修改可能破坏现有表 5.9 对应的 `profile_csv` 格式。

## 任务 2：`RK_YOLO_ZERO_COPY_INPUT`

### Claude 结论

Claude 清单称全仓库 `grep rknn_create_mem` 零匹配，认为 zero-copy 输入路径未实现。

### 当前核查结果

该结论与当前仓库不符。当前实现中：

- `rk_yolo_video/src/yolo_rknn.cpp` 已读取 `RK_YOLO_ZERO_COPY_INPUT`；
- 已使用 `rknn_create_mem` 创建输入内存；
- 已使用 `rknn_set_io_mem` 绑定输入内存；
- 已包含失败 fallback 与内存释放逻辑；
- `profile_csv` 中通过 `input_mode` 区分 `zero_copy` 与 `rknn_inputs_set`。

### 证据位置

```text
rk_yolo_video/src/yolo_rknn.cpp
rk_yolo_video/include/yolo_rknn.h
docs/superpowers/specs/2026-04-25-rknn-zero-copy-input-experiment.md
```

### 处理结论

不采纳原代码修改建议。当前实现更完整，包含 stride size、fallback、profiling 字段和实验记录。后续只需在板端继续比较 zero-copy 对 FP/INT8 的实际影响。

## 任务 3：第二章补充文字

### 当前判断

方向合理。第二章确实应包含 RK3588 的硬件参数、NPU 算力、CPU/GPU、视频编解码、RGA 和部署工具链描述。但需要以当前论文最新版为准，避免重复插入。

### 采纳策略

部分采纳：

- 保留 RK3588 `6 TOPS INT8`、`4x Cortex-A76 + 4x Cortex-A55`、`Mali-G610`、`LPDDR4/LPDDR5`、`H.264/H.265`、`RGA` 等内容；
- 与 Jetson Nano/Orin Nano 的对比只作简要工程对比，不写成绝对优劣；
- 引用需指向真实资料，优先使用 Rockchip 官方技术文档或板卡资料。

## 任务 4：补参考文献

### 当前判断

方向合理，但 Claude 给出的文献列表中含明显占位内容。例如：

```text
李航, 张三. 边缘计算环境下目标检测算法部署综述[J].
```

这类文献不能直接放入论文。

### 采纳策略

谨慎采纳：

- 可以补充 YOLO、量化、移动端网络、RK3588/RKNN 官方资料；
- 中文文献必须真实可查，不能使用示例作者或虚构题名；
- 若无法确认中文文献真实性，宁可不加，也不要引入学术风险。

## 任务 5：总结与展望

### 当前判断

Claude 给出的文字已经落后于当前项目状态。现在项目已完成：

- INT8 RKNN 离线转换；
- RGA 固定视频和实时 RTSP 多条路径验证；
- zero-copy 输入实验；
- 软件报警 overlay 替代 GPIO 外设演示；
- INT8/RGA 一键板端实验脚本。

因此不能再写成“INT8 未能形成稳定结果”或“RGA 未实现”。更准确的表述应为：

- INT8 已完成离线量化转换，尚待板端固定视频精度与性能对比；
- RGA 已实现并完成部分板端验证，但默认路径仍保留 FP/OpenCV 或稳定实时配置；
- GPIO 硬件闭环未接入，当前采用软件报警 overlay 作为直观替代。

### 采纳策略

采纳“正面说明目标调整”的方向，但必须按当前进度重写。

## 任务 6：跟踪算法描述

### 当前判断

方向合理，但 Claude 文本中存在技术错误：

```text
通过环境变量 RK_YOLO_ZERO_COPY_INPUT 切换跟踪模式
```

真实代码中跟踪模式由 `RK_YOLO_TRACK_MODE` 控制：

- 默认 `motion`；
- 可选 `optical_flow`；
- 光流路径使用 `cv::goodFeaturesToTrack` 与 `cv::calcOpticalFlowPyrLK`。

### 证据位置

```text
rk_yolo_live_rtsp/src/main.cpp
```

### 采纳策略

采纳算法描述，但修改环境变量为 `RK_YOLO_TRACK_MODE`。论文中应避免把 zero-copy 与跟踪功能混淆。

## 任务 7：技术描述小修

### 当前判断

大多值得采纳，风险较低：

- 将笼统的 `C/C++` 统一为 `C++17` 或 `基于 C++17 的 C++ 程序`；
- 对表格中的 `0.00 ms` 使用 `--` 或 `-` 表示复用帧无完整 NPU 推理；
- 在 `[1, 5, 8400]` 后补充 8400 来源，即 `80 x 80 + 40 x 40 + 20 x 20`。

### 采纳策略

后续在论文 DOCX 中逐项检查，确认不破坏格式后再修改。

## 后续推荐动作

1. 不再修改 profiling 和 zero-copy 代码，除非板端实测发现 bug。
2. 优先检查论文第 2 章、第 3.3.1 节、第 3.5.3 节、第 4.6 节、第 6 章是否已经包含最新进度。
3. 参考文献只加入真实可查来源，不采纳 Claude 给出的占位中文文献。
4. 用当前状态更新论文措辞：INT8 是“已离线转换、待板端验证”，RGA 是“已实现并部分验证的可选硬件优化路径”，GPIO 是“以软件报警 overlay 替代硬件外设闭环演示”。
