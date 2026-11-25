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
[[nodiscard]] Skeleton3DComponent MakeHumanoidSkeleton() {
    Skeleton3DComponent skeleton;
    skeleton.skeletonAssetPath = "Hero.fbx";

    SkeletonBone pelvis;
    pelvis.name = "pelvis";
    pelvis.localPosition = {0.0F, 1.0F, 0.0F};

    SkeletonBone spine;
    spine.name = "spine_01";
    spine.parentIndex = 0;
    spine.localPosition = {0.0F, 0.2F, 0.0F};

    SkeletonBone head;
    head.name = "head";
    head.parentIndex = 1;
    head.localPosition = {0.0F, 0.35F, 0.0F};

    SkeletonBone thigh;
    thigh.name = "thigh_l";
    thigh.parentIndex = 0;
    thigh.localPosition = {0.1F, -0.05F, 0.0F};

    SkeletonBone shin;
    shin.name = "shin_l";
    shin.parentIndex = 3;
    shin.localPosition = {0.0F, -0.42F, 0.0F};

    SkeletonBone foot;
    foot.name = "foot_l";
    foot.parentIndex = 4;
    foot.localPosition = {0.0F, -0.4F, 0.0F};

    skeleton.bones = {pelvis, spine, head, thigh, shin, foot};
    return skeleton;
}

class Ragdoll3DTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};

    [[nodiscard]] Entity MakeSkeletonEntity(const Skeleton3DComponent& skeleton,
                                                  const Math::Vector3 worldPosition = {}) {
        const Entity entity = entityManager.CreateEntity();
        entityManager.AddComponent<TransformComponent>(entity, TransformComponent{});
        WorldTransformComponent world;
        world.worldPosition = worldPosition;
        world.dirty = false;
        entityManager.AddComponent<WorldTransformComponent>(entity, world);
        entityManager.AddComponent<Skeleton3DComponent>(entity, skeleton);
        return entity;
    }

    [[nodiscard]] Entity MakeRagdoll(const Entity skeleton, Ragdoll3DComponent ragdoll = {}) {
        const Entity entity = entityManager.CreateEntity();
        ragdoll.skeleton = skeleton;
        entityManager.AddComponent<Ragdoll3DComponent>(entity, ragdoll);
        return entity;
    }

    [[nodiscard]] Entity MakeBoxCollider(const Math::Vector3 worldPosition,
                                               const Math::Vector3 halfExtents) {
        const Entity entity = entityManager.CreateEntity();
        entityManager.AddComponent<TransformComponent>(entity, TransformComponent{});
        WorldTransformComponent world;
        world.worldPosition = worldPosition;
        world.dirty = false;
        entityManager.AddComponent<WorldTransformComponent>(entity, world);
        ColliderComponent collider;
        collider.shapeType = ColliderShapeType::Box;
        collider.halfExtents = halfExtents;
        entityManager.AddComponent<ColliderComponent>(entity, collider);
        return entity;
    }

    [[nodiscard]] Entity MakeHumanoidRagdoll() {
        const Entity skeleton = MakeSkeletonEntity(MakeHumanoidSkeleton());
        return MakeRagdoll(skeleton);
    }
};

// ---- The profile table ------------------------------------------------------------------------

