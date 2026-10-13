// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <functional>

#include "uve/input/i_input_system_uve.h"
#include "uve/localization/localization_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/ui/ui_draw_batch_uve.h"
#include "uve/ui/ui_font_atlas_uve.h"

namespace UVE::UI {

/// Maps authored UI coordinates to the renderer's current presentation target. Draw coordinates
/// become `authored * scale + offset`; pointer coordinates are first converted by `inputScale`,
/// then `inputOffset` and the draw offset are removed before button hit testing.
struct UICoordinateTransformUVE final {
    float scaleX = 1.0F;
    float scaleY = 1.0F;
    float offsetX = 0.0F;
    float offsetY = 0.0F;
    float inputScaleX = 1.0F;
    float inputScaleY = 1.0F;
    float inputOffsetX = 0.0F;
    float inputOffsetY = 0.0F;

    [[nodiscard]] bool operator==(const UICoordinateTransformUVE&) const = default;
};

/// What UI text needs in order to be localized, supplied by the caller.
///
/// Whether an entity's text is translated is a hierarchy question - its Auto Translate mode is
/// inherited from its ancestors - and answering it belongs to the scene graph. Taking the answer as
/// a callback keeps this module free of a dependency on the scene graph while still honouring it.
struct UITextLocalizationUVE final {
    /// Null means no localization: every text is drawn exactly as authored.
    const Localization::LocalizationServiceUVE* service = nullptr;
    /// Whether `entity`'s text is looked up. Null means every text is.
    std::function<bool(Scene::EntityUVE)> isAutoTranslated;
};

/// Per-frame reconciliation of authored screen-space UI (Canvas/UIText/UIImage/UIButton) into
/// button hit-testing and a UIDrawBatchUVE. CanvasComponentUVE on an ancestor hides widgets and
/// orders canvases by sortOrder. Widgets without a canvas still draw. No GPU work here.
/// Thread-safety: not thread-safe; owned and ticked from the scene/runtime thread.
class UIRuntimeUVE final {
public:
    UIRuntimeUVE() = default;
    UIRuntimeUVE(const UIRuntimeUVE&) = delete;
    UIRuntimeUVE& operator=(const UIRuntimeUVE&) = delete;

    /// Applies the same presentation transform to drawing and pointer hit testing. Invalid scales
    /// are ignored and reset to identity so malformed runtime metrics cannot poison UI state.
    void SetCoordinateTransformUVE(const UICoordinateTransformUVE& transform) noexcept;

    /// The authored-space size roots anchor against: the reference resolution when a stretch mode
    /// is active, device points when stretching is off (the engine recomputes it every frame
    /// beside the coordinate transform). Non-finite or negative sizes are ignored, keeping the
    /// previous size; the default is zero, which collapses anchored roots to their offsets until
    /// the first real metrics arrive.
    void SetViewportSizeUVE(const Math::Vector2UVE& viewportSize) noexcept;

    [[nodiscard]] const Math::Vector2UVE& GetViewportSizeUVE() const noexcept { return m_viewportSize; }

    /// The frame's delta time in seconds, fed by the engine from the real frame clock every frame
    /// (UI tweens stay on wall time so pause menus animate while the simulation is paused).
    /// Non-finite or negative values are ignored; the default is zero, which freezes tweens.
    void SetDeltaTimeUVE(float deltaTime) noexcept;

    /// `localization` defaults to none, so a caller that does not localize draws authored text
    /// exactly as it always did. An authored string is its own translation key: a table maps
    /// "Play" to "Maglaro", and a string with no entry is drawn as authored.
    void TickUVE(Scene::IEntityManagerUVE& entityManager, const Input::IInputSystemUVE& inputSystem,
                 const UITextLocalizationUVE& localization = {});

    [[nodiscard]] const UIDrawBatchUVE& GetDrawBatchUVE() const noexcept { return m_drawBatch; }
    [[nodiscard]] const UIFontAtlasUVE& GetFontAtlasUVE() const noexcept { return m_fontAtlas; }

private:
    UIFontAtlasUVE m_fontAtlas;
    UIDrawBatchUVE m_drawBatch;
    UICoordinateTransformUVE m_coordinateTransform{};
    Math::Vector2UVE m_viewportSize{};
    float m_deltaTime = 0.0F;
};

} // namespace UVE::UI
