"""Build category references and owned package graphs through the native writer."""
import argparse
import copy
import json
from pathlib import Path
import subprocess
import time


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--library',type=Path,required=True)
    p.add_argument('--compiler',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    catalog=json.loads((a.library/'catalog.json').read_text())
    a.out.mkdir(parents=True,exist_ok=True)
    results=[]
    for i,w in enumerate(catalog['weapons']):
        folder=a.out/w['name']
        folder.mkdir(exist_ok=True)
        manifest={'format':'replay-weapon-build-v1','reference':str(a.library/w['file']),
                  'name':'iw8_cw_check_'+str(i),'display_name':'Native check '+str(i),
                  'loadout_slot':61,'attachments':True,'category':w['category'],'owned_assets':[]}
        # Each category's attachment references exercise the complete graph.
        # Own one compatible attachment so its header and children are emitted.
        source=json.loads((a.library/w['file']).read_text())
        refs=[]
        for f in source['root']['fixups']:
            if f['field'].startswith('weapon.attachments['):
                refs += [v['name'] for v in f['fixups'] if v['kind']=='asset']
        if refs:
            desc=next((x for x in catalog['attachments'] if x['name']==refs[-1]),None)
            if desc:
                root=json.loads((a.library/desc['file']).read_text())['root']
                manifest['owned_assets'].append({'pool':42,'name':desc['name'],'root':root})
        path=folder/'build.json'
        path.write_text(json.dumps(manifest))
        begin=time.monotonic()
        result=subprocess.run([str(a.compiler),'build-weapon','--project',str(path),'-o',str(folder/'output')],
                              capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90)
        (folder/'build.log').write_text(result.stdout+result.stderr)
        result={'weapon':w['name'],'category':w['category'],'exit_code':result.returncode,
                'seconds':round(time.monotonic()-begin,3),'owned_attachments':len(manifest['owned_assets'])}
        results.append(result)
        if result['exit_code']:
            print(json.dumps(result),flush=True)
    (a.out/'results.json').write_text(json.dumps(results,indent=2))
    print(json.dumps({'total':len(results),'passed':sum(r['exit_code']==0 for r in results),
                      'failed':sum(r['exit_code']!=0 for r in results)}),flush=True)
    if any(r['exit_code'] for r in results):
        raise SystemExit(1)


if __name__=='__main__':
    main()
