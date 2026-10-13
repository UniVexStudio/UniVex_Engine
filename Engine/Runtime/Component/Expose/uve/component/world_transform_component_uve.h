// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

/// The cached, derived world-space position/rotation/scale of a scene-graph entity — internal
/// infrastructure SceneGraphUVE owns, not one of the master spec's named built-in components.
/// Kept as a component separate from TransformComponentUVE so setting the authored local
/// transform never has to touch this cached data (and vice versa). `dirty` is set whenever the
/// authored local transform or the parent link changes (see SceneGraphUVE::
/// SetLocalTransformUVE()/SetParentUVE()); SceneGraphUVE::UpdateUVE() clears it after
/// recomputing worldPosition/worldRotation/worldScale from the parent chain. Ordinary ECS
/// Get/ForEach/View access exposes a const live component; SceneGraphUVE alone can obtain the
/// mutable derived-cache access key. The mutable dirty bit is an invalidation request, not a
/// second world-pose writer. Initial value construction stays compatible; no serialized layout
/// changes (this derived cache is never serialized).
struct WorldTransformComponentUVE final {
    Math::Vector3UVE worldPosition{};
    Math::QuaternionUVE worldRotation{};
    Math::Vector3UVE worldScale{1.0F, 1.0F, 1.0F};
    mutable bool dirty = true;
};

} // namespace UVE::Scene
