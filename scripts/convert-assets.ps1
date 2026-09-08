param([ValidateSet('shuffle-steam','otomeriron')][string]$Game)
$ErrorActionPreference='Stop'
$workspacePath=Split-Path $PSScriptRoot -Parent
$sourceName=if($Game -eq 'shuffle-steam'){'steam的SHUFFLE_EP2'}else{'steam的Otomeriron'}
$scale=if($Game -eq 'shuffle-steam'){0.75}else{0.5}
$source=Join-Path $workspacePath ('验收测试/'+$sourceName)
$output=Join-Path $workspacePath ('build/converted/'+$Game)
$archiveTool=Join-Path $workspacePath 'tools/assets/target/release/art3m1s-assets.exe'
$resizeTool=Join-Path $workspacePath 'build/ArtemisTools-fixed/ResizeBatch.exe'
New-Item -ItemType Directory -Force $output | Out-Null
$report=@()
foreach($archive in (Get-ChildItem -LiteralPath $source -File | Where-Object Name -Match '\.pfs(\.\d+)?$' | Sort-Object Name)){
    $working=Join-Path $workspacePath ('build/conversion-work/'+$Game+'/'+$archive.Name)
    $newArchive=Join-Path $output $archive.Name
    if(Test-Path -LiteralPath $newArchive){
        Write-Output "Revalidate completed archive $($archive.Name)"
        & $archiveTool verify $newArchive $working
        if($LASTEXITCODE -ne 0){throw 'Existing archive differs from conversion directory'}
        $images=@(Get-ChildItem -LiteralPath $working -Recurse -Filter *.png -File)
    }else{
    Write-Output "Extract $($archive.Name)"
    & $archiveTool extract $archive.FullName $working
    if($LASTEXITCODE -ne 0){throw 'Extraction failed'}
    $images=@(Get-ChildItem -LiteralPath $working -Recurse -Filter *.png -File)
    if($images.Count){
        Write-Output "Resize $($images.Count) images by $scale"
        & $resizeTool ResizeImage $working ($scale.ToString([cultureinfo]::InvariantCulture)) 1 2 6 *> ($working+'.resize.log')
        if($LASTEXITCODE -ne 0){throw "Resize failed: $working"}
        $resized=$working+'_Resize/Temp'
        $results=@(Get-ChildItem -LiteralPath $resized -Recurse -Filter *.png -File)
        if($results.Count -ne $images.Count){throw 'Resize count mismatch'}
        foreach($image in $images){
            $relative=[IO.Path]::GetRelativePath($working,$image.FullName)
            $result=Join-Path $resized $relative
            if(!(Test-Path -LiteralPath $result)){throw "Missing resized image: $relative"}
            Copy-Item -LiteralPath $result -Destination $image.FullName
        }
    }
    $ini=Join-Path $working 'system.ini'
    if(Test-Path -LiteralPath $ini){
        $text=[IO.File]::ReadAllText($ini)
        $text=[regex]::Replace($text,'(?m)^WIDTH\s*=\s*\d+','WIDTH = 960')
        $text=[regex]::Replace($text,'(?m)^HEIGHT\s*=\s*\d+','HEIGHT = 540')
        [IO.File]::WriteAllText($ini,$text,[Text.UTF8Encoding]::new($false))
    }
    & $archiveTool pack $working $newArchive
    if($LASTEXITCODE -ne 0){throw 'Packing failed'}
    & $archiveTool verify $newArchive $working
    if($LASTEXITCODE -ne 0){throw 'Archive verification failed'}
    }
    $report+=@{archive=$archive.Name;scale=$scale;pngCount=$images.Count;sourceSha256=(Get-FileHash -LiteralPath $archive.FullName).Hash;outputSha256=(Get-FileHash -LiteralPath $newArchive).Hash;bytes=(Get-Item -LiteralPath $newArchive).Length}
    $report | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 (Join-Path $output 'conversion-manifest.json')
}
