# Convert an IW3 map for Replay 1.20

IW3 fastfiles cannot be copied into MW2019. Export the source assets into the prepared dump layout, then serialize them with the native C++ compiler.

## Requirements

- A lawful copy of the IW3 map and every IWD containing its images and models.
- OpenAssetTools/Unlinker built for IW3 export.
- Visual Studio C++ Build Tools and XMake.
- MW2019 Replay 1.20.4.7623265 for collision baking and testing.

`mw120rproxy/tools/oat/ReplayMapDumpers.h` is the OpenAssetTools exporter reference used for the IW3 world, collision, lightgrid, materials, and shader data. Add it to the matching IW3 dumper registration in your OpenAssetTools checkout and build Unlinker according to that project's instructions.

## Export and prepare

Run Unlinker on `<map-id>.ff`, extract the map's IWD archives, and keep one map per dump directory. Normalize the export into the render, material, techset, lighting, entity, collision, image, and model layout documented in [Map dump input](../iw8-zonetool/docs/INPUT_FORMAT.md). Preserve material names and surface assignments.

Add the HUD overview as `dump/images/compass_map_<map-id>.iwi`, using the exact lowercase map ID.

## Build the compiler

```powershell
.\build.ps1 -Tests
```

The conversion build has no Python dependency.

## Write, validate, and install

```powershell
.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe build-map 'D:\Maps\mp_example\dump' mp_example -o 'D:\Maps\mp_example\package'
.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe validate-package 'D:\Maps\mp_example\package' mp_example
.\mw120rproxy\tools\deploy_custom_map.ps1 -GameRoot 'D:\Games\Replay' -PackageDir 'D:\Maps\mp_example\package' -Map mp_example
```

If the dump has MWCOLL02 or MWCOLL03 collision instead of a serialized `.havok`, pass `--replay`, `--collision`, and optionally `--footsteps` to `build-map`. Those inputs are embedded in `srv_<map-id>.ff` and are not copied to the package.

The result is five Replay fastfiles. IW3 GSC gameplay, bot navigation, objectives, and arbitrary scripted movers are not automatically converted.
