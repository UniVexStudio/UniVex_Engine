// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "vector3.h"

namespace UVE::Scene {

inline constexpr std::uint32_t kDefaultRenderInstanceLayerMask = 0x00000001U;
inline constexpr std::uint32_t kMaximumRenderInstanceLayerIndex = 31U;

/// The shared state of RenderInstance3D, the abstract base of every object that is drawn - meshes,
/// lights, decals, particles. No object is a RenderInstance3D on its own; its kinds carry this
/// component and add their own.
struct RenderInstanceComponent final {
    /// The render layers this instance is on. A camera draws it only when their layers overlap.
    std::uint32_t renderLayers = kDefaultRenderInstanceLayerMask;
    /// Moves this instance forward (negative) or back (positive) in transparent sorting, in world
    /// units, without moving it.
    float sortingOffset = 0.0F;
    /// Sort by the centre of the bounds rather than the origin, which is right for anything whose
    /// origin is not in its middle.
    bool sortingUseAabbCenter = true;

    [[nodiscard]] bool operator==(const RenderInstanceComponent&) const = default;
};

/// The sorting offset must be finite.
[[nodiscard]] bool IsRenderInstanceComponentValid(const RenderInstanceComponent& component) noexcept;

/// Replaces a non-finite sorting offset with `0.0F`.
[[nodiscard]] RenderInstanceComponent SanitizeRenderInstanceComponent(
    const RenderInstanceComponent& component) noexcept;

/// True when `component.renderLayers` overlaps `cameraLayerMask`.
[[nodiscard]] bool MatchesRenderInstanceLayerMask(
    const RenderInstanceComponent& component, std::uint32_t cameraLayerMask) noexcept;

/// True when layer `layerIndex` (`0..31`) is set in `component.renderLayers`.
[[nodiscard]] bool IsRenderInstanceOnLayer(const RenderInstanceComponent& component,
                                              std::uint32_t layerIndex) noexcept;

/// Sets or clears bit `layerIndex` (`0..31`) in `component.renderLayers`.
void SetRenderInstanceLayerBit(RenderInstanceComponent& component,
                                  std::uint32_t layerIndex, bool enabled) noexcept;

/// Selects either `worldAabbCenter` or `worldOrigin` according to `sortingUseAabbCenter`.
[[nodiscard]] Math::Vector3 ResolveRenderInstanceSortAnchor(
    const RenderInstanceComponent& component, const Math::Vector3& worldOrigin,
    const Math::Vector3& worldAabbCenter) noexcept;

/// Computes the effective sorting depth (`baseCameraDistance + sortingOffset`).
[[nodiscard]] float ComputeRenderInstanceSortDepth(
    const RenderInstanceComponent& component, float baseCameraDistance) noexcept;

} // namespace UVE::Scene
