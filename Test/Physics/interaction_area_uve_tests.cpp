// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/interaction_area_uve.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/interaction_area_3d_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Physics::Tests {
namespace {

using Scene::ColliderComponentUVE;
using Scene::ColliderShapeTypeUVE;
using Scene::EntityUVE;
using Scene::InteractionArea3DComponentUVE;
using Scene::kInvalidEntityUVE;

class InteractionAreaUVETest : public ::testing::Test {
protected:
    /// A character controller carrying a collider: the only thing an area ever tracks.
    [[nodiscard]] EntityUVE MakeInteractorUVE(
        const Math::Vector3UVE& position, const ColliderComponentUVE& collider = {}) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        entityManager.AddComponentUVE<ColliderComponentUVE>(entity, collider);
        entityManager.AddComponentUVE<Scene::CharacterControllerComponentUVE>(
            entity, Scene::CharacterControllerComponentUVE{});
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    /// A collider with no controller: a wall, not a player.
    [[nodiscard]] EntityUVE MakePlainColliderUVE(const Math::Vector3UVE& position) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        entityManager.AddComponentUVE<ColliderComponentUVE>(entity, ColliderComponentUVE{});
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    [[nodiscard]] EntityUVE MakeAreaUVE(const Math::Vector3UVE& position,
                                        const InteractionArea3DComponentUVE& component = {},
                                        const Math::Vector3UVE& halfExtents = {1.0F, 1.0F, 1.0F}) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        InteractionArea3DComponentUVE authored = component;
        authored.halfExtents = halfExtents;
        entityManager.AddComponentUVE<InteractionArea3DComponentUVE>(entity, authored);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    /// Moves an entity and re-sweeps, so the next scan reads the new world pose.
    void MoveUVE(const EntityUVE entity, const Math::Vector3UVE& position) {
        Scene::TransformComponentUVE transform =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
        transform.localPosition = position;
        sceneGraph.SetLocalTransformUVE(entityManager, entity, transform);
        sceneGraph.UpdateUVE(entityManager);
    }

    [[nodiscard]] InteractionAreaScanResultUVE ScanUVE() {
        return SyncInteractionAreasUVE(entityManager);
    }

    [[nodiscard]] const InteractionArea3DComponentUVE& AreaUVE(const EntityUVE entity) const {
        return entityManager.GetComponentUVE<InteractionArea3DComponentUVE>(entity);
    }

