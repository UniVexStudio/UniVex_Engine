// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "area_overlap_system.h"

namespace UVE::Physics {

/// Queued when an area/collider pair first appears in a complete overlap snapshot.
/// The pair is copied into the event; delivery is owned by IEventSystem.
struct AreaOverlapEnteredEvent final {
    AreaOverlapPair pair;

    [[nodiscard]] bool operator==(const AreaOverlapEnteredEvent&) const noexcept = default;
};

/// Queued when an area/collider pair disappears from a complete overlap snapshot.
/// The pair is the copied last-known overlap evidence; delivery is owned by IEventSystem.
struct AreaOverlapExitedEvent final {
    AreaOverlapPair pair;

    [[nodiscard]] bool operator==(const AreaOverlapExitedEvent&) const noexcept = default;
};

} // namespace UVE::Physics
