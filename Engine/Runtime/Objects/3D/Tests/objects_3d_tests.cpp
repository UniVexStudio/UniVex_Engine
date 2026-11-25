// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <limits>

#include <array>
#include <utility>
#include <gtest/gtest.h>

#include "all_objects_3d.h"
#include "bone_attachment_3d.h"
#include "animation_driver_component.h"
#include "animation_sequencer_component.h"
#include "area_component.h"
#include "audio_source_component.h"
#include "auto_translate_component.h"
#include "bone_modifier_component.h"
#include "canvas_component.h"
#include "editor_description_component.h"
#include "editor_internal_entity_component.h"
#include "hierarchy_component.h"
#include "mesh_component.h"
#include "name_component.h"
#include "physics_object_component.h"
#include "primitive_mesh_component.h"
#include "render_instance_component.h"
#include "surface_instance_component.h"
#include "thread_group_component.h"
#include "ui_text_component.h"
#include "visibility_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"

namespace UVE::Scene::Tests {
namespace {

TEST(Expanded3DObjectComponentsTest, DefaultContractsAreValid) {
    EXPECT_TRUE(IsAreaComponentValid(AreaComponent{}));
    EXPECT_TRUE(IsRayCast3DObjectComponentValid(RayCast3DComponent{}));
    EXPECT_TRUE(IsKinematic3DObjectComponentValid(Kinematic3DComponent{}));
    EXPECT_TRUE(IsNavMeshVolume3DObjectComponentValid(NavMeshVolume3DComponent{}));
    EXPECT_TRUE(IsNavSeeker3DObjectComponentValid(NavSeeker3DComponent{}));
    EXPECT_TRUE(IsSkeleton3DObjectComponentValid(Skeleton3DComponent{}));
    EXPECT_TRUE(IsBoneAttachment3DObjectComponentValid(BoneAttachment3DComponent{}));
    EXPECT_TRUE(IsSpringArm3DObjectComponentValid(SpringArm3DComponent{}));
    EXPECT_TRUE(IsMarker3DObjectComponentValid(Marker3DComponent{}));
    EXPECT_TRUE(IsHitbox3DObjectComponentValid(Hitbox3DComponent{}));
    EXPECT_TRUE(IsHurtbox3DObjectComponentValid(Hurtbox3DComponent{}));
    EXPECT_TRUE(IsProjectile3DObjectComponentValid(Projectile3DComponent{}));
    EXPECT_TRUE(IsInteractionArea3DObjectComponentValid(InteractionArea3DComponent{}));
    EXPECT_TRUE(IsWorldEnvironment3DObjectComponentValid(WorldEnvironment3DComponent{}));
    EXPECT_TRUE(IsReflectionProbe3DObjectComponentValid(ReflectionProbe3DComponent{}));
    EXPECT_TRUE(IsDecal3DObjectComponentValid(Decal3DComponent{}));
    EXPECT_TRUE(IsLodGroup3DObjectComponentValid(LodGroup3DComponent{}));
    EXPECT_TRUE(IsOccluder3DObjectComponentValid(Occluder3DComponent{}));
    EXPECT_TRUE(IsVisibilityRegion3DObjectComponentValid(VisibilityRegion3DComponent{}));
    EXPECT_TRUE(IsSpawnPoint3DObjectComponentValid(SpawnPoint3DComponent{}));
    EXPECT_TRUE(IsPlayer3DObjectComponentValid(PlayerComponent{}));
    EXPECT_TRUE(IsHealthComponentValid(HealthComponent{}));
    EXPECT_TRUE(IsLevelStreamer3DObjectComponentValid(LevelStreamer3DComponent{}));
    EXPECT_TRUE(IsWorldPartition3DObjectComponentValid(WorldPartition3DComponent{}));
}

TEST(Expanded3DObjectComponentsTest, Skeleton3DDefaultIsEmptyAndBoneAttachmentIsInert) {
    const Skeleton3DComponent skeleton;
    const BoneAttachment3DComponent attachment;

    EXPECT_TRUE(skeleton.skeletonAssetPath.empty());
    EXPECT_TRUE(skeleton.bones.empty());
    EXPECT_FALSE(IsBoneAttachment3DObjectComponentResolvable(attachment));
}

TEST(Expanded3DObjectComponentsTest, ExplicitSkeletonAssetBindingIsFailureAtomic) {
    Skeleton3DComponent skeleton;
    skeleton.enabled = false;
    const Skeleton3DComponent original = skeleton;

    EXPECT_FALSE(TryBindExplicitSkeleton3DAsset(
        skeleton, {}, {SkeletonBone{"root", -1, {}, {}, {1.0F, 1.0F, 1.0F}}}));
    EXPECT_EQ(skeleton.skeletonAssetPath, original.skeletonAssetPath);
    EXPECT_TRUE(skeleton.bones.empty());
    EXPECT_EQ(skeleton.enabled, original.enabled);

    EXPECT_FALSE(TryBindExplicitSkeleton3DAsset(skeleton, "assets/character.uvskel", {}));
    EXPECT_EQ(skeleton.skeletonAssetPath, original.skeletonAssetPath);
    EXPECT_TRUE(skeleton.bones.empty());
}

TEST(Expanded3DObjectComponentsTest, ExplicitSkeletonAssetBindingHydratesOnlySuppliedHierarchy) {
    Skeleton3DComponent skeleton;
    const std::vector<SkeletonBone> authoredBones{
        SkeletonBone{"root", -1, {}, {}, {1.0F, 1.0F, 1.0F}},
        SkeletonBone{"hand", 0, {1.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}};

    EXPECT_TRUE(TryBindExplicitSkeleton3DAsset(
        skeleton, "assets/character.uvskel", authoredBones));
    EXPECT_EQ(skeleton.skeletonAssetPath, "assets/character.uvskel");
    ASSERT_EQ(skeleton.bones.size(), authoredBones.size());
    EXPECT_EQ(skeleton.bones[0].name, authoredBones[0].name);
    EXPECT_EQ(skeleton.bones[1].name, authoredBones[1].name);
    EXPECT_EQ(skeleton.bones[1].parentIndex, authoredBones[1].parentIndex);
}

TEST(Expanded3DObjectComponentsTest, BoneAttachmentBecomesResolvableOnlyWithExplicitReferences) {
    BoneAttachment3DComponent attachment;
    attachment.skeleton = Entity{7U, 1U};
    attachment.boneName = "hand";

    EXPECT_TRUE(IsBoneAttachment3DObjectComponentResolvable(attachment));
    attachment.enabled = false;
    EXPECT_FALSE(IsBoneAttachment3DObjectComponentResolvable(attachment));

    // A bone reference is a name or an index, and the index alone is enough: a rig whose bones are
    // addressed by number needs no name at all.
    attachment.enabled = true;
    attachment.boneName.clear();
    EXPECT_FALSE(IsBoneAttachment3DObjectComponentResolvable(attachment));
    attachment.boneIndex = 0U;
    EXPECT_TRUE(IsBoneAttachment3DObjectComponentResolvable(attachment));

    // The skeleton reference is not decoration: without one the attachment is inert whatever else it
    // names, and a default-constructed component points at nothing.
    attachment.skeleton = kInvalidEntity;
    EXPECT_FALSE(IsBoneAttachment3DObjectComponentResolvable(attachment));
}

/// A two-bone rig: a root at the skeleton's origin and a hand half a metre along the root's X.
[[nodiscard]] Skeleton3DComponent TwoBoneRig() {
    Skeleton3DComponent skeleton;
    skeleton.skeletonAssetPath = "assets/character.uvskel";
    skeleton.bones = {SkeletonBone{"root", -1, {}, {}, {1.0F, 1.0F, 1.0F}},
                      SkeletonBone{"hand", 0, {0.5F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}};
    return skeleton;
}

TEST(Expanded3DObjectComponentsTest, BoneAttachmentResolverComposesTheBoneChainInPoseOrder) {
    const Skeleton3DComponent skeleton = TwoBoneRig();
    ObjectWorldFrame frame{};
    ASSERT_TRUE(TryResolveSkeletonBoneWorldFrame(skeleton, 1U, ObjectWorldFrame{}, frame));
    EXPECT_NEAR(frame.position.x, 0.5F, 1e-5F) << "rest pose: half a metre along the skeleton's X";

    // The runtime pose is per bone and only covers the bones something animates. Posing the ROOT
    // moves the hand with it, because the hand's own transform is relative to the ROOT BONE - a
    // resolver that composed every bone against the skeleton's frame would leave the hand behind.
    Skeleton3DComponent posed = skeleton;
    posed.pose = {SkeletonBonePose{{1.0F, 2.0F, 0.0F}, {}, {2.0F, 2.0F, 2.0F}}};
    ASSERT_TRUE(TryResolveSkeletonBoneWorldFrame(posed, 1U, ObjectWorldFrame{}, frame));
    EXPECT_NEAR(frame.position.x, 2.0F, 1e-5F) << "the posed root's 1 m, plus its 2x scale on the hand's 0.5 m";
    EXPECT_NEAR(frame.position.y, 2.0F, 1e-5F);
    EXPECT_NEAR(frame.scale.x, 2.0F, 1e-5F) << "the root's scale carries down the chain";

    // Rotation composes down the chain the same way: a quarter turn on the root takes the hand's +X
    // to the world's -Z.
    Skeleton3DComponent turned = skeleton;
    turned.pose = {SkeletonBonePose{{0.0F, 0.0F, 0.0F},
                                       Math::Quaternion{0.0F, 0.70710678F, 0.0F, 0.70710678F},
                                       {1.0F, 1.0F, 1.0F}}};
    ASSERT_TRUE(TryResolveSkeletonBoneWorldFrame(turned, 1U, ObjectWorldFrame{}, frame));
    EXPECT_NEAR(frame.position.x, 0.0F, 1e-5F);
    EXPECT_NEAR(frame.position.z, -0.5F, 1e-5F) << "+90 degrees about Y takes +X to -Z";

    // A skeleton that stands somewhere else carries its bones with it, and a bone's chain is composed
    // inside that frame rather than beside it.
    ASSERT_TRUE(TryResolveSkeletonBoneWorldFrame(
        skeleton, 1U, ObjectWorldFrame{{10.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, frame));
    EXPECT_NEAR(frame.position.x, 10.5F, 1e-5F);
}

TEST(Expanded3DObjectComponentsTest, BoneAttachmentResolverRefusesMalformedChainsAndUnknownBones) {
    const Skeleton3DComponent skeleton = TwoBoneRig();
    ObjectWorldFrame frame{};
    EXPECT_FALSE(TryResolveSkeletonBoneWorldFrame(skeleton, 2U, ObjectWorldFrame{}, frame));
    EXPECT_FALSE(TryResolveSkeletonBoneWorldFrame(Skeleton3DComponent{}, 0U, ObjectWorldFrame{}, frame));

    // A parent index outside the array is a corrupt asset, not a bone without a parent.
    Skeleton3DComponent stray = skeleton;
    stray.bones[1].parentIndex = 9;
    EXPECT_FALSE(TryResolveSkeletonBoneWorldFrame(stray, 1U, ObjectWorldFrame{}, frame));

    // A cycle has no root to compose from, so the walk refuses instead of running forever.
    Skeleton3DComponent cyclic = skeleton;
    cyclic.bones[0].parentIndex = 1;
    EXPECT_FALSE(TryResolveSkeletonBoneWorldFrame(cyclic, 0U, ObjectWorldFrame{}, frame));

    // A non-finite skeleton frame would poison every bone under it.
    EXPECT_FALSE(TryResolveSkeletonBoneWorldFrame(
        skeleton, 1U,
        ObjectWorldFrame{{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, frame));

    std::uint32_t index = 0U;
    EXPECT_TRUE(TryFindSkeletonBoneIndex(skeleton, "hand", index));
    EXPECT_EQ(index, 1U);
    EXPECT_FALSE(TryFindSkeletonBoneIndex(skeleton, "Hand", index)) << "names are exact, not case-insensitive";
    EXPECT_FALSE(TryFindSkeletonBoneIndex(skeleton, "", index));
    EXPECT_FALSE(TryFindSkeletonBoneIndex(skeleton, "tail", index));
}

TEST(Expanded3DObjectComponentsTest, BoneAttachmentComposesItsAuthoredOffsetInTheBonesOwnSpace) {
    const ObjectWorldFrame bone{{0.0F, 1.0F, 0.0F},
                                   Math::Quaternion{0.0F, 0.70710678F, 0.0F, 0.70710678F},
                                   {1.0F, 1.0F, 1.0F}};
    // 10 cm along the BONE's X: the bone is turned a quarter turn, so that comes out along the
    // world's -Z. An attachment authored in world units would land at +X and stay there while the
    // arm swings, which is exactly what the frame of reference has to prevent.
    const ObjectWorldFrame attachment =
        ComposeBoneAttachmentWorldFrame(bone, {0.1F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F});
    EXPECT_NEAR(attachment.position.x, 0.0F, 1e-5F);
    EXPECT_NEAR(attachment.position.y, 1.0F, 1e-5F);
    EXPECT_NEAR(attachment.position.z, -0.1F, 1e-5F);

    // The attachment's own rotation composes onto the bone's, and its scale onto the chain's, so a
    // scaled rig scales what it carries.
    const ObjectWorldFrame scaled =
        ComposeBoneAttachmentWorldFrame(bone, {}, {}, {2.0F, 1.0F, 1.0F});
    EXPECT_NEAR(scaled.scale.x, 2.0F, 1e-5F);
    EXPECT_NEAR(scaled.scale.y, 1.0F, 1e-5F);
}

TEST(Expanded3DObjectComponentsTest, BoneAttachmentLocalTransformInvertsWhatTheSceneGraphComposes) {
    // The scene graph composes world = parent then local (scale, then rotate, then translate). An
    // attachment has to be written in that same space, or its parent would move it a second time
    // during propagation - so what the resolver writes has to compose back out to the frame the bone
    // is in. Recomposed here exactly as SceneGraph::Update() does it.
    const ObjectWorldFrame parent{{5.0F, 2.0F, -1.0F},
                                     Math::Quaternion{0.0F, 0.70710678F, 0.0F, 0.70710678F},
                                     {2.0F, 2.0F, 2.0F}};
    const ObjectWorldFrame wanted{{7.0F, 3.0F, -3.0F},
                                     Math::Quaternion{0.0F, 0.0F, 0.38268343F, 0.92387953F},
                                     {4.0F, 4.0F, 4.0F}};
    Math::Vector3 localPosition{};
    Math::Quaternion localRotation{};
    Math::Vector3 localScale{};
    ASSERT_TRUE(TryMakeBoneAttachmentLocalTransform(wanted, parent, localPosition, localRotation, localScale));

    const Math::Vector3 recomposedPosition =
        parent.position +
        Math::RotateVector(parent.rotation, Math::Vector3{localPosition.x * parent.scale.x,
                                                                localPosition.y * parent.scale.y,
                                                                localPosition.z * parent.scale.z});
    EXPECT_NEAR(recomposedPosition.x, wanted.position.x, 1e-4F);
    EXPECT_NEAR(recomposedPosition.y, wanted.position.y, 1e-4F);
    EXPECT_NEAR(recomposedPosition.z, wanted.position.z, 1e-4F);
    // Rotation is compared through a vector, because q and -q are the same rotation and comparing
    // components would fail on a sign the engine is right to allow.
    const Math::Vector3 axis{0.3F, -0.6F, 0.2F};
    const Math::Vector3 throughLocal =
        Math::RotateVector(Math::Multiply(parent.rotation, localRotation), axis);
    const Math::Vector3 throughWanted = Math::RotateVector(wanted.rotation, axis);
    EXPECT_NEAR(throughLocal.x, throughWanted.x, 1e-4F);
    EXPECT_NEAR(throughLocal.y, throughWanted.y, 1e-4F);
    EXPECT_NEAR(throughLocal.z, throughWanted.z, 1e-4F);
    EXPECT_NEAR(localScale.x * parent.scale.x, wanted.scale.x, 1e-4F);
    EXPECT_NEAR(localScale.y * parent.scale.y, wanted.scale.y, 1e-4F);
    EXPECT_NEAR(localScale.z * parent.scale.z, wanted.scale.z, 1e-4F);

    // A parent flattened by a zero scale cannot be inverted: the arithmetic would divide by it and
    // hand the renderer an infinity. Refusing keeps the last good transform instead.
    const ObjectWorldFrame flattened{{0.0F, 0.0F, 0.0F}, {}, {1.0F, 0.0F, 1.0F}};
    EXPECT_FALSE(TryMakeBoneAttachmentLocalTransform(wanted, flattened, localPosition, localRotation, localScale));
    const ObjectWorldFrame infinite{{std::numeric_limits<float>::infinity(), 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}};
    EXPECT_FALSE(TryMakeBoneAttachmentLocalTransform(infinite, parent, localPosition, localRotation, localScale));
}

TEST(Expanded3DObjectComponentsTest, Hitbox3DStrikeStateIsRuntimeOnlyAndNeverAuthored) {
    Hitbox3DComponent hitbox;
    EXPECT_EQ(hitbox.strikeCount, 0U);
    EXPECT_FALSE(hitbox.strikesTruncated);
    EXPECT_EQ(hitbox.ignoreEntity, kInvalidEntity);
    EXPECT_EQ(hitbox.struckCount, 0U);

    // Mid-frame runtime state must not change authoring validity - a live hitbox with fresh
    // strikes still passes the same validation a freshly authored one does.
    hitbox.strikeCount = 3U;
    hitbox.strikesTruncated = true;
    hitbox.strikes[0U] = Hitbox3DStrike{Entity{}, 0.25F};
    EXPECT_TRUE(IsHitbox3DObjectComponentValid(hitbox));
}

TEST(Expanded3DObjectComponentsTest, Hurtbox3DHitStateIsRuntimeOnlyAndNeverAuthored) {
    Hurtbox3DComponent hurtbox;
    EXPECT_EQ(hurtbox.hitCount, 0U);
    EXPECT_FALSE(hurtbox.hitsTruncated);
    EXPECT_EQ(hurtbox.ignoreEntity, kInvalidEntity);
    EXPECT_EQ(hurtbox.receivedCount, 0U);

    hurtbox.hitCount = 3U;
    hurtbox.hitsTruncated = true;
    hurtbox.hits[0U] = Hurtbox3DHit{Entity{}, 0.25F};
    EXPECT_TRUE(IsHurtbox3DObjectComponentValid(hurtbox));
}

TEST(Expanded3DObjectComponentsTest, InteractionArea3DInteractorStateIsRuntimeOnlyAndNeverAuthored) {
    InteractionArea3DComponent area;
    EXPECT_EQ(area.interactorCount, 0U);
    EXPECT_FALSE(area.interactorsTruncated);
    EXPECT_FALSE(area.focusedByPrimaryInteractor);
    EXPECT_EQ(area.ignoreEntity, kInvalidEntity);

    area.interactorCount = 3U;
    area.interactorsTruncated = true;
    area.focusedByPrimaryInteractor = true;
    EXPECT_TRUE(IsInteractionArea3DObjectComponentValid(area));
}

TEST(Expanded3DObjectComponentsTest, RayCast3DExclusionListIsAPrefixOfRealReferences) {
    // A freshly authored ray excludes nobody: every slot is the empty sentinel, never index 0 of a
    // pool that may already hold a live entity - a default Entity is a real handle.
    RayCast3DComponent ray;
    EXPECT_EQ(CountRayCast3DExclusions(ray), 0U);
    for (const Entity& exclusion : ray.exclusions) {
        EXPECT_EQ(exclusion, kInvalidEntity);
    }
    EXPECT_TRUE(IsRayCast3DObjectComponentValid(ray));

    // Live references count as themselves, in the order they were authored.
    ray.exclusions[0] = Entity{3U, 1U};
    ray.exclusions[1] = Entity{7U, 2U};
    EXPECT_EQ(CountRayCast3DExclusions(ray), 2U);
    EXPECT_TRUE(IsRayCast3DObjectComponentValid(ray));

    // The list is a prefix, so a live reference behind an empty slot is refused: no query would
    // ever reach it, and a ray that silently ignores an authored exclusion is worse than one that
    // refuses to load. The same rule makes the trailing slots the only place a hole may be.
    RayCast3DComponent holed = ray;
    holed.exclusions[0] = kInvalidEntity;
    EXPECT_EQ(CountRayCast3DExclusions(holed), 0U);
    EXPECT_FALSE(IsRayCast3DObjectComponentValid(holed));

    // Duplicates are refused rather than collapsed: two identical slots are an authoring mistake,
    // and dropping one silently would hide which of the two was meant.
    RayCast3DComponent duplicated = ray;
    duplicated.exclusions[1] = duplicated.exclusions[0];
    EXPECT_FALSE(IsRayCast3DObjectComponentValid(duplicated));

    // All eight slots can be real. The bound is the array, so "the ninth exclusion" is not a value
    // this component can hold at all.
    RayCast3DComponent full;
    for (std::size_t index = 0U; index < kMaximumRayCastExclusions; ++index) {
        full.exclusions[index] = Entity{static_cast<std::uint32_t>(index), 1U};
    }
    EXPECT_EQ(CountRayCast3DExclusions(full), kMaximumRayCastExclusions);
    EXPECT_TRUE(IsRayCast3DObjectComponentValid(full));
}

TEST(Expanded3DObjectComponentsTest, Projectile3DHitContractRejectsWhatItCannotHonor) {
    // The authored defaults are a valid projectile: a sphere that flies, stops on a hit and lives
    // ten seconds.
    Projectile3DComponent projectile;
    EXPECT_TRUE(IsProjectile3DObjectComponentValid(projectile));
    EXPECT_TRUE(IsKnownProjectile3DHitPolicy(projectile.hitPolicy));
    EXPECT_EQ(projectile.ignoreEntity, kInvalidEntity);

    // A policy outside the enum is a malformed component, not a policy to guess at.
    Projectile3DComponent policy = projectile;
    policy.hitPolicy = static_cast<Projectile3DHitPolicy>(7U);
    EXPECT_FALSE(IsKnownProjectile3DHitPolicy(policy.hitPolicy));
    EXPECT_FALSE(IsProjectile3DObjectComponentValid(policy));

    // The bounce coefficients live inside 0..1 and nowhere else.
    Projectile3DComponent coefficient = projectile;
    coefficient.restitution = -0.1F;
    EXPECT_FALSE(IsProjectile3DObjectComponentValid(coefficient));
    coefficient.restitution = 1.1F;
    EXPECT_FALSE(IsProjectile3DObjectComponentValid(coefficient));
    coefficient.restitution = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsProjectile3DObjectComponentValid(coefficient));
    coefficient.restitution = 0.0F;
    coefficient.friction = 1.0F;
    EXPECT_TRUE(IsProjectile3DObjectComponentValid(coefficient));
    coefficient.friction = -0.01F;
    EXPECT_FALSE(IsProjectile3DObjectComponentValid(coefficient));
    coefficient.friction = 1.01F;
    EXPECT_FALSE(IsProjectile3DObjectComponentValid(coefficient));

    // A sphere has to have a size, and a lifetime has to be reachable.
    Projectile3DComponent bounds = projectile;
    bounds.radius = 0.0F;
    EXPECT_FALSE(IsProjectile3DObjectComponentValid(bounds));
    bounds = projectile;
    bounds.maxLifetime = 0.0F;
    EXPECT_FALSE(IsProjectile3DObjectComponentValid(bounds));
    bounds = projectile;
    bounds.remainingLifetime = projectile.maxLifetime + 1.0F;
    EXPECT_FALSE(IsProjectile3DObjectComponentValid(bounds));
    bounds = projectile;
    bounds.velocity.x = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsProjectile3DObjectComponentValid(bounds));

    // Runtime result state has to stay readable: a claimed hit names its entity and carries finite
    // numbers, so a consumer that trusts `hit` can never act on a hit that points nowhere.
    Projectile3DComponent hit = projectile;
    hit.hit = true;
    EXPECT_FALSE(IsProjectile3DObjectComponentValid(hit));
    hit.hitEntity = Entity{4U, 1U};
    hit.hitPosition = Math::Vector3{1.0F, 2.0F, 3.0F};
    hit.hitNormal = Math::Vector3{0.0F, 1.0F, 0.0F};
    hit.impactSpeed = 12.0F;
    EXPECT_TRUE(IsProjectile3DObjectComponentValid(hit));
    hit.impactSpeed = -1.0F;
    EXPECT_FALSE(IsProjectile3DObjectComponentValid(hit));
    hit.impactSpeed = 12.0F;
    hit.hitNormal.y = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsProjectile3DObjectComponentValid(hit));
}

TEST(Expanded3DObjectComponentsTest, Projectile3DBounceReflectsAndNeverAddsEnergy) {
    const Math::Vector3 intoTheWall{6.0F, 0.0F, 0.0F};
    const Math::Vector3 wallNormal{-1.0F, 0.0F, 0.0F};

    // Head-on: the speed into the surface comes back at the restitution, and nothing else is left.
    const Math::Vector3 bounced =
        ResolveProjectile3DBounceVelocity(intoTheWall, wallNormal, 0.5F, 0.0F);
    EXPECT_NEAR(bounced.x, -3.0F, 1.0e-5F);
    EXPECT_NEAR(bounced.y, 0.0F, 1.0e-5F);
    EXPECT_NEAR(bounced.z, 0.0F, 1.0e-5F);

    // Grazing: the component into the floor comes back at the restitution, the component along it
    // loses the friction, and the two do not mix.
    const Math::Vector3 grazing =
        ResolveProjectile3DBounceVelocity(Math::Vector3{2.0F, -4.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F},
                                             0.5F, 0.25F);
    EXPECT_NEAR(grazing.x, 1.5F, 1.0e-5F);
    EXPECT_NEAR(grazing.y, 2.0F, 1.0e-5F);

    // Coefficients are clamped: 5 is 1, and 1 is the most a bounce can return.
    const Math::Vector3 clamped =
        ResolveProjectile3DBounceVelocity(intoTheWall, wallNormal, 5.0F, 0.0F);
    EXPECT_NEAR(Math::Length(clamped), 6.0F, 1.0e-4F);
    const Math::Vector3 slick =
        ResolveProjectile3DBounceVelocity(Math::Vector3{2.0F, -4.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F},
                                             1.0F, 5.0F);
    EXPECT_NEAR(slick.x, 0.0F, 1.0e-5F);
    EXPECT_NEAR(slick.y, 4.0F, 1.0e-5F);

    // The normal is a direction, not a distance: a longer one reflects the same motion as a unit
    // one, so nothing downstream has to remember to normalize before calling this.
    const Math::Vector3 alongZ{0.0F, 0.0F, 6.0F};
    const Math::Vector3 unitNormal{0.0F, 0.0F, -1.0F};
    const Math::Vector3 longNormal{0.0F, 0.0F, -4.0F};
    const Math::Vector3 scaled = ResolveProjectile3DBounceVelocity(alongZ, longNormal, 1.0F, 0.0F);
    EXPECT_EQ(scaled, ResolveProjectile3DBounceVelocity(alongZ, unitNormal, 1.0F, 0.0F));
    EXPECT_NEAR(scaled.z, -6.0F, 1.0e-5F);

    // Nothing to reflect about, or nothing finite to reflect: the motion is handed back unchanged
    // rather than replaced with a NaN.
    const Math::Vector3 noNormal{};
    EXPECT_EQ(ResolveProjectile3DBounceVelocity(intoTheWall, noNormal, 1.0F, 0.0F), intoTheWall);
    const Math::Vector3 nanNormal{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    EXPECT_EQ(ResolveProjectile3DBounceVelocity(intoTheWall, nanNormal, 1.0F, 0.0F), intoTheWall);
    const Math::Vector3 infiniteVelocity{std::numeric_limits<float>::infinity(), 0.0F, 0.0F};
    EXPECT_EQ(ResolveProjectile3DBounceVelocity(infiniteVelocity, wallNormal, 1.0F, 0.0F), infiniteVelocity);
}

TEST(Expanded3DObjectComponentsTest, BoundedContractsRejectUnsafeValues) {
    RayCast3DComponent ray;
    // A ray with no usable direction, no reach, or a length nobody can cast is refused outright.
    ray.direction = Math::Vector3{};
    EXPECT_FALSE(IsRayCast3DObjectComponentValid(ray));
    ray.direction = Math::Vector3{0.0F, -1.0F, 0.0F};
    ray.length = 0.0F;
    EXPECT_FALSE(IsRayCast3DObjectComponentValid(ray));
    ray.length = -1.0F;
    EXPECT_FALSE(IsRayCast3DObjectComponentValid(ray));
    ray.length = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsRayCast3DObjectComponentValid(ray));
    ray.length = 1.0F;
    EXPECT_TRUE(IsRayCast3DObjectComponentValid(ray));

    Skeleton3DComponent skeleton;
    skeleton.bones.push_back(SkeletonBone{"root", -1, {}, {}, {1.0F, 1.0F, 1.0F}});
    skeleton.bones.push_back(SkeletonBone{"root", 0, {}, {}, {1.0F, 1.0F, 1.0F}});
    EXPECT_FALSE(IsSkeleton3DObjectComponentValid(skeleton));

    LodGroup3DComponent lod;
    lod.distanceThresholds[1] = lod.distanceThresholds[0];
    EXPECT_FALSE(IsLodGroup3DObjectComponentValid(lod));

    LevelStreamer3DComponent streamer;
    streamer.enabled = true;
    streamer.levelPath.clear();
    EXPECT_FALSE(IsLevelStreamer3DObjectComponentValid(streamer));

    WorldPartition3DComponent partition;
    partition.cellCounts[1] = 0U;
    EXPECT_FALSE(IsWorldPartition3DObjectComponentValid(partition));

    Kinematic3DComponent kinematic;
    kinematic.interpolation = 1.5F;
    EXPECT_FALSE(IsKinematic3DObjectComponentValid(kinematic));
    kinematic.interpolation = -0.5F;
    EXPECT_FALSE(IsKinematic3DObjectComponentValid(kinematic));
    // Both ends of the authored range are real: 1 is "at speed this step", 0 is "never ease on its
    // own" - a body a script drives by writing its velocity.
    kinematic.interpolation = 1.0F;
    EXPECT_TRUE(IsKinematic3DObjectComponentValid(kinematic));
    kinematic.interpolation = 0.0F;
    EXPECT_TRUE(IsKinematic3DObjectComponentValid(kinematic));
    // A body switched off is authored data, not something to reject: `active` is the reversible
    // "stop where you are" the mover honours.
    kinematic.interpolation = 0.5F;
    kinematic.active = false;
    EXPECT_TRUE(IsKinematic3DObjectComponentValid(kinematic));
}

TEST(Expanded3DObjectComponentsTest, FiniteAndBoundedValuesRejectNonFinitePayloads) {
    WorldEnvironment3DComponent environment;
    environment.exposure = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));

    SpringArm3DComponent springArm;
    springArm.currentLength = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsSpringArm3DObjectComponentValid(springArm));

    // A target velocity that is not a number is not a body going nowhere: it is a malformed
    // component, refused here so the mover never has to guess (it drops one written at runtime).
    Kinematic3DComponent kinematic;
    kinematic.targetVelocity.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsKinematic3DObjectComponentValid(kinematic));
}

} // namespace
TEST(LodGroup3DResolveTest, EachThresholdSelectsItsOwnLevel) {
    // The core rule: the first threshold the distance fits under wins. Boundaries are inclusive,
    // so an object sitting exactly on a threshold picks the nearer level rather than flickering
    // between two as it drifts by a millimetre.
    LodGroup3DComponent group;
    group.distanceThresholds = {10.0F, 25.0F, 60.0F, 120.0F, 240.0F, 480.0F, 960.0F, 1920.0F};
    group.levelCount = 4U;

    const std::array<std::pair<float, std::uint8_t>, 8U> cases{{
        {0.0F, 0U}, {9.9F, 0U}, {10.0F, 0U}, {10.1F, 1U},
        {25.0F, 1U}, {25.1F, 2U}, {60.0F, 2U}, {60.1F, 3U}}};
    for (const auto& [distance, expectedLevel] : cases) {
        ResolveLodGroup3DLevel(group, distance);
        EXPECT_EQ(group.currentLevel, expectedLevel) << "at distance " << distance;
        EXPECT_FALSE(group.culledByDistance) << "at distance " << distance;
    }
}

