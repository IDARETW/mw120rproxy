"""Convert an offline OAT IW3 map dump into its own selectable Replay package."""
import argparse
from collections import Counter
from datetime import datetime
import hashlib
import gc
import json
import math
from pathlib import Path
import re
import sys
from PIL import Image
from build_mp_test import REPO,TOOLS,BASE,REPLAY,COD4,run,write_json,numeric_entities
from imported_map_assets import read_material,read_obj,place,packed_normal,brush_hull,safe_asset
from prepare_textured_mp_test import decode_iwi,sky_surfaces
from cod4_assets import Images,rotation
from foliage_mesh import mask_rectangles,mask_geometry,cutout
from radiant_source import blocks,properties,vector
import radiant_collision
import ladder_data
import glass_panes
import map_lighting
import window_entities
from foliage_material import classify


def merge_surfaces(surfaces):
    output=[];active={}
    for s in surfaces:
        if not s['indices']:continue
        key=(s['material'],s.get('glassPane'));target=active.get(key)
        if len(s['vertices'])>60000 or len(s['indices'])>65535*3:raise ValueError('Source surface exceeds native mesh limits')
        if target is None or len(target['vertices'])+len(s['vertices'])>60000 or len(target['indices'])+len(s['indices'])>65535*3:
            target={'material':s['material'],'vertices':[],'indices':[]}
            if 'glassPane' in s:target['glassPane']=s['glassPane']
            active[key]=target;output.append(target)
        first=len(target['vertices']);target['vertices'].extend(s['vertices']);target['indices'].extend(i+first for i in s['indices'])
    return output


