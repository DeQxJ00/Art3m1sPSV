param(
    [Parameter(Mandatory)][string]$SessionId,
    [Parameter(Mandatory)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$payload = @{
    jsonrpc = '2.0'
    id = 1
    method = 'tools/call'
    params = @{
        name = 'capture_screen'
        arguments = @{ sessionId = $SessionId }
    }
} | ConvertTo-Json -Depth 10

$reply = Invoke-WebRequest -Uri 'http://127.0.0.1:32560/mcp' -Method Post `
    -ContentType 'application/json' -Headers @{ Accept = 'application/json, text/event-stream' } `
    -Body $payload -TimeoutSec 45
$data = (($reply.Content -split "`n" | Where-Object { $_.StartsWith('data: ') }) -replace '^data: ', '') | ConvertFrom-Json
if ($data.error) { throw ($data.error | ConvertTo-Json -Depth 10) }
$image = $data.result.content | Where-Object type -eq image | Select-Object -First 1
if (-not $image) { throw 'capture_screen returned no image content' }
$fullPath = if ([System.IO.Path]::IsPathRooted($OutputPath)) {
    [System.IO.Path]::GetFullPath($OutputPath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $OutputPath))
}
[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($fullPath)) | Out-Null
[System.IO.File]::WriteAllBytes($fullPath, [Convert]::FromBase64String($image.data))
$fullPath