TEST(LodGroup3DResolveTest, PastTheLastThresholdIsCulledWithoutRunningOffTheChain) {
    // Two things at once. The entity must be culled, and currentLevel must stay at the last REAL
    // level - a consumer that indexes a mesh array by level would otherwise read out of bounds
    // simply because an object moved too far away.
    LodGroup3DComponent group;
    group.levelCount = 4U;
    ResolveLodGroup3DLevel(group, 500.0F);

    EXPECT_TRUE(group.culledByDistance);
    EXPECT_EQ(group.currentLevel, 3U) << "must clamp to the last level, not run one past it";
    EXPECT_LT(group.currentLevel, group.levelCount);
}

TEST(LodGroup3DResolveTest, LevelCountShortensTheChainWithoutRewritingDistances) {
    // An author shortening a chain should not have to edit the distances. With levelCount 2 the
    // third threshold is ignored entirely, so 30 m is past the end even though the array still
    // holds a 60 m entry.
    LodGroup3DComponent group;
    group.distanceThresholds = {10.0F, 25.0F, 60.0F, 120.0F, 240.0F, 480.0F, 960.0F, 1920.0F};
    group.levelCount = 2U;

    ResolveLodGroup3DLevel(group, 20.0F);
    EXPECT_EQ(group.currentLevel, 1U);
    EXPECT_FALSE(group.culledByDistance);

    ResolveLodGroup3DLevel(group, 30.0F);
    EXPECT_TRUE(group.culledByDistance) << "past entry 1 is past the end when levelCount is 2";
    EXPECT_EQ(group.currentLevel, 1U);
}

