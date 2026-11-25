// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "interaction_area.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "character_controller_component.h"
#include "collider_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "quaternion.h"
#include "vector3.h"
#include "memory_manager.h"
#include "interaction_area_3d.h"
#include "scene_graph.h"

namespace UVE::Physics::Tests {
namespace {

using Scene::ColliderComponent;
using Scene::ColliderShapeType;
using Scene::Entity;
using Scene::InteractionArea3DComponent;
using Scene::kInvalidEntity;

class InteractionAreaTest : public ::testing::Test {
protected:
    /// A character controller carrying a collider: the only thing an area ever tracks.
    [[nodiscard]] Entity MakeInteractor(
        const Math::Vector3& position, const ColliderComponent& collider = {}) {
        const Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        entityManager.AddComponent<ColliderComponent>(entity, collider);
        entityManager.AddComponent<Scene::CharacterControllerComponent>(
            entity, Scene::CharacterControllerComponent{});
        sceneGraph.Update(entityManager);
        return entity;
    }

    /// A collider with no controller: a wall, not a player.
    [[nodiscard]] Entity MakePlainCollider(const Math::Vector3& position) {
        const Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        entityManager.AddComponent<ColliderComponent>(entity, ColliderComponent{});
        sceneGraph.Update(entityManager);
        return entity;
    }

    [[nodiscard]] Entity MakeArea(const Math::Vector3& position,
                                        const InteractionArea3DComponent& component = {},
                                        const Math::Vector3& halfExtents = {1.0F, 1.0F, 1.0F}) {
        const Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        InteractionArea3DComponent authored = component;
        authored.halfExtents = halfExtents;
        entityManager.AddComponent<InteractionArea3DComponent>(entity, authored);
        sceneGraph.Update(entityManager);
        return entity;
    }

    /// Moves an entity and re-sweeps, so the next scan reads the new world pose.
    void Move(const Entity entity, const Math::Vector3& position) {
        Scene::TransformComponent transform =
            entityManager.GetComponent<Scene::TransformComponent>(entity);
        transform.localPosition = position;
        sceneGraph.SetLocalTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
    }

    [[nodiscard]] InteractionAreaScanResult Scan() {
        return SyncInteractionAreas(entityManager);
    }

    [[nodiscard]] const InteractionArea3DComponent& Area(const Entity entity) const {
        return entityManager.GetComponent<InteractionArea3DComponent>(entity);
    }

