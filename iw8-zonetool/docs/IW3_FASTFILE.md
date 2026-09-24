# Direct IW3 fastfile conversion

The native `build-iw3` command reads a generated CoD4 multiplayer fastfile and writes the five
fastfiles expected by MW120R plus `map.json`. Extraction and
intermediate map data stay in a private temporary directory beside the requested output and are
deleted when the command finishes. Use `-o` on a drive with room for both extraction and output.

## OpenAssetTools Unlinker

Direct IW3 extraction uses the native OpenAssetTools `Unlinker.exe`. A matching
build is kept with this workspace at
`mw120rproxy/tools/_vendor/OpenAssetTools/build/bin/Release_x86/Unlinker.exe`.
Pass that path with `--unlinker`, or let the compiler discover it beside the
ZoneTool binary or through `IW8_ZONETOOL_UNLINKER`.

If the bundled executable is not present, build the pinned OpenAssetTools source once:

```powershell
git clone https://github.com/Laupetin/OpenAssetTools.git
Set-Location OpenAssetTools
git checkout 7d027e8f89118196713e955b0e11f8404149c54d
```

From the MW120R source folder, install the IW3 map exporters:

```powershell
.\iw8-zonetool\tools\oat\configure_exporters.ps1 'D:\Tools\OpenAssetTools'
```

Build the OpenAssetTools `Unlinker` target according to its Windows build instructions. Keep the
resulting `Unlinker.exe`; it is the only external conversion program used by `build-iw3`.

If you built Unlinker with an older version of these exporters, run the configuration script
again and rebuild Unlinker. Model conversion now reads the original indexed IW3 vertices,
tangents, colors, and material assignments directly. An older exporter will report missing model
attributes. All extracted model data stays in the temporary conversion directory; the final
output is still five fastfiles and `map.json`.

## Convert a map

Build `iw8-zonetool`, set these values to paths that exist on your computer, and run the command
from the MW120R source folder in PowerShell:

```powershell
$mapName = 'mp_example'
$mapFolder = 'C:\Maps\mp_example'
$iw3Root = 'C:\Games\Call of Duty 4 Modern Warfare'
$replay = 'E:\IW8\Builds\1.20-replay\game_dx12_ship_replay.exe'
$unlinker = '.\mw120rproxy\tools\_vendor\OpenAssetTools\build\bin\Release_x86\Unlinker.exe'
$zoneTool = '.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe'
$output = Join-Path $mapFolder 'converted'

foreach ($file in @($zoneTool, (Join-Path $mapFolder "$mapName.ff"), $replay, $unlinker)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "File not found: $file"
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $iw3Root 'main') -PathType Container)) {
    throw "CoD4 main directory not found under: $iw3Root"
}

& $zoneTool build-iw3 `
    (Join-Path $mapFolder "$mapName.ff") `
    $mapName `
    -o $output `
    --replay $replay `
    --unlinker $unlinker `
    --iw3-root $iw3Root `
    --search-path $mapFolder

if ($LASTEXITCODE -ne 0) {
    throw "IW3 conversion failed with exit code $LASTEXITCODE"
}
```

The values above are examples. `--unlinker` must name the `Unlinker.exe` that was actually built
after installing this project's exporters. Keeping the `.ff`, sibling `_load.ff`, and `.iwd`
files together lets one `--search-path` cover a downloaded map.

PowerShell and Command Prompt quote executable paths differently. In PowerShell, use the `&`
operator as shown above. In Command Prompt, use double quotes and do not use single quotes:

```bat
".\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe" build-iw3 "C:\Maps\mp_example\mp_example.ff" mp_example -o "C:\Maps\mp_example\converted" --replay "E:\IW8\Builds\1.20-replay\game_dx12_ship_replay.exe" --unlinker ".\mw120rproxy\tools\_vendor\OpenAssetTools\build\bin\Release_x86\Unlinker.exe" --iw3-root "E:\CoD4\game_build\Call of Duty 4 Modern Warfare"
```

If a Command Prompt command begins with `'.\iw8-zonetool`, Windows includes the single quote in
the filename and reports `The system cannot find the path specified.`

The map id defaults to the `.ff` filename. It must begin with `mp_`, contain only lower-case
letters, numbers, or underscores, and fit the game's 15-character map-id field. The output folder
is created beside the source as `<map>_iw8`; use `-o` only when you need another location. To
rename a longer source map, add a shorter target id immediately after the input path:

