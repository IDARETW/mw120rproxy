"""Decode Replay XAnimParts exports into sparse, native-coordinate preview tracks.

The packed stream categories are checked against the native writer and Replay
export counts. This is a preview cache, never a replacement gameplay asset.
"""
import json
import math
from pathlib import Path
import re
import struct
from functools import lru_cache


def array(value, code):
    if value is None:
        return []
    if 'active_member' in value:
        return array(value.get('value'), code)
    if 'values' in value:
        return [item.get('value', item) if isinstance(item, dict) else item for item in value['values']]
    raw = bytes.fromhex(value.get('bytes', ''))
    size = struct.calcsize('<'+code)
    if len(raw) % size:
        raise ValueError('Misaligned animation stream')
    return list(struct.unpack('<'+code*(len(raw)//size), raw))


def decode(document, strings):
    f = document['asset']['fields']
    name = f['name']['string']
    frames, fps = f['numframes'], f['framerate']
    counts = f['boneCount']
    if not 0 < fps <= 1000 or not 0 <= frames <= 65535 or len(counts) != 10:
        raise ValueError('Invalid animation frame header')
    if sum(counts[:5]) != counts[9] or sum(counts[5:9]) != counts[9]:
        raise ValueError('Animation bone categories do not cover the skeleton')
    names = [strings[i] for i in array(f['names'], 'I')]
    if len(names) != counts[9] or any(not isinstance(n,str) for n in names):
        raise ValueError('Invalid animation bone names')
    streams = {k:array(f.get(k),c) for k,c in {
        'dataByte':'B','dataShort':'h','dataInt':'i','randomDataByte':'B',
        'randomDataShort':'h','randomDataInt':'i','indices':'H' if frames>=256 else 'B'
    }.items()}
    offsets = dict.fromkeys(streams,0)

    def take(key, count=1):
        start = offsets[key]
        end = start+count
        if count<0 or end>len(streams[key]):
            raise ValueError(f'{name}: truncated {key} at {start}, need {count}')
        offsets[key] = end
        values = streams[key][start:end]
        return values[0] if count==1 else values

    def times():
        size = take('dataShort') & 65535
        if size > frames:
            raise ValueError('Animation key count exceeds its duration')
        if frames>=256 and size>=64:
            take('dataShort',((size-1)>>8)+2)
        key = 'dataByte' if frames<256 else 'indices' if size>=64 else 'dataShort'
        values = [take(key)&(255 if frames<256 else 65535) for _ in range(size+1)]
        if values != sorted(set(values)) or values[-1]>frames:
            raise ValueError('Invalid animation keyframe indices')
        return [v/fps for v in values]

    def quaternion(values):
        length = math.sqrt(sum(v*v for v in values))
        if not length:
            raise ValueError('Zero animation quaternion')
        return [v/length for v in values]

    def floats(count):
        return [struct.unpack('<f',struct.pack('<I',take('dataInt')&0xffffffff))[0] for _ in range(count)]

    tracks = [{'bone':n} for n in names]
    index = 0
    for category, count in enumerate(counts[:5]):
        for _ in range(count):
            track = tracks[index]
            index += 1
            if category == 0:
                track.update(rotation_times=[0],quaternions=[[0,0,0,1]])
                continue
            dynamic = category in (1,2)
            keys = times() if dynamic else [0]
            width = 2 if category in (1,3) else 4
            stream = 'randomDataShort' if dynamic else 'dataShort'
            values = []
            for _ in keys:
                q = take(stream,width)
                values.append(quaternion(([0,0]+q) if width==2 else q))
            track.update(rotation_times=keys,quaternions=values)
    for category,count in enumerate(counts[5:9],5):
        for _ in range(count):
            bone = take('dataByte')
            if bone>=len(tracks):
                raise ValueError('Translation references a missing bone')
            if category == 8:
                continue  # No translation channel: retain the model's bind translation.
            if category == 7:
                keys, values = [0], [floats(3)]
            else:
                keys = times()
                minimum, step = floats(3), floats(3)
                stream = 'randomDataByte' if category==5 else 'randomDataShort'
                mask = 255 if category==5 else 65535
                values = [[minimum[i]+(take(stream)&mask)*step[i] for i in range(3)] for _ in keys]
            if any(not math.isfinite(v) for row in values for v in row):
                raise ValueError('Non-finite animation translation')
            tracks[bone].update(position_times=keys,translations=values)
    for key,values in streams.items():
        if offsets[key] != len(values):
            raise ValueError(f'{name}: {key} left {len(values)-offsets[key]} unread values')
    notes = [{'name':strings[n['name']['value']],'time':n['time']*frames/fps}
             for n in (f.get('notify') or {}).get('values',[])]
    delta = f.get('deltaPart')
    return {'format':'replay-native-animation-preview-v1','native':True,'name':name,
            'asset':name,'fps':fps,'duration':frames/fps,'frames':frames,'loop':bool(f['flags']&1),
            'tracks':tracks,'notetracks':notes,
            'warnings':['Root-motion delta is not applied in this stationary preview.'] if delta else []}


class AnimationLibrary:
    def __init__(self, export):
        self.export = Path(export)
        self.files = {}
        self.strings = None
        for path in (self.export/'assets'/'xanim').glob('*.asset.json'):
            name = re.sub(r'\.\d+\.asset\.json$','',path.name)
            self.files[name] = path

    @lru_cache(maxsize=32)
    def clip(self, name):
        if name not in self.files:
            raise ValueError('Native animation data is not installed for '+name)
        if self.strings is None:
            self.strings = json.loads((self.export/'script_strings.json').read_text(encoding='utf-8-sig'))
        return decode(json.loads(self.files[name].read_text(encoding='utf-8-sig')),self.strings)
