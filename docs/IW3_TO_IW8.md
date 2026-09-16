# Convert an IW3 fastfile for Replay 1.20

> **Proof of concept:** validate every output and test it in a separate Replay installation. The
> converter is not a compatibility guarantee for arbitrary IW3 maps.

The native 'build-iw3' command reads a generated CoD4 multiplayer fastfile, extracts the supported
map data, bakes Replay collision, and writes the five fastfiles used by MW120R. It does not require
a manual map export or a Python runtime.

## Requirements

- A lawful copy of the IW3 map and any IWD or zone archives that contain its referenced assets.
- The supported MW2019 Replay executable, version 1.20.4.7623265.
- Visual Studio C++ Build Tools and XMake for building this repository.
- OpenAssetTools at the pinned commit below, with the supplied IW3 Replay exporters and a locally
  built 'Unlinker.exe'.

## Prepare OpenAssetTools

Clone the pinned OpenAssetTools source outside this repository:

~~~powershell
git clone https://github.com/Laupetin/OpenAssetTools.git 'D:\Tools\OpenAssetTools'
git -C 'D:\Tools\OpenAssetTools' checkout 7d027e8f89118196713e955b0e11f8404149c54d
~~~

From the MW120R repository root, install the IW3 map exporters into that checkout:

~~~powershell
.\iw8-zonetool\tools\oat\configure_exporters.ps1 'D:\Tools\OpenAssetTools'
~~~

Build the OpenAssetTools 'Unlinker' target according to its Windows build instructions. The
resulting executable is not checked into this repository. Keep its path available for the
conversion command.

## Convert

Build the repository first:

~~~powershell
.\build.ps1 -Tests
~~~

Pass the generated CoD4 map fastfile directly to the native compiler:

~~~powershell
$zoneTool = '.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe'
& $zoneTool build-iw3 'D:\CoD4\zone\english\mp_example.ff' mp_example -o 'D:\Maps\mp_example_iw8' --replay 'D:\Games\Replay\game_dx12_ship_replay.exe' --unlinker 'D:\Tools\OpenAssetTools\build\bin\Release_x86\Unlinker.exe' --search-path 'D:\CoD4\main' --search-path 'D:\CoD4\usermaps\mp_example'
~~~

The target map ID defaults to the input filename. Supplying the lower-case ID after the input path
renames it during conversion. The ID must begin with 'mp_', use only lower-case letters, numbers,
and underscores, and fit the 15-character Replay field. Without '-o', the output is created beside
the source as '<map>_iw8'.

The input fastfile directory is searched automatically. A sibling '<map>_load.ff' is read when
present. Repeat '--search-path' for other IW3 asset directories. Use '--iw3-root' with the root of
a normal CoD4 installation to add that root, 'main', and 'raw' together, or set 'IW3_GAME_ROOT' or
'COD4_ROOT' in the environment. The '--unlinker' option may be omitted when 'Unlinker.exe' is
beside the ZoneTool executable, under a nearby 'tools' directory, on 'PATH', or named by
'IW8_ZONETOOL_UNLINKER' or 'MW120R_UNLINKER'.

PowerShell uses the call operator '&'. Command Prompt uses double quotes and no PowerShell
backticks or single-quoted path literals:

~~~text
".\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe" build-iw3 "D:\CoD4\zone\english\mp_example.ff" mp_example -o "D:\Maps\mp_example_iw8" --replay "D:\Games\Replay\game_dx12_ship_replay.exe" --unlinker "D:\Tools\OpenAssetTools\build\bin\Release_x86\Unlinker.exe" --search-path "D:\CoD4\main"
~~~

Replace the example paths with paths that exist on your computer.

The converter embeds geometry, model LODs and material bindings, collision, entities, native
light-grid data, and a BC6H UF16 octahedral reflection-probe array in the output zones. Successful
conversion leaves exactly five fastfiles and `map.json` in the output directory. It does not leave
intermediate JSON, binary payloads, DDS files, or package directories.
