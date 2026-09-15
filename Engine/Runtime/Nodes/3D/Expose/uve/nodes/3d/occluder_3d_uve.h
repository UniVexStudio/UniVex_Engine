// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

enum class Occluder3DNodeModeUVE : std::uint8_t {
    ConservativeBox = 0,
};

struct Occluder3DNodeComponentUVE final {
    Math::Vector3UVE halfExtents{2.0F, 2.0F, 2.0F};
    Occluder3DNodeModeUVE mode = Occluder3DNodeModeUVE::ConservativeBox;
    bool enabled = true;
};

[[nodiscard]] bool IsOccluder3DNodeComponentValidUVE(const Occluder3DNodeComponentUVE& value) noexcept;

} // namespace UVE::Scene
