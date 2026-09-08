from local_paths import COD4
"""Read bounded, offline OpenAssetTools IW3 intermediates for Replay conversion."""
import json
import math
import re
from itertools import combinations
from pathlib import Path
from replay_mesh_math import pack,quaternion,unpack_normal
from radiant_collision import cross,dot,sub


def packed_normal(n,t=None,sign=1):
    if dot(n,n)<1e-10:raise ValueError('Degenerate vertex normal')
    if t is None or dot(cross(t,n),cross(t,n))<1e-10:
        t=cross([0,0,1] if abs(n[2])<.9 else [0,1,0],n)
    b=[x*sign for x in cross(n,t)]
    return pack(*quaternion(t,b,n))


def safe_asset(root,folder,name,extension):
    if not name or '..' in name or ':' in name or '\\' in name or name.startswith('/'):
        raise ValueError(f'Invalid asset name {name!r}')
    p=(root/folder/(name+extension)).resolve()
    if not p.is_relative_to(root.resolve()):raise ValueError('Asset path escapes dump')
    return p


def read_material(root,name):
    path=safe_asset(root,'materials',name,'.json')
    if not path.exists():
        from cod4_assets import material_image,BlendedMaterial,ShadowOnlyMaterial
        raw=(COD4/'raw')
        base=name.removeprefix('mc/').removeprefix('wc/')
        try:image,alpha=material_image(raw,base)
        except BlendedMaterial:
            image,_=material_image(raw,base,allow_blend=True)
            return {'image':image,'alpha_test':False,'blended':True,'techset':'stock raw material','source':str(raw/'materials'/base)}
        except ShadowOnlyMaterial:return {'skip':'shadow-only stock reference','techset':'stock raw material'}
        return {'image':image,'alpha_test':alpha,'techset':'stock raw material','source':str(raw/'materials'/base)}
    j=json.loads(path.read_text())
    tech=j['techniqueSet'];textures=j.get('textures',[])
    color=next((t['image'] for t in textures if t.get('semantic')=='colorMap'),None)
    if tech=='shadowcaster':return {'skip':'shadow-only helper','techset':tech}
    blended=bool(re.search(r'(^|_)b\d',tech))
    if re.search(r'(^|_)sky($|_)',tech):return {'skip':'sky surface','techset':tech}
    if not color:raise ValueError(f'{name}: no color map in {tech}')
    alpha=bool(re.search(r'(^|_)[at]\d',tech) or 'alphatest' in tech or
               any(state.get('alphaTest') in ('gt0','lt128','ge128') for state in j.get('stateBits',[])))
    # Ignore debug/wireframe and shadow passes: their offsets are not the
    # material's visible decal offset. Ordinary opaque materials have those too.
    visible=next((s for s in j.get('stateBits',[]) if s.get('colorWriteRgb') and not s.get('polymodeLine')), {})
    offset={'offset1':.03125,'offset2':.0625}.get(visible.get('polygonOffset'),0.)
    return {'image':color,'alpha_test':alpha and not blended,'blended':blended,'techset':tech,'depth_offset':offset}


def read_obj(root,name):
    """Reverse OAT OBJ's Z-up -> Y-up and flipped V transformations."""
    if name.startswith(','):
        # CoD4 fastfiles can reference stock models supplied by common zones.
        # Recover those from the owner's matching Mod Tools raw assets.
        from cod4_assets import load_model,transformed
        raw=(COD4/'raw')
        return [transformed(s,[0,0,0],[0,0,0],1) for s in load_model(raw,name[1:])]
    path=safe_asset(root,'model_export',name,'_lod0.obj')
    positions=[];normals=[];uvs=[];surfaces=[];current=None;mapping={}
    for line in path.read_text().splitlines():
        words=line.split()
        if not words:continue
        key,*values=words
        if key in ('v','vn'):
            x,y,z=map(float,values[:3]);(positions if key=='v' else normals).append([x,-z,y])
        elif key=='vt':uvs.append([float(values[0]),1-float(values[1])])
        elif key=='usemtl':
            current={'material':values[0],'vertices':[],'indices':[]};surfaces.append(current);mapping={}
        elif key=='f':
            if current is None or len(values)!=3:raise ValueError(f'{name}: expected material-bound triangles')
            tri=[]
            for value in values:
                vi,ti,ni=map(int,value.split('/'))
                if not (1<=vi<=len(positions) and 1<=ti<=len(uvs) and 1<=ni<=len(normals)):
                    raise ValueError('Invalid OBJ index')
                if value not in mapping:
                    mapping[value]=len(current['vertices'])
                    current['vertices'].append({'position':positions[vi-1],'uv':uvs[ti-1],'normal_vec':normals[ni-1]})
                tri.append(mapping[value])
            # OAT exports standard CCW OBJ; Replay's BSP pipeline expects CW.
            a,b,c=[current['vertices'][i] for i in tri]
            if dot(cross(sub(b['position'],a['position']),sub(c['position'],a['position'])),a['normal_vec'])>0:tri.reverse()
            current['indices'].extend(tri)
    if not surfaces:raise ValueError(f'{name}: empty model')
    return surfaces


def place(surface,origin,axis,scale):
    if not math.isfinite(scale) or not 0<scale<=100:raise ValueError('Invalid model scale')
    # GfxPackedPlacement axis stores three local basis vectors, not matrix rows.
    rotate=lambda v:[sum(axis[j][i]*v[j] for j in range(3)) for i in range(3)]
    vertices=[]
    for v in surface['vertices']:
        p=rotate(v['position']);n=rotate(v['normal_vec'] if 'normal_vec' in v else unpack_normal(v['normal']))
        vertices.append({'position':[origin[i]+p[i]*scale for i in range(3)],'uv':v['uv'],'normal':packed_normal(n)})
    return {**surface,'vertices':vertices}


def brush_hull(brush,index):
    lo=brush['mins'];hi=brush['maxs'];planes=list(brush['planes'])
    if not planes:
        points=[[x,y,z] for x in (lo[0],hi[0]) for y in (lo[1],hi[1]) for z in (lo[2],hi[2])]
        return {'vertices':points,'source':f'compiled brush {index}','contents':brush['contents']}
    for k in range(3):
        n=[0.,0.,0.];n[k]=1.;planes.append(n+[hi[k]])
        n=[0.,0.,0.];n[k]=-1.;planes.append(n+[-lo[k]])
    if len(planes)>70:raise ValueError(f'Brush {index}: too many planes')
    points=[]
    for a,b,c in combinations(planes,3):
        bc=cross(b,c);det=dot(a[:3],bc)
        if abs(det)<1e-9:continue
        ca=cross(c,a);ab=cross(a,b)
        p=[(a[3]*bc[k]+b[3]*ca[k]+c[3]*ab[k])/det for k in range(3)]
        if all(dot(n[:3],p)<=n[3]+.01 for n in planes) and not any(sum((p[k]-q[k])**2 for k in range(3))<1e-6 for q in points):points.append(p)
    if not 4<=len(points)<=252:raise ValueError(f'Brush {index}: invalid hull with {len(points)} points')
    return {'vertices':points,'source':f'compiled brush {index}','contents':brush['contents']}
