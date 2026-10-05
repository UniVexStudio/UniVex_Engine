// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <limits>

#include <array>
#include <utility>
#include <gtest/gtest.h>

#include "uve/objects/3d/all_objects_3d_uve.h"
#include "uve/objects/3d/bone_attachment_3d_uve.h"
#include "uve/component/area_component_uve.h"

namespace UVE::Scene::Tests {
namespace {

TEST(Expanded3DObjectComponentsUVETest, DefaultContractsAreValid) {
    EXPECT_TRUE(IsAreaComponentValidUVE(AreaComponentUVE{}));
    EXPECT_TRUE(IsRayCast3DObjectComponentValidUVE(RayCast3DComponentUVE{}));
    EXPECT_TRUE(IsKinematic3DObjectComponentValidUVE(Kinematic3DComponentUVE{}));
    EXPECT_TRUE(IsNavMeshVolume3DObjectComponentValidUVE(NavMeshVolume3DComponentUVE{}));
    EXPECT_TRUE(IsNavSeeker3DObjectComponentValidUVE(NavSeeker3DComponentUVE{}));
    EXPECT_TRUE(IsSkeleton3DObjectComponentValidUVE(Skeleton3DComponentUVE{}));
    EXPECT_TRUE(IsBoneAttachment3DObjectComponentValidUVE(BoneAttachment3DComponentUVE{}));
    EXPECT_TRUE(IsSpringArm3DObjectComponentValidUVE(SpringArm3DComponentUVE{}));
    EXPECT_TRUE(IsMarker3DObjectComponentValidUVE(Marker3DComponentUVE{}));
    EXPECT_TRUE(IsHitbox3DObjectComponentValidUVE(Hitbox3DComponentUVE{}));
    EXPECT_TRUE(IsHurtbox3DObjectComponentValidUVE(Hurtbox3DComponentUVE{}));
    EXPECT_TRUE(IsProjectile3DObjectComponentValidUVE(Projectile3DComponentUVE{}));
    EXPECT_TRUE(IsInteractionArea3DObjectComponentValidUVE(InteractionArea3DComponentUVE{}));
    EXPECT_TRUE(IsWorldEnvironment3DObjectComponentValidUVE(WorldEnvironment3DComponentUVE{}));
    EXPECT_TRUE(IsReflectionProbe3DObjectComponentValidUVE(ReflectionProbe3DComponentUVE{}));
    EXPECT_TRUE(IsDecal3DObjectComponentValidUVE(Decal3DComponentUVE{}));
    EXPECT_TRUE(IsLodGroup3DObjectComponentValidUVE(LodGroup3DComponentUVE{}));
    EXPECT_TRUE(IsOccluder3DObjectComponentValidUVE(Occluder3DComponentUVE{}));
    EXPECT_TRUE(IsVisibilityRegion3DObjectComponentValidUVE(VisibilityRegion3DComponentUVE{}));
    EXPECT_TRUE(IsSpawnPoint3DObjectComponentValidUVE(SpawnPoint3DComponentUVE{}));
    EXPECT_TRUE(IsLevelStreamer3DObjectComponentValidUVE(LevelStreamer3DComponentUVE{}));
    EXPECT_TRUE(IsWorldPartition3DObjectComponentValidUVE(WorldPartition3DComponentUVE{}));
}

TEST(Expanded3DObjectComponentsUVETest, Skeleton3DDefaultIsEmptyAndBoneAttachmentIsInert) {
    const Skeleton3DComponentUVE skeleton;
    const BoneAttachment3DComponentUVE attachment;

    EXPECT_TRUE(skeleton.skeletonAssetPath.empty());
    EXPECT_TRUE(skeleton.bones.empty());
    EXPECT_FALSE(IsBoneAttachment3DObjectComponentResolvableUVE(attachment));
}

TEST(Expanded3DObjectComponentsUVETest, ExplicitSkeletonAssetBindingIsFailureAtomic) {
    Skeleton3DComponentUVE skeleton;
    skeleton.enabled = false;
    const Skeleton3DComponentUVE original = skeleton;

    EXPECT_FALSE(TryBindExplicitSkeleton3DAssetUVE(
        skeleton, {}, {SkeletonBoneUVE{"root", -1, {}, {}, {1.0F, 1.0F, 1.0F}}}));
    EXPECT_EQ(skeleton.skeletonAssetPath, original.skeletonAssetPath);
    EXPECT_TRUE(skeleton.bones.empty());
    EXPECT_EQ(skeleton.enabled, original.enabled);

    EXPECT_FALSE(TryBindExplicitSkeleton3DAssetUVE(skeleton, "assets/character.uvskel", {}));
    EXPECT_EQ(skeleton.skeletonAssetPath, original.skeletonAssetPath);
    EXPECT_TRUE(skeleton.bones.empty());
}

TEST(Expanded3DObjectComponentsUVETest, ExplicitSkeletonAssetBindingHydratesOnlySuppliedHierarchy) {
    Skeleton3DComponentUVE skeleton;
    const std::vector<SkeletonBoneUVE> authoredBones{
        SkeletonBoneUVE{"root", -1, {}, {}, {1.0F, 1.0F, 1.0F}},
        SkeletonBoneUVE{"hand", 0, {1.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}};

    EXPECT_TRUE(TryBindExplicitSkeleton3DAssetUVE(
        skeleton, "assets/character.uvskel", authoredBones));
    EXPECT_EQ(skeleton.skeletonAssetPath, "assets/character.uvskel");
    ASSERT_EQ(skeleton.bones.size(), authoredBones.size());
    EXPECT_EQ(skeleton.bones[0].name, authoredBones[0].name);
    EXPECT_EQ(skeleton.bones[1].name, authoredBones[1].name);
    EXPECT_EQ(skeleton.bones[1].parentIndex, authoredBones[1].parentIndex);
}

TEST(Expanded3DObjectComponentsUVETest, BoneAttachmentBecomesResolvableOnlyWithExplicitReferences) {
    BoneAttachment3DComponentUVE attachment;
    attachment.skeleton = EntityUVE{7U, 1U};
    attachment.boneName = "hand";

    EXPECT_TRUE(IsBoneAttachment3DObjectComponentResolvableUVE(attachment));
    attachment.enabled = false;
    EXPECT_FALSE(IsBoneAttachment3DObjectComponentResolvableUVE(attachment));

    // A bone reference is a name or an index, and the index alone is enough: a rig whose bones are
    // addressed by number needs no name at all.
    attachment.enabled = true;
    attachment.boneName.clear();
    EXPECT_FALSE(IsBoneAttachment3DObjectComponentResolvableUVE(attachment));
    attachment.boneIndex = 0U;
    EXPECT_TRUE(IsBoneAttachment3DObjectComponentResolvableUVE(attachment));

    // The skeleton reference is not decoration: without one the attachment is inert whatever else it
    // names, and a default-constructed component points at nothing.
    attachment.skeleton = kInvalidEntityUVE;
    EXPECT_FALSE(IsBoneAttachment3DObjectComponentResolvableUVE(attachment));
}

/// A two-bone rig: a root at the skeleton's origin and a hand half a metre along the root's X.
[[nodiscard]] Skeleton3DComponentUVE TwoBoneRigUVE() {
    Skeleton3DComponentUVE skeleton;
    skeleton.skeletonAssetPath = "assets/character.uvskel";
    skeleton.bones = {SkeletonBoneUVE{"root", -1, {}, {}, {1.0F, 1.0F, 1.0F}},
                      SkeletonBoneUVE{"hand", 0, {0.5F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}};
    return skeleton;
}

TEST(Expanded3DObjectComponentsUVETest, BoneAttachmentResolverComposesTheBoneChainInPoseOrder) {
    const Skeleton3DComponentUVE skeleton = TwoBoneRigUVE();
    ObjectWorldFrameUVE frame{};
    ASSERT_TRUE(TryResolveSkeletonBoneWorldFrameUVE(skeleton, 1U, ObjectWorldFrameUVE{}, frame));
    EXPECT_NEAR(frame.position.x, 0.5F, 1e-5F) << "rest pose: half a metre along the skeleton's X";

    // The runtime pose is per bone and only covers the bones something animates. Posing the ROOT
    // moves the hand with it, because the hand's own transform is relative to the ROOT BONE - a
    // resolver that composed every bone against the skeleton's frame would leave the hand behind.
    Skeleton3DComponentUVE posed = skeleton;
    posed.pose = {SkeletonBonePoseUVE{{1.0F, 2.0F, 0.0F}, {}, {2.0F, 2.0F, 2.0F}}};
    ASSERT_TRUE(TryResolveSkeletonBoneWorldFrameUVE(posed, 1U, ObjectWorldFrameUVE{}, frame));
    EXPECT_NEAR(frame.position.x, 2.0F, 1e-5F) << "the posed root's 1 m, plus its 2x scale on the hand's 0.5 m";
    EXPECT_NEAR(frame.position.y, 2.0F, 1e-5F);
    EXPECT_NEAR(frame.scale.x, 2.0F, 1e-5F) << "the root's scale carries down the chain";

    // Rotation composes down the chain the same way: a quarter turn on the root takes the hand's +X
    // to the world's -Z.
    Skeleton3DComponentUVE turned = skeleton;
    turned.pose = {SkeletonBonePoseUVE{{0.0F, 0.0F, 0.0F},
                                       Math::QuaternionUVE{0.0F, 0.70710678F, 0.0F, 0.70710678F},
                                       {1.0F, 1.0F, 1.0F}}};
    ASSERT_TRUE(TryResolveSkeletonBoneWorldFrameUVE(turned, 1U, ObjectWorldFrameUVE{}, frame));
    EXPECT_NEAR(frame.position.x, 0.0F, 1e-5F);
    EXPECT_NEAR(frame.position.z, -0.5F, 1e-5F) << "+90 degrees about Y takes +X to -Z";

    // A skeleton that stands somewhere else carries its bones with it, and a bone's chain is composed
    // inside that frame rather than beside it.
    ASSERT_TRUE(TryResolveSkeletonBoneWorldFrameUVE(
        skeleton, 1U, ObjectWorldFrameUVE{{10.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, frame));
    EXPECT_NEAR(frame.position.x, 10.5F, 1e-5F);
}

TEST(Expanded3DObjectComponentsUVETest, BoneAttachmentResolverRefusesMalformedChainsAndUnknownBones) {
    const Skeleton3DComponentUVE skeleton = TwoBoneRigUVE();
    ObjectWorldFrameUVE frame{};
    EXPECT_FALSE(TryResolveSkeletonBoneWorldFrameUVE(skeleton, 2U, ObjectWorldFrameUVE{}, frame));
    EXPECT_FALSE(TryResolveSkeletonBoneWorldFrameUVE(Skeleton3DComponentUVE{}, 0U, ObjectWorldFrameUVE{}, frame));

    // A parent index outside the array is a corrupt asset, not a bone without a parent.
    Skeleton3DComponentUVE stray = skeleton;
    stray.bones[1].parentIndex = 9;
    EXPECT_FALSE(TryResolveSkeletonBoneWorldFrameUVE(stray, 1U, ObjectWorldFrameUVE{}, frame));

    // A cycle has no root to compose from, so the walk refuses instead of running forever.
    Skeleton3DComponentUVE cyclic = skeleton;
    cyclic.bones[0].parentIndex = 1;
    EXPECT_FALSE(TryResolveSkeletonBoneWorldFrameUVE(cyclic, 0U, ObjectWorldFrameUVE{}, frame));

    // A non-finite skeleton frame would poison every bone under it.
    EXPECT_FALSE(TryResolveSkeletonBoneWorldFrameUVE(
        skeleton, 1U,
        ObjectWorldFrameUVE{{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, frame));

    std::uint32_t index = 0U;
    EXPECT_TRUE(TryFindSkeletonBoneIndexUVE(skeleton, "hand", index));
    EXPECT_EQ(index, 1U);
    EXPECT_FALSE(TryFindSkeletonBoneIndexUVE(skeleton, "Hand", index)) << "names are exact, not case-insensitive";
    EXPECT_FALSE(TryFindSkeletonBoneIndexUVE(skeleton, "", index));
    EXPECT_FALSE(TryFindSkeletonBoneIndexUVE(skeleton, "tail", index));
}

TEST(Expanded3DObjectComponentsUVETest, BoneAttachmentComposesItsAuthoredOffsetInTheBonesOwnSpace) {
    const ObjectWorldFrameUVE bone{{0.0F, 1.0F, 0.0F},
                                   Math::QuaternionUVE{0.0F, 0.70710678F, 0.0F, 0.70710678F},
                                   {1.0F, 1.0F, 1.0F}};
    // 10 cm along the BONE's X: the bone is turned a quarter turn, so that comes out along the
    // world's -Z. An attachment authored in world units would land at +X and stay there while the
    // arm swings, which is exactly what the frame of reference has to prevent.
    const ObjectWorldFrameUVE attachment =
        ComposeBoneAttachmentWorldFrameUVE(bone, {0.1F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F});
    EXPECT_NEAR(attachment.position.x, 0.0F, 1e-5F);
    EXPECT_NEAR(attachment.position.y, 1.0F, 1e-5F);
    EXPECT_NEAR(attachment.position.z, -0.1F, 1e-5F);

    // The attachment's own rotation composes onto the bone's, and its scale onto the chain's, so a
    // scaled rig scales what it carries.
    const ObjectWorldFrameUVE scaled =
        ComposeBoneAttachmentWorldFrameUVE(bone, {}, {}, {2.0F, 1.0F, 1.0F});
    EXPECT_NEAR(scaled.scale.x, 2.0F, 1e-5F);
    EXPECT_NEAR(scaled.scale.y, 1.0F, 1e-5F);
}

TEST(Expanded3DObjectComponentsUVETest, BoneAttachmentLocalTransformInvertsWhatTheSceneGraphComposes) {
    // The scene graph composes world = parent then local (scale, then rotate, then translate). An
    // attachment has to be written in that same space, or its parent would move it a second time
    // during propagation - so what the resolver writes has to compose back out to the frame the bone
    // is in. Recomposed here exactly as SceneGraphUVE::UpdateUVE() does it.
    const ObjectWorldFrameUVE parent{{5.0F, 2.0F, -1.0F},
                                     Math::QuaternionUVE{0.0F, 0.70710678F, 0.0F, 0.70710678F},
                                     {2.0F, 2.0F, 2.0F}};
    const ObjectWorldFrameUVE wanted{{7.0F, 3.0F, -3.0F},
                                     Math::QuaternionUVE{0.0F, 0.0F, 0.38268343F, 0.92387953F},
                                     {4.0F, 4.0F, 4.0F}};
    Math::Vector3UVE localPosition{};
    Math::QuaternionUVE localRotation{};
    Math::Vector3UVE localScale{};
    ASSERT_TRUE(TryMakeBoneAttachmentLocalTransformUVE(wanted, parent, localPosition, localRotation, localScale));

    const Math::Vector3UVE recomposedPosition =
        parent.position +
        Math::RotateVectorUVE(parent.rotation, Math::Vector3UVE{localPosition.x * parent.scale.x,
                                                                localPosition.y * parent.scale.y,
                                                                localPosition.z * parent.scale.z});
    EXPECT_NEAR(recomposedPosition.x, wanted.position.x, 1e-4F);
    EXPECT_NEAR(recomposedPosition.y, wanted.position.y, 1e-4F);
    EXPECT_NEAR(recomposedPosition.z, wanted.position.z, 1e-4F);
    // Rotation is compared through a vector, because q and -q are the same rotation and comparing
    // components would fail on a sign the engine is right to allow.
    const Math::Vector3UVE axis{0.3F, -0.6F, 0.2F};
    const Math::Vector3UVE throughLocal =
        Math::RotateVectorUVE(Math::MultiplyUVE(parent.rotation, localRotation), axis);
    const Math::Vector3UVE throughWanted = Math::RotateVectorUVE(wanted.rotation, axis);
    EXPECT_NEAR(throughLocal.x, throughWanted.x, 1e-4F);
    EXPECT_NEAR(throughLocal.y, throughWanted.y, 1e-4F);
    EXPECT_NEAR(throughLocal.z, throughWanted.z, 1e-4F);
    EXPECT_NEAR(localScale.x * parent.scale.x, wanted.scale.x, 1e-4F);
    EXPECT_NEAR(localScale.y * parent.scale.y, wanted.scale.y, 1e-4F);
    EXPECT_NEAR(localScale.z * parent.scale.z, wanted.scale.z, 1e-4F);

    // A parent flattened by a zero scale cannot be inverted: the arithmetic would divide by it and
    // hand the renderer an infinity. Refusing keeps the last good transform instead.
    const ObjectWorldFrameUVE flattened{{0.0F, 0.0F, 0.0F}, {}, {1.0F, 0.0F, 1.0F}};
    EXPECT_FALSE(TryMakeBoneAttachmentLocalTransformUVE(wanted, flattened, localPosition, localRotation, localScale));
    const ObjectWorldFrameUVE infinite{{std::numeric_limits<float>::infinity(), 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}};
    EXPECT_FALSE(TryMakeBoneAttachmentLocalTransformUVE(infinite, parent, localPosition, localRotation, localScale));
}

TEST(Expanded3DObjectComponentsUVETest, Hitbox3DStrikeStateIsRuntimeOnlyAndNeverAuthored) {
    Hitbox3DComponentUVE hitbox;
    EXPECT_EQ(hitbox.strikeCount, 0U);
    EXPECT_FALSE(hitbox.strikesTruncated);
    EXPECT_EQ(hitbox.ignoreEntity, kInvalidEntityUVE);
    EXPECT_EQ(hitbox.struckCount, 0U);

    // Mid-frame runtime state must not change authoring validity - a live hitbox with fresh
    // strikes still passes the same validation a freshly authored one does.
    hitbox.strikeCount = 3U;
    hitbox.strikesTruncated = true;
    hitbox.strikes[0U] = Hitbox3DStrikeUVE{EntityUVE{}, 0.25F};
    EXPECT_TRUE(IsHitbox3DObjectComponentValidUVE(hitbox));
}

TEST(Expanded3DObjectComponentsUVETest, Hurtbox3DHitStateIsRuntimeOnlyAndNeverAuthored) {
    Hurtbox3DComponentUVE hurtbox;
    EXPECT_EQ(hurtbox.hitCount, 0U);
    EXPECT_FALSE(hurtbox.hitsTruncated);
    EXPECT_EQ(hurtbox.ignoreEntity, kInvalidEntityUVE);
    EXPECT_EQ(hurtbox.receivedCount, 0U);

    hurtbox.hitCount = 3U;
    hurtbox.hitsTruncated = true;
    hurtbox.hits[0U] = Hurtbox3DHitUVE{EntityUVE{}, 0.25F};
    EXPECT_TRUE(IsHurtbox3DObjectComponentValidUVE(hurtbox));
}

TEST(Expanded3DObjectComponentsUVETest, InteractionArea3DInteractorStateIsRuntimeOnlyAndNeverAuthored) {
    InteractionArea3DComponentUVE area;
    EXPECT_EQ(area.interactorCount, 0U);
    EXPECT_FALSE(area.interactorsTruncated);
    EXPECT_FALSE(area.focusedByPrimaryInteractor);
    EXPECT_EQ(area.ignoreEntity, kInvalidEntityUVE);

    area.interactorCount = 3U;
    area.interactorsTruncated = true;
    area.focusedByPrimaryInteractor = true;
    EXPECT_TRUE(IsInteractionArea3DObjectComponentValidUVE(area));
}

TEST(Expanded3DObjectComponentsUVETest, RayCast3DExclusionListIsAPrefixOfRealReferences) {
    // A freshly authored ray excludes nobody: every slot is the empty sentinel, never index 0 of a
    // pool that may already hold a live entity - a default EntityUVE is a real handle.
    RayCast3DComponentUVE ray;
    EXPECT_EQ(CountRayCast3DExclusionsUVE(ray), 0U);
    for (const EntityUVE& exclusion : ray.exclusions) {
        EXPECT_EQ(exclusion, kInvalidEntityUVE);
    }
    EXPECT_TRUE(IsRayCast3DObjectComponentValidUVE(ray));

    // Live references count as themselves, in the order they were authored.
    ray.exclusions[0] = EntityUVE{3U, 1U};
    ray.exclusions[1] = EntityUVE{7U, 2U};
    EXPECT_EQ(CountRayCast3DExclusionsUVE(ray), 2U);
    EXPECT_TRUE(IsRayCast3DObjectComponentValidUVE(ray));

    // The list is a prefix, so a live reference behind an empty slot is refused: no query would
    // ever reach it, and a ray that silently ignores an authored exclusion is worse than one that
    // refuses to load. The same rule makes the trailing slots the only place a hole may be.
    RayCast3DComponentUVE holed = ray;
    holed.exclusions[0] = kInvalidEntityUVE;
    EXPECT_EQ(CountRayCast3DExclusionsUVE(holed), 0U);
    EXPECT_FALSE(IsRayCast3DObjectComponentValidUVE(holed));

    // Duplicates are refused rather than collapsed: two identical slots are an authoring mistake,
    // and dropping one silently would hide which of the two was meant.
    RayCast3DComponentUVE duplicated = ray;
    duplicated.exclusions[1] = duplicated.exclusions[0];
    EXPECT_FALSE(IsRayCast3DObjectComponentValidUVE(duplicated));

    // All eight slots can be real. The bound is the array, so "the ninth exclusion" is not a value
    // this component can hold at all.
    RayCast3DComponentUVE full;
    for (std::size_t index = 0U; index < kMaximumRayCastExclusionsUVE; ++index) {
        full.exclusions[index] = EntityUVE{static_cast<std::uint32_t>(index), 1U};
    }
    EXPECT_EQ(CountRayCast3DExclusionsUVE(full), kMaximumRayCastExclusionsUVE);
    EXPECT_TRUE(IsRayCast3DObjectComponentValidUVE(full));
}

TEST(Expanded3DObjectComponentsUVETest, Projectile3DHitContractRejectsWhatItCannotHonor) {
    // The authored defaults are a valid projectile: a sphere that flies, stops on a hit and lives
    // ten seconds.
    Projectile3DComponentUVE projectile;
    EXPECT_TRUE(IsProjectile3DObjectComponentValidUVE(projectile));
    EXPECT_TRUE(IsKnownProjectile3DHitPolicyUVE(projectile.hitPolicy));
    EXPECT_EQ(projectile.ignoreEntity, kInvalidEntityUVE);

    // A policy outside the enum is a malformed component, not a policy to guess at.
    Projectile3DComponentUVE policy = projectile;
    policy.hitPolicy = static_cast<Projectile3DHitPolicyUVE>(7U);
    EXPECT_FALSE(IsKnownProjectile3DHitPolicyUVE(policy.hitPolicy));
    EXPECT_FALSE(IsProjectile3DObjectComponentValidUVE(policy));

    // The bounce coefficients live inside 0..1 and nowhere else.
    Projectile3DComponentUVE coefficient = projectile;
    coefficient.restitution = -0.1F;
    EXPECT_FALSE(IsProjectile3DObjectComponentValidUVE(coefficient));
    coefficient.restitution = 1.1F;
    EXPECT_FALSE(IsProjectile3DObjectComponentValidUVE(coefficient));
    coefficient.restitution = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsProjectile3DObjectComponentValidUVE(coefficient));
    coefficient.restitution = 0.0F;
    coefficient.friction = 1.0F;
    EXPECT_TRUE(IsProjectile3DObjectComponentValidUVE(coefficient));
    coefficient.friction = -0.01F;
    EXPECT_FALSE(IsProjectile3DObjectComponentValidUVE(coefficient));
    coefficient.friction = 1.01F;
    EXPECT_FALSE(IsProjectile3DObjectComponentValidUVE(coefficient));

    // A sphere has to have a size, and a lifetime has to be reachable.
    Projectile3DComponentUVE bounds = projectile;
    bounds.radius = 0.0F;
    EXPECT_FALSE(IsProjectile3DObjectComponentValidUVE(bounds));
    bounds = projectile;
    bounds.maxLifetime = 0.0F;
    EXPECT_FALSE(IsProjectile3DObjectComponentValidUVE(bounds));
    bounds = projectile;
    bounds.remainingLifetime = projectile.maxLifetime + 1.0F;
    EXPECT_FALSE(IsProjectile3DObjectComponentValidUVE(bounds));
    bounds = projectile;
    bounds.velocity.x = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsProjectile3DObjectComponentValidUVE(bounds));

    // Runtime result state has to stay readable: a claimed hit names its entity and carries finite
    // numbers, so a consumer that trusts `hit` can never act on a hit that points nowhere.
    Projectile3DComponentUVE hit = projectile;
    hit.hit = true;
    EXPECT_FALSE(IsProjectile3DObjectComponentValidUVE(hit));
    hit.hitEntity = EntityUVE{4U, 1U};
    hit.hitPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
    hit.hitNormal = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    hit.impactSpeed = 12.0F;
    EXPECT_TRUE(IsProjectile3DObjectComponentValidUVE(hit));
    hit.impactSpeed = -1.0F;
    EXPECT_FALSE(IsProjectile3DObjectComponentValidUVE(hit));
    hit.impactSpeed = 12.0F;
    hit.hitNormal.y = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsProjectile3DObjectComponentValidUVE(hit));
}

TEST(Expanded3DObjectComponentsUVETest, Projectile3DBounceReflectsAndNeverAddsEnergy) {
    const Math::Vector3UVE intoTheWall{6.0F, 0.0F, 0.0F};
    const Math::Vector3UVE wallNormal{-1.0F, 0.0F, 0.0F};

    // Head-on: the speed into the surface comes back at the restitution, and nothing else is left.
    const Math::Vector3UVE bounced =
        ResolveProjectile3DBounceVelocityUVE(intoTheWall, wallNormal, 0.5F, 0.0F);
    EXPECT_NEAR(bounced.x, -3.0F, 1.0e-5F);
    EXPECT_NEAR(bounced.y, 0.0F, 1.0e-5F);
    EXPECT_NEAR(bounced.z, 0.0F, 1.0e-5F);

    // Grazing: the component into the floor comes back at the restitution, the component along it
    // loses the friction, and the two do not mix.
    const Math::Vector3UVE grazing =
        ResolveProjectile3DBounceVelocityUVE(Math::Vector3UVE{2.0F, -4.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F},
                                             0.5F, 0.25F);
    EXPECT_NEAR(grazing.x, 1.5F, 1.0e-5F);
    EXPECT_NEAR(grazing.y, 2.0F, 1.0e-5F);

    // Coefficients are clamped: 5 is 1, and 1 is the most a bounce can return.
    const Math::Vector3UVE clamped =
        ResolveProjectile3DBounceVelocityUVE(intoTheWall, wallNormal, 5.0F, 0.0F);
    EXPECT_NEAR(Math::LengthUVE(clamped), 6.0F, 1.0e-4F);
    const Math::Vector3UVE slick =
        ResolveProjectile3DBounceVelocityUVE(Math::Vector3UVE{2.0F, -4.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F},
                                             1.0F, 5.0F);
    EXPECT_NEAR(slick.x, 0.0F, 1.0e-5F);
    EXPECT_NEAR(slick.y, 4.0F, 1.0e-5F);

    // The normal is a direction, not a distance: a longer one reflects the same motion as a unit
    // one, so nothing downstream has to remember to normalize before calling this.
    const Math::Vector3UVE alongZ{0.0F, 0.0F, 6.0F};
    const Math::Vector3UVE unitNormal{0.0F, 0.0F, -1.0F};
    const Math::Vector3UVE longNormal{0.0F, 0.0F, -4.0F};
    const Math::Vector3UVE scaled = ResolveProjectile3DBounceVelocityUVE(alongZ, longNormal, 1.0F, 0.0F);
    EXPECT_EQ(scaled, ResolveProjectile3DBounceVelocityUVE(alongZ, unitNormal, 1.0F, 0.0F));
    EXPECT_NEAR(scaled.z, -6.0F, 1.0e-5F);

    // Nothing to reflect about, or nothing finite to reflect: the motion is handed back unchanged
    // rather than replaced with a NaN.
    const Math::Vector3UVE noNormal{};
    EXPECT_EQ(ResolveProjectile3DBounceVelocityUVE(intoTheWall, noNormal, 1.0F, 0.0F), intoTheWall);
    const Math::Vector3UVE nanNormal{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    EXPECT_EQ(ResolveProjectile3DBounceVelocityUVE(intoTheWall, nanNormal, 1.0F, 0.0F), intoTheWall);
    const Math::Vector3UVE infiniteVelocity{std::numeric_limits<float>::infinity(), 0.0F, 0.0F};
    EXPECT_EQ(ResolveProjectile3DBounceVelocityUVE(infiniteVelocity, wallNormal, 1.0F, 0.0F), infiniteVelocity);
}

TEST(Expanded3DObjectComponentsUVETest, BoundedContractsRejectUnsafeValues) {
    RayCast3DComponentUVE ray;
    // A ray with no usable direction, no reach, or a length nobody can cast is refused outright.
    ray.direction = Math::Vector3UVE{};
    EXPECT_FALSE(IsRayCast3DObjectComponentValidUVE(ray));
    ray.direction = Math::Vector3UVE{0.0F, -1.0F, 0.0F};
    ray.length = 0.0F;
    EXPECT_FALSE(IsRayCast3DObjectComponentValidUVE(ray));
    ray.length = -1.0F;
    EXPECT_FALSE(IsRayCast3DObjectComponentValidUVE(ray));
    ray.length = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsRayCast3DObjectComponentValidUVE(ray));
    ray.length = 1.0F;
    EXPECT_TRUE(IsRayCast3DObjectComponentValidUVE(ray));

    Skeleton3DComponentUVE skeleton;
    skeleton.bones.push_back(SkeletonBoneUVE{"root", -1, {}, {}, {1.0F, 1.0F, 1.0F}});
    skeleton.bones.push_back(SkeletonBoneUVE{"root", 0, {}, {}, {1.0F, 1.0F, 1.0F}});
    EXPECT_FALSE(IsSkeleton3DObjectComponentValidUVE(skeleton));

    LodGroup3DComponentUVE lod;
    lod.distanceThresholds[1] = lod.distanceThresholds[0];
    EXPECT_FALSE(IsLodGroup3DObjectComponentValidUVE(lod));

    LevelStreamer3DComponentUVE streamer;
    streamer.enabled = true;
    streamer.levelPath.clear();
    EXPECT_FALSE(IsLevelStreamer3DObjectComponentValidUVE(streamer));

    WorldPartition3DComponentUVE partition;
    partition.cellCounts[1] = 0U;
    EXPECT_FALSE(IsWorldPartition3DObjectComponentValidUVE(partition));

    Kinematic3DComponentUVE kinematic;
    kinematic.interpolation = 1.5F;
    EXPECT_FALSE(IsKinematic3DObjectComponentValidUVE(kinematic));
    kinematic.interpolation = -0.5F;
    EXPECT_FALSE(IsKinematic3DObjectComponentValidUVE(kinematic));
    // Both ends of the authored range are real: 1 is "at speed this step", 0 is "never ease on its
    // own" - a body a script drives by writing its velocity.
    kinematic.interpolation = 1.0F;
    EXPECT_TRUE(IsKinematic3DObjectComponentValidUVE(kinematic));
    kinematic.interpolation = 0.0F;
    EXPECT_TRUE(IsKinematic3DObjectComponentValidUVE(kinematic));
    // A body switched off is authored data, not something to reject: `active` is the reversible
    // "stop where you are" the mover honours.
    kinematic.interpolation = 0.5F;
    kinematic.active = false;
    EXPECT_TRUE(IsKinematic3DObjectComponentValidUVE(kinematic));
}

TEST(Expanded3DObjectComponentsUVETest, FiniteAndBoundedValuesRejectNonFinitePayloads) {
    WorldEnvironment3DComponentUVE environment;
    environment.exposure = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));

    SpringArm3DComponentUVE springArm;
    springArm.currentLength = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsSpringArm3DObjectComponentValidUVE(springArm));

    // A target velocity that is not a number is not a body going nowhere: it is a malformed
    // component, refused here so the mover never has to guess (it drops one written at runtime).
    Kinematic3DComponentUVE kinematic;
    kinematic.targetVelocity.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsKinematic3DObjectComponentValidUVE(kinematic));
}

} // namespace
TEST(LodGroup3DResolveUVETest, EachThresholdSelectsItsOwnLevel) {
    // The core rule: the first threshold the distance fits under wins. Boundaries are inclusive,
    // so an object sitting exactly on a threshold picks the nearer level rather than flickering
    // between two as it drifts by a millimetre.
    LodGroup3DComponentUVE group;
    group.distanceThresholds = {10.0F, 25.0F, 60.0F, 120.0F, 240.0F, 480.0F, 960.0F, 1920.0F};
    group.levelCount = 4U;

    const std::array<std::pair<float, std::uint8_t>, 8U> cases{{
        {0.0F, 0U}, {9.9F, 0U}, {10.0F, 0U}, {10.1F, 1U},
        {25.0F, 1U}, {25.1F, 2U}, {60.0F, 2U}, {60.1F, 3U}}};
    for (const auto& [distance, expectedLevel] : cases) {
        ResolveLodGroup3DLevelUVE(group, distance);
        EXPECT_EQ(group.currentLevel, expectedLevel) << "at distance " << distance;
        EXPECT_FALSE(group.culledByDistance) << "at distance " << distance;
    }
}

