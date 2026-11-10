// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

namespace UVE::Render::Vulkan {

/// Instance-level validation tooling available from the current Vulkan loader.
struct ValidationLayerPlanUVE final {
    bool enableKhronosValidationUVE = false;
    bool enableDebugUtilsUVE = false;
    bool validationLayerUnavailableUVE = false;
    bool debugUtilsUnavailableUVE = false;

    [[nodiscard]] bool operator==(const ValidationLayerPlanUVE&) const = default;
};

/// Resolve the optional debug-layer request without making validation tooling a backend
/// requirement. The validation layer can still run without VK_EXT_debug_utils, but its
/// messages will not be forwarded through the engine's debug callback in that case.
[[nodiscard]] constexpr ValidationLayerPlanUVE ResolveValidationLayerPlanUVE(
    bool requested, bool khronosValidationAvailable, bool debugUtilsAvailable) noexcept {
    if (!requested) {
        return {};
    }
    if (!khronosValidationAvailable) {
        return ValidationLayerPlanUVE{false, false, true, false};
    }
    return ValidationLayerPlanUVE{true, debugUtilsAvailable, false, !debugUtilsAvailable};
}

} // namespace UVE::Render::Vulkan
