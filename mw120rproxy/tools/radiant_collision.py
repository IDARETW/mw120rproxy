"""Intersect source brush planes and expand prefab transforms into convex hulls."""
from itertools import combinations
import math
import re
import struct
from pathlib import Path
from radiant_source import blocks,properties,vector
from cod4_assets import rotation


def dot(a,b):return sum(x*y for x,y in zip(a,b))
def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def sub(a,b):return [x-y for x,y in zip(a,b)]


def hull(brush,label):
    brush=brush.strip()
    # Curves/terrain are tessellated by cod4map. Their actual compiled collision
    # triangles are imported after the BSP build, not guessed from control points.
    if blocks(brush[1:-1]):return None
    rows=re.findall(r'\(\s*([^()]+)\)\s*\(\s*([^()]+)\)\s*\(\s*([^()]+)\)\s*(\S+)',brush)
    if not 4<=len(rows)<=64:raise ValueError(f'{label}: expected 4..64 brush planes')
    textures={r[3] for r in rows}
    if textures=={'lightgrid_volume'}:return None
    if textures & {'portal','occluder','hint','skip','trigger','nonsolid'}:
        raise ValueError(f'{label}: unsupported tool brush {sorted(textures)}')
    planes=[]
    for a,b,c,_ in rows:
        a,b,c=map(vector,(a,b,c));normal=cross(sub(c,a),sub(b,a));length=math.sqrt(dot(normal,normal))
        if length<1e-8:raise ValueError(f'{label}: degenerate plane')
        normal=[x/length for x in normal];planes.append((normal,dot(normal,a)))
    points=[]
    for (a,da),(b,db),(c,dc) in combinations(planes,3):
        bc=cross(b,c);det=dot(a,bc)
        if abs(det)<1e-9:continue
        ca=cross(c,a);ab=cross(a,b)
        p=[(da*bc[k]+db*ca[k]+dc*ab[k])/det for k in range(3)]
        if all(dot(n,p)<=d+0.001 for n,d in planes) and not any(sum((p[k]-q[k])**2 for k in range(3))<1e-8 for q in points):points.append(p)
    if not 4<=len(points)<=252 or any(abs(x)>100000 for p in points for x in p):raise ValueError(f'{label}: invalid/unbounded convex brush')
    return {'vertices':points,'textures':sorted(textures),'source':label}


def collect(source,map_root):
    dependencies={};result=[]
    def visit(path,matrix,origin,ancestry):
        path=path.resolve()
        if not path.is_relative_to(map_root.resolve()):raise ValueError(f'Prefab outside map_source: {path}')
        if path in ancestry or len(ancestry)>16:raise ValueError(f'Prefab recursion: {path}')
        text=path.read_text(encoding='utf-8-sig');dependencies[str(path)]=text
        entities=blocks(text)
        if not entities or properties(entities[0]).get('classname')!='worldspawn':raise ValueError(f'{path}: missing worldspawn')
        transform=lambda p:[dot(row,p)+origin[k] for k,row in enumerate(matrix)]
        for i,brush in enumerate(blocks(entities[0][1:-1])):
            value=hull(brush,f'{path.name}:brush[{i}]')
            if value:
                value['vertices']=[transform(p) for p in value['vertices']]
                result.append(value)
        for entity in entities[1:]:
            props=properties(entity)
            if props.get('classname')=='misc_prefab':
                name=props.get('model','').replace('\\','/')
                if not name or '..' in name or ':' in name or name.startswith('/'):raise ValueError(f'Invalid prefab path {name}')
                scale=float(props.get('modelscale','1'))
                if not math.isfinite(scale) or not 0<scale<=100:raise ValueError('Invalid prefab scale')
                local=rotation(vector(props.get('angles','0 0 0')))
                composed=[[sum(matrix[i][k]*local[k][j] for k in range(3))*scale for j in range(3)] for i in range(3)]
                visit(map_root/name,composed,transform(vector(props.get('origin','0 0 0'))),ancestry+[path])
            elif blocks(entity[1:-1]):raise ValueError(f'{path}: brush entity {props.get("classname")} requires a separate runtime pipeline')
    visit(source,[[1,0,0],[0,1,0],[0,0,1]],[0,0,0],[])
    if not 1<=len(result)<=4096:raise ValueError('Expected 1..4096 collision brushes')
    return result,dependencies


def encode(hulls):
    data=b'MWCOLL02'+struct.pack('<I',len(hulls))
    for h in hulls:
        points=h['vertices'];data+=struct.pack('<I',len(points))+b''.join(struct.pack('<3f',*p) for p in points)
    validate(data)
    return data


def add_compiled_triangles(hulls,geometry):
    vertices=geometry['vertices'];added=0
    for i,tri in enumerate(geometry['triangles']):
        points=[vertices[j] for j in tri];n=cross(sub(points[1],points[0]),sub(points[2],points[0]));length=math.sqrt(dot(n,n))
        if length<1e-6:continue
        # Havok requires a volume, so retain the exact triangle and give it a
        # quarter-unit thickness centered on the compiled collision surface.
        offset=[x/length*0.125 for x in n]
        hulls.append({'vertices':[[p[k]+sign*offset[k] for k in range(3)] for sign in (-1,1) for p in points],
                      'textures':[],'source':f'compiled_collision_triangle[{i}]'})
        added+=1
    if len(hulls)>32768:raise ValueError('Compiled collision exceeds 32768 hulls; simplify collision geometry')
    return added


def validate(data):
    if len(data)<12 or data[:8]!=b'MWCOLL02':raise ValueError('Bad convex collision header')
    count=struct.unpack_from('<I',data,8)[0];pos=12
    if not 1<=count<=32768:raise ValueError('Bad convex hull count')
    for _ in range(count):
        if pos+4>len(data):raise ValueError('Truncated convex hull count')
        n=struct.unpack_from('<I',data,pos)[0];pos+=4
        if not 4<=n<=252 or pos+n*12>len(data):raise ValueError('Bad convex hull length')
        points=list(struct.iter_unpack('<3f',data[pos:pos+n*12]));pos+=n*12
        if any(not math.isfinite(x) or abs(x)>100000 for p in points for x in p):raise ValueError('Invalid convex hull vertex')
        a=points[0];b=max(points,key=lambda p:dot(sub(p,a),sub(p,a)));u=sub(b,a)
        c=max(points,key=lambda p:dot(cross(u,sub(p,a)),cross(u,sub(p,a))));normal=cross(u,sub(c,a))
        if max(abs(dot(normal,sub(p,a))) for p in points)<=0.00001:raise ValueError('Coplanar convex hull')
    if pos!=len(data):raise ValueError('Trailing convex collision data')
    return count
