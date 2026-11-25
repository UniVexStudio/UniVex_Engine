// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "entity.h"
#include "quaternion.h"
#include "object_3d_common.h"

namespace UVE::Scene {

class IEntityManager;

enum class FogVolumeShape : std::uint8_t {
    Ellipsoid = 0,
    Cone,
    Cylinder,
    Box,
    World,
};

inline constexpr std::size_t kMaximumFogVolumesPerFrame = 8U;
inline constexpr std::uint32_t kFogVolumeRaySamples = 12U;

/// FogVolume3D: a region of volumetric fog - mist in a valley, smoke in a room, dust in a shaft of
/// light. A RenderInstance3D.
///
/// The fog's look is set right here - density, colour, glow, falloff. A material path is stored for
/// a future custom-shader takeover; the renderer uses these fields until then.
struct FogVolume3DComponent final {
    FogVolumeShape shape = FogVolumeShape::Box;
    Math::Vector3 size{2.0F, 2.0F, 2.0F};
    float density = 1.0F;
    Math::Vector3 albedo{1.0F, 1.0F, 1.0F};
    Math::Vector3 emission{0.0F, 0.0F, 0.0F};
    float heightFalloff = 0.0F;
    float edgeFade = 0.1F;
    std::string materialAssetPath;

    [[nodiscard]] bool operator==(const FogVolume3DComponent&) const = default;
};

[[nodiscard]] bool IsFogVolume3DObjectComponentValid(const FogVolume3DComponent& value) noexcept;

struct FogVolume3DObjectDefinition final {
    static constexpr std::string_view defaultName = "FogVolume3D";
    FogVolume3DComponent fog{};
};

void ApplyFogVolume3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                       const FogVolume3DObjectDefinition& value);

struct FogVolume3DFrame final {
    Math::Vector3 worldPosition{};
    Math::Vector3 axisX{1.0F, 0.0F, 0.0F};
    Math::Vector3 axisY{0.0F, 1.0F, 0.0F};
    Math::Vector3 axisZ{0.0F, 0.0F, 1.0F};
    Math::Vector3 worldScale{1.0F, 1.0F, 1.0F};
    Math::Vector3 size{2.0F, 2.0F, 2.0F};
    Math::Vector3 albedo{1.0F, 1.0F, 1.0F};
    Math::Vector3 emission{};
    float density = 1.0F;
    float heightFalloff = 0.0F;
    float edgeFade = 0.1F;
    FogVolumeShape shape = FogVolumeShape::Box;
};

[[nodiscard]] bool TryMakeFogVolume3DFrame(const FogVolume3DComponent& value,
                                              const Math::Vector3& worldPosition,
                                              const Math::Quaternion& worldRotation,
                                              const Math::Vector3& worldScale,
                                              FogVolume3DFrame& out) noexcept;

[[nodiscard]] std::optional<Math::Vector3> FogVolume3DWorldToLocal(const FogVolume3DFrame& frame,
                                                                        const Math::Vector3& worldPoint) noexcept;

[[nodiscard]] float SampleFogVolume3DDensity(const FogVolume3DFrame& frame,
                                                const Math::Vector3& worldPoint) noexcept;

struct FogVolume3DRaySegment final {
    float enter = 0.0F;
    float exit = 0.0F;
};

[[nodiscard]] std::optional<FogVolume3DRaySegment> IntersectFogVolume3DRay(
    const FogVolume3DFrame& frame, const Math::Vector3& rayOrigin, const Math::Vector3& rayDirection,
    float rayLength) noexcept;

struct FogVolume3DRaySample final {
    float opticalDepth = 0.0F;
    Math::Vector3 scatterColor{1.0F, 1.0F, 1.0F};
    float scatterWeight = 0.0F;
};

[[nodiscard]] FogVolume3DRaySample IntegrateFogVolume3DRay(const FogVolume3DFrame& frame,
                                                                 const Math::Vector3& rayOrigin,
                                                                 const Math::Vector3& rayDirection,
                                                                 float rayLength) noexcept;

[[nodiscard]] std::size_t CollectFogVolume3DFrames(IEntityManager& entityManager,
                                                      const Math::Vector3& viewPosition,
                                                      std::span<FogVolume3DFrame> out);

struct FogVolume3DGizmo final {
    FogVolumeShape shape = FogVolumeShape::Box;
    Math::Vector3 origin{};
    Math::Vector3 axisX{1.0F, 0.0F, 0.0F};
    Math::Vector3 axisY{0.0F, 1.0F, 0.0F};
    Math::Vector3 axisZ{0.0F, 0.0F, 1.0F};
    Math::Vector3 halfExtents{1.0F, 1.0F, 1.0F};
    Math::Vector3 color{0.55F, 0.82F, 1.0F};
};

void CollectFogVolume3DGizmos(IEntityManager& entityManager, std::vector<FogVolume3DGizmo>& out);

} // namespace UVE::Scene
