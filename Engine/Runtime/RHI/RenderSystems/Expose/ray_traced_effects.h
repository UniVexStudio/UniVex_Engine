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

inline constexpr std::size_t kMaxRtSceneBoxes = 4096U;

/// 64-byte std430-aligned ray-tracing scene proxy node for compute RT shadows, reflections, and GI.
struct alignas(16) RtSceneBoxNode final {
    /// xyz = world AABB min, w = metallic.
    std::array<float, 4> minAndMetallic{0.0F, 0.0F, 0.0F, 0.0F};
    /// xyz = world AABB max, w = roughness.
    std::array<float, 4> maxAndRoughness{0.0F, 0.0F, 0.0F, 0.5F};
    /// rgb = linear albedo, w = padding.
    std::array<float, 4> albedo{0.7F, 0.7F, 0.7F, 1.0F};
    /// rgb = linear emissive, w = padding.
    std::array<float, 4> emissive{0.0F, 0.0F, 0.0F, 0.0F};
};

static_assert(sizeof(RtSceneBoxNode) == 64U, "RtSceneBoxNode must match std430 64-byte stride");

struct RtRayHit final {
    bool hit = false;
    float distance = 0.0F;
    std::uint32_t boxIndex = 0U;
    Math::Vector3 normal{0.0F, 1.0F, 0.0F};
    Math::Vector3 albedo{0.0F, 0.0F, 0.0F};
    Math::Vector3 emissive{0.0F, 0.0F, 0.0F};
};

/// Collects ray-tracing scene proxy boxes from visible primitives in `entityManager`.
[[nodiscard]] std::vector<RtSceneBoxNode> BuildRtSceneBoxesFromEntities(
    Scene::IEntityManager& entityManager, std::size_t maxBoxes = kMaxRtSceneBoxes);

/// Exact ray-AABB slab tracer matching `rt_trace.glsl` bit-for-bit in float32 arithmetic.
[[nodiscard]] RtRayHit TraceRayAgainstSceneBoxes(std::span<const RtSceneBoxNode> boxes,
                                                       const Math::Vector3& origin,
                                                       const Math::Vector3& direction,
                                                       float tMin = 0.002F,
                                                       float tMax = 250.0F) noexcept;

/// Evaluates ray-traced directional sun shadow visibility (0 = occluded, 1 = lit) and 1-bounce
/// reflection/GI radiance at `worldPos` with surface `normal`.
struct RtSurfaceResult final {
    float sunVisibility = 1.0F;
    Math::Vector3 reflectionRadiance{0.0F, 0.0F, 0.0F};
    Math::Vector3 giBounceRadiance{0.0F, 0.0F, 0.0F};
};

[[nodiscard]] RtSurfaceResult EvaluateRtShadowAndBounce(std::span<const RtSceneBoxNode> boxes,
                                                              const Math::Vector3& worldPos,
                                                              const Math::Vector3& normal,
                                                              const Math::Vector3& viewDir,
                                                              const Math::Vector3& sunDir,
                                                              const Math::Vector3& sunRadiance,
                                                              const Math::Vector3& skyColor) noexcept;

} // namespace UVE::Render
