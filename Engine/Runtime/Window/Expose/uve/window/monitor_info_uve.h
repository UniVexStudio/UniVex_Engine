// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <string>

namespace UVE::Window {

/// One entry returned by IWindowManagerUVE::EnumerateMonitorsUVE() — a snapshot, not a live
/// handle; re-enumerate to observe monitor hot-plug changes. Exists for future use (multi-monitor
/// window placement, fullscreen target selection); nothing in this increment consumes it beyond
/// exposing it.
struct MonitorInfoUVE {
    std::string name;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool isPrimary = false;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t refreshRate = 0U;
    float contentScaleX = 1.0F;
    float contentScaleY = 1.0F;
};

} // namespace UVE::Window
