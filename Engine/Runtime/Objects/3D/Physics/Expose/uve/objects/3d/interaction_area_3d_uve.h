// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "uve/component/entity_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

// Hard storage bound for an interaction area's per-tick interactor list - same bounded-list
// discipline as kMaximumHitbox3DStrikesUVE: the array is bounded so the runtime state is
// fixed-size and cache-friendly, and overflow is REPORTED (interactorsTruncated) instead of
// silently pretending extra interactors do not exist. 16 matches both the hitbox list bound and
// the authored maximumCandidates default below.
inline constexpr std::size_t kMaximumInteractionAreaCandidatesUVE = 16U;

struct InteractionArea3DComponentUVE final {
    Math::Vector3UVE halfExtents{1.0F, 1.0F, 1.0F};
    std::uint32_t collisionLayer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    std::string interactionTag = "interactable";
    std::uint32_t maximumCandidates = 16U;
    bool enabled = true;
    // Runtime-only result state below, refreshed every frame by EngineCoreUVE's
    // SyncInteractionArea3DObjectsUVE() (which, like Hitbox3D/RayCast3D, keeps the object's per-frame
    // behavior in the engine core tick - object modules stay pure authoring data) - never
    // serialized, mirroring Hitbox3DComponentUVE's authored-config/runtime-state split.
    //
    // `interactors` lists every character-controller interactor overlapping this area this tick
    // (bounded by kMaximumInteractionAreaCandidatesUVE AND the authored maximumCandidates above);
    // `focusedByPrimaryInteractor` is the single best-candidate verdict for the primary
    // interactor - the Unreal Lyra-style "one focused option per player" rule Godot leaves every
    // game to re-implement by hand. Deliberately NOT done anywhere yet: acting on the focus
    // (prompt UI, an "interact" script binding, focus enter/exit events). That is the gameplay
    // layer this engine does not own yet - real, separate follow-up, not silently faked.
    std::array<EntityUVE, kMaximumInteractionAreaCandidatesUVE> interactors{};
    std::uint8_t interactorCount = 0U;
    bool interactorsTruncated = false;
    bool focusedByPrimaryInteractor = false;
};

[[nodiscard]] bool IsInteractionArea3DObjectComponentValidUVE(const InteractionArea3DComponentUVE& value) noexcept;

// One candidate entry for focus resolution: an overlapping interaction area plus its squared
// center distance to the interactor it is being ranked for.
struct InteractionFocusCandidateUVE final {
    EntityUVE areaEntity{};
    float distanceSquared = 0.0F;
};

// Effective per-tick interactor-list cap for one area: the authored budget, but never more than
// the fixed storage can hold, so this function folds whatever it is given - including 0 - to a
// usable cap. Note that a zero never reaches a live scan: IsInteractionArea3DObjectComponentValidUVE
// refuses `maximumCandidates == 0`, so a zero budget is an invalid component (the area is cleared)
// rather than an area that tracks nobody; switch the area off for that.
[[nodiscard]] std::size_t ResolveInteractionAreaCandidateCapUVE(
    std::uint32_t authoredMaximumCandidates, std::size_t storageBound) noexcept;

// The primary interactor is the first candidate in (index,generation) order - the exact same
// deterministic "first player wins" contract SpawnPoint3D selection uses, so single-player and
// future split-screen scenes both resolve identically every run (never ECS pool order).
[[nodiscard]] std::optional<EntityUVE> ResolvePrimaryInteractorUVE(
    std::span<const EntityUVE> interactorCandidates) noexcept;

// The Lyra-style "best interactable": the overlapping area whose center is nearest to the
// interactor. Equal distances resolve deterministically by (index,generation), never by
// iteration/pool order. Empty input yields no value, so the caller's failure mode is a clean
// "nothing focused" - fail-closed, same as the rest of the 3D object syncs.
[[nodiscard]] std::optional<EntityUVE> ResolveInteractionFocusUVE(
    std::span<const InteractionFocusCandidateUVE> candidates) noexcept;

} // namespace UVE::Scene
