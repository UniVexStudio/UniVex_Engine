// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

enum class DecalProjectionModeUVE : std::uint8_t {
    Box = 0,
    Cylinder,
};

/// Decal3D: projects a material onto whatever surfaces fall inside its box - bullet holes,
/// footprints, puddles, graffiti. A RenderInstance3D: its render layers, sorting and the Node3D
/// transform and visibility above that come from its bases.
struct Decal3DNodeComponentUVE final {
    std::string materialAssetPath;
    /// The projection volume, centred on the node; the decal projects along its -Y.
    Math::Vector3UVE size{1.0F, 1.0F, 1.0F};
    DecalProjectionModeUVE projection = DecalProjectionModeUVE::Box;
    /// Seconds before the decal removes itself; 0 keeps it.
    float lifetime = 0.0F;
    bool enabled = true;

    /// Tints the projected colour; a quick way to vary one decal material across many decals.
    Math::Vector3UVE modulate{1.0F, 1.0F, 1.0F};
    /// Multiplies the material's emission.
    float emissionEnergy = 1.0F;
    /// How much of the surface's own colour the decal replaces: 1 fully, 0 not at all (normal
    /// and roughness still apply - useful for dents that keep the paint).
    float albedoMix = 1.0F;
    /// Fades the decal on surfaces facing away from the projection: 0 never, 1 at any angle.
    float normalFade = 0.0F;
    /// Fades the decal toward the top and bottom of its volume, so it does not end in a hard edge.
    float upperFade = 0.3F;
    float lowerFade = 0.3F;
    bool distanceFadeEnabled = false;
    float distanceFadeBegin = 40.0F;
    float distanceFadeLength = 10.0F;
    /// The render layers it projects onto.
    std::uint32_t cullMask = 0xFFFFFFFFU;

    [[nodiscard]] bool operator==(const Decal3DNodeComponentUVE&) const = default;
};

[[nodiscard]] bool IsDecal3DNodeComponentValidUVE(const Decal3DNodeComponentUVE& value) noexcept;

struct Decal3DNodeDefinitionUVE final {
    static constexpr std::string_view defaultName = "Decal3D";
    Decal3DNodeComponentUVE decal{};
};

/// The RenderInstance3D recipe under this node's name, then the decal component.
void ApplyDecal3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                   const Decal3DNodeDefinitionUVE& value);

} // namespace UVE::Scene
