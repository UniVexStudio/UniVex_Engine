// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "disabled_component.h"
#include "hierarchy_component.h"
#include "ui_button_component.h"
#include "ui_checkbox_component.h"
#include "ui_dropdown_component.h"
#include "ui_image_component.h"
#include "ui_progress_bar_component.h"
#include "ui_view_3d_component.h"
#include "ui_scroll_container_component.h"
#include "ui_scrollbar_component.h"
#include "ui_slider_component.h"
#include "ui_text_component.h"
#include "ui_text_input_component.h"
#include "ui_tooltip_component.h"
#include "key_code.h"
#include "mouse_button.h"
#include "ui_button.h"
#include "ui_image.h"
#include "ui_text.h"
#include "canvas_ancestry.h"
#include "ui_anchors.h"
#include "ui_layout.h"
#include "ui_scroll.h"
#include "ui_tween.h"

namespace UVE::UI {

namespace {

struct RankedWidget final {
    std::int32_t sortOrder = 0;
    bool hasCanvas = false;
    std::uint32_t canvasIndex = 0;
    std::uint32_t canvasGeneration = 0;
    std::uint8_t layer = 0;
    std::uint32_t entityIndex = 0;
    std::uint32_t entityGeneration = 0;
    std::vector<UIQuad> quads;
};

struct FocusCandidate final {
    Scene::Entity entity = Scene::kInvalidEntity;
    Math::Rect rect{};
};

void AppendFocusRing(std::vector<UIQuad>& quads, const Math::Rect& rect,
                        const Math::Vector3& color, const float alpha) {
    if (!std::isfinite(rect.position.x) || !std::isfinite(rect.position.y) ||
        !std::isfinite(rect.size.x) || !std::isfinite(rect.size.y) || rect.size.x <= 0.0F ||
        rect.size.y <= 0.0F) {
        return;
    }
    constexpr float kRingThickness = 2.0F;
    const float left = rect.position.x - kRingThickness;
    const float top = rect.position.y - kRingThickness;
    const float width = rect.size.x + 2.0F * kRingThickness;
    const float height = rect.size.y + 2.0F * kRingThickness;
    const Math::Rect edges[] = {
        Math::Rect{Math::Vector2{left, top}, Math::Vector2{width, kRingThickness}},
        Math::Rect{Math::Vector2{left, top + height - kRingThickness},
                      Math::Vector2{width, kRingThickness}},
        Math::Rect{Math::Vector2{left, top}, Math::Vector2{kRingThickness, height}},
        Math::Rect{Math::Vector2{left + width - kRingThickness, top},
                      Math::Vector2{kRingThickness, height}},
    };
    for (const Math::Rect& edge : edges) {
        UIQuad quad{};
        quad.rect = edge;
        quad.color = color;
        quad.alpha = alpha;
        quad.kind = UIDrawItemKind::SolidColor;
        quads.push_back(quad);
    }
}

void AppendImageQuads(const Scene::UIImageComponent& image, const float alpha,
                         std::vector<UIQuad>& quads) {
    const UIDrawItemKind kind = !Scene::UIImage::HasTexture(image)
                                       ? UIDrawItemKind::SolidColor
                                       : UIDrawItemKind::Image;
    UIQuad base{};
    base.rect = image.rect;
    base.color = image.tintColor;
    base.alpha = alpha;
    base.kind = kind;
    base.imageAssetGuid = image.textureAssetGuid;
    // Flat fills slice invisibly (one color everywhere), so they always stay one quad; anything
    // non-finite falls back to the plain stretched quad rather than emitting garbage.
    if (!Scene::UIImage::UsesNineSlice(image)) {
        quads.push_back(base);
        return;
    }
    const Scene::UIImageNineSliceSplits splits =
        Scene::UIImage::ResolveNineSliceSplits(image);
    const float marginLeft = splits.marginLeft;
    const float marginTop = splits.marginTop;
    const float marginRight = splits.marginRight;
    const float marginBottom = splits.marginBottom;
    const float uvLeft = splits.uvLeft;
    const float uvTop = splits.uvTop;
    const float uvRight = splits.uvRight;
    const float uvBottom = splits.uvBottom;
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
            UIQuad cell = base;
            cell.rect = Math::Rect{Math::Vector2{xs[i], ys[j]}, Math::Vector2{width, height}};
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
[[nodiscard]] std::optional<Math::Rect> ScrollClipOf(Scene::IEntityManager& entityManager,
                                                           const Scene::Entity entity) {
    std::optional<Math::Rect> clip;
    Scene::Entity current = entity;
    for (std::size_t depth = 0U; depth < kMaximumCanvasAncestorWalk; ++depth) {
        if (!entityManager.IsAlive(current) ||
            !entityManager.HasComponent<Scene::HierarchyComponent>(current)) {
            break;
        }
        const Scene::Entity parent =
            entityManager.GetComponent<Scene::HierarchyComponent>(current).parent;
        if (parent == Scene::kInvalidEntity || !entityManager.IsAlive(parent) || parent == current) {
            break;
        }
        if (entityManager.HasComponent<Scene::UIScrollContainerComponent>(parent)) {
            const Scene::UIScrollContainerComponent& scroll =
                entityManager.GetComponent<Scene::UIScrollContainerComponent>(parent);
            if (IsUIScrollContainerComponentValid(scroll)) {
                clip = clip.has_value() ? Math::Intersection(*clip, scroll.rect) : scroll.rect;
            }
        }
        current = parent;
    }
    return clip;
}

[[nodiscard]] bool ClipQuad(UIQuad& quad, const Math::Rect& clip) {
    if (!(quad.rect.size.x > 0.0F) || !(quad.rect.size.y > 0.0F)) {
        return false;
    }
    const Math::Rect surviving = Math::Intersection(quad.rect, clip);
    if (!(surviving.size.x > 0.0F) || !(surviving.size.y > 0.0F)) {
        return false;
    }
    if (quad.kind != UIDrawItemKind::SolidColor) {
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

[[nodiscard]] bool MouseInScrollAncestors(Scene::IEntityManager& entityManager,
                                             const Scene::Entity entity,
                                             const Math::Vector2& mousePosition) {
    const std::optional<Math::Rect> clip = ScrollClipOf(entityManager, entity);
    return !clip.has_value() || Math::Contains(*clip, mousePosition);
}

[[nodiscard]] bool ContainsClipped(Scene::IEntityManager& entityManager, const Scene::Entity entity,
                                      const Math::Rect& rect, const Math::Vector2& mousePosition) {
    return Math::Contains(rect, mousePosition) &&
           MouseInScrollAncestors(entityManager, entity, mousePosition);
}

[[nodiscard]] std::optional<Math::Rect> TooltipAnchorRect(Scene::IEntityManager& entityManager,
                                                               const Scene::Entity entity) {
    if (entityManager.HasComponent<Scene::UIButtonComponent>(entity)) {
        return entityManager.GetComponent<Scene::UIButtonComponent>(entity).rect;
    }
    if (entityManager.HasComponent<Scene::UISliderComponent>(entity)) {
        return entityManager.GetComponent<Scene::UISliderComponent>(entity).rect;
    }
    if (entityManager.HasComponent<Scene::UICheckboxComponent>(entity)) {
        return entityManager.GetComponent<Scene::UICheckboxComponent>(entity).rect;
    }
    if (entityManager.HasComponent<Scene::UIImageComponent>(entity)) {
        return entityManager.GetComponent<Scene::UIImageComponent>(entity).rect;
    }
    if (entityManager.HasComponent<Scene::UIProgressBarComponent>(entity)) {
        return entityManager.GetComponent<Scene::UIProgressBarComponent>(entity).rect;
    }
    if (entityManager.HasComponent<Scene::UIView3DComponent>(entity)) {
        return entityManager.GetComponent<Scene::UIView3DComponent>(entity).rect;
    }
    return std::nullopt;
}

/// Splits a dropdown's newline-separated option blob, skipping empty lines - they are never
/// options. Views into the blob, so the caller keeps the component alive while drawing.
[[nodiscard]] std::vector<std::string_view> SplitDropdownOptions(const std::string_view blob) {
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
[[nodiscard]] Math::Rect DropdownPopupRect(const Scene::UIDropdownComponent& dropdown,
                                                 const std::size_t optionCount,
                                                 const Math::Vector2& viewport) {
    const float height = static_cast<float>(optionCount) * dropdown.optionHeight;
    const float below = dropdown.rect.position.y + dropdown.rect.size.y;
    float y = below;
    if (below + height > viewport.y && dropdown.rect.position.y - height >= 0.0F) {
        y = dropdown.rect.position.y - height;
    } else {
        y = std::min(below, std::max(0.0F, viewport.y - height));
    }
    return Math::Rect{Math::Vector2{dropdown.rect.position.x, y},
                         Math::Vector2{dropdown.rect.size.x, height}};
}

/// Maps a pressed key to the character it types, if any. Letters honor shift, digits yield
/// US-layout punctuation under shift, space is space - the rest of KeyCode (arrows, editing,
/// modifiers, function keys) types nothing and is handled by the caller.
[[nodiscard]] std::optional<char> TextInputCharOf(const Input::KeyCode key, const bool shift) noexcept {
    const int code = static_cast<int>(key);
    constexpr int kA = static_cast<int>(Input::KeyCode::A);
    constexpr int kZ = static_cast<int>(Input::KeyCode::Z);
    if (code >= kA && code <= kZ) {
        return static_cast<char>((shift ? 'A' : 'a') + (code - kA));
    }
    constexpr int kNum0 = static_cast<int>(Input::KeyCode::Num0);
    constexpr int kNum9 = static_cast<int>(Input::KeyCode::Num9);
    if (code >= kNum0 && code <= kNum9) {
        if (shift) {
            constexpr char kShiftedDigits[] = {')', '!', '@', '#', '$', '%', '^', '&', '*', '('};
            return kShiftedDigits[static_cast<std::size_t>(code - kNum0)];
        }
        return static_cast<char>('0' + (code - kNum0));
    }
    if (key == Input::KeyCode::Space) {
        return ' ';
    }
    return std::nullopt;
}

void RankWidget(const Scene::Entity entity, const CanvasAncestry& ancestry, const std::uint8_t layer,
                   std::vector<UIQuad> quads, std::vector<RankedWidget>& ranked) {
    RankedWidget rankedWidget;
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

[[nodiscard]] float ScrollbarMaxOffsetY(const Scene::UIScrollContainerComponent& scroll) noexcept {
    return std::max(0.0F, scroll.contentSize.y - scroll.rect.size.y);
}

[[nodiscard]] float ScrollbarThumbHeight(const Scene::UIScrollContainerComponent& scroll,
                                            const Scene::UIScrollbarComponent& scrollbar) noexcept {
    const float trackHeight = scrollbar.rect.size.y;
    if (scroll.contentSize.y <= scroll.rect.size.y) {
        return trackHeight;
    }
    const float proportional = trackHeight * (scroll.rect.size.y / scroll.contentSize.y);
    return std::clamp(proportional, std::min(scrollbar.minThumbHeight, trackHeight), trackHeight);
}

} // namespace

void UIRuntime::SetCoordinateTransform(const UICoordinateTransform& transform) noexcept {
    if (!std::isfinite(transform.scaleX) || !std::isfinite(transform.scaleY) || transform.scaleX <= 0.0F ||
        transform.scaleY <= 0.0F || !std::isfinite(transform.offsetX) || !std::isfinite(transform.offsetY) ||
        !std::isfinite(transform.inputScaleX) || !std::isfinite(transform.inputScaleY) ||
        transform.inputScaleX <= 0.0F || transform.inputScaleY <= 0.0F ||
        !std::isfinite(transform.inputOffsetX) || !std::isfinite(transform.inputOffsetY)) {
        m_coordinateTransform = UICoordinateTransform{};
        return;
    }
    m_coordinateTransform = transform;
}

void UIRuntime::SetViewportSize(const Math::Vector2& viewportSize) noexcept {
    if (!std::isfinite(viewportSize.x) || !std::isfinite(viewportSize.y) || viewportSize.x < 0.0F ||
        viewportSize.y < 0.0F) {
        return;
    }
    m_viewportSize = viewportSize;
}

void UIRuntime::SetDeltaTime(const float deltaTime) noexcept {
    if (!std::isfinite(deltaTime) || deltaTime < 0.0F) {
        return;
    }
    m_deltaTime = deltaTime;
}

void UIRuntime::Tick(Scene::IEntityManager& entityManager, const Input::IInputSystem& inputSystem,
                           const UITextLocalization& localization) {
    // Anchors resolve first (roots against the viewport, children against fresh parent rects),
    // then containers auto-size to content and position their children, then active tweens
    // override the resting arrangement: hit-testing and every emitted quad agree on where a
    // widget is within the same tick.
    // Tick-frozen pointer state, hoisted above the layout passes: the scrollbar drag below
    // applies before LayoutUIScrollContainers stacks the children, and input cannot
    // change mid-tick, so every pass reads the same values it always has.
    const Math::Vector2 rawMousePosition = inputSystem.GetMousePosition();
    const Math::Vector2 mousePosition{
        (rawMousePosition.x * m_coordinateTransform.inputScaleX - m_coordinateTransform.inputOffsetX -
         m_coordinateTransform.offsetX) /
            m_coordinateTransform.scaleX,
        (rawMousePosition.y * m_coordinateTransform.inputScaleY - m_coordinateTransform.inputOffsetY -
         m_coordinateTransform.offsetY) /
            m_coordinateTransform.scaleY};
    const bool mouseDown = inputSystem.IsMouseButtonDown(Input::MouseButton::Left);
    const bool mousePressedThisFrame = inputSystem.WasMouseButtonPressedThisFrame(Input::MouseButton::Left);
    ResolveUIAnchors(entityManager, m_viewportSize);
    LayoutUIContainers(entityManager, m_fontAtlas);
    // Scrollbar drags apply BEFORE the scroll layout stacks the children, so dragged content
    // never lags the thumb by a frame; hover/press/release latch in the widget pass below
    // (post-tween, like every other widget), and a grab takes effect from the next tick. Do not
    // merge the halves: post-layout application would desync thumb and content for a frame on
    // every tick of the drag.
    entityManager.ForEach<Scene::UIScrollbarComponent>(
        [&entityManager, &mousePosition, mouseDown](const Scene::Entity entity,
                                                   Scene::UIScrollbarComponent& scrollbar) {
            scrollbar.wasChangedThisFrame = false;
            if (!scrollbar.isDragging || !mouseDown) {
                return;
            }
            if (!ShouldDrawUiWidget(entityManager, entity) || !IsUIScrollbarComponentValid(scrollbar) ||
                !entityManager.HasComponent<Scene::UIScrollContainerComponent>(entity)) {
                return;
            }
            Scene::UIScrollContainerComponent& scroll =
                entityManager.GetComponent<Scene::UIScrollContainerComponent>(entity);
            if (!IsUIScrollContainerComponentValid(scroll)) {
                return;
            }
            // The thumb never leaves the track: the pointer maps to the thumb center across the
            // travel and clamps past the ends, exactly like the slider on its vertical axis.
            const float thumbHeight = ScrollbarThumbHeight(scroll, scrollbar);
            const float travel = std::max(0.0F, scrollbar.rect.size.y - thumbHeight);
            const float maxOffset = ScrollbarMaxOffsetY(scroll);
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
    LayoutUIScrollContainers(entityManager, inputSystem, m_fontAtlas, mousePosition);
    TickUITweens(entityManager, m_deltaTime);

    // Keyboard focus is resolved after layout/tweens so it uses the same rects as hit-testing.
    // Reading order is deliberately independent of archetype iteration order.
    std::vector<FocusCandidate> focusCandidates;
    const auto addFocusCandidate = [&entityManager, &focusCandidates](
                                       const Scene::Entity entity, const Math::Rect& rect) {
        if (!ShouldDrawUiWidget(entityManager, entity) ||
            entityManager.HasComponent<Scene::DisabledComponent>(entity) ||
            !std::isfinite(rect.position.x) || !std::isfinite(rect.position.y) ||
            !std::isfinite(rect.size.x) || !std::isfinite(rect.size.y) || rect.size.x <= 0.0F ||
            rect.size.y <= 0.0F) {
            return;
        }
        focusCandidates.push_back(FocusCandidate{entity, rect});
    };
    entityManager.ForEach<Scene::UIButtonComponent>(
        [&addFocusCandidate](const Scene::Entity entity, const Scene::UIButtonComponent& button) {
            addFocusCandidate(entity, button.rect);
        });
    entityManager.ForEach<Scene::UISliderComponent>(
        [&addFocusCandidate](const Scene::Entity entity, const Scene::UISliderComponent& slider) {
            if (IsUISliderComponentValid(slider)) {
                addFocusCandidate(entity, slider.rect);
            }
        });
    entityManager.ForEach<Scene::UICheckboxComponent>(
        [&addFocusCandidate](const Scene::Entity entity, const Scene::UICheckboxComponent& checkbox) {
            if (IsUICheckboxComponentValid(checkbox)) {
                addFocusCandidate(entity, checkbox.rect);
            }
        });
    entityManager.ForEach<Scene::UIDropdownComponent>(
        [&addFocusCandidate](const Scene::Entity entity, const Scene::UIDropdownComponent& dropdown) {
            if (IsUIDropdownComponentValid(dropdown)) {
                addFocusCandidate(entity, dropdown.rect);
            }
        });
    entityManager.ForEach<Scene::UITextInputComponent>(
        [&addFocusCandidate](const Scene::Entity entity, const Scene::UITextInputComponent& field) {
            if (IsUITextInputComponentValid(field)) {
                addFocusCandidate(entity, field.rect);
            }
        });
    std::sort(focusCandidates.begin(), focusCandidates.end(),
              [](const FocusCandidate& lhs, const FocusCandidate& rhs) {
                  if (lhs.rect.position.y != rhs.rect.position.y) {
                      return lhs.rect.position.y < rhs.rect.position.y;
                  }
                  if (lhs.rect.position.x != rhs.rect.position.x) {
                      return lhs.rect.position.x < rhs.rect.position.x;
                  }
                  if (lhs.entity.index != rhs.entity.index) {
                      return lhs.entity.index < rhs.entity.index;
                  }
                  return lhs.entity.generation < rhs.entity.generation;
              });
    focusCandidates.erase(std::unique(focusCandidates.begin(), focusCandidates.end(),
                                      [](const FocusCandidate& lhs, const FocusCandidate& rhs) {
                                          return lhs.entity == rhs.entity;
                                      }),
                          focusCandidates.end());
    const auto focusedPosition = std::find_if(
        focusCandidates.begin(), focusCandidates.end(), [this](const FocusCandidate& candidate) {
            return candidate.entity == m_focusedEntity;
        });
    if (focusedPosition == focusCandidates.end()) {
        m_focusedEntity = Scene::kInvalidEntity;
    }
    if (inputSystem.WasKeyPressedThisFrame(Input::KeyCode::Tab) && !focusCandidates.empty()) {
        const bool backwards = inputSystem.IsKeyDown(Input::KeyCode::LeftShift) ||
                               inputSystem.IsKeyDown(Input::KeyCode::RightShift);
        if (m_focusedEntity == Scene::kInvalidEntity) {
            m_focusedEntity = backwards ? focusCandidates.back().entity : focusCandidates.front().entity;
        } else {
            const std::size_t current = static_cast<std::size_t>(focusedPosition - focusCandidates.begin());
            const std::size_t next = backwards
                                         ? (current + focusCandidates.size() - 1U) % focusCandidates.size()
                                         : (current + 1U) % focusCandidates.size();
            m_focusedEntity = focusCandidates[next].entity;
        }
    }
    const Scene::Entity focusedEntity = m_focusedEntity;
    const bool activatePressed = focusedEntity != Scene::kInvalidEntity &&
                                 (inputSystem.WasKeyPressedThisFrame(Input::KeyCode::Enter) ||
                                  inputSystem.WasKeyPressedThisFrame(Input::KeyCode::Space));
    const bool activateDown = focusedEntity != Scene::kInvalidEntity &&
                              (inputSystem.IsKeyDown(Input::KeyCode::Enter) ||
                               inputSystem.IsKeyDown(Input::KeyCode::Space));
    const bool sliderDecreasePressed =
        inputSystem.WasKeyPressedThisFrame(Input::KeyCode::Left) ||
        inputSystem.WasKeyPressedThisFrame(Input::KeyCode::Down);
    const bool sliderIncreasePressed =
        inputSystem.WasKeyPressedThisFrame(Input::KeyCode::Right) ||
        inputSystem.WasKeyPressedThisFrame(Input::KeyCode::Up);
    const bool dropdownPreviousPressed = inputSystem.WasKeyPressedThisFrame(Input::KeyCode::Up);
    const bool dropdownNextPressed = inputSystem.WasKeyPressedThisFrame(Input::KeyCode::Down);
    const bool escapePressed = inputSystem.WasKeyPressedThisFrame(Input::KeyCode::Escape);

    m_drawBatch.quads.clear();
    std::vector<RankedWidget> ranked;

    entityManager.ForEach<Scene::UIImageComponent>(
        [&entityManager, &ranked](const Scene::Entity entity, const Scene::UIImageComponent& image) {
            if (!ShouldDrawUiWidget(entityManager, entity)) {
                return;
            }
            std::vector<UIQuad> quads;
            AppendImageQuads(image, TweenedAlpha(entityManager, entity, image.alpha), quads);
            RankWidget(entity, ResolveCanvasAncestry(entityManager, entity), 0, std::move(quads), ranked);
        });

    entityManager.ForEach<Scene::UIView3DComponent>(
        [&entityManager, &ranked](const Scene::Entity entity, const Scene::UIView3DComponent& view) {
            if (!ShouldDrawUiWidget(entityManager, entity) || !Scene::IsUIView3DComponentValid(view)) {
                return;
            }
            const float alpha = TweenedAlpha(entityManager, entity, 1.0F);
            std::vector<UIQuad> quads;
            if (view.backgroundAlpha > 0.0F) {
                UIQuad background{};
                background.rect = view.rect;
                background.color = view.backgroundColor;
                background.alpha = view.backgroundAlpha * alpha;
                background.kind = UIDrawItemKind::SolidColor;
                quads.push_back(background);
            }
            UIQuad picture{};
            picture.rect = view.rect;
            // The capture is a render target: its rows run bottom-up.
            picture.v0 = 1.0F;
            picture.v1 = 0.0F;
            picture.alpha = alpha;
            picture.kind = UIDrawItemKind::View3D;
            picture.viewKey = (static_cast<std::uint64_t>(entity.generation) << 32U) | entity.index;
            quads.push_back(picture);
            RankWidget(entity, ResolveCanvasAncestry(entityManager, entity), 0, std::move(quads), ranked);
        });

    entityManager.ForEach<Scene::UIProgressBarComponent>(
        [&entityManager, &ranked](const Scene::Entity entity, const Scene::UIProgressBarComponent& bar) {
            if (!ShouldDrawUiWidget(entityManager, entity) || !IsUIProgressBarComponentValid(bar)) {
                return;
            }
            const float fraction =
                bar.maxValue > bar.minValue
                    ? std::clamp((bar.value - bar.minValue) / (bar.maxValue - bar.minValue), 0.0F, 1.0F)
                    : (bar.value >= bar.maxValue ? 1.0F : 0.0F);
            UIQuad background{};
            background.rect = bar.rect;
            background.color = bar.backgroundColor;
            background.alpha = TweenedAlpha(entityManager, entity, 1.0F);
            background.kind = UIDrawItemKind::SolidColor;
            UIQuad fill{};
            fill.rect = Math::Rect{bar.rect.position,
                                      Math::Vector2{bar.rect.size.x * fraction, bar.rect.size.y}};
            fill.color = bar.fillColor;
            fill.alpha = TweenedAlpha(entityManager, entity, 1.0F);
            fill.kind = UIDrawItemKind::SolidColor;
            std::vector<UIQuad> barQuads;
            barQuads.push_back(background);
            barQuads.push_back(fill);
            RankWidget(entity, ResolveCanvasAncestry(entityManager, entity), 0, std::move(barQuads), ranked);
        });

    entityManager.ForEach<Scene::UIButtonComponent>(
        [this, &entityManager, &ranked, &mousePosition, mouseDown, mousePressedThisFrame, focusedEntity,
         activatePressed, activateDown](const Scene::Entity entity, Scene::UIButtonComponent& button) {
            if (!ShouldDrawUiWidget(entityManager, entity)) {
                Scene::UIButton::ClearRuntimeState(button);
                return;
            }
            const bool hovered = ContainsClipped(entityManager, entity, button.rect, mousePosition);
            const bool keyboardFocused = entity == focusedEntity;
            Scene::UIButton::UpdateInteractionState(
                button, hovered, mousePressedThisFrame, keyboardFocused, activatePressed);
            if (button.isHovered && mousePressedThisFrame) {
                m_focusedEntity = entity;
            }

            UIQuad quad{};
            quad.rect = button.rect;
            quad.color = Scene::UIButton::ResolveVisualColor(
                button, keyboardFocused, mouseDown || activateDown);
            quad.alpha = TweenedAlpha(entityManager, entity, 1.0F);
            quad.kind = UIDrawItemKind::SolidColor;
            std::vector<UIQuad> quads{quad};
            if (keyboardFocused) {
                AppendFocusRing(quads, button.rect, button.hoverColor, quad.alpha);
            }
            RankWidget(entity, ResolveCanvasAncestry(entityManager, entity), 1, std::move(quads), ranked);
        });

    entityManager.ForEach<Scene::UISliderComponent>(
        [this, &entityManager, &ranked, &mousePosition, mouseDown, mousePressedThisFrame, focusedEntity,
         sliderDecreasePressed, sliderIncreasePressed](const Scene::Entity entity,
                                                       Scene::UISliderComponent& slider) {
            if (!ShouldDrawUiWidget(entityManager, entity) || !IsUISliderComponentValid(slider)) {
                slider.isHovered = false;
                slider.isDragging = false;
                slider.wasChangedThisFrame = false;
                return;
            }
            slider.isHovered = ContainsClipped(entityManager, entity, slider.rect, mousePosition);
            slider.wasChangedThisFrame = false;
            if (slider.isHovered && mousePressedThisFrame) {
                m_focusedEntity = entity;
            }
            if (entity == focusedEntity && (sliderDecreasePressed || sliderIncreasePressed)) {
                const float increment = slider.step > 0.0F
                                            ? slider.step
                                            : (slider.maxValue - slider.minValue) * 0.01F;
                float adjusted = slider.value + (sliderIncreasePressed ? increment : -increment);
                if (slider.step > 0.0F) {
                    adjusted = slider.minValue +
                               std::round((adjusted - slider.minValue) / slider.step) * slider.step;
                }
                adjusted = std::clamp(adjusted, slider.minValue, slider.maxValue);
                if (adjusted != slider.value) {
                    slider.value = adjusted;
                    slider.wasChangedThisFrame = true;
                }
            }
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
            UIQuad track{};
            track.rect = slider.rect;
            track.color = slider.trackColor;
            track.alpha = TweenedAlpha(entityManager, entity, 1.0F);
            track.kind = UIDrawItemKind::SolidColor;
            UIQuad fill{};
            fill.rect = Math::Rect{slider.rect.position,
                                      Math::Vector2{thumbX + 0.5F * slider.thumbWidth - slider.rect.position.x,
                                                       slider.rect.size.y}};
            fill.color = slider.fillColor;
            fill.alpha = TweenedAlpha(entityManager, entity, 1.0F);
            fill.kind = UIDrawItemKind::SolidColor;
            UIQuad thumb{};
            thumb.rect = Math::Rect{Math::Vector2{thumbX, slider.rect.position.y},
                                       Math::Vector2{slider.thumbWidth, slider.rect.size.y}};
            thumb.color = slider.thumbColor;
            thumb.alpha = TweenedAlpha(entityManager, entity, 1.0F);
            thumb.kind = UIDrawItemKind::SolidColor;
            std::vector<UIQuad> sliderQuads;
            sliderQuads.push_back(track);
            sliderQuads.push_back(fill);
            sliderQuads.push_back(thumb);
            if (entity == focusedEntity) {
                AppendFocusRing(sliderQuads, slider.rect, slider.thumbColor, track.alpha);
            }
            RankWidget(entity, ResolveCanvasAncestry(entityManager, entity), 1, std::move(sliderQuads), ranked);
        });

    entityManager.ForEach<Scene::UIScrollbarComponent>(
        [&entityManager, &ranked, &mousePosition, mouseDown, mousePressedThisFrame](
            const Scene::Entity entity, Scene::UIScrollbarComponent& scrollbar) {
            if (!ShouldDrawUiWidget(entityManager, entity) || !IsUIScrollbarComponentValid(scrollbar) ||
                !entityManager.HasComponent<Scene::UIScrollContainerComponent>(entity)) {
                scrollbar.isHovered = false;
                scrollbar.isDragging = false;
                scrollbar.wasChangedThisFrame = false;
                return;
            }
            const Scene::UIScrollContainerComponent& scroll =
                entityManager.GetComponent<Scene::UIScrollContainerComponent>(entity);
            if (!IsUIScrollContainerComponentValid(scroll)) {
                scrollbar.isHovered = false;
                scrollbar.isDragging = false;
                scrollbar.wasChangedThisFrame = false;
                return;
            }
            scrollbar.isHovered =
                ContainsClipped(entityManager, entity, scrollbar.rect, mousePosition);
            if (scrollbar.isDragging && !mouseDown) {
                scrollbar.isDragging = false;
            }
            if (mousePressedThisFrame && scrollbar.isHovered) {
                scrollbar.isDragging = true;
            }
            const float thumbHeight = ScrollbarThumbHeight(scroll, scrollbar);
            const float travel = std::max(0.0F, scrollbar.rect.size.y - thumbHeight);
            const float maxOffset = ScrollbarMaxOffsetY(scroll);
            const float drawnFraction =
                maxOffset > 0.0F ? std::clamp(scroll.scrollOffset.y / maxOffset, 0.0F, 1.0F) : 0.0F;
            const float thumbY = scrollbar.rect.position.y + drawnFraction * travel;
            UIQuad track{};
            track.rect = scrollbar.rect;
            track.color = scrollbar.trackColor;
            track.alpha = TweenedAlpha(entityManager, entity, 1.0F);
            track.kind = UIDrawItemKind::SolidColor;
            UIQuad thumb{};
            thumb.rect = Math::Rect{Math::Vector2{scrollbar.rect.position.x, thumbY},
                                       Math::Vector2{scrollbar.rect.size.x, thumbHeight}};
            thumb.color = scrollbar.thumbColor;
            thumb.alpha = TweenedAlpha(entityManager, entity, 1.0F);
            thumb.kind = UIDrawItemKind::SolidColor;
            std::vector<UIQuad> scrollbarQuads;
            scrollbarQuads.push_back(track);
            scrollbarQuads.push_back(thumb);
            RankWidget(entity, ResolveCanvasAncestry(entityManager, entity), 2,
                          std::move(scrollbarQuads), ranked);
        });

    entityManager.ForEach<Scene::UICheckboxComponent>(
        [this, &entityManager, &ranked, &mousePosition, mousePressedThisFrame, focusedEntity, activatePressed](
            const Scene::Entity entity, Scene::UICheckboxComponent& checkbox) {
            if (!ShouldDrawUiWidget(entityManager, entity) || !IsUICheckboxComponentValid(checkbox)) {
                checkbox.isHovered = false;
                checkbox.wasToggledThisFrame = false;
                return;
            }
            checkbox.isHovered = ContainsClipped(entityManager, entity, checkbox.rect, mousePosition);
            checkbox.wasToggledThisFrame =
                (checkbox.isHovered && mousePressedThisFrame) || (entity == focusedEntity && activatePressed);
            if (checkbox.isHovered && mousePressedThisFrame) {
                m_focusedEntity = entity;
            }
            if (checkbox.wasToggledThisFrame) {
                checkbox.checked = !checkbox.checked;
            }
            const float alpha = TweenedAlpha(entityManager, entity, 1.0F);
            UIQuad box{};
            box.rect = checkbox.rect;
            box.color = checkbox.isHovered ? checkbox.hoverColor : checkbox.boxColor;
            box.alpha = alpha;
            box.kind = UIDrawItemKind::SolidColor;
            std::vector<UIQuad> checkboxQuads;
            checkboxQuads.push_back(box);
            if (checkbox.checked) {
                const Math::Vector2 inset{checkbox.rect.size.x * 0.25F, checkbox.rect.size.y * 0.25F};
                UIQuad check{};
                check.rect = Math::Rect{checkbox.rect.position + inset, checkbox.rect.size - inset - inset};
                check.color = checkbox.checkColor;
                check.alpha = alpha;
                check.kind = UIDrawItemKind::SolidColor;
                checkboxQuads.push_back(check);
            }
            if (entity == focusedEntity) {
                AppendFocusRing(checkboxQuads, checkbox.rect, checkbox.hoverColor, alpha);
            }
            RankWidget(entity, ResolveCanvasAncestry(entityManager, entity), 1, std::move(checkboxQuads),
                          ranked);
        });

    std::vector<UIGlyphQuad> glyphQuads;
    std::string translated;
    entityManager.ForEach<Scene::UITextComponent>(
        [this, &entityManager, &ranked, &glyphQuads, &localization, &translated](const Scene::Entity entity,
                                                                                const Scene::UITextComponent& text) {
            if (!ShouldDrawUiWidget(entityManager, entity)) {
                return;
            }
            glyphQuads.clear();
            const std::string* shown = &text.text;
            if (localization.service != nullptr &&
                (!localization.isAutoTranslated || localization.isAutoTranslated(entity))) {
                translated = localization.service->Translate(text.text);
                shown = &translated;
            }
            // The box: the parent widget's rect (Fill Parent), else the text's own Box Size.
            Math::Rect box{text.positionPixels, text.boxSize};
            if (text.fillParent && entityManager.HasComponent<Scene::HierarchyComponent>(entity)) {
                const Scene::Entity parent = entityManager.GetComponent<Scene::HierarchyComponent>(entity).parent;
                if (parent != Scene::kInvalidEntity && entityManager.IsAlive(parent)) {
                    if (const std::optional<Math::Rect> parentRect = TooltipAnchorRect(entityManager, parent)) {
                        box = *parentRect;
                    }
                }
            }
            const std::vector<Scene::UIText::Line> lines = Scene::UIText::LayoutLines(
                text, *shown, box, [this, &text](const std::string_view line) { return m_fontAtlas.MeasureTextWidth(line, text.fontSize); });
            const float alpha = TweenedAlpha(entityManager, entity, text.alpha);
            std::vector<UIQuad> quads;
            // Shadow, then outline (the text drawn around itself), then the text: one glyph pass each.
            const auto emit = [&](const Math::Vector2 offset, const Math::Vector3& colour, const float quadAlpha) {
                if (quadAlpha <= 0.0F) {
                    return;
                }
                for (const Scene::UIText::Line& line : lines) {
                    glyphQuads.clear();
                    float cursorX = line.baseline.x + offset.x;
                    float cursorY = line.baseline.y + offset.y;
                    m_fontAtlas.AppendTextQuads(line.text, cursorX, cursorY, text.fontSize, glyphQuads);
                    for (const UIGlyphQuad& glyphQuad : glyphQuads) {
                        UIQuad quad{};
                        quad.rect = Math::Rect{Math::Vector2{glyphQuad.x0, glyphQuad.y0},
                                                   Math::Vector2{glyphQuad.x1 - glyphQuad.x0, glyphQuad.y1 - glyphQuad.y0}};
                        quad.u0 = glyphQuad.u0;
                        quad.v0 = glyphQuad.v0;
                        quad.u1 = glyphQuad.u1;
                        quad.v1 = glyphQuad.v1;
                        quad.color = colour;
                        quad.alpha = quadAlpha;
                        quad.kind = UIDrawItemKind::Glyph;
                        quads.push_back(quad);
                    }
                }
            };
            emit(text.shadowOffset, text.shadowColor, alpha * text.shadowAlpha);
            if (text.outlineWidth > 0.0F) {
                const float w = text.outlineWidth;
                for (const Math::Vector2 offset : {Math::Vector2{-w, 0.0F}, Math::Vector2{w, 0.0F}, Math::Vector2{0.0F, -w},
                                                      Math::Vector2{0.0F, w}, Math::Vector2{-w, -w}, Math::Vector2{w, -w},
                                                      Math::Vector2{-w, w}, Math::Vector2{w, w}}) {
                    emit(offset, text.outlineColor, alpha);
                }
            }
            emit(Math::Vector2{}, text.color, alpha);
            RankWidget(entity, ResolveCanvasAncestry(entityManager, entity), 2, std::move(quads), ranked);
        });

    entityManager.ForEach<Scene::UITooltipComponent>(
        [this, &entityManager, &ranked, &mousePosition, &glyphQuads, &localization, &translated](
            const Scene::Entity entity, Scene::UITooltipComponent& tooltip) {
            tooltip.visibleThisFrame = false;
            if (!ShouldDrawUiWidget(entityManager, entity) || !IsUITooltipComponentValid(tooltip)) {
                tooltip.hoverTime = 0.0F;
                return;
            }
            const std::optional<Math::Rect> anchor = TooltipAnchorRect(entityManager, entity);
            if (!anchor.has_value() || !ContainsClipped(entityManager, entity, *anchor, mousePosition)) {
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
                translated = localization.service->Translate(tooltip.text);
                shown = &translated;
            }
            const float textWidth = m_fontAtlas.MeasureTextWidth(*shown, tooltip.fontSize);
            const Math::Vector2 popupSize{textWidth + 2.0F * tooltip.padding,
                                             tooltip.fontSize + 2.0F * tooltip.padding};
            const Math::Vector2 desired{mousePosition.x + tooltip.offset.x, mousePosition.y + tooltip.offset.y};
            const Math::Vector2 popupPosition{
                std::clamp(desired.x, 0.0F, std::max(0.0F, m_viewportSize.x - popupSize.x)),
                std::clamp(desired.y, 0.0F, std::max(0.0F, m_viewportSize.y - popupSize.y))};
            float cursorX = popupPosition.x + tooltip.padding;
            float cursorY = popupPosition.y + tooltip.padding + tooltip.fontSize;
            m_fontAtlas.AppendTextQuads(*shown, cursorX, cursorY, tooltip.fontSize, glyphQuads);
            const float alpha = TweenedAlpha(entityManager, entity, 1.0F);
            std::vector<UIQuad> popupQuads;
            UIQuad background{};
            background.rect = Math::Rect{popupPosition, popupSize};
            background.color = tooltip.backgroundColor;
            background.alpha = alpha;
            background.kind = UIDrawItemKind::SolidColor;
            popupQuads.push_back(background);
            for (const UIGlyphQuad& glyphQuad : glyphQuads) {
                UIQuad quad{};
                quad.rect = Math::Rect{Math::Vector2{glyphQuad.x0, glyphQuad.y0},
                                           Math::Vector2{glyphQuad.x1 - glyphQuad.x0, glyphQuad.y1 - glyphQuad.y0}};
                quad.u0 = glyphQuad.u0;
                quad.v0 = glyphQuad.v0;
                quad.u1 = glyphQuad.u1;
                quad.v1 = glyphQuad.v1;
                quad.color = tooltip.textColor;
                quad.alpha = alpha;
                quad.kind = UIDrawItemKind::Glyph;
                popupQuads.push_back(quad);
            }
            RankWidget(entity, ResolveCanvasAncestry(entityManager, entity), 2, std::move(popupQuads), ranked);
            tooltip.visibleThisFrame = true;
        });

    entityManager.ForEach<Scene::UIDropdownComponent>(
        [this, &entityManager, &ranked, &mousePosition, mousePressedThisFrame, &glyphQuads, &localization,
         &translated, focusedEntity, activatePressed, dropdownPreviousPressed, dropdownNextPressed,
         escapePressed](const Scene::Entity entity, Scene::UIDropdownComponent& dropdown) {
            dropdown.wasSelectionChangedThisFrame = false;
            dropdown.hoveredIndex = -1;
            if (!ShouldDrawUiWidget(entityManager, entity) || !IsUIDropdownComponentValid(dropdown)) {
                dropdown.isHovered = false;
                dropdown.open = false;
                return;
            }
            const std::vector<std::string_view> options = SplitDropdownOptions(dropdown.options);
            const Math::Rect popup = DropdownPopupRect(dropdown, options.size(), m_viewportSize);
            dropdown.isHovered = ContainsClipped(entityManager, entity, dropdown.rect, mousePosition);
            if (dropdown.open && !options.empty()) {
                const float row = (mousePosition.y - popup.position.y) / dropdown.optionHeight;
                const std::int32_t index = static_cast<std::int32_t>(row);
                if (mousePosition.x >= popup.position.x && mousePosition.x < popup.position.x + popup.size.x &&
                    row >= 0.0F && index < static_cast<std::int32_t>(options.size()) &&
                    MouseInScrollAncestors(entityManager, entity, mousePosition)) {
                    dropdown.hoveredIndex = index;
                }
            }
            const bool keyboardFocused = entity == focusedEntity;
            if (keyboardFocused && m_dropdownKeyboardEntity != entity) {
                m_dropdownKeyboardEntity = entity;
                m_dropdownKeyboardIndex = -1;
            }
            if (keyboardFocused && dropdown.open && !options.empty() &&
                (dropdownPreviousPressed || dropdownNextPressed || activatePressed || escapePressed)) {
                std::int32_t keyboardIndex = m_dropdownKeyboardIndex >= 0
                                                 ? m_dropdownKeyboardIndex
                                                 : std::clamp(dropdown.selectedIndex, 0,
                                                              static_cast<std::int32_t>(options.size()) - 1);
                if (dropdownPreviousPressed) {
                    keyboardIndex = std::max(0, keyboardIndex - 1);
                }
                if (dropdownNextPressed) {
                    keyboardIndex =
                        std::min(static_cast<std::int32_t>(options.size()) - 1, keyboardIndex + 1);
                }
                m_dropdownKeyboardIndex = keyboardIndex;
                dropdown.hoveredIndex = keyboardIndex;
                if (activatePressed) {
                    dropdown.selectedIndex = keyboardIndex;
                    dropdown.open = false;
                    dropdown.wasSelectionChangedThisFrame = true;
                    m_dropdownKeyboardIndex = -1;
                } else if (escapePressed) {
                    dropdown.open = false;
                    m_dropdownKeyboardIndex = -1;
                }
            } else if (keyboardFocused && activatePressed && !options.empty()) {
                dropdown.open = true;
                m_dropdownKeyboardIndex =
                    std::clamp(dropdown.selectedIndex, 0, static_cast<std::int32_t>(options.size()) - 1);
                dropdown.hoveredIndex = m_dropdownKeyboardIndex;
            }
            if (mousePressedThisFrame) {
                if (dropdown.isHovered) {
                    m_focusedEntity = entity;
                    dropdown.open = !dropdown.open;
                } else if (dropdown.open && dropdown.hoveredIndex >= 0) {
                    dropdown.selectedIndex = dropdown.hoveredIndex;
                    dropdown.open = false;
                    dropdown.wasSelectionChangedThisFrame = true;
                } else if (dropdown.open) {
                    dropdown.open = false;
                }
            }
            const float alpha = TweenedAlpha(entityManager, entity, 1.0F);
            const CanvasAncestry ancestry = ResolveCanvasAncestry(entityManager, entity);
            UIQuad box{};
            box.rect = dropdown.rect;
            box.color = dropdown.isHovered || keyboardFocused ? dropdown.boxHoverColor : dropdown.boxColor;
            box.alpha = alpha;
            box.kind = UIDrawItemKind::SolidColor;
            RankWidget(entity, ancestry, 1, std::vector<UIQuad>{box}, ranked);
            std::vector<UIQuad> textQuads;
            const auto emitLabel = [&](const std::string_view label, const float x, const float baselineY) {
                glyphQuads.clear();
                std::string_view shown = label;
                if (localization.service != nullptr &&
                    (!localization.isAutoTranslated || localization.isAutoTranslated(entity))) {
                    translated = localization.service->Translate(std::string(label));
                    shown = translated;
                }
                float cursorX = x;
                float cursorY = baselineY;
                m_fontAtlas.AppendTextQuads(shown, cursorX, cursorY, dropdown.fontSize, glyphQuads);
                for (const UIGlyphQuad& glyphQuad : glyphQuads) {
                    UIQuad quad{};
                    quad.rect =
                        Math::Rect{Math::Vector2{glyphQuad.x0, glyphQuad.y0},
                                       Math::Vector2{glyphQuad.x1 - glyphQuad.x0, glyphQuad.y1 - glyphQuad.y0}};
                    quad.u0 = glyphQuad.u0;
                    quad.v0 = glyphQuad.v0;
                    quad.u1 = glyphQuad.u1;
                    quad.v1 = glyphQuad.v1;
                    quad.color = dropdown.textColor;
                    quad.alpha = alpha;
                    quad.kind = UIDrawItemKind::Glyph;
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
                UIQuad background{};
                background.rect = popup;
                background.color = dropdown.popupColor;
                background.alpha = alpha;
                background.kind = UIDrawItemKind::SolidColor;
                textQuads.push_back(background);
                for (std::size_t i = 0U; i < options.size(); ++i) {
                    const std::int32_t row = static_cast<std::int32_t>(i);
                    if (row != dropdown.hoveredIndex && row != selectedShown) {
                        continue;
                    }
                    UIQuad highlight{};
                    highlight.rect = Math::Rect{
                        Math::Vector2{popup.position.x, popup.position.y + static_cast<float>(i) * dropdown.optionHeight},
                        Math::Vector2{popup.size.x, dropdown.optionHeight}};
                    highlight.color = row == dropdown.hoveredIndex ? dropdown.optionHoverColor : dropdown.selectedColor;
                    highlight.alpha = alpha;
                    highlight.kind = UIDrawItemKind::SolidColor;
                    textQuads.push_back(highlight);
                }
                for (std::size_t i = 0U; i < options.size(); ++i) {
                    emitLabel(options[i], popup.position.x + dropdown.textPadding,
                              popup.position.y + static_cast<float>(i) * dropdown.optionHeight +
                                  dropdown.textPadding + dropdown.fontSize);
                }
            }
            RankWidget(entity, ancestry, 2, std::move(textQuads), ranked);
        });

    entityManager.ForEach<Scene::UITextInputComponent>(
        [this, &entityManager, &inputSystem, &ranked, &mousePosition, mousePressedThisFrame, &glyphQuads,
         focusedEntity](const Scene::Entity entity, Scene::UITextInputComponent& field) {
            field.wasSubmittedThisFrame = false;
            if (!ShouldDrawUiWidget(entityManager, entity) || !IsUITextInputComponentValid(field)) {
                field.focused = false;
                field.blinkTime = 0.0F;
                field.scrollOffset = 0.0F;
                return;
            }
            if (entity == focusedEntity) {
                field.focused = true;
            } else if (focusedEntity != Scene::kInvalidEntity) {
                field.focused = false;
            }
            if (mousePressedThisFrame) {
                const bool hovered = ContainsClipped(entityManager, entity, field.rect, mousePosition);
                if (hovered != field.focused) {
                    field.blinkTime = 0.0F;
                }
                field.focused = hovered;
            }
            field.caretIndex = std::clamp(field.caretIndex, 0, static_cast<std::int32_t>(field.text.size()));
            if (field.focused) {
                field.blinkTime += m_deltaTime;
                const bool shift = inputSystem.IsKeyDown(Input::KeyCode::LeftShift) ||
                                   inputSystem.IsKeyDown(Input::KeyCode::RightShift);
                bool caretActivity = false;
                for (int code = static_cast<int>(Input::KeyCode::A);
                     code < static_cast<int>(Input::KeyCode::Count) && field.focused; ++code) {
                    const Input::KeyCode key = static_cast<Input::KeyCode>(code);
                    if (!inputSystem.WasKeyPressedThisFrame(key)) {
                        continue;
                    }
                    if (key == Input::KeyCode::Backspace) {
                        if (field.caretIndex > 0) {
                            field.text.erase(static_cast<std::size_t>(field.caretIndex) - 1U, 1U);
                            --field.caretIndex;
                            caretActivity = true;
                        }
                    } else if (key == Input::KeyCode::Left) {
                        field.caretIndex = std::max(0, field.caretIndex - 1);
                        caretActivity = true;
                    } else if (key == Input::KeyCode::Right) {
                        field.caretIndex =
                            std::min(field.caretIndex + 1, static_cast<std::int32_t>(field.text.size()));
                        caretActivity = true;
                    } else if (key == Input::KeyCode::Enter) {
                        field.wasSubmittedThisFrame = true;
                    } else if (key == Input::KeyCode::Escape) {
                        field.focused = false;
                        field.blinkTime = 0.0F;
                    } else if (const std::optional<char> typed = TextInputCharOf(key, shift);
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
                        m_fontAtlas.MeasureTextWidth(
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
            const float alpha = TweenedAlpha(entityManager, entity, 1.0F);
            const CanvasAncestry ancestry = ResolveCanvasAncestry(entityManager, entity);
            UIQuad box{};
            box.rect = field.rect;
            box.color = field.focused ? field.focusColor : field.boxColor;
            box.alpha = alpha;
            box.kind = UIDrawItemKind::SolidColor;
            RankWidget(entity, ancestry, 1, std::vector<UIQuad>{box}, ranked);
            std::vector<UIQuad> fieldQuads;
            glyphQuads.clear();
            float cursorX = field.rect.position.x + field.textPadding -
                            (field.text.empty() ? 0.0F : field.scrollOffset);
            float cursorY = field.rect.position.y + field.textPadding + field.fontSize;
            m_fontAtlas.AppendTextQuads(field.text.empty() ? std::string_view(field.placeholder)
                                                              : std::string_view(field.text),
                                           cursorX, cursorY, field.fontSize, glyphQuads);
            for (const UIGlyphQuad& glyphQuad : glyphQuads) {
                UIQuad quad{};
                quad.rect =
                    Math::Rect{Math::Vector2{glyphQuad.x0, glyphQuad.y0},
                                   Math::Vector2{glyphQuad.x1 - glyphQuad.x0, glyphQuad.y1 - glyphQuad.y0}};
                quad.u0 = glyphQuad.u0;
                quad.v0 = glyphQuad.v0;
                quad.u1 = glyphQuad.u1;
                quad.v1 = glyphQuad.v1;
                quad.color = field.text.empty() ? field.placeholderColor : field.textColor;
                quad.alpha = alpha;
                quad.kind = UIDrawItemKind::Glyph;
                fieldQuads.push_back(quad);
            }
            if (field.focused && std::fmod(field.blinkTime, 1.0F) < 0.5F) {
                const float caretPenX =
                    field.textPadding +
                    m_fontAtlas.MeasureTextWidth(
                        std::string_view(field.text).substr(0U, static_cast<std::size_t>(field.caretIndex)),
                        field.fontSize);
                UIQuad caret{};
                caret.rect = Math::Rect{
                    Math::Vector2{field.rect.position.x + caretPenX - field.scrollOffset,
                                      field.rect.position.y + field.textPadding},
                    Math::Vector2{2.0F, field.fontSize}};
                caret.color = field.caretColor;
                caret.alpha = alpha;
                caret.kind = UIDrawItemKind::SolidColor;
                fieldQuads.push_back(caret);
            }
            RankWidget(entity, ancestry, 2, std::move(fieldQuads), ranked);
        });

    // Mouse focus and Escape live in the text-field pass; adopt or release keyboard focus here
    // so the next Tab starts from the widget the user is actually editing.
    bool textFieldHasFocus = false;
    entityManager.ForEach<Scene::UITextInputComponent>(
        [this, &textFieldHasFocus](const Scene::Entity entity,
                                   const Scene::UITextInputComponent& field) {
            if (field.focused) {
                m_focusedEntity = entity;
                textFieldHasFocus = true;
            }
        });
    if (!textFieldHasFocus && m_focusedEntity != Scene::kInvalidEntity &&
        entityManager.IsAlive(m_focusedEntity) &&
        entityManager.HasComponent<Scene::UITextInputComponent>(m_focusedEntity)) {
        m_focusedEntity = Scene::kInvalidEntity;
    }

    std::sort(ranked.begin(), ranked.end(), [](const RankedWidget& lhs, const RankedWidget& rhs) {
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
    for (RankedWidget& widget : ranked) {
        const Scene::Entity entity{widget.entityIndex, widget.entityGeneration};
        const std::optional<Math::Rect> clip = ScrollClipOf(entityManager, entity);
        if (!clip.has_value()) {
            continue;
        }
        widget.quads.erase(std::remove_if(widget.quads.begin(), widget.quads.end(),
                                          [&clip](UIQuad& quad) { return !ClipQuad(quad, *clip); }),
                           widget.quads.end());
    }
    for (const RankedWidget& widget : ranked) {
        m_drawBatch.quads.insert(m_drawBatch.quads.end(), widget.quads.begin(), widget.quads.end());
    }
    for (UIQuad& quad : m_drawBatch.quads) {
        quad.rect = Math::Transform(quad.rect,
                                       Math::Vector2{m_coordinateTransform.scaleX, m_coordinateTransform.scaleY},
                                       Math::Vector2{m_coordinateTransform.offsetX, m_coordinateTransform.offsetY});
    }
}

} // namespace UVE::UI
