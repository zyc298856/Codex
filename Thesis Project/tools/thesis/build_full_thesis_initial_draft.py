from __future__ import annotations

import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
THESIS_DIR = ROOT / "docs" / "thesis_drafting"
BUILDER_PATH = ROOT / "tools" / "thesis" / "build_chapter_docx_ooxml.py"


def load_builder():
    spec = importlib.util.spec_from_file_location("chapter_builder", BUILDER_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load builder: {BUILDER_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read_chapter(name: str) -> str:
    text = (THESIS_DIR / name).read_text(encoding="utf-8").strip()
    return text


def front_matter() -> str:
    return """# 基于嵌入式平台的目标检测系统研究

本科毕业论文（设计）初稿

学院：通信与信息工程学院

专业：电子信息工程

学号：22123739

学生姓名：朱奕澄

指导教师：滕国伟

[[PAGEBREAK]]

# 原创性声明

本人声明：所提交的毕业论文（设计）是在指导教师指导下进行的研究工作。除文中已经标明引用的内容外，本文不包含其他个人或集体已经发表或撰写过的研究成果。对本文研究工作作出贡献的个人和集体，均已在文中以明确方式标明。

签名：                    日期：

# 本论文使用授权说明

本人了解并遵守上海大学关于本科毕业论文（设计）保存、使用和管理的有关规定。学校有权保存论文及其电子版，允许论文被查阅和借阅，并可根据相关规定公布论文的全部或部分内容。

签名：                    指导教师签名：                    日期：

[[PAGEBREAK]]

# 摘要

随着无人机在低空安防、电力巡检和城市管理等场景中的应用增多，面向边缘端的实时目标检测系统具有重要的工程应用价值。针对传统云端检测方案存在带宽占用高、端到端延迟大和现场部署灵活性不足等问题，本文围绕 RK3588 嵌入式平台设计并实现了一套无人机目标检测系统。系统以 YOLOv10 单类无人机检测模型为基础，完成 PyTorch、ONNX 到 RKNN 模型的迁移与板端适配，并采用 C/C++ 实现固定视频检测和实时 RTSP 检测程序。

在系统实现方面，本文构建了视频输入、图像预处理、NPU 推理、后处理、显示推流和日志记录相互解耦的处理流程。针对实时检测过程中容易出现的吞吐不足、端到端延迟升高和检测框抖动问题，系统实现了检测间隔控制、动态 ROI、轻量跟踪、检测框平滑和多 context NPU 并行推理等策略。实验结果表明，恢复后的单类无人机 RKNN 模型能够在 RK3588 上完成固定视频检测，并可通过 RTSP 输出带框视频流。多 context 机制在每帧检测场景下能够提高 NPU 推理吞吐，而检测间隔与框平滑策略更适合低延迟实时观看场景。本文工作为目标检测算法在国产嵌入式平台上的工程化部署与系统级优化提供了参考。

关键词：嵌入式平台；目标检测；RK3588；神经网络处理单元；多线程

[[PAGEBREAK]]

# ABSTRACT

With the increasing use of unmanned aerial vehicles in low-altitude security, power inspection, and urban management, real-time object detection on edge devices has significant engineering value. To address the high bandwidth consumption, large end-to-end latency, and limited deployment flexibility of cloud-based detection schemes, this thesis designs and implements a UAV object detection system based on the RK3588 embedded platform. The system uses a single-class YOLOv10 UAV detection model as the baseline, completes the model migration from PyTorch and ONNX to RKNN, and implements both fixed-video detection and real-time RTSP detection programs in C/C++.

In terms of system implementation, the proposed system decouples video input, image preprocessing, NPU inference, post-processing, display streaming, and logging. To reduce throughput bottlenecks, end-to-end latency, and bounding-box jitter in real-time detection, the system implements detection interval control, dynamic ROI, lightweight tracking, bounding-box smoothing, and multi-context NPU parallel inference. Experimental results show that the recovered single-class UAV RKNN model can perform fixed-video detection on RK3588 and output annotated video streams through RTSP. The multi-context mechanism improves inference throughput in frame-by-frame detection, while detection interval control and bounding-box smoothing are more suitable for low-latency real-time viewing. This work provides a practical reference for engineering deployment and system-level optimization of object detection algorithms on domestic embedded platforms.

Keywords: Embedded platform; Object detection; RK3588; Neural Processing Unit; Multithreading

[[PAGEBREAK]]

# 目录

本页为目录占位。完整终稿应在 Word 中基于标题样式自动生成目录，并显示至三级标题。

[[PAGEBREAK]]
"""


def chapter_1() -> str:
    return """# 第一章 绪论

## 1.1 研究背景与意义

近年来，无人机在低空巡检、公共安全、应急救援和城市管理等领域得到广泛应用。无人机具有机动性强、部署灵活和使用成本较低等特点，但在复杂低空环境中也带来了目标监测、区域安全和实时预警等方面的新需求。对于无人机检测任务，如果将视频流传输到云端服务器进行处理，系统往往需要占用较高网络带宽，并引入额外传输延迟。当应用场景需要在本地快速完成目标识别和响应时，云端处理方式难以满足实时性和稳定性要求。

边缘计算为上述问题提供了可行路径。将目标检测模型部署到嵌入式平台后，系统可以在靠近数据源的位置完成视频采集、图像预处理、模型推理和结果输出，从而降低网络依赖并提升响应速度。RK3588 平台集成多核 CPU、GPU 和神经网络处理单元（Neural Processing Unit，NPU），具备较强的端侧推理能力，适合用于构建实时目标检测系统。然而，在实际部署中，模型格式转换、算子兼容、输入输出张量解析、视频流处理、多线程调度和硬件资源利用之间存在紧密耦合，单纯完成模型转换并不能保证系统达到实时运行效果。

因此，本文以“基于嵌入式平台的目标检测系统研究”为题，围绕无人机识别算法在 RK3588 平台上的工程化移植与实时部署展开研究。本文重点不在于提出新的目标检测网络结构，而是在现有检测模型基础上完成面向嵌入式平台的模型迁移、C/C++ 部署、多线程流水线设计、NPU 多 context 推理和系统性能分析。该研究对于理解目标检测算法从训练环境走向边缘设备的完整工程链路具有一定实践意义。

## 1.2 国内外研究现状

目标检测是计算机视觉领域的重要任务之一，其目标是在图像或视频中定位并识别特定类别目标。以 YOLO 系列为代表的一阶段检测算法具有结构简洁、推理速度快和工程部署方便等特点，在实时检测任务中应用广泛。随着网络结构、特征融合和训练策略不断改进，目标检测算法在通用数据集上的精度和速度均有明显提升。

无人机检测属于小目标检测的典型应用场景。远距离无人机在图像中往往只占据少量像素，且容易受到天空、建筑、树枝、鸟类和光照变化等干扰。针对这类任务，研究者通常从高分辨率特征、注意力机制、跨尺度融合、样本增强和专用数据集构建等方面提升检测能力。导师提供的 SDD-YOLO 和 UAV-DETR 相关资料也说明，面向反无人机场景的算法研究正逐步关注小目标表征、轻量化结构和边缘部署友好性。

在嵌入式部署方面，研究重点从单纯模型压缩逐步转向系统级协同优化。实际运行帧率不仅取决于神经网络推理耗时，还受到图像预处理、数据拷贝、后处理、编码推流、线程同步和队列积压等因素影响。对于 RK3588 这类异构平台，合理使用 NPU、CPU 和视频处理组件，是提升系统整体性能的重要手段。因此，端侧目标检测系统需要在模型迁移、运行时接口和多线程调度之间进行综合设计。

## 1.3 本文主要研究内容

本文围绕 RK3588 嵌入式平台上的无人机目标检测系统开展研究，主要内容包括以下几个方面。

1. 完成无人机检测模型从训练环境到 RK3588 平台的迁移。本文以已训练的 YOLOv10 单类无人机检测模型为基础，完成 PyTorch 权重、ONNX 模型和 RKNN 模型之间的转换与验证，并对板端输出张量进行解析。

2. 设计并实现 C/C++ 固定视频检测和实时 RTSP 检测程序。固定视频程序用于可重复实验和公开视频验证，实时 RTSP 程序用于 USB 摄像头或本地视频输入下的实时显示。

3. 构建面向实时检测的多线程处理流程。系统将视频读取、NPU 推理、结果绘制、RTSP 推流和日志记录进行解耦，降低单一串行流程对实时性的影响。

4. 实现检测间隔、动态 ROI、轻量跟踪和框平滑策略。上述策略用于降低不必要的 NPU 调用，并改善实时观看时检测框抖动和延迟问题。

5. 开展 RK3588 NPU 多 context 并行推理实验。本文比较单 context、双 context 和多 context 配置下的运行表现，并结合阶段耗时分析定位系统瓶颈。

## 1.4 论文组织结构

本文共分为五章。第一章介绍研究背景、研究意义、国内外研究现状和本文主要研究内容。第二章介绍目标检测、无人机小目标检测、RK3588 平台、RKNN 部署和 C/C++ 多线程视频处理等相关技术基础。第三章给出系统总体设计，包括需求分析、总体架构、模型迁移设计、实时视频处理流水线、动态 ROI 与多 context 推理设计。第四章阐述系统实现与调试过程，包括开发环境、模型转换、RKNN 推理模块、固定视频程序、实时 RTSP 程序和日志接口。第五章给出系统测试与实验分析，包括训练结果、公开视频固定输入验证、实时 RTSP 实验、多 context 对比、阶段耗时分析和 zero-copy 输入实验。

[[PAGEBREAK]]
"""


def chapter_2() -> str:
    return """# 第二章 相关技术基础

## 2.1 目标检测算法基础

目标检测任务需要同时完成目标类别判断和边界框定位。与图像分类任务相比，目标检测不仅需要判断图像中是否存在某类目标，还需要给出目标在图像中的空间位置。现代目标检测算法通常由主干网络、特征融合模块和检测头组成。主干网络负责提取图像特征，特征融合模块用于整合不同尺度信息，检测头输出目标框位置、类别和置信度。

YOLO 系列算法属于一阶段目标检测方法，其基本思想是在一次前向推理中直接预测目标类别和边界框位置。该类算法具有推理速度快、部署链路成熟和工程生态完善等优点，因此常被用于实时检测场景。本文所使用的无人机检测模型属于单类别目标检测模型，板端后处理重点集中在边界框解析、置信度筛选、非极大值抑制和坐标还原等步骤。

## 2.2 无人机小目标检测特点

无人机检测与通用目标检测相比具有更明显的小目标特征。在地面对空拍摄场景中，远距离无人机在画面中所占像素较少，目标外观信息有限，且容易与鸟类、云层、树枝、建筑边缘和图像噪声混淆。当目标尺寸较小时，轻微的定位偏差就可能导致 IoU 指标明显下降，从而影响检测评价结果。

此外，无人机检测系统通常具有较强实时性要求。对于固定视频实验，系统需要能够稳定输出逐帧检测结果和可复现实验日志；对于实时演示场景，系统还需要保证输出画面连续、检测框稳定、延迟较低。上述特点决定了本文不仅需要关注模型检测精度，还需要关注端侧系统吞吐、延迟、资源占用和显示稳定性。

## 2.3 RK3588 嵌入式平台

RK3588 是面向边缘计算场景的高性能嵌入式处理平台，集成多核 CPU、GPU、视频编解码模块和 NPU 等异构计算资源。对于目标检测任务，NPU 可承担神经网络前向推理计算，CPU 则负责视频输入、数据组织、后处理和线程调度。与通用 PC 或服务器环境相比，嵌入式平台在功耗、内存带宽、接口能力和运行稳定性方面具有不同约束。

在实际部署过程中，系统性能并不只由 NPU 推理时间决定。图像从摄像头或视频文件进入系统后，需要经过解码、缩放、颜色空间转换、输入张量设置、模型推理、输出解析、绘制和推流等步骤。任一环节耗时过高，都可能造成队列积压和端到端延迟升高。因此，在 RK3588 上实现实时检测，需要从系统级角度组织各处理模块。

## 2.4 RKNN 模型部署流程

RKNN 是 Rockchip 面向其 NPU 平台提供的模型格式和运行时接口。通常情况下，训练得到的 PyTorch 权重需要先导出为 ONNX 模型，再通过 RKNN Toolkit2 转换为 RKNN 模型文件。转换过程中需要关注输入尺寸、算子兼容性、模型输出格式和量化配置等问题。若模型包含不受支持的算子，可能导致转换失败或运行时性能下降。

本文当前稳定主路径采用浮点 RKNN 模型。虽然 INT8 量化理论上可以降低计算和存储开销，但量化需要校准数据、误差分析和板端精度验证。考虑到当前系统已稳定跑通浮点模型，本文将 FP RKNN 作为主要实验对象，INT8 量化作为后续优化方向进行讨论，而不将其写成已经完整完成的优化结果。

## 2.5 C/C++ 多线程与实时视频流水线

实时视频检测系统通常包含多个具有不同耗时特征的处理阶段。如果所有阶段在单线程中串行执行，系统整体帧率会受到最慢阶段限制，同时难以充分利用多核 CPU 和 NPU 资源。多线程流水线通过将采集、推理、发布等模块解耦，可以在不同帧之间形成重叠执行，从而提升吞吐能力。

本文系统采用 C/C++ 实现板端程序，主要原因在于 C/C++ 便于直接调用 RKNN Runtime、OpenCV 和 GStreamer 等底层库，并能够更精细地控制线程、队列和内存生命周期。在线程协作方面，系统使用有限长度队列连接不同模块，避免输入速度高于处理速度时造成无限堆积。对于实时显示任务，系统更关注最新帧和低延迟，因此在必要时允许丢弃过旧帧，以换取更好的观看响应。

## 2.6 本章小结

本章介绍了本文涉及的目标检测、无人机小目标检测、RK3588 平台、RKNN 部署和 C/C++ 多线程视频处理等基础内容。上述技术构成了后续系统总体设计、板端实现和性能实验分析的基础。

[[PAGEBREAK]]
"""


def back_matter() -> str:
    return """[[PAGEBREAK]]

# 总结与展望

本文围绕“基于嵌入式平台的目标检测系统研究”这一课题，完成了无人机目标检测模型在 RK3588 平台上的迁移、部署和系统级实验分析。本文以单类无人机检测模型为基础，完成模型导出、RKNN 转换、板端加载和输出张量解析，并在 C/C++ 程序中实现固定视频检测与实时 RTSP 检测两条运行路径。

在系统设计与实现方面，本文构建了面向实时检测的多线程处理流程，使视频输入、NPU 推理、结果绘制、推流输出和日志记录能够相互解耦。针对实时演示中出现的延迟、抖动和吞吐问题，系统实现了检测间隔控制、动态 ROI、轻量跟踪、检测框平滑和多 context 推理等策略。实验结果表明，双 context 在每帧检测模式下能够提升 NPU 推理吞吐；而在实际实时观看场景中，检测间隔配合框平滑更有利于降低端到端延迟并改善显示稳定性。

本文工作仍存在若干可继续完善之处。首先，当前主路径采用 FP RKNN 模型，INT8 量化尚未形成完整的精度和速度闭环验证，后续可基于更充分的校准数据开展量化实验。其次，当前预处理路径主要采用 OpenCV 实现，RGA 硬件预处理仍可作为后续优化方向。最后，外设 GPIO 闭环控制尚未作为本文已完成主线，后续可进一步接入报警、云台或其他执行机构，形成从目标感知到硬件响应的完整系统。

[[PAGEBREAK]]

# 参考文献

[1] Redmon J, Farhadi A. YOLOv3: An Incremental Improvement[J]. arXiv preprint arXiv:1804.02767, 2018.

[2] Jocher G, Chaurasia A, Qiu J. Ultralytics YOLOv8[EB/OL]. GitHub, 2023. https://github.com/ultralytics/ultralytics.

[3] Rockchip. RKNN Toolkit2 User Guide and RK3588 NPU Documentation[EB/OL]. Rockchip Official Documentation, 2024.

[4] Zhu X, Lyu S, Wang X, et al. TPH-YOLOv5: Improved YOLOv5 Based on Transformer Prediction Head for Object Detection on Drone-captured Scenarios[C]//Proceedings of the IEEE/CVF International Conference on Computer Vision Workshops. 2021: 2778-2788.

[5] Tijtgat N, Van Ranst W, Goedeme T, et al. Embedded Real-Time Object Detection for a UAV Warning System[C]//IEEE International Conference on Computer Vision Workshops. 2017: 2110-2118.

[6] Huang B, Li J, Chen J, et al. Anti-UAV410: A Thermal Infrared Benchmark and Customized Scheme for Tracking Drones in the Wild[J]. IEEE Transactions on Pattern Analysis and Machine Intelligence, 2023, 46(5): 2852-2865.

[7] Xu Y, Fu M, Wang Q, et al. Gliding Vertex on the Horizontal Bounding Box for Multi-Oriented Object Detection[J]. IEEE Transactions on Pattern Analysis and Machine Intelligence, 2021, 43(4): 1452-1459.

[8] Bochkovskiy A, Wang C Y, Liao H Y M. YOLOv4: Optimal Speed and Accuracy of Object Detection[J]. arXiv preprint arXiv:2004.10934, 2020.

[[PAGEBREAK]]

# 致谢

本课题的完成离不开指导教师在研究方向、系统设计和论文撰写方面给予的指导与帮助。在项目推进过程中，导师对 RK3588 嵌入式平台、NPU 多线程优化和系统级实验提出了明确要求，使本文能够从单纯模型部署进一步扩展到工程化实时系统设计与性能分析。

同时，感谢同学和实验环境提供者在模型、数据、开发板调试和测试场景方面给予的支持。本文所涉及的模型转换、板端部署、实时视频推流和公开数据验证经历了多轮调试，相关讨论和协助对系统最终跑通具有重要帮助。

最后，感谢家人和朋友在毕业设计期间给予的理解与支持。
"""


def main() -> None:
    chapter3 = read_chapter("chapter_3_system_design_draft.md")
    chapter4 = read_chapter("chapter_4_system_implementation_draft.md")
    chapter5 = read_chapter("chapter_5_experiments_analysis_draft.md")

    full = "\n\n".join(
        [
            front_matter().strip(),
            chapter_1().strip(),
            chapter_2().strip(),
            chapter3,
            "[[PAGEBREAK]]",
            chapter4,
            "[[PAGEBREAK]]",
            chapter5,
            back_matter().strip(),
        ]
    )

    md_path = THESIS_DIR / "full_thesis_initial_draft.md"
    docx_path = THESIS_DIR / "full_thesis_initial_draft.docx"
    md_path.write_text(full + "\n", encoding="utf-8")

    builder = load_builder()
    builder.build_docx(md_path, docx_path)

    print(md_path)
    print(docx_path)


if __name__ == "__main__":
    main()
