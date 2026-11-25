// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "svg_importer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Asset {
namespace {

struct SvgColor final {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;
    bool none = false;
};

struct SvgPoint final {
    float x = 0.0F;
    float y = 0.0F;
};

[[nodiscard]] std::string TrimLower(const std::string_view s) {
    std::size_t a = 0U;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    std::size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1U]))) --b;
    std::string out;
    out.reserve(b - a);
    for (std::size_t i = a; i < b; ++i) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(s[i]))));
    }
    return out;
}

[[nodiscard]] std::string ExtractAttr(const std::string_view tag, const std::string_view name) {
    std::size_t pos = 0U;
    while (pos < tag.size()) {
        const std::size_t found = tag.find(name, pos);
        if (found == std::string_view::npos) {
            return {};
        }
        const bool boundaryBefore =
            (found == 0U) || std::isspace(static_cast<unsigned char>(tag[found - 1U])) || tag[found - 1U] == ';';
        std::size_t eq = found + name.size();
        while (eq < tag.size() && std::isspace(static_cast<unsigned char>(tag[eq]))) ++eq;
        if (boundaryBefore && eq < tag.size() && tag[eq] == '=') {
            ++eq;
            while (eq < tag.size() && std::isspace(static_cast<unsigned char>(tag[eq]))) ++eq;
            if (eq < tag.size() && (tag[eq] == '"' || tag[eq] == '\'')) {
                const char quote = tag[eq++];
                const std::size_t end = tag.find(quote, eq);
                if (end != std::string_view::npos) {
                    return std::string(tag.substr(eq, end - eq));
                }
            }
        }
        pos = found + name.size();
    }
    return {};
}

[[nodiscard]] float ParseFloat(const std::string& s, const float fallback) noexcept {
    if (s.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    return (end != s.c_str() && std::isfinite(v)) ? v : fallback;
}

[[nodiscard]] int HexDigit(const char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return 0;
}

[[nodiscard]] SvgColor ParseColor(const std::string_view raw, const SvgColor& fallback) {
    const std::string s = TrimLower(raw);
    if (s.empty()) {
        return fallback;
    }
    if (s == "none" || s == "transparent") {
        return SvgColor{0.0F, 0.0F, 0.0F, 0.0F, true};
    }
    if (s[0] == '#') {
        if (s.size() == 4U) {
            return SvgColor{
                static_cast<float>(HexDigit(s[1]) * 17) / 255.0F,
                static_cast<float>(HexDigit(s[2]) * 17) / 255.0F,
                static_cast<float>(HexDigit(s[3]) * 17) / 255.0F,
                1.0F,
                false,
            };
        }
        if (s.size() >= 7U) {
            const float r = static_cast<float>((HexDigit(s[1]) << 4) | HexDigit(s[2])) / 255.0F;
            const float g = static_cast<float>((HexDigit(s[3]) << 4) | HexDigit(s[4])) / 255.0F;
            const float b = static_cast<float>((HexDigit(s[5]) << 4) | HexDigit(s[6])) / 255.0F;
            float a = 1.0F;
            if (s.size() >= 9U) {
                a = static_cast<float>((HexDigit(s[7]) << 4) | HexDigit(s[8])) / 255.0F;
            }
            return SvgColor{r, g, b, a, false};
        }
    }
    if (s == "white") return {1.0F, 1.0F, 1.0F, 1.0F, false};
    if (s == "black") return {0.0F, 0.0F, 0.0F, 1.0F, false};
    if (s == "red") return {1.0F, 0.0F, 0.0F, 1.0F, false};
    if (s == "green") return {0.0F, 0.502F, 0.0F, 1.0F, false};
    if (s == "lime") return {0.0F, 1.0F, 0.0F, 1.0F, false};
    if (s == "blue") return {0.0F, 0.0F, 1.0F, 1.0F, false};
    if (s == "yellow") return {1.0F, 1.0F, 0.0F, 1.0F, false};
    if (s == "cyan") return {0.0F, 1.0F, 1.0F, 1.0F, false};
    if (s == "magenta") return {1.0F, 0.0F, 1.0F, 1.0F, false};
    if (s == "gray" || s == "grey") return {0.5F, 0.5F, 0.5F, 1.0F, false};
    return fallback;
}

[[nodiscard]] std::vector<float> ParseNumberList(const std::string_view s) {
    std::vector<float> out;
    const char* p = s.data();
    const char* const end = s.data() + s.size();
    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')) ++p;
        if (p >= end) break;
        char* next = nullptr;
        const float v = std::strtof(p, &next);
        if (next == p) {
            ++p;
            continue;
        }
        out.push_back(v);
        p = next;
    }
    return out;
}

