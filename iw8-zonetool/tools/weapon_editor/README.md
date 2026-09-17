# Replay Weapon Workbench

See the [complete setup and authoring guide](../../docs/CUSTOM_WEAPONS.md).
This public version runs locally, uses browser file uploads, and requires the user's
own supported Replay installation. Start with `setup.py --help`; the generated local
configuration must exist before running `server.py`.

- `server.py`: loopback HTTP API, private projects, native builds.
- `web/`: Three.js viewer and authoring UI, including worker-based model conversion.
- `setup.py`: verified local game extraction and library generation.
- `extractor/`: pinned ACTS reader build and source patch.
- `layout.py`, `graph.py`, `catalog.py`, `tables.py`: native layouts and project graphs.
- `stock.py`, `animation.py`, `build_viewhands.py`: native stock inspection and playback.
- `material.py`, `sound.py`: owned native material and sound inputs.

Do not commit local configuration, generated libraries, exports, or projects. No
server-directory picker, private sample project, stock game data, or prebuilt game
modification is included. Use the main guide for requirements, limits, and checks.
