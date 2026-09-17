# MW120R Custom Maps and Weapons

> **Proof of concept.** This repository is a source-only development snapshot for inspecting and
> improving native map and weapon authoring and the MW120R local-play proxy. It is not a finished mod release
> and does not promise that an arbitrary converted map will load or play correctly in Replay.

The repository contains three cooperating parts:

- `mw120rproxy` is the Replay 1.20 XInput proxy with local custom-map and custom-weapon support.
- `iw8-zonetool` is the native C++ compiler for map and custom-weapon fastfiles.
- The **Weapon Workbench** is a local web editor with a live 3D viewport, stock weapon inspection,
  animation playback, operator arms, native tag/rig editing, attachments, materials, and SFX/VFX data.

No game executable, stock asset, converted map, prebuilt DLL, or private test capture is included.
Keep conversion and gameplay testing on a separate Replay installation.

## Requirements

- Windows x64.
- MW2019 Replay 1.20.4.7623265 with
  `game_dx12_ship_replay.exe` MD5 `1c238fe327f2ecc3b0db924c5b425439`.
- Visual Studio 2022 Build Tools with the Desktop development with C++ workload, a Windows SDK,
  and XMake 2.8 or newer.
- A lawful copy of any IW3 map and its asset archives when using the direct IW3 conversion route.
- A matching OpenAssetTools `Unlinker.exe` for direct IW3 conversion. The executable is not bundled;
  the pinned setup is documented in [Direct IW3 conversion](iw8-zonetool/docs/IW3_FASTFILE.md).

## Build and install the proxy

From the repository root, build the proxy, compiler, and native custom-map tests:

```powershell
.\build.ps1 -Tests
```

Release outputs are written to:

```text
mw120rproxy/xmake-out/x64/Release/XInput9_1_0.dll
iw8-zonetool/xmake-out/x64/Release/iw8-zonetool.exe
```

Close Replay and install the proxy beside the game executable. Replace the example path with the
directory containing `game_dx12_ship_replay.exe`:

```powershell
.\install.ps1 -GameRoot 'D:\Games\Replay' -SkipBuild
```

The installer verifies the Replay executable, stages and hashes the DLL, backs up the previous
proxy/configuration under `.proxy\backups`, and never starts or stops the game. Omit `-SkipBuild`
when the proxy should be rebuilt first. Use `-PreserveConfig` to keep the existing
`mw120rproxy.ini`.

## Create custom weapons

Follow the **[Weapon Workbench setup and authoring guide](iw8-zonetool/docs/CUSTOM_WEAPONS.md)**.
Build the native tools, install the Python/Three.js dependencies, build the pinned ACTS reader,
then run setup against **your own Replay game files**. Setup generates the private reference
library, animation cache, arms, material profile, and loadout tables on your machine.

Open the editor at `http://127.0.0.1:8766/`. Import your own models, textures, and audio through
the browser's normal file picker, or load a stock weapon from your configured game installation
to inspect its animations and tags. No tunnel or protected session link is required. The public
editor has no server-file browsing/import option. No game files or extracted stock library ship
with the repository. See the guide for preview limitations, building and installing the eight
weapon companion fastfiles, and local verification commands.

## Convert a map

The native compiler has two input routes.

### Direct IW3 fastfile conversion

`build-iw3` accepts a generated CoD4 multiplayer `.ff`, uses OpenAssetTools for extraction, bakes
Replay collision, and writes the native package. A complete example is in
[IW3 to IW8](docs/IW3_TO_IW8.md):

```powershell
$zoneTool = '.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe'
& $zoneTool build-iw3 'D:\CoD4\zone\english\mp_example.ff' mp_example `
    -o 'D:\Maps\mp_example_iw8' `
    --replay 'D:\Games\Replay\game_dx12_ship_replay.exe' `
    --unlinker 'D:\Tools\OpenAssetTools\build\bin\Release_x86\Unlinker.exe' `
    --search-path 'D:\CoD4\main'
```