TEST_F(Ragdoll3DTest, HumanoidProfile_IsWellFormed) {
    const std::span<const HumanoidRagdollProfileEntry> profile = GetHumanoidRagdollProfile();
    ASSERT_FALSE(profile.empty());
    for (const HumanoidRagdollProfileEntry& entry : profile) {
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

TEST_F(Ragdoll3DTest, BuildHumanoidRagdollBones_UsesTheRigsOwnBoneNames) {
    const std::vector<RagdollBoneSetup> bones = BuildHumanoidRagdollBones(MakeHumanoidSkeleton().bones);
    ASSERT_EQ(bones.size(), 6U);
    for (std::size_t index = 0U; index < bones.size(); ++index) {
        EXPECT_FALSE(bones[index].boneName.empty());
        EXPECT_GT(bones[index].mass, 0.0F);
    }
    EXPECT_EQ(bones[0].boneName, "pelvis") << "hierarchy order is preserved";
    EXPECT_FLOAT_EQ(bones[0].mass, 12.0F);

    // The knee is a hinge: both swings locked, the twist limited to a one-way range.
    const auto shin = std::find_if(bones.begin(), bones.end(), [](const RagdollBoneSetup& bone) {
        return bone.boneName == "shin_l";
    });
    ASSERT_NE(shin, bones.end());
    EXPECT_EQ(shin->swing1Motion, Physics::RagdollMotion::Locked);
    EXPECT_EQ(shin->swing2Motion, Physics::RagdollMotion::Locked);
    EXPECT_EQ(shin->twistMotion, Physics::RagdollMotion::Limited);
    EXPECT_TRUE(shin->twistAsymmetric);
    EXPECT_LT(shin->twistLowerLimitDegrees, shin->twistUpperLimitDegrees);
    EXPECT_LT(shin->twistUpperLimitDegrees, 10.0F) << "a knee does not hyperextend";
}

TEST_F(Ragdoll3DTest, BuildHumanoidRagdollBones_LeavesAnUnknownRigAlone) {
    std::vector<SkeletonBone> bones(3U);
    bones[0].name = "bone_01";
    bones[1].name = "prop_02";
    bones[2].name = "socket_03";
    EXPECT_TRUE(BuildHumanoidRagdollBones(bones).empty())
        << "guessing a body for a bone the table does not know is worse than not simulating";
}

// ---- Authorship validation --------------------------------------------------------------------

TEST_F(Ragdoll3DTest, IsRagdoll3DComponentValid_RejectsBadAuthorship) {
    Ragdoll3DComponent ragdoll;
    EXPECT_FALSE(IsRagdoll3DComponentValid(ragdoll)) << "no skeleton";

    const Entity authoredSkeleton = MakeSkeletonEntity(MakeHumanoidSkeleton());
    ragdoll.skeleton = authoredSkeleton;
    EXPECT_TRUE(IsRagdoll3DComponentValid(ragdoll));

    RagdollBoneSetup bone;
    bone.boneName = "pelvis";
    ragdoll.bones = {bone};
    EXPECT_TRUE(IsRagdoll3DComponentValid(ragdoll));

    ragdoll.bones[0].boneName.clear();
    EXPECT_FALSE(IsRagdoll3DComponentValid(ragdoll)) << "a nameless bone resolves to nothing";
    ragdoll.bones[0].boneName = "pelvis";

    ragdoll.bones[0].mass = 0.0F;
    EXPECT_FALSE(IsRagdoll3DComponentValid(ragdoll));
    ragdoll.bones[0].mass = 1.0F;

    ragdoll.bones[0].friction = 1.5F;
    EXPECT_FALSE(IsRagdoll3DComponentValid(ragdoll));
    ragdoll.bones[0].friction = 0.8F;

    ragdoll.bones[0].twistAsymmetric = true;
    ragdoll.bones[0].twistLowerLimitDegrees = 40.0F;
    ragdoll.bones[0].twistUpperLimitDegrees = -40.0F;
    EXPECT_FALSE(IsRagdoll3DComponentValid(ragdoll)) << "a backwards range is a typo, not a limit";

    ragdoll.gravity = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    EXPECT_FALSE(IsRagdoll3DComponentValid(ragdoll));
}

TEST_F(Ragdoll3DTest, Ragdoll3DComponent_EqualityIgnoresRuntimeState) {
    Ragdoll3DComponent authored;
    authored.skeleton = Entity{4U, 1U};
    RagdollBoneSetup bone;
    bone.boneName = "pelvis";
    bone.mass = 12.0F;
    authored.bones = {bone};

    Ragdoll3DComponent simulated = authored;
    simulated.initialized = true;
    simulated.pendingSeconds = 0.25F;
    simulated.bindings.resize(3U);
    EXPECT_TRUE(authored == simulated) << "a running ragdoll is not a different ragdoll";

    simulated.bones[0].mass = 13.0F;
    EXPECT_FALSE(authored == simulated);
}

// ---- Initialisation from the rig --------------------------------------------------------------

TEST_F(Ragdoll3DTest, InitializeRagdoll3D_BuildsOneBodyPerSimulatedBoneAndOneJointBetween) {
    const Entity ragdollEntity = MakeHumanoidRagdoll();
    ASSERT_TRUE(InitializeRagdoll3D(entityManager, ragdollEntity));

    const Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity);
    EXPECT_TRUE(ragdoll.initialized);
    ASSERT_EQ(ragdoll.solver.GetBodyCount(), 6U);
    EXPECT_EQ(ragdoll.solver.GetJointCount(), 5U) << "a tree of six has five joints";
    EXPECT_EQ(ragdoll.bindings.size(), 6U);
    EXPECT_NEAR(ragdoll.solver.GetTotalMass(), 12.0F + 6.0F + 5.0F + 9.0F + 3.5F + 1.0F, 1.0e-3F);

    // The root of a ragdoll is not jointed to anything; everything else is.
    std::size_t roots = 0U;
    for (const RagdollBoneBinding& binding : ragdoll.bindings) {
        EXPECT_LT(binding.boneIndex, 6U);
        EXPECT_NE(binding.boneIndex, kInvalidSkeletonBoneIndex);
        if (binding.parentBoneIndex == kInvalidSkeletonBoneIndex) ++roots;
    }
    EXPECT_EQ(roots, 1U);
}

TEST_F(Ragdoll3DTest, InitializeRagdoll3D_RefusesWhatItCannotSimulate) {
    // No such entity.
    EXPECT_FALSE(InitializeRagdoll3D(entityManager, Entity{1234U, 1U}));

    // A ragdoll whose skeleton has no bones.
    Skeleton3DComponent empty;
    const Entity emptySkeleton = MakeSkeletonEntity(empty);
    EXPECT_FALSE(InitializeRagdoll3D(entityManager, MakeRagdoll(emptySkeleton)));

    // A skeleton whose bones the humanoid table does not know, with no explicit bones authored.
    Skeleton3DComponent unknown;
    SkeletonBone bone;
    bone.name = "bone_01";
    unknown.bones = {bone};
    const Entity unknownSkeleton = MakeSkeletonEntity(unknown);
    const Entity unknownRagdoll = MakeRagdoll(unknownSkeleton);
    EXPECT_FALSE(InitializeRagdoll3D(entityManager, unknownRagdoll));
    EXPECT_FALSE(entityManager.GetComponent<Ragdoll3DComponent>(unknownRagdoll).initialized);
}

// ---- The update pass --------------------------------------------------------------------------

TEST_F(Ragdoll3DTest, UpdateRagdoll3DObjects_RefusesNonPositiveTime) {
    const Entity ragdoll = MakeHumanoidRagdoll();
    EXPECT_EQ(UpdateRagdoll3DObjects(entityManager, 0.0F), 0U);
    EXPECT_EQ(UpdateRagdoll3DObjects(entityManager, -0.5F), 0U);
    EXPECT_EQ(UpdateRagdoll3DObjects(entityManager, std::numeric_limits<float>::quiet_NaN()), 0U);
    EXPECT_FALSE(entityManager.GetComponent<Ragdoll3DComponent>(ragdoll).initialized)
        << "a refused call must not initialise anything";
}

TEST_F(Ragdoll3DTest, UpdateRagdoll3DObjects_StepsTheBodyOnAFixedClock) {
    const Entity ragdollEntity = MakeHumanoidRagdoll();
    ASSERT_TRUE(InitializeRagdoll3D(entityManager, ragdollEntity));
    const float startHeight =
        entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity).solver.GetBody(0).position.y;

    // One 1/60 s frame is four fixed steps at 1/240 s, and the accumulator keeps the remainder.
    EXPECT_EQ(UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F), 1U);
    Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity);
    EXPECT_TRUE(ragdoll.lastReport.stepped);
    EXPECT_NEAR(ragdoll.pendingSeconds, 0.0F, 1.0e-6F);
    EXPECT_LT(ragdoll.solver.GetBody(0).position.y, startHeight);

    // A frame too short for a whole step does not advance the simulation at all; the fraction stays
    // in the accumulator for the next frame.
    const float stepSeconds = Physics::kRagdollFixedStepSeconds;
    EXPECT_EQ(UpdateRagdoll3DObjects(entityManager, stepSeconds * 0.25F), 0U);
    EXPECT_NEAR(ragdoll.pendingSeconds, stepSeconds * 0.25F, 1.0e-6F);

    // A 100 ms hitch would be 24 steps; the cap is 8, so the accumulator is clamped instead of
    // replaying the whole frame and the simulation slips against wall-clock for one frame.
    EXPECT_EQ(UpdateRagdoll3DObjects(entityManager, 0.1F), 1U);
    EXPECT_LE(ragdoll.pendingSeconds,
              stepSeconds * static_cast<float>(Physics::kMaximumRagdollSubsteps) + 1.0e-6F);
}

