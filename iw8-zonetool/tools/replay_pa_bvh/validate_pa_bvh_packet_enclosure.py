#!/usr/bin/env python3
r"""Validate Replay 1.20 PA SIMD leaves against decoded static-mesh packets.

The validator reads exported PhysicsAsset JSON/TAG0 only. It does not launch
the game, converter, or a build. The PE is read solely to verify the exact
getNextKey implementation that defines the serialized leaf-key mapping.

Example (PowerShell):
  python .\validate_pa_bvh_packet_enclosure.py `
    --replay-pe E:\IW8\Builds\1.20-replay\game_dx12_ship_replay.exe `
    --office-assets E:\mw124-conversion-builds\pa-all-quantization-1\mw19replay\srv_mp_4doffice\assets\physicsasset\mw120r\mp_4doffice `
    --office-fastfile E:\mw124-conversion-builds\office-mesh-quantization-1\srv_mp_4doffice.ff `
    --office-manifest E:\mw124-conversion-builds\pa-all-quantization-1\mw19replay\srv_mp_4doffice\manifest.json `
    --stock-control D:\mw124\mw120rproxy\evidence\native-physics\shipment-physicsassets\mw19replay\mp_shipment\assets\physicsasset\roof_vent_chimney_02.126.asset.json `
    --stock-fastfile E:\IW8\Builds\1.20-replay\mp_shipment.ff `
    --stock-manifest D:\mw124\mw120rproxy\evidence\native-physics\shipment-physicsassets\mw19replay\mp_shipment\manifest.json
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import struct
import sys
from pathlib import Path


OFFICE_SHA256 = "695A7F34EFA230A0817E513DBC2150B64BB4190EECD9D01240BD99A31B4FE4AC"
STOCK_SHA256 = "F03A663C5A16ABD69EE4ACF95ED922F899278EB7479DBAD81EB658373ABEBC17"
STOCK_CONTROL_JSON_SHA256 = "1FAF20DA5CAA27F6FF3F8420C800AA619FB1F3E6CC86E87753175AB804216EAE"
REPLAY_PE_SHA256 = "68FB1CBCB2924182724004039DE55A4C50152BB6561803C4898B7930B38132F0"
GET_NEXT_KEY_RVA = 0x1E2DD90
GET_NEXT_KEY_SIZE = 191
GET_NEXT_KEY_MD5 = "f42049d10a12571c283f873ec9ff8801"
NODE_STRIDE = 0x80
SECTION_STRIDE = 96
UNUSED_KEY = 0xFFFFFFFF


def load_tag0_parser():
    path = Path(__file__).with_name("validate_shelf_bvh_coverage.py")
    spec = importlib.util.spec_from_file_location("replay_tag0_parser", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load TAG0 parser: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def fail(message: str) -> None:
    raise ValueError(message)


def one_array(items, type_id: int, label: str, required: bool = True):
    found = [(offset, count) for tid, offset, count in items if tid == type_id]
    if len(found) == 1:
        return found[0]
    if not required and not found:
        return None
    fail(f"{label}: expected one type-{type_id} array, found {found}")


def check_range(data: bytes, offset: int, count: int, width: int, label: str) -> None:
    if offset < 0 or count < 0 or width <= 0 or offset + count * width > len(data):
        fail(f"{label}: array exceeds TAG0 data")


def pe_file_offset(pe: bytes, rva: int, size: int) -> int:
    if len(pe) < 0x40 or pe[:2] != b"MZ":
        fail("Replay PE has no DOS header")
    pe_header = struct.unpack_from("<I", pe, 0x3C)[0]
    if pe_header + 24 > len(pe) or pe[pe_header:pe_header + 4] != b"PE\0\0":
        fail("Replay PE has no valid PE header")
    section_count = struct.unpack_from("<H", pe, pe_header + 6)[0]
    optional_size = struct.unpack_from("<H", pe, pe_header + 20)[0]
    table = pe_header + 24 + optional_size
    for index in range(section_count):
        record = table + 40 * index
        if record + 40 > len(pe):
            fail("Replay PE section table is truncated")
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
            "<IIII", pe, record + 8
        )
        extent = max(virtual_size, raw_size)
        if virtual_address <= rva and rva + size <= virtual_address + extent:
            delta = rva - virtual_address
            if delta + size > raw_size or raw_offset + delta + size > len(pe):
                fail("getNextKey RVA is not fully present in PE raw section data")
            return raw_offset + delta
    fail(f"getNextKey RVA 0x{rva:x} is outside PE sections")


def verify_pe(path: Path) -> dict:
    actual_sha = sha256_file(path)
    if actual_sha != REPLAY_PE_SHA256:
        fail(f"Replay PE SHA-256 mismatch: {actual_sha}")
    pe = path.read_bytes()
    offset = pe_file_offset(pe, GET_NEXT_KEY_RVA, GET_NEXT_KEY_SIZE)
    raw = pe[offset:offset + GET_NEXT_KEY_SIZE]
    actual_md5 = hashlib.md5(raw).hexdigest()
    if actual_md5 != GET_NEXT_KEY_MD5:
        fail(f"getNextKey bytes hash mismatch: {actual_md5}")
    return {
        "path": str(path.resolve()),
        "sha256": actual_sha,
        "function": "hkcdStaticMeshTree::Base::getNextKey",
        "rva": f"0x{GET_NEXT_KEY_RVA:X}",
        "size": GET_NEXT_KEY_SIZE,
        "raw_bytes_md5": actual_md5,
        "verified_mapping": "section=key>>8; local=(key&0xff)>>1; packet=Section[section].firstPrimitive+local",
    }


def verify_export_manifest(manifest_path: Path, fastfile_path: Path,
                           expected_sha256: str, label: str) -> dict:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not manifest.get("complete") or not manifest.get("success") or manifest.get("test_only"):
        fail(f"{label}: export manifest is incomplete, unsuccessful, or test-only")
    if manifest.get("failed", 0) != 0 or manifest.get("unavailable", 0) != 0:
        fail(f"{label}: export manifest reports failures or unavailable assets")
    listed = manifest.get("fastfile")
    if not listed or Path(listed).resolve() != fastfile_path.resolve():
        fail(f"{label}: manifest fastfile path does not match supplied fastfile")
    actual_sha = sha256_file(fastfile_path)
    if actual_sha != expected_sha256:
        fail(f"{label}: fastfile SHA-256 mismatch: {actual_sha}")
    return {
        "manifest": str(manifest_path.resolve()),
        "fastfile": str(fastfile_path.resolve()),
        "fastfile_sha256": actual_sha,
        "complete": manifest["complete"],
        "success": manifest["success"],
        "test_only": manifest["test_only"],
        "tested": manifest.get("tested"),
        "failed": manifest.get("failed", 0),
        "applied_patches": manifest.get("applied_patches", []),
    }


def lane_bounds(data: bytes, node_offset: int, node: int, lane: int):
    vectors = [
        struct.unpack_from("<4f", data, node_offset + node * NODE_STRIDE + 16 * axis)
        for axis in range(6)
    ]
    return tuple(vector[lane] for vector in vectors)


def valid_box(box) -> bool:
    return all(math.isfinite(value) for value in box) and all(
        box[2 * axis] <= box[2 * axis + 1] for axis in range(3)
    )


def valid_section_bounds(low, high) -> bool:
    return all(math.isfinite(value) for value in (*low, *high)) and all(
        low[axis] <= high[axis] for axis in range(3)
    )


def encloses(outer, inner, tolerance: float) -> bool:
    return all(
        outer[2 * axis] - tolerance <= inner[2 * axis]
        and outer[2 * axis + 1] + tolerance >= inner[2 * axis + 1]
        for axis in range(3)
    )


def contains_point(box, point, tolerance: float) -> bool:
    return all(
        box[2 * axis] - tolerance <= point[axis] <= box[2 * axis + 1] + tolerance
        for axis in range(3)
    )


def decode_asset(path: Path, parser, tolerance: float) -> dict:
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("format") != "mw19-asset-json" or document.get("format_version") != 1:
        fail(f"{path.name}: unexpected export JSON format")
    if document.get("pool") != "physicsasset" or document.get("root_type") != "PhysicsAsset":
        fail(f"{path.name}: export is not a PhysicsAsset")
    if any(document.get(key, 0) for key in (
        "read_errors", "unresolved_pointers", "unresolved_unions", "external_payloads"
    )):
        fail(f"{path.name}: export reports unread or unresolved data")
    data = parser.get_tag0(path)
    items = parser.find_items(data)
    section_offset, section_count = one_array(items, 548, path.name)
    primitive_offset, primitive_count = one_array(items, 550, path.name)
    packed_item = one_array(items, 59, path.name)
    packed_offset, packed_count = packed_item
    node_offset, node_count = one_array(items, 285, path.name)
    remap_item = one_array(items, 34, path.name, required=False)
    shared_item = one_array(items, 31, path.name, required=False)
    codec_arrays = [(offset, count) for tid, offset, count in items if tid == 524]
    if len(codec_arrays) != section_count:
        fail(f"{path.name}: type-524 arrays do not match section count {section_count}")
    check_range(data, section_offset, section_count, SECTION_STRIDE, f"{path.name}: Section")
    check_range(data, primitive_offset, primitive_count, 4, f"{path.name}: Primitive")
    check_range(data, packed_offset, packed_count, 4, f"{path.name}: packed vertex")
    check_range(data, node_offset, node_count, NODE_STRIDE, f"{path.name}: SIMD node")

    remap_offset = remap_count = shared_offset = shared_count = None
    if remap_item is not None:
        remap_offset, remap_count = remap_item
        check_range(data, remap_offset, remap_count, 2, f"{path.name}: shared remap")
    if shared_item is not None:
        shared_offset, shared_count = shared_item
        check_range(data, shared_offset, shared_count, 8, f"{path.name}: shared vertex")
    if (remap_item is None) != (shared_item is None):
        fail(f"{path.name}: only one of shared-remap/type-31 arrays is present")

    sections = []
    for ordinal in range(section_count):
        base = section_offset + SECTION_STRIDE * ordinal
        packed_start, remap_start, first_primitive = struct.unpack_from("<3I", data, base + 72)
        packed_vertices = data[base + 88]
        section_primitives = data[base + 89]
        low = struct.unpack_from("<3f", data, base + 16)
        high = struct.unpack_from("<3f", data, base + 32)
        packed_min = struct.unpack_from("<3f", data, base + 48)
        packed_scale = struct.unpack_from("<3f", data, base + 60)
        if section_primitives > 128:
            fail(f"{path.name}: section {ordinal} exceeds the 7-bit local primitive key range")
        if packed_start + packed_vertices > packed_count:
            fail(f"{path.name}: section {ordinal} packed vertices exceed type 59")
        if first_primitive + section_primitives > primitive_count:
            fail(f"{path.name}: section {ordinal} primitive range exceeds type 550")
        sections.append({
            "ordinal": ordinal,
            "packed_start": packed_start,
            "remap_start": remap_start,
            "first_primitive": first_primitive,
            "packed_vertices": packed_vertices,
            "primitive_count": section_primitives,
            "low": low,
            "high": high,
            "packed_min": packed_min,
            "packed_scale": packed_scale,
        })
        if codec_arrays[ordinal][1] != 2 * section_primitives - 1:
            fail(f"{path.name}: section {ordinal} type-524 count is not 2P-1")

    packet_ranges = sorted(
        (section["first_primitive"], section["first_primitive"] + section["primitive_count"], section["ordinal"])
        for section in sections
    )
    cursor = 0
    for start, end, ordinal in packet_ranges:
        if start != cursor:
            fail(f"{path.name}: section {ordinal} primitive range is not contiguous at {cursor}")
        cursor = end
    if cursor != primitive_count:
        fail(f"{path.name}: section primitive ranges cover {cursor} of {primitive_count} packets")

    valid_sections = [section for section in sections
                      if valid_section_bounds(section["low"], section["high"])]
    if shared_item is not None and not valid_sections:
        fail(f"{path.name}: shared vertices have no finite section bounds")
    global_min = tuple(min(section["low"][axis] for section in valid_sections)
                       for axis in range(3)) if valid_sections else None
    global_max = tuple(max(section["high"][axis] for section in valid_sections)
                       for axis in range(3)) if valid_sections else None

    node_unions = {}
    active_lanes = []
    for node in range(1, node_count):
        lanes = []
        for lane in range(4):
            key = struct.unpack_from("<I", data,
                                     node_offset + node * NODE_STRIDE + 96 + 4 * lane)[0]
            bounds = lane_bounds(data, node_offset, node, lane)
            if key != UNUSED_KEY and valid_box(bounds):
                lanes.append((lane, key, bounds))
                active_lanes.append((node, lane, key, bounds))
        if lanes:
            node_unions[node] = tuple(
                (min if axis % 2 == 0 else max)(entry[2][axis] for entry in lanes)
                for axis in range(6)
            )
    if node_count <= 1:
        fail(f"{path.name}: SIMD tree has no root node 1")

    internal_links = []
    leaves = []
    for node, lane, key, bounds in active_lanes:
        child_bounds = node_unions.get(key) if node < key < node_count else None
        if child_bounds is not None and encloses(bounds, child_bounds, tolerance):
            internal_links.append((node, lane, key))
        else:
            leaves.append((node, lane, key, bounds))

    indegree = {node: 0 for node in range(1, node_count)}
    children = {node: [] for node in range(1, node_count)}
    for parent, _lane, child in internal_links:
        indegree[child] += 1
        children[parent].append(child)
    reachable = set()
    stack = [1]
    while stack:
        node = stack.pop()
        if node in reachable:
            continue
        reachable.add(node)
        stack.extend(children[node])
    topology_ok = (
        len(internal_links) == node_count - 2
        and indegree[1] == 0
        and all(indegree[node] == 1 for node in range(2, node_count))
        and len(reachable) == node_count - 1
    )
    if not topology_ok:
        fail(f"{path.name}: internal-child classification does not form the N-2 rooted tree")

    leaf_keys = set()
    packet_to_leaf = {}
    vertex_checks = 0
    for _node, _lane, key, bounds in leaves:
        if key in leaf_keys:
            fail(f"{path.name}: duplicate leaf key 0x{key:x}")
        leaf_keys.add(key)
        if key & 1:
            fail(f"{path.name}: leaf key 0x{key:x} has nonzero triangle parity")
        section_ordinal = key >> 8
        local_primitive = (key & 0xFF) >> 1
        if section_ordinal >= section_count:
            fail(f"{path.name}: leaf key 0x{key:x} references a missing section")
        section = sections[section_ordinal]
        if local_primitive >= section["primitive_count"]:
            fail(f"{path.name}: leaf key 0x{key:x} exceeds section primitive count")
        packet_index = section["first_primitive"] + local_primitive
        if packet_index in packet_to_leaf:
            fail(f"{path.name}: multiple leaves map to packet {packet_index}")
        packet_to_leaf[packet_index] = key
        packet = tuple(data[primitive_offset + 4 * packet_index:
                            primitive_offset + 4 * packet_index + 4])
        if packet == (0xDE, 0xAD, 0xDE, 0xAD):
            fail(f"{path.name}: packet {packet_index} uses the unresolved 0xDEAD sentinel")
        for vertex in packet:
            if vertex < section["packed_vertices"]:
                vertex_index = section["packed_start"] + vertex
                if vertex_index >= packed_count:
                    fail(f"{path.name}: packet {packet_index} packed vertex is out of range")
                bits = struct.unpack_from("<I", data, packed_offset + 4 * vertex_index)[0]
                quantized = (bits & 0x7FF, (bits >> 11) & 0x7FF, (bits >> 22) & 0x3FF)
                point = tuple(section["packed_min"][axis]
                              + section["packed_scale"][axis] * quantized[axis]
                              for axis in range(3))
            elif remap_item is not None and shared_item is not None:
                if not valid_section_bounds(section["low"], section["high"]):
                    fail(f"{path.name}: section {section_ordinal} has shared vertices but invalid bounds")
                remap_index = section["remap_start"] + vertex - section["packed_vertices"]
                if remap_index >= remap_count:
                    fail(f"{path.name}: packet {packet_index} shared remap is out of range")
                shared_index = struct.unpack_from("<H", data, remap_offset + 2 * remap_index)[0]
                if shared_index >= shared_count:
                    fail(f"{path.name}: packet {packet_index} shared vertex is out of range")
                bits = struct.unpack_from("<Q", data, shared_offset + 8 * shared_index)[0]
                quantized = (bits & 0x1FFFFF, (bits >> 21) & 0x1FFFFF,
                             (bits >> 42) & 0x3FFFFF)
                point = tuple(global_min[axis] + (global_max[axis] - global_min[axis])
                              * quantized[axis] / ((1 << (21 if axis < 2 else 22)) - 1)
                              for axis in range(3))
            else:
                fail(f"{path.name}: packet {packet_index} uses shared vertices without type 34/31 arrays")
            if not contains_point(bounds, point, tolerance):
                fail(f"{path.name}: leaf 0x{key:x} does not enclose packet {packet_index} vertex {vertex}")
            vertex_checks += 1

    if len(leaves) != primitive_count or set(packet_to_leaf) != set(range(primitive_count)):
        fail(f"{path.name}: leaf keys do not cover every type-550 packet exactly once")
    if sum(section["primitive_count"] for section in sections) != primitive_count:
        fail(f"{path.name}: section primitive counts do not match type 550")

    tag0 = data
    return {
        "file": path.name,
        "path": str(path.resolve()),
        "export_json_sha256": sha256_file(path),
        "tag0_sha256": hashlib.sha256(tag0).hexdigest().upper(),
        "sections": section_count,
        "simd_nodes": node_count,
        "active_lanes": len(active_lanes),
        "internal_links": len(internal_links),
        "leaf_keys": len(leaves),
        "primitive_packets": primitive_count,
        "decoded_packet_vertices_checked": vertex_checks,
        "tolerance": tolerance,
        "topology_valid": topology_ok,
        "every_packet_has_one_leaf": True,
        "every_packet_vertex_is_enclosed": True,
        "all_packed_vertex_format": remap_item is None,
    }


def main() -> int:
    argument_parser = argparse.ArgumentParser(description=__doc__)
    argument_parser.add_argument("--replay-pe", type=Path, required=True)
    argument_parser.add_argument("--office-assets", type=Path, required=True)
    argument_parser.add_argument("--office-fastfile", type=Path, required=True)
    argument_parser.add_argument("--office-manifest", type=Path, required=True)
    argument_parser.add_argument("--stock-control", type=Path, required=True)
    argument_parser.add_argument("--stock-fastfile", type=Path, required=True)
    argument_parser.add_argument("--stock-manifest", type=Path, required=True)
    argument_parser.add_argument("--tolerance", type=float, default=0.001)
    argument_parser.add_argument("--output", type=Path)
    args = argument_parser.parse_args()
    if args.tolerance < 0 or not math.isfinite(args.tolerance):
        argument_parser.error("--tolerance must be finite and nonnegative")

    pe_record = verify_pe(args.replay_pe)
    office_source = verify_export_manifest(
        args.office_manifest, args.office_fastfile, OFFICE_SHA256, "Office"
    )
    stock_source = verify_export_manifest(
        args.stock_manifest, args.stock_fastfile, STOCK_SHA256, "Shipment stock"
    )
    if stock_source["applied_patches"]:
        fail("Shipment stock manifest reports applied patches")
    control_sha = sha256_file(args.stock_control)
    if control_sha != STOCK_CONTROL_JSON_SHA256:
        fail(f"Shipment control JSON SHA-256 mismatch: {control_sha}")

    office_paths = sorted(args.office_assets.glob("smodel_*.asset.json"), key=lambda p: p.name)
    if len(office_paths) != 44:
        fail(f"Office: expected 44 model PhysicsAssets, found {len(office_paths)}")
    parser = load_tag0_parser()
    office_results = [decode_asset(path, parser, args.tolerance) for path in office_paths]
    stock_result = decode_asset(args.stock_control, parser, args.tolerance)
    report = {
        "validation": "Replay 1.20 PA SIMD leaf coverage and packet-vertex AABB enclosure",
        "evidence_scope": "offline static export and PE byte validation only",
        "tolerance": args.tolerance,
        "getNextKey_pe_provenance": pe_record,
        "office_source": office_source,
        "office_asset_count": len(office_results),
        "office_aggregate": {
            "sections": sum(row["sections"] for row in office_results),
            "simd_nodes": sum(row["simd_nodes"] for row in office_results),
            "active_lanes": sum(row["active_lanes"] for row in office_results),
            "internal_links": sum(row["internal_links"] for row in office_results),
            "leaf_keys": sum(row["leaf_keys"] for row in office_results),
            "primitive_packets": sum(row["primitive_packets"] for row in office_results),
            "decoded_packet_vertices_checked": sum(
                row["decoded_packet_vertices_checked"] for row in office_results
            ),
        },
        "office_assets": office_results,
        "signed_stock_source": stock_source,
        "signed_stock_control": stock_result,
        "live_game_or_bullet_filtering_tested": False,
    }
    output = json.dumps(report, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output + "\n", encoding="utf-8")
    summary = report["office_aggregate"]
    print(
        f"validated {report['office_asset_count']} Office PhysicsAssets: "
        f"{summary['leaf_keys']} leaves / {summary['primitive_packets']} packets, "
        f"{summary['decoded_packet_vertices_checked']} enclosed packet vertices; "
        f"signed-stock control {stock_result['primitive_packets']} packets"
    )
    if args.output:
        print(f"full report: {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, struct.error) as exc:
        print(f"validation failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
