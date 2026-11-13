// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_widget_icons.h"

#include "canvas_component.h"
#include "i_entity_manager.h"
#include "ui_button_component.h"
#include "ui_checkbox_component.h"
#include "ui_dropdown_component.h"
#include "ui_image_component.h"
#include "ui_layout_container_component.h"
#include "ui_progress_bar_component.h"
#include "ui_scroll_container_component.h"
#include "ui_slider_component.h"
#include "ui_text_component.h"
#include "ui_text_input_component.h"
#include "ui_view_3d_component.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <vector>

namespace UVE::Editor {
namespace {

/// A colour with alpha, 0..1.
struct InkUVE {
    float r = 1.0F, g = 1.0F, b = 1.0F, a = 1.0F;
};

constexpr InkUVE kWhiteUVE{0.93F, 0.95F, 0.98F, 1.0F};
constexpr InkUVE kDimUVE{0.62F, 0.66F, 0.72F, 1.0F};
constexpr InkUVE kDarkUVE{0.13F, 0.14F, 0.17F, 1.0F};
constexpr InkUVE kBlueUVE{0.24F, 0.52F, 0.96F, 1.0F};
constexpr InkUVE kGreenUVE{0.34F, 0.78F, 0.45F, 1.0F};
constexpr InkUVE kTealUVE{0.20F, 0.76F, 0.74F, 1.0F};
constexpr InkUVE kOrangeUVE{0.97F, 0.62F, 0.22F, 1.0F};
constexpr InkUVE kPurpleUVE{0.66F, 0.48F, 0.96F, 1.0F};
constexpr InkUVE kYellowUVE{0.98F, 0.82F, 0.30F, 1.0F};
constexpr InkUVE kGoldUVE{0.82F, 0.66F, 0.40F, 1.0F};

/// A 16-unit design square painted at any size: each shape is a coverage test, sampled 4x4 per
/// pixel, laid over what is there.
class IconPainterUVE final {
public:
    explicit IconPainterUVE(const int size) : m_size(size), m_pixels(static_cast<std::size_t>(size * size), InkUVE{0, 0, 0, 0}) {}

    void ShapeUVE(const std::function<bool(float, float)>& inside, const InkUVE ink) {
        const float unit = 16.0F / static_cast<float>(m_size);
        for (int y = 0; y < m_size; ++y) {
            for (int x = 0; x < m_size; ++x) {
                int hits = 0;
                for (int sy = 0; sy < 4; ++sy) {
                    for (int sx = 0; sx < 4; ++sx) {
                        const float u = (static_cast<float>(x) + (static_cast<float>(sx) + 0.5F) / 4.0F) * unit;
                        const float v = (static_cast<float>(y) + (static_cast<float>(sy) + 0.5F) / 4.0F) * unit;
                        hits += inside(u, v) ? 1 : 0;
                    }
                }
                if (hits == 0) {
                    continue;
                }
                const float a = ink.a * static_cast<float>(hits) / 16.0F;
                InkUVE& dst = m_pixels[static_cast<std::size_t>(y * m_size + x)];
                const float outA = a + dst.a * (1.0F - a);
                if (outA <= 0.0F) {
                    continue;
                }
                dst.r = (ink.r * a + dst.r * dst.a * (1.0F - a)) / outA;
                dst.g = (ink.g * a + dst.g * dst.a * (1.0F - a)) / outA;
                dst.b = (ink.b * a + dst.b * dst.a * (1.0F - a)) / outA;
                dst.a = outA;
            }
        }
    }

