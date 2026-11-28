// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace UVE::Scene {

/// Components a scene, prefab or entity file carried that this engine could not restore: a type it
/// no longer has, or data its current validator refuses. Each entry is the component's name and
/// its JSON exactly as the file had it.
///
/// WHY KEEP THEM. A file written by another engine version used to be refused whole when one
/// component in it was unknown, so an old entity could not be placed at all. Loading now restores
/// everything it can and parks the rest here; the serializer writes these entries back unchanged,
/// so opening and saving an old asset never silently deletes the author's data, and a later engine
/// that knows the type again can still read it.
///
/// Never simulated, never shown as a component of its own; the Inspector reports it as a notice.
struct UnresolvedComponentsComponent final {
    std::vector<std::pair<std::string, std::string>> entries;

    [[nodiscard]] bool operator==(const UnresolvedComponentsComponent& other) const noexcept {
        return entries == other.entries;
    }
};

} // namespace UVE::Scene
