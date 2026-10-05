// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/process_component_uve.h"

namespace UVE::Scene {

bool IsProcessComponentValidUVE(const ProcessComponentUVE&) noexcept {
    // Every field is an enumerator or an ordering key, and no combination of them is contradictory:
    // a Disabled entity with a priority is simply a disabled entity that will use that priority the
    // moment it is enabled again.
    return true;
}

TickModeUVE ResolveTickModeUVE(const TickModeUVE mode, const TickModeUVE parentMode) noexcept {
    // Inherit passes the parent's answer through. An intermediate object that never opted in must not
    // break a subtree's chain - the same rule visibility and physics interpolation already follow.
    if (mode == TickModeUVE::Inherit) {
        // A parent that is itself unresolved (only possible at a root, or after a cycle fallback)
        // means the hierarchy default.
        return parentMode == TickModeUVE::Inherit ? TickModeUVE::Running : parentMode;
    }
    // Deliberately NOT "the most restrictive of parent and child wins". A pause menu parented under
    // something Running has to be able to say Always and be believed, which is the whole reason
    // the mode is authored per entity rather than inherited outright.
    return mode;
}

bool IsTickingUVE(const TickModeUVE resolvedMode, const bool simulationPaused) noexcept {
    switch (resolvedMode) {
        case TickModeUVE::Inherit:
            // Unresolved at the top of a hierarchy: treated as the default rather than as an error,
            // so an entity created without a parent still behaves like everything around it.
            return !simulationPaused;
        case TickModeUVE::Running:
            return !simulationPaused;
        case TickModeUVE::PausedOnly:
            return simulationPaused;
        case TickModeUVE::Always:
            return true;
        case TickModeUVE::Never:
            return false;
    }
    return false;
}

} // namespace UVE::Scene
