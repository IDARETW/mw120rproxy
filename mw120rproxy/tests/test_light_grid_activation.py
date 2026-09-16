import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from verify_replay_map_layout import validate_light_grid_activation


class LightGridActivationTests(unittest.TestCase):
    def test_resident_payload_requires_single_grid_mode(self):
        world = bytearray(0x4590)
        struct.pack_into("<f", world, 0x37F0, 1.0)
        transient = bytearray(0x148)
        struct.pack_into("<Q", transient, 0xF8, 2**64 - 2)
        with self.assertRaisesRegex(ValueError, "lightGridType"):
            validate_light_grid_activation(world, transient)
        world[0x7C0] = 1
        self.assertEqual(validate_light_grid_activation(world, transient), "single")

    def test_empty_world_cannot_advertise_a_grid(self):
        world = bytearray(0x4590)
        struct.pack_into("<f", world, 0x37F0, 1.0)
        transient = bytearray(0x148)
        self.assertEqual(validate_light_grid_activation(world, transient), "none")
        world[0x7C0] = 1
        with self.assertRaisesRegex(ValueError, "lightGridType"):
            validate_light_grid_activation(world, transient)

    def test_secondary_diffuse_cannot_be_multiplied_by_zero(self):
        world = bytearray(0x4590)
        transient = bytearray(0x148)
        world[0x7C0] = 1
        struct.pack_into("<Q", transient, 0xF8, 2**64 - 2)
        for scale in (0.0, -1.0, float("nan"), float("inf")):
            struct.pack_into("<f", world, 0x37F0, scale)
            with self.assertRaisesRegex(ValueError, "bakedLightScale"):
                validate_light_grid_activation(world, transient)


if __name__ == "__main__":
    unittest.main()
