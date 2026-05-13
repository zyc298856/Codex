param(
    [string]$OutputDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $ProjectRoot "docs\thesis_drafting\figures_paper_style"
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

function ColorFromHex([string]$Hex) {
    return [System.Drawing.ColorTranslator]::FromHtml($Hex)
}

function EscapeXml([string]$Text) {
    return [System.Security.SecurityElement]::Escape($Text)
}

function New-PaperCanvas {
    param([int]$Width, [int]$Height, [string]$Title)

    $bmp = New-Object System.Drawing.Bitmap($Width, $Height)
    $bmp.SetResolution(300, 300)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
    $g.Clear([System.Drawing.Color]::White)

    $svg = New-Object System.Collections.ArrayList
    [void]$svg.Add("<svg xmlns=""http://www.w3.org/2000/svg"" width=""$Width"" height=""$Height"" viewBox=""0 0 $Width $Height"">")
    [void]$svg.Add("<defs>")
    [void]$svg.Add("<marker id=""arrow"" markerWidth=""14"" markerHeight=""10"" refX=""13"" refY=""5"" orient=""auto"" markerUnits=""strokeWidth""><path d=""M 0 0 L 14 5 L 0 10 z"" fill=""#2E5D7A""/></marker>")
    [void]$svg.Add("<style>text{font-family:'Microsoft YaHei','SimSun',Arial,sans-serif;fill:#182B3A}.title{font-weight:700}.small{fill:#5A6B77}</style>")
    [void]$svg.Add("</defs>")
    [void]$svg.Add("<rect x=""0"" y=""0"" width=""$Width"" height=""$Height"" fill=""#FFFFFF""/>")

    return @{ Bitmap = $bmp; Graphics = $g; Svg = $svg; Width = $Width; Height = $Height; Title = $Title }
}

function Add-SvgLine {
    param($Canvas, [string]$Line)
    [void]$Canvas.Svg.Add($Line)
}

function Add-SvgText {
    param(
        $Canvas,
        [float]$X,
        [float]$Y,
        [float]$W,
        [float]$H,
        [string]$Text,
        [int]$Size = 28,
        [string]$Color = "#182B3A",
        [bool]$Bold = $false,
        [string]$Align = "center"
    )

    $lines = $Text -split "`n"
    $lineHeight = [Math]::Round($Size * 1.34)
    $total = $lineHeight * $lines.Count
    $firstY = $Y + (($H - $total) / 2) + $Size
    $anchor = "middle"
    $textX = $X + ($W / 2)
    if ($Align -eq "left") {
        $anchor = "start"
        $textX = $X + 18
    }
    $weight = if ($Bold) { "700" } else { "400" }
    $content = "<text x=""$textX"" y=""$firstY"" font-size=""$Size"" font-weight=""$weight"" fill=""$Color"" text-anchor=""$anchor"">"
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $dy = if ($i -eq 0) { 0 } else { $lineHeight }
        $safe = EscapeXml $lines[$i]
        $content += "<tspan x=""$textX"" dy=""$dy"">$safe</tspan>"
    }
    $content += "</text>"
    Add-SvgLine $Canvas $content
}

function Draw-Text {
    param(
        $Canvas,
        [float]$X,
        [float]$Y,
        [float]$W,
        [float]$H,
        [string]$Text,
        [int]$Size = 28,
        [string]$Color = "#182B3A",
        [bool]$Bold = $false,
        [string]$Align = "center"
    )

    $g = $Canvas.Graphics
    $fontStyle = if ($Bold) { [System.Drawing.FontStyle]::Bold } else { [System.Drawing.FontStyle]::Regular }
    $font = New-Object System.Drawing.Font("Microsoft YaHei", $Size, $fontStyle, [System.Drawing.GraphicsUnit]::Pixel)
    $brush = New-Object System.Drawing.SolidBrush (ColorFromHex $Color)
    $fmt = New-Object System.Drawing.StringFormat
    $fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
    if ($Align -eq "left") {
        $fmt.Alignment = [System.Drawing.StringAlignment]::Near
    } else {
        $fmt.Alignment = [System.Drawing.StringAlignment]::Center
    }
    $rect = [System.Drawing.RectangleF]::new($X, $Y, $W, $H)
    $g.DrawString($Text, $font, $brush, $rect, $fmt)

    Add-SvgText -Canvas $Canvas -X $X -Y $Y -W $W -H $H -Text $Text -Size $Size -Color $Color -Bold $Bold -Align $Align

    $fmt.Dispose()
    $brush.Dispose()
    $font.Dispose()
}

