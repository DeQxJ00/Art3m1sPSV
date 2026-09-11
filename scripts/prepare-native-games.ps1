param([string]$InstalledRoot='E:/EmuGame/vita3k_data/ux0/app',
      [string]$GamesRoot='E:/EmuGame/vita3k_data/ux0/data/art3m1s-gxm/games')
$ErrorActionPreference='Stop'

$catalog=@(
    @{id='PCSG01297';title='IxSHE Tell'},
    @{id='PCSG01201';title='となりに彼女のいる幸せ ～Winter Guest～'},
    @{id='PCSG01107';title='ノラと皇女と野良猫ハート'},
    @{id='PCSG01084';title='ワガママハイスペック'},
    @{id='PCSG01127';title='千の刃濤、桃花染の皇姫'}
)
foreach($game in $catalog){
    $source=Join-Path $InstalledRoot $game.id
    $target=Join-Path $GamesRoot $game.id
    if(!(Test-Path -LiteralPath (Join-Path $source 'root.pfs'))){throw "Missing source $source"}
    if([IO.Path]::GetFullPath($target).StartsWith([IO.Path]::GetFullPath($source),[StringComparison]::OrdinalIgnoreCase)){throw 'Target must be separate from source'}
    New-Item -ItemType Directory -Force $target | Out-Null
    $files=@(Get-ChildItem -LiteralPath $source -File | Where-Object { $_.Name -match '^root(\.\d{3})?\.pfs(\.\d{3})?$' -or $_.Name -eq 'saveicon.png' })
    # Native packages use different loose-video roots. Keep their script paths.
    foreach($mediaRoot in @('movie','movie960','psvita')){
        $media=Join-Path $source $mediaRoot
        if(Test-Path -LiteralPath $media){$files+=@(Get-ChildItem -LiteralPath $media -Recurse -File)}
    }
    $report=@()
    foreach($file in $files){
        $relative=[IO.Path]::GetRelativePath($source,$file.FullName)
        # Some installed copies contain alternate archive names; normalize only
        # in our private resource directory and reject unequal collisions.
        if($relative -match '^root\.(\d{3})\.pfs$'){$relative='root.pfs.'+$Matches[1]}
        $destination=Join-Path $target $relative
        $hash=(Get-FileHash -LiteralPath $file.FullName).Hash
        if(Test-Path -LiteralPath $destination){
            if((Get-FileHash -LiteralPath $destination).Hash -ne $hash){throw "Conflicting existing file $destination"}
        }else{
            New-Item -ItemType Directory -Force (Split-Path $destination -Parent) | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $destination
            if((Get-FileHash -LiteralPath $destination).Hash -ne $hash){throw "Copy mismatch $destination"}
        }
        $report+=@{source=$file.FullName;destination=$relative;sha256=$hash;bytes=$file.Length}
    }
    $report | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 (Join-Path $target 'copy-manifest.json')
    [IO.File]::WriteAllText((Join-Path $target 'title.txt'), $game.title, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $target 'platform.txt'), "VITA`n", [Text.UTF8Encoding]::new($false))
    Write-Output "$($game.id): $($game.title), $($report.Count) verified files"
}
