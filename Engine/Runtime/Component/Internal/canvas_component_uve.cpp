// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/canvas_component_uve.h"

#include <algorithm>
#include <cstdint>

namespace UVE::Scene {

[[nodiscard]] bool IsCanvasComponentValidUVE(const CanvasComponentUVE& /*component*/) noexcept {
    return true;
}

[[nodiscard]] bool IsCanvasSortOrderInRangeUVE(const std::int32_t sortOrder) noexcept {
    return sortOrder >= kMinimumCanvasSortOrderUVE && sortOrder <= kMaximumCanvasSortOrderUVE;
}

[[nodiscard]] std::int32_t ClampCanvasSortOrderUVE(const std::int32_t sortOrder) noexcept {
    return std::clamp(sortOrder, kMinimumCanvasSortOrderUVE, kMaximumCanvasSortOrderUVE);
}

[[nodiscard]] CanvasComponentUVE SanitizeCanvasComponentUVE(
    const CanvasComponentUVE& component) noexcept {
    return CanvasComponentUVE{
        .visible = component.visible,
        .sortOrder = ClampCanvasSortOrderUVE(component.sortOrder),
    };
}

[[nodiscard]] bool ResolveInheritedCanvasVisibilityUVE(const CanvasComponentUVE* const canvas,
                                                       const bool parentVisible) noexcept {
    if (!parentVisible) {
        return false;
    }
    return canvas == nullptr ? true : canvas->visible;
}

[[nodiscard]] std::int32_t ResolveEffectiveCanvasSortOrderUVE(
    const CanvasComponentUVE* const canvas, const std::int32_t fallbackSortOrder) noexcept {
    return canvas == nullptr ? fallbackSortOrder : canvas->sortOrder;
}

[[nodiscard]] bool CompareCanvasDrawOrderUVE(const CanvasComponentUVE& lhs,
                                             const std::uint32_t lhsTieBreak,
                                             const CanvasComponentUVE& rhs,
                                             const std::uint32_t rhsTieBreak) noexcept {
    if (lhs.sortOrder != rhs.sortOrder) {
        return lhs.sortOrder < rhs.sortOrder;
    }
    return lhsTieBreak < rhsTieBreak;
}

} // namespace UVE::Scene
