"""Build and reload a custom XAnim bound through an owned animation package."""
import argparse
import copy
import json
import math
from pathlib import Path
import shutil
import subprocess

from graph import set_value


SOURCE_PACKAGE = 'iw8_pi_mike1911_animpackage'
PACKAGE_FIELD = 'sfx.anims[239]'


def clip(asset):
    frames = 31
    translations = []
    quaternions = []
    for index in range(frames):
        phase = index / (frames - 1)
        translations.append([phase * .25, 0.0, math.sin(phase * math.pi) * .05])
        quaternions.append([0.0, 0.0, 0.0, 1.0])
    return {
        'format': 'replay-animation-source-v1', 'asset': asset, 'name': 'bound_fire',
        'fps': 30.0, 'duration': 1.0, 'loop': False, 'asset_type': 6,
        'ik_type': 1, 'finger_pose_type': 1,
        'tracks': [{'bone': 'tag_weapon', 'translations': translations,
                    'quaternions': quaternions}],
        'notetracks': [{'name': 'fire', 'time': .1}],
    }


def asset_name(value):
    if not value:
        return None
    return value.get('name', '').lstrip(',')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--acts', type=Path, required=True)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    if any(args.out.iterdir()):
        raise SystemExit('Choose an empty output directory for this check')

    catalog = json.loads((args.library/'catalog.json').read_text(encoding='utf-8-sig'))
    weapon_desc = next(item for item in catalog['weapons']
                       if item['name'] == 'iw8_pi_mike1911_mp')
    package_desc = next(item for item in catalog['packages']
                        if item['pool'] == 77 and item['name'] == SOURCE_PACKAGE)
    reference = json.loads((args.library/weapon_desc['file']).read_text(encoding='utf-8-sig'))
    package = json.loads((args.library/package_desc['file']).read_text(encoding='utf-8-sig'))

    base = 'iw8_cw_animation_binding_check'
    package_name = base + '/animpackage'
    animation_name = base + '/anim/bound_fire'
    set_value(reference['root'], 'weapon.weapDef[0].szXAnims', package_name, {})
    set_value(package['root'], 'sfx.name', package_name, {})
    set_value(package['root'], PACKAGE_FIELD, animation_name, {})
    reference_path = args.out/'reference.json'
    reference_path.write_text(json.dumps(reference, indent=2)+'\n', encoding='utf-8')

    manifest = {
        'format': 'replay-weapon-build-v1', 'reference': str(reference_path),
        'name': base, 'display_name': 'Animation binding check', 'loadout_slot': 61,
        'attachments': False, 'category': weapon_desc['category'],
        'owned_assets': [{'pool': 77, 'name': package_name, 'source': SOURCE_PACKAGE,
                          'root': copy.deepcopy(package['root'])}],
        'animations': [clip(animation_name)],
    }
    manifest_path = args.out/'build.json'
    manifest_path.write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    output = args.out/'output'
    build = subprocess.run(
        [str(args.compiler), 'build-weapon', '--project', str(manifest_path), '-o', str(output)],
        capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=180)
    (args.out/'build.log').write_text(build.stdout+build.stderr, encoding='utf-8')
    if build.returncode:
        raise SystemExit(f'animation binding build failed with exit code {build.returncode}')

    roundtrip = args.out/'roundtrip'
    load = subprocess.run(
        [str(args.acts), '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', str(args.game),
         '-a', 'weapon,animpkg,xanim', '-o', str(roundtrip),
         str(output/(base+'.ff')), str(output/(base+'_common.ff'))],
        cwd=args.acts.parent, capture_output=True, text=True, encoding='utf-8',
        errors='replace', timeout=180)
    (args.out/'roundtrip.log').write_text(load.stdout+load.stderr, encoding='utf-8')
    if load.returncode:
        raise SystemExit(f'animation binding reload failed with exit code {load.returncode}')

    recovered = {}
    for path in roundtrip.rglob('*.asset.json'):
        document = json.loads(path.read_text(encoding='utf-8-sig'))
        pool = document.get('pool')
        fields = document['asset']['fields']
        name = fields['name']['string'] if pool in ('animpkg','xanim') else fields['szInternalName']['string']
        if name in (base+'_mp', package_name, animation_name):
            recovered[(pool, name)] = fields
    weapon = recovered[('weapon', base+'_mp')]
    package_fields = recovered[('animpkg', package_name)]
    animation = recovered[('xanim', animation_name)]
    assert asset_name(weapon['weapDef']['values'][0]['szXAnims']) == package_name
    assert asset_name(package_fields['anims']['values'][239]) == animation_name
    assert animation['numframes'] == 30 and abs(animation['framerate']-30.0) < 1e-6
    assert animation['notifyCount'] == 1
    result = {
        'format': 'replay-animation-binding-check-v1', 'status': 'passed',
        'weapon': base+'_mp', 'package': package_name, 'field': PACKAGE_FIELD,
        'animation': animation_name, 'frames': 31,
        'build_exit': build.returncode, 'reload_exit': load.returncode,
    }
    (args.out/'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
