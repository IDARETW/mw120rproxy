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

Give `build-map` a prepared map dump, a lower-case `mp_` map id, and an empty output directory:

```bat
iw8-zonetool.exe build-map C:\maps\mp_example\dump mp_example -o C:\maps\mp_example\package
```

The package contains exactly:

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
  -o C:\maps\mp_example\package ^
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
  -o C:\maps\mp_example\package --sun-intensity-scale 6
```

## Check a package

```bat
iw8-zonetool.exe validate-package C:\maps\mp_example\package mp_example
iw8-zonetool.exe inspect C:\maps\mp_example\package\mp_example.ff
```

Validation requires exactly five files and checks the Replay header, resident framing, and stream
sizes of every zone.

## Install

Copy the five fastfiles to:

```text
<game>\mods\mw120r\maps\mp_example\
```

Use a current mw120rproxy build with fastfile-only package support. The folder name and map id must
match exactly.

This project currently has no project license. The bundled third-party component is listed in
[THIRD_PARTY.md](THIRD_PARTY.md).
