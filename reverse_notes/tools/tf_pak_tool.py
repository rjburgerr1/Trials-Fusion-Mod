#!/usr/bin/env python3
import argparse
import json
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path


MAGIC = 0x12345678
HEADER = struct.Struct("<III")
ENTRY = struct.Struct("<IIIBI")


def g4_hash(text: str, uppercase: bool = False) -> int:
    data = (text.upper() if uppercase else text).encode("ascii")
    seed = len(data)
    h = 0
    for ch in data:
        h = (h + ch * seed) & 0xFFFFFFFF
        seed = ((seed & 0xFFFF) * 18000 + (seed >> 16)) & 0xFFFFFFFF
    return h


def g4_path_hash(path: str) -> int:
    return g4_hash(path.replace("\\", "/").lstrip("/"), uppercase=True)


@dataclass
class PakEntry:
    index: int
    path_hash: int
    stored_size: int
    unpacked_size: int
    flags: int
    data_offset: int
    name: str | None = None

    @property
    def is_compressed(self) -> bool:
        return self.flags == 0x01

    @property
    def is_protected(self) -> bool:
        return bool(self.flags & 0x10)


class PakFile:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        self.data_start = 0
        self.entries: list[PakEntry] = []
        self.names: list[str] = []
        self._parse()

    def _parse(self) -> None:
        magic, data_start, entry_count = HEADER.unpack_from(self.data, 0)
        if magic != MAGIC:
            raise ValueError(f"bad magic 0x{magic:08x}, expected 0x{MAGIC:08x}")
        expected_data_start = HEADER.size + entry_count * ENTRY.size
        if data_start != expected_data_start:
            raise ValueError(
                f"unexpected data_start {data_start}, expected {expected_data_start}"
            )
        self.data_start = data_start
        offset = HEADER.size
        for index in range(entry_count):
            path_hash, stored_size, unpacked_size, flags, data_offset = ENTRY.unpack_from(
                self.data, offset
            )
            self.entries.append(
                PakEntry(
                    index=index,
                    path_hash=path_hash,
                    stored_size=stored_size,
                    unpacked_size=unpacked_size,
                    flags=flags,
                    data_offset=data_offset,
                )
            )
            offset += ENTRY.size
        self.names = self._parse_name_list()
        for entry, name in zip(self.entries, self.names):
            entry.name = name

    @property
    def filename_entry(self) -> PakEntry:
        return self.entries[-1]

    @property
    def file_entries(self) -> list[PakEntry]:
        return self.entries[:-1]

    def raw_payload(self, entry: PakEntry) -> bytes:
        start = entry.data_offset
        end = start + entry.stored_size
        if start < self.data_start or end > len(self.data):
            raise ValueError(f"entry {entry.index} points outside archive")
        return self.data[start:end]

    def unpacked_payload(self, entry: PakEntry) -> bytes:
        raw = self.raw_payload(entry)
        if entry.flags == 0x00:
            return raw
        if entry.flags == 0x01:
            return zlib.decompress(raw)
        raise ValueError(f"entry {entry.index} has unsupported flags 0x{entry.flags:02x}")

    def _parse_name_list(self) -> list[str]:
        entry = self.filename_entry
        raw = self.raw_payload(entry)
        if entry.flags == 0x01:
            raw = zlib.decompress(raw)
        if len(raw) < 4:
            raise ValueError("filename list is too short")
        count = struct.unpack_from("<I", raw, 0)[0]
        offset = 4
        names: list[str] = []
        for _ in range(count):
            if offset + 2 > len(raw):
                raise ValueError("filename list ended inside length field")
            size = struct.unpack_from("<H", raw, offset)[0]
            offset += 2
            end = offset + size
            if end > len(raw):
                raise ValueError("filename list ended inside name bytes")
            names.append(raw[offset:end].decode("utf-8"))
            offset = end
        if len(names) not in (len(self.entries), len(self.entries) - 1):
            raise ValueError(
                f"filename count {len(names)} does not match entries {len(self.entries)}"
            )
        return names

    def to_listing(self) -> list[dict[str, object]]:
        listing = []
        for entry in self.entries:
            name_hash = g4_path_hash(entry.name) if entry.name else None
            listing.append(
                {
                    "index": entry.index,
                    "name": entry.name,
                    "path_hash": f"0x{entry.path_hash:08x}",
                    "computed_hash": f"0x{name_hash:08x}" if name_hash is not None else None,
                    "hash_ok": name_hash == entry.path_hash if name_hash is not None else None,
                    "stored_size": entry.stored_size,
                    "unpacked_size": entry.unpacked_size,
                    "flags": f"0x{entry.flags:02x}",
                    "data_offset": entry.data_offset,
                }
            )
        return listing


