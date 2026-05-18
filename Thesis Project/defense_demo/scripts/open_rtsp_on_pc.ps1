param(
  [string]$BoardIp = "192.168.10.186",
  [int]$Port = 8554,
  [string]$Mount = "/drone"
)

$url = "rtsp://$BoardIp`:$Port$Mount"
Write-Host "RTSP URL: $url"

$ffplay = Get-Command ffplay -ErrorAction SilentlyContinue
if ($ffplay) {
  & $ffplay.Source -fflags nobuffer -flags low_delay -framedrop $url
  exit $LASTEXITCODE
}

$vlcCandidates = @(
  "$env:ProgramFiles\VideoLAN\VLC\vlc.exe",
  "${env:ProgramFiles(x86)}\VideoLAN\VLC\vlc.exe"
)

foreach ($candidate in $vlcCandidates) {
  if (Test-Path $candidate) {
    & $candidate $url
    exit 0
  }
}

Write-Host "No ffplay or VLC found. Open this URL manually in VLC:"
Write-Host $url

