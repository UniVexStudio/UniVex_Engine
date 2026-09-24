// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/hierarchy_component_uve.h"

#include <atomic>

namespace UVE::Scene {

std::int64_t NextSiblingOrderUVE() noexcept {
    // One counter for the process. Orders are compared only between siblings, so any strictly
    // increasing source works; 64 bits cannot run out in a session.
    static std::atomic<std::int64_t> next{0};
    return next.fetch_add(1, std::memory_order_relaxed) + 1;
}

} // namespace UVE::Scene