TEST(LodGroup3DResolveTest, EveryDegenerateCaseDrawsAtFullDetailRatherThanHiding) {
    // A configuration mistake should be visible so it gets fixed. Hiding geometry instead looks
    // exactly like a missing asset, which sends whoever hits it looking in the wrong place.
    LodGroup3DComponent disabled;
    disabled.enabled = false;
    ResolveLodGroup3DLevel(disabled, 100000.0F);
    EXPECT_EQ(disabled.currentLevel, 0U);
    EXPECT_FALSE(disabled.culledByDistance) << "a disabled group must never distance-cull";

    LodGroup3DComponent emptyChain;
    emptyChain.levelCount = 0U;
    ResolveLodGroup3DLevel(emptyChain, 100000.0F);
    EXPECT_EQ(emptyChain.currentLevel, 0U);
    EXPECT_FALSE(emptyChain.culledByDistance);

    LodGroup3DComponent notFinite;
    ResolveLodGroup3DLevel(notFinite, std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(notFinite.currentLevel, 0U);
    EXPECT_FALSE(notFinite.culledByDistance) << "a NaN distance must not hide an object";

    LodGroup3DComponent overLong;
    overLong.levelCount = static_cast<std::uint8_t>(kMaximumLodLevels + 1U);
    ResolveLodGroup3DLevel(overLong, 50.0F);
    EXPECT_EQ(overLong.currentLevel, 0U);
    EXPECT_FALSE(overLong.culledByDistance);
}

TEST(LodGroup3DResolveTest, ResolvingLeavesTheComponentValid) {
    // The resolver writes two derived fields, and the validator checks currentLevel against
    // levelCount. A resolve that produced an out-of-range level would make a previously valid
    // component fail validation on save - a corruption that only shows up at serialisation time.
    LodGroup3DComponent group;
    ASSERT_TRUE(IsLodGroup3DObjectComponentValid(group));
    for (const float distance : {0.0F, 5.0F, 50.0F, 500.0F, 100000.0F}) {
        ResolveLodGroup3DLevel(group, distance);
        EXPECT_TRUE(IsLodGroup3DObjectComponentValid(group)) << "after resolving at " << distance;
    }
}

TEST(LodGroup3DResolveTest, HysteresisKeepsALevelUntilTheDistanceIsWellPastItsThreshold) {
    // The whole point of the band: an object drifting across a threshold must not swap meshes on
    // alternate frames. With a 10% band on a 10 m threshold, the level is entered past 11 m and
    // only left under 9 m - and everything between those two points keeps whatever it had.
    LodGroup3DComponent group;
    group.levelCount = 4U;
    group.hysteresis = 0.1F;

    ResolveLodGroup3DLevel(group, 9.0F);
    EXPECT_EQ(group.currentLevel, 0U);

    // Past the threshold, but inside the entry band: the old level is kept, which is the whole
    // difference from the stateless rule.
    ResolveLodGroup3DLevel(group, 10.5F);
    EXPECT_EQ(group.currentLevel, 0U) << "10.5 m is not past 11 m, so the band holds level 0";

    ResolveLodGroup3DLevel(group, 11.5F);
    EXPECT_EQ(group.currentLevel, 1U) << "11.5 m is past the entry point";

    // Back inside the threshold, but not under the exit point: still level 1.
    ResolveLodGroup3DLevel(group, 10.5F);
    EXPECT_EQ(group.currentLevel, 1U) << "hysteresis means the way back is not the way in";

    ResolveLodGroup3DLevel(group, 8.5F);
    EXPECT_EQ(group.currentLevel, 0U) << "8.5 m is under the exit point";
}

TEST(LodGroup3DResolveTest, EveryThresholdCarriesItsOwnBand) {
    // The band is relative to each threshold, not one global distance, so a chain of thresholds an
    // order of magnitude apart still has a useful band at both ends.
    LodGroup3DComponent group;
    group.levelCount = 4U;
    group.hysteresis = 0.1F;

    ResolveLodGroup3DLevel(group, 20.0F);
    ASSERT_EQ(group.currentLevel, 1U);

    ResolveLodGroup3DLevel(group, 26.0F);
    EXPECT_EQ(group.currentLevel, 1U) << "25 m's entry point is 27.5 m";

    ResolveLodGroup3DLevel(group, 28.0F);
    EXPECT_EQ(group.currentLevel, 2U);

    ResolveLodGroup3DLevel(group, 24.0F);
    EXPECT_EQ(group.currentLevel, 2U) << "25 m's exit point is 22.5 m";

    ResolveLodGroup3DLevel(group, 22.0F);
    EXPECT_EQ(group.currentLevel, 1U);

    // The same rule at the far end of the chain: 60 m is entered past 66 m and left under 54 m.
    ResolveLodGroup3DLevel(group, 70.0F);
    EXPECT_EQ(group.currentLevel, 3U);
    ResolveLodGroup3DLevel(group, 61.0F);
    EXPECT_EQ(group.currentLevel, 3U);
    ResolveLodGroup3DLevel(group, 53.0F);
    EXPECT_EQ(group.currentLevel, 2U);
}

TEST(LodGroup3DResolveTest, ACulledObjectStaysCulledInsideTheBandAndReturnsOnce) {
    // Culling is the level past the end of the chain, so it has a band too. Without one, an object
    // hovering on the last threshold would pop in and out of the visibility set every frame - and
    // every pop is a full asset resolution and placement, not just a draw.
    LodGroup3DComponent group;
    group.levelCount = 4U;
    group.hysteresis = 0.1F;

    ResolveLodGroup3DLevel(group, 200.0F);
    EXPECT_TRUE(group.culledByDistance);
    EXPECT_EQ(group.currentLevel, 3U) << "a culled object still names its last real level";

    ResolveLodGroup3DLevel(group, 125.0F);
    EXPECT_TRUE(group.culledByDistance) << "125 m is inside 120 m's 12 m band, so it stays culled";

    ResolveLodGroup3DLevel(group, 100.0F);
    EXPECT_FALSE(group.culledByDistance) << "100 m is under the 108 m return point";
    EXPECT_EQ(group.currentLevel, 3U) << "and lands on the level its distance is actually in";
}

TEST(LodGroup3DResolveTest, HysteresisZeroIsTheStatelessRuleItReplaced) {
    // An object that never opts into a band must resolve exactly as it did before hysteresis
    // existed - including through jumps in both directions, which is where a rule that remembered
    // anything would diverge.
    LodGroup3DComponent group;
    group.levelCount = 4U;
    ASSERT_FLOAT_EQ(group.hysteresis, 0.0F);

    const std::array<std::pair<float, std::uint8_t>, 9U> cases{{
        {5.0F, 0U}, {200.0F, 3U}, {30.0F, 2U}, {12.0F, 1U}, {1000.0F, 3U},
        {60.0F, 2U}, {25.0F, 1U}, {10.0F, 0U}, {119.0F, 3U}}};
    for (const auto& [distance, expectedLevel] : cases) {
        ResolveLodGroup3DLevel(group, distance);
        EXPECT_EQ(group.currentLevel, expectedLevel) << "at distance " << distance;
    }
    ResolveLodGroup3DLevel(group, 500.0F);
    EXPECT_TRUE(group.culledByDistance);
}

TEST(LodGroup3DResolveTest, ATeleportAcrossSeveralLevelsLandsOnTheLevelItsDistanceIsIn) {
    // A teleported object crosses several thresholds in one frame. The step-by-step walk still ends
    // where the distance says it should, and the cull state follows the same banded rule.
    LodGroup3DComponent group;
    group.levelCount = 4U;
    group.hysteresis = 0.1F;

    ResolveLodGroup3DLevel(group, 5.0F);
    ASSERT_EQ(group.currentLevel, 0U);

    ResolveLodGroup3DLevel(group, 500.0F);
    EXPECT_TRUE(group.culledByDistance);
    EXPECT_EQ(group.currentLevel, 3U);

    // Coming back from far away is one decision: un-cull and drop to the level the distance is in,
    // rather than spending a frame visible at the coarsest level.
    ResolveLodGroup3DLevel(group, 5.0F);
    EXPECT_FALSE(group.culledByDistance);
    EXPECT_EQ(group.currentLevel, 0U);
}

TEST(LodGroup3DResolveTest, AnOutOfRangeHysteresisResolvesAsNoBandInsteadOfRefusing) {
    // Authoring rejects an out-of-range band, so this is a value that reached the resolver anyway -
    // a hand-edited file, a script writing the component. It must still resolve to something
    // sensible rather than produce an arbitrary band, so it clamps to the maximum.
    LodGroup3DComponent group;
    group.levelCount = 4U;
    group.hysteresis = 10.0F;

    // Clamped to 0.5, so the 10 m threshold is entered strictly past 15 m - the same rule the
    // maximum band would give, not a 10-second-wide band nobody could author.
    ResolveLodGroup3DLevel(group, 15.0F);
    EXPECT_EQ(group.currentLevel, 0U) << "15 m is the entry point, and entry is strict";

    ResolveLodGroup3DLevel(group, 16.0F);
    EXPECT_EQ(group.currentLevel, 1U);
    EXPECT_FALSE(group.culledByDistance);
}

TEST(LodGroup3DMeshTest, EachLevelDrawsItsOwnMeshAndEmptySlotsFallBackToTheBaseMesh) {
    // The swapping rule, in one place: the level names a mesh, and a level that names none draws
    // the entity's own MeshComponent mesh. That is what lets an author fill in levels 1..N and
    // leave level 0 - the full-detail mesh they already authored - alone.
    const Asset::AssetGuid baseMesh{7U};
    LodGroup3DComponent group;
    group.levelCount = 3U;
    group.lodMeshGuids[1] = Asset::AssetGuid{42U};

    ResolveLodGroup3DLevel(group, 5.0F);
    ASSERT_EQ(group.currentLevel, 0U);
    EXPECT_EQ(ResolveLodGroup3DMeshGuid(group, baseMesh), baseMesh)
        << "an unset level 0 draws the component's own mesh";

    ResolveLodGroup3DLevel(group, 20.0F);
    ASSERT_EQ(group.currentLevel, 1U);
    EXPECT_EQ(ResolveLodGroup3DMeshGuid(group, baseMesh), Asset::AssetGuid{42U});

    ResolveLodGroup3DLevel(group, 70.0F);
    ASSERT_EQ(group.currentLevel, 2U);
    EXPECT_EQ(ResolveLodGroup3DMeshGuid(group, baseMesh), baseMesh)
        << "level 2 overrides nothing, so it falls back too - not only level 0";
}

TEST(LodGroup3DMeshTest, ACulledGroupStillAnswersWithItsLastLevelsMesh) {
    // The renderer returns before asking - a culled object is not drawn at all - but the resolver
    // must not read out of range for a consumer that asks anyway, and it must not answer with a
    // mesh from a level the object is not on.
    const Asset::AssetGuid baseMesh{7U};
    LodGroup3DComponent group;
    group.levelCount = 2U;
    group.lodMeshGuids[0] = Asset::AssetGuid{11U};
    group.lodMeshGuids[1] = Asset::AssetGuid{22U};

    ResolveLodGroup3DLevel(group, 500.0F);
    ASSERT_TRUE(group.culledByDistance);
    ASSERT_EQ(group.currentLevel, 1U);
    EXPECT_EQ(ResolveLodGroup3DMeshGuid(group, baseMesh), Asset::AssetGuid{22U});

    // A level outside the chain (a hand-written component) falls back rather than reading past the
    // authored levels.
    group.currentLevel = 9U;
    EXPECT_EQ(ResolveLodGroup3DMeshGuid(group, baseMesh), baseMesh);
}

TEST(LodGroup3DResolveTest, TheValidatorRejectsABandOutsideTheAuthoredRange) {
    LodGroup3DComponent group;
    ASSERT_TRUE(IsLodGroup3DObjectComponentValid(group));

    group.hysteresis = -0.1F;
    EXPECT_FALSE(IsLodGroup3DObjectComponentValid(group));

    group.hysteresis = kMaximumLodHysteresis;
    EXPECT_TRUE(IsLodGroup3DObjectComponentValid(group)) << "the maximum is a real band, not a refusal";

    group.hysteresis = kMaximumLodHysteresis + 0.01F;
    EXPECT_FALSE(IsLodGroup3DObjectComponentValid(group));

    group.hysteresis = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsLodGroup3DObjectComponentValid(group));
}

