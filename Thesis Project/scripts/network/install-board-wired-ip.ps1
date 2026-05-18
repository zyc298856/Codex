param(
  [string]$BoardIp = "192.168.2.156",
  [string]$BoardUser = "ubuntu",
  [string]$IfName = "eth1",
  [string]$FixedIp = "192.168.50.2/24",
  [string]$LinkLocalIp = "169.254.163.230/16"
)

$ErrorActionPreference = "Stop"

$workspaceRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")
$vendor = Join-Path $workspaceRoot ".vendor"
$env:PYTHONPATH = $vendor

$script = Join-Path $PSScriptRoot "install-board-wired-ip.py"

Write-Host "Installing persistent wired IP service on $BoardUser@$BoardIp ..."
python $script --host $BoardIp --user $BoardUser --ifname $IfName --fixed-ip $FixedIp --linklocal-ip $LinkLocalIp

Write-Host ""
Write-Host "Board setup finished."
Write-Host "Fixed wired SSH: ssh $BoardUser@192.168.50.2"
Write-Host "Fallback wired SSH: ssh $BoardUser@169.254.163.230"
