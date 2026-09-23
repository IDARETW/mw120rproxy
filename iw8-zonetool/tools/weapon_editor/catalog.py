"""Import local Replay exports into a versioned authoring library."""
import argparse
from collections import Counter
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from prepare_melee_reference import prepare
from layout import load_schema
from weapon_models import model_slots_from_definition


def fields(record):
    result = []
    for f in record.get('fields', []):
        result.append(dict(f, record_bytes=len(record['data'])//2))
    for f in record['fixups']:
        if f['kind'] == 'record':
            result += fields(f)
        else:
            result.append({k: v for k, v in f.items() if k in ('field', 'offset', 'kind', 'asset_type', 'text', 'name', 'size')})
    return result


def import_library(schema_path, weapon_dir, common_dir, attachment_dir, output, table_fixture):
    schema = load_schema(schema_path)
    output.mkdir(parents=True, exist_ok=True)
    (output/'templates').mkdir(exist_ok=True)
    (output/'attachments').mkdir(exist_ok=True)
    (output/'packages').mkdir(exist_ok=True)
    weapon_strings = json.loads((weapon_dir/'script_strings.json').read_text())
    common_strings = json.loads((common_dir/'script_strings.json').read_text())
    attachment_strings = json.loads((attachment_dir/'script_strings.json').read_text())
    tables = json.loads(table_fixture.read_text())
    stats = {row[4]: row for row in tables['mp/statstable.csv'] if row[4]}
    catalog = {'format': 'replay-weapon-library-v1', 'weapons': [], 'attachments': [],
               'packages': [], 'issues': [], 'categories': []}
    sounds = {}
    # XAnim payloads are decoded on demand; do not parse thousands while indexing packages.
    package_files = (file for pool in ('animpkg', 'sfxpkg', 'vfxpkg')
                     for file in (common_dir/'assets'/pool).rglob('*.asset.json'))
    for file in package_files:
        document = json.loads(file.read_text())
        kind = document['root_type']
        if kind not in ('WeaponSFXPackage', 'WeaponVFXPackage', 'WeaponAnimPackage'):
            continue
        asset = document['asset']
        name = asset['fields']['name']['string']
        prepared = prepare(schema, document, common_strings, root_type=kind)['root']
        if kind == 'WeaponSFXPackage':
            sounds[name] = prepared
        dest = output/'packages'/f'{name}.json'
        dest.write_text(json.dumps({'type': kind, 'pool': document['pool_id'], 'name': name, 'root': prepared}))
        catalog['packages'].append({'name': name, 'type': kind, 'pool': document['pool_id'],
                                    'file': dest.relative_to(output).as_posix()})
    for file in (weapon_dir/'assets'/'weapon').glob('*.asset.json'):
        document = json.loads(file.read_text())
        try:
            native = document['asset']['fields']
            name = native['szInternalName']['string']
            definition = native['weapDef']['values'][0]
            sfx_name = ((definition.get('sfxPackage') or {}).get('name') or '').lstrip(',') or None
            root = prepare(schema, document, weapon_strings)['root']
            sfx = sounds.get(sfx_name)
            if sfx_name and not sfx:
                raise ValueError('SFX package is not available in common_mp: ' + sfx_name)
            reference = {'format': 'replay-weapon-reference-v1', 'source_weapon': name,
                         'source_sfx': sfx_name or '', 'root': root, 'sfx': sfx}
            base = name.removesuffix('_mp')
            row = stats.get(base)
            def enum(key):
                value = definition.get(key)
                return value.get('name', str(value.get('value'))) if isinstance(value, dict) else value
            descriptor = {'name': name, 'base': base, 'category': row[1] if row else enum('weapClass'),
                          'weapon_type': enum('weapType'), 'inventory_type': enum('inventoryType'),
                          'selectable': bool(row), 'title_key': row[3] if row else base,
                          'stats_row': row, 'source_file': str(file),
                          'file': f'templates/{name}.json', 'field_count': len(fields(root)) + (len(fields(sfx)) if sfx else 0),
                          'models': {k: ((definition.get(k) or {}).get('name') or '').lstrip(',') for k in
                                     ('gunXModel', 'worldModel', 'defaultViewModel', 'defaultWorldModel')},
                          'model_slots': model_slots_from_definition(definition),
                          'attachment_slots': [v['attachmentCount'] for v in native['attachments']]}
            (output/descriptor['file']).write_text(json.dumps(reference), encoding='utf-8')
            catalog['weapons'].append(descriptor)
        except Exception as error:
            catalog['issues'].append({'file': str(file), 'error': str(error)})
    for file in (attachment_dir/'assets'/'attachment').glob('*.asset.json'):
        document = json.loads(file.read_text())
        try:
            native = document['asset']['fields']
            name = native['szInternalName']['string']
            prepared = prepare(schema, document, attachment_strings, root_type='WeaponAttachment')['root']
            dest = output/'attachments'/f'{name}.json'
            dest.write_text(json.dumps({'format': 'replay-attachment-reference-v1', 'name': name, 'root': prepared}))
            catalog['attachments'].append({'name': name, 'file': dest.relative_to(output).as_posix(),
                                           'type': native['type'], 'weapon_type': native['weaponType'],
                                           'category': native['weapClass'], 'field_count': len(fields(prepared))})
        except Exception as error:
            catalog['issues'].append({'file': str(file), 'error': str(error)})
    for key in ('weapons', 'attachments', 'packages'):
        catalog[key].sort(key=lambda x: x['name'])
    catalog['categories'] = dict(Counter(w['category'] for w in catalog['weapons'] if w['selectable']))
    (output/'catalog.json').write_text(json.dumps(catalog, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({k: len(catalog[k]) for k in ('weapons', 'attachments', 'packages', 'issues')}))
    print(json.dumps(catalog['categories']))
    return catalog


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--schema', type=Path, required=True)
    p.add_argument('--weapons', type=Path, required=True)
    p.add_argument('--common', type=Path, required=True)
    p.add_argument('--attachments', type=Path, required=True)
    p.add_argument('--tables', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    import_library(a.schema, a.weapons, a.common, a.attachments, a.out, a.tables)
