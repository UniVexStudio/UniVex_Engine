// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "blueprint_object_host.h"
#include "blueprint_value.h"
#include "character_controller_component.h"
#include "collider_component.h"
#include "hierarchy_component.h"
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

    /// A ragdoll that is already falling: a new component stands (Kinematic) until something activates it.
    [[nodiscard]] static Ragdoll3DComponent SimulatingSetup() {
        Ragdoll3DComponent setup;
        setup.mode = RagdollMode::Simulating;
        return setup;
    }

    [[nodiscard]] Entity MakeRagdoll(const Entity skeleton, Ragdoll3DComponent ragdoll = SimulatingSetup()) {
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
    // In the solver's twist sign the knee folds toward the upper limit; the lower limit is the
    // hyperextension side, which stays within a few degrees (see KneesAndElbowsBendTheAnatomicalWay...).
    EXPECT_GT(shin->twistLowerLimitDegrees, -10.0F) << "a knee does not hyperextend";
    EXPECT_GT(shin->twistUpperLimitDegrees, 90.0F) << "and it folds a long way the other way";
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

// ---- Overhaul Regression & Capability Tests -----------------------------------------------

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

    // Test the Fast preset scales down cost for low-end hardware.
    ApplyRagdollQualityPreset(ragdoll, RagdollQualityPreset::Fast);
    EXPECT_FALSE(ragdoll.enableSelfCollision);
    EXPECT_FALSE(ragdoll.enableDynamicPushback);
    EXPECT_EQ(ragdoll.maxSubstepsPerFrame, 2U);
}

TEST_F(Ragdoll3DTest, PhysicalBone3D_DelegatesToTheRagdollSolverAndStopsOnFloorCollider) {
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

TEST_F(Ragdoll3DTest, BuildGenericRagdollBones_GeneratesBodiesForNonHumanoidRigs) {
    Skeleton3DComponent skeleton;
    auto addBone = [&](const char* name, const std::int32_t parent, const Math::Vector3 pos) {
        SkeletonBone bone{};
        bone.name = name;
        bone.parentIndex = parent;
        bone.localPosition = pos;
        skeleton.bones.push_back(bone);
    };
    addBone("Bone", -1, {0.0F, 1.0F, 0.0F});
    addBone("Bone.001", 0, {0.0F, 0.4F, 0.0F});
    addBone("Bone.002", 1, {0.0F, 0.4F, 0.0F});
    addBone("Bone.003", 2, {0.3F, 0.0F, 0.0F});
    // Helper / finger / end-nub bones should be filtered out automatically.
    addBone("IK_Foot", 0, {0.0F, -1.0F, 0.0F});
    addBone("Finger_01", 3, {0.1F, 0.0F, 0.0F});
    addBone("Bone.003_end", 3, {0.1F, 0.0F, 0.0F});

    EXPECT_TRUE(BuildHumanoidRagdollBones(skeleton.bones).empty());
    const std::vector<RagdollBoneSetup> genericBones = BuildGenericRagdollBones(skeleton.bones);
    ASSERT_EQ(genericBones.size(), 4U);

    const Entity skelEntity = MakeSkeletonEntity(skeleton);
    const Entity ragdollEntity = MakeRagdoll(skelEntity);
    entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity).bones = genericBones;
    ASSERT_TRUE(InitializeRagdoll3D(entityManager, ragdollEntity));

    const Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity);
    EXPECT_EQ(ragdoll.solver.GetBodyCount(), 4U);
    EXPECT_EQ(ragdoll.solver.GetJointCount(), 3U);
}

TEST_F(Ragdoll3DTest, UpdateRagdoll3DObjects_StopsFallingRagdollOnVirtualFloorPlane) {
    // No floor ColliderComponent exists in the scene — mirrors Entity Editor simulation!
    const Entity ragdollEntity = MakeHumanoidRagdoll();
    const Entity skeletonEntity = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity).skeleton;
    ASSERT_TRUE(InitializeRagdoll3D(entityManager, ragdollEntity));

    const float initialPoseY = entityManager.GetComponent<Skeleton3DComponent>(skeletonEntity).pose[0].position.y;
    Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity);
    bool sawContact = false;
    for (int frame = 0; frame < 180; ++frame) {
        UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F, /*editorPreviewOnly=*/false,
                               /*fallbackFloorPlaneY=*/0.0F);
        if (ragdoll.lastReport.resolvedContactCount > 0U) {
            sawContact = true;
        }
    }
    EXPECT_TRUE(sawContact) << "virtual floor plane at y = 0 must generate contacts with zero floor colliders";

    float lowestSurface = std::numeric_limits<float>::infinity();
    for (std::size_t body = 0U; body < ragdoll.solver.GetBodyCount(); ++body) {
        const Physics::RagdollBody& value = ragdoll.solver.GetBody(body);
        Math::Vector3 start{};
        Math::Vector3 end{};
        Physics::ComputeRagdollCapsuleWorldSegment(value, start, end);
        lowestSurface = std::min(lowestSurface, std::min(start.y, end.y) - value.radius);
    }
    EXPECT_GT(lowestSurface, -0.05F);
    EXPECT_LT(lowestSurface, 0.25F);
    EXPECT_LT(entityManager.GetComponent<Skeleton3DComponent>(skeletonEntity).pose[0].position.y, initialPoseY - 0.35F);
}

TEST_F(Ragdoll3DTest, UpdateRagdoll3DObjects_IgnoresOwningAncestorCharacterCollider) {
    // Place the Skeleton3D under a character root that owns an enclosing ColliderComponent.
    const Entity characterRoot = MakeBoxCollider({0.0F, 1.0F, 0.0F}, {1.0F, 1.5F, 1.0F});
    entityManager.AddComponent<HierarchyComponent>(characterRoot, HierarchyComponent{});

    const Entity ragdollEntity = MakeHumanoidRagdoll();
    const Entity skeletonEntity = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity).skeleton;
    entityManager.AddComponent<HierarchyComponent>(skeletonEntity, HierarchyComponent{});
    entityManager.GetComponent<HierarchyComponent>(skeletonEntity).parent = characterRoot;

    ASSERT_TRUE(InitializeRagdoll3D(entityManager, ragdollEntity));
    Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity);
    for (int frame = 0; frame < 120; ++frame) {
        UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F, /*editorPreviewOnly=*/false,
                               /*fallbackFloorPlaneY=*/0.0F);
    }
    const Math::Vector3 pelvisPos = ragdoll.solver.GetBody(0U).position;
    EXPECT_TRUE(std::isfinite(pelvisPos.x) && std::isfinite(pelvisPos.y) && std::isfinite(pelvisPos.z));
    EXPECT_LT(pelvisPos.y, 0.70F) << "ragdoll must fall freely instead of being trapped inside owning character collider";
    EXPECT_GT(pelvisPos.y, -0.05F);
}

// ---- Full production rigs ---------------------------------------------------------------------

