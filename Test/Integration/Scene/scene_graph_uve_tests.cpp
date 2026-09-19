// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/scene/scene_graph_uve.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/platform/platform_uve.h"

namespace UVE::Scene::Tests {
namespace {

constexpr float kEpsilon = 1e-4F;

class SceneGraphUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneGraphUVE sceneGraph;
};

TEST_F(SceneGraphUVETest, AttachTransformUVE_AddsAllThreeComponents) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});

    EXPECT_TRUE(entityManager.HasComponentUVE<TransformComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<HierarchyComponentUVE>(entity));
    EXPECT_EQ(entityManager.GetComponentUVE<HierarchyComponentUVE>(entity).parent, kInvalidEntityUVE);
}

TEST(TransformComponentUVETest, IsTransformComponentValidUVE_RejectsNonFiniteAndNonUnitValues) {
    TransformComponentUVE transform;
    EXPECT_TRUE(IsTransformComponentValidUVE(transform));

    transform.localPosition.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsTransformComponentValidUVE(transform));
    transform = TransformComponentUVE{};
    transform.localScale.z = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsTransformComponentValidUVE(transform));
    transform = TransformComponentUVE{};
    transform.localRotation.w = 0.5F;
    EXPECT_FALSE(IsTransformComponentValidUVE(transform));
    transform.localRotation = Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 1.0004F};
    EXPECT_TRUE(IsTransformComponentValidUVE(transform));
}

#if UVE_DEBUG
TEST_F(SceneGraphUVETest, InvalidMutationInputsAssertBeforePublication) {
    const EntityUVE unattached = entityManager.CreateEntityUVE();
    TransformComponentUVE invalidTransform{};
    invalidTransform.localPosition.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_DEATH({ sceneGraph.AttachTransformUVE(entityManager, unattached, invalidTransform); }, "");

    const EntityUVE attached = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, attached, TransformComponentUVE{});
    EXPECT_DEATH({ sceneGraph.SetLocalTransformUVE(entityManager, attached, invalidTransform); }, "");

    const EntityUVE invalidParent{0xFFFFFFFFU, 1U};
    EXPECT_DEATH({ sceneGraph.SetParentUVE(entityManager, attached, invalidParent); }, "");
}
#else
TEST_F(SceneGraphUVETest, InvalidMutationInputsFailClosedWithoutPublication) {
    const EntityUVE unattached = entityManager.CreateEntityUVE();
    TransformComponentUVE invalidTransform{};
    invalidTransform.localPosition.x = std::numeric_limits<float>::quiet_NaN();
    sceneGraph.AttachTransformUVE(entityManager, unattached, invalidTransform);
    EXPECT_FALSE(entityManager.HasComponentUVE<TransformComponentUVE>(unattached));

    const EntityUVE attached = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, attached, TransformComponentUVE{});
    sceneGraph.UpdateUVE(entityManager);
    const TransformComponentUVE before = entityManager.GetComponentUVE<TransformComponentUVE>(attached);
    const WorldTransformComponentUVE worldBefore = entityManager.GetComponentUVE<WorldTransformComponentUVE>(attached);
    sceneGraph.SetLocalTransformUVE(entityManager, attached, invalidTransform);
    EXPECT_EQ(entityManager.GetComponentUVE<TransformComponentUVE>(attached).localPosition, before.localPosition);
    EXPECT_EQ(entityManager.GetComponentUVE<WorldTransformComponentUVE>(attached).dirty, worldBefore.dirty);

    const EntityUVE invalidParent{0xFFFFFFFFU, 1U};
    sceneGraph.SetParentUVE(entityManager, attached, invalidParent);
    EXPECT_EQ(entityManager.GetComponentUVE<HierarchyComponentUVE>(attached).parent, kInvalidEntityUVE);
}
#endif

TEST_F(SceneGraphUVETest, UpdateUVE_RootEntity_WorldEqualsLocal) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    TransformComponentUVE local;
    local.localPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
    sceneGraph.AttachTransformUVE(entityManager, entity, local);

    sceneGraph.UpdateUVE(entityManager);

    const WorldTransformComponentUVE& world = entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity);
    EXPECT_FALSE(world.dirty);
    EXPECT_EQ(world.worldPosition, local.localPosition);
}

TEST_F(SceneGraphUVETest, SetLocalTransformUVE_MarksDirty) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
    sceneGraph.UpdateUVE(entityManager);
    ASSERT_FALSE(entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).dirty);

    TransformComponentUVE newLocal;
    newLocal.localPosition = Math::Vector3UVE{5.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, entity, newLocal);

    EXPECT_TRUE(entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).dirty);
}

