// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/ui_image_component_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the UI Image scene object (Canvas family): the component set and
/// defaults a freshly created UI Image entity attaches — one untextured white 64x64 quad.
struct UIImageObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "UIImage";
    /// Default document-entity name for a freshly created object of this kind — matches the
    /// Inspector's own "UI Image" component label exactly.
    static constexpr std::string_view defaultName = "UI Image";

    /// Image authored defaults (no texture bound, 64x64 px, white tint, opaque); the entity's
    /// transform is attached by the creation shell.
    UIImageComponentUVE image{};

    [[nodiscard]] bool operator==(const UIImageObjectDefinitionUVE&) const = default;
};

[[nodiscard]] bool IsUIImageObjectDefinitionValidUVE(const UIImageObjectDefinitionUVE& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
void ApplyUIImageObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                     const UIImageObjectDefinitionUVE& value);

/// Clamped pixel margins and UV splits for a 9-slice `UIImageComponentUVE`.
struct UIImageNineSliceSplitsUVE final {
    float marginLeft = 0.0F;
    float marginTop = 0.0F;
    float marginRight = 0.0F;
    float marginBottom = 0.0F;
    float uvLeft = 0.0F;
    float uvTop = 0.0F;
    float uvRight = 0.0F;
    float uvBottom = 0.0F;

    [[nodiscard]] bool operator==(const UIImageNineSliceSplitsUVE&) const = default;
};

/// Node policy for a `UIImage` widget object.
///
/// Owns texture presence checks, 9-slice eligibility, and proportional border clamping shared by
/// `UIRuntimeUVE`.
class UIImageUVE final {
public:
    UIImageUVE() = delete;

    /// True when `image.textureAssetGuid` is non-zero (renders as a textured quad rather than a
    /// solid-color fill).
    [[nodiscard]] static bool HasTextureUVE(const UIImageComponentUVE& image) noexcept;

    /// True when `image` has a bound texture, `nineSliceEnabled` is true, and all slice margins
    /// and UV splits are finite.
    [[nodiscard]] static bool UsesNineSliceUVE(const UIImageComponentUVE& image) noexcept;

    /// Resolves clamped pixel margins and UV splits for `image`, shrinking over-wide borders
    /// proportionally so opposite borders never overlap.
    [[nodiscard]] static UIImageNineSliceSplitsUVE ResolveNineSliceSplitsUVE(
        const UIImageComponentUVE& image) noexcept;
};

} // namespace UVE::Scene
