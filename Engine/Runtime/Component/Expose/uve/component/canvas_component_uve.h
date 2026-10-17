// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

namespace UVE::Scene {

inline constexpr std::int32_t kMinimumCanvasSortOrderUVE = -32768;
inline constexpr std::int32_t kMaximumCanvasSortOrderUVE = 32767;

/// Screen-space UI root. Widgets keep raw window-pixel coordinates; the canvas does not scale them.
/// World-space canvases are out of scope. UIRuntimeUVE walks the hierarchy to this component:
/// `visible` hides every descendant widget (and ancestor canvases AND together), `sortOrder`
/// paints later (on top) when higher. A widget with no canvas ancestor still draws at sort 0.
/// Thread-safety: value type; trivially safe to copy/move.
struct CanvasComponentUVE final {
    bool visible = true;
    /// Draw-order key when canvases overlap - higher draws later (on top).
    std::int32_t sortOrder = 0;
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

} // namespace UVE::Scene
