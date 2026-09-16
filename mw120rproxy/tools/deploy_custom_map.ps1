[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [Alias('MapOutput')]
    [string]$PackageDir,

    [Parameter(Mandatory = $true)]
    [string]$GameRoot,

    [Parameter(Position = 1)]
    [string]$Map = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$gameRoot = (Resolve-Path -LiteralPath $GameRoot -ErrorAction Stop).Path
$gameExe = Join-Path $gameRoot 'game_dx12_ship_replay.exe'
$converter = Join-Path $repoRoot 'iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe'
$expectedGameMd5 = '1C238FE327F2ECC3B0DB924C5B425439'

if (-not (Test-Path -LiteralPath $gameRoot -PathType Container)) {
    throw "Game directory is missing: $gameRoot"
}
if (-not (Test-Path -LiteralPath $gameExe -PathType Leaf)) {
    throw "Replay executable is missing: $gameExe"
}
if ((Get-FileHash -LiteralPath $gameExe -Algorithm MD5).Hash -ne $expectedGameMd5) {
    throw 'The selected game executable is not Replay 1.20.4.7623265.'
}
if (-not (Test-Path -LiteralPath $converter -PathType Leaf)) {
    throw "ZoneTool executable is missing: $converter. Run .\build.ps1 first."
}

$packageSource = (Resolve-Path -LiteralPath $PackageDir -ErrorAction Stop).Path
if (-not (Test-Path -LiteralPath $packageSource -PathType Container)) {
    throw "Package directory is missing: $packageSource"
}

function Test-MapId([string]$Value) {
    return $Value -match '^mp_[a-z0-9_]{1,12}$'
}

if ([string]::IsNullOrWhiteSpace($Map)) {
    $folderName = Split-Path -Leaf $packageSource.TrimEnd('\')
    if (Test-MapId $folderName) {
        $Map = $folderName
    }
    else {
        $primaryFiles = @(Get-ChildItem -LiteralPath $packageSource -Filter 'mp_*.ff' -File |
                Where-Object { Test-MapId $_.BaseName })
        if ($primaryFiles.Count -eq 1) {
            $Map = $primaryFiles[0].BaseName
        }
        else {
            throw 'Supply -Map with the target mp_ map id; it could not be inferred from the package.'
        }
    }
}
if (-not (Test-MapId $Map)) {
    throw 'Invalid map id. Use lower-case mp_ names up to 15 characters.'
}

function Get-TargetGameProcess {
    $expected = [IO.Path]::GetFullPath($gameExe)
    Get-CimInstance -ClassName Win32_Process -Filter "Name = 'game_dx12_ship_replay.exe'" |
        Where-Object {
            (-not $_.ExecutablePath) -or [string]::Equals($_.ExecutablePath, $expected,
                [StringComparison]::OrdinalIgnoreCase)
        }
}

function Assert-GameClosed {
    if (Get-TargetGameProcess) {
        throw 'Close Replay before deployment. The script never starts or stops the game.'
    }
}

function Assert-GamePath([string]$Candidate) {
    $resolved = [IO.Path]::GetFullPath($Candidate)
    $root = $gameRoot.TrimEnd('\')
    if (-not ($resolved.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase))) {
        throw "Path outside the named game directory: $resolved"
    }

    $part = $resolved
    while ($part.Length -gt $root.Length) {
        if (Test-Path -LiteralPath $part) {
            $item = Get-Item -LiteralPath $part
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Reparse point in deployment path: $part"
            }
        }
        $part = Split-Path -Path $part -Parent
    }
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

Assert-GameClosed

$stamp = '{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'),
    ([Guid]::NewGuid().ToString('N').Substring(0, 8))
$mapsRoot = Join-Path $gameRoot 'mods\mw120r\maps'
$installed = Join-Path $mapsRoot $Map
$stage = Join-Path $mapsRoot ('.' + $Map + '.stage-' + $stamp)
$backup = Join-Path $gameRoot ('.proxy\backups\mw120r-map-' + $stamp)
$previous = Join-Path $backup $Map
foreach ($path in @($mapsRoot, $installed, $stage, $backup, $previous)) {
    Assert-GamePath $path
}

$names = @(
    "$Map.ff",
    "srv_$Map.ff",
    "eng_$Map.ff",
    "ww_$Map.ff",
    "techsets_$Map.ff"
)
if (Test-Path -LiteralPath (Join-Path $packageSource 'map.json') -PathType Leaf) {
    $names += 'map.json'
}

& $converter validate-output $packageSource $Map
if ($LASTEXITCODE -ne 0) {
    throw 'Source package validation failed.'
}

New-Item -ItemType Directory -Path $stage -Force | Out-Null
$files = foreach ($name in $names) {
    $source = Join-Path $packageSource $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Package file is missing: $source"
    }
    $item = Get-Item -LiteralPath $source
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "Reparse source file: $source"
    }
    $destination = Join-Path $stage $name
    Copy-Item -LiteralPath $source -Destination $destination
    $hash = Get-Sha256 $source
    if ((Get-Sha256 $destination) -ne $hash) {
        throw "Staged hash mismatch: $name"
    }
    [pscustomobject]@{
        Name   = $name
        Bytes  = $item.Length
        SHA256 = $hash
    }
}

& $converter validate-output $stage $Map
if ($LASTEXITCODE -ne 0) {
    throw 'Staged package validation failed.'
}

Assert-GameClosed
New-Item -ItemType Directory -Path $backup -Force | Out-Null
$movedOld = $false
$movedNew = $false
try {
    if (Test-Path -LiteralPath $installed) {
        Move-Item -LiteralPath $installed -Destination $previous
        $movedOld = $true
    }
    Move-Item -LiteralPath $stage -Destination $installed
    $movedNew = $true
    foreach ($file in $files) {
        $installedFile = Join-Path $installed $file.Name
        if ((Get-Sha256 $installedFile) -ne $file.SHA256) {
            throw "Installed hash mismatch: $($file.Name)"
        }
    }
}
catch {
    if ($movedNew -and (Test-Path -LiteralPath $installed)) {
        $failed = Join-Path $backup 'failed-new-package'
        Move-Item -LiteralPath $installed -Destination $failed
    }
    if ($movedOld -and (Test-Path -LiteralPath $previous)) {
        Move-Item -LiteralPath $previous -Destination $installed
    }
    throw
}

[pscustomobject]@{
    Destination       = $installed
    Source            = $packageSource
    Backup            = $backup
    Kind              = 'Replay unsigned five-fastfile custom-map package'
    LiveTestPerformed = $false
    GameStarted       = $false
    Files             = @($files)
} | ConvertTo-Json -Depth 6
