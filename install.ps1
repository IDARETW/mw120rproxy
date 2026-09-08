param(
    [Parameter(Mandatory = $true)][string]$GameRoot,
    [ValidateSet('debug', 'release')]
    [string]$Mode = 'release',
    [switch]$SkipBuild,
    [switch]$PreserveConfig
)

$ErrorActionPreference = 'Stop'

$repoRoot = $PSScriptRoot
$sourceRoot = Join-Path $repoRoot 'mw120rproxy'
$gameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
$gameExe = Join-Path $gameRoot 'game_dx12_ship_replay.exe'
$destination = Join-Path $gameRoot 'XInput9_1_0.dll'
$configName = if ($Mode -eq 'debug') {
    'Debug'
}
else {
    'Release'
}

function Get-TargetGameProcess {
    $expected = [IO.Path]::GetFullPath($gameExe)
    Get-CimInstance -ClassName Win32_Process -Filter "Name = 'game_dx12_ship_replay.exe'" |
        Where-Object {
            (-not $_.ExecutablePath) -or [string]::Equals($_.ExecutablePath, $expected,
                [StringComparison]::OrdinalIgnoreCase)
        }
}

if (-not (Test-Path -LiteralPath $sourceRoot)) {
    throw "Source root is missing: $sourceRoot"
}
if (-not (Test-Path -LiteralPath $gameExe)) {
    throw "Game executable is missing: $gameExe"
}

if (!$SkipBuild) {
    Push-Location $sourceRoot
    try {
        & xmake f -m $Mode -a x64 -y
        if ($LASTEXITCODE -ne 0) {
            throw "XMake configuration failed with exit code $LASTEXITCODE."
        }
        & xmake
        if ($LASTEXITCODE -ne 0) {
            throw "XMake build failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Pop-Location
    }
}

$artifact = Join-Path $sourceRoot ("xmake-out\x64\$configName\XInput9_1_0.dll")
if (-not (Test-Path -LiteralPath $artifact)) {
    throw "Build artifact is missing: $artifact"
}
if ((Get-Item -LiteralPath $artifact).Length -lt 64KB) {
    throw "Build artifact is too small: $artifact"
}

$evidence = Join-Path $repoRoot 'evidence'
& python -X utf8 (Join-Path $sourceRoot 'tools\verify_build.py') --dll $artifact --game $gameExe --out (Join-Path $evidence 'build_validation.json')
if ($LASTEXITCODE -ne 0) {
    throw 'Target/export verification failed; no deployment.'
}
if (Get-TargetGameProcess) {
    throw 'Close Replay before deploying. The script never starts or stops the game.'
}
$backup = Join-Path $gameRoot ('.proxy\backups\mw120r-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
foreach ($name in @('XInput9_1_0.dll', 'mw120rproxy.ini', 'mw120rproxy.identity.ini', 'mw120rproxy.log', 'mw120rproxy.trace.log', 'mw120rproxy.exceptions.log', 'mw120rproxy.engine.log')) {
    $old = Join-Path $gameRoot $name
    if (Test-Path -LiteralPath $old) {
        Copy-Item -LiteralPath $old -Destination (Join-Path $backup $name)
    }
}
$staged = Join-Path $gameRoot 'XInput9_1_0.dll.mw120r-stage'
Copy-Item -LiteralPath $artifact -Destination $staged -Force
if ((Get-FileHash -LiteralPath $staged).Hash -ne (Get-FileHash -LiteralPath $artifact).Hash) {
    throw 'Staging hash mismatch.'
}
if (Get-TargetGameProcess) {
    throw 'Replay started during staging; deployment deferred.'
}
try {
    if (Test-Path -LiteralPath $destination) {
        [IO.File]::Replace($staged, $destination, (Join-Path $backup 'replaced-XInput9_1_0.dll'))
    }
    else {
        [IO.File]::Move($staged, $destination)
    }
    if (!$PreserveConfig) {
        Copy-Item -LiteralPath (Join-Path $sourceRoot 'mw120rproxy.ini') -Destination (Join-Path $gameRoot 'mw120rproxy.ini') -Force
    }
    if ((Get-FileHash -LiteralPath $destination).Hash -ne (Get-FileHash -LiteralPath $artifact).Hash) {
        throw 'Deployed hash mismatch.'
    }
}
catch {
    $previous = Join-Path $backup 'XInput9_1_0.dll'
    if (Test-Path -LiteralPath $previous) {
        Copy-Item -LiteralPath $previous -Destination $destination -Force
    }
    $previousIni = Join-Path $backup 'mw120rproxy.ini'
    if (Test-Path -LiteralPath $previousIni) {
        Copy-Item -LiteralPath $previousIni -Destination (Join-Path $gameRoot 'mw120rproxy.ini') -Force
    }
    throw
}
[pscustomobject]@{Deployed = $destination
    SHA256                 = (Get-FileHash -LiteralPath $destination).Hash
    Backup                 = $backup
    GameStarted            = $false
} |
    ConvertTo-Json | Tee-Object -FilePath (Join-Path $evidence 'deployment.json')
Write-Host 'Deployed and verified. Start Replay yourself and test Multiplayer -> Local Play.'
