// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

struct VisibilityRegion3DNodeComponentUVE final {
    Math::Vector3UVE halfExtents{10.0F, 10.0F, 10.0F};
    std::uint32_t visibilityLayers = 0xFFFFFFFFU;
    bool enabled = true;
    bool active = true;
};

[[nodiscard]] bool IsVisibilityRegion3DNodeComponentValidUVE(const VisibilityRegion3DNodeComponentUVE& value) noexcept;

} // namespace UVE::Scene
