# 毕业设计论文审查总结

> 审查日期：2026-04-29
> 审查范围：论文 DOCX + 代码仓库 + 实验记录
> 审查人：Claude Code

---

## 一、论文与代码一致性

### 结论：一致，无虚假声明

逐项核查结果：

| 功能 | 代码入口 | 论文声称 | 实际状态 | 一致性 |
|------|---------|---------|---------|--------|
| RK3588 NPU FP 推理 | `yolo_rknn.cpp` Load/Infer/Release | 已实现并验证 | 代码完整，有板端验证记录 | 一致 |
| 多 context NPU 并行 | `rk_yolo_live_rtsp/main.cpp:1796-1814` | 已实现并验证 | 双 context 有消融实验数据 | 一致 |
| detect_every_n 检测间隔 | `main.cpp:1321-1322` | 已实现并验证 | 有 N=1,2,3 对比实验 | 一致 |
| motion 运动预测 | `PredictDetections()` | 已实现 | 代码正确，限幅 kMaxStepPixels=24 | 一致 |
| optical flow 光流跟踪 | `TrackDetections()` + cv::calcOpticalFlowPyrLK | 已实现 | goodFeaturesToTrack + 中值位移 | 一致 |
| 框平滑（指数平滑） | `SmoothDetections()` | 已实现 | alpha 加权，IoU 匹配 | 一致 |
| 动态 ROI | `BuildInferenceRoi()` | 已实现 | margin + min_coverage + 周期全帧刷新 | 一致 |
| RTSP 实时推流 | `RtspPublisher` + GStreamer | 已实现并验证 | mpph264enc + rtph264pay | 一致 |
| 软件报警 overlay | `DrawAlarmOverlay()` + alarm CSV | 已实现 | UAV ALERT / NORMAL + hold_frames | 一致 |
| RK_YOLO_PROFILE 阶段计时 | `InferProfiled()` + profile_csv | 已实现 | 8 个细分阶段字段 | 一致 |
| zero-copy input | rknn_create_mem + rknn_set_io_mem | 已实现，实验性路径 | 输入更新降低但 rknn_run 增加 | 一致 |
| RGA 预处理（多路径） | rga_cvt_resize / rga_letterbox / rga_frame_resize / rga_publish_nv12 | 已实现，可选实验路径 | 各路径均有验证记录 | 一致 |
| INT8 RKNN 离线转换 | convert_onnx_to_rknn.py | 已离线转换，待板端验证 | 转换成功，板端未测 | 一致 |
| GPIO 硬件闭环 | 无代码 | 未实现，作为后续展望 | 软件报警 overlay 替代 | 一致 |

**关键点：论文没有将未实现的功能写成已完成，也没有将仅部分验证的结果写成稳定结论。**

---

## 二、代码审查结论

### 无影响运行的 Bug

核心推理路径、线程安全、内存管理、降级逻辑均正确。具体：

- `Load→Infer/InferProfiled→Release` 生命周期完整
- `BoundedQueue` / `BlockingQueue` 使用 mutex + condition_variable 保护
- `PipelineStats` 使用 std::atomic，线程安全
- zero-copy 路径有正确 fallback
- RGA 路径在编译无 HAVE_RGA 时正确降级为 OpenCV
- 多 context 模式仅在 detect_every_n=1 时启用（合理）

### 需注意但不影响运行的问题

1. **rk_yolo_video 和 rk_yolo_live_rtsp 重复定义 kCocoClassNames** — 代码重复，不影响功能
2. **run_int8_rga_experiments.sh 默认 score=0.35 与 rk_yolo_video 默认 score=0.30 不一致** — 板端实验时需手动统一
3. **ApplyCameraTune 使用 std::system() 执行 v4l2-ctl** — 设备路径来自环境变量非用户输入，无注入风险

---

## 三、P0 问题（需立即修复）

### P0-1: 参考文献不足（当前仅 14 篇）

本科论文通常需要 15-30 篇。当前缺少：

- Lucas-Kanade 光流原始论文（论文提到 LK 光流但未引用）
- 模型量化（INT8）相关工作
- NPU 多线程/多 context 相关研究
- 边缘计算/嵌入式部署综述
- 中文文献为零（如果无法确认真实性则宁缺毋滥）

**建议：补充 5-8 篇，使总数达到 20 篇左右。**

### P0-2: 第二章技术背景偏薄

- 2.1 YOLO 算法基础仅笼统介绍，未说明 YOLOv10 的 NMS-free 设计
- 2.4 RKNN 部署流程未说明核心 API 调用流程（rknn_init / rknn_inputs_set / rknn_run）
- 2.5 C++17 多线程未具体说明用了哪些特性

**建议：每节补充 1-2 段关键技术说明。**

---

## 四、P1 问题（建议尽快修复）

### P1-1: motion 跟踪描述措辞需更精确

论文写"根据相邻检测帧之间检测框中心点位移估计目标运动方向"，但代码实际是：
- 计算连续两次检测结果的位移向量
- 对位移量 clamp 到 ±24 像素
- 将位移继续外推（线性运动预测）

