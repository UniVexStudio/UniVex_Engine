// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "thread_group_component.h"

namespace UVE::Scene {

bool IsThreadGroupModeValid(const ThreadGroupMode mode) noexcept {
    switch (mode) {
        case ThreadGroupMode::Inherit:
        case ThreadGroupMode::MainThread:
        case ThreadGroupMode::SubThread:
            return true;
    }
    return false;
}

bool IsThreadGroupComponentValid(const ThreadGroupComponent& component) noexcept {
    return IsThreadGroupModeValid(component.mode) &&
           IsThreadGroupModeValid(component.resolvedModeInHierarchy);
}

ThreadGroupMode ResolveThreadGroupMode(const ThreadGroupMode mode,
                                             const ThreadGroupMode parentMode) noexcept {
    const ThreadGroupMode safeMode =
        IsThreadGroupModeValid(mode) ? mode : ThreadGroupMode::MainThread;
    if (safeMode == ThreadGroupMode::Inherit) {
        // Inherit at the top of a hierarchy means the safe default rather than "unanswered".
        return (!IsThreadGroupModeValid(parentMode) ||
                parentMode == ThreadGroupMode::Inherit)
                   ? ThreadGroupMode::MainThread
                   : parentMode;
    }
    // A MainThread ancestor is a constraint, not a preference: the child is part of whatever shared
    // state forced the parent onto the main thread. Letting the child override it would turn a
    // declared safety property into a race that only shows up under load.
    //
    // Tested against `parentMode`, not against a parent whose Inherit has already been folded into
    // MainThread. Folding first would make an entity with no ancestor constraint look like one
    // that has been pinned, and a root could then never choose SubThread at all.
    if (parentMode == ThreadGroupMode::MainThread) {
        return ThreadGroupMode::MainThread;
    }
    return safeMode;
}

ThreadGroupMode ResolveHierarchyThreadGroupMode(
    ThreadGroupComponent* const component,
    const ThreadGroupMode parentResolvedMode) noexcept {
    if (component == nullptr) {
        return ResolveThreadGroupMode(ThreadGroupMode::Inherit, parentResolvedMode);
    }
    const ThreadGroupMode resolved =
        ResolveThreadGroupMode(component->mode, parentResolvedMode);
    component->resolvedModeInHierarchy = resolved;
    return resolved;
}

bool CanDispatchOnSubThread(const ThreadGroupComponent* const component) noexcept {
    return component != nullptr &&
           component->resolvedModeInHierarchy == ThreadGroupMode::SubThread;
}

bool CompareThreadGroupExecutionOrder(const ThreadGroupComponent& lhs,
                                         const std::uint32_t lhsTieBreak,
                                         const ThreadGroupComponent& rhs,
                                         const std::uint32_t rhsTieBreak) noexcept {
    if (lhs.order != rhs.order) {
        return lhs.order < rhs.order;
    }
    return lhsTieBreak < rhsTieBreak;
}

ThreadGroupComponent SanitizeThreadGroupComponent(
    const ThreadGroupComponent& component) noexcept {
    const ThreadGroupMode safeMode =
        IsThreadGroupModeValid(component.mode) ? component.mode : ThreadGroupMode::Inherit;
    const ThreadGroupMode safeResolved =
        (IsThreadGroupModeValid(component.resolvedModeInHierarchy) &&
         component.resolvedModeInHierarchy != ThreadGroupMode::Inherit)
            ? component.resolvedModeInHierarchy
            : ResolveThreadGroupMode(safeMode, ThreadGroupMode::Inherit);
    return ThreadGroupComponent{
        .mode = safeMode,
        .order = component.order,
        .resolvedModeInHierarchy = safeResolved,
    };
}

} // namespace UVE::Scene