TEST_F(SceneGraphUVETest, ParentChildComposition_CombinesPositionCorrectly) {
    const EntityUVE parent = entityManager.CreateEntityUVE();
    TransformComponentUVE parentLocal;
    parentLocal.localPosition = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, parent, parentLocal);

    const EntityUVE child = entityManager.CreateEntityUVE();
    TransformComponentUVE childLocal;
    childLocal.localPosition = Math::Vector3UVE{0.0F, 5.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, child, childLocal);
    sceneGraph.SetParentUVE(entityManager, child, parent);

    sceneGraph.UpdateUVE(entityManager);

    const WorldTransformComponentUVE& childWorld = entityManager.GetComponentUVE<WorldTransformComponentUVE>(child);
    EXPECT_NEAR(childWorld.worldPosition.x, 10.0F, kEpsilon);
    EXPECT_NEAR(childWorld.worldPosition.y, 5.0F, kEpsilon);
    EXPECT_NEAR(childWorld.worldPosition.z, 0.0F, kEpsilon);
}

TEST_F(SceneGraphUVETest, MultiLevelHierarchy_PropagatesInOnePass) {
    const EntityUVE grandparent = entityManager.CreateEntityUVE();
    TransformComponentUVE grandparentLocal;
    grandparentLocal.localPosition = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, grandparent, grandparentLocal);

    const EntityUVE parent = entityManager.CreateEntityUVE();
    TransformComponentUVE parentLocal;
    parentLocal.localPosition = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, parent, parentLocal);
    sceneGraph.SetParentUVE(entityManager, parent, grandparent);

    const EntityUVE child = entityManager.CreateEntityUVE();
    TransformComponentUVE childLocal;
    childLocal.localPosition = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, child, childLocal);
    sceneGraph.SetParentUVE(entityManager, child, parent);

    sceneGraph.UpdateUVE(entityManager);

    const WorldTransformComponentUVE& childWorld = entityManager.GetComponentUVE<WorldTransformComponentUVE>(child);
    EXPECT_NEAR(childWorld.worldPosition.x, 3.0F, kEpsilon);
}

TEST_F(SceneGraphUVETest, UpdateUVE_PreservesFiniteCacheWhenHierarchyCompositionOverflows) {
    const float maximumFloat = std::numeric_limits<float>::max();
    const EntityUVE parent = entityManager.CreateEntityUVE();
    TransformComponentUVE parentLocal;
    parentLocal.localScale = Math::Vector3UVE{maximumFloat, 1.0F, 1.0F};
    sceneGraph.AttachTransformUVE(entityManager, parent, parentLocal);

    const EntityUVE child = entityManager.CreateEntityUVE();
    TransformComponentUVE childLocal;
    childLocal.localPosition = Math::Vector3UVE{2.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, child, childLocal);
    sceneGraph.SetParentUVE(entityManager, child, parent);

    sceneGraph.UpdateUVE(entityManager);

    const WorldTransformComponentUVE& childWorld = entityManager.GetComponentUVE<WorldTransformComponentUVE>(child);
    EXPECT_TRUE(childWorld.dirty);
    EXPECT_TRUE(std::isfinite(childWorld.worldPosition.x));
    EXPECT_TRUE(std::isfinite(childWorld.worldPosition.y));
    EXPECT_TRUE(std::isfinite(childWorld.worldPosition.z));
    EXPECT_EQ(childWorld.worldPosition, (Math::Vector3UVE{}));
}

TEST_F(SceneGraphUVETest, MovingParent_RecomputesChildEvenWhenChildNotDirty) {
    const EntityUVE parent = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, parent, TransformComponentUVE{});

    const EntityUVE child = entityManager.CreateEntityUVE();
    TransformComponentUVE childLocal;
    childLocal.localPosition = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, child, childLocal);
    sceneGraph.SetParentUVE(entityManager, child, parent);

    sceneGraph.UpdateUVE(entityManager);
    ASSERT_NEAR(entityManager.GetComponentUVE<WorldTransformComponentUVE>(child).worldPosition.x, 1.0F, kEpsilon);

    // Move only the parent; the child's own dirty flag is never touched by SetLocalTransformUVE
    // on a *different* entity, so it stays false right up until UpdateUVE() runs.
    TransformComponentUVE parentLocal;
    parentLocal.localPosition = Math::Vector3UVE{100.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, parent, parentLocal);
    ASSERT_FALSE(entityManager.GetComponentUVE<WorldTransformComponentUVE>(child).dirty);

    sceneGraph.UpdateUVE(entityManager);

    EXPECT_NEAR(entityManager.GetComponentUVE<WorldTransformComponentUVE>(child).worldPosition.x, 101.0F, kEpsilon);
}

