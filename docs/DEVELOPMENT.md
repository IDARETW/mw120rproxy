# Development

The repository builds two C++ targets:

- `mw120rproxy`: the Replay 1.20 XInput proxy.
- `iw8-zonetool`: the native Replay map compiler.

Install Visual Studio 2022 Build Tools with the Desktop development with C++ workload and XMake 2.8 or newer. From the repository root, run:

```powershell
.\build.ps1 -Tests
```

This builds both targets and runs the C++ custom-map tests. Release outputs:

```text
mw120rproxy/xmake-out/x64/Release/XInput9_1_0.dll
iw8-zonetool/xmake-out/x64/Release/iw8-zonetool.exe
```

Use the checked-in clang-format styles for C++. Keep Replay addresses, byte signatures, structure sizes, streams, and ABI padding explicit.

The compiler accepts a generated IW3 map fastfile or a prepared dump and writes exactly five fastfiles. It has no Python dependency and writes no manifest or loose runtime data. See [Direct IW3 conversion](IW3_TO_IW8.md) and [Map dump input](../iw8-zonetool/docs/INPUT_FORMAT.md).

Validate a package before installation:

```powershell
.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe validate-package 'D:\Maps\Example\package' mp_example
```

Use [deploy_custom_map.ps1](../mw120rproxy/tools/deploy_custom_map.ps1) to install it. The script checks the Replay build, stages the files, compares hashes, and backs up the prior map.
