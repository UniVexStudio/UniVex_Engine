// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/component/collider_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/ragdoll_3d_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"

namespace UVE::Scene::Tests {
namespace {

/// A six-bone humanoid: pelvis, one spine, head, and one leg. The names are deliberately the plain
/// lowercase form the profile table matches, because that is the shape a rigger's DCC exports.
[[nodiscard]] Skeleton3DComponentUVE MakeHumanoidSkeletonUVE() {
    Skeleton3DComponentUVE skeleton;
    skeleton.skeletonAssetPath = "Hero.fbx";

    SkeletonBoneUVE pelvis;
    pelvis.name = "pelvis";
    pelvis.localPosition = {0.0F, 1.0F, 0.0F};

    SkeletonBoneUVE spine;
    spine.name = "spine_01";
    spine.parentIndex = 0;
    spine.localPosition = {0.0F, 0.2F, 0.0F};

    SkeletonBoneUVE head;
    head.name = "head";
    head.parentIndex = 1;
    head.localPosition = {0.0F, 0.35F, 0.0F};

    SkeletonBoneUVE thigh;
    thigh.name = "thigh_l";
    thigh.parentIndex = 0;
    thigh.localPosition = {0.1F, -0.05F, 0.0F};

    SkeletonBoneUVE shin;
    shin.name = "shin_l";
    shin.parentIndex = 3;
    shin.localPosition = {0.0F, -0.42F, 0.0F};

    SkeletonBoneUVE foot;
    foot.name = "foot_l";
    foot.parentIndex = 4;
    foot.localPosition = {0.0F, -0.4F, 0.0F};

    skeleton.bones = {pelvis, spine, head, thigh, shin, foot};
    return skeleton;
}

class Ragdoll3DUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};

    [[nodiscard]] EntityUVE MakeSkeletonEntityUVE(const Skeleton3DComponentUVE& skeleton,
                                                  const Math::Vector3UVE worldPosition = {}) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        entityManager.AddComponentUVE<TransformComponentUVE>(entity, TransformComponentUVE{});
        WorldTransformComponentUVE world;
        world.worldPosition = worldPosition;
        world.dirty = false;
        entityManager.AddComponentUVE<WorldTransformComponentUVE>(entity, world);
        entityManager.AddComponentUVE<Skeleton3DComponentUVE>(entity, skeleton);
        return entity;
    }

    [[nodiscard]] EntityUVE MakeRagdollUVE(const EntityUVE skeleton, Ragdoll3DComponentUVE ragdoll = {}) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        ragdoll.skeleton = skeleton;
        entityManager.AddComponentUVE<Ragdoll3DComponentUVE>(entity, ragdoll);
        return entity;
    }

    [[nodiscard]] EntityUVE MakeBoxColliderUVE(const Math::Vector3UVE worldPosition,
                                               const Math::Vector3UVE halfExtents) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        entityManager.AddComponentUVE<TransformComponentUVE>(entity, TransformComponentUVE{});
        WorldTransformComponentUVE world;
        world.worldPosition = worldPosition;
        world.dirty = false;
        entityManager.AddComponentUVE<WorldTransformComponentUVE>(entity, world);
        ColliderComponentUVE collider;
        collider.shapeType = ColliderShapeTypeUVE::Box;
        collider.halfExtents = halfExtents;
        entityManager.AddComponentUVE<ColliderComponentUVE>(entity, collider);
        return entity;
    }

    [[nodiscard]] EntityUVE MakeHumanoidRagdollUVE() {
        const EntityUVE skeleton = MakeSkeletonEntityUVE(MakeHumanoidSkeletonUVE());
        return MakeRagdollUVE(skeleton);
    }
};

// ---- The profile table ------------------------------------------------------------------------

TEST_F(Ragdoll3DUVETest, HumanoidProfileUVE_IsWellFormed) {
    const std::span<const HumanoidRagdollProfileEntryUVE> profile = GetHumanoidRagdollProfileUVE();
    ASSERT_FALSE(profile.empty());
    for (const HumanoidRagdollProfileEntryUVE& entry : profile) {
        EXPECT_FALSE(entry.pattern.empty());
        for (const char character : entry.pattern) {
            EXPECT_EQ(character, static_cast<char>(std::tolower(static_cast<unsigned char>(character))))
                << "the table is matched against a lowercased bone name";
        }
        EXPECT_GT(entry.mass, 0.0F);
        EXPECT_GT(entry.radius, 0.0F);
        EXPECT_GE(entry.halfHeight, 0.0F);
        EXPECT_GE(entry.angularDamping, 0.0F);
        if (entry.hinge) {
            // A hinge bends one way. A symmetric range here is how a knee ends up folding backwards.
            EXPECT_LT(entry.twistLowerLimitDegrees, entry.twistUpperLimitDegrees);
        }
    }
}

