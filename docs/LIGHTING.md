# Custom-map lighting

MW120R uses the lighting serialized by the native zonetool. The proxy does not replace a converted map's sun direction, color, or intensity.

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
.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe build-map 'D:\Maps\Example\dump' mp_example -o 'D:\Maps\Example\package' --sun-intensity-scale 5.5
```

Change the multiplier in small steps and rebuild all five fastfiles. High values can wash out materials and emphasize shadow transitions.

## Baked and indirect light

Converted GPU lightgrid data uses both files below:

```text
maps/mp/<map-id>.d3dbsp.gpulightgrid.json
maps/mp/<map-id>.d3dbsp.gpulightgrid.bin
```

The compiler embeds both in the main map zone. Keep them from the same export. Incorrect counts, offsets, probes, or color data can cause camera-dependent artifacts and poor indoor or viewmodel lighting.

The sky image is visual and should agree with the source sun. Do not use it as a second ambient-light override.

Compare the conversion with a source screenshot. Check sun and shadow direction, outdoor exposure, indoor ambient level, viewmodel lighting while standing and crouching, shadow stability and distance, and transparent foliage or fence materials.

Correct lighting and lightgrid data in the source dump and rebuild the map. Do not compensate for conversion errors with game rendering hooks.
