# Multi-engine importer verification

The public-repository integration was checked on 2026-09-08 using Windows x64,
MSVC 2026, Python 3.14 and Pillow 12.3.0. The converter and native helper tools built,
and all 48 importer tests passed with no skips. Python/C++ formatting, Python lint and
PowerShell parsing checks passed.

Ten output packages passed native package validation, all 50 fastfiles passed exact
Replay layout checks, and all ten collision files passed the actual proxy-side disk
parser. The layout verifier reads the executable; it does not run the game.
**No game tests or deployment were performed for this change.**

| Case | Source evidence | Triangles | Collision hulls | Textures |
|---|---|---:|---:|---|
| q2 | downloaded BSP | 52 | 6 | 0 resolved, 1 missing |
| q3 | downloaded BSP | 12 | 6 | 0 resolved, 1 missing |
| source | downloaded BSP | 32 | 6 | 0 resolved, 1 missing |
| terrain | downloaded Source displacement BSP | 96 | 102 | 0 resolved, 1 missing |
| obj | downloaded OBJ/MTL and images | 1,312 | 1,312 | 4 resolved, 0 missing |
| gltf | downloaded GLB with embedded image | 12 | 12 | 1 resolved, 0 missing |
| ql | controlled quadratic patch fixture with v47 header | 72 | 73 | Graybox |
| iw4 | native OAT IW4 production exporter fixture | 2 | 1 | Graybox |
| iw5 | native OAT IW5 production exporter fixture | 2 | 1 | Graybox |
| iw3_office | Complete local CoD4 FF, graybox output | 368,609 | 17,209 | Graybox |

The downloaded BSPs are small compiled test maps and lack their original stock textures;
checkerboard replacements are explicit. The OBJ and GLB cases passed strict texture
resolution. Their meshes are downloadable asset fixtures, not production levels.

IW4 and IW5 use the production exporters against native OAT structures. Full real-map
fastfile validation for those two games remains open. Quake Live uses a controlled
version-47 curved patch. These fixtures are not presented as downloaded game maps.

Office retained 752 static-model placements and 39 spawn locations, consolidating 5,026
source surfaces into 18 Replay surfaces. Its graybox run omitted original textures.
The source Unlinker reported 399 missing image payloads; all 399 reported errors
were image lookup failures. Required world, model, entity and collision inputs converted
successfully. This result does not establish textured Office conversion through the
generic importer. Counts of removed zero-area model/OBJ faces are retained in the record.

The pinned OAT exporter installer was exercised twice against a clean checkout and
registered IW3/IW4/IW5 dumpers without duplicate registrations. The native fixture tests
also check invalid exporter index ranges.

See [the machine-readable results](multi_engine_validation.json) for counts and output
hashes, and [the import guide](MULTI_ENGINE_IMPORT.md#reproduce-the-checks) to reproduce
the checks. Generated fastfiles, source maps, extracted shaders, executable files and
machine-specific logs are excluded from the repository.

The fixture downloader pins commits/hashes from [bsp_tool](https://github.com/snake-biscuits/bsp_tool),
[Assimp](https://github.com/assimp/assimp) and [Khronos glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets)
and retains their attribution files.
