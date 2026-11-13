// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_widget_designer.h"

#include "prism/Application.h"
#include "prism/Canvas.h"
#include "prism/Font.h"
#include "prism/Theme.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

namespace UVE::Editor {
namespace {

/// The UI font at a pixel size (the game's text drawn as big as it will be on screen).
const prism::Font& FontAtUVE(const float px, const bool bold) {
    static std::map<std::pair<int, bool>, std::shared_ptr<prism::Font>> cache;
    const int size = std::clamp(static_cast<int>(std::lround(px)), 6, 160);
    const auto key = std::make_pair(size, bold);
    if (const auto it = cache.find(key); it != cache.end()) {
        return *it->second;
    }
    auto font = std::make_shared<prism::Font>(prism::theme().fontDir + "/fonts/" + (bold ? "Inter-SemiBold.otf" : "Inter-Regular.otf"), size);
    if (!font->ok()) {
        return bold ? *prism::theme().semibold : *prism::theme().ui;
    }
    cache[key] = font;
    return *font;
}

[[nodiscard]] prism::Color MixUVE(const prism::Color a, const prism::Color b, const float t) {
    const auto lerp = [t](const std::uint8_t x, const std::uint8_t y) {
        return static_cast<std::uint8_t>(std::lround(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t));
    };
    return prism::Color(lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), lerp(a.a, b.a));
}

} // namespace

WidgetDesignerViewUVE::WidgetDesignerViewUVE() {
    setTooltip("Designer: click a widget to select it, drag it to move it");
}

void WidgetDesignerViewUVE::SetItemsUVE(std::vector<ItemUVE> items) {
    m_items = std::move(items);
    invalidate();
}

void WidgetDesignerViewUVE::SetSelectedUVE(const Scene::EntityUVE entity) {
    if (m_selected != entity) {
        m_selected = entity;
        invalidate();
    }
}

void WidgetDesignerViewUVE::SetResolutionUVE(const float width, const float height) {
    m_designW = std::max(64.0F, width);
    m_designH = std::max(64.0F, height);
    invalidate();
}

prism::Vec2 WidgetDesignerViewUVE::measure(const prism::Vec2 available) {
    return constrain(prism::Vec2{std::max(available.x, 200.0F), std::max(available.y, 150.0F)});
}

float WidgetDesignerViewUVE::ScaleUVE() const {
    const prism::Rect r = rect();
    const float margin = 36.0F;
    return std::max(0.05F, std::min((r.w - 2.0F * margin) / m_designW, (r.h - 2.0F * margin - 18.0F) / m_designH));
}

prism::Rect WidgetDesignerViewUVE::CanvasRectUVE() const {
    const prism::Rect r = rect();
    const float s = ScaleUVE();
    const float w = std::floor(m_designW * s);
    const float h = std::floor(m_designH * s);
    return prism::Rect{std::floor(r.x + (r.w - w) * 0.5F), std::floor(r.y + 18.0F + (r.h - 18.0F - h) * 0.5F), w, h};
}

prism::Rect WidgetDesignerViewUVE::ToViewUVE(const prism::Rect& design) const {
    const prism::Rect canvas = CanvasRectUVE();
    const float s = ScaleUVE();
    return prism::Rect{canvas.x + design.x * s, canvas.y + design.y * s, design.w * s, design.h * s};
}

Scene::EntityUVE WidgetDesignerViewUVE::HitUVE(const prism::Vec2 p) const {
    // The last drawn is on top.
    for (auto it = m_items.rbegin(); it != m_items.rend(); ++it) {
        if (it->visible && ToViewUVE(it->rect).contains(p)) {
            // A Button's label is part of the Button: the Button is what is picked and moved.
            if (it->kind == KindUVE::Text && it->parent != Scene::kInvalidEntityUVE) {
                for (const ItemUVE& other : m_items) {
                    if (other.entity == it->parent && other.kind == KindUVE::Button) {
                        return other.entity;
                    }
                }
            }
            return it->entity;
        }
    }
    return Scene::kInvalidEntityUVE;
}

