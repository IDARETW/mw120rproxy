# Direct IW3 fastfile conversion

The native `build-iw3` command reads a generated CoD4 multiplayer fastfile and writes the five
fastfiles expected by MW120R. It can also write optional `map.json` metadata. Extraction and
intermediate map data stay in a temporary directory and are deleted when the command finishes.

## Prepare OpenAssetTools once

Clone OpenAssetTools and check out the version used by this project:

```powershell
git clone https://github.com/Laupetin/OpenAssetTools.git
Set-Location OpenAssetTools
git checkout 7d027e8f89118196713e955b0e11f8404149c54d
```

From the MW120R source folder, install the IW3 map exporters:

```powershell
.\iw8-zonetool\tools\oat\configure_exporters.ps1 'D:\Tools\OpenAssetTools'
```

Build the OpenAssetTools `Unlinker` target according to its Windows build instructions. Keep the
resulting `Unlinker.exe`; it is the only external conversion program used by `build-iw3`.

## Convert a map

Build `iw8-zonetool`, set these values to paths that exist on your computer, and run the command
from the MW120R source folder in PowerShell:

```powershell
$mapName = 'mp_example'
$mapFolder = 'C:\Maps\mp_example'
$replay = 'D:\Games\iw8\1.20.4.7623265-replay\Call of Duty Modern Warfare (1.20.4.7623265)\game_dx12_ship_replay.exe'
$unlinker = '.\mw120rproxy\tools\_vendor\OpenAssetTools\build\bin\Release_x86\Unlinker.exe'
$zoneTool = '.\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe'
$output = Join-Path $mapFolder 'converted'

foreach ($file in @($zoneTool, (Join-Path $mapFolder "$mapName.ff"), $replay, $unlinker)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "File not found: $file"
    }
}

& $zoneTool build-iw3 `
    (Join-Path $mapFolder "$mapName.ff") `
    $mapName `
    -o $output `
    --replay $replay `
    --unlinker $unlinker `
    --search-path $mapFolder

if ($LASTEXITCODE -ne 0) {
    throw "IW3 conversion failed with exit code $LASTEXITCODE"
}
```

The values above are examples. `--unlinker` must name the `Unlinker.exe` that was actually built
after installing this project's exporters. Keeping the `.ff`, sibling `_load.ff`, and `.iwd`
files together lets one `--search-path` cover a downloaded map.

PowerShell and Command Prompt quote executable paths differently. In PowerShell, use the `&`
operator as shown above. In Command Prompt, use double quotes and do not use single quotes:

```bat
".\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe" build-iw3 "C:\Maps\mp_example\mp_example.ff" mp_example -o "C:\Maps\mp_example\converted" --replay "D:\Games\iw8\1.20.4.7623265-replay\Call of Duty Modern Warfare (1.20.4.7623265)\game_dx12_ship_replay.exe" --unlinker ".\mw120rproxy\tools\_vendor\OpenAssetTools\build\bin\Release_x86\Unlinker.exe" --search-path "C:\Maps\mp_example"
```

If a Command Prompt command begins with `'.\iw8-zonetool`, Windows includes the single quote in
the filename and reports `The system cannot find the path specified.`

The map id defaults to the `.ff` filename. It must begin with `mp_`, contain only lower-case
letters, numbers, or underscores, and fit the game's 15-character map-id field. The output folder
is created beside the source as `<map>_iw8`; use `-o` only when you need another location. To
rename a longer source map, add a shorter target id immediately after the input path:

```powershell
iw8-zonetool.exe build-iw3 'D:\CoD4\mp_old.ff' mp_new `
    --replay 'D:\Replay\game_dx12_ship_replay.exe' `
    --unlinker 'D:\Tools\OpenAssetTools\Unlinker.exe'
```

`build-iw3` also reads a sibling `<map>_load.ff` when present. `--search-path` is repeatable and
lets Unlinker resolve assets stored outside the map zone. `--unlinker` may be omitted when
`Unlinker.exe` is beside `iw8-zonetool.exe`, under `tools`, available on `PATH`, or named by the
`IW8_ZONETOOL_UNLINKER` environment variable. The earlier `MW120R_UNLINKER` variable is also
accepted.

If conversion reports that Unlinker cannot be started or that the file does not exist, run
`Test-Path -LiteralPath '<your Unlinker.exe path>'` in PowerShell. Fix that path before changing
any other option.

The output directory contains these five fastfiles:

```text
mp_example.ff
srv_mp_example.ff
eng_mp_example.ff
ww_mp_example.ff
techsets_mp_example.ff
```

No JSON is required. To set the lobby title or description, pass one small file with `--metadata`:

```json
{
  "title": "Example Map",
  "description": "Converted from CoD4"
}
```

`id` is also allowed when it matches the target map id. `map.json` is the only loose file accepted
beside the five fastfiles.

Installation validates the output automatically. To inspect a failed conversion manually:

```powershell
iw8-zonetool.exe validate-output 'D:\CoD4\zone\english\mp_example_iw8' mp_example
```

Direct conversion includes the playable world mesh, placed static models, collision, entities,
source sun, vertex colors, and an available HUD minimap. The world uses Replay's stock material.
IW3 and IW8 use different technique-set and shader layouts, so source materials cannot be copied
verbatim. Maps that need converted textures, baked lighting, doors, glass, ladders, or other
authored data should use `build-map` with the prepared input format.