TEST(LodGroup3DResolveUVETest, PastTheLastThresholdIsCulledWithoutRunningOffTheChain) {
    // Two things at once. The entity must be culled, and currentLevel must stay at the last REAL
    // level - a consumer that indexes a mesh array by level would otherwise read out of bounds
    // simply because an object moved too far away.
    LodGroup3DComponentUVE group;
    group.levelCount = 4U;
    ResolveLodGroup3DLevelUVE(group, 500.0F);

    EXPECT_TRUE(group.culledByDistance);
    EXPECT_EQ(group.currentLevel, 3U) << "must clamp to the last level, not run one past it";
    EXPECT_LT(group.currentLevel, group.levelCount);
}

TEST(LodGroup3DResolveUVETest, LevelCountShortensTheChainWithoutRewritingDistances) {
    // An author shortening a chain should not have to edit the distances. With levelCount 2 the
    // third threshold is ignored entirely, so 30 m is past the end even though the array still
    // holds a 60 m entry.
    LodGroup3DComponentUVE group;
    group.distanceThresholds = {10.0F, 25.0F, 60.0F, 120.0F, 240.0F, 480.0F, 960.0F, 1920.0F};
    group.levelCount = 2U;

    ResolveLodGroup3DLevelUVE(group, 20.0F);
    EXPECT_EQ(group.currentLevel, 1U);
    EXPECT_FALSE(group.culledByDistance);

    ResolveLodGroup3DLevelUVE(group, 30.0F);
    EXPECT_TRUE(group.culledByDistance) << "past entry 1 is past the end when levelCount is 2";
    EXPECT_EQ(group.currentLevel, 1U);
}

