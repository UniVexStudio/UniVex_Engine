// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Tests for the Character3D runtime: the node-layer mover (Character3DUVE) against a fake world it
// can be reasoned about exactly, and then the same mover against the engine's real collision world
// through the Physics-layer bridge (CharacterWorldQueryUVE / StepCharacter3DUVE).
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

#include "uve/component/collider_component_uve.h"
#include "uve/component/character_controller_component_uve.h"
#include "uve/component/physics_object_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/component/solid_body_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/character_3d_uve.h"
#include "uve/physics/character_world_query_uve.h"
#include "uve/physics/collision_system_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Physics::Tests {
namespace {

using Scene::Character3DUVE;
using Scene::CharacterMotionConfigUVE;
using Scene::CharacterMotionResultUVE;
using Scene::CharacterMotionStateUVE;
using Scene::CharacterMoveCodeUVE;
using Scene::CharacterSlideCollisionUVE;
using Scene::CharacterSurfaceKindUVE;

constexpr float kToleranceUVE = 1.0e-4F;

[[nodiscard]] Scene::EntityUVE MakeEntityUVE(const std::uint32_t index) noexcept {
    Scene::EntityUVE entity;
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

struct FakeBoxUVE final {
    Scene::EntityUVE entity;
    Math::Vector3UVE min;
    Math::Vector3UVE max;
};

class FakeCharacterWorldUVE final : public Scene::ICharacterWorldQueryUVE {
public:
    Math::Vector3UVE center{};
    Math::Vector3UVE halfExtents{};
    std::vector<FakeBoxUVE> boxes;
    std::vector<CharacterSlideCollisionUVE> overlaps;
    std::unordered_map<std::uint32_t, Math::Vector3UVE> positions;
    std::unordered_map<std::uint32_t, Math::Vector3UVE> velocities;
    /// Hits to report ahead of the real geometry, consumed one per sweep - for testing contact
    /// shapes (a slope, a moving surface) that axis-aligned boxes cannot express.
    mutable std::vector<CharacterSlideCollisionUVE> scriptedHits;
    mutable std::size_t scriptedHitIndex = 0U;

    [[nodiscard]] Math::Vector3UVE GetCenterUVE() const override {
        return center;
    }

    [[nodiscard]] std::vector<CharacterSlideCollisionUVE> GetOverlapsUVE() const override {
        return overlaps;
    }

    [[nodiscard]] std::optional<CharacterSlideCollisionUVE> SweepUVE(
        const Math::Vector3UVE& sweepCenter, const Math::Vector3UVE& motion,
        const float margin) const override {
        if (scriptedHitIndex < scriptedHits.size()) {
            CharacterSlideCollisionUVE hit = scriptedHits[scriptedHitIndex];
            ++scriptedHitIndex;
            const float motionLength = Math::LengthUVE(motion);
            hit.travel = std::min(hit.travel, std::max(0.0F, motionLength - margin));
            return hit;
        }
        const float motionLength = Math::LengthUVE(motion);
        if (!std::isfinite(motionLength) || motionLength <= 0.0F) {
            return std::nullopt;
        }
        const Math::AabbUVE moving = Math::AabbUVE::FromCenterExtentsUVE(sweepCenter, halfExtents);
        std::optional<CharacterSlideCollisionUVE> best;
        float bestDistance = std::numeric_limits<float>::infinity();
        for (const FakeBoxUVE& box : boxes) {
            const std::optional<Math::SweptAabbHitUVE> swept =
                Math::SweepAabbUVE(moving, motion, Math::AabbUVE{box.min, box.max});
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
            CharacterSlideCollisionUVE hit;
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

    [[nodiscard]] Math::Vector3UVE GetSurfaceVelocityUVE(const Scene::EntityUVE entity) const override {
        const auto found = velocities.find(entity.index);
        return found != velocities.end() ? found->second : Math::Vector3UVE{};
    }

    [[nodiscard]] std::optional<Math::Vector3UVE> TryGetEntityCenterUVE(
        const Scene::EntityUVE entity) const override {
        const auto found = positions.find(entity.index);
        if (found == positions.end()) {
            return std::nullopt;
        }
        return found->second;
    }
};

[[nodiscard]] CharacterMotionConfigUVE DefaultConfigUVE() noexcept {
    CharacterMotionConfigUVE config;
    // A hair of skin, the same order the engine's own controller uses: a resting body stays
    // well-posed and contacts never land exactly on a face.
    config.safeMargin = 0.001F;
    return config;
}

void AddBoxUVE(FakeCharacterWorldUVE& world, const Scene::EntityUVE entity, const Math::Vector3UVE& min,
               const Math::Vector3UVE& max) {
    world.boxes.push_back(FakeBoxUVE{entity, min, max});
}

void AddFloorUVE(FakeCharacterWorldUVE& world, const float topY,
                 const Scene::EntityUVE entity = MakeEntityUVE(1U)) {
    AddBoxUVE(world, entity, {-100.0F, topY - 1.0F, -100.0F}, {100.0F, topY, 100.0F});
}

void AddWallUVE(FakeCharacterWorldUVE& world, const float faceX, const float nearY = -100.0F,
                const float farY = 100.0F, const Scene::EntityUVE entity = MakeEntityUVE(2U)) {
    AddBoxUVE(world, entity, {faceX, nearY, -100.0F}, {faceX + 1.0F, farY, 100.0F});
}

void AddCeilingUVE(FakeCharacterWorldUVE& world, const float undersideY, const float fromX = -100.0F,
                   const float toX = 100.0F, const Scene::EntityUVE entity = MakeEntityUVE(3U)) {
    AddBoxUVE(world, entity, {fromX, undersideY, -100.0F}, {toX, undersideY + 1.0F, 100.0F});
}

void ExpectNearUVE(const Math::Vector3UVE& actual, const Math::Vector3UVE& expected, const float tolerance) {
    EXPECT_NEAR(actual.x, expected.x, tolerance) << "x";
    EXPECT_NEAR(actual.y, expected.y, tolerance) << "y";
    EXPECT_NEAR(actual.z, expected.z, tolerance) << "z";
}

// =================================================================================================
// Surface classification: what the body is touching, and at what angle.
// =================================================================================================

TEST(Character3DSurfaceUVETest, AFloorIsWhatTheBodyCanStandOnAndAWallIsWhatItCannot) {
    float angle = 0.0F;
    EXPECT_EQ(Scene::ClassifyCharacterSurfaceUVE({0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 45.0F, angle), CharacterSurfaceKindUVE::Floor);
    EXPECT_NEAR(angle, 0.0F, kToleranceUVE);

    EXPECT_EQ(Scene::ClassifyCharacterSurfaceUVE({0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F}, 45.0F, angle), CharacterSurfaceKindUVE::Wall);
    EXPECT_NEAR(angle, 90.0F, kToleranceUVE);

    EXPECT_EQ(Scene::ClassifyCharacterSurfaceUVE({0.0F, -1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 45.0F, angle), CharacterSurfaceKindUVE::Ceiling);
    EXPECT_NEAR(angle, 180.0F, kToleranceUVE);
}

TEST(Character3DSurfaceUVETest, TheFloorLimitIsInclusiveSoARampExactlyAtItIsStillWalked) {
    const Math::Vector3UVE exactlyAtTheLimit{0.0F, 1.0F, -1.0F};
    float angle = 0.0F;
    EXPECT_EQ(Scene::ClassifyCharacterSurfaceUVE(exactlyAtTheLimit, {0.0F, 1.0F, 0.0F}, 45.0F, angle),
              CharacterSurfaceKindUVE::Floor);
    EXPECT_NEAR(angle, 45.0F, 1.0e-3F);

    // A hair steeper, and it is a wall.
    EXPECT_EQ(Scene::ClassifyCharacterSurfaceUVE({0.0F, 1.0F, -1.01F}, {0.0F, 1.0F, 0.0F}, 45.0F, angle),
              CharacterSurfaceKindUVE::Wall);
    EXPECT_GT(angle, 45.0F);
}

TEST(Character3DSurfaceUVETest, ADegenerateNormalIsNotASurfaceAndZeroesTheAngle) {
    float angle = 123.0F;
    EXPECT_EQ(Scene::ClassifyCharacterSurfaceUVE({0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 45.0F, angle),
              CharacterSurfaceKindUVE::Invalid);
    EXPECT_NEAR(angle, 0.0F, kToleranceUVE);
    EXPECT_EQ(Scene::ClassifyCharacterSurfaceUVE({0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 0.0F, angle),
              CharacterSurfaceKindUVE::Floor);
}

TEST(Character3DSurfaceUVETest, SlopeAngleIsMeasuredFromUpAndSurvivesBadInput) {
    EXPECT_NEAR(Character3DUVE::SlopeAngleDegreesUVE({0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}), 0.0F, kToleranceUVE);
    EXPECT_NEAR(Character3DUVE::SlopeAngleDegreesUVE({0.0F, -1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}), 180.0F, kToleranceUVE);
    EXPECT_NEAR(Character3DUVE::SlopeAngleDegreesUVE({0.0F, 1.0F, -1.0F}, {0.0F, 1.0F, 0.0F}), 45.0F, 1.0e-3F);
    EXPECT_NEAR(Character3DUVE::SlopeAngleDegreesUVE({0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}), 0.0F, kToleranceUVE);
}

// =================================================================================================
// Walking: sliding, stopping, stepping, snapping and riding, all against the box world above.
// =================================================================================================

TEST(Character3DMoverUVETest, AFreeMoveGoesWhereItWasAskedAndTouchesNothing) {
    FakeCharacterWorldUVE world;
    CharacterMotionStateUVE state;
    state.velocity = {1.0F, 0.0F, 0.0F};
    const CharacterMotionResultUVE result =
        Character3DUVE::MoveAndSlideUVE(world, DefaultConfigUVE(), state, {1.0F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAcceptedUVE());
    EXPECT_EQ(result.code, CharacterMoveCodeUVE::Moved);
    EXPECT_FALSE(result.blocked);
    EXPECT_FALSE(result.onFloor);
    EXPECT_FALSE(result.onWall);
    EXPECT_FALSE(result.onCeiling);
    EXPECT_EQ(result.slideCount, 0U);
    EXPECT_EQ(result.contactCount, 0U);
    EXPECT_EQ(result.GetCollisionCountUVE(), 0U);
    ExpectNearUVE(result.appliedMotion, {1.0F, 0.0F, 0.0F}, 1.0e-5F);
    ExpectNearUVE(result.finalCenter, {1.0F, 0.0F, 0.0F}, 1.0e-5F);
    ExpectNearUVE(state.lastMotion, {1.0F, 0.0F, 0.0F}, 1.0e-5F);
    // Real velocity is what the world allowed, not what was asked for: here, the same thing.
    ExpectNearUVE(state.realVelocity, {10.0F, 0.0F, 0.0F}, 1.0e-3F);
    EXPECT_FALSE(result.carriedByPlatform);
    EXPECT_FALSE(result.depenetrated);
    EXPECT_FALSE(result.configClamped);
    EXPECT_FALSE(result.snapUsed);
    EXPECT_EQ(result.GetCollisionUVE(0U), nullptr);
}

TEST(Character3DMoverUVETest, AWallStopsTheBodyASkinShortOfItAndSaysWhichWall) {
    FakeCharacterWorldUVE world;
    AddWallUVE(world, 1.0F);
    CharacterMotionStateUVE state;
    state.velocity = {2.0F, 0.0F, 0.0F};
    const CharacterMotionResultUVE result =
        Character3DUVE::MoveAndSlideUVE(world, DefaultConfigUVE(), state, {2.0F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAcceptedUVE());
    EXPECT_TRUE(result.blocked);
    EXPECT_TRUE(result.onWall);
    EXPECT_FALSE(result.onFloor);
    ExpectNearUVE(state.lastMotion, {0.999F, 0.0F, 0.0F}, 2.0e-3F);
    ExpectNearUVE(result.wallNormal, {-1.0F, 0.0F, 0.0F}, 1.0e-3F);
    // One wall, one contact, however many slides it took to stop against it.
    EXPECT_EQ(result.contactCount, 1U);
    ASSERT_EQ(result.collisions.size(), 1U);
    EXPECT_EQ(result.GetCollisionUVE(0U)->entity.index, 2U);
    EXPECT_FALSE(result.GetCollisionUVE(0U)->steppedUp);
    EXPECT_NEAR(result.GetCollisionUVE(0U)->depth, 0.0F, kToleranceUVE);
}

TEST(Character3DMoverUVETest, AWallKeepsTheMotionThatWasNotGoingIntoIt) {
    FakeCharacterWorldUVE world;
    AddWallUVE(world, 1.0F);
    CharacterMotionStateUVE state;
    state.velocity = {2.0F, 0.0F, 1.0F};
    const CharacterMotionResultUVE result =
        Character3DUVE::MoveAndSlideUVE(world, DefaultConfigUVE(), state, {2.0F, 0.0F, 1.0F}, 0.1F);

    ASSERT_TRUE(result.IsAcceptedUVE());
    // x: 1 mm short of the wall. z: the half it travelled getting there, then the half it slid.
    ExpectNearUVE(state.lastMotion, {0.999F, 0.0F, 1.0F}, 3.0e-3F);
    // Blocked means "some of what was asked for did not happen", and the x that the wall ate is
    // exactly that, even though the body kept moving along the wall.
    EXPECT_TRUE(result.blocked);
    EXPECT_TRUE(result.onWall);
    EXPECT_EQ(result.contactCount, 1U);
}

TEST(Character3DMoverUVETest, ASteepRampIsStoppedAgainstInsteadOfSlidAlong) {
    FakeCharacterWorldUVE world;
    // 50 degrees: past the 45 degree floor limit, inside the 15 degree slide band's far side.
    const Math::Vector3UVE rampNormal = Math::NormalizeUVE(Math::Vector3UVE{0.0F, 1.0F, -1.19F});
    CharacterSlideCollisionUVE ramp;
    ramp.entity = MakeEntityUVE(4U);
    ramp.normal = rampNormal;
    ramp.travel = 0.99F;
    world.scriptedHits.push_back(ramp);
    CharacterMotionStateUVE state;
    state.velocity = {0.0F, 0.0F, 1.0F};
    const CharacterMotionResultUVE result =
        Character3DUVE::MoveAndSlideUVE(world, DefaultConfigUVE(), state, {0.0F, 0.0F, 1.0F}, 0.1F);

    ASSERT_TRUE(result.IsAcceptedUVE());
    EXPECT_TRUE(result.blocked);
    EXPECT_TRUE(result.onWall);
    // The ramp band stops the body dead rather than letting it creep up the slope.
    ExpectNearUVE(state.lastMotion, {0.0F, 0.0F, 0.99F}, 2.0e-3F);
    ExpectNearUVE(result.wallNormal, rampNormal, 1.0e-3F);
}

TEST(Character3DMoverUVETest, ACeilingStopsTheRiseAndIsSlidAlongWhenThatIsAllowed) {
    // A ceiling a quarter of a metre up, walked under diagonally: the body gets there, and the
    // question is what it is allowed to do afterwards.
    const auto runOnce = [](const bool slideOnCeiling) {
        FakeCharacterWorldUVE world;
        AddCeilingUVE(world, 0.25F);
        CharacterMotionConfigUVE config = DefaultConfigUVE();
        config.slideOnCeiling = slideOnCeiling;
        CharacterMotionStateUVE state;
        state.velocity = {1.0F, 1.0F, 0.0F};
        const CharacterMotionResultUVE result =
            Character3DUVE::MoveAndSlideUVE(world, config, state, {1.0F, 1.0F, 0.0F}, 0.1F);
        return std::pair<CharacterMotionResultUVE, Math::Vector3UVE>{result, state.lastMotion};
    };

    // Sliding: the rise stops at the ceiling and the walk keeps every millimetre of its x.
    const auto sliding = runOnce(true);
    ASSERT_TRUE(sliding.first.IsAcceptedUVE());
    EXPECT_TRUE(sliding.first.onCeiling);
    ExpectNearUVE(sliding.second, {1.0F, 0.249F, 0.0F}, 3.0e-3F);
    ExpectNearUVE(sliding.first.ceilingNormal, {0.0F, -1.0F, 0.0F}, 1.0e-3F);

    // Not sliding: the ceiling ends the move where it is - the body travelled to it, and no
    // further, sideways motion included.
    const auto blocked = runOnce(false);
    ASSERT_TRUE(blocked.first.IsAcceptedUVE());
    EXPECT_TRUE(blocked.first.blocked);
    EXPECT_TRUE(blocked.first.onCeiling);
    ExpectNearUVE(blocked.second, {0.249F, 0.249F, 0.0F}, 3.0e-3F);
    EXPECT_GT(sliding.second.x, blocked.second.x + 0.5F);
}

TEST(Character3DMoverUVETest, FloorBlockOnWallStopsTheMoveAtTheWallInsteadOfSlidingAlong) {
    // The same diagonal walk, run twice: with the setting off the body slides along the wall to its
    // full z distance; with it on the move ends at the wall, taking only the z it had already
    // travelled when it got there.
    FakeCharacterWorldUVE slidingWorld;
    AddWallUVE(slidingWorld, 1.5F);
    CharacterMotionStateUVE slidingState;
    slidingState.grounded = true;
    const CharacterMotionResultUVE sliding =
        Character3DUVE::MoveAndSlideUVE(slidingWorld, DefaultConfigUVE(), slidingState, {2.0F, 0.0F, 1.0F}, 0.1F);
    ASSERT_TRUE(sliding.IsAcceptedUVE());
    EXPECT_NEAR(slidingState.lastMotion.z, 1.0F, 3.0e-3F);

    FakeCharacterWorldUVE blockedWorld;
    AddWallUVE(blockedWorld, 1.5F);
    CharacterMotionConfigUVE config = DefaultConfigUVE();
    config.floorBlockOnWall = true;
    CharacterMotionStateUVE blockedState;
    blockedState.grounded = true;
    const CharacterMotionResultUVE blocked =
        Character3DUVE::MoveAndSlideUVE(blockedWorld, config, blockedState, {2.0F, 0.0F, 1.0F}, 0.1F);

    ASSERT_TRUE(blocked.IsAcceptedUVE());
    EXPECT_TRUE(blocked.blocked);
    EXPECT_NEAR(blockedState.lastMotion.x, 1.499F, 3.0e-3F);
    // The z the body had when it met the wall, and not a millimetre of the z it wanted after that.
    EXPECT_NEAR(blockedState.lastMotion.z, 0.7495F, 3.0e-3F);
    EXPECT_LT(blockedState.lastMotion.z, slidingState.lastMotion.z - 0.2F);
}

TEST(Character3DMoverUVETest, AWalkingBodyClimbsALowStepInsteadOfStoppingAtIt) {
    FakeCharacterWorldUVE world;
    AddFloorUVE(world, 0.0F);
    // A 0.2 m step, from x = 1 onwards: inside the default 0.3 m step height.
    world.boxes.push_back(FakeBoxUVE{MakeEntityUVE(5U), {1.0F, 0.0F, -2.0F}, {3.0F, 0.2F, 2.0F}});
    CharacterMotionStateUVE state;
    state.grounded = true;
    state.velocity = {1.0F, 0.0F, 0.0F};

    const CharacterMotionResultUVE result =
        Character3DUVE::MoveAndSlideUVE(world, DefaultConfigUVE(), state, {1.0F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAcceptedUVE());
    EXPECT_TRUE(result.stepUpUsed);
    EXPECT_TRUE(result.onFloor);
    EXPECT_FALSE(result.blocked);
    // The whole walk happened: a millimetre of approach, then the step carrying the body up and
    // over, and the body standing a skin above the tread.
    ExpectNearUVE(state.lastMotion, {1.0F, 0.201F, 0.0F}, 3.0e-3F);
    ExpectNearUVE(result.appliedMotion, {1.0F, 0.201F, 0.0F}, 3.0e-3F);
    // The step's own contribution: from where the body was when the step began.
    ExpectNearUVE(result.stepMotion, {0.001F, 0.201F, 0.0F}, 3.0e-3F);
    ASSERT_EQ(result.collisions.size(), 1U);
    EXPECT_TRUE(result.GetCollisionUVE(0U)->steppedUp);
    EXPECT_EQ(result.GetCollisionUVE(0U)->entity.index, 5U);
}

TEST(Character3DMoverUVETest, AStepTallerThanTheStepHeightIsAWall) {
    FakeCharacterWorldUVE world;
    AddFloorUVE(world, 0.0F);
    // 0.5 m against a 0.3 m step height: the lift cannot clear it, so it is a wall.
    world.boxes.push_back(FakeBoxUVE{MakeEntityUVE(5U), {1.0F, 0.0F, -2.0F}, {3.0F, 0.5F, 2.0F}});
    CharacterMotionStateUVE state;
    state.grounded = true;
    state.velocity = {1.0F, 0.0F, 0.0F};

    const CharacterMotionResultUVE result =
        Character3DUVE::MoveAndSlideUVE(world, DefaultConfigUVE(), state, {1.0F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAcceptedUVE());
    EXPECT_FALSE(result.stepUpUsed);
    EXPECT_TRUE(result.blocked);
    EXPECT_TRUE(result.onWall);
    ExpectNearUVE(state.lastMotion, {0.999F, 0.0F, 0.0F}, 3.0e-3F);
}

TEST(Character3DMoverUVETest, AStepWithNoHeadroomIsRefusedRatherThanForced) {
    FakeCharacterWorldUVE world;
    AddFloorUVE(world, 0.0F);
    world.boxes.push_back(FakeBoxUVE{MakeEntityUVE(5U), {1.0F, 0.0F, -2.0F}, {3.0F, 0.2F, 2.0F}});
    // A shelf 0.15 m above the body's feet: no room to stand up by a step.
    AddCeilingUVE(world, 0.15F, 0.0F, 4.0F, MakeEntityUVE(6U));
    CharacterMotionStateUVE state;
    state.grounded = true;
    state.velocity = {1.0F, 0.0F, 0.0F};

    const CharacterMotionResultUVE result =
        Character3DUVE::MoveAndSlideUVE(world, DefaultConfigUVE(), state, {1.0F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAcceptedUVE());
    EXPECT_FALSE(result.stepUpUsed);
    EXPECT_TRUE(result.blocked);
    ExpectNearUVE(state.lastMotion, {0.999F, 0.0F, 0.0F}, 3.0e-3F);
}

TEST(Character3DMoverUVETest, ABodyInTheAirDoesNotStepUpOntoAnything) {
    FakeCharacterWorldUVE world;
    AddFloorUVE(world, -2.0F);
    world.boxes.push_back(FakeBoxUVE{MakeEntityUVE(5U), {1.0F, -2.0F, -2.0F}, {3.0F, 0.2F, 2.0F}});
    CharacterMotionStateUVE state;
    state.grounded = false; // falling
    state.velocity = {1.0F, -2.0F, 0.0F};

    const CharacterMotionResultUVE result =
        Character3DUVE::MoveAndSlideUVE(world, DefaultConfigUVE(), state, {1.0F, -2.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAcceptedUVE());
    EXPECT_FALSE(result.stepUpUsed);
    EXPECT_TRUE(result.blocked);
    // Stopped against the step's face, still in the air, with the rest of the walk thrown away.
    EXPECT_NEAR(state.lastMotion.x, 0.999F, 3.0e-3F);
    EXPECT_LT(result.finalCenter.y, 0.0F);
}

TEST(Character3DMoverUVETest, AGroundedBodyFollowsTheFloorDownSmallSteps) {
    FakeCharacterWorldUVE world;
    AddFloorUVE(world, 0.0F);
    CharacterMotionStateUVE state;
    state.grounded = true;
    state.velocity = {1.0F, 0.0F, 0.0F};
    // Five centimetres above the floor, walking: the floor is close enough to be followed.
    world.center = {0.0F, 0.05F, 0.0F};

    const CharacterMotionResultUVE result =
        Character3DUVE::MoveAndSlideUVE(world, DefaultConfigUVE(), state, {0.1F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAcceptedUVE());
    EXPECT_TRUE(result.snapUsed);
    EXPECT_TRUE(result.onFloor);
    // It dropped towards the floor and stopped above it - a snap is a follow, not a drop.
    EXPECT_LT(result.finalCenter.y, 0.02F);
    EXPECT_GT(result.finalCenter.y, -1.0e-3F);
    ExpectNearUVE(result.snapMotion, {0.0F, result.finalCenter.y - 0.05F, 0.0F}, 1.0e-3F);
}

TEST(Character3DMoverUVETest, AJumpIsNotSnappedBackDownToTheFloor) {
    FakeCharacterWorldUVE world;
    AddFloorUVE(world, 0.0F);
    CharacterMotionConfigUVE config = DefaultConfigUVE();
    config.jumpedThisStep = true;
    CharacterMotionStateUVE state;
    state.grounded = true; // it was standing...
    state.velocity = {0.0F, 5.0F, 0.0F};

    const CharacterMotionResultUVE result =
        Character3DUVE::MoveAndSlideUVE(world, config, state, {0.0F, 0.5F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAcceptedUVE());
    EXPECT_FALSE(result.snapUsed);
    EXPECT_FALSE(result.onFloor);
    EXPECT_FALSE(state.grounded);
    ExpectNearUVE(state.lastMotion, {0.0F, 0.5F, 0.0F}, 1.0e-4F);
}

TEST(Character3DMoverUVETest, ABodyThatWasNotOnTheFloorIsNotSnappedToIt) {
    FakeCharacterWorldUVE world;
    AddFloorUVE(world, 0.0F);
    CharacterMotionStateUVE state;
    state.grounded = false;
    world.center = {0.0F, 0.05F, 0.0F};

    const CharacterMotionResultUVE result =
        Character3DUVE::MoveAndSlideUVE(world, DefaultConfigUVE(), state, {0.1F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAcceptedUVE());
    EXPECT_FALSE(result.snapUsed);
    EXPECT_FALSE(result.onFloor);
    EXPECT_NEAR(result.finalCenter.y, 0.05F, 1.0e-4F);
}

TEST(Character3DMoverUVETest, APatformUnderfootCarriesTheBodyAndItsMotion) {
    FakeCharacterWorldUVE world;
    AddFloorUVE(world, 0.0F, MakeEntityUVE(9U));
    world.center = {0.0F, 0.001F, 0.0F};
    world.positions[9U] = {0.0F, 0.0F, 0.0F};
    CharacterMotionStateUVE state;
    state.grounded = true;

    // First step: the body stands on the platform, and the move remembers where the platform was.
    const CharacterMotionResultUVE standing =
        Character3DUVE::MoveAndSlideUVE(world, DefaultConfigUVE(), state, {0.0F, 0.0F, 0.0F}, 0.1F);
    ASSERT_TRUE(standing.IsAcceptedUVE());
    ASSERT_EQ(state.platform.index, 9U);
    ASSERT_TRUE(state.hasPlatformWorldPosition);
    EXPECT_FALSE(standing.carriedByPlatform);

    // The platform moves half a metre in a 0.1 s step: 5 m/s, and the body comes with it.
    world.positions[9U] = {0.5F, 0.0F, 0.0F};
    const CharacterMotionResultUVE carried =
        Character3DUVE::MoveAndSlideUVE(world, DefaultConfigUVE(), state, {0.0F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(carried.IsAcceptedUVE());
    EXPECT_TRUE(carried.carriedByPlatform);
    ExpectNearUVE(carried.platformVelocity, {5.0F, 0.0F, 0.0F}, 1.0e-3F);
    ExpectNearUVE(state.lastMotion, {0.5F, 0.0F, 0.0F}, 2.0e-3F);
    ExpectNearUVE(state.realVelocity, {5.0F, 0.0F, 0.0F}, 2.0e-2F);
    EXPECT_TRUE(carried.onFloor);
    EXPECT_EQ(state.platform.index, 9U);
}

TEST(Character3DMoverUVETest, LeavingAPlatformHandsOverItsVelocityOrDoesNotAccordingToTheMode) {
    const auto runOnce = [](const Scene::CharacterPlatformLeaveModeUVE mode) {
        FakeCharacterWorldUVE world;
        world.positions[7U] = {10.0F, 3.0F, 0.0F};
        CharacterMotionConfigUVE config = DefaultConfigUVE();
        config.platformOnLeave = mode;
        CharacterMotionStateUVE state;
        state.grounded = true;
        state.platform = MakeEntityUVE(7U);
        state.platformWorldPosition = {9.5F, 2.7F, 0.0F};
        state.hasPlatformWorldPosition = true;
        const CharacterMotionResultUVE result =
            Character3DUVE::MoveAndSlideUVE(world, config, state, {0.0F, 0.0F, 0.0F}, 0.1F);
        return std::pair<CharacterMotionResultUVE, Math::Vector3UVE>{result, state.velocity};
    };

    // AddVelocity: the body keeps the platform's whole velocity as it leaves.
    const auto add = runOnce(Scene::CharacterPlatformLeaveModeUVE::AddVelocity);
    EXPECT_TRUE(add.first.leftPlatform);
    EXPECT_FALSE(add.first.onFloor);
    ExpectNearUVE(add.second, {5.0F, 3.0F, 0.0F}, 1.0e-3F);
    // And it was carried by the platform while it was still standing on it.
    EXPECT_TRUE(add.first.carriedByPlatform);

    // AddUpwardVelocity: only the part that pushes the body up, never the sideways motion.
    const auto upward = runOnce(Scene::CharacterPlatformLeaveModeUVE::AddUpwardVelocity);
    EXPECT_TRUE(upward.first.leftPlatform);
    ExpectNearUVE(upward.second, {0.0F, 3.0F, 0.0F}, 1.0e-3F);

    // KeepVelocity: the body keeps only what it had of its own.
    const auto keep = runOnce(Scene::CharacterPlatformLeaveModeUVE::KeepVelocity);
    EXPECT_TRUE(keep.first.leftPlatform);
    ExpectNearUVE(keep.second, {0.0F, 0.0F, 0.0F}, 1.0e-3F);

    // AddVelocity with nothing to hand over: the new platform is gone before the body leaves it.
    FakeCharacterWorldUVE vanishedWorld;
    CharacterMotionStateUVE vanishedState;
    vanishedState.grounded = true;
    vanishedState.platform = MakeEntityUVE(7U);
    vanishedState.platformWorldPosition = {9.5F, 2.7F, 0.0F};
    vanishedState.hasPlatformWorldPosition = true;
    const CharacterMotionResultUVE vanished = Character3DUVE::MoveAndSlideUVE(
        vanishedWorld, DefaultConfigUVE(), vanishedState, {0.0F, 0.0F, 0.0F}, 0.1F);
    EXPECT_TRUE(vanished.leftPlatform);
    ExpectNearUVE(vanished.platformVelocity, {0.0F, 0.0F, 0.0F}, 1.0e-5F);
    EXPECT_EQ(vanishedState.platform, Scene::EntityUVE{});
}

TEST(Character3DMoverUVETest, APlatformThatTeleportsIsTreatedAsStandingStill) {
    FakeCharacterWorldUVE world;
    // Ten metres in one step: 100 m/s, past the 50 m/s teleport guard.
    world.positions[7U] = {10.0F, 0.0F, 0.0F};
    CharacterMotionStateUVE state;
    state.grounded = true;
    state.platform = MakeEntityUVE(7U);
    state.platformWorldPosition = {0.0F, 0.0F, 0.0F};
    state.hasPlatformWorldPosition = true;

    const CharacterMotionResultUVE result =
        Character3DUVE::MoveAndSlideUVE(world, DefaultConfigUVE(), state, {0.0F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAcceptedUVE());
    EXPECT_FALSE(result.carriedByPlatform);
    // The body did not move, and nothing was handed over when it left: a script writing a
    // transform is not motion, and inheriting it would fling the body across the level.
    ExpectNearUVE(state.lastMotion, {0.0F, 0.0F, 0.0F}, 1.0e-5F);
    ExpectNearUVE(result.platformVelocity, {0.0F, 0.0F, 0.0F}, 1.0e-5F);
    ExpectNearUVE(state.velocity, {0.0F, 0.0F, 0.0F}, 1.0e-5F);
}

TEST(Character3DMoverUVETest, ABodyThatStartsInsideGeometryIsPushedOutBeforeItWalks) {
    FakeCharacterWorldUVE world;
    CharacterSlideCollisionUVE overlap;
    overlap.entity = MakeEntityUVE(8U);
    overlap.normal = {-1.0F, 0.0F, 0.0F};
    overlap.depth = 0.1F;
    world.overlaps.push_back(overlap);
    CharacterMotionStateUVE state;

    const CharacterMotionResultUVE result =
        Character3DUVE::MoveAndSlideUVE(world, DefaultConfigUVE(), state, {0.0F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAcceptedUVE());
    EXPECT_TRUE(result.depenetrated);
    EXPECT_EQ(result.depenetrationPasses, 1U);
    // Out by the depth plus the skin, and the report says which body pushed it.
    ExpectNearUVE(result.appliedMotion, {-0.101F, 0.0F, 0.0F}, 2.0e-3F);
    ExpectNearUVE(result.depenetrationMotion, {-0.101F, 0.0F, 0.0F}, 2.0e-3F);
    ASSERT_EQ(result.collisions.size(), 1U);
    EXPECT_NEAR(result.GetCollisionUVE(0U)->depth, 0.1F, 1.0e-4F);
}

TEST(Character3DMoverUVETest, ABodyInsideSeveralThingsIsPushedOutDeepestFirst) {
    FakeCharacterWorldUVE world;
    const float depths[3] = {0.05F, 0.30F, 0.10F};
    const std::uint32_t entities[3] = {11U, 12U, 13U};
    for (std::size_t index = 0U; index < 3U; ++index) {
        CharacterSlideCollisionUVE overlap;
        overlap.entity = MakeEntityUVE(entities[index]);
        overlap.normal = {0.0F, 1.0F, 0.0F};
        overlap.depth = depths[index];
        world.overlaps.push_back(overlap);
    }
    CharacterMotionStateUVE state;

    const CharacterMotionResultUVE result =
        Character3DUVE::MoveAndSlideUVE(world, DefaultConfigUVE(), state, {0.0F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAcceptedUVE());
    EXPECT_EQ(result.depenetrationPasses, 3U);
    // Deepest first, and the total push is the sum - which is also the deepest-first order's own
    // doing: written out here so a change to that order shows up as a changed number.
    ExpectNearUVE(result.depenetrationMotion, {0.0F, 0.3F + 0.1F + 0.05F + 0.003F, 0.0F}, 2.0e-3F);
}

TEST(Character3DMoverUVETest, ContactsAreCappedAndTheLossIsReported) {
    FakeCharacterWorldUVE world;
    for (std::uint32_t index = 0U; index < 3U; ++index) {
        CharacterSlideCollisionUVE overlap;
        overlap.entity = MakeEntityUVE(20U + index);
        overlap.normal = {0.0F, 1.0F, 0.0F};
        overlap.depth = 0.1F + static_cast<float>(index) * 0.01F;
        world.overlaps.push_back(overlap);
    }
    CharacterMotionConfigUVE config = DefaultConfigUVE();
    config.maximumContacts = 1U;
    CharacterMotionStateUVE state;

    const CharacterMotionResultUVE result =
        Character3DUVE::MoveAndSlideUVE(world, config, state, {0.0F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAcceptedUVE());
    EXPECT_EQ(result.contactsTruncated, true);
    EXPECT_EQ(result.contactCount, 1U);
    EXPECT_EQ(result.droppedContactCount, 2U);
    EXPECT_EQ(result.collisions.size(), 1U);
    // The deepest body is the one worth telling the caller about, so it is the one that survives
    // the cap: 22 is three centimetres deeper than 20.
    EXPECT_EQ(result.GetCollisionUVE(0U)->entity.index, 22U);
}

TEST(Character3DMoverUVETest, ABadConfigStateOrWorldIsRefusedRatherThanGuessed) {
    FakeCharacterWorldUVE world;
    CharacterMotionStateUVE state;
    state.velocity = {1.0F, 0.0F, 0.0F};

    // A body with no up direction has no floor, no wall and no ceiling: refused.
    CharacterMotionConfigUVE degenerate = DefaultConfigUVE();
    degenerate.upDirection = {0.0F, 0.0F, 0.0F};
    EXPECT_EQ(Character3DUVE::MoveAndSlideUVE(world, degenerate, state, {1.0F, 0.0F, 0.0F}, 0.1F).code,
              CharacterMoveCodeUVE::InvalidConfig);

    // A step with no duration, and a motion that is not a number.
    EXPECT_EQ(Character3DUVE::MoveAndSlideUVE(world, DefaultConfigUVE(), state, {1.0F, 0.0F, 0.0F}, 0.0F).code,
              CharacterMoveCodeUVE::InvalidConfig);
    EXPECT_EQ(Character3DUVE::MoveAndSlideUVE(world, DefaultConfigUVE(), state,
                                              {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, 0.1F).code,
              CharacterMoveCodeUVE::InvalidConfig);

    // A body state that is not a number.
    CharacterMotionStateUVE broken = state;
    broken.velocity = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    EXPECT_EQ(Character3DUVE::MoveAndSlideUVE(world, DefaultConfigUVE(), broken, {1.0F, 0.0F, 0.0F}, 0.1F).code,
              CharacterMoveCodeUVE::InvalidState);

    // A world that cannot say where the body is.
    FakeCharacterWorldUVE brokenWorld;
    brokenWorld.center = {std::numeric_limits<float>::infinity(), 0.0F, 0.0F};
    EXPECT_EQ(Character3DUVE::MoveAndSlideUVE(brokenWorld, DefaultConfigUVE(), state, {1.0F, 0.0F, 0.0F}, 0.1F).code,
              CharacterMoveCodeUVE::InvalidWorld);

    // And the values a script could write that are merely wrong are clamped, not refused.
    CharacterMotionConfigUVE sloppy = DefaultConfigUVE();
    sloppy.maxSlides = 0U;
    sloppy.maximumContacts = 0U;
    sloppy.safeMargin = -1.0F;
    sloppy.floorMaxAngleDegrees = 999.0F;
    sloppy.wallMinSlideAngleDegrees = -5.0F;
    sloppy.floorSnapLength = -2.0F;
    sloppy.maxStepHeight = -1.0F;
    sloppy.minStepWidth = 42.0F;
    sloppy.maximumPlatformSpeed = -3.0F;
    sloppy.platformOnLeave = static_cast<Scene::CharacterPlatformLeaveModeUVE>(200U);
    const CharacterMotionResultUVE clamped =
        Character3DUVE::MoveAndSlideUVE(world, sloppy, state, {1.0F, 0.0F, 0.0F}, 0.1F);
    EXPECT_TRUE(clamped.IsAcceptedUVE());
    EXPECT_TRUE(clamped.configClamped);
}

TEST(Character3DMoverUVETest, AMotionTooSmallToMatterIsNotAMoveAtAll) {
    FakeCharacterWorldUVE world;
    AddWallUVE(world, 1.0F);
    CharacterMotionStateUVE state;
    state.velocity = {0.0F, 0.0F, 0.0F};

    const CharacterMotionResultUVE result = Character3DUVE::MoveAndSlideUVE(
        world, DefaultConfigUVE(), state, {Character3DUVE::kMinimumMotionDistanceUVE * 0.5F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAcceptedUVE());
    EXPECT_FALSE(result.blocked);
    EXPECT_EQ(result.contactCount, 0U);
    EXPECT_EQ(result.slideCount, 0U);
    ExpectNearUVE(result.appliedMotion, {0.0F, 0.0F, 0.0F}, 0.0F);
}

TEST(Character3DMoverUVETest, TheSameWorldAndTheSameWalkGiveTheSameAnswerToTheLastBit) {
    const auto runOnce = []() {
        FakeCharacterWorldUVE world;
        AddFloorUVE(world, 0.0F);
        world.boxes.push_back(FakeBoxUVE{MakeEntityUVE(5U), {1.5F, 0.0F, -3.0F}, {4.0F, 0.2F, 3.0F}});
        world.boxes.push_back(FakeBoxUVE{MakeEntityUVE(6U), {2.0F, -1.0F, 0.5F}, {2.5F, 1.0F, 1.0F}});
        world.boxes.push_back(FakeBoxUVE{MakeEntityUVE(7U), {-2.0F, -1.0F, -2.0F}, {2.0F, 1.0F, -1.5F}});
        world.center = {0.0F, 0.0F, 0.0F};
        CharacterMotionStateUVE state;
        state.grounded = true;
        std::vector<Math::Vector3UVE> path;
        for (int step = 0; step < 12; ++step) {
            state.velocity = {1.0F, 0.0F, 0.25F};
            const CharacterMotionResultUVE result = Character3DUVE::MoveAndSlideUVE(
                world, DefaultConfigUVE(), state, {0.1F, -0.01F, 0.025F}, 0.1F);
            path.push_back(result.appliedMotion);
            world.center = result.finalCenter;
        }
        return path;
    };

    const std::vector<Math::Vector3UVE> first = runOnce();
    const std::vector<Math::Vector3UVE> second = runOnce();
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t index = 0U; index < first.size(); ++index) {
        ExpectNearUVE(first[index], second[index], 0.0F);
    }
    // The walk went somewhere, so this is not two sequences of zeroes agreeing.
    EXPECT_GT(first.back().x, 0.0F);
}

TEST(Character3DMoverUVETest, TestMoveAnswersWhatWouldHappenWithoutMovingAnything) {
    FakeCharacterWorldUVE world;
    AddWallUVE(world, 1.0F);
    world.center = {0.0F, 0.0F, 0.0F};

    const std::optional<CharacterSlideCollisionUVE> hit =
        Character3DUVE::TestMoveUVE(world, {2.0F, 0.0F, 0.0F}, 0.001F);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->travel, 0.999F, 2.0e-3F);
    EXPECT_EQ(hit->entity.index, 2U);
    EXPECT_NEAR(hit->normal.x, -1.0F, 1.0e-3F);
    // The world is exactly where it was: a query, not a move.
    ExpectNearUVE(world.center, {0.0F, 0.0F, 0.0F}, 0.0F);

    EXPECT_FALSE(Character3DUVE::TestMoveUVE(world, {0.0F, 0.0F, 0.0F}, 0.001F).has_value());
    EXPECT_FALSE(Character3DUVE::TestMoveUVE(world, {0.0F, 5.0F, 0.0F}, 0.001F).has_value());
}

TEST(Character3DMoverUVETest, ProbeFloorFindsTheFloorBelowAndNothingElse) {
    FakeCharacterWorldUVE world;
    AddFloorUVE(world, 0.0F, MakeEntityUVE(9U));

    const std::optional<CharacterSlideCollisionUVE> floor =
        Character3DUVE::ProbeFloorUVE(world, {0.0F, 0.05F, 0.0F}, {0.0F, 1.0F, 0.0F}, 45.0F, 0.1F, 0.001F);
    ASSERT_TRUE(floor.has_value());
    EXPECT_EQ(floor->entity.index, 9U);
    ExpectNearUVE(floor->normal, {0.0F, 1.0F, 0.0F}, 1.0e-4F);

    // Floors only: a wall's face below the probe is not something to stand on.
    FakeCharacterWorldUVE wallWorld;
    AddWallUVE(wallWorld, -0.5F, -1.0F, 1.0F);
    EXPECT_FALSE(Character3DUVE::ProbeFloorUVE(wallWorld, {0.0F, 0.05F, 0.0F}, {0.0F, 1.0F, 0.0F}, 45.0F, 0.1F,
                                              0.001F)
                     .has_value());

    // And nothing within reach is nothing.
    EXPECT_FALSE(Character3DUVE::ProbeFloorUVE(world, {0.0F, 5.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 45.0F, 0.1F, 0.001F)
                     .has_value());
}

TEST(Character3DMoverUVETest, StandingStillOnASlopeDoesNotCreepDownItWhenThatIsAsked) {
    const auto runOnce = [](const bool stopOnSlope) {
        FakeCharacterWorldUVE world;
        // A 20 degree ramp rising in +x, seen as one contact.
        CharacterSlideCollisionUVE ramp;
        ramp.entity = MakeEntityUVE(4U);
        ramp.normal = Math::NormalizeUVE(Math::Vector3UVE{-0.342F, 0.940F, 0.0F});
        ramp.travel = 0.5F;
        world.scriptedHits.push_back(ramp);
        CharacterMotionConfigUVE config = DefaultConfigUVE();
        config.floorStopOnSlope = stopOnSlope;
        CharacterMotionStateUVE state;
        state.grounded = true;
        // Gravity pulling it down the slope, but not walking.
        state.velocity = {0.0F, -1.0F, 0.0F};
        const CharacterMotionResultUVE result =
            Character3DUVE::MoveAndSlideUVE(world, config, state, {0.0F, -0.1F, 0.0F}, 0.1F);
        return std::pair<CharacterMotionResultUVE, Math::Vector3UVE>{result, state.velocity};
    };

    const auto stopped = runOnce(true);
    EXPECT_TRUE(stopped.first.onFloor);
    ExpectNearUVE(stopped.second, {0.0F, 0.0F, 0.0F}, 1.0e-5F);

    const auto creeping = runOnce(false);
    EXPECT_TRUE(creeping.first.onFloor);
    EXPECT_LT(creeping.second.y, 0.0F);
}

TEST(Character3DMoverUVETest, WalkingUpASlopeKeepsTheHorizontalSpeedWhenThatIsAsked) {
    const auto runOnce = [](const bool constantSpeed) {
        FakeCharacterWorldUVE world;
        CharacterSlideCollisionUVE ramp;
        ramp.entity = MakeEntityUVE(4U);
        // A 30 degree ramp rising in +x: its normal leans back towards -x.
        ramp.normal = Math::NormalizeUVE(Math::Vector3UVE{-0.5F, 0.866F, 0.0F});
        ramp.travel = 0.5F;
        world.scriptedHits.push_back(ramp);
        CharacterMotionConfigUVE config = DefaultConfigUVE();
        config.floorConstantSpeed = constantSpeed;
        CharacterMotionStateUVE state;
        state.grounded = true;
        state.velocity = {1.0F, 0.0F, 0.0F};
        return Character3DUVE::MoveAndSlideUVE(world, config, state, {1.0F, 0.0F, 0.0F}, 0.1F);
    };

    const CharacterMotionResultUVE slowed = runOnce(false);
    const CharacterMotionResultUVE constant = runOnce(true);
    // Without the setting the walk up the ramp loses horizontal speed to it; with it, the walk
    // keeps every millimetre of the horizontal motion it asked for.
    EXPECT_NEAR(slowed.appliedMotion.x, 0.875F, 5.0e-3F);
    EXPECT_NEAR(constant.appliedMotion.x, 1.0F, 5.0e-3F);
    EXPECT_GT(constant.appliedMotion.x, slowed.appliedMotion.x);
    // The climb itself is the same either way: the setting changes the speed, not the slope.
    EXPECT_NEAR(constant.appliedMotion.y, slowed.appliedMotion.y, 5.0e-3F);
}

TEST(Character3DMoverUVETest, TheReportHelpersAnswerTheOneQuestionEachOfThemIsNamedFor) {
    FakeCharacterWorldUVE world;
    AddFloorUVE(world, 0.0F);
    world.center = {0.0F, 0.001F, 0.0F};
    CharacterMotionStateUVE state;
    state.grounded = true;
    const CharacterMotionResultUVE result =
        Character3DUVE::MoveAndSlideUVE(world, DefaultConfigUVE(), state, {0.1F, 0.0F, 0.0F}, 0.1F);

    ASSERT_TRUE(result.IsAcceptedUVE());
    EXPECT_TRUE(result.IsOnFloorOnlyUVE());
    EXPECT_FALSE(result.IsOnWallOnlyUVE());
    EXPECT_FALSE(result.IsOnCeilingOnlyUVE());
    EXPECT_EQ(result.GetCollisionCountUVE(), result.collisions.size());
    EXPECT_EQ(result.GetCollisionCountUVE(), result.contactCount);
}

TEST(Character3DMoverUVETest, StateAndConfigRoundTripThroughTheComponent) {
    Scene::CharacterControllerComponentUVE controller;
    controller.maxSlides = 12U;
    controller.floorMaxAngleDegrees = 30.0F;
    controller.wallMinSlideAngleDegrees = 5.0F;
    controller.safeMargin = 0.01F;
    controller.floorSnapLength = 0.25F;
    controller.maxStepHeight = 0.45F;
    controller.floorBlockOnWall = true;
    controller.floorStopOnSlope = true;
    controller.floorConstantSpeed = true;
    controller.maximumPlatformSpeed = 12.0F;
    controller.platformOnLeave = Scene::CharacterPlatformLeaveModeUVE::AddUpwardVelocity;

    const CharacterMotionConfigUVE config = Scene::MakeCharacterMotionConfigUVE(controller);
    EXPECT_EQ(config.maxSlides, 12U);
    EXPECT_NEAR(config.floorMaxAngleDegrees, 30.0F, 1.0e-5F);
    EXPECT_NEAR(config.wallMinSlideAngleDegrees, 5.0F, 1.0e-5F);
    EXPECT_NEAR(config.safeMargin, 0.01F, 1.0e-6F);
    EXPECT_NEAR(config.floorSnapLength, 0.25F, 1.0e-6F);
    EXPECT_NEAR(config.maxStepHeight, 0.45F, 1.0e-6F);
    EXPECT_TRUE(config.floorBlockOnWall);
    EXPECT_TRUE(config.floorStopOnSlope);
    EXPECT_TRUE(config.floorConstantSpeed);
    EXPECT_NEAR(config.maximumPlatformSpeed, 12.0F, 1.0e-6F);
    EXPECT_EQ(config.platformOnLeave, Scene::CharacterPlatformLeaveModeUVE::AddUpwardVelocity);
    EXPECT_TRUE(config.detectFloor);
    EXPECT_FALSE(config.jumpedThisStep);

    // A floating body has no floor to detect, nothing to snap to, nothing to step on and no slope
    // to stand on: the mode's whole contract, expressed as configuration.
    controller.motionMode = Scene::CharacterMotionModeUVE::Floating;
    const CharacterMotionConfigUVE floating = Scene::MakeCharacterMotionConfigUVE(controller);
    EXPECT_FALSE(floating.detectFloor);
    EXPECT_NEAR(floating.floorSnapLength, 0.0F, 1.0e-6F);
    EXPECT_NEAR(floating.maxStepHeight, 0.0F, 1.0e-6F);
    EXPECT_FALSE(floating.floorStopOnSlope);

    // State in, state out, and only the runtime block is touched.
    CharacterMotionStateUVE state;
    state.velocity = {3.0F, 0.0F, -1.0F};
    state.grounded = true;
    state.onWall = true;
    state.wallNormal = {-1.0F, 0.0F, 0.0F};
    state.timeSinceOnFloor = 0.25F;
    state.jumpBufferRemaining = 0.05F;
    state.lastMotion = {0.1F, 0.0F, 0.0F};
    state.realVelocity = {1.0F, 0.0F, 0.0F};
    state.platform = MakeEntityUVE(3U);
    state.platformVelocity = {2.0F, 0.0F, 0.0F};
    state.platformWorldPosition = {1.0F, 0.0F, 0.0F};
    state.hasPlatformWorldPosition = true;
    Scene::CharacterControllerComponentUVE target;
    Scene::StoreCharacterMotionStateUVE(target, state);
    EXPECT_TRUE(target.grounded);
    EXPECT_TRUE(target.onWall);
    EXPECT_NEAR(target.wallNormal.x, -1.0F, 1.0e-6F);
    EXPECT_NEAR(target.timeSinceOnFloor, 0.25F, 1.0e-6F);
    EXPECT_NEAR(target.jumpBufferRemaining, 0.05F, 1.0e-6F);
    EXPECT_EQ(target.platform, MakeEntityUVE(3U));
    EXPECT_TRUE(target.hasPlatformWorldPosition);
    // Authored settings were not touched by the state write: `target` was never given any, so it
    // still holds the component's own defaults.
    const Scene::CharacterControllerComponentUVE authoredDefaults;
    EXPECT_NEAR(target.safeMargin, authoredDefaults.safeMargin, 1.0e-6F);
    EXPECT_EQ(target.maxSlides, authoredDefaults.maxSlides);

    // And the state read back is the same state, platform bookkeeping included.
    const CharacterMotionStateUVE readBack = Scene::MakeCharacterMotionStateUVE(target);
    ExpectNearUVE(readBack.velocity, state.velocity, 1.0e-6F);
    EXPECT_EQ(readBack.platform, state.platform);
    EXPECT_TRUE(readBack.hasPlatformWorldPosition);
    ExpectNearUVE(readBack.platformVelocity, state.platformVelocity, 1.0e-6F);
}

TEST(Character3DMoverUVETest, ACharacterCanBeBuiltFromItsObjectDefinition) {
    const Scene::Character3DObjectDefinitionUVE definition{};
    EXPECT_TRUE(Scene::IsCharacter3DObjectDefinitionValidUVE(definition));
    const Scene::ColliderComponentUVE collider = Scene::Character3DObjectDefinitionUVE::MakeDefaultColliderUVE();
    EXPECT_GT(collider.halfExtents.y, 0.0F);
    EXPECT_GT(collider.halfExtents.x, 0.0F);
}

// =================================================================================================
// The same mover against the engine's real world: real components, the real collision system, the
// real scene graph, and the step system that ties them together.
// =================================================================================================

class Character3DStepUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;
    CollisionSystemUVE collisionSystem;

    static constexpr float kDeltaTimeUVE = 1.0F / 60.0F;
    static constexpr float kGravityYUVE = -9.81F;

    [[nodiscard]] Scene::EntityUVE MakeBoxUVE(const Math::Vector3UVE position,
                                              const Math::Vector3UVE halfExtents,
                                              const std::uint32_t layer = 1U,
                                              const std::uint32_t mask = 0xFFFFFFFFU) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform;
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        sceneGraph.UpdateUVE(entityManager);
        Scene::ColliderComponentUVE collider{halfExtents};
        collider.collisionLayer = layer;
        collider.collisionMask = mask;
        entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(entity, collider);
        return entity;
    }

    /// A character is a collider, a controller and a transform - no rigid body, because a character
    /// is moved by its own code and not by forces.
    [[nodiscard]] Scene::EntityUVE MakeCharacterUVE(
        const Math::Vector3UVE position, const Math::Vector3UVE halfExtents,
        const Scene::CharacterControllerComponentUVE& controller = {}) {
        const Scene::EntityUVE entity = MakeBoxUVE(position, halfExtents);
        entityManager.AddComponentUVE<Scene::CharacterControllerComponentUVE>(entity, controller);
        return entity;
    }

    [[nodiscard]] Scene::EntityUVE MakeFloorUVE(const float topY = 0.0F,
                                                const Math::Vector3UVE halfExtents = {100.0F, 0.5F, 100.0F}) {
        return MakeBoxUVE({0.0F, topY - halfExtents.y, 0.0F}, halfExtents);
    }

    [[nodiscard]] Character3DStepResultUVE StepUVE(
        const Scene::EntityUVE entity, const CharacterMotionInputUVE& input = {},
        const float deltaTimeSeconds = kDeltaTimeUVE) {
        return StepCharacter3DUVE(entityManager, sceneGraph, collisionSystem, entity, input, kGravityYUVE,
                                  deltaTimeSeconds);
    }

    [[nodiscard]] Math::Vector3UVE PositionUVE(const Scene::EntityUVE entity) {
        return entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localPosition;
    }

    [[nodiscard]] Scene::CharacterControllerComponentUVE ControllerUVE(const Scene::EntityUVE entity) {
        return entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(entity);
    }

    /// Steps until the predicate holds, and hands back the last report. Used where a test is about
    /// where a body ends up rather than about one particular step.
    template <typename PredicateUVE>
    [[nodiscard]] Character3DStepResultUVE StepUntilUVE(const Scene::EntityUVE entity,
                                                        PredicateUVE&& done, const int maximumSteps = 240) {
        Character3DStepResultUVE report = StepUVE(entity);
        for (int step = 1; step < maximumSteps && !done(report); ++step) {
            report = StepUVE(entity);
        }
        return report;
    }
};

TEST_F(Character3DStepUVETest, ACharacterFallingOntoTheFloorLandsOnItAndStaysThere) {
    const Scene::EntityUVE floor = MakeFloorUVE(0.0F);
    static_cast<void>(floor);
    const Scene::EntityUVE character = MakeCharacterUVE({0.0F, 3.0F, 0.0F}, {0.5F, 0.5F, 0.5F});

    for (int step = 0; step < 120; ++step) {
        const Character3DStepResultUVE report = StepUVE(character);
        ASSERT_TRUE(report.stepped) << "step " << step;
    }

    // Resting on the floor: the collider's half height above the floor's top, give or take the skin.
    EXPECT_NEAR(PositionUVE(character).y, 0.5F, 1.0e-2F);
    const Scene::CharacterControllerComponentUVE controller = ControllerUVE(character);
    EXPECT_TRUE(controller.grounded);
    EXPECT_NEAR(controller.velocity.y, 0.0F, 1.0e-3F);
    EXPECT_NEAR(controller.floorNormal.y, 1.0F, 1.0e-3F);
    EXPECT_LT(controller.timeSinceOnFloor, 1.0e-3F);
}

TEST_F(Character3DStepUVETest, WalkingIntoAWallStopsAtItAndSaysSo) {
    const Scene::EntityUVE floor = MakeFloorUVE(0.0F);
    static_cast<void>(floor);
    const Scene::EntityUVE wall = MakeBoxUVE({2.5F, 1.0F, 0.0F}, {0.5F, 1.5F, 5.0F});
    Scene::CharacterControllerComponentUVE settings;
    settings.builtInMovement = false;
    const Scene::EntityUVE character = MakeCharacterUVE({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F}, settings);

    CharacterMotionInputUVE input;
    Character3DStepResultUVE report;
    for (int step = 0; step < 120; ++step) {
        // A script driving the body: five metres a second, straight at the wall.
        entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(character).velocity =
            Math::Vector3UVE{5.0F, 0.0F, 0.0F};
        report = StepUVE(character, input);
        ASSERT_TRUE(report.stepped) << "step " << step;
    }

    // The wall's own face is at x = 2.0; the collider's surface stops a skin short of it.
    EXPECT_NEAR(PositionUVE(character).x, 1.5F, 5.0e-3F);
    EXPECT_TRUE(report.motion.onWall);
    EXPECT_TRUE(report.motion.blocked);
    EXPECT_NEAR(report.motion.wallNormal.x, -1.0F, 1.0e-3F);
    ASSERT_GT(report.motion.contactCount, 0U);
    ASSERT_NE(report.motion.GetCollisionUVE(0U), nullptr);
    EXPECT_EQ(report.motion.GetCollisionUVE(0U)->entity, wall);
}

TEST_F(Character3DStepUVETest, WalkingForwardClimbsALowStep) {
    const Scene::EntityUVE floor = MakeFloorUVE(0.0F);
    static_cast<void>(floor);
    // A step 0.25 m tall, from x = 1.0 onwards: low enough for the default 0.3 m step height.
    const Scene::EntityUVE stepBox = MakeBoxUVE({3.0F, 0.125F, 0.0F}, {2.5F, 0.125F, 5.0F});
    Scene::CharacterControllerComponentUVE settings;
    settings.builtInMovement = false;
    const Scene::EntityUVE character = MakeCharacterUVE({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F}, settings);

    bool steppedUp = false;
    // One second of walking: three metres from the start, which is the middle of the tread.
    for (int step = 0; step < 60; ++step) {
        entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(character).velocity =
            Math::Vector3UVE{3.0F, 0.0F, 0.0F};
        const Character3DStepResultUVE report = StepUVE(character);
        ASSERT_TRUE(report.stepped) << "step " << step;
        steppedUp = steppedUp || report.motion.stepUpUsed;
    }

    EXPECT_TRUE(steppedUp);
    // On top of the step: the tread is at 0.25, so the body's centre rests just above 0.75.
    EXPECT_GT(PositionUVE(character).x, 2.0F);
    EXPECT_NEAR(PositionUVE(character).y, 0.75F, 1.5e-2F);
    static_cast<void>(stepBox);
}

TEST_F(Character3DStepUVETest, AStepTallerThanTheStepHeightIsAWallAndIsNotClimbed) {
    const Scene::EntityUVE floor = MakeFloorUVE(0.0F);
    static_cast<void>(floor);
    // 0.6 m tall against a 0.3 m step height: a wall, whatever it looks like from the side. Its
    // face is a metre in front of the body, so the walk has somewhere to go before it stops.
    const Scene::EntityUVE ledge = MakeBoxUVE({3.5F, 0.3F, 0.0F}, {2.5F, 0.3F, 5.0F});
    static_cast<void>(ledge);
    Scene::CharacterControllerComponentUVE settings;
    settings.builtInMovement = false;
    const Scene::EntityUVE character = MakeCharacterUVE({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F}, settings);

    for (int step = 0; step < 120; ++step) {
        entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(character).velocity =
            Math::Vector3UVE{3.0F, 0.0F, 0.0F};
        const Character3DStepResultUVE report = StepUVE(character);
        ASSERT_TRUE(report.stepped) << "step " << step;
        EXPECT_FALSE(report.motion.stepUpUsed);
    }

    EXPECT_NEAR(PositionUVE(character).y, 0.5F, 1.0e-2F);
    EXPECT_NEAR(PositionUVE(character).x, 0.5F, 5.0e-3F);
}

TEST_F(Character3DStepUVETest, JumpingLeavesTheFloorAndComesBackDownToIt) {
    const Scene::EntityUVE floor = MakeFloorUVE(0.0F);
    static_cast<void>(floor);
    Scene::CharacterControllerComponentUVE settings;
    settings.builtInMovement = true;
    settings.jumpHeight = 1.0F;
    const Scene::EntityUVE character = MakeCharacterUVE({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F}, settings);

    // Settle first: a jump is only a jump from the floor.
    const Character3DStepResultUVE settled = StepUntilUVE(character, [](const Character3DStepResultUVE&) {
        return false;
    }, 2);
    ASSERT_TRUE(settled.stepped);
    ASSERT_TRUE(ControllerUVE(character).grounded);

    CharacterMotionInputUVE jumpInput;
    jumpInput.jumpPressed = true;
    const Character3DStepResultUVE jump = StepUVE(character, jumpInput);
    ASSERT_TRUE(jump.stepped);
    EXPECT_TRUE(jump.jumped);
    EXPECT_GT(ControllerUVE(character).velocity.y, 0.0F);
    EXPECT_FALSE(ControllerUVE(character).grounded);

    float highest = PositionUVE(character).y;
    bool landed = false;
    for (int step = 0; step < 240 && !landed; ++step) {
        const Character3DStepResultUVE report = StepUVE(character);
        ASSERT_TRUE(report.stepped);
        highest = std::max(highest, PositionUVE(character).y);
        landed = report.motion.onFloor;
    }

    EXPECT_TRUE(landed);
    EXPECT_GT(highest, 1.0F);
    EXPECT_NEAR(PositionUVE(character).y, 0.5F, 1.5e-2F);
    EXPECT_TRUE(ControllerUVE(character).grounded);
}

TEST_F(Character3DStepUVETest, AFloatingBodyIgnoresGravityAndItsLocksHoldTheirAxes) {
    const Scene::EntityUVE character = MakeCharacterUVE({0.0F, 1.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    Scene::CharacterControllerComponentUVE settings = ControllerUVE(character);
    settings.motionMode = Scene::CharacterMotionModeUVE::Floating;
    settings.builtInMovement = true;
    settings.moveSpeed = 2.0F;
    entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(character) = settings;

    Scene::SolidBodyComponentUVE locks;
    locks.lockMotionX = true;
    entityManager.AddComponentUVE<Scene::SolidBodyComponentUVE>(character, locks);

    CharacterMotionInputUVE input;
    input.move = {1.0F, 0.0F, 1.0F};
    input.rise = 1.0F;
    for (int step = 0; step < 30; ++step) {
        const Character3DStepResultUVE report = StepUVE(character, input);
        ASSERT_TRUE(report.stepped) << "step " << step;
        EXPECT_FALSE(report.motion.onFloor);
    }

    const Math::Vector3UVE position = PositionUVE(character);
    EXPECT_NEAR(position.x, 0.0F, 1.0e-5F); // locked
    EXPECT_NEAR(position.z, 1.0F, 5.0e-2F); // half a second at 2 m/s
    EXPECT_NEAR(position.y, 2.0F, 5.0e-2F); // rising at 2 m/s
}

TEST_F(Character3DStepUVETest, AMovingPlatformCarriesTheBodyStandingOnIt) {
    // The platform is the floor: a solid box the body stands on, moved by its own code each step.
    const Scene::EntityUVE platform = MakeBoxUVE({0.0F, -0.25F, 0.0F}, {2.0F, 0.25F, 2.0F});
    const Scene::EntityUVE character = MakeCharacterUVE({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F});

    // Ride for a moment so the body is genuinely standing on the platform.
    for (int step = 0; step < 10; ++step) {
        ASSERT_TRUE(StepUVE(character).stepped);
    }
    ASSERT_TRUE(ControllerUVE(character).grounded);
    const float startX = PositionUVE(character).x;

    bool carried = false;
    constexpr float kPlatformStepUVE = 0.01F;
    for (int step = 0; step < 30; ++step) {
        Scene::TransformComponentUVE platformTransform =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(platform);
        platformTransform.localPosition.x += kPlatformStepUVE;
        sceneGraph.SetLocalTransformUVE(entityManager, platform, platformTransform);

        const Character3DStepResultUVE report = StepUVE(character);
        ASSERT_TRUE(report.stepped) << "step " << step;
        carried = carried || report.motion.carriedByPlatform;
    }

    EXPECT_TRUE(carried);
    // Thirty steps at a centimetre a step: the rider went with the platform, not just its velocity.
    EXPECT_NEAR(PositionUVE(character).x - startX, 0.3F, 2.5e-2F);
    EXPECT_TRUE(ControllerUVE(character).grounded);
}

TEST_F(Character3DStepUVETest, ALayerTheCharacterDoesNotCollideWithIsWalkedStraightThrough) {
    const Scene::EntityUVE floor = MakeFloorUVE(0.0F);
    static_cast<void>(floor);
    // A wall on layer 2. The character's mask only accepts layer 1, and the wall's mask accepts
    // everything - one direction failing is enough for a pair to be ignored.
    const Scene::EntityUVE wall = MakeBoxUVE({2.5F, 1.0F, 0.0F}, {0.5F, 1.5F, 5.0F}, 2U, 0xFFFFFFFFU);
    static_cast<void>(wall);
    Scene::CharacterControllerComponentUVE settings;
    settings.builtInMovement = false;
    const Scene::EntityUVE character = MakeBoxUVE({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F}, 1U, 1U);
    entityManager.AddComponentUVE<Scene::CharacterControllerComponentUVE>(character, settings);

    for (int step = 0; step < 60; ++step) {
        entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(character).velocity =
            Math::Vector3UVE{5.0F, 0.0F, 0.0F};
        const Character3DStepResultUVE report = StepUVE(character);
        ASSERT_TRUE(report.stepped) << "step " << step;
        EXPECT_FALSE(report.motion.onWall);
    }

    EXPECT_GT(PositionUVE(character).x, 4.0F);
}

TEST_F(Character3DStepUVETest, BodiesTheStepCannotDriveAreRefusedWithAReason) {
    const Scene::EntityUVE floor = MakeFloorUVE(0.0F);
    static_cast<void>(floor);

    // Not a controller at all.
    const Scene::EntityUVE plain = MakeBoxUVE({0.0F, 1.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    EXPECT_EQ(StepUVE(plain).code, Character3DStepCodeUVE::NotAController);

    // A controller with no collider has nothing to collide with.
    const Scene::EntityUVE noCollider = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE transform;
    transform.localPosition = {0.0F, 1.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, noCollider, transform);
    entityManager.AddComponentUVE<Scene::CharacterControllerComponentUVE>(
        noCollider, Scene::CharacterControllerComponentUVE{});
    EXPECT_EQ(StepUVE(noCollider).code, Character3DStepCodeUVE::MissingCollider);

    // A dynamic rigid body is driven by forces, not by a character controller.
    const Scene::EntityUVE dynamic = MakeCharacterUVE({0.0F, 1.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    entityManager.AddComponentUVE<Scene::Rigid3DComponentUVE>(dynamic, Scene::Rigid3DComponentUVE{});
    EXPECT_EQ(StepUVE(dynamic).code, Character3DStepCodeUVE::NonKinematicBody);

    // A kinematic rigid body is fine: that is a body whose motion is authored.
    const Scene::EntityUVE kinematic = MakeCharacterUVE({0.0F, 1.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    Scene::Rigid3DComponentUVE kinematicBody;
    kinematicBody.isKinematic = true;
    entityManager.AddComponentUVE<Scene::Rigid3DComponentUVE>(kinematic, kinematicBody);
    EXPECT_EQ(StepUVE(kinematic).code, Character3DStepCodeUVE::Stepped);

    // A vanished entity, and a step with no duration.
    const Scene::EntityUVE vanished = MakeCharacterUVE({0.0F, 1.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    entityManager.DestroyEntityUVE(vanished);
    EXPECT_EQ(StepUVE(vanished).code, Character3DStepCodeUVE::UnknownEntity);
    EXPECT_EQ(StepUVE(kinematic, {}, 0.0F).code, Character3DStepCodeUVE::InvalidDeltaTime);
    EXPECT_EQ(StepUVE(kinematic, {}, -1.0F).code, Character3DStepCodeUVE::InvalidDeltaTime);
    EXPECT_EQ(StepUVE(kinematic, {}, std::numeric_limits<float>::quiet_NaN()).code,
              Character3DStepCodeUVE::InvalidDeltaTime);
}

TEST_F(Character3DStepUVETest, AScriptedVelocityMovesTheBodyThroughTheWorld) {
    const Scene::EntityUVE floor = MakeFloorUVE(0.0F);
    static_cast<void>(floor);
    Scene::CharacterControllerComponentUVE settings;
    settings.builtInMovement = false;
    const Scene::EntityUVE character = MakeCharacterUVE({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F}, settings);

    Math::Vector3UVE velocity{2.0F, 0.0F, -3.0F};
    for (int step = 0; step < 60; ++step) {
        entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(character).velocity = velocity;
        const Character3DStepResultUVE report = StepUVE(character);
        ASSERT_TRUE(report.stepped) << "step " << step;
    }

    // One second of scripted travel, and the component's velocity is left as the script set it.
    EXPECT_NEAR(PositionUVE(character).x, 2.0F, 5.0e-2F);
    EXPECT_NEAR(PositionUVE(character).z, -3.0F, 5.0e-2F);
    EXPECT_NEAR(ControllerUVE(character).velocity.x, velocity.x, 1.0e-4F);
    EXPECT_NEAR(ControllerUVE(character).velocity.z, velocity.z, 1.0e-4F);
}

TEST_F(Character3DStepUVETest, ARefusedStepLeavesTheBodyExactlyWhereItWas) {
    const Scene::EntityUVE floor = MakeFloorUVE(0.0F);
    static_cast<void>(floor);
    Scene::CharacterControllerComponentUVE settings;
    settings.builtInMovement = false;
    const Scene::EntityUVE character = MakeCharacterUVE({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F}, settings);
    entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(character).velocity =
        Math::Vector3UVE{4.0F, 0.0F, 0.0F};

    const Math::Vector3UVE positionBefore = PositionUVE(character);
    const Character3DStepResultUVE refused = StepUVE(character, {}, 0.0F);
    EXPECT_FALSE(refused.stepped);
    ExpectNearUVE(PositionUVE(character), positionBefore, 0.0F);
    EXPECT_NEAR(ControllerUVE(character).velocity.x, 4.0F, 1.0e-5F);
}

TEST_F(Character3DStepUVETest, TheBridgesSweepAgreesWithTheMoverAboutAFaceAndItsNormal) {
    const Scene::EntityUVE floor = MakeFloorUVE(0.0F);
    static_cast<void>(floor);
    const Scene::EntityUVE wall = MakeBoxUVE({2.5F, 1.0F, 0.0F}, {0.5F, 1.5F, 5.0F});
    const Scene::EntityUVE character = MakeCharacterUVE({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F});

    CharacterWorldQueryUVE world(entityManager, collisionSystem, character);
    ASSERT_TRUE(world.IsMovableCharacterUVE());
    ExpectNearUVE(world.GetCenterUVE(), {0.0F, 0.5F, 0.0F}, 1.0e-5F);

    const std::optional<CharacterSlideCollisionUVE> hit =
        world.SweepUVE(world.GetCenterUVE(), {4.0F, 0.0F, 0.0F}, 0.001F);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, wall);
    // The wall's face is at x = 2.0, the body's surface is 0.5 out from its center, and the query
    // keeps a 1 mm skin: 1.499 metres of travel, and a normal pointing back at the body.
    EXPECT_NEAR(hit->travel, 1.499F, 1.0e-3F);
    EXPECT_NEAR(hit->normal.x, -1.0F, 1.0e-3F);
    EXPECT_NEAR(hit->normal.y, 0.0F, 1.0e-3F);

    // A downward sweep hits the floor, and names the floor: the body's own geometry is never its
    // own obstacle, however long the sweep is.
    const std::optional<CharacterSlideCollisionUVE> below =
        world.SweepUVE(world.GetCenterUVE(), {0.0F, -4.0F, 0.0F}, 0.001F);
    ASSERT_TRUE(below.has_value());
    EXPECT_NE(below->entity, character);
    EXPECT_NEAR(below->normal.y, 1.0F, 1.0e-3F);
}

TEST_F(Character3DStepUVETest, TheBridgeReportsOverlapsWithNormalsPointingBackAtTheBody) {
    const Scene::EntityUVE floor = MakeFloorUVE(0.0F);
    static_cast<void>(floor);
    // The body is spawned half a metre inside the wall's face.
    const Scene::EntityUVE wall = MakeBoxUVE({2.5F, 1.0F, 0.0F}, {0.5F, 1.5F, 5.0F});
    const Scene::EntityUVE character = MakeCharacterUVE({1.75F, 1.0F, 0.0F}, {0.5F, 0.5F, 0.5F});

    CharacterWorldQueryUVE world(entityManager, collisionSystem, character);
    const std::vector<CharacterSlideCollisionUVE> overlaps = world.GetOverlapsUVE();
    ASSERT_FALSE(overlaps.empty());
    const auto found = std::find_if(overlaps.begin(), overlaps.end(),
                                    [wall](const CharacterSlideCollisionUVE& overlap) {
                                        return overlap.entity == wall;
                                    });
    ASSERT_NE(found, overlaps.end());
    EXPECT_NEAR(found->normal.x, -1.0F, 1.0e-3F);
    EXPECT_NEAR(found->depth, 0.25F, 2.0e-2F);

    // And a body standing free of everything reports nothing at all.
    Scene::TransformComponentUVE transform =
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(character);
    transform.localPosition = {0.0F, 3.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, character, transform);
    sceneGraph.UpdateUVE(entityManager);
    CharacterWorldQueryUVE clearWorld(entityManager, collisionSystem, character);
    EXPECT_TRUE(clearWorld.GetOverlapsUVE().empty());
}

TEST_F(Character3DStepUVETest, AQueryForAnEntityThatIsNotACharacterIsNotMovable) {
    const Scene::EntityUVE plain = MakeBoxUVE({0.0F, 1.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    CharacterWorldQueryUVE world(entityManager, collisionSystem, plain);
    EXPECT_FALSE(world.IsMovableCharacterUVE());
    // Everything it is asked about *moving* answers "nothing".
    EXPECT_TRUE(world.GetOverlapsUVE().empty());
    EXPECT_FALSE(world.SweepUVE({0.0F, 1.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 0.0F).has_value());
    // Where an entity is, is a fact about the world and not a question about the character: it is
    // answered whether or not this query could move the body.
    ASSERT_TRUE(world.TryGetEntityCenterUVE(plain).has_value());
    ExpectNearUVE(*world.TryGetEntityCenterUVE(plain), {0.0F, 1.0F, 0.0F}, 1.0e-5F);

    // And moving a body through it is refused rather than silently moving it from nowhere.
    Scene::CharacterControllerComponentUVE settings;
    settings.builtInMovement = false;
    CharacterMotionStateUVE state;
    const CharacterMotionResultUVE result = Character3DUVE::MoveAndSlideUVE(
        world, Scene::MakeCharacterMotionConfigUVE(settings), state, {1.0F, 0.0F, 0.0F}, kDeltaTimeUVE);
    EXPECT_FALSE(result.IsAcceptedUVE());
    EXPECT_EQ(result.code, CharacterMoveCodeUVE::InvalidWorld);
    ExpectNearUVE(state.lastMotion, {0.0F, 0.0F, 0.0F}, 0.0F);
}

TEST_F(Character3DStepUVETest, ASurfaceVelocityIsReportedForRigidBodiesAndThePlatformCentreForEntities) {
    const Scene::EntityUVE floor = MakeFloorUVE(0.0F);
    sceneGraph.UpdateUVE(entityManager);
    const Scene::EntityUVE crate = MakeBoxUVE({3.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F});
    Scene::Rigid3DComponentUVE crateBody;
    crateBody.velocity = {1.5F, 0.0F, -0.5F};
    entityManager.AddComponentUVE<Scene::Rigid3DComponentUVE>(crate, crateBody);
    const Scene::EntityUVE character = MakeCharacterUVE({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F});

    CharacterWorldQueryUVE world(entityManager, collisionSystem, character);
    ExpectNearUVE(world.GetSurfaceVelocityUVE(crate), {1.5F, 0.0F, -0.5F}, 1.0e-5F);
    // Static geometry has no surface velocity: it is not going anywhere.
    ExpectNearUVE(world.GetSurfaceVelocityUVE(floor), {0.0F, 0.0F, 0.0F}, 1.0e-5F);

    const std::optional<Math::Vector3UVE> centre = world.TryGetEntityCenterUVE(crate);
    ASSERT_TRUE(centre.has_value());
    ExpectNearUVE(*centre, {3.0F, 0.5F, 0.0F}, 1.0e-5F);
    EXPECT_FALSE(world.TryGetEntityCenterUVE(Scene::EntityUVE{}).has_value());
}

TEST_F(Character3DStepUVETest, TheSameWorldAndTheSameStepsGiveTheSameAnswerEveryTime) {
    // Two identical worlds, walked identically: the mover promises determinism, and this is where a
    // tie broken by storage order instead of by entity index would show up.
    const auto runOnce = [this]() {
        Memory::MemoryManagerUVE localMemory;
        Events::EventSystemUVE localEvents;
        Scene::EntityManagerUVE localEntities{localMemory.GetDefaultAllocatorUVE(), localEvents};
        Scene::SceneGraphUVE localGraph;
        CollisionSystemUVE localCollision;

        std::vector<Scene::EntityUVE> obstacles;
        for (int index = 0; index < 4; ++index) {
            const float x = 1.0F + static_cast<float>(index) * 0.75F;
            const Scene::EntityUVE entity = localEntities.CreateEntityUVE();
            Scene::TransformComponentUVE transform;
            transform.localPosition = {x, 0.5F, 0.0F};
            localGraph.AttachTransformUVE(localEntities, entity, transform);
            localGraph.UpdateUVE(localEntities);
            Scene::ColliderComponentUVE collider{{0.25F, 0.5F, 0.25F}};
            localEntities.AddComponentUVE<Scene::ColliderComponentUVE>(entity, collider);
            obstacles.push_back(entity);
        }
        const Scene::EntityUVE moverEntity = localEntities.CreateEntityUVE();
        Scene::TransformComponentUVE moverTransform;
        moverTransform.localPosition = {0.0F, 0.5F, 0.0F};
        localGraph.AttachTransformUVE(localEntities, moverEntity, moverTransform);
        localGraph.UpdateUVE(localEntities);
        localEntities.AddComponentUVE<Scene::ColliderComponentUVE>(
            moverEntity, Scene::ColliderComponentUVE{{0.25F, 0.5F, 0.25F}});
        Scene::CharacterControllerComponentUVE settings;
        settings.builtInMovement = false;
        localEntities.AddComponentUVE<Scene::CharacterControllerComponentUVE>(moverEntity, settings);

        std::vector<Math::Vector3UVE> path;
        for (int step = 0; step < 40; ++step) {
            localEntities.GetComponentUVE<Scene::CharacterControllerComponentUVE>(moverEntity).velocity =
                Math::Vector3UVE{3.0F, 0.0F, 0.5F};
            static_cast<void>(StepCharacter3DUVE(localEntities, localGraph, localCollision, moverEntity, {},
                                                 -9.81F, 1.0F / 60.0F));
            path.push_back(localEntities.GetComponentUVE<Scene::TransformComponentUVE>(moverEntity).localPosition);
        }
        return path;
    };

    const std::vector<Math::Vector3UVE> first = runOnce();
    const std::vector<Math::Vector3UVE> second = runOnce();
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t index = 0; index < first.size(); ++index) {
        ExpectNearUVE(first[index], second[index], 0.0F);
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

TEST_F(Character3DStepUVETest, AWalkIntoARigidBodyPushesItAndSaysSo) {
    const Scene::EntityUVE floor = MakeFloorUVE(0.0F);
    static_cast<void>(floor);

    // A dynamic box in the way, one metre of walking from the start.
    const Scene::EntityUVE target = MakeBoxUVE({2.5F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F});
    entityManager.AddComponentUVE<Scene::Rigid3DComponentUVE>(target, Scene::Rigid3DComponentUVE{});

    Scene::CharacterControllerComponentUVE settings;
    settings.builtInMovement = false;
    const Scene::EntityUVE character = MakeCharacterUVE({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F}, settings);

    const Physics::CharacterDynamicPushPolicyUVE push{true, 1.0F, 5.0F, kDeltaTimeUVE};
    std::size_t pushedTotal = 0U;
    for (int step = 0; step < 60; ++step) {
        entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(character).velocity =
            Math::Vector3UVE{3.0F, 0.0F, 0.0F};
        const Character3DStepResultUVE report = StepUVE(character);
        ASSERT_TRUE(report.stepped) << "step " << step;
        pushedTotal += PushBodiesFromCharacterMoveUVE(entityManager, report.motion.collisions, push);
    }

    // The body was pushed the way the character was walking: away from it, along +x.
    const Scene::Rigid3DComponentUVE& body = entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(target);
    EXPECT_GT(body.velocity.x, 0.0F);
    EXPECT_LT(body.velocity.x, 5.0F + 1.0e-3F);
    EXPECT_NEAR(body.velocity.y, 0.0F, 1.0e-4F);
    EXPECT_NEAR(body.velocity.z, 0.0F, 1.0e-4F);
    EXPECT_GT(pushedTotal, 0U);

    // And the character stopped at the body rather than walking through it.
    EXPECT_LT(PositionUVE(character).x, 1.5F);
}

TEST_F(Character3DStepUVETest, APushNeverSlowsABodyThatIsAlreadyLeavingFaster) {
    const Scene::EntityUVE floor = MakeFloorUVE(0.0F);
    static_cast<void>(floor);
    const Scene::EntityUVE target = MakeBoxUVE({2.5F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F});
    Scene::Rigid3DComponentUVE rigid;
    // Already running away at four times the push's own speed cap.
    rigid.velocity = Math::Vector3UVE{20.0F, 0.0F, 0.0F};
    entityManager.AddComponentUVE<Scene::Rigid3DComponentUVE>(target, rigid);

    Scene::CharacterControllerComponentUVE settings;
    settings.builtInMovement = false;
    const Scene::EntityUVE character = MakeCharacterUVE({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F}, settings);

    const Physics::CharacterDynamicPushPolicyUVE push{true, 1.0F, 5.0F, kDeltaTimeUVE};
    for (int step = 0; step < 60; ++step) {
        entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(character).velocity =
            Math::Vector3UVE{3.0F, 0.0F, 0.0F};
        const Character3DStepResultUVE report = StepUVE(character);
        ASSERT_TRUE(report.stepped) << "step " << step;
        EXPECT_EQ(PushBodiesFromCharacterMoveUVE(entityManager, report.motion.collisions, push), 0U);
    }

    // Untouched: a push adds speed, it never brakes the thing it is pushing.
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(target).velocity.x, 20.0F);
}

TEST_F(Character3DStepUVETest, OneBodyIsPushedOncePerStepAndUnmovableBodiesAreNotPushedAtAll) {
    const Scene::EntityUVE floor = MakeFloorUVE(0.0F);
    static_cast<void>(floor);
    const Scene::EntityUVE dynamicBody = MakeBoxUVE({2.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F});
    entityManager.AddComponentUVE<Scene::Rigid3DComponentUVE>(dynamicBody, Scene::Rigid3DComponentUVE{});
    const Scene::EntityUVE kinematicBody = MakeBoxUVE({2.0F, 1.5F, 0.0F}, {0.5F, 0.5F, 0.5F});
    Scene::Rigid3DComponentUVE kinematic;
    kinematic.isKinematic = true;
    entityManager.AddComponentUVE<Scene::Rigid3DComponentUVE>(kinematicBody, kinematic);
    const Scene::EntityUVE weightlessBody = MakeBoxUVE({2.0F, 2.5F, 0.0F}, {0.5F, 0.5F, 0.5F});
    Scene::Rigid3DComponentUVE weightless;
    weightless.mass = 0.0F;
    entityManager.AddComponentUVE<Scene::Rigid3DComponentUVE>(weightlessBody, weightless);

    // The same body met twice in one step - a corner contact and a face contact - plus bodies the
    // character has no business moving.
    const Math::Vector3UVE intoTheWall{3.0F, 0.0F, 0.0F};
    std::vector<Scene::CharacterSlideCollisionUVE> contacts;
    for (int repeat = 0; repeat < 2; ++repeat) {
        Scene::CharacterSlideCollisionUVE contact;
        contact.entity = dynamicBody;
        contact.normal = Math::Vector3UVE{-1.0F, 0.0F, 0.0F};
        contact.motion = intoTheWall;
        contact.travel = 0.0F;
        contacts.push_back(contact);
    }
    Scene::CharacterSlideCollisionUVE onTheKinematic;
    onTheKinematic.entity = kinematicBody;
    onTheKinematic.normal = Math::Vector3UVE{-1.0F, 0.0F, 0.0F};
    onTheKinematic.motion = intoTheWall;
    contacts.push_back(onTheKinematic);
    Scene::CharacterSlideCollisionUVE onTheWeightless;
    onTheWeightless.entity = weightlessBody;
    onTheWeightless.normal = Math::Vector3UVE{-1.0F, 0.0F, 0.0F};
    onTheWeightless.motion = intoTheWall;
    contacts.push_back(onTheWeightless);

    // The cap is high enough that pushing the same body twice would be visible: 3 m/s of pressing
    // speed over a 60 Hz step is 180 m/s of push, so a second push would double it.
    const Physics::CharacterDynamicPushPolicyUVE push{true, 1.0F, 1000.0F, kDeltaTimeUVE};
    EXPECT_EQ(PushBodiesFromCharacterMoveUVE(entityManager, contacts, push), 1U);
    EXPECT_NEAR(entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(dynamicBody).velocity.x, 180.0F, 0.5F);
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(kinematicBody).velocity.x, 0.0F);
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(weightlessBody).velocity.x, 0.0F);
}

TEST_F(Character3DStepUVETest, ABodyThatIsNotBeingSimulatedIsNotPushedByAWalkIntoIt) {
    const Scene::EntityUVE floor = MakeFloorUVE(0.0F);
    static_cast<void>(floor);
    // Two bodies in the character's path. One is stopped with MakeStatic and one is taken out of
    // the world entirely; a walk into either must leave its velocity alone, whatever the push
    // policy says - a push is something the simulation does to a body it is simulating.
    const Scene::EntityUVE parked = MakeBoxUVE({2.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F});
    entityManager.AddComponentUVE<Scene::Rigid3DComponentUVE>(parked, Scene::Rigid3DComponentUVE{});
    Scene::ProcessComponentUVE stopped{};
    stopped.mode = Scene::TickModeUVE::Never;
    entityManager.AddComponentUVE<Scene::ProcessComponentUVE>(parked, stopped);
    Scene::PhysicsObjectComponentUVE parkedObject{};
    parkedObject.disableMode = Scene::PhysicsObjectDisableModeUVE::MakeStatic;
    entityManager.AddComponentUVE<Scene::PhysicsObjectComponentUVE>(parked, parkedObject);

    const Scene::EntityUVE gone = MakeBoxUVE({2.0F, 1.5F, 0.0F}, {0.5F, 0.5F, 0.5F});
    entityManager.AddComponentUVE<Scene::Rigid3DComponentUVE>(gone, Scene::Rigid3DComponentUVE{});
    entityManager.AddComponentUVE<Scene::ProcessComponentUVE>(gone, stopped);
    Scene::PhysicsObjectComponentUVE goneObject{};
    goneObject.disableMode = Scene::PhysicsObjectDisableModeUVE::Remove;
    entityManager.AddComponentUVE<Scene::PhysicsObjectComponentUVE>(gone, goneObject);

    // Both get contacts handed straight to the push, as if the mover had reported them.
    const Math::Vector3UVE intoThem{3.0F, 0.0F, 0.0F};
    std::vector<Scene::CharacterSlideCollisionUVE> contacts;
    for (const Scene::EntityUVE entity : {parked, gone}) {
        Scene::CharacterSlideCollisionUVE contact;
        contact.entity = entity;
        contact.normal = Math::Vector3UVE{-1.0F, 0.0F, 0.0F};
        contact.motion = intoThem;
        contacts.push_back(contact);
    }
    sceneGraph.UpdateUVE(entityManager);

    EXPECT_EQ(PushBodiesFromCharacterMoveUVE(
                  entityManager, contacts,
                  Physics::CharacterDynamicPushPolicyUVE{true, 1.0F, 5.0F, kDeltaTimeUVE}),
              0U);
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(parked).velocity.x, 0.0F);
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(gone).velocity.x, 0.0F);

    // And the same contacts against a body that is being simulated still push, so the test is
    // about the state and not about the contact list.
    const Scene::EntityUVE live = MakeBoxUVE({2.0F, 2.5F, 0.0F}, {0.5F, 0.5F, 0.5F});
    entityManager.AddComponentUVE<Scene::Rigid3DComponentUVE>(live, Scene::Rigid3DComponentUVE{});
    Scene::CharacterSlideCollisionUVE liveContact;
    liveContact.entity = live;
    liveContact.normal = Math::Vector3UVE{-1.0F, 0.0F, 0.0F};
    liveContact.motion = intoThem;
    EXPECT_EQ(PushBodiesFromCharacterMoveUVE(
                  entityManager, std::vector<Scene::CharacterSlideCollisionUVE>{liveContact},
                  Physics::CharacterDynamicPushPolicyUVE{true, 1.0F, 5.0F, kDeltaTimeUVE}),
              1U);
    EXPECT_NEAR(entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(live).velocity.x, 5.0F, 1.0e-3F);
}
TEST_F(Character3DStepUVETest, APushPolicyThatIsOffOrNonsenseLeavesEveryBodyAlone) {
    const Scene::EntityUVE floor = MakeFloorUVE(0.0F);
    static_cast<void>(floor);
    const Scene::EntityUVE body = MakeBoxUVE({2.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F});
    entityManager.AddComponentUVE<Scene::Rigid3DComponentUVE>(body, Scene::Rigid3DComponentUVE{});

    Scene::CharacterSlideCollisionUVE contact;
    contact.entity = body;
    contact.normal = Math::Vector3UVE{-1.0F, 0.0F, 0.0F};
    contact.motion = Math::Vector3UVE{3.0F, 0.0F, 0.0F};
    const std::vector<Scene::CharacterSlideCollisionUVE> contacts{contact};

    // Off, no strength, no time to push over: three ways to say "nothing happens".
    for (const Physics::CharacterDynamicPushPolicyUVE& policy :
         {Physics::CharacterDynamicPushPolicyUVE{false, 1.0F, 5.0F, kDeltaTimeUVE},
          Physics::CharacterDynamicPushPolicyUVE{true, 0.0F, 5.0F, kDeltaTimeUVE},
          Physics::CharacterDynamicPushPolicyUVE{true, 1.0F, 5.0F, 0.0F}}) {
        EXPECT_EQ(PushBodiesFromCharacterMoveUVE(entityManager, contacts, policy), 0U);
    }
    // The default fallback for a nonsense cap is the engine's own, not an infinite push.
    EXPECT_EQ(PushBodiesFromCharacterMoveUVE(
                  entityManager, contacts,
                  Physics::CharacterDynamicPushPolicyUVE{true, 1.0F, -1.0F, kDeltaTimeUVE}),
              1U);
    EXPECT_NEAR(entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(body).velocity.x, 5.0F, 1.0e-3F);
}

} // namespace
} // namespace UVE::Physics::Tests
