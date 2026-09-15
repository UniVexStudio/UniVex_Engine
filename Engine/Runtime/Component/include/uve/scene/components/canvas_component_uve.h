// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

namespace UVE::Scene {

/// Marks an entity as a screen-space UI root - the minimal "Canvas" concept named by the
/// playable-engine roadmap's UI/HUD item. World-space (3D-anchored) canvases are explicitly out of
/// scope for this pass; every UITextComponentUVE/UIImageComponentUVE/UIButtonComponentUVE renders
/// in raw window-pixel coordinates regardless of which Canvas (if any) it's nested under - this
/// component exists as an authoring/visibility grouping and future extension point, not something
/// UIRuntimeUVE's batching currently reads per-element.
/// Thread-safety: value type; trivially safe to copy/move.
struct CanvasComponentUVE final {
    bool visible = true;
    /// Draw-order tiebreaker when multiple canvases overlap - higher draws later (on top). Purely
    /// a sort key; UIRuntimeUVE does not otherwise interpret it.
    std::int32_t sortOrder = 0;
};

/// A CanvasComponentUVE has no field combination that is ever invalid - this validator exists only
/// to satisfy the same authored-component-validation contract every other component follows.
[[nodiscard]] constexpr bool IsCanvasComponentValidUVE(const CanvasComponentUVE&) noexcept {
    return true;
}

} // namespace UVE::Scene