TEST(LodGroup3DResolveUVETest, EveryDegenerateCaseDrawsAtFullDetailRatherThanHiding) {
    // A configuration mistake should be visible so it gets fixed. Hiding geometry instead looks
    // exactly like a missing asset, which sends whoever hits it looking in the wrong place.
    LodGroup3DComponentUVE disabled;
    disabled.enabled = false;
    ResolveLodGroup3DLevelUVE(disabled, 100000.0F);
    EXPECT_EQ(disabled.currentLevel, 0U);
    EXPECT_FALSE(disabled.culledByDistance) << "a disabled group must never distance-cull";

    LodGroup3DComponentUVE emptyChain;
    emptyChain.levelCount = 0U;
    ResolveLodGroup3DLevelUVE(emptyChain, 100000.0F);
    EXPECT_EQ(emptyChain.currentLevel, 0U);
    EXPECT_FALSE(emptyChain.culledByDistance);

    LodGroup3DComponentUVE notFinite;
    ResolveLodGroup3DLevelUVE(notFinite, std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(notFinite.currentLevel, 0U);
    EXPECT_FALSE(notFinite.culledByDistance) << "a NaN distance must not hide an object";

    LodGroup3DComponentUVE overLong;
    overLong.levelCount = static_cast<std::uint8_t>(kMaximumLodLevelsUVE + 1U);
    ResolveLodGroup3DLevelUVE(overLong, 50.0F);
    EXPECT_EQ(overLong.currentLevel, 0U);
    EXPECT_FALSE(overLong.culledByDistance);
}

TEST(LodGroup3DResolveUVETest, ResolvingLeavesTheComponentValid) {
    // The resolver writes two derived fields, and the validator checks currentLevel against
    // levelCount. A resolve that produced an out-of-range level would make a previously valid
    // component fail validation on save - a corruption that only shows up at serialisation time.
    LodGroup3DComponentUVE group;
    ASSERT_TRUE(IsLodGroup3DObjectComponentValidUVE(group));
    for (const float distance : {0.0F, 5.0F, 50.0F, 500.0F, 100000.0F}) {
        ResolveLodGroup3DLevelUVE(group, distance);
        EXPECT_TRUE(IsLodGroup3DObjectComponentValidUVE(group)) << "after resolving at " << distance;
    }
}

TEST(LodGroup3DResolveUVETest, HysteresisKeepsALevelUntilTheDistanceIsWellPastItsThreshold) {
    // The whole point of the band: an object drifting across a threshold must not swap meshes on
    // alternate frames. With a 10% band on a 10 m threshold, the level is entered past 11 m and
    // only left under 9 m - and everything between those two points keeps whatever it had.
    LodGroup3DComponentUVE group;
    group.levelCount = 4U;
    group.hysteresis = 0.1F;

    ResolveLodGroup3DLevelUVE(group, 9.0F);
    EXPECT_EQ(group.currentLevel, 0U);

    // Past the threshold, but inside the entry band: the old level is kept, which is the whole
    // difference from the stateless rule.
    ResolveLodGroup3DLevelUVE(group, 10.5F);
    EXPECT_EQ(group.currentLevel, 0U) << "10.5 m is not past 11 m, so the band holds level 0";

    ResolveLodGroup3DLevelUVE(group, 11.5F);
    EXPECT_EQ(group.currentLevel, 1U) << "11.5 m is past the entry point";

    // Back inside the threshold, but not under the exit point: still level 1.
    ResolveLodGroup3DLevelUVE(group, 10.5F);
    EXPECT_EQ(group.currentLevel, 1U) << "hysteresis means the way back is not the way in";

    ResolveLodGroup3DLevelUVE(group, 8.5F);
    EXPECT_EQ(group.currentLevel, 0U) << "8.5 m is under the exit point";
}

TEST(LodGroup3DResolveUVETest, EveryThresholdCarriesItsOwnBand) {
    // The band is relative to each threshold, not one global distance, so a chain of thresholds an
    // order of magnitude apart still has a useful band at both ends.
    LodGroup3DComponentUVE group;
    group.levelCount = 4U;
    group.hysteresis = 0.1F;

    ResolveLodGroup3DLevelUVE(group, 20.0F);
    ASSERT_EQ(group.currentLevel, 1U);

    ResolveLodGroup3DLevelUVE(group, 26.0F);
    EXPECT_EQ(group.currentLevel, 1U) << "25 m's entry point is 27.5 m";

    ResolveLodGroup3DLevelUVE(group, 28.0F);
    EXPECT_EQ(group.currentLevel, 2U);

    ResolveLodGroup3DLevelUVE(group, 24.0F);
    EXPECT_EQ(group.currentLevel, 2U) << "25 m's exit point is 22.5 m";

    ResolveLodGroup3DLevelUVE(group, 22.0F);
    EXPECT_EQ(group.currentLevel, 1U);

    // The same rule at the far end of the chain: 60 m is entered past 66 m and left under 54 m.
    ResolveLodGroup3DLevelUVE(group, 70.0F);
    EXPECT_EQ(group.currentLevel, 3U);
    ResolveLodGroup3DLevelUVE(group, 61.0F);
    EXPECT_EQ(group.currentLevel, 3U);
    ResolveLodGroup3DLevelUVE(group, 53.0F);
    EXPECT_EQ(group.currentLevel, 2U);
}

TEST(LodGroup3DResolveUVETest, ACulledObjectStaysCulledInsideTheBandAndReturnsOnce) {
    // Culling is the level past the end of the chain, so it has a band too. Without one, an object
    // hovering on the last threshold would pop in and out of the visibility set every frame - and
    // every pop is a full asset resolution and placement, not just a draw.
    LodGroup3DComponentUVE group;
    group.levelCount = 4U;
    group.hysteresis = 0.1F;

    ResolveLodGroup3DLevelUVE(group, 200.0F);
    EXPECT_TRUE(group.culledByDistance);
    EXPECT_EQ(group.currentLevel, 3U) << "a culled object still names its last real level";

    ResolveLodGroup3DLevelUVE(group, 125.0F);
    EXPECT_TRUE(group.culledByDistance) << "125 m is inside 120 m's 12 m band, so it stays culled";

    ResolveLodGroup3DLevelUVE(group, 100.0F);
    EXPECT_FALSE(group.culledByDistance) << "100 m is under the 108 m return point";
    EXPECT_EQ(group.currentLevel, 3U) << "and lands on the level its distance is actually in";
}

TEST(LodGroup3DResolveUVETest, HysteresisZeroIsTheStatelessRuleItReplaced) {
    // An object that never opts into a band must resolve exactly as it did before hysteresis
    // existed - including through jumps in both directions, which is where a rule that remembered
    // anything would diverge.
    LodGroup3DComponentUVE group;
    group.levelCount = 4U;
    ASSERT_FLOAT_EQ(group.hysteresis, 0.0F);

    const std::array<std::pair<float, std::uint8_t>, 9U> cases{{
        {5.0F, 0U}, {200.0F, 3U}, {30.0F, 2U}, {12.0F, 1U}, {1000.0F, 3U},
        {60.0F, 2U}, {25.0F, 1U}, {10.0F, 0U}, {119.0F, 3U}}};
    for (const auto& [distance, expectedLevel] : cases) {
        ResolveLodGroup3DLevelUVE(group, distance);
        EXPECT_EQ(group.currentLevel, expectedLevel) << "at distance " << distance;
    }
    ResolveLodGroup3DLevelUVE(group, 500.0F);
    EXPECT_TRUE(group.culledByDistance);
}

TEST(LodGroup3DResolveUVETest, ATeleportAcrossSeveralLevelsLandsOnTheLevelItsDistanceIsIn) {
    // A teleported object crosses several thresholds in one frame. The step-by-step walk still ends
    // where the distance says it should, and the cull state follows the same banded rule.
    LodGroup3DComponentUVE group;
    group.levelCount = 4U;
    group.hysteresis = 0.1F;

    ResolveLodGroup3DLevelUVE(group, 5.0F);
    ASSERT_EQ(group.currentLevel, 0U);

    ResolveLodGroup3DLevelUVE(group, 500.0F);
    EXPECT_TRUE(group.culledByDistance);
    EXPECT_EQ(group.currentLevel, 3U);

    // Coming back from far away is one decision: un-cull and drop to the level the distance is in,
    // rather than spending a frame visible at the coarsest level.
    ResolveLodGroup3DLevelUVE(group, 5.0F);
    EXPECT_FALSE(group.culledByDistance);
    EXPECT_EQ(group.currentLevel, 0U);
}

TEST(LodGroup3DResolveUVETest, AnOutOfRangeHysteresisResolvesAsNoBandInsteadOfRefusing) {
    // Authoring rejects an out-of-range band, so this is a value that reached the resolver anyway -
    // a hand-edited file, a script writing the component. It must still resolve to something
    // sensible rather than produce an arbitrary band, so it clamps to the maximum.
    LodGroup3DComponentUVE group;
    group.levelCount = 4U;
    group.hysteresis = 10.0F;

    // Clamped to 0.5, so the 10 m threshold is entered strictly past 15 m - the same rule the
    // maximum band would give, not a 10-second-wide band nobody could author.
    ResolveLodGroup3DLevelUVE(group, 15.0F);
    EXPECT_EQ(group.currentLevel, 0U) << "15 m is the entry point, and entry is strict";

    ResolveLodGroup3DLevelUVE(group, 16.0F);
    EXPECT_EQ(group.currentLevel, 1U);
    EXPECT_FALSE(group.culledByDistance);
}

TEST(LodGroup3DMeshUVETest, EachLevelDrawsItsOwnMeshAndEmptySlotsFallBackToTheBaseMesh) {
    // The swapping rule, in one place: the level names a mesh, and a level that names none draws
    // the entity's own MeshComponentUVE mesh. That is what lets an author fill in levels 1..N and
    // leave level 0 - the full-detail mesh they already authored - alone.
    const Asset::AssetGuidUVE baseMesh{7U};
    LodGroup3DComponentUVE group;
    group.levelCount = 3U;
    group.lodMeshGuids[1] = Asset::AssetGuidUVE{42U};

    ResolveLodGroup3DLevelUVE(group, 5.0F);
    ASSERT_EQ(group.currentLevel, 0U);
    EXPECT_EQ(ResolveLodGroup3DMeshGuidUVE(group, baseMesh), baseMesh)
        << "an unset level 0 draws the component's own mesh";

    ResolveLodGroup3DLevelUVE(group, 20.0F);
    ASSERT_EQ(group.currentLevel, 1U);
    EXPECT_EQ(ResolveLodGroup3DMeshGuidUVE(group, baseMesh), Asset::AssetGuidUVE{42U});

    ResolveLodGroup3DLevelUVE(group, 70.0F);
    ASSERT_EQ(group.currentLevel, 2U);
    EXPECT_EQ(ResolveLodGroup3DMeshGuidUVE(group, baseMesh), baseMesh)
        << "level 2 overrides nothing, so it falls back too - not only level 0";
}

TEST(LodGroup3DMeshUVETest, ACulledGroupStillAnswersWithItsLastLevelsMesh) {
    // The renderer returns before asking - a culled object is not drawn at all - but the resolver
    // must not read out of range for a consumer that asks anyway, and it must not answer with a
    // mesh from a level the object is not on.
    const Asset::AssetGuidUVE baseMesh{7U};
    LodGroup3DComponentUVE group;
    group.levelCount = 2U;
    group.lodMeshGuids[0] = Asset::AssetGuidUVE{11U};
    group.lodMeshGuids[1] = Asset::AssetGuidUVE{22U};

    ResolveLodGroup3DLevelUVE(group, 500.0F);
    ASSERT_TRUE(group.culledByDistance);
    ASSERT_EQ(group.currentLevel, 1U);
    EXPECT_EQ(ResolveLodGroup3DMeshGuidUVE(group, baseMesh), Asset::AssetGuidUVE{22U});

    // A level outside the chain (a hand-written component) falls back rather than reading past the
    // authored levels.
    group.currentLevel = 9U;
    EXPECT_EQ(ResolveLodGroup3DMeshGuidUVE(group, baseMesh), baseMesh);
}

TEST(LodGroup3DResolveUVETest, TheValidatorRejectsABandOutsideTheAuthoredRange) {
    LodGroup3DComponentUVE group;
    ASSERT_TRUE(IsLodGroup3DObjectComponentValidUVE(group));

    group.hysteresis = -0.1F;
    EXPECT_FALSE(IsLodGroup3DObjectComponentValidUVE(group));

    group.hysteresis = kMaximumLodHysteresisUVE;
    EXPECT_TRUE(IsLodGroup3DObjectComponentValidUVE(group)) << "the maximum is a real band, not a refusal";

    group.hysteresis = kMaximumLodHysteresisUVE + 0.01F;
    EXPECT_FALSE(IsLodGroup3DObjectComponentValidUVE(group));

    group.hysteresis = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsLodGroup3DObjectComponentValidUVE(group));
}