    void RoundRectUVE(const float x, const float y, const float w, const float h, const float r, const InkUVE ink) {
        ShapeUVE([=](const float u, const float v) { return InRoundRectUVE(u, v, x, y, w, h, r); }, ink);
    }
    /// An outline `t` units thick.
    void FrameUVE(const float x, const float y, const float w, const float h, const float r, const float t, const InkUVE ink) {
        ShapeUVE([=](const float u, const float v) {
            return InRoundRectUVE(u, v, x, y, w, h, r) && !InRoundRectUVE(u, v, x + t, y + t, w - 2.0F * t, h - 2.0F * t, std::max(0.0F, r - t));
        }, ink);
    }
    void CircleUVE(const float cx, const float cy, const float r, const InkUVE ink) {
        ShapeUVE([=](const float u, const float v) { return (u - cx) * (u - cx) + (v - cy) * (v - cy) <= r * r; }, ink);
    }
    void LineUVE(const float x0, const float y0, const float x1, const float y1, const float width, const InkUVE ink) {
        ShapeUVE([=](const float u, const float v) {
            const float dx = x1 - x0;
            const float dy = y1 - y0;
            const float len2 = dx * dx + dy * dy;
            const float t = len2 > 0.0F ? std::clamp(((u - x0) * dx + (v - y0) * dy) / len2, 0.0F, 1.0F) : 0.0F;
            const float px = x0 + t * dx - u;
            const float py = y0 + t * dy - v;
            return px * px + py * py <= width * width * 0.25F;
        }, ink);
    }
    void TriangleUVE(const float ax, const float ay, const float bx, const float by, const float cx, const float cy, const InkUVE ink) {
        ShapeUVE([=](const float u, const float v) {
            const auto side = [&](const float x0, const float y0, const float x1, const float y1) {
                return (x1 - x0) * (v - y0) - (y1 - y0) * (u - x0);
            };
            const float d0 = side(ax, ay, bx, by);
            const float d1 = side(bx, by, cx, cy);
            const float d2 = side(cx, cy, ax, ay);
            return (d0 >= 0.0F && d1 >= 0.0F && d2 >= 0.0F) || (d0 <= 0.0F && d1 <= 0.0F && d2 <= 0.0F);
        }, ink);
    }

    [[nodiscard]] std::vector<std::uint32_t> PixelsUVE() const {
        std::vector<std::uint32_t> out(m_pixels.size());
        for (std::size_t i = 0; i < m_pixels.size(); ++i) {
            const InkUVE& p = m_pixels[i];
            const auto byte = [](const float f) { return static_cast<std::uint32_t>(std::lround(std::clamp(f, 0.0F, 1.0F) * 255.0F)); };
            out[i] = (byte(p.a) << 24U) | (byte(p.r) << 16U) | (byte(p.g) << 8U) | byte(p.b);
        }
        return out;
    }

private:
    static bool InRoundRectUVE(const float u, const float v, const float x, const float y, const float w, const float h, const float r) {
        if (w <= 0.0F || h <= 0.0F || u < x || v < y || u > x + w || v > y + h) {
            return false;
        }
        const float cx = std::clamp(u, x + r, x + w - r);
        const float cy = std::clamp(v, y + r, y + h - r);
        return (u - cx) * (u - cx) + (v - cy) * (v - cy) <= r * r;
    }

