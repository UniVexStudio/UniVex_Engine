// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/process_component_uve.h"

namespace UVE::Scene {

bool IsProcessComponentValidUVE(const ProcessComponentUVE&) noexcept {
    // Every field is an enumerator or an ordering key, and no combination of them is contradictory:
    // a Disabled entity with a priority is simply a disabled entity that will use that priority the
    // moment it is enabled again.
    return true;
}

ProcessModeUVE ResolveProcessModeUVE(const ProcessModeUVE mode, const ProcessModeUVE parentMode) noexcept {
    // Inherit passes the parent's answer through. An intermediate node that never opted in must not
    // break a subtree's chain - the same rule visibility and physics interpolation already follow.
    if (mode == ProcessModeUVE::Inherit) {
        // A parent that is itself unresolved (only possible at a root, or after a cycle fallback)
        // means the hierarchy default.
        return parentMode == ProcessModeUVE::Inherit ? ProcessModeUVE::Pausable : parentMode;
    }
    // Deliberately NOT "the most restrictive of parent and child wins". A pause menu parented under
    // something Pausable has to be able to say Always and be believed, which is the whole reason
    // the mode is authored per entity rather than inherited outright.
    return mode;
}

bool IsProcessingUVE(const ProcessModeUVE resolvedMode, const bool simulationPaused) noexcept {
    switch (resolvedMode) {
        case ProcessModeUVE::Inherit:
            // Unresolved at the top of a hierarchy: treated as the default rather than as an error,
            // so an entity created without a parent still behaves like everything around it.
            return !simulationPaused;
        case ProcessModeUVE::Pausable:
            return !simulationPaused;
        case ProcessModeUVE::WhenPaused:
            return simulationPaused;
        case ProcessModeUVE::Always:
            return true;
        case ProcessModeUVE::Disabled:
            return false;
    }
    return false;
}

} // namespace UVE::Scene
