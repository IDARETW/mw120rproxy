import copy
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import door_data
from imported_map_assets import packed_normal


class DoorImportTests(unittest.TestCase):
    def fixture(self):
        entities = [
            {
                "classname": "script_brushmodel",
                "targetname": "door1",
                "model": "*1",
                "origin": "10 20 0",
            },
            {"classname": "script_origin", "targetname": "door1_ori", "origin": "10 -10 0"},
        ]
        world = {"brush_models": [{}, {"bounds": [[-2, -30, 0], [2, 30, 80]]}]}
        script = 'd=getEnt("door1","targetname"); p=getEnt("door1_ori","targetname"); d linkto(p); p rotateYaw(90, 3);'
        return entities, world, script

    def test_authored_pivot_and_collision_move_together(self):
        entities, world, script = self.fixture()
        doors = door_data.discover(entities, world, script)
        self.assertEqual(doors[0]["pivot"], [10, -10, 0])
        brush = {"mins": [-2, -30, 0], "maxs": [2, 30, 80], "planes": []}
        door_data.add_hull(doors[0], brush, entities[0])
        self.assertEqual(doors[0]["hulls"][0][0], [1, 0, 0, 12])
        surface = {
            "door": 0,
            "material": "wood",
            "indices": [0, 1, 2],
            "vertices": [
                {"position": p, "normal": packed_normal([1, 0, 0]), "uv": [0, 0]}
                for p in ([12, -10, 0], [12, 50, 0], [12, 50, 80])
            ],
        }
        poses = door_data.poses([surface], doors)
        self.assertEqual(len(poses), 49)
        self.assertEqual(poses[24]["vertices"][1]["position"], [12, 50, 0])
        self.assertAlmostEqual(poses[0]["vertices"][1]["position"][0], 70)
        data = door_data.encode(doors, poses)
        self.assertEqual(
            door_data.validate(data), {"doors": 1, "surfaces": 49, "collision_hulls": 1}
        )
        for corrupt in (data[:-1], data + b"x", data[:16] + b"x"):
            with self.assertRaises(ValueError):
                door_data.validate(corrupt)
        bad = bytearray(data)
        struct.pack_into("<I", bad, len(bad) - 4, 999)
        with self.assertRaises(ValueError):
            door_data.validate(bad)

    def test_static_props_and_comments_are_not_doors(self):
        entities, world, script = self.fixture()
        self.assertEqual(door_data.discover(entities, world, "// " + script), [])
        entities[0]["classname"] = "script_model"
        self.assertEqual(door_data.discover(entities, world, script), [])

    def test_sliding_gate_preserves_travel(self):
        entities, world, _ = self.fixture()
        doors = door_data.discover(
            entities, world, 'd=getEnt("door1","targetname"); d moveZ(-700,6);'
        )
        self.assertEqual(doors[0]["travel"], [0, 0, -700])
        self.assertEqual(doors[0]["frames"], 25)
        self.assertEqual(door_data.point([10, 20, 80], doors[0], 1), [10, 20, -620])

    def test_ambiguous_motion_is_rejected(self):
        entities, world, script = self.fixture()
        with self.assertRaises(ValueError):
            door_data.discover(entities, world, script + "p moveZ(100,1);")


if __name__ == "__main__":
    unittest.main()
