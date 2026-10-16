// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/thread_group_component_uve.h"

namespace UVE::Scene {

bool IsThreadGroupModeValidUVE(const ThreadGroupModeUVE mode) noexcept {
    switch (mode) {
        case ThreadGroupModeUVE::Inherit:
        case ThreadGroupModeUVE::MainThread:
        case ThreadGroupModeUVE::SubThread:
            return true;
    }
    return false;
}

bool IsThreadGroupComponentValidUVE(const ThreadGroupComponentUVE& component) noexcept {
    return IsThreadGroupModeValidUVE(component.mode) &&
           IsThreadGroupModeValidUVE(component.resolvedModeInHierarchy);
}

ThreadGroupModeUVE ResolveThreadGroupModeUVE(const ThreadGroupModeUVE mode,
                                             const ThreadGroupModeUVE parentMode) noexcept {
    const ThreadGroupModeUVE safeMode =
        IsThreadGroupModeValidUVE(mode) ? mode : ThreadGroupModeUVE::MainThread;
    if (safeMode == ThreadGroupModeUVE::Inherit) {
        // Inherit at the top of a hierarchy means the safe default rather than "unanswered".
        return (!IsThreadGroupModeValidUVE(parentMode) ||
                parentMode == ThreadGroupModeUVE::Inherit)
                   ? ThreadGroupModeUVE::MainThread
                   : parentMode;
    }
    // A MainThread ancestor is a constraint, not a preference: the child is part of whatever shared
    // state forced the parent onto the main thread. Letting the child override it would turn a
    // declared safety property into a race that only shows up under load.
    //
    // Tested against `parentMode`, not against a parent whose Inherit has already been folded into
    // MainThread. Folding first would make an entity with no ancestor constraint look like one
    // that has been pinned, and a root could then never choose SubThread at all.
    if (parentMode == ThreadGroupModeUVE::MainThread) {
        return ThreadGroupModeUVE::MainThread;
    }
    return safeMode;
}

ThreadGroupModeUVE ResolveHierarchyThreadGroupModeUVE(
    ThreadGroupComponentUVE* const component,
    const ThreadGroupModeUVE parentResolvedMode) noexcept {
    if (component == nullptr) {
        return ResolveThreadGroupModeUVE(ThreadGroupModeUVE::Inherit, parentResolvedMode);
    }
    const ThreadGroupModeUVE resolved =
        ResolveThreadGroupModeUVE(component->mode, parentResolvedMode);
    component->resolvedModeInHierarchy = resolved;
    return resolved;
}

bool CanDispatchOnSubThreadUVE(const ThreadGroupComponentUVE* const component) noexcept {
    return component != nullptr &&
           component->resolvedModeInHierarchy == ThreadGroupModeUVE::SubThread;
}

bool CompareThreadGroupExecutionOrderUVE(const ThreadGroupComponentUVE& lhs,
                                         const std::uint32_t lhsTieBreak,
                                         const ThreadGroupComponentUVE& rhs,
                                         const std::uint32_t rhsTieBreak) noexcept {
    if (lhs.order != rhs.order) {
        return lhs.order < rhs.order;
    }
    return lhsTieBreak < rhsTieBreak;
}

ThreadGroupComponentUVE SanitizeThreadGroupComponentUVE(
    const ThreadGroupComponentUVE& component) noexcept {
    const ThreadGroupModeUVE safeMode =
        IsThreadGroupModeValidUVE(component.mode) ? component.mode : ThreadGroupModeUVE::Inherit;
    const ThreadGroupModeUVE safeResolved =
        (IsThreadGroupModeValidUVE(component.resolvedModeInHierarchy) &&
         component.resolvedModeInHierarchy != ThreadGroupModeUVE::Inherit)
            ? component.resolvedModeInHierarchy
            : ResolveThreadGroupModeUVE(safeMode, ThreadGroupModeUVE::Inherit);
    return ThreadGroupComponentUVE{
        .mode = safeMode,
        .order = component.order,
        .resolvedModeInHierarchy = safeResolved,
    };
}

} // namespace UVE::Scene
