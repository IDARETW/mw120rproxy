"""Regression checks for replacing every rendered base weapon XModel."""
import sys
import tempfile
import unittest
from pathlib import Path
import json
import gzip
import struct

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

from server import import_surface_materials, normalize_import_roots
from weapon_models import model_slots_from_definition, model_slots_from_record
from preview_glb import obj_to_glb
from material import ensure_previews


class WeaponModelSlotTests(unittest.TestCase):
    def setUp(self):
        self.fixups = [
            {'field': 'weapon.weapDef[0].gunXModel', 'kind': 'asset',
             'asset_type': 9, 'name': 'view_main'},
            {'field': 'weapon.weapDef[0].defaultViewModel', 'kind': 'asset',
             'asset_type': 9, 'name': 'view_stream'},
            {'field': 'weapon.weapDef[0].gunXModelLeftHand', 'kind': 'asset',
             'asset_type': 9, 'name': 'view_left'},
            {'field': 'weapon.weapDef[0].gunXModelRightHand', 'kind': 'asset',
             'asset_type': 9, 'name': 'view_right'},
            {'field': 'weapon.weapDef[0].worldModel', 'kind': 'asset',
             'asset_type': 9, 'name': 'world_main'},
            {'field': 'weapon.weapDef[0].worldXModelLeftHand', 'kind': 'asset',
             'asset_type': 9, 'name': 'world_left'},
            {'field': 'weapon.weapDef[0].worldXModelRightHand', 'kind': 'asset',
             'asset_type': 9, 'name': 'world_right'},
            {'field': 'weapon.weapDef[0].defaultWorldModel', 'kind': 'asset',
             'asset_type': 9, 'name': 'world_stream'},
            {'field': 'weapon.weapDef[0].defaultWorldModelLeftHand',
             'kind': 'asset', 'asset_type': 9, 'name': 'world_stream_left'},
            {'field': 'weapon.weapDef[0].defaultWorldModelRightHand',
             'kind': 'asset', 'asset_type': 9, 'name': 'world_stream_right'},
            {'field': 'weapon.weapDef[0].censorshipWorldModel', 'kind': 'asset',
             'asset_type': 9, 'name': 'world_censorship'},
            {'field': 'weapon.weapDef[0].censorshipWorldModelLeftHand',
             'kind': 'asset', 'asset_type': 9, 'name': 'world_censorship_left'},
            {'field': 'weapon.weapDef[0].censorshipWorldModelRightHand',
             'kind': 'asset', 'asset_type': 9, 'name': 'world_censorship_right'},
            {'field': 'weapon.weapDef[0].handXModel', 'kind': 'asset',
             'asset_type': 9, 'name': 'stock_hands'},
            {'field': 'weapon.weapDef[0].worldClipModel', 'kind': 'asset',
             'asset_type': 9, 'name': 'stock_clip'},
            {'field': 'weapon.weapDef[0].projectileModel', 'kind': 'asset',
             'asset_type': 9, 'name': 'stock_projectile'},
        ]
        self.root = {'fixups': [
            {'field': 'weapon.weapDef', 'kind': 'record', 'fixups': self.fixups}
        ]}

    def test_slot_scan_includes_stream_fallbacks_and_hand_variants(self):
        self.assertEqual(model_slots_from_record(self.root), {
            'view_model': ['view_main', 'view_stream', 'view_left', 'view_right'],
            'world_model': ['world_main', 'world_left', 'world_right', 'world_stream',
                            'world_stream_left', 'world_stream_right', 'world_censorship',
                            'world_censorship_left', 'world_censorship_right'],
        })

    def test_stock_inspection_rig_is_upgraded_when_custom_geometry_is_imported(self):
        project = {
            'reference_name': 'iw8_ar_example_mp',
            'category': 'weapon_assault',
            'reference': {'root': self.root},
        }
        rig = {
            'view_model': {'bones': ['j_gun'], 'replace': ['view_main']},
            'world_model': {'bones': ['j_gun'], 'replace': ['world_main']},
        }

        normalize_import_roots(project, rig)

        self.assertEqual(rig['view_model']['replace'],
                         ['view_main', 'view_stream', 'view_left', 'view_right'])
        self.assertEqual(rig['world_model']['replace'],
                         ['world_main', 'world_left', 'world_right', 'world_stream',
                          'world_stream_left', 'world_stream_right', 'world_censorship',
                          'world_censorship_left', 'world_censorship_right'])

    def test_catalog_definition_exports_the_same_view_and_world_slots(self):
        slots = model_slots_from_definition({
            'gunXModel': {'name': ',view_main'},
            'defaultViewModel': {'name': ',view_stream'},
            'defaultWorldModelLeftHand': {'name': ',world_left'},
            'worldModel': {'name': ',world_main'},
            'handXModel': {'name': ',stock_hands'},
        })
        self.assertEqual(slots, {
            'view_model': ['view_main', 'view_stream'],
            'world_model': ['world_main', 'world_left'],
        })