TEST_F(Ragdoll3DTest, UpdateRagdoll3DObjects_WritesTheSimulationBackIntoThePose) {
    const Entity ragdollEntity = MakeHumanoidRagdoll();
    const Entity skeletonEntity = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity).skeleton;
    ASSERT_TRUE(InitializeRagdoll3D(entityManager, ragdollEntity));
    Skeleton3DComponent& skeleton = entityManager.GetComponent<Skeleton3DComponent>(skeletonEntity);
    ASSERT_EQ(skeleton.pose.size(), skeleton.bones.size());

    // A sideways blast on the pelvis: the rig must end up rotated, not just translated.
    const Math::Vector3 pelvis = skeleton.pose[0].position + Math::Vector3{0.0F, 1.0F, 0.0F};
    ASSERT_TRUE(ApplyRagdollImpact(entityManager, ragdollEntity, pelvis,
                                      Math::Vector3{30.0F, 0.0F, 0.0F}));
    for (int frame = 0; frame < 30; ++frame) {
        UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F);
    }

    float maximumRotationDeviation = 0.0F;
    for (const SkeletonBonePose& pose : skeleton.pose) {
        maximumRotationDeviation =
            std::max(maximumRotationDeviation, std::fabs(1.0F - std::fabs(pose.rotation.w)));
    }
    EXPECT_GT(maximumRotationDeviation, 1.0e-3F) << "the mesh follows the ragdoll";
    EXPECT_EQ(skeleton.pose.size(), skeleton.bones.size()) << "the pose is written in place";
}

