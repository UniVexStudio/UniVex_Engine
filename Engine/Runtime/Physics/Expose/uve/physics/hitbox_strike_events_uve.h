// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/physics/hitbox_strike_uve.h"

namespace UVE::Physics {

/// Queued when a hitbox/hurtbox pairing first appears - the frame a strike *starts*.
///
/// This is the consequence contract item: the engine resolved the pairing and says so, once, with
/// the evidence (what struck what, how deeply, along which axis, on which channel); damage numbers,
/// knockback, i-frames and hit reactions are gameplay, and it decides them from this. `strike` is
/// the copied first-frame evidence: for an exit the numbers are the last-known live ones.
struct Hitbox3DStrikeEnteredEventUVE final {
    Hitbox3DStrikePairUVE strike;

    [[nodiscard]] bool operator==(const Hitbox3DStrikeEnteredEventUVE&) const noexcept = default;
};

/// Queued when a pairing that was live on the previous tick is no longer in the snapshot - the
/// frame a strike *ends* (the boxes separated, either side was disabled or destroyed, the channel
/// changed, or the layer/mask no longer accepts).
///
/// A strike that ends because the hurtbox was destroyed still carries the hurtbox handle and its
/// last-known channel: gameplay that tracks "who am I currently hitting" by this event can drop the
/// entry without a second lookup that would already be too late.
struct Hitbox3DStrikeExitedEventUVE final {
    Hitbox3DStrikePairUVE strike;

    [[nodiscard]] bool operator==(const Hitbox3DStrikeExitedEventUVE&) const noexcept = default;
};

} // namespace UVE::Physics