TEST(Decal3DProjectionTest, APointInsideTheVolumeIsPaintedAndOneOutsideIsNot) {
    // The projection turns "where is this surface point relative to the decal" into the volume's
    // own unit coordinates: 0 at the centre, 1 at the surface, whatever `size` happens to be. That
    // is the whole reason the box and the cylinder share one footprint test.
    Decal3DComponentUVE decal;
    decal.size = Math::Vector3UVE{2.0F, 2.0F, 2.0F};
    Decal3DProjectionUVE projection{};
    ASSERT_TRUE(TryMakeDecal3DProjectionUVE(decal, Math::Vector3UVE{}, Math::QuaternionUVE{},
                                            Math::Vector3UVE{1.0F, 1.0F, 1.0F}, projection));

    const Decal3DSampleUVE centre =
        SampleDecal3DUVE(projection, Math::Vector3UVE{}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F);
    EXPECT_TRUE(centre.insideVolume);
    EXPECT_TRUE(centre.PaintsUVE());
    EXPECT_NEAR(centre.local.x, 0.0F, 1.0e-5F);
    EXPECT_FLOAT_EQ(centre.combinedWeight, 1.0F);

    const Decal3DSampleUVE inner = SampleDecal3DUVE(projection, Math::Vector3UVE{0.5F, 0.5F, 0.5F},
                                                    Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F);
    EXPECT_TRUE(inner.PaintsUVE());
    EXPECT_NEAR(inner.local.x, 0.5F, 1.0e-5F) << "half of the way to the surface, in unit coordinates";

    const Decal3DSampleUVE outside = SampleDecal3DUVE(projection, Math::Vector3UVE{1.5F, 0.0F, 0.0F},
                                                      Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F);
    EXPECT_FALSE(outside.insideVolume);
    EXPECT_FALSE(outside.PaintsUVE());
    EXPECT_FLOAT_EQ(outside.combinedWeight, 0.0F) << "outside the volume, nothing paints";
}

