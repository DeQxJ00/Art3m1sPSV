param(
    [Parameter(Mandatory)][string]$Capture,
    [Parameter(Mandatory)][string]$ProbeLog,
    [Parameter(Mandatory)][string]$Report
)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$bitmap=[Drawing.Bitmap]::new((Resolve-Path -LiteralPath $Capture).Path)
$samples=@()
try {
    foreach ($line in Get-Content -LiteralPath $ProbeLog) {
        if ($line -match 'cell=(\d+) sample=(\d+) xy=(\d+),(\d+) actual=\d+,\d+,\d+,\d+ expected=(\d+),(\d+),(\d+),(\d+)') {
            $x=[int]$Matches[3];$y=[int]$Matches[4]
            $expected=@([int]$Matches[5],[int]$Matches[6],[int]$Matches[7])
            $pixel=$bitmap.GetPixel($x,$y)
            $actual=@([int]$pixel.R,[int]$pixel.G,[int]$pixel.B)
            $maxError=0
            for($i=0;$i -lt 3;$i++) { $maxError=[Math]::Max($maxError,[Math]::Abs($actual[$i]-$expected[$i])) }
            $samples+=@{cell=[int]$Matches[1];sample=[int]$Matches[2];x=$x;y=$y;actual_rgb=$actual;expected_rgb=$expected;max_error=$maxError}
        }
    }
} finally { $bitmap.Dispose() }
$ok=$samples.Count -eq 36 -and @($samples | Where-Object max_error -gt 3).Count -eq 0
$result=@{rgb_pass=$ok;sample_count=$samples.Count;alpha_check='not_available_in_MCP_screenshot';capture_sha256=(Get-FileHash -LiteralPath $Capture).Hash;log_sha256=(Get-FileHash -LiteralPath $ProbeLog).Hash;samples=$samples}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Report
if(-not $ok) { throw 'RGB screenshot comparison failed; inspect report' }
Write-Output 'PASS: 36 GPU screenshot RGB samples; alpha not verified'
