#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
from collections import Counter, defaultdict
from pathlib import Path


def sniff_type(path_text: str, ascii_sample: str) -> str:
    lower = path_text.lower()
    if lower.endswith(".gfx"):
        return "gfx"
    if lower.endswith(".mdl"):
        return "mdl"
    if lower.endswith(".xml"):
        return "xml"
    if lower.endswith(".pak"):
        return "pak"
    if lower.endswith(".swf"):
        return "swf"
    if lower.endswith(".tex"):
        return "tex"
    if ascii_sample.startswith("GFX2"):
        return "gfx"
    if ascii_sample.startswith("<?xml") or ascii_sample.startswith("<"):
        return "xml"
    if ascii_sample.startswith("FWS") or ascii_sample.startswith("CWS"):
        return "swf"
    if ascii_sample.startswith("FSB5"):
        return "fsb"
    if ascii_sample.startswith("T5X"):
        return "mdl?"
    if ascii_sample.startswith("T8X"):
        return "mdl?"
    return "unknown"


def matches_filters(path_text: str, needles: list[str]) -> bool:
    if not needles:
        return True
    lower = path_text.lower()
    return any(needle in lower for needle in needles)


def load_package_api(csv_path: Path, needles: list[str]) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    with csv_path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            path_text = row.get("arg0_string", "")
            if matches_filters(path_text, needles):
                rows.append(row)
    return rows


def load_streams(csv_path: Path, needles: list[str]) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    with csv_path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            path_text = row.get("data_patch_name", "")
            if matches_filters(path_text, needles):
                rows.append(row)
    return rows


def summarize_package_api(rows: list[dict[str, str]]) -> list[str]:
    if not rows:
        return ["Package API: no matching rows"]

    by_path: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        by_path[row["arg0_string"]].append(row)

    lines = [f"Package API: {len(rows)} matching calls across {len(by_path)} path(s)"]
    for path_text in sorted(by_path):
        path_rows = by_path[path_text]
        api_counts = Counter(row["api"] for row in path_rows)
        result_samples = ", ".join(sorted({row["result"] for row in path_rows})[:4])
        lines.append(
            f"  {path_text}: calls={len(path_rows)} apis={dict(api_counts)} results={result_samples}"
        )
    return lines


def summarize_streams(rows: list[dict[str, str]]) -> list[str]:
    if not rows:
        return ["Streams: no matching rows"]

    by_path: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        by_path[row["data_patch_name"]].append(row)

    lines = [f"Streams: {len(rows)} matching reads across {len(by_path)} path(s)"]
    for path_text in sorted(by_path):
        path_rows = by_path[path_text]
        first = path_rows[0]
        ascii_sample = first.get("ascii_sample", "")
        kind = sniff_type(path_text, ascii_sample)
        sizes = sorted({row.get("stream_size", "") for row in path_rows if row.get("stream_size")})
        read_sizes = sorted({row.get("first_returned_size", row.get("returned_size", "")) for row in path_rows})
        indices = sorted({row.get("data_patch_index", "") for row in path_rows if row.get("data_patch_index")})
        lines.append(
            "  "
            f"{path_text}: type={kind} reads={len(path_rows)} "
            f"stream_size={sizes[:3]} first_read={read_sizes[:3]} "
            f"data_patch_index={indices[:3]} sample={ascii_sample[:32]!r}"
        )
    return lines


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Summarize Trials Fusion pak_runtime package API and stream logs."
    )
    parser.add_argument(
        "--pak-package-api",
        type=Path,
        default=Path(r"F:\Trials Fusion\datapack\pak_runtime\pak_package_api.csv"),
        help="path to pak_package_api.csv",
    )
    parser.add_argument(
        "--pak-streams",
        type=Path,
        default=Path(r"F:\Trials Fusion\datapack\pak_runtime\pak_streams.csv"),
        help="path to pak_streams.csv",
    )
    parser.add_argument(
        "needle",
        nargs="*",
        help="case-insensitive substring filter(s), for example codex objectcollection6 .gfx .mdl",
    )
    args = parser.parse_args()

    needles = [item.lower() for item in args.needle]
    package_rows = load_package_api(args.pak_package_api, needles)
    stream_rows = load_streams(args.pak_streams, needles)

    for line in summarize_package_api(package_rows):
        print(line)
    for line in summarize_streams(stream_rows):
        print(line)


if __name__ == "__main__":
    main()
