"""Prepare a Replay knife reference from local ACTS exports; never copies process pointers.

This is an authoring import step. The native build-weapon command needs only its JSON output.
Use the matching Replay schema, weapon export and script_strings.json from the same extraction.
"""
import argparse
import json
import struct
from pathlib import Path


def prepare(schema, document, strings, *, root_type='WeaponCompleteDef'):
    profile = schema['profiles']['replay-1.20']
    types = schema['types']
    overrides = profile['type_overrides']
    if document.get('profile') != 'replay-1.20' or document.get('root_type') != root_type:
        raise ValueError('Expected a Replay ' + root_type + ' export')
    asset = document['asset']
    if asset.get('issues') or any(asset.get(k, 0) for k in (
        'read_errors', 'unresolved_pointers', 'unresolved_unions', 'external_payloads'
    )):
        raise ValueError('Reference export is incomplete')
    if types[overrides['WeaponDef']]['size'] != 5296 or types['WeaponCompleteDef']['size'] != 624:
        raise ValueError('Schema does not match the Replay D99D00/D9A580 loaders')
    pool_by_type = {p['root_type']: p for p in profile['pools']}
    seen = {}

    def index(value):
        if isinstance(value, dict):
            if 'address' in value and ('values' in value or 'bytes' in value):
                seen[(value['address'], value.get('type'), value.get('count'))] = value
            for child in value.values():
                index(child)
        elif isinstance(value, list):
            for child in value:
                index(child)

    index(asset)

    def definition(name):
        return types[overrides.get(name, name)]

    def alignment(name):
        t = definition(name)
        if t['kind'] == 'array':
            return alignment(t['element'])
        if t['kind'] in ('struct', 'union'):
            return max((alignment(m['type']) for m in t['members']), default=1)
        return min(t['size'], 8)

    def encode(name, value, data, offset, fixups, fields, path):
        t = definition(name)
        kind = t['kind']
        if kind == 'pointer':
            if value is None:
                return
            f = {'offset': offset, 'field': path}
            if 'string' in value:
                f.update(kind='string', text=value['string'])
            elif value.get('asset_reference'):
                pool = pool_by_type[value['type']]
                f.update(kind='asset', name=value['name'].lstrip(','),
                         asset_type=pool['id'], size=pool['size'], alignment=pool['alignment'])
            else:
                count = value['count']
                if not count:
                    return
                if value.get('status') == 'reference_already_exported':
                    value = seen[(value['address'], value['type'], count)]
                stride = definition(value['type'])['size']
                if stride != value['stride'] or count > 65536:
                    raise ValueError('Bad array extent at ' + path)
                child = bytearray(stride * count)
                children = []
                child_fields = []
                if 'bytes' in value:
                    child[:] = bytes.fromhex(value['bytes'])
                    if len(child) != stride * count:
                        raise ValueError('Bad byte array at ' + path)
                else:
                    if len(value['values']) != count:
                        raise ValueError('Bad array count at ' + path)
                    for i, item in enumerate(value['values']):
                        encode(value['type'], item, child, i * stride, children, child_fields,
                               path + f'[{i}]')
                f.update(kind='record', record_type=value['type'], alignment=alignment(value['type']),
                         data=child.hex(), fixups=children, fields=child_fields)
            fixups.append(f)
        elif kind == 'array':
            element = t['element']
            stride = definition(element)['size']
            if len(value) * stride != t['size']:
                raise ValueError('Bad inline array at ' + path)
            for i, item in enumerate(value):
                encode(element, item, data, offset + i * stride, fixups, fields, path + f'[{i}]')
        elif kind in ('struct', 'union'):
            members = t['members'] if kind == 'struct' else t['members'][:1]
            for m in members:
                if m['offset_bits'] % 8 or m['size_bits'] % 8:
                    raise ValueError('Unsupported bitfield at ' + path)
                encode(m['type'], value[m['name']], data, offset + m['offset_bits'] // 8,
                       fixups, fields, path + '.' + m['name'])
        elif kind in ('scalar', 'enum'):
            if isinstance(value, dict):
                value = value['value']
            if name == 'scr_string_t':
                if value:
                    text = strings[value]
                    if not isinstance(text, str) or not text:
                        raise ValueError('Missing script string at ' + path)
                    fixups.append({'offset': offset, 'field': path, 'kind': 'script', 'text': text})
                return
            if name in ('float', 'double'):
                raw = struct.pack('<f' if name == 'float' else '<d', value)
            else:
                raw = int(value).to_bytes(t['size'], 'little', signed=int(value) < 0)
            data[offset:offset + t['size']] = raw
            fields.append({'field': path, 'offset': offset, 'size': t['size'],
                           'type': name, 'kind': kind, 'value': value})
        else:
            raise ValueError('Unsupported type at ' + path + ': ' + name)

    data = bytearray(definition(root_type)['size'])
    fixups = []
    fields = []
    encode(root_type, asset['fields'], data, 0, fixups, fields,
           'weapon' if root_type == 'WeaponCompleteDef' else 'sfx')
    return {'format': 'replay-melee-reference-v1', 'source_weapon':
            asset['fields'].get('szInternalName', asset['fields'].get('name'))['string'], 'root':
            {'data': data.hex(), 'fixups': fixups, 'fields': fields, 'alignment': 8,
             'record_type': root_type}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--schema', type=Path, required=True)
    parser.add_argument('--weapon', type=Path, required=True)
    parser.add_argument('--strings', type=Path, required=True)
    parser.add_argument('-o', '--output', type=Path, required=True)
    parser.add_argument('--view-model', type=Path)
    parser.add_argument('--world-model', type=Path)
    parser.add_argument('--manifest', type=Path,
                        help='Also prepare the Zotov knife example manifest from local model exports')
    args = parser.parse_args()
    if any((args.view_model, args.world_model, args.manifest)) and not all(
        (args.view_model, args.world_model, args.manifest)
    ):
        parser.error('--view-model, --world-model and --manifest must be supplied together')
    result = prepare(*(json.loads(p.read_text(encoding='utf-8')) for p in
                       (args.schema, args.weapon, args.strings)))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(args.output)
    if args.manifest:
        import os
        strings = json.loads(args.strings.read_text(encoding='utf-8'))
        manifest = {
            'format': 'replay-melee-v1', 'weapon': 'iw8_knife_mp',
            'reference': Path(os.path.relpath(args.output, args.manifest.parent)).as_posix(),
            'model': 'source/knife.obj',
            'view_model': prepare_model(args.view_model, strings),
            'world_model': prepare_model(args.world_model, strings),
        }
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        args.manifest.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
        print(args.manifest)


def prepare_model(path, strings):
    document = json.loads(path.read_text(encoding='utf-8'))
    asset = document['asset']
    if document.get('profile') != 'replay-1.20' or document.get('pool') != 'xmodel':
        raise ValueError('Expected a Replay XModel export')
    if asset.get('issues') or any(asset.get(k, 0) for k in (
        'read_errors', 'unresolved_pointers', 'unresolved_unions'
    )):
        raise ValueError('Incomplete XModel reference')
    f = asset['fields']
    bones = [strings[v['value']] for v in f['boneNames']['values']]
    if f['numClientBones'] or f['numBones'] != 9 or bones[1] != 'tag_knife_offset':
        raise ValueError('Expected the stock base knife skeleton')
    name = f['name']['string']
    if name not in ('weapon_vm_me_soscar_knife', 'weapon_wm_me_soscar_knife'):
        raise ValueError('Expected the stock base knife model')
    quats, translations = f['quats']['values'], f['trans']['values']
    return {
        'bones': bones, 'root_bones': f['numRootBones'],
        'parents': list(bytes.fromhex(f['parentList']['bytes'])),
        'quats': [quats[i:i + 4] for i in range(0, len(quats), 4)],
        'translations': [translations[i:i + 3] for i in range(0, len(translations), 3)],
        'classification': list(bytes.fromhex(f['partClassification']['bytes'])),
        'bind_pose': [
            {'quat': pose['quat']['v'], 'translation': pose['trans']['v'],
             'weight': pose['transWeight']}
            for pose in f['baseMat']['values']
        ],
        'material': 'm2o/' + name, 'rigid_bone': 1,
        # Zotov's blade is thin along X; Replay's knife is thin along Y.
        # Rotate around Z, use half scale and put the grip at tag_knife_offset.
        'transform': [[0, -0.5, 0, 0], [0.5, 0, 0, -0.057], [0, 0, 0.5, -1.2]],
    }


if __name__ == '__main__':
    main()
