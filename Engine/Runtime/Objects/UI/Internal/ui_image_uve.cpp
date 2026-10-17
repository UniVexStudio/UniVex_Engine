// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/canvas/ui_image_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsUIImageObjectDefinitionValidUVE(const UIImageObjectDefinitionUVE& value) noexcept {
    return IsUIImageComponentValidUVE(value.image);
}

void ApplyUIImageObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                     const UIImageObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<UIImageComponentUVE>(entity,
                                                       SanitizeUIImageComponentUVE(value.image));
}

bool UIImageUVE::HasTextureUVE(const UIImageComponentUVE& image) noexcept {
    return HasAssignedUIImageTextureUVE(image);
}

bool UIImageUVE::UsesNineSliceUVE(const UIImageComponentUVE& image) noexcept {
    return image.nineSliceEnabled && HasTextureUVE(image) &&
           std::isfinite(image.sliceMarginMin.x) && std::isfinite(image.sliceMarginMin.y) &&
           std::isfinite(image.sliceMarginMax.x) && std::isfinite(image.sliceMarginMax.y) &&
           std::isfinite(image.sliceUVMin.x) && std::isfinite(image.sliceUVMin.y) &&
           std::isfinite(image.sliceUVMax.x) && std::isfinite(image.sliceUVMax.y);
}

UIImageNineSliceSplitsUVE UIImageUVE::ResolveNineSliceSplitsUVE(
    const UIImageComponentUVE& image) noexcept {
    UIImageNineSliceSplitsUVE splits{};
    if (!UsesNineSliceUVE(image)) {
        return splits;
    }
    splits.marginLeft = std::max(0.0F, image.sliceMarginMin.x);
    splits.marginTop = std::max(0.0F, image.sliceMarginMin.y);
    splits.marginRight = std::max(0.0F, image.sliceMarginMax.x);
    splits.marginBottom = std::max(0.0F, image.sliceMarginMax.y);
    splits.uvLeft = std::clamp(image.sliceUVMin.x, 0.0F, 1.0F);
    splits.uvTop = std::clamp(image.sliceUVMin.y, 0.0F, 1.0F);
    splits.uvRight = std::clamp(image.sliceUVMax.x, 0.0F, 1.0F);
    splits.uvBottom = std::clamp(image.sliceUVMax.y, 0.0F, 1.0F);

    const float drawnWide = splits.marginLeft + splits.marginRight;
    if (drawnWide > image.rect.size.x && drawnWide > 0.0F) {
        const float scale = image.rect.size.x / drawnWide;
        splits.marginLeft *= scale;
        splits.marginRight *= scale;
    }
    const float drawnTall = splits.marginTop + splits.marginBottom;
    if (drawnTall > image.rect.size.y && drawnTall > 0.0F) {
        const float scale = image.rect.size.y / drawnTall;
        splits.marginTop *= scale;
        splits.marginBottom *= scale;
    }
    const float uvWide = splits.uvLeft + splits.uvRight;
    if (uvWide > 1.0F) {
        splits.uvLeft /= uvWide;
        splits.uvRight /= uvWide;
    }
    const float uvTall = splits.uvTop + splits.uvBottom;
    if (uvTall > 1.0F) {
        splits.uvTop /= uvTall;
        splits.uvBottom /= uvTall;
    }
    return splits;
}

} // namespace UVE::Scene