TEST_F(Ragdoll3DUVETest, BuildHumanoidRagdollBonesUVE_UsesTheRigsOwnBoneNames) {
    const std::vector<RagdollBoneSetupUVE> bones = BuildHumanoidRagdollBonesUVE(MakeHumanoidSkeletonUVE().bones);
    ASSERT_EQ(bones.size(), 6U);
    for (std::size_t index = 0U; index < bones.size(); ++index) {
        EXPECT_FALSE(bones[index].boneName.empty());
        EXPECT_GT(bones[index].mass, 0.0F);
    }
    EXPECT_EQ(bones[0].boneName, "pelvis") << "hierarchy order is preserved";
    EXPECT_FLOAT_EQ(bones[0].mass, 12.0F);

    // The knee is a hinge: both swings locked, the twist limited to a one-way range.
    const auto shin = std::find_if(bones.begin(), bones.end(), [](const RagdollBoneSetupUVE& bone) {
        return bone.boneName == "shin_l";
    });
    ASSERT_NE(shin, bones.end());
    EXPECT_EQ(shin->swing1Motion, Physics::RagdollMotionUVE::Locked);
    EXPECT_EQ(shin->swing2Motion, Physics::RagdollMotionUVE::Locked);
    EXPECT_EQ(shin->twistMotion, Physics::RagdollMotionUVE::Limited);
    EXPECT_TRUE(shin->twistAsymmetric);
    EXPECT_LT(shin->twistLowerLimitDegrees, shin->twistUpperLimitDegrees);
    EXPECT_LT(shin->twistUpperLimitDegrees, 10.0F) << "a knee does not hyperextend";
}

TEST_F(Ragdoll3DUVETest, BuildHumanoidRagdollBonesUVE_LeavesAnUnknownRigAlone) {
    std::vector<SkeletonBoneUVE> bones(3U);
    bones[0].name = "bone_01";
    bones[1].name = "prop_02";
    bones[2].name = "socket_03";
    EXPECT_TRUE(BuildHumanoidRagdollBonesUVE(bones).empty())
        << "guessing a body for a bone the table does not know is worse than not simulating";
}

// ---- Authorship validation --------------------------------------------------------------------

TEST_F(Ragdoll3DUVETest, IsRagdoll3DComponentValidUVE_RejectsBadAuthorship) {
    Ragdoll3DComponentUVE ragdoll;
    EXPECT_FALSE(IsRagdoll3DComponentValidUVE(ragdoll)) << "no skeleton";

    const EntityUVE authoredSkeleton = MakeSkeletonEntityUVE(MakeHumanoidSkeletonUVE());
    ragdoll.skeleton = authoredSkeleton;
    EXPECT_TRUE(IsRagdoll3DComponentValidUVE(ragdoll));

    RagdollBoneSetupUVE bone;
    bone.boneName = "pelvis";
    ragdoll.bones = {bone};
    EXPECT_TRUE(IsRagdoll3DComponentValidUVE(ragdoll));

    ragdoll.bones[0].boneName.clear();
    EXPECT_FALSE(IsRagdoll3DComponentValidUVE(ragdoll)) << "a nameless bone resolves to nothing";
    ragdoll.bones[0].boneName = "pelvis";

    ragdoll.bones[0].mass = 0.0F;
    EXPECT_FALSE(IsRagdoll3DComponentValidUVE(ragdoll));
    ragdoll.bones[0].mass = 1.0F;

    ragdoll.bones[0].friction = 1.5F;
    EXPECT_FALSE(IsRagdoll3DComponentValidUVE(ragdoll));
    ragdoll.bones[0].friction = 0.8F;

    ragdoll.bones[0].twistAsymmetric = true;
    ragdoll.bones[0].twistLowerLimitDegrees = 40.0F;
    ragdoll.bones[0].twistUpperLimitDegrees = -40.0F;
    EXPECT_FALSE(IsRagdoll3DComponentValidUVE(ragdoll)) << "a backwards range is a typo, not a limit";

    ragdoll.gravity = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    EXPECT_FALSE(IsRagdoll3DComponentValidUVE(ragdoll));
}

