// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>
#include <string_view>

#include "uve/component/entity_uve.h"

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

struct WorldEnvironment3DNodeComponentUVE final {
    std::string skyAssetPath;
    Math::Vector3UVE ambientColor{0.2F, 0.2F, 0.2F};
    Math::Vector3UVE fogColor{0.5F, 0.6F, 0.7F};
    float ambientEnergy = 1.0F;
    float exposure = 1.0F;
    float fogDensity = 0.0F;
    bool fogEnabled = false;
    bool postProcessingEnabled = false;
};

[[nodiscard]] bool IsWorldEnvironment3DNodeComponentValidUVE(const WorldEnvironment3DNodeComponentUVE& value) noexcept;

class IEntityManagerUVE;

/// WorldEnvironment is a pure Node: it describes the whole world (sky, ambient light, fog,
/// exposure), not a place in it, so it has no transform and no visibility.
struct WorldEnvironmentNodeDefinitionUVE final {
    static constexpr std::string_view defaultName = "WorldEnvironment";
    WorldEnvironment3DNodeComponentUVE environment{};
};

/// The Node baseline (a transform the entity carried is removed), then the environment when missing.
void ApplyWorldEnvironmentNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                            const WorldEnvironmentNodeDefinitionUVE& value);

} // namespace UVE::Scene