TEST_F(Ragdoll3DTest, UpdateRagdoll3DObjects_StopsAFallingBodyOnAMeshCollider) {
    const Entity ragdollEntity = MakeHumanoidRagdoll();
    ASSERT_TRUE(InitializeRagdoll3D(entityManager, ragdollEntity));

    // A wide slab whose top face sits at y = 0.5, six-tenths of a metre below the lowest limb.
    constexpr float kFloorTop = 0.5F;
    static_cast<void>(MakeBoxCollider({0.0F, kFloorTop - 0.5F, 0.0F}, {20.0F, 0.5F, 20.0F}));

    Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity);
    bool sawContact = false;
    for (int frame = 0; frame < 240; ++frame) {
        UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F);
        if (ragdoll.lastReport.resolvedContactCount > 0U) sawContact = true;
    }
    EXPECT_TRUE(sawContact) << "the body never met the collider";

    // Lowest SURFACE, not lowest centre minus the capsule's half length: by the time it settles the
    // body is lying on its side, and the algebra has to follow the capsule axis where it points.
    float lowestSurface = std::numeric_limits<float>::infinity();
    for (std::size_t body = 0U; body < ragdoll.solver.GetBodyCount(); ++body) {
        const Physics::RagdollBody& value = ragdoll.solver.GetBody(body);
        Math::Vector3 start{};
        Math::Vector3 end{};
        Physics::ComputeRagdollCapsuleWorldSegment(value, start, end);
        lowestSurface = std::min(lowestSurface, std::min(start.y, end.y) - value.radius);
    }
    // The capsule rests on the slab: it does not pass through it, and it does not stop a metre
    // above it either - either of those is what "no tunneling" is actually worth.
    EXPECT_GT(lowestSurface, kFloorTop - 0.05F);
    EXPECT_LT(lowestSurface, kFloorTop + 0.25F);
}

