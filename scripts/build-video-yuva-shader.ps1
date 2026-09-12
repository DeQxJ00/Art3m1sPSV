$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$compiler=Join-Path $root '.tools/sony-shader-3.570/sdk/host_tools/bin/psp2cgc.exe'
$out=Join-Path $root 'build/video-yuva-gxm'
New-Item -ItemType Directory -Force $out | Out-Null
$binary=Join-Path $out 'video_yuva_f.gxp'
& $compiler -profile sce_fp_psp2 -O1 -nofastmath -bestprecision -o $binary "$root/host-direct/shaders/video_yuva_f.cg"
if($LASTEXITCODE -ne 0){throw 'Video YUVA shader compilation failed'}
$bytes=[IO.File]::ReadAllBytes($binary)
$lines=@('#pragma once','alignas(4) static const unsigned char video_yuva_f[] = {')
for($i=0;$i -lt $bytes.Length;$i+=16){
  $end=[Math]::Min($i+15,$bytes.Length-1)
  $lines+=(($bytes[$i..$end] | ForEach-Object {'0x{0:x2}' -f $_}) -join ',')+','
}
$lines+='};'
[IO.File]::WriteAllLines("$root/host-direct/src/video_yuva_shader.hpp",$lines)
