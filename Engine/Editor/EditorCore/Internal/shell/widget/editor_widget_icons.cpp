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
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace UVE::Editor {
namespace {

/// A colour with alpha, 0..1.
struct Ink {
    float r = 1.0F, g = 1.0F, b = 1.0F, a = 1.0F;
};

constexpr Ink kWhite{0.93F, 0.95F, 0.98F, 1.0F};
constexpr Ink kDim{0.62F, 0.66F, 0.72F, 1.0F};
constexpr Ink kDark{0.13F, 0.14F, 0.17F, 1.0F};
constexpr Ink kBlue{0.24F, 0.52F, 0.96F, 1.0F};
constexpr Ink kGreen{0.34F, 0.78F, 0.45F, 1.0F};
constexpr Ink kTeal{0.20F, 0.76F, 0.74F, 1.0F};
constexpr Ink kOrange{0.97F, 0.62F, 0.22F, 1.0F};
constexpr Ink kPurple{0.66F, 0.48F, 0.96F, 1.0F};
constexpr Ink kYellow{0.98F, 0.82F, 0.30F, 1.0F};
constexpr Ink kGold{0.82F, 0.66F, 0.40F, 1.0F};

/// A 16-unit design square painted at any size: each shape is a coverage test, sampled 4x4 per
/// pixel, laid over what is there.
class IconPainter final {
public:
    explicit IconPainter(const int size) : m_size(size), m_pixels(static_cast<std::size_t>(size * size), Ink{0, 0, 0, 0}) {}

    void Shape(const std::function<bool(float, float)>& inside, const Ink ink) {
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
                Ink& dst = m_pixels[static_cast<std::size_t>(y * m_size + x)];
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

    void RoundRect(const float x, const float y, const float w, const float h, const float r, const Ink ink) {
        Shape([=](const float u, const float v) { return InRoundRect(u, v, x, y, w, h, r); }, ink);
    }
    /// An outline `t` units thick.
    void Frame(const float x, const float y, const float w, const float h, const float r, const float t, const Ink ink) {
        Shape([=](const float u, const float v) {
            return InRoundRect(u, v, x, y, w, h, r) && !InRoundRect(u, v, x + t, y + t, w - 2.0F * t, h - 2.0F * t, std::max(0.0F, r - t));
        }, ink);
    }
    void Circle(const float cx, const float cy, const float r, const Ink ink) {
        Shape([=](const float u, const float v) { return (u - cx) * (u - cx) + (v - cy) * (v - cy) <= r * r; }, ink);
    }
    void Line(const float x0, const float y0, const float x1, const float y1, const float width, const Ink ink) {
        Shape([=](const float u, const float v) {
            const float dx = x1 - x0;
            const float dy = y1 - y0;
            const float len2 = dx * dx + dy * dy;
            const float t = len2 > 0.0F ? std::clamp(((u - x0) * dx + (v - y0) * dy) / len2, 0.0F, 1.0F) : 0.0F;
            const float px = x0 + t * dx - u;
            const float py = y0 + t * dy - v;
            return px * px + py * py <= width * width * 0.25F;
        }, ink);
    }
    void Triangle(const float ax, const float ay, const float bx, const float by, const float cx, const float cy, const Ink ink) {
        Shape([=](const float u, const float v) {
            const auto side = [&](const float x0, const float y0, const float x1, const float y1) {
                return (x1 - x0) * (v - y0) - (y1 - y0) * (u - x0);
            };
            const float d0 = side(ax, ay, bx, by);
            const float d1 = side(bx, by, cx, cy);
            const float d2 = side(cx, cy, ax, ay);
            return (d0 >= 0.0F && d1 >= 0.0F && d2 >= 0.0F) || (d0 <= 0.0F && d1 <= 0.0F && d2 <= 0.0F);
        }, ink);
    }

    [[nodiscard]] std::vector<std::uint32_t> Pixels() const {
        std::vector<std::uint32_t> out(m_pixels.size());
        for (std::size_t i = 0; i < m_pixels.size(); ++i) {
            const Ink& p = m_pixels[i];
            const auto byte = [](const float f) { return static_cast<std::uint32_t>(std::lround(std::clamp(f, 0.0F, 1.0F) * 255.0F)); };
            out[i] = (byte(p.a) << 24U) | (byte(p.r) << 16U) | (byte(p.g) << 8U) | byte(p.b);
        }
        return out;
    }

private:
    static bool InRoundRect(const float u, const float v, const float x, const float y, const float w, const float h, const float r) {
        if (w <= 0.0F || h <= 0.0F || u < x || v < y || u > x + w || v > y + h) {
            return false;
        }
        const float cx = std::clamp(u, x + r, x + w - r);
        const float cy = std::clamp(v, y + r, y + h - r);
        return (u - cx) * (u - cx) + (v - cy) * (v - cy) <= r * r;
    }

    int m_size = 16;
    std::vector<Ink> m_pixels;
};

void Paint(IconPainter& p, const WidgetKind kind) {
    switch (kind) {
        case WidgetKind::Canvas:
            p.Frame(1.5F, 2.5F, 13.0F, 11.0F, 1.5F, 1.3F, kTeal);
            p.RoundRect(3.5F, 4.5F, 4.0F, 2.0F, 0.6F, kWhite);
            p.RoundRect(3.5F, 8.0F, 6.5F, 3.0F, 0.8F, kBlue);
            break;
        case WidgetKind::Text:
            p.RoundRect(2.5F, 2.5F, 11.0F, 2.6F, 0.6F, kWhite);
            p.RoundRect(6.7F, 2.5F, 2.6F, 11.0F, 0.6F, kWhite);
            p.RoundRect(5.2F, 12.0F, 5.6F, 1.5F, 0.5F, kDim);
            break;
        case WidgetKind::Button:
            p.RoundRect(1.0F, 4.0F, 14.0F, 8.5F, 2.2F, kBlue);
            p.RoundRect(1.0F, 4.0F, 14.0F, 3.6F, 2.2F, Ink{1.0F, 1.0F, 1.0F, 0.18F});
            p.RoundRect(4.5F, 7.6F, 7.0F, 1.6F, 0.8F, kWhite);
            p.Triangle(10.5F, 9.5F, 14.5F, 13.0F, 11.6F, 13.8F, kWhite);
            break;
        case WidgetKind::Image:
            p.Frame(1.5F, 2.5F, 13.0F, 11.0F, 1.5F, 1.2F, kDim);
            p.Triangle(3.0F, 12.0F, 7.0F, 6.5F, 10.5F, 12.0F, kGreen);
            p.Triangle(7.5F, 12.0F, 10.5F, 8.5F, 13.0F, 12.0F, Ink{0.24F, 0.62F, 0.36F, 1.0F});
            p.Circle(11.2F, 5.6F, 1.5F, kYellow);
            break;
        case WidgetKind::ProgressBar:
            p.RoundRect(1.0F, 6.0F, 14.0F, 4.5F, 2.0F, kDark);
            p.Frame(1.0F, 6.0F, 14.0F, 4.5F, 2.0F, 0.8F, kDim);
            p.RoundRect(1.8F, 6.8F, 7.6F, 2.9F, 1.4F, kGreen);
            break;
        case WidgetKind::Slider:
            p.Line(1.5F, 8.0F, 14.5F, 8.0F, 1.6F, kDim);
            p.Line(1.5F, 8.0F, 9.0F, 8.0F, 1.6F, kBlue);
            p.Circle(9.0F, 8.0F, 3.0F, kWhite);
            p.Circle(9.0F, 8.0F, 1.4F, kBlue);
            break;
        case WidgetKind::CheckBox:
            p.RoundRect(2.0F, 2.0F, 12.0F, 12.0F, 2.4F, kGreen);
            p.Line(4.6F, 8.2F, 7.0F, 10.8F, 1.9F, kWhite);
            p.Line(7.0F, 10.8F, 11.6F, 5.0F, 1.9F, kWhite);
            break;
        case WidgetKind::TextBox:
            p.RoundRect(0.8F, 4.0F, 14.4F, 8.0F, 1.6F, kDark);
            p.Frame(0.8F, 4.0F, 14.4F, 8.0F, 1.6F, 1.0F, kBlue);
            p.RoundRect(3.0F, 7.2F, 5.5F, 1.6F, 0.6F, kDim);
            p.Line(10.2F, 5.8F, 10.2F, 10.2F, 1.2F, kWhite);
            break;
        case WidgetKind::ComboBox:
            p.RoundRect(0.8F, 4.0F, 14.4F, 8.0F, 1.6F, kDark);
            p.Frame(0.8F, 4.0F, 14.4F, 8.0F, 1.6F, 1.0F, kPurple);
            p.RoundRect(2.8F, 7.2F, 5.5F, 1.6F, 0.6F, kWhite);
            p.RoundRect(9.8F, 4.6F, 4.8F, 6.8F, 1.2F, kPurple);
            p.Triangle(10.7F, 7.0F, 13.7F, 7.0F, 12.2F, 9.2F, kWhite);
            break;
        case WidgetKind::VerticalBox:
            p.Frame(1.5F, 1.5F, 13.0F, 13.0F, 1.6F, 1.1F, kOrange);
            for (int i = 0; i < 3; ++i) {
                p.RoundRect(3.6F, 3.6F + static_cast<float>(i) * 3.2F, 8.8F, 2.3F, 0.7F, kWhite);
            }
            break;
        case WidgetKind::HorizontalBox:
            p.Frame(1.5F, 1.5F, 13.0F, 13.0F, 1.6F, 1.1F, kOrange);
            for (int i = 0; i < 3; ++i) {
                p.RoundRect(3.6F + static_cast<float>(i) * 3.2F, 3.6F, 2.3F, 8.8F, 0.7F, kWhite);
            }
            break;
        case WidgetKind::WrapBox:
            p.Frame(1.5F, 1.5F, 13.0F, 13.0F, 1.6F, 1.1F, kOrange);
            p.RoundRect(3.6F, 3.6F, 3.8F, 3.8F, 0.7F, kWhite);
            p.RoundRect(8.6F, 3.6F, 3.8F, 3.8F, 0.7F, kWhite);
            p.RoundRect(3.6F, 8.6F, 3.8F, 3.8F, 0.7F, kWhite);
            p.Line(9.2F, 10.5F, 12.0F, 10.5F, 1.0F, kOrange);
            p.Triangle(8.4F, 10.5F, 10.0F, 9.2F, 10.0F, 11.8F, kOrange);
            break;
        case WidgetKind::ScrollBox:
            p.Frame(1.5F, 1.5F, 13.0F, 13.0F, 1.6F, 1.1F, kTeal);
            for (int i = 0; i < 3; ++i) {
                p.RoundRect(3.4F, 3.8F + static_cast<float>(i) * 3.1F, 6.6F, 1.8F, 0.6F, kDim);
            }
            p.RoundRect(11.2F, 3.2F, 2.0F, 9.6F, 1.0F, kDark);
            p.RoundRect(11.2F, 4.0F, 2.0F, 4.0F, 1.0F, kTeal);
            break;
        case WidgetKind::Border:
            p.Frame(1.2F, 1.2F, 13.6F, 13.6F, 3.0F, 2.4F, kGold);
            p.RoundRect(4.6F, 4.6F, 6.8F, 6.8F, 1.0F, Ink{0.82F, 0.66F, 0.40F, 0.35F});
            break;
        case WidgetKind::View3D: {
            // A cube in the round, an orbit around it.
            const Ink top{0.55F, 0.80F, 1.0F, 1.0F};
            const Ink left{0.24F, 0.52F, 0.96F, 1.0F};
            const Ink right{0.15F, 0.34F, 0.70F, 1.0F};
            p.Triangle(8.0F, 2.6F, 12.6F, 5.2F, 8.0F, 7.8F, top);
            p.Triangle(8.0F, 2.6F, 3.4F, 5.2F, 8.0F, 7.8F, top);
            p.Triangle(3.4F, 5.2F, 8.0F, 7.8F, 3.4F, 10.6F, left);
            p.Triangle(8.0F, 7.8F, 8.0F, 13.2F, 3.4F, 10.6F, left);
            p.Triangle(12.6F, 5.2F, 8.0F, 7.8F, 12.6F, 10.6F, right);
            p.Triangle(8.0F, 7.8F, 8.0F, 13.2F, 12.6F, 10.6F, right);
            p.Shape([](const float u, const float v) {
                const float dx = (u - 8.0F) / 7.2F;
                const float dy = (v - 11.0F) / 2.6F;
                const float r = dx * dx + dy * dy;
                return r <= 1.0F && r >= 0.62F && v > 11.0F;
            }, kYellow);
            p.Triangle(14.6F, 10.0F, 15.6F, 12.6F, 12.8F, 12.2F, kYellow);
            break;
        }
        case WidgetKind::Spacer:
            p.Line(2.0F, 4.0F, 2.0F, 12.0F, 1.2F, kDim);
            p.Line(14.0F, 4.0F, 14.0F, 12.0F, 1.2F, kDim);
            p.Line(4.0F, 8.0F, 12.0F, 8.0F, 1.2F, kWhite);
            p.Triangle(3.2F, 8.0F, 5.6F, 6.2F, 5.6F, 9.8F, kWhite);
            p.Triangle(12.8F, 8.0F, 10.4F, 6.2F, 10.4F, 9.8F, kWhite);
            break;
    }
}

} // namespace

const char* WidgetKindLabel(const WidgetKind kind) noexcept {
    switch (kind) {
        case WidgetKind::Canvas: return "Screen";
        case WidgetKind::Text: return "Text";
        case WidgetKind::Button: return "Button";
        case WidgetKind::Image: return "Image";
        case WidgetKind::ProgressBar: return "Progress Bar";
        case WidgetKind::Slider: return "Slider";
        case WidgetKind::CheckBox: return "Check Box";
        case WidgetKind::TextBox: return "Text Box";
        case WidgetKind::ComboBox: return "Combo Box";
        case WidgetKind::VerticalBox: return "Vertical Box";
        case WidgetKind::HorizontalBox: return "Horizontal Box";
        case WidgetKind::WrapBox: return "Wrap Box";
        case WidgetKind::ScrollBox: return "Scroll Box";
        case WidgetKind::Border: return "Border";
        case WidgetKind::Spacer: return "Spacer";
        case WidgetKind::View3D: return "3D View";
    }
    return "Widget";
}

std::optional<WidgetKind> ClassifyWidget(const Scene::IEntityManager& constEm, const Scene::Entity entity) {
    auto& em = const_cast<Scene::IEntityManager&>(constEm);
    if (!em.IsAlive(entity)) {
        return std::nullopt;
    }
    if (em.HasComponent<Scene::UIView3DComponent>(entity)) {
        return WidgetKind::View3D;
    }
    if (em.HasComponent<Scene::UIButtonComponent>(entity)) {
        return WidgetKind::Button;
    }
    if (em.HasComponent<Scene::UIDropdownComponent>(entity)) {
        return WidgetKind::ComboBox;
    }
    if (em.HasComponent<Scene::UIScrollContainerComponent>(entity)) {
        return WidgetKind::ScrollBox;
    }
    if (em.HasComponent<Scene::UILayoutContainerComponent>(entity)) {
        const auto& layout = em.GetComponent<Scene::UILayoutContainerComponent>(entity);
        if (layout.wrapAfter > 0U) {
            return WidgetKind::WrapBox;
        }
        return layout.direction == Scene::UILayoutDirection::Horizontal ? WidgetKind::HorizontalBox : WidgetKind::VerticalBox;
    }
    if (em.HasComponent<Scene::UIProgressBarComponent>(entity)) {
        return WidgetKind::ProgressBar;
    }
    if (em.HasComponent<Scene::UISliderComponent>(entity)) {
        return WidgetKind::Slider;
    }
    if (em.HasComponent<Scene::UICheckboxComponent>(entity)) {
        return WidgetKind::CheckBox;
    }
    if (em.HasComponent<Scene::UITextInputComponent>(entity)) {
        return WidgetKind::TextBox;
    }
    if (em.HasComponent<Scene::UIImageComponent>(entity)) {
        const auto& image = em.GetComponent<Scene::UIImageComponent>(entity);
        if (image.alpha <= 0.0F) {
            return WidgetKind::Spacer;
        }
        return image.nineSliceEnabled ? WidgetKind::Border : WidgetKind::Image;
    }
    if (em.HasComponent<Scene::UITextComponent>(entity)) {
        return WidgetKind::Text;
    }
    if (em.HasComponent<Scene::CanvasComponent>(entity)) {
        return WidgetKind::Canvas;
    }
    return std::nullopt;
}

EditorIcons::Image WidgetIcon(const WidgetKind kind, const int size) {
    static std::map<std::pair<int, int>, std::vector<std::uint32_t>> cache;
    const int px = std::clamp(size, 8, 128);
    const auto key = std::make_pair(static_cast<int>(kind), px);
    auto it = cache.find(key);
    if (it == cache.end()) {
        IconPainter painter(px);
        Paint(painter, kind);
        it = cache.emplace(key, painter.Pixels()).first;
    }
    return EditorIcons::Image{it->second.data(), px};
}

} // namespace UVE::Editor
