# iw8-zonetool

Offline map and static-mesh conversion into MW2019 1.20 Replay packages. The
converter serializes world collision into the server fastfile using Replay's
native Havok builder and serializer; the game executable is mapped only in the
offline converter process and is never launched.

See the [multi-engine import guide](../docs/MULTI_ENGINE_IMPORT.md) for supported formats,
usage, dependencies, limitations and reproducible checks, or the
[existing IW3 workflow](../docs/IW3_TO_IW8.md) for CoD4 map-specific conversion features.

Build this project with XMake from this directory, or use the repository root
`build.ps1`. Install `requirements-import.txt`, then pass the supported
`game_dx12_ship_replay.exe` to `import --replay`. The native executable provides
`import`, `formats`, `fromdump` and `validate-package` commands. Keep `tools/`
with the source/build tree so `import` can locate its Python adapters.
