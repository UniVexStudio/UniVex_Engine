// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

inline constexpr std::uint32_t kDefaultRenderInstanceLayerMaskUVE = 0x00000001U;
inline constexpr std::uint32_t kMaximumRenderInstanceLayerIndexUVE = 31U;

/// The shared state of RenderInstance3D, the abstract base of every object that is drawn - meshes,
/// lights, decals, particles. No object is a RenderInstance3D on its own; its kinds carry this
/// component and add their own.
struct RenderInstanceComponentUVE final {
    /// The render layers this instance is on. A camera draws it only when their layers overlap.
    std::uint32_t renderLayers = kDefaultRenderInstanceLayerMaskUVE;
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

/// Replaces a non-finite sorting offset with `0.0F`.
[[nodiscard]] RenderInstanceComponentUVE SanitizeRenderInstanceComponentUVE(
    const RenderInstanceComponentUVE& component) noexcept;

/// True when `component.renderLayers` overlaps `cameraLayerMask`.
[[nodiscard]] bool MatchesRenderInstanceLayerMaskUVE(
    const RenderInstanceComponentUVE& component, std::uint32_t cameraLayerMask) noexcept;

/// True when layer `layerIndex` (`0..31`) is set in `component.renderLayers`.
[[nodiscard]] bool IsRenderInstanceOnLayerUVE(const RenderInstanceComponentUVE& component,
                                              std::uint32_t layerIndex) noexcept;

/// Sets or clears bit `layerIndex` (`0..31`) in `component.renderLayers`.
void SetRenderInstanceLayerBitUVE(RenderInstanceComponentUVE& component,
                                  std::uint32_t layerIndex, bool enabled) noexcept;

/// Selects either `worldAabbCenter` or `worldOrigin` according to `sortingUseAabbCenter`.
[[nodiscard]] Math::Vector3UVE ResolveRenderInstanceSortAnchorUVE(
    const RenderInstanceComponentUVE& component, const Math::Vector3UVE& worldOrigin,
    const Math::Vector3UVE& worldAabbCenter) noexcept;

/// Computes the effective sorting depth (`baseCameraDistance + sortingOffset`).
[[nodiscard]] float ComputeRenderInstanceSortDepthUVE(
    const RenderInstanceComponentUVE& component, float baseCameraDistance) noexcept;

} // namespace UVE::Scene
