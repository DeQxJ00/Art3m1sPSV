param([ValidateSet('shuffle-steam','otomeriron')][string]$Game)
$ErrorActionPreference='Stop'
$workspacePath=Split-Path $PSScriptRoot -Parent
$sourceName=if($Game -eq 'shuffle-steam'){'steam的SHUFFLE_EP2'}else{'steam的Otomeriron'}
$source=Join-Path $workspacePath ('验收测试/'+$sourceName+'/movie')
$output=Join-Path $workspacePath ('temp/converted/'+$Game+'/movie')
New-Item -ItemType Directory -Force $output|Out-Null
foreach($movie in (Get-ChildItem -LiteralPath $source -Filter *.dat -File)){
    $target=Join-Path $output ($movie.BaseName+'.mp4')
    if(Test-Path -LiteralPath $target){throw "Output exists: $target"}
    & ffprobe -v error -show_streams -show_format -of json $movie.FullName | Set-Content -Encoding utf8 ($target+'.source.json')
    if($LASTEXITCODE -ne 0){throw 'Input probe failed'}
    & ffmpeg -nostdin -n -i $movie.FullName -vf 'scale=960:544' -c:v libx264 -profile:v main -level 3.1 -preset medium -crf 23 -pix_fmt yuv420p -c:a aac -b:a 128k -ar 48000 -movflags +faststart $target *> ($target+'.log')
    if($LASTEXITCODE -ne 0){throw "Transcode failed: $target"}
    & ffprobe -v error -show_streams -show_format -of json $target | Set-Content -Encoding utf8 ($target+'.probe.json')
    if($LASTEXITCODE -ne 0){throw 'Output probe failed'}
    $probe=Get-Content -Raw ($target+'.probe.json')|ConvertFrom-Json
    $video=$probe.streams|Where-Object codec_type -eq video
    if($video.codec_name -ne 'h264' -or $video.width -ne 960 -or $video.height -ne 544 -or $video.pix_fmt -ne 'yuv420p'){throw 'Unexpected converted video format'}
    Write-Output "Converted and probed $($movie.Name)"
}
