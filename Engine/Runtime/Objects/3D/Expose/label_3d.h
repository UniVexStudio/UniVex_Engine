// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "asset_guid.h"
#include "entity.h"
#include "color.h"
#include "object_3d_common.h"
#include "sprite_3d.h"

namespace UVE::Scene {

class IEntityManager;

/// 3D billboard text component built on `SurfaceInstance3DBase`.
struct Label3DComponent final {
    std::string text{"Label3D"};
    Asset::AssetGuid fontAssetGuid = Asset::kInvalidAssetGuid;
    float fontSizePixels = 32.0F;
    float pixelSizeMetres = 0.01F;
    Math::Color modulate{1.0F, 1.0F, 1.0F};
    Math::Color outlineModulate{0.0F, 0.0F, 0.0F};
    float outlineSizePixels = 0.0F;
    BillboardMode3D billboardMode = BillboardMode3D::Disabled;
    bool doubleSided = true;

    [[nodiscard]] bool operator==(const Label3DComponent&) const = default;
};

[[nodiscard]] bool IsLabel3DComponentValid(const Label3DComponent& value) noexcept;

[[nodiscard]] Label3DComponent SanitizeLabel3DComponent(const Label3DComponent& value);

struct Label3DObjectDefinition final {
    static constexpr std::string_view defaultName = "Label3D";
    Label3DComponent label{};
};

[[nodiscard]] bool IsLabel3DObjectDefinitionValid(
    const Label3DObjectDefinition& value) noexcept;

void ApplyLabel3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                     const Label3DObjectDefinition& value);

class Label3D final {
public:
    Label3D() = delete;

    /// Estimates the world-space width and height in metres of `label.text` using `fontSizePixels`
    /// and `pixelSizeMetres` (accounting for newline breaks).
    [[nodiscard]] static Math::Vector2 EstimateWorldSizeMetres(
        const Label3DComponent& label) noexcept;
};

} // namespace UVE::Scene