/// The standard 65-bone Mixamo export: spine chain, neck, head + end, shoulders, arms, hands with
/// four bones per finger (and "_End" leaves), legs, feet and toes. Laid out as a rough T-pose so
/// every bone has length.
[[nodiscard]] Skeleton3DComponent MakeFullMixamoSkeleton() {
    Skeleton3DComponent skeleton;
    skeleton.skeletonAssetPath = "MixamoFull.fbx";
    const auto add = [&skeleton](const std::string& name, const std::int32_t parent, const Math::Vector3 position) {
        SkeletonBone bone;
        bone.name = "mixamorig:" + name;
        bone.parentIndex = parent;
        bone.localPosition = position;
        skeleton.bones.push_back(bone);
        return static_cast<std::int32_t>(skeleton.bones.size() - 1U);
    };
    const std::int32_t hips = add("Hips", -1, {0.0F, 1.0F, 0.0F});
    const std::int32_t spine = add("Spine", hips, {0.0F, 0.1F, 0.0F});
    const std::int32_t spine1 = add("Spine1", spine, {0.0F, 0.12F, 0.0F});
    const std::int32_t spine2 = add("Spine2", spine1, {0.0F, 0.13F, 0.0F});
    const std::int32_t neck = add("Neck", spine2, {0.0F, 0.15F, 0.0F});
    const std::int32_t head = add("Head", neck, {0.0F, 0.1F, 0.0F});
    add("HeadTop_End", head, {0.0F, 0.18F, 0.0F});
    for (const float side : {1.0F, -1.0F}) {
        const std::string s = side > 0.0F ? "Left" : "Right";
        const std::int32_t shoulder = add(s + "Shoulder", spine2, {side * 0.06F, 0.12F, 0.0F});
        const std::int32_t arm = add(s + "Arm", shoulder, {side * 0.12F, 0.0F, 0.0F});
        const std::int32_t foreArm = add(s + "ForeArm", arm, {side * 0.27F, 0.0F, 0.0F});
        const std::int32_t hand = add(s + "Hand", foreArm, {side * 0.26F, 0.0F, 0.0F});
        for (const char* finger : {"Thumb", "Index", "Middle", "Ring", "Pinky"}) {
            std::int32_t parent = hand;
            for (int joint = 1; joint <= 4; ++joint) {
                const std::string name = s + "Hand" + finger + (joint == 4 ? std::string{"4"} : std::to_string(joint));
                parent = add(name, parent, {side * 0.03F, 0.0F, 0.0F});
            }
        }
        const std::int32_t upLeg = add(s + "UpLeg", hips, {side * 0.09F, -0.05F, 0.0F});
        const std::int32_t leg = add(s + "Leg", upLeg, {0.0F, -0.42F, 0.0F});
        const std::int32_t foot = add(s + "Foot", leg, {0.0F, -0.4F, 0.0F});
        const std::int32_t toe = add(s + "ToeBase", foot, {0.0F, -0.05F, 0.1F});
        add(s + "Toe_End", toe, {0.0F, 0.0F, 0.06F});
    }
    return skeleton;
}

[[nodiscard]] bool ContainsBone(const std::vector<RagdollBoneSetup>& bones, const std::string& name) {
    return std::any_of(bones.begin(), bones.end(),
                       [&name](const RagdollBoneSetup& bone) { return bone.boneName == name; });
}

TEST_F(Ragdoll3DTest, FullMixamoRig_GetsTheWholeBodyIncludingEveryFingerAndToe) {
    const Skeleton3DComponent mixamo = MakeFullMixamoSkeleton();
    ASSERT_EQ(mixamo.bones.size(), 65U);

    const std::vector<RagdollBoneSetup> built = BuildHumanoidRagdollBones(mixamo.bones);
    EXPECT_LE(built.size(), static_cast<std::size_t>(kMaximumRagdollBones));
    // hips + 3 spine + neck + head + 2 clavicles + 2x(arm, forearm, hand) + 2x(thigh, shin, foot, toe)
    // + 2 hands x 5 fingers x 3 phalanges.
    EXPECT_EQ(built.size(), 52U);
    for (const char* body : {"mixamorig:Hips", "mixamorig:Head", "mixamorig:LeftHand", "mixamorig:RightFoot",
                             "mixamorig:LeftToeBase", "mixamorig:RightToeBase"}) {
        EXPECT_TRUE(ContainsBone(built, body)) << body;
    }
    // Every finger of both hands: the three phalanges (the "4" tip is not a body of its own).
    for (const char* side : {"Left", "Right"}) {
        for (const char* finger : {"Thumb", "Index", "Middle", "Ring", "Pinky"}) {
            for (int joint = 1; joint <= 3; ++joint) {
                EXPECT_TRUE(ContainsBone(built, std::string{"mixamorig:"} + side + "Hand" + finger + std::to_string(joint)))
                    << side << finger << joint;
            }
            EXPECT_FALSE(ContainsBone(built, std::string{"mixamorig:"} + side + "Hand" + finger + "4"));
        }
    }
    for (const RagdollBoneSetup& bone : built) {
        EXPECT_EQ(bone.boneName.find("_End"), std::string::npos) << bone.boneName;
    }
    EXPECT_EQ(BuildAutoRagdollBones(mixamo.bones).size(), built.size());
}

TEST_F(Ragdoll3DTest, FingersAreLightDetailBodiesThatNeverCollideWithTheBody) {
    const Skeleton3DComponent mixamo = MakeFullMixamoSkeleton();
    const Entity ragdollEntity = MakeRagdoll(MakeSkeletonEntity(mixamo));
    ASSERT_TRUE(InitializeRagdoll3D(entityManager, ragdollEntity))
        << entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity).lastSetupError;
    const Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity);
    ASSERT_EQ(ragdoll.solver.GetBodyCount(), 52U);

    std::uint32_t finger = Physics::kInvalidRagdollBody;
    std::uint32_t hips = Physics::kInvalidRagdollBody;
    for (const RagdollBoneBinding& binding : ragdoll.bindings) {
        const std::string& name = mixamo.bones[binding.boneIndex].name;
        if (name == "mixamorig:LeftHandIndex2") finger = binding.bodyIndex;
        if (name == "mixamorig:Hips") hips = binding.bodyIndex;
    }
    ASSERT_NE(finger, Physics::kInvalidRagdollBody);
    ASSERT_NE(hips, Physics::kInvalidRagdollBody);
    EXPECT_LT(ragdoll.solver.GetBody(finger).radius, 0.02F) << "a fingertip-sized capsule";
    EXPECT_FALSE(ragdoll.solver.CanSelfCollide(finger, hips));
}

TEST_F(Ragdoll3DTest, OrdinaryWordsInStructuralBoneNamesAreNotMistakenForFingers) {
    // "middle" and "ring" sit inside ordinary names ("spine_middle", "spring_...") - those are body bones.
    Skeleton3DComponent rig;
    const auto add = [&rig](const char* name, const std::int32_t parent, const Math::Vector3 position) {
        SkeletonBone bone;
        bone.name = name;
        bone.parentIndex = parent;
        bone.localPosition = position;
        rig.bones.push_back(bone);
    };
    add("pelvis", -1, {0.0F, 1.0F, 0.0F});
    add("spine_middle", 0, {0.0F, 0.2F, 0.0F});
    add("spine_upper", 1, {0.0F, 0.2F, 0.0F});
    add("head", 2, {0.0F, 0.2F, 0.0F});
    add("LeftHandIndex1", 2, {0.2F, 0.0F, 0.0F});
    add("LeftHand", 2, {0.2F, 0.0F, 0.0F});

    const std::vector<RagdollBoneSetup> built = BuildHumanoidRagdollBones(rig.bones);
    EXPECT_TRUE(ContainsBone(built, "spine_middle")) << "a structural bone must not be skipped as a finger";
    EXPECT_FALSE(ContainsBone(built, "LeftHandIndex1")) << "a real finger still is";
    EXPECT_TRUE(ContainsBone(built, "LeftHand"));
}

