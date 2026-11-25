// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "scene_graph.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "event_system.h"
#include "memory_manager.h"
#include "auto_translate_component.h"
#include "hierarchy_component.h"
#include "process_component.h"
#include "thread_group_component.h"
#include "visibility_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "platform.h"

namespace UVE::Scene::Tests {
namespace {

constexpr float kEpsilon = 1e-4F;

/// Sign-agnostic: q and -q are the same rotation.
[[nodiscard]] bool RepresentSameRotationForTransform(const Math::Quaternion& lhs,
                                                        const Math::Quaternion& rhs) {
    const float dot = lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
    return std::abs(std::abs(dot) - 1.0F) <= 1e-4F;
}


class SceneGraphTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneGraph sceneGraph;
};

TEST_F(SceneGraphTest, AttachTransform_AddsAllThreeComponents) {
    const Entity entity = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});

    EXPECT_TRUE(entityManager.HasComponent<TransformComponent>(entity));
    EXPECT_TRUE(entityManager.HasComponent<WorldTransformComponent>(entity));
    EXPECT_TRUE(entityManager.HasComponent<HierarchyComponent>(entity));
    EXPECT_EQ(entityManager.GetComponent<HierarchyComponent>(entity).parent, kInvalidEntity);
}

TEST(TransformComponentTest, IsTransformComponentValid_RejectsNonFiniteAndNonUnitValues) {
    TransformComponent transform;
    EXPECT_TRUE(IsTransformComponentValid(transform));

    transform.localPosition.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsTransformComponentValid(transform));
    transform = TransformComponent{};
    transform.localScale.z = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsTransformComponentValid(transform));
    transform = TransformComponent{};
    transform.localRotation.w = 0.5F;
    EXPECT_FALSE(IsTransformComponentValid(transform));
    transform.localRotation = Math::Quaternion{0.0F, 0.0F, 0.0F, 1.0004F};
    EXPECT_TRUE(IsTransformComponentValid(transform));
}

#if UVE_DEBUG
TEST_F(SceneGraphTest, InvalidMutationInputsAssertBeforePublication) {
    const Entity unattached = entityManager.CreateEntity();
    TransformComponent invalidTransform{};
    invalidTransform.localPosition.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_DEATH({ sceneGraph.AttachTransform(entityManager, unattached, invalidTransform); }, "");

    const Entity attached = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, attached, TransformComponent{});
    EXPECT_DEATH({ sceneGraph.SetLocalTransform(entityManager, attached, invalidTransform); }, "");

    const Entity invalidParent{0xFFFFFFFFU, 1U};
    EXPECT_DEATH({ sceneGraph.SetParent(entityManager, attached, invalidParent); }, "");
}
#else
TEST_F(SceneGraphTest, InvalidMutationInputsFailClosedWithoutPublication) {
    const Entity unattached = entityManager.CreateEntity();
    TransformComponent invalidTransform{};
    invalidTransform.localPosition.x = std::numeric_limits<float>::quiet_NaN();
    sceneGraph.AttachTransform(entityManager, unattached, invalidTransform);
    EXPECT_FALSE(entityManager.HasComponent<TransformComponent>(unattached));

    const Entity attached = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, attached, TransformComponent{});
    sceneGraph.Update(entityManager);
    const TransformComponent before = entityManager.GetComponent<TransformComponent>(attached);
    const WorldTransformComponent worldBefore = entityManager.GetComponent<WorldTransformComponent>(attached);
    sceneGraph.SetLocalTransform(entityManager, attached, invalidTransform);
    EXPECT_EQ(entityManager.GetComponent<TransformComponent>(attached).localPosition, before.localPosition);
    EXPECT_EQ(entityManager.GetComponent<WorldTransformComponent>(attached).dirty, worldBefore.dirty);

    const Entity invalidParent{0xFFFFFFFFU, 1U};
    sceneGraph.SetParent(entityManager, attached, invalidParent);
    EXPECT_EQ(entityManager.GetComponent<HierarchyComponent>(attached).parent, kInvalidEntity);
}
#endif

TEST_F(SceneGraphTest, Update_RootEntity_WorldEqualsLocal) {
    const Entity entity = entityManager.CreateEntity();
    TransformComponent local;
    local.localPosition = Math::Vector3{1.0F, 2.0F, 3.0F};
    sceneGraph.AttachTransform(entityManager, entity, local);

    sceneGraph.Update(entityManager);

    const WorldTransformComponent& world = entityManager.GetComponent<WorldTransformComponent>(entity);
    EXPECT_FALSE(world.dirty);
    EXPECT_EQ(world.worldPosition, local.localPosition);
}

TEST_F(SceneGraphTest, SetLocalTransform_MarksDirty) {
    const Entity entity = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
    sceneGraph.Update(entityManager);
    ASSERT_FALSE(entityManager.GetComponent<WorldTransformComponent>(entity).dirty);

    TransformComponent newLocal;
    newLocal.localPosition = Math::Vector3{5.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, entity, newLocal);

    EXPECT_TRUE(entityManager.GetComponent<WorldTransformComponent>(entity).dirty);
}

TEST_F(SceneGraphTest, ParentChildComposition_CombinesPositionCorrectly) {
    const Entity parent = entityManager.CreateEntity();
    TransformComponent parentLocal;
    parentLocal.localPosition = Math::Vector3{10.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, parent, parentLocal);

    const Entity child = entityManager.CreateEntity();
    TransformComponent childLocal;
    childLocal.localPosition = Math::Vector3{0.0F, 5.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, child, childLocal);
    sceneGraph.SetParent(entityManager, child, parent);

    sceneGraph.Update(entityManager);

    const WorldTransformComponent& childWorld = entityManager.GetComponent<WorldTransformComponent>(child);
    EXPECT_NEAR(childWorld.worldPosition.x, 10.0F, kEpsilon);
    EXPECT_NEAR(childWorld.worldPosition.y, 5.0F, kEpsilon);
    EXPECT_NEAR(childWorld.worldPosition.z, 0.0F, kEpsilon);
}

TEST_F(SceneGraphTest, MultiLevelHierarchy_PropagatesInOnePass) {
    const Entity grandparent = entityManager.CreateEntity();
    TransformComponent grandparentLocal;
    grandparentLocal.localPosition = Math::Vector3{1.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, grandparent, grandparentLocal);

    const Entity parent = entityManager.CreateEntity();
    TransformComponent parentLocal;
    parentLocal.localPosition = Math::Vector3{1.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, parent, parentLocal);
    sceneGraph.SetParent(entityManager, parent, grandparent);

    const Entity child = entityManager.CreateEntity();
    TransformComponent childLocal;
    childLocal.localPosition = Math::Vector3{1.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, child, childLocal);
    sceneGraph.SetParent(entityManager, child, parent);

    sceneGraph.Update(entityManager);

    const WorldTransformComponent& childWorld = entityManager.GetComponent<WorldTransformComponent>(child);
    EXPECT_NEAR(childWorld.worldPosition.x, 3.0F, kEpsilon);
}

TEST_F(SceneGraphTest, Update_PreservesFiniteCacheWhenHierarchyCompositionOverflows) {
    const float maximumFloat = std::numeric_limits<float>::max();
    const Entity parent = entityManager.CreateEntity();
    TransformComponent parentLocal;
    parentLocal.localScale = Math::Vector3{maximumFloat, 1.0F, 1.0F};
    sceneGraph.AttachTransform(entityManager, parent, parentLocal);

    const Entity child = entityManager.CreateEntity();
    TransformComponent childLocal;
    childLocal.localPosition = Math::Vector3{2.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, child, childLocal);
    sceneGraph.SetParent(entityManager, child, parent);

    sceneGraph.Update(entityManager);

    const WorldTransformComponent& childWorld = entityManager.GetComponent<WorldTransformComponent>(child);
    EXPECT_TRUE(childWorld.dirty);
    EXPECT_TRUE(std::isfinite(childWorld.worldPosition.x));
    EXPECT_TRUE(std::isfinite(childWorld.worldPosition.y));
    EXPECT_TRUE(std::isfinite(childWorld.worldPosition.z));
    EXPECT_EQ(childWorld.worldPosition, (Math::Vector3{}));
}

TEST_F(SceneGraphTest, MovingParent_RecomputesChildEvenWhenChildNotDirty) {
    const Entity parent = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, parent, TransformComponent{});

    const Entity child = entityManager.CreateEntity();
    TransformComponent childLocal;
    childLocal.localPosition = Math::Vector3{1.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, child, childLocal);
    sceneGraph.SetParent(entityManager, child, parent);

    sceneGraph.Update(entityManager);
    ASSERT_NEAR(entityManager.GetComponent<WorldTransformComponent>(child).worldPosition.x, 1.0F, kEpsilon);

    // Move only the parent; the child's own dirty flag is never touched by SetLocalTransform
    // on a *different* entity, so it stays false right up until Update() runs.
    TransformComponent parentLocal;
    parentLocal.localPosition = Math::Vector3{100.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, parent, parentLocal);
    ASSERT_FALSE(entityManager.GetComponent<WorldTransformComponent>(child).dirty);

    sceneGraph.Update(entityManager);

    EXPECT_NEAR(entityManager.GetComponent<WorldTransformComponent>(child).worldPosition.x, 101.0F, kEpsilon);
}

#if UVE_DEBUG
TEST_F(SceneGraphTest, SetParentDeathTest_CycleRejected) {
    const Entity a = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, a, TransformComponent{});
    const Entity b = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, b, TransformComponent{});

    sceneGraph.SetParent(entityManager, b, a); // b's parent is a
    EXPECT_DEATH({ sceneGraph.SetParent(entityManager, a, b); }, "");
}
#endif

