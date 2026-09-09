$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$compiler=Join-Path $root '.tools/sony-shader-3.570/sdk/host_tools/bin/psp2cgc.exe'
$out=Join-Path $root 'build/direct-builtin-shader'
New-Item -ItemType Directory -Force $out | Out-Null
$oldHash=(Get-FileHash "$root/host-direct/src/shaders.hpp").Hash
$lines=@('#pragma once')
foreach($shader in @('builtin_f','builtin_copy_f','builtin_composite_f','builtin_color_f','builtin_single_f')) {
  $binary=Join-Path $out "$shader.gxp"
  & $compiler -profile sce_fp_psp2 -O1 -nofastmath -bestprecision -o $binary "$root/host-direct/shaders/$shader.cg"
  if($LASTEXITCODE -ne 0){throw "Direct builtin shader compile failed: $shader"}
  $bytes=[IO.File]::ReadAllBytes($binary)
  $lines+="alignas(4) static const unsigned char $shader[] = {"
  for($i=0;$i -lt $bytes.Length;$i+=16){
    $end=[Math]::Min($i+15,$bytes.Length-1)
    $lines+=(($bytes[$i..$end] | ForEach-Object {'0x{0:x2}' -f $_}) -join ',')+','
  }
  $lines+='};'
}
[IO.File]::WriteAllLines("$root/host-direct/src/builtin_shader.hpp",$lines)
if((Get-FileHash "$root/host-direct/src/shaders.hpp").Hash -ne $oldHash){throw 'Baseline shader modified'}
