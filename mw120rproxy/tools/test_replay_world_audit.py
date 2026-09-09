import copy
import unittest

from audit_replay_world import audit


class ReplayWorldAuditTests(unittest.TestCase):
    def fixture(self):
        surface = {
            "tris": {"posOffset": 4, "vertexCount": 3, "triCount": 1, "baseIndex": 0},
            "surfDataIndex": 0,
            "transientZone": 0,
        }
        world = {
            "name": {"string": "test"},
            "primaryLightCount": 2,
            "umbraTomeSize": 0,
            "surfaces": {
                "count": 2,
                "surfDataCount": 1,
                "surfaces": {"values": [surface, copy.deepcopy(surface)]},
                "surfData": {
                    "values": [
                        {
                            "xyzOffset": 4,
                            "tangentFrameOffset": 4,
                            "texCoordOffset": 16,
                            "lmapCoordOffset": 40,
                            "layerCount": 1,
                        }
                    ]
                },
            },
            "draw": {"lightmapCount": 0, "reflectionProbeData": {"reflectionProbeCount": 0}},
        }
        transient = {
            "transientZoneIndex": 0,
            "gpuLightGrid": {"gpuLightGrid": None},
            "drawVerts": {
                "posDataSize": 40,
                "auxDataSize": 64,
                "indexCount": 3,
                "indices": {"bytes": "000001000200"},
            },
        }
        return world, transient

    def test_shared_vertex_block_is_valid(self):
        result = audit(*self.fixture())
        self.assertEqual(result["issues"], [])
        self.assertEqual(result["shared_vertex_blocks"], 1)

    def test_out_of_block_index_is_rejected(self):
        world, transient = self.fixture()
        transient["drawVerts"]["indices"]["bytes"] = "000001000300"
        self.assertTrue(any("index exceeds" in x for x in audit(world, transient)["issues"]))

    def test_vertex_shader_position_base_must_match(self):
        world, transient = self.fixture()
        world["surfaces"]["surfData"]["values"][0]["xyzOffset"] = 8
        self.assertTrue(
            any("position base mismatch" in x for x in audit(world, transient)["issues"])
        )


if __name__ == "__main__":
    unittest.main()
