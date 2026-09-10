# IW8 ZoneTool for Replay 1.20

This source builds custom multiplayer map zones for the 1.20.4 Replay executable used by
mw120rproxy. It keeps the usual ZoneTool split between common file code, source dump readers,
conversion code, and game-specific IW8 asset writers.

The compiler writes native Replay 1.20 fastfiles. Collision, footsteps, render geometry,
materials, lightgrid data, sun settings, bullet-impact effects, and an optional HUD minimap are
serialized into the zones. It does not create a manifest, loose collision file, report, preview,
or other file in the output directory.

The project has no Python scripts or Python dependency. It builds as a single Windows x64 C++
executable with XMake and Visual Studio Build Tools.

## Build

Install Visual Studio 2022 Build Tools with the Desktop development with C++ workload and install
[XMake](https://xmake.io/). Then run:

```bat
generate.bat
```

The release executable is written to:

```text
xmake-out\x64\Release\iw8-zonetool.exe
```

## Build a map

### Convert an IW3 fastfile directly

`build-iw3` accepts a CoD4 map fastfile and performs the extraction, normalization, collision
bake, and IW8 zone build in one command:

```bat
iw8-zonetool.exe build-iw3 D:\CoD4\zone\english\mp_example.ff ^
  --replay "D:\Replay\game_dx12_ship_replay.exe" ^
  --unlinker "D:\Tools\OpenAssetTools\Unlinker.exe"
```

The map id is taken from the fastfile name. Pass a second positional value to rename it during
conversion. The tool creates `mp_example_iw8` beside the source fastfile and writes the five zones
there; use `-o` only when you want another destination. A sibling `mp_example_load.ff` is read
automatically. Repeat `--search-path` for CoD4 directories or IWD locations needed by the source
zone.

This command uses the native OpenAssetTools Unlinker with the supplied IW3 Replay map exporters.
It keeps extracted files in a private temporary directory and removes them after the five fastfiles
are written. No Python runtime, manual export, `.bin`, report, preview, or extra directory layout
is required.

`map.json` is optional. Pass it with `--metadata` when the lobby should use a title or description:

```json
{
  "title": "Example Map",
  "description": "Converted from CoD4"
}
```

It is the only loose file the output accepts. It may also contain an `id` that matches the map id.

Direct fastfile conversion preserves world geometry, placed static-model LOD0 geometry, collision,
entities, source sun settings, vertex colors, and an available `compass_map_<map>` image. It uses
Replay's stock material for the 3D world because IW3 technique sets cannot be serialized as IW8
technique sets. Use the prepared-dump route below when the map needs converted materials, textures,
baked light data, doors, glass, ladders, or other authored sidecars.

The patched Unlinker setup and complete command are documented in
[`docs/IW3_FASTFILE.md`](docs/IW3_FASTFILE.md).

### Build a prepared map

Give `build-map` a prepared map dump and a lower-case `mp_` map id. The output folder is created
beside the dump unless `-o` selects another destination:

```bat
iw8-zonetool.exe build-map C:\maps\mp_example\dump mp_example
```

The output contains exactly:

```text
mp_example.ff
srv_mp_example.ff
eng_mp_example.ff
ww_mp_example.ff
techsets_mp_example.ff
```

The source dump layout is documented in [docs/INPUT_FORMAT.md](docs/INPUT_FORMAT.md). Existing
serialized Replay collision can be placed at
`maps/mp/mp_example.d3dbsp.havok`. To bake `collision.bin` directly into the server fastfile, pass
the matching Replay executable and optional footstep data:

```bat
iw8-zonetool.exe build-map C:\maps\mp_example\dump mp_example ^
  -o C:\maps\mp_example\output ^
  --replay "C:\Games\Modern Warfare\game_dx12_ship_replay.exe" ^
  --collision C:\maps\mp_example\collision.bin ^
  --footsteps C:\maps\mp_example\footsteps.bin
```

`--replay` accepts only the supported 1.20.4 Replay executable. The compiler checks its SHA-256
before using the game's Havok serializer.

Source lighting is used by default. Its intensity can be adjusted without replacing the source
sun direction or color:

```bat
iw8-zonetool.exe build-map C:\maps\mp_example\dump mp_example ^
  -o C:\maps\mp_example\output --sun-intensity-scale 6
```

## Check output

```bat
iw8-zonetool.exe validate-package C:\maps\mp_example\output mp_example
iw8-zonetool.exe inspect C:\maps\mp_example\output\mp_example.ff
```

Validation accepts exactly five fastfiles and an optional `map.json`. It checks the Replay header,
resident framing, and stream sizes of every zone.

## Install

Copy the five fastfiles to:

```text
<game>\mods\mw120r\maps\mp_example\
```

Use a current mw120rproxy build with fastfile-only package support. The folder name and map id must
match exactly.

This project currently has no project license. The bundled third-party component is listed in
[THIRD_PARTY.md](THIRD_PARTY.md).
