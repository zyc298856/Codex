$ErrorActionPreference = "Stop"

$docxPath = "C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project\paper\full_thesis_latest_merged.docx"

$word = New-Object -ComObject Word.Application
$word.Visible = $false
$word.DisplayAlerts = 0

try {
    $doc = $word.Documents.Open($docxPath, $false, $true)
    $i = 0
    foreach ($paragraph in $doc.Paragraphs) {
        $i += 1
        $text = $paragraph.Range.Text
        if ($text -like "*第一章*" -or $text -like "*绪论*") {
            $codes = ($text.ToCharArray() | ForEach-Object { "U+{0:X4}" -f [int][char]$_ }) -join " "
            Write-Output "IDX=$i"
            Write-Output "TEXT=[$text]"
            Write-Output "CODES=$codes"
        }
    }
    $doc.Close($false)
}
finally {
    $word.Quit()
    [System.Runtime.InteropServices.Marshal]::ReleaseComObject($word) | Out-Null
}