void WidgetDesignerViewUVE::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    const prism::Rect r = rect();
    c.fillRect(r, prism::Color(24, 25, 28));
    // A faint grid behind the canvas, as a design surface.
    for (float x = r.x + 16.0F; x < r.right(); x += 32.0F) {
        c.vline(std::floor(x), r.y, r.bottom(), prism::Color(30, 31, 35));
    }
    for (float y = r.y + 16.0F; y < r.bottom(); y += 32.0F) {
        c.hline(r.x, r.right(), std::floor(y), prism::Color(30, 31, 35));
    }
    const prism::Rect canvas = CanvasRectUVE();
    const float s = ScaleUVE();
    c.shadow(canvas, 2.0F, 10, prism::Color(0, 0, 0, 120));
    c.fillRect(canvas, prism::Color(46, 52, 62));
    c.gradientV(canvas, prism::Color(54, 62, 76), prism::Color(36, 40, 48));
    c.strokeRect(canvas, prism::Color(90, 96, 108));
    c.textIn(*t.small, prism::Rect{canvas.x, canvas.y - 18.0F, canvas.w, 16.0F},
             std::to_string(static_cast<int>(m_designW)) + " x " + std::to_string(static_cast<int>(m_designH)) + "   (" +
                 std::to_string(static_cast<int>(std::lround(s * 100.0F))) + "%)",
             t.textDim, prism::HAlign::Left, false);

    c.pushClip(canvas);
    for (const ItemUVE& item : m_items) {
        if (!item.visible) {
            continue;
        }
        const prism::Rect v = ToViewUVE(item.rect);
        switch (item.kind) {
            case KindUVE::Text:
                c.text(FontAtUVE(item.fontSize * s, false), prism::Vec2{v.x, v.y + item.fontSize * s * 0.95F}, item.text, item.color);
                break;
            case KindUVE::Button: {
                const prism::Color base = item.entity == m_hover ? MixUVE(item.color, prism::Color(255, 255, 255), 0.12F) : item.color;
                c.fillRoundRect(v, 4.0F * s, base);
                c.strokeRoundRect(v, 4.0F * s, MixUVE(base, prism::Color(0, 0, 0), 0.35F));
                break;
            }
            case KindUVE::Image:
                c.fillRect(v, item.color);
                break;
            case KindUVE::ProgressBar: {
                c.fillRoundRect(v, 3.0F * s, prism::Color(30, 32, 36));
                prism::Rect fill = v;
                fill.w = std::floor(v.w * std::clamp(item.value, 0.0F, 1.0F));
                c.fillRoundRect(fill, 3.0F * s, prism::Color(88, 180, 110));
                c.strokeRoundRect(v, 3.0F * s, prism::Color(80, 84, 92));
                break;
            }
            case KindUVE::Slider: {
                const float mid = v.y + v.h * 0.5F;
                c.fillRoundRect(prism::Rect{v.x, mid - 2.0F, v.w, 4.0F}, 2.0F, prism::Color(70, 74, 82));
                const float knobX = v.x + v.w * std::clamp(item.value, 0.0F, 1.0F);
                c.fillRoundRect(prism::Rect{v.x, mid - 2.0F, knobX - v.x, 4.0F}, 2.0F, t.accent);
                c.fillCircle(prism::Vec2{knobX, mid}, std::max(4.0F, v.h * 0.35F), prism::Color(230, 232, 236));
                break;
            }
            case KindUVE::CheckBox: {
                const float side = std::min(v.w, v.h);
                const prism::Rect box{v.x, v.y + (v.h - side) * 0.5F, side, side};
                c.fillRoundRect(box, 3.0F, item.checked ? t.accent : prism::Color(36, 38, 42));
                c.strokeRoundRect(box, 3.0F, prism::Color(110, 116, 126));
                if (item.checked) {
                    const prism::Vec2 a{box.x + side * 0.22F, box.y + side * 0.52F};
                    const prism::Vec2 b{box.x + side * 0.42F, box.y + side * 0.72F};
                    const prism::Vec2 d{box.x + side * 0.78F, box.y + side * 0.28F};
                    c.line(a, b, prism::Color(255, 255, 255), std::max(1.5F, side * 0.1F));
                    c.line(b, d, prism::Color(255, 255, 255), std::max(1.5F, side * 0.1F));
                }
                break;
            }
            case KindUVE::ComboBox: {
                c.fillRoundRect(v, 3.0F * s, prism::Color(34, 34, 40));
                c.strokeRoundRect(v, 3.0F * s, prism::Color(150, 120, 230));
                const prism::Font& font = FontAtUVE(item.fontSize * s, false);
                c.textIn(font, prism::Rect{v.x + 6.0F * s, v.y, v.w - v.h - 8.0F * s, v.h}, item.text, item.color, prism::HAlign::Left, true);
                const float a = std::max(3.0F, v.h * 0.18F);
                const prism::Vec2 mid{v.right() - v.h * 0.5F, v.y + v.h * 0.5F};
                const prism::Vec2 tri[] = {{mid.x - a, mid.y - a * 0.5F}, {mid.x + a, mid.y - a * 0.5F}, {mid.x, mid.y + a * 0.6F}};
                c.fillPolygon(tri, prism::Color(220, 210, 245));
                break;
            }
            case KindUVE::VerticalBox:
            case KindUVE::HorizontalBox:
            case KindUVE::WrapBox:
            case KindUVE::ScrollBox: {
                // A panel lays its children out; it draws nothing in the game, so the Designer marks
                // where it is with a dashed edge and its name.
                const prism::Color edge = item.kind == KindUVE::ScrollBox ? prism::Color(60, 200, 190, 200) : prism::Color(245, 160, 60, 200);
                const float dash = 6.0F;
                for (float x = v.x; x < v.right(); x += dash * 2.0F) {
                    c.hline(x, std::min(x + dash, v.right()), std::floor(v.y), edge);
                    c.hline(x, std::min(x + dash, v.right()), std::floor(v.bottom()) - 1.0F, edge);
                }
                for (float y = v.y; y < v.bottom(); y += dash * 2.0F) {
                    c.vline(std::floor(v.x), y, std::min(y + dash, v.bottom()), edge);
                    c.vline(std::floor(v.right()) - 1.0F, y, std::min(y + dash, v.bottom()), edge);
                }
                if (item.kind == KindUVE::ScrollBox) {
                    const prism::Rect track{v.right() - 8.0F, v.y + 3.0F, 5.0F, v.h - 6.0F};
                    c.fillRoundRect(track, 2.0F, prism::Color(30, 32, 38));
                    c.fillRoundRect(prism::Rect{track.x, track.y, track.w, track.h * 0.35F}, 2.0F, prism::Color(60, 200, 190));
                }
                if (const EditorIconsUVE::ImageUVE icon = WidgetIconUVE(item.kind, 14); icon) {
                    c.blitBlend(icon.pixels, icon.size, icon.size, icon.size, prism::Rect{v.x + 3.0F, v.y + 3.0F, 14.0F, 14.0F});
                }
                c.textIn(*t.small, prism::Rect{v.x + 20.0F, v.y + 2.0F, std::max(0.0F, v.w - 24.0F), 16.0F}, item.name, edge,
                         prism::HAlign::Left, true);
                break;
            }
            case KindUVE::Border:
                c.fillRoundRect(v, 6.0F * s, item.color);
                c.strokeRoundRect(v, 6.0F * s, MixUVE(item.color, prism::Color(255, 255, 255), 0.3F), std::max(1, static_cast<int>(3.0F * s)));
                break;
            case KindUVE::Spacer:
                if (item.entity == m_hover || item.entity == m_selected) {
                    c.fillRect(v, prism::Color(200, 200, 210, 30));
                }
                break;
            case KindUVE::Canvas:
                break;
            case KindUVE::TextBox: {
                c.fillRoundRect(v, 3.0F * s, prism::Color(22, 23, 26));
                c.strokeRoundRect(v, 3.0F * s, prism::Color(96, 102, 112));
                const prism::Font& font = FontAtUVE(item.fontSize * s, false);
                c.textIn(font, prism::Rect{v.x + 6.0F * s, v.y, v.w - 12.0F * s, v.h}, item.text.empty() ? item.name : item.text,
                         item.text.empty() ? prism::Color(120, 124, 132) : item.color, prism::HAlign::Left, true);
                break;
            }
        }
    }
    c.popClip();

    // Hover and selection on top: an outline, and handles on the selected one.
    for (const ItemUVE& item : m_items) {
        if (!item.visible || (item.entity != m_selected && item.entity != m_hover)) {
            continue;
        }
        const prism::Rect v = ToViewUVE(item.rect);
        const prism::Rect frame{std::floor(v.x) - 1.0F, std::floor(v.y) - 1.0F, std::ceil(v.w) + 2.0F, std::ceil(v.h) + 2.0F};
        if (item.entity == m_selected) {
            c.strokeRect(frame, t.accent, 2);
            for (const prism::Vec2 corner : {frame.pos(), prism::Vec2{frame.right(), frame.y}, prism::Vec2{frame.x, frame.bottom()},
                                             prism::Vec2{frame.right(), frame.bottom()}}) {
                c.fillRect(prism::Rect{corner.x - 3.0F, corner.y - 3.0F, 6.0F, 6.0F}, prism::Color(255, 255, 255));
                c.strokeRect(prism::Rect{corner.x - 3.0F, corner.y - 3.0F, 6.0F, 6.0F}, t.accent);
            }
            c.textIn(*t.small, prism::Rect{frame.x, frame.bottom() + 3.0F, std::max(frame.w, 160.0F), 14.0F}, item.name, t.accent,
                     prism::HAlign::Left, true);
        } else {
            c.strokeRect(frame, prism::Color(t.accent.r, t.accent.g, t.accent.b, 140));
        }
    }
}

