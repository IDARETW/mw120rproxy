"""Prepare the single-UV Replay weapon material and its resident RGBA textures.

The local material export supplies the native parameter ABI; the color palette
comes from the model's licensed source art. No stock texture payload is copied.
"""
import argparse
import json
from pathlib import Path
import struct

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--palette', type=Path, required=True)
    parser.add_argument('-o', '--output', type=Path, required=True)
    args = parser.parse_args()
    material = json.loads(args.reference.read_text())['asset']['fields']
    expected = 'm/lit_3_lit_rpl_ta1_804040_1042000000000030_0_1_1_0_0_13814015b_0_0_1_0_0'
    if material['techniqueSet']['name'].lstrip(',') != expected:
        raise ValueError('Reference does not use the verified single-UV model shader')
    if [x['index'] for x in material['textureTable']['values']] != [0, 9, 59]:
        raise ValueError('Reference texture slots differ from the weapon profile')
    info = bytearray(32)
    struct.pack_into('<IIfI', info, 0, 1, 6815744, 0.0, 0x80000)
    struct.pack_into('<BBHBBBBHBB', info, 16, 0, 2, 16, 3, 4, 1, 0, 0, 1, 1)
    constants = bytearray()
    for constant in material['constantTable']['values']:
        constants += struct.pack('<I', constant['index'])
        for value in constant['literal']['v']:
            constants += (bytes.fromhex(value['bits'])[::-1] if isinstance(value, dict)
                          else struct.pack('<f', value))
    buffers = material['constantBufferTable']['values']
    if len(constants) != 80 or len(buffers) != 1 or buffers[0]['psDataSize'] != 64:
        raise ValueError('Unexpected native material constant layout')
    palette = Image.open(args.palette).convert('RGBA')
    color = bytearray(palette.tobytes())
    color[3::4] = bytes([56]) * (palette.width * palette.height)  # Dielectric specular.
    args.output.parent.mkdir(parents=True, exist_ok=True)
    images = []
    for filename, width, height, pixels in (
        ('color_specular.rgba', palette.width, palette.height, color),
        ('normal_gloss.rgba', 1, 1, bytes([128, 128, 255, 96])),
        ('emissive.rgba', 1, 1, bytes([0, 0, 0, 255])),
    ):
        (args.output.parent / filename).write_bytes(pixels)
        images.append(dict(file=filename, width=width, height=height))
    result = dict(format='replay-weapon-material-v1', info=info.hex(),
                  constants=constants.hex(),
                  bufferIndices=material['constantBufferIndex']['bytes'],
                  pixelConstants=buffers[0]['psData']['bytes'], images=images)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(args.output)


if __name__ == '__main__':
    main()
