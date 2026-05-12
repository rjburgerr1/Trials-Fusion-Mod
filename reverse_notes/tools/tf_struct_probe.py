#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math
import re
import struct
from pathlib import Path


ASCII_RE = re.compile(rb"[\x20-\x7e]{4,}")


def read_u32(data: bytes, offset: int) -> int | None:
    if offset < 0 or offset + 4 > len(data):
        return None
    return struct.unpack_from("<I", data, offset)[0]


def read_i32(data: bytes, offset: int) -> int | None:
    if offset < 0 or offset + 4 > len(data):
        return None
    return struct.unpack_from("<i", data, offset)[0]


def read_f32(data: bytes, offset: int) -> float | None:
    if offset < 0 or offset + 4 > len(data):
        return None
    return struct.unpack_from("<f", data, offset)[0]


def fmt_f32(value: float | None) -> str:
    if value is None:
        return ""
    if not math.isfinite(value):
        return str(value)
    if abs(value) >= 100000 or (0 < abs(value) < 0.00001):
        return f"{value:.6e}"
    return f"{value:.6g}"


def printable_at(data: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(data):
        return ""
    match = ASCII_RE.match(data, offset)
    if match is None:
        return ""
    return match.group(0).decode("ascii", errors="replace")


def find_refs(data: bytes, needles: list[str]) -> list[tuple[str, int]]:
    refs: list[tuple[str, int]] = []
    for needle in needles:
        raw = needle.encode("ascii")
        start = 0
        while True:
            pos = data.find(raw, start)
            if pos < 0:
                break
            refs.append((needle, pos))
            start = pos + 1
    refs.sort(key=lambda item: item[1])
    return refs


def auto_refs(data: bytes, filters: list[str]) -> list[tuple[str, int]]:
    refs: list[tuple[str, int]] = []
    lower_filters = [item.lower() for item in filters]
    for match in ASCII_RE.finditer(data):
        text = match.group(0).decode("ascii", errors="replace")
        lower = text.lower()
        if lower_filters and not any(item in lower for item in lower_filters):
            continue
        refs.append((text, match.start()))
    return refs


def dump_window(data: bytes, base: int, start: int, end: int, step: int) -> list[str]:
    lines = [
        "rel,abs,hex,u32,i32,f32,ascii",
    ]
    for rel in range(start, end + 1, step):
        off = base + rel
        if off < 0 or off >= len(data):
            continue
        chunk = data[off : off + 4]
        hex_text = chunk.hex(" ")
        u32 = read_u32(data, off)
        i32 = read_i32(data, off)
        f32 = read_f32(data, off)
        ascii_text = printable_at(data, off)
        lines.append(
            f"{rel:+d},0x{off:x},{hex_text},"
            f"{'' if u32 is None else u32},"
            f"{'' if i32 is None else i32},"
            f"{fmt_f32(f32)},"
            f"{ascii_text}"
        )
    return lines


def cmd_refs(args: argparse.Namespace) -> None:
    data = args.input.read_bytes()
    refs = find_refs(data, args.needle) if args.needle else auto_refs(data, args.filter)
    for text, off in refs:
        print(f"0x{off:08x} {text}")


def cmd_window(args: argparse.Namespace) -> None:
    data = args.input.read_bytes()
    refs = find_refs(data, args.needle) if args.needle else auto_refs(data, args.filter)
    if args.index >= len(refs):
        raise ValueError(f"reference index {args.index} out of range ({len(refs)} refs)")
    text, base = refs[args.index]
    print(f"# {args.input.name} ref=0x{base:x} text={text}")
    print("\n".join(dump_window(data, base, args.start, args.end, args.step)))


def cmd_compare(args: argparse.Namespace) -> None:
    rows: list[tuple[Path, str, int, bytes]] = []
    for path in args.input:
        data = path.read_bytes()
        refs = find_refs(data, [args.needle])
        if not refs:
            print(f"# missing {args.needle}: {path}")
            continue
        text, base = refs[0]
        rows.append((path, text, base, data))

    header = ["rel"]
    for path, _text, base, _data in rows:
        header += [f"{path.name}@0x{base:x}:hex", "u32", "f32", "ascii"]
    print(",".join(header))
    for rel in range(args.start, args.end + 1, args.step):
        cells = [f"{rel:+d}"]
        for _path, _text, base, data in rows:
            off = base + rel
            chunk = data[off : off + 4] if 0 <= off < len(data) else b""
            cells += [
                chunk.hex(" "),
                str(read_u32(data, off) or ""),
                fmt_f32(read_f32(data, off)),
                printable_at(data, off).replace(",", ";"),
            ]
        print(",".join(cells))


def parse_mdl_surface(data: bytes, name_offset: int) -> dict[str, object]:
    length = read_u32(data, name_offset - 4)
    if length is None:
        raise ValueError(f"bad surface-name offset 0x{name_offset:x}")
    name = data[name_offset : name_offset + length].decode("ascii", errors="replace")
    pos = name_offset + length
    values: dict[str, object] = {
        "name": name,
        "name_offset": name_offset,
        "unk_after_name_0": read_u32(data, pos),
        "unk_after_name_4": read_u32(data, pos + 4),
        "unk_after_name_8": read_u32(data, pos + 8),
        "unk_after_name_12": read_u32(data, pos + 12),
        "range": read_f32(data, pos + 16),
        "unk_after_range": read_u32(data, pos + 20),
        "min": (
            read_f32(data, pos + 24),
            read_f32(data, pos + 28),
            read_f32(data, pos + 32),
        ),
        "max": (
            read_f32(data, pos + 36),
            read_f32(data, pos + 40),
            read_f32(data, pos + 44),
        ),
        "unk_after_bounds": read_u32(data, pos + 48),
        "geom_offset_or_hash": read_u32(data, pos + 52),
    }
    material_len_pos = pos + 56
    material_len = read_u16(data, material_len_pos)
    if material_len is not None:
        material_start = material_len_pos + 2
        material_end = material_start + material_len
        if material_end <= len(data):
            values["material"] = data[material_start:material_end].decode(
                "ascii", errors="replace"
            )
            values["after_material_offset"] = material_end
            values["after_material_bytes"] = data[material_end : material_end + 8].hex(" ")
    return values


def read_u16(data: bytes, offset: int) -> int | None:
    if offset < 0 or offset + 2 > len(data):
        return None
    return struct.unpack_from("<H", data, offset)[0]


def cmd_mdl(args: argparse.Namespace) -> None:
    data = args.input.read_bytes()
    print(f"# {args.input.name} size={len(data)}")
    if data.startswith(b"OBJ\x01VER01"):
        print(f"version={data[:9].decode('ascii', errors='replace')}")
        print(f"unk_u32_0x09={read_u32(data, 9)}")
        print(f"lod_set={data[14:19].decode('ascii', errors='replace')} count={read_u32(data, 19)}")
        print(f"lod_tag={data[24:29].decode('ascii', errors='replace')}")
        print(f"model_scale=({fmt_f32(read_f32(data, 29))}, {fmt_f32(read_f32(data, 33))}, {fmt_f32(read_f32(data, 37))})")

    refs = auto_refs(data, [args.surface])
    for text, off in refs:
        if text != args.surface:
            continue
        surface = parse_mdl_surface(data, off)
        print(
            "surface "
            f"name={surface['name']} off=0x{surface['name_offset']:x} "
            f"range={fmt_f32(surface['range'])} "
            f"min={tuple(fmt_f32(v) for v in surface['min'])} "
            f"max={tuple(fmt_f32(v) for v in surface['max'])} "
            f"material={surface.get('material')} "
            f"after=0x{surface.get('after_material_offset', 0):x} "
            f"after_bytes={surface.get('after_material_bytes')}"
        )


def cmd_gfx(args: argparse.Namespace) -> None:
    data = args.input.read_bytes()
    refs = find_refs(data, args.needle) if args.needle else auto_refs(data, args.filter)
    print(f"# {args.input.name} size={len(data)} refs={len(refs)}")
    for text, base in refs:
        translation = (
            read_f32(data, base - 84),
            read_f32(data, base - 80),
            read_f32(data, base - 76),
        )
        scale = (
            read_f32(data, base - 48),
            read_f32(data, base - 44),
            read_f32(data, base - 40),
        )
        rotation = (
            read_f32(data, base - 36),
            read_f32(data, base - 32),
            read_f32(data, base - 28),
            read_f32(data, base - 24),
        )
        print(
            f"ref={text} off=0x{base:x} "
            f"translation={tuple(fmt_f32(v) for v in translation)} "
            f"scale={tuple(fmt_f32(v) for v in scale)} "
            f"rotation={tuple(fmt_f32(v) for v in rotation)} "
            f"radius_or_extent={fmt_f32(read_f32(data, base - 20))} "
            f"variation_hash=0x{read_u32(data, base - 16) or 0:08x} "
            f"resource_id=0x{read_u32(data, base - 12) or 0:08x} "
            f"name_len={read_u32(data, base - 4)} "
            f"post_id=0x{read_u32(data, base + len(text) + 28) or 0:08x}"
        )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Probe binary structures around ASCII refs.")
    sub = parser.add_subparsers(required=True)

    refs = sub.add_parser("refs")
    refs.add_argument("input", type=Path)
    refs.add_argument("needle", nargs="*")
    refs.add_argument("--filter", action="append", default=[])
    refs.set_defaults(func=cmd_refs)

    window = sub.add_parser("window")
    window.add_argument("input", type=Path)
    window.add_argument("needle", nargs="*")
    window.add_argument("--filter", action="append", default=[])
    window.add_argument("--index", type=int, default=0)
    window.add_argument("--start", type=int, default=-128)
    window.add_argument("--end", type=int, default=96)
    window.add_argument("--step", type=int, default=4)
    window.set_defaults(func=cmd_window)

    compare = sub.add_parser("compare")
    compare.add_argument("needle")
    compare.add_argument("input", type=Path, nargs="+")
    compare.add_argument("--start", type=int, default=-128)
    compare.add_argument("--end", type=int, default=96)
    compare.add_argument("--step", type=int, default=4)
    compare.set_defaults(func=cmd_compare)

    mdl = sub.add_parser("mdl")
    mdl.add_argument("input", type=Path)
    mdl.add_argument("--surface", default="visible")
    mdl.set_defaults(func=cmd_mdl)

    gfx = sub.add_parser("gfx")
    gfx.add_argument("input", type=Path)
    gfx.add_argument("needle", nargs="*")
    gfx.add_argument("--filter", action="append", default=[])
    gfx.set_defaults(func=cmd_gfx)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    args.func(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