TEST(Decal3DProjectionTest, APointInsideTheVolumeIsPaintedAndOneOutsideIsNot) {
    // The projection turns "where is this surface point relative to the decal" into the volume's
    // own unit coordinates: 0 at the centre, 1 at the surface, whatever `size` happens to be. That
    // is the whole reason the box and the cylinder share one footprint test.
    Decal3DComponent decal;
    decal.size = Math::Vector3{2.0F, 2.0F, 2.0F};
    Decal3DProjection projection{};
    ASSERT_TRUE(TryMakeDecal3DProjection(decal, Math::Vector3{}, Math::Quaternion{},
                                            Math::Vector3{1.0F, 1.0F, 1.0F}, projection));

    const Decal3DSample centre =
        SampleDecal3D(projection, Math::Vector3{}, Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F);
    EXPECT_TRUE(centre.insideVolume);
    EXPECT_TRUE(centre.Paints());
    EXPECT_NEAR(centre.local.x, 0.0F, 1.0e-5F);
    EXPECT_FLOAT_EQ(centre.combinedWeight, 1.0F);

    const Decal3DSample inner = SampleDecal3D(projection, Math::Vector3{0.5F, 0.5F, 0.5F},
                                                    Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F);
    EXPECT_TRUE(inner.Paints());
    EXPECT_NEAR(inner.local.x, 0.5F, 1.0e-5F) << "half of the way to the surface, in unit coordinates";

    const Decal3DSample outside = SampleDecal3D(projection, Math::Vector3{1.5F, 0.0F, 0.0F},
                                                      Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F);
    EXPECT_FALSE(outside.insideVolume);
    EXPECT_FALSE(outside.Paints());
    EXPECT_FLOAT_EQ(outside.combinedWeight, 0.0F) << "outside the volume, nothing paints";
}

TEST(Decal3DProjectionTest, ScaleGrowsTheVolumeAndRotationTurnsItsAxis) {
    Decal3DComponent decal;
    decal.size = Math::Vector3{2.0F, 2.0F, 2.0F};

    // The volume follows the object's world scale - a scaled decal paints a bigger area, which is
    // what every other size in the engine does.
    Decal3DProjection scaled{};
    ASSERT_TRUE(TryMakeDecal3DProjection(decal, Math::Vector3{}, Math::Quaternion{},
                                            Math::Vector3{2.0F, 1.0F, 1.0F}, scaled));
    EXPECT_TRUE(SampleDecal3D(scaled, Math::Vector3{1.5F, 0.0F, 0.0F},
                                 Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F)
                    .Paints())
        << "1.5 is inside a half-extent of 2";
    EXPECT_FALSE(SampleDecal3D(scaled, Math::Vector3{0.0F, 0.0F, 1.5F},
                                  Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F)
                     .Paints())
        << "but outside the unscaled axes";

    // +90 degrees about Y maps the local +X axis to world -Z, so a point half a metre below in
    // world -Z comes back as local +X - the projection is not a world-axis-aligned box.
    const Math::Quaternion quarterTurnAboutY{0.0F, 0.70710678F, 0.0F, 0.70710678F};
    Decal3DProjection rotated{};
    ASSERT_TRUE(TryMakeDecal3DProjection(decal, Math::Vector3{}, quarterTurnAboutY,
                                            Math::Vector3{1.0F, 1.0F, 1.0F}, rotated));
    const Decal3DSample below = SampleDecal3D(rotated, Math::Vector3{0.0F, 0.0F, -0.5F},
                                                    Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F);
    EXPECT_TRUE(below.insideVolume);
    EXPECT_NEAR(below.local.x, 0.5F, 1.0e-4F);
    EXPECT_NEAR(below.local.z, 0.0F, 1.0e-4F);
    EXPECT_FALSE(SampleDecal3D(rotated, Math::Vector3{1.5F, 0.0F, 0.0F},
                                  Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F)
                     .Paints())
        << "world +X is the rotated volume's local -Z: 1.5 there is outside";
}

TEST(Decal3DProjectionTest, ACylinderFootprintIsRadialNotSquare) {
    Decal3DComponent decal;
    decal.size = Math::Vector3{2.0F, 2.0F, 2.0F};
    decal.projection = DecalProjectionMode::Cylinder;
    Decal3DProjection projection{};
    ASSERT_TRUE(TryMakeDecal3DProjection(decal, Math::Vector3{}, Math::Quaternion{},
                                            Math::Vector3{1.0F, 1.0F, 1.0F}, projection));

    // The corner of the box is inside the box and outside the cylinder - the one case that tells
    // the two footprints apart.
    EXPECT_FALSE(SampleDecal3D(projection, Math::Vector3{0.9F, 0.0F, 0.9F},
                                  Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F)
                     .Paints())
        << "0.9 squared twice is more than the unit radius";
    EXPECT_TRUE(SampleDecal3D(projection, Math::Vector3{0.5F, 0.0F, 0.6F},
                                 Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F)
                    .Paints());

    // The axis is still bounded the same way: the cylinder gets no taller than the box.
    EXPECT_FALSE(SampleDecal3D(projection, Math::Vector3{0.0F, 1.5F, 0.0F},
                                  Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F)
                     .Paints());
}

TEST(Decal3DProjectionTest, NormalFadeTurnsFacingIntoWeight) {
    // normalFade 0 paints any facing; 1 paints only surfaces turned back towards the decal. The
    // volume projects along -Y, so "towards the decal" is a +Y normal.
    Decal3DComponent decal;
    Decal3DProjection projection{};
    ASSERT_TRUE(TryMakeDecal3DProjection(decal, Math::Vector3{}, Math::Quaternion{},
                                            Math::Vector3{1.0F, 1.0F, 1.0F}, projection));

    const Math::Vector3 sideways{1.0F, 0.0F, 0.0F};
    EXPECT_FLOAT_EQ(SampleDecal3D(projection, Math::Vector3{}, sideways, 0.0F).combinedWeight, 1.0F)
        << "with no fade declared, a sideways surface is painted at full weight";

    Decal3DComponent strict = decal;
    strict.normalFade = 1.0F;
    Decal3DProjection strictProjection{};
    ASSERT_TRUE(TryMakeDecal3DProjection(strict, Math::Vector3{}, Math::Quaternion{},
                                            Math::Vector3{1.0F, 1.0F, 1.0F}, strictProjection));
    EXPECT_FLOAT_EQ(SampleDecal3D(strictProjection, Math::Vector3{},
                                     Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F)
                        .combinedWeight,
                    1.0F);
    EXPECT_FLOAT_EQ(SampleDecal3D(strictProjection, Math::Vector3{}, sideways, 0.0F).combinedWeight,
                    0.0F)
        << "a surface at a right angle faces away from the projection";
    EXPECT_FLOAT_EQ(SampleDecal3D(strictProjection, Math::Vector3{},
                                     Math::Vector3{0.0F, -1.0F, 0.0F}, 0.0F)
                        .combinedWeight,
                    0.0F)
        << "a backface is not painted";

    Decal3DComponent halfFade = decal;
    halfFade.normalFade = 0.5F;
    Decal3DProjection halfProjection{};
    ASSERT_TRUE(TryMakeDecal3DProjection(halfFade, Math::Vector3{}, Math::Quaternion{},
                                            Math::Vector3{1.0F, 1.0F, 1.0F}, halfProjection));
    EXPECT_FLOAT_EQ(SampleDecal3D(halfProjection, Math::Vector3{}, sideways, 0.0F).combinedWeight,
                    0.5F);
}

TEST(Decal3DProjectionTest, TheVerticalFadesCutTheEndsOfTheVolumeAndDistanceFadeCutsItsFarSide) {
    Decal3DComponent decal;
    decal.size = Math::Vector3{2.0F, 2.0F, 2.0F};
    decal.upperFade = 0.5F;
    decal.lowerFade = 0.5F;
    decal.distanceFadeEnabled = true;
    decal.distanceFadeBegin = 40.0F;
    decal.distanceFadeLength = 10.0F;
    Decal3DProjection projection{};
    ASSERT_TRUE(TryMakeDecal3DProjection(decal, Math::Vector3{}, Math::Quaternion{},
                                            Math::Vector3{1.0F, 1.0F, 1.0F}, projection));

    // Middle of the volume, at the camera: nothing fades.
    EXPECT_FLOAT_EQ(SampleDecal3D(projection, Math::Vector3{}, Math::Vector3{0.0F, 1.0F, 0.0F}, 10.0F)
                        .combinedWeight,
                    1.0F);

    // Nine tenths of the way up: inside the top band, three fifths of the way through it.
    const Decal3DSample nearTop = SampleDecal3D(
        projection, Math::Vector3{0.0F, 0.9F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 10.0F);
    EXPECT_TRUE(nearTop.insideVolume);
    EXPECT_NEAR(nearTop.combinedWeight, 0.2F, 1.0e-4F) << "(0.9 - 0.5) / 0.5 leaves a fifth";

    // The same at the bottom, which uses the lower band rather than the upper one.
    const Decal3DSample nearBottom = SampleDecal3D(
        projection, Math::Vector3{0.0F, -0.9F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 10.0F);
    EXPECT_NEAR(nearBottom.combinedWeight, 0.2F, 1.0e-4F);
    // The fades stay apart so a caller can say WHICH one removed a sample: the top point's loss is
    // entirely the vertical fade, not the normal one.
    EXPECT_FLOAT_EQ(nearTop.normalFadeWeight, 1.0F);
    EXPECT_NEAR(nearTop.depthFadeWeight, 0.2F, 1.0e-4F);
    EXPECT_FLOAT_EQ(nearTop.distanceFadeWeight, 1.0F);

    // Distance: full until the band begins, half way through it, gone past its end.
    EXPECT_FLOAT_EQ(SampleDecal3D(projection, Math::Vector3{}, Math::Vector3{0.0F, 1.0F, 0.0F}, 35.0F)
                        .combinedWeight,
                    1.0F);
    EXPECT_FLOAT_EQ(SampleDecal3D(projection, Math::Vector3{}, Math::Vector3{0.0F, 1.0F, 0.0F}, 45.0F)
                        .combinedWeight,
                    0.5F);
    EXPECT_FLOAT_EQ(SampleDecal3D(projection, Math::Vector3{}, Math::Vector3{0.0F, 1.0F, 0.0F}, 60.0F)
                        .combinedWeight,
                    0.0F);

    // A zero-length band is a hard cut at `begin`, not a division by zero.
    Decal3DComponent hardCut = decal;
    hardCut.distanceFadeLength = 0.0F;
    Decal3DProjection hardCutProjection{};
    ASSERT_TRUE(TryMakeDecal3DProjection(hardCut, Math::Vector3{}, Math::Quaternion{},
                                            Math::Vector3{1.0F, 1.0F, 1.0F}, hardCutProjection));
    EXPECT_FLOAT_EQ(SampleDecal3D(hardCutProjection, Math::Vector3{},
                                     Math::Vector3{0.0F, 1.0F, 0.0F}, 39.0F)
                        .combinedWeight,
                    1.0F);
    EXPECT_FLOAT_EQ(SampleDecal3D(hardCutProjection, Math::Vector3{},
                                     Math::Vector3{0.0F, 1.0F, 0.0F}, 41.0F)
                        .combinedWeight,
                    0.0F);
}

TEST(Decal3DProjectionTest, ADegeneratePoseOrSizePaintsNothingRatherThanASmear) {
    Decal3DComponent decal;
    Decal3DProjection projection{};

    Decal3DComponent zeroSize = decal;
    zeroSize.size = Math::Vector3{0.0F, 1.0F, 1.0F};
    EXPECT_FALSE(TryMakeDecal3DProjection(zeroSize, Math::Vector3{}, Math::Quaternion{},
                                             Math::Vector3{1.0F, 1.0F, 1.0F}, projection));
    EXPECT_FALSE(TryMakeDecal3DProjection(decal, Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
                                             Math::Quaternion{}, Math::Vector3{1.0F, 1.0F, 1.0F}, projection));
    EXPECT_FALSE(TryMakeDecal3DProjection(decal, Math::Vector3{}, Math::Quaternion{},
                                             Math::Vector3{1.0F, 0.0F, 1.0F}, projection))
        << "a zero world-scale axis has no volume to project";
    EXPECT_FALSE(TryMakeDecal3DProjection(decal, Math::Vector3{}, Math::Quaternion{0.0F, 0.0F, 0.0F, 0.0F},
                                             Math::Vector3{1.0F, 1.0F, 1.0F}, projection))
        << "a rotation that cannot be inverted has no volume frame";

    // A non-finite sample is refused too: a NaN weight would spread through the blend.
    ASSERT_TRUE(TryMakeDecal3DProjection(decal, Math::Vector3{}, Math::Quaternion{},
                                            Math::Vector3{1.0F, 1.0F, 1.0F}, projection));
    const Decal3DSample nanPoint =
        SampleDecal3D(projection, Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
                         Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F);
    EXPECT_FALSE(nanPoint.Paints());
    EXPECT_FLOAT_EQ(nanPoint.combinedWeight, 0.0F);
}

TEST(Decal3DLifetimeTest, ALifetimeCountsDownOnSimulatedSecondsAndExpiresExactlyOnce) {
    Decal3DComponent decal;
    decal.lifetime = 2.0F;

    // The first advance arms the countdown from the authored lifetime: a decal that just appeared
    // must not expire because its remaining time started at zero.
    EXPECT_FALSE(AdvanceDecal3DLifetime(decal, 0.5F));
    EXPECT_NEAR(decal.remainingLifetime, 1.5F, 1.0e-5F);
    EXPECT_FALSE(decal.expired);

    EXPECT_FALSE(AdvanceDecal3DLifetime(decal, 1.4F));
    EXPECT_NEAR(decal.remainingLifetime, 0.1F, 1.0e-5F);

    // The crossing is reported once, and only once: every later advance is a no-op.
    EXPECT_TRUE(AdvanceDecal3DLifetime(decal, 0.2F));
    EXPECT_FLOAT_EQ(decal.remainingLifetime, 0.0F);
    EXPECT_TRUE(decal.expired);
    EXPECT_FALSE(AdvanceDecal3DLifetime(decal, 10.0F));
    EXPECT_FALSE(AdvanceDecal3DLifetime(decal, 10.0F));
}

TEST(Decal3DLifetimeTest, APermanentDecalNeverExpiresAndABadClockChangesNothing) {
    Decal3DComponent permanent;
    permanent.lifetime = 0.0F;
    EXPECT_FALSE(AdvanceDecal3DLifetime(permanent, 100000.0F));
    EXPECT_FALSE(permanent.expired) << "lifetime 0 keeps a decal forever";
    EXPECT_FLOAT_EQ(permanent.remainingLifetime, 0.0F);

    Decal3DComponent timed;
    timed.lifetime = 5.0F;
    ASSERT_FALSE(AdvanceDecal3DLifetime(timed, 1.0F));
    const float before = timed.remainingLifetime;
    EXPECT_FALSE(AdvanceDecal3DLifetime(timed, std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(AdvanceDecal3DLifetime(timed, -1.0F));
    EXPECT_FALSE(AdvanceDecal3DLifetime(timed, 0.0F));
    EXPECT_FLOAT_EQ(timed.remainingLifetime, before) << "a broken or paused clock must not age it";

    // What the renderer asks before touching any geometry.
    EXPECT_TRUE(IsDecal3DPainting(timed));
    timed.enabled = false;
    EXPECT_FALSE(IsDecal3DPainting(timed));
    timed.enabled = true;
    timed.expired = true;
    EXPECT_FALSE(IsDecal3DPainting(timed));
    timed.expired = false;
    timed.size = Math::Vector3{0.0F, 1.0F, 1.0F};
    EXPECT_FALSE(IsDecal3DPainting(timed));
}

TEST(LevelStreamer3DNearestViewerTest, EmptyViewerListMeansNoDistanceAtAll) {
    // A streamer with no valid viewers must report "no distance"; the verdict function relies on
    // that absence to never load or unload on a viewerless tick.
    EXPECT_FALSE(ResolveLevelStreamer3DNearestViewerDistanceSquared({1.0F, 2.0F, 3.0F}, {})
                     .has_value());
}

TEST(LevelStreamer3DNearestViewerTest, NearestWinsAndSquaredStaysSquared) {
    // The three viewers sit 5, 2, and 3 units away on the X axis. The nearest (2 units) yields
    // 4.0 - squared, matching the ordering-only discipline that needs no sqrt anywhere in the
    // streaming path.
    const std::array<Math::Vector3, 3U> viewers{
        Math::Vector3{5.0F, 0.0F, 0.0F},
        Math::Vector3{2.0F, 0.0F, 0.0F},
        Math::Vector3{-3.0F, 0.0F, 0.0F}};
    const std::optional<float> nearest =
        ResolveLevelStreamer3DNearestViewerDistanceSquared({0.0F, 0.0F, 0.0F}, viewers);
    ASSERT_TRUE(nearest.has_value());
    EXPECT_FLOAT_EQ(*nearest, 4.0F);
}

TEST(LevelStreamer3DNearestViewerTest, ANonFiniteViewerSkipsRatherThanPoisons) {
    // A camera with a degenerate world pose must not turn the whole decision NaN: it simply
    // stops being a viewer for this tick and the surviving one decides.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::array<Math::Vector3, 3U> viewers{
        Math::Vector3{nan, 0.0F, 0.0F},
        Math::Vector3{4.0F, 0.0F, 0.0F},
        Math::Vector3{0.0F, nan, 0.0F}};
    const std::optional<float> nearest =
        ResolveLevelStreamer3DNearestViewerDistanceSquared({0.0F, 0.0F, 0.0F}, viewers);
    ASSERT_TRUE(nearest.has_value()) << "one finite viewer must still answer";
    EXPECT_FLOAT_EQ(*nearest, 16.0F);

    // Every viewer dead -> no distance at all, as if nobody is watching.
    const std::array<Math::Vector3, 1U> onlyDead{Math::Vector3{nan, nan, nan}};
    EXPECT_FALSE(ResolveLevelStreamer3DNearestViewerDistanceSquared({0.0F, 0.0F, 0.0F}, onlyDead)
                     .has_value());

    // A streamer sitting dead-center in a NaN pose answers nothing either.
    const std::array<Math::Vector3, 1U> aliveViewer{Math::Vector3{1.0F, 0.0F, 0.0F}};
    EXPECT_FALSE(ResolveLevelStreamer3DNearestViewerDistanceSquared({nan, 0.0F, 0.0F}, aliveViewer)
                     .has_value());
}

TEST(LevelStreamer3DActionTest, InvalidAuthoredConfigurationAlwaysDecidesNothing) {
    // loadDistance <= 0, non-finite distances, or no hysteresis band at all: the verdict is a
    // hard None no matter what the viewer does - fail-closed, measured.
    LevelStreamer3DStreamingFrame frame;
    frame.hasViewer = true;
    frame.nearestViewerDistanceSquared = 0.0F;
    frame.enabled = true;

    frame.loadDistance = 0.0F;
    frame.unloadDistance = 10.0F;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingAction(frame), LevelStreamer3DAction::None)
        << "a zero load distance is never a trigger";

    frame.loadDistance = 10.0F;
    frame.unloadDistance = 10.0F;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingAction(frame), LevelStreamer3DAction::None)
        << "unload <= load gives no hysteresis band to believe in";

    frame.loadDistance = std::numeric_limits<float>::infinity();
    frame.unloadDistance = 100.0F;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingAction(frame), LevelStreamer3DAction::None)
        << "non-finite configuration is never a trigger";

    frame.loadDistance = 10.0F;
    frame.unloadDistance = 20.0F;
    frame.nearestViewerDistanceSquared = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(ResolveLevelStreamer3DStreamingAction(frame), LevelStreamer3DAction::None)
        << "a NaN viewer distance is never a trigger";
}

