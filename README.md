# MW120R Custom Maps

This project is made open source with the goal of providing a modding base for others who want to play, experiment with, and iterate on the code. Later iterations will be updated in this repo as the days progress.

Load custom maps in **MW2019 Replay 1.20.4.7623265**. The repository includes normal Local Play map selection, a game console, and a native C++ map compiler that writes Replay fastfiles.

## Requirements

- Windows x64 and a complete Replay installation.
- Executable: `game_dx12_ship_replay.exe`, MD5 `1c238fe327f2ecc3b0db924c5b425439`.
- Visual Studio C++ build tools with a Windows SDK, and XMake 2.8 or newer.
- A converted MW120R map package to play a custom map. CoD4 `.ff` files can be passed directly to the converter, but they cannot be installed in Replay unchanged.

The repository contains source and tools. Game executables, stock fastfiles, shaders, and prebuilt DLLs are not bundled in the source checkout. A ready-to-install [Nuketown example map](docs/NUKETOWN_EXAMPLE.md) is available as a separate release download.

## Build and install

Open PowerShell in this repository, then build:

```powershell
.\build.ps1 -Tests
```

Outputs:

- `mw120rproxy/xmake-out/x64/Release/XInput9_1_0.dll`
- `iw8-zonetool/xmake-out/x64/Release/iw8-zonetool.exe`

Close Replay, then install into your game directory. Replace the example path below:

```powershell
.\install.ps1 -GameRoot 'D:\Games\Replay' -SkipBuild
```

The installer checks the game version, backs up any existing proxy and configuration, and copies `XInput9_1_0.dll` and `mw120rproxy.ini` beside the game executable. Replay must be closed during installation. Use `-PreserveConfig` when updating to keep your settings.

Start the game and enter **Multiplayer → Local Play**. Stock maps still require their original fastfiles to be installed.

## Install and play custom maps

Install the converter's five-fastfile output folder:

```powershell
.\mw120rproxy\tools\deploy_custom_map.ps1 `
    -GameRoot 'D:\Games\Replay' `
    -PackageDir 'D:\Maps\Office\package' `
    -Map mp_4doffice
```

Maps are installed under `mods/mw120r/maps/<map_id>/` in the game directory. The installer validates all five Replay fastfiles, verifies the copied hashes, and backs up the previous map folder in `.proxy/backups`.

1. Start Replay and open **Multiplayer → Local Play → Game Setup → Map**.
2. Select your custom map. Its title and artwork should appear in the lobby and loading screen. A packaged `compass_map_<map_id>` asset also appears on the in-game HUD minimap.
3. Start **Team Deathmatch with zero bots** for the first test, select a loadout, and spawn.
4. Return to Local Play before switching maps. Stock maps can be selected from the same menu.

Office, Nuketown, `mp_test`, and Super Mario 64 have been tested. Download the [Nuketown example package and usage instructions](docs/NUKETOWN_EXAMPLE.md) to try a converted map without running the conversion tools. The other maps are not included.

- [Convert an IW3 fastfile](docs/IW3_TO_IW8.md) — pass a generated CoD4 map `.ff` directly to the native converter with no manual export or Python dependency.
- [Build Replay fastfiles](iw8-zonetool/README.md) — convert an IW3 fastfile or compile a prepared map dump. The output directory contains only the five map fastfiles.
- [Map dump format](iw8-zonetool/docs/INPUT_FORMAT.md) — required geometry, materials, collision, lighting, entities, and the optional HUD minimap asset.
- [Imported doors](docs/DOORS.md) — open, close, and bash compatible brush doors.
- [Custom-map lighting](docs/LIGHTING.md) — rebuild a map with native sun shadows and adjust baked-light exposure.

## Controls and configuration

| Control | Action |
| --- | --- |
| `~` / grave or **F7** | Open/close the native game console |
| **F6** | Open the alternate custom-map browser |
| Up/Down, Enter | Browse/select in the F6 browser |
| R / Backspace in F6 | Refresh the map list / clear the custom-map selection |
| `noclip` or `mw_noclip` in console | Toggle local-player noclip in Local Play |

The console accepts supported dvars by their readable names. `lui_dev_features_enabled` is enabled at startup. Settings for the console, custom maps, local authentication, and logging are in `mw120rproxy.ini`.

Stale safe-mode markers are cleared at startup to prevent the safe-mode prompt after a crash.

## Supported features and limits

- CoD4 world geometry, static models, TDM spawns, and loadout selection. World collision is serialized into `srv_<map>.ff` as native Replay Havok data, including floor-material and contents tags. Replay registers it through its normal world-collision path for movement, footsteps, and bullet traces.
- Source sun direction and color, adjustable sunlight intensity, directional baked lightmaps, and native sun-shadow reception. Source static shadows remain visible beyond the nearby realtime shadow range. The skybox is visual and does not override scene lighting.
- Transparent foliage and decals, normal/specular material channels, climbable ladders, ladder sounds, and footsteps matched to floor materials.
- Breakable glass with sound when shot, hit with melee, or mantled through. Glass debris is still being investigated.
- Imported brush doors with Use to open/close, melee and sprint bashing, moving collision, and the game's interaction popup. Existing packages need to be rebuilt to include door data.
- Custom map names, menu previews, loading-screen images, and native HUD minimap materials.

CoD4 scripts, general destructible objects, and bot navigation are not supported. Model physics is not imported automatically; use clip brushes for solid props. Test converted maps in-game, especially maps with unusual materials or scripted objects.

Door conversion reads a limited set of authored brush-mover definitions; it does not run CoD4 scripts or add native door hand animations. Opening, closing, and the interaction prompt have been tested on Super Mario 64. Glass debris remains under investigation.

## Troubleshooting

- **No custom entry:** install the whole package, check `manifest.json` and its ID, then restart or refresh F6. Read the log for rejected packages.
- **Missing `.ff`:** distinguish missing stock game data from an incomplete custom package. Keep all generated companion fastfiles together.
- **Crash or loading loop:** check `mw120rproxy.log`, `mw120rproxy.engine.log`, `mw120rproxy.trace.log`, and `mw120rproxy.exceptions.log`. For a bug report, include the map name, steps to reproduce, the first error and stack trace, and the map's `build_report.json`.
- **No tilde console:** try F7; keyboard layouts differ.
- **Updating:** close Replay before replacing the DLL or packages. Do not mix files from different map builds.

To uninstall, close Replay and remove this mod's `XInput9_1_0.dll` and `mw120rproxy.ini`, or restore the backed-up proxy if you had one previously. Custom packages live in `mods/mw120r/maps`; identity and logs are local files next to the executable.

See [development and tests](docs/DEVELOPMENT.md) and [third-party notices](THIRD_PARTY_NOTICES.md).
