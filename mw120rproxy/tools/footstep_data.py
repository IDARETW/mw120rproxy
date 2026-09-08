from local_paths import COD4
"""Preserve authored surface categories in a compact walkable-triangle sidecar."""
import json,math,struct
from pathlib import Path
from collections import Counter

def surface_type(source,name):
    p=Path(source)/'materials'/(name+'.json')
    if p.exists():bits=json.loads(p.read_text()).get('surfaceTypeBits',0)
    else:
        # Mod Tools raw materials use their on-disk header, not MaterialInfo.
        # Surface flags at byte 32 encode the IW3 type in bits 20..24.
        p=(COD4/'raw/materials')/name.lstrip(',')
        b=p.read_bytes() if p.exists() else b''
        t=(struct.unpack_from('<I',b,32)[0]>>20)&31 if len(b)>=36 else 0
        return t if 1<=t<=28 else 5
    # IW3 MaterialInfo stores bit(type-1), not trace_t's packed surface flags.
    return bits.bit_length() if bits and bits&(bits-1)==0 and bits.bit_length()<=28 else 5

def encode(surfaces,types):
    records=[];counts=Counter()
    for s in surfaces:
        for i in range(0,len(s['indices']),3):
            vs=[s['vertices'][j] for j in s['indices'][i:i+3]]
            tile=int(vs[0]['lightmapUV'][0]);kind=int(vs[0]['lightmapUV'][1])%4
            if tile>=len(types) or kind in (1,2,3):continue # sky, glass, cutout overlays
            a,b,c=[v['position'] for v in vs]
            u=[b[k]-a[k] for k in range(3)];v=[c[k]-a[k] for k in range(3)]
            n=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
            # Replay BSP winding is clockwise. Retain only upward walkable faces.
            if -n[2] < .35*math.sqrt(sum(x*x for x in n)) or abs(n[2])<.001:continue
            t=types[tile];records.append(struct.pack('<9fI',*(a+b+c),t));counts[t]+=1
    if len(records)>600000:raise ValueError('Footstep triangle budget exceeded')
    return b'MWRSTEP1'+struct.pack('<I',len(records))+b''.join(records),dict(counts)

def generate(package,source,report,render):
    names=report['color_images'];types={}
    for name,m in report['materials'].items():
        if 'image' not in m:continue
        t=surface_type(source,name)
        if m['image'] not in types or t!=5:types[m['image']]=t
    data,counts=encode(render['surfaces'],[types.get(n,5) for n in names])
    (Path(package)/'footsteps.bin').write_bytes(data)
    return {'triangles':sum(counts.values()),'surface_types':counts,'fallback':'concrete'}
