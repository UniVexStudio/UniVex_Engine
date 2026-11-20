#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Unit tests for Engine/Tools/compile_shaders.py."""

import tempfile
import unittest
from pathlib import Path

import compile_shaders


class CompileShadersTest(unittest.TestCase):
    def test_detect_combined_graphics_and_compute_stages(self) -> None:
        gfx = "#version 430 core\n#ifdef VERTEX_SHADER\nvoid main(){}\n#endif\n#ifdef FRAGMENT_SHADER\nvoid main(){}\n#endif\n"
        self.assertEqual(compile_shaders.detect_stages(gfx), ["vert", "frag"])

        comp = "#version 430 core\nlayout(local_size_x = 8, local_size_y = 8) in;\nvoid main(){}\n"
        self.assertEqual(compile_shaders.detect_stages(comp), ["comp"])

    def test_rewrite_for_dialects(self) -> None:
        src = "#version 330 core\nvoid main() {}\n"
        vk = compile_shaders.rewrite_for_dialect(src, "frag", "spirv")
        self.assertTrue(vk.startswith("#version 450\n#define UVE_VULKAN 1\n#define FRAGMENT_SHADER 1\n"))
        es = compile_shaders.rewrite_for_dialect(src, "vert", "gles3")
        self.assertIn("#version 300 es", es)
        self.assertIn("precision highp float;", es)
        self.assertIn("#define VERTEX_SHADER 1", es)

    def test_validate_all_built_in_shaders_pass(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            manifest = Path(td) / "manifest.json"
            rc = compile_shaders.main(["--validate-sources", "--manifest", str(manifest)])
            self.assertEqual(rc, 0)
            self.assertTrue(manifest.exists())

    def test_emit_cpp_header(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            hdr = Path(td) / "gen_spirv.h"
            compile_shaders.emit_cpp_header({"DemoVert": b"\x03\x02\x23\x07"}, hdr)
            text = hdr.read_text(encoding="utf-8")
            self.assertIn("kDemoVertSpirvBytesUVE", text)
            self.assertIn("kDemoVertSpirvSizeUVE = 4U", text)


if __name__ == "__main__":
    unittest.main()
