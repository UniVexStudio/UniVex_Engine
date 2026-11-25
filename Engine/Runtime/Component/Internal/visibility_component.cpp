// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "visibility_component.h"

namespace UVE::Scene {

bool IsVisibilityComponentValid(const VisibilityComponent&) noexcept {
    // Two plain bools: there is no representable invalid state. Defined out of line rather than
    // made constexpr in the header so the component matches every other one's shape - a validator
    // that lives in a different place for one component is a trap for generic code.
    return true;
}

bool IsVisibilityComponentValidForEntity(const VisibilityComponent& component,
                                            const Entity selfEntity) noexcept {
    if (selfEntity == kInvalidEntity) {
        return true;
    }
    return component.visibilityParent != selfEntity;
}

bool HasVisibilityRedirect(const VisibilityComponent& component) noexcept {
    return component.visibilityParent != kInvalidEntity;
}

bool ResolveInheritedVisibility(VisibilityComponent* const visibility,
                                   const bool parentVisible) noexcept {
    if (visibility == nullptr) {
        return parentVisible;
    }
    const bool resolved = parentVisible && visibility->visible;
    visibility->visibleInHierarchy = resolved;
    return resolved;
}

bool IsEffectivelyVisible(const VisibilityComponent* const visibility) noexcept {
    return visibility == nullptr ? true : visibility->visibleInHierarchy;
}

VisibilityComponent SanitizeVisibilityComponentForEntity(
    const VisibilityComponent& component, const Entity selfEntity) noexcept {
    VisibilityComponent out = component;
    if (selfEntity != kInvalidEntity && out.visibilityParent == selfEntity) {
        out.visibilityParent = kInvalidEntity;
    }
    return out;
}

} // namespace UVE::Scene