/// The world-space hinge axis of the joint that connects the bone named `boneName` to its parent body.
[[nodiscard]] Math::Vector3 HingeWorldAxisOf(const Ragdoll3DComponent& ragdoll, const Skeleton3DComponent& skeleton,
                                             const std::string& boneName) {
    for (std::size_t j = 0U; j < ragdoll.solver.GetJointCount(); ++j) {
        const Physics::RagdollJoint& joint = ragdoll.solver.GetJoint(j);
        for (const RagdollBoneBinding& binding : ragdoll.bindings) {
            if (binding.bodyIndex == joint.secondBody && skeleton.bones[binding.boneIndex].name == boneName) {
                const Physics::RagdollBody& parentBody = ragdoll.solver.GetBody(joint.firstBody);
                const Math::Quaternion worldFrame = Math::Multiply(parentBody.rotation, joint.firstFrame);
                return Math::RotateVector(worldFrame, {1.0F, 0.0F, 0.0F});
            }
        }
    }
    return {};
}

TEST_F(Ragdoll3DTest, HingeAxesAreOrthogonalToTheLimbAndTheFacingDirection) {
    // A rig facing +Z has its left side at +X. Positive twist about the elbow axis must carry the forearm
    // toward the front; the knee range is negative, so its axis must make that fold carry the shin backward.
    // The sign was reversed once (forward computed as `up x left`), which folded every limb the wrong way.
    const Skeleton3DComponent mixamo = MakeFullMixamoSkeleton();
    const Entity skeletonEntity = MakeSkeletonEntity(mixamo);
    const Entity ragdollEntity = MakeRagdoll(skeletonEntity);
    ASSERT_TRUE(InitializeRagdoll3D(entityManager, ragdollEntity));
    const Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity);

    // Left forearm points along +X, so axis = forward(+Z) x (+X) = +Y; the right forearm mirrors it.
    EXPECT_GT(HingeWorldAxisOf(ragdoll, mixamo, "mixamorig:LeftForeArm").y, 0.8F);
    EXPECT_LT(HingeWorldAxisOf(ragdoll, mixamo, "mixamorig:RightForeArm").y, -0.8F);
    // Shins point along -Y, so axis = forward(+Z) x (-Y) = +X on both sides.
    EXPECT_GT(HingeWorldAxisOf(ragdoll, mixamo, "mixamorig:LeftLeg").x, 0.8F);
    EXPECT_GT(HingeWorldAxisOf(ragdoll, mixamo, "mixamorig:RightLeg").x, 0.8F);
}

/// The angle, in degrees, between the long axes of the bodies of two bones: 0 for a straight limb.
[[nodiscard]] float BendDegrees(const Ragdoll3DComponent& ragdoll, const Skeleton3DComponent& skeleton,
                                const std::string& upperBone, const std::string& lowerBone) {
    std::uint32_t upper = Physics::kInvalidRagdollBody;
    std::uint32_t lower = Physics::kInvalidRagdollBody;
    for (const RagdollBoneBinding& binding : ragdoll.bindings) {
        if (skeleton.bones[binding.boneIndex].name == upperBone) upper = binding.bodyIndex;
        if (skeleton.bones[binding.boneIndex].name == lowerBone) lower = binding.bodyIndex;
    }
    if (upper == Physics::kInvalidRagdollBody || lower == Physics::kInvalidRagdollBody) return -1.0F;
    const Math::Vector3 a = Math::RotateVector(ragdoll.solver.GetBody(upper).rotation, {0.0F, 1.0F, 0.0F});
    const Math::Vector3 b = Math::RotateVector(ragdoll.solver.GetBody(lower).rotation, {0.0F, 1.0F, 0.0F});
    const float cosine = std::clamp(Math::Dot(a, b), -1.0F, 1.0F);
    return std::acos(cosine) * 180.0F / 3.14159265F;
}

/// Kicks the body of `kickedBone` along `kickZ` in zero gravity and returns how far `lowerBone` bent
/// away from `upperBone` afterwards. Every body at or above `upperBone` is pinned, so only the joint
/// between the two bones can move.
[[nodiscard]] float KickAndMeasureBend(const float kickZ, const std::string& kickedBone, const std::string& upperBone,
                                       const std::string& lowerBone, EntityManager& entityManager,
                                       const Skeleton3DComponent& rig, Entity skeletonEntity,
                                       const std::vector<std::string>& pinnedBones) {
    Ragdoll3DComponent setup;
    setup.mode = RagdollMode::Simulating;
    setup.skeleton = skeletonEntity;
    setup.gravity = {0.0F, 0.0F, 0.0F};
    const Entity ragdollEntity = entityManager.CreateEntity();
    entityManager.AddComponent<Ragdoll3DComponent>(ragdollEntity, setup);
    if (!InitializeRagdoll3D(entityManager, ragdollEntity)) return -1.0F;
    Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity);
    Math::Vector3 point{};
    for (const RagdollBoneBinding& binding : ragdoll.bindings) {
        const std::string& name = rig.bones[binding.boneIndex].name;
        if (name == kickedBone) point = ragdoll.solver.GetBody(binding.bodyIndex).position;
        if (std::find(pinnedBones.begin(), pinnedBones.end(), name) != pinnedBones.end()) {
            ragdoll.solver.GetBody(binding.bodyIndex).kinematic = true;
        }
    }
    ragdoll.solver.RebuildInertia();
    if (!ApplyRagdollImpact(entityManager, ragdollEntity, point, {0.0F, 0.0F, kickZ}, 0.2F)) return -1.0F;
    float widest = 0.0F;
    for (int frame = 0; frame < 45; ++frame) {
        UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F);
        widest = std::max(widest, BendDegrees(ragdoll, rig, upperBone, lowerBone));
    }
    return widest;
}

TEST_F(Ragdoll3DTest, KneesAndElbowsBendTheAnatomicalWayAndRefuseTheOther) {
    // A rig facing +Z: a shin kicked backward must fold at the knee, kicked forward it must be stopped by
    // the joint; a forearm pushed forward must fold at the elbow, pushed backward it must be stopped.
    // Measured in zero gravity with everything above the joint pinned, so only the joint decides.
    const Skeleton3DComponent mixamo = MakeFullMixamoSkeleton();
    const std::vector<std::string> legPins{"mixamorig:Hips", "mixamorig:LeftUpLeg"};
    const std::vector<std::string> armPins{"mixamorig:Hips", "mixamorig:Spine", "mixamorig:Spine1", "mixamorig:Spine2",
                                           "mixamorig:LeftShoulder", "mixamorig:LeftArm"};
    // A fresh skeleton for every measurement: a ragdoll writes its pose back, so a reused one would start the
    // next kick from the previous kick's bent limb.
    const auto bend = [&](const float kick, const std::string& kicked, const std::string& upper,
                          const std::string& lower, const std::vector<std::string>& pins) {
        return KickAndMeasureBend(kick, kicked, upper, lower, entityManager, mixamo, MakeSkeletonEntity(mixamo), pins);
    };
    const float kneeBack = bend(-30.0F, "mixamorig:LeftLeg", "mixamorig:LeftUpLeg", "mixamorig:LeftLeg", legPins);
    const float kneeFront = bend(30.0F, "mixamorig:LeftLeg", "mixamorig:LeftUpLeg", "mixamorig:LeftLeg", legPins);
    const float elbowFront = bend(4.0F, "mixamorig:LeftForeArm", "mixamorig:LeftArm", "mixamorig:LeftForeArm", armPins);
    const float elbowBack = bend(-4.0F, "mixamorig:LeftForeArm", "mixamorig:LeftArm", "mixamorig:LeftForeArm", armPins);
    EXPECT_GT(kneeBack, 30.0F) << "the knee folds backward";
    EXPECT_LT(kneeFront, 12.0F) << "and is stopped when pushed forward";
    EXPECT_GT(elbowFront, 30.0F) << "the elbow folds forward";
    EXPECT_LT(elbowBack, 12.0F) << "and is stopped when pushed backward";
}

