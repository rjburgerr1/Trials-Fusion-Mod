#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
from pathlib import Path


DEFAULT_HASH_CSV = Path(__file__).resolve().parents[1] / "trials_hashes.csv"
DEFAULT_VTEX_MATCHES = (
    Path.home()
    / "Desktop"
    / "Trials"
    / "TrialsResearch"
    / "TrialsResearchSheets"
    / "vtex_matches.csv"
)


def g4_hash(text: str, uppercase: bool = False) -> int:
    data = (text.upper() if uppercase else text).encode("ascii")
    seed = len(data)
    value = 0
    for byte in data:
        value = (value + byte * seed) & 0xFFFFFFFF
        seed = ((seed & 0xFFFF) * 18000 + (seed >> 16)) & 0xFFFFFFFF
    return value


def load_known_hashes(path: Path) -> dict[int, str]:
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


def parse_targets(values: list[str]) -> list[int]:
    targets: list[int] = []
    for value in values:
        cleaned = value.strip().lower()
        if cleaned.startswith("0x"):
            targets.append(int(cleaned, 16))
        else:
            targets.append(int(cleaned, 16))
    return targets


def add_candidate(candidates: set[str], value: str) -> None:
    text = value.strip()
    if not text:
        return
    if any(ord(ch) > 127 for ch in text):
        return
    if len(text) > 80:
        return
    candidates.add(text)


def add_token_family(candidates: set[str], token: str) -> None:
    core = token.strip().replace(" ", "_").replace("-", "_")
    if not core:
        return

    variants = {
        core,
        core.lower(),
        core.upper(),
        core.title(),
        core.replace("_", ""),
    }

    prefixes = (
        "",
        "lod_",
        "surface_",
        "mesh_",
        "uv_",
        "uvmap_",
        "texcoord_",
        "vtex_",
        "vt_",
        "atlas_",
        "tile_",
        "virtual_texture_",
        "render_",
    )
    suffixes = (
        "",
        "_list",
        "_names",
        "_name",
        "_data",
        "_info",
        "_entries",
        "_entry",
        "_map",
        "_maps",
        "_range",
        "_ranges",
        "_start",
        "_end",
        "_count",
        "_mode",
        "_type",
    )

    for variant in variants:
        add_candidate(candidates, variant)
        for prefix in prefixes:
            add_candidate(candidates, prefix + variant)
        for suffix in suffixes:
            add_candidate(candidates, variant + suffix)
        for prefix in prefixes:
            for suffix in suffixes:
                add_candidate(candidates, prefix + variant + suffix)


def load_vtex_tokens(path: Path, filters: list[str]) -> set[str]:
    candidates: set[str] = set()
    if not path.exists():
        return candidates

    lowered_filters = [item.lower() for item in filters]
    with path.open(encoding="utf-8", newline="") as handle:
        reader = csv.reader(handle)
        for row in reader:
            joined = ",".join(row).lower()
            if lowered_filters and not any(item in joined for item in lowered_filters):
                continue
            for cell in row:
                add_candidate(candidates, cell)
                if cell.isidentifier() or "_" in cell or "-" in cell:
                    add_token_family(candidates, cell)
    return candidates


def load_candidate_file(path: Path) -> set[str]:
    candidates: set[str] = set()
    with path.open(encoding="utf-8-sig") as handle:
        for line in handle:
            text = line.strip()
            if not text or text.startswith("#"):
                continue
            add_candidate(candidates, text)
    return candidates


def themed_candidates() -> set[str]:
    candidates: set[str] = set()

    exact = [
        "LODs",
        "lods",
        "LODNames",
        "lodNames",
        "SurfaceNames",
        "surfaceNames",
        "Surfaces",
        "surfaces",
        "UVMap",
        "UVMaps",
        "uvmap",
        "uvmaps",
        "TexCoord",
        "TexCoord0",
        "TexCoords",
        "texcoord",
        "texcoords",
        "renderData",
        "renderInfo",
        "surfaceData",
        "surfaceEntries",
        "meshData",
        "meshEntries",
        "virtualTexture",
        "virtualTextures",
        "vtexData",
        "atlasData",
        "atlasEntries",
        "atlasRanges",
        "tileRanges",
        "tileRange",
        "tileStart",
        "tileEnd",
        "rangeStart",
        "rangeEnd",
        "lodMode",
        "surfaceMode",
        "renderMode",
        "type",
        "mode",
        "kind",
    ]

    for value in exact:
        add_candidate(candidates, value)
        add_token_family(candidates, value)

    for token in ("x", "y", "w", "h", "u", "v", "index", "layer", "start", "end", "count"):
        add_token_family(candidates, token)

    return candidates


def cmd_probe(args: argparse.Namespace) -> int:
    targets = parse_targets(args.hash)
    known = load_known_hashes(args.hashes) if args.hashes.exists() else {}

    candidates: set[str] = set()
    if not args.only_candidates_file:
        candidates.update(themed_candidates())
    if args.candidates_file:
        candidates.update(load_candidate_file(args.candidates_file))
    if args.vtex_matches and not args.only_candidates_file:
        candidates.update(load_vtex_tokens(args.vtex_matches, args.filter))

    matches: dict[int, list[str]] = {target: [] for target in targets}
    for candidate in sorted(candidates):
        try:
            hashed = g4_hash(candidate)
        except UnicodeEncodeError:
            continue
        if hashed in matches:
            matches[hashed].append(candidate)

    print(f"candidate_count={len(candidates)}")
    for target in targets:
        known_name = known.get(target)
        print(f"0x{target:08X}")
        if known_name:
            print(f"  known={known_name}")
        if matches[target]:
            for candidate in matches[target]:
                print(f"  match={candidate}")
        else:
            print("  match=<none>")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Generate objectcollection-themed hash candidates and test them against "
            "one or more target hashes."
        )
    )
    parser.add_argument("hash", nargs="+", help="target hash values such as FCC12101 or 0xFCC12101")
    parser.add_argument(
        "--hashes",
        type=Path,
        default=DEFAULT_HASH_CSV,
        help="known hash CSV for showing existing names",
    )
    parser.add_argument(
        "--vtex-matches",
        type=Path,
        default=DEFAULT_VTEX_MATCHES,
        help="optional research CSV to mine for related tokens",
    )
    parser.add_argument(
        "--filter",
        action="append",
        default=["mesh", "surface", "uvmap", "texcoord", "lod", "virtual", "atlas", "tile"],
        help="substring filter for rows mined from the vtex matches CSV",
    )
    parser.add_argument(
        "--candidates-file",
        type=Path,
        help="optional newline-delimited candidate strings to test",
    )
    parser.add_argument(
        "--only-candidates-file",
        action="store_true",
        help="only test strings from --candidates-file, skipping generated and mined candidates",
    )
    parser.set_defaults(func=cmd_probe)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
