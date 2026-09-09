import unittest

from map_lighting import coordinates


class LightmapCoordinatesTests(unittest.TestCase):
    def test_source_texel_centers_survive_atlas_translation(self):
        for width, height in ((4, 8), (1024, 1024)):
            for col, row in ((0, 0), (1, 2), (width - 1, height - 1)):
                result = coordinates(
                    (512, 2048, width, height), ((col + 0.5) / width, (row + 0.5) / height)
                )
                self.assertEqual(result, [(512 + col + 0.5) / 4096, (2048 + row + 0.5) / 4096])

    def test_clamp_does_not_sample_adjacent_atlas_tiles(self):
        self.assertEqual(coordinates((512, 2048, 4, 8), (-1, 2)), [512.5 / 4096, 2055.5 / 4096])


if __name__ == "__main__":
    unittest.main()