// ---- Impacts ----------------------------------------------------------------------------------

TEST_F(Ragdoll3DTest, ApplyRagdollImpact_WakesTheBodyAndPushesTheNearestOne) {
    const Entity ragdollEntity = MakeHumanoidRagdoll();
    ASSERT_TRUE(InitializeRagdoll3D(entityManager, ragdollEntity));
    Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity);
    ragdoll.solver.Sleep();
    ASSERT_TRUE(ragdoll.solver.IsSleeping());

    // Aim at the head, which is the third body in hierarchy order.
    const Physics::RagdollBody& head = ragdoll.solver.GetBody(2U);
    const Math::Vector3 hitPoint = head.position + Math::Vector3{0.0F, 0.05F, 0.0F};
    ASSERT_TRUE(ApplyRagdollImpact(entityManager, ragdollEntity, hitPoint,
                                      Math::Vector3{0.0F, 0.0F, -40.0F}));
    EXPECT_FALSE(ragdoll.solver.IsSleeping());
    EXPECT_LT(ragdoll.solver.GetBody(2U).linearVelocity.z, -0.1F);
    EXPECT_NEAR(ragdoll.solver.GetBody(0U).linearVelocity.z, 0.0F, 1.0e-6F)
        << "a hit on the head is not a hit on the pelvis";
}

TEST_F(Ragdoll3DTest, ApplyRagdollImpact_RefusesAHitThatTouchesNothing) {
    const Entity ragdollEntity = MakeHumanoidRagdoll();
    ASSERT_TRUE(InitializeRagdoll3D(entityManager, ragdollEntity));
    Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity);

    EXPECT_FALSE(ApplyRagdollImpact(entityManager, ragdollEntity, Math::Vector3{50.0F, 50.0F, 50.0F},
                                       Math::Vector3{1.0F, 0.0F, 0.0F}))
        << "a blast 80 m away must not move a corpse";
    EXPECT_FALSE(ApplyRagdollImpact(entityManager, ragdollEntity, Math::Vector3{0.0F, 1.0F, 0.0F},
                                       Math::Vector3{1.0F, 0.0F, 0.0F}, -1.0F))
        << "a negative search radius is a caller bug";
    for (std::size_t body = 0U; body < ragdoll.solver.GetBodyCount(); ++body) {
        EXPECT_FLOAT_EQ(ragdoll.solver.GetBody(body).linearVelocity.x, 0.0F);
    }
}

// ---- AAA Overhaul Regression & Capability Tests -----------------------------------------------

