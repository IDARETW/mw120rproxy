# Import maps from other engines

The offline iw8-zonetool importer converts supported source maps and static meshes into
**MW2019 Replay 1.20.4.7623265** packages for the MW120R custom-map pipeline. It writes
geometry, collision, base-color materials and TDM spawn data. It does not launch a game.

For the existing CoD4 workflow with its additional map-specific features, see
[IW3 to IW8](IW3_TO_IW8.md). This generic importer targets static content.

## Compatibility

| Format | Accepted input | Coverage and verification |
|---|---|---|
| IW3 / CoD4 | Patched OAT dump or FF through Unlinker | World, static-model LOD0 placements, compiled collision, base textures and spawns; complete Office FF converted in graybox mode |
| IW4 / MW2 | Typed OAT dump or FF through extended Unlinker | Same static content; production exporter tested against native IW4 structs; complete real-map FF validation remains open |
| IW5 / MW3 | Typed OAT dump or FF through extended Unlinker | Same static content; production exporter tested against native IW5 structs; complete real-map FF validation remains open |
| Quake II | IBSP 38 BSP | Brush geometry, solid/playerclip collision, UVs and spawns; downloaded BSP tested |
| Quake III Arena | IBSP 46 BSP | Planar/mesh surfaces, quadratic patches, brush collision and spawns; downloaded BSP and controlled patches tested |
| Quake Live | IBSP 47 BSP | Q3-style geometry/patch reader; controlled version-47 fixture tested |
| Source | VBSP 19/20 BSP, face-lump version 1 | World brushes and full-resolution displacements; downloaded lobby and terrain BSPs tested |
| Wavefront OBJ | OBJ/MTL and images | Static polygons, normals, UVs, base colors/textures and triangle-prism collision; downloaded OBJ tested |
| glTF 2.0 | GLTF/GLB | Static TRIANGLES, node transforms, base colors/textures and triangle-prism collision; downloaded GLB tested |

ZIP, PK3, IWD and Quake PAK archives may contain the source BSP/FF or provide textures.
Only the selected map is extracted. Use --source-map when more than one map exists.

These results are **offline validation**, not game-loading or gameplay proof.
Other binary versions and raw ZoneTool/x64-zt world blobs are rejected.

## Build

From the repository root:

~~~powershell
python -m pip install -r requirements.txt
.\build.ps1
.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe formats
~~~

The executable needs iw8-zonetool/tools beside its source/build tree. It locates and
invokes Python directly; set IW8_ZONETOOL_PYTHON to a Python executable path if needed.
It is a path, not a shell command.

