# Third-party notices

- **MinHook / HDE:** bundled under `mw120rproxy/thirdparty/minhook`; original BSD-style notices are retained in `LICENSE.txt` and source headers.
- **nlohmann/json:** bundled as `iw8-zonetool/src/common/json.hpp`; its MIT/SPDX notice is retained in the header.
- **DirectXTex BC6H encoder:** the minimal CPU BC6H source set from Microsoft DirectXTex commit `868198cb4bcbc4e359372e7ba38d7a6dda3a6afa` is bundled under `iw8-zonetool/src/thirdparty/directxtex`; its MIT license is retained as `LICENSE`.
- **OpenAssetTools extension:** `iw8-zonetool/tools/oat/ReplayMapDumpers.h` extends OpenAssetTools v0.33.0 with map exporters. The GPL-3.0 license text is included beside it. The full upstream project is an external dependency and is not bundled.
- **IW8 references:** mjkzy's `x64-zt` IW8 work was used as a reference for the converter. This project is not an official ZoneTool release.
- **CoD4 and MW2019:** game assets and executables are not included. The tests contain small data and instruction samples for compatibility checks. Extracted shaders and converted maps are excluded from source control.

The original project code is currently unlicensed. The third-party components listed above retain their existing licenses.
