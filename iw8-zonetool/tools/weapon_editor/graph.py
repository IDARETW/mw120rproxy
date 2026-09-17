"""Typed edits to native prepared records and Replay skeletons.

Pointers are relocations, never editable machine addresses. Array counts are
owned by their array editors. Field metadata describes bytes; it is not a
second copy of the weapon that can diverge from the compiled record.
"""
import math
import re
import struct


SLOTS = ['receiver', 'frontpiece', 'backpiece', 'magazine', 'override', 'muzzle',
         'reargrip', 'trigger', 'extra', 'scope', 'underbarrel', 'modifier', 'visual', 'other']
MAX_ATTACHMENTS_PER_SLOT = 4096


def walk(record):
    for f in record.get('fields', []):
        yield record, f
    for f in record.get('fixups', []):
        if f['kind'] == 'record':
            yield from walk(f)
        else:
            yield record, f


def editable(f):
    path = f['field']
    # Native size fields must track their buffers. Boolean/count gameplay
    # fields such as burstCount are not allocation lengths.
    leaf = path.rsplit('.', 1)[-1]
    return not (leaf in ('szInternalName', 'szLootTable') or
                'unknown' in leaf.lower() or
                leaf.endswith(('Count', 'count', 'Index', 'Size')) and
                leaf not in ('burstCount', 'shotCount', 'iClipSize', 'clipSize'))


def flatten(record, types):
    result = []
    if not record:
        return result
    for owner, f in walk(record):
        item = dict(f)
        item['editable'] = editable(f)
        item['record_size'] = len(owner['data']) // 2
        kind = f['kind']
        if kind in ('string', 'script'):
            item['value'] = f['text']
        elif kind == 'asset':
            item['value'] = f['name']
        else:
            raw = bytes.fromhex(owner['data'])[f['offset']:f['offset'] + f['size']]
            typename = f['type']
            if typename in ('float', 'double'):
                item['value'] = struct.unpack('<f' if typename == 'float' else '<d', raw)[0]
            elif typename == 'bool':
                item['value'] = bool(int.from_bytes(raw, 'little'))
            else:
                item['value'] = int.from_bytes(raw, 'little', signed=not typename.startswith(('unsigned', 'uint')))
            definition = types.get(typename, {})
            if definition.get('kind') == 'enum':
                item['choices'] = definition['values']
        result.append(item)
    return result


def set_value(record, path, value, types, managed=False):
    for owner, f in walk(record):
        if f['field'] != path:
            continue
        if not managed and not editable(f):
            raise ValueError('Field is derived or reserved: ' + path)
        if f['kind'] in ('asset', 'string', 'script'):
            if not isinstance(value, str) or len(value.encode('utf-8')) > 4095 or '\0' in value:
                raise ValueError('Expected a string of at most 4095 bytes')
            if f['kind'] == 'asset' and (not value or value.startswith(',')):
                raise ValueError('Asset references require a nonempty native asset name')
            f['name' if f['kind'] == 'asset' else 'text'] = value
        else:
            name, width = f['type'], f['size']
            if name in ('float', 'double'):
                value = float(value)
                if not math.isfinite(value):
                    raise ValueError('Value must be finite')
                raw = struct.pack('<f' if name == 'float' else '<d', value)
            else:
                if isinstance(value, float) and not value.is_integer():
                    raise ValueError('Expected a whole number')
                value = int(value)
                if name == 'bool' and value not in (0, 1):
                    raise ValueError('Expected a boolean')
                definition = types.get(name, {})
                if definition.get('kind') == 'enum' and value not in definition['values'].values():
                    raise ValueError('Unknown ' + name + ' value')
                raw = value.to_bytes(width, 'little', signed=not name.startswith(('unsigned', 'uint')))
            data = bytearray.fromhex(owner['data'])
            data[f['offset']:f['offset'] + width] = raw
            owner['data'] = data.hex()
            f['value'] = value
        return
    raise ValueError('Field is absent from this reference: ' + path)


def attachment_slots(root):
    result = [[] for _ in SLOTS]
    for f in root['fixups']:
        m = re.fullmatch(r'weapon.attachments\[(\d+)\].attachments', f['field'])
        if m:
            result[int(m[1])] = [v['name'] for v in f['fixups'] if v['kind'] == 'asset']
    return result


