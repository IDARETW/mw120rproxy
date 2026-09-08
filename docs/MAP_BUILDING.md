# Build or import a map

Run these commands from the repository root in the same PowerShell session. You'll need CoD4 with Mod Tools installed and the supported Replay build.

## One-time paths and tools

```powershell
Copy-Item local.env.example.ps1 local.env.ps1
# Edit local.env.ps1 to point to your Replay and CoD4 installations.
. .\local.env.ps1
python -m pip install -r requirements.txt
.\build.ps1
```

`MW120R_COD4` must point to a CoD4 installation with Mod Tools merged into it: `raw`, `main`, `map_source`, `zone_source`, and `bin` containing `cod4map.exe`, the x86 `cod4rad.exe`, and `linker_pc.exe`. Install/configure Radiant separately for that game directory. Models, materials, and images used by the map must be available in the raw assets or source map dump.

### Patched OpenAssetTools

The converter needs world and collision data that standard Unlinker builds don't export. Check out this OpenAssetTools version and apply the included exporter:

```powershell
git clone https://github.com/Laupetin/OpenAssetTools.git external/OpenAssetTools
git -C external/OpenAssetTools checkout 7d027e8f89118196713e955b0e11f8404149c54d
python mw120rproxy/tools/oat/configure_exporters.py external/OpenAssetTools --with-wavelet
```

Follow that checkout's Windows build instructions: run its `generate.bat`, then build **UnlinkerCli**, **Release**, **x86** in the generated Visual Studio solution. The expected output is `external/OpenAssetTools/build/bin/Release_x86/Unlinker.exe`. Set `MW120R_UNLINKER` to a different absolute output path if necessary. The extension and its GPL license are included under `mw120rproxy/tools/oat`.

### Local Replay shader templates

Map rendering combines Replay's vertex and depth shaders with a custom pixel shader. Extract the required Replay shaders once before building maps:

1. Install the proxy and start a **stock Shipment Local Play match**.
2. While it is running, execute:

```powershell
$replayProcess = Get-Process -Name game_dx12_ship_replay
python mw120rproxy/tools/setup_shader_templates.py `
    --game $env:MW120R_GAME --pid $replayProcess.Id
```

The script reads the shaders from Replay and checks their hashes before saving them. If Replay is running as administrator, run PowerShell as administrator too. A missing-shader error means the required shaders haven't loaded; make sure you're in a Shipment match.

3. Close Replay before building/deploying maps.

The script creates these files:

- `custom_map_sources/mp_test/shaders/replay_static_world_techset.json`
- `custom_map_sources/mp_test/dump/maps/mp/mp_test.d3dbsp.material.json`

Keep them for future builds. The directory is ignored by Git.

**Experimental:** shader validation has automated tests, but extraction from a running game has not yet been tested.

## Radiant source map

Save the map under your CoD4 installation's `map_source` directory. Radiant builds currently use the `mp_test` map slot in Replay, regardless of the source filename.

```powershell
python mw120rproxy/tools/build_mp_test.py `
    --source "$env:MW120R_COD4/map_source/mp_test.map" `
    --cod4 $env:MW120R_COD4 `
    --replay "$env:MW120R_GAME/game_dx12_ship_replay.exe" `
    --deploy
```

Omit `--deploy` to build without installing. The builder compiles the geometry and lighting, converts models and materials, and checks the finished map files. Each build gets a new folder under `custom_map_sources/mp_test/builds/`.

- Include `mp_tdm_spawn`, `mp_tdm_spawn_allies_start`, and `mp_tdm_spawn_axis_start`; place spawns clear of walls and above the floor.
- Place existing CoD4 models with Radiant's Model Browser / `misc_model`. `origin`, `angles`, and a positive uniform `modelscale` are supported.
- Add clip brushes where static props need collision. Embedded model collision is not automatically imported.
- Keep map enclosure/sky brushes. The default sky image is `chechnya_ft`; use `--sky <cubemap_image_name>` for a different six-face CoD4 sky image.
- Keep compiler, conversion, and validation logs. Compiler errors are fatal even when a tool exits with status zero.

## Downloaded CoD4 map

For the full setup and conversion walkthrough, see [Convert an IW3 map to IW8](IW3_TO_IW8.md). The commands below are a quick reference once the tools and shader templates are ready.

Use a directory containing `<map_id>.ff` and its IWD archives. IDs must match `mp_` followed by lowercase letters, numbers, or underscores.

```powershell
python mw120rproxy/tools/extract_imported_map.py `
    --source 'D:/Downloads/mp_4doffice' `
    --map mp_4doffice `
    --output custom_map_sources/mp_4doffice/extracted

python mw120rproxy/tools/build_imported_map.py `
    --dump custom_map_sources/mp_4doffice/extracted `
    --map mp_4doffice --title '4D Office' --credit 'Original map author'
```

Use the resulting `package` path printed by the builder:

```powershell
.\mw120rproxy\tools\deploy_custom_map.ps1 `
    -GameRoot $env:MW120R_GAME -Map mp_4doffice `
    -PackageDir 'D:/Path/To/Build/package'
```

For maps using `mp_jumper_spawn` / `mp_activator_spawn`, such as the imported SM64 deathrun map, pass `--spawn-mode deathrun-tdm` to the builder. This translates spawn points for TDM; it does not implement deathrun scripts.

Use an empty output directory for each extraction. Credit the original map author and check their redistribution terms before sharing a converted map.

## Artwork

Imported maps use `images/loadscreen_<map_id>.iwi` from their extraction directory when present. The builder stretches that image to 1024×576, including horizontal stretching. Otherwise it generates an overview from the compiled map geometry. The resulting `preview.rgba` serves both the lobby and loading UI. Rebuild after changing the source artwork.