[[nodiscard]] std::vector<std::vector<SvgPoint>> ParsePathPolygons(const std::string_view d) {
    std::vector<std::vector<SvgPoint>> polys;
    std::vector<SvgPoint> cur;
    SvgPoint pen{0.0F, 0.0F};
    SvgPoint subStart{0.0F, 0.0F};
    char cmd = 0;

    const char* p = d.data();
    const char* const end = d.data() + d.size();
    const auto skipSep = [&]() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')) ++p;
    };
    const auto readNum = [&](float& outVal) -> bool {
        skipSep();
        if (p >= end) return false;
        char* next = nullptr;
        const float v = std::strtof(p, &next);
        if (next == p) return false;
        outVal = v;
        p = next;
        return true;
    };

    while (p < end) {
        skipSep();
        if (p >= end) break;
        if (std::isalpha(static_cast<unsigned char>(*p))) {
            cmd = *p++;
            if (cmd == 'Z' || cmd == 'z') {
                if (!cur.empty()) {
                    cur.push_back(subStart);
                    polys.push_back(cur);
                    cur.clear();
                }
                pen = subStart;
                continue;
            }
        }
        if (cmd == 'M' || cmd == 'm') {
            float x = 0.0F, y = 0.0F;
            if (!readNum(x) || !readNum(y)) break;
            if (!cur.empty()) {
                polys.push_back(cur);
                cur.clear();
            }
            pen = (cmd == 'm') ? SvgPoint{pen.x + x, pen.y + y} : SvgPoint{x, y};
            subStart = pen;
            cur.push_back(pen);
            cmd = (cmd == 'm') ? 'l' : 'L';
        } else if (cmd == 'L' || cmd == 'l') {
            float x = 0.0F, y = 0.0F;
            if (!readNum(x) || !readNum(y)) break;
            pen = (cmd == 'l') ? SvgPoint{pen.x + x, pen.y + y} : SvgPoint{x, y};
            cur.push_back(pen);
        } else if (cmd == 'H' || cmd == 'h') {
            float x = 0.0F;
            if (!readNum(x)) break;
            pen.x = (cmd == 'h') ? (pen.x + x) : x;
            cur.push_back(pen);
        } else if (cmd == 'V' || cmd == 'v') {
            float y = 0.0F;
            if (!readNum(y)) break;
            pen.y = (cmd == 'v') ? (pen.y + y) : y;
            cur.push_back(pen);
        } else if (cmd == 'Q' || cmd == 'q') {
            float cx = 0.0F, cy = 0.0F, x = 0.0F, y = 0.0F;
            if (!readNum(cx) || !readNum(cy) || !readNum(x) || !readNum(y)) break;
            const SvgPoint p0 = pen;
            const SvgPoint p1 = (cmd == 'q') ? SvgPoint{pen.x + cx, pen.y + cy} : SvgPoint{cx, cy};
            const SvgPoint p2 = (cmd == 'q') ? SvgPoint{pen.x + x, pen.y + y} : SvgPoint{x, y};
            for (int s = 1; s <= 12; ++s) {
                const float t = static_cast<float>(s) / 12.0F;
                const float u = 1.0F - t;
                cur.push_back(SvgPoint{
                    u * u * p0.x + 2.0F * u * t * p1.x + t * t * p2.x,
                    u * u * p0.y + 2.0F * u * t * p1.y + t * t * p2.y,
                });
            }
            pen = p2;
        } else if (cmd == 'C' || cmd == 'c') {
            float c1x = 0.0F, c1y = 0.0F, c2x = 0.0F, c2y = 0.0F, x = 0.0F, y = 0.0F;
            if (!readNum(c1x) || !readNum(c1y) || !readNum(c2x) || !readNum(c2y) || !readNum(x) || !readNum(y)) {
                break;
            }
            const SvgPoint p0 = pen;
            const SvgPoint p1 = (cmd == 'c') ? SvgPoint{pen.x + c1x, pen.y + c1y} : SvgPoint{c1x, c1y};
            const SvgPoint p2 = (cmd == 'c') ? SvgPoint{pen.x + c2x, pen.y + c2y} : SvgPoint{c2x, c2y};
            const SvgPoint p3 = (cmd == 'c') ? SvgPoint{pen.x + x, pen.y + y} : SvgPoint{x, y};
            for (int s = 1; s <= 16; ++s) {
                const float t = static_cast<float>(s) / 16.0F;
                const float u = 1.0F - t;
                cur.push_back(SvgPoint{
                    u * u * u * p0.x + 3.0F * u * u * t * p1.x + 3.0F * u * t * t * p2.x + t * t * t * p3.x,
                    u * u * u * p0.y + 3.0F * u * u * t * p1.y + 3.0F * u * t * t * p2.y + t * t * t * p3.y,
                });
            }
            pen = p3;
        } else {
            ++p;
        }
    }
    if (!cur.empty()) {
        polys.push_back(cur);
    }
    return polys;
}

