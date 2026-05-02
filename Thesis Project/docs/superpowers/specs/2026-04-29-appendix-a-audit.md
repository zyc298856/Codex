# 附录A"英译汉"审查报告

> 审查日期：2026-04-29
> 审查对象：full_thesis_latest_merged.docx 附录A 英译汉
> 审查依据：撰写格式模板2026年6月.docx + Edge-YOLO原文（MDPI）
> 审查人：Claude Code

---

## 一、总体结论

**不建议提交** — 存在1个严重问题需修复后方可提交。

```
通过项：4/7
不通过项：1/7（严重）
需修改项：2/7
```

---

## 二、英文文献真实性

### 结论：真实存在，来源可靠

论文引用的文献：

> Li J., Ye J. Edge-YOLO: Lightweight Infrared Object Detection Method Deployed on Edge Devices. Applied Sciences, 2023, 13(7), 4402. DOI: 10.3390/app13074402.

逐项验证：

| 检查项 | 结果 |
|--------|------|
| 期刊 | Applied Sciences (MDPI)，SCI收录开放获取期刊 |
| 卷/期/页 | 2023, Vol.13, Issue 7, Article 4402 |
| DOI | https://doi.org/10.3390/app13074402 — 可正常访问 |
| 作者 | Junqing Li, Jiongyao Ye（华东理工大学信息科学与工程学院） |
| 论文编号 | thesis_text.txt 中参考文献 [21] |
| 可获取全文 | 是，MDPI开放获取，PDF可直接下载 |

---

## 三、文献与课题匹配度

### 结论：高度相关

| 匹配维度 | Edge-YOLO论文内容 | 本文课题 |
|----------|-------------------|----------|
| 目标检测 | 红外道路目标检测 | 无人机目标检测 |
| 边缘部署 | RK3588嵌入式开发板 | RK3588嵌入式平台 |
| NPU加速 | 6 TOPS NPU，INT4/8/16/FP16 | 6 TOPS NPU，FP/INT8 |
| 模型转换 | PyTorch → ONNX → RKNN | PyTorch → ONNX → RKNN |
| 工具链 | RKNN-Toolkit2 + rknpu2 | RKNN-Toolkit2 |
| YOLO系列 | YOLOv5m改（ShuffleBlock） | YOLOv10单类 |
| 量化讨论 | 提及量化精度下降 | INT8为后续方向 |

两者都关注"如何在RK3588上将YOLO类检测模型从训练环境迁移到NPU上运行"，核心问题高度共通。

---

## 四、严重问题：英文"原文"不是论文节选

### 问题描述

论文附录A声明：

> "The following English material is **selected and adapted** from the above open-access article for translation practice."

但实际英文文本**不是Edge-YOLO论文的真实节选**，而是读完论文后自写的英文综述/改写文本。

### 证据

以下表述在真实论文节选中不可能出现：

| 英文文本中的句子 | 为什么不可能是原文节选 |
|-----------------|---------------------|
| "The Edge-YOLO method **described in the source article** attempts to..." | 论文不会以第三人称称呼自己为"the source article" |
| "**The article** emphasizes that edge deployment..." | 论文不会以第三人称评论自己 |
| "**The article's** use of RK3588 is especially relevant for domestic embedded AI development." | 论文不会评价自己的工作"especially relevant" |
| "**For thesis work**, the article also suggests a practical evaluation method." | 论文不会提及"thesis work"——这是写给毕业论文的 |
| "Another **useful point** in the article is..." | 论文不会评价自己的内容"useful" |
| "**Overall**, the source article illustrates..." | 这是综述的总结句，不是论文原文 |
| "The **main lesson** is that..." | 论文不会总结自己的"main lesson" |
| "These observations have **direct engineering significance**." | 评价性语言，非论文原文 |

### 与真实论文对比

| 特征 | 真实Edge-YOLO论文 | 附录A英文文本 |
|------|------------------|--------------|
| 人称 | "We propose..."（第一人称） | "The authors..."（第三人称） |
| 结构 | Section 1/2/3编号，含figure/table引用 | 无章节编号，无figure/table |
| 公式 | 有loss function公式 | 无任何公式 |
| 实验数据 | 有mAP/FPS具体数值表格 | 无具体数值 |
| 语言风格 | 学术论文写作 | 文献综述/读书笔记 |

### 实质

这段英文文本是**读完全文后用英文写的一篇文献综述/读书笔记**，而非论文原文节选。

学校模板要求的是"**英文原稿**"（original manuscript），即应从原论文中直接选取连续段落。

### 风险

1. **答辩风险**：答辩老师如果查阅Edge-YOLO原文，会发现英文文本与原文风格、措辞完全不同
2. **查重风险**：这段文本与原文不匹配，但声明来自原文，可能被查重系统标记
3. **学术规范**：将自写内容标注为"selected from"属于来源标注不实

---

## 五、中文翻译质量与字数