TEST(Decal3DProjectionTest, ScaleGrowsTheVolumeAndRotationTurnsItsAxis) {
    Decal3DComponentUVE decal;
    decal.size = Math::Vector3UVE{2.0F, 2.0F, 2.0F};

    // The volume follows the object's world scale - a scaled decal paints a bigger area, which is
    // what every other size in the engine does.
    Decal3DProjectionUVE scaled{};
    ASSERT_TRUE(TryMakeDecal3DProjectionUVE(decal, Math::Vector3UVE{}, Math::QuaternionUVE{},
                                            Math::Vector3UVE{2.0F, 1.0F, 1.0F}, scaled));
    EXPECT_TRUE(SampleDecal3DUVE(scaled, Math::Vector3UVE{1.5F, 0.0F, 0.0F},
                                 Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F)
                    .PaintsUVE())
        << "1.5 is inside a half-extent of 2";
    EXPECT_FALSE(SampleDecal3DUVE(scaled, Math::Vector3UVE{0.0F, 0.0F, 1.5F},
                                  Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F)
                     .PaintsUVE())
        << "but outside the unscaled axes";

    // +90 degrees about Y maps the local +X axis to world -Z, so a point half a metre below in
    // world -Z comes back as local +X - the projection is not a world-axis-aligned box.
    const Math::QuaternionUVE quarterTurnAboutY{0.0F, 0.70710678F, 0.0F, 0.70710678F};
    Decal3DProjectionUVE rotated{};
    ASSERT_TRUE(TryMakeDecal3DProjectionUVE(decal, Math::Vector3UVE{}, quarterTurnAboutY,
                                            Math::Vector3UVE{1.0F, 1.0F, 1.0F}, rotated));
    const Decal3DSampleUVE below = SampleDecal3DUVE(rotated, Math::Vector3UVE{0.0F, 0.0F, -0.5F},
                                                    Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F);
    EXPECT_TRUE(below.insideVolume);
    EXPECT_NEAR(below.local.x, 0.5F, 1.0e-4F);
    EXPECT_NEAR(below.local.z, 0.0F, 1.0e-4F);
    EXPECT_FALSE(SampleDecal3DUVE(rotated, Math::Vector3UVE{1.5F, 0.0F, 0.0F},
                                  Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F)
                     .PaintsUVE())
        << "world +X is the rotated volume's local -Z: 1.5 there is outside";
}

TEST(Decal3DProjectionTest, ACylinderFootprintIsRadialNotSquare) {
    Decal3DComponentUVE decal;
    decal.size = Math::Vector3UVE{2.0F, 2.0F, 2.0F};
    decal.projection = DecalProjectionModeUVE::Cylinder;
    Decal3DProjectionUVE projection{};
    ASSERT_TRUE(TryMakeDecal3DProjectionUVE(decal, Math::Vector3UVE{}, Math::QuaternionUVE{},
                                            Math::Vector3UVE{1.0F, 1.0F, 1.0F}, projection));

    // The corner of the box is inside the box and outside the cylinder - the one case that tells
    // the two footprints apart.
    EXPECT_FALSE(SampleDecal3DUVE(projection, Math::Vector3UVE{0.9F, 0.0F, 0.9F},
                                  Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F)
                     .PaintsUVE())
        << "0.9 squared twice is more than the unit radius";
    EXPECT_TRUE(SampleDecal3DUVE(projection, Math::Vector3UVE{0.5F, 0.0F, 0.6F},
                                 Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F)
                    .PaintsUVE());

    // The axis is still bounded the same way: the cylinder gets no taller than the box.
    EXPECT_FALSE(SampleDecal3DUVE(projection, Math::Vector3UVE{0.0F, 1.5F, 0.0F},
                                  Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F)
                     .PaintsUVE());
}

TEST(Decal3DProjectionTest, NormalFadeTurnsFacingIntoWeight) {
    // normalFade 0 paints any facing; 1 paints only surfaces turned back towards the decal. The
    // volume projects along -Y, so "towards the decal" is a +Y normal.
    Decal3DComponentUVE decal;
    Decal3DProjectionUVE projection{};
    ASSERT_TRUE(TryMakeDecal3DProjectionUVE(decal, Math::Vector3UVE{}, Math::QuaternionUVE{},
                                            Math::Vector3UVE{1.0F, 1.0F, 1.0F}, projection));

    const Math::Vector3UVE sideways{1.0F, 0.0F, 0.0F};
    EXPECT_FLOAT_EQ(SampleDecal3DUVE(projection, Math::Vector3UVE{}, sideways, 0.0F).combinedWeight, 1.0F)
        << "with no fade declared, a sideways surface is painted at full weight";

    Decal3DComponentUVE strict = decal;
    strict.normalFade = 1.0F;
    Decal3DProjectionUVE strictProjection{};
    ASSERT_TRUE(TryMakeDecal3DProjectionUVE(strict, Math::Vector3UVE{}, Math::QuaternionUVE{},
                                            Math::Vector3UVE{1.0F, 1.0F, 1.0F}, strictProjection));
    EXPECT_FLOAT_EQ(SampleDecal3DUVE(strictProjection, Math::Vector3UVE{},
                                     Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F)
                        .combinedWeight,
                    1.0F);
    EXPECT_FLOAT_EQ(SampleDecal3DUVE(strictProjection, Math::Vector3UVE{}, sideways, 0.0F).combinedWeight,
                    0.0F)
        << "a surface at a right angle faces away from the projection";
    EXPECT_FLOAT_EQ(SampleDecal3DUVE(strictProjection, Math::Vector3UVE{},
                                     Math::Vector3UVE{0.0F, -1.0F, 0.0F}, 0.0F)
                        .combinedWeight,
                    0.0F)
        << "a backface is not painted";

    Decal3DComponentUVE halfFade = decal;
    halfFade.normalFade = 0.5F;
    Decal3DProjectionUVE halfProjection{};
    ASSERT_TRUE(TryMakeDecal3DProjectionUVE(halfFade, Math::Vector3UVE{}, Math::QuaternionUVE{},
                                            Math::Vector3UVE{1.0F, 1.0F, 1.0F}, halfProjection));
    EXPECT_FLOAT_EQ(SampleDecal3DUVE(halfProjection, Math::Vector3UVE{}, sideways, 0.0F).combinedWeight,
                    0.5F);
}

