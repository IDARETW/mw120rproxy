# Replay 1.20 custom weapon workbench

The weapon workbench is a local web editor backed by ZoneTool's native Replay 1.20 weapon
writer. It creates standalone custom weapon packages for every stock multiplayer weapon
reference in the supported build. The reference contributes the engine-compatible default
graph; the project owns only the weapon data, models, materials, attachment packages,
animations, effects, and audio that the author changes.

## First-time local setup

This is a source-only tool for **Replay 1.20.4.7623265 on Windows x64**. You must
supply your own complete game installation, including the original executable,
`oo2core_7_win64.dll`, the `zone` fastfiles, and streamed `pak_*.xpak` archives.
Setup checks executable SHA-256
`68fb1cbcb2924182724004039de55a4c50152bb6561803c4898b7930b38132f0` before extraction.
Other game versions are not supported by these layouts or hooks.

The repository supplies source, not a weapon library. Stock models, arms, tags,
animations, loadout tables, native material parameters, and weapon references are
extracted locally from that installation. Your game files are not modified by setup.
No stock assets, private projects, game DLLs, or game-data downloads are included.

In addition to the repository's C++/XMake prerequisites, install Python 3.11 or newer,
Node.js with npm, Git, and CMake 3.20 or newer. Choose workspace and reader-build paths
with several GB free, preferably on an SSD: the reader build and stock library use
many small files. Stock previews add a cache as you open weapons. The first ACTS
build can take a while. Replace all example paths below with your own.

```powershell
# New checkout (main is the primary publication branch):
git clone --branch main https://github.com/IDARETW/mw120rproxy.git
Set-Location mw120rproxy

# From the repository root:
.\build.ps1
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r .\iw8-zonetool\tools\weapon_editor\requirements.txt
npm --prefix .\iw8-zonetool\tools\weapon_editor ci --ignore-scripts

# Build the pinned reader and its Replay skin/layout patch:
.\iw8-zonetool\tools\weapon_editor\extractor\build.ps1 `
  -SourceDir D:\Tools\replay-reader-src -OutputDir D:\Tools\replay-reader

# Generate your private library, hands, material profile, and animation cache:
.\.venv\Scripts\python.exe .\iw8-zonetool\tools\weapon_editor\setup.py `
  --game D:\Games\Replay `
  --exporter D:\Tools\replay-reader\acts.exe `
  --workspace D:\WeaponWorkbench

.\.venv\Scripts\python.exe .\iw8-zonetool\tools\weapon_editor\server.py
```

