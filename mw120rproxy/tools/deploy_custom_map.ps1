param(
    [Parameter(Mandatory = $true)][string]$GameRoot,
    [Parameter(Mandatory = $true)][string]$MapOutput
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$gameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
$mapOutput = (Resolve-Path -LiteralPath $MapOutput).Path
$gameExe = Join-Path $gameRoot 'game_dx12_ship_replay.exe'
$converter = Join-Path $repoRoot 'iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe'

$mainZones = @(Get-ChildItem -LiteralPath $mapOutput -File -Filter 'mp_*.ff')
if ($mainZones.Count -ne 1) {
    throw 'Map output must contain exactly one primary mp_<name>.ff file.'
}
$Map = [IO.Path]::GetFileNameWithoutExtension($mainZones[0].Name)
if ($Map -notmatch '^mp_[a-z0-9_]{1,60}$') {
    throw 'Primary fastfile has an invalid map id.'
}
if (-not (Test-Path -LiteralPath $gameExe)) {
    throw "Game executable is missing: $gameExe"
}
if ((Get-FileHash -LiteralPath $gameExe -Algorithm MD5).Hash -ne
    '1C238FE327F2ECC3B0DB924C5B425439') {
    throw 'The selected game executable is not Replay 1.20.4.7623265.'
}
if (-not (Test-Path -LiteralPath $converter)) {
    throw 'Build iw8-zonetool before installing a map.'
}

function Test-NativeZones([string]$Directory) {
    $check = Join-Path ([IO.Path]::GetTempPath()) ('mw120r-zones-' + [Guid]::NewGuid())
    New-Item -ItemType Directory -Path $check | Out-Null
    try {
        foreach ($name in @("$Map.ff", "srv_$Map.ff", "eng_$Map.ff", "ww_$Map.ff",
                "techsets_$Map.ff")) {
            Copy-Item -LiteralPath (Join-Path $Directory $name) -Destination $check
        }
        $metadata = Join-Path $Directory 'map.json'
        if (Test-Path -LiteralPath $metadata) {
            Copy-Item -LiteralPath $metadata -Destination $check
        }
        & $converter validate-output $check $Map
        return $LASTEXITCODE -eq 0
    }
    finally {
        [IO.Directory]::Delete($check, $true)
    }
}

if (-not (Test-NativeZones $mapOutput)) {
    throw 'Map output validation failed.'
}

function Assert-GameClosed {
    $expected = [IO.Path]::GetFullPath($gameExe)
    $running = Get-CimInstance Win32_Process -Filter "Name = 'game_dx12_ship_replay.exe'" |
        Where-Object {
            (-not $_.ExecutablePath) -or [string]::Equals($_.ExecutablePath, $expected,
                [StringComparison]::OrdinalIgnoreCase)
        }
    if ($running) {
        throw 'Close Replay before installing a map.'
    }
}

function Assert-GamePath([string]$Candidate) {
    $resolved = [IO.Path]::GetFullPath($Candidate)
    if (-not $resolved.StartsWith($gameRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path outside the selected game directory: $resolved"
    }
}

Assert-GameClosed
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$mapsRoot = Join-Path $gameRoot 'mods\mw120r\maps'
$installed = Join-Path $mapsRoot $Map
$stage = Join-Path $mapsRoot ('.' + $Map + '.stage-' + $stamp)
$backup = Join-Path $gameRoot ('.proxy\backups\mw120r-map-' + $stamp)
$previous = Join-Path $backup $Map
foreach ($path in @($installed, $stage, $backup, $previous)) {
    Assert-GamePath $path
}

New-Item -ItemType Directory -Path $stage -Force | Out-Null
$manifestPath = Join-Path $mapOutput 'manifest.json'
if (Test-Path -LiteralPath $manifestPath) {
    $manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
    if ($manifest.schema -ne 1 -or $manifest.id -cne $Map -or !$manifest.title -or
        $manifest.gametypes -notcontains 'tdm') {
        throw 'Invalid package manifest.'
    }
    $names = @(Get-ChildItem -File -LiteralPath $mapOutput | ForEach-Object Name)
}
else {
    $names = @("$Map.ff", "srv_$Map.ff", "eng_$Map.ff", "ww_$Map.ff", "techsets_$Map.ff")
    $metadataPath = Join-Path $mapOutput 'map.json'
    if (Test-Path -LiteralPath $metadataPath) {
        $metadata = Get-Content -Raw -LiteralPath $metadataPath | ConvertFrom-Json
        if ($metadata.id -and $metadata.id -cne $Map) {
            throw 'map.json id must match the selected map.'
        }
        $names += 'map.json'
    }
}
$files = foreach ($name in $names) {
    $source = Join-Path $mapOutput $name
    $destination = Join-Path $stage $name
    $item = Get-Item -LiteralPath $source
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "Map output file is a link: $source"
    }
    Copy-Item -LiteralPath $source -Destination $destination
    $hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $hash) {
        throw "Staged hash mismatch: $name"
    }
    [pscustomobject]@{ Name = $name; Bytes = $item.Length; SHA256 = $hash }
}

if (-not (Test-NativeZones $stage)) {
    throw 'Staged map output validation failed.'
}

Assert-GameClosed
New-Item -ItemType Directory -Path $backup | Out-Null
$movedOld = $false
try {
    if (Test-Path -LiteralPath $installed) {
        Move-Item -LiteralPath $installed -Destination $previous
        $movedOld = $true
    }
    Move-Item -LiteralPath $stage -Destination $installed
    foreach ($file in $files) {
        $installedFile = Join-Path $installed $file.Name
        if ((Get-FileHash -LiteralPath $installedFile -Algorithm SHA256).Hash -ne $file.SHA256) {
            throw "Installed hash mismatch: $($file.Name)"
        }
    }
}
catch {
    if ($movedOld -and -not (Test-Path -LiteralPath $installed)) {
        Move-Item -LiteralPath $previous -Destination $installed
    }
    throw
}

[pscustomobject]@{
    Destination = $installed
    Source = $mapOutput
    Backup = $backup
    Files = @($files)
} | ConvertTo-Json -Depth 4