TEST_F(SceneGraphTest, GetChildren_ReturnsExactDirectChildren) {
    const Entity parent = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, parent, TransformComponent{});

    const Entity childA = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, childA, TransformComponent{});
    sceneGraph.SetParent(entityManager, childA, parent);

    const Entity childB = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, childB, TransformComponent{});
    sceneGraph.SetParent(entityManager, childB, parent);

    const Entity unrelated = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, unrelated, TransformComponent{});

    std::vector<Entity> children = sceneGraph.GetChildren(entityManager, parent);
    std::sort(children.begin(), children.end(), [](const Entity& lhs, const Entity& rhs) {
        return lhs.index < rhs.index;
    });

    std::vector<Entity> expected{childA, childB};
    std::sort(expected.begin(), expected.end(),
              [](const Entity& lhs, const Entity& rhs) { return lhs.index < rhs.index; });

    EXPECT_EQ(children, expected);
}

// ---------------------------------------------------------------------------
// Update's rewritten sweep.
//
// A completely static 5000-entity scene was spending 1353us per frame
// recomputing nothing, from an O(n^2) erase-from-the-middle and per-visit
// component re-lookups. Now 133us, about 10x, with the sweep semantics
// unchanged.
//
// The rewrite introduced two new hazards that did not exist before, and these
// tests target them specifically: SceneGraph now keeps scratch buffers
// BETWEEN calls (so leaked state would make the second update differ from the
// first), and the sweep holds raw component pointers captured by the initial
// walk (so anything that moved a component row mid-update would leave them
// dangling).
// ---------------------------------------------------------------------------

TEST_F(SceneGraphTest, Update_RepeatedCallsOnAStaticScene_AreIdempotent) {
    // The scratch buffers survive between calls now. If either leaked state, the second update
    // would disagree with the first - and every real frame calls this repeatedly.
    std::vector<Entity> entities;
    for (int i = 0; i < 40; ++i) {
        const Entity entity = entityManager.CreateEntity();
        TransformComponent local;
        local.localPosition = Math::Vector3{static_cast<float>(i), 2.0F, -3.0F};
        sceneGraph.AttachTransform(entityManager, entity, local);
        entities.push_back(entity);
    }

    sceneGraph.Update(entityManager);
    std::vector<Math::Vector3> first;
    for (const Entity entity : entities) {
        first.push_back(entityManager.GetComponent<WorldTransformComponent>(entity).worldPosition);
    }

    for (int pass = 0; pass < 5; ++pass) {
        sceneGraph.Update(entityManager);
    }

    for (std::size_t index = 0U; index < entities.size(); ++index) {
        const WorldTransformComponent& world =
            entityManager.GetComponent<WorldTransformComponent>(entities[index]);
        EXPECT_NEAR(world.worldPosition.x, first[index].x, kEpsilon);
        EXPECT_NEAR(world.worldPosition.y, first[index].y, kEpsilon);
        EXPECT_NEAR(world.worldPosition.z, first[index].z, kEpsilon);
        EXPECT_FALSE(world.dirty);
    }
}

TEST_F(SceneGraphTest, Update_AFreshInstanceMatchesAReusedOne) {
    // The scratch buffers must be scratch, not state: a SceneGraph that has already run must
    // produce exactly what a brand-new one produces for the same scene.
    const Entity parent = entityManager.CreateEntity();
    TransformComponent parentLocal;
    parentLocal.localPosition = Math::Vector3{10.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, parent, parentLocal);

    const Entity child = entityManager.CreateEntity();
    TransformComponent childLocal;
    childLocal.localPosition = Math::Vector3{0.0F, 5.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, child, childLocal);
    sceneGraph.SetParent(entityManager, child, parent);

    // Run the reused instance several times first, so any retained state would have accumulated.
    for (int pass = 0; pass < 3; ++pass) {
        sceneGraph.Update(entityManager);
    }
    const Math::Vector3 reusedResult =
        entityManager.GetComponent<WorldTransformComponent>(child).worldPosition;

    // Dirty everything again and run a completely fresh graph over the same scene.
    sceneGraph.SetLocalTransform(entityManager, parent, parentLocal);
    SceneGraph freshGraph;
    freshGraph.Update(entityManager);
    const Math::Vector3 freshResult =
        entityManager.GetComponent<WorldTransformComponent>(child).worldPosition;

    EXPECT_NEAR(freshResult.x, reusedResult.x, kEpsilon);
    EXPECT_NEAR(freshResult.y, reusedResult.y, kEpsilon);
    EXPECT_NEAR(freshResult.z, reusedResult.z, kEpsilon);
    EXPECT_NEAR(freshResult.x, 10.0F, kEpsilon);
    EXPECT_NEAR(freshResult.y, 5.0F, kEpsilon);
}

TEST_F(SceneGraphTest, Update_DeepChainAcrossManySweeps_ResolvesEveryLevel) {
    // The compaction path only runs when entities are deferred to a later sweep, and a chain built
    // CHILD-FIRST guarantees that: each entity's parent is created after it, so the first sweep
    // can only resolve the root. This is the case the old erase-from-the-middle handled and the
    // new write-back compaction must handle identically.
    constexpr int kDepth = 60;
    std::vector<Entity> chain;
    for (int i = 0; i < kDepth; ++i) {
        const Entity entity = entityManager.CreateEntity();
        TransformComponent local;
        local.localPosition = Math::Vector3{1.0F, 0.0F, 0.0F};
        sceneGraph.AttachTransform(entityManager, entity, local);
        chain.push_back(entity);
    }
    // chain[0] is the deepest descendant; chain[kDepth - 1] is the root.
    for (int i = 0; i + 1 < kDepth; ++i) {
        sceneGraph.SetParent(entityManager, chain[static_cast<std::size_t>(i)],
                                chain[static_cast<std::size_t>(i + 1)]);
    }

    sceneGraph.Update(entityManager);

    // Each level adds 1 on x, so the deepest descendant sits at kDepth.
    for (int i = 0; i < kDepth; ++i) {
        const WorldTransformComponent& world =
            entityManager.GetComponent<WorldTransformComponent>(chain[static_cast<std::size_t>(i)]);
        EXPECT_NEAR(world.worldPosition.x, static_cast<float>(kDepth - i), kEpsilon)
            << "depth index " << i;
        EXPECT_FALSE(world.dirty);
    }
}

TEST_F(SceneGraphTest, Update_MovingOnlyTheParent_StillMovesTheWholeSubtree) {
    // A parent that recomputes forces its children to recompute even though their own dirty flags
    // are false. That rule lives in the pass state, which the rewrite rebuilt - so it is worth
    // asserting directly rather than trusting it survived.
    const Entity root = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, root, TransformComponent{});
    const Entity middle = entityManager.CreateEntity();
    TransformComponent middleLocal;
    middleLocal.localPosition = Math::Vector3{0.0F, 1.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, middle, middleLocal);
    const Entity leaf = entityManager.CreateEntity();
    TransformComponent leafLocal;
    leafLocal.localPosition = Math::Vector3{0.0F, 1.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, leaf, leafLocal);
    sceneGraph.SetParent(entityManager, middle, root);
    sceneGraph.SetParent(entityManager, leaf, middle);
    sceneGraph.Update(entityManager);
    ASSERT_NEAR(entityManager.GetComponent<WorldTransformComponent>(leaf).worldPosition.y, 2.0F, kEpsilon);

    // Settle, so every dirty flag is clear and only the root is touched next.
    sceneGraph.Update(entityManager);
    ASSERT_FALSE(entityManager.GetComponent<WorldTransformComponent>(leaf).dirty);

    TransformComponent movedRoot;
    movedRoot.localPosition = Math::Vector3{100.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, root, movedRoot);
    sceneGraph.Update(entityManager);

    const WorldTransformComponent& leafWorld =
        entityManager.GetComponent<WorldTransformComponent>(leaf);
    EXPECT_NEAR(leafWorld.worldPosition.x, 100.0F, kEpsilon) << "the clean leaf must follow its moved ancestor";
    EXPECT_NEAR(leafWorld.worldPosition.y, 2.0F, kEpsilon);
}

TEST_F(SceneGraphTest, Update_ManyRootsInOneSweep_AllResolveWithoutDeferral) {
    // The common case and the one the rewrite targets: a flat scene of roots completes in a single
    // sweep with nothing written back. Enough entities to span several ECS chunks.
    constexpr int kEntityCount = 1100;
    std::vector<Entity> entities;
    for (int i = 0; i < kEntityCount; ++i) {
        const Entity entity = entityManager.CreateEntity();
        TransformComponent local;
        local.localPosition = Math::Vector3{static_cast<float>(i), 0.0F, 0.0F};
        sceneGraph.AttachTransform(entityManager, entity, local);
        entities.push_back(entity);
    }

    sceneGraph.Update(entityManager);

    for (int i = 0; i < kEntityCount; ++i) {
        const WorldTransformComponent& world =
            entityManager.GetComponent<WorldTransformComponent>(entities[static_cast<std::size_t>(i)]);
        EXPECT_NEAR(world.worldPosition.x, static_cast<float>(i), kEpsilon);
        EXPECT_FALSE(world.dirty);
    }
}

TEST_F(SceneGraphTest, Update_AfterEntitiesAreDestroyed_UsesTheCurrentScene) {
    // Destroying entities moves surviving component rows during chunk compaction. The sweep
    // captures component pointers during its initial walk, so this checks the capture happens
    // after the scene settled rather than surviving across a structural change.
    std::vector<Entity> entities;
    for (int i = 0; i < 30; ++i) {
        const Entity entity = entityManager.CreateEntity();
        TransformComponent local;
        local.localPosition = Math::Vector3{static_cast<float>(i), 0.0F, 0.0F};
        sceneGraph.AttachTransform(entityManager, entity, local);
        entities.push_back(entity);
    }
    sceneGraph.Update(entityManager);

    for (std::size_t index = 0U; index < entities.size(); index += 2U) {
        entityManager.DestroyEntity(entities[index]);
    }
    sceneGraph.Update(entityManager);

    for (std::size_t index = 1U; index < entities.size(); index += 2U) {
        const WorldTransformComponent& world =
            entityManager.GetComponent<WorldTransformComponent>(entities[index]);
        EXPECT_NEAR(world.worldPosition.x, static_cast<float>(index), kEpsilon)
            << "survivor " << index << " must keep its own transform after compaction";
    }
}

