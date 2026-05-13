$ErrorActionPreference = "Stop"

$docxPath = "C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project\paper\full_thesis_latest_merged.docx"
$pdfPath = "C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project\docs\thesis_drafting\qa_chapter1_heading_fixed.pdf"

$word = New-Object -ComObject Word.Application
$word.Visible = $false
$word.DisplayAlerts = 0

try {
    $doc = $word.Documents.Open($docxPath, $false, $false)

    $fixed = 0
    foreach ($paragraph in $doc.Paragraphs) {
        $text = $paragraph.Range.Text
        $text = $text -replace "`r", ""
        $text = $text -replace "`a", ""
        $text = $text.Trim()

        if ($text -eq "第一章 绪论") {
            $paragraph.Alignment = 1  # wdAlignParagraphCenter
            $paragraph.Range.Font.NameFarEast = "黑体"
            $paragraph.Range.Font.Name = "Times New Roman"
            $paragraph.Range.Font.Size = 18
            $paragraph.Range.Font.Bold = $true
            $fixed += 1
        }
    }

    if ($fixed -ne 1) {
        throw "Expected to fix exactly one body chapter heading, but fixed $fixed."
    }

    $doc.Save()
    $doc.ExportAsFixedFormat($pdfPath, 17)  # wdExportFormatPDF
    $doc.Close($false)
    Write-Output "Fixed chapter heading count: $fixed"
    Write-Output "Exported PDF: $pdfPath"
}
finally {
    $word.Quit()
    [System.Runtime.InteropServices.Marshal]::ReleaseComObject($word) | Out-Null
}
