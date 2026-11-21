#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Unit tests for Engine/Tools/uve_asset_diff.py."""

import json
import struct
import tempfile
import unittest
from pathlib import Path

import uve_asset_diff


class UveAssetDiffTest(unittest.TestCase):
    def test_semantic_diff_and_clean_3way_merge(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            base = d / "base.uvmaterial"
            ours = d / "ours.uvmaterial"
            theirs = d / "theirs.uvmaterial"
            out = d / "merged.uvmaterial"

            base.write_text(json.dumps({"roughness": 0.5, "metallic": 0.0, "albedo": [1, 1, 1]}))
            ours.write_text(json.dumps({"roughness": 0.25, "metallic": 0.0, "albedo": [1, 1, 1]}))
            theirs.write_text(json.dumps({"roughness": 0.5, "metallic": 0.8, "albedo": [1, 1, 1]}))

            diff_lines = uve_asset_diff.semantic_diff(base, ours)
            self.assertEqual(diff_lines, ["~ roughness: 0.5 -> 0.25"])

            rc = uve_asset_diff.main(["merge", str(base), str(ours), str(theirs), str(out)])
            self.assertEqual(rc, 0)
            merged = json.loads(out.read_text())
            self.assertEqual(merged["roughness"], 0.25)
            self.assertEqual(merged["metallic"], 0.8)

    def test_conflict_detected_on_overlapping_edit(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            base = d / "b.json"
            ours = d / "o.json"
            theirs = d / "t.json"
            out = d / "m.json"
            base.write_text(json.dumps({"roughness": 0.5}))
            ours.write_text(json.dumps({"roughness": 0.2}))
            theirs.write_text(json.dumps({"roughness": 0.9}))
            rc = uve_asset_diff.main(["merge", str(base), str(ours), str(theirs), str(out)])
            self.assertEqual(rc, 1)

    def test_binary_envelope_textconv(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "mesh.uvmodel"
            p.write_bytes(b"UVEA" + struct.pack("<II", 2, 1) + b"\x00\x01\x02\x03")
            info = uve_asset_diff.inspect_asset_file(p)
            self.assertEqual(info["format"], "uve_binary_envelope")
            self.assertEqual(info["version"], 2)
            self.assertEqual(info["kind"], 1)


if __name__ == "__main__":
    unittest.main()
