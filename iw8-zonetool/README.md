# IW8 ZoneTool for Replay 1.20

This source builds custom multiplayer map zones for the 1.20.4 Replay executable used by
mw120rproxy. It keeps the usual ZoneTool split between common file code, source dump readers,
conversion code, and game-specific IW8 asset writers.

The compiler writes native Replay 1.20 fastfiles. Collision, footsteps, render geometry,
materials, authored vertex occlusion, lightgrid data, sun settings, bullet-impact effects, and an optional HUD minimap are
serialized into the zones. Direct conversion does not create a manifest, loose collision file,
report, preview, or other generated sidecar. It writes the five fastfiles and one `map.json`.

The converter has no Python runtime or Python package dependency. It builds as a single Windows x64 C++
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

```powershell
$map = 'C:\Maps\mp_example'
$replay = 'E:\IW8\Builds\1.20-replay\game_dx12_ship_replay.exe'
$unlinker = '.\mw120rproxy\tools\_vendor\OpenAssetTools\build\bin\Release_x86\Unlinker.exe'
$zoneTool = '.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe'

& $zoneTool build-iw3 "$map\mp_example.ff" mp_example `
    -o "$map\converted" `
    --replay $replay `
    --unlinker $unlinker `
    --iw3-root 'E:\CoD4\game_build\Call of Duty 4 Modern Warfare'
```

The example above is PowerShell. For Command Prompt syntax, including its required double quotes,
see [`docs/IW3_FASTFILE.md`](docs/IW3_FASTFILE.md). Do not wrap a Command Prompt executable path
in single quotes.

The map id is taken from the fastfile name. It must start with `mp_`, use lower-case letters,
numbers, or underscores, and be no longer than 15 characters. Pass a second positional value to
rename a longer source map during conversion. The tool creates `mp_example_iw8` beside the source
fastfile and writes the five zones there; use `-o` only when you want another destination. A
sibling `mp_example_load.ff` is read automatically. Repeat `--search-path` for CoD4 directories or
IWD locations needed by the source zone.

The fastfile's directory is searched automatically. If the CoD4 installation is elsewhere, pass
`--iw3-root 'C:\Games\Call of Duty 4 Modern Warfare'`; the tool adds its root, `main`, and `raw`
directories automatically. You can also set `IW3_GAME_ROOT` (or `COD4_ROOT`) once for repeated
conversions. Additional `--search-path` values are still accepted for custom asset packs.

For a normal CoD4 install, the shortest complete command is:

```powershell
& $zoneTool build-iw3 "$map\mp_example.ff" mp_example `
    -o "$map\converted" --replay $replay --iw3-root 'C:\Games\Call of Duty 4 Modern Warfare'
