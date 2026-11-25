// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

namespace UVE::Render::Vulkan {

/// Instance-level validation tooling available from the current Vulkan loader.
struct ValidationLayerPlan final {
    bool enableKhronosValidation = false;
    bool enableDebugUtils = false;
    bool validationLayerUnavailable = false;
    bool debugUtilsUnavailable = false;

    [[nodiscard]] bool operator==(const ValidationLayerPlan&) const = default;
};

/// Resolve the optional debug-layer request without making validation tooling a backend
/// requirement. The validation layer can still run without VK_EXT_debug_utils, but its
/// messages will not be forwarded through the engine's debug callback in that case.
[[nodiscard]] constexpr ValidationLayerPlan ResolveValidationLayerPlan(
    bool requested, bool khronosValidationAvailable, bool debugUtilsAvailable) noexcept {
    if (!requested) {
        return {};
    }
    if (!khronosValidationAvailable) {
        return ValidationLayerPlan{false, false, true, false};
    }
    return ValidationLayerPlan{true, debugUtilsAvailable, false, !debugUtilsAvailable};
}

} // namespace UVE::Render::Vulkan