function New-RoundRectPath {
    param([float]$X, [float]$Y, [float]$W, [float]$H, [float]$R = 18)
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $R * 2
    $path.AddArc($X, $Y, $d, $d, 180, 90)
    $path.AddArc($X + $W - $d, $Y, $d, $d, 270, 90)
    $path.AddArc($X + $W - $d, $Y + $H - $d, $d, $d, 0, 90)
    $path.AddArc($X, $Y + $H - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    return $path
}

function Draw-RoundRect {
    param(
        $Canvas,
        [float]$X,
        [float]$Y,
        [float]$W,
        [float]$H,
        [string]$Fill = "#FFFFFF",
        [string]$Stroke = "#6B8EA5",
        [float]$StrokeWidth = 2,
        [float]$R = 20,
        [bool]$Dashed = $false
    )

    $path = New-RoundRectPath -X $X -Y $Y -W $W -H $H -R $R
    $brush = New-Object System.Drawing.SolidBrush (ColorFromHex $Fill)
    $pen = New-Object System.Drawing.Pen (ColorFromHex $Stroke), $StrokeWidth
    if ($Dashed) {
        $pen.DashStyle = [System.Drawing.Drawing2D.DashStyle]::Dash
    }
    $Canvas.Graphics.FillPath($brush, $path)
    $Canvas.Graphics.DrawPath($pen, $path)

    $dash = if ($Dashed) { " stroke-dasharray=""12 10""" } else { "" }
    Add-SvgLine $Canvas "<rect x=""$X"" y=""$Y"" width=""$W"" height=""$H"" rx=""$R"" ry=""$R"" fill=""$Fill"" stroke=""$Stroke"" stroke-width=""$StrokeWidth""$dash/>"

    $pen.Dispose()
    $brush.Dispose()
    $path.Dispose()
}

function Draw-Title {
    param($Canvas, [string]$Title, [string]$SubTitle)
    Draw-Text -Canvas $Canvas -X 60 -Y 34 -W ($Canvas.Width - 120) -H 52 -Text $Title -Size 42 -Color "#14344D" -Bold $true
    Draw-Text -Canvas $Canvas -X 60 -Y 92 -W ($Canvas.Width - 120) -H 36 -Text $SubTitle -Size 24 -Color "#5A6B77"
    $pen = New-Object System.Drawing.Pen (ColorFromHex "#D5E1E8"), 3
    $Canvas.Graphics.DrawLine($pen, 70, 146, $Canvas.Width - 70, 146)
    Add-SvgLine $Canvas "<line x1=""70"" y1=""146"" x2=""$($Canvas.Width - 70)"" y2=""146"" stroke=""#D5E1E8"" stroke-width=""3""/>"
    $pen.Dispose()
}

function Draw-Section {
    param($Canvas, [float]$X, [float]$Y, [float]$W, [float]$H, [string]$Title, [string]$Fill = "#F7FAFC")
    Draw-RoundRect -Canvas $Canvas -X $X -Y $Y -W $W -H $H -Fill $Fill -Stroke "#D5E1E8" -StrokeWidth 2 -R 18
    $barBrush = New-Object System.Drawing.SolidBrush (ColorFromHex "#E7F0F6")
    $barPath = New-RoundRectPath -X $X -Y $Y -W $W -H 58 -R 18
    $Canvas.Graphics.FillPath($barBrush, $barPath)
    Add-SvgLine $Canvas "<path d=""M $X $($Y + 58) L $($X + $W) $($Y + 58)"" stroke=""#D5E1E8"" stroke-width=""2""/>"
    Draw-Text -Canvas $Canvas -X ($X + 14) -Y ($Y + 8) -W ($W - 28) -H 42 -Text $Title -Size 31 -Color "#254B63" -Bold $true
    $barPath.Dispose()
    $barBrush.Dispose()
}

function Draw-Module {
    param(
        $Canvas,
        [float]$X,
        [float]$Y,
        [float]$W,
        [float]$H,
        [string]$Code,
        [string]$Title,
        [string]$Body,
        [string]$Fill = "#FFFFFF",
        [string]$Stroke = "#5E87A1"
    )

    Draw-RoundRect -Canvas $Canvas -X $X -Y $Y -W $W -H $H -Fill $Fill -Stroke $Stroke -StrokeWidth 2.3 -R 18
    $badgeR = 34
    $badgeBrush = New-Object System.Drawing.SolidBrush (ColorFromHex "#2E5D7A")
    $Canvas.Graphics.FillEllipse($badgeBrush, $X + 20, $Y + 20, $badgeR, $badgeR)
    Add-SvgLine $Canvas "<circle cx=""$($X + 37)"" cy=""$($Y + 37)"" r=""17"" fill=""#2E5D7A""/>"
    Draw-Text -Canvas $Canvas -X ($X + 20) -Y ($Y + 20) -W $badgeR -H $badgeR -Text $Code -Size 19 -Color "#FFFFFF" -Bold $true
    Draw-Text -Canvas $Canvas -X ($X + 62) -Y ($Y + 12) -W ($W - 82) -H 46 -Text $Title -Size 33 -Color "#14344D" -Bold $true -Align "left"
    Draw-Text -Canvas $Canvas -X ($X + 24) -Y ($Y + 60) -W ($W - 48) -H ($H - 72) -Text $Body -Size 28 -Color "#526675" -Align "left"
    $badgeBrush.Dispose()
}

function Draw-Arrow {
    param(
        $Canvas,
        [float]$X1,
        [float]$Y1,
        [float]$X2,
        [float]$Y2,
        [string]$Label = "",
        [bool]$Dashed = $false
    )

    $pen = New-Object System.Drawing.Pen (ColorFromHex "#2E5D7A"), 3
    $cap = New-Object System.Drawing.Drawing2D.AdjustableArrowCap(7, 8)
    $pen.CustomEndCap = $cap
    if ($Dashed) {
        $pen.DashStyle = [System.Drawing.Drawing2D.DashStyle]::Dash
    }
    $Canvas.Graphics.DrawLine($pen, $X1, $Y1, $X2, $Y2)
    $dash = if ($Dashed) { " stroke-dasharray=""14 10""" } else { "" }
    Add-SvgLine $Canvas "<line x1=""$X1"" y1=""$Y1"" x2=""$X2"" y2=""$Y2"" stroke=""#2E5D7A"" stroke-width=""3"" marker-end=""url(#arrow)""$dash/>"
    if (-not [string]::IsNullOrWhiteSpace($Label)) {
        $lx = [Math]::Min($X1, $X2) + [Math]::Abs($X2 - $X1) / 2 - 80
        $ly = [Math]::Min($Y1, $Y2) + [Math]::Abs($Y2 - $Y1) / 2 - 46
        Draw-RoundRect -Canvas $Canvas -X $lx -Y $ly -W 160 -H 38 -Fill "#FFFFFF" -Stroke "#D5E1E8" -StrokeWidth 1.2 -R 12
        Draw-Text -Canvas $Canvas -X $lx -Y $ly -W 160 -H 38 -Text $Label -Size 19 -Color "#2E5D7A"
    }
    $cap.Dispose()
    $pen.Dispose()
}

function Draw-Note {
    param($Canvas, [float]$X, [float]$Y, [float]$W, [float]$H, [string]$Text)
    Draw-RoundRect -Canvas $Canvas -X $X -Y $Y -W $W -H $H -Fill "#FFFDF5" -Stroke "#D8C98B" -StrokeWidth 2 -R 16
    Draw-Text -Canvas $Canvas -X ($X + 20) -Y ($Y + 14) -W ($W - 40) -H ($H - 28) -Text $Text -Size 25 -Color "#6B5A22" -Align "left"
}

function Save-PaperCanvas {
    param($Canvas, [string]$BaseName)
    $pngPath = Join-Path $OutputDir "$BaseName.png"
    $svgPath = Join-Path $OutputDir "$BaseName.svg"
    [void]$Canvas.Svg.Add("</svg>")
    $Canvas.Bitmap.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png)
    Set-Content -Path $svgPath -Value ($Canvas.Svg -join "`n") -Encoding UTF8
    $Canvas.Graphics.Dispose()
    $Canvas.Bitmap.Dispose()
}