def encode_filename_list(names: list[str]) -> bytes:
    out = bytearray(struct.pack("<I", len(names)))
    for name in names:
        data = name.encode("utf-8")
        if len(data) > 0xFFFF:
            raise ValueError(f"filename too long: {name}")
        out += struct.pack("<H", len(data))
        out += data
    return bytes(out)


def make_payload(data: bytes, flags: int) -> tuple[bytes, int]:
    if flags == 0x00:
        return data, len(data)
    if flags == 0x01:
        return zlib.compress(data), len(data)
    raise ValueError(f"cannot write unsupported flags 0x{flags:02x}")


def write_rebuilt(
    source: PakFile,
    output: Path,
    replacements: dict[str, tuple[bytes, int]],
    additions: list[tuple[str, bytes, int]],
) -> None:
    old_regular = source.file_entries
    old_names = source.names[: len(old_regular)]
    payload_rows: list[tuple[int, str | None, bytes, int, int]] = []

    for entry, name in zip(old_regular, old_names):
        normalized = name.replace("\\", "/") if name else None
        if normalized in replacements:
            unpacked_data, flags = replacements[normalized]
            raw, unpacked_size = make_payload(unpacked_data, flags)
            payload_rows.append((g4_path_hash(normalized), normalized, raw, unpacked_size, flags))
        else:
            payload_rows.append(
                (
                    entry.path_hash,
                    name,
                    source.raw_payload(entry),
                    entry.unpacked_size,
                    entry.flags,
                )
            )

    existing_names = {name.replace("\\", "/") for name in old_names if name}
    for name, unpacked_data, flags in additions:
        normalized = name.replace("\\", "/").lstrip("/")
        if normalized in existing_names:
            raise ValueError(f"entry already exists, use replace instead: {normalized}")
        raw, unpacked_size = make_payload(unpacked_data, flags)
        payload_rows.append((g4_path_hash(normalized), normalized, raw, unpacked_size, flags))
        existing_names.add(normalized)

    filename_names = [row[1] for row in payload_rows if row[1] is not None]
    if len(source.names) == len(source.entries):
        final_name = source.names[-1]
        if final_name is not None:
            filename_names.append(final_name)
    filename_raw, filename_unpacked_size = make_payload(
        encode_filename_list(filename_names), source.filename_entry.flags
    )
    payload_rows.append(
        (
            source.filename_entry.path_hash,
            source.filename_entry.name,
            filename_raw,
            filename_unpacked_size,
            source.filename_entry.flags,
        )
    )

    entry_count = len(payload_rows)
    data_start = HEADER.size + entry_count * ENTRY.size
    table = bytearray()
    payload = bytearray()
    current_offset = data_start
    for path_hash, _name, raw, unpacked_size, flags in payload_rows:
        table += ENTRY.pack(path_hash, len(raw), unpacked_size, flags, current_offset)
        payload += raw
        current_offset += len(raw)

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(HEADER.pack(MAGIC, data_start, entry_count) + table + payload)