### 结论：翻译质量合格，字数余量不足

| 检查项 | 结果 |
|--------|------|
| CJK汉字数 | **2506字** |
| 2500字门槛 | 刚好超过（余量仅6字） |
| 翻译对应性 | 9/9个关键技术术语准确对应 |
| 翻译流畅度 | 整体准确流畅 |

术语对应验证：

| 英文 | 中文 | 对应 |
|------|------|------|
| road surveillance | 道路监控 | 正确 |
| template matching | 模板匹配 | 正确 |
| threshold segmentation | 阈值分割 | 正确 |
| ShuffleBlock | ShuffleBlock | 正确（保留原文） |
| strip depthwise convolutional attention | 条形深度可分离卷积注意力 | 正确 |
| Cortex-A76 / Cortex-A55 | Cortex-A76 / Cortex-A55 | 正确 |
| 6 TOPS | 6 TOPS | 正确 |
| RKNN-Toolkit2 | RKNN-Toolkit2 | 正确 |
| ONNX format | ONNX格式 | 正确 |

### 需注意：翻译最后一段无英文对应

中文翻译最后一段"将这篇文献与本文课题相联系可以发现..."在英文文本中**没有对应段落**。这段是自写的关联评论，不属于翻译内容，应当删除或移至论文正文。

---

## 六、一般问题

| 编号 | 问题 | 风险等级 | 说明 |
|------|------|---------|------|
| G-1 | 字数余量仅6字 | 中 | 建议目标3000+字 |
| G-2 | 翻译末尾含无英文对应的自写评论 | 中 | 应删除最后一段 |
| G-3 | 当前英文文本约1296词 | 低 | 替换为真实节选后长度可能不同，翻译字数需重新确认 |

---

## 七、格式合规性（需在DOCX中确认）

| 编号 | 检查项 | 模板要求 | 文本中可见状态 |
|------|--------|---------|--------------|
| F-1 | 标题 | "附录A 英译汉"，黑体小二 | 标题存在，字号需在Word中确认 |
| F-2 | 内容字体 | TNR/宋体，小四号 | 需在Word中确认 |
| F-3 | 行距 | 1.5倍 | 需在Word中确认 |
| F-4 | 小节标题 | "一、英文原文" / "二、英文翻译" | 存在且格式正确 |
| F-5 | 目录 | 包含"附录A 英译汉"并可跳转 | 目录条目存在（TOC条目可见） |
| F-6 | 位置 | 参考文献之后、附录B之前 | 正确 |

---

## 八、修复方案

### 核心修改：将英文原文替换为Edge-YOLO论文的真实节选

**步骤：**

1. 下载Edge-YOLO全文PDF：https://www.mdpi.com/2076-3417/13/7/4402

2. 从论文中选取连续1.5-2页的真实段落。建议方案：

   | 方案 | 选取内容 | 优点 | 预估英文词数 |
   |------|---------|------|-------------|
   | **A（推荐）** | Section 1 Introduction 全部 | 内容涵盖研究背景和动机，翻译后容易达到3000+中文字 | ~800-1000词 |
   | B | Section 3 Method 核心段落 | 技术性最强，与课题直接相关 | ~600-800词 |
   | C | Section 1 + Section 4 部分段落 | 兼顾背景和实验 | ~1000-1200词 |

3. 以截图方式粘贴英文原文（模板允许"英文原稿可以截图粘贴"），确保清晰

4. 对选取的真实段落进行逐段翻译，目标3000+中文字

5. 删除当前所有自写评论性内容：
   - 英文侧：删除所有"The article..."、"For thesis work..."、"Overall..."等非原文句子
   - 中文侧：删除最后一段"将这篇文献与本文课题相联系..."

6. 确认替换后中文字数≥3000字

7. 确认Word中字体、字号、行距符合模板

### 时间估算

| 步骤 | 预估时间 |
|------|---------|
| 下载PDF、选取段落 | 15 min |
| 截图粘贴到Word | 10 min |
| 翻译3000+中文字 | 2-3 h |
| 格式调整 | 30 min |
| **总计** | **约3-4小时** |

---

## 九、总结

| 维度 | 状态 | 说明 |
|------|------|------|
| 文献真实性 | 通过 | Edge-YOLO真实存在，DOI/期刊/作者均可查证 |
| 课题匹配度 | 通过 | 高度相关（RK3588, NPU, YOLO, 边缘部署） |
| 英文原文真实性 | **不通过** | 非论文节选，是自写英文综述 |
| 中文翻译质量 | 通过 | 术语准确，对应性好 |
| 中文字数 | 通过（勉强） | 2506字，仅超门槛6字 |
| 格式结构 | 待确认 | 标题/位置正确，字号需在Word中检查 |
| 学术规范 | **不通过** | 将自写内容标注为"selected from" |

**核心行动：替换英文原文为Edge-YOLO论文的真实节选，重新翻译。**
