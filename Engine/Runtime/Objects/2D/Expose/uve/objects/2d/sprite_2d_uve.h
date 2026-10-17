// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <optional>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/sprite_2d_component_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the Sprite2D scene object: the component set and defaults a
/// freshly created Sprite2D entity attaches - one untextured white 64x64-equivalent quad
/// (an unset texture guid is a valid authored state: it renders as a flat modulate quad;
/// a zero-area source rect draws the whole texture). Per Engine/Runtime/Scene/README.md's
/// "one truth per concept" rule this holds the *recipe*, not a second copy of component
/// storage: the sprite record itself still lives only in Sprite2DComponentUVE. The
/// entity's transform is attached by the creation shell.
struct Sprite2DObjectDefinitionUVE final {
    /// Default document-entity name for a freshly created object of this kind - matches
    /// the registry's own display name exactly.
    static constexpr std::string_view defaultName = "Sprite2D";

    Sprite2DComponentUVE sprite{};
};

[[nodiscard]] bool IsSprite2DObjectDefinitionValidUVE(
    const Sprite2DObjectDefinitionUVE& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults.
/// The entity must be alive and must not already have any of the attached component types.
void ApplySprite2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                      const Sprite2DObjectDefinitionUVE& value);

/// The composed draw quad: local-space pixel corners plus the UVs that sample them.
/// Corners run counterclockwise from the top-left (TL, TR, BR, BL) in y-down pixels,
/// centered on the sprite's offset; UVs match corner-for-corner with v 0 at the texture's
/// top row and the flips already applied.
struct Sprite2DQuadUVE final {
    std::array<Math::Vector2UVE, 4U> corners{};
    std::array<Math::Vector2UVE, 4U> uvs{};

    [[nodiscard]] bool operator==(const Sprite2DQuadUVE&) const noexcept = default;
};

/// Composes the sprite's draw quad from its authored region plus the texture's pixel
/// size (the asset's truth, passed by the caller - never stored on the component). A
/// zero-area source rect draws the whole texture; anything else draws that region.
/// Fails closed (no value) for an invalid component, a non-finite or non-positive texture
/// size, or a region escaping the texture - the renderer draws nothing rather than
/// garbage.
[[nodiscard]] std::optional<Sprite2DQuadUVE> ComposeSprite2DQuadUVE(
    const Sprite2DComponentUVE& sprite, const Math::Vector2UVE& textureSize) noexcept;

class Sprite2DUVE final {
public:
    [[nodiscard]] static bool UsesWholeTextureRegionUVE(
        const Sprite2DComponentUVE& sprite) noexcept;
    [[nodiscard]] static Math::Vector2UVE ResolveDrawExtentPixelsUVE(
        const Sprite2DComponentUVE& sprite, const Math::Vector2UVE& textureSize) noexcept;
    [[nodiscard]] static std::optional<Sprite2DQuadUVE> ComposeQuadUVE(
        const Sprite2DComponentUVE& sprite, const Math::Vector2UVE& textureSize) noexcept;
    [[nodiscard]] static std::optional<Math::RectUVE> ComputeLocalBoundingRectUVE(
        const Sprite2DComponentUVE& sprite, const Math::Vector2UVE& textureSize) noexcept;
    [[nodiscard]] static bool ContainsLocalPointUVE(
        const Sprite2DComponentUVE& sprite, const Math::Vector2UVE& textureSize,
        const Math::Vector2UVE& localPoint) noexcept;
};

} // namespace UVE::Scene
