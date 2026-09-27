# Replay 1.20 static PhysicsAsset BVH audit

`validate_pa_bvh_packet_enclosure.py` is a fixture-specific, offline check for
the `mp_4doffice [mesh-quantization-1]` server fastfile. It reads exported
PhysicsAsset JSON/TAG0 data, the exact Replay 1.20 executable, and one signed
Shipment PhysicsAsset control. It does not load a map in game or change a
fastfile. `validate_shelf_bvh_coverage.py` supplies the shared TAG0 parser.

The validator pins SHA-256 hashes for the executable and both fastfiles, and
checks the executable's 191-byte `hkcdStaticMeshTree::Base::getNextKey` body at
RVA `0x1E2DD90`. The verified mapping is:

```text
section      = key >> 8
local        = (key & 0xff) >> 1
packet_index = Section[section].firstPrimitive + local
```

Given full non-test asset exports from those pinned fastfiles, set the
following PowerShell variables to the corresponding absolute paths, then run:

```powershell
python .\validate_pa_bvh_packet_enclosure.py `
  --replay-pe $replayPe `
  --office-assets $officeExportDirectory `
  --office-fastfile $officeServerFastfile `
  --office-manifest $officeExportManifest `
  --stock-control $shipmentControlAssetJson `
  --stock-fastfile $shipmentFastfile `
  --stock-manifest $shipmentExportManifest `
  --tolerance 0.001 `
  --output $reportJson
```

The recorded run found 44 Office static PhysicsAssets with 8,468 distinct
reachable leaf keys covering 8,468 primitive packets. All 33,872 decoded
packet vertices lie inside their associated leaf bounds at 0.001 tolerance.
The signed Shipment control passed for 16 packets and 64 vertices. This is a
serialized BVH topology and enclosure result. It does not establish runtime
bullet/player query masks, game contacts, glass breaking, or map playability.

Do not use global `key / 2` as the primitive index for multi-section assets.
For example, Office `smodel_10.10` has section 1 `firstPrimitive=92`; leaf key
`0x0100` maps to packet 92, while `key / 2` gives 128.
