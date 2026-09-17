"""Local Replay weapon workbench. Run with Python 3.11+; binds loopback only."""
import argparse
import base64
import copy
from datetime import datetime, timezone
from functools import partial
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
import mimetypes
import os
from pathlib import Path
import re
import shutil
import subprocess
import threading
import time
from urllib.parse import unquote, urlparse, parse_qs
import uuid
import zipfile

from graph import (flatten, set_value, walk, attachment_slots, set_attachment_slots,
                   set_bone_world, rebuild_bind_pose, edit_hierarchy, SLOTS)
from material import generate as generate_material
from sound import generate as generate_sound_bank
from tables import registration
from animation import AnimationLibrary
from stock import StockLibrary

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
LOCAL = Path(os.environ.get('LOCALAPPDATA', HERE/'workspace'))/'ReplayWeaponEditor'
SOURCE_EXTENSIONS = frozenset(('.obj','.glb','.gltf','.bin','.fbx','.png','.jpg','.jpeg',
                               '.webp','.tga','.wav','.ogg','.mp3','.json'))
MAX_SOURCE_BYTES = 128*1024*1024
MAX_OBJ_BYTES = 512*1024*1024


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def atomic(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name+'.tmp-'+uuid.uuid4().hex)
    tmp.write_text(json.dumps(data, ensure_ascii=False, allow_nan=False, indent=2)+'\n', encoding='utf-8')
    os.replace(tmp, path)


def safe(root, relative):
    candidate = (root/relative).resolve()
    if not candidate.is_relative_to(root.resolve()):
        raise ValueError('Path must stay inside the project')
    return candidate


def validate_obj_faces(path):
    """Require polygon faces and a UV index on every native model corner."""
    faces = 0
    for line in Path(path).read_text(encoding='utf-8-sig').splitlines():
        words = line.strip().split()
        if not words or words[0] != 'f':
            continue
        if len(words) < 4:
            raise ValueError('OBJ faces require at least three corners')
        faces += 1
        for corner in words[1:]:
            indices = corner.split('/')
            if len(indices) < 2 or not indices[1]:
                raise ValueError('Textured weapon OBJ files require UVs on every face corner')
    if not faces:
        raise ValueError('Model has no polygon faces')


def asset_fixup(record, field, asset_type=None):
    """Return one named asset relocation from a prepared native record."""
    for _, fixup in walk(record):
        if (fixup.get('kind') == 'asset' and fixup.get('field') == field and
                (asset_type is None or fixup.get('asset_type') == asset_type)):
            return fixup
    return None


def replace_asset_references(record, asset_type, source, target):
    """Replace an asset name throughout one prepared record graph."""
    changed = 0
    for _, fixup in walk(record):
        if (fixup.get('kind') == 'asset' and fixup.get('asset_type') == asset_type and
                fixup.get('name') == source):
            fixup['name'] = target
            changed += 1
    return changed


def restore_animation_binding(project, clip):
    """Restore the native animation occupying a clip's previous package slot."""
    binding = clip.get('binding')
    if not binding:
        return
    package = next((asset for asset in project.get('owned_assets', [])
                    if asset.get('pool') == 77 and asset.get('name') == binding.get('package')), None)
    if not package:
        return
    fixup = asset_fixup(package['root'], binding.get('field'), 7)
    if fixup and fixup.get('name') == clip.get('asset') and binding.get('previous'):
        fixup['name'] = binding['previous']


def stamp():
    return datetime.now(timezone.utc).isoformat()


