// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

enum class DecalProjectionModeUVE : std::uint8_t {
    Box = 0,
    Cylinder,
};

struct Decal3DNodeComponentUVE final {
    std::string materialAssetPath;
    Math::Vector3UVE size{1.0F, 1.0F, 1.0F};
    DecalProjectionModeUVE projection = DecalProjectionModeUVE::Box;
    float lifetime = 0.0F;
    bool enabled = true;
};

[[nodiscard]] bool IsDecal3DNodeComponentValidUVE(const Decal3DNodeComponentUVE& value) noexcept;

} // namespace UVE::Scene
