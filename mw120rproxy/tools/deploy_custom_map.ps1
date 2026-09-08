param(
    [Parameter(Mandatory = $true)][string]$PackageDir,
    [Parameter(Mandatory = $true)][string]$GameRoot,
    [string]$Map = 'mp_test'
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$gameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
New-Item -ItemType Directory -Path (Join-Path $repoRoot 'evidence') -Force | Out-Null
$gameExe = Join-Path $gameRoot 'game_dx12_ship_replay.exe'
if ($Map -notmatch '^mp_[a-z0-9_]{1,60}$') {
    throw 'Invalid map id.'
}
$packageSource = (Resolve-Path -LiteralPath $PackageDir).Path
$converter = Join-Path $repoRoot 'iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe'
& $converter validate-package $packageSource $Map
if ($LASTEXITCODE -ne 0) {
    throw 'Source package validation failed.'
}
$manifest = Get-Content -Raw -LiteralPath (Join-Path $packageSource 'manifest.json') | ConvertFrom-Json
if ($manifest.schema -ne 1 -or $manifest.id -cne $Map -or !$manifest.title -or $manifest.gametypes -notcontains 'tdm') {
    throw 'Invalid package manifest.'
}
$shaderDependency = $null
if ($manifest.shaderSource) {
    if ($manifest.shaderSource -cne 'mp_frontend3') {
        throw 'Unsupported Replay shader dependency.'
    }
    $shaderFile = Join-Path $gameRoot 'zone\techsets_mp_frontend3.ff'
    $shaderItem = Get-Item -LiteralPath $shaderFile
    if ($shaderItem.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw 'Shader dependency is a link.'
    }
    $shaderHeader = [IO.File]::ReadAllBytes($shaderFile)
    if ([Text.Encoding]::ASCII.GetString($shaderHeader, 0, 8) -cne 'IWffa100' -or
        [BitConverter]::ToUInt32($shaderHeader, 8) -ne 11 -or [BitConverter]::ToUInt32($shaderHeader, 12) -ne 0xFF7) {
        throw 'Shader dependency is not a stock Replay 1.20 fastfile.'
    }
    $shaderDependency = [pscustomobject]@{Path = $shaderFile
        Bytes                                  = $shaderItem.Length
        SHA256                                 = (Get-FileHash -LiteralPath $shaderFile).Hash
    }
}
if ($manifest.layout -in @('replay-1.20-minimal-v10', 'replay-1.20-bsp-v11')) {
    & python (Join-Path $PSScriptRoot 'verify_replay_map_layout.py') --game $gameExe --package $packageSource --map $Map
    if ($LASTEXITCODE -ne 0) {
        throw 'Replay asset layout verification failed.'
    }
}
function AssertGameClosed {
    $running = Get-CimInstance Win32_Process -Filter "Name = 'game_dx12_ship_replay.exe'" |
        Where-Object { !$_.ExecutablePath -or [string]::Equals($_.ExecutablePath, $gameExe, [StringComparison]::OrdinalIgnoreCase) }
    if ($running) {
        throw 'Close Replay before deployment. No process is started or stopped.'
    }
}
function AssertGamePath([string]$Candidate) {
    $resolved = [IO.Path]::GetFullPath($Candidate)
    if (!$resolved.StartsWith($gameRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path outside the named game directory: $resolved"
    }
    # Reject existing junction/symlink ancestors before directory moves.
    $part = $resolved
    while ($part.Length -gt $gameRoot.Length) {
        if (Test-Path -LiteralPath $part) {
            if ((Get-Item -LiteralPath $part).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Reparse point in deployment path: $part"
            }
        }
        $part = Split-Path -Path $part -Parent
    }
}
AssertGameClosed
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$mapsRoot = Join-Path $gameRoot 'mods\mw120r\maps'
$installed = Join-Path $mapsRoot $Map
$stage = Join-Path $mapsRoot ('.' + $Map + '.stage-' + $stamp)
$backup = Join-Path $gameRoot ('.proxy\backups\mw120r-map-' + $stamp)
$previous = Join-Path $backup $Map
foreach ($path in @($installed, $stage, $backup, $previous)) {
    AssertGamePath $path
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null
if ($manifest.glass -and $manifest.glass -cnotin @('panes-v1', 'panes-v2')) {
    throw 'Unsupported glass package.'
}
$names = @('manifest.json', "$Map.ff", "srv_$Map.ff", "eng_$Map.ff", "ww_$Map.ff", "techsets_$Map.ff")
if ($manifest.preview -eq 'rgba8-v1') {
    $names += 'preview.rgba'
}
if ($manifest.ambient -eq 'sh-probe-v1') {
    $names += 'ambient.bin'
}
if ($manifest.footsteps -eq 'triangles-v1') {
    $names += 'footsteps.bin'
}
if ($manifest.glass) {
    $names += 'glass.bin'
}
if ($manifest.collision) {
    if ($manifest.collision -cnotin @('boxes-v1', 'convex-v2')) {
        throw 'Unsupported collision package.'
    }
    $names += 'collision.bin'
}
if ($manifest.ladders) {
    if ($manifest.ladders -cnotin @('faces-v1', 'faces-v2')) {
        throw 'Unsupported ladder package.'
    }
    $names += 'ladders.bin'
}
$files = foreach ($name in $names) {
    $source = Join-Path $packageSource $name
    $destination = Join-Path $stage $name
    $item = Get-Item -LiteralPath $source
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "Reparse source file: $source"
    }
    Copy-Item -LiteralPath $source -Destination $destination
    $hash = (Get-FileHash -LiteralPath $source).Hash
    if ((Get-FileHash -LiteralPath $destination).Hash -ne $hash) {
        throw "Staged hash mismatch: $name"
    }
    [pscustomobject]@{Name = $name
        Bytes              = $item.Length
        SHA256             = $hash
    }
}
& $converter validate-package $stage $Map
if ($LASTEXITCODE -ne 0) {
    throw 'Staged package validation failed.'
}
AssertGameClosed
foreach ($path in @($installed, $stage, $backup, $previous)) {
    AssertGamePath $path
}
New-Item -ItemType Directory -Path $backup | Out-Null
$movedOld = $false
try {
    if (Test-Path -LiteralPath $installed) {
        Move-Item -LiteralPath $installed -Destination $previous
        $movedOld = $true
    }
    Move-Item -LiteralPath $stage -Destination $installed
    foreach ($file in $files) {
        if ((Get-FileHash -LiteralPath (Join-Path $installed $file.Name)).Hash -ne $file.SHA256) {
            throw "Installed hash mismatch: $($file.Name)"
        }
    }
}
catch {
    # Keep failed/new data for inspection and restore the complete prior directory.
    if (Test-Path -LiteralPath $installed) {
        $failed = Join-Path $backup 'failed-new-package'
        AssertGamePath $installed
        AssertGamePath $failed
        Move-Item -LiteralPath $installed -Destination $failed
    }
    if ($movedOld) {
        AssertGamePath $previous
        AssertGamePath $installed
        Move-Item -LiteralPath $previous -Destination $installed
    }
    throw
}
[pscustomobject]@{Destination = $installed
    Source                    = $packageSource
    Backup                    = $backup
    Kind                      = 'Replay unsigned IWC stored custom-map package'
    LiveTestPerformed         = $false
    GameStarted               = $false
    ShaderDependency          = $shaderDependency
    Files                     = @($files)
} |
    ConvertTo-Json -Depth 6 | Tee-Object -FilePath (Join-Path $repoRoot 'evidence\custom_map_package_deployment.json')