def set_attachment_slots(root, slots):
    if len(slots) != 14:
        raise ValueError('Replay has fourteen attachment slots')
    root['fixups'] = [f for f in root['fixups'] if not f['field'].startswith('weapon.attachments[')]
    for i, names in enumerate(slots):
        if len(names) > MAX_ATTACHMENTS_PER_SLOT or len(set(names)) != len(names):
            raise ValueError(f'An attachment slot supports at most {MAX_ATTACHMENTS_PER_SLOT} unique entries')
        set_value(root, f'weapon.attachments[{i}].attachmentCount', len(names), {}, managed=True)
        if names:
            path = f'weapon.attachments[{i}].attachments'
            root['fixups'].append({'offset': 48 + 16*i, 'field': path, 'kind': 'record',
                'alignment': 8, 'data': '00'*(len(names)*8), 'fields': [], 'fixups': [
                    {'offset': n*8, 'field': f'{path}[{n}]', 'kind': 'asset',
                     'name': name, 'asset_type': 42, 'size': 0x3C8} for n, name in enumerate(names)]})
    root['fixups'].sort(key=lambda f: f['offset'])


def qmul(a, b):
    x,y,z,w = a
    X,Y,Z,W = b
    return [w*X+x*W+y*Z-z*Y, w*Y-x*Z+y*W+z*X,
            w*Z+x*Y-y*X+z*W, w*W-x*X-y*Y-z*Z]


def qrotate(q, v):
    return qmul(qmul(q, [*v, 0]), [-q[0], -q[1], -q[2], q[3]])[:3]


def rebuild_bind_pose(rig):
    n, roots = len(rig['bones']), rig['root_bones']
    if n < 1 or n > 128 or roots < 1 or roots > n or len(set(rig['bones'])) != n:
        raise ValueError('Skeleton requires 1..128 unique bones and valid roots')
    if any(not re.fullmatch(r'[a-zA-Z0-9_:.-]{1,63}', name) for name in rig['bones']):
        raise ValueError('Bone names require 1..63 letters, numbers, underscores, or : . -')
    if len(rig['classification']) != n or any(not isinstance(v,int) or not 0 <= v <= 255 for v in rig['classification']):
        raise ValueError('Bone classifications must match the skeleton')
    if not isinstance(rig['rigid_bone'],int) or not 0 <= rig['rigid_bone'] < n:
        raise ValueError('Default mesh bone is outside the skeleton')
    matrix = rig['transform']
    if len(matrix) != 3 or any(len(row) != 4 or not all(math.isfinite(v) for v in row) for row in matrix):
        raise ValueError('Model transform must be a finite 3 by 4 matrix')
    for name in rig.get('part_bones',{}).values():
        if name not in rig['bones']:
            raise ValueError('Mesh part references an unknown bone: '+name)
    for weights in rig.get('vertex_weights',[]):
        if not 1 <= len(weights) <= 4 or len({w['bone'] for w in weights}) != len(weights):
            raise ValueError('Each vertex requires one to four unique bone influences')
        if any(w['bone'] not in rig['bones'] or not math.isfinite(w['weight']) or w['weight'] <= 0 for w in weights):
            raise ValueError('Invalid vertex bone influence')
    if any(len(rig[k]) != n-roots for k in ('parents', 'quats', 'translations')):
        raise ValueError('Skeleton local transforms do not match its bone count')
    poses = []
    for i in range(n):
        if i < roots:
            poses.append({'quat': [0,0,0,1], 'translation': [0,0,0], 'weight': 2})
            continue
        j = i-roots
        distance = int(rig['parents'][j])
        if distance < 1 or distance > i:
            raise ValueError('Bone parent must precede child')
        values = [*rig['quats'][j], *rig['translations'][j]]
        if not all(math.isfinite(v) for v in values):
            raise ValueError('Bone transforms must be finite')
        q = rig['quats'][j]
        length = math.sqrt(sum(v*v for v in q))
        if length < 1e-6:
            raise ValueError('Bone quaternion is zero')
        q = [v/length for v in q]
        rig['quats'][j] = [round(max(-1,min(1,v))*32767) for v in q]
        # The native quaternion has int16 precision. Preview the same quantized pose.
        packed_length = math.sqrt(sum(v*v for v in rig['quats'][j]))
        q = [v/packed_length for v in rig['quats'][j]]
        parent = poses[i-distance]
        t = qrotate(parent['quat'], rig['translations'][j])
        poses.append({'quat': qmul(parent['quat'], q),
                      'translation': [a+b for a,b in zip(parent['translation'], t)], 'weight': 2})
    rig['bind_pose'] = poses
    return rig


