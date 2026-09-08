param([string]$WslDistribution = 'Ubuntu-24.04')
$ErrorActionPreference = 'Stop'
$workspacePath = Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'build-core.ps1') -Gxm
if ($LASTEXITCODE -ne 0) { throw 'Rust GXM build failed' }
$wslWorkspace = (& wsl -d $WslDistribution -- wslpath -u $workspacePath).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve workspace in WSL' }
# Pass the path as argv rather than interpolating it into shell code.
& wsl -d $WslDistribution -- bash -c 'cd "$1" && bash scripts/build-ffmpeg.sh && bash scripts/build-tremor.sh && bash scripts/build-gxm-host.sh' art3m1s-build $wslWorkspace
if ($LASTEXITCODE -ne 0) { throw 'Vita media or host build failed' }
Get-Item -LiteralPath (Join-Path $workspacePath 'build/gxm-host/art3m1s_gxm.vpk')
