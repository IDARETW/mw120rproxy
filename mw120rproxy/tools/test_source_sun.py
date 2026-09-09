import math
import unittest

from map_lighting import sun, sun_settings


class SourceSunTests(unittest.TestCase):
    def test_original_energy_is_preserved_above_old_cap(self):
        direction, color = sun({"sundirection": "-18 100 0", "suncolor": ".9 .9 1", "sunlight": "3"})
        settings = sun_settings(direction, color)
        self.assertEqual(settings["intensity"], 3)
        for original, channel in zip(color, settings["color"]):
            self.assertAlmostEqual(original, channel * settings["intensity"])
        self.assertAlmostEqual(sum(v * v for v in settings["direction"]), 1)
        self.assertEqual(settings["up"], [0, 0, 0])

    def test_dark_maps_do_not_acquire_an_artificial_sun(self):
        self.assertEqual(sun_settings([0, 0, 1], [0, 0, 0])["intensity"], 0)
        self.assertEqual(sun_settings([0, 0, 1], [.01, .02, .03])["intensity"], .03)

    def test_invalid_light_values_are_rejected(self):
        for direction, color in (([0, 0, 0], [1, 1, 1]), ([0, 0, 1], [-1, 1, 1]),
                                 ([0, math.nan, 1], [1, 1, 1]), ([0, 0, 1], [1, math.inf, 1])):
            with self.assertRaises(ValueError):
                sun_settings(direction, color)
