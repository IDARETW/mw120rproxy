# Development

The project has two build targets:

- `mw120rproxy` — the XInput proxy DLL loaded by Replay.
- `iw8-zonetool` — the command-line map converter.

Build both targets and run the custom-map tests:

```powershell
.\build.ps1 -Tests
```

Verify a DLL against your game:

```powershell
python mw120rproxy/tools/verify_build.py `
    --dll mw120rproxy/xmake-out/x64/Release/XInput9_1_0.dll `
    --game 'D:/Games/Replay/game_dx12_ship_replay.exe' `
    --out evidence/build-validation.json
```
