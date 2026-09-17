# Development

> **Proof-of-concept status:** source builds, native unit tests, and offline package validation are
> development gates. They do not establish in-game compatibility. Test converted maps on a separate
> Replay installation.

The public checkout contains two native C++ targets:

- 'mw120rproxy' — the Replay 1.20 XInput proxy and local custom-map and custom-weapon support.
- 'iw8-zonetool' — the native compiler for Replay map and weapon fastfile packages.

The repository intentionally does not contain game binaries, stock assets, converted maps, prebuilt
DLLs, private research captures, or deployment evidence.

## Requirements

Use Windows x64 with Visual Studio 2022 Build Tools, a Windows SDK, and XMake 2.8 or newer. The
direct IW3 route additionally needs the pinned OpenAssetTools source and a locally built
'Unlinker.exe'; see [Direct IW3 fastfile details](../iw8-zonetool/docs/IW3_FASTFILE.md).

## Build

From the repository root:

~~~powershell
.\build.ps1 -Tests
~~~

This configures and builds both release targets, then builds and runs the native
'mw120rproxy/tests/custom_map_tests.cpp' target. The release artifacts are:

~~~text
mw120rproxy/xmake-out/x64/Release/XInput9_1_0.dll
iw8-zonetool/xmake-out/x64/Release/iw8-zonetool.exe
~~~

'.\build.ps1' accepts '-Mode debug' or '-Mode release'; '-Tests' is an additional native test
gate. Python preparation scripts are supplementary tooling. The native compiler itself does not
require Python.

Use the checked-in clang-format styles for C++. Replay addresses, byte signatures, structure sizes,
stream layouts, and ABI padding should remain explicit and version-scoped.

## Install

Close Replay before installing the proxy:

~~~powershell
.\install.ps1 -GameRoot 'D:\Games\Replay' -SkipBuild
~~~

The installer verifies the supported Replay executable, stages and hashes the DLL, and backs up
the previous proxy and configuration under '<GameRoot>\.proxy\backups'. It never starts or stops
the game. Omit '-SkipBuild' when the proxy should be rebuilt first; use '-PreserveConfig' to retain
the existing 'mw120rproxy.ini'.

## Map packages

The compiler accepts either a generated IW3 map fastfile or a prepared dump. Both routes emit these
five files and may also emit 'map.json' when metadata is requested:

~~~text
<map>.ff
srv_<map>.ff
eng_<map>.ff
ww_<map>.ff
techsets_<map>.ff
~~~

Run 'validate-output <package> <map>' before sharing or installing a package. The deployment helper
takes an explicit game directory and package directory:

~~~powershell
& .\mw120rproxy\tools\deploy_custom_map.ps1 -GameRoot 'D:\Games\Replay' -PackageDir 'D:\Maps\mp_example_iw8' -Map mp_example
~~~

It validates before copying, stages and hash-checks the package, rejects reparse points in the
destination path, preserves a rollback copy, and does not write local evidence into the repository.
Replay must be closed. '-MapOutput' remains an alias for '-PackageDir'.

See [Map building](MAP_BUILDING.md), [IW3 to IW8](IW3_TO_IW8.md), and the
[map dump input contract](../iw8-zonetool/docs/INPUT_FORMAT.md) for the two build routes.

## Evidence boundary

Offline tests and parser/serializer checks answer whether a known input or package satisfies a
specific structural contract. They do not prove that a live Replay installation loads the map,
renders every asset, or behaves correctly in Local Play. Keep live testing, game-folder deployment,
and release decisions as separate owner-run gates.

## Weapon editor

The Python/Three.js editor lives in `iw8-zonetool/tools/weapon_editor`. The
[weapon guide](../iw8-zonetool/docs/CUSTOM_WEAPONS.md) documents reproducible local
setup and the pinned ACTS source patch. Run setup against your own supported Replay
installation; do not commit the resulting library, exports, projects, cache, or
`config.local.json`. The public editor uses browser uploads and has no host filesystem
browsing routes. Keep native layout evidence scoped to the checked executable hash.

`write_weapon.cpp` emits the native asset graph and companion fastfiles. The proxy's
`custom_weapons.cpp` registers packages and extends loadout tables/DDL while preserving
stock ordinals. Asset extension caches keep repeated Gunsmith lookups cheap. The
shared map loader also accepts registered weapon packages; weapon changes must retain
the existing map paths and shader authentication behavior.

Run `check_public.py` against a prepared local configuration, `check_registration.py`
against the generated library, and `check_native.py` with the built compiler. Test
UI behavior in a browser and authored assets in Replay separately: source checks,
native compilation, package validation, and gameplay are distinct gates.
