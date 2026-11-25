// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <functional>

#include "accessibility_bridge.h"
#include "entity.h"
#include "i_input_system.h"
#include "localization.h"
#include "i_entity_manager.h"
#include "ui_draw_batch.h"
#include "ui_font_atlas.h"

namespace UVE::UI {

/// Maps authored UI coordinates to the renderer's current presentation target. Draw coordinates
/// become `authored * scale + offset`; pointer coordinates are first converted by `inputScale`,
/// then `inputOffset` and the draw offset are removed before button hit testing.
struct UICoordinateTransform final {
    float scaleX = 1.0F;
    float scaleY = 1.0F;
    float offsetX = 0.0F;
    float offsetY = 0.0F;
    float inputScaleX = 1.0F;
    float inputScaleY = 1.0F;
    float inputOffsetX = 0.0F;
    float inputOffsetY = 0.0F;

    [[nodiscard]] bool operator==(const UICoordinateTransform&) const = default;
};

/// What UI text needs in order to be localized, supplied by the caller.
///
/// Whether an entity's text is translated is a hierarchy question - its Auto Translate mode is
/// inherited from its ancestors - and answering it belongs to the scene graph. Taking the answer as
/// a callback keeps this module free of a dependency on the scene graph while still honouring it.
struct UITextLocalization final {
    /// Null means no localization: every text is drawn exactly as authored.
    const Localization::LocalizationService* service = nullptr;
    /// Whether `entity`'s text is looked up. Null means every text is.
    std::function<bool(Scene::Entity)> isAutoTranslated;
};

/// Per-frame reconciliation of authored screen-space UI (Canvas/UIText/UIImage/UIButton) into
/// button hit-testing and a UIDrawBatch. CanvasComponent on an ancestor hides widgets and
/// orders canvases by sortOrder. Widgets without a canvas still draw. No GPU work here.
/// Thread-safety: not thread-safe; owned and ticked from the scene/runtime thread.
class UIRuntime final {
public:
    UIRuntime() = default;
    UIRuntime(const UIRuntime&) = delete;
    UIRuntime& operator=(const UIRuntime&) = delete;

    /// Applies the same presentation transform to drawing and pointer hit testing. Invalid scales
    /// are ignored and reset to identity so malformed runtime metrics cannot poison UI state.
    void SetCoordinateTransform(const UICoordinateTransform& transform) noexcept;

    /// The authored-space size roots anchor against: the reference resolution when a stretch mode
    /// is active, device points when stretching is off (the engine recomputes it every frame
    /// beside the coordinate transform). Non-finite or negative sizes are ignored, keeping the
    /// previous size; the default is zero, which collapses anchored roots to their offsets until
    /// the first real metrics arrive.
    void SetViewportSize(const Math::Vector2& viewportSize) noexcept;

    [[nodiscard]] const Math::Vector2& GetViewportSize() const noexcept { return m_viewportSize; }

    /// The frame's delta time in seconds, fed by the engine from the real frame clock every frame
    /// (UI tweens stay on wall time so pause menus animate while the simulation is paused).
    /// Non-finite or negative values are ignored; the default is zero, which freezes tweens.
    void SetDeltaTime(float deltaTime) noexcept;

    /// `localization` defaults to none, so a caller that does not localize draws authored text
    /// exactly as it always did. An authored string is its own translation key: a table maps
    /// "Play" to "Maglaro", and a string with no entry is drawn as authored.
    void Tick(Scene::IEntityManager& entityManager, const Input::IInputSystem& inputSystem,
                 const UITextLocalization& localization = {});

    [[nodiscard]] const UIDrawBatch& GetDrawBatch() const noexcept { return m_drawBatch; }
    [[nodiscard]] const UIFontAtlas& GetFontAtlas() const noexcept { return m_fontAtlas; }

    /// The widget that currently owns keyboard focus, or kInvalidEntity. Focus moves in
    /// deterministic reading order (top-to-bottom, left-to-right, then entity handle) with
    /// Tab / Shift+Tab and wraps. It is runtime state only: it is never serialized, stale or
    /// hidden/disabled widgets lose it automatically, and mouse focus in a text field adopts it.
    [[nodiscard]] Scene::Entity GetFocusedEntity() const noexcept { return m_focusedEntity; }
    void ClearFocus() noexcept { m_focusedEntity = Scene::kInvalidEntity; }

    /// Accessibility UI scale multiplier clamped to [0.5, 3.0].
    void SetUiScaleFactor(const float scaleFactor) noexcept {
        if (scaleFactor >= 0.5F && scaleFactor <= 3.0F) {
            m_uiScaleFactor = scaleFactor;
        }
    }
    [[nodiscard]] float GetUiScaleFactor() const noexcept { return m_uiScaleFactor; }

    /// Optional OS screen-reader / accessibility bridge notified when UI focus changes.
    void SetAccessibilityBridge(IAccessibilityBridge* const bridge) noexcept {
        m_accessibilityBridge = bridge;
    }
    [[nodiscard]] IAccessibilityBridge* GetAccessibilityBridge() const noexcept {
        return m_accessibilityBridge;
    }

private:
    UIFontAtlas m_fontAtlas;
    UIDrawBatch m_drawBatch;
    Scene::Entity m_focusedEntity = Scene::kInvalidEntity;
    Scene::Entity m_dropdownKeyboardEntity = Scene::kInvalidEntity;
    std::int32_t m_dropdownKeyboardIndex = -1;
    UICoordinateTransform m_coordinateTransform{};
    Math::Vector2 m_viewportSize{};
    float m_deltaTime = 0.0F;
    float m_uiScaleFactor = 1.0F;
    IAccessibilityBridge* m_accessibilityBridge = nullptr;
};

} // namespace UVE::UI