function Draw-Figure31 {
    $c = New-PaperCanvas -Width 2100 -Height 1220 -Title "Figure 3.1"
    Draw-Title -Canvas $c -Title "RK3588 无人机目标检测系统总体架构" -SubTitle "输入采集、主控调度、NPU 推理与结果输出的分层设计"

    Draw-Section -Canvas $c -X 70 -Y 190 -W 455 -H 880 -Title "输入层"
    Draw-Section -Canvas $c -X 565 -Y 190 -W 455 -H 880 -Title "主控处理层"
    Draw-Section -Canvas $c -X 1060 -Y 190 -W 455 -H 880 -Title "NPU 推理层"
    Draw-Section -Canvas $c -X 1555 -Y 190 -W 475 -H 880 -Title "输出与记录层"

    Draw-Module -Canvas $c -X 110 -Y 285 -W 375 -H 150 -Code "1" -Title "USB 摄像头 / 固定视频" -Body "实时画面采集`n公开视频离线验证"
    Draw-Module -Canvas $c -X 110 -Y 500 -W 375 -H 150 -Code "2" -Title "RTSP / OpenCV 读取" -Body "统一视频输入接口`n保留可复现实验入口"
    Draw-Module -Canvas $c -X 110 -Y 715 -W 375 -H 150 -Code "3" -Title "帧缓存队列" -Body "限制积压与丢帧风险`n为后级调度提供最新帧"

    Draw-Module -Canvas $c -X 605 -Y 285 -W 375 -H 150 -Code "4" -Title "预处理与尺度变换" -Body "BGR/RGB 转换、Letterbox`n当前稳定路径为 OpenCV"
    Draw-Module -Canvas $c -X 605 -Y 500 -W 375 -H 150 -Code "5" -Title "调度策略" -Body "detect_every_n`n动态 ROI 与轻量跟踪"
    Draw-Module -Canvas $c -X 605 -Y 715 -W 375 -H 150 -Code "6" -Title "后处理" -Body "解码、阈值筛选`n类别过滤与框平滑"

    Draw-Module -Canvas $c -X 1100 -Y 360 -W 375 -H 160 -Code "7" -Title "RKNN Runtime" -Body "加载 FP RKNN 模型`n绑定 RK3588 NPU"
    Draw-Module -Canvas $c -X 1100 -Y 610 -W 375 -H 160 -Code "8" -Title "多 Context 推理" -Body "单 / 双 context 对比`n面向吞吐和延迟折中"

    Draw-Module -Canvas $c -X 1595 -Y 285 -W 395 -H 150 -Code "9" -Title "检测框叠加显示" -Body "类别、置信度与位置框`n支持实时观看"
    Draw-Module -Canvas $c -X 1595 -Y 500 -W 395 -H 150 -Code "10" -Title "RTSP 推流" -Body "板端输出检测后视频`nPC 端播放器查看"
    Draw-Module -Canvas $c -X 1595 -Y 715 -W 395 -H 150 -Code "11" -Title "实验记录" -Body "CSV、日志、截图`n用于论文实验分析"

    Draw-Arrow -Canvas $c -X1 485 -Y1 575 -X2 605 -Y2 575
    Draw-Arrow -Canvas $c -X1 980 -Y1 575 -X2 1100 -Y2 445
    Draw-Arrow -Canvas $c -X1 1475 -Y1 445 -X2 1595 -Y2 575
    Draw-Arrow -Canvas $c -X1 792 -Y1 650 -X2 792 -Y2 715 -Label "" 
    Draw-Arrow -Canvas $c -X1 1288 -Y1 520 -X2 1288 -Y2 610 -Label ""
    Draw-Arrow -Canvas $c -X1 1792 -Y1 650 -X2 1792 -Y2 715 -Label ""

    Draw-Note -Canvas $c -X 605 -Y 910 -W 870 -H 95 -Text "设计口径：FP RKNN 是当前稳定主路径；INT8 量化和 RGA 硬件预处理作为后续优化方向，不在本章中过度声称。"
    Save-PaperCanvas -Canvas $c -BaseName "fig_3_1_system_architecture_paper"
}

