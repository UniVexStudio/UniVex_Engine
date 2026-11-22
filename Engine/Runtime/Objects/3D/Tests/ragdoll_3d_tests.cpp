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

#include "collider_component.h"
#include "rigid_3d_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "quaternion.h"
#include "vector3.h"
#include "memory_manager.h"
#include "physical_bone_3d.h"
#include "ragdoll_3d.h"
#include "skeleton_3d.h"

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

// ---- AAA Overhaul Regression & Capability Tests -----------------------------------------------

TEST_F(Ragdoll3DUVETest, RootPositionWriteBack_DropsRenderedPelvisToFloorInsteadOfFloating) {
    // Regression test for Bug #1 (Floating Corpse): previously only rotation was written back,
    // leaving `skeleton.pose[0].position` frozen in mid-air while physics fell to the ground!
    const EntityUVE ragdollEntity = MakeHumanoidRagdollUVE();
    const EntityUVE skeletonEntity = entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(ragdollEntity).skeleton;
    ASSERT_TRUE(InitializeRagdoll3DUVE(entityManager, ragdollEntity));

    constexpr float kFloorTop = 0.0F;
    static_cast<void>(MakeBoxColliderUVE({0.0F, kFloorTop - 0.5F, 0.0F}, {20.0F, 0.5F, 20.0F}));

    const float initialPoseY = entityManager.GetComponentUVE<Skeleton3DComponentUVE>(skeletonEntity).pose[0].position.y;
    for (int frame = 0; frame < 120; ++frame) {
        UpdateRagdoll3DObjectsUVE(entityManager, 1.0F / 60.0F);
    }
    const float finalPoseY = entityManager.GetComponentUVE<Skeleton3DComponentUVE>(skeletonEntity).pose[0].position.y;
    EXPECT_LT(finalPoseY, initialPoseY - 0.35F)
        << "rendered root/pelvis pose must fall toward the floor instead of floating at bind height";
    EXPECT_GT(finalPoseY, kFloorTop - 0.1F);
}

TEST_F(Ragdoll3DUVETest, SmartSleep_RestingCorpseSleepsAndStaysAsleepOnStaticFloorUntilHit) {
    // Regression test for Bug #3: resting floor contacts previously woke the solver every frame forever!
    const EntityUVE ragdollEntity = MakeHumanoidRagdollUVE();
    Ragdoll3DComponentUVE& ragdoll = entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(ragdollEntity);
    ragdoll.stepSettings.sleepDelaySeconds = 0.2F;
    ragdoll.stepSettings.sleepLinearSpeed = 0.15F;
    ragdoll.stepSettings.sleepAngularSpeed = 0.4F;
    ASSERT_TRUE(InitializeRagdoll3DUVE(entityManager, ragdollEntity));

    static_cast<void>(MakeBoxColliderUVE({0.0F, -0.5F, 0.0F}, {20.0F, 0.5F, 20.0F}));

    bool wentToSleep = false;
    for (int frame = 0; frame < 360; ++frame) {
        UpdateRagdoll3DObjectsUVE(entityManager, 1.0F / 60.0F);
        if (ragdoll.solver.IsSleepingUVE()) {
            wentToSleep = true;
            break;
        }
    }
    ASSERT_TRUE(wentToSleep) << "settled ragdoll on a static floor must go to sleep";

    // Must STAY asleep across subsequent frames despite resting floor contacts!
    for (int frame = 0; frame < 60; ++frame) {
        EXPECT_EQ(UpdateRagdoll3DObjectsUVE(entityManager, 1.0F / 60.0F), 0U);
        EXPECT_TRUE(ragdoll.solver.IsSleepingUVE());
    }

    // External hit wakes it immediately.
    const Math::Vector3UVE pelvisPos = ragdoll.solver.GetBodyUVE(0U).position;
    ASSERT_TRUE(ApplyRagdollImpactUVE(entityManager, ragdollEntity, pelvisPos, {25.0F, 5.0F, 0.0F}, 1.0F));
    EXPECT_FALSE(ragdoll.solver.IsSleepingUVE());
}

