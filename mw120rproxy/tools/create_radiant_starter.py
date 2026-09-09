"""Create a separate starter; leave the supplied mp_test.map untouched."""

from pathlib import Path
from radiant_source import blocks, properties
from prepare_cod4_map import read_bsp
from build_mp_test import COD4


def box(lo, hi):
    # Same outward-plane convention as the supplied Radiant source.
    x, y, z = lo
    X, Y, Z = hi
    planes = [
        [(x, y, z), (X, y, z), (X, Y, z)],
        [(x, y, Z), (X, Y, Z), (X, y, Z)],
        [(x, y, z), (x, Y, z), (x, Y, Z)],
        [(X, y, z), (X, Y, Z), (X, Y, z)],
        [(x, y, z), (X, y, Z), (X, y, z)],
        [(x, Y, z), (X, Y, z), (X, Y, Z)],
    ]
    return (
        "{\n"
        + "\n".join(
            " ".join("( " + " ".join(map(str, p)) + " )" for p in plane)
            + " clip 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0"
            for plane in planes
        )
        + "\n}\n"
    )


def create():
    source = COD4 / "map_source/mp_test.map"
    target = source.with_name("mp_test_mw2019.map")
    if target.exists():
        raise FileExistsError(f"Preserving existing authored map: {target}")
    original = source.read_text()
    world = blocks(original)[0]
    # A pair of opaque stock props with explicit simple clip hulls. Their model
    # collision trees are not imported; mappers control the clip independently.
    clips = box([-372, -416, 0], [-348, -392, 24]) + box([-208, -424, 0], [-160, -392, 28])
    world = world[:-1] + clips + "}\n"
    bsp = COD4 / "raw/maps/mp/mp_test.d3dbsp"
    lumps, _ = read_bsp(bsp.read_bytes())
    entities = []
    for e in blocks(lumps[39].rstrip(b"\0").decode("ascii")):
        cls = properties(e).get("classname", "")
        if cls == "info_player_start" or cls.startswith("mp_tdm_spawn"):
            entities.append(e)
    entities += [
        '{\n"classname" "misc_model"\n"model" "ch_crate24x24"\n"origin" "-360 -404 0"\n"angles" "0 0 0"\n}',
        '{\n"classname" "misc_model"\n"model" "com_bunkercrate"\n"origin" "-184 -408 0"\n"angles" "0 0 0"\n}',
    ]
    target.write_text(
        'iwmap 4\n"000_Global" flags active\n"The Map" flags\n'
        + world
        + "\n".join(entities)
        + "\n",
        encoding="ascii",
    )
    print(target)


if __name__ == "__main__":
    create()