Textured output needs locally generated Replay shader templates:
[template setup](MAP_BUILDING.md#local-replay-shader-templates). These game-derived files
are not bundled. Once prepared, the import itself is offline. **--graybox** uses the
stock $default material and requires no extracted shader templates.

## Import

Run these commands from the repository root, replacing the example source paths.
Each -o must name a **new directory**, including on retry. Final files go in package/.

~~~powershell
# BSP with textures from a PK3; require every requested texture.
.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe import C:\Maps\arena.bsp mp_arena -o test-out\arena_v1 --asset-root C:\Maps\arena.pk3 --strict-textures

# Select a map inside an archive; graybox needs no local shader templates.
.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe import C:\Maps\pack.pk3 mp_arena -o test-out\arena_v2 --source-map arena --graybox

# Source BSP, using an extracted materials directory.
.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe import C:\Maps\terrain.bsp mp_terrain -o test-out\terrain_v1 --asset-root C:\Maps\source_assets --spawn 0 0 128

# OBJ: specify units, up axis and spawn coordinates.
.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe import C:\Maps\level.obj mp_level -o test-out\level_v1 --up-axis y --scale 1 --spawn 0 0 72 --strict-textures

# glTF defaults to Y-up -> Z-up and meters -> 39.37007874 Replay units.
.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe import C:\Maps\level.glb mp_glb -o test-out\glb_v1 --spawn 0 0 72 --strict-textures

# Complete CoD FF via the extended offline Unlinker.
.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe import C:\Maps\mp_example.ff mp_converted -o test-out\cod_v1 --format iw4 --graybox --search-path C:\Maps\dependencies
~~~

Use --title for the selector label and --credit for attribution. Repeat --asset-root for
additional directories/archives. --format verifies the source; it cannot reinterpret
an incompatible binary layout.

Repeat --spawn X Y Z for several locations. Spawn arguments use **output coordinates**
after source-axis/unit conversion. Common map spawn entities are translated automatically.
A source without spawns requires explicit coordinates. One shared location for both
teams produces a warning; the converter does not determine safe spawn positions.

--patch-steps accepts 1-32 subdivisions per Quake quadratic segment, default 6.
Source displacements use their authored full resolution and alternating cell diagonals.

Missing or unsupported requested textures receive a checkerboard and an explicit report
entry. --strict-textures fails instead. --graybox intentionally omits original textures
and cannot be combined with strict texture mode.

## CoD extraction setup

Use the pinned [OAT setup](IW3_TO_IW8.md#build-the-patched-unlinker). The existing command
now installs **IW3, IW4 and IW5** world/collision exporters:

~~~powershell
python mw120rproxy/tools/oat/configure_exporters.py external/OpenAssetTools --with-wavelet
~~~

Rebuild UnlinkerCli after installing the exporters. The generic importer honors
MW120R_UNLINKER from local.env.ps1, or accepts --unlinker. Its default is:

~~~text
external/OpenAssetTools/build/bin/Release_x86/Unlinker.exe
~~~

An existing IW4/IW5 dump must include:

~~~text
maps/mp/<source>.d3dbsp.iw8-world.json
maps/mp/<source>.d3dbsp.iw8-collision.json
maps/mp/<source>.d3dbsp.ents
xmodel/<model>.json and its referenced LOD0 OBJ files
material/<material>.json and referenced color images
~~~

IW3 uses .replay-world.json and .replay-collision.json instead. Direct FF import asks
Unlinker for DDS images and OBJ models. Missing required world/entity/model data is an
error with the extraction log retained. Use --search-path for external FF dependencies
and --asset-root for source images.

The exporters use actual OAT engine types and normal unpackers. They are pinned to OAT
commit 7d027e8f89118196713e955b0e11f8404149c54d. The IW4/IW5 installer checks the revision
and backs up changed registration files before installing.

## Output and limits

Each build retains:

~~~text
report.json              # status, counts, warnings, source/dependency/output hashes
REPORT.md                # conversion report
scene.json               # normalized scene
preview.png              # offline geometry illustration, not an engine screenshot
atlas.png                # color atlas preview, when textured
dump/                    # writer inputs
writer.log
validate.log
collision.bin
package/
  manifest.json
  <map>.ff
  srv_<map>.ff
  eng_<map>.ff
  ww_<map>.ff
  techsets_<map>.ff
  collision.bin
~~~

The importer derives world bounds, partitions native render surfaces, packs Replay
normals/winding and writes MWCOLL02 convex collision. Native package validation must pass
before a report becomes complete. Failed/interrupted output remains available for diagnosis.
The existing fromdump and validate-package commands remain available.

The current target supports up to 32,768 collision hulls, 4-252 vertices per hull,
4096 native render surfaces, and bounded finite coordinates. Large OBJ/GLB worlds may
need simpler source collision or partitioning. Limits produce errors, not truncation.

Base colors use an opaque 4096x4096 atlas with up to 1024 materials. Image input includes
common Pillow formats, DDS, selected IWI v6 and VTF 7.0-7.2 variants, and Quake II WAL
with its palette. More complex/animated/cubemap images need external image export.
Alpha remains opaque with a warning.

Scripts, objectives, AI/nav, source lighting/lightmaps, sky systems, animated doors,
triggers, effects and source physics are not translated by this generic importer.
Source props/overlays and displacement blend alpha/triangle tags are omitted. Transformed
Q3/Source brush submodels are rejected. Transformed CoD brush geometry is omitted with
warnings while compiled collision is retained, so those states require authored correction.

## Reproduce the checks

Basic importer tests run through .\build.ps1 -Tests. Downloaded inputs and native OAT
fixtures are optional for that run and explicitly skipped when absent.

For all importer fixtures, first build the converter, then run:

~~~powershell
python iw8-zonetool/tools/fetch_import_samples.py iw8-zonetool/test-out/import-fixtures/downloads

# OAT must be the pinned checkout with its submodules/build prerequisites prepared.
xmake f -P iw8-zonetool/tests -a x64 -m release -y
xmake -P iw8-zonetool/tests
.\iw8-zonetool\tests\bin\oat-export-fixture.exe iw8-zonetool/test-out/import-fixtures/oat_fixtures
python iw8-zonetool/tests/test_import.py

# Textured cases require the local shader templates described above.
python iw8-zonetool/tools/validate_imports.py --output test-out/import-validation
~~~

If OAT is elsewhere, add --oat=C:\Tools\OpenAssetTools to the xmake f command.
The exporter fixture target is available when that source checkout is present.

The runner converts nine cases, checks native packages and uses the actual proxy-side
collision disk parser. Add --replay-exe C:\Games\Replay\game_dx12_ship_replay.exe for exact
Replay layout checks. This reads the executable and does not start it.

Fixture downloads pin upstream commits and SHA-256 values and preserve attribution.
Inputs and generated packages stay in ignored directories. See
[the verification record](MULTI_ENGINE_VALIDATION.md) for results and evidence limits.

## References

- [OpenAssetTools](https://github.com/Laupetin/OpenAssetTools) supplies offline CoD loading and typed engine layouts.
- [Quake II](https://github.com/id-Software/Quake-2/blob/master/qcommon/qfiles.h) and [Quake III](https://github.com/id-Software/Quake-III-Arena/blob/master/code/qcommon/qfiles.h) define the BSP records.
- [Valve's BSP definitions](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/public/bspfile.h) and [displacement topology](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/public/disp_common.cpp) define the supported Source geometry.
- [glTF 2.0](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/Specification.adoc) defines the mesh, accessor and transform behavior.
