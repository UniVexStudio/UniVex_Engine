// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/label_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {

bool IsLabel3DComponentValidUVE(const Label3DComponentUVE& value) noexcept {
    if (static_cast<std::uint8_t>(value.billboardMode) >
        static_cast<std::uint8_t>(BillboardMode3DUVE::FixedY)) {
        return false;
    }
    return IsBounded3DObjectStringUVE(value.text) && std::isfinite(value.fontSizePixels) &&
           value.fontSizePixels > 0.0F && std::isfinite(value.pixelSizeMetres) &&
           value.pixelSizeMetres > 0.0F && std::isfinite(value.modulate.r) &&
           std::isfinite(value.modulate.g) && std::isfinite(value.modulate.b) &&
           std::isfinite(value.outlineModulate.r) && std::isfinite(value.outlineModulate.g) &&
           std::isfinite(value.outlineModulate.b) && std::isfinite(value.outlineSizePixels) &&
           value.outlineSizePixels >= 0.0F;
}

Label3DComponentUVE SanitizeLabel3DComponentUVE(const Label3DComponentUVE& value) {
    Label3DComponentUVE out = value;
    if (!IsBounded3DObjectStringUVE(out.text)) {
        out.text.resize(kMaximum3DObjectStringLengthUVE);
    }
    if (static_cast<std::uint8_t>(out.billboardMode) >
        static_cast<std::uint8_t>(BillboardMode3DUVE::FixedY)) {
        out.billboardMode = BillboardMode3DUVE::Disabled;
    }
    if (!std::isfinite(out.fontSizePixels) || out.fontSizePixels <= 0.0F) {
        out.fontSizePixels = 32.0F;
    }
    if (!std::isfinite(out.pixelSizeMetres) || out.pixelSizeMetres <= 0.0F) {
        out.pixelSizeMetres = 0.01F;
    }
    if (!std::isfinite(out.modulate.r) || !std::isfinite(out.modulate.g) ||
        !std::isfinite(out.modulate.b)) {
        out.modulate = Math::ColorUVE{1.0F, 1.0F, 1.0F};
    }
    if (!std::isfinite(out.outlineModulate.r) || !std::isfinite(out.outlineModulate.g) ||
        !std::isfinite(out.outlineModulate.b)) {
        out.outlineModulate = Math::ColorUVE{0.0F, 0.0F, 0.0F};
    }
    if (!std::isfinite(out.outlineSizePixels) || out.outlineSizePixels < 0.0F) {
        out.outlineSizePixels = 0.0F;
    }
    return out;
}

bool IsLabel3DObjectDefinitionValidUVE(const Label3DObjectDefinitionUVE& value) noexcept {
    return IsLabel3DComponentValidUVE(value.label);
}

void ApplyLabel3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                     const Label3DObjectDefinitionUVE& value) {
    ApplySurfaceInstance3DBaseUVE(entityManager, entity, Label3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<Label3DComponentUVE>(entity,
                                                       SanitizeLabel3DComponentUVE(value.label));
}

Math::Vector2UVE Label3DUVE::EstimateWorldSizeMetresUVE(const Label3DComponentUVE& label) noexcept {
    if (!IsLabel3DComponentValidUVE(label) || label.text.empty()) {
        return Math::Vector2UVE{0.0F, 0.0F};
    }
    std::size_t maxColumns = 0U;
    std::size_t currentColumns = 0U;
    std::size_t lines = 1U;
    for (const char ch : label.text) {
        if (ch == '\n') {
            maxColumns = std::max(maxColumns, currentColumns);
            currentColumns = 0U;
            ++lines;
        } else {
            ++currentColumns;
        }
    }
    maxColumns = std::max(maxColumns, currentColumns);
    const float charAdvanceMetres = label.fontSizePixels * 0.6F * label.pixelSizeMetres;
    const float lineHeightMetres = label.fontSizePixels * 1.2F * label.pixelSizeMetres;
    return Math::Vector2UVE{static_cast<float>(maxColumns) * charAdvanceMetres,
                            static_cast<float>(lines) * lineHeightMetres};
}

} // namespace UVE::Scene
