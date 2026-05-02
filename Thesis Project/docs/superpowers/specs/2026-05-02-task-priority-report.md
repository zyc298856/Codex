# 答辩前任务优先级报告

> 日期：2026-05-02
> 背景：基于嵌入式平台的目标检测系统研究（RK3588），论文答辩准备阶段
> 当前总完成度：**76%**（板端实测评分）

---

## 一、已完成成果

### 核心系统

| 成果 | 状态 | 证据 |
|------|------|------|
| RKNN 模型部署 | ✅ | 14 个模型变体在板端，非 end2end 模型全部正常推理 |
| C++ 多线程流水线 | ✅ | 4 级并行（采集→预处理→推理→发布），板端运行 |
| RGA 硬件预处理 | ✅ | 板端验证：检测结果与 OpenCV 完全一致，速度提升 12.6% |
| 零拷贝内存输入 | ✅ | rknn_create_mem + rknn_set_io_mem 已实现 |
| 性能分析框架 | ✅ | 6 阶段独立计时（prepare/input_update/rknn_run/outputs_get/decode/render） |
| 软件告警系统 | ✅ | 画面叠加 + alarm_events.csv + alarm.log |

### 稳定性测试

| 测试 | 状态 | 关键数据 |
|------|------|---------|
| 1 小时连续推理 | ✅ 完成 | 24,700 次 NPU 推理，0 崩溃，0 错误，RSS 105~107 MB 平稳 |
| 10 分钟 RTSP 实时流 | ✅ 完成 | 4,100 次 NPU 推理，0 崩溃，0 发布丢帧，0 内存泄漏 |

### 已知发现

| 发现 | 详情 |
|------|------|
| End2end 模型兼容性 | 输出 `[1,300,6]` 的 RKNN 模型在当前 Runtime/Driver 下 `rknn_run()` 段错误（EXIT_CODE=139） |
| Full INT8 精度丧失 | 130 帧检测数 = 0，检测头对 INT8 量化敏感 |
| Hybrid INT8 未加速 | 推理 187ms（比 FP 的 86ms 慢 2.2x），原因是 INT8 backbone 特征分布偏移导致后处理耗时暴增 |

---

## 二、待完成任务优先级

### P0 — 答辩必需（影响论文内容）

#### P0-1：更新论文稳定性数据

**问题**：论文中稳定性测试数据可能仍为 10 分钟（597 秒），现在有 1 小时（3605 秒）的完整数据。

**需要做的事**：
- 定位论文中所有涉及"稳定性测试"的段落
- 更新为 1 小时测试数据：24,700 次推理、0 崩溃、RSS 波动 <2%、推理耗时无退化
- 补充 RSS 趋势表格（8 个时间采样点，全程 105~107 MB）
- 补充前后对比：前 5 分钟 avg=51.87ms vs 后 5 分钟 avg=52.53ms

**数据来源**：
```
板端文件：/home/ubuntu/stability_test_20260502/stability_1h_summary.txt
CSV 数据：/home/ubuntu/stability_test_20260502/stability_1h_log.csv（2660 行）
```

**预计工作量**：小（纯文本替换 + 数据填入）

#### P0-2：生成答辩用图表

**问题**：答辩需要直观展示系统性能，纯文字描述不够。

**需要的图表**：

1. **1 小时 RSS 内存趋势图**
   - X 轴：时间（0~3600 秒）
   - Y 轴：RSS（MB）
   - 预期效果：近似水平线，波动 <2%
   - 数据：CSV 中每 10 帧采样的 rss_mb 列

2. **1 小时推理耗时分布图**
   - X 轴：时间（0~3600 秒）
   - Y 轴：infer_ms
   - 预期效果：围绕 52ms 波动，无上升趋势
   - 数据：CSV 中 infer_ms 列

3. **各模型性能对比柱状图**（如果论文中还没有的话）
   - FP+OpenCV: 86.48ms
   - FP+RGA: 75.57ms
   - Hybrid INT8: 187.71ms
   - Full INT8: 31.68ms（但 0 检测）

**建议格式**：PNG 或 PDF，分辨率 300dpi 以上，可直接插入论文或 PPT

**预计工作量**：中（需要从板端下载 CSV，用 Python/matplotlib 生成）

---

### P1 — 论文质量提升（不影响答辩，但能提升评分）

#### P1-1：补写 NPU 兼容性分析

**问题**：end2end RKNN 模型段错误是一个有研究价值的发现，目前仅在内部诊断报告中记录，未写入论文。

**建议写入论文的位置**：实验分析章节，NPU 部署兼容性讨论

