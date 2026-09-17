# Build the pinned open-source reader. No game files are downloaded or copied.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SourceDir,
    [Parameter(Mandatory)][string]$OutputDir,
    [ValidateRange(1, 16)][int]$Jobs = 2
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$revision = 'd10b9cca0785f5d5e338eec3be2e170599f66d6c'
function Run([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
$sourcePath = [IO.Path]::GetFullPath($SourceDir)
$outputPath = [IO.Path]::GetFullPath($OutputDir)
if (-not (Test-Path -LiteralPath $sourcePath)) {
    Run git @('clone', '--filter=blob:none', '--no-checkout', 'https://github.com/IDARETW/atian-cod-tools.git', $sourcePath)
    Run git @('-C', $sourcePath, 'checkout', '--detach', $revision)
}
$head = & git -C $sourcePath rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $head -ne $revision) { throw "Use a fresh SourceDir or a checkout at $revision." }
Run git @('-C', $sourcePath, 'submodule', 'update', '--init', '--recursive', '--jobs', "$Jobs")
$patch = Join-Path $PSScriptRoot 'replay-skin.patch'
& git -C $sourcePath apply --reverse --check $patch 2>$null
if ($LASTEXITCODE -ne 0) {
    Run git @('-C', $sourcePath, 'apply', '--check', $patch)
    Run git @('-C', $sourcePath, 'apply', $patch)
}
$buildPath = Join-Path $sourcePath 'build-weapon-editor'
Run cmake @('-S', $sourcePath, '-B', $buildPath, '-A', 'x64', '-DNO_QT_BUILD=ON', '-DNO_OPEN_CL_BUILD=ON')
Run cmake @('--build', $buildPath, '--config', 'Release', '--target', 'AtianCodToolsCLI', '--parallel', "$Jobs", '--', '/nr:false')
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
foreach ($name in @('acts.exe', 'acts-common.dll')) {
    Copy-Item -LiteralPath (Join-Path $sourcePath "build\bin\Release\$name") -Destination (Join-Path $outputPath $name)
}
$dataPath = Join-Path $outputPath 'data\mw19'
New-Item -ItemType Directory -Path $dataPath -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $sourcePath 'config\data\mw19\schema.json') -Destination (Join-Path $dataPath 'schema.json')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'ACTS-LICENSE.md') -Destination $outputPath
Write-Host "Reader ready: $(Join-Path $outputPath 'acts.exe')"
