# Development

The project has two build targets:

- `mw120rproxy` — the XInput proxy DLL loaded by Replay.
- `iw8-zonetool` — the command-line map converter.

Build both targets and run the custom-map tests:

```powershell
.\build.ps1 -Tests
```

This builds both targets and runs package, door, and shader checks. The lighting
tests use the Windows software renderer and do not launch Replay. See
[Custom-map lighting](LIGHTING.md) for rebuilding and testing map packages.

Verify a DLL against your game:

```powershell
python mw120rproxy/tools/verify_build.py `
    --dll mw120rproxy/xmake-out/x64/Release/XInput9_1_0.dll `
    --game 'D:/Games/Replay/game_dx12_ship_replay.exe' `
    --out evidence/build-validation.json
```

## Formatting

Use four spaces for indentation and a 100-column limit. Keep control-flow blocks
and multi-statement functions on separate lines.

- C++ uses the repository's `.clang-format` settings (clang-format 22).
- Python uses Black with the settings in `pyproject.toml`.
- PowerShell uses `PSScriptAnalyzerSettings.psd1` with `Invoke-Formatter`.

Format a C++ file or the Python tools:

```powershell
clang-format -i mw120rproxy/custom_maps.cpp
python -m black mw120rproxy/tools mw120rproxy/tests
```

Leave bundled third-party code in its original style.

The additional tests under `mw120rproxy/tools/test_*.py` include checks against
local CoD4 assets and extracted Office/Nuketown maps. Those fixtures and the
OpenAssetTools ImageConverter must be available to run the full discovery suite.
They are not included in the repository.