TEST_F(Ragdoll3DTest, RootPositionWriteBack_DropsRenderedPelvisToFloorInsteadOfFloating) {
    // Regression test for Bug #1 (Floating Corpse): previously only rotation was written back,
    // leaving `skeleton.pose[0].position` frozen in mid-air while physics fell to the ground!
    const Entity ragdollEntity = MakeHumanoidRagdoll();
    const Entity skeletonEntity = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity).skeleton;
    ASSERT_TRUE(InitializeRagdoll3D(entityManager, ragdollEntity));

    constexpr float kFloorTop = 0.0F;
    static_cast<void>(MakeBoxCollider({0.0F, kFloorTop - 0.5F, 0.0F}, {20.0F, 0.5F, 20.0F}));

    const float initialPoseY = entityManager.GetComponent<Skeleton3DComponent>(skeletonEntity).pose[0].position.y;
    for (int frame = 0; frame < 120; ++frame) {
        UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F);
    }
    const float finalPoseY = entityManager.GetComponent<Skeleton3DComponent>(skeletonEntity).pose[0].position.y;
    EXPECT_LT(finalPoseY, initialPoseY - 0.35F)
        << "rendered root/pelvis pose must fall toward the floor instead of floating at bind height";
    EXPECT_GT(finalPoseY, kFloorTop - 0.1F);
}

TEST_F(Ragdoll3DTest, SmartSleep_RestingCorpseSleepsAndStaysAsleepOnStaticFloorUntilHit) {
    // Regression test for Bug #3: resting floor contacts previously woke the solver every frame forever!
    const Entity ragdollEntity = MakeHumanoidRagdoll();
    Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity);
    ragdoll.stepSettings.sleepDelaySeconds = 0.2F;
    ragdoll.stepSettings.sleepLinearSpeed = 0.15F;
    ragdoll.stepSettings.sleepAngularSpeed = 0.4F;
    ASSERT_TRUE(InitializeRagdoll3D(entityManager, ragdollEntity));

    static_cast<void>(MakeBoxCollider({0.0F, -0.5F, 0.0F}, {20.0F, 0.5F, 20.0F}));

    bool wentToSleep = false;
    for (int frame = 0; frame < 360; ++frame) {
        UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F);
        if (ragdoll.solver.IsSleeping()) {
            wentToSleep = true;
            break;
        }
    }
    ASSERT_TRUE(wentToSleep) << "settled ragdoll on a static floor must go to sleep";

    // Must STAY asleep across subsequent frames despite resting floor contacts!
    for (int frame = 0; frame < 60; ++frame) {
        EXPECT_EQ(UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F), 0U);
        EXPECT_TRUE(ragdoll.solver.IsSleeping());
    }

    // External hit wakes it immediately.
    const Math::Vector3 pelvisPos = ragdoll.solver.GetBody(0U).position;
    ASSERT_TRUE(ApplyRagdollImpact(entityManager, ragdollEntity, pelvisPos, {25.0F, 5.0F, 0.0F}, 1.0F));
    EXPECT_FALSE(ragdoll.solver.IsSleeping());
}

TEST_F(Ragdoll3DTest, DynamicRigid3DPushback_TransfersMomentumToDynamicCrate) {
    const Entity ragdollEntity = MakeHumanoidRagdoll();
    ASSERT_TRUE(InitializeRagdoll3D(entityManager, ragdollEntity));

    // Dynamic Rigid3D crate directly beneath the falling ragdoll.
    const Entity crate = MakeBoxCollider({0.0F, 0.0F, 0.0F}, {1.0F, 0.5F, 1.0F});
    Rigid3DComponent rigid{};
    rigid.mass = 10.0F;
    rigid.isKinematic = false;
    entityManager.AddComponent<Rigid3DComponent>(crate, rigid);

    for (int frame = 0; frame < 60; ++frame) {
        UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F);
    }
    const Rigid3DComponent& after = entityManager.GetComponent<Rigid3DComponent>(crate);
    EXPECT_LT(after.velocity.y, -0.05F) << "falling ragdoll must impart downward momentum to a dynamic Rigid3D";
}