The map ID must be lower-case, begin with `mp_`, and fit the 15-character Replay field. A sibling
`<map>_load.ff` and additional `--search-path` directories are used when available. The direct
route carries over the source world geometry, resolved static-model placements and available LODs,
native model physics, supported entities, collision, source sun data, authored lightmaps and light
probes, vertex colors, and an available `compass_map_<map>` image. Source DPVS cells and AABB trees
retain both their surface and static-model membership, including trees that contain models without
world surfaces. Baked opaque surfaces use source-specific Replay materials and the native Replay
lightmap-atlas consumer; the IW3 technique-set bytes themselves are never copied into an
incompatible IW8 structure. When the IW3 entities provide the stock pair of `minimap_corner`
script origins, the compiler embeds the native Replay startup script that binds the compass image
to those world coordinates. Source cubemaps are projected into Replay's native
256-by-256 octahedral reflection array and encoded as BC6H UF16 by the bundled native encoder.
Arbitrary source scripts, bot navigation, objectives, alternate-mode spawn markers, and scripted
movers are not converted automatically unless a matching Replay asset consumer has been proven.

### Prepared map dump

`build-map` consumes a prepared dump when the map needs authored IW8 materials, textures, doors,
glass, ladders, or other explicit source data:

```powershell
& .\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe build-map `
    'D:\Maps\mp_example\dump' mp_example `
    -o 'D:\Maps\mp_example\output'
```

The required files and optional assets are defined in [Map dump input](iw8-zonetool/docs/INPUT_FORMAT.md).
Prepared native collision can be supplied as `<map>.d3dbsp.havok`, or generated in memory with
`--replay`, `--collision`, and optional `--footsteps`. Both routes write exactly these five zones and
`map.json`:

```text
mp_example.ff
srv_mp_example.ff
eng_mp_example.ff
ww_mp_example.ff
techsets_mp_example.ff
map.json
```

`map.json` is always generated and is the only loose file accepted beside the five fastfiles. It can
set a matching `id`, a lobby `title` (or `name`), and a `description`.

## Install a current map package

After building a package, validate and install it with the portable helper. `-Map` is explicit in
this example so the package folder does not need a particular name:

```powershell
& .\mw120rproxy\tools\deploy_custom_map.ps1 `
    -GameRoot 'D:\Games\Replay' `
    -PackageDir 'D:\Maps\mp_example_iw8' `
    -Map mp_example
```

The helper requires the supported Replay executable and the built ZoneTool, validates the package,
stages the five zones plus `map.json`, compares SHA-256 hashes, and keeps a rollback copy
under `<GameRoot>\.proxy\backups`. Replay must be closed. The helper never starts or stops the
game and does not write deployment evidence into this repository. `-MapOutput` is accepted as an
alias for `-PackageDir` for older command lines. When the package folder is named `mp_example` or
contains one unambiguous `mp_example.ff`, `-Map` can be omitted.

The older twelve-file Nuketown archive uses a legacy manifest and sidecars. It is not the output of
the current compiler and is intentionally installed by the manual layout in
[Nuketown example](docs/NUKETOWN_EXAMPLE.md), not by the five-fastfile helper.

## Documentation

- [Development](docs/DEVELOPMENT.md) — build, test, and contribution boundaries.
- [IW3 to IW8](docs/IW3_TO_IW8.md) — direct CoD4 fastfile conversion.
- [Map building](docs/MAP_BUILDING.md) — prepared dumps and package validation.
- [Map dump input](iw8-zonetool/docs/INPUT_FORMAT.md) — geometry, materials, lighting, collision,
  entities, and the optional HUD minimap.
- [Direct IW3 fastfile details](iw8-zonetool/docs/IW3_FASTFILE.md) — OpenAssetTools setup and
  command-line behavior.
- [Imported doors](docs/DOORS.md) — legacy authored brush-door data and current limits.
- [Custom-map lighting](docs/LIGHTING.md) — source sun and prepared native lightgrid data.
- [Nuketown example](docs/NUKETOWN_EXAMPLE.md) — historical ready-to-install package.

The code and documentation describe an experimental native conversion path. Offline parsing,
serialization, and unit-test success are useful development evidence, but only an owner-run test
on the target Replay installation can establish live map behavior.
