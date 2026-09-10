# Convert an IW3 fastfile for Replay 1.20

IW3 fastfiles cannot be installed in MW2019 unchanged. The native `build-iw3` command reads a
generated CoD4 multiplayer fastfile, converts its map data, bakes Replay collision, and writes the
five fastfiles used by MW120R. It does not require a manual map export or Python.

## Requirements

- A lawful copy of the IW3 map and any IWD archives that contain assets referenced by it.
- MW2019 Replay 1.20.4.7623265 for collision serialization and testing.
- Visual Studio C++ Build Tools, XMake, and a compatible OpenAssetTools `Unlinker.exe`.

## Prepare Unlinker once

Clone OpenAssetTools and select the version used by this project:

```powershell
git clone https://github.com/Laupetin/OpenAssetTools.git 'D:\Tools\OpenAssetTools'
git -C 'D:\Tools\OpenAssetTools' checkout 7d027e8f89118196713e955b0e11f8404149c54d
```

Install the supplied IW3 world and collision exporters, then build OpenAssetTools' `Unlinker`
target according to that project's Windows build instructions:

```powershell
.\iw8-zonetool\tools\oat\configure_exporters.ps1 'D:\Tools\OpenAssetTools'
```

## Convert

Build MW120R and the converter:

```powershell
.\build.ps1 -Tests
```

Pass the generated CoD4 map fastfile directly to `build-iw3`:

```powershell
.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe build-iw3 `
    'D:\CoD4\zone\english\mp_example.ff' `
    -o 'D:\Maps\mp_example\package' `
    --replay 'D:\Replay\game_dx12_ship_replay.exe' `
    --unlinker 'D:\Tools\OpenAssetTools\build\bin\Release_x86\Unlinker.exe' `
    --search-path 'D:\CoD4\main' `
    --search-path 'D:\CoD4\usermaps\mp_example'
```

The target map id defaults to the `.ff` filename. Add a lower-case `mp_` id after the input path
to rename it during conversion. A sibling `<map>_load.ff` is included automatically.
`--search-path` is repeatable and lets Unlinker resolve referenced zones or IWD archives.

`--unlinker` can be omitted when `Unlinker.exe` is beside `iw8-zonetool.exe`, under a nearby
`tools` folder, on `PATH`, or set in `IW8_ZONETOOL_UNLINKER` or `MW120R_UNLINKER`.

The output directory contains only:

```text
mp_example.ff
srv_mp_example.ff
eng_mp_example.ff
ww_mp_example.ff
techsets_mp_example.ff
```

The native command keeps its OpenAssetTools output, normalized geometry, JSON, and collision input
inside a private temporary directory. Those files are removed after the five fastfiles are written.

Direct conversion carries over world geometry, placed static-model LOD0 geometry, collision,
entities, source sun direction and color, vertex colors, and an available HUD minimap. It uses
Replay's stock material for the 3D world. IW3 and IW8 technique-set and shader layouts differ, so
IW3 material records cannot be copied into an IW8 zone verbatim.

For a finished textured map with converted materials, baked light data, doors, glass, ladders,
footstep tags, and other authored sidecars, use the prepared-dump route documented in
[Build a custom map](MAP_BUILDING.md) and [Map dump input](../iw8-zonetool/docs/INPUT_FORMAT.md).

## Validate and install

```powershell
.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe validate-package `
    'D:\Maps\mp_example\package' mp_example

.\mw120rproxy\tools\deploy_custom_map.ps1 `
    -GameRoot 'D:\Replay' `
    -PackageDir 'D:\Maps\mp_example\package' `
    -Map mp_example
```

IW3 scripts, bot navigation, objectives, and arbitrary scripted movers are not converted
automatically.
