// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "entity.h"
#include "i_entity_manager.h"
#include "gameplay_input.h"

namespace UVE::Scene {

// ---- Pawns and controllers -------------------------------------------------------------
//
// The possession kernel of the gameplay object model: a Controller drives at most one Pawn, a
// Pawn answers to at most one Controller, and the link is always mutual or absent - never
// one-sided. PossessController() steals both sides cleanly (the previous pawn is unlinked
// with its input purged, the previous controller is unlinked), so re-possession at runtime is an
// ordinary call, not surgery.
//
// The polled keyboard/gamepad snapshot routes to the pawns of Player controllers only; AI
// controllers own their pawns' input themselves: brains select action ids, and mapping those
// to input is game code's, never the engine's. Routing also
// self-heals: a link whose other side lost its component is cleared, and an orphaned pawn's
// input is purged, so a destroyed controller can never leave a pawn running on stale input.
//
// Possession changes also queue lifecycle events: PossessionLifecycleTracker diffs the mutual
// links once per frame (the engine owns one, beside the area and hitbox trackers), so gameplay
// can cut cameras and swap HUDs on control changes. And the
// player-look/interact/character flow resolves its player through possession (a maintained
// self-possession fill keeps possessOnPlay bodies driven), and character movement steers from
// pawn input - Player-kind faced by the body's own yaw, AI-kind raw. Unpossessed bodies stand
// still: the no-player-drives-everyone quirk died with the flag-based resolution.

enum class ControllerKind : std::uint8_t {
    /// Driven by the polled input snapshot through RouteGameplayInput().
    Player = 0U,
    /// Driven by AI; routing repairs its links but never touches its pawn's input.
    AI = 1U,
};

struct ControllerComponent final {
    Entity pawn = kInvalidEntity;
    ControllerKind kind = ControllerKind::Player;

    [[nodiscard]] bool operator==(const ControllerComponent&) const = default;
};

/// A known controller kind. The pawn link is dynamic, not structural: any value validates.
[[nodiscard]] bool IsControllerComponentValid(const ControllerComponent& value) noexcept;

struct PawnComponent final {
    Entity controller = kInvalidEntity;
    Gameplay::GameplayInput input;

    [[nodiscard]] bool operator==(const PawnComponent&) const = default;
};

/// Finite input numbers. The controller link is dynamic, not structural: any value validates.
[[nodiscard]] bool IsPawnComponentValid(const PawnComponent& value) noexcept;

/// Drives `pawnEntity` from `controllerEntity`, stealing both sides: the controller's previous
/// pawn (if any) is unlinked with its input purged, and the pawn's previous controller (if any)
/// is unlinked. Re-possessing an already-mutual pair is a no-op success. False unless both
/// entities carry their respective components.
[[nodiscard]] bool PossessController(IEntityManager& entityManager, Entity controllerEntity,
                                       Entity pawnEntity);

/// Unlinks `controllerEntity` from its pawn (if any), purging the pawn's input. False when the
/// entity carries no controller or drives nothing.
[[nodiscard]] bool UnpossessController(IEntityManager& entityManager, Entity controllerEntity);

/// The pawn `controllerEntity` mutually drives, or invalid for a missing component, an empty
/// link, a pawn without its component, or a one-sided link. Pure: repairs are Route's job.
[[nodiscard]] Entity FindPossessedPawn(const IEntityManager& entityManager,
                                             Entity controllerEntity);

/// The controller mutually driving `pawnEntity`, or invalid under the same fail-closed rules.
[[nodiscard]] Entity FindPawnController(const IEntityManager& entityManager, Entity pawnEntity);

enum class PossessionTransitionKind : std::uint8_t {
    Possessed = 0U,
    Unpossessed = 1U,
};

struct PossessionLink final {
    Entity controller = kInvalidEntity;
    Entity pawn = kInvalidEntity;

    [[nodiscard]] bool operator==(const PossessionLink&) const = default;
};

struct PossessionTransition final {
    PossessionTransitionKind kind = PossessionTransitionKind::Possessed;
    Entity controller = kInvalidEntity;
    Entity pawn = kInvalidEntity;

    [[nodiscard]] bool operator==(const PossessionTransition&) const = default;
};

struct PossessionLifecycleReport final {
    std::size_t previousLinkCount = 0U;
    std::size_t currentLinkCount = 0U;
    std::vector<PossessionTransition> transitions;
};

/// Tracks mutual controller/pawn links between frames. The tracker owns only copied entity ids
/// and never publishes events, mutates ECS state, or possesses anything itself. Every mutual link
/// is tracked - links are few, so there is no cap to truncate against. Reset() discards
/// the baseline and emits nothing itself, so links that are still live at the next Update
/// report as Possessed - they are new to the empty baseline.
class PossessionLifecycleTracker final {
public:
    /// Diffs the current mutual links against the baseline: new links report Possessed first (in
    /// link order), broken links report Unpossessed after (in link order), then the baseline
    /// becomes the current set. Idempotent: no link change, no transitions.
    [[nodiscard]] PossessionLifecycleReport Update(IEntityManager& entityManager);

    void Reset() noexcept;

    [[nodiscard]] std::size_t GetActiveCount() const noexcept { return m_activeLinks.size(); }

private:
    std::vector<PossessionLink> m_activeLinks;
};

/// Delivers `snapshot` to every live pawn of a Player controller, in (index, generation) order,
/// then sweeps every pawn: links whose other side is gone (dead entity, lost component, or
/// never linked back) are
/// cleared, and orphaned pawns have their input purged. A non-finite snapshot still repairs but
/// never writes, so routing can never invalidate a valid pawn.
void RouteGameplayInput(IEntityManager& entityManager,
                           const Gameplay::GameplayInput& snapshot);

} // namespace UVE::Scene