TEST(LevelStreamer3DActionTest, EnteringTheLoadRadiusRequestsLoadLeavingTheUnloadRadiusRequestsUnload) {
    // Happy path: a viewer inside the load radius starts the stream, inside the unload radius
    // keeps it, outside the unload radius ends it.
    LevelStreamer3DStreamingFrame frame;
    frame.hasViewer = true;
    frame.enabled = true;
    frame.loadDistance = 10.0F;
    frame.unloadDistance = 20.0F;

    frame.nearestViewerDistanceSquared = 99.0F; // 9.949... < 10
    EXPECT_EQ(ResolveLevelStreamer3DStreamingAction(frame), LevelStreamer3DAction::RequestLoad);

    frame.loaded = true;
    frame.nearestViewerDistanceSquared = 150.0F; // between 10 and 20: hold the line
    EXPECT_EQ(ResolveLevelStreamer3DStreamingAction(frame), LevelStreamer3DAction::None)
        << "the hysteresis band keeps a loaded level in place";

    frame.nearestViewerDistanceSquared = 401.0F; // 20.02... > 20
    EXPECT_EQ(ResolveLevelStreamer3DStreamingAction(frame), LevelStreamer3DAction::RequestUnload);
}

TEST(LevelStreamer3DActionTest, BoundariesBelongToExactlyOneVerdictEach) {
    // loadDistance is inclusive on approach, unloadDistance is inclusive on retreat: each
    // boundary fires exactly one transition, never both, never neither across them.
    LevelStreamer3DStreamingFrame frame;
    frame.hasViewer = true;
    frame.enabled = true;
    frame.loadDistance = 10.0F;
    frame.unloadDistance = 20.0F;

    frame.nearestViewerDistanceSquared = 100.0F; // d == loadDistance exactly
    EXPECT_EQ(ResolveLevelStreamer3DStreamingAction(frame), LevelStreamer3DAction::RequestLoad);

    frame.loaded = true;
    frame.nearestViewerDistanceSquared = 400.0F; // d == unloadDistance exactly
    EXPECT_EQ(ResolveLevelStreamer3DStreamingAction(frame), LevelStreamer3DAction::RequestUnload);

    // One epsilon INSIDE the band (d strictly between the two): hysteresis seesaws no transitions.
    frame.nearestViewerDistanceSquared = 399.0F;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingAction(frame), LevelStreamer3DAction::None)
        << "loaded and 19.97 from a 20-unload stays loaded";
    frame.loaded = false;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingAction(frame), LevelStreamer3DAction::None)
        << "unloaded and 19.97 from a 10-load stays unloaded";
}

TEST(LevelStreamer3DActionTest, NoViewerNeverMovesAnythingAndDisabledAlwaysUnloads) {
    LevelStreamer3DStreamingFrame frame;
    frame.enabled = true;
    frame.loadDistance = 10.0F;
    frame.unloadDistance = 20.0F;

    // Nobody is watching: neither the load trigger nor a stale unload verdict may move content.
    frame.hasViewer = false;
    frame.nearestViewerDistanceSquared = 0.0F;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingAction(frame), LevelStreamer3DAction::None);
    frame.loaded = true;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingAction(frame), LevelStreamer3DAction::None)
        << "a viewerless tick never unloads a loaded level (the toggle does, not silence)";

    // The latch an async path would set: a pending load must not double-issue on the boundary.
    frame.hasViewer = true;
    frame.loaded = false;
    frame.loadRequested = true;
    frame.nearestViewerDistanceSquared = 99.0F;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingAction(frame), LevelStreamer3DAction::None)
        << "a load already in flight never loads twice";
    frame.loadRequested = false;

    // Disabling a streamer ALWAYS pulls its content, regardless of where the viewer stands.
    frame.enabled = false;
    frame.loaded = true;
    frame.nearestViewerDistanceSquared = 0.0F;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingAction(frame), LevelStreamer3DAction::RequestUnload)
        << "viewer at zero distance still unloads a disabled streamer";
}

TEST(LevelStreamer3DLoadBudgetTest, BudgetIsPositiveAndStructurallySmall) {
    // The per-tick load budget is the load-burst contract: greater than zero is mandatory (a
    // zero budget would wedge streaming forever), structurally bounded so a teleport can never
    // hitch beyond it, and deliberately finite so tests can drive past it and measure carry-over.
    static_assert(kMaximumLevelStreamer3DLoadsPerTick > 0U);
    static_assert(kMaximumLevelStreamer3DLoadsPerTick <= 16U);
    EXPECT_EQ(kMaximumLevelStreamer3DLoadsPerTick, 4U) << "the documented burst budget";
}

TEST(ReflectionProbe3DInfluenceTest, CenterIsOneFaceIsZeroHalfwayIsHalf) {
    // The whole falloff contract in three exact points: the undisturbed center, the face that
    // ends the influence (the boundary itself belongs to the outside), and the geometric halfway.
    const Math::Vector3 half{2.0F, 2.0F, 2.0F};
    EXPECT_FLOAT_EQ(ResolveReflectionProbe3DInfluenceWeight({0.0F, 0.0F, 0.0F}, half), 1.0F);
    EXPECT_FLOAT_EQ(ResolveReflectionProbe3DInfluenceWeight({2.0F, 0.0F, 0.0F}, half), 0.0F)
        << "the face belongs to the outside - hard edges do not leak a floating sliver";
    EXPECT_FLOAT_EQ(ResolveReflectionProbe3DInfluenceWeight({1.0F, 0.0F, 0.0F}, half), 0.5F);
    EXPECT_FLOAT_EQ(ResolveReflectionProbe3DInfluenceWeight({-1.0F, 0.0F, 0.0F}, half), 0.5F)
        << "weight is radially symmetric on each axis";
}

TEST(ReflectionProbe3DInfluenceTest, ChebyshevRuleUsesTheWorstAxisAndAnisotropicBoxesRespectTheirSize) {
    // On {2,4,8} a point sitting (1,1,3) is far past the Y and Z wedges differently: each axis
    // normalizes against its OWN half extent (50%, 25%, 37.5%) and the maximum - the Chebyshev
    // distance - decides. 0.5 is the legal answer, not 0.75, not 0.625.
    const Math::Vector3 half{2.0F, 4.0F, 8.0F};
    EXPECT_FLOAT_EQ(
        ResolveReflectionProbe3DInfluenceWeight({1.0F, 1.0F, 3.0F}, half), 0.5F)
        << "worst-normalized axis wins, every axis readers its own extent";

    // Exactly one tick beyond the face on the shortest axis: the probe is done.
    EXPECT_FLOAT_EQ(
        ResolveReflectionProbe3DInfluenceWeight({2.01F, 0.0F, 0.0F}, half), 0.0F);
}

TEST(ReflectionProbe3DInfluenceTest, DegenerateBoxesAndNonFinitePointsInfluenceNothing) {
    const Math::Vector3 half{2.0F, 2.0F, 2.0F};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FLOAT_EQ(ResolveReflectionProbe3DInfluenceWeight({0.0F, 0.0F, 0.0F}, {0.0F, 2.0F, 2.0F}),
                    0.0F)
        << "a collapsed axis decides nothing - fail-closed instead of dividing by it";
    EXPECT_FLOAT_EQ(ResolveReflectionProbe3DInfluenceWeight({nan, 0.0F, 0.0F}, half), 0.0F);
    EXPECT_FLOAT_EQ(ResolveReflectionProbe3DInfluenceWeight({0.0F, 0.0F, 0.0F}, {nan, 2.0F, 2.0F}),
                    0.0F);
    EXPECT_FLOAT_EQ(
        ResolveReflectionProbe3DInfluenceWeight({std::numeric_limits<float>::infinity(), 0.0F, 0.0F},
                                                   half),
        0.0F);
}

TEST(ReflectionProbe3DCaptureTest, OnceCapturesExactlyOnceAndEveryFrameOnlyCapturesForTheEye) {
    // The Once mode must capture when it has never captured and then stop; EveryFrame must
    // capture only while the probe influences the eye - the measurable saving Godot's Always
    // mode never finds, because a probe influencing nothing re-renders nothing.
    ReflectionProbe3DCaptureFrame frame;
    frame.hasCameraViewer = true;
    frame.cameraInfluenceWeight = 0.5F;
    frame.updateMode = ReflectionProbeUpdateMode::Once;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureAction(frame), ReflectionProbe3DCaptureAction::Capture);
    frame.capturedOnce = true;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureAction(frame), ReflectionProbe3DCaptureAction::None)
        << "Once means once - the second verdict stays quiet";

    frame.capturedOnce = false;
    frame.updateMode = ReflectionProbeUpdateMode::EveryFrame;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureAction(frame), ReflectionProbe3DCaptureAction::Capture);

    frame.cameraInfluenceWeight = 0.0F;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureAction(frame), ReflectionProbe3DCaptureAction::None)
        << "the probe beyond the face does not re-render for an eye it cannot affect";
    frame.cameraInfluenceWeight = 0.5F;

    frame.hasCameraViewer = false;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureAction(frame), ReflectionProbe3DCaptureAction::None)
        << "no camera, no capture - even inside the influence box on paper";
}

TEST(ReflectionProbe3DCaptureTest, OnDemandListensOnlyToItsLatchAndDisabledNeverCaptures) {
    ReflectionProbe3DCaptureFrame frame;
    frame.hasCameraViewer = true;
    frame.cameraInfluenceWeight = 0.5F;
    frame.updateMode = ReflectionProbeUpdateMode::OnDemand;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureAction(frame), ReflectionProbe3DCaptureAction::None)
        << "OnDemand without the latch is silent";
    frame.updateRequested = true;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureAction(frame), ReflectionProbe3DCaptureAction::Capture);

    // A disabled probe answers None under every mode, every latch state.
    frame.enabled = false;
    frame.capturedOnce = false;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureAction(frame), ReflectionProbe3DCaptureAction::None);
    frame.updateMode = ReflectionProbeUpdateMode::Once;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureAction(frame), ReflectionProbe3DCaptureAction::None);
    frame.updateMode = ReflectionProbeUpdateMode::EveryFrame;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureAction(frame), ReflectionProbe3DCaptureAction::None);
}

TEST(ReflectionProbe3DCaptureTest, CorruptUpdateModeAndNaNWeightFailClosed) {
    // Serialization is validation-gated, but a hand-mutated or bit-rotten runtime can still
    // carry an out-of-range update mode or a NaN weight; the verdict must say nothing at all.
    ReflectionProbe3DCaptureFrame frame;
    frame.hasCameraViewer = true;
    frame.cameraInfluenceWeight = 0.5F;
    frame.updateRequested = true;

    frame.updateMode = static_cast<ReflectionProbeUpdateMode>(17U);
    EXPECT_EQ(ResolveReflectionProbe3DCaptureAction(frame), ReflectionProbe3DCaptureAction::None)
        << "an out-of-range mode is never a trigger";

    frame.updateMode = ReflectionProbeUpdateMode::EveryFrame;
    frame.cameraInfluenceWeight = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(ResolveReflectionProbe3DCaptureAction(frame), ReflectionProbe3DCaptureAction::None)
        << "a NaN weight is never a trigger";
}

TEST(ReflectionProbe3DCaptureBudgetTest, BudgetIsPositiveAndStructurallySmall) {
    // Captures cost six scene passes each; the per-tick budget must be usable (> 0) and
    // structurally tiny so carrying over is the norm-ahead behavior, not an exception.
    static_assert(kMaximumReflectionProbeCapturesPerTick > 0U);
    static_assert(kMaximumReflectionProbeCapturesPerTick <= 8U);
    EXPECT_EQ(kMaximumReflectionProbeCapturesPerTick, 2U) << "the documented capture budget";
}

