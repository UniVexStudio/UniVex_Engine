// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "vector3.h"
#include "particle_runtime.h"
#include "vfx_draw_list.h"
#include "vfx_world.h"

namespace UVE::Render {

inline constexpr std::size_t kMaximumParticleRenderItemsUVE = 1'000'000U;

struct ParticleRenderItemUVE final {
    Scene::EntityUVE entity;
    Math::Vector3UVE position{};
    float remainingLifetimeSeconds = 0.0F;
    float sortDepth = 0.0F;
    std::uint64_t sequence = 0U;

    [[nodiscard]] bool operator==(const ParticleRenderItemUVE&) const = default;
};

struct ParticleRenderSnapshotUVE final {
    std::size_t sourceParticleCount = 0U;
    bool truncated = false;
    std::vector<ParticleRenderItemUVE> items;

    [[nodiscard]] bool operator==(const ParticleRenderSnapshotUVE&) const = default;
};

/// Validates one copied particle render snapshot against bounded consistency and finite draw facts.
/// The predicate performs no GPU allocation, asset lookup, renderer registration, culling, or mutation.
[[nodiscard]] bool ValidateParticleRenderSnapshotUVE(
    const ParticleRenderSnapshotUVE& snapshot,
    std::size_t maximumItems = kMaximumParticleRenderItemsUVE) noexcept;

/// Copies enabled CPU particle state into the renderer-owned value path. It performs no GPU work,
/// asset lookup, renderer registration, culling against a camera, or mutation of ParticleRuntimeUVE.
class ParticleRenderBridgeUVE final {
public:
    [[nodiscard]] static ParticleRenderSnapshotUVE ExtractUVE(
        const Scene::ParticleRuntimeUVE& runtime,
        std::size_t maximumItems = kMaximumParticleRenderItemsUVE);

    /// Extracts all active VFX sprite batches from `world` into `outDrawList`, transforming local-space
    /// emitters by their owner transform, computing signed view-space depth along `cameraForward`
    /// (or distance to `cameraPosition` when `cameraForward` is zero), and capping total extracted
    /// sprite instances at `maxInstances`.
    static void ExtractVfxUVE(
        const VFX::VfxWorldUVE& world,
        VfxDrawListUVE& outDrawList,
        const Math::Vector3UVE& cameraPosition = {},
        const Math::Vector3UVE& cameraForward = {0.0F, 0.0F, -1.0F},
        std::size_t maxInstances = kMaximumVfxSpriteInstancesPerFrameUVE);
};

} // namespace UVE::Render

