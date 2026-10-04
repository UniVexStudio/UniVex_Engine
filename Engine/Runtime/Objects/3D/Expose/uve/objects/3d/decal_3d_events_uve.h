// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>

#include "uve/component/entity_uve.h"

namespace UVE::Scene {

/// Queued once, on the step a decal's lifetime runs out.
///
/// The decal itself does not delete anything: it stops painting and reports the edge, and the
/// system that spawned it decides what an expired decal means - nothing, for a permanent scorch
/// mark an author placed; a freed pool slot, for a bullet hole a weapon spawned. This is the same
/// split the strike and projectile events use: the engine owns the edge, gameplay owns the
/// consequence. The material path travels with the event because the entity may already be gone by
/// the time a pooled system collects the report.
struct Decal3DExpiredEventUVE final {
    /// The entity the expired decal lives on. Still alive when this is queued; a listener that
    /// wants to remove it can, and one that only wants to recycle its slot needs nothing else.
    EntityUVE decal = kInvalidEntityUVE;
    /// The material the decal was projecting, copied at the moment it expired.
    std::string materialAssetPath;

    [[nodiscard]] bool operator==(const Decal3DExpiredEventUVE&) const = default;
};

} // namespace UVE::Scene
