"""The original frame meshes must keep their measured geometry when exported."""
import json
import unittest
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1] / 'web'


class OriginalFrames(unittest.TestCase):
    def test_all_quality_size_frames_preserve_model_aspect(self):
        data=json.loads((ROOT/'frame-report.json').read_text())['frames']
        self.assertEqual(len(data),15)
        for name,frame in data.items():
            with self.subTest(frame=name),Image.open(ROOT/'frames'/name) as image:
                image.load()
                self.assertLessEqual(abs(image.width-image.height*frame['aspect']),0.51)
                self.assertEqual(image.mode,'RGBA')
                self.assertEqual(image.getpixel((0,0))[3],0)

    def test_small_medium_large_follow_game_slot_geometry(self):
        data=json.loads((ROOT/'frame-report.json').read_text())['frames']
        for quality in ['bronze','silver','gold','diamond','legendary']:
            widths=[data[f'{quality}-{size}.webp']['width'] for size in ['s','m','l']]
            self.assertAlmostEqual(widths[1]/widths[0],2,delta=.03)
            self.assertAlmostEqual(widths[2]/widths[0],3,delta=.04)