[[nodiscard]] bool PointInPolygon(const float x, const float y,
                                     const std::vector<SvgPoint>& poly) noexcept {
    if (poly.size() < 3U) {
        return false;
    }
    bool inside = false;
    const std::size_t n = poly.size();
    for (std::size_t i = 0U, j = n - 1U; i < n; j = i++) {
        const auto& pi = poly[i];
        const auto& pj = poly[j];
        if (((pi.y > y) != (pj.y > y)) &&
            (x < (pj.x - pi.x) * (y - pi.y) / (pj.y - pi.y + 1.0e-12F) + pi.x)) {
            inside = !inside;
        }
    }
    return inside;
}

[[nodiscard]] float DistSqPointSegment(const float px, const float py, const SvgPoint a,
                                          const SvgPoint b) noexcept {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float lenSq = dx * dx + dy * dy;
    const float t = lenSq > 1.0e-8F ? std::clamp(((px - a.x) * dx + (py - a.y) * dy) / lenSq, 0.0F, 1.0F) : 0.0F;
    const float cx = a.x + t * dx;
    const float cy = a.y + t * dy;
    return (px - cx) * (px - cx) + (py - cy) * (py - cy);
}

void BlendPixel(std::vector<float>& canvas, const std::uint32_t width, const std::uint32_t x,
                   const std::uint32_t y, const SvgColor& color, const float coverage) noexcept {
    const float srcA = std::clamp(color.a * coverage, 0.0F, 1.0F);
    if (srcA <= 1.0e-5F) {
        return;
    }
    const std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 4U;
    const float dstR = canvas[idx + 0U];
    const float dstG = canvas[idx + 1U];
    const float dstB = canvas[idx + 2U];
    const float dstA = canvas[idx + 3U];
    const float outA = srcA + dstA * (1.0F - srcA);
    if (outA > 1.0e-6F) {
        canvas[idx + 0U] = (color.r * srcA + dstR * dstA * (1.0F - srcA)) / outA;
        canvas[idx + 1U] = (color.g * srcA + dstG * dstA * (1.0F - srcA)) / outA;
        canvas[idx + 2U] = (color.b * srcA + dstB * dstA * (1.0F - srcA)) / outA;
        canvas[idx + 3U] = outA;
    }
}

struct SvgShape final {
    std::vector<std::vector<SvgPoint>> polygons;
    SvgColor fill{0.0F, 0.0F, 0.0F, 1.0F, false};
    SvgColor stroke{0.0F, 0.0F, 0.0F, 0.0F, true};
    float strokeWidth = 1.0F;
    bool closed = true;
};

} // namespace

