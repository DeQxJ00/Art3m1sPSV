param([string]$WslDistribution='Ubuntu-24.04')
$ErrorActionPreference='Stop'
$workspacePath=Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'build-native-commands-core.ps1')
if($LASTEXITCODE -ne 0){throw 'Core build failed'}
$wslPathOutput=& wsl -d $WslDistribution --exec wslpath -u $workspacePath.Replace('\','/')
if($LASTEXITCODE -ne 0 -or !$wslPathOutput){throw 'Cannot resolve workspace in WSL'}
$wslWorkspace=($wslPathOutput -join "`n").Trim()
& wsl -d $WslDistribution --exec bash "$wslWorkspace/scripts/build-native-commands-host.sh"
if($LASTEXITCODE -ne 0){throw 'Host build or VPK archive failed'}
$recordPath=Join-Path $workspacePath 'build/releases/latest.json'
$record=Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
$packagePath=Join-Path (Join-Path $workspacePath 'build/releases') $record.package
if(!(Test-Path -LiteralPath $packagePath)){throw 'Build did not produce a VPK'}
if((Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash -ne $record.sha256){throw 'VPK checksum mismatch'}
Write-Output "Version: $($record.version); PSV APP_VER: $($record.sfo_version)"
Get-Item -LiteralPath $packagePath
