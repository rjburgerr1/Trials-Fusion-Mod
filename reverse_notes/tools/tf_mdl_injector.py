#!/usr/bin/env python3
import argparse
import re
import shutil
import sys
from pathlib import Path

import tf_pak_tool
import tf_prop_tree


DEFAULT_CUSTOM_GFX_PATH = "contentpack6/manmade/props/codex/codex_custom_half_ramp.gfx"
DEFAULT_CUSTOM_MDL_PATH = "objects/pack6/codex_mesh_0001_302248137.mdl"
DEFAULT_CUSTOM_DEVFILE = f"<evo2_devfiles>{DEFAULT_CUSTOM_MDL_PATH}"
DEFAULT_CUSTOM_MESH_NAME = "codex_mesh_0001"
DEFAULT_CUSTOM_MESH_ID = -178212677


def infer_source_name(mdl: Path) -> str:
    match = re.match(r"(.+)_\d+\.mdl$", mdl.name, re.IGNORECASE)
    if not match:
        raise ValueError(
            f"cannot infer source object from {mdl.name}; pass --source-name explicitly"
        )
    return match.group(1)


def find_source_object(root: tf_prop_tree.Node, source_name: str) -> tf_prop_tree.Node:
    matches = []
    for node in tf_prop_tree.walk(root):
        if node.display != "object":
            continue
        attrs = node.attr_map()
        if attrs.get("name") == source_name and "filename" in attrs:
            matches.append(node)
    if not matches:
        raise ValueError(f"source object not found in objectcollection: {source_name}")
    if len(matches) > 1:
        raise ValueError(f"source object is ambiguous in objectcollection: {source_name}")
    return matches[0]


def build_objectcollection(args: argparse.Namespace, source_name: str, output: Path) -> None:
    root = tf_prop_tree.parse_file(args.objectcollection)
    source = find_source_object(root, source_name)
    if source.parent is None:
        raise ValueError("source object has no parent")

    attrs = source.attr_map()
    source_devfile = attrs["filename"]

    clone = tf_prop_tree.detach_tree(source)
    tf_prop_tree.set_attr(clone, "name", args.mesh_name)
    tf_prop_tree.set_attr(clone, "ID", args.mesh_id)
    tf_prop_tree.set_attr(clone, "filename", args.custom_devfile)

    for attr in clone.attrs:
        if attr.key.display == "#94a26a5c":
            tf_prop_tree.set_attr(clone, "#94a26a5c", args.custom_devfile)
            break

    changed = tf_prop_tree.replace_string_values(clone, source_devfile, args.custom_devfile)
    if changed == 0:
        raise ValueError(f"no generated subtree strings matched {source_devfile}")

    clone.parent = source.parent
    source.parent.children.append(clone)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(tf_prop_tree.encode_node(root))
    print(f"cloned objectcollection source {source_name} -> {args.mesh_name}")
    print(f"repathed {changed} generated string value(s)")


def build_pak(args: argparse.Namespace, objectcollection: Path) -> None:
    pak = tf_pak_tool.PakFile(args.base_pak)
    replacements: dict[str, tuple[bytes, int]] = {
        "objectcollection6.xml": (objectcollection.read_bytes(), 0x01),
        args.custom_gfx_path: (args.gfx.read_bytes(), 0x01),
    }
    additions: list[tuple[str, bytes, int]] = []

    mdl_data = args.mdl.read_bytes()
    existing = {
        entry.name.replace("\\", "/")
        for entry in pak.file_entries
        if entry.name is not None
    }
    if args.custom_mdl_path in existing:
        replacements[args.custom_mdl_path] = (mdl_data, 0x00)
    else:
        additions.append((args.custom_mdl_path, mdl_data, 0x00))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    tf_pak_tool.write_rebuilt(pak, args.output, replacements, additions)
    print(f"wrote {args.output}")


def cmd_build(args: argparse.Namespace) -> None:
    if args.source_name is None:
        args.source_name = infer_source_name(args.mdl)
    if args.custom_devfile is None:
        args.custom_devfile = f"<evo2_devfiles>{args.custom_mdl_path}"

    work_dir = args.work_dir
    work_dir.mkdir(parents=True, exist_ok=True)
    generated_objectcollection = work_dir / f"objectcollection6_{args.mesh_name}_{args.source_name}.bin"
    build_objectcollection(args, args.source_name, generated_objectcollection)
    build_pak(args, generated_objectcollection)

    if args.install:
        args.install.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.output, args.install)
        print(f"installed {args.install}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Inject an existing Trials Fusion MDL into the pack6 Codex custom object slot. "
            "The MDL must have a matching source object in objectcollection6."
        )
    )
    sub = parser.add_subparsers(required=True)

    build = sub.add_parser("build")
    build.add_argument("mdl", type=Path, help="existing Trials Fusion .mdl payload")
    build.add_argument("output", type=Path, help="rebuilt data6.pak output")
    build.add_argument("--source-name", help="objectcollection source name; inferred from *_123.mdl")
    build.add_argument(
        "--base-pak",
        type=Path,
        default=Path("reverse_notes/out/data6_codex_custom_half_ramp.pak"),
    )
    build.add_argument(
        "--objectcollection",
        type=Path,
        default=Path("reverse_notes/out/objectcollection6.bin"),
    )
    build.add_argument(
        "--gfx",
        type=Path,
        default=Path("reverse_notes/out/codex_custom_cloned_mesh_ref.gfx"),
    )
    build.add_argument("--custom-gfx-path", default=DEFAULT_CUSTOM_GFX_PATH)
    build.add_argument("--custom-mdl-path", default=DEFAULT_CUSTOM_MDL_PATH)
    build.add_argument("--custom-devfile")
    build.add_argument("--mesh-name", default=DEFAULT_CUSTOM_MESH_NAME)
    build.add_argument("--mesh-id", type=int, default=DEFAULT_CUSTOM_MESH_ID)
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
