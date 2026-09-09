import math
import struct
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from verify_replay_map_layout import (
    validate_coverage_state,
    validate_render_asset_order,
    validate_vertex_attributes,
    validate_conversion_sidecars,
    validate_source_atlas_bindings,
    validate_world_model_bounds,
)


class WorldBoundsTests(unittest.TestCase):
    def test_native_bounds_and_radius_are_consistent(self):
        world, model = bytearray(160), bytearray(96)
        struct.pack_into("<7f", model, 0x38, 100, 200, 300, 3, 4, 12, 13)
        struct.pack_into("<6f", world, 0x78, 100, 200, 300, 4, 5, 13)
        validate_world_model_bounds(world, model)
        struct.pack_into("<f", model, 0x50, 12)
        with self.assertRaises(ValueError):
            validate_world_model_bounds(world, model)

    def test_legacy_and_invalid_bounds(self):
        world, model = bytearray(160), bytearray(96)
        validate_world_model_bounds(world, model)
        struct.pack_into("<f", model, 0x38, float("nan"))
        with self.assertRaises(ValueError):
            validate_world_model_bounds(world, model)


def fixture(layers):
    gpu = bytearray(88)
    aux = bytearray()
    struct.pack_into("<I", gpu, 4, layers)
    for offset, values in (
        (12, struct.pack("<3I", 0, 1, 2)),
        (16, struct.pack("<6f", 0, 0, 1, 0, 0, 1)),
        (20, bytes((0, 128, 255, 64, 255, 0, 128, 128, 64, 128, 0, 255))),
    ):
        struct.pack_into("<I", gpu, offset, len(aux))
        aux += values
    struct.pack_into("<I", gpu, 24, len(aux))
    for uv in ((0, 0), (1, 0), (0, 1)):
        values = uv
        if layers >= 2:
            values += (9, 4 | 32 | 64)
        if layers == 4:
            values += (0.8, 4, 2.5, 0.625)
        aux += struct.pack(f"<{layers * 2}f", *values)
    return gpu, aux


