// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Tests for the Character3D runtime: the node-layer mover (Character3D) against a fake world it
// can be reasoned about exactly, and then the same mover against the engine's real collision world
// through the Physics-layer bridge (CharacterWorldQuery / StepCharacter3D).
//
// The two halves are deliberately tested apart. The mover's tests use a point body with exact
// arithmetic so a failure names its own cause; the bridge's tests use real components, the real
// collision system and the real scene graph, so a failure means the engine and the mover disagree.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "collider_component.h"
#include "mesh_component.h"
#include "primitive_mesh_component.h"
#include "character_controller_component.h"
#include "physics_object_component.h"
#include "predicted_character_component.h"
#include "process_component.h"
#include "rigid_3d_component.h"
#include "solid_body_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "aabb.h"
#include "vector3.h"
#include "memory_manager.h"
#include "character_3d.h"
#include "collider_3d.h"
#include "character_world_query.h"
#include "collision_system.h"
#include "scene_graph.h"

namespace UVE::Physics::Tests {
namespace {

using Scene::Character3D;
using Scene::CharacterMotionConfig;
using Scene::CharacterMotionResult;
using Scene::CharacterMotionState;
using Scene::CharacterMoveCode;
using Scene::CharacterSlideCollision;
using Scene::CharacterSurfaceKind;

constexpr float kTolerance = 1.0e-4F;

[[nodiscard]] Scene::Entity MakeEntity(const std::uint32_t index) noexcept {
    Scene::Entity entity;
    entity.index = index;
    entity.generation = 1U;
    return entity;
}

// =================================================================================================
// A world made of boxes.
//
// The character is a point (a zero-extent box) by default, which is what makes the arithmetic in
// these tests exact: every contact distance is a distance between a point and a face. Tests that
// need a real body size set `halfExtents`.
// =================================================================================================

struct FakeBox final {
    Scene::Entity entity;
    Math::Vector3 min;
    Math::Vector3 max;
};

class FakeCharacterWorld final : public Scene::ICharacterWorldQuery {
public:
    Math::Vector3 center{};
    Math::Vector3 halfExtents{};
    std::vector<FakeBox> boxes;
    std::vector<CharacterSlideCollision> overlaps;
    std::unordered_map<std::uint32_t, Math::Vector3> positions;
    std::unordered_map<std::uint32_t, Math::Vector3> velocities;
    /// Hits to report ahead of the real geometry, consumed one per sweep - for testing contact
    /// shapes (a slope, a moving surface) that axis-aligned boxes cannot express.
    mutable std::vector<CharacterSlideCollision> scriptedHits;
    mutable std::size_t scriptedHitIndex = 0U;

    [[nodiscard]] Math::Vector3 GetCenter() const override {
        return center;
    }

    [[nodiscard]] std::vector<CharacterSlideCollision> GetOverlapsAt(
        const Math::Vector3& sweepCenter) const override {
        std::vector<CharacterSlideCollision> result;
        if (Math::IsFinite(sweepCenter)) {
            const Math::Aabb moving = Math::Aabb::FromCenterExtents(sweepCenter, halfExtents);
            for (const FakeBox& box : boxes) {
                const std::optional<Math::Penetration> penetration =
                    Math::ComputePenetration(moving, Math::Aabb{box.min, box.max});
                if (!penetration.has_value() || !std::isfinite(penetration->depth) ||
                    penetration->depth <= 0.0F) {
                    continue;
                }
                CharacterSlideCollision overlap;
                overlap.entity = box.entity;
                overlap.normal = penetration->axis * -1.0F;
                overlap.position = sweepCenter;
                overlap.depth = penetration->depth;
                result.push_back(overlap);
            }
        }
        // Scripted overlaps describe the query's own pose, not a pose the mover has already left.
        if (sweepCenter == center) {
            result.insert(result.end(), overlaps.begin(), overlaps.end());
        }
        return result;
    }

    [[nodiscard]] std::optional<CharacterSlideCollision> Sweep(
        const Math::Vector3& sweepCenter, const Math::Vector3& motion,
        const float margin) const override {
        if (scriptedHitIndex < scriptedHits.size()) {
            CharacterSlideCollision hit = scriptedHits[scriptedHitIndex];
            ++scriptedHitIndex;
            const float motionLength = Math::Length(motion);
            hit.travel = std::min(hit.travel, std::max(0.0F, motionLength - margin));
            return hit;
        }
        const float motionLength = Math::Length(motion);
        if (!std::isfinite(motionLength) || motionLength <= 0.0F) {
            return std::nullopt;
        }
        const Math::Aabb moving = Math::Aabb::FromCenterExtents(sweepCenter, halfExtents);
        std::optional<CharacterSlideCollision> best;
        float bestDistance = std::numeric_limits<float>::infinity();
        for (const FakeBox& box : boxes) {
            const std::optional<Math::SweptAabbHit> swept =
                Math::SweepAabb(moving, motion, Math::Aabb{box.min, box.max});
            if (!swept.has_value()) {
                continue;
            }
            const float distance = swept->time * motionLength;
            const bool closer = distance < bestDistance;
            const bool tied = distance == bestDistance && best.has_value() &&
                              box.entity.index < best->entity.index;
            if (!closer && !tied) {
                continue;
            }
            CharacterSlideCollision hit;
            hit.entity = box.entity;
            // The engine's swept-AABB normal is the direction the body was moving; the mover's
            // contract is the surface normal pointing back at the body.
            hit.normal = -swept->normal;
            hit.position = sweepCenter + motion * swept->time;
            hit.travel = std::max(0.0F, distance - margin);
            bestDistance = distance;
            best = hit;
        }
        return best;
    }

    [[nodiscard]] Math::Vector3 GetSurfaceVelocity(const Scene::Entity entity) const override {
        const auto found = velocities.find(entity.index);
        return found != velocities.end() ? found->second : Math::Vector3{};
    }

