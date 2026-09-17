"""Assemble stock Replay weapon parts from native attachment tables and XModels."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import threading

from build_viewhands import read_glb, write_glb, accessor_values, matrix, multiply, inverse_rigid
from graph import walk, attachment_slots


def refs(record, kind):
    return {f['field']:f for _,f in walk(record) if f.get('kind')==kind}


class StockLibrary:
    def __init__(self, config, library, cache):
        self.library=Path(library);self.cache=Path(cache)
        self.game=Path(config['game'])
        self.exporter=Path(config.get('stock_exporter',''))
        self.lock=threading.RLock()

    def extract(self, fastfile, name=None, asset_type='xmodel'):
        destination=self.cache.parent/'stock-raw'
        output=destination/'mw19replay'/fastfile.stem
        ready=output/('.'+asset_type+'.'+name+('.packages' if asset_type=='xmodelsurfs' else '')+'.ready' if name else '.ready')
        if not ready.exists():
            destination.mkdir(parents=True,exist_ok=True)
            if shutil.disk_usage(destination).free < 512*1024*1024:
                raise ValueError('Stock cache needs at least 512 MiB of free disk space')
            if not self.exporter.is_file():raise ValueError('Configure the local stock model exporter first')
            command=[str(self.exporter),'--noUpdater','fastfile','-r','mw19replay','-g',str(self.game/'game_dx12_ship_replay.exe'),
                     '--oodle',str(self.game/'oo2core_7_win64.dll'),'--geometry','-o',('\\\\?\\' if os.name=='nt' else '')+str(destination.resolve())]
            if name:command+=['-a',asset_type,'-n',name]
            if name and asset_type=='xmodelsurfs':
                packages=sorted((self.game/'zone').glob('pak_*.xpak'))
                if len(packages)>64:raise ValueError('The stock exporter accepts at most 64 XPak archives')
                for package in packages:command+=['--xpak',str(package)]
            command.append(str(fastfile))
            try:
                result=subprocess.run(command,cwd=self.game,capture_output=True,text=True,
                                      timeout=180 if name and asset_type=='xmodelsurfs' else 90,
                                      creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            except subprocess.TimeoutExpired as error:
                raise ValueError('Stock extraction timed out for '+fastfile.name+'; retry when disk activity is lower.') from error
            manifest=output/'manifest.json'
            status=json.loads(manifest.read_text()) if manifest.exists() else {}
            unavailable=(status.get('unavailable',0)>0 and status.get('failed')==0 and 'Replay:' in result.stdout)
            if not unavailable and (result.returncode or not status.get('complete')):
                raise ValueError('Stock extraction failed for '+fastfile.name+': '+(result.stdout+result.stderr)[-500:])
            for glb in (output/'assets/xmodelsurfs').glob('*.geometry.glb'):
                with glb.open('rb') as stream:header=stream.read(12)
                if len(header)!=12 or struct.unpack('<III',header)!=(0x46546c67,2,glb.stat().st_size):
                    raise ValueError('Incomplete stock geometry export: '+glb.name)
            ready.touch()
        return output

    def model(self, name):
        zone=self.game/'zone'
        prefixes=['mp_av_','mp_wv_'] if '_vm_' in name else ['mp_aw_','mp_ww_','mp_av_']
        candidates=[]
        for prefix in prefixes:
            exact=zone/(prefix+name+'_tr.ff')
            if exact.is_file():candidates.append(exact)
            candidates+=sorted(zone.glob(prefix+name+'_lod*_tr.ff'))
        # Some non-modular weapon models live directly in the persistent zone.
        candidates.extend([zone/'global_stream_mp.ff',zone/'common_mp.ff'])
        for fastfile in candidates:
            shared=fastfile.stem in ('global_stream_mp','common_mp')
            output=self.extract(fastfile,name if shared else None)
            for file in (output/'assets/xmodel').glob(name+'.*.asset.json'):
                native=json.loads(file.read_text(encoding='utf-8-sig'))['asset']['fields']
                if native['name']['string'].lstrip(',')!=name:continue
                geometry=None
                if native['numsurfs']:
                    lod=native['lodInfo'][0];surfs=(lod.get('modelSurfsStaging') or {}).get('name','').lstrip(',')
                    geometry=next((output/'assets/xmodelsurfs').glob(surfs+'.*.geometry.glb'),None)
                    if not geometry and shared:
                        self.extract(fastfile,surfs,'xmodelsurfs')
                        geometry=next((output/'assets/xmodelsurfs').glob(surfs+'.*.geometry.glb'),None)
                        if not geometry and fastfile.stem=='global_stream_mp':
                            for shared_name in ['global_mp','global_core_mp','global','common_stream_mp','common_base_mp','common_core_mp','common_mp']:
                                geometry_output=self.extract(zone/(shared_name+'.ff'),surfs,'xmodelsurfs')
                                geometry=next((geometry_output/'assets/xmodelsurfs').glob(surfs+'.*.geometry.glb'),None)
                                if geometry:break
                    if not geometry:continue
                strings=json.loads((output/'script_strings.json').read_text(encoding='utf-8-sig'))
                return native,strings,geometry
        raise ValueError('Stock model payload is not available: '+name)

    def assemble(self, descriptor, tables, view, folder):
        reference=json.loads((self.library/descriptor['file']).read_text())
        components=[{'name':descriptor['models']['gunXModel' if view=='view_model' else 'worldModel'],'attach':''}]
        header=tables['mp/attachmentmap.csv'][0];mapping={}
        for key in [descriptor['category'],descriptor['base']]:
            row=next((r for r in tables['mp/attachmentmap.csv'] if r[0]==key),[])
            mapping.update({header[i]:v for i,v in enumerate(row) if i and v})
        defaults=(descriptor.get('stats_row') or ['']*10)[9].split()
        slots={name for group in attachment_slots(reference['root']) for name in group}
        for token in defaults:
            name=mapping.get(token)
            if not name or name not in slots:continue
            attachment=json.loads((self.library/'attachments'/(name+'.json')).read_text())['root']
            models=refs(attachment,'asset');scripts=refs(attachment,'script')
            model=models.get('sfx.'+('viewModelVariations[0]' if view=='view_model' else 'worldModelVariations[0]'))
            if model:components.append({'name':model['name'],'attach':scripts.get('sfx.attachPoint',{}).get('text','')})

        names=[];parents=[];locals_=[];world=[];primitives=[];parts=[];vertex_weights=[]
        obj=[];vertex_offset=0;binary=bytearray();document={'asset':{'version':'2.0'},'bufferViews':[],'accessors':[]}
        warnings=[]
        def accessor(values,code,components,component_type):
            while len(binary)%4:binary.append(0)
            start=len(binary)
            for row in values:binary.extend(struct.pack('<'+code*components,*row))
            index=len(document['accessors']);bv=len(document['bufferViews'])
            document['bufferViews'].append({'buffer':0,'byteOffset':start,'byteLength':len(binary)-start})
            document['accessors'].append({'bufferView':bv,'componentType':component_type,'count':len(values),
                                          'type':{1:'SCALAR',2:'VEC2',3:'VEC3',4:'VEC4'}[components]})
            if components==3 and values:
                document['accessors'][-1].update(min=[min(v[i] for v in values) for i in range(3)],
                                                max=[max(v[i] for v in values) for i in range(3)])
            return index
        for component in components:
            if not component['name']:continue
            native,strings,geometry=self.model(component['name'])
            count=native['numBones']+native.get('numClientBones',0);roots=native['numRootBones']
            bone_names=[strings[b['value']] for b in native['boneNames']['values']]
            if len(bone_names)!=count:raise ValueError('Stock skeleton name count mismatch')
            parent_bytes=bytes.fromhex((native.get('parentList') or {}).get('bytes',''))
            base=native['baseMat']['values'];trans=(native.get('trans') or {}).get('values',[]);quats=(native.get('quats') or {}).get('values',[])
            attach=component['attach'] or (bone_names[0] if names else '')
            if names and attach not in names:raise ValueError('Stock attachment tag is missing: '+attach)
            alignment=world[names.index(attach)] if names else matrix([0,0,0],[0,0,0,1])
            local_map=[]
            for i,name in enumerate(bone_names):
                if name in names:local_map.append(names.index(name));continue
                if i<roots:
                    p=names.index(attach) if names else -1
                    t=base[i]['trans']['v'];q=base[i]['quat']['v']
                else:
                    p=local_map[i-parent_bytes[i-roots]];j=i-roots;t=trans[j*3:j*3+3];q=[v/32767 for v in quats[j*4:j*4+4]]
                if p>=len(names) or p < -1:raise ValueError('Invalid stock bone hierarchy')
                names.append(name);parents.append(p);locals_.append((t,q));m=matrix(t,q)
                world.append(multiply(world[p],m) if p>=0 else m);local_map.append(len(names)-1)
            if not geometry:continue
            source,payload=read_glb(geometry)
            root_bind=matrix(base[0]['trans']['v'],base[0]['quat']['v'])
            placement=multiply(alignment,inverse_rigid(root_bind))
            for surface,primitive in enumerate(source['meshes'][0]['primitives']):
                attrs=primitive['attributes'];positions=accessor_values(source,payload,attrs['POSITION'])
                normals=accessor_values(source,payload,attrs['NORMAL']);uvs=accessor_values(source,payload,attrs['TEXCOORD_0'])
                if not {'JOINTS_0','WEIGHTS_0'}<=attrs.keys():raise ValueError('Stock geometry exporter must include skin weights')
                joints=accessor_values(source,payload,attrs['JOINTS_0']);weights=accessor_values(source,payload,attrs['WEIGHTS_0'])
                triangles=accessor_values(source,payload,primitive['indices'])
                positions=[[sum(placement[r][c]*v[c] for c in range(3))+placement[r][3] for r in range(3)] for v in positions]
                normals=[[sum(placement[r][c]*v[c] for c in range(3)) for r in range(3)] for v in normals]
                mapped=[[local_map[j] if w>0 else 0 for j,w in zip(js,ws)] for js,ws in zip(joints,weights)]
                part=component['name']+'_'+str(surface);parts.append(part);obj.append('g '+part)
                for v,n,uv,js,ws in zip(positions,normals,uvs,mapped,weights):
                    obj+=['v '+' '.join(map(str,v)),'vn '+' '.join(map(str,n)),'vt '+' '.join(map(str,uv))]
                    vertex_weights.append([{'bone':names[j],'weight':w} for j,w in zip(js,ws) if w>0])
                for i in range(0,len(triangles),3):
                    obj.append('f '+' '.join(f'{k}/{k}/{k}' for k in [vertex_offset+triangles[i+j][0]+1 for j in range(3)]))
                vertex_offset+=len(positions)
                primitives.append({'attributes':{'POSITION':accessor(positions,'f',3,5126),'NORMAL':accessor(normals,'f',3,5126),
                                                  'TEXCOORD_0':accessor(uvs,'f',2,5126)},
                                   'indices':accessor(triangles,'I',1,5125),'mode':4,'extras':{'nativePart':part}})
        if not names:raise ValueError('Stock weapon has no model skeleton')
        if not primitives:warnings.append('This stock asset has no weapon render surfaces; its native skeleton is available.')
        root_count=sum(p<0 for p in parents)
        if any(p<0 for p in parents[root_count:]):raise ValueError('Stock roots are not ordered before child bones')
        # bind_pose is rebuilt by the existing native rig helper from these local transforms.
        rig={'bones':names,'root_bones':root_count,'parents':[i-parents[i] for i in range(root_count,len(names))],
             'translations':[t for t,q in locals_[root_count:]],'quats':[[round(v*32767) for v in q] for t,q in locals_[root_count:]],
             'classification':[0]*len(names),'bind_pose':[], 'rigid_bone':0,'material':'','part_bones':{},'vertex_weights':vertex_weights,
             'transform':[[1,0,0,0],[0,1,0,0],[0,0,1,0]],'replace':[descriptor['models']['gunXModel' if view=='view_model' else 'worldModel']]}
        from graph import rebuild_bind_pose
        rebuild_bind_pose(rig)
        folder.mkdir(parents=True,exist_ok=True);(folder/(view+'.obj')).write_text('\n'.join(obj)+'\n',encoding='utf-8')
        # Each primitive gets a stable mesh name matching the canonical OBJ part assignment.
        document['meshes']=[{'name':name,'primitives':[primitive]} for name,primitive in zip(parts,primitives)]
        document['nodes']=[{'name':name,'mesh':i} for i,name in enumerate(parts)]
        document['scenes']=[{'nodes':list(range(len(parts)))}];document['scene']=0
        write_glb(folder/(view+'.glb'),document,binary)
        return {'rig':rig,'parts':parts,'components':components,'warnings':warnings}

    def load(self, descriptor, tables):
        with self.lock:
            folder=self.cache/'assembled'/descriptor['name'];manifest=folder/'stock.json'
            if not manifest.exists():
                result={view:self.assemble(descriptor,tables,view,folder) for view in ['view_model','world_model']}
                pending=manifest.with_suffix('.tmp')
                pending.write_text(json.dumps(result),encoding='utf-8');pending.replace(manifest)
            return folder,json.loads(manifest.read_text(encoding='utf-8'))
