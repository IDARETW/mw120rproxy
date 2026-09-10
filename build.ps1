param([ValidateSet('debug', 'release')][string]$Mode = 'release', [switch]$Tests)
$ErrorActionPreference = 'Stop'
foreach ($project in @('mw120rproxy', 'iw8-zonetool')) {
    Push-Location (Join-Path $PSScriptRoot $project)
    try {
        & xmake f -m $Mode -a x64 -y
        if ($LASTEXITCODE -ne 0) {
            throw "Configuration failed: $project"
        }
        & xmake -j 2
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed: $project"
        }
    }
    finally {
        Pop-Location
    }
}
if ($Tests) {
    $testProject = Join-Path $PSScriptRoot 'mw120rproxy/tests'
    & xmake f -P $testProject -m $Mode -a x64 -y
    if ($LASTEXITCODE -ne 0) {
        throw 'Test configuration failed'
    }
    & xmake -P $testProject -j 2 custom_map_tests
    if ($LASTEXITCODE -ne 0) {
        throw 'Test build failed'
    }
    & (Join-Path $PSScriptRoot 'test-out/custom_map_tests.exe')
    if ($LASTEXITCODE -ne 0) {
        throw 'Tests failed'
    }
}
