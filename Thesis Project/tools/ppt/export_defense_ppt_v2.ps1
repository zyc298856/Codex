$ErrorActionPreference = "Stop"

$project = "C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project"
$pptx = Join-Path $project "paper\defense_ppt\defense_presentation_v2.pptx"
$pdf = Join-Path $project "paper\defense_ppt\defense_presentation_v2.pdf"
$pngDir = Join-Path $project "paper\defense_ppt\defense_presentation_v2_pages"

if (-not (Test-Path $pptx)) {
    throw "PPTX not found: $pptx"
}

if (Test-Path $pngDir) {
    Remove-Item -LiteralPath $pngDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $pngDir | Out-Null

$powerPoint = New-Object -ComObject PowerPoint.Application
$presentation = $null
try {
    $presentation = $powerPoint.Presentations.Open($pptx, $true, $false, $false)
    $presentation.SaveAs($pdf, 32)
    $presentation.SaveAs($pngDir, 18)
}
finally {
    if ($presentation -ne $null) {
        $presentation.Close()
    }
    $powerPoint.Quit()
}

Write-Host "exported $pdf"
Write-Host "exported $pngDir"
