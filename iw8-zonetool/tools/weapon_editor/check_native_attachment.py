"""Build and reload custom weapon and attachment XModels with mapped materials."""
import argparse
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from weapon_models import VIEW_MODEL_FIELDS, WORLD_MODEL_FIELDS, model_slots_from_record


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library',type=Path,required=True)
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--acts',type=Path,required=True)
    parser.add_argument('--game',type=Path,required=True)
    parser.add_argument('--project',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    if args.out.exists() and any(args.out.iterdir()):
        raise SystemExit('Choose an empty output directory for this check')
    args.out.mkdir(parents=True, exist_ok=True)
    project=json.loads(args.project.read_text(encoding='utf-8-sig'))
    catalog=json.loads((args.library/'catalog.json').read_text(encoding='utf-8-sig'))
    reference_name=project.get('reference_name') or 'iw8_pi_mike1911_mp'
    reference=next(item for item in catalog['weapons'] if item['name']==reference_name)
    reference_data=json.loads((args.library/reference['file']).read_text(encoding='utf-8-sig'))
    model_slots=model_slots_from_record(reference_data['root'])
    weapon_rig=copy.deepcopy(project['rig'])
    weapon_rig['view_model']['replace']=model_slots['view_model']
    weapon_rig['world_model']['replace']=model_slots['world_model']
    # This stock attachment is deliberately small but owns one view-model and
    # one world-model dependency, which makes both replacement paths observable.
    attachment_path=args.library/'attachments'/'akimbo_cpapa.json'
    attachment=json.loads(attachment_path.read_text(encoding='utf-8-sig'))
    identity=[[1,0,0,0],[0,1,0,0],[0,0,1,0]]
    attachment_rig=copy.deepcopy(project['rig'])
    attachment_rig['view_model']['replace']=[];attachment_rig['world_model']['replace']=[]
    name=attachment['name']
    source_model=args.project.parent/project['model']
    lines=source_model.read_text(encoding='utf-8-sig').splitlines()
    if not any(line.strip()=='g attachment_surface' for line in lines):
        first_face=next(i for i,line in enumerate(lines) if line.startswith('f '))
        lines.insert(first_face,'g attachment_surface')
    surface_group=max(i for i,line in enumerate(lines) if line.strip()=='g attachment_surface')
    if not any(line.strip()=='usemtl attachment_surface' for line in lines):
        lines.insert(surface_group+1,'usemtl attachment_surface')
    model_text='\n'.join(lines)+'\n'
    attachment_model=args.out/'attachment-material.obj'
    attachment_model.write_text(model_text,encoding='utf-8')
    weapon_rig_path=args.out/'weapon-rig.json'
    weapon_rig_path.write_text(json.dumps(weapon_rig,indent=2),encoding='utf-8')
    owned={'pool':42,'name':name,'source':name,'root':attachment['root'],'lifetime':'global',
           'geometry':{'model':str(attachment_model),'rig':attachment_rig,
                       'surface_materials':[{'key':'attachment_surface',
                                             'definition':str(args.project.parent/project['material']['definition']),
                                             'parts':['attachment_surface']}],
                       'view_model':{'bone':'tag_weapon','matrix':identity},
                       'world_model':{'bone':'tag_weapon','matrix':identity}}}
    manifest={'format':'replay-weapon-build-v1','reference':str(args.library/reference['file']),
              'name':'iw8_cw_attachment_check','display_name':'Attachment check','loadout_slot':61,
              'attachments':False,'category':project.get('category','weapon_pistol'),
              'owned_assets':[owned],'animations':[],
              'model':str(attachment_model),'rig':str(weapon_rig_path),
              'material':str(args.project.parent/project['material']['definition'])}
    build_json=args.out/'build.json';build_json.write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    output=args.out/'output'
    build=subprocess.run([str(args.compiler),'build-weapon','--project',str(build_json),'-o',str(output)],
                         capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
    (args.out/'build.log').write_text(build.stdout+build.stderr,encoding='utf-8')
    if build.returncode:
        raise SystemExit('attachment fastfile build failed with exit code '+str(build.returncode))
    roundtrip=args.out/'roundtrip'
    executable=args.game/'game_dx12_ship_replay.exe'
    load=subprocess.run([str(args.acts),'--noUpdater','fastfile','-r','mw19replay','-g',str(executable),
                         '-a','weapon,attachment,xmodel','-o',str(roundtrip),
                         str(output/'iw8_cw_attachment_check.ff')],cwd=args.acts.parent,
                        capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
    (args.out/'roundtrip.log').write_text(load.stdout+load.stderr,encoding='utf-8')
    if load.returncode:
        raise SystemExit('attachment fastfile reload failed with exit code '+str(load.returncode))
    documents=[json.loads(path.read_text(encoding='utf-8-sig')) for path in roundtrip.rglob('*.asset.json')]
    expected={name+'/custom_vm',name+'/custom_wm'}
    attachment_doc=next((d for d in documents if d['pool']=='attachment'),None)
    if not attachment_doc:
        raise SystemExit('native reload did not recover the owned attachment')
    fields=attachment_doc['asset']['fields']
    view={item['name'] for item in fields['viewModelVariations']['values'] if item}
    world={item['name'] for item in fields['worldModelVariations']['values'] if item}
    if view != {name+'/custom_vm'} or world != {name+'/custom_wm'}:
        raise SystemExit('owned attachment did not reference its custom view/world XModels')
    weapon_doc=next((d for d in documents if d.get('root_type')=='WeaponCompleteDef'),None)
    if not weapon_doc:
        raise SystemExit('native reload did not recover the custom WeaponCompleteDef')
    weapon_definition=weapon_doc['asset']['fields']['weapDef']['values'][0]
    view_models={weapon_definition[field]['name'].lstrip(',') for field in VIEW_MODEL_FIELDS
                 if weapon_definition.get(field)}
    world_models={weapon_definition[field]['name'].lstrip(',') for field in WORLD_MODEL_FIELDS
                  if weapon_definition.get(field)}
    if view_models!={'iw8_cw_attachment_check/vm'} or world_models!={'iw8_cw_attachment_check/wm'}:
        raise SystemExit('WeaponDef retained a stock base, streamed, hand-specific, or censorship model')
    custom_models=[d['asset']['fields'] for d in documents
                   if d.get('asset',{}).get('fields',{}).get('name',{}).get('string') in expected]
    expected_material='iw8_cw_attachment_check/material_a0_attachment_surface'
    if len(custom_models)!=2 or any(
            not any(item.get('name','').lstrip(',')==expected_material
                    for item in model.get('materialHandles',{}).get('values',[]))
            for model in custom_models):
        raise SystemExit('attachment XModels did not retain their per-surface native material')
    result={'format':'replay-native-attachment-check-v1','attachment':name,
            'models':sorted(expected),'replaced_base_slots':{
                'view':len(model_slots['view_model']),'world':len(model_slots['world_model'])},
            'build_exit':build.returncode,'reload_exit':load.returncode,
            'status':'passed'}
    (args.out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))


if __name__=='__main__':
    main()
