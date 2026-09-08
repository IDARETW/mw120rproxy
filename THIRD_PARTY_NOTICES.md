# Third-party notices

- **MinHook / HDE:** bundled under `mw120rproxy/thirdparty/minhook`; original BSD-style notices are retained in `LICENSE.txt` and source headers.
- **zlib 1.3.1:** bundled under `iw8-zonetool/src/common/zlib`; its license is retained in `zlib.h` and source headers.
- **nlohmann/json:** bundled as `iw8-zonetool/src/common/json.hpp`; its MIT/SPDX notice is retained in the header.
- **OpenAssetTools extensions:** `mw120rproxy/tools/oat/ReplayMapDumpers.h` and `iw8-zonetool/tools/oat/ReplayMapExport.h` extend OpenAssetTools v0.33.0 with IW3/IW4/IW5 map exporters. GPL-3.0 license text is included in both directories. The full upstream project is an external dependency, not bundled.
- **IW8 references:** mjkzy's `x64-zt` IW8 work was used as a reference for the converter. This project is not an official ZoneTool release.
- **CoD4 and MW2019:** game assets and executables are not included. The tests contain small data and instruction samples for compatibility checks. Extracted shaders and converted maps are excluded from source control.
- **Optional importer fixtures:** `iw8-zonetool/tools/fetch_import_samples.py` downloads commit-pinned samples from bsp_tool, Assimp and Khronos glTF-Sample-Assets into an ignored test directory and preserves their attribution/license files. These assets are not bundled. Khronos BoxTextured identifies Cesium and CC BY 4.0 in its model README.

The original project code is currently unlicensed. The third-party components listed above retain their existing licenses.
