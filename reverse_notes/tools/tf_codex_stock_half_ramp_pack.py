#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import struct
import sys
from pathlib import Path

import tf_pak_tool
import tf_prop_tree


CODEX_NAME = "codex_custom_half_ramp"
CODEX_ID = -1047365226
CODEX_GFX_PATH = "contentpack6/manmade/props/codex/codex_custom_half_ramp.gfx"
CODEX_DEVFILE = f"<evo2_devfiles>{CODEX_GFX_PATH}"
STOCK_NAME = "wooden_ramp_half"
STOCK_GFX_PATH = "contentpack6/manmade/props/wooden_ramp_set/wooden_ramp_half.gfx"
STOCK_DEVFILE = f"<evo2_devfiles>{STOCK_GFX_PATH}"
STOCK_GFX_REFS = (
    "woodenramp_half",
    "woodenramp_half_u30",
    "woodenramp_half_u45",
    "woodenramp_half_u60",
    "woodenramp_half_u75",
    "woodenramp_half_u90",
)


def find_object(root: tf_prop_tree.Node, name: str) -> tf_prop_tree.Node:
    matches = []
    for node in tf_prop_tree.walk(root):
        if node.display != "object":
            continue
        attrs = node.attr_map()
        if attrs.get("name") == name and "filename" in attrs:
            matches.append(node)
    if not matches:
        raise ValueError(f"object not found: {name}")
    if len(matches) > 1:
        raise ValueError(f"object is ambiguous: {name}")
    return matches[0]


def build_objectcollection(input_path: Path, output_path: Path) -> None:
    root = tf_prop_tree.parse_file(input_path)
    stock = find_object(root, STOCK_NAME)
    existing = find_object(root, CODEX_NAME)
    if existing.parent is None:
        raise ValueError(f"{CODEX_NAME} has no parent")

    clone = tf_prop_tree.detach_tree(stock)
    tf_prop_tree.set_attr(clone, "name", CODEX_NAME)
    tf_prop_tree.set_attr(clone, "ID", CODEX_ID)
    tf_prop_tree.set_attr(clone, "filename", CODEX_DEVFILE)
    tf_prop_tree.set_attr(clone, "#94a26a5c", CODEX_DEVFILE)
    tf_prop_tree.replace_string_values(clone, STOCK_DEVFILE, CODEX_DEVFILE)

    for child in tf_prop_tree.walk(clone):
        attrs = child.attr_map()
        if attrs.get("name") == "Wooden_Ramp_Half":
            tf_prop_tree.set_attr(child, "name", CODEX_NAME)

    parent = existing.parent
    index = parent.children.index(existing)
    clone.parent = parent
    parent.children[index] = clone

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(tf_prop_tree.encode_node(root))
    print(f"rebuilt {CODEX_NAME} from {STOCK_NAME} metadata")


def patch_gfx_scale(gfx_data: bytes, scale: float) -> bytes:
    if scale == 1.0:
        return gfx_data

    data = bytearray(gfx_data)
    patched = 0
    for ref in STOCK_GFX_REFS:
        needle = ref.encode("ascii")
        offsets = []
        start = 0
        while True:
            offset = data.find(needle, start)
            if offset < 0:
                break
            if offset >= 4 and struct.unpack_from("<I", data, offset - 4)[0] == len(needle):
                offsets.append(offset)
            start = offset + 1
        if not offsets:
            raise ValueError(f"GFX mesh ref not found: {ref}")
        if len(offsets) > 1:
            raise ValueError(f"GFX mesh ref is ambiguous: {ref}")
        offset = offsets[0]
        for rel in (-48, -44, -40):
            struct.pack_into("<f", data, offset + rel, scale)
        patched += 1
    print(f"patched {patched} stock half-ramp GFX refs to scale {scale:g}")
    return bytes(data)


def build_pack(args: argparse.Namespace, objectcollection_path: Path) -> None:
    pak = tf_pak_tool.PakFile(args.base_pak)
    stock_gfx = tf_pak_tool.find_entry(pak, STOCK_GFX_PATH)
    stock_gfx_data = patch_gfx_scale(pak.unpacked_payload(stock_gfx), args.gfx_scale)
    replacements = {
        "objectcollection6.xml": (objectcollection_path.read_bytes(), 0x01),
        CODEX_GFX_PATH: (stock_gfx_data, stock_gfx.flags),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    tf_pak_tool.write_rebuilt(pak, args.output, replacements, [])
    print(f"wrote {args.output}")


def cmd_build(args: argparse.Namespace) -> None:
    objectcollection_path = args.work_dir / "objectcollection6_codex_stock_half_ramp.bin"
    build_objectcollection(args.objectcollection, objectcollection_path)
    build_pack(args, objectcollection_path)
    if args.install:
        args.install.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.output, args.install)
        print(f"installed {args.install}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Make the Codex custom half-ramp slot use stock wooden_ramp_half "
            "object metadata, stock wooden-ramp-half GFX mesh payload, and stock "
            "wooden ramp collision."
        )
    )
    sub = parser.add_subparsers(required=True)

    build = sub.add_parser("build")
    build.add_argument("output", type=Path)
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
    build.add_argument("--work-dir", type=Path, default=Path("reverse_notes/out"))
    build.add_argument(
        "--gfx-scale",
        type=float,
        default=1.0,
        help="optional static scale for all stock wooden_ramp_half GFX refs",
    )
    build.add_argument("--install", type=Path)
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
