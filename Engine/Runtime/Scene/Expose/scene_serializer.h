// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "i_scene_serializer.h"

namespace UVE::Scene {

/// SceneSerializer is the concrete, engine-standard implementation of ISceneSerializer.
/// Holds no persistent members of its own (same "stateless, dependencies passed per-call" shape
/// as SceneGraph) — the per-component-type JSON (de)serialization table and the binary
/// envelope helpers are private implementation details entirely inside scene_serializer_uve.cpp,
/// including the only `#include <nlohmann/json.hpp>` in this module.
class SceneSerializer final : public ISceneSerializer {
public:
    SceneSerializer() = default;

    [[nodiscard]] std::optional<SceneSnapshot> Capture(
        IEntityManager& entityManager, const std::vector<Entity>& rootEntities,
        SceneAssetType assetType) const override;
    [[nodiscard]] std::vector<Entity> Restore(IEntityManager& entityManager,
                                                     const SceneSnapshot& snapshot) const override;

    [[nodiscard]] bool Save(IEntityManager& entityManager,
                                const std::vector<Entity>& rootEntities,
                                const std::filesystem::path& path,
                                SceneAssetType assetType) override;
    [[nodiscard]] std::vector<Entity> Load(IEntityManager& entityManager,
                                                  const std::filesystem::path& path) override;
};

} // namespace UVE::Scene
