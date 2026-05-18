param(
  [string]$AdapterDescriptionPattern = "*Realtek USB GbE*",
  [string]$PcIp = "192.168.50.1",
  [string]$BoardIp = "192.168.50.2",
  [string]$FallbackBoardIp = "169.254.163.230",
  [switch]$ConfigurePcIp,
  [switch]$OpenSsh
)

$ErrorActionPreference = "Continue"

function Test-Admin {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = [Security.Principal.WindowsPrincipal]::new($identity)
  return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-TcpPort {
  param([string]$HostName, [int]$Port)
  try {
    $client = [Net.Sockets.TcpClient]::new()
    $async = $client.BeginConnect($HostName, $Port, $null, $null)
    $ok = $async.AsyncWaitHandle.WaitOne(1500, $false)
    if ($ok) { $client.EndConnect($async) }
    $client.Close()
    return $ok
  } catch {
    return $false
  }
}

Write-Host "=== RK3588 wired demo connection check ==="

try {
  $adapter = Get-NetAdapter -ErrorAction Stop |
    Where-Object { $_.InterfaceDescription -like $AdapterDescriptionPattern -and $_.Status -eq "Up" } |
    Select-Object -First 1
} catch {
  Write-Host "Get-NetAdapter is unavailable, falling back to CIM adapter query." -ForegroundColor Yellow
  $adapter = Get-CimInstance Win32_NetworkAdapter -Filter "NetEnabled=True" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like $AdapterDescriptionPattern -or $_.NetConnectionID -like $AdapterDescriptionPattern } |
    Select-Object -First 1 -Property `
      @{Name="Name";Expression={$_.NetConnectionID}},
      @{Name="InterfaceDescription";Expression={$_.Name}},
      @{Name="LinkSpeed";Expression={if ($_.Speed) { "{0:N0} bps" -f [double]$_.Speed } else { "unknown" }}},
      @{Name="ifIndex";Expression={$_.InterfaceIndex}}
}

if (-not $adapter) {
  Write-Host "No active wired adapter matched: $AdapterDescriptionPattern" -ForegroundColor Red
  Write-Host "Check cable, USB Ethernet adapter, or adapter driver."
  exit 2
}

Write-Host "Adapter: $($adapter.Name) | $($adapter.InterfaceDescription) | $($adapter.LinkSpeed)"

if ($ConfigurePcIp) {
  if (-not (Test-Admin)) {
    Write-Host "Administrator permission is required to add $PcIp/24." -ForegroundColor Yellow
    Write-Host "Re-launching this script as administrator ..."
    $argsList = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"", "-ConfigurePcIp")
    if ($OpenSsh) { $argsList += "-OpenSsh" }
    Start-Process powershell -Verb RunAs -ArgumentList $argsList
    exit 0
  }

  $exists = Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue |
    Where-Object { $_.IPAddress -eq $PcIp }
  if (-not $exists) {
    Write-Host "Adding temporary PC wired IP: $PcIp/24"
    New-NetIPAddress -InterfaceIndex $adapter.ifIndex -IPAddress $PcIp -PrefixLength 24 -PolicyStore ActiveStore | Out-Null
  } else {
    Write-Host "PC wired IP already exists: $PcIp"
  }
}

Write-Host ""
Write-Host "Current IPv4 on wired adapter:"
Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue |
  Format-Table -Auto InterfaceAlias,IPAddress,PrefixLength,AddressState

$candidates = @($BoardIp, $FallbackBoardIp)
$selected = $null

foreach ($ip in $candidates) {
  Write-Host "Testing SSH ${ip}:22 ..."
  if (Test-TcpPort -HostName $ip -Port 22) {
    $selected = $ip
    break
  }
}

if (-not $selected) {
  Write-Host ""
  Write-Host "Wired SSH is not reachable yet." -ForegroundColor Red
  Write-Host "Try one of these:"
  Write-Host "1. Run once as administrator: .\rk3588-wired-connect.ps1 -ConfigurePcIp"
  Write-Host "2. Ensure the board service was installed: .\install-board-wired-ip.ps1 -BoardIp <wifi-ip>"
  Write-Host "3. Replug the Ethernet cable and wait 5 seconds."
  exit 1
}

$rtspOk = Test-TcpPort -HostName $selected -Port 8554

Write-Host ""
Write-Host "Wired connection is ready." -ForegroundColor Green
Write-Host "SSH:  ssh ubuntu@$selected"
Write-Host "RTSP: rtsp://$selected`:8554/drone"
Write-Host "RTSP port 8554: $rtspOk"

if ($OpenSsh) {
  ssh "ubuntu@$selected"
}
