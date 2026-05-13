$ErrorActionPreference = "Stop"

$docx = "C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project\paper\full_thesis_latest_merged.docx"
$pdf = "C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project\paper\full_thesis_latest_merged.pdf"

function Join-Chars([int[]]$codes) {
    $chars = foreach ($code in $codes) { [char]$code }
    return -join $chars
}

function From-Utf8Base64([string]$value) {
    return [System.Text.Encoding]::UTF8.GetString([System.Convert]::FromBase64String($value))
}

# Avoid non-ASCII source text so Windows PowerShell 5.1 cannot corrupt Chinese literals.
$cTable = [char]0x8868
$cGave = Join-Chars @(0x7ed9, 0x51fa, 0x4e86)   # gei chu le
$cAsFollows = Join-Chars @(0x5982, 0x4e0b, 0x3002) # ru xia .
$cStop = [char]0x3002
$cFullWidthStop = [char]0xff0e

$word = $null
$doc = $null

try {
    $word = New-Object -ComObject Word.Application
    $word.Visible = $false
    $word.DisplayAlerts = 0
    $doc = $word.Documents.Open($docx)

    # Convert duplicate-looking guide sentences such as:
    # "Table 4.2 gives ..." -> "... as follows."
    foreach ($p in $doc.Paragraphs) {
        $raw = $p.Range.Text
        $txt = $raw.Trim([char]13, [char]7, " ", "`t")
        $pattern = "^" + [regex]::Escape([string]$cTable) + "\s*\d+\.\d+\s*" + [regex]::Escape($cGave) + "\s*(.+?)\s*[" + [regex]::Escape([string]$cStop + [string]$cFullWidthStop + ".") + "]?\s*$"

        if ($txt -match $pattern) {
            $body = $Matches[1].Trim()
            if ($body.EndsWith([string]$cStop) -or $body.EndsWith([string]$cFullWidthStop) -or $body.EndsWith(".")) {
                $body = $body.Substring(0, $body.Length - 1).Trim()
            }

            $range = $p.Range
            if ($range.End -gt $range.Start) {
                $range.End = $range.End - 1
            }
            $range.Text = $body + $cAsFollows
            $p.Range.ListFormat.RemoveNumbers()
        }
    }

    # One table guide sentence is embedded at the end of a longer paragraph.
    $inlineReplacements = @(
        @{
            Find = From-Utf8Base64 "6KGoNS4357uZ5Ye65LqG6K+l6L2u5raI6J6N5a6e6aqM57uT5p6c44CC"
            Replace = From-Utf8Base64 "6K+l6L2u5raI6J6N5a6e6aqM57uT5p6c5aaC5LiL44CC"
        }
    )
    foreach ($item in $inlineReplacements) {
        $find = $doc.Content.Find
        $find.ClearFormatting()
        $find.Replacement.ClearFormatting()
        $find.Text = $item.Find
        $find.Replacement.Text = $item.Replace
        $find.Forward = $true
        $find.Wrap = 1
        $find.Format = $false
        $find.MatchCase = $false
        $find.MatchWholeWord = $false
        $find.MatchWildcards = $false
        [void]$find.Execute($item.Find, $false, $false, $false, $false, $false, $true, 1, $false, $item.Replace, 2)
    }

    # Remove accidental list bullets from formal table captions and table contents.
    foreach ($p in $doc.Paragraphs) {
        $txt = $p.Range.Text.Trim([char]13, [char]7, " ", "`t")
        $captionPattern = "^" + [regex]::Escape([string]$cTable) + "\s*\d+\.\d+\s+"
        if ($txt -match $captionPattern) {
            $p.Range.ListFormat.RemoveNumbers()
        }
    }

    foreach ($tbl in $doc.Tables) {
        foreach ($p in $tbl.Range.Paragraphs) {
            $p.Range.ListFormat.RemoveNumbers()
        }
    }

    foreach ($toc in $doc.TablesOfContents) {
        $toc.Update()
    }
    $doc.Fields.Update() | Out-Null
    $doc.Save()
    $doc.ExportAsFixedFormat($pdf, 17)
    Write-Output "Fixed table guide sentences and exported PDF."
}
finally {
    if ($doc -ne $null) {
        try { $doc.Close($false) | Out-Null } catch {}
    }
    if ($word -ne $null) {
        try { $word.Quit() | Out-Null } catch {}
    }
}
