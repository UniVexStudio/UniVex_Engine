// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

struct Marker3DNodeComponentUVE final {
    std::string markerName = "Marker";
    Math::Vector3UVE localPosition{};
    Math::QuaternionUVE localRotation{};
    bool enabled = true;
};

[[nodiscard]] bool IsMarker3DNodeComponentValidUVE(const Marker3DNodeComponentUVE& value) noexcept;

} // namespace UVE::Scene