TEST_F(Ragdoll3DUVETest, Ragdoll3DComponentUVE_EqualityIgnoresRuntimeState) {
    Ragdoll3DComponentUVE authored;
    authored.skeleton = EntityUVE{4U, 1U};
    RagdollBoneSetupUVE bone;
    bone.boneName = "pelvis";
    bone.mass = 12.0F;
    authored.bones = {bone};

    Ragdoll3DComponentUVE simulated = authored;
    simulated.initialized = true;
    simulated.pendingSeconds = 0.25F;
    simulated.bindings.resize(3U);
    EXPECT_TRUE(authored == simulated) << "a running ragdoll is not a different ragdoll";

    simulated.bones[0].mass = 13.0F;
    EXPECT_FALSE(authored == simulated);
}

// ---- Initialisation from the rig --------------------------------------------------------------

TEST_F(Ragdoll3DUVETest, InitializeRagdoll3DUVE_BuildsOneBodyPerSimulatedBoneAndOneJointBetween) {
    const EntityUVE ragdollEntity = MakeHumanoidRagdollUVE();
    ASSERT_TRUE(InitializeRagdoll3DUVE(entityManager, ragdollEntity));

    const Ragdoll3DComponentUVE& ragdoll = entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(ragdollEntity);
    EXPECT_TRUE(ragdoll.initialized);
    ASSERT_EQ(ragdoll.solver.GetBodyCountUVE(), 6U);
    EXPECT_EQ(ragdoll.solver.GetJointCountUVE(), 5U) << "a tree of six has five joints";
    EXPECT_EQ(ragdoll.bindings.size(), 6U);
    EXPECT_NEAR(ragdoll.solver.GetTotalMassUVE(), 12.0F + 6.0F + 5.0F + 9.0F + 3.5F + 1.0F, 1.0e-3F);

    // The root of a ragdoll is not jointed to anything; everything else is.
    std::size_t roots = 0U;
    for (const RagdollBoneBindingUVE& binding : ragdoll.bindings) {
        EXPECT_LT(binding.boneIndex, 6U);
        EXPECT_NE(binding.boneIndex, kInvalidSkeletonBoneIndexUVE);
        if (binding.parentBoneIndex == kInvalidSkeletonBoneIndexUVE) ++roots;
    }
    EXPECT_EQ(roots, 1U);
}

TEST_F(Ragdoll3DUVETest, InitializeRagdoll3DUVE_RefusesWhatItCannotSimulate) {
    // No such entity.
    EXPECT_FALSE(InitializeRagdoll3DUVE(entityManager, EntityUVE{1234U, 1U}));

    // A ragdoll whose skeleton has no bones.
    Skeleton3DComponentUVE empty;
    const EntityUVE emptySkeleton = MakeSkeletonEntityUVE(empty);
    EXPECT_FALSE(InitializeRagdoll3DUVE(entityManager, MakeRagdollUVE(emptySkeleton)));

    // A skeleton whose bones the humanoid table does not know, with no explicit bones authored.
    Skeleton3DComponentUVE unknown;
    SkeletonBoneUVE bone;
    bone.name = "bone_01";
    unknown.bones = {bone};
    const EntityUVE unknownSkeleton = MakeSkeletonEntityUVE(unknown);
    const EntityUVE unknownRagdoll = MakeRagdollUVE(unknownSkeleton);
    EXPECT_FALSE(InitializeRagdoll3DUVE(entityManager, unknownRagdoll));
    EXPECT_FALSE(entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(unknownRagdoll).initialized);
}

// ---- The update pass --------------------------------------------------------------------------

TEST_F(Ragdoll3DUVETest, UpdateRagdoll3DObjectsUVE_RefusesNonPositiveTime) {
    const EntityUVE ragdoll = MakeHumanoidRagdollUVE();
    EXPECT_EQ(UpdateRagdoll3DObjectsUVE(entityManager, 0.0F), 0U);
    EXPECT_EQ(UpdateRagdoll3DObjectsUVE(entityManager, -0.5F), 0U);
    EXPECT_EQ(UpdateRagdoll3DObjectsUVE(entityManager, std::numeric_limits<float>::quiet_NaN()), 0U);
    EXPECT_FALSE(entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(ragdoll).initialized)
        << "a refused call must not initialise anything";
}