/// Height of the named bone in world space, from the skeleton's current pose.
[[nodiscard]] float BoneWorldHeight(const Skeleton3DComponent& skeleton, const std::string& boneName) {
    for (std::uint32_t bone = 0U; bone < skeleton.bones.size(); ++bone) {
        if (skeleton.bones[bone].name != boneName) continue;
        ObjectWorldFrame frame;
        if (TryResolveSkeletonBoneWorldFrame(skeleton, bone, ObjectWorldFrame{}, frame)) return frame.position.y;
    }
    return -1000.0F;
}

TEST_F(Ragdoll3DTest, GetUp_StandsTheFallenBodyBackIntoItsStartingPose) {
    const Skeleton3DComponent mixamo = MakeFullMixamoSkeleton();
    const Entity skeletonEntity = MakeSkeletonEntity(mixamo);
    const Entity ragdollEntity = MakeRagdoll(skeletonEntity);
    const float startHips = BoneWorldHeight(mixamo, "mixamorig:Hips");
    ASSERT_TRUE(StartRagdollEditorPreview(entityManager, ragdollEntity));
    for (int frame = 0; frame < 150; ++frame) {
        UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F, true, 0.0F);
    }
    const Skeleton3DComponent& posed = entityManager.GetComponent<Skeleton3DComponent>(skeletonEntity);
    EXPECT_LT(BoneWorldHeight(posed, "mixamorig:Hips"), startHips - 0.5F) << "it has fallen";

    ASSERT_TRUE(BeginRagdollRecovery(entityManager, ragdollEntity));
    EXPECT_EQ(entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity).mode, RagdollMode::BlendingOut);
    // Halfway through it is neither lying down nor standing.
    for (int frame = 0; frame < 48; ++frame) {
        UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F, true, 0.0F);
    }
    const float midway = BoneWorldHeight(posed, "mixamorig:Hips");
    EXPECT_GT(midway, 0.2F);
    EXPECT_LT(midway, startHips - 0.1F);
    for (int frame = 0; frame < 120; ++frame) {
        UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F, true, 0.0F);
    }
    const Ragdoll3DComponent& done = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity);
    EXPECT_NE(done.mode, RagdollMode::BlendingOut);
    EXPECT_FALSE(done.editorPreviewSimulating) << "the preview ends standing, so Simulate can be pressed again";
    EXPECT_NEAR(BoneWorldHeight(posed, "mixamorig:Hips"), startHips, 0.03F);
    EXPECT_NEAR(BoneWorldHeight(posed, "mixamorig:Head"), BoneWorldHeight(mixamo, "mixamorig:Head"), 0.05F);
    EXPECT_NEAR(BoneWorldHeight(posed, "mixamorig:LeftFoot"), BoneWorldHeight(mixamo, "mixamorig:LeftFoot"), 0.05F);
}

TEST_F(Ragdoll3DTest, GetUp_WithoutAFallIsRefused) {
    const Entity ragdollEntity = MakeRagdoll(MakeSkeletonEntity(MakeFullMixamoSkeleton()));
    EXPECT_FALSE(BeginRagdollRecovery(entityManager, ragdollEntity)) << "nothing is simulating";
    EXPECT_FALSE(IsRagdollActive(entityManager, ragdollEntity));
}

TEST_F(Ragdoll3DTest, AutoRecover_StandsUpOnItsOwnAfterTheBodyComesToRest) {
    const Skeleton3DComponent mixamo = MakeFullMixamoSkeleton();
    const Entity skeletonEntity = MakeSkeletonEntity(mixamo);
    Ragdoll3DComponent setup;
    setup.autoRecover = true;
    setup.recoverDelaySeconds = 0.5F;
    setup.recoverDurationSeconds = 1.0F;
    const Entity ragdollEntity = MakeRagdoll(skeletonEntity, setup);
    const float startHips = BoneWorldHeight(mixamo, "mixamorig:Hips");
    ASSERT_TRUE(StartRagdollEditorPreview(entityManager, ragdollEntity));

    bool fell = false;
    bool recovered = false;
    for (int frame = 0; frame < 60 * 20 && !recovered; ++frame) {
        UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F, true, 0.0F);
        const Skeleton3DComponent& posed = entityManager.GetComponent<Skeleton3DComponent>(skeletonEntity);
        if (BoneWorldHeight(posed, "mixamorig:Hips") < startHips - 0.5F) fell = true;
        recovered = fell && !entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity).editorPreviewSimulating;
    }
    EXPECT_TRUE(fell);
    EXPECT_TRUE(recovered) << "an auto-recovering body stands back up once it has rested";
    EXPECT_NEAR(BoneWorldHeight(entityManager.GetComponent<Skeleton3DComponent>(skeletonEntity), "mixamorig:Hips"),
                startHips, 0.03F);
}

TEST_F(Ragdoll3DTest, RecoverySettings_AreValidated) {
    Ragdoll3DComponent bad;
    bad.recoverDurationSeconds = 0.0F;
    EXPECT_FALSE(IsRagdoll3DComponentAuthoredValid(bad));
    bad = Ragdoll3DComponent{};
    bad.recoverDelaySeconds = -1.0F;
    EXPECT_FALSE(IsRagdoll3DComponentAuthoredValid(bad));
    EXPECT_TRUE(IsRagdoll3DComponentAuthoredValid(Ragdoll3DComponent{}));
}

TEST_F(Ragdoll3DTest, FullMixamoRig_PreviewStartsAndFallsOntoTheStudioFloor) {
    const Entity skeleton = MakeSkeletonEntity(MakeFullMixamoSkeleton());
    const Entity ragdollEntity = MakeRagdoll(skeleton);
    ASSERT_TRUE(StartRagdollEditorPreview(entityManager, ragdollEntity))
        << entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity).lastSetupError;
    Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity);
    EXPECT_TRUE(ragdoll.lastSetupError.empty());
    EXPECT_EQ(ragdoll.solver.GetBodyCount(), 52U);
    EXPECT_GT(ragdoll.solver.GetJointCount(), 0U);

    const float startY = ragdoll.solver.GetBody(0U).position.y;
    for (int frame = 0; frame < 120; ++frame) {
        UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F, /*editorPreviewOnly=*/true, /*fallbackFloorPlaneY=*/0.0F);
    }
    const float endY = ragdoll.solver.GetBody(0U).position.y;
    EXPECT_LT(endY, startY - 0.2F) << "the preview must actually simulate";
    EXPECT_GT(endY, -0.05F) << "the studio floor catches it";
    EXPECT_TRUE(StopRagdollEditorPreview(entityManager, ragdollEntity));
}

