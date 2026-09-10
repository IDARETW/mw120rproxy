# Direct IW3 fastfile conversion

The native `build-iw3` command reads a generated CoD4 multiplayer fastfile and writes the five
fastfiles expected by MW120R. It can also write optional `map.json` metadata. Extraction and
intermediate map data stay in a temporary directory and are deleted when the command finishes.

## Prepare OpenAssetTools once

Clone OpenAssetTools and check out the version used by this project:

```powershell
git clone https://github.com/Laupetin/OpenAssetTools.git
Set-Location OpenAssetTools
git checkout 7d027e8f89118196713e955b0e11f8404149c54d
```

From the MW120R source folder, install the IW3 map exporters:

```powershell
.\iw8-zonetool\tools\oat\configure_exporters.ps1 'D:\Tools\OpenAssetTools'
```

Build the OpenAssetTools `Unlinker` target according to its Windows build instructions. Keep the
resulting `Unlinker.exe`; it is the only external conversion program used by `build-iw3`.

## Convert a map

Build `iw8-zonetool`, then run:

```powershell
.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe build-iw3 `
    'D:\CoD4\zone\english\mp_example.ff' `
    --replay 'D:\Replay\game_dx12_ship_replay.exe' `
    --unlinker 'D:\Tools\OpenAssetTools\build\bin\Release_x86\Unlinker.exe' `
    --search-path 'D:\CoD4\main' `
    --search-path 'D:\CoD4\usermaps\mp_example'
```

The map id defaults to the `.ff` filename. The output folder is created beside the source as
`<map>_iw8`; use `-o` only when you need another location. To rename it, add the target id
immediately after the input path:

```powershell
iw8-zonetool.exe build-iw3 'D:\CoD4\mp_old.ff' mp_new `
    --replay 'D:\Replay\game_dx12_ship_replay.exe' `
    --unlinker 'D:\Tools\OpenAssetTools\Unlinker.exe'
```

`build-iw3` also reads a sibling `<map>_load.ff` when present. `--search-path` is repeatable and
lets Unlinker resolve assets stored outside the map zone. `--unlinker` may be omitted when
`Unlinker.exe` is beside `iw8-zonetool.exe`, under `tools`, available on `PATH`, or named by the
`IW8_ZONETOOL_UNLINKER` environment variable. The earlier `MW120R_UNLINKER` variable is also
accepted.

The output directory contains these five fastfiles:

```text
mp_example.ff
srv_mp_example.ff
eng_mp_example.ff
ww_mp_example.ff
techsets_mp_example.ff
```

No JSON is required. To set the lobby title or description, pass one small file with `--metadata`:

```json
{
  "title": "Example Map",
  "description": "Converted from CoD4"
}
```

`id` is also allowed when it matches the target map id. `map.json` is the only loose file accepted
beside the five fastfiles.

Use `validate-package` before installation:

```powershell
iw8-zonetool.exe validate-package 'D:\CoD4\zone\english\mp_example_iw8' mp_example
```

Direct conversion includes the playable world mesh, placed static models, collision, entities,
source sun, vertex colors, and an available HUD minimap. The world uses Replay's stock material.
IW3 and IW8 use different technique-set and shader layouts, so source materials cannot be copied
verbatim. Maps that need converted textures, baked lighting, doors, glass, ladders, or other
authored data should use `build-map` with the prepared input format.
