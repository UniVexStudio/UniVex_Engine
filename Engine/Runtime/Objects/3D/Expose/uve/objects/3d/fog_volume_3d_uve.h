// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

enum class FogVolumeShapeUVE : std::uint8_t {
    Ellipsoid = 0,
    Cone,
    Cylinder,
    Box,
    World,
};

inline constexpr std::size_t kMaximumFogVolumesPerFrameUVE = 8U;
inline constexpr std::uint32_t kFogVolumeRaySamplesUVE = 12U;

/// FogVolume3D: a region of volumetric fog - mist in a valley, smoke in a room, dust in a shaft of
/// light. A RenderInstance3D.
///
/// The fog's look is set right here - density, colour, glow, falloff. A material path is stored for
/// a future custom-shader takeover; the renderer uses these fields until then.
struct FogVolume3DComponentUVE final {
    FogVolumeShapeUVE shape = FogVolumeShapeUVE::Box;
    Math::Vector3UVE size{2.0F, 2.0F, 2.0F};
    float density = 1.0F;
    Math::Vector3UVE albedo{1.0F, 1.0F, 1.0F};
    Math::Vector3UVE emission{0.0F, 0.0F, 0.0F};
    float heightFalloff = 0.0F;
    float edgeFade = 0.1F;
    std::string materialAssetPath;

    [[nodiscard]] bool operator==(const FogVolume3DComponentUVE&) const = default;
};

[[nodiscard]] bool IsFogVolume3DObjectComponentValidUVE(const FogVolume3DComponentUVE& value) noexcept;

struct FogVolume3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "FogVolume3D";
    FogVolume3DComponentUVE fog{};
};

void ApplyFogVolume3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                       const FogVolume3DObjectDefinitionUVE& value);

struct FogVolume3DFrameUVE final {
    Math::Vector3UVE worldPosition{};
    Math::Vector3UVE axisX{1.0F, 0.0F, 0.0F};
    Math::Vector3UVE axisY{0.0F, 1.0F, 0.0F};
    Math::Vector3UVE axisZ{0.0F, 0.0F, 1.0F};
    Math::Vector3UVE worldScale{1.0F, 1.0F, 1.0F};
    Math::Vector3UVE size{2.0F, 2.0F, 2.0F};
    Math::Vector3UVE albedo{1.0F, 1.0F, 1.0F};
    Math::Vector3UVE emission{};
    float density = 1.0F;
    float heightFalloff = 0.0F;
    float edgeFade = 0.1F;
    FogVolumeShapeUVE shape = FogVolumeShapeUVE::Box;
};

[[nodiscard]] bool TryMakeFogVolume3DFrameUVE(const FogVolume3DComponentUVE& value,
                                              const Math::Vector3UVE& worldPosition,
                                              const Math::QuaternionUVE& worldRotation,
                                              const Math::Vector3UVE& worldScale,
                                              FogVolume3DFrameUVE& out) noexcept;

[[nodiscard]] std::optional<Math::Vector3UVE> FogVolume3DWorldToLocalUVE(const FogVolume3DFrameUVE& frame,
                                                                        const Math::Vector3UVE& worldPoint) noexcept;

[[nodiscard]] float SampleFogVolume3DDensityUVE(const FogVolume3DFrameUVE& frame,
                                                const Math::Vector3UVE& worldPoint) noexcept;

struct FogVolume3DRaySegmentUVE final {
    float enter = 0.0F;
    float exit = 0.0F;
};

[[nodiscard]] std::optional<FogVolume3DRaySegmentUVE> IntersectFogVolume3DRayUVE(
    const FogVolume3DFrameUVE& frame, const Math::Vector3UVE& rayOrigin, const Math::Vector3UVE& rayDirection,
    float rayLength) noexcept;

struct FogVolume3DRaySampleUVE final {
    float opticalDepth = 0.0F;
    Math::Vector3UVE scatterColor{1.0F, 1.0F, 1.0F};
    float scatterWeight = 0.0F;
};

[[nodiscard]] FogVolume3DRaySampleUVE IntegrateFogVolume3DRayUVE(const FogVolume3DFrameUVE& frame,
                                                                 const Math::Vector3UVE& rayOrigin,
                                                                 const Math::Vector3UVE& rayDirection,
                                                                 float rayLength) noexcept;

[[nodiscard]] std::size_t CollectFogVolume3DFramesUVE(IEntityManagerUVE& entityManager,
                                                      const Math::Vector3UVE& viewPosition,
                                                      std::span<FogVolume3DFrameUVE> out);

struct FogVolume3DGizmoUVE final {
    FogVolumeShapeUVE shape = FogVolumeShapeUVE::Box;
    Math::Vector3UVE origin{};
    Math::Vector3UVE axisX{1.0F, 0.0F, 0.0F};
    Math::Vector3UVE axisY{0.0F, 1.0F, 0.0F};
    Math::Vector3UVE axisZ{0.0F, 0.0F, 1.0F};
    Math::Vector3UVE halfExtents{1.0F, 1.0F, 1.0F};
    Math::Vector3UVE color{0.55F, 0.82F, 1.0F};
};

void CollectFogVolume3DGizmosUVE(IEntityManagerUVE& entityManager, std::vector<FogVolume3DGizmoUVE>& out);

} // namespace UVE::Scene