def find_entry(pak: PakFile, path_or_hash: str) -> PakEntry:
    wanted = path_or_hash.replace("\\", "/").lstrip("/")
    wanted_hash = None
    if wanted.lower().startswith("0x"):
        wanted_hash = int(wanted, 16)
    else:
        wanted_hash = g4_path_hash(wanted)
    matches = [
        entry
        for entry in pak.entries
        if entry.path_hash == wanted_hash
        or (entry.name and entry.name.replace("\\", "/").lower() == wanted.lower())
    ]
    if not matches:
        raise ValueError(f"entry not found: {path_or_hash}")
    if len(matches) > 1:
        names = ", ".join(entry.name or f"#{entry.index}" for entry in matches)
        raise ValueError(f"ambiguous entry {path_or_hash}: {names}")
    return matches[0]


def cmd_list(args: argparse.Namespace) -> None:
    pak = PakFile(args.pak)
    listing = pak.to_listing()
    if args.json:
        print(json.dumps(listing, indent=2))
        return
    print(f"{args.pak}")
    print(f"entries={len(pak.entries)} files={len(pak.file_entries)}")
    for item in listing:
        status = "ok" if item["hash_ok"] in (True, None) else "BAD"
        print(
            f"{item['index']:4d} {item['flags']:>4} {item['stored_size']:9d} "
            f"{item['unpacked_size']:9d} {item['path_hash']} {status:>3} {item['name']}"
        )


def cmd_extract(args: argparse.Namespace) -> None:
    pak = PakFile(args.pak)
    entries = pak.file_entries if args.all else [find_entry(pak, args.entry)]
    for entry in entries:
        if entry.is_protected:
            raise ValueError(f"entry {entry.index} is protected")
        data = pak.unpacked_payload(entry) if args.unpack else pak.raw_payload(entry)
        if args.all:
            if not entry.name:
                raise ValueError(f"entry {entry.index} has no filename")
            output = args.output / entry.name.replace("/", "\\")
        else:
            output = args.output
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(data)
        print(f"wrote {output} ({len(data)} bytes)")


def cmd_add(args: argparse.Namespace) -> None:
    pak = PakFile(args.pak)
    flags = 0x01 if args.zlib else 0x00
    additions = [(args.path.replace("\\", "/").lstrip("/"), args.input.read_bytes(), flags)]
    write_rebuilt(pak, args.output, {}, additions)
    print(f"wrote {args.output}")


def cmd_replace(args: argparse.Namespace) -> None:
    pak = PakFile(args.pak)
    entry = find_entry(pak, args.path)
    if not entry.name:
        raise ValueError("replacement target must have a filename")
    flags = entry.flags if args.keep_flags else (0x01 if args.zlib else 0x00)
    replacements = {entry.name.replace("\\", "/"): (args.input.read_bytes(), flags)}
    write_rebuilt(pak, args.output, replacements, [])
    print(f"wrote {args.output}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Trials Fusion pak inspector/rebuilder")
    sub = parser.add_subparsers(required=True)

    list_cmd = sub.add_parser("list")
    list_cmd.add_argument("pak", type=Path)
    list_cmd.add_argument("--json", action="store_true")
    list_cmd.set_defaults(func=cmd_list)

    extract_cmd = sub.add_parser("extract")
    extract_cmd.add_argument("pak", type=Path)
    extract_cmd.add_argument("entry", nargs="?")
    extract_cmd.add_argument("output", type=Path)
    extract_cmd.add_argument("--all", action="store_true")
    extract_cmd.add_argument("--unpack", action="store_true", help="inflate zlib entries")
    extract_cmd.set_defaults(func=cmd_extract)

    add_cmd = sub.add_parser("add")
    add_cmd.add_argument("pak", type=Path)
    add_cmd.add_argument("path")
    add_cmd.add_argument("input", type=Path)
    add_cmd.add_argument("output", type=Path)
    add_cmd.add_argument("--zlib", action="store_true")
    add_cmd.set_defaults(func=cmd_add)

    replace_cmd = sub.add_parser("replace")
    replace_cmd.add_argument("pak", type=Path)
    replace_cmd.add_argument("path")
    replace_cmd.add_argument("input", type=Path)
    replace_cmd.add_argument("output", type=Path)
    replace_cmd.add_argument("--zlib", action="store_true")
    replace_cmd.add_argument("--keep-flags", action="store_true")
    replace_cmd.set_defaults(func=cmd_replace)

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
