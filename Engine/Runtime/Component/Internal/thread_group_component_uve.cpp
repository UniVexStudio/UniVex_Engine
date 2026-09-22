// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/thread_group_component_uve.h"

namespace UVE::Scene {

bool IsThreadGroupComponentValidUVE(const ThreadGroupComponentUVE&) noexcept {
    return true;
}

ThreadGroupModeUVE ResolveThreadGroupModeUVE(const ThreadGroupModeUVE mode,
                                             const ThreadGroupModeUVE parentMode) noexcept {
    if (mode == ThreadGroupModeUVE::Inherit) {
        // Inherit at the top of a hierarchy means the safe default rather than "unanswered".
        return parentMode == ThreadGroupModeUVE::Inherit ? ThreadGroupModeUVE::MainThread : parentMode;
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
    return mode;
}

} // namespace UVE::Scene