TEST_F(SceneGraphTest, Update_NonFiniteParent_StillInvalidatesItsSubtree) {
    // An invalid parent marks its whole subtree dirty and invalid rather than publishing garbage.
    // That branch sits on the compaction path, so the rewrite could plausibly have dropped it.
    //
    // The NaN is written straight into the component rather than through SetLocalTransform:
    // that setter validates its input and asserts (SIGTRAP in debug) on a non-finite transform, so
    // a corrupt local transform can only reach the sweep by some other route - a raw component
    // write, deserialization, or script. Going through the setter would be testing the setter's
    // guard, not the sweep's handling, and would abort before reaching the code under test. I
    // confirmed the assert is pre-existing by reproducing it against unmodified HEAD before
    // changing this test.
    const Entity parent = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, parent, TransformComponent{});
    const Entity child = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, child, TransformComponent{});
    sceneGraph.SetParent(entityManager, child, parent);
    sceneGraph.Update(entityManager);
    ASSERT_FALSE(entityManager.GetComponent<WorldTransformComponent>(child).dirty);

    entityManager.GetComponent<TransformComponent>(parent).localPosition.x =
        std::numeric_limits<float>::quiet_NaN();
    entityManager.GetComponent<WorldTransformComponent>(parent).dirty = true;

    sceneGraph.Update(entityManager);

    EXPECT_TRUE(entityManager.GetComponent<WorldTransformComponent>(parent).dirty)
        << "a parent that cannot produce a finite transform must stay dirty";
    EXPECT_TRUE(entityManager.GetComponent<WorldTransformComponent>(child).dirty)
        << "a child of an invalid parent must stay dirty rather than publish a derived value";
    EXPECT_TRUE(std::isfinite(entityManager.GetComponent<WorldTransformComponent>(child).worldPosition.x))
        << "the child must keep its last good value rather than inherit the NaN";
}

TEST_F(SceneGraphTest, Update_HidingAParentHidesItsWholeSubtree) {
    // The behaviour the component exists for. A parent's switch must reach every descendant, not
    // just its direct children - a two-level tree is the shortest case that can tell the
    // difference between real inheritance and a single-level copy.
    const Entity parent = entityManager.CreateEntity();
    const Entity child = entityManager.CreateEntity();
    const Entity grandchild = entityManager.CreateEntity();
    for (const Entity entity : {parent, child, grandchild}) {
        sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
        entityManager.AddComponent<VisibilityComponent>(entity, VisibilityComponent{});
    }
    sceneGraph.SetParent(entityManager, child, parent);
    sceneGraph.SetParent(entityManager, grandchild, child);

    entityManager.GetComponent<VisibilityComponent>(parent).visible = false;
    sceneGraph.Update(entityManager);

    EXPECT_FALSE(entityManager.GetComponent<VisibilityComponent>(parent).visibleInHierarchy);
    EXPECT_FALSE(entityManager.GetComponent<VisibilityComponent>(child).visibleInHierarchy);
    EXPECT_FALSE(entityManager.GetComponent<VisibilityComponent>(grandchild).visibleInHierarchy);

    // The authored switch on the descendants is untouched - only the derived field moved.
    EXPECT_TRUE(entityManager.GetComponent<VisibilityComponent>(child).visible);
    EXPECT_TRUE(entityManager.GetComponent<VisibilityComponent>(grandchild).visible);
}

TEST_F(SceneGraphTest, Update_ShowingAParentDoesNotShowAnIndependentlyHiddenChild) {
    // The reason there are two fields instead of one. If propagation wrote the authored flag, the
    // child's own choice would be destroyed the moment its parent was hidden, and showing the
    // parent again would wrongly reveal it. This is the case a single-flag implementation passes
    // every other test and fails here.
    const Entity parent = entityManager.CreateEntity();
    const Entity child = entityManager.CreateEntity();
    for (const Entity entity : {parent, child}) {
        sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
        entityManager.AddComponent<VisibilityComponent>(entity, VisibilityComponent{});
    }
    sceneGraph.SetParent(entityManager, child, parent);

    entityManager.GetComponent<VisibilityComponent>(child).visible = false;
    entityManager.GetComponent<VisibilityComponent>(parent).visible = false;
    sceneGraph.Update(entityManager);
    ASSERT_FALSE(entityManager.GetComponent<VisibilityComponent>(child).visibleInHierarchy);

    // Parent back on. The child stays hidden because it was hidden in its own right.
    entityManager.GetComponent<VisibilityComponent>(parent).visible = true;
    sceneGraph.Update(entityManager);

    EXPECT_TRUE(entityManager.GetComponent<VisibilityComponent>(parent).visibleInHierarchy);
    EXPECT_FALSE(entityManager.GetComponent<VisibilityComponent>(child).visibleInHierarchy)
        << "showing a parent must not override a child's own hidden state";
}

TEST_F(SceneGraphTest, Update_AnEntityWithoutTheComponentPassesVisibilityThrough) {
    // The component is optional, so most entities will not have one. An intermediate object without
    // it must not break the chain: hiding the grandparent still has to hide the grandchild.
    const Entity grandparent = entityManager.CreateEntity();
    const Entity middle = entityManager.CreateEntity();
    const Entity grandchild = entityManager.CreateEntity();
    for (const Entity entity : {grandparent, middle, grandchild}) {
        sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
    }
    // Deliberately only the ends carry the component; `middle` has none.
    entityManager.AddComponent<VisibilityComponent>(grandparent, VisibilityComponent{});
    entityManager.AddComponent<VisibilityComponent>(grandchild, VisibilityComponent{});
    sceneGraph.SetParent(entityManager, middle, grandparent);
    sceneGraph.SetParent(entityManager, grandchild, middle);

    entityManager.GetComponent<VisibilityComponent>(grandparent).visible = false;
    sceneGraph.Update(entityManager);

    EXPECT_FALSE(entityManager.GetComponent<VisibilityComponent>(grandchild).visibleInHierarchy)
        << "an object without the component must pass its parent's state through, not reset it";
}

TEST_F(SceneGraphTest, Update_ResolvesTickModeThroughAObjectThatDoesNotCarryTheComponent) {
    // Same rule visibility follows: an intermediate object that never opted in must pass its
    // parent's answer through rather than resetting the chain.
    const Entity root = entityManager.CreateEntity();
    const Entity middle = entityManager.CreateEntity();
    const Entity leaf = entityManager.CreateEntity();
    for (const Entity entity : {root, middle, leaf}) {
        sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
    }
    entityManager.AddComponent<ProcessComponent>(root, ProcessComponent{});
    entityManager.AddComponent<ProcessComponent>(leaf, ProcessComponent{});
    sceneGraph.SetParent(entityManager, middle, root);
    sceneGraph.SetParent(entityManager, leaf, middle);

    entityManager.GetComponent<ProcessComponent>(root).mode = TickMode::Never;
    sceneGraph.Update(entityManager);

    EXPECT_EQ(entityManager.GetComponent<ProcessComponent>(leaf).resolvedModeInHierarchy,
              TickMode::Never);
    EXPECT_FALSE(IsTicking(
        entityManager.GetComponent<ProcessComponent>(leaf).resolvedModeInHierarchy,
        /*simulationPaused=*/false));

    // A pause menu under a Running parent has to be able to say Always and be believed, which is
    // the whole reason the mode is authored per entity.
    entityManager.GetComponent<ProcessComponent>(root).mode = TickMode::Running;
    entityManager.GetComponent<ProcessComponent>(leaf).mode = TickMode::Always;
    sceneGraph.Update(entityManager);
    EXPECT_EQ(entityManager.GetComponent<ProcessComponent>(leaf).resolvedModeInHierarchy,
              TickMode::Always);
    EXPECT_TRUE(IsTicking(
        entityManager.GetComponent<ProcessComponent>(leaf).resolvedModeInHierarchy,
        /*simulationPaused=*/true));
}

TEST_F(SceneGraphTest, Update_AChildCannotEscapeAMainThreadAncestor) {
    const Entity root = entityManager.CreateEntity();
    const Entity child = entityManager.CreateEntity();
    for (const Entity entity : {root, child}) {
        sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
        entityManager.AddComponent<ThreadGroupComponent>(entity, ThreadGroupComponent{});
    }
    sceneGraph.SetParent(entityManager, child, root);

    entityManager.GetComponent<ThreadGroupComponent>(root).mode = ThreadGroupMode::MainThread;
    entityManager.GetComponent<ThreadGroupComponent>(child).mode = ThreadGroupMode::SubThread;
    sceneGraph.Update(entityManager);

    EXPECT_EQ(entityManager.GetComponent<ThreadGroupComponent>(child).resolvedModeInHierarchy,
              ThreadGroupMode::MainThread)
        << "a main-thread ancestor is a constraint about shared state, not a preference";

    entityManager.GetComponent<ThreadGroupComponent>(root).mode = ThreadGroupMode::SubThread;
    sceneGraph.Update(entityManager);
    EXPECT_EQ(entityManager.GetComponent<ThreadGroupComponent>(child).resolvedModeInHierarchy,
              ThreadGroupMode::SubThread);
}

