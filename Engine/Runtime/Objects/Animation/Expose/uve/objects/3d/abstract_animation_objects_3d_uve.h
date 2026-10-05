// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Abstract animation bases used by animation scene objects. These are not directly creatable.
struct BoneModifier3DObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "BoneModifier3D";
};

/// Pure-Object animation base shared by AnimationSequencer and AnimationGraph (no transform or visibility).
struct AnimationDriverObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "AnimationDriver";
};

/// Applies the Object3D recipe and BoneModifier component where missing.
void ApplyBoneModifier3DBaseUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                std::string_view nameFallback);
/// Applies the pure Object baseline and AnimationDriver component where missing.
void ApplyAnimationDriverBaseUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                std::string_view nameFallback);

} // namespace UVE::Scene