TEST_F(Ragdoll3DTest, MixamoTPoseRig_BuildsMultiChildPelvisUpwardAndOrthogonalElbowHinges) {
    // Regression test for Bug #5: Mixamo names (`Hips`, `LeftUpLeg`, `LeftLeg`, `LeftArm`, `LeftForeArm`),
    // with thighs listed BEFORE spine, and arms stretched laterally along +/-X (T-pose).
    Skeleton3DComponent mixamo;
    mixamo.skeletonAssetPath = "MixamoTPose.glb";
    auto addBone = [&](const char* name, std::int32_t parent, Math::Vector3 pos) {
        SkeletonBone b;
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

    const std::vector<RagdollBoneSetup> built = BuildHumanoidRagdollBones(mixamo.bones);
    EXPECT_EQ(built.size(), 10U) << "all 10 Mixamo bones should match the expanded profile";

    const Entity skelEnt = MakeSkeletonEntity(mixamo);
    const Entity ragEnt = MakeRagdoll(skelEnt);
    ASSERT_TRUE(InitializeRagdoll3D(entityManager, ragEnt));

    const Ragdoll3DComponent& rag = entityManager.GetComponent<Ragdoll3DComponent>(ragEnt);
    // Pelvis capsule axis must point UP toward Spine (+Y), NOT sideways down LeftUpLeg!
    const Math::Vector3 pelvisAxis =
        Math::RotateVector(rag.solver.GetBody(0U).rotation, {0.0F, 1.0F, 0.0F});
    EXPECT_GT(pelvisAxis.y, 0.9F);

    // Find LeftForeArm elbow joint: its twist axis MUST be orthogonal to the arm (+X), NOT parallel to +X!
    bool checkedLeftElbow = false;
    for (std::size_t j = 0U; j < rag.solver.GetJointCount(); ++j) {
        const Physics::RagdollJoint& joint = rag.solver.GetJoint(j);
        for (const RagdollBoneBinding& b : rag.bindings) {
            if (b.bodyIndex == joint.secondBody && mixamo.bones[b.boneIndex].name == "mixamorig:LeftForeArm") {
                const Physics::RagdollBody& parentBody = rag.solver.GetBody(joint.firstBody);
                const Math::Quaternion worldFrame = Math::Multiply(parentBody.rotation, joint.firstFrame);
                const Math::Vector3 hingeWorldAxis = Math::RotateVector(worldFrame, {1.0F, 0.0F, 0.0F});
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

TEST_F(Ragdoll3DTest, ActiveModePartialSubtreeVelocityInheritanceAndGetUpBlendOut) {
    const Entity ragdollEntity = MakeHumanoidRagdoll();
    const Entity skeletonEntity = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity).skeleton;
    Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity);
    ragdoll.mode = RagdollMode::Kinematic;
    ASSERT_TRUE(InitializeRagdoll3D(entityManager, ragdollEntity));

    // Simulate 2 frames of rightward animated motion in Kinematic mode -> velocity inheritance!
    Skeleton3DComponent& skel = entityManager.GetComponent<Skeleton3DComponent>(skeletonEntity);
    skel.pose[0].position = {0.0F, 1.0F, 0.0F};
    UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F);
    skel.pose[0].position = {0.1F, 1.0F, 0.0F}; // +6 m/s rightward
    UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F);

    ASSERT_TRUE(ActivateRagdoll(entityManager, ragdollEntity, RagdollMode::Simulating));
    EXPECT_GT(ragdoll.solver.GetBody(0U).linearVelocity.x, 3.0F)
        << "activating ragdoll from Kinematic mode must inherit animated linear velocity";

    // Test partial subtree mode: simulate ONLY spine_01+head while pelvis+leg stay kinematic!
    ragdoll.partialRootBoneName = "spine_01";
    ragdoll.mode = RagdollMode::Active;
    ragdoll.motorStiffness = 200.0F;
    ragdoll.motorDamping = 24.0F;
    ragdoll.maxMotorTorque = 200.0F;
    UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F);
    EXPECT_TRUE(ragdoll.solver.GetBody(0U).kinematic) << "pelvis outside partial subtree must be kinematic";
    EXPECT_FALSE(ragdoll.solver.GetBody(1U).kinematic) << "spine_01 inside partial subtree must simulate";

    // Test Prone/Supine Get-Up state classification & Pose Snapshot smooth BlendingOut!
    ragdoll.partialRootBoneName.clear();
    ragdoll.mode = RagdollMode::Simulating;
    Math::Quaternion faceUpRot{};
    ASSERT_TRUE(Math::TryMakeAxisAngle({1.0F, 0.0F, 0.0F}, -1.5707963F, faceUpRot));
    ragdoll.solver.GetBody(0U).rotation = faceUpRot; // Local +Z now points +Y (face up!)
    RagdollGetUpState getUp{};
    ASSERT_TRUE(EvaluateRagdollGetUpState(entityManager, ragdollEntity, getUp));
    EXPECT_TRUE(getUp.valid);
    EXPECT_EQ(getUp.orientation, RagdollLyingOrientation::FaceUp);

    ASSERT_TRUE(BeginBlendOutFromRagdoll(entityManager, ragdollEntity, 0.2F));
    EXPECT_EQ(ragdoll.mode, RagdollMode::BlendingOut);
    for (int frame = 0; frame < 20; ++frame) {
        UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F);
    }
    EXPECT_EQ(ragdoll.mode, RagdollMode::Kinematic) << "completed BlendingOut transitions cleanly to Kinematic";

    // Test Editor Live Preview backup & exact pose restoration!
    skel.pose[0].position = {0.0F, 1.0F, 0.0F};
    ASSERT_TRUE(StartRagdollEditorPreview(entityManager, ragdollEntity));
    EXPECT_TRUE(ragdoll.editorPreviewSimulating);
    for (int frame = 0; frame < 30; ++frame) {
        UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F, /*editorPreviewOnly=*/true);
    }
    EXPECT_LT(skel.pose[0].position.y, 0.9F);
    ASSERT_TRUE(StopRagdollEditorPreview(entityManager, ragdollEntity));
    EXPECT_FALSE(ragdoll.editorPreviewSimulating);
    EXPECT_NEAR(skel.pose[0].position.y, 1.0F, 1.0e-4F) << "stopping Editor Preview must restore exact bind pose";

    // Test MobileFast preset scales down cost for low-end Android phones.
    ApplyRagdollQualityPreset(ragdoll, RagdollQualityPreset::MobileFast);
    EXPECT_FALSE(ragdoll.enableSelfCollision);
    EXPECT_FALSE(ragdoll.enableDynamicPushback);
    EXPECT_EQ(ragdoll.maxSubstepsPerFrame, 2U);
}