bool RasterizeSvgToTextureAsset(const std::string_view svgSource,
                                   const SvgRasterOptions& options,
                                   TextureAsset& outTexture) {
    if (svgSource.find("<svg") == std::string_view::npos) {
        return false;
    }

    float vbX = 0.0F, vbY = 0.0F;
    float vbW = static_cast<float>(options.fallbackWidth);
    float vbH = static_cast<float>(options.fallbackHeight);
    float docW = 0.0F, docH = 0.0F;
    std::vector<SvgShape> shapes;

    std::size_t cursor = 0U;
    while (cursor < svgSource.size()) {
        const std::size_t open = svgSource.find('<', cursor);
        if (open == std::string_view::npos) break;
        const std::size_t close = svgSource.find('>', open + 1U);
        if (close == std::string_view::npos) break;
        const std::string_view tag = svgSource.substr(open + 1U, close - open - 1U);
        cursor = close + 1U;
        if (tag.empty() || tag[0] == '/' || tag[0] == '?' || tag[0] == '!') {
            continue;
        }
        std::size_t nameEnd = 0U;
        while (nameEnd < tag.size() && !std::isspace(static_cast<unsigned char>(tag[nameEnd])) &&
               tag[nameEnd] != '/') {
            ++nameEnd;
        }
        const std::string tagName = TrimLower(tag.substr(0U, nameEnd));

        if (tagName == "svg") {
            docW = ParseFloat(ExtractAttr(tag, "width"), 0.0F);
            docH = ParseFloat(ExtractAttr(tag, "height"), 0.0F);
            const auto vb = ParseNumberList(ExtractAttr(tag, "viewBox"));
            if (vb.size() >= 4U && vb[2] > 0.0F && vb[3] > 0.0F) {
                vbX = vb[0];
                vbY = vb[1];
                vbW = vb[2];
                vbH = vb[3];
            } else if (docW > 0.0F && docH > 0.0F) {
                vbW = docW;
                vbH = docH;
            }
            continue;
        }

        SvgShape shape{};
        const float opacity = std::clamp(ParseFloat(ExtractAttr(tag, "opacity"), 1.0F), 0.0F, 1.0F);
        const float fillOp = std::clamp(ParseFloat(ExtractAttr(tag, "fill-opacity"), 1.0F), 0.0F, 1.0F);
        const float strokeOp =
            std::clamp(ParseFloat(ExtractAttr(tag, "stroke-opacity"), 1.0F), 0.0F, 1.0F);
        shape.fill = ParseColor(ExtractAttr(tag, "fill"), SvgColor{0.0F, 0.0F, 0.0F, 1.0F, false});
        shape.fill.a *= opacity * fillOp;
        shape.stroke = ParseColor(ExtractAttr(tag, "stroke"), SvgColor{0.0F, 0.0F, 0.0F, 0.0F, true});
        shape.stroke.a *= opacity * strokeOp;
        shape.strokeWidth = std::max(0.0F, ParseFloat(ExtractAttr(tag, "stroke-width"), 1.0F));

        if (tagName == "rect") {
            const float x = ParseFloat(ExtractAttr(tag, "x"), 0.0F);
            const float y = ParseFloat(ExtractAttr(tag, "y"), 0.0F);
            const float w = ParseFloat(ExtractAttr(tag, "width"), 0.0F);
            const float h = ParseFloat(ExtractAttr(tag, "height"), 0.0F);
            if (w > 0.0F && h > 0.0F) {
                shape.polygons.push_back({{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}});
                shapes.push_back(std::move(shape));
            }
        } else if (tagName == "circle" || tagName == "ellipse") {
            const float cx = ParseFloat(ExtractAttr(tag, "cx"), 0.0F);
            const float cy = ParseFloat(ExtractAttr(tag, "cy"), 0.0F);
            const float rx = tagName == "circle" ? ParseFloat(ExtractAttr(tag, "r"), 0.0F)
                                                 : ParseFloat(ExtractAttr(tag, "rx"), 0.0F);
            const float ry = tagName == "circle" ? rx : ParseFloat(ExtractAttr(tag, "ry"), 0.0F);
            if (rx > 0.0F && ry > 0.0F) {
                std::vector<SvgPoint> pts;
                constexpr int kSteps = 48;
                pts.reserve(kSteps);
                for (int i = 0; i < kSteps; ++i) {
                    const float a = (6.2831853F * static_cast<float>(i)) / static_cast<float>(kSteps);
                    pts.push_back({cx + rx * std::cos(a), cy + ry * std::sin(a)});
                }
                shape.polygons.push_back(std::move(pts));
                shapes.push_back(std::move(shape));
            }
        } else if (tagName == "polygon" || tagName == "polyline") {
            const auto nums = ParseNumberList(ExtractAttr(tag, "points"));
            std::vector<SvgPoint> pts;
            for (std::size_t i = 0U; i + 1U < nums.size(); i += 2U) {
                pts.push_back({nums[i], nums[i + 1U]});
            }
            if (pts.size() >= 2U) {
                shape.closed = (tagName == "polygon");
                shape.polygons.push_back(std::move(pts));
                shapes.push_back(std::move(shape));
            }
        } else if (tagName == "line") {
            const float x1 = ParseFloat(ExtractAttr(tag, "x1"), 0.0F);
            const float y1 = ParseFloat(ExtractAttr(tag, "y1"), 0.0F);
            const float x2 = ParseFloat(ExtractAttr(tag, "x2"), 0.0F);
            const float y2 = ParseFloat(ExtractAttr(tag, "y2"), 0.0F);
            shape.fill.none = true;
            shape.closed = false;
            shape.polygons.push_back({{x1, y1}, {x2, y2}});
            shapes.push_back(std::move(shape));
        } else if (tagName == "path") {
            shape.polygons = ParsePathPolygons(ExtractAttr(tag, "d"));
            if (!shape.polygons.empty()) {
                shapes.push_back(std::move(shape));
            }
        }
    }

    const float scale = std::clamp(options.rasterScale, 0.1F, 16.0F);
    const float baseW = docW > 0.0F ? docW : vbW;
    const float baseH = docH > 0.0F ? docH : vbH;
    const std::uint32_t outW =
        std::clamp(static_cast<std::uint32_t>(std::lrintf(baseW * scale)), 1U, options.maxDimension);
    const std::uint32_t outH =
        std::clamp(static_cast<std::uint32_t>(std::lrintf(baseH * scale)), 1U, options.maxDimension);

    std::vector<float> canvas(static_cast<std::size_t>(outW) * outH * 4U, 0.0F);
    const int aaSamples = options.antiAlias2x ? 2 : 1;
    const float invSamples = 1.0F / static_cast<float>(aaSamples * aaSamples);
    const float scaleX = vbW / static_cast<float>(outW);
    const float scaleY = vbH / static_cast<float>(outH);

    for (const SvgShape& shape : shapes) {
        const bool drawFill = !shape.fill.none && shape.fill.a > 1.0e-4F && shape.closed;
        const bool drawStroke = !shape.stroke.none && shape.stroke.a > 1.0e-4F && shape.strokeWidth > 0.0F;
        if (!drawFill && !drawStroke) {
            continue;
        }
        const float halfStrokeSq = (0.5F * shape.strokeWidth) * (0.5F * shape.strokeWidth);

        for (std::uint32_t y = 0U; y < outH; ++y) {
            for (std::uint32_t x = 0U; x < outW; ++x) {
                int fillHits = 0;
                int strokeHits = 0;
                for (int sy = 0; sy < aaSamples; ++sy) {
                    for (int sx = 0; sx < aaSamples; ++sx) {
                        const float px =
                            vbX + (static_cast<float>(x) + (static_cast<float>(sx) + 0.5F) / static_cast<float>(aaSamples)) * scaleX;
                        const float py =
                            vbY + (static_cast<float>(y) + (static_cast<float>(sy) + 0.5F) / static_cast<float>(aaSamples)) * scaleY;

                        if (drawFill) {
                            bool hit = false;
                            for (const auto& poly : shape.polygons) {
                                if (PointInPolygon(px, py, poly)) {
                                    hit = !hit;
                                }
                            }
                            if (hit) ++fillHits;
                        }
                        if (drawStroke) {
                            bool hit = false;
                            for (const auto& poly : shape.polygons) {
                                if (poly.size() < 2U) continue;
                                const std::size_t segCount = shape.closed ? poly.size() : (poly.size() - 1U);
                                for (std::size_t i = 0U; i < segCount; ++i) {
                                    const auto& a = poly[i];
                                    const auto& b = poly[(i + 1U) % poly.size()];
                                    if (DistSqPointSegment(px, py, a, b) <= halfStrokeSq) {
                                        hit = true;
                                        break;
                                    }
                                }
                                if (hit) break;
                            }
                            if (hit) ++strokeHits;
                        }
                    }
                }
                if (fillHits > 0) {
                    BlendPixel(canvas, outW, x, y, shape.fill, static_cast<float>(fillHits) * invSamples);
                }
                if (strokeHits > 0) {
                    BlendPixel(canvas, outW, x, y, shape.stroke, static_cast<float>(strokeHits) * invSamples);
                }
            }
        }
    }

    outTexture = TextureAsset{};
    outTexture.width = outW;
    outTexture.height = outH;
    outTexture.format = TextureAssetFormat::RGBA8Unorm;
    outTexture.colorSpace = TextureAssetColorSpace::Srgb;
    outTexture.usage = TextureUsage::Color;
    outTexture.pixels.resize(static_cast<std::size_t>(outW) * outH * 4U);
    for (std::size_t i = 0U; i < outTexture.pixels.size(); ++i) {
        const int v = static_cast<int>(std::lrintf(std::clamp(canvas[i], 0.0F, 1.0F) * 255.0F));
        outTexture.pixels[i] = static_cast<std::byte>(std::clamp(v, 0, 255));
    }
    return true;
}

} // namespace UVE::Asset