bool WidgetDesignerViewUVE::onMouseDown(const prism::MouseEvent& e) {
    if (e.button != prism::MouseButton::Left) {
        return false;
    }
    const Scene::EntityUVE hit = HitUVE(e.pos);
    m_selected = hit;
    if (onPicked) {
        onPicked(hit);
    }
    m_dragging = hit != Scene::kInvalidEntityUVE;
    m_moved = false;
    m_dragLast = e.pos;
    m_dragAccum = prism::Vec2{0.0F, 0.0F};
    invalidate();
    return true;
}

bool WidgetDesignerViewUVE::onMouseMove(const prism::MouseEvent& e) {
    if (!m_dragging) {
        const Scene::EntityUVE hover = HitUVE(e.pos);
        if (hover != m_hover) {
            m_hover = hover;
            invalidate();
        }
        return false;
    }
    const float s = ScaleUVE();
    // Whole design pixels, so the widget stays on the pixel grid.
    m_dragAccum.x += (e.pos.x - m_dragLast.x) / s;
    m_dragAccum.y += (e.pos.y - m_dragLast.y) / s;
    m_dragLast = e.pos;
    const float dx = std::trunc(m_dragAccum.x);
    const float dy = std::trunc(m_dragAccum.y);
    if (dx == 0.0F && dy == 0.0F) {
        return true;
    }
    m_dragAccum.x -= dx;
    m_dragAccum.y -= dy;
    m_moved = true;
    for (ItemUVE& item : m_items) {
        if (item.entity == m_selected || item.parent == m_selected) {
            item.rect.x += dx;
            item.rect.y += dy;
        }
    }
    if (onMoved) {
        onMoved(m_selected, dx, dy, false);
    }
    invalidate();
    return true;
}

bool WidgetDesignerViewUVE::onMouseUp(const prism::MouseEvent& e) {
    if (e.button != prism::MouseButton::Left || !m_dragging) {
        return false;
    }
    m_dragging = false;
    if (m_moved && onMoved) {
        onMoved(m_selected, 0.0F, 0.0F, true);
    }
    m_moved = false;
    return true;
}

} // namespace UVE::Editor
