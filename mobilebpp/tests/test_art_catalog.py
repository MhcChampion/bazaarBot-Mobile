"""Regression cases for game ArtKey aliases and malformed catalog entries."""
import importlib.util
import struct
import unittest
from types import SimpleNamespace
from PIL import Image
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('extract_art', ROOT / 'tools/extract_art.py')
art = importlib.util.module_from_spec(spec)
spec.loader.exec_module(art)


class Catalog:
    def __init__(self, data):
        self.data = data

    def namelist(self):
        return ['assets/aa/Android/catalog_test.bin']

    def read(self, _):
        return self.data


class ArtAliases(unittest.TestCase):
    def test_diffuse_effect_mask_does_not_erase_item_when_resized(self):
        texture=SimpleNamespace(image=Image.new('RGBA',(512,512),(180,90,35,0)))
        image=art.card_illustration(texture)
        self.assertEqual(image.mode,'RGB')
        self.assertEqual(image.size,(320,320))
        self.assertEqual(image.getpixel((160,160)),(180,90,35))

    def test_art_key_maps_renamed_card_without_using_template_guid(self):
        key = 'b32b472b008f4ca4fa843071a27b4bb6'
        asset = b'BladedBass_CardData.asset'
        data = b'prefix' + struct.pack('<I', 32) + key.encode() + struct.pack('<I', len(asset)) + asset
        self.assertEqual(art.addressable_names(Catalog(data), {'new-template-id': {'ArtKey': key}}),
                         {'bladedbass_carddata.asset': ['new-template-id']})

    def test_truncated_catalog_does_not_create_false_asset_match(self):
        key = 'b32b472b008f4ca4fa843071a27b4bb6'
        data = struct.pack('<I', 32) + key.encode() + struct.pack('<I', 6000)
        self.assertEqual(art.addressable_names(Catalog(data), {'id': {'ArtKey': key}}), {})

    def test_icon_name_is_not_interpreted_as_guid(self):
        self.assertEqual(art.addressable_names(Catalog(b'Icon_Skill_Test'), {'id': {'ArtKey': 'Icon_Skill_Test'}}), {})


if __name__ == '__main__':
    unittest.main()
