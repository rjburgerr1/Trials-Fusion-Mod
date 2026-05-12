#!/usr/bin/env python3
import argparse
import json
import copy
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


KNOWN_HASHES = {
    0x68C4C754: "objectCollection",
    0x86CE3990: "categories",
    0xF71D1550: "category",
    0xCA82E140: "name",
    0xAC346ED5: "objects",
    0xD8D20FE4: "object",
    0x1A267D88: "filename",
    0x00255B12: "ID",
    0x0036EF52: "id",
    0xDF6B6B5B: "type",
    0xA4016384: "packageID",
    0x522C090E: "exclusive",
    0x186A5856: "container",
    0x0CB17821: "lods",
    0x0CBA931A: "bounds",
    0x0041EBE6: "bounds.min.x",
    0x00427886: "bounds.min.y",
    0x00430526: "bounds.min.z",
    0x0041EBCA: "bounds.max.x",
    0x0042786A: "bounds.max.y",
    0x0043050A: "bounds.max.z",
    0xDD4CF1A6: "mesh.min.x",
    0xDD4DC496: "mesh.min.y",
    0xDD4E9786: "mesh.min.z",
    0x173C7CA6: "mesh.max.x",
    0x173D4F96: "mesh.max.y",
    0x173E2286: "mesh.max.z",
    0x8EF7A80A: "containers",
    0xEAAF31A4: "decalContainers",
    0x854F8C9B: "materials",
    0xD955D183: "surface",
    0xA4512B1B: "meshes",
    0x3F427CA3: "source",
    0x6A582B65: "package",
    0xEB739B94: "directory",
    0x978CB0D6: "materials",
    0xCC42E703: "material",
    0x00000075: "u",
    0x00000076: "v",
    0x00000077: "w",
    0x00000078: "x",
    0x00000079: "y",
    0x0000007A: "z",
}


def g4_hash(text: str) -> int:
    data = text.encode("ascii")
    seed = len(data)
    h = 0
    for ch in data:
        h = (h + ch * seed) & 0xFFFFFFFF
        seed = ((seed & 0xFFFF) * 18000 + (seed >> 16)) & 0xFFFFFFFF
    return h


def resolve_hash(value: int) -> str:
    return KNOWN_HASHES.get(value, f"#{value:08x}")


@dataclass
class NameToken:
    marker: int
    text: str | None
    hash_value: int | None

    @property
    def display(self) -> str:
        if self.text is not None:
            return self.text
        assert self.hash_value is not None
        return resolve_hash(self.hash_value)


@dataclass
class Attribute:
    key: NameToken
    value_type: int
    raw_value: bytes

    @property
    def value(self) -> Any:
        data = self.raw_value
        if self.value_type in (3, 4) and len(data) == 4:
            return struct.unpack("<i", data)[0]
        if self.value_type == 5 and len(data) == 4:
            return struct.unpack("<f", data)[0]
        if self.value_type == 6:
            return data.decode("utf-8", errors="replace").rstrip("\x00")
        if self.value_type == 2 and len(data) == 1:
            return bool(data[0])
        return data.hex()


@dataclass
class Node:
    name: NameToken
    attrs: list[Attribute] = field(default_factory=list)
    children: list["Node"] = field(default_factory=list)
    start: int = 0
    end: int = 0
    parent: "Node | None" = field(default=None, repr=False)

    @property
    def display(self) -> str:
        return self.name.display

    def attr_map(self) -> dict[str, Any]:
        return {attr.key.display: attr.value for attr in self.attrs}


class Parser:
    def __init__(self, data: bytes):
        self.data = data
        self.offset = 0

    def read_u8(self) -> int:
        if self.offset >= len(self.data):
            raise ValueError("unexpected end of data")
        value = self.data[self.offset]
        self.offset += 1
        return value

    def read_u16(self) -> int:
        if self.offset + 2 > len(self.data):
            raise ValueError("unexpected end of data")
        value = struct.unpack_from("<H", self.data, self.offset)[0]
        self.offset += 2
        return value

    def read_u32(self) -> int:
        if self.offset + 4 > len(self.data):
            raise ValueError("unexpected end of data")
        value = struct.unpack_from("<I", self.data, self.offset)[0]
        self.offset += 4
        return value

    def read_bytes(self, size: int) -> bytes:
        if self.offset + size > len(self.data):
            raise ValueError("unexpected end of data")
        value = self.data[self.offset : self.offset + size]
        self.offset += size
        return value

    def read_token(self, node_name: bool) -> NameToken:
        marker = self.read_u8() if node_name else 0
        size = self.read_u8()
        if size == 0:
            return NameToken(marker, None, self.read_u32())
        return NameToken(marker, self.read_bytes(size).decode("utf-8", errors="replace"), None)

    def read_node(self, parent: Node | None = None) -> Node:
        start = self.offset
        node = Node(self.read_token(node_name=True), start=start, parent=parent)
        while self.read_u8():
            key = self.read_token(node_name=False)
            value_type = self.read_u8()
            size = self.read_u16()
            node.attrs.append(Attribute(key, value_type, self.read_bytes(size)))
        while self.read_u8():
            node.children.append(self.read_node(parent=node))
        node.end = self.offset
        return node


