param([Parameter(Mandatory)][string]$Tool, [string]$Arguments = '{}')
$ErrorActionPreference = 'Stop'
$payload = @{jsonrpc='2.0';id=1;method='tools/call';params=@{name=$Tool;arguments=($Arguments | ConvertFrom-Json)}} | ConvertTo-Json -Depth 20
$reply = Invoke-WebRequest -Uri 'http://127.0.0.1:32569/mcp' -Method Post -ContentType 'application/json' -Headers @{Accept='application/json, text/event-stream'} -Body $payload -TimeoutSec 45
$data = (($reply.Content -split "`n" | Where-Object { $_.StartsWith('data: ') }) -replace '^data: ','') | ConvertFrom-Json
if ($data.error) { throw ($data.error | ConvertTo-Json -Depth 10) }
if ($data.result.structuredContent) { $data.result.structuredContent | ConvertTo-Json -Depth 20 }
else { $data.result.content | Where-Object type -eq text | ForEach-Object text }
