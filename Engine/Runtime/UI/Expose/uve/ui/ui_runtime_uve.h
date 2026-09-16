// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/input/i_input_system_uve.h"
#include "uve/scene/i_entity_manager_uve.h"
#include "uve/ui/ui_draw_batch_uve.h"
#include "uve/ui/ui_font_atlas_uve.h"

namespace UVE::UI {

/// Per-frame reconciliation of authored screen-space UI (Canvas/UIText/UIImage/UIButton) into two
/// things: real button hit-testing against the actual mouse state (writing `isHovered`/
/// `wasClickedThisFrame` back onto UIButtonComponentUVE, the same "runtime state written by a
/// Sync* system" convention CharacterControllerComponentUVE::isGrounded already established), and
/// a plain-data UIDrawBatchUVE snapshot a renderer can later turn into pixels (Phase U3 - no GPU
/// resource is touched here). Ticked once per real frame (not the fixed-step loop), since UI
/// responsiveness should track real input latency, not simulation steps.
/// Thread-safety: not thread-safe; owned and ticked from the scene/runtime thread.
class UIRuntimeUVE final {
public:
    UIRuntimeUVE() = default;
    UIRuntimeUVE(const UIRuntimeUVE&) = delete;
    UIRuntimeUVE& operator=(const UIRuntimeUVE&) = delete;

    void TickUVE(Scene::IEntityManagerUVE& entityManager, const Input::IInputSystemUVE& inputSystem);

    [[nodiscard]] const UIDrawBatchUVE& GetDrawBatchUVE() const noexcept { return m_drawBatch; }
    [[nodiscard]] const UIFontAtlasUVE& GetFontAtlasUVE() const noexcept { return m_fontAtlas; }

private:
    UIFontAtlasUVE m_fontAtlas;
    UIDrawBatchUVE m_drawBatch;
};

} // namespace UVE::UI
