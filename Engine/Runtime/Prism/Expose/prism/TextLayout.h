// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - text layout subset (no HarfBuzz / ICU dependency):
//  * FontStack      : ordered fallback chain; each code point is drawn with the first font that has it.
//  * Bidi           : simplified UAX#9 (strong Hebrew/Arabic -> RTL runs, numbers stay LTR, neutrals take context,
//                     bracket mirroring) returning visual order. No explicit embeddings/isolates.
//  * Line breaking  : simplified UAX#14 (spaces, hyphens, CJK ideographs, mandatory newline) + greedy wrap.
// With HarfBuzz + ICU (CMake auto-detects them; PRISM_HAS_HARFBUZZ / PRISM_HAS_ICU):
//  * shapeText      : full OpenType shaping per font run (Arabic joining, Indic reordering, ligatures, kerning, marks)
//  * resolveBidi    : complete UAX#9 via ubidi (embeddings, isolates, mirroring rules)
//  * lineBreakOps   : UAX#14 via BreakIterator (incl. dictionary breaking for Thai / Lao / Khmer / CJK)
//  * graphemeBounds : UAX#29 clusters (emoji ZWJ sequences, flags, combining marks stay together)
// Colour emoji render from colour fonts (CBDT / sbix / COLR via FreeType) on the CPU canvas and GPU backends.
#pragma once
#include "Canvas.h"
#include <string>
#include <vector>

namespace prism {

class FontStack {
public:
  FontStack() = default;
  explicit FontStack(std::vector<const Font*> fonts) : m_fonts(std::move(fonts)) {}
  FontStack& add(const Font& f) { m_fonts.push_back(&f); return *this; }
  size_t size() const { return m_fonts.size(); }
  const Font* primary() const { return m_fonts.empty() ? nullptr : m_fonts.front(); }
  const Font* fontFor(char32_t cp) const;            // first font with the glyph; primary if none has it
  float advance(char32_t cp) const;
  float width(std::string_view utf8) const;
  float lineHeight() const;
private:
  std::vector<const Font*> m_fonts;
};

enum class BidiClass { L, R, AL, EN, AN, WS, ON, B };
BidiClass bidiClassOf(char32_t cp);
bool isRtl(char32_t cp);
char32_t mirrorOf(char32_t cp);                      // paired brackets; cp itself otherwise

struct BidiResult {
  bool rtlParagraph = false;
  std::vector<uint8_t> levels;                       // per logical code point
  std::vector<int> visual;                           // visual position -> logical index
};
// `baseRtl`: -1 = auto (first strong), 0 = LTR, 1 = RTL
BidiResult resolveBidi(const std::u32string& text, int baseRtl = -1);

std::u32string toU32(std::string_view utf8);
std::string toUtf8(const std::u32string& s);
void appendUtf8(std::string& out, char32_t cp);

enum class BreakOp : uint8_t { None, Allowed, Mandatory };
// ops[i] describes the break between text[i-1] and text[i] (ops[0] is always None; ops.size() == text.size()+1,
// the final entry is Mandatory = end of text).
std::vector<BreakOp> lineBreakOps(const std::u32string& text);

// Grapheme cluster boundaries (indices into the u32 string, first 0, last text.size()).
std::vector<size_t> graphemeBounds(const std::u32string& text);

// Shaped, visually ordered glyphs of one line. `x` is the pen offset from the line start; `cluster` is the index of the
// first code point (u32) the glyph came from.
struct ShapedGlyph { const Font* font = nullptr; char32_t key = 0; float x = 0, y = 0, advance = 0; size_t cluster = 0; };
struct ShapedLine { std::vector<ShapedGlyph> glyphs; float width = 0; bool rtl = false; };
ShapedLine shapeText(const FontStack& fonts, std::string_view utf8, int baseRtl = -1);
bool textShapingAvailable();          // true when built with HarfBuzz
bool unicodeAlgorithmsAvailable();    // true when built with ICU

struct TextLine { std::string text; float width = 0; };
std::vector<TextLine> wrapText(const FontStack& fonts, std::string_view utf8, float maxWidth);

// Draws one line (bidi-reordered, per-glyph fallback). Returns the advance width.
float drawTextLine(Canvas& c, const FontStack& fonts, Vec2 pos, std::string_view utf8, Color color, int baseRtl = -1);

} // namespace prism
