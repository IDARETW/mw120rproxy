"""Regression checks for replacing every rendered base weapon XModel."""
import sys
import tempfile
import unittest
from pathlib import Path
import json

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

from server import import_surface_materials, normalize_import_roots
from weapon_models import model_slots_from_definition, model_slots_from_record


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


if __name__ == '__main__':
    unittest.main()