def walk(node: Node):
    yield node
    for child in node.children:
        yield from walk(child)


def dump_node(node: Node, max_depth: int, depth: int = 0) -> dict[str, Any]:
    result: dict[str, Any] = {
        "name": node.display,
        "attrs": node.attr_map(),
        "offset": node.start,
        "size": node.end - node.start,
    }
    if depth < max_depth:
        result["children"] = [dump_node(child, max_depth, depth + 1) for child in node.children]
    else:
        result["child_count"] = len(node.children)
    return result


def parse_file(path: Path) -> Node:
    parser = Parser(path.read_bytes())
    root = parser.read_node()
    if parser.offset != len(parser.data):
        raise ValueError(f"trailing bytes at 0x{parser.offset:x}")
    return root


def encode_token(token: NameToken, node_name: bool) -> bytes:
    out = bytearray()
    if node_name:
        out.append(token.marker)
    if token.text is None:
        assert token.hash_value is not None
        out.append(0)
        out += struct.pack("<I", token.hash_value)
    else:
        data = token.text.encode("utf-8")
        if len(data) > 0xFF:
            raise ValueError(f"token too long: {token.text}")
        out.append(len(data))
        out += data
    return bytes(out)


def encode_node(node: Node) -> bytes:
    out = bytearray(encode_token(node.name, node_name=True))
    for attr in node.attrs:
        out.append(1)
        out += encode_token(attr.key, node_name=False)
        out.append(attr.value_type)
        if len(attr.raw_value) > 0xFFFF:
            raise ValueError(f"attribute too large: {attr.key.display}")
        out += struct.pack("<H", len(attr.raw_value))
        out += attr.raw_value
    out.append(0)
    for child in node.children:
        out.append(1)
        out += encode_node(child)
    out.append(0)
    return bytes(out)


def as_i32(value: int) -> int:
    value &= 0xFFFFFFFF
    if value >= 0x80000000:
        value -= 0x100000000
    return value


def set_attr(node: Node, key: str, value: Any) -> None:
    for attr in node.attrs:
        if attr.key.display != key:
            continue
        if attr.value_type in (3, 4):
            attr.raw_value = struct.pack("<i", int(value))
            return
        if attr.value_type == 5:
            attr.raw_value = struct.pack("<f", float(value))
            return
        if attr.value_type == 6:
            attr.raw_value = str(value).encode("utf-8") + b"\x00"
            return
        raise ValueError(f"unsupported editable attr type {attr.value_type} for {key}")
    raise ValueError(f"attribute not found: {key}")


def replace_string_values(node: Node, old: str, new: str) -> int:
    count = 0
    for current in walk(node):
        for attr in current.attrs:
            if attr.value_type != 6:
                continue
            value = attr.value
            if value != old:
                continue
            attr.raw_value = new.encode("utf-8") + b"\x00"
            count += 1
    return count


def detach_tree(node: Node) -> Node:
    clone = copy.deepcopy(node)

    def fix(current: Node, parent: Node | None) -> None:
        current.parent = parent
        current.start = 0
        current.end = 0
        for child in current.children:
            fix(child, current)

    fix(clone, None)
    return clone


def cmd_summary(args: argparse.Namespace) -> None:
    root = parse_file(args.input)
    counts: dict[str, int] = {}
    for node in walk(root):
        counts[node.display] = counts.get(node.display, 0) + 1
    print(json.dumps({"root": dump_node(root, args.depth), "counts": counts}, indent=2))


def cmd_objects(args: argparse.Namespace) -> None:
    root = parse_file(args.input)
    rows = []
    for node in walk(root):
        if node.display != "object":
            continue
        attrs = node.attr_map()
        if "filename" not in attrs:
            continue
        rows.append(
            {
                "name": attrs.get("name"),
                "id": attrs.get("ID", attrs.get("id")),
                "packageID": attrs.get("packageID"),
                "type": attrs.get("type"),
                "exclusive": attrs.get("exclusive"),
                "filename": attrs.get("filename"),
                "offset": node.start,
                "size": node.end - node.start,
                "children": len(node.children),
            }
        )
    if args.json:
        print(json.dumps(rows, indent=2))
        return
    for row in rows:
        print(
            f"{row['offset']:8d} {row['size']:7d} pkg={row['packageID']} "
            f"id={row['id']} children={row['children']} {row['name']} -> {row['filename']}"
        )


