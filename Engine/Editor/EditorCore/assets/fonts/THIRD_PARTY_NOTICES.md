# Third-Party Notices — `engine/editor/assets/fonts`

## Liberation Sans (subsetted)

`liberation-sans-subset.ttf` is a subset of Liberation Sans Regular
(`fonts-liberation` Debian package, version `1:2.1.5-3`), used as the
editor's main UI text font in place of ImGui's own built-in low-resolution
bitmap font (`ImGui::AddFontDefault()`). Upstream project:
<https://github.com/liberationfonts>.

Only Basic Latin + Latin-1 Supplement glyphs (`U+0020-007E`, `U+00A0-00FF`)
are kept - everything the editor's own UI text actually renders - to avoid
vendoring the full ~400 KB font for a handful of scripts this editor never
displays. The subset was produced with `fonttools`'s `pyftsubset`:

```
pyftsubset LiberationSans-Regular.ttf \
    --output-file=liberation-sans-subset.ttf \
    --unicodes="U+0020-007E,U+00A0-00FF" \
    --layout-features='*' --glyph-names --symbol-cmap --legacy-cmap \
    --notdef-glyph --notdef-outline --recommended-glyphs
```

This was then compiled into `engine/editor/src/uve_ui_font_bytes.inc`
(a plain `std::uint8_t` byte array, matching the icon font's own
`uve_icon_font_bytes.inc` convention below), which is what the editor
actually links against; the `.ttf` here is the checked-in source asset.

Chosen over other locally-available fonts because it is a clean,
metric-compatible sans-serif (visually similar to Arial/Helvetica) suitable
for editor UI text, and is freely embeddable under the SIL Open Font
License - not because it is a verified match for any specific commercial
game engine's own UI font, a claim this notice makes no attempt to assert.

### License

Liberation Fonts are SIL Open Font License 1.1 licensed:

```
Digitized data copyright (c) 2010 Google Corporation with Reserved Font
Arimo, Tinos and Cousine.
Copyright (c) 2012 Red Hat, Inc. with Reserved Font Name Liberation.

SIL OPEN FONT LICENSE Version 1.1 - 26 February 2007

PREAMBLE
The goals of the Open Font License (OFL) are to stimulate worldwide
development of collaborative font projects, to support the font creation
efforts of academic and linguistic communities, and to provide a free and
open framework in which fonts may be shared and improved in partnership
with others.

The OFL allows the licensed fonts to be used, studied, modified and
redistributed freely as long as they are not sold by themselves. The
fonts, including any derivative works, can be bundled, embedded,
redistributed and/or sold with any software provided that any reserved
names are not used by derivative works. The fonts and derivatives,
however, cannot be released under any other type of license. The
requirement for fonts to remain under this license does not apply to any
document created using the fonts or their derivatives.

DEFINITIONS
"Font Software" refers to the set of files released by the Copyright
Holder(s) under this license and clearly marked as such. This may
include source files, build scripts and documentation.

"Reserved Font Name" refers to any names specified as such after the
copyright statement(s).

"Original Version" refers to the collection of Font Software components
as distributed by the Copyright Holder(s).

"Modified Version" refers to any derivative made by adding to, deleting,
or substituting - in part or in whole - any of the components of the
Original Version, by changing formats or by porting the Font Software to
a new environment.

"Author" refers to any designer, engineer, programmer, technical writer
or other person who contributed to the Font Software.

PERMISSION & CONDITIONS
Permission is hereby granted, free of charge, to any person obtaining a
copy of the Font Software, to use, study, copy, merge, embed, modify,
redistribute, and sell modified and unmodified copies of the Font
Software, subject to the following conditions:

1) Neither the Font Software nor any of its individual components, in
   Original or Modified Versions, may be sold by itself.

2) Original or Modified Versions of the Font Software may be bundled,
   redistributed and/or sold with any software, provided that each copy
   contains the above copyright notice and this license. These can be
   included either as stand-alone text files, human-readable headers or
   in the appropriate machine-readable metadata fields within text or
   binary files as long as those fields can be easily viewed by the user.

3) No Modified Version of the Font Software may use the Reserved Font
   Name(s) unless explicit written permission is granted by the
   corresponding Copyright Holder. This restriction only applies to the
   primary font name as presented to the users.

4) The name(s) of the Copyright Holder(s) or the Author(s) of the Font
   Software shall not be used to promote, endorse or advertise any
   Modified Version, except to acknowledge the contribution(s) of the
   Copyright Holder(s) and the Author(s) or with their explicit written
   permission.

5) The Font Software, modified or unmodified, in part or in whole, must
   be distributed entirely under this license, and must not be
   distributed under any other license. The requirement for fonts to
   remain under this license does not apply to any document created
   using the Font Software.

TERMINATION
This license becomes null and void if any of the above conditions are
not met.

DISCLAIMER
THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
COPYRIGHT, PATENT, TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL THE
COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
INCLUDING ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL
DAMAGES, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF THE USE OR INABILITY TO USE THE FONT SOFTWARE OR FROM OTHER
DEALINGS IN THE FONT SOFTWARE.
```

## Tabler Icons (subsetted)

`tabler-icons-subset.ttf` is a subset of the Tabler Icons webfont, built from
the `@tabler/icons-webfont` npm package (version `3.46.0`,
`https://registry.npmjs.org/@tabler/icons-webfont/-/icons-webfont-3.46.0.tgz`).
Upstream project: <https://github.com/tabler/tabler-icons>.

Only the 13 glyphs the editor chrome actually uses are kept, to avoid
vendoring a multi-megabyte font for a dozen-odd icons. The subset was produced
with `fonttools`'s `pyftsubset`:

```
pyftsubset tabler-icons.ttf \
    --output-file=tabler-icons-subset.ttf \
    --unicodes=EA03,EA45,EA54,EA98,EAA4,EAAD,EB2E,EBD9,EDBA,F91D,FA97,FAF7,FAFA \
    --glyph-names --layout-features='*' --no-hinting --desubroutinize
```

`tools/generate_icon_font_bytes.py` compiles this file into
`engine/editor/src/uve_icon_font_bytes.inc`, which is what the editor
actually links against; the `.ttf` here is the checked-in source asset the
generator reads, following the same source-asset-plus-generator convention
already used for `engine/editor/assets/gizmos` and `engine/editor/assets/icons`.

### License

Tabler Icons is MIT licensed:

```
MIT License

Copyright (c) 2020-2026 Paweł Kuna

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```