TEST_F(Ragdoll3DTest, OversizedAuthoredBoneList_IsTrimmedNotRefused) {
    // A list written by the old uncapped generator: every Mixamo bone the profile used to match,
    // fingers included.
    const Skeleton3DComponent mixamo = MakeFullMixamoSkeleton();
    Ragdoll3DComponent authored;
    for (const SkeletonBone& bone : mixamo.bones) {
        if (bone.name.find("_End") != std::string::npos || bone.name.find("Toe") != std::string::npos) continue;
        RagdollBoneSetup setup;
        setup.boneName = bone.name;
        setup.mass = bone.name.find("Hand") != std::string::npos && bone.name.size() > 18U ? 0.5F : 3.0F;
        authored.bones.push_back(setup);
    }
    for (int ghost = 0; ghost < 16; ++ghost) {
        RagdollBoneSetup extra;
        extra.boneName = "ghost_" + std::to_string(ghost);
        extra.mass = 0.1F;
        authored.bones.push_back(extra);
    }
    ASSERT_GT(authored.bones.size(), static_cast<std::size_t>(kMaximumRagdollBones));

    std::vector<RagdollBoneSetup> trimmed = authored.bones;
    EXPECT_TRUE(TrimRagdollBonesToCap(trimmed));
    EXPECT_EQ(trimmed.size(), static_cast<std::size_t>(kMaximumRagdollBones));
    EXPECT_TRUE(ContainsBone(trimmed, "mixamorig:Hips"));
    EXPECT_TRUE(ContainsBone(trimmed, "mixamorig:LeftUpLeg"));
    std::vector<RagdollBoneSetup> again = trimmed;
    EXPECT_FALSE(TrimRagdollBonesToCap(again)) << "trimming is idempotent";

    const Entity skeleton = MakeSkeletonEntity(mixamo);
    const Entity ragdollEntity = MakeRagdoll(skeleton, authored);
    EXPECT_TRUE(InitializeRagdoll3D(entityManager, ragdollEntity))
        << entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity).lastSetupError;
    EXPECT_LE(entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity).solver.GetBodyCount(),
              Physics::kMaximumRagdollBodies);
}

TEST_F(Ragdoll3DTest, SetupFailures_SayWhy) {
    const Entity unlinked = MakeRagdoll(kInvalidEntity);
    EXPECT_FALSE(InitializeRagdoll3D(entityManager, unlinked));
    EXPECT_NE(entityManager.GetComponent<Ragdoll3DComponent>(unlinked).lastSetupError.find("Skeleton3D"),
              std::string::npos);

    Ragdoll3DComponent zeroBlend;
    zeroBlend.physicsBlendWeight = 0.0F;
    const Entity blended = MakeRagdoll(MakeSkeletonEntity(MakeHumanoidSkeleton()), zeroBlend);
    EXPECT_FALSE(StartRagdollEditorPreview(entityManager, blended));
    EXPECT_NE(entityManager.GetComponent<Ragdoll3DComponent>(blended).lastSetupError.find("Blend"),
              std::string::npos);

    // Bone names from a different rig are regenerated for the rig the ragdoll is on now.
    Ragdoll3DComponent stale;
    RagdollBoneSetup ghost;
    ghost.boneName = "SomeOtherRig_Pelvis";
    stale.bones = {ghost};
    const Entity restale = MakeRagdoll(MakeSkeletonEntity(MakeHumanoidSkeleton()), stale);
    EXPECT_TRUE(InitializeRagdoll3D(entityManager, restale));
    EXPECT_TRUE(ContainsBone(entityManager.GetComponent<Ragdoll3DComponent>(restale).bones, "pelvis"));
}

TEST_F(Ragdoll3DTest, FindNearestSkeleton_PrefersItsOwnCharacterOverAnother) {
    const auto attach = [this](const Entity child, const Entity parent) {
        if (!entityManager.HasComponent<HierarchyComponent>(child)) {
            entityManager.AddComponent<HierarchyComponent>(child, HierarchyComponent{});
        }
        entityManager.GetComponent<HierarchyComponent>(child).parent = parent;
    };
    const Entity level = entityManager.CreateEntity();
    entityManager.AddComponent<HierarchyComponent>(level, HierarchyComponent{});

    const Entity heroRoot = entityManager.CreateEntity();
    attach(heroRoot, level);
    const Entity heroSkeleton = MakeSkeletonEntity(MakeHumanoidSkeleton());
    attach(heroSkeleton, heroRoot);
    const Entity heroRagdoll = MakeRagdoll(kInvalidEntity);
    attach(heroRagdoll, heroRoot);

    const Entity villainRoot = entityManager.CreateEntity();
    attach(villainRoot, level);
    const Entity villainSkeleton = MakeSkeletonEntity(MakeHumanoidSkeleton());
    attach(villainSkeleton, villainRoot);

    EXPECT_EQ(FindNearestSkeleton3D(entityManager, heroRagdoll), heroSkeleton);
    EXPECT_TRUE(PrepareRagdoll3D(entityManager, heroRagdoll));
    EXPECT_EQ(entityManager.GetComponent<Ragdoll3DComponent>(heroRagdoll).skeleton, heroSkeleton);

    // Directly under the level, beside both characters, the choice is ambiguous: never guessed.
    const Entity loose = MakeRagdoll(kInvalidEntity);
    attach(loose, level);
    EXPECT_EQ(FindNearestSkeleton3D(entityManager, loose), kInvalidEntity);
    EXPECT_FALSE(PrepareRagdoll3D(entityManager, loose));
    EXPECT_FALSE(entityManager.GetComponent<Ragdoll3DComponent>(loose).lastSetupError.empty());
    static_cast<void>(villainSkeleton);
}

TEST_F(Ragdoll3DTest, BlueprintHost_DescribesRagdollFunctionsOnlyWhereThereIsARagdoll) {
    const Entity ragdollEntity = MakeRagdoll(MakeSkeletonEntity(MakeFullMixamoSkeleton()));
    const Core::BlueprintObjectHost host{entityManager, nullptr, ragdollEntity};
    for (const char* name : {"ragdoll.activate", "ragdoll.activate_powered", "ragdoll.deactivate", "ragdoll.get_up",
                             "ragdoll.is_active", "ragdoll.is_at_rest", "ragdoll.is_lying"}) {
        const std::optional<Blueprint::HostFunction> described = host.DescribeFunction(name);
        ASSERT_TRUE(described.has_value()) << name;
        EXPECT_TRUE(described->params.empty()) << name;
        EXPECT_EQ(described->result.kind, Blueprint::Type::Kind::Bool) << name;
    }
    const std::optional<Blueprint::HostFunction> hit = host.DescribeFunction("ragdoll.hit");
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->params.size(), 2U);
    const std::optional<Blueprint::HostFunction> blend = host.DescribeFunction("ragdoll.get_blend");
    ASSERT_TRUE(blend.has_value());
    EXPECT_EQ(blend->result.kind, Blueprint::Type::Kind::Float);

    const Core::BlueprintObjectHost bare{entityManager, nullptr, entityManager.CreateEntity()};
    EXPECT_FALSE(bare.DescribeFunction("ragdoll.activate").has_value());
    EXPECT_FALSE(bare.DescribeFunction("ragdoll.hit").has_value());
}

TEST_F(Ragdoll3DTest, BlueprintHost_ADriverOnTheCharacterReachesTheRagdollUnderIt) {
    // A character's script lives on the character; the ragdoll is a child of it.
    const Entity character = entityManager.CreateEntity();
    entityManager.AddComponent<TransformComponent>(character, TransformComponent{});
    const Entity skeletonEntity = MakeSkeletonEntity(MakeFullMixamoSkeleton());
    Ragdoll3DComponent setup;
    setup.skeleton = skeletonEntity;
    const Entity ragdollEntity = entityManager.CreateEntity();
    entityManager.AddComponent<Ragdoll3DComponent>(ragdollEntity, setup);
    HierarchyComponent link;
    link.parent = character;
    entityManager.AddComponent<HierarchyComponent>(ragdollEntity, link);

    Core::BlueprintObjectHost host{entityManager, nullptr, character};
    ASSERT_TRUE(host.DescribeFunction("ragdoll.activate").has_value());
    EXPECT_FALSE(std::get<bool>(host.CallFunction("ragdoll.is_active", {})));
    EXPECT_TRUE(std::get<bool>(host.CallFunction("ragdoll.activate", {})));
    EXPECT_TRUE(std::get<bool>(host.CallFunction("ragdoll.is_active", {})));
    EXPECT_EQ(entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity).mode, RagdollMode::Simulating);
}

