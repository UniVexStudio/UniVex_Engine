// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace UVE::Scene {

/// Components a scene, prefab or entity file carried that this engine could not restore: a type it
/// no longer has, or data its current validator refuses. Each entry is the component's name and
/// its JSON as the file had it.
///
/// WHY KEEP THEM. A file written by another engine version used to be refused whole when one
/// component in it was unknown, so an old entity could not be placed at all. A file loaded from
/// disk now restores everything it can and parks the rest here; saving it writes the entries back
/// unchanged, so opening and saving an old asset never silently deletes the author's data, and a
/// later engine that knows the type again can still read it.
///
/// ENTITY REFERENCES. The JSON of an entry names other entities by file-local id (`...LocalId`,
/// `...LocalIds`). Those ids mean nothing once entities are created, deleted or renumbered, so
/// while parked they are held as entity handles (`{"$ref":[index,generation]}`) and turned back
/// into the local ids of whatever file or snapshot is written. A target outside what is written
/// becomes -1, the same way a live reference to an outside entity does.
///
/// PLACEHOLDERS. `placeholders` names components this entity carries only as a stand-in for a
/// parked one (a default transform, so the entity can still be placed). While the stand-in is
/// untouched, saving writes the parked original instead of it; once the author edits it, their
/// edit wins.
///
/// Never simulated and never an Inspector section; placing an asset reports the names in the
/// content status message.
struct UnresolvedComponentsComponent final {
    std::vector<std::pair<std::string, std::string>> entries;
    std::vector<std::string> placeholders;

    [[nodiscard]] bool operator==(const UnresolvedComponentsComponent& other) const noexcept {
        return entries == other.entries && placeholders == other.placeholders;
    }
};

} // namespace UVE::Scene
