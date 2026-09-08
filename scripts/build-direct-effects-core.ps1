$ErrorActionPreference = 'Stop'
$workspacePath = Split-Path $PSScriptRoot -Parent
$sdkPath = 'F:\WorkSpaceAI2\art3m1s-psv\.tools\vitasdk\sdk-2026.08'
$rustPath = 'F:\WorkSpaceAI2\art3m1s-psv\.tools\rustup\toolchains\nightly-2026-08-28-x86_64-pc-windows-msvc'
$env:PATH = "$rustPath\bin;$sdkPath\bin;$env:PATH"
$env:VITASDK = $sdkPath
$env:CARGO_TARGET_DIR = Join-Path $workspacePath 'build/shader-completion/rust'
$env:CARGO_TARGET_ARMV7_SONY_VITA_NEWLIBEABIHF_LINKER = "$sdkPath\bin\arm-vita-eabi-gcc.exe"
$env:CC_armv7_sony_vita_newlibeabihf = "$sdkPath\bin\arm-vita-eabi-gcc.exe"
$env:AR_armv7_sony_vita_newlibeabihf = "$sdkPath\bin\arm-vita-eabi-ar.exe"
& "$rustPath\bin\cargo.exe" rustc --manifest-path "$workspacePath/core/Cargo.toml" --lib --release --no-default-features -Z build-std=std,panic_abort --target armv7-sony-vita-newlibeabihf --crate-type staticlib --features gl-backend,gxm-builtin-effects
if ($LASTEXITCODE -ne 0) { throw 'Effects core build failed' }
