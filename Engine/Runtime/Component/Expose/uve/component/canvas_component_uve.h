// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

namespace UVE::Scene {

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

/// A CanvasComponentUVE has no field combination that is ever invalid - this validator exists only
/// to satisfy the same authored-component-validation contract every other component follows.
[[nodiscard]] bool IsCanvasComponentValidUVE(const CanvasComponentUVE&) noexcept;

} // namespace UVE::Scene
