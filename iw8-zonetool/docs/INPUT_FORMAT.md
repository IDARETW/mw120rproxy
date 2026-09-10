# Map dump input

`build-map` reads one prepared dump directory. All map-specific files use the same asset name:

```text
maps/mp/<map-id>.d3dbsp
```

For `mp_example`, the minimum useful layout is:

```text
dump/
  mp_example_iw8_ents.txt
  maps/
    mp/
      mp_example.d3dbsp.render.json
      mp_example.d3dbsp.material.json
      mp_example.d3dbsp.techset.json
      mp_example.d3dbsp.lighting.json
      mp_example.d3dbsp.havok
      mp_example_atlas_0.rgba
```

Additional material definitions, techset definitions, atlas images, model dumps, and GPU lightgrid
data can be stored beside those files. The compiler discovers referenced assets from the dump and
adds them to the main map zone.

## Optional HUD minimap

Place the map's compass image at:

```text
dump/images/compass_map_<map-id>.iwi
```

The image must be an IW3 IWI version 6 file containing one resident DXT1, DXT3, or DXT5 surface.
The compiler writes both the Replay image and its `compass_map_<map-id>` 2D material into the main
fastfile. `mw120rproxy` supplies that material name through the map-info table, following the same
convention as shipped maps. When the image is absent, the build completes with a warning.

## Required data

### Render geometry

`<asset>.render.json` uses schema 1. It contains a primary material definition, optional additional
materials, and an array of surfaces. Each surface contains its material index, vertices, and
16-bit triangle indices. A surface is limited to 60,000 vertices, and its largest index must fit in
16 bits.

Each vertex supplies:

- `position`: three finite world coordinates.
- `uv`: two texture coordinates.
- `normal`: the packed IW8 tangent-frame value.
- `color`: four bytes.
- `lightmapUV`: two lightmap or source-channel coordinates when the material uses them.

### Material and techset definitions

The primary material JSON named by `materialDefinition` and every entry in
`additionalMaterials` must exist beside the render file. Material definitions may reference RGBA8
image files and a matching techset definition. Referenced compiled shader data stays in the dump;
the writer serializes it into the map fastfile.

### Lighting

`<asset>.lighting.json` uses schema 1 and contains:

```json
{
  "schema": 1,
  "intensity": 1.0,
  "color": [1.0, 1.0, 1.0],
  "direction": [0.0, 0.0, -1.0],
  "up": [0.0, 1.0, 0.0]
}
```

`direction` must be normalized. `up` may be zero or normalized and must be perpendicular to the
direction. `--sun-intensity-scale` multiplies only the source intensity.

### Collision

Use either of these inputs:

- Place a valid serialized Replay TAG0 blob at `<asset>.havok`.
- Pass `--replay`, `--collision`, and optionally `--footsteps` to build it in memory.

The second form accepts the MWCOLL02/MWCOLL03 collision format and MWRSTEP1 footstep data used by
the existing conversion pipeline. No intermediate `.havok` file is written.

### Entities and bounds

The compiler first looks for `dump/<map-id>_iw8_ents.txt`. It can also read the map entity dump and
convert supported IW3 spawn names. If neither exists, it emits a worldspawn and one deathmatch
spawn.

Optional `<asset>.bounds.json` supplies schema 1 `min` and `max` vectors. When omitted, available
clip-map bounds are used.

## Optional GPU lightgrid

Place `<asset>.gpulightgrid.bin` and `<asset>.gpulightgrid.json` beside the render file. The metadata
describes the native Replay lightgrid layout and the binary file contains its arrays. Both files
are consumed during compilation and embedded in the main fastfile.

## Output rule

The output directory must be empty or already contain only the same map's five fastfiles. The
compiler rejects unrelated entries instead of silently deleting them.
