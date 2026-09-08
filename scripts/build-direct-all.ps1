param([string]$WslDistribution='Ubuntu-24.04')
$ErrorActionPreference='Stop'
$workspacePath=Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'build-core.ps1') -NativeRenderer
if($LASTEXITCODE -ne 0){throw 'Direct GXM core build failed'}
& (Join-Path $PSScriptRoot 'build-direct-shaders.ps1')
$wslWorkspace=(& wsl -d $WslDistribution -- wslpath -u $workspacePath).Trim()
if($LASTEXITCODE -ne 0){throw 'Cannot resolve workspace in WSL'}
& wsl -d $WslDistribution -- bash -c 'cd "$1" && bash scripts/build-ffmpeg.sh && bash scripts/build-tremor.sh && bash scripts/build-direct-host.sh' art3m1s-direct-build $wslWorkspace
if($LASTEXITCODE -ne 0){throw 'Direct GXM media or host build failed'}
Get-Item -LiteralPath (Join-Path $workspacePath 'build/direct-host/art3m1s_direct.vpk')