class RenderLayoutTests(unittest.TestCase):
    def test_pruned_shader_dependencies_with_three_source_atlases(self):
        validate_render_asset_order([19] * 3 + [14] + [17] * 3 + [18, 11] * 4 + [31, 25])
        validate_render_asset_order([14, 17, 18, 11, 31, 25])

    def test_source_atlases_keep_linear_data_separate_from_color(self):
        bindings = {0: b",color", 9: b",normal", 59: b",response"}
        formats = {b"color": 7, b"normal": 6, b"response": 6}
        validate_source_atlas_bindings(bindings, formats)
        for semantic in (9, 59):
            malformed = dict(formats)
            malformed[bindings[semantic][1:]] = 7
            with self.assertRaisesRegex(ValueError, "linear/sRGB"):
                validate_source_atlas_bindings(bindings, malformed)
        with self.assertRaisesRegex(ValueError, "semantics"):
            validate_source_atlas_bindings({0: b",color"}, formats)
        with self.assertRaisesRegex(ValueError, "alias"):
            validate_source_atlas_bindings({0: b",color", 9: b",normal", 59: b",normal"}, formats)

    def test_dependencies_must_precede_users(self):
        for types in (
            [19, 17, 14, 18, 11, 31, 25],
            [19, 14, 18, 11, 31, 25],
            [19, 14, 17, 18, 11, 19, 31, 25],
            [19, 14, 17, 18, 11, 25, 31],
            [19, 14, 17, 18, 11] * 5 + [31, 25],
        ):
            with self.subTest(types=types), self.assertRaises(ValueError):
                validate_render_asset_order(types)

    def test_owned_masked_depth_shadow_and_color_states(self):
        for count in (2, 4):
            for kind, depth, blend in (
                (0, 0xE00, 0xFFFFFF0F00000000),
                (27, 0xE20, 0xFFFFFFFF00000000),
                (28, 0xE20, 0xFFFFFFFF00000000),
                (34, 0x800, 0),
            ):
                if count == 2 and kind in (27, 28):
                    continue
                te = bytearray(184)
                te[0x9C] = 35
                struct.pack_into("<QQ", te, 0xA0, depth, blend)
                validate_coverage_state(b"tw/mw120r_map_foliage_abcdef", count, kind, te)
                te[0x9C] = 34
                with self.assertRaisesRegex(ValueError, "coverage/depth"):
                    validate_coverage_state(b"tw/mw120r_map_foliage_abcdef", count, kind, te)

    def test_masked_shadows_cannot_write_a_color_target_or_cull_leaf_backs(self):
        te = bytearray(184)
        te[0x9C] = 35
        struct.pack_into("<QQ", te, 0xA0, 0xE20, 0xFFFFFFFF00000000)
        te[0x82] = 1
        with self.assertRaisesRegex(ValueError, "no color target"):
            validate_coverage_state(b"tw/mw120r_map_foliage_abcdef", 4, 27, te)
        te[0x82] = 0
        te[0xA0] |= 1
        with self.assertRaisesRegex(ValueError, "coverage/depth"):
            validate_coverage_state(b"tw/mw120r_map_foliage_abcdef", 4, 27, te)

    def test_all_three_vertex_layouts_accept_rgba8_and_source_parameters(self):
        for layers in (1, 2, 4):
            with self.subTest(layers=layers):
                gpu, aux = fixture(layers)
                validate_vertex_attributes(gpu, aux, 3)

    def test_color_stream_must_fit(self):
        gpu, aux = fixture(4)
        struct.pack_into("<I", gpu, 20, len(aux) - 4)
        with self.assertRaisesRegex(ValueError, "exceeds buffer"):
            validate_vertex_attributes(gpu, aux, 3)

    def test_stream_overlap_and_invalid_layer_count_are_rejected(self):
        gpu, aux = fixture(4)
        struct.pack_into("<I", gpu, 24, 4)
        with self.assertRaisesRegex(ValueError, "overlap"):
            validate_vertex_attributes(gpu, aux, 3)
        struct.pack_into("<I", gpu, 4, 3)
        with self.assertRaisesRegex(ValueError, "layers"):
            validate_vertex_attributes(gpu, aux, 3)

    def test_metadata_and_source_parameters_are_flat_and_finite(self):
        for component, value, message in (
            (2, 9.5, "tile or material"),
            (3, 256, "flags"),
            (4, math.nan, "Nonfinite"),
            (4, 1e8, "parameters"),
        ):
            gpu, aux = fixture(4)
            uv = struct.unpack_from("<I", gpu, 24)[0]
            struct.pack_into("<f", aux, uv + component * 4, value)
            with self.subTest(component=component), self.assertRaisesRegex(ValueError, message):
                validate_vertex_attributes(gpu, aux, 3)
        for component in (2, 4):
            gpu, aux = fixture(4)
            uv = struct.unpack_from("<I", gpu, 24)[0]
            struct.pack_into("<f", aux, uv + 32 + component * 4, 2)
            with self.assertRaisesRegex(ValueError, "within a triangle"):
                validate_vertex_attributes(gpu, aux, 3)

    def test_merged_surface_may_use_different_metadata_on_separate_triangles(self):
        gpu, aux = fixture(4)
        uv = struct.unpack_from("<I", gpu, 24)[0]
        struct.pack_into("<f", aux, uv + 32 + 3 * 4, 32)
        validate_vertex_attributes(gpu, aux, 3, (0, 0, 0, 1, 1, 1, 2, 2, 2))
        with self.assertRaisesRegex(ValueError, "within a triangle"):
            validate_vertex_attributes(gpu, aux, 3, (0, 1, 2))

    def test_baked_lightmap_coordinates_cannot_address_outside_atlas(self):
        gpu, aux = fixture(4)
        lm = struct.unpack_from("<I", gpu, 16)[0]
        struct.pack_into("<f", aux, lm, 1.1)
        with self.assertRaisesRegex(ValueError, "exceeds its atlas"):
            validate_vertex_attributes(gpu, aux, 3)

    def test_collision_version_matches_manifest(self):
        with TemporaryDirectory() as folder:
            package = Path(folder)
            hull = struct.pack("<12f", 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1)
            for contract, magic, record in (
                ("boxes-v1", b"MWCOLL01", struct.pack("<6f", 0, 0, 0, 1, 1, 1)),
                ("convex-v2", b"MWCOLL02", struct.pack("<I", 4) + hull),
                ("convex-v3", b"MWCOLL03", struct.pack("<II", 4, 1) + hull),
            ):
                (package / "collision.bin").write_bytes(magic + struct.pack("<I", 1) + record)
                self.assertEqual(
                    validate_conversion_sidecars(package, {"collision": contract})[
                        "collision_hulls"
                    ],
                    1,
                )
                with self.assertRaisesRegex(ValueError, "version"):
                    validate_conversion_sidecars(
                        package,
                        {"collision": "convex-v3" if contract != "convex-v3" else "convex-v2"},
                    )

    def test_spatial_ambient_is_validated_and_reported(self):
        with TemporaryDirectory() as folder:
            package = Path(folder)
            path = package / "ambient_grid.bin"
            path.write_bytes(
                b"MWLGRID1" + struct.pack("<II3iHBB3f", 1, 1, 0, 0, 0, 0, 0, 0, 0.1, 0.2, 0.3)
            )
            result = validate_conversion_sidecars(package, {"ambient_grid": "spatial-dc-v1"})
            self.assertEqual(result["ambient_grid"]["cells"], 1)
            self.assertEqual(result["ambient_grid"]["palette_colors"], 1)
            with self.assertRaisesRegex(ValueError, "manifest"):
                validate_conversion_sidecars(package, {})
            path.write_bytes(path.read_bytes()[:-1])
            with self.assertRaisesRegex(ValueError, "byte count"):
                validate_conversion_sidecars(package, {"ambient_grid": "spatial-dc-v1"})


if __name__ == "__main__":
    unittest.main()
