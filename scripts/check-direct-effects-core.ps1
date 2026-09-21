$ErrorActionPreference='Stop'
$workspacePath=Split-Path $PSScriptRoot -Parent
$env:PATH='F:/WorkSpaceAI2/art3m1s-psv/.tools/rustup/toolchains/nightly-2026-08-28-x86_64-pc-windows-msvc/bin;'+$env:PATH
$env:CARGO_TARGET_DIR=Join-Path $workspacePath 'build/shader-completion/tests'
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
  $logPath=Join-Path $workspacePath ('build/shader-completion/check-'+$run.name+'.log')
  $testArgs=@('test','--manifest-path',(Join-Path $workspacePath $run.manifest))+$run.args
  & cargo @testArgs *> $logPath
  $results+=@{name=$run.name;exitCode=$LASTEXITCODE;log=$logPath}
}
$results | ConvertTo-Json | Set-Content (Join-Path $workspacePath 'build/shader-completion/core-suite-results.json')
$results | ConvertTo-Json
if($results | Where-Object exitCode -ne 0){exit 1}
