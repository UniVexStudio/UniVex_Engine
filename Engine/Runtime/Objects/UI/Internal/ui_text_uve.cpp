// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/canvas/ui_text_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsUITextObjectDefinitionValidUVE(const UITextObjectDefinitionUVE& value) noexcept {
    return IsUITextComponentValidUVE(value.text);
}

void ApplyUITextObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                    const UITextObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<UITextComponentUVE>(entity, value.text);
}

bool UITextUVE::HasTextUVE(const UITextComponentUVE& text) noexcept {
    return IsUITextComponentValidUVE(text) && !text.text.empty();
}

Math::Vector2UVE UITextUVE::BaselineCursorPixelsUVE(const UITextComponentUVE& text) noexcept {
    return Math::Vector2UVE{text.positionPixels.x, text.positionPixels.y + text.fontSize};
}

Math::RectUVE UITextUVE::ResolveBoundingRectUVE(
    const UITextComponentUVE& text, const float measuredWidthPixels) noexcept {
    const float safeWidth =
        std::isfinite(measuredWidthPixels) ? std::max(0.0F, measuredWidthPixels) : 0.0F;
    const float safeHeight = std::isfinite(text.fontSize) ? ClampUIFontSizeUVE(text.fontSize) : 0.0F;
    return Math::RectUVE{text.positionPixels, Math::Vector2UVE{safeWidth, safeHeight}};
}

} // namespace UVE::Scene
