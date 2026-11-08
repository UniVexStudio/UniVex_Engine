// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/math/matrix4x4_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

inline constexpr std::int32_t kMinimumCanvasSortOrderUVE = -32768;
inline constexpr std::int32_t kMaximumCanvasSortOrderUVE = 32767;

/// Rendering domain for Canvas UI.
enum class CanvasRenderModeUVE : std::uint8_t {
    ScreenSpaceOverlay = 0U,
    WorldSpace = 1U,
};

/// UI root component supporting both 2D Screen-space overlay and 3D World-space UI
/// (such as diegetic in-game computer screens, floating health bars, or name tags).
struct CanvasComponentUVE final {
    bool visible = true;
    /// Draw-order key when canvases overlap - higher draws later (on top).
    std::int32_t sortOrder = 0;
    /// Rendering mode: ScreenSpaceOverlay (HUD) or WorldSpace (3D in-scene canvas).
    CanvasRenderModeUVE renderMode = CanvasRenderModeUVE::ScreenSpaceOverlay;
    /// Metric physical size in world space (meters) when in WorldSpace mode.
    Math::Vector2UVE worldSize{1.0F, 1.0F};
    /// Reference pixel resolution mapped across the world quad (e.g. 100 px = 1 meter).
    Math::Vector2UVE referencePixels{100.0F, 100.0F};
    /// When true in WorldSpace mode, automatically orients the canvas to face the camera.
    bool billboard = false;

    [[nodiscard]] bool operator==(const CanvasComponentUVE&) const = default;
};

/// A CanvasComponentUVE has no field combination that is ever invalid - this validator exists
/// to satisfy the authored-component-validation contract every other component follows.
[[nodiscard]] bool IsCanvasComponentValidUVE(const CanvasComponentUVE&) noexcept;

/// True when `sortOrder` sits within the standard 16-bit signed canvas sort range.
[[nodiscard]] bool IsCanvasSortOrderInRangeUVE(std::int32_t sortOrder) noexcept;

/// Clamps `sortOrder` into `[kMinimumCanvasSortOrderUVE, kMaximumCanvasSortOrderUVE]`.
[[nodiscard]] std::int32_t ClampCanvasSortOrderUVE(std::int32_t sortOrder) noexcept;

/// Returns a copy of `component` with `sortOrder` clamped to the supported range.
[[nodiscard]] CanvasComponentUVE SanitizeCanvasComponentUVE(
    const CanvasComponentUVE& component) noexcept;

/// Combines `canvas->visible` (when present) with an ancestor's already-resolved visibility.
[[nodiscard]] bool ResolveInheritedCanvasVisibilityUVE(const CanvasComponentUVE* canvas,
                                                       bool parentVisible = true) noexcept;

/// Resolves the effective sort order for a canvas, falling back to `fallbackSortOrder` when null.
[[nodiscard]] std::int32_t ResolveEffectiveCanvasSortOrderUVE(
    const CanvasComponentUVE* canvas, std::int32_t fallbackSortOrder = 0) noexcept;

/// Deterministic draw-order comparator for two canvases (`sortOrder` ascending, then tie-break).
[[nodiscard]] bool CompareCanvasDrawOrderUVE(const CanvasComponentUVE& lhs,
                                             std::uint32_t lhsTieBreak,
                                             const CanvasComponentUVE& rhs,
                                             std::uint32_t rhsTieBreak) noexcept;

/// Computes the 3D world matrix that maps widget 2D pixel coordinates [0..referencePixels] to the 3D quad in world space.
[[nodiscard]] Math::Matrix4x4UVE ComputeWorldSpaceCanvasTransformUVE(
    const CanvasComponentUVE& canvas, const Math::Vector3UVE& worldPosition,
    const Math::QuaternionUVE& worldRotation, const Math::Vector3UVE& worldScale) noexcept;

/// Computes the 3D billboard world matrix that orients the canvas to face cameraPosition.
[[nodiscard]] Math::Matrix4x4UVE ComputeWorldSpaceCanvasBillboardTransformUVE(
    const CanvasComponentUVE& canvas, const Math::Vector3UVE& worldPosition,
    const Math::Vector3UVE& cameraPosition, const Math::Vector3UVE& worldScale) noexcept;

} // namespace UVE::Scene
