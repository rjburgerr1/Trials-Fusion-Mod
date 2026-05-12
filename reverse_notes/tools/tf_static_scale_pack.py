#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import struct
import sys
from pathlib import Path

import tf_gfx_transform_tool
import tf_mdl_injector
import tf_prop_tree


BOUNDS_KEYS = (
    "bounds.min.x",
    "bounds.min.y",
    "bounds.min.z",
    "bounds.max.x",
    "bounds.max.y",
    "bounds.max.z",
)
MESH_BOUNDS_KEYS = (
    "mesh.min.x",
    "mesh.min.y",
    "mesh.min.z",
    "mesh.max.x",
    "mesh.max.y",
    "mesh.max.z",
)


def scale_mdl_visible_bounds(input_path: Path, output_path: Path, scale: float) -> int:
    data = bytearray(input_path.read_bytes())
    needle = b"visible"
    count = 0
    start = 0
    while True:
        name_offset = data.find(needle, start)
        if name_offset < 0:
            break
        if name_offset < 4 or struct.unpack_from("<I", data, name_offset - 4)[0] != len(needle):
            start = name_offset + 1
            continue
        base = name_offset + len(needle)
        for rel in (24, 28, 32, 36, 40, 44):
            value = struct.unpack_from("<f", data, base + rel)[0]
            struct.pack_into("<f", data, base + rel, value * scale)
        count += 1
        start = name_offset + len(needle)
    if count == 0:
        raise ValueError(f"no visible MDL surface bounds found in {input_path}")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(data)
    return count


def scale_node_attrs(node: tf_prop_tree.Node, keys: tuple[str, ...], scale: float) -> int:
    changed = 0
    for current in tf_prop_tree.walk(node):
        attrs = current.attr_map()
        if not all(key in attrs for key in keys):
            continue
        for key in keys:
            tf_prop_tree.set_attr(current, key, attrs[key] * scale)
        changed += 1
    return changed


def build_scaled_objectcollection(args: argparse.Namespace, output_path: Path) -> tuple[int, int]:
    root = tf_prop_tree.parse_file(args.objectcollection)
    source = tf_mdl_injector.find_source_object(root, args.source_name)
    if source.parent is None:
        raise ValueError("source object has no parent")

    attrs = source.attr_map()
    source_devfile = attrs["filename"]
    custom_devfile = args.custom_devfile or f"<evo2_devfiles>{args.custom_mdl_path}"

    clone = tf_prop_tree.detach_tree(source)
    tf_prop_tree.set_attr(clone, "name", args.mesh_name)
    tf_prop_tree.set_attr(clone, "ID", args.mesh_id)
    tf_prop_tree.set_attr(clone, "filename", custom_devfile)
    for attr in clone.attrs:
        if attr.key.display == "#94a26a5c":
            tf_prop_tree.set_attr(clone, "#94a26a5c", custom_devfile)
            break

    changed_paths = tf_prop_tree.replace_string_values(clone, source_devfile, custom_devfile)
    if changed_paths == 0:
        raise ValueError(f"no generated subtree strings matched {source_devfile}")

    container_count = scale_node_attrs(clone, BOUNDS_KEYS, args.scale)
    mesh_count = scale_node_attrs(clone, MESH_BOUNDS_KEYS, args.scale)
    if container_count == 0 or mesh_count == 0:
        raise ValueError(
            f"incomplete objectcollection bounds patch: containers={container_count} meshes={mesh_count}"
        )

    clone.parent = source.parent
    source.parent.children.append(clone)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(tf_prop_tree.encode_node(root))
    return container_count, mesh_count


def patch_gfx_scale(input_path: Path, output_path: Path, mesh_name: str, scale: float) -> None:
    data = bytearray(input_path.read_bytes())
    base = tf_gfx_transform_tool.find_ref(data, mesh_name)
    tf_gfx_transform_tool.write_floats(
        data,
        base,
        tf_gfx_transform_tool.OFF_SCALE,
        (scale, scale, scale),
    )
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(data)


def cmd_build(args: argparse.Namespace) -> None:
    work_dir = args.work_dir
    work_dir.mkdir(parents=True, exist_ok=True)

    stem = f"{args.mesh_name}_static_scale{args.scale:g}"
    patched_gfx = work_dir / f"{stem}.gfx"
    patched_mdl = work_dir / f"{stem}.mdl"
    patched_objectcollection = work_dir / f"objectcollection6_{stem}.bin"

    patch_gfx_scale(args.gfx, patched_gfx, args.mesh_name, args.scale)
    mdl_surfaces = scale_mdl_visible_bounds(args.mdl, patched_mdl, args.scale)
    container_count, mesh_count = build_scaled_objectcollection(args, patched_objectcollection)

    pak_args = argparse.Namespace(
        base_pak=args.base_pak,
        output=args.output,
        custom_gfx_path=args.custom_gfx_path,
        gfx=patched_gfx,
        custom_mdl_path=args.custom_mdl_path,
        mdl=patched_mdl,
    )
    tf_mdl_injector.build_pak(pak_args, patched_objectcollection)

    if args.install:
        args.install.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.output, args.install)
        print(f"installed {args.install}")

    print(
        "static scale patch complete: "
        f"scale={args.scale:g} gfx=1 mdl_surfaces={mdl_surfaces} "
        f"containers={container_count} mesh_records={mesh_count}"
    )
    print(f"patched gfx: {patched_gfx}")
    print(f"patched mdl: {patched_mdl}")
    print(f"patched objectcollection: {patched_objectcollection}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Build a static custom-object scale experiment pack by scaling the GFX "
            "mesh transform, MDL visible surface bounds, and objectcollection "
            "container/mesh bounds together."
        )
    )
    sub = parser.add_subparsers(required=True)

    build = sub.add_parser("build")
    build.add_argument("output", type=Path, help="rebuilt data6.pak output")
    build.add_argument("--scale", type=float, default=3.0)
    build.add_argument(
        "--base-pak",
        type=Path,
        default=Path("reverse_notes/out/data6_codex_custom_half_ramp.pak"),
    )
    build.add_argument(
        "--objectcollection",
        type=Path,
        default=Path("reverse_notes/out/objectcollection6_codex_custom_half_ramp.bin"),
    )
    build.add_argument(
        "--gfx",
        type=Path,
        default=Path("reverse_notes/out/codex_custom_cloned_mesh_ref.gfx"),
    )
    build.add_argument(
        "--mdl",
        type=Path,
        default=Path("reverse_notes/out/codex_mesh_0001_brickwall_roundtrip.mdl"),
    )
    build.add_argument("--source-name", default="broken_brickwall_03")
    build.add_argument("--mesh-name", default=tf_mdl_injector.DEFAULT_CUSTOM_MESH_NAME)
    build.add_argument("--mesh-id", type=int, default=tf_mdl_injector.DEFAULT_CUSTOM_MESH_ID)
    build.add_argument("--custom-gfx-path", default=tf_mdl_injector.DEFAULT_CUSTOM_GFX_PATH)
    build.add_argument("--custom-mdl-path", default=tf_mdl_injector.DEFAULT_CUSTOM_MDL_PATH)
    build.add_argument("--custom-devfile")
    build.add_argument("--work-dir", type=Path, default=Path("reverse_notes/out"))
    build.add_argument("--install", type=Path, help="optional live data6.pak destination")
    build.set_defaults(func=cmd_build)

    return parser


def main(argv: list[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        args.func(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
