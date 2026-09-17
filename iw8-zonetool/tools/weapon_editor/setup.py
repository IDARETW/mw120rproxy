"""Prepare a local editor library from the owner's supported Replay installation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import uuid

from catalog import import_library
from layout import TARGET_SHA256, generate, load_schema, markdown
from tables import decode_csv
from stock import StockLibrary

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
TECHNIQUE = 'm/lit_3_lit_rpl_ta1_804040_1042000000000030_0_1_1_0_0_13814015b_0_0_1_0_0'
ARMS = 'mp_western_vm_arms_domino_1_1'


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')


def import_local_tables(root):
    import re
    tables = {}
    for zone in ('code_post_gfx', 'ui', 'global_mp'):
        folder = root / zone / 'assets/stringtable'
        for path in folder.rglob('*.csv'):
            name = re.sub(r'\.\d+\.csv$', '', path.relative_to(folder).as_posix()).lower()
            if name in ('mp/statstable.csv', 'mp/attachmenttable.csv', 'mp/attachmentmap.csv', 'loot/weapon_ids.csv') or name.startswith(('mp/gunsmith/', 'loot/iw8_', 'frontendscenedata/', 'ui/stringtable/weapon_')):
                tables[name] = decode_csv(path)
    for name, width in (('mp/statstable.csv', 61), ('mp/attachmenttable.csv', 33), ('loot/weapon_ids.csv', 7)):
        rows = tables.get(name, [])
        if not rows or any(len(row) != width for row in rows):
            raise ValueError('Missing or unexpected native table layout: ' + name)
    if not any(row[4] == 'iw8_pi_mike1911' for row in tables['mp/statstable.csv']):
        raise ValueError('Stats table permutation does not contain the expected Replay weapon names')
    if not tables.get('mp/attachmentmap.csv'):
        raise ValueError('Missing native attachment map')
    return tables


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True, help='Your Replay 1.20 installation')
    parser.add_argument('--exporter', type=Path, required=True, help='Patched ACTS executable; see extractor/README.md')
    parser.add_argument('--workspace', type=Path, required=True, help='Private data directory, preferably outside this checkout')
    parser.add_argument('--compiler', type=Path, default=REPO/'iw8-zonetool/xmake-out/x64/Release/iw8-zonetool.exe')
    parser.add_argument('--schema', type=Path, help='Defaults to data/mw19/schema.json beside ACTS')
    parser.add_argument('--config', type=Path, default=HERE/'config.local.json')
    args = parser.parse_args()
    args.game, args.exporter, args.workspace, args.compiler = (p.resolve() for p in (args.game, args.exporter, args.workspace, args.compiler))
    schema = (args.schema or args.exporter.parent/'data/mw19/schema.json').resolve()
    executable = args.game/'game_dx12_ship_replay.exe'
    for path in (executable, args.game/'oo2core_7_win64.dll', args.exporter, schema, args.compiler):
        if not path.is_file():
            parser.error('Required local file is missing: ' + str(path))
    if hashlib.file_digest(executable.open('rb'), 'sha256').hexdigest() != TARGET_SHA256:
        parser.error('Game executable is not the supported Replay 1.20 build; no files were extracted')
    zones = ('global_stream_mp', 'common_mp', 'code_post_gfx', 'ui', 'global_mp', 'techsets_global_stream_mp')
    for zone in zones:
        if not (args.game/'zone'/(zone+'.ff')).is_file():
            parser.error('Required game fastfile is missing: ' + zone + '.ff')
    from PIL import Image
    vendor = HERE/'node_modules'
    if not (vendor/'three/build/three.module.js').is_file():
        parser.error('Run npm ci in tools/weapon_editor first')
    args.workspace.mkdir(parents=True, exist_ok=True)
    if shutil.disk_usage(args.workspace).free < 2*1024**3:
        parser.error('Setup needs at least 2 GiB free in the workspace')
    # The reader resolves its schema beside the executable. Give this workspace a
    # private copy with the same Replay corrections used by the authoring graph.
    # Never overwrite a user's ACTS installation or another editor's schema.
    reader = args.workspace/'reader'
    reader.mkdir(exist_ok=True)
    for name in ('acts.exe', 'acts-common.dll'):
        source = args.exporter.parent/name
        target = reader/name
        if source.resolve() != target.resolve():
            shutil.copy2(source, target)
    corrected_schema = reader/'data/mw19/schema.json'
    write(corrected_schema, load_schema(schema))
    args.exporter = reader/'acts.exe'
    raw = args.workspace/'exports'
    raw.mkdir(exist_ok=True)
    logs = args.workspace/'logs'; logs.mkdir(exist_ok=True)
    # Reuse only successful exports made with the same inputs and selection.
    def extract(zone, selection=None, geometry=False, name=None):
        source = args.game/'zone'/(zone+'.ff')
        output = raw/'mw19replay'/zone
        marker = output/'.editor-setup.json'
        signature = {'source': str(source), 'bytes': source.stat().st_size,
                     'mtime': source.stat().st_mtime_ns, 'selection': selection,
                     'geometry': geometry, 'name': name, 'exporter_sha256': hashlib.file_digest(args.exporter.open('rb'),'sha256').hexdigest(),
                     'schema_sha256': hashlib.file_digest(corrected_schema.open('rb'),'sha256').hexdigest()}
        patch = source.with_suffix('.fp')
        signature['patch'] = {'bytes':patch.stat().st_size, 'mtime':patch.stat().st_mtime_ns} if patch.is_file() else None
        signature['reader_sha256'] = hashlib.file_digest((args.exporter.parent/'acts-common.dll').open('rb'),'sha256').hexdigest()
        if marker.is_file() and read(marker) == signature:
            print('Reusing ' + zone, flush=True)
            return output
        command = [str(args.exporter), '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', str(executable), '--oodle', str(args.game/'oo2core_7_win64.dll'), '-o', ('\\\\?\\' if os.name == 'nt' else '') + str(raw)]
        if selection:
            command += ['-a', selection]
        if geometry:
            command += ['--geometry']
        if name:
            command += ['-n', name]
        command.append(str(source))
        print('Extracting ' + zone + ' from your installation...', flush=True)
        with (logs/(zone+'.log')).open('w', encoding='utf-8') as log:
            result = subprocess.run(command, cwd=args.game, stdout=log, stderr=subprocess.STDOUT, timeout=600, creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        manifest = output/'manifest.json'
        if result.returncode or not manifest.is_file() or not read(manifest).get('complete') or read(manifest).get('failed'):
            raise ValueError('Extraction failed. See ' + str(logs/(zone+'.log')))
        write(marker, signature)
        return output
    weapons = extract('global_stream_mp', 'weapon,attachment')
    common = extract('common_mp', 'animpkg,sfxpkg,vfxpkg,xanim')
    for zone in ('code_post_gfx', 'ui', 'global_mp'):
        extract(zone, 'stringtable')
    materials = extract('techsets_global_stream_mp', 'material', name='m/mp_codl_helmet_cloth')
    # Generate a new library before publishing the configuration. Existing projects stay intact.
    library = args.workspace/('library-'+uuid.uuid4().hex[:8])
    library.mkdir()
    write(library/'tables.json', import_local_tables(raw/'mw19replay'))
    catalog = import_library(schema, weapons, common, weapons, library, library/'tables.json')
    if catalog['issues'] or not catalog['weapons'] or not catalog['attachments'] or not catalog['packages']:
        raise ValueError('Incomplete library. Inspect ' + str(library/'catalog.json'))
    layout = generate(schema)
    write(library/'weapon_layout.json', layout)
    write(library/'weapon_schema.json', load_schema(schema))
    (library/'WEAPON_LAYOUT.md').write_text(markdown(layout), encoding='utf-8')
    material = None
    for path in (materials/'assets/material').rglob('*.asset.json'):
        fields = read(path)['asset']['fields']
        if (fields.get('techniqueSet') or {}).get('name','').lstrip(',') == TECHNIQUE:
            material = path
            break
    if material is None:
        raise ValueError('Compatible single-UV material was not found in your game files')
    palette = library/'material/white.png'; palette.parent.mkdir()
    Image.new('RGBA', (1,1), 'white').save(palette)
    subprocess.run([sys.executable, str(HERE.parent/'prepare_weapon_material.py'), '--reference', str(material), '--palette', str(palette), '-o', str(palette.parent/'material.json')], check=True)
    # The base arms live in the persistent zone and use streamed XPak geometry.
    # Variant companion zones do not own that payload. Reuse the stock resolver.
    print('Preparing operator arms from your installed stock assets...', flush=True)
    stock = StockLibrary({'game':str(args.game), 'stock_exporter':str(args.exporter)},
                         library, args.workspace/'stock-library')
    native, strings, geometry = stock.model(ARMS)
    if geometry is None:
        raise ValueError('Operator arms geometry is missing from the installed game archives')
    xmodel = library/'arms/model.json'
    script_strings = library/'arms/script_strings.json'
    write(xmodel, {'asset':{'fields':native}})
    write(script_strings, strings)
    subprocess.run([sys.executable, str(HERE/'build_viewhands.py'), '--xmodel', str(xmodel), '--strings', str(script_strings), '--geometry', str(geometry), '--output', str(library/'viewhands.glb')], check=True)
    config = {'library':str(library), 'workspace':str(args.workspace/'projects'), 'vendor':str(vendor), 'compiler':str(args.compiler), 'game':str(args.game), 'stock_exporter':str(args.exporter), 'stock_cache':str(args.workspace/'stock-library'), 'animation_export':str(common)}
    args.config.parent.mkdir(parents=True, exist_ok=True)
    temp = args.config.with_suffix('.tmp')
    write(temp, config); os.replace(temp, args.config)
    print('Setup complete. Your game files were read, not modified.', flush=True)
    print('Start: ' + subprocess.list2cmdline([sys.executable, str(HERE/'server.py'), '--config', str(args.config)]))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        raise SystemExit('Setup failed: ' + str(error))
