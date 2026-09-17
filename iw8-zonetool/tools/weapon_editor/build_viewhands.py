"""Assemble a local skinned operator-arms GLB from Replay extraction evidence."""
import argparse
import json
import math
from pathlib import Path
import struct


COMPONENT_SIZE={5121:1,5123:2,5125:4,5126:4}
TYPE_COMPONENTS={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}


def read_glb(path):
    data=path.read_bytes()
    if len(data)<28 or struct.unpack_from('<III',data,0)!=(0x46546C67,2,len(data)):
        raise ValueError('Expected a glTF 2.0 binary file')
    json_size,json_type=struct.unpack_from('<II',data,12)
    if json_type!=0x4E4F534A:
        raise ValueError('GLB has no JSON chunk')
    document=json.loads(data[20:20+json_size].decode('utf-8'))
    at=20+json_size
    bin_size,bin_type=struct.unpack_from('<II',data,at)
    if bin_type!=0x004E4942 or at+8+bin_size!=len(data):
        raise ValueError('GLB binary chunk is malformed')
    return document,bytearray(data[at+8:])


def write_glb(path,document,binary):
    while len(binary)%4:
        binary.append(0)
    document['buffers']=[{'byteLength':len(binary)}]
    encoded=json.dumps(document,separators=(',',':')).encode('utf-8')
    encoded+=b' '*(-len(encoded)%4)
    total=28+len(encoded)+len(binary)
    output=bytearray(struct.pack('<III',0x46546C67,2,total))
    output+=struct.pack('<II',len(encoded),0x4E4F534A)+encoded
    output+=struct.pack('<II',len(binary),0x004E4942)+binary
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes(output)


def normalize_quat(values):
    length=math.sqrt(sum(value*value for value in values))
    if length<1e-9:
        raise ValueError('Viewhands skeleton contains a zero quaternion')
    return [value/length for value in values]


def matrix(translation,quaternion):
    x,y,z,w=normalize_quat(quaternion)
    return [[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),translation[0]],
            [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),translation[1]],
            [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y),translation[2]],
            [0.0,0.0,0.0,1.0]]


def multiply(left,right):
    return [[sum(left[row][k]*right[k][column] for k in range(4)) for column in range(4)] for row in range(4)]


def inverse_rigid(value):
    result=[[value[column][row] if row<3 and column<3 else 0.0 for column in range(4)] for row in range(4)]
    result[3]=[0.0,0.0,0.0,1.0]
    for row in range(3):
        result[row][3]=-sum(result[row][column]*value[column][3] for column in range(3))
    return result