function Draw-Figure32 {
    $c = New-PaperCanvas -Width 2100 -Height 1050 -Title "Figure 3.2"
    Draw-Title -Canvas $c -Title "YOLOv10 模型迁移与板端验证流程" -SubTitle "从训练权重到 RK3588 可运行模型的工程化转换路径"

    $y = 300
    $w = 300
    $h = 185
    Draw-Module -Canvas $c -X 90 -Y $y -W $w -H $h -Code "A" -Title "训练权重" -Body "best.pt`n单类 drone 检测"
    Draw-Module -Canvas $c -X 485 -Y $y -W $w -H $h -Code "B" -Title "ONNX 导出" -Body "固定输入尺寸`n校验算子兼容性"
    Draw-Module -Canvas $c -X 880 -Y $y -W $w -H $h -Code "C" -Title "RKNN 转换" -Body "RK3588 平台`nFP 模型优先稳定"
    Draw-Module -Canvas $c -X 1275 -Y $y -W $w -H $h -Code "D" -Title "板端加载" -Body "RKNN Runtime`n模型初始化检查"
    Draw-Module -Canvas $c -X 1670 -Y $y -W $w -H $h -Code "E" -Title "视频验证" -Body "固定视频 / RTSP`n输出带框结果"

    Draw-Arrow -Canvas $c -X1 390 -Y1 392 -X2 485 -Y2 392
    Draw-Arrow -Canvas $c -X1 785 -Y1 392 -X2 880 -Y2 392
    Draw-Arrow -Canvas $c -X1 1180 -Y1 392 -X2 1275 -Y2 392
    Draw-Arrow -Canvas $c -X1 1575 -Y1 392 -X2 1670 -Y2 392

    Draw-Section -Canvas $c -X 180 -Y 605 -W 1740 -H 245 -Title "验证闭环"
    Draw-Module -Canvas $c -X 245 -Y 690 -W 320 -H 105 -Code "1" -Title "PC 离线测试" -Body "先确认模型输出和类别映射"
    Draw-Module -Canvas $c -X 650 -Y 690 -W 320 -H 105 -Code "2" -Title "板端固定视频" -Body "可重复比较 FPS / 延迟"
    Draw-Module -Canvas $c -X 1055 -Y 690 -W 320 -H 105 -Code "3" -Title "实时 RTSP" -Body "验证实际观看与演示效果"
    Draw-Module -Canvas $c -X 1460 -Y 690 -W 320 -H 105 -Code "4" -Title "问题回流" -Body "漏检、误检、抖动再优化"
    Draw-Arrow -Canvas $c -X1 565 -Y1 742 -X2 650 -Y2 742
    Draw-Arrow -Canvas $c -X1 970 -Y1 742 -X2 1055 -Y2 742
    Draw-Arrow -Canvas $c -X1 1375 -Y1 742 -X2 1460 -Y2 742

    Draw-Note -Canvas $c -X 600 -Y 890 -W 900 -H 82 -Text "本阶段采用 FP RKNN 稳定模型作为论文实验基线；量化版本仅作为扩展方向记录，避免把未完整闭环的结果写成最终结论。"
    Save-PaperCanvas -Canvas $c -BaseName "fig_3_2_model_migration_paper"
}