```powershell
iw8-zonetool.exe build-iw3 'D:\CoD4\mp_old.ff' mp_new `
    --replay 'D:\Replay\game_dx12_ship_replay.exe' `
    --unlinker 'D:\Tools\OpenAssetTools\Unlinker.exe'
```

`build-iw3` also reads a sibling `<map>_load.ff` when present. The fastfile directory is searched
automatically. Pass `--iw3-root "C:\\Games\\Call of Duty 4 Modern Warfare"` to add its root,
`main`, and `raw` directories in one option. You may instead set `IW3_GAME_ROOT` or `COD4_ROOT`
to a CoD4 installation for repeated conversions. `--search-path` remains repeatable for custom
asset packs. `--unlinker` may be omitted when
`Unlinker.exe` is beside `iw8-zonetool.exe`, under `tools`, available on `PATH`, or named by the
`IW8_ZONETOOL_UNLINKER` environment variable. The earlier `MW120R_UNLINKER` variable is also
accepted.

The converter resolves exported material JSON from every explicit search root. CoD4's binary-only
`mc/lambert1` placeholder is represented by the native adapter, so it does not need a hand-written
material file. Other referenced images and materials still need to be present in the supplied map
directory, IWD extraction, or CoD4 `main`/`raw` roots; missing source data is reported by name.

If conversion reports that Unlinker cannot be started or that the file does not exist, run
`Test-Path -LiteralPath '<your Unlinker.exe path>'` in PowerShell. Fix that path before changing
any other option.

The output directory contains five fastfiles and `map.json`:

```text
mp_example.ff
srv_mp_example.ff
eng_mp_example.ff
ww_mp_example.ff
techsets_mp_example.ff
map.json
```

ZoneTool builds all six files in a private sibling directory and validates the complete package
before replacing the requested output directory. A failed conversion leaves the previous package
intact. The requested directory must be empty or contain only that map's six output files.

`techsets_mp_example.ff` owns the converted material, technique-set, and shader definitions.
`mp_example.ff` keeps the resident images and the native material references used by world and
model geometry, glass, and effects. The `eng_` and `ww_` companions can be empty.

Direct conversion also generates native compressed sun shadows for the fixed world and placed
static models. The bake follows the converted sun direction and material alpha/culling rules,
and embeds its forest and projection parameters in `mp_example.ff`. Windows' software Direct3D
device performs this offline step. It needs no additional package directory, input JSON, shader
export, or loose shadow file. Moving objects and breakable panes keep their runtime shadow paths.
This new bake has native shader and loader checks; its first Office iteration still needs
gameplay verification.

No input JSON is required. The converter creates `map.json` with the map id and a default title. To set a friendly lobby title or description, pass one small file with `--metadata`. `name` and `title` are accepted aliases and must match if both are present:

```json
{
  "name": "Example Map",
  "description": "Converted from CoD4"
}
```

`id` is also allowed when it matches the target map id. The supplied name and description are
written to the output `map.json`, which is the only loose file accepted beside the five fastfiles.
If an authored compass image is rotated relative to the map geometry, the same input metadata can
include `"compassRotation": 90` (degrees clockwise; accepted values are 0, 90, 180, and 270).
This rotates only the embedded `compass_map_<map>` pixels. It does not change worldspawn `northyaw`
or the `minimap_corner` bounds, and it does not add an output file. The default is 0. The converter
preserves BC1, BC2, and BC3 block endpoints and rotates their pixel selectors without recompression;
RGBA8 compasses are also supported. Other formats or incomplete resident images cause a clear
conversion error when rotation is requested.

Installation validates the output automatically. To inspect a failed conversion manually:

```powershell
iw8-zonetool.exe validate-output 'D:\CoD4\zone\english\mp_example_iw8' mp_example
```

Direct conversion reads the world mesh, placed model LODs, material assignments, collision,
supported entities, source sun, lightmaps, light grid, reflection probes, vertex colors, and an
available HUD minimap. Source DPVS cells and AABB trees preserve their surface and static-model
membership, including model-only trees, in Replay's native visibility layout. Model conversion
preserves the original indexed geometry and UVs, and
generates Replay material/technique assets from the source color, normal, and response images.
The server collision includes the authored collision LOD of placed static models that provide one.
Exported IW3 material surface-type bits are translated to Replay's native collision and impact types;
materials without a known source type use a name-based fallback.
Authored model culling and alpha coverage are carried into the generated passes. IW3 technique
sets themselves cannot be copied verbatim because the engines use different shader layouts.

The opaque BSP world path retains the IW3 specular image bytes, but its current
Replay material constants leave the shader's third-texture sampling branch
disabled. When enabled, that branch reads only the texture's red channel;
IW3's shader uses RGB specular color and alpha gloss. Their exact mapping to
Replay's separate response controls is not established, so world specular
appearance is still a fidelity gap even when the image asset loads.

