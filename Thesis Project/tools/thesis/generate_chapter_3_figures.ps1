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
    $OutputDir = Join-Path $ProjectRoot "docs\thesis_drafting\figures"
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

function New-Color([string]$Hex) {
    if ($Hex -notmatch '^#[0-9A-Fa-f]{6}$') {
        $Hex = "#526A7A"
    }
    return [System.Drawing.ColorTranslator]::FromHtml($Hex)
}

function New-Canvas {
    param([int]$Width = 1600, [int]$Height = 620)
    $bmp = New-Object System.Drawing.Bitmap($Width, $Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
    $g.Clear([System.Drawing.Color]::White)
    return @{ Bitmap = $bmp; Graphics = $g }
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

function Draw-Box {
    param(
        [System.Drawing.Graphics]$G,
        [float]$X,
        [float]$Y,
        [float]$W,
        [float]$H,
        [string]$Title,
        [string]$Sub = "",
        [string]$Fill = "#EAF3F8",
        [string]$Stroke = "#4D6D82"
    )
    $path = New-RoundRectPath -X $X -Y $Y -W $W -H $H -R 18
    $brush = New-Object System.Drawing.SolidBrush (New-Color $Fill)
    $pen = New-Object System.Drawing.Pen (New-Color $Stroke), 2.2
    $G.FillPath($brush, $path)
    $G.DrawPath($pen, $path)
    $brush.Dispose()
    $pen.Dispose()
    $path.Dispose()

    $titleFont = New-Object System.Drawing.Font("Microsoft YaHei", 20, [System.Drawing.FontStyle]::Bold)
    $subFont = New-Object System.Drawing.Font("Microsoft YaHei", 14, [System.Drawing.FontStyle]::Regular)
    $dark = New-Object System.Drawing.SolidBrush (New-Color "#1F2A33")
    $muted = New-Object System.Drawing.SolidBrush (New-Color "#4D5963")
    $fmt = New-Object System.Drawing.StringFormat
    $fmt.Alignment = [System.Drawing.StringAlignment]::Center
    $fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
    $titleRect = [System.Drawing.RectangleF]::new([float]($X + 10), [float]($Y + 18), [float]($W - 20), [float]32)
    $G.DrawString($Title, $titleFont, $dark, $titleRect, $fmt)
    if (-not [string]::IsNullOrWhiteSpace($Sub)) {
        $subRect = [System.Drawing.RectangleF]::new([float]($X + 14), [float]($Y + 58), [float]($W - 28), [float]($H - 66))
        $G.DrawString($Sub, $subFont, $muted, $subRect, $fmt)
    }
    $fmt.Dispose()
    $dark.Dispose()
    $muted.Dispose()
    $titleFont.Dispose()
    $subFont.Dispose()
}

function Draw-Arrow {
    param(
        [System.Drawing.Graphics]$G,
        [float]$X1,
        [float]$Y1,
        [float]$X2,
        [float]$Y2,
        [string]$Color = "#526A7A"
    )
    $pen = New-Object System.Drawing.Pen (New-Color $Color), 3
    $cap = New-Object System.Drawing.Drawing2D.AdjustableArrowCap(7, 9, $true)
    $pen.CustomEndCap = $cap
    $G.DrawLine($pen, $X1, $Y1, $X2, $Y2)
    $cap.Dispose()
    $pen.Dispose()
}

function Draw-Line {
    param(
        [System.Drawing.Graphics]$G,
        [float]$X1,
        [float]$Y1,
        [float]$X2,
        [float]$Y2,
        [string]$Color = "#526A7A"
    )
    $pen = New-Object System.Drawing.Pen (New-Color $Color), 3
    $G.DrawLine($pen, $X1, $Y1, $X2, $Y2)
    $pen.Dispose()
}

function Draw-Label {
    param(
        [System.Drawing.Graphics]$G,
        [float]$X,
        [float]$Y,
        [float]$W,
        [float]$H,
        [string]$Text
    )
    $font = New-Object System.Drawing.Font("Microsoft YaHei", 15, [System.Drawing.FontStyle]::Regular)
    $brush = New-Object System.Drawing.SolidBrush (New-Color "#54616B")
    $fmt = New-Object System.Drawing.StringFormat
    $fmt.Alignment = [System.Drawing.StringAlignment]::Center
    $fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
    $rect = [System.Drawing.RectangleF]::new([float]$X, [float]$Y, [float]$W, [float]$H)
    $G.DrawString($Text, $font, $brush, $rect, $fmt)
    $fmt.Dispose()
    $brush.Dispose()
    $font.Dispose()
}

function Save-Figure {
    param($Canvas, [string]$Name)
    $path = Join-Path $OutputDir $Name
    $Canvas.Bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $Canvas.Graphics.Dispose()
    $Canvas.Bitmap.Dispose()
    Write-Output $path
}

function Draw-Figure31 {
    $c = New-Canvas -Height 470
    $g = $c.Graphics
    $y = 105; $w = 190; $h = 112; $gap = 55
    $xs = @(
        [float]55,
        [float](55 + ($w+$gap)),
        [float](55 + (2 * ($w+$gap))),
        [float](55 + (3 * ($w+$gap))),
        [float](55 + (4 * ($w+$gap))),
        [float](55 + (5 * ($w+$gap)))
    )
    Draw-Box $g $($xs[0]) $y $w $h "视频输入" "本地视频`nUSB 摄像头`nRTSP"
    Draw-Box $g $($xs[1]) $y $w $h "预处理" "缩放`n颜色转换`n张量组织"
    Draw-Box $g $($xs[2]) $y $w $h "NPU 推理" "RKNN Runtime`n单/多 context"
    Draw-Box $g $($xs[3]) $y $w $h "后处理" "输出解析`n阈值筛选`nNMS"
    Draw-Box $g $($xs[4]) $y $w $h "策略控制" "detect_every_n`nROI/跟踪`n平滑"
    Draw-Box $g $($xs[5]) $y $w $h "结果输出" "带框视频`nRTSP`nCSV/JSONL"
    for ($i=0; $i -lt 5; $i++) {
        Draw-Arrow $g ($xs[$i] + $w + 6) ($y + $h/2) ($xs[($i+1)] - 8) ($y + $h/2)
    }
    Draw-Arrow $g ($xs[4] + $w/2) ($y + $h + 8) ($xs[4] + $w/2) 250
    Draw-Box $g 970 255 330 110 "性能记录" "FPS / 延迟`n阶段耗时 / 资源占用" "#F5F7F9" "#6F7D86"
    Save-Figure $c "fig_3_1_system_architecture.png"
}

function Draw-Figure32 {
    $c = New-Canvas -Height 420
    $g = $c.Graphics
    $y = 125; $w = 190; $h = 110; $gap = 52
    $xs = @(
        [float]70,
        [float](70 + ($w+$gap)),
        [float](70 + (2 * ($w+$gap))),
        [float](70 + (3 * ($w+$gap))),
        [float](70 + (4 * ($w+$gap))),
        [float](70 + (5 * ($w+$gap)))
    )
    Draw-Box $g $($xs[0]) $y $w $h "训练权重" "best.pt" "#EEF6E8" "#688456"
    Draw-Box $g $($xs[1]) $y $w $h "ONNX 导出" "opset / simplify" "#EEF6E8" "#688456"
    Draw-Box $g $($xs[2]) $y $w $h "RKNN 转换" "target=rk3588" "#EAF3F8" "#4D6D82"
    Draw-Box $g $($xs[3]) $y $w $h "板端加载" "RKNN Runtime" "#EAF3F8" "#4D6D82"
    Draw-Box $g $($xs[4]) $y $w $h "输出校验" "shape / 类别数`n坐标格式" "#F8F3E6" "#8A7650"
    Draw-Box $g $($xs[5]) $y $w $h "检测后处理" "阈值筛选`nNMS / 绘框" "#F8F3E6" "#8A7650"
    for ($i=0; $i -lt 5; $i++) {
        Draw-Arrow $g ($xs[$i] + $w + 7) ($y + $h/2) ($xs[($i+1)] - 8) ($y + $h/2)
    }
    Draw-Label $g 555 260 485 45 "关键检查：模型输出需与板端后处理逻辑一致"
    Save-Figure $c "fig_3_2_model_migration.png"
}

function Draw-Figure33 {
    $c = New-Canvas -Height 470
    $g = $c.Graphics
    $y = 120; $w = 215; $h = 112; $gap = 65
    $xs = @(
        [float]80,
        [float](80 + ($w+$gap)),
        [float](80 + (2 * ($w+$gap))),
        [float](80 + (3 * ($w+$gap))),
        [float](80 + (4 * ($w+$gap)))
    )
    Draw-Box $g $($xs[0]) $y $w $h "采集线程" "Capture`n读取视频帧" "#EEF6E8" "#688456"
    Draw-Box $g $($xs[1]) $y $w $h "输入队列" "Bounded Queue`n保留较新帧" "#F5F7F9" "#6F7D86"
    Draw-Box $g $($xs[2]) $y $w $h "推理线程" "Preprocess`nRKNN + Decode" "#EAF3F8" "#4D6D82"
    Draw-Box $g $($xs[3]) $y $w $h "结果缓存" "Latest Result`n时间戳对齐" "#F8F3E6" "#8A7650"
    Draw-Box $g $($xs[4]) $y $w $h "输出线程" "Draw + Encode`nRTSP / Video" "#EEF6E8" "#688456"
    for ($i=0; $i -lt 4; $i++) {
        Draw-Arrow $g ($xs[$i] + $w + 8) ($y + $h/2) ($xs[($i+1)] - 10) ($y + $h/2)
    }
    Draw-Line $g ($xs[2] + $w/2) ($y + $h + 5) ($xs[2] + $w/2) 330
    Draw-Arrow $g ($xs[2] + $w/2) 330 850 330
    Draw-Box $g 860 278 300 105 "实验日志" "CSV / JSONL`nProfile" "#F5F7F9" "#6F7D86"
    Draw-Label $g 365 260 310 55 "队列满时优先丢弃旧帧，降低实时累计延迟"
    Save-Figure $c "fig_3_3_realtime_pipeline.png"
}

function Draw-Figure34 {
    $c = New-Canvas -Height 560
    $g = $c.Graphics
    Draw-Box $g 80 225 210 110 "输入帧队列" "Frame Queue" "#F5F7F9" "#6F7D86"
    Draw-Box $g 375 225 210 110 "调度器" "分配空闲 worker" "#F8F3E6" "#8A7650"
    Draw-Arrow $g 300 280 365 280

    Draw-Box $g 700 70 285 98 "Worker 1" "RKNN Context 1" "#EAF3F8" "#4D6D82"
    Draw-Box $g 700 225 285 98 "Worker 2" "RKNN Context 2" "#EAF3F8" "#4D6D82"
    Draw-Box $g 700 380 285 98 "Worker 3" "RKNN Context 3" "#EAF3F8" "#4D6D82"

    Draw-Line $g 585 280 640 280
    Draw-Line $g 640 119 640 429
    Draw-Arrow $g 640 119 690 119
    Draw-Arrow $g 640 280 690 280
    Draw-Arrow $g 640 429 690 429

    Draw-Box $g 1120 225 215 110 "结果合并" "按帧号/时间戳`n选择最新结果" "#F8F3E6" "#8A7650"
    Draw-Box $g 1410 225 140 110 "输出" "绘框`n推流" "#EEF6E8" "#688456"
    Draw-Arrow $g 995 119 1110 255
    Draw-Arrow $g 995 280 1110 280
    Draw-Arrow $g 995 429 1110 305
    Draw-Arrow $g 1345 280 1400 280
    Draw-Label $g 690 500 450 45 "多 context 用于提高每帧检测条件下的 NPU 吞吐"
    Save-Figure $c "fig_3_4_multi_context.png"
}

Draw-Figure31
Draw-Figure32
Draw-Figure33
Draw-Figure34