TEST_F(SceneGraphTest, Update_TopLevelStillInheritsTheCommonObjectModes) {
    // topLevel cuts the transform chain only. A top-level child is still paused with its parent and
    // still translated with the menu it belongs to - the same reasoning that keeps visibility
    // inherited across it.
    const Entity root = entityManager.CreateEntity();
    const Entity child = entityManager.CreateEntity();
    for (const Entity entity : {root, child}) {
        sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
        entityManager.AddComponent<ProcessComponent>(entity, ProcessComponent{});
        entityManager.AddComponent<AutoTranslateComponent>(entity, AutoTranslateComponent{});
    }
    sceneGraph.SetParent(entityManager, child, root);
    entityManager.GetComponent<TransformComponent>(child).topLevel = true;

    entityManager.GetComponent<ProcessComponent>(root).mode = TickMode::PausedOnly;
    entityManager.GetComponent<AutoTranslateComponent>(root).mode = LocalizeMode::Literal;
    sceneGraph.Update(entityManager);

    EXPECT_EQ(entityManager.GetComponent<ProcessComponent>(child).resolvedModeInHierarchy,
              TickMode::PausedOnly);
    EXPECT_EQ(entityManager.GetComponent<AutoTranslateComponent>(child).resolvedModeInHierarchy,
              LocalizeMode::Literal);
}

TEST_F(SceneGraphTest, Update_ARootWithNoAncestorsResolvesToTheHierarchyDefaults) {
    const Entity root = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, root, TransformComponent{});
    entityManager.AddComponent<ProcessComponent>(root, ProcessComponent{});
    entityManager.AddComponent<ThreadGroupComponent>(root, ThreadGroupComponent{});
    entityManager.AddComponent<AutoTranslateComponent>(root, AutoTranslateComponent{});

    sceneGraph.Update(entityManager);

    // Inherit at the top of a hierarchy means the default, not "unanswered".
    EXPECT_EQ(entityManager.GetComponent<ProcessComponent>(root).resolvedModeInHierarchy,
              TickMode::Running);
    EXPECT_EQ(entityManager.GetComponent<ThreadGroupComponent>(root).resolvedModeInHierarchy,
              ThreadGroupMode::MainThread);
    EXPECT_EQ(entityManager.GetComponent<AutoTranslateComponent>(root).resolvedModeInHierarchy,
              LocalizeMode::Localized);
}

TEST_F(SceneGraphTest, TryGetResolvedObjectModes_AnswersForAObjectThatCarriesNoComponent) {
    // The case the query exists for. The resolved mode is written onto a component only when the
    // entity carries one, so a consumer that read the component alone would translate a label
    // with no Auto Translate component sitting under a Disabled menu. The query answers for it.
    const Entity menu = entityManager.CreateEntity();
    const Entity label = entityManager.CreateEntity();
    for (const Entity entity : {menu, label}) {
        sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
    }
    sceneGraph.SetParent(entityManager, label, menu);
    AutoTranslateComponent disabled{};
    disabled.mode = LocalizeMode::Literal;
    entityManager.AddComponent<AutoTranslateComponent>(menu, disabled);
    ProcessComponent always{};
    always.mode = TickMode::Always;
    entityManager.AddComponent<ProcessComponent>(menu, always);

    sceneGraph.Update(entityManager);

    ASSERT_FALSE(entityManager.HasComponent<AutoTranslateComponent>(label));
    const std::optional<ResolvedObjectModes> resolved = sceneGraph.TryGetResolvedObjectModes(label);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->autoTranslate, LocalizeMode::Literal);
    EXPECT_EQ(resolved->process, TickMode::Always);
    EXPECT_EQ(resolved->threadGroup, ThreadGroupMode::MainThread);
}

TEST_F(SceneGraphTest, TryGetResolvedObjectModes_IsEmptyForAnEntityTheUpdateNeverSaw) {
    // Not a scene-graph object at all, and an object created after the last update: in both cases
    // there is no answer yet, and saying so is better than inventing the default.
    const Entity bare = entityManager.CreateEntity();
    sceneGraph.Update(entityManager);
    EXPECT_FALSE(sceneGraph.TryGetResolvedObjectModes(bare).has_value());

    const Entity late = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, late, TransformComponent{});
    EXPECT_FALSE(sceneGraph.TryGetResolvedObjectModes(late).has_value());
    sceneGraph.Update(entityManager);
    ASSERT_TRUE(sceneGraph.TryGetResolvedObjectModes(late).has_value());
    // A root that opted into nothing resolves to the hierarchy defaults, never to Inherit.
    EXPECT_EQ(*sceneGraph.TryGetResolvedObjectModes(late), ResolvedObjectModes{});
}

// A pure Object is in the hierarchy with no transform of its own - the scene root is one. The rule
// these lock: it passes the modes and visibility down like any object, and it cuts the transform
// chain, because there is no transform on it to compose from.

[[nodiscard]] Entity CreatePureObject(EntityManager& entityManager) {
    const Entity entity = entityManager.CreateEntity();
    entityManager.AddComponent<HierarchyComponent>(entity, HierarchyComponent{});
    return entity;
}

TEST_F(SceneGraphTest, Update_AChildOfAPureObjectStartsItsOwnTransformChain) {
    const Entity object = CreatePureObject(entityManager);
    const Entity child = entityManager.CreateEntity();
    TransformComponent local{};
    local.localPosition = Math::Vector3{3.0F, 4.0F, 5.0F};
    local.localScale = Math::Vector3{2.0F, 2.0F, 2.0F};
    sceneGraph.AttachTransform(entityManager, child, local);
    sceneGraph.SetParent(entityManager, child, object);

    sceneGraph.Update(entityManager);

    const WorldTransformComponent& world = entityManager.GetComponent<WorldTransformComponent>(child);
    EXPECT_FALSE(world.dirty);
    EXPECT_EQ(world.worldPosition, local.localPosition);
    EXPECT_EQ(world.worldScale, local.localScale);
    // The pure Object gained nothing: the sweep never gives it a transform.
    EXPECT_FALSE(entityManager.HasComponent<WorldTransformComponent>(object));
}

TEST_F(SceneGraphTest, Update_APureObjectBetweenSpatialObjectsCutsTheChainButNotTheModes) {
    // Spatial -> pure -> spatial. The grandchild must not compose from the spatial grandparent:
    // the object in between has no transform, so the chain restarts there. The modes and
    // visibility, which do not depend on a transform, still reach it.
    const Entity grandparent = entityManager.CreateEntity();
    TransformComponent moved{};
    moved.localPosition = Math::Vector3{100.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, grandparent, moved);
    ProcessComponent always{};
    always.mode = TickMode::Always;
    entityManager.AddComponent<ProcessComponent>(grandparent, always);
    VisibilityComponent hidden{};
    hidden.visible = false;
    entityManager.AddComponent<VisibilityComponent>(grandparent, hidden);

    const Entity object = CreatePureObject(entityManager);
    // SetParent is for spatial children; a pure Object's parent is authored directly.
    entityManager.GetComponent<HierarchyComponent>(object).parent = grandparent;
    AutoTranslateComponent disabled{};
    disabled.mode = LocalizeMode::Literal;
    entityManager.AddComponent<AutoTranslateComponent>(object, disabled);

    const Entity grandchild = entityManager.CreateEntity();
    TransformComponent local{};
    local.localPosition = Math::Vector3{1.0F, 2.0F, 3.0F};
    sceneGraph.AttachTransform(entityManager, grandchild, local);
    sceneGraph.SetParent(entityManager, grandchild, object);
    entityManager.AddComponent<VisibilityComponent>(grandchild, VisibilityComponent{});

    sceneGraph.Update(entityManager);

    EXPECT_EQ(entityManager.GetComponent<WorldTransformComponent>(grandchild).worldPosition,
              local.localPosition);
    EXPECT_FALSE(entityManager.GetComponent<VisibilityComponent>(grandchild).visibleInHierarchy);

    const std::optional<ResolvedObjectModes> resolved = sceneGraph.TryGetResolvedObjectModes(grandchild);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->process, TickMode::Always);
    EXPECT_EQ(resolved->autoTranslate, LocalizeMode::Literal);
}

TEST_F(SceneGraphTest, TryGetResolvedObjectModes_AnswersForThePureObjectItself) {
    // The scene root's own Process and Thread Group settings are what its whole scene inherits, so
    // the pure Object carrying them must have an answer of its own, published to its components.
    const Entity root = CreatePureObject(entityManager);
    ProcessComponent whenPaused{};
    whenPaused.mode = TickMode::PausedOnly;
    entityManager.AddComponent<ProcessComponent>(root, whenPaused);
    ThreadGroupComponent subThread{};
    subThread.mode = ThreadGroupMode::SubThread;
    entityManager.AddComponent<ThreadGroupComponent>(root, subThread);

    const Entity child = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, child, TransformComponent{});
    sceneGraph.SetParent(entityManager, child, root);

    sceneGraph.Update(entityManager);

    const std::optional<ResolvedObjectModes> own = sceneGraph.TryGetResolvedObjectModes(root);
    ASSERT_TRUE(own.has_value());
    EXPECT_EQ(own->process, TickMode::PausedOnly);
    EXPECT_EQ(own->threadGroup, ThreadGroupMode::SubThread);
    EXPECT_EQ(entityManager.GetComponent<ProcessComponent>(root).resolvedModeInHierarchy,
              TickMode::PausedOnly);

    const std::optional<ResolvedObjectModes> inherited = sceneGraph.TryGetResolvedObjectModes(child);
    ASSERT_TRUE(inherited.has_value());
    EXPECT_EQ(inherited->process, TickMode::PausedOnly);
    EXPECT_EQ(inherited->threadGroup, ThreadGroupMode::SubThread);
}