TEST_F(Ragdoll3DTest, BlueprintHost_GoLimpFallGetUpAndDeactivate) {
    const Skeleton3DComponent mixamo = MakeFullMixamoSkeleton();
    const Entity skeletonEntity = MakeSkeletonEntity(mixamo);
    const Entity ragdollEntity = MakeRagdoll(skeletonEntity);
    Core::BlueprintObjectHost host{entityManager, nullptr, ragdollEntity};
    const float startHips = BoneWorldHeight(mixamo, "mixamorig:Hips");

    EXPECT_FALSE(std::get<bool>(host.CallFunction("ragdoll.get_up", {}))) << "nothing has fallen yet";
    ASSERT_TRUE(std::get<bool>(host.CallFunction("ragdoll.activate", {})));
    for (int frame = 0; frame < 150; ++frame) {
        UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F, false, 0.0F);
    }
    EXPECT_LT(BoneWorldHeight(entityManager.GetComponent<Skeleton3DComponent>(skeletonEntity), "mixamorig:Hips"),
              startHips - 0.5F);

    ASSERT_TRUE(std::get<bool>(host.CallFunction("ragdoll.get_up", {})));
    EXPECT_EQ(entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity).mode, RagdollMode::BlendingOut);
    for (int frame = 0; frame < 240; ++frame) {
        UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F, false, 0.0F);
    }
    EXPECT_EQ(entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity).mode, RagdollMode::Kinematic);
    EXPECT_NEAR(BoneWorldHeight(entityManager.GetComponent<Skeleton3DComponent>(skeletonEntity), "mixamorig:Hips"),
                startHips, 0.05F) << "a scripted get up really stands the body back up";

    ASSERT_TRUE(std::get<bool>(host.CallFunction("ragdoll.activate", {})));
    ASSERT_TRUE(std::get<bool>(host.CallFunction("ragdoll.deactivate", {})));
    EXPECT_EQ(entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity).mode, RagdollMode::Kinematic);
    EXPECT_FALSE(std::get<bool>(host.CallFunction("ragdoll.is_active", {})));
    EXPECT_FALSE(std::get<bool>(host.CallFunction("ragdoll.deactivate", {}))) << "already handed back";
}

TEST_F(Ragdoll3DTest, BlueprintHost_SettingsAreWrittenAndBadNumbersRefused) {
    const Entity ragdollEntity = MakeRagdoll(MakeSkeletonEntity(MakeFullMixamoSkeleton()));
    Core::BlueprintObjectHost host{entityManager, nullptr, ragdollEntity};
    const Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity);

    const std::array<Blueprint::Value, 1U> half{Blueprint::Value{0.5}};
    EXPECT_TRUE(std::get<bool>(host.CallFunction("ragdoll.set_blend", half)));
    EXPECT_FLOAT_EQ(ragdoll.physicsBlendWeight, 0.5F);
    EXPECT_DOUBLE_EQ(std::get<double>(host.CallFunction("ragdoll.get_blend", {})), 0.5);
    const std::array<Blueprint::Value, 1U> tooMuch{Blueprint::Value{7.0}};
    EXPECT_TRUE(std::get<bool>(host.CallFunction("ragdoll.set_blend", tooMuch)));
    EXPECT_FLOAT_EQ(ragdoll.physicsBlendWeight, 1.0F) << "clamped";
    const std::array<Blueprint::Value, 1U> notANumber{Blueprint::Value{std::numeric_limits<double>::quiet_NaN()}};
    EXPECT_FALSE(std::get<bool>(host.CallFunction("ragdoll.set_blend", notANumber)));
    EXPECT_FLOAT_EQ(ragdoll.physicsBlendWeight, 1.0F);

    const std::array<Blueprint::Value, 1U> yes{Blueprint::Value{true}};
    EXPECT_TRUE(std::get<bool>(host.CallFunction("ragdoll.set_auto_recover", yes)));
    EXPECT_TRUE(ragdoll.autoRecover);
    const std::array<Blueprint::Value, 1U> stiff{Blueprint::Value{300.0}};
    EXPECT_TRUE(std::get<bool>(host.CallFunction("ragdoll.set_motor_stiffness", stiff)));
    EXPECT_FLOAT_EQ(ragdoll.motorStiffness, 300.0F);
}

TEST_F(Ragdoll3DTest, BlueprintHost_AHitShovesTheBody) {
    const Entity skeletonEntity = MakeSkeletonEntity(MakeFullMixamoSkeleton());
    const Entity ragdollEntity = MakeRagdoll(skeletonEntity);
    Core::BlueprintObjectHost host{entityManager, nullptr, ragdollEntity};
    ASSERT_TRUE(std::get<bool>(host.CallFunction("ragdoll.activate", {})));
    const Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity);
    ASSERT_GT(ragdoll.solver.GetBodyCount(), 0U);
    const Math::Vector3 before = ragdoll.solver.GetBody(0U).linearVelocity;

    const std::array<Blueprint::Value, 1U> shove{Blueprint::Value{Blueprint::Vec3Value{0.0, 0.0, 400.0}}};
    EXPECT_TRUE(std::get<bool>(host.CallFunction("ragdoll.apply_impulse", shove)));
    EXPECT_GT(ragdoll.solver.GetBody(0U).linearVelocity.z, before.z + 0.5F);
}


// ---- Editing the mode or preset must never pull the bodies off the bones -----------------------

TEST_F(Ragdoll3DTest, EditingModeOrPreset_KeepsEveryBodyOnItsBoneAtAnyRigScale) {
    for (const float scale : {1.0F, 0.01F}) {
        for (const RagdollMode mode : {RagdollMode::Kinematic, RagdollMode::Active, RagdollMode::Simulating,
                                       RagdollMode::BlendingOut}) {
            for (const RagdollQualityPreset preset :
                 {RagdollQualityPreset::High, RagdollQualityPreset::Balanced, RagdollQualityPreset::Fast}) {
                Skeleton3DComponent mixamo = MakeFullMixamoSkeleton();
                for (SkeletonBone& bone : mixamo.bones) bone.localPosition = bone.localPosition * scale;
                const Entity skeletonEntity = entityManager.CreateEntity();
                entityManager.AddComponent<TransformComponent>(skeletonEntity, TransformComponent{});
                WorldTransformComponent world;
                world.worldScale = Math::Vector3{1.0F, 1.0F, 1.0F};
                world.dirty = false;
                entityManager.AddComponent<WorldTransformComponent>(skeletonEntity, world);
                entityManager.AddComponent<Skeleton3DComponent>(skeletonEntity, mixamo);
                const Entity ragdollEntity = MakeRagdoll(skeletonEntity);
                ASSERT_TRUE(InitializeRagdoll3D(entityManager, ragdollEntity)) << scale;

                Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity);
                std::vector<Math::Vector3> reference;
                for (std::size_t i = 0U; i < ragdoll.solver.GetBodyCount(); ++i) {
                    reference.push_back(ragdoll.solver.GetBody(i).position);
                }
                const float hipsBefore =
                    BoneWorldHeight(entityManager.GetComponent<Skeleton3DComponent>(skeletonEntity), "mixamorig:Hips");

                // What the inspector does for an edit of `mode` or `qualityPreset`.
                ragdoll.mode = mode;
                ragdoll.qualityPreset = preset;
                ApplyRagdollQualityPreset(ragdoll, preset);
                ragdoll.initialized = false;
                ASSERT_TRUE(InitializeRagdoll3D(entityManager, ragdollEntity));
                for (int frame = 0; frame < 30; ++frame) {
                    UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F, /*editorPreviewOnly=*/true);
                }
                ASSERT_TRUE(SyncRagdollBodiesToSkeletonPose(entityManager, ragdollEntity));

                float worst = 0.0F;
                ASSERT_EQ(ragdoll.solver.GetBodyCount(), reference.size());
                for (std::size_t i = 0U; i < reference.size(); ++i) {
                    worst = std::max(worst, std::sqrt(Math::LengthSquared(ragdoll.solver.GetBody(i).position -
                                                                             reference[i])));
                }
                EXPECT_LT(worst, 0.001F * std::max(scale, 0.1F))
                    << "mode " << static_cast<int>(mode) << " preset " << static_cast<int>(preset) << " scale "
                    << scale << ": a body moved off its bone by " << worst;
                const float hipsAfter =
                    BoneWorldHeight(entityManager.GetComponent<Skeleton3DComponent>(skeletonEntity), "mixamorig:Hips");
                EXPECT_NEAR(hipsAfter, hipsBefore, 1.0e-4F) << "the mesh pose must not move while only editing";
            }
        }
    }
}