TEST(WorldPartition3DCellIdTest, OriginAndInteriorPointsLandInTheExpectedCells) {
    // The grid origin is the minimum corner: the partition's own position is cell (0,0,0)'s
    // start, and a point clear inside another cell reads its integer coordinates exactly.
    WorldPartition3DComponent config;
    config.cellSize = 10.0F;
    config.cellCounts = {4U, 1U, 4U};

    const Math::Vector3 origin{100.0F, 0.0F, -50.0F};
    const auto atOrigin = ResolveWorldPartition3DCellIdForPosition(config, origin, origin);
    ASSERT_TRUE(atOrigin.has_value());
    EXPECT_EQ(atOrigin->x, 0);
    EXPECT_EQ(atOrigin->y, 0);
    EXPECT_EQ(atOrigin->z, 0);

    // origin + (25, 3, 11): inside cell (2, 0, 1) - every axis divides independently.
    const Math::Vector3 interior{125.0F, 3.0F, -39.0F};
    const auto inside = ResolveWorldPartition3DCellIdForPosition(config, origin, interior);
    ASSERT_TRUE(inside.has_value());
    EXPECT_EQ(inside->x, 2);
    EXPECT_EQ(inside->y, 0);
    EXPECT_EQ(inside->z, 1);
}

TEST(WorldPartition3DCellIdTest, BoundariesBelongToExactlyOneCellEachAndTheOutsideIsUnmanaged) {
    // The floor rule at every edge class: exact cell starts belong to the cell they start,
    // one epsilon before belongs to the previous cell, the volume boundary itself is outside,
    // and anything behind or beyond the grid answers no cell at all (unmanaged: it stays
    // rendered, because volume-rejection must never hide geometry).
    WorldPartition3DComponent config;
    config.cellSize = 10.0F;
    config.cellCounts = {2U, 1U, 1U}; // volume: x in [0,20), y in [0,10), z in [0,10)
    const Math::Vector3 origin{0.0F, 0.0F, 0.0F};

    const auto onBoundary = ResolveWorldPartition3DCellIdForPosition(
        config, origin, Math::Vector3{10.0F, 0.0F, 0.0F});
    ASSERT_TRUE(onBoundary.has_value());
    EXPECT_EQ(onBoundary->x, 1) << "an exact cell start belongs to the cell it starts";

    const auto justBefore = ResolveWorldPartition3DCellIdForPosition(
        config, origin, Math::Vector3{9.9999F, 0.0F, 0.0F});
    ASSERT_TRUE(justBefore.has_value());
    EXPECT_EQ(justBefore->x, 0) << "one epsilon earlier is still the previous cell";

    EXPECT_FALSE(ResolveWorldPartition3DCellIdForPosition(
                     config, origin, Math::Vector3{-0.0001F, 0.0F, 0.0F})
                     .has_value())
        << "behind the origin corner is outside the volume";
    EXPECT_FALSE(ResolveWorldPartition3DCellIdForPosition(
                     config, origin, Math::Vector3{20.0F, 0.0F, 0.0F})
                     .has_value())
        << "the volume's upper bound on an axis is outside it";
    EXPECT_FALSE(ResolveWorldPartition3DCellIdForPosition(
                     config, origin, Math::Vector3{5.0F, 15.0F, 5.0F})
                     .has_value())
        << "inside on two axes but outside the short third is still outside";
}

TEST(WorldPartition3DCellIdTest, DegenerateConfigAndNonFinitePosesNeverAnswerACell) {
    WorldPartition3DComponent broken;
    broken.cellSize = 0.0F; // never valid
    EXPECT_FALSE(ResolveWorldPartition3DCellIdForPosition(
                     broken, Math::Vector3{}, Math::Vector3{})
                     .has_value());

    WorldPartition3DComponent valid;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(ResolveWorldPartition3DCellIdForPosition(
                     valid, Math::Vector3{}, Math::Vector3{nan, 0.0F, 0.0F})
                     .has_value());
    EXPECT_FALSE(ResolveWorldPartition3DCellIdForPosition(
                     valid, Math::Vector3{0.0F, nan, 0.0F}, Math::Vector3{})
                     .has_value())
        << "a NaN grid origin manages nothing either";
}

TEST(WorldPartition3DCellLinearIndexTest, IndexingIsDeterministicAndXMajor) {
    // The tie-break key consumers sort cells by: a fixed, space-filling index so two runs always
    // admit the same budget candidates even when distances tie exactly.
    const std::array<std::uint32_t, 3U> counts{4U, 2U, 3U};
    EXPECT_EQ(ResolveWorldPartition3DCellLinearIndex({0, 0, 0}, counts), 0U);
    EXPECT_EQ(ResolveWorldPartition3DCellLinearIndex({3, 0, 0}, counts), 3U);
    EXPECT_EQ(ResolveWorldPartition3DCellLinearIndex({0, 1, 0}, counts), 4U) << "y strides by cx";
    EXPECT_EQ(ResolveWorldPartition3DCellLinearIndex({0, 0, 1}, counts), 8U) << "z strides by cx*cy";
    EXPECT_EQ(ResolveWorldPartition3DCellLinearIndex({2, 1, 2}, counts), 2U + 4U + 16U);
}

TEST(WorldPartition3DMembershipLiveTest, LiveOwnerTrustsItsFlagDeadOwnerFailsOpen) {
    // The renderer-side contract measured in isolation: while the partition exists, the live
    // flag rules; once it is gone, nobody may hide content with a stale opinion.
    EXPECT_FALSE(ResolveWorldPartition3DMembershipLive(/*partitionOwnerAlive=*/true, false))
        << "a live partition's fade verdict rules";
    EXPECT_TRUE(ResolveWorldPartition3DMembershipLive(true, true));
    EXPECT_TRUE(ResolveWorldPartition3DMembershipLive(/*partitionOwnerAlive=*/false, false))
        << "dead partition: fail open, never delete the world by stale flag";
    EXPECT_TRUE(ResolveWorldPartition3DMembershipLive(false, true));
}

TEST(VisibilityRegion3DContainmentTest, InteriorAndBoundaryPointsAreInsideTheOutsideIsNot) {
    // The whole room-culling contract rests on one question, so measure it: where is inside?
    // A region at the origin with half extents {2, 1, 4}; the boundary itself is INCLUDED on
    // purpose (unlike the world partition cells, a single box's skin belongs to itself exactly
    // once), and the outside never is.
    VisibilityRegion3DComponent config;
    config.halfExtents = Math::Vector3{2.0F, 1.0F, 4.0F};
    const Math::Vector3 origin{};

    EXPECT_TRUE(ResolveVisibilityRegion3DContainsPoint(config, origin, Math::Vector3{}))
        << "the center is as inside as it gets";
    EXPECT_TRUE(
        ResolveVisibilityRegion3DContainsPoint(config, origin, Math::Vector3{1.5F, 0.5F, -3.5F}))
        << "a routine interior point";
    EXPECT_TRUE(
        ResolveVisibilityRegion3DContainsPoint(config, origin, Math::Vector3{2.0F, 1.0F, 4.0F}))
        << "the exact corner is INCLUDED - paint a mesh on the wall and it is managed";
    EXPECT_TRUE(
        ResolveVisibilityRegion3DContainsPoint(config, origin, Math::Vector3{-2.0F, -1.0F, -4.0F}))
        << "the opposite corner too";
    EXPECT_FALSE(
        ResolveVisibilityRegion3DContainsPoint(config, origin, Math::Vector3{2.01F, 0.0F, 0.0F}))
        << "one millimetre past the wall is outside - the box is not elastic";
    EXPECT_FALSE(
        ResolveVisibilityRegion3DContainsPoint(config, origin, Math::Vector3{0.0F, -1.01F, 0.0F}));

    // A second origin proves this is region-relative, not world-relative:
    const Math::Vector3 room{100.0F, 0.0F, 0.0F};
    EXPECT_TRUE(ResolveVisibilityRegion3DContainsPoint(config, room,
                                                          Math::Vector3{101.0F, 0.0F, 0.0F}));
    EXPECT_FALSE(
        ResolveVisibilityRegion3DContainsPoint(config, room, Math::Vector3{}))
        << "the origin is outside the room at x=100";
}

TEST(VisibilityRegion3DContainmentTest, DegenerateConfigAndNonFinitePosesContainNothing) {
    // An unusable region manages NOTHING: zero half extent, negative extents, or a NaN pose all
    // answer false, so a typo in the inspector can never silently absorb a corridor.
    VisibilityRegion3DComponent flat;
    flat.halfExtents = Math::Vector3{0.0F, 1.0F, 1.0F};
    EXPECT_FALSE(ResolveVisibilityRegion3DContainsPoint(flat, Math::Vector3{},
                                                           Math::Vector3{}));

    VisibilityRegion3DComponent negative;
    negative.halfExtents = Math::Vector3{-2.0F, 1.0F, 1.0F};
    EXPECT_FALSE(ResolveVisibilityRegion3DContainsPoint(negative, Math::Vector3{},
                                                           Math::Vector3{}));

    VisibilityRegion3DComponent fine;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(ResolveVisibilityRegion3DContainsPoint(
        fine, Math::Vector3{}, Math::Vector3{nan, 0.0F, 0.0F}));
    EXPECT_FALSE(ResolveVisibilityRegion3DContainsPoint(
        fine, Math::Vector3{nan, 0.0F, 0.0F}, Math::Vector3{}));
}

TEST(VisibilityRegion3DLayerGateTest, AnySharedBitManagesAndZeroMasksManageNothing) {
    // Layer gating must be an unsigned mask intersection, measured on corners: default-on-
    // default passes, disjoint masks close the gate, and a ZERO mask on either side manages
    // nothing (a mesh on no layers is never managed - an authoring choice, not an error).
    EXPECT_TRUE(ResolveVisibilityRegion3DLayerGate(0xFFFFFFFFU, 0x00000001U))
        << "region-everything + mesh-layer-0: the out-of-the-box behaviour";
    EXPECT_TRUE(ResolveVisibilityRegion3DLayerGate(0x00000002U, 0x00000002U))
        << "both on layer 1 alone";
    EXPECT_TRUE(ResolveVisibilityRegion3DLayerGate(0x0000000AU, 0x00000008U))
        << "any ONE shared bit is enough";
    EXPECT_FALSE(ResolveVisibilityRegion3DLayerGate(0x00000002U, 0x00000001U))
        << "props-layer region does not manage an NPC on the default layer";
    EXPECT_FALSE(ResolveVisibilityRegion3DLayerGate(0U, 0xFFFFFFFFU))
        << "a region masking zero layers manages nothing";
    EXPECT_FALSE(ResolveVisibilityRegion3DLayerGate(0xFFFFFFFFU, 0U))
        << "a mesh on zero layers is managed by nothing";
}

TEST(VisibilityRegion3DViewerTest, AnyViewerInsideActivatesAndAnEmptyCloudDoesNot) {
    // The pure half of the fail-open rule: any one viewer activates the region; an EMPTY cloud
    // answers false here (the engine's policy turns that into "show everything" above this
    // function, keeping the pure answer total).
    VisibilityRegion3DComponent config;
    config.halfExtents = Math::Vector3{2.0F, 2.0F, 2.0F};
    const Math::Vector3 origin{};

    EXPECT_FALSE(ResolveVisibilityRegion3DAnyViewerInside(
        config, origin, std::vector<Math::Vector3>{}));
    EXPECT_TRUE(ResolveVisibilityRegion3DAnyViewerInside(
        config, origin, std::vector<Math::Vector3>{Math::Vector3{1.0F, 0.0F, 0.0F}}));
    EXPECT_FALSE(ResolveVisibilityRegion3DAnyViewerInside(
        config, origin,
        std::vector<Math::Vector3>{Math::Vector3{50.0F, 0.0F, 0.0F},
                                      Math::Vector3{-50.0F, 0.0F, 0.0F}}))
        << "two viewers, neither nearby";
    EXPECT_TRUE(ResolveVisibilityRegion3DAnyViewerInside(
        config, origin,
        std::vector<Math::Vector3>{Math::Vector3{50.0F, 0.0F, 0.0F},
                                      Math::Vector3{0.0F, 0.0F, 0.0F}}))
        << "one step over the room line activates for the whole party";
}

TEST(VisibilityRegion3DMembershipLiveTest, LiveOwnerTrustsItsFlagDeadOwnerFailsOpen) {
    // The renderer-side contract, same shape the world partition keeps: while the region owns
    // a membership the live flag rules; once the owner is gone nobody may hide content with its
    // stale opinion.
    EXPECT_FALSE(ResolveVisibilityRegion3DMembershipLive(/*regionOwnerAlive=*/true, false))
        << "inactive region: interior content is skipped";
    EXPECT_TRUE(ResolveVisibilityRegion3DMembershipLive(true, true));
    EXPECT_TRUE(ResolveVisibilityRegion3DMembershipLive(/*regionOwnerAlive=*/false, false))
        << "dead region: fail open, never leave a door closed behind a deleted room";
    EXPECT_TRUE(ResolveVisibilityRegion3DMembershipLive(false, true));
}

TEST(Occluder3DFullyHiddenTest, BehindTheWallIsHiddenInFrontBesideAndBeyondOverReachAreNot) {
    // The single strict rule, measured on a wall at the origin with half extents {2,1,2}: the
    // viewer at z=+5 looking down -z. A point on the far side of the wall, on the segment
    // through the wall's interior, is hidden; everything else draws.
    Occluder3DComponent wall;
    wall.halfExtents = Math::Vector3{2.0F, 1.0F, 2.0F};
    const Math::Vector3 wallOrigin{};
    const Math::Vector3 viewer{0.0F, 0.0F, 5.0F};

    EXPECT_TRUE(ResolveOccluder3DFullyHidden(wall, wallOrigin, viewer,
                                                Math::Vector3{0.0F, 0.0F, -5.0F}))
        << "dead center behind the wall: hidden";
    EXPECT_TRUE(ResolveOccluder3DFullyHidden(wall, wallOrigin, viewer,
                                                Math::Vector3{1.0F, 0.5F, -30.0F}))
        << "off-axis behind the wall, still covered: hidden";
    EXPECT_FALSE(ResolveOccluder3DFullyHidden(wall, wallOrigin, viewer,
                                                 Math::Vector3{0.0F, 0.0F, 3.0F}))
        << "in FRONT of the wall: nothing was hidden";
    EXPECT_FALSE(ResolveOccluder3DFullyHidden(wall, wallOrigin, viewer,
                                                 Math::Vector3{8.0F, 0.0F, -5.0F}))
        << "beside the wall on the far side: the segment clears the box (minded cone math:"
           " at x=8 the ray exits the wall's x-slab before ever entering its z-slab)";
    EXPECT_FALSE(ResolveOccluder3DFullyHidden(wall, wallOrigin,
                                                 Math::Vector3{20.0F, 0.0F, 5.0F},
                                                 Math::Vector3{0.0F, 0.0F, -5.0F}))
        << "viewer beside the wall, same answer - the cover is directional";
}

