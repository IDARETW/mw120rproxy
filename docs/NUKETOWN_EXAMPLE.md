# Nuketown example map

This is a ready-to-install conversion of **Nuketown by >>N.B.Z.I.<<** for
MW120R and **MW2019 Replay 1.20.4.7623265**. The map is listed in Local Play as
**Nuketown (CoD4)** and uses the ID `mp_nuketown`.

This release uses the earlier package layout and includes its manifest and sidecar files. It is a
ready-to-play example, not the normal output of current `build-iw3` conversion. New direct
conversions contain five fastfiles and, optionally, `map.json` metadata.

Download [mw120r-nuketown-example-v1.zip](https://github.com/IDARETW/mw120rproxy/releases/download/nuketown-example-v1/mw120r-nuketown-example-v1.zip)
from the [Nuketown example release](https://github.com/IDARETW/mw120rproxy/releases/tag/nuketown-example-v1).
Download the named ZIP asset, not GitHub's automatically generated source-code
archives.

## Requirements

- Windows x64 and the supported Replay installation.
- `game_dx12_ship_replay.exe`, MD5 `1c238fe327f2ecc3b0db924c5b425439`.
- The current MW120R proxy installed beside the game executable. Follow
  [Build and install](../README.md#build-and-install) if it is not installed yet.

The download contains the converted map, not the game or the proxy DLL. You do
not need CoD4, Radiant, an asset dump, or shader extraction to play this example.

## Install

1. Download the ZIP and extract it, for example into `D:\Maps\Nuketown`.
2. Close Replay.
3. Open PowerShell in your MW120R repository and run the installer below. Replace
   the example game and extraction paths with yours.

```powershell
.\mw120rproxy\tools\deploy_custom_map.ps1 `
    -GameRoot 'D:\Games\Replay' `
    -PackageDir 'D:\Maps\Nuketown\mp_nuketown' `
    -Map mp_nuketown
```

The installer uses the native converter built by `build.ps1`. It validates the five fastfiles,
verifies the installed hashes, and preserves an existing installed version in `.proxy/backups`.

If the mod is already set up and you are copying the package manually, copy
the complete `mp_nuketown` folder into `mods/mw120r/maps` under your game
directory. Keep a copy of any previous `mp_nuketown` folder before replacing it.
The final layout must be:

```text
<Replay>/mods/mw120r/maps/mp_nuketown/
    manifest.json
    mp_nuketown.ff
    srv_mp_nuketown.ff
    eng_mp_nuketown.ff
    ww_mp_nuketown.ff
    techsets_mp_nuketown.ff
    collision.bin
    footsteps.bin
    glass.bin
    ladders.bin
    ambient.bin
    preview.rgba
```

Keep all twelve files together. Do not rename the map folder or put another
`mp_nuketown` folder between it and the legacy `manifest.json`.

## Play

1. Start Replay and enter **Multiplayer → Local Play**.
2. Open **Game Setup → Map** and select **Nuketown (CoD4)**.
3. Choose **Team Deathmatch**, with **zero bots** for the first match.
4. Start the match, select a loadout, and spawn.

You can also select it through the **F6** custom-map browser. Return to the
Local Play lobby before switching maps. Press **F7** or **~** for the console;
`noclip` toggles local-player noclip for exploring the map.

## Included features and limits

The package includes world geometry, static props, collision, TDM spawns,
breakable glass, footstep material data, ambient lighting, and custom menu and
loading artwork. It is the existing Nuketown conversion that has been tested
in Replay; this download does not apply the optional lighting rebuild.

Team Deathmatch is the supported mode for this conversion. CoD4 scripts, the
original map's other gametypes, and PeZBOT waypoints are not included. Bot
navigation is not converted. Indoor lighting and native weapon lighting retain
the limitations described in [Custom-map lighting](LIGHTING.md).

If the map is missing from the menu, check the folder layout above, restart the
game, or press **R** in the F6 browser to refresh the list. For a loading error,
check `mw120rproxy.log` and `mw120rproxy.exceptions.log` beside the game
executable. Avoid mixing files from different map builds.

## Credits and checksums

- Original CoD4 map: **>>N.B.Z.I.<<**, version 1.0.
- Original Nuketown design: **Treyarch**.
- CoD4 Mod Tools: **Infinity Ward**.
- MW120R conversion and packaging: **RETW**.
- [Original map download page](https://cfgfactory.com/skins/show/4dd29e0b98fae.html).

The archive includes the original map README for attribution. Its installation
instructions and gametype list describe the CoD4 version; use this guide for
Replay.

The release includes `SHA256SUMS.txt` for the ZIP. To compare your download:

```powershell
Get-FileHash -Algorithm SHA256 'D:\Downloads\mw120r-nuketown-example-v1.zip'
```

`MAP_FILES.sha256` inside the ZIP lists checksums for the twelve map files.
