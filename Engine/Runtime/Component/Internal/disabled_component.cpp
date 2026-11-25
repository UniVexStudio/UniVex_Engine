// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "disabled_component.h"

namespace UVE::Scene {

bool IsDisabledComponentValid(const DisabledComponent& /*component*/) noexcept {
    return true;
}

bool IsDisabledTagPresent(const DisabledComponent* const component) noexcept {
    return component != nullptr;
}

bool ShouldSimulateEntity(const DisabledComponent* const component) noexcept {
    return !IsDisabledTagPresent(component);
}

bool ShouldSkipEntitySimulation(const DisabledComponent* const component) noexcept {
    return IsDisabledTagPresent(component);
}

bool ShouldPropagateTransformWhenDisabled(
    const DisabledComponent* const /*component*/) noexcept {
    return true;
}

} // namespace UVE::Scene