TEST(Occluder3DFullyHiddenTest, HonestAmbiguitiesAllFailOpen) {
    // The no-false-culls edge list, each case measured: grazing the exact skin, standing on the
    // surface, standing inside, the box exactly AT the candidate, an invalid box, a NaN pose -
    // every one of them answers NOT hidden.
    Occluder3DComponent wall;
    wall.halfExtents = Math::Vector3{2.0F, 1.0F, 2.0F};
    const Math::Vector3 wallOrigin{};
    const Math::Vector3 viewer{0.0F, 0.0F, 5.0F};

    EXPECT_FALSE(ResolveOccluder3DFullyHidden(wall, wallOrigin, viewer,
                                                 Math::Vector3{4.0F, 0.0F, -1.0F}))
        << "true tangency: the segment kisses the far corner (2,0,2) exactly once - grazing is"
           " no cover, the strict-overlap rule fails open at the boundary";
    EXPECT_FALSE(ResolveOccluder3DFullyHidden(wall, wallOrigin, viewer,
                                                 Math::Vector3{0.0F, 0.0F, -2.0F}))
        << "the candidate stands ON the back face (the wall hides what is BEHIND, not at it)";
    EXPECT_FALSE(ResolveOccluder3DFullyHidden(wall, wallOrigin, viewer,
                                                 Math::Vector3{0.0F, 0.0F, 0.5F}))
        << "inside the box is never hidden by the box";
    EXPECT_FALSE(ResolveOccluder3DFullyHidden(wall, wallOrigin,
                                                 Math::Vector3{0.0F, 0.0F, 1.0F},
                                                 Math::Vector3{0.0F, 0.0F, -5.0F}))
        << "the viewer inside the cover sees past it";

    Occluder3DComponent degenerate;
    degenerate.halfExtents = Math::Vector3{0.0F, 1.0F, 1.0F};
    EXPECT_FALSE(ResolveOccluder3DFullyHidden(degenerate, wallOrigin, viewer,
                                                 Math::Vector3{0.0F, 0.0F, -5.0F}))
        << "a zero-half-extent cover covers nothing";

    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(ResolveOccluder3DFullyHidden(wall, wallOrigin, viewer,
                                                 Math::Vector3{nan, 0.0F, -5.0F}));
    EXPECT_FALSE(ResolveOccluder3DFullyHidden(wall, wallOrigin,
                                                 Math::Vector3{0.0F, 0.0F, nan},
                                                 Math::Vector3{0.0F, 0.0F, -5.0F}));
}

TEST(Occluder3DFullyHiddenTest, CoverIsAboutTheSegmentNotAboutDistance) {
    // Distance is not cover: a candidate VERY far behind but off the wall's silhouette draws,
    // and one centimetre behind the wall on-axis is hidden - the box casts an exact
    // infinite-depth shadow down the segment, nothing shorter, nothing longer.
    Occluder3DComponent wall;
    wall.halfExtents = Math::Vector3{1.0F, 1.0F, 1.0F};
    const Math::Vector3 wallOrigin{};
    const Math::Vector3 viewer{0.0F, 0.0F, 5.0F};

    EXPECT_TRUE(ResolveOccluder3DFullyHidden(wall, wallOrigin, viewer,
                                                Math::Vector3{0.0F, 0.0F, -1.01F}))
        << "2 cm behind the back face is already hidden";
    EXPECT_TRUE(ResolveOccluder3DFullyHidden(wall, wallOrigin, viewer,
                                                Math::Vector3{0.0F, 0.0F, -10000.0F}))
        << "and the shadow has no draw-distance";
    EXPECT_TRUE(ResolveOccluder3DFullyHidden(wall, wallOrigin, viewer,
                                                Math::Vector3{1.5F, 0.0F, -10000.0F}))
        << "perspective narrows at range: even far-off-axis points fall under the cover";
    EXPECT_FALSE(ResolveOccluder3DFullyHidden(wall, wallOrigin, viewer,
                                                 Math::Vector3{3000.0F, 0.0F, -10000.0F}))
        << "10 km away outside the silhouette cone: still drawn";
}

TEST(Object3DCommonHelpersTest, PositiveVectorNormalizableQuaternionAndWorldComposition) {
    EXPECT_TRUE(IsPositive3DObjectVector(Math::Vector3{1.0F, 2.0F, 3.0F}));
    EXPECT_FALSE(IsPositive3DObjectVector(Math::Vector3{1.0F, 0.0F, 3.0F}));
    EXPECT_TRUE(IsNormalizable3DObjectQuaternion(Math::Quaternion{}));
    EXPECT_FALSE(IsNormalizable3DObjectQuaternion(Math::Quaternion{0.0F, 0.0F, 0.0F, 0.0F}));
    EXPECT_TRUE(Is3DObjectLayerMaskNonZero(1U));
    EXPECT_FALSE(Is3DObjectLayerMaskNonZero(0U));

    const Math::Quaternion quarterTurnY{0.0F, 0.70710678F, 0.0F, 0.70710678F};
    const std::optional<Math::Vector3> worldPoint =
        Compose3DObjectWorldPoint(Math::Vector3{10.0F, 2.0F, 0.0F}, quarterTurnY,
                                     Math::Vector3{1.0F, 0.0F, 0.0F});
    ASSERT_TRUE(worldPoint.has_value());
    EXPECT_NEAR(worldPoint->x, 10.0F, 1e-4F);
    EXPECT_NEAR(worldPoint->y, 2.0F, 1e-4F);
    EXPECT_NEAR(worldPoint->z, -1.0F, 1e-4F);

    const std::optional<Math::Quaternion> worldRot =
        Compose3DObjectWorldRotation(quarterTurnY, Math::Quaternion{});
    ASSERT_TRUE(worldRot.has_value());
    EXPECT_NEAR(worldRot->y, 0.70710678F, 1e-4F);
    EXPECT_NEAR(worldRot->w, 0.70710678F, 1e-4F);
}

TEST(MeshAndPrimitiveNodePoliciesTest, MeshInstanceBoxSphereAndPlanePolicies) {
    MeshComponent mesh{};
    EXPECT_FALSE(MeshInstance3D::HasAssignedMesh(mesh));
    EXPECT_FALSE(MeshInstance3D::HasAssignedMaterial(mesh));
    mesh.meshGuid = Asset::AssetGuid{11ULL};
    mesh.materialGuid = Asset::AssetGuid{22ULL};
    EXPECT_TRUE(MeshInstance3D::HasAssignedMesh(mesh));
    EXPECT_TRUE(MeshInstance3D::HasAssignedMaterial(mesh));
    EXPECT_EQ(MeshInstance3D::ResolveEffectiveMaterialGuid(mesh, Asset::kInvalidAssetGuid),
              Asset::AssetGuid{22ULL});
    EXPECT_EQ(MeshInstance3D::ResolveEffectiveMaterialGuid(mesh, Asset::AssetGuid{99ULL}),
              Asset::AssetGuid{99ULL});
    EXPECT_TRUE(MeshInstance3D::HasRenderableAssetPair(
        Asset::AssetGuid{11ULL}, Asset::AssetGuid{22ULL}));
    EXPECT_FALSE(MeshInstance3D::HasRenderableAssetPair(
        Asset::kInvalidAssetGuid, Asset::AssetGuid{22ULL}));

    SurfaceInstanceComponent surface{};
    surface.materialOverridePath = "materials/override.uvmaterial";
    EXPECT_TRUE(MeshInstance3D::HasSurfaceMaterialOverride(&surface));
    EXPECT_TRUE(MeshInstance3D::ParticipatesInPass(&surface, /*shadowPass=*/false));
    EXPECT_TRUE(MeshInstance3D::ParticipatesInPass(&surface, /*shadowPass=*/true));
    // The per-surface list answers for one surface at a time, and a whole-object override mutes the
    // whole list rather than being merged with it.
    SurfaceInstanceComponent perSurface{};
    EXPECT_FALSE(MeshInstance3D::HasPerSurfaceMaterialOverride(&perSurface));
    EXPECT_FALSE(MeshInstance3D::HasPerSurfaceMaterialOverride(nullptr));
    perSurface.surfaceMaterialOverridePaths = {"", "materials/slot_one.uvmaterial"};
    EXPECT_TRUE(MeshInstance3D::HasPerSurfaceMaterialOverride(&perSurface));
    EXPECT_TRUE(MeshInstance3D::SurfaceMaterialOverridePath(perSurface, 0U).empty());
    EXPECT_EQ(MeshInstance3D::SurfaceMaterialOverridePath(perSurface, 1U),
              "materials/slot_one.uvmaterial");
    EXPECT_TRUE(MeshInstance3D::SurfaceMaterialOverridePath(perSurface, 2U).empty());
    perSurface.materialOverridePath = "materials/all.uvmaterial";
    EXPECT_TRUE(MeshInstance3D::SurfaceMaterialOverridePath(perSurface, 1U).empty());
    EXPECT_FALSE(MeshInstance3D::HasPerSurfaceMaterialOverride(&perSurface));

    surface.castShadow = SurfaceShadowMode::ShadowsOnly;
    EXPECT_FALSE(MeshInstance3D::ParticipatesInPass(&surface, /*shadowPass=*/false));
    EXPECT_TRUE(MeshInstance3D::ParticipatesInPass(&surface, /*shadowPass=*/true));

    // BoxMesh3D
    const BoxMesh3DObjectDefinition boxDef{};
    EXPECT_TRUE(BoxMesh3D::IsBoxMesh(boxDef.mesh));
    EXPECT_FLOAT_EQ(BoxMesh3D::VolumeFromScale(Math::Vector3{2.0F, 3.0F, 4.0F}), 24.0F);
    EXPECT_FLOAT_EQ(BoxMesh3D::VolumeFromScale(Math::Vector3{0.0F, 3.0F, 4.0F}), 0.0F);

    // SphereMesh3D
    const SphereMesh3DObjectDefinition sphereDef{};
    EXPECT_TRUE(SphereMesh3D::IsSphereMesh(sphereDef.mesh));
    EXPECT_FLOAT_EQ(SphereMesh3D::EffectiveRadiusFromScale(Math::Vector3{2.0F, 6.0F, 4.0F}), 3.0F);

    // PlaneMesh3D
    const PlaneMesh3DObjectDefinition planeDef{};
    EXPECT_TRUE(PlaneMesh3D::IsPlaneMesh(planeDef.mesh));
    EXPECT_FLOAT_EQ(PlaneMesh3D::SurfaceAreaFromScale(Math::Vector3{5.0F, 1.0F, 4.0F}), 20.0F);
    const Math::Vector3 normal = PlaneMesh3D::WorldNormal(Math::Quaternion{});
    EXPECT_NEAR(normal.y, 1.0F, 1e-5F);
}

TEST(LightAudioParticleAndScriptNodePoliciesTest, DomainRulesMatchRuntimeSystems) {
    // Light3D
    LightComponent light{};
    light.type = LightType::Spot;
    light.intensity = 2.0F;
    light.range = 10.0F;
    light.spotAngleDegrees = 60.0F;
    EXPECT_TRUE(Light3D::IsEmitting(light));
    EXPECT_NEAR(Light3D::ComputeRec709Luminance(Math::Color{1.0F, 1.0F, 1.0F}), 1.0F, 1e-4F);
    const Math::Vector3 forward = Light3D::ResolveWorldDirection(Math::Quaternion{});
    EXPECT_NEAR(forward.z, -1.0F, 1e-5F);
    EXPECT_FLOAT_EQ(Light3D::EvaluateSpotConeWeight(60.0F, forward, forward), 1.0F);
    EXPECT_FLOAT_EQ(Light3D::EvaluateSpotConeWeight(
                        60.0F, forward, Math::Vector3{1.0F, 0.0F, 0.0F}),
                    0.0F);
    EXPECT_TRUE(std::isinf(Light3D::EstimateViewContribution(
        LightType::Directional, light.color, 1.0F, 10.0F, {}, {})));

    // AudioSource3D
    AudioSourceComponent audio{};
    EXPECT_FALSE(AudioSource3D::IsPlayable(audio));
    audio.audioAssetPath = "sfx/step.wav";
    audio.playOnAwake = true;
    audio.spatial = true;
    audio.minDistance = 2.0F;
    audio.maxDistance = 12.0F;
    audio.attenuationCurve = AudioAttenuationCurve::Linear;
    EXPECT_TRUE(AudioSource3D::IsPlayable(audio));
    EXPECT_TRUE(AudioSource3D::ShouldAutoPlayOnAwake(audio));
    EXPECT_EQ(AudioSource3D::ResolveEffectiveMixerGroup(audio, "Master"), "Master");
    EXPECT_FLOAT_EQ(AudioSource3D::EvaluateDistanceGain(audio, 1.0F), 1.0F);
    EXPECT_NEAR(AudioSource3D::EvaluateDistanceGain(audio, 7.0F), 0.5F, 1e-4F);
    EXPECT_FLOAT_EQ(AudioSource3D::EvaluateDistanceGain(audio, 15.0F), 0.0F);
    AudioSourceComponent changedAudio = audio;
    changedAudio.looping = !audio.looping;
    EXPECT_TRUE(AudioSource3D::RequiresVoiceReplacement(audio, changedAudio));

    // ParticleEmitter3D
    ParticleEmitterComponent emitter{};
    emitter.maxParticles = 10U;
    emitter.emissionRate = 20.0F;
    emitter.lifetimeSeconds = 2.0F;
    emitter.emitting = true;
    EXPECT_TRUE(ParticleEmitter3D::IsEmitting(emitter));
    EXPECT_FLOAT_EQ(ParticleEmitter3D::SpawnIntervalSeconds(emitter), 0.05F);
    float remainder = 0.0F;
    EXPECT_EQ(ParticleEmitter3D::AccumulateSpawnCount(emitter, 0.125F, remainder, 0U), 2U);
    EXPECT_NEAR(remainder, 0.5F, 1e-4F);
    const Math::Vector3 steppedVel = ParticleEmitter3D::StepVelocity(
        Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, -9.8F, 0.0F}, 0.5F);
    EXPECT_NEAR(steppedVel.y, -4.9F, 1e-4F);
    EXPECT_FLOAT_EQ(ParticleEmitter3D::NormalizedAge(emitter, 1.0F), 0.5F);

    // Script
    ScriptComponent script{};
    EXPECT_FALSE(Script::HasScriptPath(script));
    script.scriptAssetPath = "scripts/player_controller.uvs";
    EXPECT_TRUE(Script::HasScriptPath(script));
    EXPECT_TRUE(Script::IsRunnableUVScript(script));
    ASSERT_TRUE(Script::TrySetExportedProperty(script, "moveSpeed", "6.5"));
    ASSERT_NE(Script::FindExportedProperty(script, "moveSpeed"), nullptr);
    EXPECT_EQ(*Script::FindExportedProperty(script, "moveSpeed"), "6.5");
    EXPECT_TRUE(Script::RemoveExportedProperty(script, "moveSpeed"));
    EXPECT_EQ(Script::FindExportedProperty(script, "moveSpeed"), nullptr);
}