def set_bone_world(rig, name, translation, quaternion):
    if len(translation) != 3 or len(quaternion) != 4 or not all(math.isfinite(v) for v in [*translation,*quaternion]):
        raise ValueError('Bone position and quaternion must be finite')
    index = rig['bones'].index(name)
    j = index - rig['root_bones']
    if j < 0:
        raise ValueError('Move the model transform to reposition its root')
    rebuild_bind_pose(rig)
    parent = rig['bind_pose'][index-rig['parents'][j]]
    q = parent['quat']
    inverse = [-q[0], -q[1], -q[2], q[3]]
    local_q = qmul(inverse, quaternion)
    length = math.sqrt(sum(v*v for v in local_q))
    if length < 1e-6:
        raise ValueError('Bone quaternion is zero')
    rig['quats'][j] = [round(max(-1,min(1,v/length))*32767) for v in local_q]
    rig['translations'][j] = qrotate(inverse, [a-b for a,b in zip(translation,parent['translation'])])
    rebuild_bind_pose(rig)


def edit_hierarchy(rig, operation, name, parent=None, new_name=None):
    """Reorder native parent distances while retaining every world bind transform."""
    rebuild_bind_pose(rig)
    names = rig['bones'][:]
    roots = names[:rig['root_bones']]
    parents = {n: (None if i < len(roots) else names[i-rig['parents'][i-len(roots)]]) for i,n in enumerate(names)}
    poses = dict(zip(names,rig['bind_pose']))
    classes = dict(zip(names,rig['classification']))
    default = names[rig['rigid_bone']]
    if operation == 'add':
        if name in names or len(names) >= 128 or parent not in names:
            raise ValueError('Choose a unique bone name and an existing parent (maximum 128 bones)')
        names.append(name)
        parents[name] = parent
        poses[name] = poses[parent]
        classes[name] = 0
    elif operation == 'parent':
        if name in roots or name not in names or parent not in names or name == parent:
            raise ValueError('Choose a non-root bone and a different parent')
        parents[name] = parent
    elif operation == 'rename':
        if name not in names or not new_name or new_name in names:
            raise ValueError('Choose a unique bone name')
        names[names.index(name)] = new_name
        roots = [new_name if n == name else n for n in roots]
        parents = {(new_name if n == name else n):(new_name if p == name else p) for n,p in parents.items()}
        poses[new_name], classes[new_name] = poses.pop(name), classes.pop(name)
        default = new_name if default == name else default
        rig['part_bones'] = {part:(new_name if n == name else n) for part,n in rig.get('part_bones',{}).items()}
        for weights in rig.get('vertex_weights',[]):
            for weight in weights:
                if weight['bone'] == name:
                    weight['bone'] = new_name
    elif operation == 'remove':
        used = set(rig.get('part_bones',{}).values()) | {w['bone'] for weights in rig.get('vertex_weights',[]) for w in weights}
        if name not in names or name in roots or name == default or name in used or name in parents.values():
            raise ValueError('Reassign this bone’s mesh parts, weights and children before removing it')
        names.remove(name)
    else:
        raise ValueError('Unknown bone operation')
    ordered = roots[:]
    while len(ordered) < len(names):
        ready = [n for n in names if n not in ordered and parents[n] in ordered]
        if not ready:
            raise ValueError('Bone parenting would create a cycle')
        ordered.extend(ready)
    rig['bones'] = ordered
    rig['root_bones'] = len(roots)
    rig['classification'] = [classes[n] for n in ordered]
    rig['rigid_bone'] = ordered.index(default)
    rig['parents'], rig['quats'], rig['translations'] = [], [], []
    for i,n in enumerate(ordered[len(roots):],len(roots)):
        p = parents[n]
        pose, base = poses[n], poses[p]
        inverse = [-base['quat'][0],-base['quat'][1],-base['quat'][2],base['quat'][3]]
        rig['parents'].append(i-ordered.index(p))
        rig['quats'].append(qmul(inverse,pose['quat']))
        rig['translations'].append(qrotate(inverse,[a-b for a,b in zip(pose['translation'],base['translation'])]))
    return rebuild_bind_pose(rig)