TEST_F(Ragdoll3DUVETest, UpdateRagdoll3DObjectsUVE_StepsTheBodyOnAFixedClock) {
    const EntityUVE ragdollEntity = MakeHumanoidRagdollUVE();
    ASSERT_TRUE(InitializeRagdoll3DUVE(entityManager, ragdollEntity));
    const float startHeight =
        entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(ragdollEntity).solver.GetBodyUVE(0).position.y;

    // One 1/60 s frame is four fixed steps at 1/240 s, and the accumulator keeps the remainder.
    EXPECT_EQ(UpdateRagdoll3DObjectsUVE(entityManager, 1.0F / 60.0F), 1U);
    Ragdoll3DComponentUVE& ragdoll = entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(ragdollEntity);
    EXPECT_TRUE(ragdoll.lastReport.stepped);
    EXPECT_NEAR(ragdoll.pendingSeconds, 0.0F, 1.0e-6F);
    EXPECT_LT(ragdoll.solver.GetBodyUVE(0).position.y, startHeight);

    // A frame too short for a whole step does not advance the simulation at all; the fraction stays
    // in the accumulator for the next frame.
    const float stepSeconds = Physics::kRagdollFixedStepSecondsUVE;
    EXPECT_EQ(UpdateRagdoll3DObjectsUVE(entityManager, stepSeconds * 0.25F), 0U);
    EXPECT_NEAR(ragdoll.pendingSeconds, stepSeconds * 0.25F, 1.0e-6F);

    // A 100 ms hitch would be 24 steps; the cap is 8, so the accumulator is clamped instead of
    // replaying the whole frame and the simulation slips against wall-clock for one frame.
    EXPECT_EQ(UpdateRagdoll3DObjectsUVE(entityManager, 0.1F), 1U);
    EXPECT_LE(ragdoll.pendingSeconds,
              stepSeconds * static_cast<float>(Physics::kMaximumRagdollSubstepsUVE) + 1.0e-6F);
}

TEST_F(Ragdoll3DUVETest, UpdateRagdoll3DObjectsUVE_WritesTheSimulationBackIntoThePose) {
    const EntityUVE ragdollEntity = MakeHumanoidRagdollUVE();
    const EntityUVE skeletonEntity = entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(ragdollEntity).skeleton;
    ASSERT_TRUE(InitializeRagdoll3DUVE(entityManager, ragdollEntity));
    Skeleton3DComponentUVE& skeleton = entityManager.GetComponentUVE<Skeleton3DComponentUVE>(skeletonEntity);
    ASSERT_EQ(skeleton.pose.size(), skeleton.bones.size());

    // A sideways blast on the pelvis: the rig must end up rotated, not just translated.
    const Math::Vector3UVE pelvis = skeleton.pose[0].position + Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    ASSERT_TRUE(ApplyRagdollImpactUVE(entityManager, ragdollEntity, pelvis,
                                      Math::Vector3UVE{30.0F, 0.0F, 0.0F}));
    for (int frame = 0; frame < 30; ++frame) {
        UpdateRagdoll3DObjectsUVE(entityManager, 1.0F / 60.0F);
    }

    float maximumRotationDeviation = 0.0F;
    for (const SkeletonBonePoseUVE& pose : skeleton.pose) {
        maximumRotationDeviation =
            std::max(maximumRotationDeviation, std::fabs(1.0F - std::fabs(pose.rotation.w)));
    }
    EXPECT_GT(maximumRotationDeviation, 1.0e-3F) << "the mesh follows the ragdoll";
    EXPECT_EQ(skeleton.pose.size(), skeleton.bones.size()) << "the pose is written in place";
}

