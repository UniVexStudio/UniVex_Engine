// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/visibility_component_uve.h"

namespace UVE::Scene {

bool IsVisibilityComponentValidUVE(const VisibilityComponentUVE&) noexcept {
    // Two plain bools: there is no representable invalid state. Defined out of line rather than
    // made constexpr in the header so the component matches every other one's shape - a validator
    // that lives in a different place for one component is a trap for generic code.
    return true;
}

bool IsVisibilityComponentValidForEntityUVE(const VisibilityComponentUVE& component,
                                            const EntityUVE selfEntity) noexcept {
    if (selfEntity == kInvalidEntityUVE) {
        return true;
    }
    return component.visibilityParent != selfEntity;
}

bool HasVisibilityRedirectUVE(const VisibilityComponentUVE& component) noexcept {
    return component.visibilityParent != kInvalidEntityUVE;
}

bool ResolveInheritedVisibilityUVE(VisibilityComponentUVE* const visibility,
                                   const bool parentVisible) noexcept {
    if (visibility == nullptr) {
        return parentVisible;
    }
    const bool resolved = parentVisible && visibility->visible;
    visibility->visibleInHierarchy = resolved;
    return resolved;
}

bool IsEffectivelyVisibleUVE(const VisibilityComponentUVE* const visibility) noexcept {
    return visibility == nullptr ? true : visibility->visibleInHierarchy;
}

VisibilityComponentUVE SanitizeVisibilityComponentForEntityUVE(
    const VisibilityComponentUVE& component, const EntityUVE selfEntity) noexcept {
    VisibilityComponentUVE out = component;
    if (selfEntity != kInvalidEntityUVE && out.visibilityParent == selfEntity) {
        out.visibilityParent = kInvalidEntityUVE;
    }
    return out;
}

} // namespace UVE::Scene