    /// The interaction area's runtime half is exactly three fields; everything else is authored.
    void ExpectClearedUVE(const EntityUVE entity) {
        const InteractionArea3DComponentUVE& area = AreaUVE(entity);
        EXPECT_EQ(area.interactorCount, 0U) << "entity " << entity.index;
        EXPECT_FALSE(area.interactorsTruncated) << "entity " << entity.index;
        EXPECT_FALSE(area.focusedByPrimaryInteractor) << "entity " << entity.index;
    }

    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;
};

TEST_F(InteractionAreaUVETest, AnAreaTracksEveryOverlappingInteractorInContentOrder) {
    // A wide area (2m half extents) with three controllers inside it, one outside.
    const EntityUVE area = MakeAreaUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE first = MakeInteractorUVE({-0.5F, 0.0F, 0.0F});
    const EntityUVE second = MakeInteractorUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE third = MakeInteractorUVE({0.5F, 0.0F, 0.0F});
    static_cast<void>(MakeInteractorUVE({10.0F, 0.0F, 0.0F}));

    const InteractionAreaScanResultUVE result = ScanUVE();
    EXPECT_EQ(result.interactorCount, 4U); // all four are interactors...
    ASSERT_EQ(result.areaCount, 1U);
    EXPECT_EQ(result.refreshedAreaCount, 1U);
    EXPECT_EQ(result.truncatedAreaCount, 0U);

    const InteractionArea3DComponentUVE& live = AreaUVE(area);
    ASSERT_EQ(live.interactorCount, 3U);   // ...but only three overlap this area
    EXPECT_EQ(live.interactors[0U], first); // (index, generation) order, not insertion luck
    EXPECT_EQ(live.interactors[1U], second);
    EXPECT_EQ(live.interactors[2U], third);
    EXPECT_FALSE(live.interactorsTruncated);
    EXPECT_TRUE(live.focusedByPrimaryInteractor); // the primary is the first, and it is inside
    EXPECT_EQ(result.primaryInteractor, first);
    EXPECT_EQ(result.focusedArea, area);

    // The list is a snapshot of THIS frame: walk everyone out and the area empties itself.
    MoveUVE(first, {20.0F, 0.0F, 0.0F});
    MoveUVE(second, {21.0F, 0.0F, 0.0F});
    MoveUVE(third, {22.0F, 0.0F, 0.0F});
    const InteractionAreaScanResultUVE empty = ScanUVE();
    EXPECT_EQ(empty.interactorCount, 4U);
    ExpectClearedUVE(area);
    EXPECT_FALSE(empty.HasFocusedAreaUVE());
}

TEST_F(InteractionAreaUVETest, TheAuthoredCandidateCapIsHonouredAndTheOverflowIsFlagged) {
    const EntityUVE area = MakeAreaUVE({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeInteractorUVE({-0.5F, 0.0F, 0.0F}));
    static_cast<void>(MakeInteractorUVE({0.0F, 0.0F, 0.0F}));
    static_cast<void>(MakeInteractorUVE({0.5F, 0.0F, 0.0F}));

    // The authored budget is a real budget: 2 means two stored, and the rest is REPORTED as
    // overflow instead of silently pretending the extra interactors do not exist.
    entityManager.GetComponentUVE<InteractionArea3DComponentUVE>(area).maximumCandidates = 2U;
    InteractionAreaScanResultUVE result = ScanUVE();
    EXPECT_EQ(AreaUVE(area).interactorCount, 2U);
    EXPECT_TRUE(AreaUVE(area).interactorsTruncated);
    EXPECT_EQ(result.truncatedAreaCount, 1U);

    // The cap bounds the stored LIST, never the focus: the primary interactor still earns this
    // area the focus even when it is the candidate that did not fit.
    EXPECT_TRUE(AreaUVE(area).focusedByPrimaryInteractor);

    // An authored 0 is NOT a budget of zero: it fails the component's own validator
    // (`maximumCandidates > 0`), so the area is refused as invalid and cleared - the same
    // fail-closed path a malformed tag takes. Tracking nobody on purpose is a disabled area.
    entityManager.GetComponentUVE<InteractionArea3DComponentUVE>(area).maximumCandidates = 0U;
    result = ScanUVE();
    EXPECT_EQ(result.areaCount, 1U);
    EXPECT_EQ(result.refreshedAreaCount, 0U);
    ExpectClearedUVE(area);
    EXPECT_EQ(result.truncatedAreaCount, 0U);
    EXPECT_FALSE(result.HasFocusedAreaUVE());

    // Room for everyone: no overflow flag at all.
    entityManager.GetComponentUVE<InteractionArea3DComponentUVE>(area).maximumCandidates = 16U;
    result = ScanUVE();
    EXPECT_EQ(AreaUVE(area).interactorCount, 3U);
    EXPECT_FALSE(AreaUVE(area).interactorsTruncated);
    EXPECT_EQ(result.truncatedAreaCount, 0U);
}

TEST_F(InteractionAreaUVETest, ADisabledAreaClearsWhatItWasTrackingAndComesBackWhenEnabled) {
    const EntityUVE area = MakeAreaUVE({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeInteractorUVE({0.0F, 0.0F, 0.0F}));
    static_cast<void>(ScanUVE());
    ASSERT_EQ(AreaUVE(area).interactorCount, 1U);

    entityManager.GetComponentUVE<InteractionArea3DComponentUVE>(area).enabled = false;
    const InteractionAreaScanResultUVE disabled = ScanUVE();
    ExpectClearedUVE(area);
    EXPECT_EQ(disabled.refreshedAreaCount, 0U); // visited and cleared, but not evaluated
    EXPECT_EQ(disabled.areaCount, 1U);
    EXPECT_FALSE(disabled.HasFocusedAreaUVE());

    entityManager.GetComponentUVE<InteractionArea3DComponentUVE>(area).enabled = true;
    static_cast<void>(ScanUVE());
    EXPECT_EQ(AreaUVE(area).interactorCount, 1U);
    EXPECT_TRUE(AreaUVE(area).focusedByPrimaryInteractor);
}

TEST_F(InteractionAreaUVETest, AnInvalidOrUnsweptAreaClearsItsStateRatherThanKeepingTheLastFrame) {
    // An area whose authored contract is broken: the validator refuses an empty interactionTag.
    const EntityUVE invalid = MakeAreaUVE({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeInteractorUVE({0.0F, 0.0F, 0.0F}));
    static_cast<void>(ScanUVE());
    ASSERT_EQ(AreaUVE(invalid).interactorCount, 1U);
    entityManager.GetComponentUVE<InteractionArea3DComponentUVE>(invalid).interactionTag.clear();

    // A second area with no world transform at all: authored, never swept, never evaluated.
    const EntityUVE unswept = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<InteractionArea3DComponentUVE>(
        unswept, InteractionArea3DComponentUVE{});

    const InteractionAreaScanResultUVE result = ScanUVE();
    EXPECT_EQ(result.areaCount, 2U);
    EXPECT_EQ(result.refreshedAreaCount, 0U);
    ExpectClearedUVE(invalid);
    ExpectClearedUVE(unswept);
    EXPECT_FALSE(result.HasFocusedAreaUVE());
}

TEST_F(InteractionAreaUVETest, AcceptanceIsSymmetricInBothDirections) {
    const EntityUVE area = MakeAreaUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE interactor = MakeInteractorUVE({0.0F, 0.0F, 0.0F});
    static_cast<void>(ScanUVE());
    ASSERT_EQ(AreaUVE(area).interactorCount, 1U);

    // The interactor's mask stops accepting the area's layer...
    entityManager.GetComponentUVE<ColliderComponentUVE>(interactor).collisionMask = 0x2U;
    static_cast<void>(ScanUVE());
    ExpectClearedUVE(area);

    // ...and the area's mask stops accepting the interactor's layer. Either direction alone is
    // enough to end participation - symmetric acceptance, the AreaOverlapSystemUVE rule.
    entityManager.GetComponentUVE<ColliderComponentUVE>(interactor).collisionMask = 0xFFFFFFFFU;
    entityManager.GetComponentUVE<InteractionArea3DComponentUVE>(area).collisionMask = 0x2U;
    static_cast<void>(ScanUVE());
    ExpectClearedUVE(area);

    // Both accept again: participation comes back with no leftover state.
    entityManager.GetComponentUVE<InteractionArea3DComponentUVE>(area).collisionMask = 0xFFFFFFFFU;
    const InteractionAreaScanResultUVE result = ScanUVE();
    EXPECT_EQ(AreaUVE(area).interactorCount, 1U);
    EXPECT_TRUE(result.HasFocusedAreaUVE());
}

TEST_F(InteractionAreaUVETest, AnAreaNeverListsTheInteractorLivingOnItsOwnEntity) {
    // The odd case an author can actually build: one entity that is both the area and a
    // character controller. It overlaps itself trivially, and must not list itself.
    const EntityUVE both = MakeAreaUVE({0.0F, 0.0F, 0.0F});
    entityManager.AddComponentUVE<ColliderComponentUVE>(both, ColliderComponentUVE{});
    entityManager.AddComponentUVE<Scene::CharacterControllerComponentUVE>(
        both, Scene::CharacterControllerComponentUVE{});

    const InteractionAreaScanResultUVE result = ScanUVE();
    EXPECT_EQ(result.interactorCount, 1U); // it IS an interactor...
    EXPECT_EQ(result.primaryInteractor, both);
    ExpectClearedUVE(both); // ...but never its own area's occupant, and it earns no focus
    EXPECT_FALSE(result.HasFocusedAreaUVE());
}

TEST_F(InteractionAreaUVETest, OnlyControllersWithAUsableColliderEverInteract) {
    const EntityUVE area = MakeAreaUVE({0.0F, 0.0F, 0.0F});
    // A collider with no controller: scenery, not a player.
    static_cast<void>(MakePlainColliderUVE({0.0F, 0.0F, 0.0F}));
    // A controller whose collider is invalid (a zero collision layer can never interact).
    ColliderComponentUVE dead;
    dead.collisionLayer = 0U;
    static_cast<void>(MakeInteractorUVE({0.0F, 0.0F, 0.0F}, dead));

    const InteractionAreaScanResultUVE none = ScanUVE();
    EXPECT_EQ(none.interactorCount, 0U);
    ExpectClearedUVE(area);
    EXPECT_FALSE(none.HasPrimaryInteractorUVE());

    // The real thing: a capsule is shape-aware, so its half extents are the capsule's own, not
    // the box defaults - a capsule tall enough to reach the area overlaps it.
    ColliderComponentUVE capsule;
    capsule.shapeType = ColliderShapeTypeUVE::Capsule;
    capsule.radius = 0.5F;
    capsule.height = 3.0F;
    const EntityUVE tall = MakeInteractorUVE({0.9F, 0.0F, 0.0F}, capsule);
    const InteractionAreaScanResultUVE one = ScanUVE();
    EXPECT_EQ(one.interactorCount, 1U);
    EXPECT_EQ(one.primaryInteractor, tall);
    EXPECT_EQ(AreaUVE(area).interactorCount, 1U);
}

TEST_F(InteractionAreaUVETest, TouchingBoundariesAreNotOverlaps) {
    // Two unit boxes whose faces meet exactly: the 15-axis test treats that as separation.
    const EntityUVE area = MakeAreaUVE({0.0F, 0.0F, 0.0F}, {}, {0.5F, 0.5F, 0.5F});
    static_cast<void>(MakeInteractorUVE({1.0F, 0.0F, 0.0F}));
    static_cast<void>(ScanUVE());
    ExpectClearedUVE(area);

    // One step inside and they overlap.
    static_cast<void>(MakeInteractorUVE({0.999F, 0.0F, 0.0F}));
    const InteractionAreaScanResultUVE inside = ScanUVE();
    EXPECT_EQ(inside.interactorCount, 2U);
    EXPECT_EQ(AreaUVE(area).interactorCount, 1U);
}

TEST_F(InteractionAreaUVETest, OnlyThePrimaryInteractorEarnsFocusAndItGoesToTheNearestArea) {
    // Two controllers: `first` is the primary by content order, `second` is closer to the far
    // area. Focus belongs to the primary alone, so the far area must still win.
    const EntityUVE nearFirst = MakeAreaUVE({-0.6F, 0.0F, 0.0F});
    const EntityUVE nearSecond = MakeAreaUVE({0.6F, 0.0F, 0.0F});
    const EntityUVE first = MakeInteractorUVE({-0.6F, 0.0F, 0.0F});
    const EntityUVE second = MakeInteractorUVE({0.6F, 0.0F, 0.0F});

    const InteractionAreaScanResultUVE result = ScanUVE();
    EXPECT_EQ(result.interactorCount, 2U);
    EXPECT_EQ(result.primaryInteractor, first); // (index, generation): the first made wins
    EXPECT_NE(result.primaryInteractor, second); // the nearer-to-the-far-area controller is not it
    EXPECT_EQ(AreaUVE(nearFirst).interactorCount, 2U);  // both areas track both controllers
    EXPECT_EQ(AreaUVE(nearSecond).interactorCount, 2U);
    EXPECT_TRUE(AreaUVE(nearFirst).focusedByPrimaryInteractor);
    EXPECT_FALSE(AreaUVE(nearSecond).focusedByPrimaryInteractor);
    EXPECT_EQ(result.focusedArea, nearFirst);

    // Move the primary next to the other area: the focus follows it, nothing sticks.
    MoveUVE(first, {0.6F, 0.0F, 0.0F});
    static_cast<void>(MakeInteractorUVE({0.0F, 0.0F, 0.0F}));
    const InteractionAreaScanResultUVE moved = ScanUVE();
    EXPECT_EQ(moved.primaryInteractor, first);
    EXPECT_FALSE(AreaUVE(nearFirst).focusedByPrimaryInteractor);
    EXPECT_TRUE(AreaUVE(nearSecond).focusedByPrimaryInteractor);
    EXPECT_EQ(moved.focusedArea, nearSecond);
}

TEST_F(InteractionAreaUVETest, EqualDistanceTiesBreakOnContentOrderDeterministically) {
    // Two areas symmetric about the interactor: equal squared distance, so the tie goes to the
    // lower (index, generation) handle - every run, every machine.
    const EntityUVE left = MakeAreaUVE({-0.5F, 0.0F, 0.0F});
    const EntityUVE right = MakeAreaUVE({0.5F, 0.0F, 0.0F});
    static_cast<void>(MakeInteractorUVE({0.0F, 0.0F, 0.0F}));

    const InteractionAreaScanResultUVE result = ScanUVE();
    EXPECT_EQ(result.focusedArea, left);
    EXPECT_TRUE(AreaUVE(left).focusedByPrimaryInteractor);
    EXPECT_FALSE(AreaUVE(right).focusedByPrimaryInteractor);

    // The same scene asked again answers identically - no state, no drift.
    const InteractionAreaScanResultUVE again = ScanUVE();
    EXPECT_EQ(again.focusedArea, left);
}

TEST_F(InteractionAreaUVETest, ASceneWithNoInteractorFocusesNothing) {
    const EntityUVE area = MakeAreaUVE({0.0F, 0.0F, 0.0F});
    const InteractionAreaScanResultUVE result = ScanUVE();
    EXPECT_EQ(result.interactorCount, 0U);
    EXPECT_EQ(result.areaCount, 1U);
    EXPECT_EQ(result.refreshedAreaCount, 1U);
    EXPECT_FALSE(result.HasPrimaryInteractorUVE());
    EXPECT_FALSE(result.HasFocusedAreaUVE());
    ExpectClearedUVE(area);
}

TEST_F(InteractionAreaUVETest, ASceneWithNoAreasStillFindsTheInteractors) {
    static_cast<void>(MakeInteractorUVE({0.0F, 0.0F, 0.0F}));
    const InteractionAreaScanResultUVE result = ScanUVE();
    EXPECT_EQ(result.interactorCount, 1U);
    EXPECT_EQ(result.areaCount, 0U);
    EXPECT_EQ(result.refreshedAreaCount, 0U);
    EXPECT_FALSE(result.HasFocusedAreaUVE());
}

TEST_F(InteractionAreaUVETest, ADegenerateWorldRotationFallsBackToIdentityInsteadOfDroppingTheArea) {
    // A zero quaternion in the world transform is a pose the sweep could not normalize: the area
    // falls back to identity rather than dropping out, so an axis-aligned overlap still counts.
    const EntityUVE area = MakeAreaUVE({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeInteractorUVE({0.0F, 0.0F, 0.0F}));
    static_cast<void>(ScanUVE());
    ASSERT_EQ(AreaUVE(area).interactorCount, 1U);

    Scene::WorldTransformComponentUVE& world =
        entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(area);
    world.worldRotation = Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 0.0F};
    const InteractionAreaScanResultUVE result = ScanUVE();
    EXPECT_EQ(result.refreshedAreaCount, 1U);
    EXPECT_EQ(AreaUVE(area).interactorCount, 1U); // identity fallback, not a dropped area
}

TEST_F(InteractionAreaUVETest, ANonUnitWorldRotationStillTracksTheInteractor) {
    // A world quaternion carrying numerical drift - the shape a long-lived sweep can leave behind -
    // is normalized rather than trusted, so the drift cannot silently hide an interactor.
    ColliderComponentUVE collider;
    collider.halfExtents = Math::Vector3UVE{0.5F, 0.5F, 0.5F};
    const EntityUVE interactor = MakeInteractorUVE({0.0F, 0.0F, 0.0F}, collider);
    Scene::WorldTransformComponentUVE& world =
        entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(interactor);
    world.worldRotation = Math::QuaternionUVE{0.0F, 0.1F, 0.0F, 0.99498743F}; // ~11.5 degrees

    const EntityUVE area = MakeAreaUVE({0.7000F, 0.0F, 0.0F}, {}, {0.5F, 0.5F, 0.5F});
    static_cast<void>(ScanUVE());
    EXPECT_EQ(AreaUVE(area).interactorCount, 1U);
}

TEST_F(InteractionAreaUVETest, TheScanWritesOnlyTheRuntimeHalfAndNeverTheAuthoredFields) {
    InteractionArea3DComponentUVE authored;
    authored.halfExtents = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
    authored.collisionLayer = 3U;
    authored.collisionMask = 0xFU;
    authored.interactionTag = "shop";
    authored.maximumCandidates = 4U;
    authored.enabled = true;
    // Poisoned runtime state from "last frame": the scan must overwrite exactly these and leave
    // every authored field alone.
    authored.interactorCount = 1U;
    authored.interactorsTruncated = true;
    authored.focusedByPrimaryInteractor = true;

    const EntityUVE area = MakeAreaUVE({0.0F, 0.0F, 0.0F}, authored);
    ColliderComponentUVE collider;
    collider.collisionLayer = 1U;
    collider.collisionMask = 0xFU;
    static_cast<void>(MakeInteractorUVE({0.0F, 0.0F, 0.0F}, collider));

    const InteractionAreaScanResultUVE result = ScanUVE();
    const InteractionArea3DComponentUVE& live = AreaUVE(area);
    EXPECT_EQ(live.interactorCount, 1U);
    EXPECT_FALSE(live.interactorsTruncated);
    EXPECT_TRUE(live.focusedByPrimaryInteractor);
    EXPECT_EQ(result.focusedArea, area);
    EXPECT_EQ(live.halfExtents, authored.halfExtents);
    EXPECT_EQ(live.collisionLayer, 3U);
    EXPECT_EQ(live.collisionMask, 0xFU);
    EXPECT_EQ(live.interactionTag, "shop");
    EXPECT_EQ(live.maximumCandidates, 4U);
    EXPECT_TRUE(live.enabled);
}

TEST_F(InteractionAreaUVETest, TheTagIsCarriedAndDeliberatelyDoesNotFilter) {
    // Two areas with different tags both track the same controller: tag-based gating belongs to
    // the gameplay layer that acts on the focus, and inventing a rule here would silently change
    // what an authored scene means.
    InteractionArea3DComponentUVE shop;
    shop.interactionTag = "shop";
    InteractionArea3DComponentUVE door;
    door.interactionTag = "door";
    const EntityUVE shopArea = MakeAreaUVE({0.0F, 0.0F, 0.0F}, shop);
    const EntityUVE doorArea = MakeAreaUVE({0.0F, 0.0F, 0.0F}, door);
    static_cast<void>(MakeInteractorUVE({0.0F, 0.0F, 0.0F}));

    const InteractionAreaScanResultUVE result = ScanUVE();
    EXPECT_EQ(AreaUVE(shopArea).interactorCount, 1U);
    EXPECT_EQ(AreaUVE(doorArea).interactorCount, 1U);
    // Both overlap equally, so the tie goes to the lower handle - the tag plays no part in it.
    EXPECT_EQ(result.focusedArea, shopArea);
}

} // namespace
} // namespace UVE::Physics::Tests
