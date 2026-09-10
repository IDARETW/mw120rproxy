# Build a custom map

The supported compiler is the native C++ source in `iw8-zonetool`. It accepts a generated IW3
fastfile directly or consumes a prepared map dump and produces the five fastfiles read by MW120R,
plus optional `map.json` metadata.

## Build the tools

```powershell
.\build.ps1 -Tests
```

No Python installation is required.

To start directly from a generated CoD4 `.ff`, follow [Convert an IW3 fastfile](IW3_TO_IW8.md).
That path needs no manually prepared directory or JSON files. The prepared layout below remains
available for finished maps that supply converted materials and authored gameplay sidecars.

## Prepare the dump

Use a lowercase ID beginning with `mp_`. A minimum useful dump for `mp_example` contains:

```text
dump/
  mp_example_iw8_ents.txt
  maps/mp/
    mp_example.d3dbsp.render.json
    mp_example.d3dbsp.material.json
    mp_example.d3dbsp.techset.json
    mp_example.d3dbsp.lighting.json
    mp_example.d3dbsp.havok
    mp_example_atlas_0.rgba
```

Models, images, extra materials, and GPU lightgrid data can be placed in the same dump. See [Map dump input](../iw8-zonetool/docs/INPUT_FORMAT.md) for the complete layout.

For the HUD minimap, add `dump/images/compass_map_mp_example.iwi`. It must be an IW3 IWI version 6 image with one resident DXT1, DXT3, or DXT5 surface.

## Compile

```powershell
.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe build-map 'D:\Maps\Example\dump' mp_example -o 'D:\Maps\Example\output'
```

To bake MWCOLL02 or MWCOLL03 collision in memory, add the supported Replay executable, collision input, and optional footstep data:

```powershell
--replay 'D:\Games\Replay\game_dx12_ship_replay.exe' --collision 'D:\Maps\Example\collision.bin' --footsteps 'D:\Maps\Example\footsteps.bin'
```

The output contains exactly:

```text
mp_example.ff
srv_mp_example.ff
eng_mp_example.ff
ww_mp_example.ff
techsets_mp_example.ff
```

`map.json` is optional and is the only loose output file accepted. It can set the lobby `title`,
`description`, and a matching `id`.

## Install

```powershell
.\mw120rproxy\tools\deploy_custom_map.ps1 -GameRoot 'D:\Games\Replay' -MapOutput 'D:\Maps\Example\output'
```

The installer derives the map ID from the output and validates it before installation.

Test geometry, movement and bullet collision, materials, sunlight, interiors, footsteps, glass, ladders, spawns, and the HUD minimap before sharing a conversion.
