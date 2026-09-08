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
    & python -B (Join-Path $PSScriptRoot 'mw120rproxy/tests/test_shader_setup.py')
    if ($LASTEXITCODE -ne 0) {
        throw 'Shader setup validation tests failed'
    }
    & python -B (Join-Path $PSScriptRoot 'mw120rproxy/tests/test_door_data.py')
    if ($LASTEXITCODE -ne 0) {
        throw 'Door conversion tests failed'
    }
    & xmake -P (Join-Path $PSScriptRoot 'mw120rproxy/tests') -j 2 custom_map_tests
    if ($LASTEXITCODE -ne 0) {
        throw 'Test build failed'
    }
    Push-Location $PSScriptRoot
    try {
        & './test-out/custom_map_tests.exe'
        if ($LASTEXITCODE -ne 0) {
            throw 'Tests failed'
        }
        foreach ($target in @('door_tests', 'door_ui_tests')) {
            & xmake -P (Join-Path $PSScriptRoot 'mw120rproxy/tests') -j 2 $target
            if ($LASTEXITCODE -ne 0) {
                throw "Test build failed: $target"
            }
            & "./test-out/$target.exe"
            if ($LASTEXITCODE -ne 0) {
                throw "Tests failed: $target"
            }
        }
        & xmake -P (Join-Path $PSScriptRoot 'mw120rproxy/tests') -j 2 render_raster_tests
        if ($LASTEXITCODE -ne 0) {
            throw 'Shader raster test build failed'
        }
        foreach ($rasterMode in @('biased', 'shadow', 'ao', 'night', 'glass', 'baked', 'baked_half', 'sky_half')) {
            & './test-out/render_raster_tests.exe' 'mw120rproxy/tools/map_surface_realtime.hlsl' $rasterMode
            if ($LASTEXITCODE -ne 0) {
                throw "Shader raster test failed: $rasterMode"
            }
        }
    }
    finally {
        Pop-Location
    }
}
