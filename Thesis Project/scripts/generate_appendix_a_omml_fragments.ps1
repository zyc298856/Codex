$ErrorActionPreference = "Stop"

$xsl = "C:\Program Files\Microsoft Office\root\Office16\MML2OMML.XSL"
$outDir = "C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project\docs\thesis_drafting\appendix_a_omml_fragments"
if (-not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}

function Convert-MathMlToOmmlFile {
    param(
        [Parameter(Mandatory=$true)][string]$Name,
        [Parameter(Mandatory=$true)][string]$MathMl
    )
    $transform = New-Object System.Xml.Xsl.XslCompiledTransform
    $transform.Load($xsl)

    $reader = [System.Xml.XmlReader]::Create((New-Object System.IO.StringReader($MathMl)))
    $settings = New-Object System.Xml.XmlWriterSettings
    $settings.OmitXmlDeclaration = $true
    $settings.ConformanceLevel = [System.Xml.ConformanceLevel]::Fragment
    $settings.Encoding = [System.Text.Encoding]::UTF8

    $path = Join-Path $outDir "$Name.xml"
    $writer = [System.Xml.XmlWriter]::Create($path, $settings)
    $transform.Transform($reader, $writer)
    $writer.Close()
    $reader.Close()
    Write-Output $path
}

$ciou1 = @'
<math xmlns="http://www.w3.org/1998/Math/MathML" display="block">
  <mrow>
    <msub><mi>L</mi><mrow><mi>CIoU</mi></mrow></msub>
    <mo>=</mo><mn>1</mn><mo>-</mo><mi>IoU</mi><mo>+</mo>
    <mfrac>
      <mrow><msup><mi>&#x03C1;</mi><mn>2</mn></msup><mo>(</mo><mi>b</mi><mo>,</mo><msup><mi>b</mi><mrow><mi>gt</mi></mrow></msup><mo>)</mo></mrow>
      <msup><mi>c</mi><mn>2</mn></msup>
    </mfrac>
    <mo>+</mo><mi>&#x03B1;</mi><mi>v</mi>
  </mrow>
</math>
'@

$ciou2 = @'
<math xmlns="http://www.w3.org/1998/Math/MathML" display="block">
  <mrow>
    <mi>&#x03B1;</mi><mo>=</mo>
    <mfrac>
      <mi>v</mi>
      <mrow><mo>(</mo><mo>(</mo><mn>1</mn><mo>-</mo><mi>IoU</mi><mo>)</mo><mo>+</mo><mi>v</mi><mo>)</mo></mrow>
    </mfrac>
  </mrow>
</math>
'@

$ciou3 = @'
<math xmlns="http://www.w3.org/1998/Math/MathML" display="block">
  <mrow>
    <mi>v</mi><mo>=</mo>
    <mfrac><mn>4</mn><msup><mi>&#x03C0;</mi><mn>2</mn></msup></mfrac>
    <msup>
      <mrow>
        <mo>(</mo>
        <mi>arctan</mi><mfrac><msup><mi>w</mi><mrow><mi>gt</mi></mrow></msup><msup><mi>h</mi><mrow><mi>gt</mi></mrow></msup></mfrac>
        <mo>-</mo>
        <mi>arctan</mi><mfrac><mi>w</mi><mi>h</mi></mfrac>
        <mo>)</mo>
      </mrow>
      <mn>2</mn>
    </msup>
  </mrow>
</math>
'@

