// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "label_3d.h"

#include <algorithm>
#include <cmath>

#include "i_entity_manager.h"
#include "abstract_objects_3d.h"

namespace UVE::Scene {

bool IsLabel3DComponentValid(const Label3DComponent& value) noexcept {
    if (static_cast<std::uint8_t>(value.billboardMode) >
        static_cast<std::uint8_t>(BillboardMode3D::FixedY)) {
        return false;
    }
    return IsBounded3DObjectString(value.text) && std::isfinite(value.fontSizePixels) &&
           value.fontSizePixels > 0.0F && std::isfinite(value.pixelSizeMetres) &&
           value.pixelSizeMetres > 0.0F && std::isfinite(value.modulate.r) &&
           std::isfinite(value.modulate.g) && std::isfinite(value.modulate.b) &&
           std::isfinite(value.outlineModulate.r) && std::isfinite(value.outlineModulate.g) &&
           std::isfinite(value.outlineModulate.b) && std::isfinite(value.outlineSizePixels) &&
           value.outlineSizePixels >= 0.0F;
}

Label3DComponent SanitizeLabel3DComponent(const Label3DComponent& value) {
    Label3DComponent out = value;
    if (!IsBounded3DObjectString(out.text)) {
        out.text.resize(kMaximum3DObjectStringLength);
    }
    if (static_cast<std::uint8_t>(out.billboardMode) >
        static_cast<std::uint8_t>(BillboardMode3D::FixedY)) {
        out.billboardMode = BillboardMode3D::Disabled;
    }
    if (!std::isfinite(out.fontSizePixels) || out.fontSizePixels <= 0.0F) {
        out.fontSizePixels = 32.0F;
    }
    if (!std::isfinite(out.pixelSizeMetres) || out.pixelSizeMetres <= 0.0F) {
        out.pixelSizeMetres = 0.01F;
    }
    if (!std::isfinite(out.modulate.r) || !std::isfinite(out.modulate.g) ||
        !std::isfinite(out.modulate.b)) {
        out.modulate = Math::Color{1.0F, 1.0F, 1.0F};
    }
    if (!std::isfinite(out.outlineModulate.r) || !std::isfinite(out.outlineModulate.g) ||
        !std::isfinite(out.outlineModulate.b)) {
        out.outlineModulate = Math::Color{0.0F, 0.0F, 0.0F};
    }
    if (!std::isfinite(out.outlineSizePixels) || out.outlineSizePixels < 0.0F) {
        out.outlineSizePixels = 0.0F;
    }
    return out;
}

bool IsLabel3DObjectDefinitionValid(const Label3DObjectDefinition& value) noexcept {
    return IsLabel3DComponentValid(value.label);
}

void ApplyLabel3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                     const Label3DObjectDefinition& value) {
    ApplySurfaceInstance3DBase(entityManager, entity, Label3DObjectDefinition::defaultName);
    entityManager.AddComponent<Label3DComponent>(entity,
                                                       SanitizeLabel3DComponent(value.label));
}

Math::Vector2 Label3D::EstimateWorldSizeMetres(const Label3DComponent& label) noexcept {
    if (!IsLabel3DComponentValid(label) || label.text.empty()) {
        return Math::Vector2{0.0F, 0.0F};
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
    return Math::Vector2{static_cast<float>(maxColumns) * charAdvanceMetres,
                            static_cast<float>(lines) * lineHeightMetres};
}

} // namespace UVE::Scene