Open **[http://127.0.0.1:8766/](http://127.0.0.1:8766/)** in your browser. No tunnel,
protected link, or online account is required. The server binds to loopback. Model,
texture, sound, and animation imports open the browser's file picker; there is no
server-directory picker or arbitrary server-file import endpoint. Select a glTF file
and its `.bin`/image companions together. Stock loading reads only the game installation
configured during setup.

Setup writes the ignored `tools/weapon_editor/config.local.json`. It records the game,
compiler, reader, private workspace, generated library, and local Three.js paths. Use
`--config <path>` with setup and the server for another configuration. `--compiler`
selects a non-default ZoneTool build. The default is the repository's Release output.
Keep the reader's `data/mw19/schema.json` beside its executable; see
[reader build details](../tools/weapon_editor/extractor/README.md).

Successful extraction steps are cached by their input identity. Re-running setup
creates a new generated library, switches the config only after success, and preserves
projects. If setup fails, check `<workspace>/logs`; the previous config remains usable.
Generated libraries and exports are private game data: keep them outside the checkout
and do not include them when sharing editor source or custom projects.

## Opening a stock weapon

Click **Load stock weapon**, search a reference such as `kilo433`, `mike1911`, or
`akilo47`, and load it. This creates a separate project with the native weapon fields,
default attachment assembly, view/world geometry, original skeleton, tags, and skin
weights. The first load extracts the required companion fastfiles and can take longer;
later loads use the local cache. Some weapons also need the installed XPak archives.

Use **Rig** to inspect native bone/tag placements. Use **Animations** to select a
package and available clip, then Play, Pause, seek, change speed, or Loop. The single
operator-arms toggle applies to both model viewing and animation playback. Bone
overlays follow the animated pose. Reset pose returns to the authored placement.

Stock assemblies are inspection previews. Building them reuses their native model
and attachment references; editing a preview rig or material does not rebake stock
geometry. Import your own model before compiling replacement geometry. Native stock
materials are displayed with a neutral preview material, not reconstructed shaders.

## Authoring workflow

1. Create a weapon and choose the closest stock reference. The catalog covers all 167 stock
   multiplayer weapon definitions and every weapon family represented by them.
2. Import an OBJ, FBX, GLB, or glTF model. The editor converts exchange formats to the native
   OBJ/rig inputs used by the writer. External skin weights are not assumed to use Replay's
   native bind space, so assign named rigid parts to Replay bones for moving components.
3. Enable the operator-arms button in the viewport. This is one persistent toggle shared by
   model, rig, attachment, and animation views, so the same hands remain visible while placing
   the grip, IK locators, moving parts, and animated bones.
4. Use the placement controls for exact position, rotation, and scale values; optional snapping
   makes repeated gizmo edits deterministic. The toolbar's reference-wireframe button loads the
   selected stock weapon as a non-exported placement guide.
5. Import the model's companion material files and images with the model in the same file picker. Embedded FBX and glTF images are extracted before the material maps appear. OBJ materials load
   from their selected .mtl file; FBX and glTF material slots are retained separately. In
   **Imported material maps**, review each surface and select its base-color, normal, roughness,
   metallic, ambient-occlusion, specular, and emissive images. Filename matching ignores a
   .jpg/.jpeg extension mismatch, and every map can be reassigned manually. The live preview
   uses the same packed textures that the native weapon materials own.
6. Add compatible stock attachments or clone an attachment package. A cloned attachment can
   own custom view/world geometry, its bone, and an independent placement matrix. The assembled
   weapon and custom attachments are visible and selectable in the same viewport.
7. Import animated FBX or GLB clips, clone the relevant animation package, and bind each clip to
   one of that package's native XAnim event slots. The editor replaces the package's stock source
   wherever the weapon used it and retains the displaced animation for undo or remapping. Configure
   looping, notetracks, IK type, and finger pose, then preview the result on the weapon and visible
   operator arms. Unbound clips remain available as preview-only XAnim assets and produce a build
   warning until they are mapped.
7. Import PCM WAV files on the Sound page. Each source becomes a native alias in a resident SAB;
   map it to any exposed SFX event and set player/world/mechanical/UI defaults, volume, pitch,
   distance, and looping.
8. Clone native animation, SFX, and VFX packages when the weapon needs owned package records.
   The complete field inspector can change scalar, enum, string, script-string, record, and asset
   references while preserving the reference's native relocation graph.
9. Build from the Builds page. Validation errors identify the source file, field, asset, or graph
   that cannot be emitted safely.

The Data layout page documents 269 native types, 96 serialized record layouts, and 2,121 mapped
members from the supported Replay schema. It also shows every preserved padding or still-unmapped
range at its exact byte offset, accounting for the full record without inventing semantics.
Layout/runtime fields remain visible but read-only. This prevents pointer values, counts derived
from owned arrays, and internal runtime state from being authored as ordinary gameplay values.

## Native package layout

`build-weapon --project <build.json>` emits a base group and a common group. Each group follows
Replay's normal companion-zone contract:

```text
<weapon>.ff
techsets_<weapon>.ff
ww_<weapon>.ff
eng_<weapon>.ff
<weapon>_common.ff
techsets_<weapon>_common.ff
ww_<weapon>_common.ff
eng_<weapon>_common.ff
<weapon>.weapon.json
```

The base group owns the `WeaponCompleteDef`, loadout registration strings and tables, localized
UI strings, custom base models, materials, images, attachment definitions, and attachment models.
The common group owns the full SFX package, animation/effect packages, custom XAnim assets, the
transient SndBank, and its resident StreamKey/SAB data. Keeping those assets in their stock
lifetime prevents weapon references from surviving the zones that own their backing data.

The registration metadata tells mw120rproxy which base and common groups to request. The proxy
uses the normal fastfile loader and loadout enumeration path. It does not substitute models at
render time. Existing stock weapons remain present; a custom weapon receives its own reserved
loadout ordinal.

## Model, rig, and attachment rules

- View and world models are separate native XModels generated from the same source unless the
  project supplies different transforms.
- The rig stores Replay bone names, parent-relative bind transforms, optional per-part rigid
  assignments, native-space per-vertex weights when explicitly authored, material references,
  and view/world placement matrices.
- Imported attachment geometry is emitted as `<attachment>/custom_vm` and
  `<attachment>/custom_wm`. The owned attachment's native model-variation dependencies are
  rewritten to those assets; attachments without model dependencies are rejected rather than
  silently producing unused geometry.
- Operator arms are editor preview data. They provide the stock viewhands skeleton and mesh for
  placement and animation review; they are not duplicated into every custom weapon package.
- A source reference may still require weapon-specific animation or attachment tuning. Choosing
  the closest stock reference gives the safest starting graph.

## Audio and animation rules

PCM WAV input supports mono or stereo, 8/16/24/32-bit integer samples. The authoring server
normalizes it to signed 16-bit samples and writes Replay's resident raw-FLAC framing, SAB header,
entry hashes, and checksums. The native SndBank owns aliases which reference those SAB entry IDs;
the SFX package points weapon events at the aliases.

Custom animation clips are emitted as native XAnimParts with Replay script strings, bone tracks,
short or long frame indices, notetracks, frequency, looping state, IK type, and finger-pose type.
Animation preview drives matching weapon and operator-hand bones from the same imported tracks.

## Validation

The following focused checks live beside the editor:

```powershell
python .\iw8-zonetool\tools\weapon_editor\check_animation.py --help
python .\iw8-zonetool\tools\weapon_editor\check_animation_binding.py --help
python .\iw8-zonetool\tools\weapon_editor\check_native_sound.py --help
python .\iw8-zonetool\tools\weapon_editor\check_native_attachment.py --help
python .\iw8-zonetool\tools\weapon_editor\check_native.py --help
python .\iw8-zonetool\tools\weapon_editor\check_all_weapon_reload.py --help
python .\iw8-zonetool\tools\weapon_editor\check_registration.py --help
```

The native checks build fastfiles and reload them through the matching ACTS `mw19replay` reader.
They cover short/long animation indices, a weapon-to-owned-package-to-custom-XAnim gameplay
binding, attachment-owned view/world XModels, resident SAB decode, SndBank aliases, SFX event
mapping, every stock weapon reference, and custom package graphs.
`check_all_weapon_reload.py` also reloads every generated stock-reference weapon zone
and verifies its weapon, NetConstStrings, and StringTable asset counts. This establishes
serialization and loader compatibility. `check_registration.py` exercises loadout registration,
Gunsmith tables, native attachment maps, and custom attachment rows across every reference.
Final gameplay behavior,
first-person placement, third-person/world placement, animation event timing, and sound mix still
require a live Replay test.

Use only the supported Replay executable and assets extracted from that exact build. Numeric
layouts, asset IDs, and loader behavior from another IW8 build are not interchangeable.

## Preview limits

FBX, GLB, and glTF imports wait for source images to finish loading before material maps are
extracted. Embedded images are saved into the project, and imported material slots remain
assigned to their corresponding model surfaces. The Replay weapon profile stores color/specular, normal/gloss, and emissive
images. During packing, ambient occlusion is multiplied into base color, roughness is inverted
into the gloss channel, and metallic values contribute to the available specular channel; Replay's
profile does not expose a separate metallic image slot. If an external texture is absent or
incorrectly linked, choose its correct image in **Imported material maps**. Source files are
limited to 128 MiB (512 MiB for the canonical OBJ), with chunked uploads.

Native animation clips drive matching weapon and operator bones. Imported
geometry needs named-part assignments to move its magazine, bolt, and other
parts. Missing native clip payloads are disabled in the picker. Additive clips are
previewed alone, root-motion deltas are omitted, and notetracks do not yet trigger
sound or effect playback. SFX/VFX package editing and native serialization are
separate from live visual/audio simulation.

The layout index accounts for serialized bytes and labels unknown or padding ranges;
it does not claim every engine field's behavior is understood. All stock weapon
families can supply reference data. Special non-loadout weapons may require a
selectable loadout reference before registration. Offline build checks do not prove
every authored weapon/attachment combination will work in the game.

## Install a built weapon

The Builds page reports the package output directory. Close Replay, then deploy all
eight companion fastfiles and their metadata together:

```powershell
.\mw120rproxy\tools\deploy_custom_weapon.ps1 `
  -GameRoot D:\Games\Replay `
  -PackageDir D:\WeaponWorkbench\projects\<project-id>\builds\<build-id>\output `
  -ProxyPath .\mw120rproxy\xmake-out\x64\Release\XInput9_1_0.dll
```

Use the actual output path shown by the editor. The helper checks the supported
executable, package headers and metadata, and the proxy; backs up replaced files;
stages and verifies the copies; and never starts or stops Replay. For a first proxy
installation, run the repository's `install.ps1` first to create the configuration.
Keep `custom_map_loader=1` in `mw120rproxy.ini` (the default) for native package
loading and loadout asset extension.

Start Replay, select the custom weapon in a custom class, inspect it in Gunsmith,
and enter a local match with that class. Check view/world geometry, fire/reload,
attachments, and audio in game. Do not overwrite the stock weapon fastfiles.
Back up saved loadouts before removing packages that those loadouts reference.
Do not assign two installed packages the same reserved loadout ordinal.

## Developer checks

The checks consume your generated library and write results outside source control:

```powershell
.\.venv\Scripts\python.exe .\iw8-zonetool\tools\weapon_editor\check_public.py `
  --config .\iw8-zonetool\tools\weapon_editor\config.local.json
.\.venv\Scripts\python.exe .\iw8-zonetool\tools\weapon_editor\check_registration.py `
  --library D:\WeaponWorkbench\library-<id> --out D:\WeaponWorkbench\checks\registration.json
.\.venv\Scripts\python.exe .\iw8-zonetool\tools\weapon_editor\check_native.py `
  --library D:\WeaponWorkbench\library-<id> `
  --compiler .\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe `
  --out D:\WeaponWorkbench\checks\native
```

Use `--help` on the animation, stock-assembly, and round-trip checks for their inputs.
The optional native `custom-weapon-tests` target requires private native table/DDL
fixtures and is separate from the map test target.
