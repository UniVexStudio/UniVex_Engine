// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "entity.h"
#include "i_entity_manager.h"

namespace UVE::Physics {

inline constexpr std::size_t kMaximumAreaOverlapResults = 4096U;
inline constexpr std::size_t kMaximumAreaOverlapQueryAreas = 4096U;

enum class AreaOverlapParticipation : std::uint8_t {
    MonitoringAreas = 0,
    AllValidAreas = 1,
};

struct AreaOverlapPair final {
    Scene::Entity area;
    Scene::Entity other;
    float penetrationDepth = 0.0F;

    [[nodiscard]] bool operator==(const AreaOverlapPair&) const noexcept = default;
};

struct AreaOverlapQueryResult final {
    std::size_t inspectedAreas = 0U;
    std::size_t inspectedColliders = 0U;
    bool truncated = false;
    std::vector<AreaOverlapPair> overlaps;

    [[nodiscard]] bool IsTruncated() const noexcept { return truncated; }
};

class AreaOverlapSystem final {
public:
    [[nodiscard]] static AreaOverlapQueryResult Query(
        Scene::IEntityManager& entityManager,
        std::size_t maximumResults = kMaximumAreaOverlapResults,
        AreaOverlapParticipation participation = AreaOverlapParticipation::MonitoringAreas);
};

} // namespace UVE::Physics
