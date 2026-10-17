// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "uve/asset/asset_guid_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/math/color_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"
#include "uve/objects/3d/sprite_3d_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// 3D billboard text component built on `SurfaceInstance3DBaseUVE`.
struct Label3DComponentUVE final {
    std::string text{"Label3D"};
    Asset::AssetGuidUVE fontAssetGuid = Asset::kInvalidAssetGuidUVE;
    float fontSizePixels = 32.0F;
    float pixelSizeMetres = 0.01F;
    Math::ColorUVE modulate{1.0F, 1.0F, 1.0F};
    Math::ColorUVE outlineModulate{0.0F, 0.0F, 0.0F};
    float outlineSizePixels = 0.0F;
    BillboardMode3DUVE billboardMode = BillboardMode3DUVE::Disabled;
    bool doubleSided = true;

    [[nodiscard]] bool operator==(const Label3DComponentUVE&) const = default;
};

[[nodiscard]] bool IsLabel3DComponentValidUVE(const Label3DComponentUVE& value) noexcept;

[[nodiscard]] Label3DComponentUVE SanitizeLabel3DComponentUVE(const Label3DComponentUVE& value);

struct Label3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Label3D";
    Label3DComponentUVE label{};
};

[[nodiscard]] bool IsLabel3DObjectDefinitionValidUVE(
    const Label3DObjectDefinitionUVE& value) noexcept;

void ApplyLabel3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                     const Label3DObjectDefinitionUVE& value);

class Label3DUVE final {
public:
    Label3DUVE() = delete;

    /// Estimates the world-space width and height in metres of `label.text` using `fontSizePixels`
    /// and `pixelSizeMetres` (accounting for newline breaks).
    [[nodiscard]] static Math::Vector2UVE EstimateWorldSizeMetresUVE(
        const Label3DComponentUVE& label) noexcept;
};

} // namespace UVE::Scene
