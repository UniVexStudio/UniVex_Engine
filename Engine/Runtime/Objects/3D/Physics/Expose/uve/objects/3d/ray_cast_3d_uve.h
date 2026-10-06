// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "uve/objects/3d/object_3d_common_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Scene {

inline constexpr std::size_t kMaximumRayCastExclusionsUVE = 8U;

struct RayCast3DComponentUVE final {
    Math::Vector3UVE direction{0.0F, -1.0F, 0.0F};
    float length = 100.0F;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    bool enabled = true;
    // Other entities this ray refuses to hit, on top of its own entity (which is always excluded -
    // a ray never hits the collider it starts inside). Real entity references, not raw handles: the
    // scene serializer remaps them through its file-local id table (the same mechanism the
    // visibility parent, the hierarchy parent and an animation target use), so an exclusion still
    // points at the same object after a save/load and one that names an entity outside the file
    // being saved is dropped rather than written as a number that would mean something else on the
    // next load.
    //
    // The list is a PREFIX: slot 0 is the first exclusion, and an empty slot
    // (kInvalidEntityUVE - which is what every untouched slot holds, because a default
    // EntityUVE is index 0, a real slot in a real pool) ends the list. CountRayCast3DExclusionsUVE()
    // is the one place that reads "how many are live", and the validator refuses a live reference
    // that trails an empty one, so every consumer sees the same list. There is deliberately no
    // separate count member: with real references in the slots the count is already written down -
    // in the array - and a second copy of it is a second thing to keep in sync.
    std::array<EntityUVE, kMaximumRayCastExclusionsUVE> exclusions =
        MakeEmptyEntityReferencesUVE<kMaximumRayCastExclusionsUVE>();
    // Runtime-only result state below, refreshed every frame by SyncRayCast3DObjectsUVE() - never
    // serialized (mirrors CharacterControllerComponentUVE's own authored-config/runtime-state
    // split).
    bool hit = false;
    Math::Vector3UVE hitPosition{};
    Math::Vector3UVE hitNormal{};
    EntityUVE hitEntity = kInvalidEntityUVE;
};

/// How many exclusion slots a ray actually uses: the number of live references before the first
/// empty slot. The validator guarantees the list is dense (nothing live trails an empty slot), so
/// this prefix length is the whole list - what the engine hands to a raycast query, what the
/// serializer writes out, and what the Inspector counts.
[[nodiscard]] std::size_t CountRayCast3DExclusionsUVE(const RayCast3DComponentUVE& value) noexcept;

/// Authored-data rule: a finite, non-degenerate direction, a finite positive length, and an
/// exclusion list that is dense and free of duplicates. Runtime result fields do not affect it,
/// except that a claimed hit must name an entity and be finite.
[[nodiscard]] bool IsRayCast3DObjectComponentValidUVE(const RayCast3DComponentUVE& value) noexcept;

class RayCast3DUVE final {
public:
    [[nodiscard]] static bool IsCastingUVE(const RayCast3DComponentUVE& rayCast) noexcept;

    [[nodiscard]] static std::size_t ExclusionCountUVE(const RayCast3DComponentUVE& rayCast) noexcept;

    [[nodiscard]] static std::span<const EntityUVE> ExclusionSpanUVE(const RayCast3DComponentUVE& rayCast) noexcept;

    [[nodiscard]] static bool AcceptsTargetUVE(EntityUVE rayEntity, const RayCast3DComponentUVE& rayCast,
                                               EntityUVE obstacle) noexcept;

    [[nodiscard]] static Math::Vector3UVE ResolveWorldDirectionUVE(
        const Math::Vector3UVE& localDirection, const Math::QuaternionUVE& worldRotation) noexcept;

    static void ClearResultUVE(RayCast3DComponentUVE& rayCast) noexcept;

    static void RecordHitUVE(RayCast3DComponentUVE& rayCast, EntityUVE hitEntity,
                             const Math::Vector3UVE& hitPosition, const Math::Vector3UVE& hitNormal) noexcept;
};

} // namespace UVE::Scene
