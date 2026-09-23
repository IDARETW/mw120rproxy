"""Build a compact glTF preview from the exact OBJ supplied to the fastfile writer.

The preview keeps each OBJ group as a named mesh. It contains no texture or
material guesses: the workbench applies its authored material definitions.
"""

from array import array
import json
from pathlib import Path
import struct
import sys


def obj_to_glb(source, destination):
    source, destination = Path(source), Path(destination)
    positions, uvs, normals = [], [], []
    parts = {}
    current_name = 'default'
    def current_part():
        if current_name not in parts:
            parts[current_name] = (array('f'), array('I'), {})
        return parts[current_name]

    def vertex_index(token):
        vertices, _, remap = current_part()
        fields = token.split('/')
        def index(value, count):
            if not value:
                return None
            number = int(value)
            return number - 1 if number > 0 else count + number
        fields += [''] * (3 - len(fields))
        counts = (len(positions), len(uvs), len(normals))
        key = tuple(index(value, count) for value, count in zip(fields[:3], counts))
        if key[0] is None or any(item is not None and (item < 0 or item >= count)
                                 for item, count in zip(key, counts)):
            raise ValueError('Preview OBJ contains an out-of-range vertex index')
        if key not in remap:
            p = positions[key[0]]
            uv = uvs[key[1]] if key[1] is not None else (0.0, 0.0)
            normal = normals[key[2]] if key[2] is not None else (0.0, 0.0, 1.0)
            remap[key] = len(vertices) // 8
            vertices.extend((*p, *uv, *normal))
        return remap[key]

    with source.open('r', encoding='utf-8-sig') as stream:
        for raw in stream:
            if not raw or raw[0] == '#':
                continue
            words = raw.split()
            if not words:
                continue
            command = words[0]
            if command == 'v':
                positions.append(tuple(map(float, words[1:4])))
            elif command == 'vt':
                uvs.append(tuple(map(float, words[1:3])))
            elif command == 'vn':
                normals.append(tuple(map(float, words[1:4])))
            elif command in ('g', 'o'):
                current_name = ' '.join(words[1:]) or 'default'
            elif command == 'f':
                _, indices, _ = current_part()
                face = [vertex_index(token) for token in words[1:]]
                if len(face) < 3:
                    raise ValueError('Preview OBJ face needs at least three corners')
                for index in range(1, len(face) - 1):
                    indices.extend((face[0], face[index], face[index + 1]))
    if not parts:
        raise ValueError('Preview OBJ contains no faces')

    binary = bytearray()
    gltf = {'asset': {'version': '2.0', 'generator': 'Replay Weapon Workbench'},
            'scene': 0, 'scenes': [{'nodes': []}], 'nodes': [], 'meshes': [],
            'buffers': [{'byteLength': 0}], 'bufferViews': [], 'accessors': []}

    def accessor(values, components, component_type, target, minimum=None, maximum=None):
        while len(binary) % 4:
            binary.append(0)
        offset = len(binary)
        binary.extend(values.tobytes())
        view = len(gltf['bufferViews'])
        gltf['bufferViews'].append({'buffer': 0, 'byteOffset': offset,
                                    'byteLength': len(binary) - offset, 'target': target})
        item = {'bufferView': view, 'componentType': component_type,
                'count': len(values) // components,
                'type': {1: 'SCALAR', 2: 'VEC2', 3: 'VEC3'}[components]}
        if minimum is not None:
            item['min'], item['max'] = minimum, maximum
        result = len(gltf['accessors'])
        gltf['accessors'].append(item)
        return result

    for name, (packed, triangles, _) in parts.items():
        xyz, uv, normal = array('f'), array('f'), array('f')
        low, high = [float('inf')] * 3, [float('-inf')] * 3
        for offset in range(0, len(packed), 8):
            p = packed[offset:offset + 3]
            xyz.extend(p)
            uv.extend(packed[offset + 3:offset + 5])
            normal.extend(packed[offset + 5:offset + 8])
            for axis in range(3):
                low[axis] = min(low[axis], p[axis])
                high[axis] = max(high[axis], p[axis])
        attributes = {'POSITION': accessor(xyz, 3, 5126, 34962, low, high),
                      'TEXCOORD_0': accessor(uv, 2, 5126, 34962),
                      'NORMAL': accessor(normal, 3, 5126, 34962)}
        mesh = len(gltf['meshes'])
        gltf['meshes'].append({'name': name, 'primitives': [{
            'attributes': attributes, 'indices': accessor(triangles, 1, 5125, 34963)}]})
        gltf['nodes'].append({'name': name, 'mesh': mesh})
        gltf['scenes'][0]['nodes'].append(len(gltf['nodes']) - 1)

    gltf['buffers'][0]['byteLength'] = len(binary)
    header = json.dumps(gltf, separators=(',', ':')).encode('utf-8')
    header += b' ' * (-len(header) % 4)
    binary.extend(b'\0' * (-len(binary) % 4))
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open('wb') as stream:
        stream.write(struct.pack('<4sII', b'glTF', 2, 12 + 8 + len(header) + 8 + len(binary)))
        stream.write(struct.pack('<I4s', len(header), b'JSON'))
        stream.write(header)
        stream.write(struct.pack('<I4s', len(binary), b'BIN\0'))
        stream.write(binary)
    return {'parts': len(parts), 'triangles': sum(len(indices) // 3 for _, indices, _ in parts.values()),
            'bytes': destination.stat().st_size}


if __name__ == '__main__':
    print(json.dumps(obj_to_glb(sys.argv[1], sys.argv[2])))
