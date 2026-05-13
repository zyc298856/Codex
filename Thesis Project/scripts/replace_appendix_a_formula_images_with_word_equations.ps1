$ErrorActionPreference = "Stop"

$docx = "C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project\paper\full_thesis_latest_merged.docx"
$pdf = "C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project\paper\full_thesis_latest_merged.pdf"

function Build-EquationInRange {
    param(
        [Parameter(Mandatory=$true)] $Doc,
        [Parameter(Mandatory=$true)] $Range
    )
    $start = $Range.Start
    $end = $Range.End
    while ($end -gt $start) {
        $last = $Doc.Range($end - 1, $end).Text
        if ($last -eq "`r" -or $last -eq [string][char]7) {
            $end--
        } else {
            break
        }
    }
    if ($end -le $start) { return }
    $mathRange = $Doc.Range($start, $end)
    $null = $Doc.OMaths.Add($mathRange)
    $Doc.OMaths.Item($Doc.OMaths.Count).BuildUp()
}

function Replace-FormulaImage {
    param(
        [Parameter(Mandatory=$true)] $Doc,
        [Parameter(Mandatory=$true)] [int] $InlineShapeIndex,
        [Parameter(Mandatory=$true)] [string[]] $Lines,
        [Parameter(Mandatory=$true)] [string] $Label
    )

    $shape = $Doc.InlineShapes.Item($InlineShapeIndex)
    $paraRange = $shape.Range.Paragraphs.Item(1).Range
    $insertPos = $paraRange.Start
    $paraRange.Delete()

    $insertRange = $Doc.Range($insertPos, $insertPos)
    $table = $Doc.Tables.Add($insertRange, 1, 2)
    $table.Borders.Enable = 0
    $table.AllowAutoFit = $true
    $table.Columns.Item(1).SetWidth(385, 0)
    $table.Columns.Item(2).SetWidth(42, 0)

    $formulaRange = $table.Cell(1, 1).Range
    $formulaRange.End = $formulaRange.End - 1
    $formulaRange.Text = ($Lines -join "`r")
    $table.Cell(1, 1).Range.ParagraphFormat.Alignment = 1

    for ($i = 1; $i -le $table.Cell(1, 1).Range.Paragraphs.Count; $i++) {
        $pRange = $table.Cell(1, 1).Range.Paragraphs.Item($i).Range
        $text = $pRange.Text.Trim("`r", [string][char]7, " ")
        if ($text.Length -gt 0) {
            Build-EquationInRange -Doc $Doc -Range $pRange
            $pRange.ParagraphFormat.Alignment = 1
        }
    }

    $labelRange = $table.Cell(1, 2).Range
    $labelRange.End = $labelRange.End - 1
    $labelRange.Text = $Label
    $table.Cell(1, 2).Range.ParagraphFormat.Alignment = 2
    $table.Cell(1, 2).Range.Font.Name = "Times New Roman"
    $table.Cell(1, 2).Range.Font.Size = 10.5
}

$word = New-Object -ComObject Word.Application
$word.Visible = $false
$word.DisplayAlerts = 0

$doc = $word.Documents.Open($docx)

# Build Greek symbols from character codes so Windows PowerShell never corrupts
# the script through ANSI code-page interpretation.
$alpha = [string][char]0x03B1
$rho = [string][char]0x03C1
$pi = [string][char]0x03C0

# Process from back to front so InlineShape indices stay stable.
$jobs = @(
    @{Index=27; Label="(4)"; Lines=@("L_(EX-IoU)=1-IoU^${alpha}+(${rho}^(2${alpha})(b,b^gt))/(c^(2${alpha}))+(${rho}^(2${alpha})(w,w^gt))/(C_w^(2${alpha}))+(${rho}^(2${alpha})(h,h^gt))/(C_h^(2${alpha}))")},
    @{Index=26; Label="(3)"; Lines=@("L_(${alpha}-IoU)=1-IoU^${alpha}")},
    @{Index=25; Label="(2)"; Lines=@("L_(EIoU)=1-IoU+(${rho}^2(b,b^gt))/c^2+(${rho}^2(w,w^gt))/(C_w^2)+(${rho}^2(h,h^gt))/(C_h^2)")},
    @{Index=24; Label="(1)"; Lines=@("L_(CIoU)=1-IoU+(${rho}^2(b,b^gt))/c^2+${alpha}v", "${alpha}=v/((1-IoU)+v)", "v=4/${pi}^2 (arctan(w^gt/h^gt)-arctan(w/h))^2")},
    @{Index=16; Label="(4)"; Lines=@("L_(EX-IoU)=1-IoU^${alpha}+(${rho}^(2${alpha})(b,b^gt))/(c^(2${alpha}))+(${rho}^(2${alpha})(w,w^gt))/(C_w^(2${alpha}))+(${rho}^(2${alpha})(h,h^gt))/(C_h^(2${alpha}))")},
    @{Index=15; Label="(3)"; Lines=@("L_(${alpha}-IoU)=1-IoU^${alpha}")},
    @{Index=14; Label="(2)"; Lines=@("L_(EIoU)=1-IoU+(${rho}^2(b,b^gt))/c^2+(${rho}^2(w,w^gt))/(C_w^2)+(${rho}^2(h,h^gt))/(C_h^2)")},
    @{Index=13; Label="(1)"; Lines=@("L_(CIoU)=1-IoU+(${rho}^2(b,b^gt))/c^2+${alpha}v", "${alpha}=v/((1-IoU)+v)", "v=4/${pi}^2 (arctan(w^gt/h^gt)-arctan(w/h))^2")}
)

foreach ($job in $jobs) {
    Replace-FormulaImage -Doc $doc -InlineShapeIndex $job.Index -Lines $job.Lines -Label $job.Label
}

foreach ($toc in $doc.TablesOfContents) { $toc.Update() }
$doc.Fields.Update() | Out-Null
$doc.Save()
$doc.ExportAsFixedFormat($pdf, 17)
$doc.Close($false)
$word.Quit()

Write-Output "Replaced appendix A formula images with editable Word equations and exported PDF."