def cmd_roundtrip(args: argparse.Namespace) -> None:
    root = parse_file(args.input)
    data = encode_node(root)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print(f"wrote {args.output} ({len(data)} bytes)")


def cmd_clone_object(args: argparse.Namespace) -> None:
    root = parse_file(args.input)
    source = None
    for node in walk(root):
        if node.display != "object":
            continue
        attrs = node.attr_map()
        if attrs.get("name") == args.source_name:
            source = node
            break
    if source is None:
        raise ValueError(f"source object not found: {args.source_name}")
    if source.parent is None:
        raise ValueError("source object has no parent")

    clone = detach_tree(source)
    set_attr(clone, "name", args.new_name)
    if args.new_id is None:
        new_id = as_i32(g4_hash(args.new_name))
    else:
        new_id = int(args.new_id, 0)
    set_attr(clone, "ID", new_id)
    if args.filename is not None:
        set_attr(clone, "filename", args.filename)
        if args.source is None:
            for attr in clone.attrs:
                if attr.key.display == "#94a26a5c":
                    set_attr(clone, "#94a26a5c", args.filename)
                    break
    if args.source is not None:
        set_attr(clone, "#94a26a5c", args.source)
    if args.child_name is not None:
        for child in clone.children:
            try:
                set_attr(child, "name", args.child_name)
                break
            except ValueError:
                continue
    for replacement in args.replace_string:
        if "=" not in replacement:
            raise ValueError("--replace-string must be OLD=NEW")
        old, new = replacement.split("=", 1)
        count = replace_string_values(clone, old, new)
        print(f"replaced {count} string value(s): {old} -> {new}")

    clone.parent = source.parent
    source.parent.children.append(clone)
    data = encode_node(root)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print(
        f"cloned {args.source_name} -> {args.new_name} "
        f"ID={as_i32(new_id)} output={args.output}"
    )


def cmd_rename_object(args: argparse.Namespace) -> None:
    root = parse_file(args.input)
    matches = []
    for node in walk(root):
        if node.display != "object":
            continue
        attrs = node.attr_map()
        if attrs.get("name") == args.source_name:
            matches.append(node)
    if not matches:
        raise ValueError(f"source object not found: {args.source_name}")
    if len(matches) > 1 and not args.all:
        raise ValueError(
            f"found {len(matches)} objects named {args.source_name}; pass --all to rename all"
        )
    for node in matches:
        set_attr(node, "name", args.new_name)
        if args.new_id is not None:
            set_attr(node, "ID", int(args.new_id, 0))
    data = encode_node(root)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print(f"renamed {len(matches)} object(s) {args.source_name} -> {args.new_name}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Trials Fusion binary property-tree reader")
    sub = parser.add_subparsers(required=True)

    summary = sub.add_parser("summary")
    summary.add_argument("input", type=Path)
    summary.add_argument("--depth", type=int, default=2)
    summary.set_defaults(func=cmd_summary)

    objects = sub.add_parser("objects")
    objects.add_argument("input", type=Path)
    objects.add_argument("--json", action="store_true")
    objects.set_defaults(func=cmd_objects)

    roundtrip = sub.add_parser("roundtrip")
    roundtrip.add_argument("input", type=Path)
    roundtrip.add_argument("output", type=Path)
    roundtrip.set_defaults(func=cmd_roundtrip)

    clone = sub.add_parser("clone-object")
    clone.add_argument("input", type=Path)
    clone.add_argument("source_name")
    clone.add_argument("new_name")
    clone.add_argument("output", type=Path)
    clone.add_argument("--new-id")
    clone.add_argument("--filename")
    clone.add_argument("--source")
    clone.add_argument("--child-name")
    clone.add_argument("--replace-string", action="append", default=[])
    clone.set_defaults(func=cmd_clone_object)

    rename = sub.add_parser("rename-object")
    rename.add_argument("input", type=Path)
    rename.add_argument("source_name")
    rename.add_argument("new_name")
    rename.add_argument("output", type=Path)
    rename.add_argument("--new-id")
    rename.add_argument("--all", action="store_true")
    rename.set_defaults(func=cmd_rename_object)

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
