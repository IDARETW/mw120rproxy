# MW120R Custom Maps

> **Proof of concept.** This repository is a source-only development snapshot for inspecting and
> improving native map conversion and the MW120R local-play proxy. It is not a finished mod release
> and does not promise that an arbitrary converted map will load or play correctly in Replay.

The repository contains two cooperating parts:

- `mw120rproxy` is the Replay 1.20 XInput proxy and local custom-map support.
- `iw8-zonetool` is the native C++ compiler that writes the five Replay map fastfiles.

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
native model physics, supported entities, collision, source sun data, vertex colors, and an
available `compass_map_<map>` image.
It uses a Replay stock material for the 3D world; IW3 technique sets are not serialized verbatim
as IW8 technique sets. Scripts, bot navigation, objectives, and arbitrary scripted movers are not
converted automatically.

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
