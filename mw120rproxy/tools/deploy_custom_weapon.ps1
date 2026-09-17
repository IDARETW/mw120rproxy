# Deploy one standalone custom weapon and its supporting DLL. Never starts/stops Replay.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameRoot,
    [Parameter(Mandatory)][string]$PackageDir,
    [Parameter(Mandatory)][string]$ProxyPath,
    [string]$WeaponName
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-PlainPath([string]$Path) {
    $current = [IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Deployment path contains a reparse point: $current"
            }
        }
        $current = [IO.Path]::GetDirectoryName($current)
    }
}

$gameDirectory = (Resolve-Path -LiteralPath $GameRoot).ProviderPath
$packageDirectory = (Resolve-Path -LiteralPath $PackageDir).ProviderPath
if (-not $WeaponName) {
    $manifests = @(Get-ChildItem -LiteralPath $packageDirectory -Filter 'iw8_cw_*.weapon.json' -File)
    if ($manifests.Count -ne 1) { throw 'Specify -WeaponName when the package directory does not contain exactly one weapon manifest.' }
    $WeaponName = $manifests[0].Name.Replace('.weapon.json', '')
}
if ($WeaponName -notmatch '^iw8_cw_[a-z0-9_]{1,41}$' -or $WeaponName.EndsWith('_mp')) { throw 'Invalid custom weapon base name.' }
$zoneNames = @(foreach ($group in @($WeaponName, ($WeaponName + '_common'))) {
    foreach ($prefix in @('', 'techsets_', 'ww_', 'eng_')) { $prefix + $group }
})
$manifest = Join-Path $packageDirectory ($WeaponName + '.weapon.json')
$metadata = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
$supportedFormats = @('replay-custom-weapon-v2', 'replay-custom-weapon-v3')
if ($metadata.format -notin $supportedFormats -or $metadata.base -ne $WeaponName -or
    $metadata.asset -ne ($WeaponName + '_mp') -or $metadata.attachments -isnot [bool] -or
    $metadata.loadout_slot -lt 61 -or $metadata.loadout_slot -gt 254) { throw 'Invalid weapon registration metadata.' }
$proxy = (Resolve-Path -LiteralPath $ProxyPath).ProviderPath
$exe = Join-Path $gameDirectory 'game_dx12_ship_replay.exe'
Assert-PlainPath $gameDirectory
if ((Get-FileHash -LiteralPath $exe -Algorithm MD5).Hash -ne '1C238FE327F2ECC3B0DB924C5B425439') {
    throw 'Replay executable does not match the analyzed 1.20 build.'
}
if (Get-Process -Name game_dx12_ship_replay -ErrorAction SilentlyContinue) {
    throw 'Close Replay before deploying the custom weapon package.'
}
foreach ($zoneName in $zoneNames) {
    $header = [byte[]]::new(20)
    $package = Join-Path $packageDirectory ($zoneName + '.ff')
    $stream = [IO.File]::OpenRead($package)
    try { $read = $stream.Read($header, 0, $header.Length) } finally { $stream.Dispose() }
    if ($read -ne 20 -or [BitConverter]::ToString($header) -ne '49-57-66-66-63-31-30-30-0B-00-00-00-F7-0F-00-00-00-00-00-00') {
        throw "Zone is not an unsigned Replay 1.20 stored fastfile: $zoneName"
    }
}
$tag = 'weapon-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$backup = Join-Path $gameDirectory ".proxy\backups\$tag"
$zoneDirectory = Join-Path $gameDirectory 'zone'
Assert-PlainPath $backup
Assert-PlainPath $zoneDirectory
New-Item -ItemType Directory -Path $backup -Force | Out-Null
New-Item -ItemType Directory -Path $zoneDirectory -Force | Out-Null
$items = @(
    @{ Source = $manifest; Target = (Join-Path $zoneDirectory ($WeaponName + '.weapon.json')) },
    @{ Source = $proxy; Target = (Join-Path $gameDirectory 'XInput9_1_0.dll') }
)
foreach ($zoneName in $zoneNames) {
    $items += @{ Source = (Join-Path $packageDirectory ($zoneName + '.ff'));
                 Target = (Join-Path $zoneDirectory ($zoneName + '.ff')) }
}
foreach ($item in $items) {
    Assert-PlainPath $item.Target
    $item.Existed = Test-Path -LiteralPath $item.Target
    $item.Backup = Join-Path $backup ([IO.Path]::GetFileName($item.Target))
    $item.Stage = $item.Target + '.' + $tag + '.stage'
    $item.Hash = (Get-FileHash -LiteralPath $item.Source -Algorithm SHA256).Hash
    $item.Replaced = $false
    if ($item.Existed) {
        Copy-Item -LiteralPath $item.Target -Destination $item.Backup
        $item.PreviousHash = (Get-FileHash -LiteralPath $item.Backup -Algorithm SHA256).Hash
    }
}
try {
    foreach ($item in $items) {
        Copy-Item -LiteralPath $item.Source -Destination $item.Stage
        if ((Get-FileHash -LiteralPath $item.Stage -Algorithm SHA256).Hash -ne $item.Hash) {
            throw "Staged hash mismatch: $($item.Target)"
        }
    }
    foreach ($item in $items) {
        if ((Test-Path -LiteralPath $item.Target) -ne $item.Existed -or
            ($item.Existed -and (Get-FileHash -LiteralPath $item.Target -Algorithm SHA256).Hash -ne $item.PreviousHash)) {
            throw "Destination changed during staging; deployment cancelled: $($item.Target)"
        }
    }
    if (Get-Process -Name game_dx12_ship_replay -ErrorAction SilentlyContinue) {
        throw 'Replay started during staging; deployment cancelled.'
    }
    foreach ($item in $items) {
        if ($item.Existed) { [IO.File]::Replace($item.Stage, $item.Target, $item.Backup) }
        else { [IO.File]::Move($item.Stage, $item.Target) }
        $item.Replaced = $true
        if ((Get-FileHash -LiteralPath $item.Target -Algorithm SHA256).Hash -ne $item.Hash) {
            throw "Deployed hash mismatch: $($item.Target)"
        }
    }
    $report = [ordered]@{ BackupDirectory = $backup; GameLaunched = $false; Files = @(
        foreach ($item in $items) {
            [ordered]@{ Target = $item.Target; SHA256 = $item.Hash; PreviouslyExisted = $item.Existed }
        }
    ) }
    $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $backup 'deployment.json') -Encoding utf8
    $report | ConvertTo-Json -Depth 5
} catch {
    foreach ($item in $items) {
        if ($item.Replaced) {
            if ($item.Existed) { Copy-Item -LiteralPath $item.Backup -Destination $item.Target -Force }
            else { Remove-Item -LiteralPath $item.Target }
        }
    }
    throw
} finally {
    foreach ($item in $items) {
        if (Test-Path -LiteralPath $item.Stage) { Remove-Item -LiteralPath $item.Stage }
    }
}
