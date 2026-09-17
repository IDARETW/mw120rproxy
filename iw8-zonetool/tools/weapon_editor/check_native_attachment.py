"""Build and reload custom view/world XModels owned by a cloned attachment."""
import argparse
import copy
import json
from pathlib import Path
import shutil
import subprocess


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
    reference=next(item for item in catalog['weapons'] if item['name']=='iw8_pi_mike1911_mp')
    # This stock attachment is deliberately small but owns one view-model and
    # one world-model dependency, which makes both replacement paths observable.
    attachment_path=args.library/'attachments'/'akimbo_cpapa.json'
    attachment=json.loads(attachment_path.read_text(encoding='utf-8-sig'))
    identity=[[1,0,0,0],[0,1,0,0],[0,0,1,0]]
    rig=copy.deepcopy(project['rig'])
    rig['view_model']['replace']=[];rig['world_model']['replace']=[]
    name=attachment['name']
    owned={'pool':42,'name':name,'source':name,'root':attachment['root'],'lifetime':'global',
           'geometry':{'model':str(args.project.parent/project['model']),'rig':rig,
                       'view_model':{'bone':'tag_weapon','matrix':identity},
                       'world_model':{'bone':'tag_weapon','matrix':identity}}}
    manifest={'format':'replay-weapon-build-v1','reference':str(args.library/reference['file']),
              'name':'iw8_cw_attachment_check','display_name':'Attachment check','loadout_slot':61,
              'attachments':False,'category':'pistol','owned_assets':[owned],'animations':[],
              'material':str(args.project.parent/project['material']['definition'])}
    build_json=args.out/'build.json';build_json.write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    output=args.out/'output'
    build=subprocess.run([str(args.compiler),'build-weapon','--project',str(build_json),'-o',str(output)],
                         capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
    (args.out/'build.log').write_text(build.stdout+build.stderr,encoding='utf-8')
    if build.returncode:
        raise SystemExit('attachment fastfile build failed with exit code '+str(build.returncode))
    roundtrip=args.out/'roundtrip'
    load=subprocess.run([str(args.acts),'--noUpdater','fastfile','-r','mw19replay','-g',str(args.game),
                         '-a','attachment,xmodel','-o',str(roundtrip),
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
    result={'format':'replay-native-attachment-check-v1','attachment':name,
            'models':sorted(expected),'build_exit':build.returncode,'reload_exit':load.returncode,
            'status':'passed'}
    (args.out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))


if __name__=='__main__':
    main()