// ---- Automatic reactions ------------------------------------------------------------------------

TEST_F(Ragdoll3DTest, NewRagdollStandsUntilSomethingActivatesIt) {
    EXPECT_EQ(Ragdoll3DComponent{}.mode, RagdollMode::Kinematic);
    EXPECT_FALSE(Ragdoll3DComponent{}.automaticReactions);
}

TEST_F(Ragdoll3DTest, Reactions_OffIgnoreHits_OnReactToTheirStrength) {
    const Skeleton3DComponent mixamo = MakeFullMixamoSkeleton();
    const Entity skeletonEntity = MakeSkeletonEntity(mixamo);
    Ragdoll3DComponent setup;
    setup.mode = RagdollMode::Kinematic;
    const Entity ragdollEntity = MakeRagdoll(skeletonEntity, setup);
    ASSERT_TRUE(InitializeRagdoll3D(entityManager, ragdollEntity));
    Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity);
    const Math::Vector3 hips = ragdoll.solver.GetBody(0U).position;

    // Off: a strong hit does not change what the body is doing.
    EXPECT_TRUE(ApplyRagdollImpact(entityManager, ragdollEntity, hips, {0.0F, 0.0F, 400.0F}, 0.5F));
    EXPECT_EQ(ragdoll.mode, RagdollMode::Kinematic);

    ragdoll.automaticReactions = true;
    // Below the stagger threshold: nothing.
    EXPECT_TRUE(ApplyRagdollImpact(entityManager, ragdollEntity, hips, {0.0F, 0.0F, 10.0F}, 0.5F));
    EXPECT_EQ(ragdoll.mode, RagdollMode::Kinematic);
    EXPECT_FALSE(ragdoll.staggering);
    // A light hit staggers.
    EXPECT_TRUE(ApplyRagdollImpact(entityManager, ragdollEntity, hips, {0.0F, 0.0F, 80.0F}, 0.5F));
    EXPECT_EQ(ragdoll.mode, RagdollMode::Active);
    EXPECT_TRUE(ragdoll.staggering);
    // A strong hit while staggering knocks the body down.
    EXPECT_TRUE(ApplyRagdollImpact(entityManager, ragdollEntity, hips, {0.0F, 0.0F, 400.0F}, 0.5F));
    EXPECT_EQ(ragdoll.mode, RagdollMode::Simulating);
    EXPECT_FALSE(ragdoll.staggering);
}

TEST_F(Ragdoll3DTest, Reactions_AStrongHitOnAStandingBodyKnocksItDown) {
    const Skeleton3DComponent mixamo = MakeFullMixamoSkeleton();
    const Entity skeletonEntity = MakeSkeletonEntity(mixamo);
    Ragdoll3DComponent setup;
    setup.mode = RagdollMode::Kinematic;
    setup.automaticReactions = true;
    const Entity ragdollEntity = MakeRagdoll(skeletonEntity, setup);
    ASSERT_TRUE(InitializeRagdoll3D(entityManager, ragdollEntity));
    const float startHips = BoneWorldHeight(mixamo, "mixamorig:Hips");
    const Math::Vector3 hips = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity).solver.GetBody(0U).position;
    ASSERT_TRUE(ApplyRagdollImpact(entityManager, ragdollEntity, hips, {0.0F, 0.0F, 600.0F}, 0.5F));
    for (int frame = 0; frame < 150; ++frame) {
        UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F, false, 0.0F);
    }
    EXPECT_LT(BoneWorldHeight(entityManager.GetComponent<Skeleton3DComponent>(skeletonEntity), "mixamorig:Hips"),
              startHips - 0.5F);
}

TEST_F(Ragdoll3DTest, Reactions_ALightHitStaggersAndTheBodyReturnsToTheAnimation) {
    const Skeleton3DComponent mixamo = MakeFullMixamoSkeleton();
    const Entity skeletonEntity = MakeSkeletonEntity(mixamo);
    Ragdoll3DComponent setup;
    setup.mode = RagdollMode::Kinematic;
    setup.automaticReactions = true;
    const Entity ragdollEntity = MakeRagdoll(skeletonEntity, setup);
    ASSERT_TRUE(InitializeRagdoll3D(entityManager, ragdollEntity));
    Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity);
    const float startHips = BoneWorldHeight(mixamo, "mixamorig:Hips");
    ASSERT_TRUE(ApplyRagdollImpact(entityManager, ragdollEntity, ragdoll.solver.GetBody(0U).position,
                                   {0.0F, 0.0F, 60.0F}, 0.5F));
    ASSERT_TRUE(ragdoll.staggering);

    float peak = 0.0F;
    float lowestHips = startHips;
    bool returned = false;
    for (int frame = 0; frame < 60 * 4 && !returned; ++frame) {
        UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F, false, 0.0F);
        if (ragdoll.staggering) peak = std::max(peak, ragdoll.effectiveBlend);
        lowestHips = std::min(lowestHips,
                              BoneWorldHeight(entityManager.GetComponent<Skeleton3DComponent>(skeletonEntity),
                                              "mixamorig:Hips"));
        returned = ragdoll.mode == RagdollMode::Kinematic && !ragdoll.staggering;
    }
    EXPECT_TRUE(returned) << "a stagger ends by itself";
    EXPECT_GT(peak, 0.1F) << "the physics took over for a moment";
    EXPECT_LE(peak, ragdoll.staggerBlendPeak + 1.0e-4F);
    EXPECT_GT(lowestHips, startHips - 0.3F) << "a stagger is not a fall";
}

