// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "aabb.h"
#include "i_entity_manager.h"
#include "vector3.h"

namespace UVE::Render {

inline constexpr std::size_t kMaxRtSceneBoxesUVE = 4096U;

/// 64-byte std430-aligned ray-tracing scene proxy node for compute RT shadows, reflections, and GI.
struct alignas(16) RtSceneBoxNodeUVE final {
    /// xyz = world AABB min, w = metallic.
    std::array<float, 4> minAndMetallic{0.0F, 0.0F, 0.0F, 0.0F};
    /// xyz = world AABB max, w = roughness.
    std::array<float, 4> maxAndRoughness{0.0F, 0.0F, 0.0F, 0.5F};
    /// rgb = linear albedo, w = padding.
    std::array<float, 4> albedo{0.7F, 0.7F, 0.7F, 1.0F};
    /// rgb = linear emissive, w = padding.
    std::array<float, 4> emissive{0.0F, 0.0F, 0.0F, 0.0F};
};

static_assert(sizeof(RtSceneBoxNodeUVE) == 64U, "RtSceneBoxNodeUVE must match std430 64-byte stride");

struct RtRayHitUVE final {
    bool hit = false;
    float distance = 0.0F;
    std::uint32_t boxIndex = 0U;
    Math::Vector3UVE normal{0.0F, 1.0F, 0.0F};
    Math::Vector3UVE albedo{0.0F, 0.0F, 0.0F};
    Math::Vector3UVE emissive{0.0F, 0.0F, 0.0F};
};

/// Collects ray-tracing scene proxy boxes from visible primitives in `entityManager`.
[[nodiscard]] std::vector<RtSceneBoxNodeUVE> BuildRtSceneBoxesFromEntitiesUVE(
    Scene::IEntityManagerUVE& entityManager, std::size_t maxBoxes = kMaxRtSceneBoxesUVE);

/// Exact ray-AABB slab tracer matching `rt_trace.glsl` bit-for-bit in float32 arithmetic.
[[nodiscard]] RtRayHitUVE TraceRayAgainstSceneBoxesUVE(std::span<const RtSceneBoxNodeUVE> boxes,
                                                       const Math::Vector3UVE& origin,
                                                       const Math::Vector3UVE& direction,
                                                       float tMin = 0.002F,
                                                       float tMax = 250.0F) noexcept;

/// Evaluates ray-traced directional sun shadow visibility (0 = occluded, 1 = lit) and 1-bounce
/// reflection/GI radiance at `worldPos` with surface `normal`.
struct RtSurfaceResultUVE final {
    float sunVisibility = 1.0F;
    Math::Vector3UVE reflectionRadiance{0.0F, 0.0F, 0.0F};
    Math::Vector3UVE giBounceRadiance{0.0F, 0.0F, 0.0F};
};

[[nodiscard]] RtSurfaceResultUVE EvaluateRtShadowAndBounceUVE(std::span<const RtSceneBoxNodeUVE> boxes,
                                                              const Math::Vector3UVE& worldPos,
                                                              const Math::Vector3UVE& normal,
                                                              const Math::Vector3UVE& viewDir,
                                                              const Math::Vector3UVE& sunDir,
                                                              const Math::Vector3UVE& sunRadiance,
                                                              const Math::Vector3UVE& skyColor) noexcept;

} // namespace UVE::Render
