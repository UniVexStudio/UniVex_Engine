// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Editor {

/// One difference between two versions of an entity: an object added or removed, a component
/// added or removed, or one property of a component changed.
struct EntityDiffRowUVE final {
    enum class KindUVE : std::uint8_t { Added, Removed, Changed };
    KindUVE kind = KindUVE::Changed;
    /// The object's path in the entity's tree ("Prop/Wheel"); a second object with the same path
    /// among its siblings is "Prop/Wheel #2".
    std::string object;
    /// Empty for a whole object; "Collider" for a component; "Collider.friction" for a property.
    std::string what;
    std::string before;  ///< the saved value (Changed / Removed)
    std::string after;   ///< the current value (Changed / Added)
};

/// Compares two scene payloads (the JSON inside a `.uventity` / `.uvprefab` envelope, as the scene
/// serializer writes it: {"entities": [{"localId", "components": {...}}]}) object by object.
/// Objects are matched by their tree path, components by type, properties by name; component type
/// names lose the engine's "ComponentUVE" suffix. The root's name is not compared (the editor names
/// the root after the file). Rows come in tree order of `current` (removed objects after).
/// Unreadable JSON gives no rows.
[[nodiscard]] std::vector<EntityDiffRowUVE> DiffEntityPayloadsUVE(std::string_view savedJson, std::string_view currentJson);

} // namespace UVE::Editor
