# Custom-map lighting

The lighting rebuild adds Replay's native sun shadows and screen-space ambient
occlusion to opaque custom-map surfaces. It retains authored baked light and
uses a separate sky material so the sky does not block sunlight or cast shadows.

This is mixed lighting: real-time sunlight and shadows combined with baked
indirect light. It does not add real-time global illumination or a dvar that
replaces baking. The normal map converter keeps its existing lighting until you
run this additional step.

## Rebuild a converted map

Build the tools, load your local paths, and generate the
[Replay shader templates](MAP_BUILDING.md#local-replay-shader-templates) first.
The input must be a completed map build containing `package/` and its retained
conversion dump, or a `build_report.json` that points to that dump.

From the repository root:

```powershell
. .\local.env.ps1
.\build.ps1
python mw120rproxy/tools/rebuild_map_lighting.py `
    --build 'custom_map_sources/mp_4doffice/builds/<existing-build>' `
    --indirect-ev -1
```

The command prints a new package path. It preserves the source build, keeps
surface indices stable, and copies collision, doors, glass, ladders, footsteps,
ambient data, and preview images unchanged. It does not install files or launch
the game.

`--indirect-ev -1` halves the authored baked indirect light; `0` preserves it and
`1` doubles it. Direct sunlight and the sky image keep their existing exposure.
Use `--unbaked-ev` separately for the fill on imported props without lightmaps.
Both options accept values from -4 to +4 and default to zero.

The Office map has been tested with `--indirect-ev -1` and the default
`--unbaked-ev 0`. Treat this as a starting point for Office, not a universal
setting for every map.

Close Replay and install the generated package:

```powershell
.\mw120rproxy\tools\deploy_custom_map.ps1 `
    -GameRoot $env:MW120R_GAME `
    -PackageDir '<new-package-path>' `
    -Map mp_4doffice
```

Updating the DLL alone does not change lighting stored in an existing map
package. Rebuild and install each map you want to update. The installer retains
the previous package in `.proxy/backups`.

## What to check

Compare the same outdoor and indoor locations before and after rebuilding.
Check moving shadows, the sky, basement brightness, glass, foliage, and doors.
The build runs converter and Replay layout checks; `build.ps1 -Tests` also runs
eight offscreen shader raster tests using Windows' software renderer.

Glass and foliage do not yet receive the new screen-space shadow and occlusion
buffers because their depth passes differ from opaque geometry. They retain
their existing lighting behavior.

Native weapons and spawned objects still use the map-wide ambient fallback.
The source map's spatial light grid is not connected to Replay yet, so indoor
weapons and objects can still differ in brightness from the surrounding map.
Player shadows on custom geometry and full normal/specular material conversion
also remain unsupported.