TEST(Decal3DProjectionTest, TheVerticalFadesCutTheEndsOfTheVolumeAndDistanceFadeCutsItsFarSide) {
    Decal3DComponentUVE decal;
    decal.size = Math::Vector3UVE{2.0F, 2.0F, 2.0F};
    decal.upperFade = 0.5F;
    decal.lowerFade = 0.5F;
    decal.distanceFadeEnabled = true;
    decal.distanceFadeBegin = 40.0F;
    decal.distanceFadeLength = 10.0F;
    Decal3DProjectionUVE projection{};
    ASSERT_TRUE(TryMakeDecal3DProjectionUVE(decal, Math::Vector3UVE{}, Math::QuaternionUVE{},
                                            Math::Vector3UVE{1.0F, 1.0F, 1.0F}, projection));

    // Middle of the volume, at the camera: nothing fades.
    EXPECT_FLOAT_EQ(SampleDecal3DUVE(projection, Math::Vector3UVE{}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 10.0F)
                        .combinedWeight,
                    1.0F);

    // Nine tenths of the way up: inside the top band, three fifths of the way through it.
    const Decal3DSampleUVE nearTop = SampleDecal3DUVE(
        projection, Math::Vector3UVE{0.0F, 0.9F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 10.0F);
    EXPECT_TRUE(nearTop.insideVolume);
    EXPECT_NEAR(nearTop.combinedWeight, 0.2F, 1.0e-4F) << "(0.9 - 0.5) / 0.5 leaves a fifth";

    // The same at the bottom, which uses the lower band rather than the upper one.
    const Decal3DSampleUVE nearBottom = SampleDecal3DUVE(
        projection, Math::Vector3UVE{0.0F, -0.9F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 10.0F);
    EXPECT_NEAR(nearBottom.combinedWeight, 0.2F, 1.0e-4F);
    // The fades stay apart so a caller can say WHICH one removed a sample: the top point's loss is
    // entirely the vertical fade, not the normal one.
    EXPECT_FLOAT_EQ(nearTop.normalFadeWeight, 1.0F);
    EXPECT_NEAR(nearTop.depthFadeWeight, 0.2F, 1.0e-4F);
    EXPECT_FLOAT_EQ(nearTop.distanceFadeWeight, 1.0F);

    // Distance: full until the band begins, half way through it, gone past its end.
    EXPECT_FLOAT_EQ(SampleDecal3DUVE(projection, Math::Vector3UVE{}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 35.0F)
                        .combinedWeight,
                    1.0F);
    EXPECT_FLOAT_EQ(SampleDecal3DUVE(projection, Math::Vector3UVE{}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 45.0F)
                        .combinedWeight,
                    0.5F);
    EXPECT_FLOAT_EQ(SampleDecal3DUVE(projection, Math::Vector3UVE{}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 60.0F)
                        .combinedWeight,
                    0.0F);

    // A zero-length band is a hard cut at `begin`, not a division by zero.
    Decal3DComponentUVE hardCut = decal;
    hardCut.distanceFadeLength = 0.0F;
    Decal3DProjectionUVE hardCutProjection{};
    ASSERT_TRUE(TryMakeDecal3DProjectionUVE(hardCut, Math::Vector3UVE{}, Math::QuaternionUVE{},
                                            Math::Vector3UVE{1.0F, 1.0F, 1.0F}, hardCutProjection));
    EXPECT_FLOAT_EQ(SampleDecal3DUVE(hardCutProjection, Math::Vector3UVE{},
                                     Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 39.0F)
                        .combinedWeight,
                    1.0F);
    EXPECT_FLOAT_EQ(SampleDecal3DUVE(hardCutProjection, Math::Vector3UVE{},
                                     Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 41.0F)
                        .combinedWeight,
                    0.0F);
}

TEST(Decal3DProjectionTest, ADegeneratePoseOrSizePaintsNothingRatherThanASmear) {
    Decal3DComponentUVE decal;
    Decal3DProjectionUVE projection{};

    Decal3DComponentUVE zeroSize = decal;
    zeroSize.size = Math::Vector3UVE{0.0F, 1.0F, 1.0F};
    EXPECT_FALSE(TryMakeDecal3DProjectionUVE(zeroSize, Math::Vector3UVE{}, Math::QuaternionUVE{},
                                             Math::Vector3UVE{1.0F, 1.0F, 1.0F}, projection));
    EXPECT_FALSE(TryMakeDecal3DProjectionUVE(decal, Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
                                             Math::QuaternionUVE{}, Math::Vector3UVE{1.0F, 1.0F, 1.0F}, projection));
    EXPECT_FALSE(TryMakeDecal3DProjectionUVE(decal, Math::Vector3UVE{}, Math::QuaternionUVE{},
                                             Math::Vector3UVE{1.0F, 0.0F, 1.0F}, projection))
        << "a zero world-scale axis has no volume to project";
    EXPECT_FALSE(TryMakeDecal3DProjectionUVE(decal, Math::Vector3UVE{}, Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 0.0F},
                                             Math::Vector3UVE{1.0F, 1.0F, 1.0F}, projection))
        << "a rotation that cannot be inverted has no volume frame";

    // A non-finite sample is refused too: a NaN weight would spread through the blend.
    ASSERT_TRUE(TryMakeDecal3DProjectionUVE(decal, Math::Vector3UVE{}, Math::QuaternionUVE{},
                                            Math::Vector3UVE{1.0F, 1.0F, 1.0F}, projection));
    const Decal3DSampleUVE nanPoint =
        SampleDecal3DUVE(projection, Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
                         Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F);
    EXPECT_FALSE(nanPoint.PaintsUVE());
    EXPECT_FLOAT_EQ(nanPoint.combinedWeight, 0.0F);
}

TEST(Decal3DLifetimeTest, ALifetimeCountsDownOnSimulatedSecondsAndExpiresExactlyOnce) {
    Decal3DComponentUVE decal;
    decal.lifetime = 2.0F;

    // The first advance arms the countdown from the authored lifetime: a decal that just appeared
    // must not expire because its remaining time started at zero.
    EXPECT_FALSE(AdvanceDecal3DLifetimeUVE(decal, 0.5F));
    EXPECT_NEAR(decal.remainingLifetime, 1.5F, 1.0e-5F);
    EXPECT_FALSE(decal.expired);

    EXPECT_FALSE(AdvanceDecal3DLifetimeUVE(decal, 1.4F));
    EXPECT_NEAR(decal.remainingLifetime, 0.1F, 1.0e-5F);

    // The crossing is reported once, and only once: every later advance is a no-op.
    EXPECT_TRUE(AdvanceDecal3DLifetimeUVE(decal, 0.2F));
    EXPECT_FLOAT_EQ(decal.remainingLifetime, 0.0F);
    EXPECT_TRUE(decal.expired);
    EXPECT_FALSE(AdvanceDecal3DLifetimeUVE(decal, 10.0F));
    EXPECT_FALSE(AdvanceDecal3DLifetimeUVE(decal, 10.0F));
}

TEST(Decal3DLifetimeTest, APermanentDecalNeverExpiresAndABadClockChangesNothing) {
    Decal3DComponentUVE permanent;
    permanent.lifetime = 0.0F;
    EXPECT_FALSE(AdvanceDecal3DLifetimeUVE(permanent, 100000.0F));
    EXPECT_FALSE(permanent.expired) << "lifetime 0 keeps a decal forever";
    EXPECT_FLOAT_EQ(permanent.remainingLifetime, 0.0F);

    Decal3DComponentUVE timed;
    timed.lifetime = 5.0F;
    ASSERT_FALSE(AdvanceDecal3DLifetimeUVE(timed, 1.0F));
    const float before = timed.remainingLifetime;
    EXPECT_FALSE(AdvanceDecal3DLifetimeUVE(timed, std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(AdvanceDecal3DLifetimeUVE(timed, -1.0F));
    EXPECT_FALSE(AdvanceDecal3DLifetimeUVE(timed, 0.0F));
    EXPECT_FLOAT_EQ(timed.remainingLifetime, before) << "a broken or paused clock must not age it";

    // What the renderer asks before touching any geometry.
    EXPECT_TRUE(IsDecal3DPaintingUVE(timed));
    timed.enabled = false;
    EXPECT_FALSE(IsDecal3DPaintingUVE(timed));
    timed.enabled = true;
    timed.expired = true;
    EXPECT_FALSE(IsDecal3DPaintingUVE(timed));
    timed.expired = false;
    timed.size = Math::Vector3UVE{0.0F, 1.0F, 1.0F};
    EXPECT_FALSE(IsDecal3DPaintingUVE(timed));
}

TEST(LevelStreamer3DNearestViewerUVETest, EmptyViewerListMeansNoDistanceAtAll) {
    // A streamer with no valid viewers must report "no distance"; the verdict function relies on
    // that absence to never load or unload on a viewerless tick.
    EXPECT_FALSE(ResolveLevelStreamer3DNearestViewerDistanceSquaredUVE({1.0F, 2.0F, 3.0F}, {})
                     .has_value());
}

TEST(LevelStreamer3DNearestViewerUVETest, NearestWinsAndSquaredStaysSquared) {
    // The three viewers sit 5, 2, and 3 units away on the X axis. The nearest (2 units) yields
    // 4.0 - squared, matching the ordering-only discipline that needs no sqrt anywhere in the
    // streaming path.
    const std::array<Math::Vector3UVE, 3U> viewers{
        Math::Vector3UVE{5.0F, 0.0F, 0.0F},
        Math::Vector3UVE{2.0F, 0.0F, 0.0F},
        Math::Vector3UVE{-3.0F, 0.0F, 0.0F}};
    const std::optional<float> nearest =
        ResolveLevelStreamer3DNearestViewerDistanceSquaredUVE({0.0F, 0.0F, 0.0F}, viewers);
    ASSERT_TRUE(nearest.has_value());
    EXPECT_FLOAT_EQ(*nearest, 4.0F);
}

TEST(LevelStreamer3DNearestViewerUVETest, ANonFiniteViewerSkipsRatherThanPoisons) {
    // A camera with a degenerate world pose must not turn the whole decision NaN: it simply
    // stops being a viewer for this tick and the surviving one decides.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::array<Math::Vector3UVE, 3U> viewers{
        Math::Vector3UVE{nan, 0.0F, 0.0F},
        Math::Vector3UVE{4.0F, 0.0F, 0.0F},
        Math::Vector3UVE{0.0F, nan, 0.0F}};
    const std::optional<float> nearest =
        ResolveLevelStreamer3DNearestViewerDistanceSquaredUVE({0.0F, 0.0F, 0.0F}, viewers);
    ASSERT_TRUE(nearest.has_value()) << "one finite viewer must still answer";
    EXPECT_FLOAT_EQ(*nearest, 16.0F);

    // Every viewer dead -> no distance at all, as if nobody is watching.
    const std::array<Math::Vector3UVE, 1U> onlyDead{Math::Vector3UVE{nan, nan, nan}};
    EXPECT_FALSE(ResolveLevelStreamer3DNearestViewerDistanceSquaredUVE({0.0F, 0.0F, 0.0F}, onlyDead)
                     .has_value());

    // A streamer sitting dead-center in a NaN pose answers nothing either.
    const std::array<Math::Vector3UVE, 1U> aliveViewer{Math::Vector3UVE{1.0F, 0.0F, 0.0F}};
    EXPECT_FALSE(ResolveLevelStreamer3DNearestViewerDistanceSquaredUVE({nan, 0.0F, 0.0F}, aliveViewer)
                     .has_value());
}

TEST(LevelStreamer3DActionUVETest, InvalidAuthoredConfigurationAlwaysDecidesNothing) {
    // loadDistance <= 0, non-finite distances, or no hysteresis band at all: the verdict is a
    // hard None no matter what the viewer does - fail-closed, measured.
    LevelStreamer3DStreamingFrameUVE frame;
    frame.hasViewer = true;
    frame.nearestViewerDistanceSquared = 0.0F;
    frame.enabled = true;

    frame.loadDistance = 0.0F;
    frame.unloadDistance = 10.0F;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::None)
        << "a zero load distance is never a trigger";

    frame.loadDistance = 10.0F;
    frame.unloadDistance = 10.0F;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::None)
        << "unload <= load gives no hysteresis band to believe in";

    frame.loadDistance = std::numeric_limits<float>::infinity();
    frame.unloadDistance = 100.0F;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::None)
        << "non-finite configuration is never a trigger";

    frame.loadDistance = 10.0F;
    frame.unloadDistance = 20.0F;
    frame.nearestViewerDistanceSquared = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::None)
        << "a NaN viewer distance is never a trigger";
}

TEST(LevelStreamer3DActionUVETest, EnteringTheLoadRadiusRequestsLoadLeavingTheUnloadRadiusRequestsUnload) {
    // Happy path: a viewer inside the load radius starts the stream, inside the unload radius
    // keeps it, outside the unload radius ends it.
    LevelStreamer3DStreamingFrameUVE frame;
    frame.hasViewer = true;
    frame.enabled = true;
    frame.loadDistance = 10.0F;
    frame.unloadDistance = 20.0F;

    frame.nearestViewerDistanceSquared = 99.0F; // 9.949... < 10
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::RequestLoad);

    frame.loaded = true;
    frame.nearestViewerDistanceSquared = 150.0F; // between 10 and 20: hold the line
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::None)
        << "the hysteresis band keeps a loaded level in place";

    frame.nearestViewerDistanceSquared = 401.0F; // 20.02... > 20
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::RequestUnload);
}

TEST(LevelStreamer3DActionUVETest, BoundariesBelongToExactlyOneVerdictEach) {
    // loadDistance is inclusive on approach, unloadDistance is inclusive on retreat: each
    // boundary fires exactly one transition, never both, never neither across them.
    LevelStreamer3DStreamingFrameUVE frame;
    frame.hasViewer = true;
    frame.enabled = true;
    frame.loadDistance = 10.0F;
    frame.unloadDistance = 20.0F;

    frame.nearestViewerDistanceSquared = 100.0F; // d == loadDistance exactly
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::RequestLoad);

    frame.loaded = true;
    frame.nearestViewerDistanceSquared = 400.0F; // d == unloadDistance exactly
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::RequestUnload);

    // One epsilon INSIDE the band (d strictly between the two): hysteresis seesaws no transitions.
    frame.nearestViewerDistanceSquared = 399.0F;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::None)
        << "loaded and 19.97 from a 20-unload stays loaded";
    frame.loaded = false;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::None)
        << "unloaded and 19.97 from a 10-load stays unloaded";
}

TEST(LevelStreamer3DActionUVETest, NoViewerNeverMovesAnythingAndDisabledAlwaysUnloads) {
    LevelStreamer3DStreamingFrameUVE frame;
    frame.enabled = true;
    frame.loadDistance = 10.0F;
    frame.unloadDistance = 20.0F;

    // Nobody is watching: neither the load trigger nor a stale unload verdict may move content.
    frame.hasViewer = false;
    frame.nearestViewerDistanceSquared = 0.0F;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::None);
    frame.loaded = true;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::None)
        << "a viewerless tick never unloads a loaded level (the toggle does, not silence)";

    // The latch an async path would set: a pending load must not double-issue on the boundary.
    frame.hasViewer = true;
    frame.loaded = false;
    frame.loadRequested = true;
    frame.nearestViewerDistanceSquared = 99.0F;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::None)
        << "a load already in flight never loads twice";
    frame.loadRequested = false;

    // Disabling a streamer ALWAYS pulls its content, regardless of where the viewer stands.
    frame.enabled = false;
    frame.loaded = true;
    frame.nearestViewerDistanceSquared = 0.0F;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::RequestUnload)
        << "viewer at zero distance still unloads a disabled streamer";
}

TEST(LevelStreamer3DLoadBudgetUVETest, BudgetIsPositiveAndStructurallySmall) {
    // The per-tick load budget is the load-burst contract: greater than zero is mandatory (a
    // zero budget would wedge streaming forever), structurally bounded so a teleport can never
    // hitch beyond it, and deliberately finite so tests can drive past it and measure carry-over.
    static_assert(kMaximumLevelStreamer3DLoadsPerTickUVE > 0U);
    static_assert(kMaximumLevelStreamer3DLoadsPerTickUVE <= 16U);
    EXPECT_EQ(kMaximumLevelStreamer3DLoadsPerTickUVE, 4U) << "the documented burst budget";
}

TEST(ReflectionProbe3DInfluenceUVETest, CenterIsOneFaceIsZeroHalfwayIsHalf) {
    // The whole falloff contract in three exact points: the undisturbed center, the face that
    // ends the influence (the boundary itself belongs to the outside), and the geometric halfway.
    const Math::Vector3UVE half{2.0F, 2.0F, 2.0F};
    EXPECT_FLOAT_EQ(ResolveReflectionProbe3DInfluenceWeightUVE({0.0F, 0.0F, 0.0F}, half), 1.0F);
    EXPECT_FLOAT_EQ(ResolveReflectionProbe3DInfluenceWeightUVE({2.0F, 0.0F, 0.0F}, half), 0.0F)
        << "the face belongs to the outside - hard edges do not leak a floating sliver";
    EXPECT_FLOAT_EQ(ResolveReflectionProbe3DInfluenceWeightUVE({1.0F, 0.0F, 0.0F}, half), 0.5F);
    EXPECT_FLOAT_EQ(ResolveReflectionProbe3DInfluenceWeightUVE({-1.0F, 0.0F, 0.0F}, half), 0.5F)
        << "weight is radially symmetric on each axis";
}