TEST_F(Ragdoll3DUVETest, DynamicRigid3DPushback_TransfersMomentumToDynamicCrate) {
    const EntityUVE ragdollEntity = MakeHumanoidRagdollUVE();
    ASSERT_TRUE(InitializeRagdoll3DUVE(entityManager, ragdollEntity));

    // Dynamic Rigid3D crate directly beneath the falling ragdoll.
    const EntityUVE crate = MakeBoxColliderUVE({0.0F, 0.0F, 0.0F}, {1.0F, 0.5F, 1.0F});
    Rigid3DComponentUVE rigid{};
    rigid.mass = 10.0F;
    rigid.isKinematic = false;
    entityManager.AddComponentUVE<Rigid3DComponentUVE>(crate, rigid);

    for (int frame = 0; frame < 60; ++frame) {
        UpdateRagdoll3DObjectsUVE(entityManager, 1.0F / 60.0F);
    }
    const Rigid3DComponentUVE& after = entityManager.GetComponentUVE<Rigid3DComponentUVE>(crate);
    EXPECT_LT(after.velocity.y, -0.05F) << "falling ragdoll must impart downward momentum to a dynamic Rigid3D";
}

TEST_F(Ragdoll3DUVETest, MixamoTPoseRig_BuildsMultiChildPelvisUpwardAndOrthogonalElbowHinges) {
    // Regression test for Bug #5: Mixamo names (`Hips`, `LeftUpLeg`, `LeftLeg`, `LeftArm`, `LeftForeArm`),
    // with thighs listed BEFORE spine, and arms stretched laterally along +/-X (T-pose).
    Skeleton3DComponentUVE mixamo;
    mixamo.skeletonAssetPath = "MixamoTPose.glb";
    auto addBone = [&](const char* name, std::int32_t parent, Math::Vector3UVE pos) {
        SkeletonBoneUVE b;
        b.name = name;
        b.parentIndex = parent;
        b.localPosition = pos;
        mixamo.bones.push_back(b);
    };
    addBone("mixamorig:Hips", -1, {0.0F, 1.0F, 0.0F});          // 0
    addBone("mixamorig:LeftUpLeg", 0, {0.1F, -0.05F, 0.0F});    // 1 (listed BEFORE spine!)
    addBone("mixamorig:LeftLeg", 1, {0.0F, -0.45F, 0.0F});      // 2
    addBone("mixamorig:Spine", 0, {0.0F, 0.25F, 0.0F});         // 3
    addBone("mixamorig:LeftArm", 3, {0.2F, 0.2F, 0.0F});        // 4
    addBone("mixamorig:LeftForeArm", 4, {0.3F, 0.0F, 0.0F});    // 5 (+X T-pose arm!)
    addBone("mixamorig:LeftHand", 5, {0.28F, 0.0F, 0.0F});      // 6
    addBone("mixamorig:RightArm", 3, {-0.2F, 0.2F, 0.0F});      // 7
    addBone("mixamorig:RightForeArm", 7, {-0.3F, 0.0F, 0.0F});  // 8 (-X T-pose arm!)
    addBone("mixamorig:RightHand", 8, {-0.28F, 0.0F, 0.0F});    // 9

    const std::vector<RagdollBoneSetupUVE> built = BuildHumanoidRagdollBonesUVE(mixamo.bones);
    EXPECT_EQ(built.size(), 10U) << "all 10 Mixamo bones should match the expanded profile";

    const EntityUVE skelEnt = MakeSkeletonEntityUVE(mixamo);
    const EntityUVE ragEnt = MakeRagdollUVE(skelEnt);
    ASSERT_TRUE(InitializeRagdoll3DUVE(entityManager, ragEnt));

    const Ragdoll3DComponentUVE& rag = entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(ragEnt);
    // Pelvis capsule axis must point UP toward Spine (+Y), NOT sideways down LeftUpLeg!
    const Math::Vector3UVE pelvisAxis =
        Math::RotateVectorUVE(rag.solver.GetBodyUVE(0U).rotation, {0.0F, 1.0F, 0.0F});
    EXPECT_GT(pelvisAxis.y, 0.9F);

    // Find LeftForeArm elbow joint: its twist axis MUST be orthogonal to the arm (+X), NOT parallel to +X!
    bool checkedLeftElbow = false;
    for (std::size_t j = 0U; j < rag.solver.GetJointCountUVE(); ++j) {
        const Physics::RagdollJointUVE& joint = rag.solver.GetJointUVE(j);
        for (const RagdollBoneBindingUVE& b : rag.bindings) {
            if (b.bodyIndex == joint.secondBody && mixamo.bones[b.boneIndex].name == "mixamorig:LeftForeArm") {
                const Physics::RagdollBodyUVE& parentBody = rag.solver.GetBodyUVE(joint.firstBody);
                const Math::QuaternionUVE worldFrame = Math::MultiplyUVE(parentBody.rotation, joint.firstFrame);
                const Math::Vector3UVE hingeWorldAxis = Math::RotateVectorUVE(worldFrame, {1.0F, 0.0F, 0.0F});
                EXPECT_LT(std::fabs(hingeWorldAxis.x), 0.15F)
                    << "T-pose elbow hinge axis must NOT be parallel to the arm (+X)";
                EXPECT_GT(std::fabs(hingeWorldAxis.y), 0.85F)
                    << "T-pose elbow hinge axis must be roughly vertical (+/-Y) to fold forearm forward";
                checkedLeftElbow = true;
            }
        }
    }
    EXPECT_TRUE(checkedLeftElbow);
}