function Draw-Figure33 {
    $c = New-PaperCanvas -Width 2100 -Height 1180 -Title "Figure 3.3"
    Draw-Title -Canvas $c -Title "实时视频检测流水线与轻量跟踪反馈" -SubTitle "以稳定实时显示为目标的异步采集、跳帧检测与结果平滑机制"

    Draw-Section -Canvas $c -X 80 -Y 210 -W 1940 -H 345 -Title "主流水线"
    $xs = @(140, 455, 770, 1085, 1400, 1715)
    $titles = @("视频读取", "帧预处理", "检测调度", "NPU 推理", "后处理", "显示 / 推流")
    $bodies = @(
        "USB/RTSP/视频文件`n统一输入",
        "尺寸变换`n颜色空间转换",
        "detect_every_n`n优先处理最新帧",
        "RKNN Context`nNPU 计算",
        "阈值筛选`n类别过滤",
        "绘制框与标签`nRTSP 输出"
    )
    for ($i = 0; $i -lt 6; $i++) {
        Draw-Module -Canvas $c -X $xs[$i] -Y 310 -W 245 -H 150 -Code ([string]($i + 1)) -Title $titles[$i] -Body $bodies[$i]
        if ($i -lt 5) {
            Draw-Arrow -Canvas $c -X1 ($xs[$i] + 245) -Y1 385 -X2 $xs[$i + 1] -Y2 385
        }
    }

    Draw-Section -Canvas $c -X 80 -Y 620 -W 1940 -H 360 -Title "稳定性增强回路"
    Draw-Module -Canvas $c -X 190 -Y 730 -W 340 -H 145 -Code "A" -Title "轻量跟踪" -Body "基于历史框和运动趋势`n降低逐帧检测压力"
    Draw-Module -Canvas $c -X 640 -Y 730 -W 340 -H 145 -Code "B" -Title "动态 ROI" -Body "围绕目标区域优先处理`n兼顾小目标与实时性"
    Draw-Module -Canvas $c -X 1090 -Y 730 -W 340 -H 145 -Code "C" -Title "框平滑" -Body "box_smooth / IoU 关联`n减少左右抖动"
    Draw-Module -Canvas $c -X 1540 -Y 730 -W 340 -H 145 -Code "D" -Title "实验日志" -Body "FPS、延迟、丢帧`n形成可复现实验记录"
    Draw-Arrow -Canvas $c -X1 530 -Y1 802 -X2 640 -Y2 802
    Draw-Arrow -Canvas $c -X1 980 -Y1 802 -X2 1090 -Y2 802
    Draw-Arrow -Canvas $c -X1 1430 -Y1 802 -X2 1540 -Y2 802
    Draw-Arrow -Canvas $c -X1 810 -Y1 730 -X2 890 -Y2 460 -Label "ROI 反馈" -Dashed $true
    Draw-Arrow -Canvas $c -X1 1260 -Y1 730 -X2 1715 -Y2 460 -Label "显示稳定" -Dashed $true

    Draw-Note -Canvas $c -X 350 -Y 1030 -W 1400 -H 82 -Text "工程目标不是盲目追求每帧检测，而是在可观看、低抖动和可重复记录之间取得平衡。"
    Save-PaperCanvas -Canvas $c -BaseName "fig_3_3_realtime_pipeline_paper"
}

