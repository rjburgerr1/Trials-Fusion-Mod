#!/usr/bin/env python3
import argparse
import json
import math
import struct
import sys
from pathlib import Path


OFF_TRANSLATION = (-84, -80, -76)
OFF_SCALE = (-48, -44, -40)
OFF_ROTATION = (-36, -32, -28, -24)


def find_ref(data: bytes, name: str) -> int:
    needle = name.encode("ascii")
    pos = data.find(needle)
    if pos < 0:
        raise ValueError(f"mesh reference not found: {name}")
    if data.find(needle, pos + 1) >= 0:
        raise ValueError(f"mesh reference is ambiguous: {name}")
    return pos


def read_floats(data: bytes, base: int, offsets: tuple[int, ...]) -> tuple[float, ...]:
    return tuple(struct.unpack_from("<f", data, base + off)[0] for off in offsets)


def write_floats(data: bytearray, base: int, offsets: tuple[int, ...], values: tuple[float, ...]) -> None:
    if len(offsets) != len(values):
        raise ValueError("offset/value count mismatch")
    for off, value in zip(offsets, values):
        struct.pack_into("<f", data, base + off, value)


def z_rotation_quat(degrees: float) -> tuple[float, float, float, float]:
    half = math.radians(degrees) / 2.0
    return (0.0, 0.0, math.sin(half), math.cos(half))


def cmd_inspect(args: argparse.Namespace) -> None:
    data = args.gfx.read_bytes()
    base = find_ref(data, args.name)
    print(f"name={args.name} offset=0x{base:x}")
    print("translation", read_floats(data, base, OFF_TRANSLATION))
    print("scale      ", read_floats(data, base, OFF_SCALE))
    print("rotation   ", read_floats(data, base, OFF_ROTATION))


def cmd_patch(args: argparse.Namespace) -> None:
    data = bytearray(args.gfx.read_bytes())
    base = find_ref(data, args.name)
    if args.translation:
        write_floats(data, base, OFF_TRANSLATION, tuple(args.translation))
    if args.scale:
        write_floats(data, base, OFF_SCALE, tuple(args.scale))
    if args.rotation_z is not None:
        write_floats(data, base, OFF_ROTATION, z_rotation_quat(args.rotation_z))
    args.output.write_bytes(data)
    print(f"wrote {args.output}")


def cmd_batch(args: argparse.Namespace) -> None:
    data = bytearray(args.gfx.read_bytes())
    specs = json.loads(args.spec.read_text())
    if not isinstance(specs, list):
        raise ValueError("batch spec must be a JSON list")

    for spec in specs:
        if not isinstance(spec, dict):
            raise ValueError("each batch item must be an object")
        name = spec["name"]
        base = find_ref(data, name)
        if "translation" in spec:
            write_floats(data, base, OFF_TRANSLATION, tuple(float(v) for v in spec["translation"]))
        if "scale" in spec:
            write_floats(data, base, OFF_SCALE, tuple(float(v) for v in spec["scale"]))
        if "rotation_z" in spec:
            write_floats(data, base, OFF_ROTATION, z_rotation_quat(float(spec["rotation_z"])))
        print(f"patched {name}")

    args.output.write_bytes(data)
    print(f"wrote {args.output}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Inspect or patch Trials Fusion GFX mesh transforms")
    sub = parser.add_subparsers(required=True)

    inspect = sub.add_parser("inspect")
    inspect.add_argument("gfx", type=Path)
    inspect.add_argument("name")
    inspect.set_defaults(func=cmd_inspect)

    patch = sub.add_parser("patch")
    patch.add_argument("gfx", type=Path)
    patch.add_argument("name")
    patch.add_argument("output", type=Path)
    patch.add_argument("--translation", nargs=3, type=float, metavar=("X", "Y", "Z"))
    patch.add_argument("--scale", nargs=3, type=float, metavar=("X", "Y", "Z"))
    patch.add_argument("--rotation-z", type=float, help="write a Z-axis quaternion in degrees")
    patch.set_defaults(func=cmd_patch)

    batch = sub.add_parser("batch")
    batch.add_argument("gfx", type=Path)
    batch.add_argument("spec", type=Path)
    batch.add_argument("output", type=Path)
    batch.set_defaults(func=cmd_batch)

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