TEST_F(SceneGraphTest, Update_ReparentingRecomputesInheritedVisibility) {
    // Moving an object between a hidden and a visible parent has to change its answer. Nothing else
    // in the sweep is keyed on the old parent, so a cached result would survive the move.
    const Entity hiddenParent = entityManager.CreateEntity();
    const Entity shownParent = entityManager.CreateEntity();
    const Entity child = entityManager.CreateEntity();
    for (const Entity entity : {hiddenParent, shownParent, child}) {
        sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
        entityManager.AddComponent<VisibilityComponent>(entity, VisibilityComponent{});
    }
    entityManager.GetComponent<VisibilityComponent>(hiddenParent).visible = false;

    sceneGraph.SetParent(entityManager, child, hiddenParent);
    sceneGraph.Update(entityManager);
    ASSERT_FALSE(entityManager.GetComponent<VisibilityComponent>(child).visibleInHierarchy);

    sceneGraph.SetParent(entityManager, child, shownParent);
    sceneGraph.Update(entityManager);
    EXPECT_TRUE(entityManager.GetComponent<VisibilityComponent>(child).visibleInHierarchy);

    // And back, so the test is not passing on a one-way transition.
    sceneGraph.SetParent(entityManager, child, hiddenParent);
    sceneGraph.Update(entityManager);
    EXPECT_FALSE(entityManager.GetComponent<VisibilityComponent>(child).visibleInHierarchy);
}

TEST_F(SceneGraphTest, Update_ANonFiniteTransformDoesNotUnhideASubtree) {
    // Being hidden and having a broken transform are separate failures, and the sweep has a
    // dedicated early-out arm for an invalid parent. If that arm skipped the visibility work, a
    // single NaN anywhere in a hidden subtree would quietly reveal everything below it - a bug
    // that needs both conditions at once to appear.
    const Entity hiddenRoot = entityManager.CreateEntity();
    const Entity broken = entityManager.CreateEntity();
    const Entity belowBroken = entityManager.CreateEntity();
    for (const Entity entity : {hiddenRoot, broken, belowBroken}) {
        sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
        entityManager.AddComponent<VisibilityComponent>(entity, VisibilityComponent{});
    }
    sceneGraph.SetParent(entityManager, broken, hiddenRoot);
    sceneGraph.SetParent(entityManager, belowBroken, broken);

    entityManager.GetComponent<VisibilityComponent>(hiddenRoot).visible = false;
    entityManager.GetComponent<TransformComponent>(broken).localPosition.x =
        std::numeric_limits<float>::quiet_NaN();
    sceneGraph.Update(entityManager);

    EXPECT_FALSE(entityManager.GetComponent<VisibilityComponent>(broken).visibleInHierarchy);
    EXPECT_FALSE(entityManager.GetComponent<VisibilityComponent>(belowBroken).visibleInHierarchy)
        << "a broken transform must not cancel a hidden ancestor";
}

TEST_F(SceneGraphTest, Update_DefaultVisibilityIsVisible) {
    // The default has to be visible, or adding the component to an entity would make it vanish -
    // which is the opposite of what someone reaching for a visibility toggle expects.
    const Entity entity = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
    entityManager.AddComponent<VisibilityComponent>(entity, VisibilityComponent{});

    sceneGraph.Update(entityManager);

    EXPECT_TRUE(entityManager.GetComponent<VisibilityComponent>(entity).visible);
    EXPECT_TRUE(entityManager.GetComponent<VisibilityComponent>(entity).visibleInHierarchy);
    EXPECT_TRUE(IsVisibilityComponentValid(VisibilityComponent{}));
}

TEST(TransformRotationAuthoringTest, TypedAnglesSurviveGimbalLock) {
    // Measured before this field existed: typing (90, 45, 30) and reading it back gave
    // (60, 90, 45). The rotation was right; the numbers the author typed were gone, and there was
    // no way to get them back because a quaternion does not record which of the infinitely many
    // angle triples at a pole you meant.
    constexpr float kDegreesToRadians = 3.14159265F / 180.0F;
    TransformComponent transform;
    transform.localEulerRadians =
        Math::Vector3{90.0F * kDegreesToRadians, 45.0F * kDegreesToRadians, 30.0F * kDegreesToRadians};
    ASSERT_TRUE(TrySyncRotationFromEuler(transform));

    Math::Vector3 shown{};
    ASSERT_TRUE(TryGetDisplayEuler(transform, shown));
    EXPECT_NEAR(shown.x / kDegreesToRadians, 90.0F, 1e-3F);
    EXPECT_NEAR(shown.y / kDegreesToRadians, 45.0F, 1e-3F);
    EXPECT_NEAR(shown.z / kDegreesToRadians, 30.0F, 1e-3F);

    // And the rotation itself is still correct - keeping the angles must not cost correctness.
    EXPECT_TRUE(IsTransformComponentValid(transform));
}

TEST(TransformRotationAuthoringTest, TurnsBeyondAFullCircleAreNotFoldedAway) {
    // 370 degrees used to read back as 10. That is not a rounding difference: a quaternion cannot
    // tell one turn from two, so an author animating a full spin plus a bit lost the spin.
    constexpr float kDegreesToRadians = 3.14159265F / 180.0F;
    TransformComponent transform;
    transform.localEulerRadians = Math::Vector3{0.0F, 370.0F * kDegreesToRadians, 0.0F};
    ASSERT_TRUE(TrySyncRotationFromEuler(transform));

    Math::Vector3 shown{};
    ASSERT_TRUE(TryGetDisplayEuler(transform, shown));
    EXPECT_NEAR(shown.y / kDegreesToRadians, 370.0F, 1e-3F) << "the extra turn must be preserved";
}

TEST(TransformRotationAuthoringTest, RepeatedDisplayDoesNotDriftTheRotation) {
    // The worst of the three, because it needs no user action at all: re-extracting Euler angles
    // every redraw accumulated round-trip error, and a transform nobody touched drifted by up to
    // 54 degrees over a thousand frames. Storing the authored angles makes redraw a read.
    constexpr float kDegreesToRadians = 3.14159265F / 180.0F;
    TransformComponent transform;
    transform.localEulerRadians = Math::Vector3{23.7F * kDegreesToRadians, 41.3F * kDegreesToRadians,
                                                   67.9F * kDegreesToRadians};
    ASSERT_TRUE(TrySyncRotationFromEuler(transform));

    // A thousand draw-then-write-back cycles, which is what an Inspector left open does.
    for (int frame = 0; frame < 1000; ++frame) {
        Math::Vector3 shown{};
        ASSERT_TRUE(TryGetDisplayEuler(transform, shown));
        transform.localEulerRadians = shown;
        ASSERT_TRUE(TrySyncRotationFromEuler(transform));
    }

    Math::Vector3 finalAngles{};
    ASSERT_TRUE(TryGetDisplayEuler(transform, finalAngles));
    EXPECT_NEAR(finalAngles.x / kDegreesToRadians, 23.7F, 1e-3F);
    EXPECT_NEAR(finalAngles.y / kDegreesToRadians, 41.3F, 1e-3F);
    EXPECT_NEAR(finalAngles.z / kDegreesToRadians, 67.9F, 1e-3F);
}

TEST(TransformRotationAuthoringTest, QuaternionModeShowsTheQuaternionNotStaleAngles) {
    // When physics or a gizmo writes the quaternion, the stored Euler angles describe a rotation
    // the object no longer has. Showing them would be a lie, so Quaternion mode extracts instead -
    // the one case where a derived view is the honest answer.
    constexpr float kDegreesToRadians = 3.14159265F / 180.0F;
    TransformComponent transform;
    transform.localEulerRadians = Math::Vector3{10.0F * kDegreesToRadians, 20.0F * kDegreesToRadians,
                                                   30.0F * kDegreesToRadians};
    ASSERT_TRUE(TrySyncRotationFromEuler(transform));

    // Physics writes a rotation directly and declares the quaternion authoritative.
    Math::Quaternion fromPhysics{};
    ASSERT_TRUE(Math::TryMakeEulerOrdered(Math::Vector3{45.0F * kDegreesToRadians, 0.0F, 0.0F},
                                             Math::EulerOrder::XYZ, fromPhysics));
    transform.localRotation = fromPhysics;
    transform.rotationEditMode = RotationEditMode::Quaternion;

    Math::Vector3 shown{};
    ASSERT_TRUE(TryGetDisplayEuler(transform, shown));
    EXPECT_NEAR(shown.x / kDegreesToRadians, 45.0F, 1e-2F) << "must show the physics rotation";
    EXPECT_NEAR(shown.y / kDegreesToRadians, 0.0F, 1e-2F) << "not the stale authored 20 degrees";

    // And syncing must not overwrite what physics wrote.
    ASSERT_TRUE(TrySyncRotationFromEuler(transform));
    EXPECT_TRUE(RepresentSameRotationForTransform(transform.localRotation, fromPhysics))
        << "Quaternion mode must not replay the stale Euler angles over the live rotation";
}

TEST(TransformRotationAuthoringTest, EachOrderGivesADifferentRotationFromTheSameAngles) {
    // The dropdown has to mean something. Same typed angles, different order, different result -
    // otherwise the control is decoration.
    constexpr float kDegreesToRadians = 3.14159265F / 180.0F;
    const Math::Vector3 angles{35.0F * kDegreesToRadians, 50.0F * kDegreesToRadians,
                                  70.0F * kDegreesToRadians};
    TransformComponent xyz;
    xyz.localEulerRadians = angles;
    xyz.eulerOrder = Math::EulerOrder::XYZ;
    ASSERT_TRUE(TrySyncRotationFromEuler(xyz));

    TransformComponent zxy;
    zxy.localEulerRadians = angles;
    zxy.eulerOrder = Math::EulerOrder::ZXY;
    ASSERT_TRUE(TrySyncRotationFromEuler(zxy));

    EXPECT_FALSE(RepresentSameRotationForTransform(xyz.localRotation, zxy.localRotation));
    // Both must still be valid, normalized rotations.
    EXPECT_TRUE(IsTransformComponentValid(xyz));
    EXPECT_TRUE(IsTransformComponentValid(zxy));
}

