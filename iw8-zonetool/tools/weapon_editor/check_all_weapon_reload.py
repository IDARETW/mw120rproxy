"""Reload every generated stock-reference weapon zone through the Replay reader.

Run check_native.py first. This second stage intentionally asks ACTS to export
only the generated registration assets. ACTS still parses and loads the whole
zone, including WeaponCompleteDef and all dependencies, without invoking known
stock structured-export limitations in inactive weapon union members.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--builds',type=Path,required=True,
                        help='Output directory produced by check_native.py')
    parser.add_argument('--acts',type=Path,required=True)
    parser.add_argument('--game',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--workers',type=int,default=4)
    args=parser.parse_args()
    if not 1 <= args.workers <= 8:
        raise SystemExit('workers must be 1..8')
    results=json.loads((args.builds/'results.json').read_text(encoding='utf-8-sig'))
    if not results or any(item['exit_code'] for item in results):
        raise SystemExit('input build sweep is incomplete or contains failures')
    zones=[]
    for index,item in enumerate(results):
        zone=args.builds/item['weapon']/'output'/f'iw8_cw_check_{index}.ff'
        if not zone.is_file():
            raise SystemExit('missing generated zone: '+str(zone))
        zones.append((item,zone))
    if args.out.exists() and any(args.out.iterdir()):
        raise SystemExit('Choose an empty output directory for this check')
    args.out.mkdir(parents=True, exist_ok=True)
    def reload_one(entry, attempt=1):
        number, (item, zone) = entry
        folder=args.out/f'zone-{number:03}'
        command=[str(args.acts),'--noUpdater','fastfile','-r','mw19replay','-g',str(args.game),
                 '-a','netconststrings,stringtable','-o',str(folder),
                 str(zone)]
        run=subprocess.run(command,cwd=args.acts.parent,capture_output=True,text=True,
                           encoding='utf-8',errors='replace',timeout=180)
        (args.out/f'zone-{number:03}.attempt-{attempt}.log').write_text(
            run.stdout+run.stderr,encoding='utf-8')
        manifests=[]
        if folder.exists():
            manifests=[json.loads(path.read_text(encoding='utf-8-sig'))
                       for path in folder.rglob('manifest.json')]
        good=run.returncode==0 and len(manifests)==1
        recovered = None
        for manifest in manifests:
            loaded=manifest.get('loaded_by_type',{})
            manifest_good = bool(
                manifest.get('complete') and manifest.get('success')
                and not manifest.get('failed') and not manifest.get('unavailable')
                and loaded.get('weapon') == 1
                and loaded.get('netconststrings') == 1
                and loaded.get('stringtable') == 2
            )
            good = good and manifest_good
            if manifest_good:
                recovered = Path(manifest['fastfile']).stem
        return {'index':number,'weapon':item['weapon'],'zone':zone.stem,
                'exit_code':run.returncode,'manifests':len(manifests),
                'recovered':recovered,'passed':bool(good),'attempts':attempt}

    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        run_results=list(pool.map(reload_one,enumerate(zones)))
    # Concurrent reader startup can occasionally fail before ACTS creates a
    # manifest. Retry only those launch failures in isolation; a deterministic
    # parser or asset-count failure still fails on its sequential retry.
    for attempt in (2, 3):
        failed=[result for result in run_results if not result['passed']]
        if not failed:
            break
        for previous in failed:
            folder=args.out/f"zone-{previous['index']:03}"
            if folder.exists():
                shutil.rmtree(folder)
            run_results[previous['index']]=reload_one(
                (previous['index'],zones[previous['index']]),attempt)
    recovered={result['recovered'] for result in run_results if result['recovered']}
    for result in run_results:
        if not result['passed']:
            print(json.dumps(result),flush=True)
    expected={f'iw8_cw_check_{i}' for i in range(len(zones))}
    summary={'format':'replay-all-weapon-reload-check-v1','weapons':len(zones),
             'runs':len(run_results),'passed':sum(item['passed'] for item in run_results),
             'recovered':len(recovered),'status':'passed' if recovered==expected and
             all(item['passed'] for item in run_results) else 'failed'}
    (args.out/'results.json').write_text(json.dumps({'summary':summary,'runs':run_results},indent=2)+'\n',
                                         encoding='utf-8')
    print(json.dumps(summary),flush=True)
    if summary['status']!='passed':
        raise SystemExit(1)


if __name__=='__main__':
    main()
