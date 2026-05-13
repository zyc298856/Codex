$ErrorActionPreference = "Stop"

$docx = "C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project\paper\full_thesis_latest_merged.docx"
$pdf = "C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project\paper\full_thesis_latest_merged.pdf"

$major = [ordered]@{
    "2.1 目标检测算法基础" = "2.1 目标检测与无人机小目标检测基础"
    "2.3 RK3588嵌入式平台" = "2.2 RK3588平台与RKNN模型部署"
    "2.5 C++17 多线程与实时视频流水线" = "2.3 C++17 多线程与实时视频流水线"
    "5.1 实验环境、评价指标与模型训练结果" = "5.1 实验环境、评价指标与模型基础验证"
    "5.2 公开视频固定输入与实时RTSP验证" = "5.2 固定视频、实时RTSP与调度策略实验"
    "5.4 阶段耗时、Zero-copy与硬件路径分析" = "5.3 硬件路径分析、推荐配置与稳定性测试"
}

$demote = @(
    "2.2 无人机小目标检测特点",
    "2.4 RKNN模型部署流程",
    "2.6 本章小结",
    "5.3 检测间隔、框平滑与多context实验",
    "5.5 推荐配置与长时间稳定性测试"
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
    Write-Output "Merged Chapter 2 and Chapter 5 sections, updated TOC, and exported PDF."
}
finally {
    if ($doc -ne $null) {
        try { $doc.Close($false) | Out-Null } catch {}
    }
    if ($word -ne $null) {
        try { $word.Quit() | Out-Null } catch {}
    }
}
