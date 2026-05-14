#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import fnmatch
import struct
import sys
import xml.etree.ElementTree as ET
import zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


PAK_MAGIC = 0x12345678
PAK_HEADER = struct.Struct("<III")
PAK_ENTRY = struct.Struct("<IIIBI")


KNOWN_HASHES = {
    0x68C4C754: "collection",
    0x86CE3990: "categories",
    0xF71D1550: "category",
    0xCA82E140: "name",
    0xAC346ED5: "objects",
    0xD8D20FE4: "object",
    0x1A267D88: "filename",
    0x94A26A5C: "originalFilename",
    0x00255B12: "ID",
    0x0036EF52: "id",
    0xDF6B6B5B: "type",
    0xA4016384: "packageID",
    0x522C090E: "exclusive",
    0x804C4199: "export",
    0xCF97D2E5: "icon",
    0xFB0E9D1F: "date",
    0x26C5EF60: "creationDate",
    0xEE0FE53A: "value",
    0x186A5856: "container",
    0x0CB17821: "lods",
    0x0CBA931A: "bounds",
    0x8EF7A80A: "containers",
    0xEAAF31A4: "decalContainers",
    0x854F8C9B: "materials",
    0xD955D183: "surface",
    0xA4512B1B: "meshes",
    0x3F427CA3: "source",
    0x6A582B65: "package",
    0xEB739B94: "directory",
    0xCC42E703: "material",
    0x55E806B2: "diffuse",
    0x1B80EC1E: "color",
    0x78CA42FA: "sources",
    0x8D306823: "data",
    0x89B43684: "tiling",
    0x00000030: "0",
    0x00000075: "u",
    0x00000076: "v",
    0x00000077: "w",
    0x00000078: "x",
    0x00000079: "y",
    0x0000007A: "z",
}


DEFAULT_HASH_CSV = Path(__file__).resolve().parents[1] / "trials_hashes.csv"


def g4_hash(text: str, uppercase: bool = False) -> int:
    data = (text.upper() if uppercase else text).encode("ascii")
    seed = len(data)
    value = 0
    for byte in data:
        value = (value + byte * seed) & 0xFFFFFFFF
        seed = ((seed & 0xFFFF) * 18000 + (seed >> 16)) & 0xFFFFFFFF
    return value


def g4_path_hash(path: str) -> int:
    normalized = path.replace("\\", "/").lstrip("./\\/")
    return g4_hash(normalized, uppercase=True)


def strip_virtual_prefix(value: str) -> str:
    for prefix in ("<evo2_devfiles>", "<evo2_sourcefiles>"):
        if value.startswith(prefix):
            return value[len(prefix) :]
    return value


