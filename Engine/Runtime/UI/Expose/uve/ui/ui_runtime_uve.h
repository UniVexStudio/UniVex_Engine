// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <functional>

#include "uve/input/i_input_system_uve.h"
#include "uve/localization/localization_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/ui/ui_draw_batch_uve.h"
#include "uve/ui/ui_font_atlas_uve.h"

namespace UVE::UI {

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
};

} // namespace UVE::UI
