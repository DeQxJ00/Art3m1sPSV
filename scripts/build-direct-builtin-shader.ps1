$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$compiler=Join-Path $root '.tools/sony-shader-3.570/sdk/host_tools/bin/psp2cgc.exe'
$out=Join-Path $root 'build/direct-builtin-shader'
New-Item -ItemType Directory -Force $out | Out-Null
$oldHash=(Get-FileHash "$root/host-direct/src/shaders.hpp").Hash
$binary=Join-Path $out 'builtin_f.gxp'
& $compiler -profile sce_fp_psp2 -O1 -nofastmath -bestprecision -o $binary "$root/host-direct/shaders/builtin_f.cg"
if($LASTEXITCODE -ne 0){throw 'Direct builtin shader compile failed'}
$bytes=[IO.File]::ReadAllBytes($binary)
$lines=@('#pragma once','alignas(4) static const unsigned char builtin_f[] = {')
for($i=0;$i -lt $bytes.Length;$i+=16){
  $end=[Math]::Min($i+15,$bytes.Length-1)
  $lines+=(($bytes[$i..$end] | ForEach-Object {'0x{0:x2}' -f $_}) -join ',')+','
}
$lines+='};'
[IO.File]::WriteAllLines("$root/host-direct/src/builtin_shader.hpp",$lines)
if((Get-FileHash "$root/host-direct/src/shaders.hpp").Hash -ne $oldHash){throw 'Baseline shader modified'}
