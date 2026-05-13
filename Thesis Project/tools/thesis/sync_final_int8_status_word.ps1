$ErrorActionPreference = 'Stop'

$project = 'C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project'
$docPath = Join-Path $project 'paper\full_thesis_latest_merged.docx'
$backupDir = Join-Path $project 'docs\thesis_drafting'
$timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$backupPath = Join-Path $backupDir "full_thesis_latest_merged_before_word_int8_sync_$timestamp.docx"
$pdfPath = Join-Path $backupDir 'qa_word_int8_sync_check.pdf'

Copy-Item -LiteralPath $docPath -Destination $backupPath -Force

$word = New-Object -ComObject Word.Application
$word.Visible = $false
$word.DisplayAlerts = 0

function Set-ParagraphText {
    param(
        [Parameter(Mandatory=$true)] $Paragraph,
        [Parameter(Mandatory=$true)] [string] $Text
    )
    $Paragraph.Range.Text = $Text + "`r"
}

try {
    $doc = $word.Documents.Open($docPath, $false, $false)
    $changed = 0
    $inserted = $false

    foreach ($p in @($doc.Paragraphs)) {
        $text = $p.Range.Text.Trim()

        if ($text.StartsWith('在神经网络推理方面，RK3588内置NPU标称算力为6 TOPS') -and $text.Contains('完整INT8量化仍需要')) {
            Set-ParagraphText $p '在神经网络推理方面，RK3588内置NPU标称算力为6 TOPS（INT8），并支持INT4、INT8、INT16、FP16等多种数据类型加速[12]。对于本文的目标检测任务，NPU主要负责YOLO模型前向推理，CPU则负责视频输入、张量组织、后处理、线程调度和日志记录。需要指出的是，本文当前稳定演示主路径仍采用FP RKNN模型；同时，本文已完成full INT8与hybrid INT8的板端对比实验，并将INT8结果作为硬件优化分析的重要依据，而非简单替代FP主路径。'
            $changed++
            continue
        }

        if ($text.StartsWith('本文当前稳定主路径采用浮点RKNN模型。') -and $text.Contains('INT8 RKNN模型的离线转换')) {
            Set-ParagraphText $p '本文当前稳定主路径采用浮点RKNN模型。虽然INT8量化理论上可以降低计算和存储开销，相关研究表明整数推理能够显著减少模型存储和算术开销[17]，但量化部署必须结合输出分布、校准数据和后处理逻辑进行验证。本文已完成full INT8、hybrid INT8、RGA和zero-copy的板端对比实验。实验表明，直接full INT8量化会导致无人机置信度输出塌缩并出现不出框现象；在保护输出Sigmoid至最终输出层的hybrid INT8方案中，模型能够恢复检测结果。因此，本文仍将FP RKNN作为主要演示路径，同时将hybrid INT8作为更接近任务书量化要求的实验性优化结果。'
            $changed++
            continue
        }

        if ($text.StartsWith('此外，模型格式也是部署约束之一。') -and $text.Contains('INT8量化优化和完整RGA硬件预处理闭环作为后续扩展方向')) {
            Set-ParagraphText $p '此外，模型格式也是部署约束之一。训练阶段通常得到PyTorch权重文件或ONNX模型，而RK3588的NPU推理需要使用RKNN格式模型[7,9]。因此，系统需要建立从训练模型到RKNN模型的转换流程，并在板端验证模型输入输出张量是否与后处理代码匹配。本文当前稳定运行的主路径采用浮点RKNN模型，同时补充实现了RGA硬件预处理闭环和hybrid INT8量化实验路径，用于验证任务书中硬件预处理与量化优化相关要求。'
            $changed++
            continue
        }

        if ($text.StartsWith('在模型转换过程中，本文优先采用FP类型RKNN模型作为稳定部署版本。') -and $text.Contains('INT8量化仅作为后续优化方向保留')) {
            Set-ParagraphText $p '在模型转换过程中，本文优先采用FP类型RKNN模型作为稳定部署版本。针对任务书中的量化优化要求，本文进一步完成了full INT8与hybrid INT8 RKNN模型转换和板端验证。实验发现，直接full INT8虽然可以加载运行，但在公开视频测试中出现置信度输出塌缩，导致检测框无法稳定产生；保护输出Sigmoid至最终输出层的hybrid INT8方案能够恢复检测结果。因此，本章将FP RKNN作为稳定主路径，将hybrid INT8作为经过板端验证的实验性量化路径。'
            $changed++
            continue
        }

        if ($text.StartsWith('因此，本文将RGA和zero-copy作为已经完成可切换验证') -and $text.Contains('INT8输入匹配')) {
            Set-ParagraphText $p '因此，本文将RGA和zero-copy作为已经完成可切换验证的硬件优化探索，而不是默认稳定路径。当前演示仍建议保持OpenCV预处理和常规输入设置；RGA、zero-copy和hybrid INT8量化路径则用于说明系统已经围绕输入传输、硬件预处理和模型量化进行了分层实验。'
            $changed++
            continue
        }

        if ($text -eq 'zero-copy输入、RGA cvt+resize、INT8量化输入匹配') {
            Set-ParagraphText $p 'zero-copy输入、RGA cvt+resize、hybrid INT8量化'
            $changed++
            continue
        }

        if ($text -eq '已完成可切换验证，但默认演示仍保持稳定OpenCV与FP RKNN路径') {
            Set-ParagraphText $p '均已完成可切换验证；hybrid INT8可恢复检测结果，但默认演示仍保持稳定OpenCV与FP RKNN路径'
            $changed++
            continue
        }

        if ($text.StartsWith('需要特别说明的是，INT8量化和完整RGA硬件预处理目前不应写成')) {
            Set-ParagraphText $p '需要特别说明的是，full INT8量化和完整RGA硬件预处理目前不应写成已经替代主路径的稳定优化结果。根据当前实验，FP RKNN模型和OpenCV预处理仍是稳定主路径；RGA cvt+resize、zero-copy输入和hybrid INT8路径已经完成可切换验证，其中hybrid INT8能够在保护少量输出层的条件下恢复检测结果，但仍应表述为硬件优化探索，而不是默认演示配置。'
            $changed++
            continue
        }

        if ($text -eq '保持关闭，作为后续 INT8 和输入格式匹配优化方向。') {
            Set-ParagraphText $p '保持关闭；与hybrid INT8量化共同作为输入格式和内存路径优化方向。'
            $changed++
            continue
        }

        if ($text.StartsWith('阶段耗时分析进一步指出，当前系统的主要瓶颈不是后处理和画框') -and $text.Contains('RGA letterbox 和 zero-copy 实验表明')) {
            Set-ParagraphText $p '阶段耗时分析进一步指出，当前系统的主要瓶颈不是后处理和画框，而是 RKNN 输入设置、NPU 执行和实时调度之间的组合开销。RGA cvt+resize、RGA 帧缩放、RGA letterbox、zero-copy 和 hybrid INT8 实验表明，硬件输入路径与模型量化路径均具备继续优化空间，但不同路径对端到端性能和检测稳定性的影响并不一致。在成为默认路径前，仍需结合输入数据类型、模型量化方式、MPP 解码、RGA 物理连续内存和实时 RTSP 链路继续验证。'
            if (-not $inserted) {
                $p.Range.InsertAfter("在INT8量化方面，本文进一步完成了多组校准样本和量化配置的板端对比。直接full INT8模型虽然模型体积较小且可以执行推理，但在公开视频中无法稳定产生检测框，说明无人机小目标任务对输出置信度量化较敏感。采用hybrid INT8配置后，仅保护输出Sigmoid至最终输出层，模型在anti_uav_fig1.mp4的130帧测试中恢复了检测结果；在阈值0.35时，hybrid_sigmoid500配置获得23帧有检测结果、31个检测框，平均rknn_run约30.637 ms。该结果表明INT8优化已经从离线转换推进到板端有效性验证，但最终演示仍优先选择FP RKNN以保证检测稳定性。`r")
                $inserted = $true
            }
            $changed++
            continue
        }

        if ($text.StartsWith('本文工作仍存在若干可继续完善之处。首先，当前最终演示主路径仍以FP RKNN模型为主。') -and $text.Contains('INT8模型尚需在RK3588上进一步完成')) {
            Set-ParagraphText $p '本文工作仍存在若干可继续完善之处。首先，当前最终演示主路径仍以FP RKNN模型为主。项目后期已经完成full INT8和hybrid INT8的板端验证，其中full INT8能够加载运行但出现置信度输出塌缩和不出框现象，hybrid INT8通过保护输出Sigmoid至最终输出层恢复了检测结果。该结果说明本文已对INT8量化进行了工程验证和误差分析，但尚不能得出其全面优于FP主路径的结论。其次，RGA硬件预处理、RGA帧缩放和NV12发布路径已经实现并完成部分验证，但不同RGA路径的端到端收益并不一致，后续仍需结合MPP解码、物理连续内存和模型输入格式继续优化。第三，外设GPIO闭环控制尚未作为本文已完成主线，当前系统采用软件报警overlay和报警事件CSV作为直观替代；后续可进一步接入报警、云台或其他执行机构，形成从目标感知到硬件响应的完整系统。最后，本文公开视频和室内演示能够支持系统验证，但真实飞行场景受测试条件限制仍不充分，后续应在安全合规条件下补充不同距离、光照、背景和运动状态下的真实无人机测试。'
            $changed++
            continue
        }

        if ($text.StartsWith('与任务书和开题阶段的初始设想相比，本文实际实施过程中对研究重点进行了收敛。') -and $text.Contains('INT8量化已完成离线RKNN转换但仍需板端充分验证')) {
            Set-ParagraphText $p '与任务书和开题阶段的初始设想相比，本文实际实施过程中对研究重点进行了收敛。由于无人机专用模型在RK3588板端适配过程中出现过输出张量不匹配和rknn_run运行异常等问题，模型恢复、后处理适配和实时链路稳定性调试占用了较多周期。因此，本文将可验证主线集中在FP RKNN模型稳定部署、固定视频与RTSP实时检测、检测间隔/框平滑策略以及多context NPU并行推理实验上。同时，本文补充完成了RGA严格路径和hybrid INT8量化路径验证；其中RGA用于证明视频采集、RGA预处理、NPU推理和后处理链路已经打通，hybrid INT8用于说明量化优化已从离线转换推进到板端检测恢复阶段。GPIO硬件闭环则以软件报警显示和事件日志作为替代演示。这样的处理能够保证论文结论与当前代码和实验数据保持一致。'
            $changed++
            continue
        }

        if ($text.StartsWith('为了兼顾技术效果、社会环境和可持续发展，后续工作可以从三个方向继续完善。首先，在模型层面，应在现有FP RKNN稳定部署基础上继续完成INT8量化模型')) {
            Set-ParagraphText $p '为了兼顾技术效果、社会环境和可持续发展，后续工作可以从三个方向继续完善。首先，在模型层面，应在现有FP RKNN稳定部署基础上继续完善hybrid INT8量化模型的精度和速度对比，并进一步研究full INT8置信度塌缩的原因，明确量化是否真正带来端到端收益，而不是只关注理论算力提升。其次，在系统层面，可进一步完善RGA硬件预处理、零拷贝输入和统一性能日志，使每次优化都能够通过同一套公开视频和同一组指标进行复现实验。再次，在应用层面，如果未来需要接入实际报警设备，应将软件报警接口抽象为统一事件输出层，再按场景连接GPIO、网络消息或上位机平台，避免把硬件控制逻辑直接写死在检测程序中。'
            $changed++
            continue
        }
    }

    if ($changed -lt 10 -or -not $inserted) {
        throw "Unexpected update count: changed=$changed inserted=$inserted"
    }

    $doc.Save()
    $doc.ExportAsFixedFormat($pdfPath, 17)
    $doc.Close($false)
    Write-Output "changed=$changed inserted=$inserted"
    Write-Output "backup=$backupPath"
    Write-Output "exported=$pdfPath"
}
finally {
    $word.Quit()
}