#if UVE_DEBUG
TEST_F(SceneGraphUVETest, SetParentUVEDeathTest_CycleRejected) {
    const EntityUVE a = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, a, TransformComponentUVE{});
    const EntityUVE b = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, b, TransformComponentUVE{});

    sceneGraph.SetParentUVE(entityManager, b, a); // b's parent is a
    EXPECT_DEATH({ sceneGraph.SetParentUVE(entityManager, a, b); }, "");
}
#endif

TEST_F(SceneGraphUVETest, GetChildrenUVE_ReturnsExactDirectChildren) {
    const EntityUVE parent = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, parent, TransformComponentUVE{});

    const EntityUVE childA = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, childA, TransformComponentUVE{});
    sceneGraph.SetParentUVE(entityManager, childA, parent);

    const EntityUVE childB = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, childB, TransformComponentUVE{});
    sceneGraph.SetParentUVE(entityManager, childB, parent);

    const EntityUVE unrelated = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, unrelated, TransformComponentUVE{});

    std::vector<EntityUVE> children = sceneGraph.GetChildrenUVE(entityManager, parent);
    std::sort(children.begin(), children.end(), [](const EntityUVE& lhs, const EntityUVE& rhs) {
        return lhs.index < rhs.index;
    });

    std::vector<EntityUVE> expected{childA, childB};
    std::sort(expected.begin(), expected.end(),
              [](const EntityUVE& lhs, const EntityUVE& rhs) { return lhs.index < rhs.index; });

    EXPECT_EQ(children, expected);
}

// ---------------------------------------------------------------------------
// UpdateUVE's rewritten sweep.
//
// A completely static 5000-entity scene was spending 1353us per frame
// recomputing nothing, from an O(n^2) erase-from-the-middle and per-visit
// component re-lookups. Now 133us, about 10x, with the sweep semantics
// unchanged.
//
// The rewrite introduced two new hazards that did not exist before, and these
// tests target them specifically: SceneGraphUVE now keeps scratch buffers
// BETWEEN calls (so leaked state would make the second update differ from the
// first), and the sweep holds raw component pointers captured by the initial
// walk (so anything that moved a component row mid-update would leave them
// dangling).
// ---------------------------------------------------------------------------

TEST_F(SceneGraphUVETest, UpdateUVE_RepeatedCallsOnAStaticScene_AreIdempotent) {
    // The scratch buffers survive between calls now. If either leaked state, the second update
    // would disagree with the first - and every real frame calls this repeatedly.
    std::vector<EntityUVE> entities;
    for (int i = 0; i < 40; ++i) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        TransformComponentUVE local;
        local.localPosition = Math::Vector3UVE{static_cast<float>(i), 2.0F, -3.0F};
        sceneGraph.AttachTransformUVE(entityManager, entity, local);
        entities.push_back(entity);
    }

    sceneGraph.UpdateUVE(entityManager);
    std::vector<Math::Vector3UVE> first;
    for (const EntityUVE entity : entities) {
        first.push_back(entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).worldPosition);
    }

    for (int pass = 0; pass < 5; ++pass) {
        sceneGraph.UpdateUVE(entityManager);
    }

    for (std::size_t index = 0U; index < entities.size(); ++index) {
        const WorldTransformComponentUVE& world =
            entityManager.GetComponentUVE<WorldTransformComponentUVE>(entities[index]);
        EXPECT_NEAR(world.worldPosition.x, first[index].x, kEpsilon);
        EXPECT_NEAR(world.worldPosition.y, first[index].y, kEpsilon);
        EXPECT_NEAR(world.worldPosition.z, first[index].z, kEpsilon);
        EXPECT_FALSE(world.dirty);
    }
}

