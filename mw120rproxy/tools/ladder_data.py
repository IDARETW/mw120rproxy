"""Carry authored CoD4 ladder faces into the Replay movement sidecar."""
import math
import struct

MAGIC=b'MWRLAD02'
MAX_LADDERS=512

def faces(brush):
    lo,hi=brush['mins'],brush['maxs']
    if hi[2]-lo[2]<48:return []
    axis=min(range(2),key=lambda k:hi[k]-lo[k])
    width=hi[1-axis]-lo[1-axis]
    if width<12:return []
    result=[]
    for p in brush.get('ladder_planes',[]):
        normal=p[:3]
        if abs(normal[axis])<.999 or abs(normal[2])>.001:continue
        bottom=[(lo[k]+hi[k])/2 for k in range(3)]
        bottom[axis]=p[3]/normal[axis];bottom[2]=lo[2]
        top=bottom.copy();top[2]=lo[2]+math.floor((hi[2]-lo[2])/12)*12
        result.append({'bottom':bottom,'top':top,'normal':normal,'width':width})
    return result

def encode(ladders):
    if len(ladders)>MAX_LADDERS:raise ValueError('Too many ladder faces')
    data=MAGIC+struct.pack('<I',len(ladders))
    for l in ladders:data+=struct.pack('<15f',*l['bottom'],*l['top'],*l['normal'],l['width'],*l.get('grip',l['bottom']),l.get('rung_distance',12),l.get('grip_width',min(l['width'],15.2496)))
    validate(data)
    return data

def validate(data):
    if len(data)<12 or data[:8] not in (MAGIC,b'MWRLAD01'):raise ValueError('Invalid ladder header')
    count=struct.unpack_from('<I',data,8)[0];stride=60 if data[:8]==MAGIC else 40
    if count>MAX_LADDERS or len(data)!=12+stride*count:raise ValueError('Invalid ladder count/length')
    for off in range(12,len(data),stride):
        v=struct.unpack_from('<'+('15f' if stride==60 else '10f'),data,off)
        if any(not math.isfinite(x) or abs(x)>100000 for x in v):raise ValueError('Invalid ladder coordinate')
        if abs(v[0]-v[3])>.01 or abs(v[1]-v[4])>.01 or not 48<=v[5]-v[2]<=8192:raise ValueError('Invalid vertical ladder span')
        if abs(v[8])>.001 or abs(sum(x*x for x in v[6:9])-1)>.001 or not 12<=v[9]<=1024:raise ValueError('Invalid ladder normal/width')
        if stride==60 and (not 4<=v[13]<=64 or not 8<=v[14]<=128 or abs(v[12]-v[2])>64):raise ValueError('Invalid ladder grip geometry')
    return count


def align_models(ladders,instances,source):
    """Measure rungs from welded model components and match authored ladder clips."""
    from imported_map_assets import read_obj
    from statistics import median
    cache={};matched=0
    for inst in instances:
        name=inst['model']
        if 'ladder' not in name.lower():continue
        if name not in cache:
            rungs=[]
            for surface in read_obj(source,name):
                vertices=surface['vertices'];parent=list(range(len(vertices)));owners={}
                def find(i):
                    while parent[i]!=i:parent[i]=parent[parent[i]];i=parent[i]
                    return i
                for i,v in enumerate(vertices):
                    key=tuple(round(x,2) for x in v['position'])
                    if key in owners:parent[find(i)]=find(owners[key])
                    else:owners[key]=i
                for at in range(0,len(surface['indices']),3):
                    tri=surface['indices'][at:at+3]
                    for i in tri[1:]:parent[find(i)]=find(tri[0])
                groups={}
                for i,v in enumerate(vertices):groups.setdefault(find(i),[]).append(v['position'])
                for points in groups.values():
                    lo=[min(p[k] for p in points) for k in range(3)];hi=[max(p[k] for p in points) for k in range(3)]
                    if hi[2]-lo[2]<6 and max(hi[k]-lo[k] for k in range(2))>12:
                        rungs.append(([(a+b)/2 for a,b in zip(lo,hi)],max(hi[k]-lo[k] for k in range(2))))
            rungs.sort(key=lambda r:r[0][2])
            cache[name]=rungs
        rungs=cache[name]
        if len(rungs)<3:continue
        scale=inst['scale'];pitch=median([b[0][2]-a[0][2] for a,b in zip(rungs,rungs[1:])])*scale
        if not 4<=pitch<=64:continue
        # Round tiny vertex noise; all six com_ladder_wood rungs are on a 24-unit grid.
        if abs(pitch-round(pitch))<.15:pitch=float(round(pitch))
        local=rungs[0][0];axis=inst['axis'];origin=inst['origin']
        anchor=[origin[k]+scale*sum(local[j]*axis[j][k] for j in range(3)) for k in range(3)]
        for face in ladders:
            if sum((face['bottom'][k]-anchor[k])**2 for k in range(2))>16**2 or abs(face['bottom'][2]-origin[2])>48:continue
            phase=anchor[2]+math.floor((face['bottom'][2]-anchor[2])/pitch)*pitch
            # Replay 14A94E0 snaps the IK contact directly to bottom+n*rung.
            # CC5A70's half-rung offset belongs only to animation scrubbing.
            face['grip']=[anchor[0],anchor[1],phase]
            face['rung_distance']=pitch;face['grip_width']=max(8,median([r[1] for r in rungs])*scale-4)
            matched+=1
    return matched
