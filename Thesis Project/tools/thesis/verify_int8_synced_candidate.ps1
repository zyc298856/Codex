$ErrorActionPreference = 'Stop'

$doc = 'C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project\paper\full_thesis_latest_merged_int8_synced.docx'
$pdf = 'C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project\docs\thesis_drafting\qa_int8_synced_candidate_check.pdf'

$word = New-Object -ComObject Word.Application
$word.Visible = $false
$word.DisplayAlerts = 0
try {
    $document = $word.Documents.Open($doc, $false, $true)
    $document.ExportAsFixedFormat($pdf, 17)
    $document.Close($false)
    Write-Output "exported=$pdf"
}
finally {
    $word.Quit()
}
