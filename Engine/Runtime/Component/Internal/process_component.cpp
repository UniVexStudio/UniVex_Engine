// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "process_component.h"

namespace UVE::Scene {

bool IsProcessComponentValid(const ProcessComponent&) noexcept {
    // Every field is an enumerator or an ordering key, and no combination of them is contradictory:
    // a Disabled entity with a priority is simply a disabled entity that will use that priority the
    // moment it is enabled again.
    return true;
}

TickMode ResolveTickMode(const TickMode mode, const TickMode parentMode) noexcept {
    // Inherit passes the parent's answer through. An intermediate object that never opted in must not
    // break a subtree's chain - the same rule visibility and physics interpolation already follow.
    if (mode == TickMode::Inherit) {
        // A parent that is itself unresolved (only possible at a root, or after a cycle fallback)
        // means the hierarchy default.
        return parentMode == TickMode::Inherit ? TickMode::Running : parentMode;
    }
    // Deliberately NOT "the most restrictive of parent and child wins". A pause menu parented under
    // something Running has to be able to say Always and be believed, which is the whole reason
    // the mode is authored per entity rather than inherited outright.
    return mode;
}

bool IsTicking(const TickMode resolvedMode, const bool simulationPaused) noexcept {
    switch (resolvedMode) {
        case TickMode::Inherit:
            // Unresolved at the top of a hierarchy: treated as the default rather than as an error,
            // so an entity created without a parent still behaves like everything around it.
            return !simulationPaused;
        case TickMode::Running:
            return !simulationPaused;
        case TickMode::PausedOnly:
            return simulationPaused;
        case TickMode::Always:
            return true;
        case TickMode::Never:
            return false;
    }
    return false;
}

} // namespace UVE::Scene
