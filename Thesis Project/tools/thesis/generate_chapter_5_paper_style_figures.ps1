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
    $OutputDir = Join-Path $ProjectRoot "docs\thesis_drafting\figures_chapter_5_paper_style"
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

function ColorFromHex([string]$Hex) {
    return [System.Drawing.ColorTranslator]::FromHtml($Hex)
}

function New-Canvas([int]$Width, [int]$Height) {
    $bmp = New-Object System.Drawing.Bitmap($Width, $Height)
    $bmp.SetResolution(300, 300)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
    $g.Clear([System.Drawing.Color]::White)
    return @{ Bitmap = $bmp; Graphics = $g; Width = $Width; Height = $Height }
}

function New-Font([float]$Size, [string]$Style = "Regular") {
    $fontStyle = [System.Drawing.FontStyle]::Regular
    if ($Style -eq "Bold") { $fontStyle = [System.Drawing.FontStyle]::Bold }
    return New-Object System.Drawing.Font("Microsoft YaHei", $Size, $fontStyle, [System.Drawing.GraphicsUnit]::Pixel)
}

function Add-Text($Canvas, [string]$Text, [float]$X, [float]$Y, [float]$W, [float]$H, [float]$Size = 26, [string]$Color = "#182B3A", [string]$Align = "Center", [string]$Style = "Regular") {
    $fmt = New-Object System.Drawing.StringFormat
    $fmt.Alignment = [System.Drawing.StringAlignment]::$Align
    $fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
    $fmt.Trimming = [System.Drawing.StringTrimming]::EllipsisCharacter
    $font = New-Font $Size $Style
    $brush = New-Object System.Drawing.SolidBrush (ColorFromHex $Color)
    $Canvas.Graphics.DrawString($Text, $font, $brush, (New-Object System.Drawing.RectangleF($X, $Y, $W, $H)), $fmt)
    $font.Dispose()
    $brush.Dispose()
    $fmt.Dispose()
}

function Add-Axes($Canvas, [float]$X, [float]$Y, [float]$W, [float]$H, [float]$MaxValue, [string]$Unit) {
    $pen = New-Object System.Drawing.Pen((ColorFromHex "#A9C1D0"), 2)
    $axisPen = New-Object System.Drawing.Pen((ColorFromHex "#2E5D7A"), 3)
    for ($i = 0; $i -le 4; $i++) {
        $yy = $Y + $H - ($H * $i / 4)
        $Canvas.Graphics.DrawLine($pen, $X, $yy, $X + $W, $yy)
        $label = [Math]::Round($MaxValue * $i / 4, 0).ToString() + $Unit
        Add-Text $Canvas $label ($X - 90) ($yy - 18) 80 36 18 "#5A6B77" "Far"
    }
    $Canvas.Graphics.DrawLine($axisPen, $X, $Y + $H, $X + $W, $Y + $H)
    $Canvas.Graphics.DrawLine($axisPen, $X, $Y, $X, $Y + $H)
    $pen.Dispose()
    $axisPen.Dispose()
}

function Add-Bar($Canvas, [float]$X, [float]$BaseY, [float]$W, [float]$H, [string]$Fill, [string]$Label, [string]$Value) {
    $brush = New-Object System.Drawing.SolidBrush (ColorFromHex $Fill)
    $pen = New-Object System.Drawing.Pen((ColorFromHex "#2E5D7A"), 2)
    $Canvas.Graphics.FillRectangle($brush, $X, $BaseY - $H, $W, $H)
    $Canvas.Graphics.DrawRectangle($pen, $X, $BaseY - $H, $W, $H)
    Add-Text $Canvas $Value ($X - 20) ($BaseY - $H - 42) ($W + 40) 34 20 "#182B3A" "Center" "Bold"
    Add-Text $Canvas $Label ($X - 45) ($BaseY + 12) ($W + 90) 70 20 "#182B3A" "Center"
    $brush.Dispose()
    $pen.Dispose()
}

function Save-Figure($Canvas, [string]$Name) {
    $path = Join-Path $OutputDir $Name
    $Canvas.Bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $Canvas.Graphics.Dispose()
    $Canvas.Bitmap.Dispose()
}

