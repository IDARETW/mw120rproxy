"""Read-only verification of the exact Replay target and the built proxy exports."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import pefile

parser = argparse.ArgumentParser()
parser.add_argument('--dll', type=Path, required=True)
parser.add_argument('--game', type=Path, required=True)
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
pe = pefile.PE(str(args.dll))
exports = {e.name.decode(): e.ordinal for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
expected = {'DllMain': 1, 'XInputGetCapabilities': 2, 'XInputGetDSoundAudioDeviceGuids': 3,
            'XInputGetState': 4, 'XInputSetState': 5}
if exports != expected or pe.FILE_HEADER.Machine != 0x8664:
    raise SystemExit(f'Unexpected proxy architecture/exports: {exports}')
data = args.game.read_bytes()
if hashlib.md5(data).hexdigest() != '1c238fe327f2ecc3b0db924c5b425439':
    raise SystemExit('Replay executable does not match the analyzed 1.20 image; deployment refused')
target = pefile.PE(data=data, fast_load=True)
checks = {0x13E7670: '48895c24084889742410574883ec60',
          0x3061A0: '4057b8c0330000e8446dec01482be0',
          0x13E7D40: '48895c2420555657415441554156',
          0x13D9240: '48895c2408574883ec20488bda488bf9',
          0x1BD3479: 'ff1501ea7700'}
checks.update({
    0xD8CA30: '48895c24084889742410574883ec20',
    0x165FFA0: '40534883ec50488b054b443f04',
    0x1528490: '40534883ec208bd9',
    0x17EC930: '40534883ec204863c1ba21000000',
    0x12A1EB0: '4883ec28488b05653abb0d',
    0x1666030: '40534883ec20488bda',
    0x1665C80: '48895c2408574883ec20',
    0x16649F0: '40534881ecb0000000488b05f8f93e04',
    0x1660280: '488d058926fc02c3',
    0x1662FC0: '488d05497c9f0dc3',
    0x1662C40: '48895c240848897c241055488bec',
    0x1665990: '4863c14869c8e8000000488d058fadf50c',
    0x1665C10: '488b05d1509f0d488901488bc1c3',
    0x259C448: 'a0969b4101000000',
    0x1A039E0: '40534883ec20ba01000000488bd9',
})
for rva, hex_bytes in checks.items():
    value = bytes.fromhex(hex_bytes)
    if target.get_data(rva, len(value)) != value:
        raise SystemExit(f'Target bytes differ at RVA 0x{rva:X}')
# Bindings for the native console, package browser and disk-open route.
bindings = Path(__file__).resolve().parents[1] / 'data' / 'replay_bindings.json'
binding_records = json.loads(bindings.read_text(encoding='utf-8'))
source_root = Path(__file__).resolve().parents[1]
compiled_bindings = {}
for name, rva, values, size in re.findall(
        r'Binding\s+(\w+)\{(0x[0-9A-Fa-f]+),\s*\{([^}]+)\},\s*(\d+)\}',
        (source_root / 'replay_bindings.h').read_text(encoding='utf-8')):
    value = bytes(int(token.strip(), 16) for token in values.split(','))
    if len(value) != int(size):
        raise SystemExit(f'Binding size mismatch: {name}')
    compiled_bindings[name] = (int(rva, 16), value)
if set(compiled_bindings) != {b['name'] for b in binding_records}:
    raise SystemExit('Compiled binding names and evidence differ')
for binding in binding_records:
    rva = int(binding['rva'], 16)
    value = bytes.fromhex(binding['bytes'])
    if compiled_bindings[binding['name']] != (rva, value):
        raise SystemExit(f"Compiled binding differs from evidence: {binding['name']}")
    if target.get_data(rva, len(value)) != value:
        raise SystemExit(f"Replay binding differs: {binding['name']} at 0x{rva:X}")
fixture_source = (source_root / 'tests' / 'replay_decoder_fixture.h').read_text(encoding='utf-8')
decoder_fixtures = []
for name, rva, values in re.findall(
        r'uintptr_t (\w+)Rva=(0x[0-9A-Fa-f]+);\s*inline constexpr unsigned char \1\[\]=\{([^}]+)\}', fixture_source):
    value = bytes(int(token.strip(), 16) for token in values.split(',') if token.strip())
    if target.get_data(int(rva, 16), len(value)) != value:
        raise SystemExit(f'Native decoder test fixture differs from Replay: {name}')
    decoder_fixtures.append({'name': name, 'rva': rva, 'size': len(value)})
if len(decoder_fixtures) != 5:
    raise SystemExit('Expected five native decoder test fixtures')
physics_source = (source_root / 'tests' / 'replay_physics_fixture.h').read_text(encoding='utf-8')
physics_fixtures = []
for name, rva, values in re.findall(
        r'uintptr_t (\w+)Rva=(0x[0-9A-Fa-f]+);\s*inline constexpr unsigned char \1\[\]=\{([^}]+)\}', physics_source):
    value = bytes(int(token.strip(), 16) for token in values.split(',') if token.strip())
    if target.get_data(int(rva, 16), len(value)) != value:
        raise SystemExit(f'Native physics test fixture differs from Replay: {name}')
    physics_fixtures.append({'name': name, 'rva': rva, 'size': len(value)})
if len(physics_fixtures) != 6:
    raise SystemExit('Expected six native physics test fixtures')
metadata_source = (source_root / 'tests' / 'replay_metadata_fixture.h').read_text(encoding='utf-8')
metadata_fixtures = []
for name, rva, values in re.findall(
        r'uintptr_t (\w+)Rva=(0x[0-9A-Fa-f]+);\s*inline constexpr unsigned char \1\[\]=\{([^}]+)\}', metadata_source):
    value = bytes(int(token.strip(), 16) for token in values.split(',') if token.strip())
    if target.get_data(int(rva, 16), len(value)) != value:
        raise SystemExit(f'Native metadata test fixture differs from Replay: {name}')
    metadata_fixtures.append({'name': name, 'rva': rva, 'size': len(value)})
if len(metadata_fixtures) != 2:
    raise SystemExit('Expected two native metadata test fixtures')
convex_source=(source_root/'tests/replay_convex_fixture.h').read_text()
convex_bytes=bytes(int(x,16) for x in re.findall(r'0x([0-9a-f]{2})(?=[,\n])',convex_source))
if len(convex_bytes)!=0x3B9 or target.get_data(0x161CBF0,len(convex_bytes))!=convex_bytes:
    raise SystemExit('Native convex hull test fixture differs from Replay')
compound_source=(source_root/'tests/replay_compound_fixture.h').read_text()
compound_bytes=bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)',compound_source.split('ReplayCompoundCode[]{',1)[1].split('}',1)[0]))
if len(compound_bytes)!=0x474 or target.get_data(0x161C770,len(compound_bytes))!=compound_bytes:
    raise SystemExit('Native compound test fixture differs from Replay')
compound_info=bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)',compound_source.split('ReplayCompoundInfoCode[]{',1)[1].split('}',1)[0]))
if len(compound_info)!=32 or target.get_data(0x1E459F0,len(compound_info))!=compound_info:
    raise SystemExit('Native compound configuration initializer differs from Replay')
image_source=(source_root/'tests/replay_image_fixture.h').read_text()
image_fixtures=[]
for name,rva,values in re.findall(r'uintptr_t (\w+)Rva=(0x[0-9A-Fa-f]+);\s*inline constexpr unsigned char \1\[\]=\{([^}]+)\}',image_source):
    value=bytes(int(token.strip(),16) for token in values.split(',') if token.strip())
    if target.get_data(int(rva,16),len(value))!=value:raise SystemExit(f'Native image fixture differs from Replay: {name}')
    image_fixtures.append(name)
if len(image_fixtures)!=2:raise SystemExit('Expected two native image setup fixtures')
localize_source=(source_root/'tests/replay_localize_fixture.h').read_text()
localize_bytes=bytes(int(x,16) for x in re.findall(r'0x([0-9a-f]{2})',localize_source.split('ReplayLocalizeCode[]={',1)[1]))
if len(localize_bytes)!=0x6B or target.get_data(0x13CC2A0,len(localize_bytes))!=localize_bytes:
    raise SystemExit('Native localization test fixture differs from Replay')
report = {'dll': str(args.dll.resolve()), 'sha256': hashlib.sha256(args.dll.read_bytes()).hexdigest(),
          'exports': exports, 'target_md5': hashlib.md5(data).hexdigest(),
          'target_sha256': hashlib.sha256(data).hexdigest(), 'prologues_and_splash_call_match': True,
          'developer_ui_and_disk_bindings_checked': [b['name'] for b in binding_records],
          'native_decoder_fixtures_checked': decoder_fixtures,
          'native_physics_fixtures_checked': physics_fixtures,
          'native_metadata_fixtures_checked': metadata_fixtures,
          'native_image_fixtures_checked': image_fixtures,
          'game_launched': False}
args.out.parent.mkdir(parents=True, exist_ok=True)
args.out.write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report, indent=2))
