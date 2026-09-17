"""Build and reload a resident Replay SndBank, StreamKey and mapped SFX event."""
import argparse
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import wave

from sound import generate


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library',type=Path,required=True)
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--acts',type=Path,required=True)
    parser.add_argument('--game',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    if args.out.exists() and any(args.out.iterdir()):
        raise SystemExit('Choose an empty output directory for this check')
    (args.out/'assets').mkdir(parents=True)
    rate, frames = 22050, 5000
    wav=args.out/'assets'/'fire.wav'
    with wave.open(str(wav),'wb') as output:
        output.setparams((1,2,rate,frames,'NONE','not compressed'))
        output.writeframes(b''.join(struct.pack('<h',round(math.sin(i*math.tau*330/rate)*14000))
                                    for i in range(frames)))
    source={'alias':'iw8_cw_sound_check/fire','path':'assets/fire.wav',
            'event':'sfx.sounds[0].fireSoundPlayer.name','preset':'weapon_player',
            'volume':.8,'pitch':1.0,'distance':25000,'looping':False}
    sounds=generate(args.out,[source],args.out/'custom.sabl')
    catalog=json.loads((args.library/'catalog.json').read_text(encoding='utf-8-sig'))
    reference=next(item for item in catalog['weapons'] if item['name']=='iw8_pi_mike1911_mp')
    manifest={'format':'replay-weapon-build-v1','reference':str(args.library/reference['file']),
              'name':'iw8_cw_sound_check','display_name':'Sound check','loadout_slot':61,
              'attachments':False,'category':'pistol','owned_assets':[],'animations':[],
              'sounds':sounds,'sound_bank':str(args.out/'custom.sabl')}
    build_json=args.out/'build.json';build_json.write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    output=args.out/'output'
    build=subprocess.run([str(args.compiler),'build-weapon','--project',str(build_json),'-o',str(output)],
                         capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
    (args.out/'build.log').write_text(build.stdout+build.stderr,encoding='utf-8')
    if build.returncode:
        raise SystemExit('sound fastfile build failed with exit code '+str(build.returncode))
    roundtrip=args.out/'roundtrip'
    load=subprocess.run([str(args.acts),'--noUpdater','fastfile','-r','mw19replay','-g',str(args.game),
                         '-a','soundbanktransient,streamkey,sfxpkg','-o',str(roundtrip),
                         str(output/'iw8_cw_sound_check_common.ff')],cwd=args.acts.parent,
                        capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
    (args.out/'roundtrip.log').write_text(load.stdout+load.stderr,encoding='utf-8')
    if load.returncode:
        raise SystemExit('sound fastfile reload failed with exit code '+str(load.returncode))
    documents=[]
    for path in roundtrip.rglob('*.asset.json'):
        documents.append(json.loads(path.read_text(encoding='utf-8-sig')))
    pools={document['pool'] for document in documents}
    if not {'soundbanktransient','sfxpkg'} <= pools:
        raise SystemExit('native reload did not recover the sound bank and SFX package')
    bank=next(d for d in documents if d['pool']=='soundbanktransient')['asset']['fields']['bank']
    if bank['aliasCount'] != 1 or bank['alias']['values'][0]['aliasName']['string'] != source['alias']:
        raise SystemExit('reloaded SndBank alias table differs from the authored alias')
    if bank['streamInfo']['loadedStreamKey']['name'] != 'iw8_cw_sound_check_all_loaded':
        raise SystemExit('reloaded SndBank does not reference the resident StreamKey')
    alias=bank['alias']['values'][0]['head']['values'][0]
    if alias['assetId'] != sounds[0]['asset_id'] or alias['maxDuration_dsec'] != 3:
        raise SystemExit('reloaded SndAlias metadata differs from the resident SAB entry')
    sfx=next(d for d in documents if d['pool']=='sfxpkg')['asset']['fields']
    if sfx['sounds']['values'][0]['fireSoundPlayer']['name']['string'] != source['alias']:
        raise SystemExit('custom sound alias was not mapped to the SFX event')
    sample=subprocess.run([str(args.acts),'mw19soundtest',str(args.out/'custom.sabl'),str(sounds[0]['asset_id'])],
                          cwd=args.acts.parent,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=60)
    if sample.returncode:
        raise SystemExit('resident SAB sample did not decode')
    result={'format':'replay-native-sound-check-v1','aliases':1,'frames':frames,
            'build_exit':build.returncode,'reload_exit':load.returncode,
            'decode_exit':sample.returncode,'status':'passed'}
    (args.out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))


if __name__=='__main__':
    main()
