param([string]$WslDistribution='Ubuntu-24.04')
$ErrorActionPreference='Stop'
$workspacePath=Split-Path $PSScriptRoot -Parent
# Baseline builds deliberately use the archived 01.02 core and shader header.
# Rebuilding today's core would not reproduce the historical core source.
$wslWorkspace=(& wsl -d $WslDistribution -- wslpath -u $workspacePath).Trim()
if($LASTEXITCODE -ne 0){throw 'Cannot resolve workspace in WSL'}
& wsl -d $WslDistribution -- bash -c 'cd "$1" && bash scripts/build-direct-host.sh' art3m1s-direct-build $wslWorkspace
if($LASTEXITCODE -ne 0){throw 'Direct GXM media or host build failed'}
Get-Item -LiteralPath (Join-Path $workspacePath 'build/direct-01.02-host/art3m1s_direct.vpk')
