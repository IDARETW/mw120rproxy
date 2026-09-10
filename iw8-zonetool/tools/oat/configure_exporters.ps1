[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [string]$Checkout
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$expectedCommit = '7d027e8f89118196713e955b0e11f8404149c54d'
$root = (Resolve-Path -LiteralPath $Checkout).Path
$actualCommit = (& git -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $expectedCommit) {
    throw "Expected OpenAssetTools v0.33.0 $expectedCommit, found $actualCommit"
}

$iw3Directory = Join-Path $root 'src\ObjWriting\Game\IW3'
$writerPath = Join-Path $iw3Directory 'ObjWriterIW3.cpp'
$writer = Get-Content -Raw -LiteralPath $writerPath

$include = '#include "ReplayMapDumpers.h"'
if (-not $writer.Contains($include)) {
    $anchor = '#include "ObjWriterIW3.h"'
    if (($writer.Split($anchor).Count - 1) -ne 1) {
        throw 'Could not find the IW3 writer include anchor'
    }
    $writer = $writer.Replace($anchor, "$anchor`r`n$include")
}

$clipMap = 'RegisterAssetDumper(std::make_unique<replay_export::Collision<IW3::AssetClipMap>>());'
if (-not $writer.Contains($clipMap)) {
    $anchor = '// REGISTER_DUMPER(AssetDumperClipMap)'
    if (($writer.Split($anchor).Count - 1) -ne 1) {
        throw 'Could not find the IW3 clip-map registration anchor'
    }
    $writer = $writer.Replace($anchor, $clipMap)
}

$clipMapPvs = 'RegisterAssetDumper(std::make_unique<replay_export::Collision<IW3::AssetClipMapPvs>>());'
if (-not $writer.Contains($clipMapPvs)) {
    $writer = $writer.Replace($clipMap, "$clipMap`r`n    $clipMapPvs")
}

$world = 'RegisterAssetDumper(std::make_unique<replay_export::World>());'
if (-not $writer.Contains($world)) {
    $anchor = '// REGISTER_DUMPER(AssetDumperGfxWorld)'
    if (($writer.Split($anchor).Count - 1) -ne 1) {
        throw 'Could not find the IW3 world registration anchor'
    }
    $writer = $writer.Replace($anchor, $world)
}

Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'ReplayMapDumpers.h') `
    -Destination (Join-Path $iw3Directory 'ReplayMapDumpers.h') -Force
Set-Content -LiteralPath $writerPath -Value $writer -NoNewline

Write-Host "Configured the IW3 Replay map exporters in $root"
Write-Host 'Rebuild the OpenAssetTools Unlinker target, then pass Unlinker.exe to iw8-zonetool.'