```

This command uses the native OpenAssetTools Unlinker with the supplied IW3 Replay map exporters.
It keeps extracted files in a private temporary directory beside the output destination and
removes them after the package is written. Choose an output drive with enough free space for
the source extraction; `-o` also chooses the drive used for temporary work. No Python runtime,
manual export, report, preview, or extra directory layout
is required.

Direct conversion creates `map.json` with the map id as its default title. Pass `--metadata` when
the lobby should use a friendly name/title or description. `name` and `title` are accepted aliases
and must match if both are present:

```json
{
  "name": "Example Map",
  "description": "Converted from CoD4"
}
```

The supplied values replace the defaults in the output `map.json`. It is the only loose file the
output accepts. It may also contain an `id` that matches the map id.

Direct fastfile conversion preserves world geometry, every resolved static-model placement and
available source LOD, native model physics, dynamic model definitions, collision, source sun
settings, authored vertex color and occlusion, and an available
`compass_map_<map>` image. Source DPVS cells and AABB trees retain their world-surface and
static-model membership, including model-only trees, so Replay can use the native visibility
consumer without dropping distant placements. When the source entities contain exactly two
`script_origin` records named `minimap_corner`, the compiler orders them with the IW3 worldspawn
`northyaw` rule and embeds a Replay startup `ScriptFile` that calls the native minimap builtin with
the image and world bounds. It also
derives native walkable-surface triangles from upward-facing IW3 world faces and bakes their
surface types into the Replay Havok shape tags. Unclassified faces use Replay's concrete fallback.
All IW3 multiplayer spawn classes are retained with their source origin and angles, including DM,
Domination, Sabotage, Search and Destroy, CTF, and TDM markers. Script origins, brush models, and
brush-backed triggers are retained as Replay `MapEnts` records. Trigger hulls and non-axis slabs
reference the same native Havok entity shapes as their source brush models.

The material adapter converts IW3 color, normal, specular, glass, foliage, and sky inputs into the
matching Replay material and technique-set layouts. It does not copy IW3 technique-set bytes into an
incompatible IW8 structure. Reachable single-image `effect_zfeather` and `effect_zfeather_add` FX
materials are also converted into native Replay effect-quad materials and resident images; the
converter validates their IW3 atlas and render-state data before serialization. The stock
`impacts/small_glass` source graph is converted into a native Replay particle system, including its
material, decal, model-shard, velocity, gravity, size, color, and atlas data, and is installed in all
small-glass entries of the native impact table. The generated footstep, collision, and light-grid
files exist only in the private conversion directory and are embedded in the fastfiles before that
directory is removed. No loose gameplay sidecar is emitted.

The patched Unlinker setup and complete command are documented in
[`docs/IW3_FASTFILE.md`](docs/IW3_FASTFILE.md).

### Build a prepared map

Give `build-map` a prepared map dump and a lower-case `mp_` map id no longer than 15 characters.
The output folder is created beside the dump unless `-o` selects another destination:

```bat
iw8-zonetool.exe build-map C:\maps\mp_example\dump mp_example
```

The output contains exactly these five fastfiles and `map.json`:

```text
mp_example.ff
srv_mp_example.ff
eng_mp_example.ff
ww_mp_example.ff
techsets_mp_example.ff
```

`map.json` is required and is the only loose file accepted beside the fastfiles. The source dump
layout is documented in [docs/INPUT_FORMAT.md](docs/INPUT_FORMAT.md). Existing
serialized Replay collision can be placed at
`maps/mp/mp_example.d3dbsp.havok`. To bake native collision data directly into the server fastfile, pass
the matching Replay executable and optional footstep data:

```bat
iw8-zonetool.exe build-map C:\maps\mp_example\dump mp_example ^
  -o C:\maps\mp_example\output ^
  --replay "C:\Games\Modern Warfare\game_dx12_ship_replay.exe" ^
  --collision C:\maps\mp_example\collision.native ^
  --footsteps C:\maps\mp_example\footsteps.native
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
iw8-zonetool.exe validate-output C:\maps\mp_example\output mp_example
iw8-zonetool.exe inspect C:\maps\mp_example\output\mp_example.ff
```

Validation accepts exactly five fastfiles and `map.json`. It checks the Replay header,
resident framing, and stream sizes of every zone. Normal installation runs this validation
automatically; the command is useful when diagnosing a failed build.

## Install

Copy the five fastfiles and `map.json` to:

```text
<game>\mods\mw120r\maps\mp_example\
```

Use a current mw120rproxy build with fastfile-only package support. The folder name and map id must
match exactly.

From the repository root, the companion deployment helper can install a validated package into the
configured Replay directory:

```powershell
& .\mw120rproxy\tools\deploy_custom_map.ps1 `
    'C:\Maps\mp_example\output' mp_example
```

Close Replay before running it. The helper stages and hash-checks the five zones, preserves a
rollback copy, and accepts only those zones plus `map.json`.

This project currently has no project license. The bundled third-party component is listed in
[THIRD_PARTY.md](THIRD_PARTY.md).
