// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

namespace UVE::Scene {

/// The shared state of RenderInstance3D, the abstract base of every node that is drawn - meshes,
/// lights, decals, particles. No node is a RenderInstance3D on its own; its kinds carry this
/// component and add their own.
struct RenderInstanceComponentUVE final {
    /// The render layers this instance is on. A camera draws it only when their layers overlap.
    std::uint32_t renderLayers = 0x00000001U;
    /// Moves this instance forward (negative) or back (positive) in transparent sorting, in world
    /// units, without moving it.
    float sortingOffset = 0.0F;
    /// Sort by the centre of the bounds rather than the origin, which is right for anything whose
    /// origin is not in its middle.
    bool sortingUseAabbCenter = true;

    [[nodiscard]] bool operator==(const RenderInstanceComponentUVE&) const = default;
};

/// The sorting offset must be finite.
[[nodiscard]] bool IsRenderInstanceComponentValidUVE(const RenderInstanceComponentUVE& component) noexcept;

} // namespace UVE::Scene
