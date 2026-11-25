// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "matrix4x4.h"
#include "quaternion.h"
#include "vector2.h"
#include "vector3.h"

namespace UVE::Scene {

inline constexpr std::int32_t kMinimumCanvasSortOrder = -32768;
inline constexpr std::int32_t kMaximumCanvasSortOrder = 32767;

/// Rendering domain for Canvas UI.
enum class CanvasRenderMode : std::uint8_t {
    ScreenSpaceOverlay = 0U,
    WorldSpace = 1U,
};

/// UI root component supporting both 2D Screen-space overlay and 3D World-space UI
/// (such as diegetic in-game computer screens, floating health bars, or name tags).
struct CanvasComponent final {
    bool visible = true;
    /// Draw-order key when canvases overlap - higher draws later (on top).
    std::int32_t sortOrder = 0;
    /// Rendering mode: ScreenSpaceOverlay (HUD) or WorldSpace (3D in-scene canvas).
    CanvasRenderMode renderMode = CanvasRenderMode::ScreenSpaceOverlay;
    /// Metric physical size in world space (meters) when in WorldSpace mode.
    Math::Vector2 worldSize{1.0F, 1.0F};
    /// Reference pixel resolution mapped across the world quad (e.g. 100 px = 1 meter).
    Math::Vector2 referencePixels{100.0F, 100.0F};
    /// When true in WorldSpace mode, automatically orients the canvas to face the camera.
    bool billboard = false;
    /// Screen-space: the canvas was laid out for `referenceResolution` and is scaled to fit the
    /// screen it is shown on (the window, a phone, the editor's view), centred - what a player sees
    /// on any screen is what the Designer showed. Off: authored pixels are screen pixels.
    bool scaleWithScreen = false;
    /// The screen size the widgets were placed for. Zero: the device's own screen.
    Math::Vector2 referenceResolution{0.0F, 0.0F};

    [[nodiscard]] bool operator==(const CanvasComponent&) const = default;
};

/// A CanvasComponent has no field combination that is ever invalid - this validator exists
/// to satisfy the authored-component-validation contract every other component follows.
[[nodiscard]] bool IsCanvasComponentValid(const CanvasComponent&) noexcept;

/// True when `sortOrder` sits within the standard 16-bit signed canvas sort range.
[[nodiscard]] bool IsCanvasSortOrderInRange(std::int32_t sortOrder) noexcept;

/// Clamps `sortOrder` into `[kMinimumCanvasSortOrder, kMaximumCanvasSortOrder]`.
[[nodiscard]] std::int32_t ClampCanvasSortOrder(std::int32_t sortOrder) noexcept;

/// Returns a copy of `component` with `sortOrder` clamped to the supported range.
[[nodiscard]] CanvasComponent SanitizeCanvasComponent(
    const CanvasComponent& component) noexcept;

/// Combines `canvas->visible` (when present) with an ancestor's already-resolved visibility.
[[nodiscard]] bool ResolveInheritedCanvasVisibility(const CanvasComponent* canvas,
                                                       bool parentVisible = true) noexcept;

/// Resolves the effective sort order for a canvas, falling back to `fallbackSortOrder` when null.
[[nodiscard]] std::int32_t ResolveEffectiveCanvasSortOrder(
    const CanvasComponent* canvas, std::int32_t fallbackSortOrder = 0) noexcept;

/// Deterministic draw-order comparator for two canvases (`sortOrder` ascending, then tie-break).
[[nodiscard]] bool CompareCanvasDrawOrder(const CanvasComponent& lhs,
                                             std::uint32_t lhsTieBreak,
                                             const CanvasComponent& rhs,
                                             std::uint32_t rhsTieBreak) noexcept;

/// Computes the 3D world matrix that maps widget 2D pixel coordinates [0..referencePixels] to the 3D quad in world space.
[[nodiscard]] Math::Matrix4x4 ComputeWorldSpaceCanvasTransform(
    const CanvasComponent& canvas, const Math::Vector3& worldPosition,
    const Math::Quaternion& worldRotation, const Math::Vector3& worldScale) noexcept;

/// Computes the 3D billboard world matrix that orients the canvas to face cameraPosition.
[[nodiscard]] Math::Matrix4x4 ComputeWorldSpaceCanvasBillboardTransform(
    const CanvasComponent& canvas, const Math::Vector3& worldPosition,
    const Math::Vector3& cameraPosition, const Math::Vector3& worldScale) noexcept;

} // namespace UVE::Scene
