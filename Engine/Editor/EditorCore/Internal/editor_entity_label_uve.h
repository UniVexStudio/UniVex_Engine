// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>

#include "uve/component/entity_uve.h"

namespace UVE::Editor {

/// Fallback display text for an entity with no NameComponentUVE: "Entity 12:3", index then
/// generation.
///
/// Shared because both the inspector and the hierarchy fall back to it, and the two must agree -
/// an entity that reads "Entity 12:3" in the outliner and something else in the inspector looks
/// like two different objects. Including the generation is deliberate: entity indices are
/// recycled, so index alone would give a destroyed entity and its replacement the same label.
[[nodiscard]] inline std::string EntityLabelUVE(const Scene::EntityUVE entity) {
    return "Entity " + std::to_string(entity.index) + ":" + std::to_string(entity.generation);
}

} // namespace UVE::Editor
