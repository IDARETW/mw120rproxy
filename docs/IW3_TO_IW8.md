# Convert an IW3 map to IW8

This guide converts a compiled **Call of Duty 4 (IW3)** multiplayer map into a playable **MW2019 Replay 1.20.4.7623265 (IW8)** map using MW120R. It uses `mp_4doffice` as an example; replace the map ID, title, and download path for another map.

The result requires the MW120R mod. It won't load in current retail MW2019 or another IW8 build.

The conversion has four steps:

1. Extract the CoD4 fastfile and textures with the patched OpenAssetTools Unlinker.
2. Convert the extracted geometry, materials, collision, and entities into a Replay map package.
3. Install the complete package into Replay.
4. Select the map in Local Play and start a match.

You don't need the original Radiant `.map` source to convert a downloaded map. For a map you're editing in Radiant, use the [Radiant build instructions](MAP_BUILDING.md#radiant-source-map).

## 1. Set up the tools

You'll need:

- Replay 1.20.4.7623265 with the MW120R proxy installed. See [Build and install](../README.md#build-and-install).
- CoD4 with Mod Tools installed, including the `main` and `raw` asset folders. Some custom maps reference stock models and materials from these folders.
- Python 3.10 or newer, Git, XMake, and Visual Studio C++ build tools with a Windows SDK.
- The patched OpenAssetTools Unlinker described below.
- Local Replay shader templates, generated once before converting maps.

Open PowerShell in the repository root. Copy the example configuration if you haven't already:

```powershell
Copy-Item local.env.example.ps1 local.env.ps1
notepad local.env.ps1
```

Set `MW120R_GAME` to your Replay directory and `MW120R_COD4` to your CoD4 directory. Save the file, then load it:

```powershell
. .\local.env.ps1
python -m pip install -r requirements.txt
.\build.ps1
```

Run `. .\local.env.ps1` again whenever you open a new PowerShell session. The map tools read these paths when they start.

### Build the patched Unlinker

The standard OpenAssetTools release doesn't include the world and collision exporters required by this converter. Use this version and apply the included extension:

```powershell
git clone https://github.com/Laupetin/OpenAssetTools.git external/OpenAssetTools
git -C external/OpenAssetTools checkout 7d027e8f89118196713e955b0e11f8404149c54d
python mw120rproxy/tools/oat/configure_exporters.py external/OpenAssetTools --with-wavelet
```

In the OpenAssetTools directory, run `generate.bat`. Open the generated Visual Studio solution and build **UnlinkerCli** with **Release / x86** selected. Follow that checkout's build instructions if it requests additional components.

By default, the map tools look for:

```text
external/OpenAssetTools/build/bin/Release_x86/Unlinker.exe
```

If your build puts it elsewhere, add the absolute path to `local.env.ps1` and reload the file:

```powershell
$env:MW120R_UNLINKER = 'D:\Tools\OpenAssetTools\build\bin\Release_x86\Unlinker.exe'
```

### Generate the Replay shader templates

Follow [Local Replay shader templates](MAP_BUILDING.md#local-replay-shader-templates). This step reads the required shaders from a running stock Shipment match and saves:

```text
custom_map_sources/mp_test/shaders/replay_static_world_techset.json
custom_map_sources/mp_test/dump/maps/mp/mp_test.d3dbsp.material.json
```

These templates are shared by the converters for all map IDs. Keep them even if you aren't converting `mp_test`. They contain locally extracted game data and are excluded from Git.

**Experimental:** the shader setup script's validation has automated tests, but extraction from a running game has not yet been tested.

Close Replay after this step and before installing custom maps.

## 2. Prepare the CoD4 map files

Extract the downloaded ZIP or RAR into a folder. Point the converter at the folder containing the actual `.ff`, not an outer folder or the archive itself:

```text
D:/Downloads/mp_4doffice/
    mp_4doffice.ff
    mp_4doffice.iwd
```

Keep any supplied IWD files beside the fastfile; they often contain custom textures. Some maps have several IWDs or no IWD at all.

The map ID must match the fastfile's name without `.ff`. Use the original ID throughout extraction, conversion, and installation. Changing `--title` changes the display name without renaming the map.

Set the paths for this conversion:

```powershell
$mapId = 'mp_4doffice'
$mapTitle = '4D Office'
$mapSource = 'D:/Downloads/mp_4doffice'
$mapDump = "custom_map_sources/$mapId/extracted"
```

## 3. Extract the IW3 assets

```powershell
python mw120rproxy/tools/extract_imported_map.py `
    --source $mapSource `
    --map $mapId `
    --output $mapDump
```

Wait for the `Extracted ...` message before continuing. The output directory must be new or empty. If you're re-extracting a map, use another directory and update `$mapDump`.

The extraction should include:

```text
maps/mp/mp_4doffice.d3dbsp.ents
maps/mp/mp_4doffice.d3dbsp.replay-world.json
maps/mp/mp_4doffice.d3dbsp.replay-collision.json
```

Materials, images, and model meshes are also exported as needed. The extraction directory contains `unlinker.log` and `extraction_report.json` for troubleshooting.

A missing `replay-world.json` or `replay-collision.json` usually means the wrong Unlinker was used. Rebuild it with the included exporters and extract into a fresh directory.

## 4. Build the IW8 package

```powershell
python mw120rproxy/tools/build_imported_map.py `
    --dump $mapDump `
    --map $mapId `
    --title $mapTitle `
    --credit 'Original map author'
```

Replace the credit with the name from the map's readme. The builder converts the map, creates its supporting files, and runs the package checks. Each run creates a new timestamped directory:

```text
custom_map_sources/mp_4doffice/builds/<timestamp>/
    package/
    build_report.json
    import_report.json
    convert.log
    shader.log
    validate.log
    layout.log
```

Continue only after the command finishes successfully and writes `build_report.json`. A partially created `package` directory doesn't mean the build completed.

The builder prints the full package path. Copy that path for installation:

```powershell
$mapPackage = 'D:/Path/To/Repository/custom_map_sources/mp_4doffice/builds/<timestamp>/package'
```

Replace the entire example with the path from your successful build.

### Deathrun maps, including SM64

For a map with `mp_jumper_spawn` and `mp_activator_spawn` entities, add `--spawn-mode deathrun-tdm`:

```powershell
python mw120rproxy/tools/build_imported_map.py `
    --dump $mapDump `
    --map $mapId `
    --title $mapTitle `
    --credit 'Original map author' `
    --spawn-mode deathrun-tdm
```

This turns those spawn points into TDM spawns. Deathrun scripts, traps, rounds, and other scripted gameplay aren't converted.

## 5. Install the converted map

With Replay closed:

```powershell
.\mw120rproxy\tools\deploy_custom_map.ps1 `
    -GameRoot $env:MW120R_GAME `
    -PackageDir $mapPackage `
    -Map $mapId
```

The installer checks the package and copies it to:

```text
<Replay directory>/mods/mw120r/maps/mp_4doffice/
```

The installed map includes `manifest.json` and five generated fastfiles:

```text
mp_4doffice.ff
srv_mp_4doffice.ff
eng_mp_4doffice.ff
ww_mp_4doffice.ff
techsets_mp_4doffice.ff
```

It also includes the supporting files listed in its manifest, such as `collision.bin`, `ladders.bin`, `glass.bin`, `footsteps.bin`, `ambient.bin`, and `preview.rgba`. Keep the complete package together. Copying only the main fastfile will leave the map incomplete.

Installing another map under a different ID adds it alongside your existing maps. Reinstalling the same ID replaces that map and backs up the previous version under `.proxy/backups`.

## 6. Play

1. Start Replay and enter **Multiplayer → Local Play**.
2. Open **Game Setup → Map** and select the converted map.
3. Choose **Team Deathmatch** and start with **zero bots**. The converter doesn't generate bot navigation.
4. Start the match, select a loadout, and spawn.

Check the spawn positions, floors, stairs, ladders, glass, foliage, and lighting. Return to Local Play before selecting another map. F6 opens the alternate map browser if you need to refresh the list.

## Common problems

| Problem | What to check |
| --- | --- |
| `<map_id>.ff` not found | `--source` must point directly to the folder containing that fastfile. Check the map ID and any nested download folders. |
| Missing world or collision JSON | Use the patched OpenAssetTools build, then extract into an empty directory. |
| Missing stock models, materials, or images | Check `MW120R_COD4`, the Mod Tools `raw` assets, and the map's IWD files. |
| Missing shader template JSON | Complete the one-time shader setup. All map builds use the templates under `custom_map_sources/mp_test`. |
| Missing required TDM entity | The map lacks the expected TDM spawns. Use `deathrun-tdm` only for maps with the supported deathrun spawn classes; otherwise add suitable spawns in the source map. |
| Deployment asks for `techsets_mp_frontend3.ff` | The Replay installation needs its stock frontend shader fastfile. A CoD4 fastfile cannot replace it. |
| Map doesn't appear | Check the installed `manifest.json`, restart Replay, and read `mw120rproxy.log` for a rejected package. |
| Loading error or crash | Check `convert.log` and `layout.log`, then the game's engine and exception logs. Keep the first error and stack trace. |

Map conversion supports static geometry, materials, collision, supported glass and ladders, and TDM spawns. CoD4 scripts, general destructible objects, and embedded model physics aren't automatically converted. Credit the map's author and check their redistribution terms before sharing a converted package.
