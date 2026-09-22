$ErrorActionPreference='Stop'
$workspacePath=Split-Path $PSScriptRoot -Parent
if(!(Test-Path -LiteralPath (Join-Path $workspacePath "core/Cargo.toml"))){throw "Core submodule is missing; run git submodule update --init --recursive"}
$env:PATH='F:/WorkSpaceAI2/art3m1s-psv/.tools/rustup/toolchains/nightly-2026-08-28-x86_64-pc-windows-msvc/bin;'+$env:PATH
$env:CARGO_TARGET_DIR=Join-Path $workspacePath 'build/shader-completion/tests'
$cargoPath=(Get-Command cargo -CommandType Application -ErrorAction Stop).Source
$resultsDirectory=Join-Path $workspacePath 'temp/test-results/core'
[IO.Directory]::CreateDirectory($resultsDirectory) | Out-Null
# Windows equivalents of core/scripts/test-all.sh; each result is preserved.
$runs=@(
  @{name='all-core';manifest='core/Cargo.toml';args=@('--all-targets')},
  @{name='gxm-core';manifest='core/Cargo.toml';args=@('--lib','--no-default-features','--features','gl-backend,gxm-native-renderer,gxm-text-epoch,gxm-menu-key-alias,gxm-builtin-effects')},
  @{name='asb-lua51';manifest='core/crates/asb-interpreter/Cargo.toml';args=@('--all-targets')},
  @{name='asb-luau';manifest='core/crates/asb-interpreter/Cargo.toml';args=@('--no-default-features','--features','backend-luau','--lib','--tests')},
  @{name='emote';manifest='core/crates/art3m1s-emote/Cargo.toml';args=@('--all-targets')},
  @{name='eluna';manifest='core/crates/eluna/Cargo.toml';args=@('--all-targets')},
  @{name='pf8';manifest='core/crates/pf8/Cargo.toml';args=@('--all-targets')},
  @{name='pfs';manifest='core/crates/pfs-upk-rust/Cargo.toml';args=@('--all-targets')}
)
$results=@()
foreach($run in $runs){
  $logPath=Join-Path $resultsDirectory ('check-'+$run.name+'.log')
  $testArgs=@('test','--manifest-path',('"'+(Join-Path $workspacePath $run.manifest)+'"'))+$run.args
  # Own the process and drain both pipes concurrently. PowerShell 5's
  # Start-Process can wait on descendant jobs or lose the native exit code.
  $startInfo=New-Object Diagnostics.ProcessStartInfo
  $startInfo.FileName=$cargoPath
  $startInfo.Arguments=$testArgs -join ' '
  $startInfo.UseShellExecute=$false
  $startInfo.CreateNoWindow=$true
  $startInfo.RedirectStandardOutput=$true
  $startInfo.RedirectStandardError=$true
  $process=New-Object Diagnostics.Process
  $process.StartInfo=$startInfo
  if(!$process.Start()){throw "Cannot start Cargo for $($run.name)"}
  $stdoutTask=$process.StandardOutput.ReadToEndAsync()
  $stderrTask=$process.StandardError.ReadToEndAsync()
  $process.WaitForExit()
  $stdout=$stdoutTask.GetAwaiter().GetResult()
  $stderr=$stderrTask.GetAwaiter().GetResult()
  [IO.File]::WriteAllText($logPath+'.stdout',$stdout)
  [IO.File]::WriteAllText($logPath+'.stderr',$stderr)
  [IO.File]::WriteAllText($logPath,$stderr+$stdout)
  $results+=@{name=$run.name;exitCode=$process.ExitCode;log=$logPath}
  $process.Dispose()
}
$results | ConvertTo-Json | Set-Content (Join-Path $resultsDirectory 'core-suite-results.json')
$results | ConvertTo-Json
if($results | Where-Object exitCode -ne 0){exit 1}
