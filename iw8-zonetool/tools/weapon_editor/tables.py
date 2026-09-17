"""Native Gunsmith table import and per-project registration data.

This ACTS build exports Replay's row-major cell array through a column-major
accessor. Undo that permutation, then verify against the independently captured
native stats and loot fixtures before accepting the library.
"""
import argparse
import copy
import csv
import json
import io
from pathlib import Path
import re


def decode_csv(path):
    raw=path.read_bytes()
    try:
        text=raw.decode('utf-8-sig')
    except UnicodeDecodeError:
        text=raw.decode('cp1252')
    cells=list(csv.reader(io.StringIO(text,newline='')))
    rows=len(cells)
    if not rows:
        return []
    columns=len(cells[0]) if rows else 0
    if not rows or any(len(row)!=columns for row in cells):
        raise ValueError('Invalid exported table extent: '+str(path))
    return [[cells[(r*columns+c)%rows][(r*columns+c)//rows] for c in range(columns)] for r in range(rows)]


def import_tables(source,fixtures,out):
    tables={}
    for directory in Path(source).glob('*/assets/stringtable'):
        for f in directory.rglob('*.csv'):
            name=re.sub(r'\.\d+\.csv$','',f.relative_to(directory).as_posix()).lower()
            if name not in ('mp/statstable.csv','mp/attachmenttable.csv','mp/attachmentmap.csv',
                            'loot/weapon_ids.csv') and not name.startswith((
                                'mp/gunsmith/','loot/iw8_','frontendscenedata/','ui/stringtable/weapon_')):
                continue
            tables[name]=decode_csv(f)
    expected=json.loads(Path(fixtures).read_text())
    for name in ('mp/statsTable.csv','loot/weapon_ids.csv'):
        if tables.get(name.lower())!=expected[name]:
            raise ValueError('Export permutation does not match native fixture: '+name)
    Path(out).write_text(json.dumps(tables),encoding='utf-8')
    print(f'{len(tables)} tables imported; native stats and loot fixtures match exactly')
    return tables


def registration(project,tables):
    base=project['base']
    reference=project.get('loadout_reference',project['reference_name']).removesuffix('_mp')
    stats=next((row for row in tables['mp/statstable.csv'] if row[4]==reference),None)
    if not stats:
        raise ValueError('This special weapon requires a loadout reference category before registration')
    header=tables['mp/attachmentmap.csv'][0]
    tokens=set(header[1:]) | {row[5] for row in tables['mp/attachmenttable.csv'] if row[5]}
    native_map={}
    for key in (stats[1],reference):
        row=next((r for r in tables['mp/attachmentmap.csv'] if r[0]==key),None)
        if row:
            native_map.update({header[i]:name for i,name in enumerate(row) if i and name})
    slots=set(name for group in project['attachment_slots'] for name in group)
    mapping={token:name for token,name in native_map.items() if name in slots}
    rows_by_name={r[4]:r for r in tables['mp/attachmenttable.csv']}
    owned={a['name']:a for a in project['owned_assets'] if a['pool']==42}
    custom_rows=[]
    for name in sorted(slots):
        source=owned.get(name,{}).get('source',name)
        row=rows_by_name.get(source)
        ui=owned.get(name,{}).get('ui',{})
        token=ui.get('token') or (row[5] if row else next((k for k,v in native_map.items() if v==source),None))
        if not token:
            # Native-only default parts need no loadout token; they can still
            # occur in the weapon's attachment graph.
            continue
        if token not in tokens:
            raise ValueError('Unknown loadout attachment token: '+token)
        if name in owned:
            if token in mapping and mapping[token] not in (name,source):
                raise ValueError('Attachment token '+token+' is already assigned to '+mapping[token])
            mapping[token]=name
        else:
            mapping.setdefault(token,name)
        if name in owned and row:
            custom_rows.append({'asset':name,'reference':source,'token':token,
                                'display_name':ui.get('title',name.rsplit('/',1)[-1])})
    defaults=[token for token in stats[9].split() if token in mapping]
    progression=['']*25
    progression[0]='0'
    ids=[]
    attachment_ids=[]
    for i,token in enumerate(sorted(mapping)):
        if token in defaults:
            continue
        identifier=str(6000000+project['loadout_slot']*256+i)
        ids.append(identifier)
        attachment_ids.append([identifier,token,'0','0','0','0']+['']*12)
    progression[1]='|'.join(ids)
    variant=['']*26
    variant[0:4]=['0',base+'_variant_0',base,base+'_mp']
    variant[17]='CUSTOM_WEAPON/'+base
    emitted={f'mp/gunsmith/{base[4:]}_progression.csv':[progression],
             f'mp/gunsmith/{base[4:]}_variants.csv':[variant]}
    if attachment_ids:
        emitted[f'loot/{base}_attachment_ids.csv']=attachment_ids
    return {'loadout_reference':reference+'_mp','attachment_map':mapping,'attachment_rows':custom_rows,
            'default_attachments':' '.join(defaults),'tables':emitted}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exports',type=Path,required=True)
    p.add_argument('--fixtures',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    import_tables(a.exports,a.fixtures,a.out)
