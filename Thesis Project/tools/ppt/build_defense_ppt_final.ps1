$ErrorActionPreference = "Stop"

$ScriptDir = if ($PSScriptRoot) { $PSScriptRoot } elseif ($global:__DefensePptScriptDir) { $global:__DefensePptScriptDir } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$ProjectRoot = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
$OutDir = Join-Path $ProjectRoot "paper\defense_ppt"
$PreviewDir = Join-Path $OutDir "previews_final"
$PptxPath = Join-Path $OutDir "rk3588_uav_defense_final.pptx"
$PdfPath = Join-Path $OutDir "rk3588_uav_defense_final.pdf"
$NotesPath = Join-Path $OutDir "rk3588_uav_defense_speaker_notes.md"
$InspectPath = Join-Path $OutDir "pptx_media_inspection.txt"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
if (Test-Path $PreviewDir) { Remove-Item -LiteralPath $PreviewDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $PreviewDir | Out-Null

Add-Type -AssemblyName System.Drawing

function RGBi([int]$r, [int]$g, [int]$b) {
    return $r + (256 * $g) + (65536 * $b)
}

$C = @{
    Navy = RGBi 18 44 71
    Blue = RGBi 22 107 178
    LightBlue = RGBi 221 238 248
    Orange = RGBi 225 133 33
    LightOrange = RGBi 255 238 214
    Green = RGBi 31 140 91
    Gray = RGBi 92 104 115
    LightGray = RGBi 244 247 250
    Dark = RGBi 28 35 43
    White = RGBi 255 255 255
    Red = RGBi 194 35 35
}

$Assets = @{
    Alarm = Join-Path $ProjectRoot "eval_runs\alarm_validation\fig1_alarm_overlay_frame1.jpg"
    Pexels = Join-Path $ProjectRoot "eval_runs\hard_ft_compare_20260512\hard_ft_conf024_pexels\pexels-demo__pexels_18253602_drone_flying_18s_720p\max_score_detection.jpg"
    Dut = Join-Path $ProjectRoot "eval_runs\public_videos_dut_screen_conf035\dut-anti-uav-tracking__video01\max_score_detection.jpg"
    Train = Join-Path $ProjectRoot "training_runs\drone_gpu_50e\results.png"
    ValPred = Join-Path $ProjectRoot "training_runs\drone_gpu_50e\val_batch0_pred.jpg"
}

function Set-Fill($shape, [int]$color) {
    $shape.Fill.Visible = -1
    $shape.Fill.ForeColor.RGB = $color
}

function Set-Line($shape, [int]$color, [double]$weight = 1.2) {
    $shape.Line.Visible = -1
    $shape.Line.ForeColor.RGB = $color
    $shape.Line.Weight = $weight
}

function Add-TextBox($slide, [string]$text, [double]$x, [double]$y, [double]$w, [double]$h, [double]$size, [int]$color, [bool]$bold = $false, [int]$align = 1) {
    $shape = $slide.Shapes.AddTextbox(1, $x, $y, $w, $h)
    $shape.TextFrame.MarginLeft = 0
    $shape.TextFrame.MarginRight = 0
    $shape.TextFrame.MarginTop = 0
    $shape.TextFrame.MarginBottom = 0
    $shape.TextFrame.WordWrap = -1
    $tr = $shape.TextFrame.TextRange
    $tr.Text = $text
    $tr.Font.Name = "Microsoft YaHei"
    $tr.Font.NameFarEast = "Microsoft YaHei"
    $tr.Font.Size = $size
    $tr.Font.Bold = if ($bold) { -1 } else { 0 }
    $tr.Font.Color.RGB = $color
    $tr.ParagraphFormat.Alignment = $align
    return $shape
}

function Add-Title($slide, [string]$title, [string]$eyebrow = "") {
    Add-TextBox $slide $title 48 30 720 42 24 $C.Navy $true 1 | Out-Null
    if ($eyebrow.Length -gt 0) {
        Add-TextBox $slide $eyebrow 50 72 650 20 10 $C.Gray $false 1 | Out-Null
    }
    $line = $slide.Shapes.AddLine(48, 95, 912, 95)
    Set-Line $line $C.LightBlue 2.0
}

function Add-Footer($slide, [int]$index) {
    Add-TextBox $slide ("朱奕澄 | 基于嵌入式平台的目标检测系统研究 | " + $index.ToString("00")) 48 510 540 15 8 $C.Gray $false 1 | Out-Null
}

function Add-RoundBox($slide, [string]$text, [double]$x, [double]$y, [double]$w, [double]$h, [int]$fill, [int]$line, [int]$txt, [double]$size = 12, [bool]$bold = $false) {
    $box = $slide.Shapes.AddShape(5, $x, $y, $w, $h)
    Set-Fill $box $fill
    Set-Line $box $line 1.2
    $box.TextFrame.MarginLeft = 8
    $box.TextFrame.MarginRight = 8
    $box.TextFrame.MarginTop = 5
    $box.TextFrame.MarginBottom = 5
    $tr = $box.TextFrame.TextRange
    $tr.Text = $text
    $tr.Font.Name = "Microsoft YaHei"
    $tr.Font.NameFarEast = "Microsoft YaHei"
    $tr.Font.Size = $size
    $tr.Font.Bold = if ($bold) { -1 } else { 0 }
    $tr.Font.Color.RGB = $txt
    $tr.ParagraphFormat.Alignment = 2
    return $box
}

function Add-Arrow($slide, [double]$x1, [double]$y1, [double]$x2, [double]$y2, [int]$color = $C.Blue) {
    $ln = $slide.Shapes.AddLine($x1, $y1, $x2, $y2)
    Set-Line $ln $color 2.0
    $ln.Line.EndArrowheadStyle = 3
    return $ln
}

function Add-BulletList($slide, [string[]]$items, [double]$x, [double]$y, [double]$w, [double]$h, [int]$color = $C.Dark, [double]$size = 14) {
    $text = ($items | ForEach-Object { "• " + $_ }) -join [Environment]::NewLine
    $shape = Add-TextBox $slide $text $x $y $w $h $size $color $false 1
    $shape.TextFrame.TextRange.ParagraphFormat.SpaceAfter = 6
    return $shape
}

function Get-ImageSize($path) {
    $img = [System.Drawing.Image]::FromFile($path)
    try { return @{ W = [double]$img.Width; H = [double]$img.Height } }
    finally { $img.Dispose() }
}

function Add-PictureFit($slide, [string]$path, [double]$x, [double]$y, [double]$w, [double]$h, [string]$caption = "") {
    if (-not (Test-Path -LiteralPath $path)) {
        $ph = $slide.Shapes.AddShape(1, $x, $y, $w, $h)
        Set-Fill $ph $C.LightGray
        Set-Line $ph $C.Gray 1
        Add-TextBox $slide "素材缺失" ($x + 12) ($y + ($h / 2) - 10) ($w - 24) 24 12 $C.Gray $false 2 | Out-Null
        return $ph
    }
    $sz = Get-ImageSize $path
    $scale = [Math]::Min($w / $sz.W, $h / $sz.H)
    $nw = $sz.W * $scale
    $nh = $sz.H * $scale
    $nx = $x + ($w - $nw) / 2
    $ny = $y + ($h - $nh) / 2
    $pic = $slide.Shapes.AddPicture($path, 0, -1, $nx, $ny, $nw, $nh)
    if ($caption.Length -gt 0) {
        Add-TextBox $slide $caption $x ($y + $h + 6) $w 16 9 $C.Gray $false 2 | Out-Null
    }
    return $pic
}

function Add-Notes($slide, [string]$notes) {
    try {
        $slide.NotesPage.Shapes.Placeholders(2).TextFrame.TextRange.Text = $notes
    } catch {
        # Speaker notes are optional; the markdown file below is the source of truth.
    }
}

function Add-TableLike($slide, [object[]]$rows, [double]$x, [double]$y, [double]$w, [double]$rowH) {
    $cols = @(0.36, 0.31, 0.33)
    $headers = @("问题", "策略", "答辩口径")
    $cx = $x
    for ($i = 0; $i -lt 3; $i++) {
        Add-TextBox $slide $headers[$i] $cx $y ($w * $cols[$i]) 20 11 $C.Navy $true 2 | Out-Null
        $cx += $w * $cols[$i]
    }
    $top = $slide.Shapes.AddLine($x, $y + 24, $x + $w, $y + 24)
    Set-Line $top $C.Navy 1.6
    for ($r = 0; $r -lt $rows.Count; $r++) {
        $cy = $y + 30 + ($r * $rowH)
        $row = $rows[$r]
        $cx = $x
        for ($i = 0; $i -lt 3; $i++) {
            Add-TextBox $slide $row[$i] $cx $cy ($w * $cols[$i] - 8) ($rowH - 4) 10 $C.Dark $false 1 | Out-Null
            $cx += $w * $cols[$i]
        }
        $ln = $slide.Shapes.AddLine($x, $cy + $rowH - 4, $x + $w, $cy + $rowH - 4)
        Set-Line $ln (RGBi 215 224 232) 0.8
    }
}

$ppLayoutBlank = 12
$ppt = New-Object -ComObject PowerPoint.Application
$ppt.Visible = -1
$pres = $ppt.Presentations.Add()
$pres.PageSetup.SlideWidth = 960
$pres.PageSetup.SlideHeight = 540

$slides = @()

# Slide 1
$s = $pres.Slides.Add(1, $ppLayoutBlank)
$s.Background.Fill.ForeColor.RGB = $C.White
$bar = $s.Shapes.AddShape(1, 0, 0, 260, 540)
Set-Fill $bar $C.Navy
$accent = $s.Shapes.AddShape(1, 260, 0, 18, 540)
Set-Fill $accent $C.Orange
Add-TextBox $s "RK3588" 52 62 160 34 26 $C.White $true 1 | Out-Null
Add-TextBox $s "NPU · RTSP · RGA · INT8" 54 105 170 22 11 (RGBi 204 226 241) $false 1 | Out-Null
Add-TextBox $s "基于嵌入式平台的`n目标检测系统研究" 318 118 560 110 34 $C.Navy $true 1 | Out-Null
Add-TextBox $s "无人机目标检测算法在 RK3588 开发板上的迁移、实时显示与系统级优化" 322 242 520 38 15 $C.Gray $false 1 | Out-Null
Add-TextBox $s "朱奕澄  |  通信与信息工程学院  |  指导教师：滕国伟" 322 420 510 24 12 $C.Dark $false 1 | Out-Null
$slides += $s
Add-Notes $s "开场约15秒。说明题目不是重新发明检测算法，而是把无人机检测模型真正迁移到 RK3588，完成摄像头/视频输入、NPU 推理、带框输出和实验记录。"

# Slide 2
$s = $pres.Slides.Add(2, $ppLayoutBlank)
Add-Title $s "课题目标：从模型检测到板端实时系统" "答辩重点：任务书要求与本文完成内容一一对应"
Add-TextBox $s "任务书关注的不是单帧检测结果，而是完整链路。" 62 120 560 28 18 $C.Navy $true 1 | Out-Null
Add-BulletList $s @(
    "视频采集：本地视频、USB 摄像头、RTSP 输出",
    "板端推理：ONNX / RKNN 转换，RKNN Runtime 调用",
    "工程实现：C++17 多线程流水线，队列解耦采集、推理和输出",
    "硬件探索：RGA 预处理、INT8 / hybrid INT8、zero-copy 路径",
    "报警闭环：画面 Overlay + 事件日志，预留外设接口"
) 70 170 500 210 $C.Dark 14 | Out-Null
Add-RoundBox $s "本文定位" 645 128 180 34 $C.LightBlue $C.Blue $C.Navy 15 $true | Out-Null
Add-TextBox $s "不声称提出新的检测网络，而是完成模型迁移、板端适配、实时视频链路和性能实验。" 624 178 235 104 18 $C.Dark $false 1 | Out-Null
Add-RoundBox $s "答辩时一句话" 645 338 180 34 $C.LightOrange $C.Orange $C.Navy 15 $true | Out-Null
Add-TextBox $s "我的核心工作是把算法落到 RK3588 上，并解释为什么这样配置最稳。" 624 388 235 70 18 $C.Dark $false 1 | Out-Null
Add-Footer $s 2
$slides += $s
Add-Notes $s "这一页约35秒。强调对任务书的覆盖：C++、视频采集、RGA、NPU、后处理、软件报警。这里不要讲太细，后面用实验和演示证明。"

# Slide 3
$s = $pres.Slides.Add(3, $ppLayoutBlank)
Add-Title $s "系统架构：固定视频验证 + 实时 RTSP 演示" "两条路径分别解决“可复现”和“可观看”"
Add-TextBox $s "固定视频评测路径" 104 125 250 22 16 $C.Blue $true 2 | Out-Null
Add-RoundBox $s "公开视频 / 本地视频" 66 170 110 52 $C.LightBlue $C.Blue $C.Dark 11 | Out-Null
Add-RoundBox $s "rk_yolo_video" 218 170 118 52 $C.LightBlue $C.Blue $C.Dark 11 $true | Out-Null
Add-RoundBox $s "带框视频`nCSV / ROI JSONL" 380 160 130 72 $C.LightBlue $C.Blue $C.Dark 11 | Out-Null
Add-RoundBox $s "实验对比表" 560 170 120 52 $C.LightBlue $C.Blue $C.Dark 11 | Out-Null
Add-Arrow $s 176 196 218 196 $C.Blue | Out-Null
Add-Arrow $s 336 196 380 196 $C.Blue | Out-Null
Add-Arrow $s 510 196 560 196 $C.Blue | Out-Null
Add-TextBox $s "用于控制输入源，适合做多方案对比和论文实验。" 710 166 160 70 12 $C.Gray $false 1 | Out-Null

Add-TextBox $s "实时演示路径" 104 305 250 22 16 $C.Orange $true 2 | Out-Null
Add-RoundBox $s "USB 摄像头" 66 350 100 52 $C.LightOrange $C.Orange $C.Dark 11 | Out-Null
Add-RoundBox $s "rk_yolo_live_rtsp" 205 350 132 52 $C.LightOrange $C.Orange $C.Dark 11 $true | Out-Null
Add-RoundBox $s "NPU 检测`n+ 轻量跟踪" 382 340 132 72 $C.LightOrange $C.Orange $C.Dark 11 | Out-Null
Add-RoundBox $s "GStreamer`nRTSP" 560 350 112 52 $C.LightOrange $C.Orange $C.Dark 11 | Out-Null
Add-RoundBox $s "电脑端观看" 724 350 100 52 $C.LightOrange $C.Orange $C.Dark 11 | Out-Null
Add-Arrow $s 166 376 205 376 $C.Orange | Out-Null
Add-Arrow $s 337 376 382 376 $C.Orange | Out-Null
Add-Arrow $s 514 376 560 376 $C.Orange | Out-Null
Add-Arrow $s 672 376 724 376 $C.Orange | Out-Null
Add-Footer $s 3
$slides += $s
Add-Notes $s "这一页约45秒。固定视频是实验入口，实时 RTSP 是演示入口。固定视频更严谨，实时链路更接近部署。"

# Slide 4
$s = $pres.Slides.Add(4, $ppLayoutBlank)
Add-Title $s "模型迁移：PyTorch → ONNX → RKNN" "关键不是转换命令，而是输出张量与后处理保持一致"
Add-RoundBox $s "best.pt" 92 148 110 50 $C.LightGray $C.Gray $C.Dark 14 $true | Out-Null
Add-RoundBox $s "best.onnx" 250 148 122 50 $C.LightGray $C.Gray $C.Dark 14 $true | Out-Null
Add-RoundBox $s "best.rk3588.fp.rknn" 420 148 190 50 $C.LightBlue $C.Blue $C.Navy 13 $true | Out-Null
Add-RoundBox $s "RK3588 NPU 推理" 660 148 165 50 $C.LightOrange $C.Orange $C.Navy 13 $true | Out-Null
Add-Arrow $s 202 173 250 173 $C.Blue | Out-Null
Add-Arrow $s 372 173 420 173 $C.Blue | Out-Null
Add-Arrow $s 610 173 660 173 $C.Orange | Out-Null
Add-TextBox $s "迁移中遇到的核心问题" 72 260 290 26 18 $C.Navy $true 1 | Out-Null
Add-BulletList $s @(
    "end2end 模式与 RKNN 后处理兼容性不稳定",
    "改用 end2end=False，输出保持 1×5×8400",
    "在 C++ 端完成置信度筛选、坐标还原和 NMS",
    "单类别 drone 后处理避免 COCO 多类别映射错误"
) 82 305 420 130 $C.Dark 13 | Out-Null
Add-TextBox $s "答辩可说" 600 260 170 26 18 $C.Orange $true 1 | Out-Null
Add-TextBox $s "模型能转换并不等于能稳定推理。本文把输出格式、后处理逻辑和板端实际结果逐项对齐，最终形成 FP RKNN 稳定基线方案。" 600 305 245 118 16 $C.Dark $false 1 | Out-Null
Add-Footer $s 4
$slides += $s
Add-Notes $s "这一页约40秒。突出 end2end=False 和 1×5×8400。老师如果问为什么不直接端到端，回答：RKNN 端兼容性和可控性更重要。"

# Slide 5
$s = $pres.Slides.Add(5, $ppLayoutBlank)
Add-Title $s "实时优化：不只追求 NPU FPS" "实时演示看的是端到端体验：低延迟、不卡顿、框稳定"
$rows = @(
    @("视频延迟累积", "有界队列，优先保留新帧", "宁可跳过旧帧，也不要显示滞后画面"),
    @("NPU 调用过密", "detect_every_n + 结果复用", "降低推理压力，换取更稳定的实时显示"),
    @("检测框抖动", "box_smooth + 运动预测", "减少相邻帧检测框突变，演示观感更稳"),
    @("逐帧吞吐不足", "多 context 并行", "验证 NPU 并行能力，但并非默认最优")
)
Add-TableLike $s $rows 74 132 812 54
Add-RoundBox $s "最终演示推荐" 92 414 180 38 $C.LightBlue $C.Blue $C.Navy 15 $true | Out-Null
Add-TextBox $s "FP RKNN + 当前阈值配置 + box_smooth + 按需关闭动态 ROI。若现场移动较快，可降低置信度阈值并启用更积极的摄像头控制策略。" 302 414 520 48 15 $C.Dark $false 1 | Out-Null
Add-Footer $s 5
$slides += $s
Add-Notes $s "这一页约50秒。解释为什么多 context 不是永远越多越好：输入拷贝、调度和总线争用会影响端到端延迟。"

# Slide 6
$s = $pres.Slides.Add(6, $ppLayoutBlank)
Add-Title $s "硬件优化探索：RGA、INT8 与 zero-copy" "结论不是“没做成”，而是完成验证后选择最稳方案"
Add-RoundBox $s "RGA 预处理" 82 138 170 44 $C.LightBlue $C.Blue $C.Navy 15 $true | Out-Null
Add-TextBox $s "已实现 BGR→RGB、resize、letterbox 的 RGA 可选路径，并可通过强制开关验证不回退 OpenCV。" 82 198 215 78 13 $C.Dark $false 1 | Out-Null
Add-RoundBox $s "INT8 量化" 380 138 170 44 $C.LightOrange $C.Orange $C.Navy 15 $true | Out-Null
Add-TextBox $s "full INT8 在小目标场景下出现置信度塌缩；hybrid INT8 可恢复检测，但速度优势尚未稳定超过 FP。" 380 198 220 78 13 $C.Dark $false 1 | Out-Null
Add-RoundBox $s "zero-copy 方向" 680 138 170 44 $C.LightGray $C.Gray $C.Navy 15 $true | Out-Null
Add-TextBox $s "已分析输入拷贝瓶颈。后续理想路径是 MPP 解码 → DMA/物理连续内存 → RGA → RKNN input memory。" 680 198 210 88 13 $C.Dark $false 1 | Out-Null
Add-TextBox $s "工程判断" 86 355 130 26 18 $C.Navy $true 1 | Out-Null
Add-TextBox $s "答辩时可以明确：硬件优化方向没有错，但真实收益取决于数据搬运链路是否打通。本文把 RGA 和 INT8 做成可验证路径，同时保留 FP 稳定方案用于最终演示。" 210 352 620 64 17 $C.Dark $false 1 | Out-Null
Add-Footer $s 6
$slides += $s
Add-Notes $s "这一页约50秒。重点是诚实表达：RGA、INT8 都做过，也验证过，但默认方案选择 FP 是为了稳定性。"

# Slide 7
$s = $pres.Slides.Add(7, $ppLayoutBlank)
Add-Title $s "实验结果：公开输入、板端运行、长期稳定" "用可重复的视频和日志支撑最终配置"
Add-PictureFit $s $Assets.Alarm 58 120 270 160 "软件报警 Overlay：UAV ALERT + 检测框" | Out-Null
Add-PictureFit $s $Assets.Pexels 350 120 250 160 "公开无人机视频：近景 / 放大场景" | Out-Null
Add-PictureFit $s $Assets.ValPred 622 120 260 160 "训练验证预测样例" | Out-Null
Add-RoundBox $s "mAP50 = 0.8901" 88 330 180 42 $C.LightBlue $C.Blue $C.Navy 16 $true | Out-Null
Add-RoundBox $s "Recall = 0.8426" 308 330 180 42 $C.LightBlue $C.Blue $C.Navy 16 $true | Out-Null
Add-RoundBox $s "24,700 次 NPU 推理" 528 330 210 42 $C.LightOrange $C.Orange $C.Navy 16 $true | Out-Null
Add-TextBox $s "1 小时独立推理测试：0 错误、0 崩溃，RSS 约 105.6–107.2 MB，无明显内存泄漏。" 104 405 720 34 15 $C.Dark $false 2 | Out-Null
Add-Footer $s 7
$slides += $s
Add-Notes $s "这一页约55秒。先讲可视化结果，再讲指标。强调公开视频和长期稳定性比只展示一张图更有说服力。"

# Slide 8
$s = $pres.Slides.Add(8, $ppLayoutBlank)
$bg8 = $s.Shapes.AddShape(1, 0, 0, 960, 540)
Set-Fill $bg8 $C.Navy
$bg8.Line.Visible = 0
Add-TextBox $s "总结与现场演示" 70 64 520 52 32 $C.White $true 1 | Out-Null
Add-TextBox $s "本文完成了从模型迁移、板端推理、实时视频输出到性能实验的端到端验证。" 72 128 700 28 17 (RGBi 215 230 242) $false 1 | Out-Null
Add-TextBox $s "三个答辩记忆点" 78 204 220 24 18 $C.White $true 1 | Out-Null
Add-BulletList $s @(
    "模型迁移：解决 RKNN 输出格式与 C++ 后处理适配",
    "实时系统：固定视频可复现，RTSP 可观看，日志可记录",
    "工程取舍：FP 稳定基线用于演示，RGA/INT8 作为验证过的优化路径"
) 88 250 650 120 (RGBi 238 246 252) 16 | Out-Null
Add-RoundBox $s "演示顺序：摄像头/视频输入 → 带框画面 → 报警提示 → 解释自动变焦/对焦策略" 116 420 725 50 $C.LightOrange $C.Orange $C.Navy 15 $true | Out-Null
Add-TextBox $s "谢谢老师，请批评指正" 685 62 210 24 14 (RGBi 204 226 241) $false 3 | Out-Null
$slides += $s
Add-Notes $s "最后20秒收束。马上切到演示。不要讲太多细节，留下问答空间。"

$notes = @"
# 答辩讲稿建议（PPT 约 5 分钟）

## 第1页 题目（约15秒）
各位老师好，我的题目是“基于嵌入式平台的目标检测系统研究”。这项工作的重点不是重新提出一个检测网络，而是把已经训练好的无人机检测模型迁移到 RK3588 开发板上，并完成实时视频检测和系统级优化。

## 第2页 课题目标（约35秒）
任务书要求的是一个完整链路：视频采集、硬件预处理、NPU 推理、后处理和报警输出。本文对应实现了两个 C++ 程序：固定视频评测程序和实时 RTSP 程序，并补充了 RGA、INT8、zero-copy 等硬件优化实验。最终演示采用稳定性最好的 FP RKNN 配置。

## 第3页 系统架构（约45秒）
系统分两条路径。固定视频路径用于论文实验，因为输入可重复，适合比较不同配置。实时 RTSP 路径用于实际演示，摄像头输入后在板端推理，再通过 GStreamer 推流到电脑端观看。

## 第4页 模型迁移（约40秒）
模型迁移不是简单转换格式。早期 end2end 导出在 RKNN 端的输出和后处理逻辑不匹配，因此我改为 end2end=False，让输出保持为 1×5×8400，并在 C++ 中完成置信度筛选、坐标还原和 NMS。

## 第5页 实时优化（约50秒）
实时场景不只看 NPU FPS。若每帧都完整检测，可能造成延迟累积和画面卡顿。本文采用有界队列、检测间隔、框平滑和轻量跟踪，在“检测刷新率”和“显示稳定性”之间取得平衡。

## 第6页 硬件优化（约50秒）
RGA、INT8 和 zero-copy 都做了验证。RGA 可完成硬件 resize 和颜色转换，但端到端收益受内存搬运影响。full INT8 会导致小目标置信度塌缩，hybrid INT8 能恢复检测但速度优势不稳定。因此最终演示采用 FP 稳定基线方案。

## 第7页 实验结果（约55秒）
模型在验证集上 mAP50 为 0.8901，Recall 为 0.8426；公开视频和板端测试中能够产生有效检测框。稳定性方面，1 小时独立推理完成 24,700 次 NPU 推理，0 错误、0 崩溃，内存没有明显增长。

## 第8页 总结（约20秒）
本文完成了从模型迁移到板端实时检测的端到端系统。接下来我会进行现场演示，展示摄像头或视频输入、带框输出和报警提示。
"@
Set-Content -LiteralPath $NotesPath -Value $notes -Encoding UTF8

$pres.SaveAs($PptxPath)
$pres.SaveAs($PdfPath, 32)
$pres.Export($PreviewDir, "PNG", 1600, 900)
$pres.Close()
$ppt.Quit()

[System.Runtime.InteropServices.Marshal]::ReleaseComObject($pres) | Out-Null
[System.Runtime.InteropServices.Marshal]::ReleaseComObject($ppt) | Out-Null
[GC]::Collect()
[GC]::WaitForPendingFinalizers()

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($PptxPath)
try {
    $media = @($zip.Entries | Where-Object { $_.FullName -like "ppt/media/*" } | Select-Object -ExpandProperty FullName)
    $rels = @($zip.Entries | Where-Object { $_.FullName -like "ppt/slides/_rels/*.rels" })
    $external = @()
    foreach ($rel in $rels) {
        $sr = New-Object System.IO.StreamReader($rel.Open())
        try {
            $txt = $sr.ReadToEnd()
            if ($txt -match 'TargetMode="External"') { $external += $rel.FullName }
        } finally {
            $sr.Dispose()
        }
    }
    $report = @()
    $report += "PPTX: $PptxPath"
    $report += "Embedded media count: $($media.Count)"
    $report += "External slide relationship count: $($external.Count)"
    if ($external.Count -gt 0) {
        $report += "External relationships:"
        $report += $external
    } else {
        $report += "No external image links found in slide relationships."
    }
    $report += "Preview PNG count: $((Get-ChildItem -LiteralPath $PreviewDir -Filter *.PNG -File).Count)"
    Set-Content -LiteralPath $InspectPath -Value ($report -join [Environment]::NewLine) -Encoding UTF8
} finally {
    $zip.Dispose()
}

Write-Host "PPTX=$PptxPath"
Write-Host "PDF=$PdfPath"
Write-Host "NOTES=$NotesPath"
Write-Host "PREVIEW_DIR=$PreviewDir"
Write-Host "INSPECT=$InspectPath"
