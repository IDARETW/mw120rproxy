"""Build Replay 1.20 resident SAB v10 sound banks from PCM WAV sources."""
from __future__ import annotations

import hashlib
from pathlib import Path
import struct
import wave


def snd_hash(value: str) -> int:
    result = 5381
    for byte in value.encode('ascii'):
        if 65 <= byte <= 90:
            byte += 32
        result = (result * 65599 + byte) & 0xffffffff
    return result or 1


def _crc8(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ (7 if crc & 0x80 else 0)) & 0xff
    return crc


def _crc16(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x8005 if crc & 0x8000 else 0)) & 0xffff
    return crc


def _utf8_number(value: int) -> bytes:
    if value < 0x80:
        return bytes((value,))
    if value < 0x800:
        return bytes((0xc0 | value >> 6, 0x80 | value & 0x3f))
    if value < 0x10000:
        return bytes((0xe0 | value >> 12, 0x80 | value >> 6 & 0x3f, 0x80 | value & 0x3f))
    if value < 0x200000:
        return bytes((0xf0 | value >> 18, 0x80 | value >> 12 & 0x3f,
                      0x80 | value >> 6 & 0x3f, 0x80 | value & 0x3f))
    if value < 0x4000000:
        return bytes((0xf8 | value >> 24, 0x80 | value >> 18 & 0x3f,
                      0x80 | value >> 12 & 0x3f, 0x80 | value >> 6 & 0x3f,
                      0x80 | value & 0x3f))
    if value < 0x80000000:
        return bytes((0xfc | value >> 30, 0x80 | value >> 24 & 0x3f,
                      0x80 | value >> 18 & 0x3f, 0x80 | value >> 12 & 0x3f,
                      0x80 | value >> 6 & 0x3f, 0x80 | value & 0x3f))
    raise ValueError('Sound requires too many FLAC frames')


def _pcm16(path: Path):
    try:
        with wave.open(str(path), 'rb') as source:
            channels, width, rate, frames = (source.getnchannels(), source.getsampwidth(),
                                              source.getframerate(), source.getnframes())
            if source.getcomptype() != 'NONE' or channels not in (1, 2) or width not in (1, 2, 3, 4):
                raise ValueError('Native audio requires mono or stereo uncompressed PCM WAV')
            if not 8000 <= rate <= 192000 or not frames:
                raise ValueError('WAV sample rate or frame count is invalid')
            raw = source.readframes(frames)
    except wave.Error as error:
        raise ValueError('Native audio requires an uncompressed PCM WAV file') from error
    expected = frames * channels * width
    if len(raw) != expected:
        raise ValueError('WAV sample data is truncated')
    samples = [[] for _ in range(channels)]
    for frame in range(frames):
        for channel in range(channels):
            at = (frame * channels + channel) * width
            if width == 1:
                value = (raw[at] - 128) << 8
            elif width == 2:
                value = int.from_bytes(raw[at:at + 2], 'little', signed=True)
            elif width == 3:
                value = int.from_bytes(raw[at:at + 3] + (b'\xff' if raw[at + 2] & 0x80 else b'\0'),
                                       'little', signed=True) >> 8
            else:
                value = int.from_bytes(raw[at:at + 4], 'little', signed=True) >> 16
            samples[channel].append(max(-32768, min(32767, value)))
    return rate, channels, frames, samples


def _flac_frames(samples: list[list[int]]) -> bytes:
    total = len(samples[0])
    channels = len(samples)
    encoded = bytearray()
    frame_number = 0
    for start in range(0, total, 4096):
        count = min(4096, total - start)
        # Sync, fixed-block strategy, 16-bit explicit block size, inherited rate,
        # independent channel assignment and explicit signed 16-bit samples.
        header = bytearray((0xff, 0xf8, 0x70, ((channels - 1) << 4) | (4 << 1)))
        header.extend(_utf8_number(frame_number))
        header.extend(((count - 1) >> 8, (count - 1) & 0xff))
        header.append(_crc8(header))
        frame = bytearray(header)
        for channel in range(channels):
            frame.append(0x02)  # zero padding + VERBATIM subframe type
            for value in samples[channel][start:start + count]:
                frame.extend(struct.pack('>h', value))
        frame.extend(struct.pack('>H', _crc16(frame)))
        encoded.extend(frame)
        frame_number += 1
    return bytes(encoded)


def generate(root: Path, sources: list[dict], output: Path) -> list[dict]:
    """Write one resident .sabl and return validated native alias metadata."""
    if not sources:
        return []
    entries = []
    used_aliases, used_assets = set(), set()
    for index, source in enumerate(sources):
        alias = str(source.get('alias', '')).strip().lower()
        if not alias or len(alias) > 120 or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789_/' for c in alias):
            raise ValueError('Sound alias names use lowercase letters, numbers, slash and underscore')
        if alias in used_aliases:
            raise ValueError('Two native sounds use the same alias')
        used_aliases.add(alias)
        relative = str(source.get('path', ''))
        path = (root / relative).resolve()
        if not path.is_relative_to(root.resolve()) or path.suffix.lower() != '.wav' or not path.is_file():
            raise ValueError('Native sound source must be a project PCM WAV file')
        rate, channels, frames, pcm = _pcm16(path)
        encoded = _flac_frames(pcm)
        asset_name = f"{alias}/sample_{index + 1}"
        asset_id = snd_hash(asset_name)
        while asset_id in used_assets:
            asset_name += '_x'
            asset_id = snd_hash(asset_name)
        used_assets.add(asset_id)
        entry = dict(source)
        entry.update(alias=alias, alias_id=snd_hash(alias), asset_file=asset_name, asset_id=asset_id,
                     rate=rate, channels=channels, frames=frames, encoded=encoded,
                     duration=frames / rate, looping=bool(source.get('looping', False)))
        entries.append(entry)

    header_size, entry_size, checksum_size = 688, 44, 16
    index_offset = header_size
    checksum_offset = index_offset + entry_size * len(entries)
    data_offset = (checksum_offset + checksum_size * len(entries) + 15) & ~15
    cursor = data_offset
    for entry in entries:
        entry['offset'] = cursor
        cursor += len(entry['encoded'])
        cursor = (cursor + 15) & ~15
    bank = bytearray(cursor)
    struct.pack_into('<IIII', bank, 0, 0x23585532, 10, entry_size, checksum_size)
    struct.pack_into('<I', bank, 20, len(entries))
    struct.pack_into('<I', bank, 28, 16)
    struct.pack_into('<QQQ', bank, 32, len(bank), index_offset, checksum_offset)
    struct.pack_into('<I', bank, 679, len(bank) - data_offset)
    for index, entry in enumerate(entries):
        at = index_offset + index * entry_size
        struct.pack_into('<IIIIIQIBBB', bank, at, entry['asset_id'], len(entry['encoded']), 0,
                         entry['frames'], 0, entry['offset'], entry['rate'], entry['channels'],
                         int(entry['looping']), 8)
        bank[checksum_offset + index * checksum_size:checksum_offset + (index + 1) * checksum_size] = \
            hashlib.md5(entry['encoded']).digest()
        bank[entry['offset']:entry['offset'] + len(entry['encoded'])] = entry['encoded']
    output.write_bytes(bank)
    return [{k: v for k, v in entry.items() if k != 'encoded'} for entry in entries]