TEST_F(Ragdoll3DUVETest, UpdateRagdoll3DObjectsUVE_StopsAFallingBodyOnAMeshCollider) {
    const EntityUVE ragdollEntity = MakeHumanoidRagdollUVE();
    ASSERT_TRUE(InitializeRagdoll3DUVE(entityManager, ragdollEntity));

    // A wide slab whose top face sits at y = 0.5, six-tenths of a metre below the lowest limb.
    constexpr float kFloorTopUVE = 0.5F;
    static_cast<void>(MakeBoxColliderUVE({0.0F, kFloorTopUVE - 0.5F, 0.0F}, {20.0F, 0.5F, 20.0F}));

    Ragdoll3DComponentUVE& ragdoll = entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(ragdollEntity);
    bool sawContact = false;
    for (int frame = 0; frame < 240; ++frame) {
        UpdateRagdoll3DObjectsUVE(entityManager, 1.0F / 60.0F);
        if (ragdoll.lastReport.resolvedContactCount > 0U) sawContact = true;
    }
    EXPECT_TRUE(sawContact) << "the body never met the collider";

    // Lowest SURFACE, not lowest centre minus the capsule's half length: by the time it settles the
    // body is lying on its side, and the algebra has to follow the capsule axis where it points.
    float lowestSurface = std::numeric_limits<float>::infinity();
    for (std::size_t body = 0U; body < ragdoll.solver.GetBodyCountUVE(); ++body) {
        const Physics::RagdollBodyUVE& value = ragdoll.solver.GetBodyUVE(body);
        Math::Vector3UVE start{};
        Math::Vector3UVE end{};
        Physics::ComputeRagdollCapsuleWorldSegmentUVE(value, start, end);
        lowestSurface = std::min(lowestSurface, std::min(start.y, end.y) - value.radius);
    }
    // The capsule rests on the slab: it does not pass through it, and it does not stop a metre
    // above it either - either of those is what "no tunneling" is actually worth.
    EXPECT_GT(lowestSurface, kFloorTopUVE - 0.05F);
    EXPECT_LT(lowestSurface, kFloorTopUVE + 0.25F);
}

// ---- Impacts ----------------------------------------------------------------------------------

TEST_F(Ragdoll3DUVETest, ApplyRagdollImpactUVE_WakesTheBodyAndPushesTheNearestOne) {
    const EntityUVE ragdollEntity = MakeHumanoidRagdollUVE();
    ASSERT_TRUE(InitializeRagdoll3DUVE(entityManager, ragdollEntity));
    Ragdoll3DComponentUVE& ragdoll = entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(ragdollEntity);
    ragdoll.solver.SleepUVE();
    ASSERT_TRUE(ragdoll.solver.IsSleepingUVE());

    // Aim at the head, which is the third body in hierarchy order.
    const Physics::RagdollBodyUVE& head = ragdoll.solver.GetBodyUVE(2U);
    const Math::Vector3UVE hitPoint = head.position + Math::Vector3UVE{0.0F, 0.05F, 0.0F};
    ASSERT_TRUE(ApplyRagdollImpactUVE(entityManager, ragdollEntity, hitPoint,
                                      Math::Vector3UVE{0.0F, 0.0F, -40.0F}));
    EXPECT_FALSE(ragdoll.solver.IsSleepingUVE());
    EXPECT_LT(ragdoll.solver.GetBodyUVE(2U).linearVelocity.z, -0.1F);
    EXPECT_NEAR(ragdoll.solver.GetBodyUVE(0U).linearVelocity.z, 0.0F, 1.0e-6F)
        << "a hit on the head is not a hit on the pelvis";
}

TEST_F(Ragdoll3DUVETest, ApplyRagdollImpactUVE_RefusesAHitThatTouchesNothing) {
    const EntityUVE ragdollEntity = MakeHumanoidRagdollUVE();
    ASSERT_TRUE(InitializeRagdoll3DUVE(entityManager, ragdollEntity));
    Ragdoll3DComponentUVE& ragdoll = entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(ragdollEntity);

    EXPECT_FALSE(ApplyRagdollImpactUVE(entityManager, ragdollEntity, Math::Vector3UVE{50.0F, 50.0F, 50.0F},
                                       Math::Vector3UVE{1.0F, 0.0F, 0.0F}))
        << "a blast 80 m away must not move a corpse";
    EXPECT_FALSE(ApplyRagdollImpactUVE(entityManager, ragdollEntity, Math::Vector3UVE{0.0F, 1.0F, 0.0F},
                                       Math::Vector3UVE{1.0F, 0.0F, 0.0F}, -1.0F))
        << "a negative search radius is a caller bug";
    for (std::size_t body = 0U; body < ragdoll.solver.GetBodyCountUVE(); ++body) {
        EXPECT_FLOAT_EQ(ragdoll.solver.GetBodyUVE(body).linearVelocity.x, 0.0F);
    }
}

} // namespace
} // namespace UVE::Scene::Tests
