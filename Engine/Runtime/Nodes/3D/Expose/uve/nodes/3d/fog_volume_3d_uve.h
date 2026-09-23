// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

enum class FogVolumeShapeUVE : std::uint8_t {
    Ellipsoid = 0,
    Cone,
    Cylinder,
    Box,
    /// Fills the whole world, ignoring the size - a global fog layer with a node you can place.
    World,
};

/// FogVolume3D: a region of volumetric fog - mist in a valley, smoke in a room, dust in a shaft of
/// light. A RenderInstance3D.
///
/// The fog's look is set right here - density, colour, glow, falloff - rather than through a
/// separate material that has to be created first. A material still takes over when one is set,
/// for fog driven by a custom shader.
struct FogVolume3DNodeComponentUVE final {
    FogVolumeShapeUVE shape = FogVolumeShapeUVE::Box;
    /// The volume's extent, centred on the node. Ignored for World.
    Math::Vector3UVE size{2.0F, 2.0F, 2.0F};
    /// How thick the fog is. Negative carves fog out of other volumes - a clear room in a foggy
    /// level.
    float density = 1.0F;
    /// The colour light takes on as it scatters through the fog.
    Math::Vector3UVE albedo{1.0F, 1.0F, 1.0F};
    /// Light the fog gives off itself, for glowing mist.
    Math::Vector3UVE emission{0.0F, 0.0F, 0.0F};
    /// Thins the fog with height inside the volume; 0 keeps it even.
    float heightFalloff = 0.0F;
    /// Softens the volume's boundary: 0 a hard edge, 1 fades all the way from the centre.
    float edgeFade = 0.1F;
    /// Optional project-relative fog material; when set it replaces the values above.
    std::string materialAssetPath;

    [[nodiscard]] bool operator==(const FogVolume3DNodeComponentUVE&) const = default;
};

[[nodiscard]] bool IsFogVolume3DNodeComponentValidUVE(const FogVolume3DNodeComponentUVE& value) noexcept;

struct FogVolume3DNodeDefinitionUVE final {
    static constexpr std::string_view defaultName = "FogVolume3D";
    FogVolume3DNodeComponentUVE fog{};
};

/// The RenderInstance3D recipe under this node's name, then the fog component.
void ApplyFogVolume3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                       const FogVolume3DNodeDefinitionUVE& value);

} // namespace UVE::Scene
