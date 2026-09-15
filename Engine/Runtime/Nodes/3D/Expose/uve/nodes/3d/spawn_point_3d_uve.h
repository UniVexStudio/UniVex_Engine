// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

struct SpawnPoint3DNodeComponentUVE final {
    std::string spawnTag = "spawn";
    Math::Vector3UVE localPosition{};
    Math::QuaternionUVE localRotation{};
    bool enabled = true;
    bool oneShot = false;
};

[[nodiscard]] bool IsSpawnPoint3DNodeComponentValidUVE(const SpawnPoint3DNodeComponentUVE& value) noexcept;

} // namespace UVE::Scene
