# Custom-map lighting

> **POC status:** Office has owner-tested lighting iterations, but arbitrary converted maps still
> need in-game verification. Direct IW3 conversion now generates map-specific compressed sun
> shadows; the first Office bake still needs a gameplay test. Serialization checks alone do not
> prove visual correctness.

MW120R uses the lighting serialized by the native zonetool. The proxy does not replace a converted map's sun direction, color, or intensity. This page describes the prepared-dump route; direct `build-iw3` conversion uses the source sun without requiring a lighting JSON file.

## Source sun

Every dump needs `maps/mp/<map-id>.d3dbsp.lighting.json` with schema 1:

```json
{
  "schema": 1,
  "intensity": 1.0,
  "color": [1.0, 0.96, 0.90],
  "direction": [0.25, -0.45, -0.86],
  "up": [0.0, 0.0, 0.0]
}
```

The compiler keeps the source direction and color. Adjust exposure with a positive multiplier:

```powershell
.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe build-map 'D:\Maps\Example\dump' mp_example -o 'D:\Maps\Example\output' --sun-intensity-scale 5.5
```

Change the multiplier in small steps and rebuild all five fastfiles. High values can wash out materials and emphasize shadow transitions.

## Baked and indirect light

Prepared-map staging can describe the converted GPU light grid with both files below:

```text
maps/mp/<map-id>.d3dbsp.gpulightgrid.json
maps/mp/<map-id>.d3dbsp.gpulightgrid.bin
```

The direct `build-iw3` route creates the same native staging data privately during conversion. The
compiler validates it and embeds it in the main fastfile; neither file is part of the final map
package. Keep prepared inputs from the same export. Incorrect counts, offsets, probes, or color
data can cause camera-dependent artifacts and poor indoor or viewmodel lighting.

Authored IW3 lightmaps feed Replay's native temporary atlas contract. A single source lightmap
keeps its source dimensions and local UVs. Multiple source lightmaps share a packed atlas with
correspondingly remapped UVs. Baked opaque surfaces use source-specific native Replay materials
and the native lightmap index. The atlas contains the three target planes in BC4, R11G11B10F,
and BC5 order. The single-lightmap UV correction passes a source-to-fastfile check across every
native world vertex in Office. The owner tested `lightmap-uv-1` and confirmed correct UVs and no
blackened faces in the map.

## Native compressed sun shadows

`build-iw3` bakes the fixed world and placed static models at their highest-detail LOD using the
final sun direction, shadow-caster flags, and converted material culling and alpha tests. Moving
objects, the sky, and breakable panes are excluded from permanent occlusion. Their supported
runtime shadow behavior remains in Replay's renderer.

The offline baker uses the Windows software Direct3D device. It stores exact depth samples in
Replay's 512-pixel mini-trees and embeds the forest, crop, and projection parameters in the main
fastfile. No extra input directory, shadow image, JSON configuration, or output sidecar is needed.
The final package remains five fastfiles and `map.json`. Prepared `build-map` inputs without an
IW3 caster scene do not automatically receive this bake.

The encoder has synthetic CPU and original Replay shader tests, including cropped forests,
empty cells, alpha cutouts, and depth normalization. It is lossless and does not reproduce the
shipped compiler's lossy compression. These checks establish the tested data contract, not the
live game's final shadow quality. Office `native-css-1` is the first gameplay candidate.

## Ambient occlusion

Screen-space ambient occlusion remains owned by MW2019's rendering engine. Lit generated passes
bind Replay's GTAO code image at `t95` and sampler at `s8`; the converter does not bake a custom AO
texture or install a rendering hook. Replay light-grid tetrahedron visibility is a separate baked
target structure. IW3 does not expose a proven transform for its per-corner flags and 64-byte
visibility records, so the converter keeps that optional path disabled rather than fabricating
lighting data.

The sky image is visual and should agree with the source sun. Do not use it as a second ambient-light override.

Compare the conversion with a source screenshot. Check sun and shadow direction, outdoor exposure, indoor ambient level, viewmodel lighting while standing and crouching, shadow stability and distance, and transparent foliage or fence materials.

Correct lighting and lightgrid data in the source dump and rebuild the map. Do not compensate for conversion errors with game rendering hooks.
