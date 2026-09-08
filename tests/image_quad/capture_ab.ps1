param([Parameter(Mandatory)][string]$SessionId, [Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$workspacePath=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$outputPath=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $outputPath | Out-Null
Add-Type -AssemblyName System.Drawing
$seen=@{}
for($i=0;$i -lt 22 -and $seen.Count -lt 2;$i++) {
    $capturePath=Join-Path $outputPath 'current.png'
    & (Join-Path $workspacePath 'scripts/capture-mcp.ps1') -SessionId $SessionId -OutputPath $capturePath | Out-Null
    $bitmap=[Drawing.Bitmap]::new($capturePath)
    try { $pixel=$bitmap.GetPixel(2,2) } finally { $bitmap.Dispose() }
    $mode=if($pixel.R -eq 255 -and $pixel.G -eq 0 -and $pixel.B -eq 0) {'legacy'}
          elseif($pixel.R -eq 0 -and $pixel.G -eq 255 -and $pixel.B -eq 0) {'quad'} else {''}
    if($mode -and -not $seen.ContainsKey($mode)) {
        Copy-Item -LiteralPath $capturePath -Destination (Join-Path $outputPath "$mode.png")
        $seen[$mode]=[DateTime]::UtcNow.ToString('o')
    }
    if($seen.Count -lt 2) { Start-Sleep -Seconds 1 }
}
$seen | ConvertTo-Json | Set-Content (Join-Path $outputPath 'captures.json')
if($seen.Count -ne 2) { throw 'Did not observe both diagnostic paths; keep the game stationary and use the A/B build.' }
$seen | ConvertTo-Json
