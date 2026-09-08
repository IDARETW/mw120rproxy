# MW120R Custom Maps

This project is made open source with the goal of providing a modding base for others who want to play, experiement with, and iterate on the code. Later iterations will be updated in this repo as the days progress.

Load CoD4 custom maps in **MW2019 Replay 1.20.4.7623265**. Includes normal Local Play map selection, a game console, and tools for converting downloaded maps or building maps from Radiant.

## Requirements

- Windows x64 and a complete Replay installation.
- Executable: `game_dx12_ship_replay.exe`, MD5 `1c238fe327f2ecc3b0db924c5b425439`.
- Visual Studio C++ build tools with a Windows SDK, and XMake 2.8 or newer.
- Python 3.10 or newer.
- A converted MW120R map package to play a custom map. Original CoD4 `.ff` files cannot be installed directly.

The repository contains source and tools. Game executables, stock fastfiles, shaders, downloaded maps, and prebuilt DLLs are not bundled.

## Build and install

Open PowerShell in this repository. Install the Python dependencies, then build:

```powershell
python -m pip install -r requirements.txt
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

Install the complete converted map folder, including `manifest.json` and all supporting files:

```powershell
.\mw120rproxy\tools\deploy_custom_map.ps1 `
    -GameRoot 'D:\Games\Replay' `
    -PackageDir 'D:\Maps\Office\package' `
    -Map mp_4doffice
```

Maps are installed under `mods/mw120r/maps/<map_id>/` in the game directory. The installer checks the map files and required stock shaders before installing. Previous versions are backed up in `.proxy/backups`.

1. Start Replay and open **Multiplayer → Local Play → Game Setup → Map**.
2. Select your custom map. Its title and artwork should appear in the lobby and loading screen.
3. Start **Team Deathmatch with zero bots** for the first test, select a loadout, and spawn.
4. Return to Local Play before switching maps. Stock maps can be selected from the same menu.

Office, Nuketown, `mp_test`, and Super Mario 64 have been tested. These maps are not included.

- [Convert an IW3 map to IW8](docs/IW3_TO_IW8.md) — extract a downloaded CoD4 map, convert it, and install it in Replay.
- [Build a map from Radiant](docs/MAP_BUILDING.md#radiant-source-map) — compile your own map source and static models.

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

- CoD4 world geometry, static models, collision, TDM spawns, and loadout selection.
- Baked lighting and static shadows, the map's sun direction, and ambient lighting based on the skybox.
- Transparent foliage, blended decals, climbable ladders, ladder sounds, and footsteps matched to floor materials.
- Breakable glass with sound and debris effects when shot, hit with melee, or mantled through.
- Custom map names, menu previews, and loading-screen images.

CoD4 scripts, general destructible objects, bot navigation, player shadows on custom geometry, and full normal/specular material conversion are not supported. Model physics is not imported automatically; use clip brushes for solid props. Test converted maps in-game, especially maps with unusual materials or scripted objects.

## Troubleshooting

- **No custom entry:** install the whole package, check `manifest.json` and its ID, then restart or refresh F6. Read the log for rejected packages.
- **Missing `.ff`:** distinguish missing stock game data from an incomplete custom package. Keep all generated companion fastfiles together.
- **Crash or loading loop:** check `mw120rproxy.log`, `mw120rproxy.engine.log`, `mw120rproxy.trace.log`, and `mw120rproxy.exceptions.log`. For a bug report, include the map name, steps to reproduce, the first error and stack trace, and the map's `build_report.json`.
- **No tilde console:** try F7; keyboard layouts differ.
- **Updating:** close Replay before replacing the DLL or packages. Do not mix files from different map builds.

To uninstall, close Replay and remove this mod's `XInput9_1_0.dll` and `mw120rproxy.ini`, or restore the backed-up proxy if you had one previously. Custom packages live in `mods/mw120r/maps`; identity and logs are local files next to the executable.

See [development and tests](docs/DEVELOPMENT.md) and [third-party notices](THIRD_PARTY_NOTICES.md).