TEST(MarkerLevelStreamerNavAndAnimationNodePoliciesTest, DomainHelpersRespectContracts) {
    // Marker3D
    Marker3DComponent marker{};
    marker.markerName = "BossArenaCenter";
    marker.localPosition = Math::Vector3{0.0F, 2.0F, -3.0F};
    EXPECT_TRUE(Marker3D::IsQueryable(marker));
    EXPECT_TRUE(Marker3D::MatchesNameFilter(marker, ""));
    EXPECT_TRUE(Marker3D::MatchesNameFilter(marker, "BossArenaCenter"));
    EXPECT_FALSE(Marker3D::MatchesNameFilter(marker, "Other"));
    const std::optional<Marker3DPose> pose =
        Marker3D::ResolveWorldPose(marker, Math::Vector3{10.0F, 0.0F, 0.0F}, Math::Quaternion{});
    ASSERT_TRUE(pose.has_value());
    EXPECT_FLOAT_EQ(pose->position.x, 10.0F);
    EXPECT_FLOAT_EQ(pose->position.y, 2.0F);
    EXPECT_FLOAT_EQ(pose->position.z, -3.0F);
    EXPECT_NEAR(Marker3D::ForwardDirection(*pose).z, -1.0F, 1e-5F);
    EXPECT_NEAR(Marker3D::UpDirection(*pose).y, 1.0F, 1e-5F);

    // LevelStreamer3D
    LevelStreamer3DComponent streamer{};
    streamer.levelPath = "levels/dungeon.uvscene";
    streamer.loadDistance = 100.0F;
    streamer.unloadDistance = 150.0F;
    streamer.enabled = true;
    EXPECT_FLOAT_EQ(LevelStreamer3D::HysteresisBandWidth(streamer), 50.0F);
    const std::array<Math::Vector3, 1> closeViewer{Math::Vector3{50.0F, 0.0F, 0.0F}};
    EXPECT_EQ(LevelStreamer3D::EvaluateAction(streamer, Math::Vector3{}, closeViewer),
              LevelStreamer3DAction::RequestLoad);

    // NavMeshVolume3D & NavSeeker3D
    NavMeshVolume3DComponent volume{};
    volume.boundsHalfExtents = Math::Vector3{10.0F, 2.0F, 10.0F};
    volume.navigationLayers = 0b0010U;
    volume.rebuildRequested = true;
    EXPECT_TRUE(NavMeshVolume3D::IsBakingEnabled(volume));
    EXPECT_TRUE(NavMeshVolume3D::ContainsWorldPoint(volume, Math::Vector3{}, Math::Vector3{5.0F, 1.0F, -5.0F}));
    EXPECT_FALSE(NavMeshVolume3D::ContainsWorldPoint(volume, Math::Vector3{}, Math::Vector3{15.0F, 1.0F, 0.0F}));
    EXPECT_TRUE(NavMeshVolume3D::SharesNavigationLayer(volume, 0b0011U));
    EXPECT_TRUE(NavMeshVolume3D::ConsumeRebuildRequest(volume));
    EXPECT_FALSE(volume.rebuildRequested);

    NavSeeker3DComponent seeker{};
    ASSERT_TRUE(NavSeeker3D::TrySetTargetPosition(seeker, Math::Vector3{3.0F, 0.0F, 4.0F}));
    EXPECT_NEAR(NavSeeker3D::PlanarDistanceToTarget(seeker, Math::Vector3{}), 5.0F, 1e-4F);
    EXPECT_FALSE(NavSeeker3D::IsWithinTargetTolerance(seeker, Math::Vector3{}));
    EXPECT_TRUE(NavSeeker3D::IsWithinTargetTolerance(seeker, Math::Vector3{2.5F, 0.0F, 3.5F}));
    NavSeeker3D::PublishIdleState(seeker, Math::Vector3{1.0F, 2.0F, 3.0F});
    EXPECT_EQ(seeker.pathStatus, NavigationAgentPathStatus::Idle);
    EXPECT_EQ(seeker.nextPathPosition, (Math::Vector3{1.0F, 2.0F, 3.0F}));

    // Abstract animation helpers
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    const Entity ikEntity = entityManager.CreateEntity();
    ApplyBoneModifier3DBase(entityManager, ikEntity, "IK");
    EXPECT_FLOAT_EQ(ResolveBoneModifierInfluence(entityManager, ikEntity), 1.0F);
    EXPECT_TRUE(IsBoneModifierActive(entityManager, ikEntity));
    entityManager.GetComponent<BoneModifierComponent>(ikEntity).influence = 0.0F;
    EXPECT_FALSE(IsBoneModifierActive(entityManager, ikEntity));

    const Entity parentEntity = entityManager.CreateEntity();
    const Entity driverEntity = entityManager.CreateEntity();
    ApplyAnimationDriverBase(entityManager, driverEntity, "Driver");
    entityManager.GetComponent<HierarchyComponent>(driverEntity).parent = parentEntity;
    const AnimationDriverComponent driver = ResolveAnimationDriver(entityManager, driverEntity);
    EXPECT_TRUE(IsAnimationDriverRunningOnClock(driver, AnimationProcessCallback::Frame));
    EXPECT_EQ(ResolveAnimationDriverTargetEntity(entityManager, driverEntity, driver), parentEntity);
}

TEST(Expanded3DObjectComponentsTest, ComponentAndObjectHelpersValidateSanitizeAndEvaluateCorrectly) {
    // EditorInternalEntityComponent
    const EditorInternalEntityComponent internalTag{};
    EXPECT_TRUE(IsEditorInternalEntityComponentValid(internalTag));
    EXPECT_TRUE(IsEditorInternalEntityTagPresent(&internalTag));
    EXPECT_FALSE(ShouldIncludeInDocumentHierarchy(&internalTag));
    EXPECT_TRUE(ShouldIncludeInDocumentHierarchy(nullptr));
    EXPECT_FALSE(ShouldSerializeDocumentEntity(&internalTag));
    EXPECT_FALSE(ShouldClearWithDocumentScene(&internalTag));

    // WorldTransformComponent
    TransformComponent local{};
    local.localPosition = Math::Vector3{1.0F, 2.0F, 3.0F};
    local.localScale = Math::Vector3{2.0F, 2.0F, 2.0F};
    const WorldTransformComponent rootWorld = MakeWorldTransformFromLocal(local);
    EXPECT_TRUE(IsWorldTransformComponentValid(rootWorld));
    EXPECT_TRUE(HasSameWorldPose(rootWorld, rootWorld));
    const Math::Vector3 transformed =
        TransformWorldPoint(rootWorld, Math::Vector3{1.0F, 0.0F, 0.0F});
    EXPECT_FLOAT_EQ(transformed.x, 3.0F);
    Math::Vector3 roundTrip{};
    ASSERT_TRUE(TryInverseTransformWorldPoint(rootWorld, transformed, roundTrip));
    EXPECT_NEAR(roundTrip.x, 1.0F, 1e-5F);
    EXPECT_NEAR(ResolveWorldForward(rootWorld).z, -1.0F, 1e-5F);

    // CanvasComponent
    CanvasComponent canvas{.visible = true, .sortOrder = 50000};
    EXPECT_FALSE(IsCanvasSortOrderInRange(canvas.sortOrder));
    const CanvasComponent sanitizedCanvas = SanitizeCanvasComponent(canvas);
    EXPECT_EQ(sanitizedCanvas.sortOrder, kMaximumCanvasSortOrder);
    EXPECT_TRUE(ResolveInheritedCanvasVisibility(&sanitizedCanvas, true));
    EXPECT_FALSE(ResolveInheritedCanvasVisibility(&sanitizedCanvas, false));

    // BoneModifierComponent
    BoneModifierComponent boneMod{.active = true, .influence = 0.5F};
    EXPECT_TRUE(IsBoneModifierEffective(boneMod));
    const Math::Vector3 blendedPos = BlendBoneModifierPosition(
        boneMod, Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{10.0F, 0.0F, 0.0F});
    EXPECT_FLOAT_EQ(blendedPos.x, 5.0F);

    // RenderInstanceComponent & MeshComponent & SurfaceInstanceComponent
    RenderInstanceComponent renderInst{};
    SetRenderInstanceLayerBit(renderInst, 3U, true);
    EXPECT_TRUE(IsRenderInstanceOnLayer(renderInst, 3U));
    EXPECT_TRUE(MatchesRenderInstanceLayerMask(renderInst, (1U << 3U)));
    renderInst.sortingOffset = 2.5F;
    EXPECT_FLOAT_EQ(ComputeRenderInstanceSortDepth(renderInst, 10.0F), 12.5F);

    MeshComponent meshComp{};
    meshComp.materialGuid = Asset::AssetGuid{42ULL};
    EXPECT_FALSE(IsMeshComponentValid(meshComp));
    const MeshComponent sanitizedMesh = SanitizeMeshComponent(meshComp);
    EXPECT_TRUE(IsMeshComponentValid(sanitizedMesh));
    EXPECT_FALSE(HasAssignedMaterialAsset(sanitizedMesh));

    SurfaceInstanceComponent surfaceComp{};
    surfaceComp.transparency = 0.25F;
    surfaceComp.visibilityRangeBegin = 5.0F;
    surfaceComp.visibilityRangeEnd = 20.0F;
    EXPECT_FLOAT_EQ(ResolveSurfaceInstanceOpacity(surfaceComp), 0.75F);
    EXPECT_TRUE(CastsSurfaceShadow(surfaceComp));
    EXPECT_TRUE(DrawsMainPassSurfaceGeometry(surfaceComp));
    EXPECT_TRUE(IsDistanceWithinSurfaceVisibilityRange(surfaceComp, 10.0F));
    EXPECT_FALSE(IsDistanceWithinSurfaceVisibilityRange(surfaceComp, 25.0F));

    // PhysicsObjectComponent
    PhysicsObjectComponent physA{.disableMode = PhysicsObjectDisableMode::MakeStatic, .collisionPriority = 3.0F};
    PhysicsObjectComponent physB{.disableMode = PhysicsObjectDisableMode::Remove, .collisionPriority = 1.0F};
    EXPECT_TRUE(ShouldKeepPhysicsBodyInWorldWhenDisabled(physA));
    EXPECT_FALSE(ShouldSimulatePhysicsBodyWhenDisabled(physA));
    EXPECT_NEAR(ResolvePairDepenetrationShare(physA, physB), 0.25F, 1e-5F);

    // VisibilityComponent & NameComponent & HierarchyComponent & EditorDescriptionComponent
    VisibilityComponent vis{.visible = false, .visibleInHierarchy = true, .visibilityParent = Entity{4U, 1U}};
    EXPECT_FALSE(IsVisibilityComponentValidForEntity(vis, Entity{4U, 1U}));
    const VisibilityComponent sanitizedVis = SanitizeVisibilityComponentForEntity(vis, Entity{4U, 1U});
    EXPECT_FALSE(HasVisibilityRedirect(sanitizedVis));
    EXPECT_FALSE(ResolveInheritedVisibility(&vis, true));

    const NameComponent sanitizedName = SanitizeNameComponent(NameComponent{""}, "FallbackNode");
    EXPECT_EQ(sanitizedName.name, "FallbackNode");
    EXPECT_EQ(ResolveEntityDisplayNameOrFallback(&sanitizedName, "Other"), "FallbackNode");

    HierarchyComponent hierarchy{};
    EXPECT_TRUE(IsHierarchyRoot(hierarchy));
    EXPECT_TRUE(AssignParentAndAdvanceSiblingOrder(hierarchy, Entity{2U, 1U}));
    EXPECT_FALSE(IsHierarchyRoot(hierarchy));

    EditorDescriptionComponent desc{.description = "  First summary line\nSecond line"};
    EXPECT_TRUE(HasNonEmptyEditorDescription(desc));
    EXPECT_EQ(CountEditorDescriptionLines(desc), 2U);
    EXPECT_EQ(BuildEditorDescriptionPreview(desc, 64U), "First summary line");

    // AutoTranslateComponent & ThreadGroupComponent
    AutoTranslateComponent autoTrans{.mode = LocalizeMode::Inherit};
    EXPECT_EQ(ResolveHierarchyLocalizeMode(&autoTrans, LocalizeMode::Literal), LocalizeMode::Literal);
    EXPECT_FALSE(ShouldTranslateEntityText(&autoTrans));

    ThreadGroupComponent threadGroup{.mode = ThreadGroupMode::SubThread};
    EXPECT_EQ(ResolveHierarchyThreadGroupMode(&threadGroup, ThreadGroupMode::Inherit),
              ThreadGroupMode::SubThread);
    EXPECT_TRUE(CanDispatchOnSubThread(&threadGroup));

    // UITextComponent & PrimitiveMeshComponent & AudioSourceComponent
    UITextComponent uiText{.text = "Hello\nWorld", .fontSize = 20.0F, .alpha = 1.0F};
    EXPECT_TRUE(IsUITextVisibleForDraw(uiText));
    EXPECT_EQ(CountUITextLines(uiText), 2U);
    EXPECT_GT(EstimateUITextBoundsSizePixels(uiText).x, 0.0F);

    PrimitiveMeshComponent prim{.kind = PrimitiveMeshKind::Plane, .baseColor = Math::Vector3{2.0F, -1.0F, 0.5F}};
    // No material by default: the built-in lit material is what an unauthored primitive draws.
    EXPECT_FALSE(HasAssignedPrimitiveMaterial(prim));
    const PrimitiveMeshComponent sanitizedPrim = SanitizePrimitiveMeshComponent(prim);
    EXPECT_TRUE(IsPrimitiveMeshComponentValid(sanitizedPrim));
    EXPECT_EQ(GetPrimitiveMeshKindDisplayName(sanitizedPrim.kind), "Plane");
    EXPECT_EQ(sanitizedPrim.baseColor, (Math::Vector3{1.0F, 0.0F, 0.5F}));
    EXPECT_FALSE(HasAssignedPrimitiveMaterial(sanitizedPrim));

    // A material reference is opaque here: sanitising clamps the colour and the kind, and must
    // carry the reference through untouched rather than detaching the author's material.
    PrimitiveMeshComponent primWithMaterial{
        .kind = PrimitiveMeshKind::Capsule,
        .baseColor = Math::Vector3{-4.0F, 0.25F, 9.0F},
        .materialGuid = Asset::AssetGuid{77U},
    };
    EXPECT_TRUE(HasAssignedPrimitiveMaterial(primWithMaterial));
    const PrimitiveMeshComponent sanitizedWithMaterial =
        SanitizePrimitiveMeshComponent(primWithMaterial);
    EXPECT_EQ(sanitizedWithMaterial.materialGuid, (Asset::AssetGuid{77U}));
    EXPECT_EQ(sanitizedWithMaterial.baseColor, (Math::Vector3{0.0F, 0.25F, 1.0F}));
    EXPECT_TRUE(HasAssignedPrimitiveMaterial(sanitizedWithMaterial));

    AudioSourceComponent audio{};
    audio.audioAssetPath = "sfx/explosion.wav";
    audio.minDistance = 2.0F;
    audio.maxDistance = 12.0F;
    EXPECT_TRUE(HasAudioSourceAssetPath(audio));
    EXPECT_NEAR(ComputeAudioSourceDistanceAttenuation(audio, 7.0F), 0.5F, 1e-5F);

    // AnimationDriverComponent & AnimationSequencerComponent
    AnimationDriverComponent animDriver{};
    animDriver.speedScale = 2.0F;
    EXPECT_TRUE(HasAnyAnimatedTransformChannel(animDriver));
    EXPECT_FLOAT_EQ(ComputeScaledAnimationDeltaTime(animDriver, 0.25F), 0.5F);

    AnimationSequencerComponent seq{};
    seq.blendInSeconds = 2.0F;
    seq.blendElapsedSeconds = 0.5F;
    seq.isPlaying = true;
    EXPECT_FLOAT_EQ(ResolveAnimationBlendInWeight(seq), 0.25F);
    ResetAnimationSequencerRuntimeState(seq);
    EXPECT_FALSE(seq.isPlaying);

    // Static3D, Camera3D
    ColliderComponent staticCollider = Collider3D::MakeBox(Math::Vector3{2.0F, 2.0F, 2.0F});
    EXPECT_TRUE(Static3D::ContainsWorldPoint(staticCollider, WorldTransformComponent{},
                                                   Math::Vector3{1.0F, 1.0F, 1.0F}));
    CameraComponent cam3d{};
    EXPECT_TRUE(Camera3D::IsDepthWithinClipPlanes(cam3d, 10.0F));
    EXPECT_FALSE(Camera3D::IsDepthWithinClipPlanes(cam3d, 0.01F));
}

} // namespace UVE::Scene::Tests
