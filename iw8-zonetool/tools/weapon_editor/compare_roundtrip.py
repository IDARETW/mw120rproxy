"""Compare normalized native reloads with the complete prepared source graphs."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0,str(Path(__file__).resolve().parent.parent))
from prepare_melee_reference import prepare


def normalized(record):
    # Relocation fields carry their content, never the process address or
    # intermediate writer script-string index. Header scalar bytes must agree.
    result={'data':record['data'],'fixups':[]}
    for f in sorted(record['fixups'],key=lambda f:f['offset']):
        item={k:f[k] for k in ('offset','kind','text','name','asset_type') if k in f}
        if f['kind']=='record':
            item.update(normalized(f))
        result['fixups'].append(item)
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--library',type=Path,required=True)
    p.add_argument('--reloads',type=Path,required=True)
    p.add_argument('--schema',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    schema=json.loads(a.schema.read_text())
    catalog=json.loads((a.library/'catalog.json').read_text())
    expected={(x.get('pool',42),x['name']):x for x in catalog['attachments']+catalog['packages']}
    seen=set();mismatches=[]
    for f in a.reloads.glob('batch-*/roundtrip/mw19replay/*/assets/**/*.asset.json'):
        document=json.loads(f.read_text())
        fields=document['asset']['fields']
        key=(document['pool_id'],fields.get('szInternalName',fields.get('name'))['string'])
        if key not in expected:
            continue
        zone=next(x for x in f.parents if x.parent.name=='mw19replay')
        strings=json.loads((zone/'script_strings.json').read_text())
        original=json.loads((a.library/expected[key]['file']).read_text())['root']
        try:
            actual=prepare(schema,document,strings,root_type=document['root_type'])['root']
            if normalized(original)!=normalized(actual):
                mismatches.append({'pool':key[0],'name':key[1],'export':str(f)})
        except Exception as e:
            mismatches.append({'pool':key[0],'name':key[1],'export':str(f),'error':str(e)})
        seen.add(key)
    missing=[{'pool':k[0],'name':k[1]} for k in expected if k not in seen]
    result={'expected':len(expected),'reloaded':len(seen),'matching':len(seen)-len(mismatches),
            'mismatches':mismatches,'missing':missing}
    a.out.write_text(json.dumps(result,indent=2))
    print(json.dumps({k:v if isinstance(v,int) else len(v) for k,v in result.items()}))


if __name__=='__main__':
    main()
