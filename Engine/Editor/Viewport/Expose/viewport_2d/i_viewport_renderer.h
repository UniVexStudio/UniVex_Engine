#pragma once
// Backend seam: the ONLY thing the 2D viewport needs from the renderer.
// All coordinates are screen-space pixels (origin top-left). A very basic
// backend (lines, rects, quads, bitmap text) is enough. Implement this on UVE.
#include <cstdint>

namespace univex::viewport {

struct Vec2 {
    float x = 0.f, y = 0.f;
    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
};

struct Color {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
};

enum class TextOrient { Horizontal, VerticalUp };

struct IViewportRenderer {
    virtual ~IViewportRenderer() = default;
    virtual void beginFrame(int widthPx, int heightPx, Color clear) = 0;
    virtual void drawLine(Vec2 a, Vec2 b, Color c, float thicknessPx) = 0;
    virtual void fillRect(Vec2 topLeft, Vec2 sizePx, Color c) = 0;
    // Convex quad, vertices in order (used for rotated sprites / selection fill).
    virtual void fillQuad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Color col) = 0;
    // Horizontal: pos = top-left of the text. VerticalUp: text reads bottom->top,
    // pos = bottom-left of the rotated text.
    virtual void drawText(Vec2 pos, const char* text, Color c, float sizePx, TextOrient orient) = 0;
    virtual void endFrame() = 0;
};

} // namespace univex::viewport
