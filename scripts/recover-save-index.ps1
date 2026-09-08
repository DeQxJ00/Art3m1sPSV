param([string]$BackupDirectory = 'build/repro/avatar-alpha')
$ErrorActionPreference='Stop'
$raw=[IO.File]::ReadAllText((Join-Path $PWD "$BackupDirectory/saveg-corrupt.dat"))
$depth=0;$quoted=$false;$escaped=$false;$end=-1
for($i=0;$i -lt $raw.Length;$i++){
    $c=$raw[$i]
    if($quoted){if($escaped){$escaped=$false}elseif($c -eq '\'){$escaped=$true}elseif($c -eq '"'){$quoted=$false}}
    elseif($c -eq '"'){$quoted=$true}
    elseif($c -eq '{' -or $c -eq '['){$depth++}
    elseif($c -eq '}' -or $c -eq ']'){$depth--;if($depth -eq 0){$end=$i+1;break}}
}
if($end -lt 0){throw 'No complete leading JSON object'}
$index=$raw.Substring(0,$end)|ConvertFrom-Json -AsHashtable
$system=$index.system|ConvertFrom-Json -AsHashtable
if(!$system){$system=@{}}
$slots=@{page=1;quick=1;cont=181;last=1;actv=1;lock=@{}}
foreach($number in @(1,181)){
    $name='save{0:D4}' -f $number
    $file=Get-Item -LiteralPath "$BackupDirectory/$name.dat"
    $save=Get-Content -Raw -LiteralPath $file.FullName|ConvertFrom-Json -AsHashtable
    $serialized=$save.variables.local.script
    if(!$serialized){$serialized=$save.variables.local.scr}
    $scene=$serialized|ConvertFrom-Json -AsHashtable
    if(!$scene.ip.save.text){throw "No saved text found in $name"}
    $slots[[string]$number]=@{file=$name;text=$scene.ip.save.text;title=@{ja='恢复的存档'};date=([DateTimeOffset]$file.LastWriteTimeUtc).ToUnixTimeSeconds()}
}
$system.saveslot=$slots
$index.system=$system|ConvertTo-Json -Depth 30 -Compress
$index|ConvertTo-Json -Depth 30|Set-Content -Encoding utf8 "$BackupDirectory/saveg-recovered.dat"
Get-Content -Raw "$BackupDirectory/saveg-recovered.dat"|ConvertFrom-Json|Out-Null
Write-Output 'Prepared recovered save index for slots 1 and 181; live files unchanged.'
