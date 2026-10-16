// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/disabled_component_uve.h"

namespace UVE::Scene {

bool IsDisabledComponentValidUVE(const DisabledComponentUVE& /*component*/) noexcept {
    return true;
}

bool IsDisabledTagPresentUVE(const DisabledComponentUVE* const component) noexcept {
    return component != nullptr;
}

bool ShouldSimulateEntityUVE(const DisabledComponentUVE* const component) noexcept {
    return !IsDisabledTagPresentUVE(component);
}

bool ShouldSkipEntitySimulationUVE(const DisabledComponentUVE* const component) noexcept {
    return IsDisabledTagPresentUVE(component);
}

bool ShouldPropagateTransformWhenDisabledUVE(
    const DisabledComponentUVE* const /*component*/) noexcept {
    return true;
}

} // namespace UVE::Scene
