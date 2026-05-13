$ErrorActionPreference = "Stop"

$docxPath = "C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project\paper\full_thesis_latest_merged.docx"
$pdfPath = "C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project\docs\thesis_drafting\qa_chapter1_heading_fixed.pdf"

$word = New-Object -ComObject Word.Application
$word.Visible = $false
$word.DisplayAlerts = 0

try {
    $doc = $word.Documents.Open($docxPath, $false, $false)
    $doc.ExportAsFixedFormat($pdfPath, 17)
    $doc.Close($false)
    Write-Output "Exported PDF: $pdfPath"
}
finally {
    $word.Quit()
    [System.Runtime.InteropServices.Marshal]::ReleaseComObject($word) | Out-Null
}
