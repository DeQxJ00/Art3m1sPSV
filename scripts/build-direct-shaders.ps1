$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$compiler=Join-Path $root '.tools/sony-shader-3.570/sdk/host_tools/bin/psp2cgc.exe'
$out=Join-Path $root 'build/direct-shaders'
New-Item -ItemType Directory -Force $out | Out-Null
$lines=@('#pragma once')
foreach($shader in @('sprite_v','sprite_f','image_f','clip_f','rule_f','ruleclip_f')) {
  $profile=if($shader.EndsWith('_v')) {'sce_vp_psp2'} else {'sce_fp_psp2'}
  $binary=Join-Path $out "$shader.gxp"
  & $compiler -profile $profile -O1 -nofastmath -bestprecision -o $binary (Join-Path $root "host-direct/shaders/$shader.cg")
  if($LASTEXITCODE -ne 0) { throw "Direct shader failed: $shader" }
  $bytes=[IO.File]::ReadAllBytes($binary)
  $lines+="alignas(4) static const unsigned char $shader[] = {"
  for($i=0;$i -lt $bytes.Length;$i+=16) {
    $end=[Math]::Min($i+15,$bytes.Length-1)
    $lines+=(($bytes[$i..$end] | ForEach-Object { '0x{0:x2}' -f $_ }) -join ',')+','
  }
  $lines+='};'
}
[IO.File]::WriteAllLines((Join-Path $root 'host-direct/src/shaders.hpp'),$lines)
