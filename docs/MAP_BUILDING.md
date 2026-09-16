# Build a custom map

> **Proof-of-concept status:** this pipeline is for experimental conversion. A package can pass
> structural validation and still require map-specific live testing in Replay.

The native C++ compiler in 'iw8-zonetool' accepts a prepared dump and writes the five fastfiles
expected by MW120R plus 'map.json' metadata. Use [IW3 to IW8](IW3_TO_IW8.md) when the
starting point is a generated CoD4 map fastfile.

## Build the tools

From the repository root:

~~~powershell
.\build.ps1 -Tests
~~~

The compiler itself does not require Python. The preparation scripts under
'mw120rproxy/tools' are separate helpers and may have additional input or asset requirements.

## Prepare the dump

Use a lower-case map ID beginning with 'mp_' and no longer than 15 characters. A minimum useful
dump for 'mp_example' contains:

~~~text
dump/
  mp_example_iw8_ents.txt
  maps/mp/
    mp_example.d3dbsp.render.json
    mp_example.d3dbsp.material.json
    mp_example.d3dbsp.techset.json
    mp_example.d3dbsp.lighting.json
    mp_example.d3dbsp.havok
    mp_example_atlas_0.rgba
~~~

The render, material, techset, lighting, collision, entity, model, image, and optional native
lightgrid inputs are described in [Map dump input](../iw8-zonetool/docs/INPUT_FORMAT.md). Add the
HUD minimap at 'dump/images/compass_map_mp_example.iwi' when one is available. It must be an IW3
IWI version 6 image with one resident DXT1, DXT3, or DXT5 surface.

## Compile

Build a prepared dump with the native compiler:

~~~powershell
.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe build-map 'D:\Maps\Example\dump' mp_example -o 'D:\Maps\Example\output'
~~~

The compiler can read a pre-serialized native Havok blob at
'maps/mp/mp_example.d3dbsp.havok'. To bake collision in memory instead, provide the supported
Replay executable, an MWCOLL02 or MWCOLL03 collision input, and optional MWRSTEP1 footstep data:

~~~powershell
.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe build-map 'D:\Maps\Example\dump' mp_example -o 'D:\Maps\Example\output' --replay 'D:\Games\Replay\game_dx12_ship_replay.exe' --collision 'D:\Maps\Example\collision.native' --footsteps 'D:\Maps\Example\footsteps.native'
~~~

Use '—sun-intensity-scale' with a positive multiplier when the prepared lighting profile needs an
exposure adjustment. Rebuild the complete package after changing lighting or lightgrid data.

The output contains exactly:

~~~text
mp_example.ff
srv_mp_example.ff
eng_mp_example.ff
ww_mp_example.ff
techsets_mp_example.ff
map.json
~~~

'map.json' is always generated and is the only loose output file accepted beside those fastfiles. It may
contain a matching 'id', a lobby 'title' or 'name', and a 'description'. The compiler rejects
unrelated files in an existing output directory.

## Validate and install

Validate the package explicitly before handing it to the proxy:

~~~powershell
.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe validate-output 'D:\Maps\Example\output' mp_example
~~~

Close Replay, then install the package with the portable deployment helper:

~~~powershell
.\mw120rproxy\tools\deploy_custom_map.ps1 -GameRoot 'D:\Games\Replay' -PackageDir 'D:\Maps\Example\output' -Map mp_example
~~~

The helper validates again, copies only the five fastfiles plus 'map.json', verifies
SHA-256 hashes, and preserves the previous map folder under the game's '.proxy/backups' directory.
It does not launch the game.

Before sharing a conversion, test the geometry, movement, bullet collision, materials, sunlight,
interiors, footsteps, glass, ladders, spawns, and HUD minimap on the target Replay installation.
Those live checks are separate from this repository's offline validation.
