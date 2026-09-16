// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/spawn_point_3d_uve.h"

namespace UVE::Scene {

bool IsSpawnPoint3DNodeComponentValidUVE(const SpawnPoint3DNodeComponentUVE& value) noexcept {
    return IsBounded3DNodeStringUVE(value.spawnTag, false) && IsFinite3DNodeVectorUVE(value.localPosition) &&
           IsFinite3DNodeQuaternionUVE(value.localRotation);
}

} // namespace UVE::Scene