def load_hash_csv(path: Path) -> dict[int, str]:
    hashes: dict[int, str] = {}
    with path.open(newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        fieldnames = reader.fieldnames or []
        hash_field = next((name for name in fieldnames if name.lower().startswith("hash")), None)
        value_field = next((name for name in fieldnames if name.lower().startswith("value")), None)
        if hash_field is None or value_field is None:
            raise ValueError(f"{path} does not look like a hash/value CSV")
        for row in reader:
            raw_hash = (row.get(hash_field) or "").strip()
            value = (row.get(value_field) or "").strip()
            if not raw_hash or not value:
                continue
            hashes[int(raw_hash, 16)] = value
    return hashes


def merged_hashes(extra_paths: list[Path]) -> dict[int, str]:
    hashes = dict(KNOWN_HASHES)
    paths = []
    if DEFAULT_HASH_CSV.exists():
        paths.append(DEFAULT_HASH_CSV)
    paths.extend(extra_paths)
    for path in paths:
        hashes.update(load_hash_csv(path))
    return hashes


def is_xml_name(value: str) -> bool:
    if not value:
        return False
    first = value[0]
    if not (first.isalpha() or first == "_"):
        return False
    return all(ch.isalnum() or ch in "._-" for ch in value)


def safe_text_xml_name(value: str, attr: bool = False) -> str:
    prefix = "attr" if attr else "tag"
    cleaned = "".join(ch if ch.isalnum() or ch in "._-" else "_" for ch in value)
    cleaned = cleaned.strip("._-")
    if cleaned and (cleaned[0].isalpha() or cleaned[0] == "_"):
        return cleaned
    return f"{prefix}_{cleaned or 'unnamed'}"


@dataclass
class PakEntry:
    index: int
    path_hash: int
    stored_size: int
    unpacked_size: int
    flags: int
    data_offset: int
    name: str | None = None


class PakFile:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        self.entries: list[PakEntry] = []
        self.names: list[str] = []
        self._parse()

    def _parse(self) -> None:
        magic, data_start, entry_count = PAK_HEADER.unpack_from(self.data, 0)
        if magic != PAK_MAGIC:
            raise ValueError(f"{self.path} is not a Trials Fusion pak")
        expected_data_start = PAK_HEADER.size + entry_count * PAK_ENTRY.size
        if data_start != expected_data_start:
            raise ValueError(f"{self.path} has unexpected data_start 0x{data_start:x}")

        offset = PAK_HEADER.size
        for index in range(entry_count):
            row = PAK_ENTRY.unpack_from(self.data, offset)
            self.entries.append(PakEntry(index, *row))
            offset += PAK_ENTRY.size

        self.names = self._read_name_list()
        for entry, name in zip(self.entries, self.names):
            entry.name = name

    @property
    def file_entries(self) -> list[PakEntry]:
        return self.entries[:-1]

    def raw_payload(self, entry: PakEntry) -> bytes:
        start = entry.data_offset
        end = start + entry.stored_size
        if start < PAK_HEADER.size or end > len(self.data):
            raise ValueError(f"{self.path}: entry {entry.index} points outside archive")
        return self.data[start:end]

    def unpacked_payload(self, entry: PakEntry) -> bytes:
        raw = self.raw_payload(entry)
        if entry.flags == 0x00:
            return raw
        if entry.flags == 0x01:
            return zlib.decompress(raw)
        if entry.flags & 0x10:
            raise ValueError(f"{entry.name or entry.index} is protected/encrypted")
        raise ValueError(f"{entry.name or entry.index} has unsupported flags 0x{entry.flags:02x}")

    def _read_name_list(self) -> list[str]:
        entry = self.entries[-1]
        raw = self.unpacked_payload(entry)
        count = struct.unpack_from("<I", raw, 0)[0]
        offset = 4
        names = []
        for _ in range(count):
            size = struct.unpack_from("<H", raw, offset)[0]
            offset += 2
            names.append(raw[offset : offset + size].decode("utf-8"))
            offset += size
        return names


@dataclass
class NameToken:
    marker: int
    text: str | None
    hash_value: int | None

    def display(self, hashes: dict[int, str] | None = None) -> str:
        if self.text is not None:
            return self.text
        assert self.hash_value is not None
        lookup = hashes if hashes is not None else KNOWN_HASHES
        return lookup.get(self.hash_value, f"#{self.hash_value:08x}")

    def legacy_label(self, hashes: dict[int, str] | None = None) -> str:
        if self.hash_value is None:
            return self.display(hashes)
        return f"{self.display(hashes)}|0x{self.hash_value:08X}"


@dataclass
class Attribute:
    key: NameToken
    value_type: int
    raw_value: bytes

    @property
    def value(self) -> Any:
        if self.value_type in (3, 4) and len(self.raw_value) == 4:
            return struct.unpack("<i", self.raw_value)[0]
        if self.value_type == 5 and len(self.raw_value) == 4:
            return struct.unpack("<f", self.raw_value)[0]
        if self.value_type == 6:
            return self.raw_value.decode("utf-8", errors="replace").rstrip("\x00")
        if self.value_type == 2 and len(self.raw_value) == 1:
            return bool(self.raw_value[0])
        return self.raw_value

    @property
    def unsigned_value(self) -> int | None:
        if self.value_type in (3, 4) and len(self.raw_value) == 4:
            return struct.unpack("<I", self.raw_value)[0]
        return None


@dataclass
class Node:
    name: NameToken
    attrs: list[Attribute] = field(default_factory=list)
    children: list["Node"] = field(default_factory=list)
    start: int = 0
    end: int = 0

    @property
    def display(self) -> str:
        return self.name.display()

    def attr_map(self, hashes: dict[int, str] | None = None) -> dict[str, Any]:
        return {attr.key.display(hashes): attr.value for attr in self.attrs}


class PropTreeParser:
    def __init__(self, data: bytes):
        self.data = data
        self.offset = 0

    def read_u8(self) -> int:
        if self.offset >= len(self.data):
            raise ValueError("unexpected end of property tree")
        value = self.data[self.offset]
        self.offset += 1
        return value

    def read_u16(self) -> int:
        if self.offset + 2 > len(self.data):
            raise ValueError("unexpected end of property tree")
        value = struct.unpack_from("<H", self.data, self.offset)[0]
        self.offset += 2
        return value

    def read_u32(self) -> int:
        if self.offset + 4 > len(self.data):
            raise ValueError("unexpected end of property tree")
        value = struct.unpack_from("<I", self.data, self.offset)[0]
        self.offset += 4
        return value

    def read_bytes(self, size: int) -> bytes:
        if self.offset + size > len(self.data):
            raise ValueError("unexpected end of property tree")
        value = self.data[self.offset : self.offset + size]
        self.offset += size
        return value

    def read_token(self, node_name: bool) -> NameToken:
        marker = self.read_u8() if node_name else 0
        size = self.read_u8()
        if size == 0:
            return NameToken(marker, None, self.read_u32())
        return NameToken(marker, self.read_bytes(size).decode("utf-8", errors="replace"), None)

    def read_node(self) -> Node:
        start = self.offset
        node = Node(self.read_token(node_name=True), start=start)
        while self.read_u8():
            key = self.read_token(node_name=False)
            value_type = self.read_u8()
            size = self.read_u16()
            node.attrs.append(Attribute(key, value_type, self.read_bytes(size)))
        while self.read_u8():
            node.children.append(self.read_node())
        node.end = self.offset
        return node


def parse_prop_tree(data: bytes) -> Node:
    parser = PropTreeParser(data)
    root = parser.read_node()
    if parser.offset != len(data):
        raise ValueError(f"trailing property-tree bytes at 0x{parser.offset:x}")
    return root


def walk(node: Node):
    yield node
    for child in node.children:
        yield from walk(child)


def format_attr_value(attr: Attribute) -> str:
    value = attr.value
    if isinstance(value, bytes):
        printable = "".join(chr(b) if 32 <= b < 127 else "." for b in value)
        return printable if printable.strip(".") else value.hex()
    if isinstance(value, float):
        return f"{value:.6g}"
    return str(value)


def xml_name(token: NameToken, hashes: dict[int, str], attr: bool = False) -> str:
    if token.text is not None:
        if is_xml_name(token.text):
            return token.text
        if token.hash_value is None:
            return safe_text_xml_name(token.text, attr)
    assert token.hash_value is not None
    known = hashes.get(token.hash_value)
    if known is not None and is_xml_name(known):
        return known
    prefix = "attr" if attr else "tag"
    return f"{prefix}_{token.hash_value:08X}"


def xml_attr_value(attr: Attribute) -> str:
    if attr.value_type in (3, 4) and len(attr.raw_value) == 4:
        return str(attr.unsigned_value)
    if attr.value_type == 5 and len(attr.raw_value) == 4:
        return f"{struct.unpack('<f', attr.raw_value)[0]:.6f}"
    if attr.value_type == 6:
        return attr.raw_value.decode("utf-8", errors="replace").rstrip("\x00")
    if attr.value_type == 2 and len(attr.raw_value) == 1:
        return "1" if attr.raw_value[0] else "0"
    return attr.raw_value.hex()


def decoded_element(node: Node, hashes: dict[int, str]) -> ET.Element:
    element = ET.Element(xml_name(node.name, hashes))
    for attr in node.attrs:
        element.set(xml_name(attr.key, hashes, attr=True), xml_attr_value(attr))
    for child in node.children:
        element.append(decoded_element(child, hashes))
    return element


def build_decoded_xml(data: bytes, hashes: dict[int, str]) -> ET.ElementTree:
    root_node = parse_prop_tree(data)
    root = decoded_element(root_node, hashes)
    ET.indent(root, space="\t")
    return ET.ElementTree(root)


def append_legacy_parts(
    node: Node, parts: list[str], skip_keys: set[str], hashes: dict[int, str]
) -> None:
    for attr in node.attrs:
        key = attr.key.display(hashes)
        value = format_attr_value(attr)
        if key in skip_keys:
            if key == "originalFilename" and value:
                parts.append(strip_virtual_prefix(value))
            continue
        parts.append(f"[{attr.key.legacy_label(hashes)}]")
        if value:
            parts.append(strip_virtual_prefix(value))
    for child in node.children:
        parts.append(f"[{child.name.legacy_label(hashes)}]")
        append_legacy_parts(child, parts, set(), hashes)


def legacy_source_text(node: Node, hashes: dict[int, str]) -> str:
    parts: list[str] = []
    append_legacy_parts(node, parts, {"name", "filename", "originalFilename"}, hashes)
    return "...".join(part for part in parts if part != "")


def object_rows(
    root: Node, carry_devfile: bool, hashes: dict[int, str]
) -> list[tuple[Node, str, str]]:
    rows = []
    last_devfile = ""
    for node in walk(root):
        if node.display != "object":
            continue
        attrs = node.attr_map(hashes)
        filename = attrs.get("filename", "")
        if isinstance(filename, str) and filename:
            last_devfile = strip_virtual_prefix(filename)
        devfile = strip_virtual_prefix(filename) if isinstance(filename, str) else ""
        if not devfile and carry_devfile:
            devfile = last_devfile
        source = legacy_source_text(node, hashes)
        if devfile or source:
            rows.append((node, devfile, source))
    return rows


def build_legacy_xml(
    data: bytes, source_name: str, carry_devfile: bool, hashes: dict[int, str]
) -> ET.ElementTree:
    root_node = parse_prop_tree(data)
    rows = object_rows(root_node, carry_devfile, hashes)
    root = ET.Element("ObjectCollection", {"source": source_name, "objects": str(len(rows))})
    for index, (node, devfile, source) in enumerate(rows):
        attrs = node.attr_map(hashes)
        element = ET.SubElement(
            root,
            "Object",
            {
                "index": str(index),
                "offset": str(node.start),
                "size": str(node.end - node.start),
            },
        )
        if "name" in attrs:
            element.set("name", str(attrs["name"]))
        for key in ("ID", "id", "type", "packageID", "exclusive"):
            if key in attrs:
                element.set(key, str(attrs[key]))
        ET.SubElement(element, "DevFile").text = devfile
        ET.SubElement(element, "SourceFile").text = source
    ET.indent(root, space="  ")
    return ET.ElementTree(root)


def iter_objectcollection_payloads(source: Path, pattern: str):
    if source.is_dir():
        for pak_path in sorted(source.rglob("*.pak")):
            yield from iter_objectcollection_payloads(pak_path, pattern)
        return

    if source.suffix.lower() == ".pak":
        pak = PakFile(source)
        for entry in pak.file_entries:
            name = (entry.name or "").replace("\\", "/")
            if fnmatch.fnmatch(name.lower(), pattern.lower()):
                yield f"{source}:{name}", pak.unpacked_payload(entry)
        return

    source_name = source.name.lower()
    if fnmatch.fnmatch(source_name, pattern.lower()) or source_name.startswith("objectcollection"):
        yield str(source), source.read_bytes()
        return

    raise ValueError(f"{source} is not a directory, pak, or matching objectcollection file")


def write_xml(tree: ET.ElementTree, output: Path | None) -> None:
    if output is None:
        tree.write(sys.stdout.buffer, encoding="utf-8", xml_declaration=True)
        sys.stdout.buffer.write(b"\n")
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    tree.write(output, encoding="utf-8", xml_declaration=True)


def safe_output_name(source_name: str) -> str:
    name = source_name.split(":", 1)[-1].replace("\\", "/").split("/")[-1]
    if name.lower().endswith(".xml"):
        name = name[:-4]
    return f"{name}_objects.xml"


def cmd_dump(args: argparse.Namespace) -> None:
    hashes = merged_hashes(args.hashes)
    matches = list(iter_objectcollection_payloads(args.source, args.entry))
    if not matches:
        raise ValueError(f"no entries matched {args.entry}")
    if len(matches) > 1 and not args.all:
        names = "\n  ".join(name for name, _ in matches[:50])
        more = "" if len(matches) <= 50 else f"\n  ... {len(matches) - 50} more"
        raise ValueError(f"matched {len(matches)} objectcollections; use --all:\n  {names}{more}")

    for source_name, data in matches:
        if args.mode == "objects":
            tree = build_legacy_xml(data, source_name, args.carry_devfile, hashes)
        else:
            tree = build_decoded_xml(data, hashes)
        output = args.output
        if args.all:
            if output is None:
                raise ValueError("--all requires --output to be a directory")
            output = output / safe_output_name(source_name)
        write_xml(tree, output)
        if output is not None:
            print(f"wrote {output}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Parse Trials Fusion binary objectcollection*.xml files from a pak, "
            "an already-extracted file, or a build/datapack directory."
        )
    )
    parser.add_argument("source", type=Path, help="datapack/build directory, .pak, or binary objectcollection*.xml")
    parser.add_argument(
        "--entry",
        default="objectcollection*.xml",
        help="pak entry/file glob to parse (default: objectcollection*.xml)",
    )
    parser.add_argument("-o", "--output", type=Path, help="output XML file, or directory with --all")
    parser.add_argument(
        "--hashes",
        type=Path,
        action="append",
        default=[],
        help=(
            "extra hash CSV to merge; reverse_notes/trials_hashes.csv is loaded "
            "automatically when present"
        ),
    )
    parser.add_argument(
        "--mode",
        choices=("decoded", "objects"),
        default="decoded",
        help="decoded writes the full property tree; objects writes the older flattened object list",
    )
    parser.add_argument("--all", action="store_true", help="dump every matching objectcollection")
    parser.add_argument(
        "--no-carry-devfile",
        dest="carry_devfile",
        action="store_false",
        help="do not reuse the previous non-empty DevFile for blank object records",
    )
    parser.set_defaults(func=cmd_dump, carry_devfile=True)
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
