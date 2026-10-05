// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "uve/component/character_controller_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/character_3d_uve.h"
#include "uve/physics/character_body_motion_uve.h"
#include "uve/physics/detail/collider_world_aabb_cache_uve.h"

namespace UVE::Scene {
class IEntityManagerUVE;
class ISceneGraphUVE;
} // namespace UVE::Scene

namespace UVE::Physics {

class ICollisionSystemUVE;

/// The collision world, as a Character3D sees it.
///
/// Character3DUVE (the node layer) deliberately knows nothing about colliders, broad phases or scene
/// graphs: it moves a body through an ICharacterWorldQueryUVE, which is why the same mover can be
/// unit-tested against a handful of boxes. This is the other half of that seam - the implementation
/// backed by the engine's real collision world.
///
/// What it answers, and how honestly:
///  * Sweeps are conservative. The moving body is taken as its collider's world AABB, the target as
///    its own world AABB, and the query is a ray against the target AABB expanded by the mover's
///    half extents. That is exact for a box character against an unrotated box target, and it can
///    stop a body early - never late, so it cannot tunnel.
///  * The skin is the caller's margin, so queries never report a surface closer than a margin to the
///    body. That is what keeps a resting body's next query well-posed instead of exactly touching.
///  * Overlaps come from the collision system's own narrow phase, so whatever the rest of the engine
///    treats as touching, a character treats as touching too.
///
/// The collider list is cached the first time it is needed and reused for the rest of the step: a
/// step moves one body through a world that is not moving under it, so the list cannot go stale
/// while a move is in progress, and a step that never collides never pays for the cache at all.
class CharacterWorldQueryUVE final : public Scene::ICharacterWorldQueryUVE {
public:
    CharacterWorldQueryUVE(Scene::IEntityManagerUVE& entityManager, ICollisionSystemUVE& collisionSystem,
                           Scene::EntityUVE entity);

    /// An entity this query must never report - the body being carried, a trigger the caller has
    /// already decided to ignore. Never the query's own entity, which is always ignored.
    void SetIgnoredEntityUVE(Scene::EntityUVE entity) noexcept;

    [[nodiscard]] Scene::EntityUVE GetEntityUVE() const noexcept;

    /// True when the entity has what a character needs: a collider, a world transform, and a rigid
    /// body that is either absent or kinematic. Everything else is refused, and a refused query
    /// answers every question with "nothing".
    [[nodiscard]] bool IsMovableCharacterUVE() const noexcept;

    /// The body's world half extents: its collider's, which is what every sweep is built from.
    [[nodiscard]] Math::Vector3UVE GetHalfExtentsUVE() const noexcept;

    [[nodiscard]] Math::Vector3UVE GetCenterUVE() const override;
    [[nodiscard]] std::vector<Scene::CharacterSlideCollisionUVE> GetOverlapsUVE() const override;
    [[nodiscard]] std::optional<Scene::CharacterSlideCollisionUVE> SweepUVE(
        const Math::Vector3UVE& center, const Math::Vector3UVE& motion, float margin) const override;
    [[nodiscard]] Math::Vector3UVE GetSurfaceVelocityUVE(Scene::EntityUVE entity) const override;
    [[nodiscard]] std::optional<Math::Vector3UVE> TryGetEntityCenterUVE(
        Scene::EntityUVE entity) const override;

private:
    /// The layer/mask test the collision system itself applies to a pair, both directions: the
    /// character has to accept the target's layer, and the target has to accept the character's.
    [[nodiscard]] bool IsCollidableUVE(const Detail::ColliderWorldAabbUVE& collider) const noexcept;

