$ErrorActionPreference = "Stop"

$docx = "C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project\paper\full_thesis_latest_merged.docx"
$word = New-Object -ComObject Word.Application
$word.Visible = $false
$word.DisplayAlerts = 0
$doc = $word.Documents.Open($docx)

Write-Output ("InlineShapes={0}" -f $doc.InlineShapes.Count)
for ($i = 1; $i -le $doc.InlineShapes.Count; $i++) {
    $shape = $doc.InlineShapes.Item($i)
    $page = $shape.Range.Information(3)
    if ($page -ge 52 -and $page -le 84) {
        Write-Output ("shape#{0} page={1} w={2:n1} h={3:n1}" -f $i, $page, $shape.Width, $shape.Height)
    }
}

$doc.Close($false)
$word.Quit()