function Draw-Figure34 {
    $c = New-PaperCanvas -Width 2100 -Height 1260 -Title "Figure 3.4"
    Draw-Title -Canvas $c -Title "RK3588 NPU 多 Context 并行推理方案" -SubTitle "通过任务调度与多推理上下文比较吞吐、延迟和实时显示稳定性"

    Draw-Section -Canvas $c -X 85 -Y 210 -W 470 -H 720 -Title "输入与调度"
    Draw-Module -Canvas $c -X 140 -Y 325 -W 360 -H 150 -Code "1" -Title "最新帧队列" -Body "保留最新画面`n避免历史帧堆积"
    Draw-Module -Canvas $c -X 140 -Y 555 -W 360 -H 150 -Code "2" -Title "检测频率策略" -Body "N=1/2/3 对比`n决定推理触发间隔"
    Draw-Module -Canvas $c -X 140 -Y 785 -W 360 -H 105 -Code "3" -Title "任务分发" -Body "按 context 空闲状态派发"

    Draw-Section -Canvas $c -X 650 -Y 210 -W 780 -H 720 -Title "NPU 推理 Context 池"
    Draw-Module -Canvas $c -X 725 -Y 310 -W 630 -H 130 -Code "C0" -Title "Context 0" -Body "稳定基线：单 context，低资源占用"
    Draw-Module -Canvas $c -X 725 -Y 525 -W 630 -H 130 -Code "C1" -Title "Context 1" -Body "并行扩展：提升推理吞吐，观察端到端延迟"
    Draw-Module -Canvas $c -X 725 -Y 740 -W 630 -H 130 -Code "C2" -Title "Context 2" -Body "进一步扩展：验证是否出现边际收益下降"

    Draw-Section -Canvas $c -X 1525 -Y 210 -W 490 -H 720 -Title "结果融合与决策"
    Draw-Module -Canvas $c -X 1585 -Y 325 -W 370 -H 145 -Code "4" -Title "结果收集" -Body "按帧序号回收推理结果`n避免显示乱序"
    Draw-Module -Canvas $c -X 1585 -Y 555 -W 370 -H 145 -Code "5" -Title "平滑与跟踪" -Body "补偿跳帧间隔`n改善框抖动"
    Draw-Module -Canvas $c -X 1585 -Y 785 -W 370 -H 105 -Code "6" -Title "配置推荐" -Body "按 FPS、延迟、稳定性综合选择"

    Draw-Arrow -Canvas $c -X1 500 -Y1 615 -X2 725 -Y2 375
    Draw-Arrow -Canvas $c -X1 500 -Y1 615 -X2 725 -Y2 590
    Draw-Arrow -Canvas $c -X1 500 -Y1 615 -X2 725 -Y2 805
    Draw-Arrow -Canvas $c -X1 1355 -Y1 375 -X2 1585 -Y2 397
    Draw-Arrow -Canvas $c -X1 1355 -Y1 590 -X2 1585 -Y2 397
    Draw-Arrow -Canvas $c -X1 1355 -Y1 805 -X2 1585 -Y2 397
    Draw-Arrow -Canvas $c -X1 1770 -Y1 470 -X2 1770 -Y2 555
    Draw-Arrow -Canvas $c -X1 1770 -Y1 700 -X2 1770 -Y2 785

    Draw-Section -Canvas $c -X 250 -Y 990 -W 1600 -H 185 -Title "实验结论表达方式"
    Draw-Text -Canvas $c -X 320 -Y 1062 -W 1460 -H 92 -Text "固定视频用于严谨对比，实时 RTSP 用于演示验证。`n当前推荐口径：N=2 + 单 context + 框平滑；双 context 作为吞吐优化对照。" -Size 25 -Color "#314C5E" -Align "left"

    Save-PaperCanvas -Canvas $c -BaseName "fig_3_4_multi_context_paper"
}

Draw-Figure31
Draw-Figure32
Draw-Figure33
Draw-Figure34

Write-Host "Generated paper-style figures in: $OutputDir"
