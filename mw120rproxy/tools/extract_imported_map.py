from local_paths import UNLINKER
"""Extract a downloaded CoD4 FF/IWD into a fresh offline Replay import directory."""
import argparse
import hashlib
import json
from pathlib import Path,PurePosixPath
import re
import zipfile
from build_mp_test import REPO,TOOLS,COD4,run,write_json


def extract(args):
    if not re.fullmatch(r'mp_[a-z0-9_]{1,60}',args.map):raise ValueError('Invalid map id')
    source=args.source.resolve();output=args.output.resolve()
    ff=source/(args.map+'.ff')
    if not ff.is_file():raise FileNotFoundError(ff)
    if output.exists() and any(output.iterdir()):raise ValueError('Use a fresh output directory to preserve existing dumps')
    output.mkdir(parents=True,exist_ok=True)
    oat=args.unlinker.resolve()
    run([oat,'--no-color','--image-format','IWI','--model-format','OBJ','--search-path',str(COD4/'main')+';'+str(COD4/'raw')+';'+str(source),
        '-o',output,ff],REPO,output/'unlinker.log')
    for extension in ('ents','replay-world.json','replay-collision.json'):
        if not (output/f'maps/mp/{args.map}.d3dbsp.{extension}').is_file():
            raise ValueError(f'Missing {extension}; build the custom OAT exporters first')
    archives=[]
    for archive in sorted(source.glob('*.iwd')):
        record={'path':str(archive),'sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'images':[]}
        with zipfile.ZipFile(archive) as z:
            for entry in z.infolist():
                path=PurePosixPath(entry.filename)
                if entry.is_dir() or path.suffix.lower()!='.iwi':continue
                if path.is_absolute() or len(path.parts)!=2 or path.parts[0]!='images' or any(c in entry.filename for c in ('\\',':')):
                    raise ValueError(f'Unexpected IWD image path: {entry.filename}')
                if entry.file_size>64*1024*1024:raise ValueError('IWD image exceeds 64 MiB')
                destination=output.joinpath(*path.parts).resolve()
                if not destination.is_relative_to(output):raise ValueError('Image leaves extraction directory')
                if destination.exists():continue
                destination.parent.mkdir(parents=True,exist_ok=True);data=z.read(entry)
                destination.write_bytes(data);record['images'].append({'name':entry.filename,'sha256':hashlib.sha256(data).hexdigest()})
        archives.append(record)
    write_json(output/'extraction_report.json',{'map':args.map,'source':str(ff),'source_sha256':hashlib.sha256(ff.read_bytes()).hexdigest(),
        'unlinker':str(oat),'unlinker_sha256':hashlib.sha256(oat.read_bytes()).hexdigest(),'archives':archives,
        'note':'Read unlinker.log for unsupported unused assets. The map builder rejects missing used model/material/image dependencies.'})
    print(f'Extracted {args.map} into {output}. Build with build_imported_map.py next.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True)
    p.add_argument('--map',required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--unlinker',type=Path,default=UNLINKER)
    extract(p.parse_args())