TEST_F(Ragdoll3DTest, Reactions_AHardLandingKnocksTheBodyDownASoftOneDoesNot) {
    for (const float landingSpeed : {3.0F, 14.0F}) {
        const Entity character = entityManager.CreateEntity();
        entityManager.AddComponent<TransformComponent>(character, TransformComponent{});
        entityManager.AddComponent<CharacterControllerComponent>(character, CharacterControllerComponent{});
        const Entity skeletonEntity = MakeSkeletonEntity(MakeFullMixamoSkeleton());
        HierarchyComponent link;
        link.parent = character;
        entityManager.AddComponent<HierarchyComponent>(skeletonEntity, link);
        Ragdoll3DComponent setup;
        setup.mode = RagdollMode::Kinematic;
        setup.automaticReactions = true;
        const Entity ragdollEntity = MakeRagdoll(skeletonEntity, setup);
        ASSERT_TRUE(InitializeRagdoll3D(entityManager, ragdollEntity));
        CharacterControllerComponent& controller = entityManager.GetComponent<CharacterControllerComponent>(character);

        controller.grounded = false;
        controller.velocity = {0.0F, -landingSpeed, 0.0F};
        for (int frame = 0; frame < 5; ++frame) UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F, false, 0.0F);
        EXPECT_EQ(entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity).mode, RagdollMode::Kinematic)
            << "still in the air";
        controller.grounded = true;
        controller.velocity = {};
        UpdateRagdoll3DObjects(entityManager, 1.0F / 60.0F, false, 0.0F);
        EXPECT_EQ(entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity).mode,
                  landingSpeed > 9.0F ? RagdollMode::Simulating : RagdollMode::Kinematic)
            << "landing at " << landingSpeed;
    }
}

TEST_F(Ragdoll3DTest, Reactions_SettingsAreValidatedAndPresetsTuneTheStagger) {
    Ragdoll3DComponent bad;
    bad.fallImpulseThreshold = 10.0F; // below the stagger threshold
    EXPECT_FALSE(IsRagdoll3DComponentAuthoredValid(bad));
    bad = Ragdoll3DComponent{};
    bad.staggerBlendPeak = 1.5F;
    EXPECT_FALSE(IsRagdoll3DComponentAuthoredValid(bad));
    bad = Ragdoll3DComponent{};
    bad.staggerDecaySeconds = 0.0F;
    EXPECT_FALSE(IsRagdoll3DComponentAuthoredValid(bad));
    bad = Ragdoll3DComponent{};
    bad.fallLandingSpeed = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsRagdoll3DComponentAuthoredValid(bad));

    Ragdoll3DComponent preset;
    ApplyRagdollQualityPreset(preset, RagdollQualityPreset::High);
    const float highDecay = preset.staggerDecaySeconds;
    ApplyRagdollQualityPreset(preset, RagdollQualityPreset::Fast);
    EXPECT_LT(preset.staggerDecaySeconds, highDecay);
    EXPECT_TRUE(IsRagdoll3DComponentAuthoredValid(preset));
}

TEST_F(Ragdoll3DTest, Stagger_RefusesABodyThatIsAlreadyFalling) {
    const Entity skeletonEntity = MakeSkeletonEntity(MakeFullMixamoSkeleton());
    Ragdoll3DComponent setup;
    setup.mode = RagdollMode::Kinematic;
    const Entity ragdollEntity = MakeRagdoll(skeletonEntity, setup);
    EXPECT_TRUE(StaggerRagdoll(entityManager, ragdollEntity, 1.0F));
    ASSERT_TRUE(ActivateRagdoll(entityManager, ragdollEntity, RagdollMode::Simulating));
    EXPECT_FALSE(StaggerRagdoll(entityManager, ragdollEntity, 1.0F));
    EXPECT_FALSE(StaggerRagdoll(entityManager, ragdollEntity, std::numeric_limits<float>::quiet_NaN()));
}

TEST_F(Ragdoll3DTest, BlueprintHost_ModeReactionsAndStaggerNodes) {
    const Entity skeletonEntity = MakeSkeletonEntity(MakeFullMixamoSkeleton());
    Ragdoll3DComponent setup;
    setup.mode = RagdollMode::Kinematic;
    const Entity ragdollEntity = MakeRagdoll(skeletonEntity, setup);
    Core::BlueprintObjectHost host{entityManager, nullptr, ragdollEntity};
    const Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(ragdollEntity);
    const auto text = [](const char* value) { return Blueprint::Value{std::string{value}}; };

    EXPECT_EQ(std::get<std::string>(host.CallFunction("ragdoll.get_mode", {})), "Kinematic");
    const std::array<Blueprint::Value, 1U> simulating{text("Simulating")};
    EXPECT_TRUE(std::get<bool>(host.CallFunction("ragdoll.set_mode", simulating)));
    EXPECT_EQ(std::get<std::string>(host.CallFunction("ragdoll.get_mode", {})), "Simulating");
    const std::array<Blueprint::Value, 1U> kinematic{text("Kinematic")};
    EXPECT_TRUE(std::get<bool>(host.CallFunction("ragdoll.set_mode", kinematic)));
    EXPECT_EQ(ragdoll.mode, RagdollMode::Kinematic);
    const std::array<Blueprint::Value, 1U> active{text("Active")};
    EXPECT_TRUE(std::get<bool>(host.CallFunction("ragdoll.set_mode", active)));
    EXPECT_EQ(std::get<std::string>(host.CallFunction("ragdoll.get_mode", {})), "Active");
    const std::array<Blueprint::Value, 1U> nonsense{text("Floating")};
    EXPECT_FALSE(std::get<bool>(host.CallFunction("ragdoll.set_mode", nonsense))) << "an unknown mode is refused";
    EXPECT_EQ(ragdoll.mode, RagdollMode::Active);
    ASSERT_TRUE(std::get<bool>(host.CallFunction("ragdoll.deactivate", {})));

    // Reactions on and off, and the named settings.
    EXPECT_FALSE(std::get<bool>(host.CallFunction("ragdoll.get_automatic_reactions", {})));
    const std::array<Blueprint::Value, 1U> yes{Blueprint::Value{true}};
    EXPECT_TRUE(std::get<bool>(host.CallFunction("ragdoll.set_automatic_reactions", yes)));
    EXPECT_TRUE(ragdoll.automaticReactions);
    const std::array<Blueprint::Value, 2U> peak{text("StaggerPeak"), Blueprint::Value{0.4}};
    EXPECT_TRUE(std::get<bool>(host.CallFunction("ragdoll.set_reaction", peak)));
    EXPECT_FLOAT_EQ(ragdoll.staggerBlendPeak, 0.4F);
    const std::array<Blueprint::Value, 1U> peakName{text("StaggerPeak")};
    EXPECT_DOUBLE_EQ(std::get<double>(host.CallFunction("ragdoll.get_reaction", peakName)), static_cast<double>(0.4F));
    const std::array<Blueprint::Value, 2U> tooMuch{text("StaggerPeak"), Blueprint::Value{4.0}};
    EXPECT_FALSE(std::get<bool>(host.CallFunction("ragdoll.set_reaction", tooMuch))) << "refused by the component rule";
    EXPECT_FLOAT_EQ(ragdoll.staggerBlendPeak, 0.4F) << "and left as it was";
    const std::array<Blueprint::Value, 2U> unknown{text("Nothing"), Blueprint::Value{1.0}};
    EXPECT_FALSE(std::get<bool>(host.CallFunction("ragdoll.set_reaction", unknown)));
    const std::array<Blueprint::Value, 2U> belowStagger{text("FallImpulse"), Blueprint::Value{1.0}};
    EXPECT_FALSE(std::get<bool>(host.CallFunction("ragdoll.set_reaction", belowStagger)))
        << "a fall threshold under the stagger threshold is refused";

    // Stagger by hand.
    const std::array<Blueprint::Value, 1U> full{Blueprint::Value{1.0}};
    EXPECT_TRUE(std::get<bool>(host.CallFunction("ragdoll.stagger", full)));
    EXPECT_TRUE(ragdoll.staggering);
    EXPECT_EQ(ragdoll.mode, RagdollMode::Active);
}

} // namespace
} // namespace UVE::Scene::Tests