**建议改为"根据连续两次检测结果的位移向量进行线性运动预测，并对单步位移量设置上限以防止漂移"。**

### P1-2: 表格中 0.00 ms 值的处理

复用帧（未执行 NPU 推理的帧）的推理相关字段显示 0.00 ms，可能被误解为数据缺失。

**建议：复用帧推理字段统一用 `--` 代替 `0.00 ms`。**

### P1-3: 确认 RGA 实验在论文正文中的覆盖

specs/ 中有 RGA 各路径的验证记录（cvt+resize、letterbox、frame resize、NV12 publish），需确认论文第 5 章是否有对应小节。如果只在 specs/ 中有但论文未提及，需补充。

### P1-4: 摘要未提及软件报警功能

软件报警 overlay 是系统特色功能（替代 GPIO 硬件），正文中多处描述，但中英文摘要均未提及。

**建议在摘要中增加一句话。**

---

## 五、P2 问题（建议改进）

### P2-1: 论文中缺少环境变量配置汇总表

`2026-04-29-final-experiment-material-index.md` 中已有 Runtime Switch Index，可精简后纳入论文正文。

### P2-2: 第 5 章缺少 RK3588 端模型精度对比

表 5.4 仅有帧数和推理时间，无 mAP 或 precision/recall 对比。当前只做了定性验证。

### P2-3: INT8 实验脚本 score 阈值不一致（见上文）

---

## 六、不建议修改的内容

1. **代码结构重构** — 答辩在即，不宜大改
2. **GPIO 闭环实现** — 论文已如实说明为后续工作
3. **INT8 板端验证** — 已如实说明，不应在无数据时写结论
4. **论文整体框架和叙述主线** — 定位为"工程化部署与系统级优化"是诚实恰当的
5. **第 6 章总结与展望** — 对 INT8/RGA/GPIO 的表述已与代码实际状态对齐

---

## 七、论文第 3.3.1 节 8400 来源说明

已确认论文正文（line 920-936）包含正确解释：

> 8400 来自 YOLOv10 三个检测头的特征图位置数量之和，即 80×80、40×40 和 20×20 三个尺度共 8400 个候选位置。

**此项已正确，无需修改。**

---

## 八、论文第 3.5.3/4.6 节跟踪算法描述

已确认论文正文中：
- 正确使用了 `RK_YOLO_TRACK_MODE` 环境变量名（非之前审查中误写的 `RK_YOLO_ZERO_COPY_INPUT`）
- 正确描述了 motion 和 optical flow 两种模式
- 正确引用了 `goodFeaturesToTrack` 和 `calcOpticalFlowPyrLK`
- 正确说明了中值位移而非均值位移
- 正确说明了失败时的回退策略

**跟踪算法描述基本准确，仅需微调 motion 模式的措辞（见 P1-1）。**

---

## 九、参考文献真实性核查

逐条核查 [1]-[14]：

| 编号 | 文献 | 真实性 |
|------|------|--------|
| [1] | Redmon J, Farhadi A. YOLOv3. arXiv:1804.02767 | 真实 |
| [2] | Bochkovskiy A, et al. YOLOv4. arXiv:2004.10934 | 真实 |
| [3] | Wang A, et al. YOLOv10. arXiv:2405.14458 | 真实 |
| [4] | Zhu X, et al. TPH-YOLOv5. ICCVW 2021 | 真实 |
| [5] | Huang B, et al. Anti-UAV410. TPAMI 2024 | 真实 |
| [6] | Tijtgat N, et al. Embedded Real-Time Object Detection for UAV Warning. ICCVW 2017 | 真实 |
| [7] | Rockchip RKNN Toolkit2 GitHub | 真实 |
| [8] | Ultralytics GitHub | 真实 |
| [9] | ONNX Documentation | 真实 |
| [10] | OpenCV Documentation | 真实 |
| [11] | GStreamer Documentation | 真实 |
| [12] | Rockchip RK3588 Product Specification | 真实 |
| [13] | NVIDIA Jetson Nano Technical Specifications | 真实 |
| [14] | NVIDIA Jetson | 真实 |

**无占位、虚构或不相关文献。所有 14 篇均可查证。**

---

## 十、总结

### 论文优点

1. 论文与代码高度一致，无虚假声称
2. 代码工程质量在本科毕设中属较高水平
3. 实验记录体系完善（specs/ 目录 30+ 文件）
4. 第 6 章对未完成工作的表述诚实透明
5. 8400 来源、跟踪算法描述、环境变量名等关键技术细节已正确写入

### 需要改进

1. 参考文献补充（P0-1，最重要）
2. 第二章充实（P0-2）
3. motion 跟踪措辞修正（P1-1）
4. 表格 0.00 ms 处理（P1-2）

### 优先级排序

```
P0-1 补充参考文献       → 立即
P0-2 充实第二章         → 立即
P1-1 motion 措辞修正    → 本周
P1-2 表格 0.00 ms      → 本周
P1-3 RGA 实验覆盖确认   → 本周
P1-4 摘要增加报警       → 有空就改
P2-* 其他改进           → 答辩前有时间再做
```
