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

    // What the region is baked for. These are agent measurements rather than mesh ones - the grid
    // the volume is rasterized on, the agent's width used to erode walls and ledges away, its height
    // for headroom, how steep a surface it can climb and how tall a step it can take. Two agent
    // types on one platform need two regions, because a single mesh can only be eroded for one
    // width.
    float cellSize = 0.5F;
    float agentRadius = 0.5F;
    float agentHeight = 1.8F;
    float maximumSlopeDegrees = 45.0F;
    float maximumStepHeight = 0.4F;

    bool enabled = true;
    /// Set from a script, the editor or a tool to ask for a re-rasterization: the navigation runtime
    /// clears it once the bake has run. An author does not need to raise it for a region that was
    /// moved or resized - a bake follows its bounds - but the collision world moving without the
    /// region moving is what it is for.
    bool rebuildRequested = false;
};

[[nodiscard]] bool IsNavMeshVolume3DObjectComponentValidUVE(const NavMeshVolume3DComponentUVE& value) noexcept;

} // namespace UVE::Scene