TEST_F(Ragdoll3DUVETest, ActiveModePartialSubtreeVelocityInheritanceAndGetUpBlendOut) {
    const EntityUVE ragdollEntity = MakeHumanoidRagdollUVE();
    const EntityUVE skeletonEntity = entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(ragdollEntity).skeleton;
    Ragdoll3DComponentUVE& ragdoll = entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(ragdollEntity);
    ragdoll.mode = RagdollModeUVE::Kinematic;
    ASSERT_TRUE(InitializeRagdoll3DUVE(entityManager, ragdollEntity));

    // Simulate 2 frames of rightward animated motion in Kinematic mode -> velocity inheritance!
    Skeleton3DComponentUVE& skel = entityManager.GetComponentUVE<Skeleton3DComponentUVE>(skeletonEntity);
    skel.pose[0].position = {0.0F, 1.0F, 0.0F};
    UpdateRagdoll3DObjectsUVE(entityManager, 1.0F / 60.0F);
    skel.pose[0].position = {0.1F, 1.0F, 0.0F}; // +6 m/s rightward
    UpdateRagdoll3DObjectsUVE(entityManager, 1.0F / 60.0F);

    ASSERT_TRUE(ActivateRagdollUVE(entityManager, ragdollEntity, RagdollModeUVE::Simulating));
    EXPECT_GT(ragdoll.solver.GetBodyUVE(0U).linearVelocity.x, 3.0F)
        << "activating ragdoll from Kinematic mode must inherit animated linear velocity";

    // Test partial subtree mode: simulate ONLY spine_01+head while pelvis+leg stay kinematic!
    ragdoll.partialRootBoneName = "spine_01";
    ragdoll.mode = RagdollModeUVE::Active;
    ragdoll.motorStiffness = 200.0F;
    ragdoll.motorDamping = 24.0F;
    ragdoll.maxMotorTorque = 200.0F;
    UpdateRagdoll3DObjectsUVE(entityManager, 1.0F / 60.0F);
    EXPECT_TRUE(ragdoll.solver.GetBodyUVE(0U).kinematic) << "pelvis outside partial subtree must be kinematic";
    EXPECT_FALSE(ragdoll.solver.GetBodyUVE(1U).kinematic) << "spine_01 inside partial subtree must simulate";

    // Test Prone/Supine Get-Up state classification & Pose Snapshot smooth BlendingOut!
    ragdoll.partialRootBoneName.clear();
    ragdoll.mode = RagdollModeUVE::Simulating;
    Math::QuaternionUVE faceUpRot{};
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE({1.0F, 0.0F, 0.0F}, -1.5707963F, faceUpRot));
    ragdoll.solver.GetBodyUVE(0U).rotation = faceUpRot; // Local +Z now points +Y (face up!)
    RagdollGetUpStateUVE getUp{};
    ASSERT_TRUE(EvaluateRagdollGetUpStateUVE(entityManager, ragdollEntity, getUp));
    EXPECT_TRUE(getUp.valid);
    EXPECT_EQ(getUp.orientation, RagdollLyingOrientationUVE::FaceUp);

    ASSERT_TRUE(BeginBlendOutFromRagdollUVE(entityManager, ragdollEntity, 0.2F));
    EXPECT_EQ(ragdoll.mode, RagdollModeUVE::BlendingOut);
    for (int frame = 0; frame < 20; ++frame) {
        UpdateRagdoll3DObjectsUVE(entityManager, 1.0F / 60.0F);
    }
    EXPECT_EQ(ragdoll.mode, RagdollModeUVE::Kinematic) << "completed BlendingOut transitions cleanly to Kinematic";

    // Test Editor Live Preview backup & exact pose restoration!
    skel.pose[0].position = {0.0F, 1.0F, 0.0F};
    ASSERT_TRUE(StartRagdollEditorPreviewUVE(entityManager, ragdollEntity));
    EXPECT_TRUE(ragdoll.editorPreviewSimulating);
    for (int frame = 0; frame < 30; ++frame) {
        UpdateRagdoll3DObjectsUVE(entityManager, 1.0F / 60.0F, /*editorPreviewOnly=*/true);
    }
    EXPECT_LT(skel.pose[0].position.y, 0.9F);
    ASSERT_TRUE(StopRagdollEditorPreviewUVE(entityManager, ragdollEntity));
    EXPECT_FALSE(ragdoll.editorPreviewSimulating);
    EXPECT_NEAR(skel.pose[0].position.y, 1.0F, 1.0e-4F) << "stopping Editor Preview must restore exact bind pose";

    // Test MobileFast preset scales down cost for low-end Android phones.
    ApplyRagdollQualityPresetUVE(ragdoll, RagdollQualityPresetUVE::MobileFast);
    EXPECT_FALSE(ragdoll.enableSelfCollision);
    EXPECT_FALSE(ragdoll.enableDynamicPushback);
    EXPECT_EQ(ragdoll.maxSubstepsPerFrame, 2U);
}