TEST(ReflectionProbe3DInfluenceUVETest, ChebyshevRuleUsesTheWorstAxisAndAnisotropicBoxesRespectTheirSize) {
    // On {2,4,8} a point sitting (1,1,3) is far past the Y and Z wedges differently: each axis
    // normalizes against its OWN half extent (50%, 25%, 37.5%) and the maximum - the Chebyshev
    // distance - decides. 0.5 is the legal answer, not 0.75, not 0.625.
    const Math::Vector3UVE half{2.0F, 4.0F, 8.0F};
    EXPECT_FLOAT_EQ(
        ResolveReflectionProbe3DInfluenceWeightUVE({1.0F, 1.0F, 3.0F}, half), 0.5F)
        << "worst-normalized axis wins, every axis readers its own extent";

    // Exactly one tick beyond the face on the shortest axis: the probe is done.
    EXPECT_FLOAT_EQ(
        ResolveReflectionProbe3DInfluenceWeightUVE({2.01F, 0.0F, 0.0F}, half), 0.0F);
}

TEST(ReflectionProbe3DInfluenceUVETest, DegenerateBoxesAndNonFinitePointsInfluenceNothing) {
    const Math::Vector3UVE half{2.0F, 2.0F, 2.0F};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FLOAT_EQ(ResolveReflectionProbe3DInfluenceWeightUVE({0.0F, 0.0F, 0.0F}, {0.0F, 2.0F, 2.0F}),
                    0.0F)
        << "a collapsed axis decides nothing - fail-closed instead of dividing by it";
    EXPECT_FLOAT_EQ(ResolveReflectionProbe3DInfluenceWeightUVE({nan, 0.0F, 0.0F}, half), 0.0F);
    EXPECT_FLOAT_EQ(ResolveReflectionProbe3DInfluenceWeightUVE({0.0F, 0.0F, 0.0F}, {nan, 2.0F, 2.0F}),
                    0.0F);
    EXPECT_FLOAT_EQ(
        ResolveReflectionProbe3DInfluenceWeightUVE({std::numeric_limits<float>::infinity(), 0.0F, 0.0F},
                                                   half),
        0.0F);
}

TEST(ReflectionProbe3DCaptureUVETest, OnceCapturesExactlyOnceAndEveryFrameOnlyCapturesForTheEye) {
    // The Once mode must capture when it has never captured and then stop; EveryFrame must
    // capture only while the probe influences the eye - the measurable saving Godot's Always
    // mode never finds, because a probe influencing nothing re-renders nothing.
    ReflectionProbe3DCaptureFrameUVE frame;
    frame.hasCameraViewer = true;
    frame.cameraInfluenceWeight = 0.5F;
    frame.updateMode = ReflectionProbeUpdateModeUVE::Once;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::Capture);
    frame.capturedOnce = true;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::None)
        << "Once means once - the second verdict stays quiet";

    frame.capturedOnce = false;
    frame.updateMode = ReflectionProbeUpdateModeUVE::EveryFrame;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::Capture);

    frame.cameraInfluenceWeight = 0.0F;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::None)
        << "the probe beyond the face does not re-render for an eye it cannot affect";
    frame.cameraInfluenceWeight = 0.5F;

    frame.hasCameraViewer = false;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::None)
        << "no camera, no capture - even inside the influence box on paper";
}

TEST(ReflectionProbe3DCaptureUVETest, OnDemandListensOnlyToItsLatchAndDisabledNeverCaptures) {
    ReflectionProbe3DCaptureFrameUVE frame;
    frame.hasCameraViewer = true;
    frame.cameraInfluenceWeight = 0.5F;
    frame.updateMode = ReflectionProbeUpdateModeUVE::OnDemand;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::None)
        << "OnDemand without the latch is silent";
    frame.updateRequested = true;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::Capture);

    // A disabled probe answers None under every mode, every latch state.
    frame.enabled = false;
    frame.capturedOnce = false;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::None);
    frame.updateMode = ReflectionProbeUpdateModeUVE::Once;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::None);
    frame.updateMode = ReflectionProbeUpdateModeUVE::EveryFrame;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::None);
}

TEST(ReflectionProbe3DCaptureUVETest, CorruptUpdateModeAndNaNWeightFailClosed) {
    // Serialization is validation-gated, but a hand-mutated or bit-rotten runtime can still
    // carry an out-of-range update mode or a NaN weight; the verdict must say nothing at all.
    ReflectionProbe3DCaptureFrameUVE frame;
    frame.hasCameraViewer = true;
    frame.cameraInfluenceWeight = 0.5F;
    frame.updateRequested = true;

    frame.updateMode = static_cast<ReflectionProbeUpdateModeUVE>(17U);
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::None)
        << "an out-of-range mode is never a trigger";

    frame.updateMode = ReflectionProbeUpdateModeUVE::EveryFrame;
    frame.cameraInfluenceWeight = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::None)
        << "a NaN weight is never a trigger";
}

TEST(ReflectionProbe3DCaptureBudgetUVETest, BudgetIsPositiveAndStructurallySmall) {
    // Captures cost six scene passes each; the per-tick budget must be usable (> 0) and
    // structurally tiny so carrying over is the norm-ahead behavior, not an exception.
    static_assert(kMaximumReflectionProbeCapturesPerTickUVE > 0U);
    static_assert(kMaximumReflectionProbeCapturesPerTickUVE <= 8U);
    EXPECT_EQ(kMaximumReflectionProbeCapturesPerTickUVE, 2U) << "the documented capture budget";
}

TEST(WorldPartition3DCellIdUVETest, OriginAndInteriorPointsLandInTheExpectedCells) {
    // The grid origin is the minimum corner: the partition's own position is cell (0,0,0)'s
    // start, and a point clear inside another cell reads its integer coordinates exactly.
    WorldPartition3DComponentUVE config;
    config.cellSize = 10.0F;
    config.cellCounts = {4U, 1U, 4U};

    const Math::Vector3UVE origin{100.0F, 0.0F, -50.0F};
    const auto atOrigin = ResolveWorldPartition3DCellIdForPositionUVE(config, origin, origin);
    ASSERT_TRUE(atOrigin.has_value());
    EXPECT_EQ(atOrigin->x, 0);
    EXPECT_EQ(atOrigin->y, 0);
    EXPECT_EQ(atOrigin->z, 0);

    // origin + (25, 3, 11): inside cell (2, 0, 1) - every axis divides independently.
    const Math::Vector3UVE interior{125.0F, 3.0F, -39.0F};
    const auto inside = ResolveWorldPartition3DCellIdForPositionUVE(config, origin, interior);
    ASSERT_TRUE(inside.has_value());
    EXPECT_EQ(inside->x, 2);
    EXPECT_EQ(inside->y, 0);
    EXPECT_EQ(inside->z, 1);
}

TEST(WorldPartition3DCellIdUVETest, BoundariesBelongToExactlyOneCellEachAndTheOutsideIsUnmanaged) {
    // The floor rule at every edge class: exact cell starts belong to the cell they start,
    // one epsilon before belongs to the previous cell, the volume boundary itself is outside,
    // and anything behind or beyond the grid answers no cell at all (unmanaged: it stays
    // rendered, because volume-rejection must never hide geometry).
    WorldPartition3DComponentUVE config;
    config.cellSize = 10.0F;
    config.cellCounts = {2U, 1U, 1U}; // volume: x in [0,20), y in [0,10), z in [0,10)
    const Math::Vector3UVE origin{0.0F, 0.0F, 0.0F};

    const auto onBoundary = ResolveWorldPartition3DCellIdForPositionUVE(
        config, origin, Math::Vector3UVE{10.0F, 0.0F, 0.0F});
    ASSERT_TRUE(onBoundary.has_value());
    EXPECT_EQ(onBoundary->x, 1) << "an exact cell start belongs to the cell it starts";

    const auto justBefore = ResolveWorldPartition3DCellIdForPositionUVE(
        config, origin, Math::Vector3UVE{9.9999F, 0.0F, 0.0F});
    ASSERT_TRUE(justBefore.has_value());
    EXPECT_EQ(justBefore->x, 0) << "one epsilon earlier is still the previous cell";

    EXPECT_FALSE(ResolveWorldPartition3DCellIdForPositionUVE(
                     config, origin, Math::Vector3UVE{-0.0001F, 0.0F, 0.0F})
                     .has_value())
        << "behind the origin corner is outside the volume";
    EXPECT_FALSE(ResolveWorldPartition3DCellIdForPositionUVE(
                     config, origin, Math::Vector3UVE{20.0F, 0.0F, 0.0F})
                     .has_value())
        << "the volume's upper bound on an axis is outside it";
    EXPECT_FALSE(ResolveWorldPartition3DCellIdForPositionUVE(
                     config, origin, Math::Vector3UVE{5.0F, 15.0F, 5.0F})
                     .has_value())
        << "inside on two axes but outside the short third is still outside";
}

TEST(WorldPartition3DCellIdUVETest, DegenerateConfigAndNonFinitePosesNeverAnswerACell) {
    WorldPartition3DComponentUVE broken;
    broken.cellSize = 0.0F; // never valid
    EXPECT_FALSE(ResolveWorldPartition3DCellIdForPositionUVE(
                     broken, Math::Vector3UVE{}, Math::Vector3UVE{})
                     .has_value());

    WorldPartition3DComponentUVE valid;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(ResolveWorldPartition3DCellIdForPositionUVE(
                     valid, Math::Vector3UVE{}, Math::Vector3UVE{nan, 0.0F, 0.0F})
                     .has_value());
    EXPECT_FALSE(ResolveWorldPartition3DCellIdForPositionUVE(
                     valid, Math::Vector3UVE{0.0F, nan, 0.0F}, Math::Vector3UVE{})
                     .has_value())
        << "a NaN grid origin manages nothing either";
}

TEST(WorldPartition3DCellLinearIndexUVETest, IndexingIsDeterministicAndXMajor) {
    // The tie-break key consumers sort cells by: a fixed, space-filling index so two runs always
    // admit the same budget candidates even when distances tie exactly.
    const std::array<std::uint32_t, 3U> counts{4U, 2U, 3U};
    EXPECT_EQ(ResolveWorldPartition3DCellLinearIndexUVE({0, 0, 0}, counts), 0U);
    EXPECT_EQ(ResolveWorldPartition3DCellLinearIndexUVE({3, 0, 0}, counts), 3U);
    EXPECT_EQ(ResolveWorldPartition3DCellLinearIndexUVE({0, 1, 0}, counts), 4U) << "y strides by cx";
    EXPECT_EQ(ResolveWorldPartition3DCellLinearIndexUVE({0, 0, 1}, counts), 8U) << "z strides by cx*cy";
    EXPECT_EQ(ResolveWorldPartition3DCellLinearIndexUVE({2, 1, 2}, counts), 2U + 4U + 16U);
}

TEST(WorldPartition3DMembershipLiveUVETest, LiveOwnerTrustsItsFlagDeadOwnerFailsOpen) {
    // The renderer-side contract measured in isolation: while the partition exists, the live
    // flag rules; once it is gone, nobody may hide content with a stale opinion.
    EXPECT_FALSE(ResolveWorldPartition3DMembershipLiveUVE(/*partitionOwnerAlive=*/true, false))
        << "a live partition's fade verdict rules";
    EXPECT_TRUE(ResolveWorldPartition3DMembershipLiveUVE(true, true));
    EXPECT_TRUE(ResolveWorldPartition3DMembershipLiveUVE(/*partitionOwnerAlive=*/false, false))
        << "dead partition: fail open, never delete the world by stale flag";
    EXPECT_TRUE(ResolveWorldPartition3DMembershipLiveUVE(false, true));
}