Baked opaque surfaces use Replay's native lightmap inputs. One source lightmap keeps its original
dimensions and normalized coordinates; several source lightmaps share a packed atlas and receive
remapped coordinates. The owner confirmed correct UVs and no blackened map faces in Office's
`lightmap-uv-1` iteration. Native lighting and the new compressed sun-shadow bake remain under
development; package checks do not establish visual parity on every converted map.

Each source cubemap is converted into one slice of Replay's native 256-by-256 octahedral
reflection array. The compiler writes six mip levels in Replay's mip-major, slice-major order,
encodes them as BC6H UF16 with the bundled DirectXTex CPU encoder, and aligns every resident
subresource to 16 bytes. This data is embedded in `mp_<map>.ff`; conversion does not produce a
loose DDS, image cache, or reflection sidecar.

When the source entity string contains the stock pair of `script_origin` entities whose
`targetname` is `minimap_corner`, the compiler derives northwest and southeast world bounds using
the IW3 worldspawn `northyaw` rule. If `compass_map_<map>` is available, the main fastfile then
contains a native Replay startup `ScriptFile` that binds that material and those coordinates through
the engine's minimap builtin. The worldspawn also carries `northyaw`, using IW3's 90-degree default
when absent, so Replay validates the bounds in the intended orientation. No proxy-side image
substitution or loose script is required.

The same command serializes native glass and ladder data where those source features are
recognized. Glass definitions include Replay's native pane break-sound aliases; loader validation
confirms their references, while audible playback remains a gameplay check. A unique
`target`/`targetname` link from an intact glass brush to a shattered
`script_brushmodel` supplies the native shattered-material slot. Without an unambiguous authored link,
that slot retains the intact material. When linked brushes use different texture coordinates,
the converter embeds a shattered-material shader variant that maps the pane's native UVs to the
linked brush's UVs; no external shader or texture file is needed. The converter also
converts surface information into native footstep and collision tags in
`srv_<map>.ff`. These features do not require an additional export step, package directory,
`build-map` invocation, or loose sidecar. Supported multiplayer start and TDM spawn markers retain
their source origins and angles. Alternate-mode markers without a proven Replay consumer, including
DM and non-start Sabotage markers, are omitted with an explicit build warning.

Map-local `rawfile` declarations from the map and sibling load zones are embedded in the main
fastfile with Replay's native zlib-compressed `RawFile` layout. Payload bytes are preserved exactly,
including valid zero-byte marker files. A `.gsc` rawfile is retained as source text; the converter
does not compile IW3 source into Replay `ScriptFile` bytecode. Arbitrary IW3 gameplay scripts,
scripted doors/movers, and destructible systems are therefore not translated by this path yet.

The Unlinker collects map-local and shared FX source graphs and their typed
dependencies. `build-iw3` validates that data against the IW3 multiplayer ABI and embeds supported
single-image `effect_zfeather`, `effect_zfeather_add`, and `particle_cloud` materials plus supported
decal materials as native Replay materials and images.

The native graph translator currently supports complete graph closures made from billboard,
oriented-sprite, tail, cloud, model, decal, and runner elements. It preserves supported material,
model, and child-effect references together with spawn, lifetime, velocity, gravity, rotation,
size, color, and atlas data. Model-particle effect-on-impact edges use Replay's native
`TestImpact` module. IW3 emitted-effect edges use the reconstructed Replay 1.20 `TestBirth`
module, including the target build's 0x70 payload, linked-particle selector, child-state flag,
relative orientation, and dependency ordering. A source graph is emitted only when every element,
event child, and runner child in its closure is eligible; effect-on-death, effect-on-impact on other
element families, unsupported materials, and unsupported element types keep the graph out of the
output rather than creating a partial particle system.

The converter reads the IW3 impact table and maps its twelve semantic rows into Replay's native
impact table: small and large bullet hit/exit, shotgun hit/exit, armor-piercing hit/exit, grenade
bounce and explosion, rocket explosion, and projectile dud. Ordinary bullet-hit slots retain IW8's
native effects and decal emitters. Supported converted glass effects replace their bullet-glass
slots, and supported non-bullet effects can replace their corresponding native slots. Null or
unsupported source effects keep Replay's native fallback. The converted `impacts/small_glass`
effect is also wired into the native glass consumer. Successful structural validation checks the
generated package; it is not a gameplay or visual acceptance test for every converted feature.