# Figure 5.1: first-round live comparison
$c = New-Canvas 1800 980
Add-Text $c "实时 RTSP 首轮配置对比" 0 38 1800 70 40 "#102A43" "Center" "Bold"
$chartX = 170; $chartY = 170; $chartW = 650; $chartH = 560
Add-Text $c "输出帧率 stream_fps" $chartX 105 $chartW 50 27 "#2E5D7A" "Center" "Bold"
Add-Axes $c $chartX $chartY $chartW $chartH 15 "fps"
$baseY = $chartY + $chartH
$vals = @(6.56, 13.98, 10.55)
$labels = @("Baseline", "Multi-Context", "Policy")
$colors = @("#DCEBF4", "#9EC9E2", "#FCE8B2")
for ($i = 0; $i -lt 3; $i++) {
    $h = $chartH * $vals[$i] / 15
    Add-Bar $c ($chartX + 105 + $i * 175) $baseY 95 $h $colors[$i] $labels[$i] ([string]$vals[$i])
}
$chartX2 = 1030; $chartY2 = 170; $chartW2 = 600; $chartH2 = 560
Add-Text $c "端到端延迟" $chartX2 105 $chartW2 50 27 "#2E5D7A" "Center" "Bold"
Add-Axes $c $chartX2 $chartY2 $chartW2 $chartH2 450 "ms"
$baseY2 = $chartY2 + $chartH2
$lat = @(240.59, 307.30, 104.91)
for ($i = 0; $i -lt 3; $i++) {
    $h = $chartH2 * $lat[$i] / 450
    Add-Bar $c ($chartX2 + 70 + $i * 170) $baseY2 95 $h $colors[$i] $labels[$i] ([string]$lat[$i])
}
Add-Text $c "多 context 提升吞吐，策略优化降低延迟，二者对应不同优化目标。" 160 850 1480 48 27 "#182B3A" "Center"
Save-Figure $c "fig_5_1_live_config_comparison.png"

# Figure 5.2: policy sweep
$c = New-Canvas 1800 980
Add-Text $c "检测间隔策略性能变化" 0 38 1800 70 40 "#102A43" "Center" "Bold"
$chartX = 180; $chartY = 175; $chartW = 620; $chartH = 540
Add-Text $c "NPU 推理频率" $chartX 112 $chartW 48 27 "#2E5D7A" "Center" "Bold"
Add-Axes $c $chartX $chartY $chartW $chartH 8 "fps"
$vals = @(6.68, 4.96, 3.82)
$labels = @("N=2", "N=3", "N=4")
for ($i = 0; $i -lt 3; $i++) {
    Add-Bar $c ($chartX + 105 + $i * 170) ($chartY + $chartH) 95 ($chartH * $vals[$i] / 8) "#DCEBF4" $labels[$i] ([string]$vals[$i])
}
$chartX2 = 1020; $chartY2 = 175; $chartW2 = 620; $chartH2 = 540
Add-Text $c "端到端延迟" $chartX2 112 $chartW2 48 27 "#2E5D7A" "Center" "Bold"
Add-Axes $c $chartX2 $chartY2 $chartW2 $chartH2 200 "ms"
$lat = @(175.20, 107.18, 95.54)
for ($i = 0; $i -lt 3; $i++) {
    $color = "#DCEBF4"
    if ($i -eq 1) { $color = "#FCE8B2" }
    Add-Bar $c ($chartX2 + 105 + $i * 170) ($chartY2 + $chartH2) 95 ($chartH2 * $lat[$i] / 200) $color $labels[$i] ([string]$lat[$i])
}
Add-Text $c "N=3 在帧率、NPU 刷新强度和延迟之间取得较均衡表现。" 160 850 1480 48 27 "#182B3A" "Center"
Save-Figure $c "fig_5_2_detect_interval_sweep.png"

# Figure 5.3: stage profiling
$c = New-Canvas 1800 980
Add-Text $c "RKNN 推理阶段耗时分布" 0 38 1800 70 40 "#102A43" "Center" "Bold"
$chartX = 250; $chartY = 180; $chartW = 1280; $chartH = 560
Add-Axes $c $chartX $chartY $chartW $chartH 70 "ms"
$names = @("Prepare", "inputs_set", "rknn_run", "outputs_get", "Decode+NMS", "Render")
$vals = @(1.42, 63.78, 45.72, 0.54, 2.48, 0.13)
$fills = @("#DCEBF4", "#F6C26B", "#9EC9E2", "#DCEBF4", "#DCEBF4", "#DCEBF4")
for ($i = 0; $i -lt $names.Count; $i++) {
    $x = $chartX + 70 + $i * 195
    $h = $chartH * $vals[$i] / 70
    Add-Bar $c $x ($chartY + $chartH) 105 $h $fills[$i] $names[$i] ([string]$vals[$i])
}
Add-Text $c "rknn_inputs_set 和 rknn_run 是主要瓶颈，后处理与绘制耗时较小。" 170 850 1460 48 27 "#182B3A" "Center"
Save-Figure $c "fig_5_3_stage_profile_bottleneck.png"

Write-Output $OutputDir
