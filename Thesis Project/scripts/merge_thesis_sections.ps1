$ErrorActionPreference = "Stop"

$docx = "C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project\paper\full_thesis_latest_merged.docx"
$pdf = "C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project\paper\full_thesis_latest_merged.pdf"

$major = [ordered]@{
    "3.1 系统需求分析" = "3.1 系统需求与总体架构"
    "3.3 模型移植与检测模块设计" = "3.2 模型移植与检测模块设计"
    "3.4 实时视频处理流水线设计" = "3.3 实时视频流水线与输出设计"
    "3.5 动态ROI与轻量跟踪策略设计" = "3.4 动态ROI、轻量跟踪与多context设计"
    "4.1 开发与部署环境" = "4.1 开发环境与模型转换实现"
    "4.3 RKNN推理模块实现" = "4.2 RKNN推理与固定视频程序实现"
    "4.5 实时RTSP检测程序实现" = "4.3 实时RTSP检测与策略控制实现"
    "4.7 多context NPU并行推理实现" = "4.4 多context并行、日志与性能调试实现"
    "5.1 实验环境与评价指标" = "5.1 实验环境、评价指标与模型训练结果"
    "5.3 公开视频固定输入验证" = "5.2 公开视频固定输入与实时RTSP验证"
    "5.5 检测间隔与框平滑策略实验" = "5.3 检测间隔、框平滑与多context实验"
    "5.7 阶段耗时与瓶颈分析" = "5.4 阶段耗时、Zero-copy与硬件路径分析"
    "5.9 综合讨论与推荐配置" = "5.5 推荐配置与长时间稳定性测试"
}

$demote = @(
    "3.1.1 功能需求",
    "3.1.2 性能需求",
    "3.1.3 部署约束",
    "3.2 系统总体架构",
    "3.3.1 模型移植流程",
    "3.3.2 检测结果解析",
    "3.4.1 多线程流水线结构",
    "3.4.2 实时输出方式",
    "3.5.1 检测间隔策略",
    "3.5.2 动态ROI策略",
    "3.5.3 轻量跟踪与框平滑",
    "3.6 多context NPU并行推理设计",
    "3.6.1 多context设计思想",
    "3.6.2 多context与检测间隔的关系",
    "3.6.3 阶段耗时分析设计",
    "3.7 本章小结",
    "4.2 模型导出与RKNN转换实现",
    "4.4 固定视频检测程序实现",
    "4.6 检测间隔、动态ROI与轻量跟踪实现",
    "4.8 日志记录与性能调试接口",
    "4.9 本章小结",
    "5.2 无人机检测模型训练结果",
    "5.4 实时RTSP首轮配置对比",
    "5.6 多context NPU并行推理实验",
    "5.8 Zero-copy输入路径探索",
    "5.10 长时间运行稳定性测试",
    "5.11 本章小结"
)

function Replace-ParagraphText($paragraph, [string]$newText) {
    $range = $paragraph.Range
    if ($range.End -gt $range.Start) {
        $range.End = $range.End - 1
    }
    $range.Text = $newText
}

function Strip-NumberPrefix([string]$text) {
    return ($text -replace '^\d+\.\d+(\.\d+)?\s*', '').Trim()
}

$word = $null
$doc = $null

try {
    $word = New-Object -ComObject Word.Application
    $word.Visible = $false
    $word.DisplayAlerts = 0
    $doc = $word.Documents.Open($docx)

    foreach ($p in $doc.Paragraphs) {
        $txt = $p.Range.Text.Trim([char]13, [char]7, " ", "`t")
        if (-not $txt) { continue }

        $styleName = ""
        try { $styleName = $p.Range.Style.NameLocal } catch {}
        if ($styleName -like "TOC*") { continue }

        if ($major.Contains($txt)) {
            Replace-ParagraphText $p $major[$txt]
            $p.OutlineLevel = 2
            $p.Range.ListFormat.RemoveNumbers()
            $p.Range.Font.Bold = $true
            $p.Range.Font.Size = 14
            continue
        }

        if ($demote -contains $txt) {
            $newText = Strip-NumberPrefix $txt
            Replace-ParagraphText $p $newText
            $p.OutlineLevel = 10
            $p.Range.ListFormat.RemoveNumbers()
            $p.Range.Font.Bold = $true
            $p.Range.Font.Size = 12
            $p.Format.SpaceBefore = 6
            $p.Format.SpaceAfter = 3
            continue
        }
    }

    foreach ($toc in $doc.TablesOfContents) {
        $toc.Update()
    }
    $doc.Fields.Update() | Out-Null
    $doc.Save()
    $doc.ExportAsFixedFormat($pdf, 17)
    Write-Output "Merged thesis sections, updated TOC, and exported PDF."
}
finally {
    if ($doc -ne $null) {
        try { $doc.Close($false) | Out-Null } catch {}
    }
    if ($word -ne $null) {
        try { $word.Quit() | Out-Null } catch {}
    }
}
