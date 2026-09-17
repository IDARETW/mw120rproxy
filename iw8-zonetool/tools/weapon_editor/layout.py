"""Build the complete weapon-family layout index from the versioned local schema.

Asset pointers terminate a record graph. Their native pool layouts remain linked,
without silently importing another game's numeric asset IDs or member offsets.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path

ROOTS = ('WeaponCompleteDef', 'WeaponDef', 'WeaponAttachment', 'WeaponAnimPackage',
         'WeaponSFXPackage', 'WeaponVFXPackage')
TARGET_SHA256 = '68fb1cbcb2924182724004039de55a4c50152bb6561803c4898b7930b38132f0'
LOADERS = {'WeaponCompleteDef': '0xD99D00', 'WeaponDef': '0xD9A580',
           'WeaponAttachment': '0xE0E480', 'AttProjectile': '0xE08FF0',
           'WeaponOffsetPatternScaleInfo': '0xE20BC0'}


def load_schema(source):
    schema = json.loads(Path(source).read_bytes())
    # Exact Replay attachment loader E0F804/E0F82A consumes a 16-byte
    # WeaponOffsetPatternScaleInfo at +248, not a 256-byte SwaySettings.
    attachment = schema['types']['Replay::WeaponAttachment']
    # Replay predates the attachment HyperBurstInfo pointer. The +240
    # consumer E1F6F0 reads a 0x100 SwaySettings, including advanced hip sway.
    sway = next(m for m in attachment['members'] if m['offset_bits'] == 0x240 * 8)
    if sway['type'] not in ('HyperBurstInfo *', 'SwaySettings *'):
        raise ValueError('Unexpected Replay attachment +240 layout')
    sway.update(name='swaySettings', type='SwaySettings *')
    member = next(m for m in attachment['members'] if m['offset_bits'] == 0x248 * 8)
    if member['name'] == 'swaySettings' and member['type'] == 'SwaySettings *':
        member.update(name='weaponOffsetPatternScaleInfo', type='WeaponOffsetPatternScaleInfo *')
    elif member['type'] != 'WeaponOffsetPatternScaleInfo *':
        raise ValueError('Unexpected Replay attachment +248 layout')
    # E08FFD reads F0 bytes; E0901B resolves the projectile model at +28.
    # The later source build added six stepped-explosion ints at +14..+28.
    projectile = copy.deepcopy(schema['types']['AttProjectile'])
    if projectile['size'] != 0x108:
        raise ValueError('Unexpected source AttProjectile layout')
    projectile.update(name='Replay::AttProjectile', size=0xF0)
    projectile['members'] = [m for m in projectile['members'] if not m['name'].startswith('iExplosionStepped')]
    for m in projectile['members']:
        if m['offset_bits'] >= 0x2C * 8:
            m['offset_bits'] -= 0x18 * 8
    schema['types']['Replay::AttProjectile'] = projectile
    schema['profiles']['replay-1.20']['type_overrides']['AttProjectile'] = 'Replay::AttProjectile'
    # Replay's E0E480 inline ammunition loader consumes 0x1C bytes. The
    # requireAmmoUsedPerShot boolean was added after this build.
    ammunition = copy.deepcopy(schema['types']['AttAmmunition'])
    if ammunition['size'] != 32:
        raise ValueError('Unexpected source AttAmmunition layout')
    ammunition.update(name='Replay::AttAmmunition', size=28)
    ammunition['members'] = [m for m in ammunition['members'] if m['offset_bits'] < 28*8]
    schema['types']['Replay::AttAmmunition'] = ammunition
    schema['profiles']['replay-1.20']['type_overrides']['AttAmmunition'] = 'Replay::AttAmmunition'
    return schema


def generate(source):
    raw = Path(source).read_bytes()
    schema = load_schema(source)
    profile = schema['profiles']['replay-1.20']
    overrides = profile['type_overrides']
    pools = {p['root_type']: p for p in profile['pools']}
    types, external = {}, {}

    def walk(name):
        if name in types:
            return
        resolved = overrides.get(name, name)
        original = schema['types'][resolved]
        item = dict(original, name=name, resolved_type=resolved)
        types[name] = item
        if original['kind'] == 'pointer':
            target = original['target']
            if target in pools and target not in ROOTS:
                external[target] = pools[target]
            else:
                walk(target)
        elif original['kind'] == 'array':
            walk(original['element'])
        elif original['kind'] in ('struct', 'union'):
            item['members'] = []
            covered = set()
            for member in original['members']:
                m = dict(member, offset=member['offset_bits']/8, size=member['size_bits']/8)
                start, length = member['offset_bits'], member['size_bits']
                if start < 0 or length < 0 or start + length > original['size'] * 8:
                    raise ValueError(f'{name}.{member["name"]}: member outside record')
                covered.update(range(start, start + length))
                rule = schema['pointer_rules'].get(name, {}).get(member['name'])
                if rule:
                    m['pointer_rule'] = rule
                m['semantic_status'] = 'unknown' if 'unknown' in member['name'].lower() else 'named in schema'
                item['members'].append(m)
                walk(member['type'])
            gaps, start = [], None
            for bit in range(original['size'] * 8 + 1):
                if bit < original['size'] * 8 and bit not in covered:
                    if start is None:
                        start = bit
                elif start is not None:
                    gaps.append({'offset_bits': start, 'size_bits': bit-start})
                    start = None
            item['unmapped_ranges'] = gaps
            item['named_bits'] = len(covered)
            item['evidence'] = {'layout': 'Replay schema override' if resolved != name else 'shared schema; validate against Replay consumer',
                                'loader_rva': LOADERS.get(name),
                                'pointer_counts': 'See each member; source-derived rules are not independent Replay proof'}
            if name in schema['union_rules']:
                item['union_rule'] = schema['union_rules'][name]
        if name in pools:
            item['asset'] = pools[name]

    for root in ROOTS:
        walk(root)
    structs = [t for t in types.values() if t['kind'] in ('struct', 'union')]
    return {'format': 'replay-weapon-layout-v1', 'target': profile['module'],
            'target_sha256': TARGET_SHA256, 'schema_sha256': hashlib.sha256(raw).hexdigest(),
            'roots': list(ROOTS), 'types': dict(sorted(types.items())),
            'external_assets': external,
            'summary': {'types': len(types), 'records': len(structs),
                        'members': sum(len(t['members']) for t in structs),
                        'enums': sum(t['kind'] == 'enum' for t in types.values()),
                        'unmapped_bytes': sum(sum(g['size_bits'] for g in t['unmapped_ranges']) for t in structs)/8},
            'serialization': {
                'root_stream': 0, 'trailing_data_stream': 5, 'pointer_size': 8,
                'script_string_size': 4,
                'pointers': {'0': 'null', '-1': 'shared inline data', '-2': 'inline follows',
                             '-3': 'inserted pointer', 'positive': 'packed stream offset or asset alias'},
                'warning': 'Offsets are relative to individual records. They are not absolute .ff file positions.',
                'exceptions': ['WeaponDef accuracy graphs: name[0], knots[0], name[1], knots[1]; not simple offset order.'],
                'zones': {'global': ['WeaponCompleteDef', 'WeaponDef', 'models', 'attachments', 'tables', 'weapon NetConstStrings'],
                          'common': ['WeaponSFXPackage', 'sound bank dependencies'],
                          'techsets_global': ['materials', 'resident images', 'shader dependencies'],
                          'english_global': ['localized names and descriptions'],
                          'companions': 'Both lifetime groups require techsets, ww and language companions.'}}}


def markdown(data):
    s = data['summary']
    out = ['# Replay 1.20 weapon fastfile layout', '',
           f"{s['records']} structures/unions, {s['members']} named members, {s['enums']} enum types.", '',
           'This is the complete transitive weapon-family schema, including fields absent from the pistol reference.',
           'External asset pointers terminate the record graph and link to their Replay pool identity and size.',
           'A named field is not a claim that its gameplay behavior has been tested. Unmapped ranges and',
           'source-derived pointer-count rules are explicitly reported. Never transfer addresses from another IW8 build.', '',
           f"Target SHA256: `{data['target_sha256']}`", '',
           '## Serialization', '',
           'Root records use stream 0; trailing strings and owned records use stream 5. Pointers are 8 bytes;',
           'script strings are 4-byte indices. On-disk pointers are sentinels/packed offsets, not process addresses.',
           'An asset reference is resolved through the native asset list; a name-only dependency starts with a comma.',
           'Member offsets below are relative to their record, not absolute compressed fastfile offsets.', '',
           'WeaponDef accuracy graph pointers are consumed as name[0], knots[0], name[1], knots[1].',
           'The writer must follow the native loader traversal, which is not always increasing field offset.', '',
           '## Root assets', '', '| Record | Size | Pool | Known Replay loader RVA |', '|---|---:|---:|---|']
    for name in data['roots']:
        t = data['types'][name]
        out.append(f"| {name} | 0x{t['size']:X} | {t.get('asset', {}).get('id', 'embedded')} | {LOADERS.get(name, 'see schema/evidence')} |")
    out += ['', '## Complete records', '']
    for name, t in data['types'].items():
        if t['kind'] not in ('struct', 'union'):
            continue
        out += [f'### {name}', '', f"Size `0x{t['size']:X}`; {t['evidence']['layout']}.", '',
                '| Offset | Bytes | Field | Type | Pointer count / rule |', '|---:|---:|---|---|---|']
        for m in t['members']:
            offset = f"0x{int(m['offset']):X}" if m['offset'].is_integer() else str(m['offset'])
            rule = m.get('pointer_rule', {})
            expression = rule.get('expression', json.dumps(rule.get('count', '')))
            out.append(f"| {offset} | {m['size']:g} | {m['name']} | {m['type']} | {str(expression).replace('|', '/')} |")
        if t['unmapped_ranges']:
            out += ['', 'Unmapped/padding bit ranges: ' + ', '.join(f"{g['offset_bits']}+{g['size_bits']}" for g in t['unmapped_ranges']) + '.']
        out.append('')
    out += ['## Enums', '']
    for name, t in data['types'].items():
        if t['kind'] == 'enum':
            out += [f'### {name}', '', f"Width: {t['size']} bytes.", '', '| Value | Name |', '|---:|---|']
            out += [f'| {value} | {key} |' for key, value in t.get('values', {}).items()]
            out.append('')
    out += ['## External asset boundaries', '', '| Type | Replay pool | Size |', '|---|---:|---:|']
    for name, pool in sorted(data['external_assets'].items()):
        out.append(f"| {name} | {pool['id']} ({pool['name']}) | 0x{pool['size']:X} |")
    return '\n'.join(out) + '\n'


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('schema', type=Path)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    result = generate(a.schema)
    a.out.mkdir(parents=True, exist_ok=True)
    (a.out/'weapon_layout.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    (a.out/'WEAPON_LAYOUT.md').write_text(markdown(result), encoding='utf-8')
    (a.out/'weapon_schema.json').write_text(json.dumps(load_schema(a.schema), indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result['summary']))
