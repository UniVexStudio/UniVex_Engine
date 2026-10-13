// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/ui_runtime_uve.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_checkbox_component_uve.h"
#include "uve/component/ui_dropdown_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_progress_bar_component_uve.h"
#include "uve/component/ui_scroll_container_component_uve.h"
#include "uve/component/ui_scrollbar_component_uve.h"
#include "uve/component/ui_slider_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/component/ui_text_input_component_uve.h"
#include "uve/component/ui_tooltip_component_uve.h"
#include "uve/input/key_code_uve.h"
#include "uve/input/mouse_button_uve.h"
#include "uve/ui/canvas_ancestry_uve.h"
#include "uve/ui/ui_anchors_uve.h"
#include "uve/ui/ui_layout_uve.h"
#include "uve/ui/ui_scroll_uve.h"
#include "uve/ui/ui_tween_uve.h"

namespace UVE::UI {

namespace {

struct RankedWidgetUVE final {
    std::int32_t sortOrder = 0;
    bool hasCanvas = false;
    std::uint32_t canvasIndex = 0;
    std::uint32_t canvasGeneration = 0;
    std::uint8_t layer = 0;
    std::uint32_t entityIndex = 0;
    std::uint32_t entityGeneration = 0;
    std::vector<UIQuadUVE> quads;
};

void AppendImageQuadsUVE(const Scene::UIImageComponentUVE& image, const float alpha,
                         std::vector<UIQuadUVE>& quads) {
    const UIDrawItemKindUVE kind = image.textureAssetGuid.value == 0U ? UIDrawItemKindUVE::SolidColor
                                                                      : UIDrawItemKindUVE::Image;
    UIQuadUVE base{};
    base.rect = image.rect;
    base.color = image.tintColor;
    base.alpha = alpha;
    base.kind = kind;
    base.imageAssetGuid = image.textureAssetGuid;
    // Flat fills slice invisibly (one color everywhere), so they always stay one quad; anything
    // non-finite falls back to the plain stretched quad rather than emitting garbage.
    const bool marginsSane =
        std::isfinite(image.sliceMarginMin.x) && std::isfinite(image.sliceMarginMin.y) &&
        std::isfinite(image.sliceMarginMax.x) && std::isfinite(image.sliceMarginMax.y) &&
        std::isfinite(image.sliceUVMin.x) && std::isfinite(image.sliceUVMin.y) &&
        std::isfinite(image.sliceUVMax.x) && std::isfinite(image.sliceUVMax.y);
    if (!image.nineSliceEnabled || kind != UIDrawItemKindUVE::Image || !marginsSane) {
        quads.push_back(base);
        return;
    }
    float marginLeft = std::max(0.0F, image.sliceMarginMin.x);
    float marginTop = std::max(0.0F, image.sliceMarginMin.y);
    float marginRight = std::max(0.0F, image.sliceMarginMax.x);
    float marginBottom = std::max(0.0F, image.sliceMarginMax.y);
    float uvLeft = std::clamp(image.sliceUVMin.x, 0.0F, 1.0F);
    float uvTop = std::clamp(image.sliceUVMin.y, 0.0F, 1.0F);
    float uvRight = std::clamp(image.sliceUVMax.x, 0.0F, 1.0F);
    float uvBottom = std::clamp(image.sliceUVMax.y, 0.0F, 1.0F);
    // Over-wide borders shrink proportionally instead of overlapping: a 120px border pair on a
    // 100px panel becomes 50/50, and the swallowed middle column drops out below.
    const float drawnWide = marginLeft + marginRight;
    if (drawnWide > image.rect.size.x && drawnWide > 0.0F) {
        const float scale = image.rect.size.x / drawnWide;
        marginLeft *= scale;
        marginRight *= scale;
    }
    const float drawnTall = marginTop + marginBottom;
    if (drawnTall > image.rect.size.y && drawnTall > 0.0F) {
        const float scale = image.rect.size.y / drawnTall;
        marginTop *= scale;
        marginBottom *= scale;
    }
    const float uvWide = uvLeft + uvRight;
    if (uvWide > 1.0F) {
        uvLeft /= uvWide;
        uvRight /= uvWide;
    }
    const float uvTall = uvTop + uvBottom;
    if (uvTall > 1.0F) {
        uvTop /= uvTall;
        uvBottom /= uvTall;
    }
    const float x0 = image.rect.position.x;
    const float y0 = image.rect.position.y;
    const float x1 = x0 + image.rect.size.x;
    const float y1 = y0 + image.rect.size.y;
    const float xs[4] = {x0, x0 + marginLeft, x1 - marginRight, x1};
    const float ys[4] = {y0, y0 + marginTop, y1 - marginBottom, y1};
    const float us[4] = {0.0F, uvLeft, 1.0F - uvRight, 1.0F};
    const float vs[4] = {0.0F, uvTop, 1.0F - uvBottom, 1.0F};
    for (int j = 0; j < 3; ++j) {
        for (int i = 0; i < 3; ++i) {
            if (!image.sliceFillCenter && i == 1 && j == 1) {
                continue;
            }
            const float width = xs[i + 1] - xs[i];
            const float height = ys[j + 1] - ys[j];
            if (width <= 0.0F || height <= 0.0F) {
                continue;
            }
            UIQuadUVE cell = base;
            cell.rect = Math::RectUVE{Math::Vector2UVE{xs[i], ys[j]}, Math::Vector2UVE{width, height}};
            cell.u0 = us[i];
            cell.v0 = vs[j];
            cell.u1 = us[i + 1];
            cell.v1 = vs[j + 1];
            quads.push_back(cell);
        }
    }
}

/// The rect a tooltip watches: the entity's first rect widget in button/slider/checkbox/image/
/// progress priority. Text-only entities hover nothing - a tooltip needs a rect to watch.
[[nodiscard]] std::optional<Math::RectUVE> ScrollClipOfUVE(Scene::IEntityManagerUVE& entityManager,
                                                           const Scene::EntityUVE entity) {
    std::optional<Math::RectUVE> clip;
    Scene::EntityUVE current = entity;
    for (std::size_t depth = 0U; depth < kMaximumCanvasAncestorWalkUVE; ++depth) {
        if (!entityManager.IsAliveUVE(current) ||
            !entityManager.HasComponentUVE<Scene::HierarchyComponentUVE>(current)) {
            break;
        }
        const Scene::EntityUVE parent =
            entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(current).parent;
        if (parent == Scene::kInvalidEntityUVE || !entityManager.IsAliveUVE(parent) || parent == current) {
            break;
        }
        if (entityManager.HasComponentUVE<Scene::UIScrollContainerComponentUVE>(parent)) {
            const Scene::UIScrollContainerComponentUVE& scroll =
                entityManager.GetComponentUVE<Scene::UIScrollContainerComponentUVE>(parent);
            if (IsUIScrollContainerComponentValidUVE(scroll)) {
                clip = clip.has_value() ? Math::IntersectionUVE(*clip, scroll.rect) : scroll.rect;
            }
        }
        current = parent;
    }
    return clip;
}

[[nodiscard]] bool ClipQuadUVE(UIQuadUVE& quad, const Math::RectUVE& clip) {
    if (!(quad.rect.size.x > 0.0F) || !(quad.rect.size.y > 0.0F)) {
        return false;
    }
    const Math::RectUVE surviving = Math::IntersectionUVE(quad.rect, clip);
    if (!(surviving.size.x > 0.0F) || !(surviving.size.y > 0.0F)) {
        return false;
    }
    if (quad.kind != UIDrawItemKindUVE::SolidColor) {
        const float oldU0 = quad.u0;
        const float oldV0 = quad.v0;
        const float uSpan = quad.u1 - oldU0;
        const float vSpan = quad.v1 - oldV0;
        quad.u0 = oldU0 + (surviving.position.x - quad.rect.position.x) / quad.rect.size.x * uSpan;
        quad.u1 = oldU0 + (surviving.position.x + surviving.size.x - quad.rect.position.x) / quad.rect.size.x *
                              uSpan;
        quad.v0 = oldV0 + (surviving.position.y - quad.rect.position.y) / quad.rect.size.y * vSpan;
        quad.v1 = oldV0 + (surviving.position.y + surviving.size.y - quad.rect.position.y) / quad.rect.size.y *
                              vSpan;
    }
    quad.rect = surviving;
    return true;
}

[[nodiscard]] bool MouseInScrollAncestorsUVE(Scene::IEntityManagerUVE& entityManager,
                                             const Scene::EntityUVE entity,
                                             const Math::Vector2UVE& mousePosition) {
    const std::optional<Math::RectUVE> clip = ScrollClipOfUVE(entityManager, entity);
    return !clip.has_value() || Math::ContainsUVE(*clip, mousePosition);
}

[[nodiscard]] bool ContainsClippedUVE(Scene::IEntityManagerUVE& entityManager, const Scene::EntityUVE entity,
                                      const Math::RectUVE& rect, const Math::Vector2UVE& mousePosition) {
    return Math::ContainsUVE(rect, mousePosition) &&
           MouseInScrollAncestorsUVE(entityManager, entity, mousePosition);
}

[[nodiscard]] std::optional<Math::RectUVE> TooltipAnchorRectUVE(Scene::IEntityManagerUVE& entityManager,
                                                               const Scene::EntityUVE entity) {
    if (entityManager.HasComponentUVE<Scene::UIButtonComponentUVE>(entity)) {
        return entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(entity).rect;
    }
    if (entityManager.HasComponentUVE<Scene::UISliderComponentUVE>(entity)) {
        return entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(entity).rect;
    }
    if (entityManager.HasComponentUVE<Scene::UICheckboxComponentUVE>(entity)) {
        return entityManager.GetComponentUVE<Scene::UICheckboxComponentUVE>(entity).rect;
    }
    if (entityManager.HasComponentUVE<Scene::UIImageComponentUVE>(entity)) {
        return entityManager.GetComponentUVE<Scene::UIImageComponentUVE>(entity).rect;
    }
    if (entityManager.HasComponentUVE<Scene::UIProgressBarComponentUVE>(entity)) {
        return entityManager.GetComponentUVE<Scene::UIProgressBarComponentUVE>(entity).rect;
    }
    return std::nullopt;
}

/// Splits a dropdown's newline-separated option blob, skipping empty lines - they are never
/// options. Views into the blob, so the caller keeps the component alive while drawing.
[[nodiscard]] std::vector<std::string_view> SplitDropdownOptionsUVE(const std::string_view blob) {
    std::vector<std::string_view> options;
    std::size_t start = 0U;
    while (start <= blob.size()) {
        const std::size_t end = blob.find('\n', start);
        const std::string_view line = blob.substr(start, end == std::string_view::npos ? end : end - start);
        if (!line.empty()) {
            options.push_back(line);
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1U;
    }
    return options;
}

/// The popup rect for an open dropdown: as wide as the box, one optionHeight row per option.
/// Opens below the box, flips above when below would leave the viewport and above fits, and
/// otherwise clamps below into the viewport.
[[nodiscard]] Math::RectUVE DropdownPopupRectUVE(const Scene::UIDropdownComponentUVE& dropdown,
                                                 const std::size_t optionCount,
                                                 const Math::Vector2UVE& viewport) {
    const float height = static_cast<float>(optionCount) * dropdown.optionHeight;
    const float below = dropdown.rect.position.y + dropdown.rect.size.y;
    float y = below;
    if (below + height > viewport.y && dropdown.rect.position.y - height >= 0.0F) {
        y = dropdown.rect.position.y - height;
    } else {
        y = std::min(below, std::max(0.0F, viewport.y - height));
    }
    return Math::RectUVE{Math::Vector2UVE{dropdown.rect.position.x, y},
                         Math::Vector2UVE{dropdown.rect.size.x, height}};
}

/// Maps a pressed key to the character it types, if any. Letters honor shift, digits yield
/// US-layout punctuation under shift, space is space - the rest of KeyCodeUVE (arrows, editing,
/// modifiers, function keys) types nothing and is handled by the caller.
[[nodiscard]] std::optional<char> TextInputCharOfUVE(const Input::KeyCodeUVE key, const bool shift) noexcept {
    const int code = static_cast<int>(key);
    constexpr int kA = static_cast<int>(Input::KeyCodeUVE::A);
    constexpr int kZ = static_cast<int>(Input::KeyCodeUVE::Z);
    if (code >= kA && code <= kZ) {
        return static_cast<char>((shift ? 'A' : 'a') + (code - kA));
    }
    constexpr int kNum0 = static_cast<int>(Input::KeyCodeUVE::Num0);
    constexpr int kNum9 = static_cast<int>(Input::KeyCodeUVE::Num9);
    if (code >= kNum0 && code <= kNum9) {
        if (shift) {
            constexpr char kShiftedDigits[] = {')', '!', '@', '#', '$', '%', '^', '&', '*', '('};
            return kShiftedDigits[static_cast<std::size_t>(code - kNum0)];
        }
        return static_cast<char>('0' + (code - kNum0));
    }
    if (key == Input::KeyCodeUVE::Space) {
        return ' ';
    }
    return std::nullopt;
}

void RankWidgetUVE(const Scene::EntityUVE entity, const CanvasAncestryUVE& ancestry, const std::uint8_t layer,
                   std::vector<UIQuadUVE> quads, std::vector<RankedWidgetUVE>& ranked) {
    RankedWidgetUVE rankedWidget;
    rankedWidget.sortOrder = ancestry.sortOrder;
    rankedWidget.hasCanvas = ancestry.hasCanvas;
    rankedWidget.canvasIndex = ancestry.canvas.index;
    rankedWidget.canvasGeneration = ancestry.canvas.generation;
    rankedWidget.layer = layer;
    rankedWidget.entityIndex = entity.index;
    rankedWidget.entityGeneration = entity.generation;
    rankedWidget.quads = std::move(quads);
    ranked.push_back(std::move(rankedWidget));
}

[[nodiscard]] float ScrollbarMaxOffsetYUVE(const Scene::UIScrollContainerComponentUVE& scroll) noexcept {
    return std::max(0.0F, scroll.contentSize.y - scroll.rect.size.y);
}

[[nodiscard]] float ScrollbarThumbHeightUVE(const Scene::UIScrollContainerComponentUVE& scroll,
                                            const Scene::UIScrollbarComponentUVE& scrollbar) noexcept {
    const float trackHeight = scrollbar.rect.size.y;
    if (scroll.contentSize.y <= scroll.rect.size.y) {
        return trackHeight;
    }
    const float proportional = trackHeight * (scroll.rect.size.y / scroll.contentSize.y);
    return std::clamp(proportional, std::min(scrollbar.minThumbHeight, trackHeight), trackHeight);
}

} // namespace

void UIRuntimeUVE::SetCoordinateTransformUVE(const UICoordinateTransformUVE& transform) noexcept {
    if (!std::isfinite(transform.scaleX) || !std::isfinite(transform.scaleY) || transform.scaleX <= 0.0F ||
        transform.scaleY <= 0.0F || !std::isfinite(transform.offsetX) || !std::isfinite(transform.offsetY) ||
        !std::isfinite(transform.inputScaleX) || !std::isfinite(transform.inputScaleY) ||
        transform.inputScaleX <= 0.0F || transform.inputScaleY <= 0.0F ||
        !std::isfinite(transform.inputOffsetX) || !std::isfinite(transform.inputOffsetY)) {
        m_coordinateTransform = UICoordinateTransformUVE{};
        return;
    }
    m_coordinateTransform = transform;
}

void UIRuntimeUVE::SetViewportSizeUVE(const Math::Vector2UVE& viewportSize) noexcept {
    if (!std::isfinite(viewportSize.x) || !std::isfinite(viewportSize.y) || viewportSize.x < 0.0F ||
        viewportSize.y < 0.0F) {
        return;
    }
    m_viewportSize = viewportSize;
}

void UIRuntimeUVE::SetDeltaTimeUVE(const float deltaTime) noexcept {
    if (!std::isfinite(deltaTime) || deltaTime < 0.0F) {
        return;
    }
    m_deltaTime = deltaTime;
}

void UIRuntimeUVE::TickUVE(Scene::IEntityManagerUVE& entityManager, const Input::IInputSystemUVE& inputSystem,
                           const UITextLocalizationUVE& localization) {
    // Anchors resolve first (roots against the viewport, children against fresh parent rects),
    // then containers auto-size to content and position their children, then active tweens
    // override the resting arrangement: hit-testing and every emitted quad agree on where a
    // widget is within the same tick.
    // Tick-frozen pointer state, hoisted above the layout passes: the scrollbar drag below
    // applies before LayoutUIScrollContainersUVE stacks the children, and input cannot
    // change mid-tick, so every pass reads the same values it always has.
    const Math::Vector2UVE rawMousePosition = inputSystem.GetMousePositionUVE();
    const Math::Vector2UVE mousePosition{
        (rawMousePosition.x * m_coordinateTransform.inputScaleX - m_coordinateTransform.inputOffsetX -
         m_coordinateTransform.offsetX) /
            m_coordinateTransform.scaleX,
        (rawMousePosition.y * m_coordinateTransform.inputScaleY - m_coordinateTransform.inputOffsetY -
         m_coordinateTransform.offsetY) /
            m_coordinateTransform.scaleY};
    const bool mouseDown = inputSystem.IsMouseButtonDownUVE(Input::MouseButtonUVE::Left);
    const bool mousePressedThisFrame = inputSystem.WasMouseButtonPressedThisFrameUVE(Input::MouseButtonUVE::Left);
    ResolveUIAnchorsUVE(entityManager, m_viewportSize);
    LayoutUIContainersUVE(entityManager, m_fontAtlas);
    // Scrollbar drags apply BEFORE the scroll layout stacks the children, so dragged content
    // never lags the thumb by a frame; hover/press/release latch in the widget pass below
    // (post-tween, like every other widget), and a grab takes effect from the next tick. Do not
    // merge the halves: post-layout application would desync thumb and content for a frame on
    // every tick of the drag.
    entityManager.ForEachUVE<Scene::UIScrollbarComponentUVE>(
        [&entityManager, &mousePosition, mouseDown](const Scene::EntityUVE entity,
                                                   Scene::UIScrollbarComponentUVE& scrollbar) {
            scrollbar.wasChangedThisFrame = false;
            if (!scrollbar.isDragging || !mouseDown) {
                return;
            }
            if (!ShouldDrawUiWidgetUVE(entityManager, entity) || !IsUIScrollbarComponentValidUVE(scrollbar) ||
                !entityManager.HasComponentUVE<Scene::UIScrollContainerComponentUVE>(entity)) {
                return;
            }
            Scene::UIScrollContainerComponentUVE& scroll =
                entityManager.GetComponentUVE<Scene::UIScrollContainerComponentUVE>(entity);
            if (!IsUIScrollContainerComponentValidUVE(scroll)) {
                return;
            }
            // The thumb never leaves the track: the pointer maps to the thumb center across the
            // travel and clamps past the ends, exactly like the slider on its vertical axis.
            const float thumbHeight = ScrollbarThumbHeightUVE(scroll, scrollbar);
            const float travel = std::max(0.0F, scrollbar.rect.size.y - thumbHeight);
            const float maxOffset = ScrollbarMaxOffsetYUVE(scroll);
            if (travel <= 0.0F || maxOffset <= 0.0F) {
                return;
            }
            const float pointerFraction =
                (mousePosition.y - scrollbar.rect.position.y - 0.5F * thumbHeight) / travel;
            const float dragged = std::clamp(pointerFraction, 0.0F, 1.0F) * maxOffset;
            if (dragged != scroll.scrollOffset.y) {
                scroll.scrollOffset.y = dragged;
                scrollbar.wasChangedThisFrame = true;
            }
        });
    LayoutUIScrollContainersUVE(entityManager, inputSystem, m_fontAtlas);
    TickUITweensUVE(entityManager, m_deltaTime);
    m_drawBatch.quads.clear();
    std::vector<RankedWidgetUVE> ranked;

    entityManager.ForEachUVE<Scene::UIImageComponentUVE>(
        [&entityManager, &ranked](const Scene::EntityUVE entity, const Scene::UIImageComponentUVE& image) {
            if (!ShouldDrawUiWidgetUVE(entityManager, entity)) {
                return;
            }
            std::vector<UIQuadUVE> quads;
            AppendImageQuadsUVE(image, TweenedAlphaUVE(entityManager, entity, image.alpha), quads);
            RankWidgetUVE(entity, ResolveCanvasAncestryUVE(entityManager, entity), 0, std::move(quads), ranked);
        });

    entityManager.ForEachUVE<Scene::UIProgressBarComponentUVE>(
        [&entityManager, &ranked](const Scene::EntityUVE entity, const Scene::UIProgressBarComponentUVE& bar) {
            if (!ShouldDrawUiWidgetUVE(entityManager, entity) || !IsUIProgressBarComponentValidUVE(bar)) {
                return;
            }
            const float fraction =
                bar.maxValue > bar.minValue
                    ? std::clamp((bar.value - bar.minValue) / (bar.maxValue - bar.minValue), 0.0F, 1.0F)
                    : (bar.value >= bar.maxValue ? 1.0F : 0.0F);
            UIQuadUVE background{};
            background.rect = bar.rect;
            background.color = bar.backgroundColor;
            background.alpha = TweenedAlphaUVE(entityManager, entity, 1.0F);
            background.kind = UIDrawItemKindUVE::SolidColor;
            UIQuadUVE fill{};
            fill.rect = Math::RectUVE{bar.rect.position,
                                      Math::Vector2UVE{bar.rect.size.x * fraction, bar.rect.size.y}};
            fill.color = bar.fillColor;
            fill.alpha = TweenedAlphaUVE(entityManager, entity, 1.0F);
            fill.kind = UIDrawItemKindUVE::SolidColor;
            std::vector<UIQuadUVE> barQuads;
            barQuads.push_back(background);
            barQuads.push_back(fill);
            RankWidgetUVE(entity, ResolveCanvasAncestryUVE(entityManager, entity), 0, std::move(barQuads), ranked);
        });

    entityManager.ForEachUVE<Scene::UIButtonComponentUVE>(
        [&entityManager, &ranked, &mousePosition, mouseDown, mousePressedThisFrame](
            const Scene::EntityUVE entity, Scene::UIButtonComponentUVE& button) {
            if (!ShouldDrawUiWidgetUVE(entityManager, entity)) {
                button.isHovered = false;
                button.wasClickedThisFrame = false;
                return;
            }
            button.isHovered = ContainsClippedUVE(entityManager, entity, button.rect, mousePosition);
            button.wasClickedThisFrame = button.isHovered && mousePressedThisFrame;

            UIQuadUVE quad{};
            quad.rect = button.rect;
            quad.color = button.isHovered ? (mouseDown ? button.pressedColor : button.hoverColor) : button.normalColor;
            quad.alpha = TweenedAlphaUVE(entityManager, entity, 1.0F);
            quad.kind = UIDrawItemKindUVE::SolidColor;
            RankWidgetUVE(entity, ResolveCanvasAncestryUVE(entityManager, entity), 1, {quad}, ranked);
        });

    entityManager.ForEachUVE<Scene::UISliderComponentUVE>(
        [&entityManager, &ranked, &mousePosition, mouseDown, mousePressedThisFrame](
            const Scene::EntityUVE entity, Scene::UISliderComponentUVE& slider) {
            if (!ShouldDrawUiWidgetUVE(entityManager, entity) || !IsUISliderComponentValidUVE(slider)) {
                slider.isHovered = false;
                slider.isDragging = false;
                slider.wasChangedThisFrame = false;
                return;
            }
            slider.isHovered = ContainsClippedUVE(entityManager, entity, slider.rect, mousePosition);
            slider.wasChangedThisFrame = false;
            if (slider.isDragging && !mouseDown) {
                slider.isDragging = false;
            }
            if (mousePressedThisFrame && slider.isHovered) {
                slider.isDragging = true;
            }
            // The thumb never leaves the track: the pointer maps to the thumb center across the
            // travel, clamps past the ends, and snaps to `step` when one is set.
            const float travel = std::max(0.0F, slider.rect.size.x - slider.thumbWidth);
            if (slider.isDragging && travel > 0.0F) {
                const float pointerFraction =
                    (mousePosition.x - slider.rect.position.x - 0.5F * slider.thumbWidth) / travel;
                float dragged = slider.minValue + std::clamp(pointerFraction, 0.0F, 1.0F) *
                                                     (slider.maxValue - slider.minValue);
                if (slider.step > 0.0F) {
                    dragged =
                        slider.minValue + std::round((dragged - slider.minValue) / slider.step) * slider.step;
                    dragged = std::clamp(dragged, slider.minValue, slider.maxValue);
                }
                if (dragged != slider.value) {
                    slider.value = dragged;
                    slider.wasChangedThisFrame = true;
                }
            }
            const float drawnFraction =
                slider.maxValue > slider.minValue
                    ? std::clamp((slider.value - slider.minValue) / (slider.maxValue - slider.minValue), 0.0F,
                                 1.0F)
                    : (slider.value >= slider.maxValue ? 1.0F : 0.0F);
            const float thumbX = slider.rect.position.x + drawnFraction * travel;
            UIQuadUVE track{};
            track.rect = slider.rect;
            track.color = slider.trackColor;
            track.alpha = TweenedAlphaUVE(entityManager, entity, 1.0F);
            track.kind = UIDrawItemKindUVE::SolidColor;
            UIQuadUVE fill{};
            fill.rect = Math::RectUVE{slider.rect.position,
                                      Math::Vector2UVE{thumbX + 0.5F * slider.thumbWidth - slider.rect.position.x,
                                                       slider.rect.size.y}};
            fill.color = slider.fillColor;
            fill.alpha = TweenedAlphaUVE(entityManager, entity, 1.0F);
            fill.kind = UIDrawItemKindUVE::SolidColor;
            UIQuadUVE thumb{};
            thumb.rect = Math::RectUVE{Math::Vector2UVE{thumbX, slider.rect.position.y},
                                       Math::Vector2UVE{slider.thumbWidth, slider.rect.size.y}};
            thumb.color = slider.thumbColor;
            thumb.alpha = TweenedAlphaUVE(entityManager, entity, 1.0F);
            thumb.kind = UIDrawItemKindUVE::SolidColor;
            std::vector<UIQuadUVE> sliderQuads;
            sliderQuads.push_back(track);
            sliderQuads.push_back(fill);
            sliderQuads.push_back(thumb);
            RankWidgetUVE(entity, ResolveCanvasAncestryUVE(entityManager, entity), 1, std::move(sliderQuads), ranked);
        });

    entityManager.ForEachUVE<Scene::UIScrollbarComponentUVE>(
        [&entityManager, &ranked, &mousePosition, mouseDown, mousePressedThisFrame](
            const Scene::EntityUVE entity, Scene::UIScrollbarComponentUVE& scrollbar) {
            if (!ShouldDrawUiWidgetUVE(entityManager, entity) || !IsUIScrollbarComponentValidUVE(scrollbar) ||
                !entityManager.HasComponentUVE<Scene::UIScrollContainerComponentUVE>(entity)) {
                scrollbar.isHovered = false;
                scrollbar.isDragging = false;
                scrollbar.wasChangedThisFrame = false;
                return;
            }
            const Scene::UIScrollContainerComponentUVE& scroll =
                entityManager.GetComponentUVE<Scene::UIScrollContainerComponentUVE>(entity);
            if (!IsUIScrollContainerComponentValidUVE(scroll)) {
                scrollbar.isHovered = false;
                scrollbar.isDragging = false;
                scrollbar.wasChangedThisFrame = false;
                return;
            }
            scrollbar.isHovered =
                ContainsClippedUVE(entityManager, entity, scrollbar.rect, mousePosition);
            if (scrollbar.isDragging && !mouseDown) {
                scrollbar.isDragging = false;
            }
            if (mousePressedThisFrame && scrollbar.isHovered) {
                scrollbar.isDragging = true;
            }
            const float thumbHeight = ScrollbarThumbHeightUVE(scroll, scrollbar);
            const float travel = std::max(0.0F, scrollbar.rect.size.y - thumbHeight);
            const float maxOffset = ScrollbarMaxOffsetYUVE(scroll);
            const float drawnFraction =
                maxOffset > 0.0F ? std::clamp(scroll.scrollOffset.y / maxOffset, 0.0F, 1.0F) : 0.0F;
            const float thumbY = scrollbar.rect.position.y + drawnFraction * travel;
            UIQuadUVE track{};
            track.rect = scrollbar.rect;
            track.color = scrollbar.trackColor;
            track.alpha = TweenedAlphaUVE(entityManager, entity, 1.0F);
            track.kind = UIDrawItemKindUVE::SolidColor;
            UIQuadUVE thumb{};
            thumb.rect = Math::RectUVE{Math::Vector2UVE{scrollbar.rect.position.x, thumbY},
                                       Math::Vector2UVE{scrollbar.rect.size.x, thumbHeight}};
            thumb.color = scrollbar.thumbColor;
            thumb.alpha = TweenedAlphaUVE(entityManager, entity, 1.0F);
            thumb.kind = UIDrawItemKindUVE::SolidColor;
            std::vector<UIQuadUVE> scrollbarQuads;
            scrollbarQuads.push_back(track);
            scrollbarQuads.push_back(thumb);
            RankWidgetUVE(entity, ResolveCanvasAncestryUVE(entityManager, entity), 2,
                          std::move(scrollbarQuads), ranked);
        });

    entityManager.ForEachUVE<Scene::UICheckboxComponentUVE>(
        [&entityManager, &ranked, &mousePosition, mousePressedThisFrame](
            const Scene::EntityUVE entity, Scene::UICheckboxComponentUVE& checkbox) {
            if (!ShouldDrawUiWidgetUVE(entityManager, entity) || !IsUICheckboxComponentValidUVE(checkbox)) {
                checkbox.isHovered = false;
                checkbox.wasToggledThisFrame = false;
                return;
            }
            checkbox.isHovered = ContainsClippedUVE(entityManager, entity, checkbox.rect, mousePosition);
            checkbox.wasToggledThisFrame = checkbox.isHovered && mousePressedThisFrame;
            if (checkbox.wasToggledThisFrame) {
                checkbox.checked = !checkbox.checked;
            }
            const float alpha = TweenedAlphaUVE(entityManager, entity, 1.0F);
            UIQuadUVE box{};
            box.rect = checkbox.rect;
            box.color = checkbox.isHovered ? checkbox.hoverColor : checkbox.boxColor;
            box.alpha = alpha;
            box.kind = UIDrawItemKindUVE::SolidColor;
            std::vector<UIQuadUVE> checkboxQuads;
            checkboxQuads.push_back(box);
            if (checkbox.checked) {
                const Math::Vector2UVE inset{checkbox.rect.size.x * 0.25F, checkbox.rect.size.y * 0.25F};
                UIQuadUVE check{};
                check.rect = Math::RectUVE{checkbox.rect.position + inset, checkbox.rect.size - inset - inset};
                check.color = checkbox.checkColor;
                check.alpha = alpha;
                check.kind = UIDrawItemKindUVE::SolidColor;
                checkboxQuads.push_back(check);
            }
            RankWidgetUVE(entity, ResolveCanvasAncestryUVE(entityManager, entity), 1, std::move(checkboxQuads),
                          ranked);
        });

    std::vector<UIGlyphQuadUVE> glyphQuads;
    std::string translated;
    entityManager.ForEachUVE<Scene::UITextComponentUVE>(
        [this, &entityManager, &ranked, &glyphQuads, &localization, &translated](const Scene::EntityUVE entity,
                                                                                const Scene::UITextComponentUVE& text) {
            if (!ShouldDrawUiWidgetUVE(entityManager, entity)) {
                return;
            }
            glyphQuads.clear();
            const std::string* shown = &text.text;
            if (localization.service != nullptr &&
                (!localization.isAutoTranslated || localization.isAutoTranslated(entity))) {
                translated = localization.service->TranslateUVE(text.text);
                shown = &translated;
            }
            float cursorX = text.positionPixels.x;
            float cursorY = text.positionPixels.y + text.fontSize;
            m_fontAtlas.AppendTextQuadsUVE(*shown, cursorX, cursorY, text.fontSize, glyphQuads);
            std::vector<UIQuadUVE> quads;
            quads.reserve(glyphQuads.size());
            for (const UIGlyphQuadUVE& glyphQuad : glyphQuads) {
                UIQuadUVE quad{};
                quad.rect = Math::RectUVE{Math::Vector2UVE{glyphQuad.x0, glyphQuad.y0},
                                           Math::Vector2UVE{glyphQuad.x1 - glyphQuad.x0, glyphQuad.y1 - glyphQuad.y0}};
                quad.u0 = glyphQuad.u0;
                quad.v0 = glyphQuad.v0;
                quad.u1 = glyphQuad.u1;
                quad.v1 = glyphQuad.v1;
                quad.color = text.color;
                quad.alpha = TweenedAlphaUVE(entityManager, entity, text.alpha);
                quad.kind = UIDrawItemKindUVE::Glyph;
                quads.push_back(quad);
            }
            RankWidgetUVE(entity, ResolveCanvasAncestryUVE(entityManager, entity), 2, std::move(quads), ranked);
        });

    entityManager.ForEachUVE<Scene::UITooltipComponentUVE>(
        [this, &entityManager, &ranked, &mousePosition, &glyphQuads, &localization, &translated](
            const Scene::EntityUVE entity, Scene::UITooltipComponentUVE& tooltip) {
            tooltip.visibleThisFrame = false;
            if (!ShouldDrawUiWidgetUVE(entityManager, entity) || !IsUITooltipComponentValidUVE(tooltip)) {
                tooltip.hoverTime = 0.0F;
                return;
            }
            const std::optional<Math::RectUVE> anchor = TooltipAnchorRectUVE(entityManager, entity);
            if (!anchor.has_value() || !ContainsClippedUVE(entityManager, entity, *anchor, mousePosition)) {
                tooltip.hoverTime = 0.0F;
                return;
            }
            tooltip.hoverTime += m_deltaTime;
            if (tooltip.text.empty() || tooltip.hoverTime < tooltip.delay) {
                return;
            }
            glyphQuads.clear();
            const std::string* shown = &tooltip.text;
            if (localization.service != nullptr &&
                (!localization.isAutoTranslated || localization.isAutoTranslated(entity))) {
                translated = localization.service->TranslateUVE(tooltip.text);
                shown = &translated;
            }
            const float textWidth = m_fontAtlas.MeasureTextWidthUVE(*shown, tooltip.fontSize);
            const Math::Vector2UVE popupSize{textWidth + 2.0F * tooltip.padding,
                                             tooltip.fontSize + 2.0F * tooltip.padding};
            const Math::Vector2UVE desired{mousePosition.x + tooltip.offset.x, mousePosition.y + tooltip.offset.y};
            const Math::Vector2UVE popupPosition{
                std::clamp(desired.x, 0.0F, std::max(0.0F, m_viewportSize.x - popupSize.x)),
                std::clamp(desired.y, 0.0F, std::max(0.0F, m_viewportSize.y - popupSize.y))};
            float cursorX = popupPosition.x + tooltip.padding;
            float cursorY = popupPosition.y + tooltip.padding + tooltip.fontSize;
            m_fontAtlas.AppendTextQuadsUVE(*shown, cursorX, cursorY, tooltip.fontSize, glyphQuads);
            const float alpha = TweenedAlphaUVE(entityManager, entity, 1.0F);
            std::vector<UIQuadUVE> popupQuads;
            UIQuadUVE background{};
            background.rect = Math::RectUVE{popupPosition, popupSize};
            background.color = tooltip.backgroundColor;
            background.alpha = alpha;
            background.kind = UIDrawItemKindUVE::SolidColor;
            popupQuads.push_back(background);
            for (const UIGlyphQuadUVE& glyphQuad : glyphQuads) {
                UIQuadUVE quad{};
                quad.rect = Math::RectUVE{Math::Vector2UVE{glyphQuad.x0, glyphQuad.y0},
                                           Math::Vector2UVE{glyphQuad.x1 - glyphQuad.x0, glyphQuad.y1 - glyphQuad.y0}};
                quad.u0 = glyphQuad.u0;
                quad.v0 = glyphQuad.v0;
                quad.u1 = glyphQuad.u1;
                quad.v1 = glyphQuad.v1;
                quad.color = tooltip.textColor;
                quad.alpha = alpha;
                quad.kind = UIDrawItemKindUVE::Glyph;
                popupQuads.push_back(quad);
            }
            RankWidgetUVE(entity, ResolveCanvasAncestryUVE(entityManager, entity), 2, std::move(popupQuads), ranked);
            tooltip.visibleThisFrame = true;
        });

    entityManager.ForEachUVE<Scene::UIDropdownComponentUVE>(
        [this, &entityManager, &ranked, &mousePosition, mousePressedThisFrame, &glyphQuads, &localization,
         &translated](const Scene::EntityUVE entity, Scene::UIDropdownComponentUVE& dropdown) {
            dropdown.wasSelectionChangedThisFrame = false;
            dropdown.hoveredIndex = -1;
            if (!ShouldDrawUiWidgetUVE(entityManager, entity) || !IsUIDropdownComponentValidUVE(dropdown)) {
                dropdown.isHovered = false;
                dropdown.open = false;
                return;
            }
            const std::vector<std::string_view> options = SplitDropdownOptionsUVE(dropdown.options);
            const Math::RectUVE popup = DropdownPopupRectUVE(dropdown, options.size(), m_viewportSize);
            dropdown.isHovered = ContainsClippedUVE(entityManager, entity, dropdown.rect, mousePosition);
            if (dropdown.open && !options.empty()) {
                const float row = (mousePosition.y - popup.position.y) / dropdown.optionHeight;
                const std::int32_t index = static_cast<std::int32_t>(row);
                if (mousePosition.x >= popup.position.x && mousePosition.x < popup.position.x + popup.size.x &&
                    row >= 0.0F && index < static_cast<std::int32_t>(options.size()) &&
                    MouseInScrollAncestorsUVE(entityManager, entity, mousePosition)) {
                    dropdown.hoveredIndex = index;
                }
            }
            if (mousePressedThisFrame) {
                if (dropdown.isHovered) {
                    dropdown.open = !dropdown.open;
                } else if (dropdown.open && dropdown.hoveredIndex >= 0) {
                    dropdown.selectedIndex = dropdown.hoveredIndex;
                    dropdown.open = false;
                    dropdown.wasSelectionChangedThisFrame = true;
                } else if (dropdown.open) {
                    dropdown.open = false;
                }
            }
            const float alpha = TweenedAlphaUVE(entityManager, entity, 1.0F);
            const CanvasAncestryUVE ancestry = ResolveCanvasAncestryUVE(entityManager, entity);
            UIQuadUVE box{};
            box.rect = dropdown.rect;
            box.color = dropdown.isHovered ? dropdown.boxHoverColor : dropdown.boxColor;
            box.alpha = alpha;
            box.kind = UIDrawItemKindUVE::SolidColor;
            RankWidgetUVE(entity, ancestry, 1, std::vector<UIQuadUVE>{box}, ranked);
            std::vector<UIQuadUVE> textQuads;
            const auto emitLabel = [&](const std::string_view label, const float x, const float baselineY) {
                glyphQuads.clear();
                std::string_view shown = label;
                if (localization.service != nullptr &&
                    (!localization.isAutoTranslated || localization.isAutoTranslated(entity))) {
                    translated = localization.service->TranslateUVE(std::string(label));
                    shown = translated;
                }
                float cursorX = x;
                float cursorY = baselineY;
                m_fontAtlas.AppendTextQuadsUVE(shown, cursorX, cursorY, dropdown.fontSize, glyphQuads);
                for (const UIGlyphQuadUVE& glyphQuad : glyphQuads) {
                    UIQuadUVE quad{};
                    quad.rect =
                        Math::RectUVE{Math::Vector2UVE{glyphQuad.x0, glyphQuad.y0},
                                       Math::Vector2UVE{glyphQuad.x1 - glyphQuad.x0, glyphQuad.y1 - glyphQuad.y0}};
                    quad.u0 = glyphQuad.u0;
                    quad.v0 = glyphQuad.v0;
                    quad.u1 = glyphQuad.u1;
                    quad.v1 = glyphQuad.v1;
                    quad.color = dropdown.textColor;
                    quad.alpha = alpha;
                    quad.kind = UIDrawItemKindUVE::Glyph;
                    textQuads.push_back(quad);
                }
            };
            const std::int32_t selectedShown =
                options.empty() ? -1
                                : std::min(dropdown.selectedIndex,
                                           static_cast<std::int32_t>(options.size()) - 1);
            if (selectedShown >= 0) {
                emitLabel(options[static_cast<std::size_t>(selectedShown)],
                          dropdown.rect.position.x + dropdown.textPadding,
                          dropdown.rect.position.y + dropdown.textPadding + dropdown.fontSize);
            } else {
                emitLabel(dropdown.placeholder, dropdown.rect.position.x + dropdown.textPadding,
                          dropdown.rect.position.y + dropdown.textPadding + dropdown.fontSize);
            }
            if (dropdown.open && !options.empty()) {
                UIQuadUVE background{};
                background.rect = popup;
                background.color = dropdown.popupColor;
                background.alpha = alpha;
                background.kind = UIDrawItemKindUVE::SolidColor;
                textQuads.push_back(background);
                for (std::size_t i = 0U; i < options.size(); ++i) {
                    const std::int32_t row = static_cast<std::int32_t>(i);
                    if (row != dropdown.hoveredIndex && row != selectedShown) {
                        continue;
                    }
                    UIQuadUVE highlight{};
                    highlight.rect = Math::RectUVE{
                        Math::Vector2UVE{popup.position.x, popup.position.y + static_cast<float>(i) * dropdown.optionHeight},
                        Math::Vector2UVE{popup.size.x, dropdown.optionHeight}};
                    highlight.color = row == dropdown.hoveredIndex ? dropdown.optionHoverColor : dropdown.selectedColor;
                    highlight.alpha = alpha;
                    highlight.kind = UIDrawItemKindUVE::SolidColor;
                    textQuads.push_back(highlight);
                }
                for (std::size_t i = 0U; i < options.size(); ++i) {
                    emitLabel(options[i], popup.position.x + dropdown.textPadding,
                              popup.position.y + static_cast<float>(i) * dropdown.optionHeight +
                                  dropdown.textPadding + dropdown.fontSize);
                }
            }
            RankWidgetUVE(entity, ancestry, 2, std::move(textQuads), ranked);
        });

    entityManager.ForEachUVE<Scene::UITextInputComponentUVE>(
        [this, &entityManager, &inputSystem, &ranked, &mousePosition, mousePressedThisFrame, &glyphQuads](
            const Scene::EntityUVE entity, Scene::UITextInputComponentUVE& field) {
            field.wasSubmittedThisFrame = false;
            if (!ShouldDrawUiWidgetUVE(entityManager, entity) || !IsUITextInputComponentValidUVE(field)) {
                field.focused = false;
                field.blinkTime = 0.0F;
                field.scrollOffset = 0.0F;
                return;
            }
            if (mousePressedThisFrame) {
                const bool hovered = ContainsClippedUVE(entityManager, entity, field.rect, mousePosition);
                if (hovered != field.focused) {
                    field.blinkTime = 0.0F;
                }
                field.focused = hovered;
            }
            field.caretIndex = std::clamp(field.caretIndex, 0, static_cast<std::int32_t>(field.text.size()));
            if (field.focused) {
                field.blinkTime += m_deltaTime;
                const bool shift = inputSystem.IsKeyDownUVE(Input::KeyCodeUVE::LeftShift) ||
                                   inputSystem.IsKeyDownUVE(Input::KeyCodeUVE::RightShift);
                bool caretActivity = false;
                for (int code = static_cast<int>(Input::KeyCodeUVE::A);
                     code < static_cast<int>(Input::KeyCodeUVE::Count) && field.focused; ++code) {
                    const Input::KeyCodeUVE key = static_cast<Input::KeyCodeUVE>(code);
                    if (!inputSystem.WasKeyPressedThisFrameUVE(key)) {
                        continue;
                    }
                    if (key == Input::KeyCodeUVE::Backspace) {
                        if (field.caretIndex > 0) {
                            field.text.erase(static_cast<std::size_t>(field.caretIndex) - 1U, 1U);
                            --field.caretIndex;
                            caretActivity = true;
                        }
                    } else if (key == Input::KeyCodeUVE::Left) {
                        field.caretIndex = std::max(0, field.caretIndex - 1);
                        caretActivity = true;
                    } else if (key == Input::KeyCodeUVE::Right) {
                        field.caretIndex =
                            std::min(field.caretIndex + 1, static_cast<std::int32_t>(field.text.size()));
                        caretActivity = true;
                    } else if (key == Input::KeyCodeUVE::Enter) {
                        field.wasSubmittedThisFrame = true;
                    } else if (key == Input::KeyCodeUVE::Escape) {
                        field.focused = false;
                        field.blinkTime = 0.0F;
                    } else if (const std::optional<char> typed = TextInputCharOfUVE(key, shift);
                               typed.has_value()) {
                        if (field.text.size() < static_cast<std::size_t>(field.maxLength)) {
                            field.text.insert(static_cast<std::size_t>(field.caretIndex), 1U, *typed);
                            ++field.caretIndex;
                            caretActivity = true;
                        }
                    }
                }
                if (caretActivity) {
                    field.blinkTime = 0.0F;
                }
                if (field.text.empty()) {
                    field.scrollOffset = 0.0F;
                } else {
                    const float caretPenX =
                        field.textPadding +
                        m_fontAtlas.MeasureTextWidthUVE(
                            std::string_view(field.text).substr(0U, static_cast<std::size_t>(field.caretIndex)),
                            field.fontSize);
                    const float visibleWidth =
                        std::max(0.0F, field.rect.size.x - 2.0F * field.textPadding);
                    if (caretPenX - field.scrollOffset > field.textPadding + visibleWidth) {
                        field.scrollOffset = caretPenX - field.textPadding - visibleWidth;
                    } else if (caretPenX - field.scrollOffset < field.textPadding) {
                        field.scrollOffset = std::max(0.0F, caretPenX - field.textPadding);
                    }
                }
            }
            const float alpha = TweenedAlphaUVE(entityManager, entity, 1.0F);
            const CanvasAncestryUVE ancestry = ResolveCanvasAncestryUVE(entityManager, entity);
            UIQuadUVE box{};
            box.rect = field.rect;
            box.color = field.focused ? field.focusColor : field.boxColor;
            box.alpha = alpha;
            box.kind = UIDrawItemKindUVE::SolidColor;
            RankWidgetUVE(entity, ancestry, 1, std::vector<UIQuadUVE>{box}, ranked);
            std::vector<UIQuadUVE> fieldQuads;
            glyphQuads.clear();
            float cursorX = field.rect.position.x + field.textPadding -
                            (field.text.empty() ? 0.0F : field.scrollOffset);
            float cursorY = field.rect.position.y + field.textPadding + field.fontSize;
            m_fontAtlas.AppendTextQuadsUVE(field.text.empty() ? std::string_view(field.placeholder)
                                                              : std::string_view(field.text),
                                           cursorX, cursorY, field.fontSize, glyphQuads);
            for (const UIGlyphQuadUVE& glyphQuad : glyphQuads) {
                UIQuadUVE quad{};
                quad.rect =
                    Math::RectUVE{Math::Vector2UVE{glyphQuad.x0, glyphQuad.y0},
                                   Math::Vector2UVE{glyphQuad.x1 - glyphQuad.x0, glyphQuad.y1 - glyphQuad.y0}};
                quad.u0 = glyphQuad.u0;
                quad.v0 = glyphQuad.v0;
                quad.u1 = glyphQuad.u1;
                quad.v1 = glyphQuad.v1;
                quad.color = field.text.empty() ? field.placeholderColor : field.textColor;
                quad.alpha = alpha;
                quad.kind = UIDrawItemKindUVE::Glyph;
                fieldQuads.push_back(quad);
            }
            if (field.focused && std::fmod(field.blinkTime, 1.0F) < 0.5F) {
                const float caretPenX =
                    field.textPadding +
                    m_fontAtlas.MeasureTextWidthUVE(
                        std::string_view(field.text).substr(0U, static_cast<std::size_t>(field.caretIndex)),
                        field.fontSize);
                UIQuadUVE caret{};
                caret.rect = Math::RectUVE{
                    Math::Vector2UVE{field.rect.position.x + caretPenX - field.scrollOffset,
                                      field.rect.position.y + field.textPadding},
                    Math::Vector2UVE{2.0F, field.fontSize}};
                caret.color = field.caretColor;
                caret.alpha = alpha;
                caret.kind = UIDrawItemKindUVE::SolidColor;
                fieldQuads.push_back(caret);
            }
            RankWidgetUVE(entity, ancestry, 2, std::move(fieldQuads), ranked);
        });

    std::sort(ranked.begin(), ranked.end(), [](const RankedWidgetUVE& lhs, const RankedWidgetUVE& rhs) {
        if (lhs.sortOrder != rhs.sortOrder) {
            return lhs.sortOrder < rhs.sortOrder;
        }
        if (lhs.hasCanvas != rhs.hasCanvas) {
            return !lhs.hasCanvas && rhs.hasCanvas;
        }
        if (lhs.canvasIndex != rhs.canvasIndex) {
            return lhs.canvasIndex < rhs.canvasIndex;
        }
        if (lhs.canvasGeneration != rhs.canvasGeneration) {
            return lhs.canvasGeneration < rhs.canvasGeneration;
        }
        if (lhs.layer != rhs.layer) {
            return lhs.layer < rhs.layer;
        }
        if (lhs.entityIndex != rhs.entityIndex) {
            return lhs.entityIndex < rhs.entityIndex;
        }
        return lhs.entityGeneration < rhs.entityGeneration;
    });
    for (RankedWidgetUVE& widget : ranked) {
        const Scene::EntityUVE entity{widget.entityIndex, widget.entityGeneration};
        const std::optional<Math::RectUVE> clip = ScrollClipOfUVE(entityManager, entity);
        if (!clip.has_value()) {
            continue;
        }
        widget.quads.erase(std::remove_if(widget.quads.begin(), widget.quads.end(),
                                          [&clip](UIQuadUVE& quad) { return !ClipQuadUVE(quad, *clip); }),
                           widget.quads.end());
    }
    for (const RankedWidgetUVE& widget : ranked) {
        m_drawBatch.quads.insert(m_drawBatch.quads.end(), widget.quads.begin(), widget.quads.end());
    }
    for (UIQuadUVE& quad : m_drawBatch.quads) {
        quad.rect = Math::TransformUVE(quad.rect,
                                       Math::Vector2UVE{m_coordinateTransform.scaleX, m_coordinateTransform.scaleY},
                                       Math::Vector2UVE{m_coordinateTransform.offsetX, m_coordinateTransform.offsetY});
    }
}

} // namespace UVE::UI
