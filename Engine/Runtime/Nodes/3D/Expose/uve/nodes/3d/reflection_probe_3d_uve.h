// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

enum class ReflectionProbeUpdateModeUVE : std::uint8_t {
    Once = 0,
    EveryFrame,
    OnDemand,
};

struct ReflectionProbe3DNodeComponentUVE final {
    Math::Vector3UVE size{5.0F, 5.0F, 5.0F};
    std::uint32_t visibilityLayers = 0xFFFFFFFFU;
    ReflectionProbeUpdateModeUVE updateMode = ReflectionProbeUpdateModeUVE::Once;
    bool updateRequested = false;
    bool enabled = true;
};

[[nodiscard]] bool IsReflectionProbe3DNodeComponentValidUVE(const ReflectionProbe3DNodeComponentUVE& value) noexcept;

} // namespace UVE::Scene