TEST(TransformRotationAuthoringTest, ADefaultTransformIsUnchangedByTheNewFields) {
    // Every entity in every existing scene has a default-constructed transform. The new fields
    // must leave it exactly as it was: identity rotation, Euler mode, XYZ order.
    const TransformComponent transform;
    EXPECT_FLOAT_EQ(transform.localRotation.w, 1.0F);
    EXPECT_FLOAT_EQ(transform.localEulerRadians.x, 0.0F);
    EXPECT_EQ(transform.eulerOrder, Math::EulerOrder::XYZ);
    EXPECT_EQ(transform.rotationEditMode, RotationEditMode::Euler);
    EXPECT_TRUE(IsTransformComponentValid(transform));

    TransformComponent synced = transform;
    ASSERT_TRUE(TrySyncRotationFromEuler(synced));
    EXPECT_FLOAT_EQ(synced.localRotation.w, 1.0F) << "syncing a default must stay identity";
}

TEST(TransformRotationAuthoringTest, NonFiniteAnglesAreRejectedWithoutTouchingTheRotation) {
    // A bad edit must not be able to teleport an object. The previous rotation stays.
    TransformComponent transform;
    transform.localEulerRadians = Math::Vector3{0.5F, 0.5F, 0.5F};
    ASSERT_TRUE(TrySyncRotationFromEuler(transform));
    const Math::Quaternion good = transform.localRotation;

    transform.localEulerRadians = Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    EXPECT_FALSE(TrySyncRotationFromEuler(transform));
    EXPECT_FLOAT_EQ(transform.localRotation.x, good.x) << "a rejected edit must leave the rotation alone";
    EXPECT_FLOAT_EQ(transform.localRotation.w, good.w);
}

TEST_F(SceneGraphTest, Update_TopLevelIgnoresTheParentTransform) {
    // The whole point: local values become world values, as if the entity had no parent at all.
    const Entity parent = entityManager.CreateEntity();
    const Entity child = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, parent, TransformComponent{});
    sceneGraph.AttachTransform(entityManager, child, TransformComponent{});
    sceneGraph.SetParent(entityManager, child, parent);

    TransformComponent parentLocal;
    parentLocal.localPosition = Math::Vector3{100.0F, 50.0F, -20.0F};
    parentLocal.localScale = Math::Vector3{3.0F, 3.0F, 3.0F};
    sceneGraph.SetLocalTransform(entityManager, parent, parentLocal);

    TransformComponent childLocal;
    childLocal.localPosition = Math::Vector3{1.0F, 2.0F, 3.0F};
    childLocal.topLevel = true;
    sceneGraph.SetLocalTransform(entityManager, child, childLocal);
    sceneGraph.Update(entityManager);

    const WorldTransformComponent& world = entityManager.GetComponent<WorldTransformComponent>(child);
    EXPECT_NEAR(world.worldPosition.x, 1.0F, kEpsilon) << "the parent's offset must not apply";
    EXPECT_NEAR(world.worldPosition.y, 2.0F, kEpsilon);
    EXPECT_NEAR(world.worldPosition.z, 3.0F, kEpsilon);
    EXPECT_NEAR(world.worldScale.x, 1.0F, kEpsilon) << "nor the parent's scale";
}

TEST_F(SceneGraphTest, Update_TopLevelStillInheritsVisibility) {
    // Top level cuts the TRANSFORM chain only. If it also detached visibility there would be no
    // way to get the organisational half - saved and deleted with the parent - without losing the
    // ability to hide the group, which is most of why people parent things in the first place.
    const Entity parent = entityManager.CreateEntity();
    const Entity child = entityManager.CreateEntity();
    for (const Entity entity : {parent, child}) {
        sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
        entityManager.AddComponent<VisibilityComponent>(entity, VisibilityComponent{});
    }
    sceneGraph.SetParent(entityManager, child, parent);

    TransformComponent childLocal;
    childLocal.topLevel = true;
    sceneGraph.SetLocalTransform(entityManager, child, childLocal);
    entityManager.GetComponent<VisibilityComponent>(parent).visible = false;
    sceneGraph.Update(entityManager);

    EXPECT_FALSE(entityManager.GetComponent<VisibilityComponent>(child).visibleInHierarchy)
        << "hiding a parent must still hide a top-level child";
}

TEST_F(SceneGraphTest, Update_TopLevelSurvivesANonFiniteParent) {
    // An entity that does not read its parent's world transform cannot be corrupted by it.
    // Invalidating it anyway would invent a dependency it deliberately does not have - and would
    // make top level useless for the case it exists for, since a broken rig would take the
    // detached camera down with it.
    const Entity parent = entityManager.CreateEntity();
    const Entity normalChild = entityManager.CreateEntity();
    const Entity topLevelChild = entityManager.CreateEntity();
    for (const Entity entity : {parent, normalChild, topLevelChild}) {
        sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
    }
    sceneGraph.SetParent(entityManager, normalChild, parent);
    sceneGraph.SetParent(entityManager, topLevelChild, parent);

    TransformComponent detached;
    detached.localPosition = Math::Vector3{7.0F, 8.0F, 9.0F};
    detached.topLevel = true;
    sceneGraph.SetLocalTransform(entityManager, topLevelChild, detached);

    // Corrupt the parent directly - SetLocalTransform would reject a non-finite transform.
    entityManager.GetComponent<TransformComponent>(parent).localPosition.x =
        std::numeric_limits<float>::quiet_NaN();
    entityManager.GetComponent<WorldTransformComponent>(parent).dirty = true;
    sceneGraph.Update(entityManager);

    // The ordinary child is invalidated, as it always was.
    EXPECT_TRUE(entityManager.GetComponent<WorldTransformComponent>(normalChild).dirty);
    // The top-level one is untouched and correct.
    const WorldTransformComponent& detachedWorld =
        entityManager.GetComponent<WorldTransformComponent>(topLevelChild);
    EXPECT_FALSE(detachedWorld.dirty) << "a NaN above must not reach an entity that never reads it";
    EXPECT_NEAR(detachedWorld.worldPosition.x, 7.0F, kEpsilon);
}

TEST_F(SceneGraphTest, Update_ClearingTopLevelReattachesToTheParentTransform) {
    // The flag has to be reversible in place. Nothing else in the sweep is keyed on it, so a
    // cached world transform could otherwise survive the change.
    const Entity parent = entityManager.CreateEntity();
    const Entity child = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, parent, TransformComponent{});
    sceneGraph.AttachTransform(entityManager, child, TransformComponent{});
    sceneGraph.SetParent(entityManager, child, parent);

    TransformComponent parentLocal;
    parentLocal.localPosition = Math::Vector3{10.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, parent, parentLocal);

    TransformComponent childLocal;
    childLocal.localPosition = Math::Vector3{1.0F, 0.0F, 0.0F};
    childLocal.topLevel = true;
    sceneGraph.SetLocalTransform(entityManager, child, childLocal);
    sceneGraph.Update(entityManager);
    ASSERT_NEAR(entityManager.GetComponent<WorldTransformComponent>(child).worldPosition.x, 1.0F, kEpsilon);

    childLocal.topLevel = false;
    sceneGraph.SetLocalTransform(entityManager, child, childLocal);
    sceneGraph.Update(entityManager);
    EXPECT_NEAR(entityManager.GetComponent<WorldTransformComponent>(child).worldPosition.x, 11.0F, kEpsilon)
        << "clearing the flag must restore the parent offset";
}

TEST_F(SceneGraphTest, Update_TopLevelBecomesTheOriginForItsOwnChildren) {
    // A top-level entity is a root for the subtree BELOW it: its children compose from it as
    // normal. If they did not, the flag would flatten a whole branch instead of detaching one
    // object, and a detached rig would come apart.
    const Entity grandparent = entityManager.CreateEntity();
    const Entity detached = entityManager.CreateEntity();
    const Entity grandchild = entityManager.CreateEntity();
    for (const Entity entity : {grandparent, detached, grandchild}) {
        sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
    }
    sceneGraph.SetParent(entityManager, detached, grandparent);
    sceneGraph.SetParent(entityManager, grandchild, detached);

    TransformComponent grandparentLocal;
    grandparentLocal.localPosition = Math::Vector3{100.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, grandparent, grandparentLocal);

    TransformComponent detachedLocal;
    detachedLocal.localPosition = Math::Vector3{5.0F, 0.0F, 0.0F};
    detachedLocal.topLevel = true;
    sceneGraph.SetLocalTransform(entityManager, detached, detachedLocal);

    TransformComponent grandchildLocal;
    grandchildLocal.localPosition = Math::Vector3{2.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, grandchild, grandchildLocal);
    sceneGraph.Update(entityManager);

    EXPECT_NEAR(entityManager.GetComponent<WorldTransformComponent>(detached).worldPosition.x, 5.0F, kEpsilon);
    EXPECT_NEAR(entityManager.GetComponent<WorldTransformComponent>(grandchild).worldPosition.x, 7.0F, kEpsilon)
        << "the grandchild composes from the detached object, not from the grandparent";
}

TEST_F(SceneGraphTest, Update_TopLevelOnARootEntityChangesNothing) {
    // A root has no parent to ignore. Setting the flag there must be a no-op rather than an edge
    // case - the Inspector shows the checkbox on every object, including roots.
    const Entity root = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, root, TransformComponent{});
    TransformComponent local;
    local.localPosition = Math::Vector3{4.0F, 5.0F, 6.0F};
    local.topLevel = true;
    sceneGraph.SetLocalTransform(entityManager, root, local);
    sceneGraph.Update(entityManager);

    const WorldTransformComponent& world = entityManager.GetComponent<WorldTransformComponent>(root);
    EXPECT_NEAR(world.worldPosition.x, 4.0F, kEpsilon);
    EXPECT_FALSE(world.dirty);
}

