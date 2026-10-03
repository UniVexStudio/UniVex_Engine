// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>

#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

struct NavMeshVolume3DComponentUVE final {
    Math::Vector3UVE boundsHalfExtents{10.0F, 2.0F, 10.0F};
    std::string navigationMeshAssetPath;
    std::uint32_t navigationLayers = 1U;
    bool enabled = true;
    bool rebuildRequested = false;
};

[[nodiscard]] bool IsNavMeshVolume3DObjectComponentValidUVE(const NavMeshVolume3DComponentUVE& value) noexcept;

} // namespace UVE::Scene