def build(args):
    mapid=args.map
    if not re.fullmatch(r'mp_[a-z0-9_]{1,60}',mapid):raise ValueError('Invalid map id')
    source=args.dump.resolve();world=json.loads((source/f'maps/mp/{mapid}.d3dbsp.replay-world.json').read_text())
    collision=json.loads((source/f'maps/mp/{mapid}.d3dbsp.replay-collision.json').read_text())
    if world['name']!=f'maps/mp/{mapid}.d3dbsp' or collision['name']!=world['name']:raise ValueError('Mismatched map intermediates')
    out=REPO/f'custom_map_sources/{mapid}/builds'/datetime.now().strftime('%Y%m%d-%H%M%S-%f');out.mkdir(parents=True)
    print(f'Build output: {out}',flush=True)
    entitytext=(source/f'maps/mp/{mapid}.d3dbsp.ents').read_text()
    entities=[properties(b) for b in blocks(entitytext)]
    spawn_entities=entities
    if args.spawn_mode=='deathrun-tdm':
        spawn_entities=list(entities)
        for e in entities:
            if e.get('classname') in ('mp_jumper_spawn','mp_activator_spawn'):
                team='allies' if e['classname']=='mp_jumper_spawn' else 'axis'
                spawn_entities.extend([{**e,'classname':'mp_tdm_spawn'},{**e,'classname':f'mp_tdm_spawn_{team}_start'}])
        first=next((e for e in entities if e.get('classname')=='mp_jumper_spawn'),None)
        if first:spawn_entities.append({**first,'classname':'info_player_start'})
    numeric,spawns,omitted_entities=numeric_entities(spawn_entities)
    hidden,windows=window_entities.states(entities)
    def visible_entity(e):
        return bool(e) and id(e) not in hidden and e.get('targetname')!='exploder' and not e.get('script_fxid') and e.get('script_gameobjectname','tdm')=='tdm'
    brush_entities={int(e['model'][1:]):e for e in entities if re.fullmatch(r'\*\d+',e.get('model',''))}
    surface_entities={}
    for index,m in enumerate(world['brush_models'][1:],1):
        for si in range(m['start'],m['start']+m['count']):surface_entities[si]=brush_entities.get(index)
    def transform(v,e):
        matrix=rotation(vector(e.get('angles','0 0 0')));origin=vector(e.get('origin','0 0 0'))
        return [sum(matrix[i][j]*v[j] for j in range(3))+origin[i] for i in range(3)]
    materials={};omitted=Counter();surfaces=[];model_cache={}
    def include(name):
        if name not in materials:materials[name]=read_material(source,name)
        if 'skip' in materials[name]:omitted[name]+=1;return False
        return True
    images={};fallback=Images(COD4,COD4/'raw');foliage=[];masks={}
    def image(name):
        if name not in images:
            p=safe_asset(source,'images',name,'.iwi')
            images[name]=decode_iwi(p.read_bytes()) if p.is_file() else fallback.get(name)
        return images[name]
    def alpha_convert(s):
        material=materials[s['material']]
        classify(s['material'],material,image(material['image'])[0])
        if material.get('cutout'):foliage.append({'material':s['material'],'method':'gpu_alpha_test','threshold':128,'input_triangles':len(s['indices'])//3})
        return [s]
    for si,s in enumerate(world['surfaces']):
        entity=surface_entities.get(si)
        if si in surface_entities and (not visible_entity(entity) or entity.get('classname')!='script_brushmodel'):continue
        if not include(s['material']):continue
        vertices=[]
        for v in s['vertices']:
            position=v['position'];normal=v['normal'];tangent=v['tangent']
            if entity:
                position=transform(position,entity);matrix=rotation(vector(entity.get('angles','0 0 0')))
                normal=[sum(matrix[i][j]*normal[j] for j in range(3)) for i in range(3)]
                tangent=[sum(matrix[i][j]*tangent[j] for j in range(3)) for i in range(3)]
            vertices.append({'position':position,'uv':v['uv'],'normal':packed_normal(normal,tangent,v['binormal_sign']),'baked_uv':v.get('lightmap_uv',[0,0]),'baked_index':s.get('lightmap',255)})
        surface={**s,'vertices':vertices}
        if id(entity) in windows:surface['glassGroup']=windows[id(entity)]
        surfaces.extend(alpha_convert(surface))
    instances=list(world['models'])
    for e in entities:
        if e.get('classname')!='script_model' or not e.get('model') or not visible_entity(e):continue
        matrix=rotation(vector(e.get('angles','0 0 0')))
        instances.append({'model':e['model'].removeprefix('xmodel/'),'origin':vector(e.get('origin','0 0 0')),
            'axis':[list(v) for v in zip(*matrix)],'scale':float(e.get('modelscale',1)),
            **({'glassGroup':windows[id(e)]} if id(e) in windows else {})})
    for instance_index,instance in enumerate(instances):
        if instance_index%100==0:print(f'Baking prop {instance_index+1}/{len(instances)} ({len(model_cache)} unique models)',flush=True)
        name=instance['model']
        if name not in model_cache:
            model_cache[name]=[]
            for s in read_obj(source,name):
                if include(s['material']):
                    model_cache[name].extend(alpha_convert(place(s,[0,0,0],[[1,0,0],[0,1,0],[0,0,1]],1)))
        for s in model_cache[name]:
            surface=place(s,instance['origin'],instance['axis'],instance['scale'])
            if 'glassGroup' in instance:surface['glassGroup']=instance['glassGroup']
            surfaces.append(surface)
    from decal_geometry import separate
    decal_report=separate(surfaces,materials)
    print('Decal separation: '+str(decal_report),flush=True)
    surfaces,panes=glass_panes.prepare(surfaces,materials)
    surfaces=merge_surfaces(surfaces)
    print(f'Geometry: {len(surfaces)} surfaces, {sum(len(s["indices"])//3 for s in surfaces)} triangles',flush=True)
    names=sorted({materials[s['material']]['image'] for s in surfaces})
    if len(names)>250:raise ValueError(f'{len(names)} images exceeds 250-image atlas budget')
    columns=1
    while columns*columns<len(names)+6:columns*=2
    columns=max(4,columns);cell=4096//columns;atlas=Image.new('RGBA',(4096,4096))
    for i,name in enumerate(names):
        faces=image(name)
        if len(faces)!=1:raise ValueError(f'{name}: expected 2D color image')
        atlas.paste(faces[0].resize((cell,cell),Image.Resampling.LANCZOS),((i%columns)*cell,(i//columns)*cell))
    skyname=world['sky'];sky=image(skyname)
    if len(sky)!=6:raise ValueError(f'{skyname}: expected cubemap')
    for i,face in enumerate(sky,start=len(names)):
        atlas.paste(face.resize((cell,cell),Image.Resampling.LANCZOS),((i%columns)*cell,(i//columns)*cell))
    lightmaps=map_lighting.pack(atlas,source,world,math.ceil((len(names)+6)/columns),cell)
    has_cutout=any(m.get('cutout') for m in materials.values());has_glass=any(m.get('blended') for m in materials.values())
    for s in surfaces:
        mat=materials[s.pop('material')];tile=names.index(mat['image'])
        kind=3 if mat.get('cutout') else (2 if mat.get('blended') else 0)
        s['materialIndex']=1 if kind==3 else ((2 if has_cutout else 1) if kind==2 else 0)
        for v in s['vertices']:
            index=v.pop('baked_index',255);uv=v.pop('baked_uv',[0,0])
            coord=map_lighting.coordinates(lightmaps[index],uv) if index<len(lightmaps) else [0,0]
            v['lightmapUV']=[tile+.25+coord[0]*.25,kind+(4 if index<len(lightmaps) else 0)+.25+coord[1]*.25]
    for i,s in enumerate(sky_surfaces()):
        for v in s['vertices']:v['lightmapUV']=[len(names)+i+.25,1.25]
        surfaces.append(s)
    surfaces.sort(key=lambda s:s.get('materialIndex',0))
    (out/'glass.bin').write_bytes(glass_panes.encode(panes,surfaces))
    blended_surfaces=sum(s.get('materialIndex',0)==(2 if has_cutout else 1) for s in surfaces) if has_glass else 0
    if len(surfaces)>4096:raise ValueError('Too many render surfaces')
    brushes=[];ladders=[];skipped_contents=Counter();brush_owners={}
    def leaf_brushes(root):
        if root==0:return set() # CoD4's explicit empty brush-leaf sentinel.
        todo=[root];seen=set();result=set();nodes=collision['leaf_brush_nodes']
        while todo:
            n=todo.pop()
            if n in seen:continue
            if not 0<n<len(nodes):raise ValueError('Invalid compiled leaf brush node')
            seen.add(n);node=nodes[n]
            if node['count']>0:result.update(node['brushes'])
            else:
                if node['count']<0:todo.append(n+1)
                todo.extend(n+offset for offset in node['children'] if offset)
        return result
    for i,m in enumerate(collision['submodels'][1:],1):
        for b in leaf_brushes(m['leaf']):
            if b in brush_owners:raise ValueError('Brush belongs to multiple submodels')
            brush_owners[b]=brush_entities.get(i)
    for i,b in enumerate(collision['brushes']):
        # CoD4 solid or playerclip. Sky/trigger/portal-only brushes are not walls.
        if not b['contents'] & (1|0x10000):skipped_contents[str(b['contents'])]+=1;continue
        entity=brush_owners.get(i)
        if i in brush_owners and (not visible_entity(entity) or entity.get('classname')!='script_brushmodel'):continue
        h=brush_hull(b,i)
        if entity:h['vertices']=[transform(v,entity) for v in h['vertices']]
        brushes.append(h)
        if not entity:ladders.extend(ladder_data.faces(b))
    triangle_hulls=radiant_collision.add_compiled_triangles(brushes,collision)
    brushes,glass_collision_removed=glass_panes.remove_static_collision(brushes,panes)
    write_json(out/'source_collision.json',brushes)
    ladder_matches=ladder_data.align_models(ladders,instances,source)
    (out/'ladders.bin').write_bytes(ladder_data.encode(ladders))
    folder=out/'dump/maps/mp';folder.mkdir(parents=True)
    stem=mapid+'.d3dbsp'
    (folder/(stem+'.ents')).write_text(entitytext,encoding='ascii')
    (out/f'dump/{mapid}_iw8_ents.txt').write_text(numeric,encoding='ascii')
    with (folder/(stem+'.render.json')).open('w',encoding='utf-8') as mesh_output:
        json.dump({'schema':1,'material':f'w/mw120r_{mapid}',
            'materialDefinition':stem+'.material.json',
            'additionalMaterials':[{'schema':1,'material':f'w/mw120r_{mapid}_{kind}','materialDefinition':stem+'.'+kind+'.material.json'} for kind,enabled in [('foliage',has_cutout),('glass',has_glass)] if enabled],
            'surfaces':surfaces},mesh_output,separators=(',',':'))
    material=json.loads((BASE/'dump/maps/mp/mp_test.d3dbsp.material.json').read_text())
    pixels=atlas.tobytes();image_name=f'mw120r/{mapid}_'+hashlib.sha256(pixels).hexdigest()[:16]
    material['textures'][0]['image']=image_name;material['techsetDefinition']=stem+'.techset.json'
    material['imageDefinitions']=[{'name':image_name,'width':4096,'height':4096,'rgba8':mapid+'_atlas.rgba'}]
    write_json(folder/(stem+'.material.json'),material)
    (folder/(mapid+'_atlas.rgba')).write_bytes(pixels);atlas.save(out/'atlas.png')
    direction,sun_color=map_lighting.sun(entities[0])
    from map_presentation import prepare
    prepare(folder,mapid,columns,direction,sun_color,sky)
    shader=(TOOLS/'map_surface.hlsl').read_text().replace('ATLAS_COLUMNS',str(columns)).replace('SUN_DIRECTION','float3('+','.join(map(str,direction))+')').replace('SUN_COLOR','float3('+','.join(map(str,sun_color))+')')
    (out/'map.hlsl').write_text(shader)
    run([sys.executable,TOOLS/'compile_graybox_shader.py','--source',out/'map.hlsl','--target-root',out,'--map',mapid],REPO,out/'shader.log')
    from glass_material import create
    for kind,enabled in [('foliage',has_cutout),('glass',has_glass)]:
        if enabled:create(folder,stem,mapid,kind)
    converter=REPO/'iw8-zonetool/xmake-out/x64/Release/iw8-zonetool.exe';package=out/'package'
    report={'map':mapid,'title':args.title,'credit':args.credit,'source':str(source),'package':str(package),
        'surfaces':len(surfaces),'vertices':sum(len(s['vertices']) for s in surfaces),'triangles':sum(len(s['indices'])//3 for s in surfaces),
        'models':len(world['models']),'unique_models':len(model_cache),'materials':materials,'decal_separation':decal_report,'omitted_materials':dict(omitted),'spawn_mode':args.spawn_mode,
        'color_images':names,'atlas_cell':cell,'sky':skyname,'foliage':foliage,'spawns':spawns,'omitted_entities':omitted_entities,
        'collision_hulls':len(brushes),'triangle_hulls':triangle_hulls,'glass_collision_removed':glass_collision_removed,'hidden_window_states':len(hidden),'scripted_windows':len(windows),'skipped_brush_contents':dict(skipped_contents),
        'static_collision_models':collision['static_models'],'submodel_count':collision['submodel_count'],'game_tested':False,
        'foliage_mask_size':args.foliage_mask_size,'ladder_faces':len(ladders),'blended_surfaces':blended_surfaces,'glass_panes':len(panes),'measured_ladder_faces':ladder_matches,'lightmaps':len(lightmaps),'sun_direction':direction,'sun_color':sun_color}
    write_json(out/'import_report.json',report)
    del surfaces,world,model_cache,images,brushes,collision,atlas,pixels
    gc.collect()
    run([converter,'fromdump',out/'dump',mapid,'-o',package,'--stored'],REPO,out/'convert.log')
    from finish_imported_map import finish
    finish(out,mapid,args.title,args.credit,source)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--dump',type=Path,required=True)
    parser.add_argument('--map',required=True);parser.add_argument('--title',required=True);parser.add_argument('--credit',default='')
    parser.add_argument('--spawn-mode',choices=('native-tdm','deathrun-tdm'),default='native-tdm')
    parser.add_argument('--foliage-mask-size',type=int,choices=(16,32,64,128),default=32)
    build(parser.parse_args())