TEST_F(Ragdoll3DUVETest, PhysicalBone3D_DelegatesToAAASolverAndStopsOnFloorCollider) {
    static_cast<void>(MakeBoxColliderUVE({0.0F, -0.5F, 0.0F}, {20.0F, 0.5F, 20.0F}));
    const EntityUVE pbEntity = entityManager.CreateEntityUVE();
    PhysicalBone3DObjectDefinitionUVE def{};
    def.physicalBone.mass = 3.0F;
    def.physicalBone.simulatePhysics = true;
    ApplyPhysicalBone3DObjectDefinitionUVE(entityManager, pbEntity, def);
    entityManager.GetComponentUVE<TransformComponentUVE>(pbEntity).localPosition = {0.0F, 0.6F, 0.0F};

    for (int frame = 0; frame < 90; ++frame) {
        EXPECT_EQ(UpdatePhysicalBone3DObjectsUVE(entityManager, {0.0F, -9.81F, 0.0F}, 1.0F / 60.0F), 1U);
    }
    const float finalY = entityManager.GetComponentUVE<TransformComponentUVE>(pbEntity).localPosition.y;
    EXPECT_GT(finalY, 0.05F) << "upgraded PhysicalBone3D must rest on floor collider instead of tunneling forever";
    EXPECT_LT(finalY, 0.35F);
}

} // namespace
} // namespace UVE::Scene::Tests
