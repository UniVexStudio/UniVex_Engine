// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Abstract animation bases used by animation scene nodes. These are not directly creatable.
struct BoneModifier3DNodeDefinitionUVE final {
    static constexpr std::string_view typeName = "BoneModifier3D";
};

/// Pure-Node animation base shared by AnimationPlayer and AnimationTree (no transform or visibility).
struct AnimationMixerNodeDefinitionUVE final {
    static constexpr std::string_view typeName = "AnimationMixer";
};

/// Applies the Node3D recipe and BoneModifier component where missing.
void ApplyBoneModifier3DBaseUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                std::string_view nameFallback);
/// Applies the pure Node baseline and AnimationMixer component where missing.
void ApplyAnimationMixerBaseUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                std::string_view nameFallback);

} // namespace UVE::Scene
