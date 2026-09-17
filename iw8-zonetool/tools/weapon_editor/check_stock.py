"""Extract every selectable stock assembly and validate geometry/skin correspondence."""
import argparse
import json
import math
from pathlib import Path
from stock import StockLibrary
from build_viewhands import read_glb

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--config',default='config.local.json')
parser.add_argument('--library',required=True)
parser.add_argument('--output',required=True)
parser.add_argument('--weapon',action='append')
args=parser.parse_args()
config=json.loads(Path(args.config).read_text())
library=Path(args.library)
stock=StockLibrary(config,library,Path(config['stock_cache']))
catalog=json.loads((library/'catalog.json').read_text())
tables=json.loads((library/'tables.json').read_text())
results=[]
for weapon in catalog['weapons']:
    if not weapon.get('selectable') or args.weapon and weapon['name'] not in args.weapon:continue
    row={'name':weapon['name']}
    try:
        folder,assembly=stock.load(weapon,tables)
        row['views']={}
        for view,data in assembly.items():
            rig=data['rig'];document,_=read_glb(folder/(view+'.glb'))
            count=sum(document['accessors'][p['attributes']['POSITION']]['count']
                      for mesh in document['meshes'] for p in mesh['primitives'])
            assert count==len(rig['vertex_weights']), 'Vertex weights must match indexed geometry'
            for weights in rig['vertex_weights']:
                assert weights and all(w['bone'] in rig['bones'] and math.isfinite(w['weight']) and w['weight']>0 for w in weights)
                assert abs(sum(w['weight'] for w in weights)-1)<.01
            row['views'][view]={'bones':len(rig['bones']),'vertices':count,'warnings':data['warnings']}
    except Exception as error:row['error']=str(error)
    results.append(row);print(json.dumps(row),flush=True)
    Path(args.output).write_text(json.dumps(results,indent=2))
raise SystemExit(1 if any('error' in r for r in results) else 0)