TEST_F(SceneGraphTest, Update_VisibilityParentOverridesTheTransformParent) {
    // The case the redirect exists for: a weapon parented to a hand for position, but which
    // should disappear with the whole character rather than with the hand.
    const Entity character = entityManager.CreateEntity();
    const Entity hand = entityManager.CreateEntity();
    const Entity weapon = entityManager.CreateEntity();
    for (const Entity entity : {character, hand, weapon}) {
        sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
        entityManager.AddComponent<VisibilityComponent>(entity, VisibilityComponent{});
    }
    sceneGraph.SetParent(entityManager, hand, character);
    sceneGraph.SetParent(entityManager, weapon, hand);
    entityManager.GetComponent<VisibilityComponent>(weapon).visibilityParent = character;

    // Hiding the intermediate hand must NOT hide the weapon - it no longer inherits from there.
    entityManager.GetComponent<VisibilityComponent>(hand).visible = false;
    sceneGraph.Update(entityManager);
    EXPECT_FALSE(entityManager.GetComponent<VisibilityComponent>(hand).visibleInHierarchy);
    EXPECT_TRUE(entityManager.GetComponent<VisibilityComponent>(weapon).visibleInHierarchy)
        << "the redirect must replace the transform-parent chain, not add to it";

    // Hiding the character does hide it.
    entityManager.GetComponent<VisibilityComponent>(hand).visible = true;
    entityManager.GetComponent<VisibilityComponent>(character).visible = false;
    sceneGraph.Update(entityManager);
    EXPECT_FALSE(entityManager.GetComponent<VisibilityComponent>(weapon).visibleInHierarchy);
}

TEST_F(SceneGraphTest, Update_ARedirectDoesNotOverrideTheEntitysOwnHiddenState) {
    // The redirect replaces the INHERITED half only. An entity hidden in its own right stays
    // hidden no matter what it points at - the same rule the transform-parent path follows.
    const Entity target = entityManager.CreateEntity();
    const Entity entity = entityManager.CreateEntity();
    for (const Entity created : {target, entity}) {
        sceneGraph.AttachTransform(entityManager, created, TransformComponent{});
        entityManager.AddComponent<VisibilityComponent>(created, VisibilityComponent{});
    }
    entityManager.GetComponent<VisibilityComponent>(entity).visibilityParent = target;
    entityManager.GetComponent<VisibilityComponent>(entity).visible = false;
    sceneGraph.Update(entityManager);

    EXPECT_FALSE(entityManager.GetComponent<VisibilityComponent>(entity).visibleInHierarchy)
        << "pointing at a visible target must not reveal something hidden in its own right";
}

TEST_F(SceneGraphTest, Update_RedirectsFollowAChainOfRedirects) {
    // A visibility parent may itself redirect. Resolving only one hop would silently ignore
    // everything past the first link.
    const Entity root = entityManager.CreateEntity();
    const Entity middle = entityManager.CreateEntity();
    const Entity leaf = entityManager.CreateEntity();
    for (const Entity entity : {root, middle, leaf}) {
        sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
        entityManager.AddComponent<VisibilityComponent>(entity, VisibilityComponent{});
    }
    entityManager.GetComponent<VisibilityComponent>(leaf).visibilityParent = middle;
    entityManager.GetComponent<VisibilityComponent>(middle).visibilityParent = root;
    entityManager.GetComponent<VisibilityComponent>(root).visible = false;
    sceneGraph.Update(entityManager);

    EXPECT_FALSE(entityManager.GetComponent<VisibilityComponent>(leaf).visibleInHierarchy)
        << "the chain must be followed to its end";
}

TEST_F(SceneGraphTest, Update_ARedirectCycleFallsBackInsteadOfHanging) {
    // Nothing prevents an author pointing two objects at each other, and a .uvscene can be
    // hand-edited into one. The resolver must terminate and must not pick an arbitrary winner:
    // falling back to the transform-parent answer leaves the entities visible and predictable.
    const Entity first = entityManager.CreateEntity();
    const Entity second = entityManager.CreateEntity();
    for (const Entity entity : {first, second}) {
        sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
        entityManager.AddComponent<VisibilityComponent>(entity, VisibilityComponent{});
    }
    entityManager.GetComponent<VisibilityComponent>(first).visibilityParent = second;
    entityManager.GetComponent<VisibilityComponent>(second).visibilityParent = first;

    sceneGraph.Update(entityManager); // must return

    EXPECT_TRUE(entityManager.GetComponent<VisibilityComponent>(first).visibleInHierarchy);
    EXPECT_TRUE(entityManager.GetComponent<VisibilityComponent>(second).visibleInHierarchy);
}

TEST_F(SceneGraphTest, Update_ADanglingRedirectDoesNotHideAnything) {
    // The target was deleted, or the reference came from a file that no longer matches the scene.
    // Losing a reference must not make geometry disappear - a vanished object with no error is
    // far harder to diagnose than one that is simply still there.
    const Entity target = entityManager.CreateEntity();
    const Entity entity = entityManager.CreateEntity();
    for (const Entity created : {target, entity}) {
        sceneGraph.AttachTransform(entityManager, created, TransformComponent{});
        entityManager.AddComponent<VisibilityComponent>(created, VisibilityComponent{});
    }
    entityManager.GetComponent<VisibilityComponent>(entity).visibilityParent = target;
    entityManager.DestroyEntity(target);
    sceneGraph.Update(entityManager);

    EXPECT_TRUE(entityManager.GetComponent<VisibilityComponent>(entity).visibleInHierarchy);
}

TEST_F(SceneGraphTest, Update_ARedirectPicksUpTheTargetsOwnInheritedState) {
    // The target's answer includes ITS transform parent. Reading only the target's own `visible`
    // switch would miss a target that is hidden because its own parent is.
    const Entity grandparent = entityManager.CreateEntity();
    const Entity target = entityManager.CreateEntity();
    const Entity entity = entityManager.CreateEntity();
    for (const Entity created : {grandparent, target, entity}) {
        sceneGraph.AttachTransform(entityManager, created, TransformComponent{});
        entityManager.AddComponent<VisibilityComponent>(created, VisibilityComponent{});
    }
    sceneGraph.SetParent(entityManager, target, grandparent);
    entityManager.GetComponent<VisibilityComponent>(entity).visibilityParent = target;

    // The target itself is switched on; its PARENT is not.
    entityManager.GetComponent<VisibilityComponent>(grandparent).visible = false;
    sceneGraph.Update(entityManager);

    ASSERT_FALSE(entityManager.GetComponent<VisibilityComponent>(target).visibleInHierarchy);
    EXPECT_FALSE(entityManager.GetComponent<VisibilityComponent>(entity).visibleInHierarchy)
        << "a redirect inherits the target's resolved state, not just its own switch";
}

TEST_F(SceneGraphTest, Update_InterpolationRecordsTwoPosesAsTheEntityMoves) {
    // The core mechanic. One pose is not enough to blend between, so the first recorded step must
    // NOT report itself as ready - otherwise a freshly spawned object blends in from wherever a
    // default-constructed "previous" happens to be.
    const Entity entity = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
    entityManager.AddComponent<PhysicsInterpolationComponent>(
        entity, PhysicsInterpolationComponent{});

    TransformComponent local;
    local.localPosition = Math::Vector3{10.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, entity, local);
    sceneGraph.Update(entityManager);

    {
        const PhysicsInterpolationComponent& interpolation =
            entityManager.GetComponent<PhysicsInterpolationComponent>(entity);
        EXPECT_NEAR(interpolation.currentPosition.x, 10.0F, kEpsilon);
        EXPECT_NEAR(interpolation.previousPosition.x, 10.0F, kEpsilon)
            << "the first pose must seed previous to itself, not leave it at the origin";
    }

    local.localPosition = Math::Vector3{20.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, entity, local);
    sceneGraph.Update(entityManager);

    const PhysicsInterpolationComponent& interpolation =
        entityManager.GetComponent<PhysicsInterpolationComponent>(entity);
    EXPECT_NEAR(interpolation.previousPosition.x, 10.0F, kEpsilon);
    EXPECT_NEAR(interpolation.currentPosition.x, 20.0F, kEpsilon);

    Math::Vector3 position{};
    Math::Quaternion rotation{};
    Math::Vector3 scale{};
    ASSERT_TRUE(TryGetInterpolatedPose(interpolation, 0.5F, position, rotation, scale));
    EXPECT_NEAR(position.x, 15.0F, kEpsilon) << "halfway between the two recorded poses";
}

TEST_F(SceneGraphTest, Update_AStationaryEntityDoesNotCollapseItsTwoPoses) {
    // If every sweep recorded a pose, a stationary object would have previous == current, and the
    // first frame after it started moving would have nothing to blend from - a visible hitch
    // exactly when motion begins, which is the worst possible moment for one.
    const Entity entity = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
    entityManager.AddComponent<PhysicsInterpolationComponent>(
        entity, PhysicsInterpolationComponent{});

    TransformComponent local;
    local.localPosition = Math::Vector3{1.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, entity, local);
    sceneGraph.Update(entityManager);
    local.localPosition = Math::Vector3{2.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, entity, local);
    sceneGraph.Update(entityManager);

    // Several sweeps with nothing moving.
    for (int frame = 0; frame < 5; ++frame) {
        sceneGraph.Update(entityManager);
    }

    const PhysicsInterpolationComponent& interpolation =
        entityManager.GetComponent<PhysicsInterpolationComponent>(entity);
    EXPECT_NEAR(interpolation.previousPosition.x, 1.0F, kEpsilon)
        << "idle sweeps must not overwrite the previous pose";
    EXPECT_NEAR(interpolation.currentPosition.x, 2.0F, kEpsilon);
}

