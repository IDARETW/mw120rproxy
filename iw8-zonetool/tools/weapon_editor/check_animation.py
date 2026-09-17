"""Build and reload native Replay XAnim records, including 16-bit frame indices."""
import argparse
import json
import math
from pathlib import Path
import subprocess


def track(bone, frames, fps, phase=0.0):
    translations=[]
    quaternions=[]
    for index in range(frames):
        angle=phase+index/max(frames-1,1)*0.35
        translations.append([index/max(frames-1,1)*2.0, math.sin(angle)*0.25, 0.0])
        quaternions.append([0.0, 0.0, math.sin(angle/2), math.cos(angle/2)])
    return {'bone':bone,'translations':translations,'quaternions':quaternions}


def clip(asset, frames, fps, bones):
    duration=(frames-1)/fps
    return {'format':'replay-animation-source-v1','asset':asset,'name':asset.rsplit('/',1)[-1],
            'fps':fps,'duration':duration,'loop':True,'asset_type':6,'ik_type':1,
            'finger_pose_type':1,
            'tracks':[track(bone,frames,fps,index*0.1) for index,bone in enumerate(bones)],
            'notetracks':[{'name':'check_quarter','time':duration*0.25},
                          {'name':'check_three_quarters','time':duration*0.75}]}


def fields(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))['asset']['fields']


def scalar(value):
    return value['value'] if isinstance(value,dict) and 'value' in value else value


def assert_record(record, frames, bones, long_indices):
    expected_bones=[0,0,bones,0,0,0,bones,0,0,bones]
    assert scalar(record['numframes']) == frames-1
    assert abs(scalar(record['framerate'])-(60.0 if long_indices else 30.0)) < 1e-4
    assert scalar(record['flags']) == 1
    assert record['boneCount'] == expected_bones
    assert scalar(record['notifyCount']) == 2
    assert scalar(record['assetType']) == 6
    assert scalar(record['ikType']) == 1
    assert scalar(record['fingerPoseType']) == 1
    assert scalar(record['dataIntCount']) == bones*6
    assert scalar(record['randomDataShortCount']) == bones*frames*7
    if long_indices:
        assert scalar(record['dataByteCount']) == bones
        assert scalar(record['dataShortCount']) == bones*6
        assert scalar(record['indexCount']) == bones*frames*2
    else:
        assert scalar(record['dataByteCount']) == bones*(frames*2+1)
        assert scalar(record['dataShortCount']) == bones*2
        assert scalar(record['indexCount']) == 0
    notifies=record['notify']['values']
    assert len(notifies) == 2
    assert abs(notifies[0]['time']-0.25) < 1e-5
    assert abs(notifies[1]['time']-0.75) < 1e-5


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library',type=Path,required=True)
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--acts',type=Path,required=True)
    parser.add_argument('--game',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    catalog=json.loads((args.library/'catalog.json').read_text(encoding='utf-8-sig'))
    reference=next(item for item in catalog['weapons'] if item['name']=='iw8_pi_mike1911_mp')
    short_name='iw8_cw_animation_check/anim/short_indices'
    long_name='iw8_cw_animation_check/anim/long_indices'
    manifest={'format':'replay-weapon-build-v1','reference':str(args.library/reference['file']),
              'name':'iw8_cw_animation_check','display_name':'Animation check','loadout_slot':61,
              'attachments':False,'category':reference['category'],'owned_assets':[],
              'animations':[clip(short_name,31,30.0,['tag_weapon','tag_flash']),
                            clip(long_name,257,60.0,['tag_weapon'])]}
    manifest_path=args.out/'build.json'
    manifest_path.write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    output=args.out/'output'
    build=subprocess.run([str(args.compiler),'build-weapon','--project',str(manifest_path),'-o',str(output)],
                         capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
    (args.out/'build.log').write_text(build.stdout+build.stderr,encoding='utf-8')
    if build.returncode:
        raise SystemExit(f'animation build failed with exit code {build.returncode}')
    roundtrip=args.out/'roundtrip'
    load=subprocess.run([str(args.acts),'--noUpdater','fastfile','-r','mw19replay','-g',str(args.game),
                         '-a','xanim','-o',str(roundtrip),str(output/'iw8_cw_animation_check_common.ff')],
                        cwd=args.acts.parent,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
    (args.out/'roundtrip.log').write_text(load.stdout+load.stderr,encoding='utf-8')
    if load.returncode:
        raise SystemExit(f'animation reload failed with exit code {load.returncode}')
    records={}
    for path in roundtrip.rglob('*.asset.json'):
        document=json.loads(path.read_text(encoding='utf-8-sig'))
        name=document['asset']['fields']['name'].get('string')
        if name in (short_name,long_name):
            records[name]=document['asset']['fields']
    if set(records)!={short_name,long_name}:
        raise SystemExit(f'expected both custom animations, found {sorted(records)}')
    assert_record(records[short_name],31,2,False)
    assert_record(records[long_name],257,1,True)
    result={'format':'replay-animation-check-v1','animations':2,
            'short_frames':31,'long_frames':257,'build_exit':build.returncode,
            'reload_exit':load.returncode,'status':'passed'}
    (args.out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))


if __name__=='__main__':
    main()