TEST_F(SceneGraphUVETest, UpdateUVE_AFreshInstanceMatchesAReusedOne) {
    // The scratch buffers must be scratch, not state: a SceneGraphUVE that has already run must
    // produce exactly what a brand-new one produces for the same scene.
    const EntityUVE parent = entityManager.CreateEntityUVE();
    TransformComponentUVE parentLocal;
    parentLocal.localPosition = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, parent, parentLocal);

    const EntityUVE child = entityManager.CreateEntityUVE();
    TransformComponentUVE childLocal;
    childLocal.localPosition = Math::Vector3UVE{0.0F, 5.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, child, childLocal);
    sceneGraph.SetParentUVE(entityManager, child, parent);

    // Run the reused instance several times first, so any retained state would have accumulated.
    for (int pass = 0; pass < 3; ++pass) {
        sceneGraph.UpdateUVE(entityManager);
    }
    const Math::Vector3UVE reusedResult =
        entityManager.GetComponentUVE<WorldTransformComponentUVE>(child).worldPosition;

    // Dirty everything again and run a completely fresh graph over the same scene.
    sceneGraph.SetLocalTransformUVE(entityManager, parent, parentLocal);
    SceneGraphUVE freshGraph;
    freshGraph.UpdateUVE(entityManager);
    const Math::Vector3UVE freshResult =
        entityManager.GetComponentUVE<WorldTransformComponentUVE>(child).worldPosition;

    EXPECT_NEAR(freshResult.x, reusedResult.x, kEpsilon);
    EXPECT_NEAR(freshResult.y, reusedResult.y, kEpsilon);
    EXPECT_NEAR(freshResult.z, reusedResult.z, kEpsilon);
    EXPECT_NEAR(freshResult.x, 10.0F, kEpsilon);
    EXPECT_NEAR(freshResult.y, 5.0F, kEpsilon);
}

TEST_F(SceneGraphUVETest, UpdateUVE_DeepChainAcrossManySweeps_ResolvesEveryLevel) {
    // The compaction path only runs when entities are deferred to a later sweep, and a chain built
    // CHILD-FIRST guarantees that: each entity's parent is created after it, so the first sweep
    // can only resolve the root. This is the case the old erase-from-the-middle handled and the
    // new write-back compaction must handle identically.
    constexpr int kDepth = 60;
    std::vector<EntityUVE> chain;
    for (int i = 0; i < kDepth; ++i) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        TransformComponentUVE local;
        local.localPosition = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
        sceneGraph.AttachTransformUVE(entityManager, entity, local);
        chain.push_back(entity);
    }
    // chain[0] is the deepest descendant; chain[kDepth - 1] is the root.
    for (int i = 0; i + 1 < kDepth; ++i) {
        sceneGraph.SetParentUVE(entityManager, chain[static_cast<std::size_t>(i)],
                                chain[static_cast<std::size_t>(i + 1)]);
    }

    sceneGraph.UpdateUVE(entityManager);

    // Each level adds 1 on x, so the deepest descendant sits at kDepth.
    for (int i = 0; i < kDepth; ++i) {
        const WorldTransformComponentUVE& world =
            entityManager.GetComponentUVE<WorldTransformComponentUVE>(chain[static_cast<std::size_t>(i)]);
        EXPECT_NEAR(world.worldPosition.x, static_cast<float>(kDepth - i), kEpsilon)
            << "depth index " << i;
        EXPECT_FALSE(world.dirty);
    }
}

TEST_F(SceneGraphUVETest, UpdateUVE_MovingOnlyTheParent_StillMovesTheWholeSubtree) {
    // A parent that recomputes forces its children to recompute even though their own dirty flags
    // are false. That rule lives in the pass state, which the rewrite rebuilt - so it is worth
    // asserting directly rather than trusting it survived.
    const EntityUVE root = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, root, TransformComponentUVE{});
    const EntityUVE middle = entityManager.CreateEntityUVE();
    TransformComponentUVE middleLocal;
    middleLocal.localPosition = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, middle, middleLocal);
    const EntityUVE leaf = entityManager.CreateEntityUVE();
    TransformComponentUVE leafLocal;
    leafLocal.localPosition = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, leaf, leafLocal);
    sceneGraph.SetParentUVE(entityManager, middle, root);
    sceneGraph.SetParentUVE(entityManager, leaf, middle);
    sceneGraph.UpdateUVE(entityManager);
    ASSERT_NEAR(entityManager.GetComponentUVE<WorldTransformComponentUVE>(leaf).worldPosition.y, 2.0F, kEpsilon);

    // Settle, so every dirty flag is clear and only the root is touched next.
    sceneGraph.UpdateUVE(entityManager);
    ASSERT_FALSE(entityManager.GetComponentUVE<WorldTransformComponentUVE>(leaf).dirty);

    TransformComponentUVE movedRoot;
    movedRoot.localPosition = Math::Vector3UVE{100.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, root, movedRoot);
    sceneGraph.UpdateUVE(entityManager);

    const WorldTransformComponentUVE& leafWorld =
        entityManager.GetComponentUVE<WorldTransformComponentUVE>(leaf);
    EXPECT_NEAR(leafWorld.worldPosition.x, 100.0F, kEpsilon) << "the clean leaf must follow its moved ancestor";
    EXPECT_NEAR(leafWorld.worldPosition.y, 2.0F, kEpsilon);
}