TEST_F(SceneGraphTest, Update_InterpolationModeOffIsHonouredAndInherited) {
    // Off must win over an inheriting ancestor, and Inherit must actually follow the parent -
    // otherwise the three-value enum is really a two-value one.
    const Entity parent = entityManager.CreateEntity();
    const Entity inheriting = entityManager.CreateEntity();
    const Entity explicitlyOff = entityManager.CreateEntity();
    for (const Entity entity : {parent, inheriting, explicitlyOff}) {
        sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
        entityManager.AddComponent<PhysicsInterpolationComponent>(
            entity, PhysicsInterpolationComponent{});
    }
    sceneGraph.SetParent(entityManager, inheriting, parent);
    sceneGraph.SetParent(entityManager, explicitlyOff, parent);

    entityManager.GetComponent<PhysicsInterpolationComponent>(parent).mode =
        PoseSmoothing::Exact;
    entityManager.GetComponent<PhysicsInterpolationComponent>(explicitlyOff).mode =
        PoseSmoothing::Blended;
    sceneGraph.Update(entityManager);

    EXPECT_FALSE(entityManager.GetComponent<PhysicsInterpolationComponent>(parent).interpolatedInHierarchy);
    EXPECT_FALSE(entityManager.GetComponent<PhysicsInterpolationComponent>(inheriting).interpolatedInHierarchy)
        << "Inherit must follow the parent";
    EXPECT_TRUE(entityManager.GetComponent<PhysicsInterpolationComponent>(explicitlyOff).interpolatedInHierarchy)
        << "an explicit On must override an inheriting chain";
}

TEST_F(SceneGraphTest, Update_AnEntityWithoutTheComponentPassesInterpolationThrough) {
    // The component is optional, so an intermediate object usually will not have one. It must not
    // break the chain - the same rule visibility follows.
    const Entity grandparent = entityManager.CreateEntity();
    const Entity middle = entityManager.CreateEntity();
    const Entity grandchild = entityManager.CreateEntity();
    for (const Entity entity : {grandparent, middle, grandchild}) {
        sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
    }
    entityManager.AddComponent<PhysicsInterpolationComponent>(
        grandparent, PhysicsInterpolationComponent{});
    entityManager.AddComponent<PhysicsInterpolationComponent>(
        grandchild, PhysicsInterpolationComponent{});
    sceneGraph.SetParent(entityManager, middle, grandparent);
    sceneGraph.SetParent(entityManager, grandchild, middle);

    entityManager.GetComponent<PhysicsInterpolationComponent>(grandparent).mode =
        PoseSmoothing::Exact;
    sceneGraph.Update(entityManager);

    EXPECT_FALSE(
        entityManager.GetComponent<PhysicsInterpolationComponent>(grandchild).interpolatedInHierarchy)
        << "an object without the component must pass the parent's setting through";
}

TEST_F(SceneGraphTest, Update_ANonFiniteTransformDoesNotRecordAPoseToBlendTowards) {
    // Recording a NaN pose would hand the renderer something to blend TOWARDS, dragging the object
    // off to infinity over the following frames. Keeping the last good pose freezes it instead,
    // which is visible and recoverable.
    const Entity entity = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
    entityManager.AddComponent<PhysicsInterpolationComponent>(
        entity, PhysicsInterpolationComponent{});

    TransformComponent local;
    local.localPosition = Math::Vector3{3.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, entity, local);
    sceneGraph.Update(entityManager);
    local.localPosition = Math::Vector3{4.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, entity, local);
    sceneGraph.Update(entityManager);

    // Corrupt directly - SetLocalTransform would reject a non-finite value.
    entityManager.GetComponent<TransformComponent>(entity).localPosition.x =
        std::numeric_limits<float>::quiet_NaN();
    entityManager.GetComponent<WorldTransformComponent>(entity).dirty = true;
    sceneGraph.Update(entityManager);

    const PhysicsInterpolationComponent& interpolation =
        entityManager.GetComponent<PhysicsInterpolationComponent>(entity);
    EXPECT_TRUE(std::isfinite(interpolation.currentPosition.x)) << "a NaN must not reach the recorded pose";
    EXPECT_NEAR(interpolation.currentPosition.x, 4.0F, kEpsilon);
}

TEST(PhysicsInterpolationBlendTest, ASingleRecordedPoseReportsNotReadyRatherThanBlendingFromTheOrigin) {
    // The spawn bug this guards against only appears on an object's first visible frame, which is
    // precisely when nobody is looking for it.
    PhysicsInterpolationComponent interpolation;
    interpolation.currentPosition = Math::Vector3{100.0F, 0.0F, 0.0F};
    interpolation.hasPreviousPose = false;

    Math::Vector3 position{7.0F, 7.0F, 7.0F};
    Math::Quaternion rotation{};
    Math::Vector3 scale{};
    EXPECT_FALSE(TryGetInterpolatedPose(interpolation, 0.5F, position, rotation, scale));
    EXPECT_FLOAT_EQ(position.x, 7.0F) << "a rejected blend must not write its outputs";
}

TEST(PhysicsInterpolationBlendTest, AlphaIsClampedRatherThanRejectedAtTheBoundaries) {
    // A timer that overshoots after a long frame should keep drawing smoothly, not stutter. And
    // alpha 1.0 simply means "the current pose", which is a legitimate request.
    PhysicsInterpolationComponent interpolation;
    interpolation.previousPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    interpolation.currentPosition = Math::Vector3{10.0F, 0.0F, 0.0F};
    interpolation.hasPreviousPose = true;

    Math::Vector3 position{};
    Math::Quaternion rotation{};
    Math::Vector3 scale{};
    ASSERT_TRUE(TryGetInterpolatedPose(interpolation, 1.4F, position, rotation, scale));
    EXPECT_NEAR(position.x, 10.0F, 1e-4F) << "an overshooting alpha must clamp to the current pose";
    ASSERT_TRUE(TryGetInterpolatedPose(interpolation, -0.3F, position, rotation, scale));
    EXPECT_NEAR(position.x, 0.0F, 1e-4F);

    // A non-finite alpha is a different matter - there is no sensible clamp, so it is refused.
    EXPECT_FALSE(TryGetInterpolatedPose(interpolation, std::numeric_limits<float>::quiet_NaN(), position,
                                           rotation, scale));
}

TEST(PhysicsInterpolationBlendTest, TheSimulatedPoseIsUsedWhenInterpolationIsOff) {
    // Off must actually mean off: the blend reports not-ready so the caller falls back to the
    // world transform, rather than quietly returning a blended pose anyway.
    PhysicsInterpolationComponent interpolation;
    interpolation.previousPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    interpolation.currentPosition = Math::Vector3{10.0F, 0.0F, 0.0F};
    interpolation.hasPreviousPose = true;
    interpolation.interpolatedInHierarchy = false;

    Math::Vector3 position{};
    Math::Quaternion rotation{};
    Math::Vector3 scale{};
    EXPECT_FALSE(TryGetInterpolatedPose(interpolation, 0.5F, position, rotation, scale));
}

TEST_F(SceneGraphTest, ChildrenKeepTheirOrderWhenAComponentMovesOneInStorage) {
    const Entity parent = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, parent, TransformComponent{});
    std::vector<Entity> children;
    for (int i = 0; i < 4; ++i) {
        const Entity child = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, child, TransformComponent{});
        sceneGraph.SetParent(entityManager, child, parent);
        children.push_back(child);
    }
    ASSERT_EQ(sceneGraph.GetChildren(entityManager, parent), children);

    // Adding a component moves the first child to another archetype, and so to the end of the
    // storage walk. Its place among its siblings must not follow.
    entityManager.AddComponent<VisibilityComponent>(children.front(), VisibilityComponent{});
    EXPECT_EQ(sceneGraph.GetChildren(entityManager, parent), children);
    EXPECT_EQ(sceneGraph.GetSiblingIndex(entityManager, children.front()), std::optional<std::size_t>{0U});
}

TEST_F(SceneGraphTest, AChildThatChangesParentGoesLastAndOneThatDoesNotStaysPut) {
    const Entity a = entityManager.CreateEntity();
    const Entity b = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, a, TransformComponent{});
    sceneGraph.AttachTransform(entityManager, b, TransformComponent{});
    std::vector<Entity> underA;
    for (int i = 0; i < 3; ++i) {
        const Entity child = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, child, TransformComponent{});
        sceneGraph.SetParent(entityManager, child, a);
        underA.push_back(child);
    }
    const Entity moved = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, moved, TransformComponent{});
    sceneGraph.SetParent(entityManager, moved, b);

    // Setting the parent a child already has is not a move.
    sceneGraph.SetParent(entityManager, underA.front(), a);
    EXPECT_EQ(sceneGraph.GetChildren(entityManager, a), underA);

    sceneGraph.SetParent(entityManager, moved, a);
    underA.push_back(moved);
    EXPECT_EQ(sceneGraph.GetChildren(entityManager, a), underA);
}

TEST_F(SceneGraphTest, SetSiblingIndex_MovesOneAndKeepsTheRestInOrder) {
    const Entity parent = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, parent, TransformComponent{});
    std::vector<Entity> c;
    for (int i = 0; i < 4; ++i) {
        const Entity child = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, child, TransformComponent{});
        sceneGraph.SetParent(entityManager, child, parent);
        c.push_back(child);
    }

    ASSERT_TRUE(sceneGraph.SetSiblingIndex(entityManager, c[3], 1U));
    EXPECT_EQ(sceneGraph.GetChildren(entityManager, parent), (std::vector<Entity>{c[0], c[3], c[1], c[2]}));
    ASSERT_TRUE(sceneGraph.SetSiblingIndex(entityManager, c[0], 99U)); // past the end means last
    EXPECT_EQ(sceneGraph.GetChildren(entityManager, parent), (std::vector<Entity>{c[3], c[1], c[2], c[0]}));
    EXPECT_EQ(sceneGraph.GetSiblingIndex(entityManager, c[2]), std::optional<std::size_t>{2U});

    const Entity loose = entityManager.CreateEntity();
    EXPECT_FALSE(sceneGraph.SetSiblingIndex(entityManager, loose, 0U));
    EXPECT_FALSE(sceneGraph.GetSiblingIndex(entityManager, loose).has_value());
}

} // namespace
} // namespace UVE::Scene::Tests
