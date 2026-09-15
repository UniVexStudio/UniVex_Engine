// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/world/world_uve.h"

#include <cmath>

#include "uve/debug/assert_uve.h"

namespace UVE::World {

WorldUVE::WorldUVE(Memory::IAllocatorUVE& allocator, Events::IEventSystemUVE& eventSystem)
    : m_entityManager(allocator, eventSystem) {}

void WorldUVE::TickUVE(float deltaTimeSeconds) {
    UVE_ASSERT(std::isfinite(deltaTimeSeconds));
    UVE_ASSERT(deltaTimeSeconds >= 0.0F);
    const float safeDeltaTimeSeconds = std::isfinite(deltaTimeSeconds) && deltaTimeSeconds >= 0.0F
                                            ? deltaTimeSeconds
                                            : 0.0F;

    ++m_frameCount;
    m_totalTimeSeconds += safeDeltaTimeSeconds;

    m_sceneGraph.UpdateUVE(m_entityManager);
}

} // namespace UVE::World