class AttachmentMaterialTests(unittest.TestCase):
    def test_attachment_materials_pack_images_under_an_owned_unique_folder(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            Image.new('RGBA', (2, 2), (220, 40, 30, 255)).save(root/'paint.png')
            profile = root/'profile.json'
            profile.write_text(json.dumps({'format':'replay-weapon-material-v1','images':[
                {'file':'color.rgba','width':1,'height':1},
                {'file':'normal.rgba','width':1,'height':1},
                {'file':'emissive.rgba','width':1,'height':1},
            ]}), encoding='utf-8')
            result = import_surface_materials(root, [{
                'key': 'receiver', 'name': 'Receiver', 'parts': ['receiver_body'],
                'maps': {'color_texture': 'paint.png'},
            }], ['receiver_body'], profile, 'attachment-a1b2c3')
            self.assertEqual(len(result), 1)
            definition = root/result[0]['definition']
            self.assertEqual(definition.parent.name, 'attachment-a1b2c3-receiver')
            self.assertTrue((definition.parent/'color_specular.rgba').is_file())
            self.assertEqual(result[0]['parts'], ['receiver_body'])


class PreviewAssetTests(unittest.TestCase):
    def test_obj_preview_keeps_mesh_names_and_faces(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source = root/'weapon.obj'
            source.write_text(
                'v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n'
                'vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n'
                'g receiver\nf 1/1 2/2 3/3 4/4\n'
                'g trigger\nf 1/1 2/2 3/3\n', encoding='utf-8')
            preview = root/'weapon.preview.glb'
            result = obj_to_glb(source, preview)
            content = preview.read_bytes()
            self.assertEqual(content[:4], b'glTF')
            self.assertEqual(struct.unpack_from('<I', content, 8)[0], len(content))
            json_size = struct.unpack_from('<I', content, 12)[0]
            scene = json.loads(content[20:20+json_size])
            self.assertEqual([mesh['name'] for mesh in scene['meshes']],
                             ['receiver', 'trigger'])
            self.assertEqual(result['triangles'], 3)

    def test_texture_preview_is_downsized_without_changing_native_image(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            original = bytes([12, 34, 56, 255]) * (4 * 2)
            (root/'color.rgba').write_bytes(original)
            definition = root/'material.json'
            definition.write_text(json.dumps({
                'format': 'replay-weapon-material-v1',
                'images': [{'file': 'color.rgba', 'width': 4, 'height': 2}],
            }), encoding='utf-8')
            ensure_previews(definition, max_dimension=2)
            metadata = json.loads(definition.read_text(encoding='utf-8'))
            self.assertEqual(metadata['preview_images'][0]['width'], 2)
            self.assertEqual(metadata['preview_images'][0]['height'], 1)
            self.assertEqual(len(gzip.decompress(
                (root/'color.rgba.preview.gz').read_bytes())), 2 * 1 * 4)
            self.assertEqual((root/'color.rgba').read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