**内容要点**：
- 现象：输出 shape `[1,300,6]`（含 NMS）的 end2end 模型在 `rknn_run()` 段错误（EXIT_CODE=139）
- 受影响模型：`best.rk3588.fp.v220.rknn`、`yolov10n.512.rk3588.fp.rknn`
- 正常模型：输出 `[1,5,8400]` 的非 end2end 模型均正常
- 结论：RKNN Toolkit2 的 end2end NMS 算子与 RKNN Runtime 2.3.0 / RKNPU Driver 0.9.8 存在兼容性问题
- 影响：项目主线使用非 end2end 模型 + 软件后处理 NMS，不受影响

**预计工作量**：中（需要写 1~2 段分析文字）

#### P1-2：答辩 Q&A 完善

**已知弱点及建议应答**：

**Q: INT8 量化为什么没有加速？**
> "Full INT8 量化导致检测精度完全丧失（130 帧 0 检测），原因是 YOLOv10 检测头对量化敏感。Hybrid INT8（检测头保留 FP16）恢复了检测能力，但实测推理速度从 FP 的 86ms 增加到 188ms。分析原因是 INT8 backbone 的特征分布偏移导致检测头输出大量低置信度候选框（114359 vs FP 的 9352），后处理 NMS 耗时大幅增加。最终采用 FP + RGA 硬件预处理方案，推理速度提升 12.6%。"

**Q: GPIO 做了吗？**
> "系统实现了完整的软件告警链路——画面叠加告警标识、告警事件日志记录。GPIO 硬件驱动接口设计了兼容层（gpio_alarm_path 参数化），但因缺少实体外设（LED/蜂鸣器/继电器），硬件验证作为后续工作。"

**Q: 稳定性怎么样？**
> "系统在 1 小时连续推理测试中完成 24,700 次 NPU 推理，零崩溃、零推理错误。内存全程在 105~107 MB 波动，波动幅度 1.6 MB（<2%），未观察到内存泄漏。推理耗时前后无退化（前 5 分钟 51.87ms vs 后 5 分钟 52.53ms）。"

**预计工作量**：小（口语准备，不需要改代码或论文）

---

### P2 — 有余力再做（答辩前不建议动）

#### P2-1：GPIO 软件模拟实现

**风险**：修改代码可能引入 bug，答辩前不建议动

#### P2-2：升级 RKNN Runtime

**风险**：可能引入新的兼容性问题，答辩前不建议动

#### P2-3：End2end 模型兼容性修复

**风险**：需要研究 RKNN Toolkit2 导出配置 + Runtime 版本匹配，时间不确定

---

## 三、任务书完成度变化

| 版本 | 总完成度 | 关键变化 |
|------|---------|---------|
| 代码审查（4/29） | 76% | 基于代码审查 |
| 板端实测 v1（5/02 上午） | 73% | Hybrid INT8 实测比 FP 慢，GPIO 确认 disabled |
| **板端实测 v2（5/02 下午）** | **76%** | **1 小时稳定性测试通过，研究内容 4 完成度提升** |

### 逐条评分（最新）

| 任务书条款 | 完成度 | 变化 |
|-----------|--------|------|
| 目的要求 1：工程化集成 | 95% | — |
| 目的要求 2：异构计算与量化 | 60% | — |
| 目的要求 3：多线程系统架构 | 100% | — |
| 目的要求 4：闭环控制与验证 | 60% | ↑（稳定性测试通过） |
| 研究内容 1：算法验证 | 75% | — |
| 研究内容 2：嵌入式移植 | 70% | — |
| 研究内容 3：多线程异构架构 | 90% | — |
| 研究内容 4：系统集成与评估 | 65% | ↑（1 小时稳定性数据） |

---

## 四、板端数据文件索引

```
1 小时稳定性测试：
  /home/ubuntu/stability_test_20260502/stability_1h_summary.txt    — 汇总指标
  /home/ubuntu/stability_test_20260502/stability_1h_log.csv        — 2660 行逐帧数据
  /home/ubuntu/stability_test_20260502/stability_1h_stdout.log     — 控制台输出
  /home/ubuntu/stability_test_20260502/long_stability_test.cpp     — 测试程序源码

10 分钟 RTSP 测试：
  /home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_live_rtsp/artifacts/stability_10min_20260428_195508_with_resource/

NPU 诊断（Codex 复核记录）：
  Thesis Project/docs/superpowers/specs/2026-05-02-npu-driver-issue-codex-check.md
  Thesis Project/tools/diagnostics/rknn_minimal_probe.cpp

板端验证报告：
  Thesis Project/docs/superpowers/specs/2026-05-02-board-verification-report.md
  Thesis Project/docs/superpowers/specs/2026-05-02-stability-test-report.md
```

---

*报告结束。建议优先完成 P0-1（论文更新）和 P0-2（图表生成），这两个任务工作量不大但对答辩效果提升最大。*