$eiou = @'
<math xmlns="http://www.w3.org/1998/Math/MathML" display="block">
  <mrow>
    <msub><mi>L</mi><mrow><mi>EIoU</mi></mrow></msub>
    <mo>=</mo><mn>1</mn><mo>-</mo><mi>IoU</mi><mo>+</mo>
    <mfrac>
      <mrow><msup><mi>&#x03C1;</mi><mn>2</mn></msup><mo>(</mo><mi>b</mi><mo>,</mo><msup><mi>b</mi><mrow><mi>gt</mi></mrow></msup><mo>)</mo></mrow>
      <msup><mi>c</mi><mn>2</mn></msup>
    </mfrac>
    <mo>+</mo>
    <mfrac>
      <mrow><msup><mi>&#x03C1;</mi><mn>2</mn></msup><mo>(</mo><mi>w</mi><mo>,</mo><msup><mi>w</mi><mrow><mi>gt</mi></mrow></msup><mo>)</mo></mrow>
      <msup><msub><mi>C</mi><mi>w</mi></msub><mn>2</mn></msup>
    </mfrac>
    <mo>+</mo>
    <mfrac>
      <mrow><msup><mi>&#x03C1;</mi><mn>2</mn></msup><mo>(</mo><mi>h</mi><mo>,</mo><msup><mi>h</mi><mrow><mi>gt</mi></mrow></msup><mo>)</mo></mrow>
      <msup><msub><mi>C</mi><mi>h</mi></msub><mn>2</mn></msup>
    </mfrac>
  </mrow>
</math>
'@

$aiou = @'
<math xmlns="http://www.w3.org/1998/Math/MathML" display="block">
  <mrow>
    <msub><mi>L</mi><mrow><mi>&#x03B1;</mi><mo>-</mo><mi>IoU</mi></mrow></msub>
    <mo>=</mo><mn>1</mn><mo>-</mo><msup><mi>IoU</mi><mi>&#x03B1;</mi></msup>
  </mrow>
</math>
'@

$exiou = @'
<math xmlns="http://www.w3.org/1998/Math/MathML" display="block">
  <mrow>
    <msub><mi>L</mi><mrow><mi>EX</mi><mo>-</mo><mi>IoU</mi></mrow></msub>
    <mo>=</mo><mn>1</mn><mo>-</mo><msup><mi>IoU</mi><mi>&#x03B1;</mi></msup><mo>+</mo>
    <mfrac>
      <mrow><msup><mi>&#x03C1;</mi><mrow><mn>2</mn><mi>&#x03B1;</mi></mrow></msup><mo>(</mo><mi>b</mi><mo>,</mo><msup><mi>b</mi><mrow><mi>gt</mi></mrow></msup><mo>)</mo></mrow>
      <msup><mi>c</mi><mrow><mn>2</mn><mi>&#x03B1;</mi></mrow></msup>
    </mfrac>
    <mo>+</mo>
    <mfrac>
      <mrow><msup><mi>&#x03C1;</mi><mrow><mn>2</mn><mi>&#x03B1;</mi></mrow></msup><mo>(</mo><mi>w</mi><mo>,</mo><msup><mi>w</mi><mrow><mi>gt</mi></mrow></msup><mo>)</mo></mrow>
      <msup><msub><mi>C</mi><mi>w</mi></msub><mrow><mn>2</mn><mi>&#x03B1;</mi></mrow></msup>
    </mfrac>
    <mo>+</mo>
    <mfrac>
      <mrow><msup><mi>&#x03C1;</mi><mrow><mn>2</mn><mi>&#x03B1;</mi></mrow></msup><mo>(</mo><mi>h</mi><mo>,</mo><msup><mi>h</mi><mrow><mi>gt</mi></mrow></msup><mo>)</mo></mrow>
      <msup><msub><mi>C</mi><mi>h</mi></msub><mrow><mn>2</mn><mi>&#x03B1;</mi></mrow></msup>
    </mfrac>
  </mrow>
</math>
'@

Convert-MathMlToOmmlFile -Name "ciou1" -MathMl $ciou1
Convert-MathMlToOmmlFile -Name "ciou2" -MathMl $ciou2
Convert-MathMlToOmmlFile -Name "ciou3" -MathMl $ciou3
Convert-MathMlToOmmlFile -Name "eiou" -MathMl $eiou
Convert-MathMlToOmmlFile -Name "aiou" -MathMl $aiou
Convert-MathMlToOmmlFile -Name "exiou" -MathMl $exiou
