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
    $OutputDir = Join-Path $ProjectRoot "docs\thesis_drafting\figures_chapter_4_paper_style"
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

function Add-Text($Canvas, [string]$Text, [float]$X, [float]$Y, [float]$W, [float]$H, [float]$Size = 30, [string]$Color = "#182B3A", [string]$Align = "Center", [string]$Style = "Regular") {
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

function Add-Box($Canvas, [string]$Text, [float]$X, [float]$Y, [float]$W, [float]$H, [string]$Fill = "#F8FBFD", [string]$Stroke = "#2E5D7A", [float]$Size = 28, [string]$Style = "Regular") {
    $rect = New-Object System.Drawing.RectangleF($X, $Y, $W, $H)
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $r = 22
    $path.AddArc($X, $Y, $r, $r, 180, 90)
    $path.AddArc($X + $W - $r, $Y, $r, $r, 270, 90)
    $path.AddArc($X + $W - $r, $Y + $H - $r, $r, $r, 0, 90)
    $path.AddArc($X, $Y + $H - $r, $r, $r, 90, 90)
    $path.CloseFigure()
    $brush = New-Object System.Drawing.SolidBrush (ColorFromHex $Fill)
    $pen = New-Object System.Drawing.Pen((ColorFromHex $Stroke), 3)
    $Canvas.Graphics.FillPath($brush, $path)
    $Canvas.Graphics.DrawPath($pen, $path)
    Add-Text $Canvas $Text ($X + 18) ($Y + 8) ($W - 36) ($H - 16) $Size "#182B3A" "Center" $Style
    $brush.Dispose()
    $pen.Dispose()
    $path.Dispose()
}

function Add-Lane($Canvas, [string]$Title, [float]$X, [float]$Y, [float]$W, [float]$H, [string]$Fill = "#F4F8FB") {
    $brush = New-Object System.Drawing.SolidBrush (ColorFromHex $Fill)
    $pen = New-Object System.Drawing.Pen((ColorFromHex "#A9C1D0"), 2)
    $Canvas.Graphics.FillRectangle($brush, $X, $Y, $W, $H)
    $Canvas.Graphics.DrawRectangle($pen, $X, $Y, $W, $H)
    Add-Text $Canvas $Title ($X + 10) ($Y + 8) ($W - 20) 48 25 "#2E5D7A" "Center" "Bold"
    $brush.Dispose()
    $pen.Dispose()
}

function Add-Arrow($Canvas, [float]$X1, [float]$Y1, [float]$X2, [float]$Y2, [string]$Color = "#2E5D7A") {
    $pen = New-Object System.Drawing.Pen((ColorFromHex $Color), 4)
    $cap = New-Object System.Drawing.Drawing2D.AdjustableArrowCap(8, 10)
    $pen.CustomEndCap = $cap
    $Canvas.Graphics.DrawLine($pen, $X1, $Y1, $X2, $Y2)
    $cap.Dispose()
    $pen.Dispose()
}

function Save-Figure($Canvas, [string]$Name) {
    $path = Join-Path $OutputDir $Name
    $Canvas.Bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $Canvas.Graphics.Dispose()
    $Canvas.Bitmap.Dispose()
}

# Figure 4.1
$c = New-Canvas 1800 980
Add-Text $c "RKNN 推理模块实现流程" 0 34 1800 70 42 "#102A43" "Center" "Bold"
Add-Box $c "加载 RKNN 模型`nrknn_init" 90 190 245 125 "#EAF4FA"
Add-Box $c "查询输入/输出`nrknn_query" 415 190 260 125 "#F8FBFD"
Add-Box $c "帧预处理`nResize / RGB / Letterbox" 755 190 310 125 "#F8FBFD"
Add-Box $c "设置输入`nrknn_inputs_set" 1145 190 260 125 "#F8FBFD"
Add-Box $c "NPU 推理`nrknn_run" 1485 190 245 125 "#EAF4FA"
Add-Arrow $c 335 252 415 252
Add-Arrow $c 675 252 755 252
Add-Arrow $c 1065 252 1145 252
Add-Arrow $c 1405 252 1485 252
Add-Box $c "获取输出`nrknn_outputs_get" 1485 500 245 125 "#F8FBFD"
Add-Box $c "输出解码`n坐标还原 / 置信度筛选" 1095 500 310 125 "#F8FBFD"
Add-Box $c "后处理`nNMS / 类别过滤" 725 500 290 125 "#F8FBFD"
Add-Box $c "结果绘制与记录`nBox / CSV / JSONL" 330 500 315 125 "#EAF4FA"
Add-Arrow $c 1608 315 1608 500
Add-Arrow $c 1485 562 1405 562
Add-Arrow $c 1095 562 1015 562
Add-Arrow $c 725 562 645 562
Save-Figure $c "fig_4_1_rknn_inference_flow.png"

# Figure 4.2
$c = New-Canvas 1800 980
Add-Text $c "固定视频与实时 RTSP 两条运行路径" 0 34 1800 70 42 "#102A43" "Center" "Bold"
Add-Lane $c "固定视频评测路径：可重复、可对比、可记录" 80 155 1640 310 "#F4F8FB"
Add-Box $c "公开视频/本地视频" 145 260 250 100 "#FFFFFF"
Add-Box $c "rk_yolo_video" 495 260 250 100 "#EAF4FA" "#2E5D7A" 30 "Bold"
Add-Box $c "带框视频" 845 235 220 90 "#FFFFFF"
Add-Box $c "逐帧 CSV" 845 350 220 90 "#FFFFFF"
Add-Box $c "ROI JSONL" 1135 292 220 90 "#FFFFFF"
Add-Box $c "实验对比表" 1430 292 220 90 "#EAF4FA"
Add-Arrow $c 395 310 495 310
Add-Arrow $c 745 310 845 280
Add-Arrow $c 745 310 845 395
Add-Arrow $c 1065 322 1135 332
Add-Arrow $c 1355 337 1430 337
Add-Lane $c "实时演示路径：摄像头输入、低延迟显示、远程观看" 80 535 1640 310 "#FBF7EF"
Add-Box $c "USB 摄像头" 145 642 230 100 "#FFFFFF" "#B7791F"
Add-Box $c "rk_yolo_live_rtsp" 455 642 300 100 "#FFF4DA" "#B7791F" 28 "Bold"
Add-Box $c "NPU 检测 + 轻量跟踪" 845 642 315 100 "#FFFFFF" "#B7791F"
Add-Box $c "GStreamer RTSP" 1248 642 270 100 "#FFFFFF" "#B7791F"
Add-Box $c "电脑端实时观看" 1538 642 160 100 "#FFF4DA" "#B7791F" 24 "Bold"
Add-Arrow $c 375 692 455 692 "#B7791F"
Add-Arrow $c 755 692 845 692 "#B7791F"
Add-Arrow $c 1160 692 1248 692 "#B7791F"
Add-Arrow $c 1518 692 1538 692 "#B7791F"
Save-Figure $c "fig_4_2_video_rtsp_paths.png"

# Figure 4.3
$c = New-Canvas 1800 980
Add-Text $c "跳帧检测、动态 ROI 与检测框平滑策略" 0 34 1800 70 42 "#102A43" "Center" "Bold"
Add-Box $c "当前帧 k" 90 190 180 95 "#EAF4FA"
Add-Box $c "判断 k mod N" 350 190 240 95 "#F8FBFD"
Add-Box $c "Infer(k)=1`n执行 NPU 检测" 690 140 290 110 "#EAF4FA"
Add-Box $c "Infer(k)=0`n复用/跟踪上一结果" 690 310 290 110 "#FFF8E7" "#B7791F"
Add-Box $c "动态 ROI`n围绕历史目标裁剪" 1090 140 300 110 "#F8FBFD"
Add-Box $c "全帧刷新`n防止 ROI 漂移" 1090 310 300 110 "#F8FBFD"
Add-Box $c "框平滑`nB_smooth(k)" 1500 225 230 120 "#EAF4FA"
Add-Arrow $c 270 238 350 238
Add-Arrow $c 590 238 690 195
Add-Arrow $c 590 238 690 365 "#B7791F"
Add-Arrow $c 980 195 1090 195
Add-Arrow $c 980 365 1090 365 "#B7791F"
Add-Arrow $c 1390 195 1500 285
Add-Arrow $c 1390 365 1500 285 "#B7791F"
Add-Box $c "稳定显示框`n抑制左右抖动" 760 610 280 110 "#EAF4FA"
Add-Arrow $c 1615 345 1040 610
Add-Text $c "核心思想：减少不必要的 NPU 调用，同时让显示框在跳帧场景下保持连续稳定。" 180 760 1440 70 30 "#182B3A" "Center"
Save-Figure $c "fig_4_3_roi_skip_smoothing.png"

# Figure 4.4
$c = New-Canvas 1800 980
Add-Text $c "多 context NPU 并行推理实现" 0 34 1800 70 42 "#102A43" "Center" "Bold"
Add-Box $c "采集/解码线程" 95 230 250 110 "#EAF4FA"
Add-Box $c "任务队列`nFrameTask" 450 230 240 110 "#F8FBFD"
Add-Box $c "Worker 1`nRKNN Context 1" 820 130 300 105 "#FFFFFF"
Add-Box $c "Worker 2`nRKNN Context 2" 820 285 300 105 "#FFFFFF"
Add-Box $c "Worker 3/4`n按需扩展" 820 440 300 105 "#FFFFFF"
Add-Box $c "结果队列`nDetectionResult" 1240 285 265 110 "#F8FBFD"
Add-Box $c "显示/推流线程" 1570 285 180 110 "#EAF4FA"
Add-Arrow $c 345 285 450 285
Add-Arrow $c 690 285 820 182
Add-Arrow $c 690 285 820 337
Add-Arrow $c 690 285 820 492
Add-Arrow $c 1120 182 1240 320
Add-Arrow $c 1120 337 1240 340
Add-Arrow $c 1120 492 1240 360
Add-Arrow $c 1505 340 1570 340
Add-Text $c "每个 worker 持有独立 RKNN context，避免多个线程共享同一推理上下文导致同步冲突。" 200 665 1400 60 28 "#182B3A" "Center"
Add-Text $c "实验发现：W=2 可显著提升每帧检测吞吐；W=3/4 受输入设置阶段瓶颈影响，收益有限。" 200 735 1400 60 28 "#182B3A" "Center"
Save-Figure $c "fig_4_4_multi_context_workers.png"

Write-Output $OutputDir
