param([string]$WslDistribution='Ubuntu-24.04')
$ErrorActionPreference='Stop'
$workspacePath=Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'build-recovered-core.ps1')
if($LASTEXITCODE -ne 0){throw 'Core build failed'}
$wslPathOutput=& wsl -d $WslDistribution --exec wslpath -u $workspacePath.Replace('\','/')
if($LASTEXITCODE -ne 0 -or !$wslPathOutput){throw 'Cannot resolve workspace in WSL'}
$wslWorkspace=($wslPathOutput -join "`n").Trim()
& wsl -d $WslDistribution --exec bash "$wslWorkspace/scripts/build-recovered-host.sh" "$wslWorkspace/build/recovered-core/libart3m1s_core.a" ON
if($LASTEXITCODE -ne 0){throw 'Recovered host build failed'}
& wsl -d $WslDistribution --exec python3 "$wslWorkspace/scripts/verify-recovered-release.py" --build "$wslWorkspace/build/recovered-host" --sdk /home/qxj00/ae3-vitagl-build-20260830/vitasdk
if($LASTEXITCODE -ne 0){throw 'Recovered backend verification failed'}
& wsl -d $WslDistribution --exec cmake --build "$wslWorkspace/build/recovered-host" --target art3m1s_release -j2
if($LASTEXITCODE -ne 0){throw 'Host build or VPK archive failed'}
$recordPath=Join-Path $workspacePath 'build/releases/latest.json'
$record=Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
if($record.emote_backend -ne 'recovered' -or $record.emote_default_backend -ne 2){throw 'Release must use the recovered E-mote backend by default'}
$packagePath=Join-Path (Join-Path $workspacePath 'build/releases') $record.package
if(!(Test-Path -LiteralPath $packagePath)){throw 'Build did not produce a VPK'}
if((Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash -ne $record.sha256){throw 'VPK checksum mismatch'}
Write-Output "Version: $($record.version); PSV APP_VER: $($record.sfo_version)"
Get-Item -LiteralPath $packagePath
