// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/TextLayout.h"
#include <algorithm>
#include <map>
#if defined(PRISM_HAS_HARFBUZZ)
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>
#endif
#if defined(PRISM_HAS_ICU)
#include <unicode/brkiter.h>
#include <unicode/ubidi.h>
#include <unicode/unistr.h>
#include <unicode/uscript.h>
#endif

namespace prism {

// ------------------------------------------------------------------ utf8
void appendUtf8(std::string& o, char32_t cp) {
  if (cp < 0x80) o += char(cp);
  else if (cp < 0x800) { o += char(0xC0 | (cp >> 6)); o += char(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) { o += char(0xE0 | (cp >> 12)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
  else { o += char(0xF0 | (cp >> 18)); o += char(0x80 | ((cp >> 12) & 0x3F)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
}
std::u32string toU32(std::string_view s) {
  std::u32string o;
  for (size_t i = 0; i < s.size();) o += decodeUtf8(s, i);
  return o;
}
std::string toUtf8(const std::u32string& s) { std::string o; for (char32_t c : s) appendUtf8(o, c); return o; }

// ------------------------------------------------------------------ font stack
const Font* FontStack::fontFor(char32_t cp) const {
  for (auto* f : m_fonts) if (f && f->has(cp)) return f;
  return primary();
}
float FontStack::advance(char32_t cp) const { auto* f = fontFor(cp); return f ? f->advance(cp) : 0.f; }
float FontStack::width(std::string_view s) const {
  float w = 0;
  for (size_t i = 0; i < s.size();) w += advance(decodeUtf8(s, i));
  return w;
}
float FontStack::lineHeight() const {
  float h = 0;
  for (auto* f : m_fonts) if (f) h = std::max(h, f->lineHeight());
  return h;
}

// ------------------------------------------------------------------ bidi
bool isRtl(char32_t c) {
  return (c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF) || (c >= 0x10800 && c <= 0x10FFF) || (c >= 0x1E800 && c <= 0x1EFFF);
}
BidiClass bidiClassOf(char32_t c) {
  if (c == '\n' || c == '\r' || c == 0x2029) return BidiClass::B;
  if (c == ' ' || c == '\t' || c == 0x00A0 || (c >= 0x2000 && c <= 0x200A)) return BidiClass::WS;
  if (c >= '0' && c <= '9') return BidiClass::EN;
  if (c >= 0x0660 && c <= 0x0669) return BidiClass::AN;
  if (c == 0x200F) return BidiClass::R;
  if (c == 0x200E) return BidiClass::L;
  if (isRtl(c)) return (c >= 0x0600 && c <= 0x08FF) || (c >= 0xFB50 && c <= 0xFEFF) ? BidiClass::AL : BidiClass::R;
  if (c < 0x80) return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ? BidiClass::L : BidiClass::ON;
  if (c >= 0x2010 && c <= 0x2027) return BidiClass::ON;       // general punctuation
  if (c >= 0x00A1 && c <= 0x00BF) return BidiClass::ON;
  return BidiClass::L;                                         // other letters / ideographs
}
char32_t mirrorOf(char32_t c) {
  switch (c) {
    case '(': return ')'; case ')': return '('; case '[': return ']'; case ']': return '[';
    case '{': return '}'; case '}': return '{'; case '<': return '>'; case '>': return '<';
    case 0x00AB: return 0x00BB; case 0x00BB: return 0x00AB;
    default: return c;
  }
}

static BidiResult resolveBidiSimple(const std::u32string& t, int baseRtl) {
  BidiResult r;
  size_t n = t.size();
  std::vector<BidiClass> cls(n);
  for (size_t i = 0; i < n; i++) cls[i] = bidiClassOf(t[i]);
  // paragraph direction
  bool rtl = baseRtl == 1;
  if (baseRtl < 0) for (size_t i = 0; i < n; i++) {
    if (cls[i] == BidiClass::L) { rtl = false; break; }
    if (cls[i] == BidiClass::R || cls[i] == BidiClass::AL) { rtl = true; break; }
  }
  r.rtlParagraph = rtl;
  uint8_t base = rtl ? 1 : 0;
  // W-rules (subset): AL -> R; EN after AL becomes AN (kept as number); ES/CS between numbers ignored here
  std::vector<BidiClass> st(n);
  BidiClass lastStrong = rtl ? BidiClass::R : BidiClass::L;
  for (size_t i = 0; i < n; i++) {
    BidiClass c = cls[i];
    if (c == BidiClass::L || c == BidiClass::R || c == BidiClass::AL) lastStrong = c;
    if (c == BidiClass::EN && lastStrong == BidiClass::AL) c = BidiClass::AN;
    if (c == BidiClass::AL) c = BidiClass::R;
    st[i] = c;
  }
  // N-rules: neutrals/whitespace between two strong runs of the same direction take it; else the paragraph direction
  auto strongDir = [&](BidiClass c, bool& isR) {
    if (c == BidiClass::L) { isR = false; return true; }
    if (c == BidiClass::R || c == BidiClass::EN || c == BidiClass::AN) { isR = c == BidiClass::R || c == BidiClass::AN || (c == BidiClass::EN && false); return true; }
    return false;
  };
  std::vector<bool> dirR(n, rtl);
  for (size_t i = 0; i < n; i++) {
    bool isR;
    if (st[i] == BidiClass::EN || st[i] == BidiClass::L) { dirR[i] = false; }
    else if (st[i] == BidiClass::R || st[i] == BidiClass::AN) { dirR[i] = true; }
    else {
      size_t a = i, b = i;
      bool aR = rtl, bR = rtl, fa = false, fb = false;
      while (a > 0) { a--; if (strongDir(st[a], isR)) { fa = true; aR = (st[a] == BidiClass::EN) ? false : isR; if (st[a] == BidiClass::EN) aR = rtl ? true : false; break; } }
      while (b + 1 < n) { b++; if (strongDir(st[b], isR)) { fb = true; bR = (st[b] == BidiClass::EN) ? (rtl ? true : false) : isR; break; } }
      if (st[i] == BidiClass::B) dirR[i] = rtl;
      else dirR[i] = (fa && fb && aR == bR) ? aR : rtl;
    }
  }
  r.levels.assign(n, base);
  for (size_t i = 0; i < n; i++) {
    uint8_t lv = base;
    if (st[i] == BidiClass::EN || st[i] == BidiClass::AN) lv = rtl ? 2 : (st[i] == BidiClass::AN ? 2 : base);
    else if (dirR[i]) lv = rtl ? 1 : 1;
    else lv = rtl ? 2 : 0;
    r.levels[i] = lv;
  }
  // L2: reverse from highest level down to the lowest odd level
  r.visual.resize(n);
  for (size_t i = 0; i < n; i++) r.visual[i] = int(i);
  uint8_t hi = 0, lowOdd = 255;
  for (uint8_t lv : r.levels) { hi = std::max(hi, lv); if (lv & 1) lowOdd = std::min(lowOdd, lv); }
  for (int lv = hi; lv >= int(lowOdd) && lowOdd != 255; lv--) {
    for (size_t i = 0; i < n;) {
      if (r.levels[size_t(r.visual[i])] < lv) { i++; continue; }
      size_t j = i;
      while (j < n && r.levels[size_t(r.visual[j])] >= lv) j++;
      std::reverse(r.visual.begin() + long(i), r.visual.begin() + long(j));
      i = j;
    }
  }
  return r;
}

// ------------------------------------------------------------------ line breaking
static bool isCjk(char32_t c) {
  return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFFEF) || (c >= 0x3000 && c <= 0x303F) || (c >= 0x20000 && c <= 0x2FFFF);
}
// characters a line must not start with (closing punctuation, small kana, prolonged mark) / not end with (opening)
static bool noStart(char32_t c) {
  switch (c) {
    case 0x3001: case 0x3002: case 0xFF0C: case 0xFF0E: case 0xFF01: case 0xFF1F: case 0xFF09: case 0x300D: case 0x300F: case 0x3011:
    case 0x30FC: case 0x3041: case 0x3043: case 0x3045: case 0x3047: case 0x3049: case 0x30A1: case 0x30A3: case 0x30A5: case 0x30A7: case 0x30A9:
    case ')': case ']': case '}': case ',': case '.': case '!': case '?': case ':': case ';': case '%':
      return true;
    default: return false;
  }
}
static bool noEnd(char32_t c) { return c == 0x300C || c == 0x300E || c == 0x3010 || c == 0xFF08 || c == '(' || c == '[' || c == '{'; }

static std::vector<BreakOp> lineBreakOpsSimple(const std::u32string& t) {
  size_t n = t.size();
  std::vector<BreakOp> ops(n + 1, BreakOp::None);
  for (size_t i = 1; i <= n; i++) {
    if (i == n) { ops[i] = BreakOp::Mandatory; break; }
    char32_t prev = t[i - 1], cur = t[i];
    if (prev == '\n' || prev == 0x2029) { ops[i] = BreakOp::Mandatory; continue; }
    if (prev == '\r' && cur == '\n') continue;
    if (cur == '\n' || cur == '\r' || cur == ' ' || cur == '\t') continue;      // never break before spaces / newline
    bool afterSpace = prev == ' ' || prev == '\t';
    bool afterHyphen = (prev == '-' || prev == 0x2010 || prev == 0x2013) && i >= 2 && t[i - 2] != ' ' && cur != ' ';
    bool cjkPair = (isCjk(prev) || isCjk(cur)) && !noStart(cur) && !noEnd(prev) && !(prev == 0x00A0 || cur == 0x00A0);
    if (prev == 0x00A0 || cur == 0x00A0) continue;                               // no-break space
    if (afterSpace || afterHyphen || cjkPair) ops[i] = BreakOp::Allowed;
    if (afterSpace && noStart(cur) && cur != '(') ops[i] = BreakOp::Allowed;      // spaces always allow a break
  }
  return ops;
}

// ------------------------------------------------------------------ ICU-backed algorithms
#if defined(PRISM_HAS_ICU)
namespace {
// UTF-32 -> UTF-16 with a map from every UTF-16 unit back to its UTF-32 index
icu::UnicodeString toUtf16(const std::u32string& t, std::vector<size_t>& u16ToU32) {
  icu::UnicodeString u;
  u16ToU32.clear();
  for (size_t i = 0; i < t.size(); i++) {
    UChar32 c = UChar32(t[i]);
    if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) c = 0xFFFD;
    int before = u.length();
    u.append(c);
    for (int k = before; k < u.length(); k++) u16ToU32.push_back(i);
  }
  u16ToU32.push_back(t.size());
  return u;
}
} // namespace
#endif

BidiResult resolveBidi(const std::u32string& t, int baseRtl) {
#if defined(PRISM_HAS_ICU)
  if (t.empty()) return {};
  std::vector<size_t> map;
  icu::UnicodeString u = toUtf16(t, map);
  UErrorCode err = U_ZERO_ERROR;
  UBiDi* bidi = ubidi_openSized(u.length(), 0, &err);
  UBiDiLevel para = baseRtl < 0 ? UBIDI_DEFAULT_LTR : UBiDiLevel(baseRtl ? 1 : 0);
  ubidi_setPara(bidi, u.getBuffer(), u.length(), para, nullptr, &err);
  if (U_FAILURE(err)) { ubidi_close(bidi); return resolveBidiSimple(t, baseRtl); }
  BidiResult r;
  r.rtlParagraph = (ubidi_getParaLevel(bidi) & 1) != 0;
  const UBiDiLevel* lv = ubidi_getLevels(bidi, &err);
  r.levels.assign(t.size(), 0);
  for (int k = 0; k < u.length(); k++) r.levels[map[size_t(k)]] = lv[k];
  std::vector<int32_t> vis(size_t(u.length()));
  ubidi_getVisualMap(bidi, vis.data(), &err);
  for (int32_t k : vis) {
    size_t li = map[size_t(k)];
    if (r.visual.empty() || r.visual.back() != int(li)) r.visual.push_back(int(li));   // one entry per code point
  }
  ubidi_close(bidi);
  if (U_FAILURE(err) || r.visual.size() != t.size()) return resolveBidiSimple(t, baseRtl);
  return r;
#else
  return resolveBidiSimple(t, baseRtl);
#endif
}

std::vector<BreakOp> lineBreakOps(const std::u32string& t) {
#if defined(PRISM_HAS_ICU)
  std::vector<BreakOp> ops(t.size() + 1, BreakOp::None);
  if (t.empty()) { ops[0] = BreakOp::Mandatory; return ops; }
  std::vector<size_t> map;
  icu::UnicodeString u = toUtf16(t, map);
  UErrorCode err = U_ZERO_ERROR;
  std::unique_ptr<icu::BreakIterator> bi(icu::BreakIterator::createLineInstance(icu::Locale::getDefault(), err));
  if (U_FAILURE(err) || !bi) return lineBreakOpsSimple(t);
  bi->setText(u);
  for (int32_t p = bi->next(); p != icu::BreakIterator::DONE; p = bi->next()) {
    size_t i = map[size_t(p)];
    bool hard = bi->getRuleStatus() >= UBRK_LINE_HARD && bi->getRuleStatus() < UBRK_LINE_HARD_LIMIT;
    ops[i] = hard || i == t.size() ? BreakOp::Mandatory : BreakOp::Allowed;
  }
  ops[0] = BreakOp::None;
  ops[t.size()] = BreakOp::Mandatory;
  return ops;
#else
  return lineBreakOpsSimple(t);
#endif
}

std::vector<size_t> graphemeBounds(const std::u32string& t) {
  std::vector<size_t> b{0};
#if defined(PRISM_HAS_ICU)
  if (!t.empty()) {
    std::vector<size_t> map;
    icu::UnicodeString u = toUtf16(t, map);
    UErrorCode err = U_ZERO_ERROR;
    std::unique_ptr<icu::BreakIterator> bi(icu::BreakIterator::createCharacterInstance(icu::Locale::getDefault(), err));
    if (U_SUCCESS(err) && bi) {
      bi->setText(u);
      for (int32_t p = bi->next(); p != icu::BreakIterator::DONE; p = bi->next()) { size_t i = map[size_t(p)]; if (i != b.back()) b.push_back(i); }
      if (b.back() != t.size()) b.push_back(t.size());
      return b;
    }
  }
#endif
  // fallback: combining marks, variation selectors, ZWJ sequences and skin tones join the previous code point
  for (size_t i = 1; i < t.size(); i++) {
    char32_t c = t[i], p = t[i - 1];
    bool join = (c >= 0x300 && c <= 0x36F) || (c >= 0xFE00 && c <= 0xFE0F) || c == 0x200D || p == 0x200D ||
                (c >= 0x1F3FB && c <= 0x1F3FF) || (c >= 0xE0020 && c <= 0xE007F);
    if (!join) b.push_back(i);
  }
  if (b.back() != t.size()) b.push_back(t.size());
  return b;
}

bool textShapingAvailable() {
#if defined(PRISM_HAS_HARFBUZZ)
  return true;
#else
  return false;
#endif
}
bool unicodeAlgorithmsAvailable() {
#if defined(PRISM_HAS_ICU)
  return true;
#else
  return false;
#endif
}

// ------------------------------------------------------------------ shaping

ShapedLine shapeText(const FontStack& fonts, std::string_view utf8, int baseRtl) {
  ShapedLine out;
  std::u32string t = toU32(utf8);
  if (t.empty() || !fonts.primary()) return out;
  BidiResult bd = resolveBidi(t, baseRtl);
  out.rtl = bd.rtlParagraph;
  // font per grapheme cluster (a cluster stays in one font so emoji sequences and marks shape together)
  std::vector<size_t> gb = graphemeBounds(t);
  std::vector<const Font*> fontOf(t.size(), fonts.primary());
  for (size_t k = 0; k + 1 < gb.size(); k++) {
    size_t s = gb[k], e = gb[k + 1];
    const Font* f = nullptr;
    for (const Font* cand : {fonts.fontFor(t[s])}) {
      bool all = true;
      for (size_t i = s; i < e; i++) if (t[i] != 0x200D && !(t[i] >= 0xFE00 && t[i] <= 0xFE0F) && !cand->has(t[i])) { all = false; break; }
      if (all) f = cand;
    }
    if (!f) f = fonts.fontFor(t[s]);
    for (size_t i = s; i < e; i++) fontOf[i] = f;
  }
  // runs in visual order: maximal stretches with the same bidi level and font
  struct Run { size_t s, e; int level; const Font* f; };
  std::vector<Run> logical;
  for (size_t i = 0; i < t.size();) {
    size_t j = i + 1;
    while (j < t.size() && bd.levels[j] == bd.levels[i] && fontOf[j] == fontOf[i]) j++;
    logical.push_back({i, j, bd.levels[i], fontOf[i]});
    i = j;
  }
  // L2 on runs: reverse sequences at each level >= the lowest odd level
  std::vector<int> order(logical.size());
  for (size_t i = 0; i < order.size(); i++) order[i] = int(i);
  int hi = 0, lowOdd = 255;
  for (auto& r : logical) { hi = std::max(hi, r.level); if (r.level & 1) lowOdd = std::min(lowOdd, r.level); }
  for (int lv = hi; lv >= lowOdd && lowOdd != 255; lv--)
    for (size_t i = 0; i < order.size();) {
      if (logical[size_t(order[i])].level < lv) { i++; continue; }
      size_t j = i;
      while (j < order.size() && logical[size_t(order[j])].level >= lv) j++;
      std::reverse(order.begin() + long(i), order.begin() + long(j));
      i = j;
    }
  float pen = 0;
  for (int ri : order) {
    const Run& r = logical[size_t(ri)];
    bool rtl = r.level & 1;
#if defined(PRISM_HAS_HARFBUZZ)
    hb_font_t* hf = static_cast<hb_font_t*>(r.f->shaper());
    if (hf) {
      hb_buffer_t* buf = hb_buffer_create();
      hb_buffer_add_utf32(buf, reinterpret_cast<const uint32_t*>(t.data()), int(t.size()), unsigned(r.s), int(r.e - r.s));
      hb_buffer_set_direction(buf, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
      hb_buffer_guess_segment_properties(buf);
      hb_shape(hf, buf, nullptr, 0);
      unsigned n = 0;
      hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &n);
      hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &n);
      float k = r.f->bitmapScale();
      for (unsigned g = 0; g < n; g++) {
        ShapedGlyph sg;
        sg.font = r.f;
        sg.key = kGlyphIndexKey | char32_t(info[g].codepoint);
        sg.x = pen + float(pos[g].x_offset) / 64.f * k;
        sg.y = -float(pos[g].y_offset) / 64.f * k;
        sg.advance = float(pos[g].x_advance) / 64.f * k;
        sg.cluster = info[g].cluster;
        pen += sg.advance;
        out.glyphs.push_back(sg);
      }
      hb_buffer_destroy(buf);
      continue;
    }
#endif
    // no shaper: one glyph per code point, mirrored brackets in RTL runs
    for (size_t q = 0; q < r.e - r.s; q++) {
      size_t i = rtl ? r.e - 1 - q : r.s + q;
      char32_t c = t[i];
      if (c == '\n' || c == '\r') continue;
      if (rtl) c = mirrorOf(c);
      ShapedGlyph sg;
      sg.font = r.f; sg.key = c; sg.x = pen; sg.advance = r.f->advance(c); sg.cluster = i;
      pen += sg.advance;
      out.glyphs.push_back(sg);
    }
  }
  out.width = pen;
  return out;
}

std::vector<TextLine> wrapText(const FontStack& fonts, std::string_view utf8, float maxWidth) {
  std::vector<TextLine> lines;
  std::u32string t = toU32(utf8);
  auto ops = lineBreakOps(t);
  size_t start = 0, lastBreak = 0;
  float w = 0, wAtBreak = 0;
  auto emit = [&](size_t from, size_t to) {
    size_t e = to;
    while (e > from && (t[e - 1] == ' ' || t[e - 1] == '\n' || t[e - 1] == '\r' || t[e - 1] == '\t')) e--;   // trailing space is not painted
    std::u32string seg = t.substr(from, e - from);
    lines.push_back({toUtf8(seg), fonts.width(toUtf8(seg))});
  };
  for (size_t i = 0; i < t.size(); i++) {
    if (i > start && ops[i] == BreakOp::Mandatory) { emit(start, i); start = lastBreak = i; w = wAtBreak = 0; }
    else if (i > start && ops[i] == BreakOp::Allowed) { lastBreak = i; wAtBreak = w; }
    float adv = (t[i] == '\n' || t[i] == '\r') ? 0.f : fonts.advance(t[i]);
    bool space = t[i] == ' ' || t[i] == '\t';
    if (!space && w + adv > maxWidth && i > start) {
      if (lastBreak > start) { emit(start, lastBreak); w = w - wAtBreak; start = lastBreak; }
      else { emit(start, i); start = lastBreak = i; w = 0; }                    // single word wider than the line: hard split
      wAtBreak = 0;
    }
    w += adv;
  }
  if (start < t.size() || lines.empty()) emit(start, t.size());
  return lines;
}

float drawTextLine(Canvas& c, const FontStack& fonts, Vec2 pos, std::string_view utf8, Color color, int baseRtl) {
  ShapedLine line = shapeText(fonts, utf8, baseRtl);
  // one glyphRun per font (glyph keys are per-face), all sharing the line's top-left and the primary ascent
  const Font* primary = fonts.primary();
  if (!primary) return 0;
  std::vector<Canvas::GlyphPos> run;
  const Font* cur = nullptr;
  auto flush = [&] {
    if (cur && !run.empty()) {
      float dy = primary->ascent() - cur->ascent();       // align every font on the primary baseline
      c.glyphRun(*cur, {pos.x, pos.y + dy}, run, color);
    }
    run.clear();
  };
  for (auto& g : line.glyphs) {
    if (g.font != cur) { flush(); cur = g.font; }
    run.push_back({g.key, g.x, g.y});
  }
  flush();
  return line.width;
}

} // namespace prism