TEST_F(Ragdoll3DTest, PhysicalBone3D_DelegatesToAAASolverAndStopsOnFloorCollider) {
    static_cast<void>(MakeBoxCollider({0.0F, -0.5F, 0.0F}, {20.0F, 0.5F, 20.0F}));
    const Entity pbEntity = entityManager.CreateEntity();
    PhysicalBone3DObjectDefinition def{};
    def.physicalBone.mass = 3.0F;
    def.physicalBone.simulatePhysics = true;
    ApplyPhysicalBone3DObjectDefinition(entityManager, pbEntity, def);
    entityManager.GetComponent<TransformComponent>(pbEntity).localPosition = {0.0F, 0.6F, 0.0F};

    for (int frame = 0; frame < 90; ++frame) {
        EXPECT_EQ(UpdatePhysicalBone3DObjects(entityManager, {0.0F, -9.81F, 0.0F}, 1.0F / 60.0F), 1U);
    }
    const float finalY = entityManager.GetComponent<TransformComponent>(pbEntity).localPosition.y;
    EXPECT_GT(finalY, 0.05F) << "upgraded PhysicalBone3D must rest on floor collider instead of tunneling forever";
    EXPECT_LT(finalY, 0.35F);
}

} // namespace
} // namespace UVE::Scene::Tests
