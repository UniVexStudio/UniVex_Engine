// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/component/entity_uve.h"

namespace UVE::Gameplay {

/// Queued when a controller/pawn link becomes mutual - the frame's new control relationships.
/// Both ids are live at queue time; possession only ever forms between live entities.
struct PawnPossessedEventUVE final {
    Scene::EntityUVE controller = Scene::kInvalidEntityUVE;
    Scene::EntityUVE pawn = Scene::kInvalidEntityUVE;

    [[nodiscard]] bool operator==(const PawnPossessedEventUVE&) const noexcept = default;
};

/// Queued when a mutual link stops being mutual: unpossessed, stolen, or either side destroyed.
/// The ids are last-known - a side that died mid-frame is already stale, so check IsAliveUVE
/// before following them. A lost link still reports: control dying with its driver is exactly
/// the signal gameplay needs to cut away from the body.
struct PawnUnpossessedEventUVE final {
    Scene::EntityUVE controller = Scene::kInvalidEntityUVE;
    Scene::EntityUVE pawn = Scene::kInvalidEntityUVE;

    [[nodiscard]] bool operator==(const PawnUnpossessedEventUVE&) const noexcept = default;
};

} // namespace UVE::Gameplay