    [[nodiscard]] std::optional<Math::Vector3> TryGetEntityCenter(
        const Scene::Entity entity) const override {
        const auto found = positions.find(entity.index);
        if (found == positions.end()) {
            return std::nullopt;
        }
        return found->second;
    }
};

[[nodiscard]] CharacterMotionConfig DefaultConfig() noexcept {
    CharacterMotionConfig config;
    // A hair of skin, the same order the engine's own controller uses: a resting body stays
    // well-posed and contacts never land exactly on a face.
    config.safeMargin = 0.001F;
    return config;
}

void AddBox(FakeCharacterWorld& world, const Scene::Entity entity, const Math::Vector3& min,
               const Math::Vector3& max) {
    world.boxes.push_back(FakeBox{entity, min, max});
}

void AddFloor(FakeCharacterWorld& world, const float topY,
                 const Scene::Entity entity = MakeEntity(1U)) {
    AddBox(world, entity, {-100.0F, topY - 1.0F, -100.0F}, {100.0F, topY, 100.0F});
}

void AddWall(FakeCharacterWorld& world, const float faceX, const float nearY = -100.0F,
                const float farY = 100.0F, const Scene::Entity entity = MakeEntity(2U)) {
    AddBox(world, entity, {faceX, nearY, -100.0F}, {faceX + 1.0F, farY, 100.0F});
}

void AddCeiling(FakeCharacterWorld& world, const float undersideY, const float fromX = -100.0F,
                   const float toX = 100.0F, const Scene::Entity entity = MakeEntity(3U)) {
    AddBox(world, entity, {fromX, undersideY, -100.0F}, {toX, undersideY + 1.0F, 100.0F});
}

void ExpectNear(const Math::Vector3& actual, const Math::Vector3& expected, const float tolerance) {
    EXPECT_NEAR(actual.x, expected.x, tolerance) << "x";
    EXPECT_NEAR(actual.y, expected.y, tolerance) << "y";
    EXPECT_NEAR(actual.z, expected.z, tolerance) << "z";
}

// =================================================================================================
// Surface classification: what the body is touching, and at what angle.
// =================================================================================================

TEST(Character3DSurfaceTest, AFloorIsWhatTheBodyCanStandOnAndAWallIsWhatItCannot) {
    float angle = 0.0F;
    EXPECT_EQ(Scene::ClassifyCharacterSurface({0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 45.0F, angle), CharacterSurfaceKind::Floor);
    EXPECT_NEAR(angle, 0.0F, kTolerance);

    EXPECT_EQ(Scene::ClassifyCharacterSurface({0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F}, 45.0F, angle), CharacterSurfaceKind::Wall);
    EXPECT_NEAR(angle, 90.0F, kTolerance);

    EXPECT_EQ(Scene::ClassifyCharacterSurface({0.0F, -1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 45.0F, angle), CharacterSurfaceKind::Ceiling);
    EXPECT_NEAR(angle, 180.0F, kTolerance);
}

TEST(Character3DSurfaceTest, TheFloorLimitIsInclusiveSoARampExactlyAtItIsStillWalked) {
    const Math::Vector3 exactlyAtTheLimit{0.0F, 1.0F, -1.0F};
    float angle = 0.0F;
    EXPECT_EQ(Scene::ClassifyCharacterSurface(exactlyAtTheLimit, {0.0F, 1.0F, 0.0F}, 45.0F, angle),
              CharacterSurfaceKind::Floor);
    EXPECT_NEAR(angle, 45.0F, 1.0e-3F);

    // A hair steeper, and it is a wall.
    EXPECT_EQ(Scene::ClassifyCharacterSurface({0.0F, 1.0F, -1.01F}, {0.0F, 1.0F, 0.0F}, 45.0F, angle),
              CharacterSurfaceKind::Wall);
    EXPECT_GT(angle, 45.0F);
}

TEST(Character3DSurfaceTest, ADegenerateNormalIsNotASurfaceAndZeroesTheAngle) {
    float angle = 123.0F;
    EXPECT_EQ(Scene::ClassifyCharacterSurface({0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 45.0F, angle),
              CharacterSurfaceKind::Invalid);
    EXPECT_NEAR(angle, 0.0F, kTolerance);
    EXPECT_EQ(Scene::ClassifyCharacterSurface({0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 0.0F, angle),
              CharacterSurfaceKind::Floor);
}

TEST(Character3DSurfaceTest, SlopeAngleIsMeasuredFromUpAndSurvivesBadInput) {
    EXPECT_NEAR(Character3D::SlopeAngleDegrees({0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}), 0.0F, kTolerance);
    EXPECT_NEAR(Character3D::SlopeAngleDegrees({0.0F, -1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}), 180.0F, kTolerance);
    EXPECT_NEAR(Character3D::SlopeAngleDegrees({0.0F, 1.0F, -1.0F}, {0.0F, 1.0F, 0.0F}), 45.0F, 1.0e-3F);
    EXPECT_NEAR(Character3D::SlopeAngleDegrees({0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}), 0.0F, kTolerance);
}

// =================================================================================================
// Walking: sliding, stopping, stepping, snapping and riding, all against the box world above.
// =================================================================================================

TEST(Character3DMoverTest, AFreeMoveGoesWhereItWasAskedAndTouchesNothing) {
    FakeCharacterWorld world;
    CharacterMotionState state;
    state.velocity = {1.0F, 0.0F, 0.0F};
    const CharacterMotionResult result =
        Character3D::MoveAndSlide(world, DefaultConfig(), state, {1.0F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_EQ(result.code, CharacterMoveCode::Moved);
    EXPECT_FALSE(result.blocked);
    EXPECT_FALSE(result.onFloor);
    EXPECT_FALSE(result.onWall);
    EXPECT_FALSE(result.onCeiling);
    EXPECT_EQ(result.slideCount, 0U);
    EXPECT_EQ(result.contactCount, 0U);
    EXPECT_EQ(result.GetCollisionCount(), 0U);
    ExpectNear(result.appliedMotion, {1.0F, 0.0F, 0.0F}, 1.0e-5F);
    ExpectNear(result.finalCenter, {1.0F, 0.0F, 0.0F}, 1.0e-5F);
    ExpectNear(state.lastMotion, {1.0F, 0.0F, 0.0F}, 1.0e-5F);
    // Real velocity is what the world allowed, not what was asked for: here, the same thing.
    ExpectNear(state.realVelocity, {10.0F, 0.0F, 0.0F}, 1.0e-3F);
    EXPECT_FALSE(result.carriedByPlatform);
    EXPECT_FALSE(result.depenetrated);
    EXPECT_FALSE(result.configClamped);
    EXPECT_FALSE(result.snapUsed);
    EXPECT_EQ(result.GetCollision(0U), nullptr);
}

TEST(Character3DMoverTest, AWallStopsTheBodyASkinShortOfItAndSaysWhichWall) {
    FakeCharacterWorld world;
    AddWall(world, 1.0F);
    CharacterMotionState state;
    state.velocity = {2.0F, 0.0F, 0.0F};
    const CharacterMotionResult result =
        Character3D::MoveAndSlide(world, DefaultConfig(), state, {2.0F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_TRUE(result.blocked);
    EXPECT_TRUE(result.onWall);
    EXPECT_FALSE(result.onFloor);
    ExpectNear(state.lastMotion, {0.999F, 0.0F, 0.0F}, 2.0e-3F);
    ExpectNear(result.wallNormal, {-1.0F, 0.0F, 0.0F}, 1.0e-3F);
    // One wall, one contact, however many slides it took to stop against it.
    EXPECT_EQ(result.contactCount, 1U);
    ASSERT_EQ(result.collisions.size(), 1U);
    EXPECT_EQ(result.GetCollision(0U)->entity.index, 2U);
    EXPECT_FALSE(result.GetCollision(0U)->steppedUp);
    EXPECT_NEAR(result.GetCollision(0U)->depth, 0.0F, kTolerance);
}

TEST(Character3DMoverTest, AWallKeepsTheMotionThatWasNotGoingIntoIt) {
    FakeCharacterWorld world;
    AddWall(world, 1.0F);
    CharacterMotionState state;
    state.velocity = {2.0F, 0.0F, 1.0F};
    const CharacterMotionResult result =
        Character3D::MoveAndSlide(world, DefaultConfig(), state, {2.0F, 0.0F, 1.0F}, 0.1F);

    ASSERT_TRUE(result.IsAccepted());
    // x: 1 mm short of the wall. z: the half it travelled getting there, then the half it slid.
    ExpectNear(state.lastMotion, {0.999F, 0.0F, 1.0F}, 3.0e-3F);
    // Blocked means "some of what was asked for did not happen", and the x that the wall ate is
    // exactly that, even though the body kept moving along the wall.
    EXPECT_TRUE(result.blocked);
    EXPECT_TRUE(result.onWall);
    EXPECT_EQ(result.contactCount, 1U);
}

TEST(Character3DMoverTest, ASteepRampIsStoppedAgainstInsteadOfSlidAlong) {
    FakeCharacterWorld world;
    // 50 degrees: past the 45 degree floor limit, inside the 15 degree slide band's far side.
    const Math::Vector3 rampNormal = Math::Normalize(Math::Vector3{0.0F, 1.0F, -1.19F});
    CharacterSlideCollision ramp;
    ramp.entity = MakeEntity(4U);
    ramp.normal = rampNormal;
    ramp.travel = 0.99F;
    world.scriptedHits.push_back(ramp);
    CharacterMotionState state;
    state.velocity = {0.0F, 0.0F, 1.0F};
    const CharacterMotionResult result =
        Character3D::MoveAndSlide(world, DefaultConfig(), state, {0.0F, 0.0F, 1.0F}, 0.1F);

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_TRUE(result.blocked);
    EXPECT_TRUE(result.onWall);
    // The ramp band stops the body dead rather than letting it creep up the slope.
    ExpectNear(state.lastMotion, {0.0F, 0.0F, 0.99F}, 2.0e-3F);
    ExpectNear(result.wallNormal, rampNormal, 1.0e-3F);
}

TEST(Character3DMoverTest, ACeilingStopsTheRiseAndIsSlidAlongWhenThatIsAllowed) {
    // A ceiling a quarter of a metre up, walked under diagonally: the body gets there, and the
    // question is what it is allowed to do afterwards.
    const auto runOnce = [](const bool slideOnCeiling) {
        FakeCharacterWorld world;
        AddCeiling(world, 0.25F);
        CharacterMotionConfig config = DefaultConfig();
        config.slideOnCeiling = slideOnCeiling;
        CharacterMotionState state;
        state.velocity = {1.0F, 1.0F, 0.0F};
        const CharacterMotionResult result =
            Character3D::MoveAndSlide(world, config, state, {1.0F, 1.0F, 0.0F}, 0.1F);
        return std::pair<CharacterMotionResult, Math::Vector3>{result, state.lastMotion};
    };

    // Sliding: the rise stops at the ceiling and the walk keeps every millimetre of its x.
    const auto sliding = runOnce(true);
    ASSERT_TRUE(sliding.first.IsAccepted());
    EXPECT_TRUE(sliding.first.onCeiling);
    ExpectNear(sliding.second, {1.0F, 0.249F, 0.0F}, 3.0e-3F);
    ExpectNear(sliding.first.ceilingNormal, {0.0F, -1.0F, 0.0F}, 1.0e-3F);

    // Not sliding: the ceiling ends the move where it is - the body travelled to it, and no
    // further, sideways motion included.
    const auto blocked = runOnce(false);
    ASSERT_TRUE(blocked.first.IsAccepted());
    EXPECT_TRUE(blocked.first.blocked);
    EXPECT_TRUE(blocked.first.onCeiling);
    ExpectNear(blocked.second, {0.249F, 0.249F, 0.0F}, 3.0e-3F);
    EXPECT_GT(sliding.second.x, blocked.second.x + 0.5F);
}

TEST(Character3DMoverTest, FloorBlockOnWallStopsTheMoveAtTheWallInsteadOfSlidingAlong) {
    // The same diagonal walk, run twice: with the setting off the body slides along the wall to its
    // full z distance; with it on the move ends at the wall, taking only the z it had already
    // travelled when it got there.
    FakeCharacterWorld slidingWorld;
    AddWall(slidingWorld, 1.5F);
    CharacterMotionState slidingState;
    slidingState.grounded = true;
    const CharacterMotionResult sliding =
        Character3D::MoveAndSlide(slidingWorld, DefaultConfig(), slidingState, {2.0F, 0.0F, 1.0F}, 0.1F);
    ASSERT_TRUE(sliding.IsAccepted());
    EXPECT_NEAR(slidingState.lastMotion.z, 1.0F, 3.0e-3F);

    FakeCharacterWorld blockedWorld;
    AddWall(blockedWorld, 1.5F);
    CharacterMotionConfig config = DefaultConfig();
    config.floorBlockOnWall = true;
    CharacterMotionState blockedState;
    blockedState.grounded = true;
    const CharacterMotionResult blocked =
        Character3D::MoveAndSlide(blockedWorld, config, blockedState, {2.0F, 0.0F, 1.0F}, 0.1F);

    ASSERT_TRUE(blocked.IsAccepted());
    EXPECT_TRUE(blocked.blocked);
    EXPECT_NEAR(blockedState.lastMotion.x, 1.499F, 3.0e-3F);
    // The z the body had when it met the wall, and not a millimetre of the z it wanted after that.
    EXPECT_NEAR(blockedState.lastMotion.z, 0.7495F, 3.0e-3F);
    EXPECT_LT(blockedState.lastMotion.z, slidingState.lastMotion.z - 0.2F);
}

TEST(Character3DMoverTest, AWalkingBodyClimbsALowStepInsteadOfStoppingAtIt) {
    FakeCharacterWorld world;
    AddFloor(world, 0.0F);
    // A 0.2 m step, from x = 1 onwards: inside the default 0.3 m step height.
    world.boxes.push_back(FakeBox{MakeEntity(5U), {1.0F, 0.0F, -2.0F}, {3.0F, 0.2F, 2.0F}});
    CharacterMotionState state;
    state.grounded = true;
    state.velocity = {1.0F, 0.0F, 0.0F};

    const CharacterMotionResult result =
        Character3D::MoveAndSlide(world, DefaultConfig(), state, {1.0F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_TRUE(result.stepUpUsed);
    EXPECT_TRUE(result.onFloor);
    EXPECT_FALSE(result.blocked);
    // The whole walk happened: a millimetre of approach, then the step carrying the body up and
    // over, and the body standing a skin above the tread.
    ExpectNear(state.lastMotion, {1.0F, 0.201F, 0.0F}, 3.0e-3F);
    ExpectNear(result.appliedMotion, {1.0F, 0.201F, 0.0F}, 3.0e-3F);
    // The step's own contribution: from where the body was when the step began.
    ExpectNear(result.stepMotion, {0.001F, 0.201F, 0.0F}, 3.0e-3F);
    ASSERT_EQ(result.collisions.size(), 1U);
    EXPECT_TRUE(result.GetCollision(0U)->steppedUp);
    EXPECT_EQ(result.GetCollision(0U)->entity.index, 5U);
}

TEST(Character3DMoverTest, AStepTallerThanTheStepHeightIsAWall) {
    FakeCharacterWorld world;
    AddFloor(world, 0.0F);
    // 0.5 m against a 0.3 m step height: the lift cannot clear it, so it is a wall.
    world.boxes.push_back(FakeBox{MakeEntity(5U), {1.0F, 0.0F, -2.0F}, {3.0F, 0.5F, 2.0F}});
    CharacterMotionState state;
    state.grounded = true;
    state.velocity = {1.0F, 0.0F, 0.0F};

    const CharacterMotionResult result =
        Character3D::MoveAndSlide(world, DefaultConfig(), state, {1.0F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_FALSE(result.stepUpUsed);
    EXPECT_TRUE(result.blocked);
    EXPECT_TRUE(result.onWall);
    ExpectNear(state.lastMotion, {0.999F, 0.0F, 0.0F}, 3.0e-3F);
}

TEST(Character3DMoverTest, AStepWithNoHeadroomIsRefusedRatherThanForced) {
    FakeCharacterWorld world;
    AddFloor(world, 0.0F);
    world.boxes.push_back(FakeBox{MakeEntity(5U), {1.0F, 0.0F, -2.0F}, {3.0F, 0.2F, 2.0F}});
    // A shelf 0.15 m above the body's feet: no room to stand up by a step.
    AddCeiling(world, 0.15F, 0.0F, 4.0F, MakeEntity(6U));
    CharacterMotionState state;
    state.grounded = true;
    state.velocity = {1.0F, 0.0F, 0.0F};

    const CharacterMotionResult result =
        Character3D::MoveAndSlide(world, DefaultConfig(), state, {1.0F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_FALSE(result.stepUpUsed);
    EXPECT_TRUE(result.blocked);
    ExpectNear(state.lastMotion, {0.999F, 0.0F, 0.0F}, 3.0e-3F);
}

TEST(Character3DMoverTest, AStepWhoseOwnMeshIsTheCeilingIsRefused) {
    FakeCharacterWorld world;
    AddFloor(world, 0.0F);
    const Scene::Entity stepAndLintel = MakeEntity(5U);
    // A 0.2 m step, and a lintel 0.15 m above the body's feet that is the same entity: a doorway
    // whose wall and ceiling are one mesh. Rising along the riser is not rising through the roof.
    world.boxes.push_back(FakeBox{stepAndLintel, {1.0F, 0.0F, -2.0F}, {3.0F, 0.2F, 2.0F}});
    world.boxes.push_back(FakeBox{stepAndLintel, {0.0F, 0.15F, -2.0F}, {4.0F, 1.15F, 2.0F}});
    CharacterMotionState state;
    state.grounded = true;
    state.velocity = {1.0F, 0.0F, 0.0F};

    const CharacterMotionResult result =
        Character3D::MoveAndSlide(world, DefaultConfig(), state, {1.0F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_FALSE(result.stepUpUsed);
    EXPECT_TRUE(result.blocked);
    ExpectNear(state.lastMotion, {0.999F, 0.0F, 0.0F}, 3.0e-3F);
}

TEST(Character3DMoverTest, ABodyInTheAirDoesNotStepUpOntoAnything) {
    FakeCharacterWorld world;
    AddFloor(world, -2.0F);
    world.boxes.push_back(FakeBox{MakeEntity(5U), {1.0F, -2.0F, -2.0F}, {3.0F, 0.2F, 2.0F}});
    CharacterMotionState state;
    state.grounded = false; // falling
    state.velocity = {1.0F, -2.0F, 0.0F};

    const CharacterMotionResult result =
        Character3D::MoveAndSlide(world, DefaultConfig(), state, {1.0F, -2.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_FALSE(result.stepUpUsed);
    EXPECT_TRUE(result.blocked);
    // Stopped against the step's face, still in the air, with the rest of the walk thrown away.
    EXPECT_NEAR(state.lastMotion.x, 0.999F, 3.0e-3F);
    EXPECT_LT(result.finalCenter.y, 0.0F);
}

TEST(Character3DMoverTest, AGroundedBodyFollowsTheFloorDownSmallSteps) {
    FakeCharacterWorld world;
    AddFloor(world, 0.0F);
    CharacterMotionState state;
    state.grounded = true;
    state.velocity = {1.0F, 0.0F, 0.0F};
    // Five centimetres above the floor, walking: the floor is close enough to be followed.
    world.center = {0.0F, 0.05F, 0.0F};

    const CharacterMotionResult result =
        Character3D::MoveAndSlide(world, DefaultConfig(), state, {0.1F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_TRUE(result.snapUsed);
    EXPECT_TRUE(result.onFloor);
    // It dropped towards the floor and stopped above it - a snap is a follow, not a drop.
    EXPECT_LT(result.finalCenter.y, 0.02F);
    EXPECT_GT(result.finalCenter.y, -1.0e-3F);
    ExpectNear(result.snapMotion, {0.0F, result.finalCenter.y - 0.05F, 0.0F}, 1.0e-3F);
}

TEST(Character3DMoverTest, ASnapOntoTheFloorIsAContactTheCallerCanRead) {
    FakeCharacterWorld world;
    AddFloor(world, 0.0F, MakeEntity(9U));
    CharacterMotionState state;
    state.grounded = true;
    world.center = {0.0F, 0.05F, 0.0F};

    const CharacterMotionResult result =
        Character3D::MoveAndSlide(world, DefaultConfig(), state, {0.1F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_TRUE(result.snapUsed);
    EXPECT_TRUE(result.onFloor);
    ASSERT_EQ(result.collisions.size(), 1U);
    EXPECT_EQ(result.GetCollision(0U)->entity.index, 9U);
    EXPECT_FALSE(result.GetCollision(0U)->steppedUp);
    ExpectNear(result.GetCollision(0U)->normal, {0.0F, 1.0F, 0.0F}, 1.0e-3F);
}

TEST(Character3DMoverTest, AJumpIsNotSnappedBackDownToTheFloor) {
    FakeCharacterWorld world;
    AddFloor(world, 0.0F);
    CharacterMotionConfig config = DefaultConfig();
    config.jumpedThisStep = true;
    CharacterMotionState state;
    state.grounded = true; // it was standing...
    state.velocity = {0.0F, 5.0F, 0.0F};

    const CharacterMotionResult result =
        Character3D::MoveAndSlide(world, config, state, {0.0F, 0.5F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_FALSE(result.snapUsed);
    EXPECT_FALSE(result.onFloor);
    EXPECT_FALSE(state.grounded);
    ExpectNear(state.lastMotion, {0.0F, 0.5F, 0.0F}, 1.0e-4F);
}

TEST(Character3DMoverTest, ABodyThatWasNotOnTheFloorIsNotSnappedToIt) {
    FakeCharacterWorld world;
    AddFloor(world, 0.0F);
    CharacterMotionState state;
    state.grounded = false;
    world.center = {0.0F, 0.05F, 0.0F};

    const CharacterMotionResult result =
        Character3D::MoveAndSlide(world, DefaultConfig(), state, {0.1F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_FALSE(result.snapUsed);
    EXPECT_FALSE(result.onFloor);
    EXPECT_NEAR(result.finalCenter.y, 0.05F, 1.0e-4F);
}

TEST(Character3DMoverTest, APatformUnderfootCarriesTheBodyAndItsMotion) {
    FakeCharacterWorld world;
    AddFloor(world, 0.0F, MakeEntity(9U));
    world.center = {0.0F, 0.001F, 0.0F};
    world.positions[9U] = {0.0F, 0.0F, 0.0F};
    CharacterMotionState state;
    state.grounded = true;

    // First step: the body stands on the platform, and the move remembers where the platform was.
    const CharacterMotionResult standing =
        Character3D::MoveAndSlide(world, DefaultConfig(), state, {0.0F, 0.0F, 0.0F}, 0.1F);
    ASSERT_TRUE(standing.IsAccepted());
    ASSERT_EQ(state.platform.index, 9U);
    ASSERT_TRUE(state.hasPlatformWorldPosition);
    EXPECT_FALSE(standing.carriedByPlatform);

    // The platform moves half a metre in a 0.1 s step: 5 m/s, and the body comes with it.
    world.positions[9U] = {0.5F, 0.0F, 0.0F};
    const CharacterMotionResult carried =
        Character3D::MoveAndSlide(world, DefaultConfig(), state, {0.0F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(carried.IsAccepted());
    EXPECT_TRUE(carried.carriedByPlatform);
    ExpectNear(carried.platformVelocity, {5.0F, 0.0F, 0.0F}, 1.0e-3F);
    ExpectNear(state.lastMotion, {0.5F, 0.0F, 0.0F}, 2.0e-3F);
    ExpectNear(state.realVelocity, {5.0F, 0.0F, 0.0F}, 2.0e-2F);
    EXPECT_TRUE(carried.onFloor);
    EXPECT_EQ(state.platform.index, 9U);
}

TEST(Character3DMoverTest, LeavingAPlatformHandsOverItsVelocityOrDoesNotAccordingToTheMode) {
    const auto runOnce = [](const Scene::CharacterPlatformLeaveMode mode) {
        FakeCharacterWorld world;
        world.positions[7U] = {10.0F, 3.0F, 0.0F};
        CharacterMotionConfig config = DefaultConfig();
        config.platformOnLeave = mode;
        CharacterMotionState state;
        state.grounded = true;
        state.platform = MakeEntity(7U);
        state.platformWorldPosition = {9.5F, 2.7F, 0.0F};
        state.hasPlatformWorldPosition = true;
        const CharacterMotionResult result =
            Character3D::MoveAndSlide(world, config, state, {0.0F, 0.0F, 0.0F}, 0.1F);
        return std::pair<CharacterMotionResult, Math::Vector3>{result, state.velocity};
    };

    // AddVelocity: the body keeps the platform's whole velocity as it leaves.
    const auto add = runOnce(Scene::CharacterPlatformLeaveMode::AddVelocity);
    EXPECT_TRUE(add.first.leftPlatform);
    EXPECT_FALSE(add.first.onFloor);
    ExpectNear(add.second, {5.0F, 3.0F, 0.0F}, 1.0e-3F);
    // And it was carried by the platform while it was still standing on it.
    EXPECT_TRUE(add.first.carriedByPlatform);

    // AddUpwardVelocity: only the part that pushes the body up, never the sideways motion.
    const auto upward = runOnce(Scene::CharacterPlatformLeaveMode::AddUpwardVelocity);
    EXPECT_TRUE(upward.first.leftPlatform);
    ExpectNear(upward.second, {0.0F, 3.0F, 0.0F}, 1.0e-3F);

    // KeepVelocity: the body keeps only what it had of its own.
    const auto keep = runOnce(Scene::CharacterPlatformLeaveMode::KeepVelocity);
    EXPECT_TRUE(keep.first.leftPlatform);
    ExpectNear(keep.second, {0.0F, 0.0F, 0.0F}, 1.0e-3F);

    // AddVelocity with nothing to hand over: the new platform is gone before the body leaves it.
    FakeCharacterWorld vanishedWorld;
    CharacterMotionState vanishedState;
    vanishedState.grounded = true;
    vanishedState.platform = MakeEntity(7U);
    vanishedState.platformWorldPosition = {9.5F, 2.7F, 0.0F};
    vanishedState.hasPlatformWorldPosition = true;
    const CharacterMotionResult vanished = Character3D::MoveAndSlide(
        vanishedWorld, DefaultConfig(), vanishedState, {0.0F, 0.0F, 0.0F}, 0.1F);
    EXPECT_TRUE(vanished.leftPlatform);
    ExpectNear(vanished.platformVelocity, {0.0F, 0.0F, 0.0F}, 1.0e-5F);
    EXPECT_EQ(vanishedState.platform, Scene::Entity{});
}

TEST(Character3DMoverTest, APlatformThatTeleportsIsTreatedAsStandingStill) {
    FakeCharacterWorld world;
    // Ten metres in one step: 100 m/s, past the 50 m/s teleport guard.
    world.positions[7U] = {10.0F, 0.0F, 0.0F};
    CharacterMotionState state;
    state.grounded = true;
    state.platform = MakeEntity(7U);
    state.platformWorldPosition = {0.0F, 0.0F, 0.0F};
    state.hasPlatformWorldPosition = true;

    const CharacterMotionResult result =
        Character3D::MoveAndSlide(world, DefaultConfig(), state, {0.0F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_FALSE(result.carriedByPlatform);
    // The body did not move, and nothing was handed over when it left: a script writing a
    // transform is not motion, and inheriting it would fling the body across the level.
    ExpectNear(state.lastMotion, {0.0F, 0.0F, 0.0F}, 1.0e-5F);
    ExpectNear(result.platformVelocity, {0.0F, 0.0F, 0.0F}, 1.0e-5F);
    ExpectNear(state.velocity, {0.0F, 0.0F, 0.0F}, 1.0e-5F);
}

TEST(Character3DMoverTest, ABodyThatStartsInsideGeometryIsPushedOutBeforeItWalks) {
    FakeCharacterWorld world;
    CharacterSlideCollision overlap;
    overlap.entity = MakeEntity(8U);
    overlap.normal = {-1.0F, 0.0F, 0.0F};
    overlap.depth = 0.1F;
    world.overlaps.push_back(overlap);
    CharacterMotionState state;

    const CharacterMotionResult result =
        Character3D::MoveAndSlide(world, DefaultConfig(), state, {0.0F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_TRUE(result.depenetrated);
    EXPECT_EQ(result.depenetrationPasses, 1U);
    // Out by the depth plus the skin, and the report says which body pushed it.
    ExpectNear(result.appliedMotion, {-0.101F, 0.0F, 0.0F}, 2.0e-3F);
    ExpectNear(result.depenetrationMotion, {-0.101F, 0.0F, 0.0F}, 2.0e-3F);
    ASSERT_EQ(result.collisions.size(), 1U);
    EXPECT_NEAR(result.GetCollision(0U)->depth, 0.1F, 1.0e-4F);
}

TEST(Character3DMoverTest, ABodyInsideSeveralThingsIsPushedOutDeepestFirst) {
    FakeCharacterWorld world;
    const float depths[3] = {0.05F, 0.30F, 0.10F};
    const std::uint32_t entities[3] = {11U, 12U, 13U};
    for (std::size_t index = 0U; index < 3U; ++index) {
        CharacterSlideCollision overlap;
        overlap.entity = MakeEntity(entities[index]);
        overlap.normal = {0.0F, 1.0F, 0.0F};
        overlap.depth = depths[index];
        world.overlaps.push_back(overlap);
    }
    CharacterMotionState state;

    const CharacterMotionResult result =
        Character3D::MoveAndSlide(world, DefaultConfig(), state, {0.0F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_EQ(result.depenetrationPasses, 1U);
    // Deepest first, and only that one: after the push the working center has left the pose the
    // scripted overlaps described, so they cannot keep pushing.
    ExpectNear(result.depenetrationMotion, {0.0F, 0.301F, 0.0F}, 2.0e-3F);
    ASSERT_GE(result.collisions.size(), 1U);
    EXPECT_EQ(result.GetCollision(0U)->entity.index, 12U);
}

TEST(Character3DMoverTest, DepenetrationRequeriesTheWorkingCenterSoABoxIsLeftBehind) {
    FakeCharacterWorld world;
    world.halfExtents = {0.5F, 0.5F, 0.5F};
    // The body occupies [-0.5, 0.5]; this wall's face sits at x = 0.2, so the body is 0.7 m inside.
    AddBox(world, MakeEntity(8U), {-2.0F, -2.0F, -2.0F}, {0.2F, 2.0F, 2.0F});
    CharacterMotionState state;

    const CharacterMotionResult result =
        Character3D::MoveAndSlide(world, DefaultConfig(), state, {0.0F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_TRUE(result.depenetrated);
    EXPECT_EQ(result.depenetrationPasses, 1U);
    ExpectNear(result.depenetrationMotion, {0.701F, 0.0F, 0.0F}, 2.0e-3F);
    // Asked about the pose it was pushed to, the wall is gone: a second pass would be a ghost.
    EXPECT_TRUE(world.GetOverlapsAt(result.finalCenter).empty());
}

TEST(Character3DMoverTest, ContactsAreCappedAndTheLossIsReported) {
    FakeCharacterWorld world;
    for (std::uint32_t index = 0U; index < 3U; ++index) {
        CharacterSlideCollision overlap;
        overlap.entity = MakeEntity(20U + index);
        overlap.normal = {0.0F, 1.0F, 0.0F};
        overlap.depth = 0.1F + static_cast<float>(index) * 0.01F;
        world.overlaps.push_back(overlap);
    }
    CharacterMotionConfig config = DefaultConfig();
    config.maximumContacts = 1U;
    CharacterMotionState state;

    const CharacterMotionResult result =
        Character3D::MoveAndSlide(world, config, state, {0.0F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_EQ(result.contactsTruncated, true);
    EXPECT_EQ(result.contactCount, 1U);
    EXPECT_EQ(result.droppedContactCount, 2U);
    EXPECT_EQ(result.collisions.size(), 1U);
    // The deepest body is the one worth telling the caller about, so it is the one that survives
    // the cap: 22 is three centimetres deeper than 20.
    EXPECT_EQ(result.GetCollision(0U)->entity.index, 22U);
}

TEST(Character3DMoverTest, ABadConfigStateOrWorldIsRefusedRatherThanGuessed) {
    FakeCharacterWorld world;
    CharacterMotionState state;
    state.velocity = {1.0F, 0.0F, 0.0F};

    // A body with no up direction has no floor, no wall and no ceiling: refused.
    CharacterMotionConfig degenerate = DefaultConfig();
    degenerate.upDirection = {0.0F, 0.0F, 0.0F};
    EXPECT_EQ(Character3D::MoveAndSlide(world, degenerate, state, {1.0F, 0.0F, 0.0F}, 0.1F).code,
              CharacterMoveCode::InvalidConfig);

    // A step with no duration, and a motion that is not a number.
    EXPECT_EQ(Character3D::MoveAndSlide(world, DefaultConfig(), state, {1.0F, 0.0F, 0.0F}, 0.0F).code,
              CharacterMoveCode::InvalidConfig);
    EXPECT_EQ(Character3D::MoveAndSlide(world, DefaultConfig(), state,
                                              {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, 0.1F).code,
              CharacterMoveCode::InvalidConfig);

    // A body state that is not a number.
    CharacterMotionState broken = state;
    broken.velocity = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    EXPECT_EQ(Character3D::MoveAndSlide(world, DefaultConfig(), broken, {1.0F, 0.0F, 0.0F}, 0.1F).code,
              CharacterMoveCode::InvalidState);

    // A world that cannot say where the body is.
    FakeCharacterWorld brokenWorld;
    brokenWorld.center = {std::numeric_limits<float>::infinity(), 0.0F, 0.0F};
    EXPECT_EQ(Character3D::MoveAndSlide(brokenWorld, DefaultConfig(), state, {1.0F, 0.0F, 0.0F}, 0.1F).code,
              CharacterMoveCode::InvalidWorld);

    // And the values a script could write that are merely wrong are clamped, not refused.
    CharacterMotionConfig sloppy = DefaultConfig();
    sloppy.maxSlides = 0U;
    sloppy.maximumContacts = 0U;
    sloppy.safeMargin = -1.0F;
    sloppy.floorMaxAngleDegrees = 999.0F;
    sloppy.wallMinSlideAngleDegrees = -5.0F;
    sloppy.floorSnapLength = -2.0F;
    sloppy.maxStepHeight = -1.0F;
    sloppy.minStepWidth = 42.0F;
    sloppy.maximumPlatformSpeed = -3.0F;
    sloppy.platformOnLeave = static_cast<Scene::CharacterPlatformLeaveMode>(200U);
    const CharacterMotionResult clamped =
        Character3D::MoveAndSlide(world, sloppy, state, {1.0F, 0.0F, 0.0F}, 0.1F);
    EXPECT_TRUE(clamped.IsAccepted());
    EXPECT_TRUE(clamped.configClamped);
}

TEST(Character3DMoverTest, AMotionTooSmallToMatterIsNotAMoveAtAll) {
    FakeCharacterWorld world;
    AddWall(world, 1.0F);
    CharacterMotionState state;
    state.velocity = {0.0F, 0.0F, 0.0F};

    const CharacterMotionResult result = Character3D::MoveAndSlide(
        world, DefaultConfig(), state, {Character3D::kMinimumMotionDistance * 0.5F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_FALSE(result.blocked);
    EXPECT_EQ(result.contactCount, 0U);
    EXPECT_EQ(result.slideCount, 0U);
    ExpectNear(result.appliedMotion, {0.0F, 0.0F, 0.0F}, 0.0F);
}

TEST(Character3DMoverTest, TheSameWorldAndTheSameWalkGiveTheSameAnswerToTheLastBit) {
    const auto runOnce = []() {
        FakeCharacterWorld world;
        AddFloor(world, 0.0F);
        world.boxes.push_back(FakeBox{MakeEntity(5U), {1.5F, 0.0F, -3.0F}, {4.0F, 0.2F, 3.0F}});
        world.boxes.push_back(FakeBox{MakeEntity(6U), {2.0F, -1.0F, 0.5F}, {2.5F, 1.0F, 1.0F}});
        world.boxes.push_back(FakeBox{MakeEntity(7U), {-2.0F, -1.0F, -2.0F}, {2.0F, 1.0F, -1.5F}});
        world.center = {0.0F, 0.0F, 0.0F};
        CharacterMotionState state;
        state.grounded = true;
        std::vector<Math::Vector3> path;
        for (int step = 0; step < 12; ++step) {
            state.velocity = {1.0F, 0.0F, 0.25F};
            const CharacterMotionResult result = Character3D::MoveAndSlide(
                world, DefaultConfig(), state, {0.1F, -0.01F, 0.025F}, 0.1F);
            path.push_back(result.appliedMotion);
            world.center = result.finalCenter;
        }
        return path;
    };

    const std::vector<Math::Vector3> first = runOnce();
    const std::vector<Math::Vector3> second = runOnce();
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t index = 0U; index < first.size(); ++index) {
        ExpectNear(first[index], second[index], 0.0F);
    }
    // The walk went somewhere, so this is not two sequences of zeroes agreeing.
    EXPECT_GT(first.back().x, 0.0F);
}

TEST(Character3DMoverTest, TestMoveAnswersWhatWouldHappenWithoutMovingAnything) {
    FakeCharacterWorld world;
    AddWall(world, 1.0F);
    world.center = {0.0F, 0.0F, 0.0F};

    const std::optional<CharacterSlideCollision> hit =
        Character3D::TestMove(world, {2.0F, 0.0F, 0.0F}, 0.001F);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->travel, 0.999F, 2.0e-3F);
    EXPECT_EQ(hit->entity.index, 2U);
    EXPECT_NEAR(hit->normal.x, -1.0F, 1.0e-3F);
    // The world is exactly where it was: a query, not a move.
    ExpectNear(world.center, {0.0F, 0.0F, 0.0F}, 0.0F);

    EXPECT_FALSE(Character3D::TestMove(world, {0.0F, 0.0F, 0.0F}, 0.001F).has_value());
    EXPECT_FALSE(Character3D::TestMove(world, {0.0F, 5.0F, 0.0F}, 0.001F).has_value());
}

TEST(Character3DMoverTest, ProbeFloorFindsTheFloorBelowAndNothingElse) {
    FakeCharacterWorld world;
    AddFloor(world, 0.0F, MakeEntity(9U));

    const std::optional<CharacterSlideCollision> floor =
        Character3D::ProbeFloor(world, {0.0F, 0.05F, 0.0F}, {0.0F, 1.0F, 0.0F}, 45.0F, 0.1F, 0.001F);
    ASSERT_TRUE(floor.has_value());
    EXPECT_EQ(floor->entity.index, 9U);
    ExpectNear(floor->normal, {0.0F, 1.0F, 0.0F}, 1.0e-4F);

    // Floors only: a wall's face below the probe is not something to stand on.
    FakeCharacterWorld wallWorld;
    AddWall(wallWorld, -0.5F, -1.0F, 1.0F);
    EXPECT_FALSE(Character3D::ProbeFloor(wallWorld, {0.0F, 0.05F, 0.0F}, {0.0F, 1.0F, 0.0F}, 45.0F, 0.1F,
                                              0.001F)
                     .has_value());

    // And nothing within reach is nothing.
    EXPECT_FALSE(Character3D::ProbeFloor(world, {0.0F, 5.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 45.0F, 0.1F, 0.001F)
                     .has_value());
}

TEST(Character3DMoverTest, StandingStillOnASlopeDoesNotCreepDownItWhenThatIsAsked) {
    const auto runOnce = [](const bool stopOnSlope) {
        FakeCharacterWorld world;
        // A 20 degree ramp rising in +x, seen as one contact.
        CharacterSlideCollision ramp;
        ramp.entity = MakeEntity(4U);
        ramp.normal = Math::Normalize(Math::Vector3{-0.342F, 0.940F, 0.0F});
        ramp.travel = 0.5F;
        world.scriptedHits.push_back(ramp);
        CharacterMotionConfig config = DefaultConfig();
        config.floorStopOnSlope = stopOnSlope;
        CharacterMotionState state;
        state.grounded = true;
        // Gravity pulling it down the slope, but not walking.
        state.velocity = {0.0F, -1.0F, 0.0F};
        const CharacterMotionResult result =
            Character3D::MoveAndSlide(world, config, state, {0.0F, -0.1F, 0.0F}, 0.1F);
        return std::pair<CharacterMotionResult, Math::Vector3>{result, state.velocity};
    };

    const auto stopped = runOnce(true);
    EXPECT_TRUE(stopped.first.onFloor);
    ExpectNear(stopped.second, {0.0F, 0.0F, 0.0F}, 1.0e-5F);

    const auto creeping = runOnce(false);
    EXPECT_TRUE(creeping.first.onFloor);
    EXPECT_LT(creeping.second.y, 0.0F);
}

TEST(Character3DMoverTest, WalkingUpASlopeKeepsTheHorizontalSpeedWhenThatIsAsked) {
    const auto runOnce = [](const bool constantSpeed) {
        FakeCharacterWorld world;
        CharacterSlideCollision ramp;
        ramp.entity = MakeEntity(4U);
        // A 30 degree ramp rising in +x: its normal leans back towards -x.
        ramp.normal = Math::Normalize(Math::Vector3{-0.5F, 0.866F, 0.0F});
        ramp.travel = 0.5F;
        world.scriptedHits.push_back(ramp);
        CharacterMotionConfig config = DefaultConfig();
        config.floorConstantSpeed = constantSpeed;
        CharacterMotionState state;
        state.grounded = true;
        state.velocity = {1.0F, 0.0F, 0.0F};
        return Character3D::MoveAndSlide(world, config, state, {1.0F, 0.0F, 0.0F}, 0.1F);
    };

    const CharacterMotionResult slowed = runOnce(false);
    const CharacterMotionResult constant = runOnce(true);
    // Without the setting the walk up the ramp loses horizontal speed to it; with it, the walk
    // keeps every millimetre of the horizontal motion it asked for.
    EXPECT_NEAR(slowed.appliedMotion.x, 0.875F, 5.0e-3F);
    EXPECT_NEAR(constant.appliedMotion.x, 1.0F, 5.0e-3F);
    EXPECT_GT(constant.appliedMotion.x, slowed.appliedMotion.x);
    // The climb itself is the same either way: the setting changes the speed, not the slope.
    EXPECT_NEAR(constant.appliedMotion.y, slowed.appliedMotion.y, 5.0e-3F);
}

TEST(Character3DMoverTest, TheReportHelpersAnswerTheOneQuestionEachOfThemIsNamedFor) {
    FakeCharacterWorld world;
    AddFloor(world, 0.0F);
    world.center = {0.0F, 0.001F, 0.0F};
    CharacterMotionState state;
    state.grounded = true;
    const CharacterMotionResult result =
        Character3D::MoveAndSlide(world, DefaultConfig(), state, {0.1F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_TRUE(result.IsOnFloorOnly());
    EXPECT_FALSE(result.IsOnWallOnly());
    EXPECT_FALSE(result.IsOnCeilingOnly());
    EXPECT_EQ(result.GetCollisionCount(), result.collisions.size());
    EXPECT_EQ(result.GetCollisionCount(), result.contactCount);
}

TEST(Character3DMoverTest, StateAndConfigRoundTripThroughTheComponent) {
    Scene::CharacterControllerComponent controller;
    controller.maxSlides = 12U;
    controller.floorMaxAngleDegrees = 30.0F;
    controller.wallMinSlideAngleDegrees = 5.0F;
    controller.safeMargin = 0.01F;
    controller.floorSnapLength = 0.25F;
    controller.maxStepHeight = 0.45F;
    controller.floorBlockOnWall = true;
    controller.slideOnCeiling = false;
    controller.floorStopOnSlope = true;
    controller.floorConstantSpeed = true;
    controller.maximumPlatformSpeed = 12.0F;
    controller.platformOnLeave = Scene::CharacterPlatformLeaveMode::AddUpwardVelocity;
    controller.upDirection = {0.0F, 0.0F, 1.0F};

    const CharacterMotionConfig config = Scene::MakeCharacterMotionConfig(controller);
    ExpectNear(config.upDirection, {0.0F, 0.0F, 1.0F}, 1.0e-6F);
    EXPECT_EQ(config.maxSlides, 12U);
    EXPECT_NEAR(config.floorMaxAngleDegrees, 30.0F, 1.0e-5F);
    EXPECT_NEAR(config.wallMinSlideAngleDegrees, 5.0F, 1.0e-5F);
    EXPECT_NEAR(config.safeMargin, 0.01F, 1.0e-6F);
    EXPECT_NEAR(config.floorSnapLength, 0.25F, 1.0e-6F);
    EXPECT_NEAR(config.maxStepHeight, 0.45F, 1.0e-6F);
    EXPECT_TRUE(config.floorBlockOnWall);
    EXPECT_FALSE(config.slideOnCeiling);
    EXPECT_TRUE(config.floorStopOnSlope);
    EXPECT_TRUE(config.floorConstantSpeed);
    EXPECT_NEAR(config.maximumPlatformSpeed, 12.0F, 1.0e-6F);
    EXPECT_EQ(config.platformOnLeave, Scene::CharacterPlatformLeaveMode::AddUpwardVelocity);
    EXPECT_TRUE(config.detectFloor);
    EXPECT_FALSE(config.jumpedThisStep);

    // A floating body has no floor to detect, nothing to snap to, nothing to step on and no slope
    // to stand on: the mode's whole contract, expressed as configuration.
    controller.motionMode = Scene::CharacterMotionMode::Floating;
    const CharacterMotionConfig floating = Scene::MakeCharacterMotionConfig(controller);
    EXPECT_FALSE(floating.detectFloor);
    EXPECT_NEAR(floating.floorSnapLength, 0.0F, 1.0e-6F);
    EXPECT_NEAR(floating.maxStepHeight, 0.0F, 1.0e-6F);
    EXPECT_FALSE(floating.floorStopOnSlope);

    // State in, state out, and only the runtime block is touched.
    CharacterMotionState state;
    state.velocity = {3.0F, 0.0F, -1.0F};
    state.grounded = true;
    state.onWall = true;
    state.wallNormal = {-1.0F, 0.0F, 0.0F};
    state.timeSinceOnFloor = 0.25F;
    state.jumpBufferRemaining = 0.05F;
    state.lastMotion = {0.1F, 0.0F, 0.0F};
    state.realVelocity = {1.0F, 0.0F, 0.0F};
    state.platform = MakeEntity(3U);
    state.platformVelocity = {2.0F, 0.0F, 0.0F};
    state.platformWorldPosition = {1.0F, 0.0F, 0.0F};
    state.hasPlatformWorldPosition = true;
    Scene::CharacterControllerComponent target;
    Scene::StoreCharacterMotionState(target, state);
    EXPECT_TRUE(target.grounded);
    EXPECT_TRUE(target.onWall);
    EXPECT_NEAR(target.wallNormal.x, -1.0F, 1.0e-6F);
    EXPECT_NEAR(target.timeSinceOnFloor, 0.25F, 1.0e-6F);
    EXPECT_NEAR(target.jumpBufferRemaining, 0.05F, 1.0e-6F);
    EXPECT_EQ(target.platform, MakeEntity(3U));
    EXPECT_TRUE(target.hasPlatformWorldPosition);
    // Authored settings were not touched by the state write: `target` was never given any, so it
    // still holds the component's own defaults.
    const Scene::CharacterControllerComponent authoredDefaults;
    EXPECT_NEAR(target.safeMargin, authoredDefaults.safeMargin, 1.0e-6F);
    EXPECT_EQ(target.maxSlides, authoredDefaults.maxSlides);

    // And the state read back is the same state, platform bookkeeping included.
    const CharacterMotionState readBack = Scene::MakeCharacterMotionState(target);
    ExpectNear(readBack.velocity, state.velocity, 1.0e-6F);
    EXPECT_EQ(readBack.platform, state.platform);
    EXPECT_TRUE(readBack.hasPlatformWorldPosition);
    ExpectNear(readBack.platformVelocity, state.platformVelocity, 1.0e-6F);
}

TEST(Character3DMoverTest, SnapAndStepUseTheBodyUpNotWorldY) {
    // A Z-up body whose world-Y velocity is large: that used to be read as "going up", which
    // skipped the snap and refused the step. Along this body's own up it is standing still, so
    // both have to happen.
    const auto zUpConfig = []() {
        CharacterMotionConfig config = DefaultConfig();
        config.upDirection = {0.0F, 0.0F, 1.0F};
        return config;
    };

    {
        FakeCharacterWorld world;
        AddBox(world, MakeEntity(1U), {-100.0F, -100.0F, -1.0F}, {100.0F, 100.0F, 0.0F});
        world.center = {0.0F, 0.0F, 0.05F};
        CharacterMotionState state;
        state.grounded = true;
        state.velocity = {0.0F, 5.0F, 0.0F};
        const CharacterMotionResult result =
            Character3D::MoveAndSlide(world, zUpConfig(), state, {0.1F, 0.0F, 0.0F}, 0.1F);
        ASSERT_TRUE(result.IsAccepted());
        EXPECT_TRUE(result.snapUsed);
        EXPECT_TRUE(result.onFloor);
        EXPECT_LT(result.finalCenter.z, 0.02F);
        EXPECT_GT(result.finalCenter.z, -1.0e-3F);
    }

    {
        FakeCharacterWorld world;
        AddBox(world, MakeEntity(1U), {-100.0F, -100.0F, -1.0F}, {100.0F, 100.0F, 0.0F});
        world.center = {0.0F, 0.0F, 0.05F};
        CharacterMotionState state;
        state.grounded = true;
        state.velocity = {0.0F, 0.0F, 5.0F};
        const CharacterMotionResult result =
            Character3D::MoveAndSlide(world, zUpConfig(), state, {0.1F, 0.0F, 0.0F}, 0.1F);
        ASSERT_TRUE(result.IsAccepted());
        EXPECT_FALSE(result.snapUsed);
        EXPECT_NEAR(result.finalCenter.z, 0.05F, 1.0e-4F);
    }

    {
        FakeCharacterWorld world;
        AddBox(world, MakeEntity(1U), {-100.0F, -100.0F, -1.0F}, {100.0F, 100.0F, 0.0F});
        world.boxes.push_back(
            FakeBox{MakeEntity(5U), {1.0F, -2.0F, 0.0F}, {3.0F, 2.0F, 0.2F}});
        world.center = {0.0F, 0.0F, 0.001F};
        CharacterMotionState state;
        state.grounded = true;
        state.velocity = {1.0F, 5.0F, 0.0F};
        const CharacterMotionResult result =
            Character3D::MoveAndSlide(world, zUpConfig(), state, {1.0F, 0.0F, 0.0F}, 0.1F);
        ASSERT_TRUE(result.IsAccepted());
        EXPECT_TRUE(result.stepUpUsed);
        EXPECT_TRUE(result.onFloor);
        EXPECT_GT(result.finalCenter.z, 0.15F);
    }
}

TEST(Character3DMoverTest, FloorStopOnSlopeUsesTheBodyUpNotWorldY) {
    FakeCharacterWorld world;
    CharacterSlideCollision ramp;
    ramp.entity = MakeEntity(4U);
    ramp.normal = Math::Normalize(Math::Vector3{-0.342F, 0.0F, 0.940F});
    ramp.travel = 0.5F;
    world.scriptedHits.push_back(ramp);
    CharacterMotionConfig config = DefaultConfig();
    config.upDirection = {0.0F, 0.0F, 1.0F};
    config.floorStopOnSlope = true;
    CharacterMotionState state;
    state.grounded = true;
    // Horizontal in Z-up. World-Y would have read this as "going up" and left the body creeping.
    state.velocity = {0.0F, 0.05F, 0.0F};

    const CharacterMotionResult result =
        Character3D::MoveAndSlide(world, config, state, {0.0F, 0.0F, -0.1F}, 0.1F);

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_TRUE(result.onFloor);
    ExpectNear(state.velocity, {0.0F, 0.0F, 0.0F}, 1.0e-5F);
}

TEST(Character3DMoverTest, ACharacterCanBeBuiltFromItsObjectDefinition) {
    const Scene::Character3DObjectDefinition definition{};
    EXPECT_TRUE(Scene::IsCharacter3DObjectDefinitionValid(definition));
    const Scene::ColliderComponent collider = Scene::Character3DObjectDefinition::MakeDefaultCollider();
    EXPECT_GT(collider.halfExtents.y, 0.0F);
    EXPECT_GT(collider.halfExtents.x, 0.0F);
}

// =================================================================================================
// The same mover against the engine's real world: real components, the real collision system, the
// real scene graph, and the step system that ties them together.
// =================================================================================================

class Character3DStepTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    CollisionSystem collisionSystem;

    static constexpr float kDeltaTime = 1.0F / 60.0F;
    static constexpr float kGravityY = -9.81F;

    [[nodiscard]] Scene::Entity MakeBox(const Math::Vector3 position,
                                              const Math::Vector3 halfExtents,
                                              const std::uint32_t layer = 1U,
                                              const std::uint32_t mask = 0xFFFFFFFFU) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform;
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
        Scene::ColliderComponent collider{halfExtents};
        collider.collisionLayer = layer;
        collider.collisionMask = mask;
        entityManager.AddComponent<Scene::ColliderComponent>(entity, collider);
        return entity;
    }

    /// A character is a collider, a controller and a transform - no rigid body, because a character
    /// is moved by its own code and not by forces.
    [[nodiscard]] Scene::Entity MakeCharacter(
        const Math::Vector3 position, const Math::Vector3 halfExtents,
        const Scene::CharacterControllerComponent& controller = {}) {
        const Scene::Entity entity = MakeBox(position, halfExtents);
        entityManager.AddComponent<Scene::CharacterControllerComponent>(entity, controller);
        return entity;
    }

    [[nodiscard]] Scene::Entity MakeFloor(const float topY = 0.0F,
                                                const Math::Vector3 halfExtents = {100.0F, 0.5F, 100.0F}) {
        return MakeBox({0.0F, topY - halfExtents.y, 0.0F}, halfExtents);
    }

    [[nodiscard]] Character3DStepResult Step(
        const Scene::Entity entity, const CharacterMotionInput& input = {},
        const float deltaTimeSeconds = kDeltaTime) {
        return StepCharacter3D(entityManager, sceneGraph, collisionSystem, entity, input, kGravityY,
                                  deltaTimeSeconds);
    }

    [[nodiscard]] Math::Vector3 Position(const Scene::Entity entity) {
        return entityManager.GetComponent<Scene::TransformComponent>(entity).localPosition;
    }

    [[nodiscard]] Scene::CharacterControllerComponent Controller(const Scene::Entity entity) {
        return entityManager.GetComponent<Scene::CharacterControllerComponent>(entity);
    }

    /// Steps until the predicate holds, and hands back the last report. Used where a test is about
    /// where a body ends up rather than about one particular step.
    template <typename Predicate>
    [[nodiscard]] Character3DStepResult StepUntil(const Scene::Entity entity,
                                                        Predicate&& done, const int maximumSteps = 240) {
        Character3DStepResult report = Step(entity);
        for (int step = 1; step < maximumSteps && !done(report); ++step) {
            report = Step(entity);
        }
        return report;
    }
};

TEST_F(Character3DStepTest, ACharacterFallingOntoTheFloorLandsOnItAndStaysThere) {
    const Scene::Entity floor = MakeFloor(0.0F);
    static_cast<void>(floor);
    const Scene::Entity character = MakeCharacter({0.0F, 3.0F, 0.0F}, {0.5F, 0.5F, 0.5F});

    for (int step = 0; step < 120; ++step) {
        const Character3DStepResult report = Step(character);
        ASSERT_TRUE(report.stepped) << "step " << step;
    }

    // Resting on the floor: the collider's half height above the floor's top, give or take the skin.
    EXPECT_NEAR(Position(character).y, 0.5F, 1.0e-2F);
    const Scene::CharacterControllerComponent controller = Controller(character);
    EXPECT_TRUE(controller.grounded);
    EXPECT_NEAR(controller.velocity.y, 0.0F, 1.0e-3F);
    EXPECT_NEAR(controller.floorNormal.y, 1.0F, 1.0e-3F);
    EXPECT_LT(controller.timeSinceOnFloor, 1.0e-3F);
}

TEST_F(Character3DStepTest, WalkingIntoAWallStopsAtItAndSaysSo) {
    const Scene::Entity floor = MakeFloor(0.0F);
    static_cast<void>(floor);
    const Scene::Entity wall = MakeBox({2.5F, 1.0F, 0.0F}, {0.5F, 1.5F, 5.0F});
    Scene::CharacterControllerComponent settings;
    settings.builtInMovement = false;
    const Scene::Entity character = MakeCharacter({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F}, settings);

    CharacterMotionInput input;
    Character3DStepResult report;
    for (int step = 0; step < 120; ++step) {
        // A script driving the body: five metres a second, straight at the wall.
        entityManager.GetComponent<Scene::CharacterControllerComponent>(character).velocity =
            Math::Vector3{5.0F, 0.0F, 0.0F};
        report = Step(character, input);
        ASSERT_TRUE(report.stepped) << "step " << step;
    }

    // The wall's own face is at x = 2.0; the collider's surface stops a skin short of it.
    EXPECT_NEAR(Position(character).x, 1.5F, 5.0e-3F);
    EXPECT_TRUE(report.motion.onWall);
    EXPECT_TRUE(report.motion.blocked);
    EXPECT_NEAR(report.motion.wallNormal.x, -1.0F, 1.0e-3F);
    ASSERT_GT(report.motion.contactCount, 0U);
    ASSERT_NE(report.motion.GetCollision(0U), nullptr);
    EXPECT_EQ(report.motion.GetCollision(0U)->entity, wall);
}

TEST_F(Character3DStepTest, WalkingForwardClimbsALowStep) {
    const Scene::Entity floor = MakeFloor(0.0F);
    static_cast<void>(floor);
    // A step 0.25 m tall, from x = 1.0 onwards: low enough for the default 0.3 m step height.
    const Scene::Entity stepBox = MakeBox({3.0F, 0.125F, 0.0F}, {2.5F, 0.125F, 5.0F});
    Scene::CharacterControllerComponent settings;
    settings.builtInMovement = false;
    const Scene::Entity character = MakeCharacter({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F}, settings);

    bool steppedUp = false;
    // One second of walking: three metres from the start, which is the middle of the tread.
    for (int step = 0; step < 60; ++step) {
        entityManager.GetComponent<Scene::CharacterControllerComponent>(character).velocity =
            Math::Vector3{3.0F, 0.0F, 0.0F};
        const Character3DStepResult report = Step(character);
        ASSERT_TRUE(report.stepped) << "step " << step;
        steppedUp = steppedUp || report.motion.stepUpUsed;
    }

    EXPECT_TRUE(steppedUp);
    // On top of the step: the tread is at 0.25, so the body's centre rests just above 0.75.
    EXPECT_GT(Position(character).x, 2.0F);
    EXPECT_NEAR(Position(character).y, 0.75F, 1.5e-2F);
    static_cast<void>(stepBox);
}

TEST_F(Character3DStepTest, AStepTallerThanTheStepHeightIsAWallAndIsNotClimbed) {
    const Scene::Entity floor = MakeFloor(0.0F);
    static_cast<void>(floor);
    // 0.6 m tall against a 0.3 m step height: a wall, whatever it looks like from the side. Its
    // face is a metre in front of the body, so the walk has somewhere to go before it stops.
    const Scene::Entity ledge = MakeBox({3.5F, 0.3F, 0.0F}, {2.5F, 0.3F, 5.0F});
    static_cast<void>(ledge);
    Scene::CharacterControllerComponent settings;
    settings.builtInMovement = false;
    const Scene::Entity character = MakeCharacter({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F}, settings);

    for (int step = 0; step < 120; ++step) {
        entityManager.GetComponent<Scene::CharacterControllerComponent>(character).velocity =
            Math::Vector3{3.0F, 0.0F, 0.0F};
        const Character3DStepResult report = Step(character);
        ASSERT_TRUE(report.stepped) << "step " << step;
        EXPECT_FALSE(report.motion.stepUpUsed);
    }

    EXPECT_NEAR(Position(character).y, 0.5F, 1.0e-2F);
    EXPECT_NEAR(Position(character).x, 0.5F, 5.0e-3F);
}

TEST_F(Character3DStepTest, JumpingLeavesTheFloorAndComesBackDownToIt) {
    const Scene::Entity floor = MakeFloor(0.0F);
    static_cast<void>(floor);
    Scene::CharacterControllerComponent settings;
    settings.builtInMovement = true;
    settings.jumpHeight = 1.0F;
    const Scene::Entity character = MakeCharacter({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F}, settings);

    // Settle first: a jump is only a jump from the floor.
    const Character3DStepResult settled = StepUntil(character, [](const Character3DStepResult&) {
        return false;
    }, 2);
    ASSERT_TRUE(settled.stepped);
    ASSERT_TRUE(Controller(character).grounded);

    CharacterMotionInput jumpInput;
    jumpInput.jumpPressed = true;
    const Character3DStepResult jump = Step(character, jumpInput);
    ASSERT_TRUE(jump.stepped);
    EXPECT_TRUE(jump.jumped);
    EXPECT_GT(Controller(character).velocity.y, 0.0F);
    EXPECT_FALSE(Controller(character).grounded);

    float highest = Position(character).y;
    bool landed = false;
    for (int step = 0; step < 240 && !landed; ++step) {
        const Character3DStepResult report = Step(character);
        ASSERT_TRUE(report.stepped);
        highest = std::max(highest, Position(character).y);
        landed = report.motion.onFloor;
    }

    EXPECT_TRUE(landed);
    EXPECT_GT(highest, 1.0F);
    EXPECT_NEAR(Position(character).y, 0.5F, 1.5e-2F);
    EXPECT_TRUE(Controller(character).grounded);
}

TEST_F(Character3DStepTest, AFloatingBodyIgnoresGravityAndItsLocksHoldTheirAxes) {
    const Scene::Entity character = MakeCharacter({0.0F, 1.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    Scene::CharacterControllerComponent settings = Controller(character);
    settings.motionMode = Scene::CharacterMotionMode::Floating;
    settings.builtInMovement = true;
    settings.moveSpeed = 2.0F;
    entityManager.GetComponent<Scene::CharacterControllerComponent>(character) = settings;

    Scene::SolidBodyComponent locks;
    locks.lockMotionX = true;
    entityManager.AddComponent<Scene::SolidBodyComponent>(character, locks);

    CharacterMotionInput input;
    input.move = {1.0F, 0.0F, 1.0F};
    input.rise = 1.0F;
    for (int step = 0; step < 30; ++step) {
        const Character3DStepResult report = Step(character, input);
        ASSERT_TRUE(report.stepped) << "step " << step;
        EXPECT_FALSE(report.motion.onFloor);
    }

    const Math::Vector3 position = Position(character);
    EXPECT_NEAR(position.x, 0.0F, 1.0e-5F); // locked
    EXPECT_NEAR(position.z, 1.0F, 5.0e-2F); // half a second at 2 m/s
    EXPECT_NEAR(position.y, 2.0F, 5.0e-2F); // rising at 2 m/s
}

TEST_F(Character3DStepTest, AMovingPlatformCarriesTheBodyStandingOnIt) {
    // The platform is the floor: a solid box the body stands on, moved by its own code each step.
    const Scene::Entity platform = MakeBox({0.0F, -0.25F, 0.0F}, {2.0F, 0.25F, 2.0F});
    const Scene::Entity character = MakeCharacter({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F});

    // Ride for a moment so the body is genuinely standing on the platform.
    for (int step = 0; step < 10; ++step) {
        ASSERT_TRUE(Step(character).stepped);
    }
    ASSERT_TRUE(Controller(character).grounded);
    const float startX = Position(character).x;

    bool carried = false;
    constexpr float kPlatformStep = 0.01F;
    for (int step = 0; step < 30; ++step) {
        Scene::TransformComponent platformTransform =
            entityManager.GetComponent<Scene::TransformComponent>(platform);
        platformTransform.localPosition.x += kPlatformStep;
        sceneGraph.SetLocalTransform(entityManager, platform, platformTransform);

        const Character3DStepResult report = Step(character);
        ASSERT_TRUE(report.stepped) << "step " << step;
        carried = carried || report.motion.carriedByPlatform;
    }

    EXPECT_TRUE(carried);
    // Thirty steps at a centimetre a step: the rider went with the platform, not just its velocity.
    EXPECT_NEAR(Position(character).x - startX, 0.3F, 2.5e-2F);
    EXPECT_TRUE(Controller(character).grounded);
}

TEST_F(Character3DStepTest, ALayerTheCharacterDoesNotCollideWithIsWalkedStraightThrough) {
    const Scene::Entity floor = MakeFloor(0.0F);
    static_cast<void>(floor);
    // A wall on layer 2. The character's mask only accepts layer 1, and the wall's mask accepts
    // everything - one direction failing is enough for a pair to be ignored.
    const Scene::Entity wall = MakeBox({2.5F, 1.0F, 0.0F}, {0.5F, 1.5F, 5.0F}, 2U, 0xFFFFFFFFU);
    static_cast<void>(wall);
    Scene::CharacterControllerComponent settings;
    settings.builtInMovement = false;
    const Scene::Entity character = MakeBox({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F}, 1U, 1U);
    entityManager.AddComponent<Scene::CharacterControllerComponent>(character, settings);

    for (int step = 0; step < 60; ++step) {
        entityManager.GetComponent<Scene::CharacterControllerComponent>(character).velocity =
            Math::Vector3{5.0F, 0.0F, 0.0F};
        const Character3DStepResult report = Step(character);
        ASSERT_TRUE(report.stepped) << "step " << step;
        EXPECT_FALSE(report.motion.onWall);
    }

    EXPECT_GT(Position(character).x, 4.0F);
}

TEST_F(Character3DStepTest, AControllerWithNoShapeFallsInsteadOfStandingInTheAir) {
    const Scene::Entity floor = MakeFloor(0.0F);
    static_cast<void>(floor);
    // A Character3D authored without a Collider3D: a controller with nothing to stand on and
    // nothing to hit. It used to be refused - missing collider - and left exactly where the editor
    // placed it, which reads as a body born with an invisible collider of its own.
    const Scene::Entity shapeless = entityManager.CreateEntity();
    Scene::TransformComponent transform;
    transform.localPosition = {0.0F, 3.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, shapeless, transform);
    entityManager.AddComponent<Scene::CharacterControllerComponent>(
        shapeless, Scene::CharacterControllerComponent{});

    const Math::Vector3 start = Position(shapeless);
    for (int step = 0; step < 120; ++step) {
        const Character3DStepResult report = Step(shapeless);
        ASSERT_TRUE(report.stepped) << "step " << step << ": a shapeless body still moves";
        ASSERT_EQ(report.code, Character3DStepCode::NoShape) << "step " << step;
    }
    EXPECT_LT(Position(shapeless).y, start.y) << "gravity reaches a body with no shape";
    EXPECT_LT(Position(shapeless).y, 0.0F) << "no shape means no floor: it falls through it";
    EXPECT_FALSE(Controller(shapeless).grounded);
}

TEST_F(Character3DStepTest, AMeshAndAShapeAuthoredAtTheSameHeightStandOnTheFloor) {
    // The reported setup, both halves of it. A Player3D at the world origin with two children, each
    // one metre up: the MeshInstance3D showing a CapsuleMesh, and the Collider3D that is its body.
    // The floor's top is y = 0.
    //
    // The mesh capsule is 2.0 m tall (radius 0.5, half height 1.0) - the size the CapsuleShape3D
    // fit produces - so a shape and a mesh authored at the same height have the same bottom, and a
    // body standing on that shape puts the model exactly on the floor: body at 0, mesh 1 m up.
    //
    // It used to float a whole metre, because the mover read the collider's SIZE but took its
    // CENTRE from the body. The shape was queried at the body's origin while the mesh was drawn one
    // metre above it, which is a capsule hanging in the air over an invisible body.
    const Scene::Entity floor = MakeFloor(0.0F);
    static_cast<void>(floor);

    const Scene::Entity body = entityManager.CreateEntity();
    Scene::TransformComponent bodyTransform;
    bodyTransform.localPosition = {0.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, body, bodyTransform);
    entityManager.AddComponent<Scene::CharacterControllerComponent>(
        body, Scene::CharacterControllerComponent{});

    const Scene::Entity mesh = entityManager.CreateEntity();
    Scene::TransformComponent meshTransform;
    meshTransform.localPosition = {0.0F, 1.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, mesh, meshTransform);
    entityManager.AddComponent<Scene::MeshComponent>(mesh, Scene::MeshComponent{});
    entityManager.AddComponent<Scene::PrimitiveMeshComponent>(
        mesh, Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::Capsule});
    sceneGraph.SetParent(entityManager, mesh, body);

    const Scene::Entity shape = entityManager.CreateEntity();
    Scene::TransformComponent shapeTransform;
    shapeTransform.localPosition = {0.0F, 1.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, shape, shapeTransform);
    Scene::ColliderComponent capsule = Scene::Collider3D::MakeCapsule(0.5F, 2.0F);
    entityManager.AddComponent<Scene::ColliderComponent>(shape, capsule);
    sceneGraph.SetParent(entityManager, shape, body);
    sceneGraph.Update(entityManager);

    ASSERT_FALSE(entityManager.HasComponent<Scene::ColliderComponent>(body))
        << "the body itself carries no shape - the Collider3D under it does";

    for (int step = 0; step < 240 && !Controller(body).grounded; ++step) {
        const Character3DStepResult report = Step(body);
        ASSERT_TRUE(report.stepped) << "step " << step;
    }
    ASSERT_TRUE(Controller(body).grounded) << "the body never reached the floor";

    // The shape is what stands on the floor: its centre is half its height above it.
    EXPECT_NEAR(entityManager.GetComponent<Scene::WorldTransformComponent>(shape).worldPosition.y,
                1.0F, 0.02F)
        << "the Collider3D rests at its own half height, as authored";
    // ...and the body, whose origin is a metre below the shape, lands at the floor.
    EXPECT_NEAR(Position(body).y, 0.0F, 0.02F)
        << "the body is not lifted by its child's offset, and not sunk by it either";
    // ...which puts the bottom of the capsule the author sees exactly on the floor.
    EXPECT_NEAR(entityManager.GetComponent<Scene::WorldTransformComponent>(mesh).worldPosition.y -
                    1.0F,
                0.0F, 0.02F)
        << "the mesh capsule is 2 m tall, centred 1 m up, so its bottom is the floor";
}

TEST_F(Character3DStepTest, ABodyWearingAMeshWithNoColliderFalls) {
    // The reported setup, exactly: a body with no Collider3D, and the mesh it wears under it as a
    // MeshInstance3D showing a CapsuleMesh. Neither carries a collider - a MeshInstance3D gets a
    // MeshComponent and nothing else - so there is no shape anywhere, and the body hangs in the
    // air wearing a capsule. What looks like an invisible collider holding it up is the body being
    // refused and left where it was placed.
    const Scene::Entity floor = MakeFloor(0.0F);
    static_cast<void>(floor);

    const Scene::Entity body = entityManager.CreateEntity();
    Scene::TransformComponent bodyTransform;
    bodyTransform.localPosition = {0.0F, 3.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, body, bodyTransform);
    entityManager.AddComponent<Scene::CharacterControllerComponent>(
        body, Scene::CharacterControllerComponent{});

    const Scene::Entity worn = entityManager.CreateEntity();
    Scene::TransformComponent wornTransform;
    sceneGraph.AttachTransform(entityManager, worn, wornTransform);
    entityManager.AddComponent<Scene::MeshComponent>(worn, Scene::MeshComponent{});
    entityManager.AddComponent<Scene::PrimitiveMeshComponent>(
        worn, Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::Capsule});
    sceneGraph.SetParent(entityManager, worn, body);
    sceneGraph.Update(entityManager);

    ASSERT_FALSE(entityManager.HasComponent<Scene::ColliderComponent>(body))
        << "a Character3D is a controller, not a shape";
    ASSERT_FALSE(entityManager.HasComponent<Scene::ColliderComponent>(worn))
        << "a MeshInstance3D draws a mesh; it is not a collider";

    const Math::Vector3 start = Position(body);
    for (int step = 0; step < 120; ++step) {
        const Character3DStepResult report = Step(body);
        ASSERT_TRUE(report.stepped) << "step " << step << ": a body wearing a mesh still moves";
        ASSERT_EQ(report.code, Character3DStepCode::NoShape) << "step " << step;
    }
    EXPECT_LT(Position(body).y, start.y) << "the body falls instead of hanging in the air";
    EXPECT_NEAR(Position(body).y, start.y - 0.5F * 9.81F * 2.0F * 2.0F, 0.35F)
        << "and it falls at gravity, not slowly and not not-at-all";
}

TEST_F(Character3DStepTest, AShapeAuthoredUnderTheBodyPutsTheBodyWhereTheShapeIs) {
    const Scene::Entity floor = MakeFloor(0.0F);
    static_cast<void>(floor);

    // A character with no collider of its own and a Collider3D under it, offset 0.4 up - the shape
    // is where the author put it, not where the body's origin is. The body falls until the SHAPE
    // rests on the floor, which leaves the body 0.4 below the shape's centre. Taking the centre
    // from the body instead would stop the body 0.4 higher and leave the shape hovering.
    const Scene::Entity body = entityManager.CreateEntity();
    Scene::TransformComponent bodyTransform;
    bodyTransform.localPosition = {0.0F, 3.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, body, bodyTransform);
    entityManager.AddComponent<Scene::CharacterControllerComponent>(
        body, Scene::CharacterControllerComponent{});

    const Scene::Entity shape = entityManager.CreateEntity();
    Scene::TransformComponent shapeTransform;
    shapeTransform.localPosition = {0.0F, 0.4F, 0.0F};
    sceneGraph.AttachTransform(entityManager, shape, shapeTransform);
    entityManager.AddComponent<Scene::ColliderComponent>(
        shape, Scene::Collider3D::MakeCapsule(0.4F, 1.8F));
    sceneGraph.SetParent(entityManager, shape, body);
    sceneGraph.Update(entityManager);

    ASSERT_EQ(Position(body), (Math::Vector3{0.0F, 3.0F, 0.0F}));

    for (int step = 0; step < 240 && !Controller(body).grounded; ++step) {
        const Character3DStepResult report = Step(body);
        ASSERT_TRUE(report.stepped) << "step " << step;
    }
    ASSERT_TRUE(Controller(body).grounded) << "the body never reached the floor";

    const Scene::WorldTransformComponent shapeWorld =
        entityManager.GetComponent<Scene::WorldTransformComponent>(shape);
    // A 1.8-tall capsule rests with its centre at half its height.
    EXPECT_NEAR(shapeWorld.worldPosition.y, 0.9F, 0.02F)
        << "the shape is what stands on the floor, so the shape is what must be at rest height";
    EXPECT_NEAR(Position(body).y, 0.5F, 0.02F)
        << "the body sits 0.4 under the shape it was authored with";
}

TEST_F(Character3DStepTest, BodiesTheStepCannotDriveAreRefusedWithAReason) {
    const Scene::Entity floor = MakeFloor(0.0F);
    static_cast<void>(floor);

    // Not a controller at all.
    const Scene::Entity plain = MakeBox({0.0F, 1.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    EXPECT_EQ(Step(plain).code, Character3DStepCode::NotAController);

    // A predicted body belongs to the predictor: refused, and left exactly where it was.
    const Scene::Entity predicted = MakeCharacter({0.0F, 1.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    entityManager.AddComponent<Scene::PredictedCharacterComponent>(
        predicted, Scene::PredictedCharacterComponent{});
    EXPECT_EQ(Step(predicted).code, Character3DStepCode::PredictedElsewhere);
    EXPECT_FALSE(Step(predicted).stepped);
    EXPECT_EQ(Position(predicted), (Math::Vector3{0.0F, 1.0F, 0.0F}));

    // A dynamic rigid body is driven by forces, not by a character controller.
    const Scene::Entity dynamic = MakeCharacter({0.0F, 1.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    entityManager.AddComponent<Scene::Rigid3DComponent>(dynamic, Scene::Rigid3DComponent{});
    EXPECT_EQ(Step(dynamic).code, Character3DStepCode::NonKinematicBody);

    // A kinematic rigid body is fine: that is a body whose motion is authored.
    const Scene::Entity kinematic = MakeCharacter({0.0F, 1.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    Scene::Rigid3DComponent kinematicBody;
    kinematicBody.isKinematic = true;
    entityManager.AddComponent<Scene::Rigid3DComponent>(kinematic, kinematicBody);
    EXPECT_EQ(Step(kinematic).code, Character3DStepCode::Stepped);

    // A vanished entity, and a step with no duration.
    const Scene::Entity vanished = MakeCharacter({0.0F, 1.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    entityManager.DestroyEntity(vanished);
    EXPECT_EQ(Step(vanished).code, Character3DStepCode::UnknownEntity);
    EXPECT_EQ(Step(kinematic, {}, 0.0F).code, Character3DStepCode::InvalidDeltaTime);
    EXPECT_EQ(Step(kinematic, {}, -1.0F).code, Character3DStepCode::InvalidDeltaTime);
    EXPECT_EQ(Step(kinematic, {}, std::numeric_limits<float>::quiet_NaN()).code,
              Character3DStepCode::InvalidDeltaTime);
}

TEST_F(Character3DStepTest, AScriptedVelocityMovesTheBodyThroughTheWorld) {
    const Scene::Entity floor = MakeFloor(0.0F);
    static_cast<void>(floor);
    Scene::CharacterControllerComponent settings;
    settings.builtInMovement = false;
    const Scene::Entity character = MakeCharacter({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F}, settings);

    Math::Vector3 velocity{2.0F, 0.0F, -3.0F};
    for (int step = 0; step < 60; ++step) {
        entityManager.GetComponent<Scene::CharacterControllerComponent>(character).velocity = velocity;
        const Character3DStepResult report = Step(character);
        ASSERT_TRUE(report.stepped) << "step " << step;
    }

    // One second of scripted travel, and the component's velocity is left as the script set it.
    EXPECT_NEAR(Position(character).x, 2.0F, 5.0e-2F);
    EXPECT_NEAR(Position(character).z, -3.0F, 5.0e-2F);
    EXPECT_NEAR(Controller(character).velocity.x, velocity.x, 1.0e-4F);
    EXPECT_NEAR(Controller(character).velocity.z, velocity.z, 1.0e-4F);
}

TEST_F(Character3DStepTest, ARefusedStepLeavesTheBodyExactlyWhereItWas) {
    const Scene::Entity floor = MakeFloor(0.0F);
    static_cast<void>(floor);
    Scene::CharacterControllerComponent settings;
    settings.builtInMovement = false;
    const Scene::Entity character = MakeCharacter({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F}, settings);
    entityManager.GetComponent<Scene::CharacterControllerComponent>(character).velocity =
        Math::Vector3{4.0F, 0.0F, 0.0F};

    const Math::Vector3 positionBefore = Position(character);
    const Character3DStepResult refused = Step(character, {}, 0.0F);
    EXPECT_FALSE(refused.stepped);
    ExpectNear(Position(character), positionBefore, 0.0F);
    EXPECT_NEAR(Controller(character).velocity.x, 4.0F, 1.0e-5F);
}

TEST_F(Character3DStepTest, TheBridgesSweepAgreesWithTheMoverAboutAFaceAndItsNormal) {
    const Scene::Entity floor = MakeFloor(0.0F);
    static_cast<void>(floor);
    const Scene::Entity wall = MakeBox({2.5F, 1.0F, 0.0F}, {0.5F, 1.5F, 5.0F});
    const Scene::Entity character = MakeCharacter({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F});

    CharacterWorldQuery world(entityManager, collisionSystem, character);
    ASSERT_TRUE(world.IsMovableCharacter());
    ExpectNear(world.GetCenter(), {0.0F, 0.5F, 0.0F}, 1.0e-5F);

    const std::optional<CharacterSlideCollision> hit =
        world.Sweep(world.GetCenter(), {4.0F, 0.0F, 0.0F}, 0.001F);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, wall);
    // The wall's face is at x = 2.0, the body's surface is 0.5 out from its center, and the query
    // keeps a 1 mm skin: 1.499 metres of travel, and a normal pointing back at the body.
    EXPECT_NEAR(hit->travel, 1.499F, 1.0e-3F);
    EXPECT_NEAR(hit->normal.x, -1.0F, 1.0e-3F);
    EXPECT_NEAR(hit->normal.y, 0.0F, 1.0e-3F);

    // A downward sweep hits the floor, and names the floor: the body's own geometry is never its
    // own obstacle, however long the sweep is.
    const std::optional<CharacterSlideCollision> below =
        world.Sweep(world.GetCenter(), {0.0F, -4.0F, 0.0F}, 0.001F);
    ASSERT_TRUE(below.has_value());
    EXPECT_NE(below->entity, character);
    EXPECT_NEAR(below->normal.y, 1.0F, 1.0e-3F);
}

TEST_F(Character3DStepTest, TheBridgeReportsOverlapsWithNormalsPointingBackAtTheBody) {
    const Scene::Entity floor = MakeFloor(0.0F);
    static_cast<void>(floor);
    // The body is spawned half a metre inside the wall's face.
    const Scene::Entity wall = MakeBox({2.5F, 1.0F, 0.0F}, {0.5F, 1.5F, 5.0F});
    const Scene::Entity character = MakeCharacter({1.75F, 1.0F, 0.0F}, {0.5F, 0.5F, 0.5F});

    CharacterWorldQuery world(entityManager, collisionSystem, character);
    const std::vector<CharacterSlideCollision> overlaps = world.GetOverlaps();
    ASSERT_FALSE(overlaps.empty());
    const auto found = std::find_if(overlaps.begin(), overlaps.end(),
                                    [wall](const CharacterSlideCollision& overlap) {
                                        return overlap.entity == wall;
                                    });
    ASSERT_NE(found, overlaps.end());
    EXPECT_NEAR(found->normal.x, -1.0F, 1.0e-3F);
    EXPECT_NEAR(found->depth, 0.25F, 2.0e-2F);

    // And a body standing free of everything reports nothing at all.
    Scene::TransformComponent transform =
        entityManager.GetComponent<Scene::TransformComponent>(character);
    transform.localPosition = {0.0F, 3.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, character, transform);
    sceneGraph.Update(entityManager);
    CharacterWorldQuery clearWorld(entityManager, collisionSystem, character);
    EXPECT_TRUE(clearWorld.GetOverlaps().empty());
}

TEST_F(Character3DStepTest, AQueryForAnEntityThatIsNotACharacterIsNotMovable) {
    const Scene::Entity plain = MakeBox({0.0F, 1.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    CharacterWorldQuery world(entityManager, collisionSystem, plain);
    EXPECT_FALSE(world.IsMovableCharacter());
    // Everything it is asked about *moving* answers "nothing".
    EXPECT_TRUE(world.GetOverlaps().empty());
    EXPECT_FALSE(world.Sweep({0.0F, 1.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 0.0F).has_value());
    // Where an entity is, is a fact about the world and not a question about the character: it is
    // answered whether or not this query could move the body.
    ASSERT_TRUE(world.TryGetEntityCenter(plain).has_value());
    ExpectNear(*world.TryGetEntityCenter(plain), {0.0F, 1.0F, 0.0F}, 1.0e-5F);

    // And moving a body through it is refused rather than silently moving it from nowhere.
    Scene::CharacterControllerComponent settings;
    settings.builtInMovement = false;
    CharacterMotionState state;
    const CharacterMotionResult result = Character3D::MoveAndSlide(
        world, Scene::MakeCharacterMotionConfig(settings), state, {1.0F, 0.0F, 0.0F}, kDeltaTime);
    EXPECT_FALSE(result.IsAccepted());
    EXPECT_EQ(result.code, CharacterMoveCode::InvalidWorld);
    ExpectNear(state.lastMotion, {0.0F, 0.0F, 0.0F}, 0.0F);
}

TEST_F(Character3DStepTest, ASurfaceVelocityIsReportedForRigidBodiesAndThePlatformCentreForEntities) {
    const Scene::Entity floor = MakeFloor(0.0F);
    sceneGraph.Update(entityManager);
    const Scene::Entity crate = MakeBox({3.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F});
    Scene::Rigid3DComponent crateBody;
    crateBody.velocity = {1.5F, 0.0F, -0.5F};
    entityManager.AddComponent<Scene::Rigid3DComponent>(crate, crateBody);
    const Scene::Entity character = MakeCharacter({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F});

    CharacterWorldQuery world(entityManager, collisionSystem, character);
    ExpectNear(world.GetSurfaceVelocity(crate), {1.5F, 0.0F, -0.5F}, 1.0e-5F);
    // Static geometry has no surface velocity: it is not going anywhere.
    ExpectNear(world.GetSurfaceVelocity(floor), {0.0F, 0.0F, 0.0F}, 1.0e-5F);

    const std::optional<Math::Vector3> centre = world.TryGetEntityCenter(crate);
    ASSERT_TRUE(centre.has_value());
    ExpectNear(*centre, {3.0F, 0.5F, 0.0F}, 1.0e-5F);
    EXPECT_FALSE(world.TryGetEntityCenter(Scene::Entity{}).has_value());
}

TEST_F(Character3DStepTest, TheSameWorldAndTheSameStepsGiveTheSameAnswerEveryTime) {
    // Two identical worlds, walked identically: the mover promises determinism, and this is where a
    // tie broken by storage order instead of by entity index would show up.
    const auto runOnce = [this]() {
        Memory::MemoryManager localMemory;
        Events::EventSystem localEvents;
        Scene::EntityManager localEntities{localMemory.GetDefaultAllocator(), localEvents};
        Scene::SceneGraph localGraph;
        CollisionSystem localCollision;

        std::vector<Scene::Entity> obstacles;
        for (int index = 0; index < 4; ++index) {
            const float x = 1.0F + static_cast<float>(index) * 0.75F;
            const Scene::Entity entity = localEntities.CreateEntity();
            Scene::TransformComponent transform;
            transform.localPosition = {x, 0.5F, 0.0F};
            localGraph.AttachTransform(localEntities, entity, transform);
            localGraph.Update(localEntities);
            Scene::ColliderComponent collider{{0.25F, 0.5F, 0.25F}};
            localEntities.AddComponent<Scene::ColliderComponent>(entity, collider);
            obstacles.push_back(entity);
        }
        const Scene::Entity moverEntity = localEntities.CreateEntity();
        Scene::TransformComponent moverTransform;
        moverTransform.localPosition = {0.0F, 0.5F, 0.0F};
        localGraph.AttachTransform(localEntities, moverEntity, moverTransform);
        localGraph.Update(localEntities);
        localEntities.AddComponent<Scene::ColliderComponent>(
            moverEntity, Scene::ColliderComponent{{0.25F, 0.5F, 0.25F}});
        Scene::CharacterControllerComponent settings;
        settings.builtInMovement = false;
        localEntities.AddComponent<Scene::CharacterControllerComponent>(moverEntity, settings);

        std::vector<Math::Vector3> path;
        for (int step = 0; step < 40; ++step) {
            localEntities.GetComponent<Scene::CharacterControllerComponent>(moverEntity).velocity =
                Math::Vector3{3.0F, 0.0F, 0.5F};
            static_cast<void>(StepCharacter3D(localEntities, localGraph, localCollision, moverEntity, {},
                                                 -9.81F, 1.0F / 60.0F));
            path.push_back(localEntities.GetComponent<Scene::TransformComponent>(moverEntity).localPosition);
        }
        return path;
    };

    const std::vector<Math::Vector3> first = runOnce();
    const std::vector<Math::Vector3> second = runOnce();
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t index = 0; index < first.size(); ++index) {
        ExpectNear(first[index], second[index], 0.0F);
    }
    // And the walk actually went somewhere - up to the first obstacle and no further - so this is
    // not a test of two bodies that never moved.
    EXPECT_NEAR(first.back().x, 0.499F, 5.0e-3F);
    EXPECT_LT(first.back().x, 0.51F);
}

// =================================================================================================
// Pushing: the one thing a character does to another body. The mover reports contacts; the bridge
// turns the contacts into velocity for the rigid bodies that were in the way.
// =================================================================================================

TEST_F(Character3DStepTest, AWalkIntoARigidBodyPushesItAndSaysSo) {
    const Scene::Entity floor = MakeFloor(0.0F);
    static_cast<void>(floor);

    // A dynamic box in the way, one metre of walking from the start.
    const Scene::Entity target = MakeBox({2.5F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F});
    entityManager.AddComponent<Scene::Rigid3DComponent>(target, Scene::Rigid3DComponent{});

    Scene::CharacterControllerComponent settings;
    settings.builtInMovement = false;
    const Scene::Entity character = MakeCharacter({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F}, settings);

    const Physics::CharacterDynamicPushPolicy push{true, 1.0F, 5.0F, kDeltaTime};
    std::size_t pushedTotal = 0U;
    for (int step = 0; step < 60; ++step) {
        entityManager.GetComponent<Scene::CharacterControllerComponent>(character).velocity =
            Math::Vector3{3.0F, 0.0F, 0.0F};
        const Character3DStepResult report = Step(character);
        ASSERT_TRUE(report.stepped) << "step " << step;
        pushedTotal += PushBodiesFromCharacterMove(entityManager, report.motion.collisions, push);
    }

    // The body was pushed the way the character was walking: away from it, along +x.
    const Scene::Rigid3DComponent& body = entityManager.GetComponent<Scene::Rigid3DComponent>(target);
    EXPECT_GT(body.velocity.x, 0.0F);
    EXPECT_LT(body.velocity.x, 5.0F + 1.0e-3F);
    EXPECT_NEAR(body.velocity.y, 0.0F, 1.0e-4F);
    EXPECT_NEAR(body.velocity.z, 0.0F, 1.0e-4F);
    EXPECT_GT(pushedTotal, 0U);

    // And the character stopped at the body rather than walking through it.
    EXPECT_LT(Position(character).x, 1.5F);
}

TEST_F(Character3DStepTest, APushNeverSlowsABodyThatIsAlreadyLeavingFaster) {
    const Scene::Entity floor = MakeFloor(0.0F);
    static_cast<void>(floor);
    const Scene::Entity target = MakeBox({2.5F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F});
    Scene::Rigid3DComponent rigid;
    // Already running away at four times the push's own speed cap.
    rigid.velocity = Math::Vector3{20.0F, 0.0F, 0.0F};
    entityManager.AddComponent<Scene::Rigid3DComponent>(target, rigid);

    Scene::CharacterControllerComponent settings;
    settings.builtInMovement = false;
    const Scene::Entity character = MakeCharacter({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F}, settings);

    const Physics::CharacterDynamicPushPolicy push{true, 1.0F, 5.0F, kDeltaTime};
    for (int step = 0; step < 60; ++step) {
        entityManager.GetComponent<Scene::CharacterControllerComponent>(character).velocity =
            Math::Vector3{3.0F, 0.0F, 0.0F};
        const Character3DStepResult report = Step(character);
        ASSERT_TRUE(report.stepped) << "step " << step;
        EXPECT_EQ(PushBodiesFromCharacterMove(entityManager, report.motion.collisions, push), 0U);
    }

    // Untouched: a push adds speed, it never brakes the thing it is pushing.
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::Rigid3DComponent>(target).velocity.x, 20.0F);
}

TEST_F(Character3DStepTest, OneBodyIsPushedOncePerStepAndUnmovableBodiesAreNotPushedAtAll) {
    const Scene::Entity floor = MakeFloor(0.0F);
    static_cast<void>(floor);
    const Scene::Entity dynamicBody = MakeBox({2.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F});
    entityManager.AddComponent<Scene::Rigid3DComponent>(dynamicBody, Scene::Rigid3DComponent{});
    const Scene::Entity kinematicBody = MakeBox({2.0F, 1.5F, 0.0F}, {0.5F, 0.5F, 0.5F});
    Scene::Rigid3DComponent kinematic;
    kinematic.isKinematic = true;
    entityManager.AddComponent<Scene::Rigid3DComponent>(kinematicBody, kinematic);
    const Scene::Entity weightlessBody = MakeBox({2.0F, 2.5F, 0.0F}, {0.5F, 0.5F, 0.5F});
    Scene::Rigid3DComponent weightless;
    weightless.mass = 0.0F;
    entityManager.AddComponent<Scene::Rigid3DComponent>(weightlessBody, weightless);

    // The same body met twice in one step - a corner contact and a face contact - plus bodies the
    // character has no business moving.
    const Math::Vector3 intoTheWall{3.0F, 0.0F, 0.0F};
    std::vector<Scene::CharacterSlideCollision> contacts;
    for (int repeat = 0; repeat < 2; ++repeat) {
        Scene::CharacterSlideCollision contact;
        contact.entity = dynamicBody;
        contact.normal = Math::Vector3{-1.0F, 0.0F, 0.0F};
        contact.motion = intoTheWall;
        contact.travel = 0.0F;
        contacts.push_back(contact);
    }
    Scene::CharacterSlideCollision onTheKinematic;
    onTheKinematic.entity = kinematicBody;
    onTheKinematic.normal = Math::Vector3{-1.0F, 0.0F, 0.0F};
    onTheKinematic.motion = intoTheWall;
    contacts.push_back(onTheKinematic);
    Scene::CharacterSlideCollision onTheWeightless;
    onTheWeightless.entity = weightlessBody;
    onTheWeightless.normal = Math::Vector3{-1.0F, 0.0F, 0.0F};
    onTheWeightless.motion = intoTheWall;
    contacts.push_back(onTheWeightless);

    // The cap is high enough that pushing the same body twice would be visible: 3 m/s of pressing
    // speed over a 60 Hz step is 180 m/s of push, so a second push would double it.
    const Physics::CharacterDynamicPushPolicy push{true, 1.0F, 1000.0F, kDeltaTime};
    EXPECT_EQ(PushBodiesFromCharacterMove(entityManager, contacts, push), 1U);
    EXPECT_NEAR(entityManager.GetComponent<Scene::Rigid3DComponent>(dynamicBody).velocity.x, 180.0F, 0.5F);
    EXPECT_EQ(entityManager.GetComponent<Scene::Rigid3DComponent>(kinematicBody).velocity.x, 0.0F);
    EXPECT_EQ(entityManager.GetComponent<Scene::Rigid3DComponent>(weightlessBody).velocity.x, 0.0F);
}

TEST_F(Character3DStepTest, ABodyThatIsNotBeingSimulatedIsNotPushedByAWalkIntoIt) {
    const Scene::Entity floor = MakeFloor(0.0F);
    static_cast<void>(floor);
    // Two bodies in the character's path. One is stopped with MakeStatic and one is taken out of
    // the world entirely; a walk into either must leave its velocity alone, whatever the push
    // policy says - a push is something the simulation does to a body it is simulating.
    const Scene::Entity parked = MakeBox({2.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F});
    entityManager.AddComponent<Scene::Rigid3DComponent>(parked, Scene::Rigid3DComponent{});
    Scene::ProcessComponent stopped{};
    stopped.mode = Scene::TickMode::Never;
    entityManager.AddComponent<Scene::ProcessComponent>(parked, stopped);
    Scene::PhysicsObjectComponent parkedObject{};
    parkedObject.disableMode = Scene::PhysicsObjectDisableMode::MakeStatic;
    entityManager.AddComponent<Scene::PhysicsObjectComponent>(parked, parkedObject);

    const Scene::Entity gone = MakeBox({2.0F, 1.5F, 0.0F}, {0.5F, 0.5F, 0.5F});
    entityManager.AddComponent<Scene::Rigid3DComponent>(gone, Scene::Rigid3DComponent{});
    entityManager.AddComponent<Scene::ProcessComponent>(gone, stopped);
    Scene::PhysicsObjectComponent goneObject{};
    goneObject.disableMode = Scene::PhysicsObjectDisableMode::Remove;
    entityManager.AddComponent<Scene::PhysicsObjectComponent>(gone, goneObject);

    // Both get contacts handed straight to the push, as if the mover had reported them.
    const Math::Vector3 intoThem{3.0F, 0.0F, 0.0F};
    std::vector<Scene::CharacterSlideCollision> contacts;
    for (const Scene::Entity entity : {parked, gone}) {
        Scene::CharacterSlideCollision contact;
        contact.entity = entity;
        contact.normal = Math::Vector3{-1.0F, 0.0F, 0.0F};
        contact.motion = intoThem;
        contacts.push_back(contact);
    }
    sceneGraph.Update(entityManager);

    EXPECT_EQ(PushBodiesFromCharacterMove(
                  entityManager, contacts,
                  Physics::CharacterDynamicPushPolicy{true, 1.0F, 5.0F, kDeltaTime}),
              0U);
    EXPECT_EQ(entityManager.GetComponent<Scene::Rigid3DComponent>(parked).velocity.x, 0.0F);
    EXPECT_EQ(entityManager.GetComponent<Scene::Rigid3DComponent>(gone).velocity.x, 0.0F);

    // And the same contacts against a body that is being simulated still push, so the test is
    // about the state and not about the contact list.
    const Scene::Entity live = MakeBox({2.0F, 2.5F, 0.0F}, {0.5F, 0.5F, 0.5F});
    entityManager.AddComponent<Scene::Rigid3DComponent>(live, Scene::Rigid3DComponent{});
    Scene::CharacterSlideCollision liveContact;
    liveContact.entity = live;
    liveContact.normal = Math::Vector3{-1.0F, 0.0F, 0.0F};
    liveContact.motion = intoThem;
    EXPECT_EQ(PushBodiesFromCharacterMove(
                  entityManager, std::vector<Scene::CharacterSlideCollision>{liveContact},
                  Physics::CharacterDynamicPushPolicy{true, 1.0F, 5.0F, kDeltaTime}),
              1U);
    EXPECT_NEAR(entityManager.GetComponent<Scene::Rigid3DComponent>(live).velocity.x, 5.0F, 1.0e-3F);
}
TEST_F(Character3DStepTest, APushPolicyThatIsOffOrNonsenseLeavesEveryBodyAlone) {
    const Scene::Entity floor = MakeFloor(0.0F);
    static_cast<void>(floor);
    const Scene::Entity body = MakeBox({2.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F});
    entityManager.AddComponent<Scene::Rigid3DComponent>(body, Scene::Rigid3DComponent{});

    Scene::CharacterSlideCollision contact;
    contact.entity = body;
    contact.normal = Math::Vector3{-1.0F, 0.0F, 0.0F};
    contact.motion = Math::Vector3{3.0F, 0.0F, 0.0F};
    const std::vector<Scene::CharacterSlideCollision> contacts{contact};

    // Off, no strength, no time to push over: three ways to say "nothing happens".
    for (const Physics::CharacterDynamicPushPolicy& policy :
         {Physics::CharacterDynamicPushPolicy{false, 1.0F, 5.0F, kDeltaTime},
          Physics::CharacterDynamicPushPolicy{true, 0.0F, 5.0F, kDeltaTime},
          Physics::CharacterDynamicPushPolicy{true, 1.0F, 5.0F, 0.0F}}) {
        EXPECT_EQ(PushBodiesFromCharacterMove(entityManager, contacts, policy), 0U);
    }
    // The default fallback for a nonsense cap is the engine's own, not an infinite push.
    EXPECT_EQ(PushBodiesFromCharacterMove(
                  entityManager, contacts,
                  Physics::CharacterDynamicPushPolicy{true, 1.0F, -1.0F, kDeltaTime}),
              1U);
    EXPECT_NEAR(entityManager.GetComponent<Scene::Rigid3DComponent>(body).velocity.x, 5.0F, 1.0e-3F);
}

} // namespace
} // namespace UVE::Physics::Tests