def accessor_values(document,binary,index):
    accessor=document['accessors'][index]
    view=document['bufferViews'][accessor['bufferView']]
    component=accessor['componentType']
    count=TYPE_COMPONENTS[accessor['type']]
    formats={5121:'B',5123:'H',5125:'I',5126:'f'}
    size=COMPONENT_SIZE[component]*count
    stride=view.get('byteStride',size)
    start=view.get('byteOffset',0)+accessor.get('byteOffset',0)
    return [struct.unpack_from('<'+formats[component]*count,binary,start+item*stride) for item in range(accessor['count'])]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xmodel',type=Path,required=True)
    parser.add_argument('--strings',type=Path,required=True)
    parser.add_argument('--geometry',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    root=json.loads(args.xmodel.read_text(encoding='utf-8-sig'))['asset']['fields']
    strings=json.loads(args.strings.read_text(encoding='utf-8-sig'))
    count=int(root['numBones'])
    roots=int(root['numRootBones'])
    if count<1 or count>255 or roots<1 or roots>count:
        raise ValueError('Unsupported operator-arms bone count')
    ids=[entry['value'] for entry in root['boneNames']['values']]
    names=[strings[value] if value<len(strings) and strings[value] else f'bone_{value}' for value in ids]
    if len(names)!=count or len(set(names))!=count:
        raise ValueError('Operator-arms bone names are missing or duplicated')
    parent_bytes=bytes.fromhex(root['parentList']['bytes'])
    if len(parent_bytes)!=count-roots:
        raise ValueError('Operator-arms parent list has the wrong length')
    parents=[None]*roots+[index-parent_bytes[index-roots] for index in range(roots,count)]
    if any(parent is not None and (parent<0 or parent>=index) for index,parent in enumerate(parents)):
        raise ValueError('Operator-arms parent hierarchy is invalid')
    packed_quats=root['quats']['values']
    translations=root['trans']['values']
    base=root['baseMat']['values']
    local=[]
    for index in range(count):
        if index<roots:
            translation=base[index]['trans']['v']
            quaternion=base[index]['quat']['v']
        else:
            offset=(index-roots)*4
            quaternion=[value/32767.0 for value in packed_quats[offset:offset+4]]
            offset=(index-roots)*3
            translation=translations[offset:offset+3]
        quaternion=normalize_quat(quaternion)
        local.append((translation,quaternion))
    world=[]
    for index,(translation,quaternion) in enumerate(local):
        value=matrix(translation,quaternion)
        world.append(multiply(world[parents[index]],value) if parents[index] is not None else value)

    document,binary=read_glb(args.geometry)
    primitives=document['meshes'][0]['primitives']
    if len(primitives)!=root['numsurfs']:
        raise ValueError('Operator-arms geometry does not match its model surface count')
    for primitive in primitives:
        attributes=primitive['attributes']
        if 'JOINTS_0' not in attributes or 'WEIGHTS_0' not in attributes:
            raise ValueError('Geometry extraction does not contain Replay skin weights')
        joints=accessor_values(document,binary,attributes['JOINTS_0'])
        weights=accessor_values(document,binary,attributes['WEIGHTS_0'])
        if len(joints)!=len(weights) or any(max(row)>=count for row in joints):
            raise ValueError('Geometry skin refers to a missing operator-arms bone')
        if any(abs(sum(row)-1.0)>2e-4 for row in weights):
            raise ValueError('Geometry skin weights are not normalized')

    while len(binary)%4:
        binary.append(0)
    inverse_offset=len(binary)
    for value in world:
        inverse=inverse_rigid(value)
        binary+=struct.pack('<16f',*(inverse[row][column] for column in range(4) for row in range(4)))
    view=len(document['bufferViews'])
    document['bufferViews'].append({'buffer':0,'byteOffset':inverse_offset,'byteLength':count*64})
    accessor=len(document['accessors'])
    document['accessors'].append({'bufferView':view,'componentType':5126,'count':count,'type':'MAT4'})

    bone_nodes=[]
    for index,(translation,quaternion) in enumerate(local):
        node={'name':names[index],'translation':translation,'rotation':quaternion}
        children=[child+2 for child,parent in enumerate(parents) if parent==index]
        if children:
            node['children']=children
        bone_nodes.append(node)
    weapon_bone=names.index('tag_weapon')
    alignment=inverse_rigid(world[weapon_bone])
    document['nodes']=[
        {'name':'Viewhands weapon alignment',
         'matrix':[alignment[row][column] for column in range(4) for row in range(4)],
         'children':[1,*[index+2 for index in range(roots)]]},
        {'name':'Domino operator arms','mesh':0,'skin':0},
        *bone_nodes]
    document['scenes']=[{'nodes':[0]}]
    document['scene']=0
    document['skins']=[{'name':'Domino viewhands skeleton','joints':list(range(2,count+2)),
                        'skeleton':2,'inverseBindMatrices':accessor}]
    document['materials']=[
        {'name':'Domino sleeves','pbrMetallicRoughness':{'baseColorFactor':[0.13,0.16,0.19,1.0],'metallicFactor':0.0,'roughnessFactor':0.9}},
        {'name':'Domino gloves','pbrMetallicRoughness':{'baseColorFactor':[0.045,0.05,0.06,1.0],'metallicFactor':0.0,'roughnessFactor':0.72}},
        {'name':'Domino skin','pbrMetallicRoughness':{'baseColorFactor':[0.38,0.22,0.16,1.0],'metallicFactor':0.0,'roughnessFactor':0.84}}]
    for index,primitive in enumerate(primitives):
        primitive['material']=min(index,len(document['materials'])-1)
    document['asset']['generator']='Replay Weapon Workbench viewhands assembler'
    document['extras']={'scope':'local stock operator-arms preview','sourceModel':root['name']['string'],
                        'sourceCoordinates':'right-handed Z-up, inches','bones':count,
                        'surfaces':len(primitives),'weights':'Replay native, top four normalized'}
    write_glb(args.output,document,binary)
    print(json.dumps({'output':str(args.output),'bytes':args.output.stat().st_size,
                      'bones':count,'surfaces':len(primitives),
                      'vertices':sum(document['accessors'][primitive['attributes']['POSITION']]['count'] for primitive in primitives)}))


if __name__=='__main__':
    main()