class Workbench:
    def __init__(self, config):
        self.config = config
        self.library = Path(config['library'])
        self.workspace = Path(config['workspace'])
        self.vendor = Path(config['vendor'])
        self.workspace.mkdir(parents=True, exist_ok=True)
        self.catalog = read(self.library/'catalog.json')
        self.tables = read(self.library/'tables.json') if (self.library/'tables.json').exists() else {}
        self.layout = read(self.library/'weapon_layout.json')
        self.animations = AnimationLibrary(config.get('animation_export',LOCAL/'native-animation-export'/'mw19replay'/'common_mp'))
        self.stock = StockLibrary(config,self.library,Path(config.get('stock_cache',LOCAL/'stock-library')))
        self.types = self.layout['types']
        self.lock = threading.RLock()
        self.build_lock = threading.Lock()
        self.history, self.future, self.jobs = {}, {}, {}

    def project_path(self, pid):
        if not re.fullmatch(r'[a-f0-9]{12}', pid):
            raise ValueError('Invalid project id')
        return self.workspace/pid

    def load(self, pid):
        p = read(self.project_path(pid)/'project.json')
        p.setdefault('sound_sources', [])
        if p.get('model') and 'model_parts' not in p:
            group, parts = 'default', []
            for line in safe(self.project_path(pid),p['model']).read_text(encoding='utf-8-sig').splitlines():
                words = line.strip().split(maxsplit=1)
                if words and words[0] in ('g','o'):
                    group = words[1] if len(words) > 1 else 'default'
                elif words and words[0] == 'f' and group not in parts:
                    parts.append(group)
            p['model_parts'] = parts
        return p

    def save(self, project):
        project['updated'] = stamp()
        atomic(self.project_path(project['id'])/'project.json', project)

    def animation_preview(self, project, asset=None):
        packages = {a['name']:a for a in project.get('owned_assets',[]) if a['pool']==77}
        refs = {f['name'] for graph in [project['reference']['root'],*[a['root'] for a in project.get('owned_assets',[])]]
                for _,f in walk(graph) if f.get('kind')=='asset' and f.get('asset_type')==77}
        for descriptor in self.catalog['packages']:
            if descriptor['pool']==77 and descriptor['name'] in refs and descriptor['name'] not in packages:
                packages[descriptor['name']] = read(self.library/descriptor['file'])
        imported = {clip['asset']:clip for clip in project.get('source_clips',[])}
        result = []
        for name, package in packages.items():
            events = [{'field':f['field'],'asset':f['name'],
                       'available':f['name'] in imported or f['name'] in self.animations.files}
                      for _,f in walk(package['root']) if f.get('kind')=='asset' and f.get('asset_type')==7]
            result.append({'name':name,'source':package.get('source',name),
                           'owned':'source' in package,'events':events})
        if asset is not None:
            if asset not in {event['asset'] for p in result for event in p['events']}:
                raise ValueError('Animation is not referenced by this project')
            return imported[asset] if asset in imported else self.animations.clip(asset)
        return {'packages':result}

    def list_projects(self):
        result = []
        for f in self.workspace.glob('*/project.json'):
            p = read(f)
            result.append({k: p[k] for k in ('id','title','base','reference_name','category','updated','revision')})
        return sorted(result, key=lambda p: p['updated'], reverse=True)

    def create(self, name, title, stock=False):
        descriptor = next((w for w in self.catalog['weapons'] if w['name'] == name), None)
        if not descriptor:
            raise ValueError('Unknown reference weapon')
        pid = uuid.uuid4().hex[:12]
        slug = re.sub(r'[^a-z0-9]+', '_', title.lower()).strip('_')[:32] or 'weapon'
        slots = [p.get('loadout_slot',61) for p in (self.load(x['id']) for x in self.list_projects())]
        slot = next((x for x in range(61,255) if x not in slots), None)
        if slot is None:
            raise ValueError('All custom weapon ordinals are allocated')
        p = {'format': 'replay-weapon-project-v1', 'id': pid, 'title': title[:120],
             'base': 'iw8_cw_'+slug, 'description': 'Custom weapon for Replay.',
             'reference_name': name, 'category': descriptor['category'], 'loadout_slot': slot,
             'reference': read(self.library/descriptor['file']), 'revision': 0,
             'created': stamp(), 'updated': stamp(), 'model': None, 'rig': None,
             'material': {'color':'#ffffff', 'metalness': 0.55, 'specular':0.22, 'roughness': 0.45},
             'owned_assets': [], 'files': [], 'sound_sources': [], 'animation_preview': {}, 'notes': ''}
        p['attachment_slots'] = attachment_slots(p['reference']['root'])
        if stock:
            source,assembly=self.stock.load(descriptor,self.tables)
            dest=self.project_path(pid)/'assets/stock';dest.mkdir(parents=True,exist_ok=True)
            p['rig']={'format':'replay-weapon-rig-v1'};p['stock_views']={}
            for view,data in assembly.items():
                p['rig'][view]=data['rig']
                for extension in ['obj','glb']:
                    filename=view+'.'+extension;shutil.copy2(source/filename,dest/filename)
                    p['files'].append({'path':'assets/stock/'+filename,'name':filename,'bytes':(dest/filename).stat().st_size})
                p['stock_views'][view]={'model':'assets/stock/'+view+'.obj','source':'assets/stock/'+view+'.glb'}
            p['model']=p['stock_views']['view_model']['model'];p['model_source']=p['stock_views']['view_model']['source']
            p['model_parts']=assembly['view_model']['parts'];p['stock_reference']=name
            p['description']='Stock Replay model, native tags, skin weights, and weapon data.'
            p['stock_components']={view:data['components'] for view,data in assembly.items()}
            p['stock_warnings']=list(dict.fromkeys(warning for data in assembly.values() for warning in data['warnings']))
        with self.lock:
            allocated={self.load(item['id'])['loadout_slot'] for item in self.list_projects()}
            if p['loadout_slot'] in allocated:
                p['loadout_slot']=next((value for value in range(61,255) if value not in allocated),None)
                if p['loadout_slot'] is None:raise ValueError('All custom weapon ordinals are allocated')
            self.save(p)
        return p

    def start_stock_project(self, name, title):
        if not any(w['name']==name for w in self.catalog['weapons']):
            raise ValueError('Unknown reference weapon')
        jid=uuid.uuid4().hex[:12]
        job={'id':jid,'kind':'stock','status':'queued','log':'Loading native models and attachment parts.', 'started':stamp()}
        self.jobs[jid]=job
        def load_stock():
            try:
                job['status']='loading'
                project=self.create(name,title,stock=True)
                job.update(status='succeeded',project=project['id'],log='Stock weapon loaded.')
            except Exception as error:
                job.update(status='failed',log=str(error))
            job['finished']=stamp()
        threading.Thread(target=load_stock,daemon=True).start()
        return job

    def record(self, p, key):
        if key in ('weapon','sfx'):
            value = p['reference']['root' if key == 'weapon' else 'sfx']
            if not value:
                raise ValueError('This reference has no sound package')
            return value
        asset = next((a for a in p['owned_assets'] if a['name'] == key), None)
        if asset:
            return asset['root']
        raise ValueError('Unknown project asset')

    def mutate(self, pid, action):
        with self.lock:
            p = self.load(pid)
            if action.get('revision', p['revision']) != p['revision']:
                raise ValueError('Project changed in another tab. Reload before saving.')
            op = action['op']
            if op in ('undo', 'redo'):
                source, target = (self.history,self.future) if op == 'undo' else (self.future,self.history)
                if not source.get(pid):
                    raise ValueError('Nothing to '+op)
                target.setdefault(pid, []).append(p)
                old = source[pid].pop()
                old['revision'] = p['revision']+1
                self.save(old)
                return old
            old = copy.deepcopy(p)
            if op == 'field':
                set_value(self.record(p,action.get('asset','weapon')), action['field'], action['value'], self.types)
            elif op == 'metadata':
                for k in ('title','base','description','notes','loadout_slot','loadout_reference'):
                    if k in action:
                        p[k] = action[k]
                if not re.fullmatch(r'iw8_cw_[a-z0-9_]{1,41}',p['base']) or p['base'].endswith('_mp'):
                    raise ValueError('Base name must be iw8_cw_<name>, without _mp')
                if not isinstance(p['loadout_slot'],int) or not 61 <= p['loadout_slot'] <= 254:
                    raise ValueError('Loadout ordinal must be 61..254')
                if p.get('loadout_reference') and not any(w['name']==p['loadout_reference'] and w['selectable'] for w in self.catalog['weapons']):
                    raise ValueError('Choose a selectable native weapon for the loadout reference')
            elif op == 'attachments':
                # Some stock references retain internal/default attachments
                # that the structured catalog exporter cannot describe. Keep
                # those existing native names valid when the author edits a
                # different slot, while still rejecting newly introduced
                # names outside the catalog and this project.
                allowed = ({a['name'] for a in self.catalog['attachments']} |
                           {a['name'] for a in p['owned_assets'] if a['pool']==42} |
                           {name for group in p['attachment_slots'] for name in group})
                if any(n not in allowed for group in action['slots'] for n in group):
                    raise ValueError('Unknown attachment')
                set_attachment_slots(p['reference']['root'], action['slots'])
                p['attachment_slots'] = action['slots']
            elif op == 'clone_asset':
                desc = next((a for a in self.catalog['attachments']+self.catalog['packages'] if a['name']==action['source'] and a.get('pool',42)==action.get('pool',a.get('pool',42))),None)
                if not desc:
                    raise ValueError('Unknown reference asset')
                name = action['name']
                if not re.fullmatch(r'[a-z0-9_/]{1,120}',name) or '..' in name:
                    raise ValueError('Invalid asset name')
                if any(a['name']==name for a in p['owned_assets']):
                    raise ValueError('Asset name already exists in this project')
                source = read(self.library/desc['file'])
                pool = desc.get('pool',42)
                root = source['root']
                identity = next(f for f in root['fixups'] if f['offset']==0 and f['kind']=='string')
                identity['text'] = name
                for fixup in root['fixups']:
                    if fixup['kind']=='script' and fixup.get('text')==action['source']:
                        fixup['text']=name
                p['owned_assets'].append({'pool':pool,'name':name,'source':action['source'],'root':root,
                                           'lifetime':'global' if pool==42 else 'common'})
            elif op == 'asset_ui':
                asset = next((a for a in p['owned_assets'] if a['name']==action['asset'] and a['pool']==42),None)
                if not asset:
                    raise ValueError('Unknown owned attachment')
                token, title = action.get('token',''), action.get('title','')
                tokens = set(self.tables['mp/attachmentmap.csv'][0][1:]) | {r[5] for r in self.tables['mp/attachmenttable.csv'] if r[5]}
                if token and token not in tokens:
                    raise ValueError('Choose a native attachment token')
                if not isinstance(title,str) or len(title)>120 or '\0' in title:
                    raise ValueError('Attachment title must contain at most 120 characters')
                asset['ui']={'token':token,'title':title}
            elif op == 'attachment_model':
                asset = next((a for a in p['owned_assets'] if a['name']==action['asset'] and a['pool']==42),None)
                if not asset:
                    raise ValueError('Unknown owned attachment')
                path = safe(self.project_path(pid), action['path'])
                if path.suffix.lower() != '.obj' or not path.is_file():
                    raise ValueError('Compiled attachment model must be a project OBJ')
                validate_obj_faces(path)
                rig = action['rig']
                if rig.get('format') != 'replay-weapon-rig-v1':
                    raise ValueError('Expected a Replay weapon rig for the attachment')
                for view in ('view_model','world_model'):
                    rebuild_bind_pose(rig[view])
                identity = [[1,0,0,0],[0,1,0,0],[0,0,1,0]]
                asset['geometry']={'model':action['path'],'source':action.get('source'),
                    'rig':rig,'view_model':{'bone':'tag_weapon','matrix':copy.deepcopy(identity)},
                    'world_model':{'bone':'tag_weapon','matrix':copy.deepcopy(identity)}}
                if not p['material'].get('definition'):
                    p['material']['definition'] = generate_material(self.project_path(pid),p['material'],
                        self.library/'material/material.json')
            elif op == 'attachment_transform':
                asset = next((a for a in p['owned_assets'] if a['name']==action['asset'] and a['pool']==42),None)
                view, matrix = action.get('view','view_model'), action['matrix']
                if not asset or not asset.get('geometry') or view not in ('view_model','world_model'):
                    raise ValueError('Unknown custom attachment geometry')
                if len(matrix)!=3 or any(len(row)!=4 for row in matrix):
                    raise ValueError('Expected a 3 by 4 attachment transform')
                asset['geometry'][view]['matrix']=matrix
            elif op == 'attachment_bone':
                asset = next((a for a in p['owned_assets'] if a['name']==action['asset'] and a['pool']==42),None)
                view, bone = action.get('view','view_model'), str(action.get('bone',''))
                if not asset or not asset.get('geometry') or view not in ('view_model','world_model'):
                    raise ValueError('Unknown custom attachment geometry')
                if p.get('rig') and bone not in p['rig'][view]['bones']:
                    raise ValueError('Attachment placement bone is absent from the weapon rig')
                asset['geometry'][view]['bone']=bone
            elif op == 'bone':
                rig = p['rig'][action.get('view','view_model')]
                set_bone_world(rig,action['bone'],action['translation'],action['quaternion'])
            elif op == 'hierarchy':
                rig = p['rig'][action.get('view','view_model')]
                edit_hierarchy(rig,action['operation'],action['bone'],action.get('parent'),action.get('name'))
            elif op == 'part_bone':
                rig = p['rig'][action.get('view','view_model')]
                if action['part'] not in p.get('model_parts',[]):
                    raise ValueError('Unknown mesh part')
                if action['bone']:
                    rig.setdefault('part_bones',{})[action['part']] = action['bone']
                else:
                    rig.setdefault('part_bones',{}).pop(action['part'],None)
                rebuild_bind_pose(rig)
            elif op == 'rig':
                rig = action['rig']
                if rig.get('format') != 'replay-weapon-rig-v1':
                    raise ValueError('Expected a Replay weapon rig')
                for view in ('view_model','world_model'):
                    rebuild_bind_pose(rig[view])
                p['rig'] = rig
            elif op == 'model':
                path = safe(self.project_path(pid),action['path'])
                if not path.is_file() or path.suffix.lower() != '.obj':
                    raise ValueError('Compiled model must be a project OBJ')
                validate_obj_faces(path)
                p['model'] = action['path']
                parts, group = [], 'default'
                for line in path.read_text(encoding='utf-8-sig').splitlines():
                    command = line.strip().split(maxsplit=1)
                    if not command:
                        continue
                    if command[0] in ('g','o'):
                        group = command[1] if len(command) == 2 else 'default'
                    elif command[0] == 'f' and group not in parts:
                        parts.append(group)
                if not parts:
                    raise ValueError('Model has no polygon faces')
                p['model_parts'] = parts
                if action.get('source'):
                    source = safe(self.project_path(pid),action['source'])
                    if not source.is_file():
                        raise ValueError('Original model source is missing')
                    p['model_source'] = action['source']
                else:
                    p.pop('model_source',None)
                p.pop('stock_views',None);p.pop('stock_reference',None);p.pop('stock_components',None);p.pop('stock_warnings',None)
                # A replacement import owns a fresh clip list. Restore any
                # package slots previously claimed by the old import before
                # those binding records disappear.
                for old_clip in p.get('source_clips', []):
                    restore_animation_binding(p, old_clip)
                clips = copy.deepcopy(action.get('clips',[]))
                if not isinstance(clips,list) or len(clips)>128:
                    raise ValueError('A model can import at most 128 animation clips')
                used = set()
                for index,clip in enumerate(clips):
                    if clip.get('format') != 'replay-animation-source-v1':
                        raise ValueError('Imported animation has an unsupported format')
                    slug = re.sub(r'[^a-z0-9]+','_',str(clip.get('name','clip')).lower()).strip('_')[:48] or f'clip_{index+1}'
                    candidate = f"{p['base']}/anim/{slug}"
                    suffix = 2
                    while candidate in used:
                        candidate = f"{p['base']}/anim/{slug}_{suffix}"
                        suffix += 1
                    used.add(candidate)
                    clip.update(asset=candidate,loop=False,asset_type=6,ik_type=1,finger_pose_type=1)
                p['source_clips'] = clips
                if action.get('rig'):
                    for view in ('view_model','world_model'):
                        rebuild_bind_pose(action['rig'][view])
                    p['rig'] = action['rig']
                if not p['material'].get('definition'):
                    p['material']['definition'] = generate_material(self.project_path(pid),p['material'],
                        self.library/'material/material.json')
            elif op == 'animation':
                index = action.get('index')
                if not isinstance(index,int) or index<0 or index>=len(p.get('source_clips',[])):
                    raise ValueError('Unknown imported animation')
                clip = p['source_clips'][index]
                asset = action.get('asset',clip.get('asset',''))
                if not re.fullmatch(r'[a-z0-9_/]{1,120}',asset) or '..' in asset:
                    raise ValueError('Animation asset names use lowercase letters, numbers, slash and underscore')
                if any(i!=index and other.get('asset')==asset for i,other in enumerate(p['source_clips'])):
                    raise ValueError('Animation asset name is already used in this project')
                notes = action.get('notetracks',clip.get('notetracks',[]))
                if not isinstance(notes,list) or len(notes)>255:
                    raise ValueError('An animation can contain at most 255 notetracks')
                for note in notes:
                    if (not isinstance(note,dict) or not re.fullmatch(r'[A-Za-z0-9_./:-]{1,63}',str(note.get('name',''))) or
                            not isinstance(note.get('time'),(int,float)) or not 0<=note['time']<=clip['duration']):
                        raise ValueError('Notetracks require a valid name and a time inside the clip')
                package_name = str(action.get('package', clip.get('binding',{}).get('package','')))
                package_field = str(action.get('field', clip.get('binding',{}).get('field','')))
                if bool(package_name) != bool(package_field):
                    raise ValueError('Choose both an animation package and one of its event slots')
                previous_binding = clip.get('binding')
                binding_changed = bool(previous_binding) and (
                    previous_binding.get('package') != package_name or
                    previous_binding.get('field') != package_field)
                if binding_changed or (previous_binding and not package_name):
                    restore_animation_binding(p, clip)
                next_binding = None
                if package_name:
                    package = next((owned for owned in p.get('owned_assets', [])
                                    if owned.get('pool') == 77 and owned.get('name') == package_name), None)
                    if not package:
                        raise ValueError('Choose an owned animation package')
                    target = asset_fixup(package['root'], package_field, 7)
                    if not target:
                        raise ValueError('Choose an XAnim event slot from the selected package')
                    if any(other_index != index and other.get('binding',{}).get('package') == package_name and
                           other.get('binding',{}).get('field') == package_field
                           for other_index, other in enumerate(p['source_clips'])):
                        raise ValueError('Another imported animation already owns this package event slot')
                    if previous_binding and not binding_changed:
                        previous = previous_binding.get('previous')
                    else:
                        previous = target.get('name')
                    if not previous:
                        raise ValueError('The selected package event does not have a native fallback animation')
                    target['name'] = asset
                    next_binding = {'package':package_name,'field':package_field,'previous':previous}
                    if action.get('link_package', True):
                        graphs = [p['reference']['root']] + [owned['root'] for owned in p.get('owned_assets', [])
                                                                if owned is not package]
                        linked = any(fixup.get('kind') == 'asset' and fixup.get('asset_type') == 77 and
                                     fixup.get('name') == package_name
                                     for graph in graphs for _, fixup in walk(graph))
                        if not linked:
                            source = package.get('source','')
                            changed = sum(replace_asset_references(graph, 77, source, package_name)
                                          for graph in graphs) if source else 0
                            if not changed:
                                raise ValueError('The owned animation package is not reachable from this weapon; link it in Weapon data first')
                clip.update(asset=asset,loop=bool(action.get('loop',clip.get('loop',False))),notetracks=notes)
                if next_binding:
                    clip['binding'] = next_binding
                else:
                    clip.pop('binding', None)
                for key in ('asset_type','ik_type','finger_pose_type'):
                    value = action.get(key,clip.get(key,0))
                    if not isinstance(value,int) or not 0<=value<=255:
                        raise ValueError('Animation native classifications must be byte values')
                    clip[key] = value
            elif op == 'sound':
                operation = action.get('operation', 'add')
                sounds = p.setdefault('sound_sources', [])
                if operation == 'remove':
                    before = len(sounds)
                    sounds[:] = [item for item in sounds if item.get('id') != action.get('id')]
                    if len(sounds) == before:
                        raise ValueError('Unknown native sound alias')
                else:
                    alias = str(action.get('alias', '')).strip().lower()
                    path = str(action.get('path', ''))
                    event = str(action.get('event', ''))
                    preset = str(action.get('preset', 'weapon_player'))
                    if not re.fullmatch(r'[a-z0-9_/]{1,120}', alias) or '..' in alias:
                        raise ValueError('Sound aliases use lowercase letters, numbers, slash and underscore')
                    source = safe(self.project_path(pid), path)
                    if source.suffix.lower() != '.wav' or not source.is_file():
                        raise ValueError('Native sound aliases require an uploaded PCM WAV file')
                    fields = {f['field'] for f in flatten(p['reference']['sfx'], self.types)
                              if f.get('kind') == 'string'} if p['reference'].get('sfx') else set()
                    if event and event not in fields:
                        raise ValueError('Choose a string event from the weapon sound package')
                    if preset not in ('weapon_player', 'weapon_world', 'mechanical', 'ui'):
                        raise ValueError('Unknown sound preset')
                    try:
                        volume = float(action.get('volume', 1.0))
                        pitch = float(action.get('pitch', 1.0))
                        distance = float(action.get('distance', 25000.0))
                    except (TypeError, ValueError) as error:
                        raise ValueError('Sound volume, pitch and distance must be numeric') from error
                    if not 0 <= volume <= 4 or not .25 <= pitch <= 4 or not 0 <= distance <= 100000:
                        raise ValueError('Sound volume, pitch or distance is outside its supported range')
                    item = {'id': str(action.get('id') or uuid.uuid4().hex[:12]), 'alias': alias,
                            'path': path, 'event': event, 'preset': preset, 'volume': volume,
                            'pitch': pitch, 'distance': distance, 'looping': bool(action.get('looping', False))}
                    duplicate = next((other for other in sounds if other.get('alias') == alias and
                                      other.get('id') != item['id']), None)
                    if duplicate:
                        raise ValueError('A native sound alias already uses that name')
                    if operation == 'edit':
                        index = next((i for i, other in enumerate(sounds) if other.get('id') == item['id']), None)
                        if index is None:
                            raise ValueError('Unknown native sound alias')
                        sounds[index] = item
                    else:
                        sounds.append(item)
            elif op == 'transform':
                matrix = action['matrix']
                if len(matrix)!=3 or any(len(row)!=4 for row in matrix):
                    raise ValueError('Expected a 3 by 4 transform')
                p['rig'][action.get('view','view_model')]['transform'] = matrix
                rebuild_bind_pose(p['rig'][action.get('view','view_model')])
            elif op == 'material':
                update = action.get('material')
                allowed = {'color', 'metalness', 'specular', 'roughness',
                           'color_texture', 'normal_texture', 'emissive_texture'}
                if not isinstance(update, dict) or set(update) - allowed:
                    raise ValueError('Unknown material setting')
                if 'color' in update and not re.fullmatch(r'#[0-9a-fA-F]{6}', str(update['color'])):
                    raise ValueError('Material color must be a six-digit hex color')
                for key in ('metalness', 'specular', 'roughness'):
                    if key not in update:
                        continue
                    value = float(update[key])
                    if not math.isfinite(value) or not 0 <= value <= 1:
                        raise ValueError(f'Material {key} must be between 0 and 1')
                    update[key] = value
                for key in ('color_texture', 'normal_texture', 'emissive_texture'):
                    if key in update:
                        source = safe(self.project_path(pid), str(update[key]))
                        if source.suffix.lower() not in ('.png', '.jpg', '.jpeg', '.webp', '.tga') or not source.is_file():
                            raise ValueError('Material textures require an uploaded image')
                p['material'].update(update)
                p['material']['definition'] = generate_material(self.project_path(pid),p['material'],
                    self.library/'material/material.json')
            else:
                raise ValueError('Unknown project operation')
            p['revision'] += 1
            self.save(p)
            self.history.setdefault(pid,[]).append(old)
            self.history[pid] = self.history[pid][-30:]
            self.future[pid] = []
            return p

    def upload(self, pid, name, data, chunk=None):
        name = Path(name.replace('\\','/')).name
        if len(name)>160 or not re.fullmatch(r'[a-zA-Z0-9_. -]+',name):
            raise ValueError('Invalid file name')
        if Path(name).suffix.lower() not in SOURCE_EXTENSIONS:
            raise ValueError('Unsupported source asset extension')
        content = base64.b64decode(data,validate=True)
        limit=MAX_OBJ_BYTES if Path(name).suffix.lower()=='.obj' else MAX_SOURCE_BYTES
        if len(content)>limit:
            raise ValueError(f'Source file exceeds {limit//1024//1024} MiB')
        if chunk is not None:
            total,offset=chunk.get('total'),chunk.get('offset')
            upload_id=chunk.get('id')
            if (type(total) is not int or type(offset) is not int or not 0<total<=limit or
                    not 0<=offset<total or not 0<len(content)<=4*1024*1024 or offset+len(content)>total):
                raise ValueError('Invalid upload chunk')
            if upload_id is None:
                if offset!=0:raise ValueError('Upload must start at offset zero')
                upload_id=uuid.uuid4().hex
            elif not isinstance(upload_id,str) or not re.fullmatch('[a-f0-9]{32}',upload_id):
                raise ValueError('Invalid upload ID')
            with self.lock:
                self.load(pid)
                pending=self.project_path(pid)/'.uploads'/(upload_id+'-'+name+'.part')
                pending.parent.mkdir(parents=True,exist_ok=True)
                mode='xb' if chunk.get('id') is None else 'r+b'
                with pending.open(mode) as output:
                    output.seek(0,2)
                    if output.tell()!=offset:raise ValueError('Upload offset does not match the received file')
                    output.write(content)
                if offset+len(content)<total:
                    return {'id':upload_id,'offset':offset+len(content)}
                relative='assets/'+upload_id[:8]+'-'+name
                dest=safe(self.project_path(pid),relative);dest.parent.mkdir(parents=True,exist_ok=True)
                pending.replace(dest)
                p=self.load(pid);p['files'].append({'path':relative,'name':name,'bytes':total})
                p['revision']+=1;self.save(p)
                return {'path':relative,'project':p}
        relative = 'assets/'+uuid.uuid4().hex[:8]+'-'+name
        dest = safe(self.project_path(pid),relative)
        dest.parent.mkdir(parents=True,exist_ok=True)
        dest.write_bytes(content)
        with self.lock:
            p = self.load(pid)
            p['files'].append({'path':relative,'name':name,'bytes':len(content)})
            p['revision'] += 1
            self.save(p)
        return {'path':relative,'project':p}

    def validate(self,p):
        errors, warnings = [], []
        if p.get('stock_reference'):
            warnings.append('Stock assemblies are inspection previews. Builds reuse the native model and attachment assets; preview rig and material edits do not replace them. Import a custom model to compile replacement geometry.')
        if p['model'] and not p['rig']:
            errors.append('Custom geometry requires a rig with native bone names.')
        if not p['model']:
            if any(asset.get('geometry') for asset in p.get('owned_assets', [])):
                warnings.append('The main weapon reuses its reference models; custom attachment models compile independently.')
            else:
                warnings.append('This project reuses the reference models. Import geometry to change their appearance.')
        if p['rig']:
            try:
                for key in ('view_model','world_model'):
                    rebuild_bind_pose(p['rig'][key])
            except (ValueError,KeyError,TypeError) as e:
                errors.append(str(e))
        for asset in p.get('owned_assets', []):
            if asset.get('pool') != 42 or not asset.get('geometry'):
                continue
            try:
                geometry = asset['geometry']
                path = safe(self.project_path(p['id']), geometry['model'])
                if path.suffix.lower() != '.obj' or not path.is_file():
                    raise ValueError('An owned attachment model is missing')
                validate_obj_faces(path)
                for view in ('view_model','world_model'):
                    rebuild_bind_pose(geometry['rig'][view])
                    matrix = geometry[view]['matrix']
                    if len(matrix)!=3 or any(len(row)!=4 for row in matrix):
                        raise ValueError('An attachment placement transform is invalid')
            except (ValueError,KeyError,TypeError) as e:
                errors.append(str(e))
        animation_names = set()
        rig_bones = set((p.get('rig') or {}).get('view_model',{}).get('bones',[]))
        for clip in p.get('source_clips',[]):
            try:
                if clip.get('format')!='replay-animation-source-v1' or not clip.get('tracks'):
                    raise ValueError('Imported animation is empty or has an unsupported format')
                if clip.get('asset') in animation_names:
                    raise ValueError('Two imported animations use the same native asset name')
                animation_names.add(clip.get('asset'))
                missing = sorted({track['bone'] for track in clip['tracks']} - rig_bones)
                if missing:
                    raise ValueError('Animation bones are absent from the view rig: '+', '.join(missing[:8]))
                binding = clip.get('binding')
                if binding:
                    package = next((asset for asset in p.get('owned_assets', [])
                                    if asset.get('pool') == 77 and asset.get('name') == binding.get('package')), None)
                    if not package:
                        raise ValueError('Animation binding references a missing owned package')
                    target = asset_fixup(package['root'], binding.get('field'), 7)
                    if not target or target.get('name') != clip.get('asset'):
                        raise ValueError('Animation package event no longer points to its imported XAnim')
                    graphs = [p['reference']['root']] + [asset['root'] for asset in p.get('owned_assets', [])
                                                            if asset is not package]
                    if not any(fixup.get('kind') == 'asset' and fixup.get('asset_type') == 77 and
                               fixup.get('name') == package['name']
                               for graph in graphs for _, fixup in walk(graph)):
                        raise ValueError('Animation package is not linked into the weapon graph')
                else:
                    warnings.append(f"Animation {clip.get('name','clip')} is preview-only until it is mapped to an owned package event.")
            except (ValueError,KeyError,TypeError) as e:
                errors.append(str(e))
        aliases = set()
        for source in p.get('sound_sources', []):
            try:
                if source.get('alias') in aliases:
                    raise ValueError('Two native sounds use the same alias')
                aliases.add(source.get('alias'))
                path = safe(self.project_path(p['id']), source['path'])
                if path.suffix.lower() != '.wav' or not path.is_file():
                    raise ValueError('A native sound source WAV is missing')
            except (ValueError, KeyError, TypeError) as e:
                errors.append(str(e))
        if self.tables:
            try:
                registration(p,self.tables)
            except (ValueError,KeyError,TypeError) as e:
                errors.append(str(e))
        for other in self.list_projects():
            if other['id'] != p['id']:
                q = self.load(other['id'])
                if q['loadout_slot']==p['loadout_slot']:
                    errors.append('Loadout ordinal is already used by '+q['title'])
                if q['base']==p['base']:
                    errors.append('Base name is already used by '+q['title'])
        return {'errors':errors,'warnings':warnings,'ready':not errors}

    def start_build(self,pid):
        p = self.load(pid)
        validation = self.validate(p)
        if validation['errors']:
            raise ValueError('; '.join(validation['errors']))
        jid = uuid.uuid4().hex[:12]
        job = {'id':jid,'project':pid,'revision':p['revision'],'status':'queued','log':'','started':stamp()}
        self.jobs[jid] = job
        threading.Thread(target=self.build,args=(job,p),daemon=True).start()
        return job

    def build(self,job,p):
        with self.build_lock:
            try:
                job['status']='building'
                folder = self.project_path(p['id'])/'builds'/job['id']
                folder.mkdir(parents=True)
                atomic(folder/'project.snapshot.json',p)
                reference = copy.deepcopy(p['reference'])
                set_attachment_slots(reference['root'],p['attachment_slots'])
                atomic(folder/'reference.json',reference)
                owned_assets = copy.deepcopy(p['owned_assets'])
                for asset in owned_assets:
                    if asset.get('geometry'):
                        asset['geometry']['model'] = str(safe(self.project_path(p['id']), asset['geometry']['model']))
                manifest = {'format':'replay-weapon-build-v1','reference':str(folder/'reference.json'),
                    'name':p['base'],'display_name':p['title'],'description':p['description'],
                    'loadout_slot':p['loadout_slot'],'attachments':any(p['attachment_slots']),
                    'category':p['category'],'owned_assets':owned_assets,
                    'animations':p.get('source_clips',[])}
                if self.tables:
                    manifest.update(registration(p,self.tables))
                root = self.project_path(p['id'])
                if p.get('sound_sources'):
                    bank_path = folder/'custom.sabl'
                    manifest['sounds'] = generate_sound_bank(root, p['sound_sources'], bank_path)
                    manifest['sound_bank'] = str(bank_path)
                if p['model'] and not p.get('stock_reference'):
                    atomic(folder/'rig.json',p['rig'])
                    manifest.update(model=str(safe(root,p['model'])),rig=str(folder/'rig.json'))
                if p['material'].get('definition') and (p['model'] and not p.get('stock_reference') or any(a.get('geometry') for a in owned_assets)):
                    manifest['material']=str(safe(root,p['material']['definition']))
                atomic(folder/'build.json',manifest)
                exe = Path(self.config['compiler'])
                if not exe.is_file():
                    raise ValueError('ZoneTool compiler is not configured. Set compiler in config.local.json.')
                command = [str(exe),'build-weapon','--project',str(folder/'build.json'),'-o',str(folder/'output')]
                job['log'] = ' '.join(command)+'\n\n'
                process = subprocess.Popen(command,cwd=exe.parent,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                    text=True,encoding='utf-8',errors='replace',creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                def read_output():
                    for line in process.stdout:
                        job['log'] = (job['log']+line)[-200000:]
                reader = threading.Thread(target=read_output,daemon=True)
                reader.start()
                try:
                    code = process.wait(timeout=600)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    raise RuntimeError('ZoneTool exceeded the ten minute build limit')
                finally:
                    reader.join(timeout=5)
                if code:
                    raise RuntimeError('ZoneTool exited with code '+str(code))
                files = sorted((folder/'output').glob('*'))
                if sum(f.suffix=='.ff' for f in files)!=8:
                    raise RuntimeError('Expected eight native fastfiles including companions')
                with zipfile.ZipFile(folder/'package.zip','w',zipfile.ZIP_DEFLATED) as z:
                    for f in files:
                        z.write(f,f.name)
                job.update(status='succeeded',files=[{'name':f.name,'bytes':f.stat().st_size} for f in files],
                           download=f'/project-files/{p["id"]}/builds/{job["id"]}/package.zip')
            except Exception as e:
                job.update(status='failed',log=job['log']+'\n'+str(e)+'\n')
            job['finished']=stamp()


class Handler(BaseHTTPRequestHandler):
    server_version='ReplayWorkbench/0.1'

    def log_message(self, fmt, *args):
        if args and isinstance(args[0],str):
            request = re.sub(r'/access/[^?\s]+','/access/[redacted]',args[0])
            request = re.sub(r'([?&](?:token|access)=)[^&\s]+',r'\1[redacted]',request)
            args = (request,*args[1:])
        if self.command!='GET' or args and str(args[1])!='200':
            super().log_message(fmt,*args)

    @property
    def app(self):
        return self.server.app

    def send_json(self,status,value):
        data = json.dumps(value,ensure_ascii=False,allow_nan=False).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type','application/json; charset=utf-8')
        self.send_header('Content-Length',str(len(data)))
        self.send_header('Cache-Control','no-store')
        self.send_header('X-Content-Type-Options','nosniff')
        self.send_header('Referrer-Policy','no-referrer')
        self.end_headers()
        self.wfile.write(data)

    def file(self,path):
        if not path.is_file():
            raise FileNotFoundError(path.name)
        content = None
        if path.suffix in ('.js','.html'):
            content=path.read_bytes()
            if path.suffix=='.js':
                # Module workers do not inherit the document import map.
                content=re.sub(rb"(['\"])three\1",rb"\1/vendor/three/build/three.module.js\1",content)
                content=content.replace(b"'three/addons/",b"'/vendor/three/examples/jsm/").replace(b'"three/addons/',b'"/vendor/three/examples/jsm/')
        size=len(content) if content is not None else path.stat().st_size
        start,end,status=0,size-1,200
        if self.headers.get('Range'):
            match=re.fullmatch(r'bytes=(\d+)-(\d*)',self.headers['Range'])
            if match:
                start=int(match[1]);end=min(int(match[2]) if match[2] else size-1,size-1)
            if not match or not 0<=start<=end<size:
                self.send_response(416);self.send_header('Content-Range',f'bytes */{size}');self.end_headers();return
            status=206
        self.send_response(status)
        mime = {'.js':'text/javascript','.obj':'text/plain','.glb':'model/gltf-binary'}.get(path.suffix,
                mimetypes.guess_type(str(path))[0] or 'application/octet-stream')
        self.send_header('Content-Type',mime)
        self.send_header('Content-Length',str(end-start+1))
        self.send_header('Accept-Ranges','bytes')
        if status==206:self.send_header('Content-Range',f'bytes {start}-{end}/{size}')
        self.send_header('X-Content-Type-Options','nosniff')
        self.send_header('Cache-Control','no-store' if path.suffix == '.html' else 'no-cache')
        self.send_header('Referrer-Policy','no-referrer')
        self.end_headers()
        if content is not None:self.wfile.write(content[start:end+1]);return
        with path.open('rb') as source:
            source.seek(start);remaining=end-start+1
            while remaining:
                data=source.read(min(1024*1024,remaining))
                if not data:break
                self.wfile.write(data);remaining-=len(data)

    def dispatch(self):
        expected = f'127.0.0.1:{self.server.server_port}'
        if self.headers.get('Host') not in (expected,f'localhost:{self.server.server_port}'):
            raise ValueError('Invalid local host')
        url = urlparse(self.path)
        path = unquote(url.path)
        parts = path.strip('/').split('/')
        query = parse_qs(url.query)
        if self.command=='GET':
            if path=='/api/bootstrap':
                return {'catalog':self.app.catalog,'projects':self.app.list_projects(),
                        'layout_summary':self.app.layout['summary'],'slots':SLOTS,'version':'0.1.0',
                        'protected':False,
                        'attachment_tokens':sorted(set(self.app.tables.get('mp/attachmentmap.csv',[[]])[0][1:]) |
                                                   {r[5] for r in self.app.tables.get('mp/attachmenttable.csv',[]) if r[5]})}
            if path=='/api/layout':
                return self.app.layout
            if path=='/api/projects':
                return self.app.list_projects()
            if len(parts)>=3 and parts[:2]==['api','projects']:
                p = self.app.load(parts[2])
                if len(parts)==3:
                    return p
                if parts[3]=='fields':
                    return flatten(self.app.record(p,query.get('asset',['weapon'])[0]),self.app.types)
                if parts[3]=='validate':
                    return self.app.validate(p)
                if parts[3]=='animation-preview':
                    return self.app.animation_preview(p,query.get('asset',[None])[0])
            if len(parts)==3 and parts[:2]==['api','jobs']:
                return self.app.jobs[parts[2]]
            if parts[0]=='project-files' and len(parts)>=3:
                self.file(safe(self.app.project_path(parts[1]),'/'.join(parts[2:])))
                return None
            if parts[0]=='library-files' and len(parts)>=2:
                self.file(safe(self.app.library,'/'.join(parts[1:])))
                return None
            if parts[0]=='vendor':
                self.file(safe(self.app.vendor,'/'.join(parts[1:])))
                return None
            self.file(safe(HERE/'web',path.lstrip('/') or 'index.html'))
            return None
        if self.headers.get('X-Replay-Editor')!='1' or self.headers.get('Content-Type')!='application/json':
            raise ValueError('Expected a local editor JSON request')
        origin = self.headers.get('Origin')
        if origin and origin not in (f'http://{expected}',f'http://localhost:{self.server.server_port}'):
            raise ValueError('Cross-origin writes are not allowed')
        length = int(self.headers.get('Content-Length','0'))
        if length<2 or length>60*1024*1024:
            raise ValueError('Invalid request size')
        body = json.loads(self.rfile.read(length))
        if path=='/api/projects':
            if body.get('stock'):
                return self.app.start_stock_project(body['reference'],body['title'])
            return self.app.create(body['reference'],body['title'])
        if len(parts)>=3 and parts[:2]==['api','projects']:
            pid = parts[2]
            if len(parts)==3:
                return self.app.mutate(pid,body)
            if parts[3]=='upload':
                return self.app.upload(pid,body['name'],body['data'],body.get('chunk'))
            if parts[3]=='build':
                return self.app.start_build(pid)
        raise ValueError('Unknown endpoint')

    def handle_request(self):
        try:
            result = self.dispatch()
            if result is not None:
                self.send_json(200,result)
        except (ValueError,KeyError,TypeError,OverflowError) as e:
            self.send_json(400,{'error':str(e)})
        except FileNotFoundError as e:
            self.send_json(404,{'error':str(e)})
        except Exception as e:
            self.send_json(500,{'error':str(e)})

    do_GET=handle_request
    do_POST=handle_request


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',type=int,default=8766)
    parser.add_argument('--config',type=Path,default=HERE/'config.local.json')
    args=parser.parse_args()
    config={'library':str(LOCAL/'library'),'workspace':str(LOCAL/'projects'),
            'vendor':str(LOCAL/'vendor'),
            'compiler':str(REPO/'iw8-zonetool/xmake-out/x64/Release/iw8-zonetool.exe')}
    if not args.config.is_file():
        parser.error('Run setup.py with your own Replay game files first. See docs/CUSTOM_WEAPONS.md.')
    config.update(read(args.config))
    app=Workbench(config)
    server=ThreadingHTTPServer(('127.0.0.1',args.port),Handler)
    server.app=app
    print(f'Replay Weapon Workbench: http://127.0.0.1:{args.port}',flush=True)
    print(f'Projects: {app.workspace}',flush=True)
    server.serve_forever()


if __name__=='__main__':
    main()
