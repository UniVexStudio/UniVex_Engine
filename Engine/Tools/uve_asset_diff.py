#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Version-control-friendly UniVex asset diff, textconv, and 3-way merge driver.

Supports JSON-backed UniVex assets (`.uvscene`, `.uvmaterial`, `.uvprefab`, `.uvmatgraph`, etc.)
and binary `.uve*` envelope headers (`UVEA` magic, version, asset kind, payload SHA-256).

Git setup example:
  git config diff.uve.textconv "python3 Engine/Tools/uve_asset_diff.py textconv"
  git config merge.uve.driver "python3 Engine/Tools/uve_asset_diff.py merge %O %A %B %A"
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path
from typing import Any, Dict, List, Tuple


def inspect_asset_file(path: Path) -> Dict[str, Any]:
    raw = path.read_bytes()
    # Try UTF-8 JSON first.
    try:
        parsed = json.loads(raw.decode("utf-8"))
        return {"format": "json", "data": parsed}
    except Exception:
        pass

    # Check binary UniVex envelope header (`UVE` magic).
    if len(raw) >= 12 and raw[:3] == b"UVE":
        magic = raw[:4].decode("ascii", errors="replace")
        version, kind = struct.unpack_from("<II", raw, 4)
        return {
            "format": "uve_binary_envelope",
            "magic": magic,
            "version": version,
            "kind": kind,
            "size_bytes": len(raw),
            "sha256": hashlib.sha256(raw).hexdigest(),
        }

    # Fallback: UTF-8 line-oriented text asset (e.g. `.uvmatgraph`).
    try:
        text = raw.decode("utf-8")
        return {"format": "text", "lines": text.splitlines()}
    except UnicodeDecodeError:
        return {
            "format": "binary",
            "size_bytes": len(raw),
            "sha256": hashlib.sha256(raw).hexdigest(),
        }


def flatten_dict(obj: Any, prefix: str = "") -> Dict[str, Any]:
    out: Dict[str, Any] = {}
    if isinstance(obj, dict):
        for k in sorted(obj.keys()):
            sub = f"{prefix}.{k}" if prefix else str(k)
            out.update(flatten_dict(obj[k], sub))
    elif isinstance(obj, list):
        for idx, item in enumerate(obj):
            sub = f"{prefix}[{idx}]"
            out.update(flatten_dict(item, sub))
    else:
        out[prefix or "$"] = obj
    return out


def semantic_diff(path_a: Path, path_b: Path) -> List[str]:
    a = inspect_asset_file(path_a)
    b = inspect_asset_file(path_b)
    if a["format"] == "json" and b["format"] == "json":
        fa = flatten_dict(a["data"])
        fb = flatten_dict(b["data"])
        changes: List[str] = []
        for k in sorted(set(fa.keys()) | set(fb.keys())):
            if k not in fa:
                changes.append(f"+ {k} = {json.dumps(fb[k], sort_keys=True)}")
            elif k not in fb:
                changes.append(f"- {k} = {json.dumps(fa[k], sort_keys=True)}")
            elif fa[k] != fb[k]:
                changes.append(f"~ {k}: {json.dumps(fa[k], sort_keys=True)} -> {json.dumps(fb[k], sort_keys=True)}")
        return changes

    if a == b:
        return []
    return [f"- {json.dumps(a, sort_keys=True)}", f"+ {json.dumps(b, sort_keys=True)}"]


def merge_json_objects(base: Any, ours: Any, theirs: Any) -> Tuple[Any, List[str]]:
    if ours == theirs:
        return (ours, [])
    if ours == base:
        return (theirs, [])
    if theirs == base:
        return (ours, [])
    if isinstance(base, dict) and isinstance(ours, dict) and isinstance(theirs, dict):
        merged: Dict[str, Any] = {}
        conflicts: List[str] = []
        all_keys = sorted(set(base.keys()) | set(ours.keys()) | set(theirs.keys()))
        for k in all_keys:
            in_b, in_o, in_t = k in base, k in ours, k in theirs
            if in_o and in_t:
                val, sub_c = merge_json_objects(base.get(k), ours[k], theirs[k])
                merged[k] = val
                conflicts.extend([f"{k}.{c}" if c else k for c in sub_c])
            elif in_o and not in_b:
                merged[k] = ours[k]
            elif in_t and not in_b:
                merged[k] = theirs[k]
            elif in_o and in_b and not in_t:
                if ours[k] != base[k]:
                    conflicts.append(k)
                    merged[k] = ours[k]
            elif in_t and in_b and not in_o:
                if theirs[k] != base[k]:
                    conflicts.append(k)
                    merged[k] = theirs[k]
        return (merged, conflicts)
    return ({"__conflict_ours__": ours, "__conflict_theirs__": theirs}, [""])


def main(argv: List[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="UniVex VCS asset diff & 3-way merge tool")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_tc = sub.add_parser("textconv", help="Emit canonical VCS-diffable text for an asset file")
    p_tc.add_argument("file", type=Path)

    p_diff = sub.add_parser("diff", help="Show semantic property diff between two asset files")
    p_diff.add_argument("file_a", type=Path)
    p_diff.add_argument("file_b", type=Path)

    p_merge = sub.add_parser("merge", help="3-way property merge for JSON UniVex assets")
    p_merge.add_argument("base", type=Path)
    p_merge.add_argument("ours", type=Path)
    p_merge.add_argument("theirs", type=Path)
    p_merge.add_argument("output", type=Path)

    args = parser.parse_args(argv)
    if args.cmd == "textconv":
        info = inspect_asset_file(args.file)
        print(json.dumps(info, indent=2, sort_keys=True))
        return 0
    if args.cmd == "diff":
        changes = semantic_diff(args.file_a, args.file_b)
        for line in changes:
            print(line)
        return 0
    if args.cmd == "merge":
        b = inspect_asset_file(args.base)
        o = inspect_asset_file(args.ours)
        t = inspect_asset_file(args.theirs)
        if b["format"] != "json" or o["format"] != "json" or t["format"] != "json":
            print("3-way property merge requires JSON assets", file=sys.stderr)
            return 1
        merged, conflicts = merge_json_objects(b["data"], o["data"], t["data"])
        args.output.write_text(json.dumps(merged, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        if conflicts:
            print(f"Merge conflicts on: {', '.join(conflicts)}", file=sys.stderr)
            return 1
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
