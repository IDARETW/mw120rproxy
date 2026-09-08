"""Bounded CoD4 v25 raw-model reader for static Radiant props.

Format cross-checked against mauserzjeh/cod-asset-importer assets/xmodel.rs and
xmodelsurf.rs and the owner's Mod Tools files. No Blender/runtime dependency.
Positions in v25 surfaces are model-space bind positions; no animation is run.
"""
from pathlib import Path
import math
import re
import struct
import zipfile
from prepare_textured_mp_test import decode_iwi
from replay_mesh_math import quaternion, pack

class BlendedMaterial(ValueError):pass
class ShadowOnlyMaterial(ValueError):pass


class Reader:
    def __init__(self,path):
        if path.stat().st_size>64*1024*1024:raise ValueError(f'Asset too large: {path}')
        self.data=path.read_bytes();self.pos=0;self.path=path
    def take(self,n):
        if n<0 or self.pos+n>len(self.data):raise ValueError(f'Truncated {self.path} at {self.pos:#x}')
        b=self.data[self.pos:self.pos+n];self.pos+=n;return b
    def read(self,fmt):
        v=struct.unpack('<'+fmt,self.take(struct.calcsize('<'+fmt)))
        return v[0] if len(v)==1 else list(v)
    def string(self):
        end=self.data.find(b'\0',self.pos,self.pos+512)
        if end<0:raise ValueError(f'Unterminated string in {self.path}')
        return self.take(end-self.pos+1)[:-1].decode('ascii')


def asset_path(root,kind,name):
    if not name or '\\' in name or '..' in name or name.startswith('/') or ':' in name:
        raise ValueError(f'Invalid {kind} asset name {name!r}')
    p=(root/kind/name).resolve()
    if not p.is_relative_to(root.resolve()):raise ValueError('Asset path escapes raw directory')
    return p


def load_model(raw,name):
    r=Reader(asset_path(raw,'xmodel',name))
    if r.read('H')!=25:raise ValueError(f'{name}: expected CoD4 model v25')
    r.take(25);r.string()
    lods=[]
    for _ in range(4):
        distance=r.read('f');lod=r.string()
        if lod:lods.append({'name':lod,'distance':distance})
    r.take(4);count=r.read('I')
    if count>4096:raise ValueError('Too many model collision surfaces')
    for _ in range(count):
        n=r.read('I');r.take(n*48+36)
    for lod in lods:
        n=r.read('H')
        if n>256:raise ValueError('Too many model materials')
        lod['materials']=[r.string() for _ in range(n)]
    if not lods:raise ValueError(f'{name}: no model LOD')
    surfaces=load_surfaces(asset_path(raw,'xmodelsurfs',lods[0]['name']))
    if len(surfaces)!=len(lods[0]['materials']):raise ValueError(f'{name}: surface/material mismatch')
    for s,m in zip(surfaces,lods[0]['materials']):s['material']=m
    return surfaces


def load_surfaces(path):
    r=Reader(path)
    if r.read('H')!=25:raise ValueError(f'{path}: expected CoD4 surfaces v25')
    count=r.read('H');surfaces=[]
    if not 1<=count<=256:raise ValueError('Invalid model surface count')
    for _ in range(count):
        r.take(3);nv,nt,nv2=r.read('3H')
        if not nv or not nt:raise ValueError('Empty model surface')
        if nv!=nv2:
            r.take(2)
            if nv2:
                while r.read('H')!=0:pass
                r.take(2)
        else:r.take(4)
        vertices=[]
        for _ in range(nv):
            normal=r.read('3f');color=r.read('4B');uv=r.read('2f');tangent=r.read('3f');binormal=r.read('3f')
            weights=0
            if nv!=nv2:weights=r.read('B');r.read('H')
            position=r.read('3f')
            if weights>16:raise ValueError('Invalid model skin weights')
            for _ in range(weights):r.read('2H')
            values=normal+uv+tangent+binormal+position
            if not all(math.isfinite(x) for x in values):raise ValueError('Nonfinite model vertex')
            vertices.append({'position':position,'normal':normal,'uv':uv,'tangent':tangent,'binormal':binormal})
        indices=r.read(str(nt*3)+'H')
        if max(indices)>=nv:raise ValueError('Model triangle index outside vertex array')
        surfaces.append({'vertices':vertices,'indices':indices})
    if r.pos!=len(r.data):raise ValueError(f'{path}: unexpected surface tail ({len(r.data)-r.pos} bytes)')
    return surfaces


def material_image(raw,name,allow_blend=False):
    r=Reader(asset_path(raw,'materials',name));b=r.data
    def string_at(offset):
        if offset>=len(b):raise ValueError(f'{name}: invalid material offset')
        r.pos=offset;return r.string()
    if len(b)<64:raise ValueError(f'{name}: short material')
    tech=string_at(struct.unpack_from('<I',b,52)[0])
    if tech=='shadowcaster':raise ShadowOnlyMaterial(f'{name}: shadow-caster helper is not a visible surface')
    # Alpha-blended glass/decals require a transparent draw list. Do not silently
    # turn them into opaque black sheets in the current opaque BSP pipeline.
    if re.search(r'(^|_)b\d',tech) and not allow_blend:raise BlendedMaterial(f'{name}: blended material {tech} needs a transparent draw list')
    table=struct.unpack_from('<I',b,56)[0]
    for i in range(b[48]):
        key,semantic,image=struct.unpack_from('<III',b,table+i*12)
        if string_at(key)=='colorMap':return string_at(image),bool(re.search(r'(^|_)[at]\d',tech))
    raise ValueError(f'{name}: no colorMap')


class Images:
    def __init__(self,game,raw):
        self.raw=raw;self.archives=sorted(game.rglob('*.iwd'),reverse=True);self.cache={}
    def get(self,name):
        if name in self.cache:return self.cache[name]
        p=asset_path(self.raw,'images',name+'.iwi')
        if p.exists():data=p.read_bytes()
        else:
            data=None
            for path in self.archives:
                with zipfile.ZipFile(path) as z:
                    try:info=z.getinfo('images/'+name+'.iwi')
                    except KeyError:continue
                    if info.file_size>64*1024*1024:raise ValueError('Image too large')
                    data=z.read(info);break
            if data is None:raise FileNotFoundError(f'Missing CoD4 image {name}')
        self.cache[name]=decode_iwi(data)
        return self.cache[name]


def rotation(angles):
    p,y,r=[math.radians(float(x)) for x in angles]
    cp,sp,cy,sy,cr,sr=math.cos(p),math.sin(p),math.cos(y),math.sin(y),math.cos(r),math.sin(r)
    return [[cy*cp,cy*sp*sr-sy*cr,cy*sp*cr+sy*sr],[sy*cp,sy*sp*sr+cy*cr,sy*sp*cr-cy*sr],[-sp,cp*sr,cp*cr]]


def transformed(surface,origin,angles,scale):
    matrix=rotation(angles)
    rotate=lambda v:[sum(row[k]*v[k] for k in range(3)) for row in matrix]
    out=[]
    if not math.isfinite(scale) or not 0<scale<=100:raise ValueError('Invalid positive modelscale')
    for v in surface['vertices']:
        pos=rotate(v['position']);n=rotate(v['normal']);t=rotate(v['tangent']);b=rotate(v['binormal'])
        q,sign=quaternion(t,b,n)
        out.append({'position':[pos[k]*scale+origin[k] for k in range(3)],'uv':v['uv'],'normal':pack(q,sign)})
    return {'vertices':out,'indices':surface['indices'],'material':surface['material']}