    int m_size = 16;
    std::vector<InkUVE> m_pixels;
};

void PaintUVE(IconPainterUVE& p, const WidgetKindUVE kind) {
    switch (kind) {
        case WidgetKindUVE::Canvas:
            p.FrameUVE(1.5F, 2.5F, 13.0F, 11.0F, 1.5F, 1.3F, kTealUVE);
            p.RoundRectUVE(3.5F, 4.5F, 4.0F, 2.0F, 0.6F, kWhiteUVE);
            p.RoundRectUVE(3.5F, 8.0F, 6.5F, 3.0F, 0.8F, kBlueUVE);
            break;
        case WidgetKindUVE::Text:
            p.RoundRectUVE(2.5F, 2.5F, 11.0F, 2.6F, 0.6F, kWhiteUVE);
            p.RoundRectUVE(6.7F, 2.5F, 2.6F, 11.0F, 0.6F, kWhiteUVE);
            p.RoundRectUVE(5.2F, 12.0F, 5.6F, 1.5F, 0.5F, kDimUVE);
            break;
        case WidgetKindUVE::Button:
            p.RoundRectUVE(1.0F, 4.0F, 14.0F, 8.5F, 2.2F, kBlueUVE);
            p.RoundRectUVE(1.0F, 4.0F, 14.0F, 3.6F, 2.2F, InkUVE{1.0F, 1.0F, 1.0F, 0.18F});
            p.RoundRectUVE(4.5F, 7.6F, 7.0F, 1.6F, 0.8F, kWhiteUVE);
            p.TriangleUVE(10.5F, 9.5F, 14.5F, 13.0F, 11.6F, 13.8F, kWhiteUVE);
            break;
        case WidgetKindUVE::Image:
            p.FrameUVE(1.5F, 2.5F, 13.0F, 11.0F, 1.5F, 1.2F, kDimUVE);
            p.TriangleUVE(3.0F, 12.0F, 7.0F, 6.5F, 10.5F, 12.0F, kGreenUVE);
            p.TriangleUVE(7.5F, 12.0F, 10.5F, 8.5F, 13.0F, 12.0F, InkUVE{0.24F, 0.62F, 0.36F, 1.0F});
            p.CircleUVE(11.2F, 5.6F, 1.5F, kYellowUVE);
            break;
        case WidgetKindUVE::ProgressBar:
            p.RoundRectUVE(1.0F, 6.0F, 14.0F, 4.5F, 2.0F, kDarkUVE);
            p.FrameUVE(1.0F, 6.0F, 14.0F, 4.5F, 2.0F, 0.8F, kDimUVE);
            p.RoundRectUVE(1.8F, 6.8F, 7.6F, 2.9F, 1.4F, kGreenUVE);
            break;
        case WidgetKindUVE::Slider:
            p.LineUVE(1.5F, 8.0F, 14.5F, 8.0F, 1.6F, kDimUVE);
            p.LineUVE(1.5F, 8.0F, 9.0F, 8.0F, 1.6F, kBlueUVE);
            p.CircleUVE(9.0F, 8.0F, 3.0F, kWhiteUVE);
            p.CircleUVE(9.0F, 8.0F, 1.4F, kBlueUVE);
            break;
        case WidgetKindUVE::CheckBox:
            p.RoundRectUVE(2.0F, 2.0F, 12.0F, 12.0F, 2.4F, kGreenUVE);
            p.LineUVE(4.6F, 8.2F, 7.0F, 10.8F, 1.9F, kWhiteUVE);
            p.LineUVE(7.0F, 10.8F, 11.6F, 5.0F, 1.9F, kWhiteUVE);
            break;
        case WidgetKindUVE::TextBox:
            p.RoundRectUVE(0.8F, 4.0F, 14.4F, 8.0F, 1.6F, kDarkUVE);
            p.FrameUVE(0.8F, 4.0F, 14.4F, 8.0F, 1.6F, 1.0F, kBlueUVE);
            p.RoundRectUVE(3.0F, 7.2F, 5.5F, 1.6F, 0.6F, kDimUVE);
            p.LineUVE(10.2F, 5.8F, 10.2F, 10.2F, 1.2F, kWhiteUVE);
            break;
        case WidgetKindUVE::ComboBox:
            p.RoundRectUVE(0.8F, 4.0F, 14.4F, 8.0F, 1.6F, kDarkUVE);
            p.FrameUVE(0.8F, 4.0F, 14.4F, 8.0F, 1.6F, 1.0F, kPurpleUVE);
            p.RoundRectUVE(2.8F, 7.2F, 5.5F, 1.6F, 0.6F, kWhiteUVE);
            p.RoundRectUVE(9.8F, 4.6F, 4.8F, 6.8F, 1.2F, kPurpleUVE);
            p.TriangleUVE(10.7F, 7.0F, 13.7F, 7.0F, 12.2F, 9.2F, kWhiteUVE);
            break;
        case WidgetKindUVE::VerticalBox:
            p.FrameUVE(1.5F, 1.5F, 13.0F, 13.0F, 1.6F, 1.1F, kOrangeUVE);
            for (int i = 0; i < 3; ++i) {
                p.RoundRectUVE(3.6F, 3.6F + static_cast<float>(i) * 3.2F, 8.8F, 2.3F, 0.7F, kWhiteUVE);
            }
            break;
        case WidgetKindUVE::HorizontalBox:
            p.FrameUVE(1.5F, 1.5F, 13.0F, 13.0F, 1.6F, 1.1F, kOrangeUVE);
            for (int i = 0; i < 3; ++i) {
                p.RoundRectUVE(3.6F + static_cast<float>(i) * 3.2F, 3.6F, 2.3F, 8.8F, 0.7F, kWhiteUVE);
            }
            break;
        case WidgetKindUVE::WrapBox:
            p.FrameUVE(1.5F, 1.5F, 13.0F, 13.0F, 1.6F, 1.1F, kOrangeUVE);
            p.RoundRectUVE(3.6F, 3.6F, 3.8F, 3.8F, 0.7F, kWhiteUVE);
            p.RoundRectUVE(8.6F, 3.6F, 3.8F, 3.8F, 0.7F, kWhiteUVE);
            p.RoundRectUVE(3.6F, 8.6F, 3.8F, 3.8F, 0.7F, kWhiteUVE);
            p.LineUVE(9.2F, 10.5F, 12.0F, 10.5F, 1.0F, kOrangeUVE);
            p.TriangleUVE(8.4F, 10.5F, 10.0F, 9.2F, 10.0F, 11.8F, kOrangeUVE);
            break;
        case WidgetKindUVE::ScrollBox:
            p.FrameUVE(1.5F, 1.5F, 13.0F, 13.0F, 1.6F, 1.1F, kTealUVE);
            for (int i = 0; i < 3; ++i) {
                p.RoundRectUVE(3.4F, 3.8F + static_cast<float>(i) * 3.1F, 6.6F, 1.8F, 0.6F, kDimUVE);
            }
            p.RoundRectUVE(11.2F, 3.2F, 2.0F, 9.6F, 1.0F, kDarkUVE);
            p.RoundRectUVE(11.2F, 4.0F, 2.0F, 4.0F, 1.0F, kTealUVE);
            break;
        case WidgetKindUVE::Border:
            p.FrameUVE(1.2F, 1.2F, 13.6F, 13.6F, 3.0F, 2.4F, kGoldUVE);
            p.RoundRectUVE(4.6F, 4.6F, 6.8F, 6.8F, 1.0F, InkUVE{0.82F, 0.66F, 0.40F, 0.35F});
            break;
        case WidgetKindUVE::View3D: {
            // A cube in the round, an orbit around it.
            const InkUVE top{0.55F, 0.80F, 1.0F, 1.0F};
            const InkUVE left{0.24F, 0.52F, 0.96F, 1.0F};
            const InkUVE right{0.15F, 0.34F, 0.70F, 1.0F};
            p.TriangleUVE(8.0F, 2.6F, 12.6F, 5.2F, 8.0F, 7.8F, top);
            p.TriangleUVE(8.0F, 2.6F, 3.4F, 5.2F, 8.0F, 7.8F, top);
            p.TriangleUVE(3.4F, 5.2F, 8.0F, 7.8F, 3.4F, 10.6F, left);
            p.TriangleUVE(8.0F, 7.8F, 8.0F, 13.2F, 3.4F, 10.6F, left);
            p.TriangleUVE(12.6F, 5.2F, 8.0F, 7.8F, 12.6F, 10.6F, right);
            p.TriangleUVE(8.0F, 7.8F, 8.0F, 13.2F, 12.6F, 10.6F, right);
            p.ShapeUVE([](const float u, const float v) {
                const float dx = (u - 8.0F) / 7.2F;
                const float dy = (v - 11.0F) / 2.6F;
                const float r = dx * dx + dy * dy;
                return r <= 1.0F && r >= 0.62F && v > 11.0F;
            }, kYellowUVE);
            p.TriangleUVE(14.6F, 10.0F, 15.6F, 12.6F, 12.8F, 12.2F, kYellowUVE);
            break;
        }
        case WidgetKindUVE::Spacer:
            p.LineUVE(2.0F, 4.0F, 2.0F, 12.0F, 1.2F, kDimUVE);
            p.LineUVE(14.0F, 4.0F, 14.0F, 12.0F, 1.2F, kDimUVE);
            p.LineUVE(4.0F, 8.0F, 12.0F, 8.0F, 1.2F, kWhiteUVE);
            p.TriangleUVE(3.2F, 8.0F, 5.6F, 6.2F, 5.6F, 9.8F, kWhiteUVE);
            p.TriangleUVE(12.8F, 8.0F, 10.4F, 6.2F, 10.4F, 9.8F, kWhiteUVE);
            break;
    }
}

} // namespace

const char* WidgetKindLabelUVE(const WidgetKindUVE kind) noexcept {
    switch (kind) {
        case WidgetKindUVE::Canvas: return "Canvas Panel";
        case WidgetKindUVE::Text: return "Text";
        case WidgetKindUVE::Button: return "Button";
        case WidgetKindUVE::Image: return "Image";
        case WidgetKindUVE::ProgressBar: return "Progress Bar";
        case WidgetKindUVE::Slider: return "Slider";
        case WidgetKindUVE::CheckBox: return "Check Box";
        case WidgetKindUVE::TextBox: return "Text Box";
        case WidgetKindUVE::ComboBox: return "Combo Box";
        case WidgetKindUVE::VerticalBox: return "Vertical Box";
        case WidgetKindUVE::HorizontalBox: return "Horizontal Box";
        case WidgetKindUVE::WrapBox: return "Wrap Box";
        case WidgetKindUVE::ScrollBox: return "Scroll Box";
        case WidgetKindUVE::Border: return "Border";
        case WidgetKindUVE::Spacer: return "Spacer";
        case WidgetKindUVE::View3D: return "3D View";
    }
    return "Widget";
}

std::optional<WidgetKindUVE> ClassifyWidgetUVE(const Scene::IEntityManagerUVE& constEm, const Scene::EntityUVE entity) {
    auto& em = const_cast<Scene::IEntityManagerUVE&>(constEm);
    if (!em.IsAliveUVE(entity)) {
        return std::nullopt;
    }
    if (em.HasComponentUVE<Scene::UIView3DComponentUVE>(entity)) {
        return WidgetKindUVE::View3D;
    }
    if (em.HasComponentUVE<Scene::UIButtonComponentUVE>(entity)) {
        return WidgetKindUVE::Button;
    }
    if (em.HasComponentUVE<Scene::UIDropdownComponentUVE>(entity)) {
        return WidgetKindUVE::ComboBox;
    }
    if (em.HasComponentUVE<Scene::UIScrollContainerComponentUVE>(entity)) {
        return WidgetKindUVE::ScrollBox;
    }
    if (em.HasComponentUVE<Scene::UILayoutContainerComponentUVE>(entity)) {
        const auto& layout = em.GetComponentUVE<Scene::UILayoutContainerComponentUVE>(entity);
        if (layout.wrapAfter > 0U) {
            return WidgetKindUVE::WrapBox;
        }
        return layout.direction == Scene::UILayoutDirectionUVE::Horizontal ? WidgetKindUVE::HorizontalBox : WidgetKindUVE::VerticalBox;
    }
    if (em.HasComponentUVE<Scene::UIProgressBarComponentUVE>(entity)) {
        return WidgetKindUVE::ProgressBar;
    }
    if (em.HasComponentUVE<Scene::UISliderComponentUVE>(entity)) {
        return WidgetKindUVE::Slider;
    }
    if (em.HasComponentUVE<Scene::UICheckboxComponentUVE>(entity)) {
        return WidgetKindUVE::CheckBox;
    }
    if (em.HasComponentUVE<Scene::UITextInputComponentUVE>(entity)) {
        return WidgetKindUVE::TextBox;
    }
    if (em.HasComponentUVE<Scene::UIImageComponentUVE>(entity)) {
        const auto& image = em.GetComponentUVE<Scene::UIImageComponentUVE>(entity);
        if (image.alpha <= 0.0F) {
            return WidgetKindUVE::Spacer;
        }
        return image.nineSliceEnabled ? WidgetKindUVE::Border : WidgetKindUVE::Image;
    }
    if (em.HasComponentUVE<Scene::UITextComponentUVE>(entity)) {
        return WidgetKindUVE::Text;
    }
    if (em.HasComponentUVE<Scene::CanvasComponentUVE>(entity)) {
        return WidgetKindUVE::Canvas;
    }
    return std::nullopt;
}

EditorIconsUVE::ImageUVE WidgetIconUVE(const WidgetKindUVE kind, const int size) {
    static std::map<std::pair<int, int>, std::vector<std::uint32_t>> cache;
    const int px = std::clamp(size, 8, 128);
    const auto key = std::make_pair(static_cast<int>(kind), px);
    auto it = cache.find(key);
    if (it == cache.end()) {
        IconPainterUVE painter(px);
        PaintUVE(painter, kind);
        it = cache.emplace(key, painter.PixelsUVE()).first;
    }
    return EditorIconsUVE::ImageUVE{it->second.data(), px};
}

} // namespace UVE::Editor