    /// The interaction area's runtime half is exactly three fields; everything else is authored.
    void ExpectCleared(const Entity entity) {
        const InteractionArea3DComponent& area = Area(entity);
        EXPECT_EQ(area.interactorCount, 0U) << "entity " << entity.index;
        EXPECT_FALSE(area.interactorsTruncated) << "entity " << entity.index;
        EXPECT_FALSE(area.focusedByPrimaryInteractor) << "entity " << entity.index;
    }

    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
};

TEST_F(InteractionAreaTest, AnAreaTracksEveryOverlappingInteractorInContentOrder) {
    // A wide area (2m half extents) with three controllers inside it, one outside.
    const Entity area = MakeArea({0.0F, 0.0F, 0.0F});
    const Entity first = MakeInteractor({-0.5F, 0.0F, 0.0F});
    const Entity second = MakeInteractor({0.0F, 0.0F, 0.0F});
    const Entity third = MakeInteractor({0.5F, 0.0F, 0.0F});
    static_cast<void>(MakeInteractor({10.0F, 0.0F, 0.0F}));

    const InteractionAreaScanResult result = Scan();
    EXPECT_EQ(result.interactorCount, 4U); // all four are interactors...
    ASSERT_EQ(result.areaCount, 1U);
    EXPECT_EQ(result.refreshedAreaCount, 1U);
    EXPECT_EQ(result.truncatedAreaCount, 0U);

    const InteractionArea3DComponent& live = Area(area);
    ASSERT_EQ(live.interactorCount, 3U);   // ...but only three overlap this area
    EXPECT_EQ(live.interactors[0U], first); // (index, generation) order, not insertion luck
    EXPECT_EQ(live.interactors[1U], second);
    EXPECT_EQ(live.interactors[2U], third);
    EXPECT_FALSE(live.interactorsTruncated);
    EXPECT_TRUE(live.focusedByPrimaryInteractor); // the primary is the first, and it is inside
    EXPECT_EQ(result.primaryInteractor, first);
    EXPECT_EQ(result.focusedArea, area);

    // The list is a snapshot of THIS frame: walk everyone out and the area empties itself.
    Move(first, {20.0F, 0.0F, 0.0F});
    Move(second, {21.0F, 0.0F, 0.0F});
    Move(third, {22.0F, 0.0F, 0.0F});
    const InteractionAreaScanResult empty = Scan();
    EXPECT_EQ(empty.interactorCount, 4U);
    ExpectCleared(area);
    EXPECT_FALSE(empty.HasFocusedArea());
}

TEST_F(InteractionAreaTest, TheAuthoredCandidateCapIsHonouredAndTheOverflowIsFlagged) {
    const Entity area = MakeArea({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeInteractor({-0.5F, 0.0F, 0.0F}));
    static_cast<void>(MakeInteractor({0.0F, 0.0F, 0.0F}));
    static_cast<void>(MakeInteractor({0.5F, 0.0F, 0.0F}));

    // The authored budget is a real budget: 2 means two stored, and the rest is REPORTED as
    // overflow instead of silently pretending the extra interactors do not exist.
    entityManager.GetComponent<InteractionArea3DComponent>(area).maximumCandidates = 2U;
    InteractionAreaScanResult result = Scan();
    EXPECT_EQ(Area(area).interactorCount, 2U);
    EXPECT_TRUE(Area(area).interactorsTruncated);
    EXPECT_EQ(result.truncatedAreaCount, 1U);

    // The cap bounds the stored LIST, never the focus: the primary interactor still earns this
    // area the focus even when it is the candidate that did not fit.
    EXPECT_TRUE(Area(area).focusedByPrimaryInteractor);

    // An authored 0 is NOT a budget of zero: it fails the component's own validator
    // (`maximumCandidates > 0`), so the area is refused as invalid and cleared - the same
    // fail-closed path a malformed tag takes. Tracking nobody on purpose is a disabled area.
    entityManager.GetComponent<InteractionArea3DComponent>(area).maximumCandidates = 0U;
    result = Scan();
    EXPECT_EQ(result.areaCount, 1U);
    EXPECT_EQ(result.refreshedAreaCount, 0U);
    ExpectCleared(area);
    EXPECT_EQ(result.truncatedAreaCount, 0U);
    EXPECT_FALSE(result.HasFocusedArea());

    // Room for everyone: no overflow flag at all.
    entityManager.GetComponent<InteractionArea3DComponent>(area).maximumCandidates = 16U;
    result = Scan();
    EXPECT_EQ(Area(area).interactorCount, 3U);
    EXPECT_FALSE(Area(area).interactorsTruncated);
    EXPECT_EQ(result.truncatedAreaCount, 0U);
}

TEST_F(InteractionAreaTest, ADisabledAreaClearsWhatItWasTrackingAndComesBackWhenEnabled) {
    const Entity area = MakeArea({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeInteractor({0.0F, 0.0F, 0.0F}));
    static_cast<void>(Scan());
    ASSERT_EQ(Area(area).interactorCount, 1U);

    entityManager.GetComponent<InteractionArea3DComponent>(area).enabled = false;
    const InteractionAreaScanResult disabled = Scan();
    ExpectCleared(area);
    EXPECT_EQ(disabled.refreshedAreaCount, 0U); // visited and cleared, but not evaluated
    EXPECT_EQ(disabled.areaCount, 1U);
    EXPECT_FALSE(disabled.HasFocusedArea());

    entityManager.GetComponent<InteractionArea3DComponent>(area).enabled = true;
    static_cast<void>(Scan());
    EXPECT_EQ(Area(area).interactorCount, 1U);
    EXPECT_TRUE(Area(area).focusedByPrimaryInteractor);
}

TEST_F(InteractionAreaTest, AnInvalidOrUnsweptAreaClearsItsStateRatherThanKeepingTheLastFrame) {
    // An area whose authored contract is broken: the validator refuses an empty interactionTag.
    const Entity invalid = MakeArea({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeInteractor({0.0F, 0.0F, 0.0F}));
    static_cast<void>(Scan());
    ASSERT_EQ(Area(invalid).interactorCount, 1U);
    entityManager.GetComponent<InteractionArea3DComponent>(invalid).interactionTag.clear();

    // A second area with no world transform at all: authored, never swept, never evaluated.
    const Entity unswept = entityManager.CreateEntity();
    entityManager.AddComponent<InteractionArea3DComponent>(
        unswept, InteractionArea3DComponent{});

    const InteractionAreaScanResult result = Scan();
    EXPECT_EQ(result.areaCount, 2U);
    EXPECT_EQ(result.refreshedAreaCount, 0U);
    ExpectCleared(invalid);
    ExpectCleared(unswept);
    EXPECT_FALSE(result.HasFocusedArea());
}

TEST_F(InteractionAreaTest, AcceptanceIsSymmetricInBothDirections) {
    const Entity area = MakeArea({0.0F, 0.0F, 0.0F});
    const Entity interactor = MakeInteractor({0.0F, 0.0F, 0.0F});
    static_cast<void>(Scan());
    ASSERT_EQ(Area(area).interactorCount, 1U);

    // The interactor's mask stops accepting the area's layer...
    entityManager.GetComponent<ColliderComponent>(interactor).collisionMask = 0x2U;
    static_cast<void>(Scan());
    ExpectCleared(area);

    // ...and the area's mask stops accepting the interactor's layer. Either direction alone is
    // enough to end participation - symmetric acceptance, the AreaOverlapSystem rule.
    entityManager.GetComponent<ColliderComponent>(interactor).collisionMask = 0xFFFFFFFFU;
    entityManager.GetComponent<InteractionArea3DComponent>(area).collisionMask = 0x2U;
    static_cast<void>(Scan());
    ExpectCleared(area);

    // Both accept again: participation comes back with no leftover state.
    entityManager.GetComponent<InteractionArea3DComponent>(area).collisionMask = 0xFFFFFFFFU;
    const InteractionAreaScanResult result = Scan();
    EXPECT_EQ(Area(area).interactorCount, 1U);
    EXPECT_TRUE(result.HasFocusedArea());
}

TEST_F(InteractionAreaTest, AnAreaNeverListsTheInteractorLivingOnItsOwnEntity) {
    // The odd case an author can actually build: one entity that is both the area and a
    // character controller. It overlaps itself trivially, and must not list itself.
    const Entity both = MakeArea({0.0F, 0.0F, 0.0F});
    entityManager.AddComponent<ColliderComponent>(both, ColliderComponent{});
    entityManager.AddComponent<Scene::CharacterControllerComponent>(
        both, Scene::CharacterControllerComponent{});

    const InteractionAreaScanResult result = Scan();
    EXPECT_EQ(result.interactorCount, 1U); // it IS an interactor...
    EXPECT_EQ(result.primaryInteractor, both);
    ExpectCleared(both); // ...but never its own area's occupant, and it earns no focus
    EXPECT_FALSE(result.HasFocusedArea());
}

TEST_F(InteractionAreaTest, OnlyControllersWithAUsableColliderEverInteract) {
    const Entity area = MakeArea({0.0F, 0.0F, 0.0F});
    // A collider with no controller: scenery, not a player.
    static_cast<void>(MakePlainCollider({0.0F, 0.0F, 0.0F}));
    // A controller whose collider is invalid (a zero collision layer can never interact).
    ColliderComponent dead;
    dead.collisionLayer = 0U;
    static_cast<void>(MakeInteractor({0.0F, 0.0F, 0.0F}, dead));

    const InteractionAreaScanResult none = Scan();
    EXPECT_EQ(none.interactorCount, 0U);
    ExpectCleared(area);
    EXPECT_FALSE(none.HasPrimaryInteractor());

    // The real thing: a capsule is shape-aware, so its half extents are the capsule's own, not
    // the box defaults - a capsule tall enough to reach the area overlaps it.
    ColliderComponent capsule;
    capsule.shapeType = ColliderShapeType::Capsule;
    capsule.radius = 0.5F;
    capsule.height = 3.0F;
    const Entity tall = MakeInteractor({0.9F, 0.0F, 0.0F}, capsule);
    const InteractionAreaScanResult one = Scan();
    EXPECT_EQ(one.interactorCount, 1U);
    EXPECT_EQ(one.primaryInteractor, tall);
    EXPECT_EQ(Area(area).interactorCount, 1U);
}

TEST_F(InteractionAreaTest, TouchingBoundariesAreNotOverlaps) {
    // Two unit boxes whose faces meet exactly: the 15-axis test treats that as separation.
    const Entity area = MakeArea({0.0F, 0.0F, 0.0F}, {}, {0.5F, 0.5F, 0.5F});
    static_cast<void>(MakeInteractor({1.0F, 0.0F, 0.0F}));
    static_cast<void>(Scan());
    ExpectCleared(area);

    // One step inside and they overlap.
    static_cast<void>(MakeInteractor({0.999F, 0.0F, 0.0F}));
    const InteractionAreaScanResult inside = Scan();
    EXPECT_EQ(inside.interactorCount, 2U);
    EXPECT_EQ(Area(area).interactorCount, 1U);
}

TEST_F(InteractionAreaTest, OnlyThePrimaryInteractorEarnsFocusAndItGoesToTheNearestArea) {
    // Two controllers: `first` is the primary by content order, `second` is closer to the far
    // area. Focus belongs to the primary alone, so the far area must still win.
    const Entity nearFirst = MakeArea({-0.6F, 0.0F, 0.0F});
    const Entity nearSecond = MakeArea({0.6F, 0.0F, 0.0F});
    const Entity first = MakeInteractor({-0.6F, 0.0F, 0.0F});
    const Entity second = MakeInteractor({0.6F, 0.0F, 0.0F});

    const InteractionAreaScanResult result = Scan();
    EXPECT_EQ(result.interactorCount, 2U);
    EXPECT_EQ(result.primaryInteractor, first); // (index, generation): the first made wins
    EXPECT_NE(result.primaryInteractor, second); // the nearer-to-the-far-area controller is not it
    EXPECT_EQ(Area(nearFirst).interactorCount, 2U);  // both areas track both controllers
    EXPECT_EQ(Area(nearSecond).interactorCount, 2U);
    EXPECT_TRUE(Area(nearFirst).focusedByPrimaryInteractor);
    EXPECT_FALSE(Area(nearSecond).focusedByPrimaryInteractor);
    EXPECT_EQ(result.focusedArea, nearFirst);

    // Move the primary next to the other area: the focus follows it, nothing sticks.
    Move(first, {0.6F, 0.0F, 0.0F});
    static_cast<void>(MakeInteractor({0.0F, 0.0F, 0.0F}));
    const InteractionAreaScanResult moved = Scan();
    EXPECT_EQ(moved.primaryInteractor, first);
    EXPECT_FALSE(Area(nearFirst).focusedByPrimaryInteractor);
    EXPECT_TRUE(Area(nearSecond).focusedByPrimaryInteractor);
    EXPECT_EQ(moved.focusedArea, nearSecond);
}

TEST_F(InteractionAreaTest, EqualDistanceTiesBreakOnContentOrderDeterministically) {
    // Two areas symmetric about the interactor: equal squared distance, so the tie goes to the
    // lower (index, generation) handle - every run, every machine.
    const Entity left = MakeArea({-0.5F, 0.0F, 0.0F});
    const Entity right = MakeArea({0.5F, 0.0F, 0.0F});
    static_cast<void>(MakeInteractor({0.0F, 0.0F, 0.0F}));

    const InteractionAreaScanResult result = Scan();
    EXPECT_EQ(result.focusedArea, left);
    EXPECT_TRUE(Area(left).focusedByPrimaryInteractor);
    EXPECT_FALSE(Area(right).focusedByPrimaryInteractor);

    // The same scene asked again answers identically - no state, no drift.
    const InteractionAreaScanResult again = Scan();
    EXPECT_EQ(again.focusedArea, left);
}

TEST_F(InteractionAreaTest, ASceneWithNoInteractorFocusesNothing) {
    const Entity area = MakeArea({0.0F, 0.0F, 0.0F});
    const InteractionAreaScanResult result = Scan();
    EXPECT_EQ(result.interactorCount, 0U);
    EXPECT_EQ(result.areaCount, 1U);
    EXPECT_EQ(result.refreshedAreaCount, 1U);
    EXPECT_FALSE(result.HasPrimaryInteractor());
    EXPECT_FALSE(result.HasFocusedArea());
    ExpectCleared(area);
}

TEST_F(InteractionAreaTest, ASceneWithNoAreasStillFindsTheInteractors) {
    static_cast<void>(MakeInteractor({0.0F, 0.0F, 0.0F}));
    const InteractionAreaScanResult result = Scan();
    EXPECT_EQ(result.interactorCount, 1U);
    EXPECT_EQ(result.areaCount, 0U);
    EXPECT_EQ(result.refreshedAreaCount, 0U);
    EXPECT_FALSE(result.HasFocusedArea());
}

TEST_F(InteractionAreaTest, ADegenerateWorldRotationFallsBackToIdentityInsteadOfDroppingTheArea) {
    // A zero quaternion in the world transform is a pose the sweep could not normalize: the area
    // falls back to identity rather than dropping out, so an axis-aligned overlap still counts.
    const Entity area = MakeArea({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeInteractor({0.0F, 0.0F, 0.0F}));
    static_cast<void>(Scan());
    ASSERT_EQ(Area(area).interactorCount, 1U);

    auto world = entityManager.GetComponent<Scene::WorldTransformComponent>(area);
    world.worldRotation = Math::Quaternion{0.0F, 0.0F, 0.0F, 0.0F};
    entityManager.RemoveComponent<Scene::WorldTransformComponent>(area);
    entityManager.AddComponent<Scene::WorldTransformComponent>(area, world);
    const InteractionAreaScanResult result = Scan();
    EXPECT_EQ(result.refreshedAreaCount, 1U);
    EXPECT_EQ(Area(area).interactorCount, 1U); // identity fallback, not a dropped area
}

TEST_F(InteractionAreaTest, ANonUnitWorldRotationStillTracksTheInteractor) {
    // A world quaternion carrying numerical drift - the shape a long-lived sweep can leave behind -
    // is normalized rather than trusted, so the drift cannot silently hide an interactor.
    ColliderComponent collider;
    collider.halfExtents = Math::Vector3{0.5F, 0.5F, 0.5F};
    const Entity interactor = MakeInteractor({0.0F, 0.0F, 0.0F}, collider);
    auto world = entityManager.GetComponent<Scene::WorldTransformComponent>(interactor);
    world.worldRotation = Math::Quaternion{0.0F, 0.1F, 0.0F, 0.99498743F}; // ~11.5 degrees
    entityManager.RemoveComponent<Scene::WorldTransformComponent>(interactor);
    entityManager.AddComponent<Scene::WorldTransformComponent>(interactor, world);

    const Entity area = MakeArea({0.7000F, 0.0F, 0.0F}, {}, {0.5F, 0.5F, 0.5F});
    static_cast<void>(Scan());
    EXPECT_EQ(Area(area).interactorCount, 1U);
}

TEST_F(InteractionAreaTest, TheScanWritesOnlyTheRuntimeHalfAndNeverTheAuthoredFields) {
    InteractionArea3DComponent authored;
    authored.halfExtents = Math::Vector3{1.0F, 1.0F, 1.0F};
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

    const Entity area = MakeArea({0.0F, 0.0F, 0.0F}, authored);
    ColliderComponent collider;
    collider.collisionLayer = 1U;
    collider.collisionMask = 0xFU;
    static_cast<void>(MakeInteractor({0.0F, 0.0F, 0.0F}, collider));

    const InteractionAreaScanResult result = Scan();
    const InteractionArea3DComponent& live = Area(area);
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

TEST_F(InteractionAreaTest, AnIgnoredInteractorIsNeverListed) {
    const Entity area = MakeArea({0.0F, 0.0F, 0.0F});
    const Entity ignored = MakeInteractor({-0.5F, 0.0F, 0.0F});
    const Entity other = MakeInteractor({0.5F, 0.0F, 0.0F});
    entityManager.GetComponent<InteractionArea3DComponent>(area).ignoreEntity = ignored;

    InteractionAreaScanResult result = Scan();
    ASSERT_EQ(Area(area).interactorCount, 1U);
    EXPECT_EQ(Area(area).interactors[0], other);
    EXPECT_EQ(result.primaryInteractor, ignored);
    EXPECT_FALSE(Area(area).focusedByPrimaryInteractor);

    entityManager.GetComponent<InteractionArea3DComponent>(area).ignoreEntity = kInvalidEntity;
    result = Scan();
    EXPECT_EQ(Area(area).interactorCount, 2U);
    EXPECT_TRUE(Area(area).focusedByPrimaryInteractor);
}

TEST_F(InteractionAreaTest, ADisabledColliderDoesNotInteract) {
    const Entity area = MakeArea({0.0F, 0.0F, 0.0F});
    const Entity interactor = MakeInteractor({0.0F, 0.0F, 0.0F});
    static_cast<void>(Scan());
    ASSERT_EQ(Area(area).interactorCount, 1U);

    entityManager.GetComponent<ColliderComponent>(interactor).disabled = true;
    const InteractionAreaScanResult disabled = Scan();
    EXPECT_EQ(disabled.interactorCount, 0U);
    ExpectCleared(area);
    EXPECT_FALSE(disabled.HasPrimaryInteractor());
}

TEST_F(InteractionAreaTest, TheTagIsCarriedAndDeliberatelyDoesNotFilter) {
    // Two areas with different tags both track the same controller: tag-based gating belongs to
    // the gameplay layer that acts on the focus, and inventing a rule here would silently change
    // what an authored scene means.
    InteractionArea3DComponent shop;
    shop.interactionTag = "shop";
    InteractionArea3DComponent door;
    door.interactionTag = "door";
    const Entity shopArea = MakeArea({0.0F, 0.0F, 0.0F}, shop);
    const Entity doorArea = MakeArea({0.0F, 0.0F, 0.0F}, door);
    static_cast<void>(MakeInteractor({0.0F, 0.0F, 0.0F}));

    const InteractionAreaScanResult result = Scan();
    EXPECT_EQ(Area(shopArea).interactorCount, 1U);
    EXPECT_EQ(Area(doorArea).interactorCount, 1U);
    // Both overlap equally, so the tie goes to the lower handle - the tag plays no part in it.
    EXPECT_EQ(result.focusedArea, shopArea);
}

} // namespace
} // namespace UVE::Physics::Tests
