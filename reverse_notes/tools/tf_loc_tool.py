#!/usr/bin/env python3
import argparse
import copy
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


NS = {"o": "http://schemas.ubisoft.com/oasis/2009/extractor"}
ET.register_namespace("", NS["o"])
ET.register_namespace("xsi", "http://www.w3.org/2001/XMLSchema-instance")


def read_tree(path: Path) -> ET.ElementTree:
    data = path.read_bytes()
    for encoding in ("utf-16", "utf-16-le", "utf-8-sig"):
        try:
            text = data.decode(encoding)
            return ET.ElementTree(ET.fromstring(text))
        except (UnicodeError, ET.ParseError):
            continue
    return ET.parse(path)


def clone_entry(
    input_path: Path,
    output_path: Path,
    source_name: str,
    new_name: str,
    new_text: str,
) -> None:
    tree = read_tree(input_path)
    root = tree.getroot()
    entries = root.findall(".//o:d", NS)
    source = None
    for entry in entries:
        if entry.get("name") == source_name:
            source = entry
            break
    if source is None:
        raise ValueError(f"source localization entry not found: {source_name}")

    parent = None
    for candidate in root.iter():
        if source in list(candidate):
            parent = candidate
            break
    if parent is None:
        raise ValueError("source localization entry has no parent")

    sibling_entries = [child for child in list(parent) if child.tag.endswith("d")]
    max_d_id = max(int(entry.get("id", "0")) for entry in sibling_entries)
    max_l_id = max(
        int(line.get("id", "0"))
        for entry in sibling_entries
        for line in entry.findall("o:l", NS)
    )
    max_order = max(int(entry.get("orderIndex", "0")) for entry in sibling_entries)
    max_tag_number = 0
    tag_prefix = None
    for entry in sibling_entries:
        tag = entry.get("tag", "")
        match = re.match(r"^(.*?)(\d+)$", tag)
        if not match:
            continue
        tag_prefix = match.group(1)
        max_tag_number = max(max_tag_number, int(match.group(2)))

    clone = copy.deepcopy(source)
    clone.set("id", str(max_d_id + 1))
    clone.set("name", new_name)
    if tag_prefix is not None:
        clone.set("tag", f"{tag_prefix}{max_tag_number + 1:05d}")
    else:
        clone.set("tag", f"ME_CODEX_{new_name.upper()[:32]}")
    clone.set("orderIndex", str(max_order + 1))
    for line in clone.findall("o:l", NS):
        line.set("id", str(max_l_id + 1))
        line.set("text", new_text)
        line.attrib.pop("parenthetical", None)
        max_l_id += 1

    parent.append(clone)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    tree.write(output_path, encoding="utf-16", xml_declaration=True)
    print(f"cloned localization {source_name} -> {new_name}")


def rename_text(input_path: Path, output_path: Path, name: str, text: str) -> None:
    tree = read_tree(input_path)
    root = tree.getroot()
    for entry in root.findall(".//o:d", NS):
        if entry.get("name") != name:
            continue
        for line in entry.findall("o:l", NS):
            line.set("text", text)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        tree.write(output_path, encoding="utf-16", xml_declaration=True)
        print(f"renamed localization text for {name}")
        return
    raise ValueError(f"localization entry not found: {name}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Trials Fusion localization XML helper")
    sub = parser.add_subparsers(required=True)

    clone = sub.add_parser("clone-entry")
    clone.add_argument("input", type=Path)
    clone.add_argument("source_name")
    clone.add_argument("new_name")
    clone.add_argument("new_text")
    clone.add_argument("output", type=Path)
    clone.set_defaults(func=lambda args: clone_entry(
        args.input, args.output, args.source_name, args.new_name, args.new_text
    ))

    rename = sub.add_parser("rename-text")
    rename.add_argument("input", type=Path)
    rename.add_argument("name")
    rename.add_argument("text")
    rename.add_argument("output", type=Path)
    rename.set_defaults(func=lambda args: rename_text(
        args.input, args.output, args.name, args.text
    ))

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