TEST(VisibilityRegion3DContainmentUVETest, InteriorAndBoundaryPointsAreInsideTheOutsideIsNot) {
    // The whole room-culling contract rests on one question, so measure it: where is inside?
    // A region at the origin with half extents {2, 1, 4}; the boundary itself is INCLUDED on
    // purpose (unlike the world partition cells, a single box's skin belongs to itself exactly
    // once), and the outside never is.
    VisibilityRegion3DComponentUVE config;
    config.halfExtents = Math::Vector3UVE{2.0F, 1.0F, 4.0F};
    const Math::Vector3UVE origin{};

    EXPECT_TRUE(ResolveVisibilityRegion3DContainsPointUVE(config, origin, Math::Vector3UVE{}))
        << "the center is as inside as it gets";
    EXPECT_TRUE(
        ResolveVisibilityRegion3DContainsPointUVE(config, origin, Math::Vector3UVE{1.5F, 0.5F, -3.5F}))
        << "a routine interior point";
    EXPECT_TRUE(
        ResolveVisibilityRegion3DContainsPointUVE(config, origin, Math::Vector3UVE{2.0F, 1.0F, 4.0F}))
        << "the exact corner is INCLUDED - paint a mesh on the wall and it is managed";
    EXPECT_TRUE(
        ResolveVisibilityRegion3DContainsPointUVE(config, origin, Math::Vector3UVE{-2.0F, -1.0F, -4.0F}))
        << "the opposite corner too";
    EXPECT_FALSE(
        ResolveVisibilityRegion3DContainsPointUVE(config, origin, Math::Vector3UVE{2.01F, 0.0F, 0.0F}))
        << "one millimetre past the wall is outside - the box is not elastic";
    EXPECT_FALSE(
        ResolveVisibilityRegion3DContainsPointUVE(config, origin, Math::Vector3UVE{0.0F, -1.01F, 0.0F}));

    // A second origin proves this is region-relative, not world-relative:
    const Math::Vector3UVE room{100.0F, 0.0F, 0.0F};
    EXPECT_TRUE(ResolveVisibilityRegion3DContainsPointUVE(config, room,
                                                          Math::Vector3UVE{101.0F, 0.0F, 0.0F}));
    EXPECT_FALSE(
        ResolveVisibilityRegion3DContainsPointUVE(config, room, Math::Vector3UVE{}))
        << "the origin is outside the room at x=100";
}

TEST(VisibilityRegion3DContainmentUVETest, DegenerateConfigAndNonFinitePosesContainNothing) {
    // An unusable region manages NOTHING: zero half extent, negative extents, or a NaN pose all
    // answer false, so a typo in the inspector can never silently absorb a corridor.
    VisibilityRegion3DComponentUVE flat;
    flat.halfExtents = Math::Vector3UVE{0.0F, 1.0F, 1.0F};
    EXPECT_FALSE(ResolveVisibilityRegion3DContainsPointUVE(flat, Math::Vector3UVE{},
                                                           Math::Vector3UVE{}));

    VisibilityRegion3DComponentUVE negative;
    negative.halfExtents = Math::Vector3UVE{-2.0F, 1.0F, 1.0F};
    EXPECT_FALSE(ResolveVisibilityRegion3DContainsPointUVE(negative, Math::Vector3UVE{},
                                                           Math::Vector3UVE{}));

    VisibilityRegion3DComponentUVE fine;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(ResolveVisibilityRegion3DContainsPointUVE(
        fine, Math::Vector3UVE{}, Math::Vector3UVE{nan, 0.0F, 0.0F}));
    EXPECT_FALSE(ResolveVisibilityRegion3DContainsPointUVE(
        fine, Math::Vector3UVE{nan, 0.0F, 0.0F}, Math::Vector3UVE{}));
}

TEST(VisibilityRegion3DLayerGateUVETest, AnySharedBitManagesAndZeroMasksManageNothing) {
    // Layer gating must be an unsigned mask intersection, measured on corners: default-on-
    // default passes, disjoint masks close the gate, and a ZERO mask on either side manages
    // nothing (a mesh on no layers is never managed - an authoring choice, not an error).
    EXPECT_TRUE(ResolveVisibilityRegion3DLayerGateUVE(0xFFFFFFFFU, 0x00000001U))
        << "region-everything + mesh-layer-0: the out-of-the-box behaviour";
    EXPECT_TRUE(ResolveVisibilityRegion3DLayerGateUVE(0x00000002U, 0x00000002U))
        << "both on layer 1 alone";
    EXPECT_TRUE(ResolveVisibilityRegion3DLayerGateUVE(0x0000000AU, 0x00000008U))
        << "any ONE shared bit is enough";
    EXPECT_FALSE(ResolveVisibilityRegion3DLayerGateUVE(0x00000002U, 0x00000001U))
        << "props-layer region does not manage an NPC on the default layer";
    EXPECT_FALSE(ResolveVisibilityRegion3DLayerGateUVE(0U, 0xFFFFFFFFU))
        << "a region masking zero layers manages nothing";
    EXPECT_FALSE(ResolveVisibilityRegion3DLayerGateUVE(0xFFFFFFFFU, 0U))
        << "a mesh on zero layers is managed by nothing";
}

TEST(VisibilityRegion3DViewerUVETest, AnyViewerInsideActivatesAndAnEmptyCloudDoesNot) {
    // The pure half of the fail-open rule: any one viewer activates the region; an EMPTY cloud
    // answers false here (the engine's policy turns that into "show everything" above this
    // function, keeping the pure answer total).
    VisibilityRegion3DComponentUVE config;
    config.halfExtents = Math::Vector3UVE{2.0F, 2.0F, 2.0F};
    const Math::Vector3UVE origin{};

    EXPECT_FALSE(ResolveVisibilityRegion3DAnyViewerInsideUVE(
        config, origin, std::vector<Math::Vector3UVE>{}));
    EXPECT_TRUE(ResolveVisibilityRegion3DAnyViewerInsideUVE(
        config, origin, std::vector<Math::Vector3UVE>{Math::Vector3UVE{1.0F, 0.0F, 0.0F}}));
    EXPECT_FALSE(ResolveVisibilityRegion3DAnyViewerInsideUVE(
        config, origin,
        std::vector<Math::Vector3UVE>{Math::Vector3UVE{50.0F, 0.0F, 0.0F},
                                      Math::Vector3UVE{-50.0F, 0.0F, 0.0F}}))
        << "two viewers, neither nearby";
    EXPECT_TRUE(ResolveVisibilityRegion3DAnyViewerInsideUVE(
        config, origin,
        std::vector<Math::Vector3UVE>{Math::Vector3UVE{50.0F, 0.0F, 0.0F},
                                      Math::Vector3UVE{0.0F, 0.0F, 0.0F}}))
        << "one step over the room line activates for the whole party";
}

TEST(VisibilityRegion3DMembershipLiveUVETest, LiveOwnerTrustsItsFlagDeadOwnerFailsOpen) {
    // The renderer-side contract, same shape the world partition keeps: while the region owns
    // a membership the live flag rules; once the owner is gone nobody may hide content with its
    // stale opinion.
    EXPECT_FALSE(ResolveVisibilityRegion3DMembershipLiveUVE(/*regionOwnerAlive=*/true, false))
        << "inactive region: interior content is skipped";
    EXPECT_TRUE(ResolveVisibilityRegion3DMembershipLiveUVE(true, true));
    EXPECT_TRUE(ResolveVisibilityRegion3DMembershipLiveUVE(/*regionOwnerAlive=*/false, false))
        << "dead region: fail open, never leave a door closed behind a deleted room";
    EXPECT_TRUE(ResolveVisibilityRegion3DMembershipLiveUVE(false, true));
}

TEST(Occluder3DFullyHiddenUVETest, BehindTheWallIsHiddenInFrontBesideAndBeyondOverReachAreNot) {
    // The single strict rule, measured on a wall at the origin with half extents {2,1,2}: the
    // viewer at z=+5 looking down -z. A point on the far side of the wall, on the segment
    // through the wall's interior, is hidden; everything else draws.
    Occluder3DComponentUVE wall;
    wall.halfExtents = Math::Vector3UVE{2.0F, 1.0F, 2.0F};
    const Math::Vector3UVE wallOrigin{};
    const Math::Vector3UVE viewer{0.0F, 0.0F, 5.0F};

    EXPECT_TRUE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                Math::Vector3UVE{0.0F, 0.0F, -5.0F}))
        << "dead center behind the wall: hidden";
    EXPECT_TRUE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                Math::Vector3UVE{1.0F, 0.5F, -30.0F}))
        << "off-axis behind the wall, still covered: hidden";
    EXPECT_FALSE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                 Math::Vector3UVE{0.0F, 0.0F, 3.0F}))
        << "in FRONT of the wall: nothing was hidden";
    EXPECT_FALSE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                 Math::Vector3UVE{8.0F, 0.0F, -5.0F}))
        << "beside the wall on the far side: the segment clears the box (minded cone math:"
           " at x=8 the ray exits the wall's x-slab before ever entering its z-slab)";
    EXPECT_FALSE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin,
                                                 Math::Vector3UVE{20.0F, 0.0F, 5.0F},
                                                 Math::Vector3UVE{0.0F, 0.0F, -5.0F}))
        << "viewer beside the wall, same answer - the cover is directional";
}

TEST(Occluder3DFullyHiddenUVETest, HonestAmbiguitiesAllFailOpen) {
    // The no-false-culls edge list, each case measured: grazing the exact skin, standing on the
    // surface, standing inside, the box exactly AT the candidate, an invalid box, a NaN pose -
    // every one of them answers NOT hidden.
    Occluder3DComponentUVE wall;
    wall.halfExtents = Math::Vector3UVE{2.0F, 1.0F, 2.0F};
    const Math::Vector3UVE wallOrigin{};
    const Math::Vector3UVE viewer{0.0F, 0.0F, 5.0F};

    EXPECT_FALSE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                 Math::Vector3UVE{4.0F, 0.0F, -1.0F}))
        << "true tangency: the segment kisses the far corner (2,0,2) exactly once - grazing is"
           " no cover, the strict-overlap rule fails open at the boundary";
    EXPECT_FALSE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                 Math::Vector3UVE{0.0F, 0.0F, -2.0F}))
        << "the candidate stands ON the back face (the wall hides what is BEHIND, not at it)";
    EXPECT_FALSE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                 Math::Vector3UVE{0.0F, 0.0F, 0.5F}))
        << "inside the box is never hidden by the box";
    EXPECT_FALSE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin,
                                                 Math::Vector3UVE{0.0F, 0.0F, 1.0F},
                                                 Math::Vector3UVE{0.0F, 0.0F, -5.0F}))
        << "the viewer inside the cover sees past it";

    Occluder3DComponentUVE degenerate;
    degenerate.halfExtents = Math::Vector3UVE{0.0F, 1.0F, 1.0F};
    EXPECT_FALSE(ResolveOccluder3DFullyHiddenUVE(degenerate, wallOrigin, viewer,
                                                 Math::Vector3UVE{0.0F, 0.0F, -5.0F}))
        << "a zero-half-extent cover covers nothing";

    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                 Math::Vector3UVE{nan, 0.0F, -5.0F}));
    EXPECT_FALSE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin,
                                                 Math::Vector3UVE{0.0F, 0.0F, nan},
                                                 Math::Vector3UVE{0.0F, 0.0F, -5.0F}));
}

TEST(Occluder3DFullyHiddenUVETest, CoverIsAboutTheSegmentNotAboutDistance) {
    // Distance is not cover: a candidate VERY far behind but off the wall's silhouette draws,
    // and one centimetre behind the wall on-axis is hidden - the box casts an exact
    // infinite-depth shadow down the segment, nothing shorter, nothing longer.
    Occluder3DComponentUVE wall;
    wall.halfExtents = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
    const Math::Vector3UVE wallOrigin{};
    const Math::Vector3UVE viewer{0.0F, 0.0F, 5.0F};

    EXPECT_TRUE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                Math::Vector3UVE{0.0F, 0.0F, -1.01F}))
        << "2 cm behind the back face is already hidden";
    EXPECT_TRUE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                Math::Vector3UVE{0.0F, 0.0F, -10000.0F}))
        << "and the shadow has no draw-distance";
    EXPECT_TRUE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                Math::Vector3UVE{1.5F, 0.0F, -10000.0F}))
        << "perspective narrows at range: even far-off-axis points fall under the cover";
    EXPECT_FALSE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                 Math::Vector3UVE{3000.0F, 0.0F, -10000.0F}))
        << "10 km away outside the silhouette cone: still drawn";
}

} // namespace UVE::Scene::Tests