TEST_F(SceneGraphUVETest, UpdateUVE_ManyRootsInOneSweep_AllResolveWithoutDeferral) {
    // The common case and the one the rewrite targets: a flat scene of roots completes in a single
    // sweep with nothing written back. Enough entities to span several ECS chunks.
    constexpr int kEntityCount = 1100;
    std::vector<EntityUVE> entities;
    for (int i = 0; i < kEntityCount; ++i) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        TransformComponentUVE local;
        local.localPosition = Math::Vector3UVE{static_cast<float>(i), 0.0F, 0.0F};
        sceneGraph.AttachTransformUVE(entityManager, entity, local);
        entities.push_back(entity);
    }

    sceneGraph.UpdateUVE(entityManager);

    for (int i = 0; i < kEntityCount; ++i) {
        const WorldTransformComponentUVE& world =
            entityManager.GetComponentUVE<WorldTransformComponentUVE>(entities[static_cast<std::size_t>(i)]);
        EXPECT_NEAR(world.worldPosition.x, static_cast<float>(i), kEpsilon);
        EXPECT_FALSE(world.dirty);
    }
}

TEST_F(SceneGraphUVETest, UpdateUVE_AfterEntitiesAreDestroyed_UsesTheCurrentScene) {
    // Destroying entities moves surviving component rows during chunk compaction. The sweep
    // captures component pointers during its initial walk, so this checks the capture happens
    // after the scene settled rather than surviving across a structural change.
    std::vector<EntityUVE> entities;
    for (int i = 0; i < 30; ++i) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        TransformComponentUVE local;
        local.localPosition = Math::Vector3UVE{static_cast<float>(i), 0.0F, 0.0F};
        sceneGraph.AttachTransformUVE(entityManager, entity, local);
        entities.push_back(entity);
    }
    sceneGraph.UpdateUVE(entityManager);

    for (std::size_t index = 0U; index < entities.size(); index += 2U) {
        entityManager.DestroyEntityUVE(entities[index]);
    }
    sceneGraph.UpdateUVE(entityManager);

    for (std::size_t index = 1U; index < entities.size(); index += 2U) {
        const WorldTransformComponentUVE& world =
            entityManager.GetComponentUVE<WorldTransformComponentUVE>(entities[index]);
        EXPECT_NEAR(world.worldPosition.x, static_cast<float>(index), kEpsilon)
            << "survivor " << index << " must keep its own transform after compaction";
    }
}

TEST_F(SceneGraphUVETest, UpdateUVE_NonFiniteParent_StillInvalidatesItsSubtree) {
    // An invalid parent marks its whole subtree dirty and invalid rather than publishing garbage.
    // That branch sits on the compaction path, so the rewrite could plausibly have dropped it.
    //
    // The NaN is written straight into the component rather than through SetLocalTransformUVE:
    // that setter validates its input and asserts (SIGTRAP in debug) on a non-finite transform, so
    // a corrupt local transform can only reach the sweep by some other route - a raw component
    // write, deserialization, or script. Going through the setter would be testing the setter's
    // guard, not the sweep's handling, and would abort before reaching the code under test. I
    // confirmed the assert is pre-existing by reproducing it against unmodified HEAD before
    // changing this test.
    const EntityUVE parent = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, parent, TransformComponentUVE{});
    const EntityUVE child = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, child, TransformComponentUVE{});
    sceneGraph.SetParentUVE(entityManager, child, parent);
    sceneGraph.UpdateUVE(entityManager);
    ASSERT_FALSE(entityManager.GetComponentUVE<WorldTransformComponentUVE>(child).dirty);

    entityManager.GetComponentUVE<TransformComponentUVE>(parent).localPosition.x =
        std::numeric_limits<float>::quiet_NaN();
    entityManager.GetComponentUVE<WorldTransformComponentUVE>(parent).dirty = true;

    sceneGraph.UpdateUVE(entityManager);

    EXPECT_TRUE(entityManager.GetComponentUVE<WorldTransformComponentUVE>(parent).dirty)
        << "a parent that cannot produce a finite transform must stay dirty";
    EXPECT_TRUE(entityManager.GetComponentUVE<WorldTransformComponentUVE>(child).dirty)
        << "a child of an invalid parent must stay dirty rather than publish a derived value";
    EXPECT_TRUE(std::isfinite(entityManager.GetComponentUVE<WorldTransformComponentUVE>(child).worldPosition.x))
        << "the child must keep its last good value rather than inherit the NaN";
}

} // namespace
} // namespace UVE::Scene::Tests