    /// Kept as a reference to the entity manager so the cache can be built lazily; the entity
    /// manager outlives the query, which lives for one step.
    Scene::IEntityManagerUVE* m_entityManager = nullptr;
    ICollisionSystemUVE* m_collisionSystem = nullptr;
    Scene::EntityUVE m_entity{};
    Scene::EntityUVE m_ignoredEntity{};
    Math::Vector3UVE m_center{};
    Math::Vector3UVE m_halfExtents{};
    std::uint32_t m_layer = 0U;
    std::uint32_t m_mask = 0U;
    bool m_isMovable = false;
    mutable std::optional<std::vector<Detail::ColliderWorldAabbUVE>> m_colliders;
};

/// Why StepCharacter3DUVE() refused to move a body, or that it moved one.
enum class Character3DStepCodeUVE : std::uint8_t {
    Stepped = 0,
    /// The step's duration was zero, negative or not finite.
    InvalidDeltaTime,
    /// No such entity.
    UnknownEntity,
    /// The entity is not a character: no CharacterControllerComponentUVE.
    NotAController,
    /// A character needs a collider to collide with, and a transform to be somewhere.
    MissingCollider,
    MissingTransform,
    /// A rigid body that is not kinematic is driven by forces. A character owns its own motion.
    NonKinematicBody,
    /// The move ran but the world could not describe the body (non-finite state or transforms).
    InvalidWorld,
    /// The step ran and the move stopped for a reason of its own - a wall, a ceiling, a body out of
    /// slides. The move's own report says which.
    Blocked,
};

/// The step's outcome: the code, and the mover's own report when the move ran at all.
struct Character3DStepResultUVE final {
    Character3DStepCodeUVE code = Character3DStepCodeUVE::UnknownEntity;
    /// True when the whole step ran, from intent through the component write-back.
    bool stepped = false;
    /// True when the built-in movement jumped this step.
    bool jumped = false;
    /// The mover's report: contacts, per-source motion breakdown, floor/wall/ceiling state,
    /// platform bookkeeping.
    Scene::CharacterMotionResultUVE motion{};
};

/// One fixed step of one Character3D, start to finish.
///
/// The order is the one a player feels, and it is the whole point of having this function at all:
///
///  1. the world transforms are brought up to date, so the move is queried against the world as it
///     is this step rather than as it was last step;
///  2. the body's own intent is resolved - built-in movement steering (or, with that off, the
///     velocity a script set), gravity, jump buffer, coyote time, air control;
///  3. the character's author settings become the mover's configuration, with one deliberate
///     correction: the mover is told the body *was* on the floor, because a jump clears that flag
///     and a body that was standing a moment ago is still allowed to step up and to leave a platform;
///  4. the move runs through the collision world above;
///  5. the finished state is written back into the component, and the motion the move applied is
///     added to the entity's transform - through the scene graph, so the world transforms the next
///     step reads are dirty and get recomputed.
///
/// A body that cannot be stepped is refused with a code and left *exactly* where it was: no half
/// moves, no velocity quietly zeroed. `gravityY` is the engine's gravity as a signed y, so a world
/// that flips gravity passes the flip along and nothing here needs to know about it.
[[nodiscard]] Character3DStepResultUVE StepCharacter3DUVE(
    Scene::IEntityManagerUVE& entityManager, Scene::ISceneGraphUVE& sceneGraph,
    ICollisionSystemUVE& collisionSystem, Scene::EntityUVE entity, const CharacterMotionInputUVE& input,
    float gravityY, float deltaTimeSeconds);

/// How a Character3D pushes the rigid bodies it runs into. Mirrors the authored Pushing settings on
/// the controller; a policy with `enabled` false does nothing at all.
struct CharacterDynamicPushPolicyUVE final {
    bool enabled = false;
    /// How hard the character pushes, relative to how fast it was going into the surface.
    float strength = 1.0F;
    /// The fastest a push may send a body, in metres per second. Zero or negative asks for the
    /// engine's own fallback rather than for no push at all.
    float maximumSpeed = 5.0F;
    /// The step duration the push is spread over.
    float deltaTimeSeconds = 1.0F / 60.0F;
};

/// Applies a character's push to every rigid body its move ran into this step, and returns how many
/// bodies it pushed.
///
/// The push is a velocity change away from the character, sized by how much of the character's
/// motion the surface removed and capped by the policy's speed, exactly as the controller's own
/// Pushing settings promise: a body already travelling away faster than the push is left alone
/// rather than slowed down, and walking into something squares off the shove instead of adding to
/// whatever the body was already doing sideways. One push per body per step, however many of its
/// faces the character touched.
///
/// Static geometry, kinematic bodies, massless bodies and non-finite state are all left untouched.
[[nodiscard]] std::size_t PushBodiesFromCharacterMoveUVE(
    Scene::IEntityManagerUVE& entityManager,
    const std::vector<Scene::CharacterSlideCollisionUVE>& contacts,
    const CharacterDynamicPushPolicyUVE& policy);

} // namespace UVE::Physics
