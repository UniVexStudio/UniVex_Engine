// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/scene/scene_graph_uve.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/component/auto_translate_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/thread_group_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/platform/platform_uve.h"

namespace UVE::Scene::Tests {
namespace {

constexpr float kEpsilon = 1e-4F;

/// Sign-agnostic: q and -q are the same rotation.
[[nodiscard]] bool RepresentSameRotationForTransformUVE(const Math::QuaternionUVE& lhs,
                                                        const Math::QuaternionUVE& rhs) {
    const float dot = lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
    return std::abs(std::abs(dot) - 1.0F) <= 1e-4F;
}


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

TEST_F(SceneGraphUVETest, UpdateUVE_HidingAParentHidesItsWholeSubtree) {
    // The behaviour the component exists for. A parent's switch must reach every descendant, not
    // just its direct children - a two-level tree is the shortest case that can tell the
    // difference between real inheritance and a single-level copy.
    const EntityUVE parent = entityManager.CreateEntityUVE();
    const EntityUVE child = entityManager.CreateEntityUVE();
    const EntityUVE grandchild = entityManager.CreateEntityUVE();
    for (const EntityUVE entity : {parent, child, grandchild}) {
        sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
        entityManager.AddComponentUVE<VisibilityComponentUVE>(entity, VisibilityComponentUVE{});
    }
    sceneGraph.SetParentUVE(entityManager, child, parent);
    sceneGraph.SetParentUVE(entityManager, grandchild, child);

    entityManager.GetComponentUVE<VisibilityComponentUVE>(parent).visible = false;
    sceneGraph.UpdateUVE(entityManager);

    EXPECT_FALSE(entityManager.GetComponentUVE<VisibilityComponentUVE>(parent).visibleInHierarchy);
    EXPECT_FALSE(entityManager.GetComponentUVE<VisibilityComponentUVE>(child).visibleInHierarchy);
    EXPECT_FALSE(entityManager.GetComponentUVE<VisibilityComponentUVE>(grandchild).visibleInHierarchy);

    // The authored switch on the descendants is untouched - only the derived field moved.
    EXPECT_TRUE(entityManager.GetComponentUVE<VisibilityComponentUVE>(child).visible);
    EXPECT_TRUE(entityManager.GetComponentUVE<VisibilityComponentUVE>(grandchild).visible);
}

TEST_F(SceneGraphUVETest, UpdateUVE_ShowingAParentDoesNotShowAnIndependentlyHiddenChild) {
    // The reason there are two fields instead of one. If propagation wrote the authored flag, the
    // child's own choice would be destroyed the moment its parent was hidden, and showing the
    // parent again would wrongly reveal it. This is the case a single-flag implementation passes
    // every other test and fails here.
    const EntityUVE parent = entityManager.CreateEntityUVE();
    const EntityUVE child = entityManager.CreateEntityUVE();
    for (const EntityUVE entity : {parent, child}) {
        sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
        entityManager.AddComponentUVE<VisibilityComponentUVE>(entity, VisibilityComponentUVE{});
    }
    sceneGraph.SetParentUVE(entityManager, child, parent);

    entityManager.GetComponentUVE<VisibilityComponentUVE>(child).visible = false;
    entityManager.GetComponentUVE<VisibilityComponentUVE>(parent).visible = false;
    sceneGraph.UpdateUVE(entityManager);
    ASSERT_FALSE(entityManager.GetComponentUVE<VisibilityComponentUVE>(child).visibleInHierarchy);

    // Parent back on. The child stays hidden because it was hidden in its own right.
    entityManager.GetComponentUVE<VisibilityComponentUVE>(parent).visible = true;
    sceneGraph.UpdateUVE(entityManager);

    EXPECT_TRUE(entityManager.GetComponentUVE<VisibilityComponentUVE>(parent).visibleInHierarchy);
    EXPECT_FALSE(entityManager.GetComponentUVE<VisibilityComponentUVE>(child).visibleInHierarchy)
        << "showing a parent must not override a child's own hidden state";
}

TEST_F(SceneGraphUVETest, UpdateUVE_AnEntityWithoutTheComponentPassesVisibilityThrough) {
    // The component is optional, so most entities will not have one. An intermediate node without
    // it must not break the chain: hiding the grandparent still has to hide the grandchild.
    const EntityUVE grandparent = entityManager.CreateEntityUVE();
    const EntityUVE middle = entityManager.CreateEntityUVE();
    const EntityUVE grandchild = entityManager.CreateEntityUVE();
    for (const EntityUVE entity : {grandparent, middle, grandchild}) {
        sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
    }
    // Deliberately only the ends carry the component; `middle` has none.
    entityManager.AddComponentUVE<VisibilityComponentUVE>(grandparent, VisibilityComponentUVE{});
    entityManager.AddComponentUVE<VisibilityComponentUVE>(grandchild, VisibilityComponentUVE{});
    sceneGraph.SetParentUVE(entityManager, middle, grandparent);
    sceneGraph.SetParentUVE(entityManager, grandchild, middle);

    entityManager.GetComponentUVE<VisibilityComponentUVE>(grandparent).visible = false;
    sceneGraph.UpdateUVE(entityManager);

    EXPECT_FALSE(entityManager.GetComponentUVE<VisibilityComponentUVE>(grandchild).visibleInHierarchy)
        << "a node without the component must pass its parent's state through, not reset it";
}

TEST_F(SceneGraphUVETest, UpdateUVE_ResolvesProcessModeThroughANodeThatDoesNotCarryTheComponent) {
    // Same rule visibility follows: an intermediate node that never opted in must pass its
    // parent's answer through rather than resetting the chain.
    const EntityUVE root = entityManager.CreateEntityUVE();
    const EntityUVE middle = entityManager.CreateEntityUVE();
    const EntityUVE leaf = entityManager.CreateEntityUVE();
    for (const EntityUVE entity : {root, middle, leaf}) {
        sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
    }
    entityManager.AddComponentUVE<ProcessComponentUVE>(root, ProcessComponentUVE{});
    entityManager.AddComponentUVE<ProcessComponentUVE>(leaf, ProcessComponentUVE{});
    sceneGraph.SetParentUVE(entityManager, middle, root);
    sceneGraph.SetParentUVE(entityManager, leaf, middle);

    entityManager.GetComponentUVE<ProcessComponentUVE>(root).mode = ProcessModeUVE::Disabled;
    sceneGraph.UpdateUVE(entityManager);

    EXPECT_EQ(entityManager.GetComponentUVE<ProcessComponentUVE>(leaf).resolvedModeInHierarchy,
              ProcessModeUVE::Disabled);
    EXPECT_FALSE(IsProcessingUVE(
        entityManager.GetComponentUVE<ProcessComponentUVE>(leaf).resolvedModeInHierarchy,
        /*simulationPaused=*/false));

    // A pause menu under a Pausable parent has to be able to say Always and be believed, which is
    // the whole reason the mode is authored per entity.
    entityManager.GetComponentUVE<ProcessComponentUVE>(root).mode = ProcessModeUVE::Pausable;
    entityManager.GetComponentUVE<ProcessComponentUVE>(leaf).mode = ProcessModeUVE::Always;
    sceneGraph.UpdateUVE(entityManager);
    EXPECT_EQ(entityManager.GetComponentUVE<ProcessComponentUVE>(leaf).resolvedModeInHierarchy,
              ProcessModeUVE::Always);
    EXPECT_TRUE(IsProcessingUVE(
        entityManager.GetComponentUVE<ProcessComponentUVE>(leaf).resolvedModeInHierarchy,
        /*simulationPaused=*/true));
}

TEST_F(SceneGraphUVETest, UpdateUVE_AChildCannotEscapeAMainThreadAncestor) {
    const EntityUVE root = entityManager.CreateEntityUVE();
    const EntityUVE child = entityManager.CreateEntityUVE();
    for (const EntityUVE entity : {root, child}) {
        sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
        entityManager.AddComponentUVE<ThreadGroupComponentUVE>(entity, ThreadGroupComponentUVE{});
    }
    sceneGraph.SetParentUVE(entityManager, child, root);

    entityManager.GetComponentUVE<ThreadGroupComponentUVE>(root).mode = ThreadGroupModeUVE::MainThread;
    entityManager.GetComponentUVE<ThreadGroupComponentUVE>(child).mode = ThreadGroupModeUVE::SubThread;
    sceneGraph.UpdateUVE(entityManager);

    EXPECT_EQ(entityManager.GetComponentUVE<ThreadGroupComponentUVE>(child).resolvedModeInHierarchy,
              ThreadGroupModeUVE::MainThread)
        << "a main-thread ancestor is a constraint about shared state, not a preference";

    entityManager.GetComponentUVE<ThreadGroupComponentUVE>(root).mode = ThreadGroupModeUVE::SubThread;
    sceneGraph.UpdateUVE(entityManager);
    EXPECT_EQ(entityManager.GetComponentUVE<ThreadGroupComponentUVE>(child).resolvedModeInHierarchy,
              ThreadGroupModeUVE::SubThread);
}

TEST_F(SceneGraphUVETest, UpdateUVE_TopLevelStillInheritsTheCommonNodeModes) {
    // topLevel cuts the transform chain only. A top-level child is still paused with its parent and
    // still translated with the menu it belongs to - the same reasoning that keeps visibility
    // inherited across it.
    const EntityUVE root = entityManager.CreateEntityUVE();
    const EntityUVE child = entityManager.CreateEntityUVE();
    for (const EntityUVE entity : {root, child}) {
        sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
        entityManager.AddComponentUVE<ProcessComponentUVE>(entity, ProcessComponentUVE{});
        entityManager.AddComponentUVE<AutoTranslateComponentUVE>(entity, AutoTranslateComponentUVE{});
    }
    sceneGraph.SetParentUVE(entityManager, child, root);
    entityManager.GetComponentUVE<TransformComponentUVE>(child).topLevel = true;

    entityManager.GetComponentUVE<ProcessComponentUVE>(root).mode = ProcessModeUVE::WhenPaused;
    entityManager.GetComponentUVE<AutoTranslateComponentUVE>(root).mode = AutoTranslateModeUVE::Disabled;
    sceneGraph.UpdateUVE(entityManager);

    EXPECT_EQ(entityManager.GetComponentUVE<ProcessComponentUVE>(child).resolvedModeInHierarchy,
              ProcessModeUVE::WhenPaused);
    EXPECT_EQ(entityManager.GetComponentUVE<AutoTranslateComponentUVE>(child).resolvedModeInHierarchy,
              AutoTranslateModeUVE::Disabled);
}

TEST_F(SceneGraphUVETest, UpdateUVE_ARootWithNoAncestorsResolvesToTheHierarchyDefaults) {
    const EntityUVE root = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, root, TransformComponentUVE{});
    entityManager.AddComponentUVE<ProcessComponentUVE>(root, ProcessComponentUVE{});
    entityManager.AddComponentUVE<ThreadGroupComponentUVE>(root, ThreadGroupComponentUVE{});
    entityManager.AddComponentUVE<AutoTranslateComponentUVE>(root, AutoTranslateComponentUVE{});

    sceneGraph.UpdateUVE(entityManager);

    // Inherit at the top of a hierarchy means the default, not "unanswered".
    EXPECT_EQ(entityManager.GetComponentUVE<ProcessComponentUVE>(root).resolvedModeInHierarchy,
              ProcessModeUVE::Pausable);
    EXPECT_EQ(entityManager.GetComponentUVE<ThreadGroupComponentUVE>(root).resolvedModeInHierarchy,
              ThreadGroupModeUVE::MainThread);
    EXPECT_EQ(entityManager.GetComponentUVE<AutoTranslateComponentUVE>(root).resolvedModeInHierarchy,
              AutoTranslateModeUVE::Always);
}

TEST_F(SceneGraphUVETest, TryGetResolvedNodeModesUVE_AnswersForANodeThatCarriesNoComponent) {
    // The case the query exists for. The resolved mode is written onto a component only when the
    // entity carries one, so a consumer that read the component alone would translate a label
    // with no Auto Translate component sitting under a Disabled menu. The query answers for it.
    const EntityUVE menu = entityManager.CreateEntityUVE();
    const EntityUVE label = entityManager.CreateEntityUVE();
    for (const EntityUVE entity : {menu, label}) {
        sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
    }
    sceneGraph.SetParentUVE(entityManager, label, menu);
    AutoTranslateComponentUVE disabled{};
    disabled.mode = AutoTranslateModeUVE::Disabled;
    entityManager.AddComponentUVE<AutoTranslateComponentUVE>(menu, disabled);
    ProcessComponentUVE always{};
    always.mode = ProcessModeUVE::Always;
    entityManager.AddComponentUVE<ProcessComponentUVE>(menu, always);

    sceneGraph.UpdateUVE(entityManager);

    ASSERT_FALSE(entityManager.HasComponentUVE<AutoTranslateComponentUVE>(label));
    const std::optional<ResolvedNodeModesUVE> resolved = sceneGraph.TryGetResolvedNodeModesUVE(label);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->autoTranslate, AutoTranslateModeUVE::Disabled);
    EXPECT_EQ(resolved->process, ProcessModeUVE::Always);
    EXPECT_EQ(resolved->threadGroup, ThreadGroupModeUVE::MainThread);
}

TEST_F(SceneGraphUVETest, TryGetResolvedNodeModesUVE_IsEmptyForAnEntityTheUpdateNeverSaw) {
    // Not a scene-graph node at all, and a node created after the last update: in both cases
    // there is no answer yet, and saying so is better than inventing the default.
    const EntityUVE bare = entityManager.CreateEntityUVE();
    sceneGraph.UpdateUVE(entityManager);
    EXPECT_FALSE(sceneGraph.TryGetResolvedNodeModesUVE(bare).has_value());

    const EntityUVE late = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, late, TransformComponentUVE{});
    EXPECT_FALSE(sceneGraph.TryGetResolvedNodeModesUVE(late).has_value());
    sceneGraph.UpdateUVE(entityManager);
    ASSERT_TRUE(sceneGraph.TryGetResolvedNodeModesUVE(late).has_value());
    // A root that opted into nothing resolves to the hierarchy defaults, never to Inherit.
    EXPECT_EQ(*sceneGraph.TryGetResolvedNodeModesUVE(late), ResolvedNodeModesUVE{});
}

// A pure Node is in the hierarchy with no transform of its own - the scene root is one. The rule
// these lock: it passes the modes and visibility down like any node, and it cuts the transform
// chain, because there is no transform on it to compose from.

[[nodiscard]] EntityUVE CreatePureNodeUVE(EntityManagerUVE& entityManager) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<HierarchyComponentUVE>(entity, HierarchyComponentUVE{});
    return entity;
}

TEST_F(SceneGraphUVETest, UpdateUVE_AChildOfAPureNodeStartsItsOwnTransformChain) {
    const EntityUVE node = CreatePureNodeUVE(entityManager);
    const EntityUVE child = entityManager.CreateEntityUVE();
    TransformComponentUVE local{};
    local.localPosition = Math::Vector3UVE{3.0F, 4.0F, 5.0F};
    local.localScale = Math::Vector3UVE{2.0F, 2.0F, 2.0F};
    sceneGraph.AttachTransformUVE(entityManager, child, local);
    sceneGraph.SetParentUVE(entityManager, child, node);

    sceneGraph.UpdateUVE(entityManager);

    const WorldTransformComponentUVE& world = entityManager.GetComponentUVE<WorldTransformComponentUVE>(child);
    EXPECT_FALSE(world.dirty);
    EXPECT_EQ(world.worldPosition, local.localPosition);
    EXPECT_EQ(world.worldScale, local.localScale);
    // The pure Node gained nothing: the sweep never gives it a transform.
    EXPECT_FALSE(entityManager.HasComponentUVE<WorldTransformComponentUVE>(node));
}

TEST_F(SceneGraphUVETest, UpdateUVE_APureNodeBetweenSpatialNodesCutsTheChainButNotTheModes) {
    // Spatial -> pure -> spatial. The grandchild must not compose from the spatial grandparent:
    // the node in between has no transform, so the chain restarts there. The modes and
    // visibility, which do not depend on a transform, still reach it.
    const EntityUVE grandparent = entityManager.CreateEntityUVE();
    TransformComponentUVE moved{};
    moved.localPosition = Math::Vector3UVE{100.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, grandparent, moved);
    ProcessComponentUVE always{};
    always.mode = ProcessModeUVE::Always;
    entityManager.AddComponentUVE<ProcessComponentUVE>(grandparent, always);
    VisibilityComponentUVE hidden{};
    hidden.visible = false;
    entityManager.AddComponentUVE<VisibilityComponentUVE>(grandparent, hidden);

    const EntityUVE node = CreatePureNodeUVE(entityManager);
    // SetParentUVE is for spatial children; a pure Node's parent is authored directly.
    entityManager.GetComponentUVE<HierarchyComponentUVE>(node).parent = grandparent;
    AutoTranslateComponentUVE disabled{};
    disabled.mode = AutoTranslateModeUVE::Disabled;
    entityManager.AddComponentUVE<AutoTranslateComponentUVE>(node, disabled);

    const EntityUVE grandchild = entityManager.CreateEntityUVE();
    TransformComponentUVE local{};
    local.localPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
    sceneGraph.AttachTransformUVE(entityManager, grandchild, local);
    sceneGraph.SetParentUVE(entityManager, grandchild, node);
    entityManager.AddComponentUVE<VisibilityComponentUVE>(grandchild, VisibilityComponentUVE{});

    sceneGraph.UpdateUVE(entityManager);

    EXPECT_EQ(entityManager.GetComponentUVE<WorldTransformComponentUVE>(grandchild).worldPosition,
              local.localPosition);
    EXPECT_FALSE(entityManager.GetComponentUVE<VisibilityComponentUVE>(grandchild).visibleInHierarchy);

    const std::optional<ResolvedNodeModesUVE> resolved = sceneGraph.TryGetResolvedNodeModesUVE(grandchild);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->process, ProcessModeUVE::Always);
    EXPECT_EQ(resolved->autoTranslate, AutoTranslateModeUVE::Disabled);
}

TEST_F(SceneGraphUVETest, TryGetResolvedNodeModesUVE_AnswersForThePureNodeItself) {
    // The scene root's own Process and Thread Group settings are what its whole scene inherits, so
    // the pure Node carrying them must have an answer of its own, published to its components.
    const EntityUVE root = CreatePureNodeUVE(entityManager);
    ProcessComponentUVE whenPaused{};
    whenPaused.mode = ProcessModeUVE::WhenPaused;
    entityManager.AddComponentUVE<ProcessComponentUVE>(root, whenPaused);
    ThreadGroupComponentUVE subThread{};
    subThread.mode = ThreadGroupModeUVE::SubThread;
    entityManager.AddComponentUVE<ThreadGroupComponentUVE>(root, subThread);

    const EntityUVE child = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, child, TransformComponentUVE{});
    sceneGraph.SetParentUVE(entityManager, child, root);

    sceneGraph.UpdateUVE(entityManager);

    const std::optional<ResolvedNodeModesUVE> own = sceneGraph.TryGetResolvedNodeModesUVE(root);
    ASSERT_TRUE(own.has_value());
    EXPECT_EQ(own->process, ProcessModeUVE::WhenPaused);
    EXPECT_EQ(own->threadGroup, ThreadGroupModeUVE::SubThread);
    EXPECT_EQ(entityManager.GetComponentUVE<ProcessComponentUVE>(root).resolvedModeInHierarchy,
              ProcessModeUVE::WhenPaused);

    const std::optional<ResolvedNodeModesUVE> inherited = sceneGraph.TryGetResolvedNodeModesUVE(child);
    ASSERT_TRUE(inherited.has_value());
    EXPECT_EQ(inherited->process, ProcessModeUVE::WhenPaused);
    EXPECT_EQ(inherited->threadGroup, ThreadGroupModeUVE::SubThread);
}

TEST_F(SceneGraphUVETest, UpdateUVE_ReparentingRecomputesInheritedVisibility) {
    // Moving a node between a hidden and a visible parent has to change its answer. Nothing else
    // in the sweep is keyed on the old parent, so a cached result would survive the move.
    const EntityUVE hiddenParent = entityManager.CreateEntityUVE();
    const EntityUVE shownParent = entityManager.CreateEntityUVE();
    const EntityUVE child = entityManager.CreateEntityUVE();
    for (const EntityUVE entity : {hiddenParent, shownParent, child}) {
        sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
        entityManager.AddComponentUVE<VisibilityComponentUVE>(entity, VisibilityComponentUVE{});
    }
    entityManager.GetComponentUVE<VisibilityComponentUVE>(hiddenParent).visible = false;

    sceneGraph.SetParentUVE(entityManager, child, hiddenParent);
    sceneGraph.UpdateUVE(entityManager);
    ASSERT_FALSE(entityManager.GetComponentUVE<VisibilityComponentUVE>(child).visibleInHierarchy);

    sceneGraph.SetParentUVE(entityManager, child, shownParent);
    sceneGraph.UpdateUVE(entityManager);
    EXPECT_TRUE(entityManager.GetComponentUVE<VisibilityComponentUVE>(child).visibleInHierarchy);

    // And back, so the test is not passing on a one-way transition.
    sceneGraph.SetParentUVE(entityManager, child, hiddenParent);
    sceneGraph.UpdateUVE(entityManager);
    EXPECT_FALSE(entityManager.GetComponentUVE<VisibilityComponentUVE>(child).visibleInHierarchy);
}

TEST_F(SceneGraphUVETest, UpdateUVE_ANonFiniteTransformDoesNotUnhideASubtree) {
    // Being hidden and having a broken transform are separate failures, and the sweep has a
    // dedicated early-out arm for an invalid parent. If that arm skipped the visibility work, a
    // single NaN anywhere in a hidden subtree would quietly reveal everything below it - a bug
    // that needs both conditions at once to appear.
    const EntityUVE hiddenRoot = entityManager.CreateEntityUVE();
    const EntityUVE broken = entityManager.CreateEntityUVE();
    const EntityUVE belowBroken = entityManager.CreateEntityUVE();
    for (const EntityUVE entity : {hiddenRoot, broken, belowBroken}) {
        sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
        entityManager.AddComponentUVE<VisibilityComponentUVE>(entity, VisibilityComponentUVE{});
    }
    sceneGraph.SetParentUVE(entityManager, broken, hiddenRoot);
    sceneGraph.SetParentUVE(entityManager, belowBroken, broken);

    entityManager.GetComponentUVE<VisibilityComponentUVE>(hiddenRoot).visible = false;
    entityManager.GetComponentUVE<TransformComponentUVE>(broken).localPosition.x =
        std::numeric_limits<float>::quiet_NaN();
    sceneGraph.UpdateUVE(entityManager);

    EXPECT_FALSE(entityManager.GetComponentUVE<VisibilityComponentUVE>(broken).visibleInHierarchy);
    EXPECT_FALSE(entityManager.GetComponentUVE<VisibilityComponentUVE>(belowBroken).visibleInHierarchy)
        << "a broken transform must not cancel a hidden ancestor";
}

TEST_F(SceneGraphUVETest, UpdateUVE_DefaultVisibilityIsVisible) {
    // The default has to be visible, or adding the component to an entity would make it vanish -
    // which is the opposite of what someone reaching for a visibility toggle expects.
    const EntityUVE entity = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
    entityManager.AddComponentUVE<VisibilityComponentUVE>(entity, VisibilityComponentUVE{});

    sceneGraph.UpdateUVE(entityManager);

    EXPECT_TRUE(entityManager.GetComponentUVE<VisibilityComponentUVE>(entity).visible);
    EXPECT_TRUE(entityManager.GetComponentUVE<VisibilityComponentUVE>(entity).visibleInHierarchy);
    EXPECT_TRUE(IsVisibilityComponentValidUVE(VisibilityComponentUVE{}));
}

TEST(TransformRotationAuthoringUVETest, TypedAnglesSurviveGimbalLock) {
    // Measured before this field existed: typing (90, 45, 30) and reading it back gave
    // (60, 90, 45). The rotation was right; the numbers the author typed were gone, and there was
    // no way to get them back because a quaternion does not record which of the infinitely many
    // angle triples at a pole you meant.
    constexpr float kDegreesToRadians = 3.14159265F / 180.0F;
    TransformComponentUVE transform;
    transform.localEulerRadians =
        Math::Vector3UVE{90.0F * kDegreesToRadians, 45.0F * kDegreesToRadians, 30.0F * kDegreesToRadians};
    ASSERT_TRUE(TrySyncRotationFromEulerUVE(transform));

    Math::Vector3UVE shown{};
    ASSERT_TRUE(TryGetDisplayEulerUVE(transform, shown));
    EXPECT_NEAR(shown.x / kDegreesToRadians, 90.0F, 1e-3F);
    EXPECT_NEAR(shown.y / kDegreesToRadians, 45.0F, 1e-3F);
    EXPECT_NEAR(shown.z / kDegreesToRadians, 30.0F, 1e-3F);

    // And the rotation itself is still correct - keeping the angles must not cost correctness.
    EXPECT_TRUE(IsTransformComponentValidUVE(transform));
}

TEST(TransformRotationAuthoringUVETest, TurnsBeyondAFullCircleAreNotFoldedAway) {
    // 370 degrees used to read back as 10. That is not a rounding difference: a quaternion cannot
    // tell one turn from two, so an author animating a full spin plus a bit lost the spin.
    constexpr float kDegreesToRadians = 3.14159265F / 180.0F;
    TransformComponentUVE transform;
    transform.localEulerRadians = Math::Vector3UVE{0.0F, 370.0F * kDegreesToRadians, 0.0F};
    ASSERT_TRUE(TrySyncRotationFromEulerUVE(transform));

    Math::Vector3UVE shown{};
    ASSERT_TRUE(TryGetDisplayEulerUVE(transform, shown));
    EXPECT_NEAR(shown.y / kDegreesToRadians, 370.0F, 1e-3F) << "the extra turn must be preserved";
}

TEST(TransformRotationAuthoringUVETest, RepeatedDisplayDoesNotDriftTheRotation) {
    // The worst of the three, because it needs no user action at all: re-extracting Euler angles
    // every redraw accumulated round-trip error, and a transform nobody touched drifted by up to
    // 54 degrees over a thousand frames. Storing the authored angles makes redraw a read.
    constexpr float kDegreesToRadians = 3.14159265F / 180.0F;
    TransformComponentUVE transform;
    transform.localEulerRadians = Math::Vector3UVE{23.7F * kDegreesToRadians, 41.3F * kDegreesToRadians,
                                                   67.9F * kDegreesToRadians};
    ASSERT_TRUE(TrySyncRotationFromEulerUVE(transform));

    // A thousand draw-then-write-back cycles, which is what an Inspector left open does.
    for (int frame = 0; frame < 1000; ++frame) {
        Math::Vector3UVE shown{};
        ASSERT_TRUE(TryGetDisplayEulerUVE(transform, shown));
        transform.localEulerRadians = shown;
        ASSERT_TRUE(TrySyncRotationFromEulerUVE(transform));
    }

    Math::Vector3UVE finalAngles{};
    ASSERT_TRUE(TryGetDisplayEulerUVE(transform, finalAngles));
    EXPECT_NEAR(finalAngles.x / kDegreesToRadians, 23.7F, 1e-3F);
    EXPECT_NEAR(finalAngles.y / kDegreesToRadians, 41.3F, 1e-3F);
    EXPECT_NEAR(finalAngles.z / kDegreesToRadians, 67.9F, 1e-3F);
}

TEST(TransformRotationAuthoringUVETest, QuaternionModeShowsTheQuaternionNotStaleAngles) {
    // When physics or a gizmo writes the quaternion, the stored Euler angles describe a rotation
    // the object no longer has. Showing them would be a lie, so Quaternion mode extracts instead -
    // the one case where a derived view is the honest answer.
    constexpr float kDegreesToRadians = 3.14159265F / 180.0F;
    TransformComponentUVE transform;
    transform.localEulerRadians = Math::Vector3UVE{10.0F * kDegreesToRadians, 20.0F * kDegreesToRadians,
                                                   30.0F * kDegreesToRadians};
    ASSERT_TRUE(TrySyncRotationFromEulerUVE(transform));

    // Physics writes a rotation directly and declares the quaternion authoritative.
    Math::QuaternionUVE fromPhysics{};
    ASSERT_TRUE(Math::TryMakeEulerOrderedUVE(Math::Vector3UVE{45.0F * kDegreesToRadians, 0.0F, 0.0F},
                                             Math::EulerOrderUVE::XYZ, fromPhysics));
    transform.localRotation = fromPhysics;
    transform.rotationEditMode = RotationEditModeUVE::Quaternion;

    Math::Vector3UVE shown{};
    ASSERT_TRUE(TryGetDisplayEulerUVE(transform, shown));
    EXPECT_NEAR(shown.x / kDegreesToRadians, 45.0F, 1e-2F) << "must show the physics rotation";
    EXPECT_NEAR(shown.y / kDegreesToRadians, 0.0F, 1e-2F) << "not the stale authored 20 degrees";

    // And syncing must not overwrite what physics wrote.
    ASSERT_TRUE(TrySyncRotationFromEulerUVE(transform));
    EXPECT_TRUE(RepresentSameRotationForTransformUVE(transform.localRotation, fromPhysics))
        << "Quaternion mode must not replay the stale Euler angles over the live rotation";
}

TEST(TransformRotationAuthoringUVETest, EachOrderGivesADifferentRotationFromTheSameAngles) {
    // The dropdown has to mean something. Same typed angles, different order, different result -
    // otherwise the control is decoration.
    constexpr float kDegreesToRadians = 3.14159265F / 180.0F;
    const Math::Vector3UVE angles{35.0F * kDegreesToRadians, 50.0F * kDegreesToRadians,
                                  70.0F * kDegreesToRadians};
    TransformComponentUVE xyz;
    xyz.localEulerRadians = angles;
    xyz.eulerOrder = Math::EulerOrderUVE::XYZ;
    ASSERT_TRUE(TrySyncRotationFromEulerUVE(xyz));

    TransformComponentUVE zxy;
    zxy.localEulerRadians = angles;
    zxy.eulerOrder = Math::EulerOrderUVE::ZXY;
    ASSERT_TRUE(TrySyncRotationFromEulerUVE(zxy));

    EXPECT_FALSE(RepresentSameRotationForTransformUVE(xyz.localRotation, zxy.localRotation));
    // Both must still be valid, normalized rotations.
    EXPECT_TRUE(IsTransformComponentValidUVE(xyz));
    EXPECT_TRUE(IsTransformComponentValidUVE(zxy));
}

TEST(TransformRotationAuthoringUVETest, ADefaultTransformIsUnchangedByTheNewFields) {
    // Every entity in every existing scene has a default-constructed transform. The new fields
    // must leave it exactly as it was: identity rotation, Euler mode, XYZ order.
    const TransformComponentUVE transform;
    EXPECT_FLOAT_EQ(transform.localRotation.w, 1.0F);
    EXPECT_FLOAT_EQ(transform.localEulerRadians.x, 0.0F);
    EXPECT_EQ(transform.eulerOrder, Math::EulerOrderUVE::XYZ);
    EXPECT_EQ(transform.rotationEditMode, RotationEditModeUVE::Euler);
    EXPECT_TRUE(IsTransformComponentValidUVE(transform));

    TransformComponentUVE synced = transform;
    ASSERT_TRUE(TrySyncRotationFromEulerUVE(synced));
    EXPECT_FLOAT_EQ(synced.localRotation.w, 1.0F) << "syncing a default must stay identity";
}

TEST(TransformRotationAuthoringUVETest, NonFiniteAnglesAreRejectedWithoutTouchingTheRotation) {
    // A bad edit must not be able to teleport an object. The previous rotation stays.
    TransformComponentUVE transform;
    transform.localEulerRadians = Math::Vector3UVE{0.5F, 0.5F, 0.5F};
    ASSERT_TRUE(TrySyncRotationFromEulerUVE(transform));
    const Math::QuaternionUVE good = transform.localRotation;

    transform.localEulerRadians = Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    EXPECT_FALSE(TrySyncRotationFromEulerUVE(transform));
    EXPECT_FLOAT_EQ(transform.localRotation.x, good.x) << "a rejected edit must leave the rotation alone";
    EXPECT_FLOAT_EQ(transform.localRotation.w, good.w);
}

TEST_F(SceneGraphUVETest, UpdateUVE_TopLevelIgnoresTheParentTransform) {
    // The whole point: local values become world values, as if the entity had no parent at all.
    const EntityUVE parent = entityManager.CreateEntityUVE();
    const EntityUVE child = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, parent, TransformComponentUVE{});
    sceneGraph.AttachTransformUVE(entityManager, child, TransformComponentUVE{});
    sceneGraph.SetParentUVE(entityManager, child, parent);

    TransformComponentUVE parentLocal;
    parentLocal.localPosition = Math::Vector3UVE{100.0F, 50.0F, -20.0F};
    parentLocal.localScale = Math::Vector3UVE{3.0F, 3.0F, 3.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, parent, parentLocal);

    TransformComponentUVE childLocal;
    childLocal.localPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
    childLocal.topLevel = true;
    sceneGraph.SetLocalTransformUVE(entityManager, child, childLocal);
    sceneGraph.UpdateUVE(entityManager);

    const WorldTransformComponentUVE& world = entityManager.GetComponentUVE<WorldTransformComponentUVE>(child);
    EXPECT_NEAR(world.worldPosition.x, 1.0F, kEpsilon) << "the parent's offset must not apply";
    EXPECT_NEAR(world.worldPosition.y, 2.0F, kEpsilon);
    EXPECT_NEAR(world.worldPosition.z, 3.0F, kEpsilon);
    EXPECT_NEAR(world.worldScale.x, 1.0F, kEpsilon) << "nor the parent's scale";
}

TEST_F(SceneGraphUVETest, UpdateUVE_TopLevelStillInheritsVisibility) {
    // Top level cuts the TRANSFORM chain only. If it also detached visibility there would be no
    // way to get the organisational half - saved and deleted with the parent - without losing the
    // ability to hide the group, which is most of why people parent things in the first place.
    const EntityUVE parent = entityManager.CreateEntityUVE();
    const EntityUVE child = entityManager.CreateEntityUVE();
    for (const EntityUVE entity : {parent, child}) {
        sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
        entityManager.AddComponentUVE<VisibilityComponentUVE>(entity, VisibilityComponentUVE{});
    }
    sceneGraph.SetParentUVE(entityManager, child, parent);

    TransformComponentUVE childLocal;
    childLocal.topLevel = true;
    sceneGraph.SetLocalTransformUVE(entityManager, child, childLocal);
    entityManager.GetComponentUVE<VisibilityComponentUVE>(parent).visible = false;
    sceneGraph.UpdateUVE(entityManager);

    EXPECT_FALSE(entityManager.GetComponentUVE<VisibilityComponentUVE>(child).visibleInHierarchy)
        << "hiding a parent must still hide a top-level child";
}

TEST_F(SceneGraphUVETest, UpdateUVE_TopLevelSurvivesANonFiniteParent) {
    // An entity that does not read its parent's world transform cannot be corrupted by it.
    // Invalidating it anyway would invent a dependency it deliberately does not have - and would
    // make top level useless for the case it exists for, since a broken rig would take the
    // detached camera down with it.
    const EntityUVE parent = entityManager.CreateEntityUVE();
    const EntityUVE normalChild = entityManager.CreateEntityUVE();
    const EntityUVE topLevelChild = entityManager.CreateEntityUVE();
    for (const EntityUVE entity : {parent, normalChild, topLevelChild}) {
        sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
    }
    sceneGraph.SetParentUVE(entityManager, normalChild, parent);
    sceneGraph.SetParentUVE(entityManager, topLevelChild, parent);

    TransformComponentUVE detached;
    detached.localPosition = Math::Vector3UVE{7.0F, 8.0F, 9.0F};
    detached.topLevel = true;
    sceneGraph.SetLocalTransformUVE(entityManager, topLevelChild, detached);

    // Corrupt the parent directly - SetLocalTransformUVE would reject a non-finite transform.
    entityManager.GetComponentUVE<TransformComponentUVE>(parent).localPosition.x =
        std::numeric_limits<float>::quiet_NaN();
    entityManager.GetComponentUVE<WorldTransformComponentUVE>(parent).dirty = true;
    sceneGraph.UpdateUVE(entityManager);

    // The ordinary child is invalidated, as it always was.
    EXPECT_TRUE(entityManager.GetComponentUVE<WorldTransformComponentUVE>(normalChild).dirty);
    // The top-level one is untouched and correct.
    const WorldTransformComponentUVE& detachedWorld =
        entityManager.GetComponentUVE<WorldTransformComponentUVE>(topLevelChild);
    EXPECT_FALSE(detachedWorld.dirty) << "a NaN above must not reach an entity that never reads it";
    EXPECT_NEAR(detachedWorld.worldPosition.x, 7.0F, kEpsilon);
}

TEST_F(SceneGraphUVETest, UpdateUVE_ClearingTopLevelReattachesToTheParentTransform) {
    // The flag has to be reversible in place. Nothing else in the sweep is keyed on it, so a
    // cached world transform could otherwise survive the change.
    const EntityUVE parent = entityManager.CreateEntityUVE();
    const EntityUVE child = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, parent, TransformComponentUVE{});
    sceneGraph.AttachTransformUVE(entityManager, child, TransformComponentUVE{});
    sceneGraph.SetParentUVE(entityManager, child, parent);

    TransformComponentUVE parentLocal;
    parentLocal.localPosition = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, parent, parentLocal);

    TransformComponentUVE childLocal;
    childLocal.localPosition = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    childLocal.topLevel = true;
    sceneGraph.SetLocalTransformUVE(entityManager, child, childLocal);
    sceneGraph.UpdateUVE(entityManager);
    ASSERT_NEAR(entityManager.GetComponentUVE<WorldTransformComponentUVE>(child).worldPosition.x, 1.0F, kEpsilon);

    childLocal.topLevel = false;
    sceneGraph.SetLocalTransformUVE(entityManager, child, childLocal);
    sceneGraph.UpdateUVE(entityManager);
    EXPECT_NEAR(entityManager.GetComponentUVE<WorldTransformComponentUVE>(child).worldPosition.x, 11.0F, kEpsilon)
        << "clearing the flag must restore the parent offset";
}

TEST_F(SceneGraphUVETest, UpdateUVE_TopLevelBecomesTheOriginForItsOwnChildren) {
    // A top-level entity is a root for the subtree BELOW it: its children compose from it as
    // normal. If they did not, the flag would flatten a whole branch instead of detaching one
    // node, and a detached rig would come apart.
    const EntityUVE grandparent = entityManager.CreateEntityUVE();
    const EntityUVE detached = entityManager.CreateEntityUVE();
    const EntityUVE grandchild = entityManager.CreateEntityUVE();
    for (const EntityUVE entity : {grandparent, detached, grandchild}) {
        sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
    }
    sceneGraph.SetParentUVE(entityManager, detached, grandparent);
    sceneGraph.SetParentUVE(entityManager, grandchild, detached);

    TransformComponentUVE grandparentLocal;
    grandparentLocal.localPosition = Math::Vector3UVE{100.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, grandparent, grandparentLocal);

    TransformComponentUVE detachedLocal;
    detachedLocal.localPosition = Math::Vector3UVE{5.0F, 0.0F, 0.0F};
    detachedLocal.topLevel = true;
    sceneGraph.SetLocalTransformUVE(entityManager, detached, detachedLocal);

    TransformComponentUVE grandchildLocal;
    grandchildLocal.localPosition = Math::Vector3UVE{2.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, grandchild, grandchildLocal);
    sceneGraph.UpdateUVE(entityManager);

    EXPECT_NEAR(entityManager.GetComponentUVE<WorldTransformComponentUVE>(detached).worldPosition.x, 5.0F, kEpsilon);
    EXPECT_NEAR(entityManager.GetComponentUVE<WorldTransformComponentUVE>(grandchild).worldPosition.x, 7.0F, kEpsilon)
        << "the grandchild composes from the detached node, not from the grandparent";
}

TEST_F(SceneGraphUVETest, UpdateUVE_TopLevelOnARootEntityChangesNothing) {
    // A root has no parent to ignore. Setting the flag there must be a no-op rather than an edge
    // case - the Inspector shows the checkbox on every node, including roots.
    const EntityUVE root = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, root, TransformComponentUVE{});
    TransformComponentUVE local;
    local.localPosition = Math::Vector3UVE{4.0F, 5.0F, 6.0F};
    local.topLevel = true;
    sceneGraph.SetLocalTransformUVE(entityManager, root, local);
    sceneGraph.UpdateUVE(entityManager);

    const WorldTransformComponentUVE& world = entityManager.GetComponentUVE<WorldTransformComponentUVE>(root);
    EXPECT_NEAR(world.worldPosition.x, 4.0F, kEpsilon);
    EXPECT_FALSE(world.dirty);
}

TEST_F(SceneGraphUVETest, UpdateUVE_VisibilityParentOverridesTheTransformParent) {
    // The case the redirect exists for: a weapon parented to a hand for position, but which
    // should disappear with the whole character rather than with the hand.
    const EntityUVE character = entityManager.CreateEntityUVE();
    const EntityUVE hand = entityManager.CreateEntityUVE();
    const EntityUVE weapon = entityManager.CreateEntityUVE();
    for (const EntityUVE entity : {character, hand, weapon}) {
        sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
        entityManager.AddComponentUVE<VisibilityComponentUVE>(entity, VisibilityComponentUVE{});
    }
    sceneGraph.SetParentUVE(entityManager, hand, character);
    sceneGraph.SetParentUVE(entityManager, weapon, hand);
    entityManager.GetComponentUVE<VisibilityComponentUVE>(weapon).visibilityParent = character;

    // Hiding the intermediate hand must NOT hide the weapon - it no longer inherits from there.
    entityManager.GetComponentUVE<VisibilityComponentUVE>(hand).visible = false;
    sceneGraph.UpdateUVE(entityManager);
    EXPECT_FALSE(entityManager.GetComponentUVE<VisibilityComponentUVE>(hand).visibleInHierarchy);
    EXPECT_TRUE(entityManager.GetComponentUVE<VisibilityComponentUVE>(weapon).visibleInHierarchy)
        << "the redirect must replace the transform-parent chain, not add to it";

    // Hiding the character does hide it.
    entityManager.GetComponentUVE<VisibilityComponentUVE>(hand).visible = true;
    entityManager.GetComponentUVE<VisibilityComponentUVE>(character).visible = false;
    sceneGraph.UpdateUVE(entityManager);
    EXPECT_FALSE(entityManager.GetComponentUVE<VisibilityComponentUVE>(weapon).visibleInHierarchy);
}

TEST_F(SceneGraphUVETest, UpdateUVE_ARedirectDoesNotOverrideTheEntitysOwnHiddenState) {
    // The redirect replaces the INHERITED half only. An entity hidden in its own right stays
    // hidden no matter what it points at - the same rule the transform-parent path follows.
    const EntityUVE target = entityManager.CreateEntityUVE();
    const EntityUVE entity = entityManager.CreateEntityUVE();
    for (const EntityUVE created : {target, entity}) {
        sceneGraph.AttachTransformUVE(entityManager, created, TransformComponentUVE{});
        entityManager.AddComponentUVE<VisibilityComponentUVE>(created, VisibilityComponentUVE{});
    }
    entityManager.GetComponentUVE<VisibilityComponentUVE>(entity).visibilityParent = target;
    entityManager.GetComponentUVE<VisibilityComponentUVE>(entity).visible = false;
    sceneGraph.UpdateUVE(entityManager);

    EXPECT_FALSE(entityManager.GetComponentUVE<VisibilityComponentUVE>(entity).visibleInHierarchy)
        << "pointing at a visible target must not reveal something hidden in its own right";
}

TEST_F(SceneGraphUVETest, UpdateUVE_RedirectsFollowAChainOfRedirects) {
    // A visibility parent may itself redirect. Resolving only one hop would silently ignore
    // everything past the first link.
    const EntityUVE root = entityManager.CreateEntityUVE();
    const EntityUVE middle = entityManager.CreateEntityUVE();
    const EntityUVE leaf = entityManager.CreateEntityUVE();
    for (const EntityUVE entity : {root, middle, leaf}) {
        sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
        entityManager.AddComponentUVE<VisibilityComponentUVE>(entity, VisibilityComponentUVE{});
    }
    entityManager.GetComponentUVE<VisibilityComponentUVE>(leaf).visibilityParent = middle;
    entityManager.GetComponentUVE<VisibilityComponentUVE>(middle).visibilityParent = root;
    entityManager.GetComponentUVE<VisibilityComponentUVE>(root).visible = false;
    sceneGraph.UpdateUVE(entityManager);

    EXPECT_FALSE(entityManager.GetComponentUVE<VisibilityComponentUVE>(leaf).visibleInHierarchy)
        << "the chain must be followed to its end";
}

TEST_F(SceneGraphUVETest, UpdateUVE_ARedirectCycleFallsBackInsteadOfHanging) {
    // Nothing prevents an author pointing two nodes at each other, and a .uvescene can be
    // hand-edited into one. The resolver must terminate and must not pick an arbitrary winner:
    // falling back to the transform-parent answer leaves the entities visible and predictable.
    const EntityUVE first = entityManager.CreateEntityUVE();
    const EntityUVE second = entityManager.CreateEntityUVE();
    for (const EntityUVE entity : {first, second}) {
        sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
        entityManager.AddComponentUVE<VisibilityComponentUVE>(entity, VisibilityComponentUVE{});
    }
    entityManager.GetComponentUVE<VisibilityComponentUVE>(first).visibilityParent = second;
    entityManager.GetComponentUVE<VisibilityComponentUVE>(second).visibilityParent = first;

    sceneGraph.UpdateUVE(entityManager); // must return

    EXPECT_TRUE(entityManager.GetComponentUVE<VisibilityComponentUVE>(first).visibleInHierarchy);
    EXPECT_TRUE(entityManager.GetComponentUVE<VisibilityComponentUVE>(second).visibleInHierarchy);
}

TEST_F(SceneGraphUVETest, UpdateUVE_ADanglingRedirectDoesNotHideAnything) {
    // The target was deleted, or the reference came from a file that no longer matches the scene.
    // Losing a reference must not make geometry disappear - a vanished object with no error is
    // far harder to diagnose than one that is simply still there.
    const EntityUVE target = entityManager.CreateEntityUVE();
    const EntityUVE entity = entityManager.CreateEntityUVE();
    for (const EntityUVE created : {target, entity}) {
        sceneGraph.AttachTransformUVE(entityManager, created, TransformComponentUVE{});
        entityManager.AddComponentUVE<VisibilityComponentUVE>(created, VisibilityComponentUVE{});
    }
    entityManager.GetComponentUVE<VisibilityComponentUVE>(entity).visibilityParent = target;
    entityManager.DestroyEntityUVE(target);
    sceneGraph.UpdateUVE(entityManager);

    EXPECT_TRUE(entityManager.GetComponentUVE<VisibilityComponentUVE>(entity).visibleInHierarchy);
}

TEST_F(SceneGraphUVETest, UpdateUVE_ARedirectPicksUpTheTargetsOwnInheritedState) {
    // The target's answer includes ITS transform parent. Reading only the target's own `visible`
    // switch would miss a target that is hidden because its own parent is.
    const EntityUVE grandparent = entityManager.CreateEntityUVE();
    const EntityUVE target = entityManager.CreateEntityUVE();
    const EntityUVE entity = entityManager.CreateEntityUVE();
    for (const EntityUVE created : {grandparent, target, entity}) {
        sceneGraph.AttachTransformUVE(entityManager, created, TransformComponentUVE{});
        entityManager.AddComponentUVE<VisibilityComponentUVE>(created, VisibilityComponentUVE{});
    }
    sceneGraph.SetParentUVE(entityManager, target, grandparent);
    entityManager.GetComponentUVE<VisibilityComponentUVE>(entity).visibilityParent = target;

    // The target itself is switched on; its PARENT is not.
    entityManager.GetComponentUVE<VisibilityComponentUVE>(grandparent).visible = false;
    sceneGraph.UpdateUVE(entityManager);

    ASSERT_FALSE(entityManager.GetComponentUVE<VisibilityComponentUVE>(target).visibleInHierarchy);
    EXPECT_FALSE(entityManager.GetComponentUVE<VisibilityComponentUVE>(entity).visibleInHierarchy)
        << "a redirect inherits the target's resolved state, not just its own switch";
}

TEST_F(SceneGraphUVETest, UpdateUVE_InterpolationRecordsTwoPosesAsTheEntityMoves) {
    // The core mechanic. One pose is not enough to blend between, so the first recorded step must
    // NOT report itself as ready - otherwise a freshly spawned object blends in from wherever a
    // default-constructed "previous" happens to be.
    const EntityUVE entity = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
    entityManager.AddComponentUVE<PhysicsInterpolationComponentUVE>(
        entity, PhysicsInterpolationComponentUVE{});

    TransformComponentUVE local;
    local.localPosition = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, entity, local);
    sceneGraph.UpdateUVE(entityManager);

    {
        const PhysicsInterpolationComponentUVE& interpolation =
            entityManager.GetComponentUVE<PhysicsInterpolationComponentUVE>(entity);
        EXPECT_NEAR(interpolation.currentPosition.x, 10.0F, kEpsilon);
        EXPECT_NEAR(interpolation.previousPosition.x, 10.0F, kEpsilon)
            << "the first pose must seed previous to itself, not leave it at the origin";
    }

    local.localPosition = Math::Vector3UVE{20.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, entity, local);
    sceneGraph.UpdateUVE(entityManager);

    const PhysicsInterpolationComponentUVE& interpolation =
        entityManager.GetComponentUVE<PhysicsInterpolationComponentUVE>(entity);
    EXPECT_NEAR(interpolation.previousPosition.x, 10.0F, kEpsilon);
    EXPECT_NEAR(interpolation.currentPosition.x, 20.0F, kEpsilon);

    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};
    Math::Vector3UVE scale{};
    ASSERT_TRUE(TryGetInterpolatedPoseUVE(interpolation, 0.5F, position, rotation, scale));
    EXPECT_NEAR(position.x, 15.0F, kEpsilon) << "halfway between the two recorded poses";
}

TEST_F(SceneGraphUVETest, UpdateUVE_AStationaryEntityDoesNotCollapseItsTwoPoses) {
    // If every sweep recorded a pose, a stationary object would have previous == current, and the
    // first frame after it started moving would have nothing to blend from - a visible hitch
    // exactly when motion begins, which is the worst possible moment for one.
    const EntityUVE entity = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
    entityManager.AddComponentUVE<PhysicsInterpolationComponentUVE>(
        entity, PhysicsInterpolationComponentUVE{});

    TransformComponentUVE local;
    local.localPosition = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, entity, local);
    sceneGraph.UpdateUVE(entityManager);
    local.localPosition = Math::Vector3UVE{2.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, entity, local);
    sceneGraph.UpdateUVE(entityManager);

    // Several sweeps with nothing moving.
    for (int frame = 0; frame < 5; ++frame) {
        sceneGraph.UpdateUVE(entityManager);
    }

    const PhysicsInterpolationComponentUVE& interpolation =
        entityManager.GetComponentUVE<PhysicsInterpolationComponentUVE>(entity);
    EXPECT_NEAR(interpolation.previousPosition.x, 1.0F, kEpsilon)
        << "idle sweeps must not overwrite the previous pose";
    EXPECT_NEAR(interpolation.currentPosition.x, 2.0F, kEpsilon);
}

TEST_F(SceneGraphUVETest, UpdateUVE_InterpolationModeOffIsHonouredAndInherited) {
    // Off must win over an inheriting ancestor, and Inherit must actually follow the parent -
    // otherwise the three-value enum is really a two-value one.
    const EntityUVE parent = entityManager.CreateEntityUVE();
    const EntityUVE inheriting = entityManager.CreateEntityUVE();
    const EntityUVE explicitlyOff = entityManager.CreateEntityUVE();
    for (const EntityUVE entity : {parent, inheriting, explicitlyOff}) {
        sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
        entityManager.AddComponentUVE<PhysicsInterpolationComponentUVE>(
            entity, PhysicsInterpolationComponentUVE{});
    }
    sceneGraph.SetParentUVE(entityManager, inheriting, parent);
    sceneGraph.SetParentUVE(entityManager, explicitlyOff, parent);

    entityManager.GetComponentUVE<PhysicsInterpolationComponentUVE>(parent).mode =
        PhysicsInterpolationModeUVE::Off;
    entityManager.GetComponentUVE<PhysicsInterpolationComponentUVE>(explicitlyOff).mode =
        PhysicsInterpolationModeUVE::On;
    sceneGraph.UpdateUVE(entityManager);

    EXPECT_FALSE(entityManager.GetComponentUVE<PhysicsInterpolationComponentUVE>(parent).interpolatedInHierarchy);
    EXPECT_FALSE(entityManager.GetComponentUVE<PhysicsInterpolationComponentUVE>(inheriting).interpolatedInHierarchy)
        << "Inherit must follow the parent";
    EXPECT_TRUE(entityManager.GetComponentUVE<PhysicsInterpolationComponentUVE>(explicitlyOff).interpolatedInHierarchy)
        << "an explicit On must override an inheriting chain";
}

TEST_F(SceneGraphUVETest, UpdateUVE_AnEntityWithoutTheComponentPassesInterpolationThrough) {
    // The component is optional, so an intermediate node usually will not have one. It must not
    // break the chain - the same rule visibility follows.
    const EntityUVE grandparent = entityManager.CreateEntityUVE();
    const EntityUVE middle = entityManager.CreateEntityUVE();
    const EntityUVE grandchild = entityManager.CreateEntityUVE();
    for (const EntityUVE entity : {grandparent, middle, grandchild}) {
        sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
    }
    entityManager.AddComponentUVE<PhysicsInterpolationComponentUVE>(
        grandparent, PhysicsInterpolationComponentUVE{});
    entityManager.AddComponentUVE<PhysicsInterpolationComponentUVE>(
        grandchild, PhysicsInterpolationComponentUVE{});
    sceneGraph.SetParentUVE(entityManager, middle, grandparent);
    sceneGraph.SetParentUVE(entityManager, grandchild, middle);

    entityManager.GetComponentUVE<PhysicsInterpolationComponentUVE>(grandparent).mode =
        PhysicsInterpolationModeUVE::Off;
    sceneGraph.UpdateUVE(entityManager);

    EXPECT_FALSE(
        entityManager.GetComponentUVE<PhysicsInterpolationComponentUVE>(grandchild).interpolatedInHierarchy)
        << "a node without the component must pass the parent's setting through";
}

TEST_F(SceneGraphUVETest, UpdateUVE_ANonFiniteTransformDoesNotRecordAPoseToBlendTowards) {
    // Recording a NaN pose would hand the renderer something to blend TOWARDS, dragging the object
    // off to infinity over the following frames. Keeping the last good pose freezes it instead,
    // which is visible and recoverable.
    const EntityUVE entity = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
    entityManager.AddComponentUVE<PhysicsInterpolationComponentUVE>(
        entity, PhysicsInterpolationComponentUVE{});

    TransformComponentUVE local;
    local.localPosition = Math::Vector3UVE{3.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, entity, local);
    sceneGraph.UpdateUVE(entityManager);
    local.localPosition = Math::Vector3UVE{4.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, entity, local);
    sceneGraph.UpdateUVE(entityManager);

    // Corrupt directly - SetLocalTransformUVE would reject a non-finite value.
    entityManager.GetComponentUVE<TransformComponentUVE>(entity).localPosition.x =
        std::numeric_limits<float>::quiet_NaN();
    entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).dirty = true;
    sceneGraph.UpdateUVE(entityManager);

    const PhysicsInterpolationComponentUVE& interpolation =
        entityManager.GetComponentUVE<PhysicsInterpolationComponentUVE>(entity);
    EXPECT_TRUE(std::isfinite(interpolation.currentPosition.x)) << "a NaN must not reach the recorded pose";
    EXPECT_NEAR(interpolation.currentPosition.x, 4.0F, kEpsilon);
}

TEST(PhysicsInterpolationBlendUVETest, ASingleRecordedPoseReportsNotReadyRatherThanBlendingFromTheOrigin) {
    // The spawn bug this guards against only appears on an object's first visible frame, which is
    // precisely when nobody is looking for it.
    PhysicsInterpolationComponentUVE interpolation;
    interpolation.currentPosition = Math::Vector3UVE{100.0F, 0.0F, 0.0F};
    interpolation.hasPreviousPose = false;

    Math::Vector3UVE position{7.0F, 7.0F, 7.0F};
    Math::QuaternionUVE rotation{};
    Math::Vector3UVE scale{};
    EXPECT_FALSE(TryGetInterpolatedPoseUVE(interpolation, 0.5F, position, rotation, scale));
    EXPECT_FLOAT_EQ(position.x, 7.0F) << "a rejected blend must not write its outputs";
}

TEST(PhysicsInterpolationBlendUVETest, AlphaIsClampedRatherThanRejectedAtTheBoundaries) {
    // A timer that overshoots after a long frame should keep drawing smoothly, not stutter. And
    // alpha 1.0 simply means "the current pose", which is a legitimate request.
    PhysicsInterpolationComponentUVE interpolation;
    interpolation.previousPosition = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    interpolation.currentPosition = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
    interpolation.hasPreviousPose = true;

    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};
    Math::Vector3UVE scale{};
    ASSERT_TRUE(TryGetInterpolatedPoseUVE(interpolation, 1.4F, position, rotation, scale));
    EXPECT_NEAR(position.x, 10.0F, 1e-4F) << "an overshooting alpha must clamp to the current pose";
    ASSERT_TRUE(TryGetInterpolatedPoseUVE(interpolation, -0.3F, position, rotation, scale));
    EXPECT_NEAR(position.x, 0.0F, 1e-4F);

    // A non-finite alpha is a different matter - there is no sensible clamp, so it is refused.
    EXPECT_FALSE(TryGetInterpolatedPoseUVE(interpolation, std::numeric_limits<float>::quiet_NaN(), position,
                                           rotation, scale));
}

TEST(PhysicsInterpolationBlendUVETest, TheSimulatedPoseIsUsedWhenInterpolationIsOff) {
    // Off must actually mean off: the blend reports not-ready so the caller falls back to the
    // world transform, rather than quietly returning a blended pose anyway.
    PhysicsInterpolationComponentUVE interpolation;
    interpolation.previousPosition = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    interpolation.currentPosition = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
    interpolation.hasPreviousPose = true;
    interpolation.interpolatedInHierarchy = false;

    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};
    Math::Vector3UVE scale{};
    EXPECT_FALSE(TryGetInterpolatedPoseUVE(interpolation, 0.5F, position, rotation, scale));
}

TEST_F(SceneGraphUVETest, ChildrenKeepTheirOrderWhenAComponentMovesOneInStorage) {
    const EntityUVE parent = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, parent, TransformComponentUVE{});
    std::vector<EntityUVE> children;
    for (int i = 0; i < 4; ++i) {
        const EntityUVE child = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, child, TransformComponentUVE{});
        sceneGraph.SetParentUVE(entityManager, child, parent);
        children.push_back(child);
    }
    ASSERT_EQ(sceneGraph.GetChildrenUVE(entityManager, parent), children);

    // Adding a component moves the first child to another archetype, and so to the end of the
    // storage walk. Its place among its siblings must not follow.
    entityManager.AddComponentUVE<VisibilityComponentUVE>(children.front(), VisibilityComponentUVE{});
    EXPECT_EQ(sceneGraph.GetChildrenUVE(entityManager, parent), children);
    EXPECT_EQ(sceneGraph.GetSiblingIndexUVE(entityManager, children.front()), std::optional<std::size_t>{0U});
}

TEST_F(SceneGraphUVETest, AChildThatChangesParentGoesLastAndOneThatDoesNotStaysPut) {
    const EntityUVE a = entityManager.CreateEntityUVE();
    const EntityUVE b = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, a, TransformComponentUVE{});
    sceneGraph.AttachTransformUVE(entityManager, b, TransformComponentUVE{});
    std::vector<EntityUVE> underA;
    for (int i = 0; i < 3; ++i) {
        const EntityUVE child = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, child, TransformComponentUVE{});
        sceneGraph.SetParentUVE(entityManager, child, a);
        underA.push_back(child);
    }
    const EntityUVE moved = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, moved, TransformComponentUVE{});
    sceneGraph.SetParentUVE(entityManager, moved, b);

    // Setting the parent a child already has is not a move.
    sceneGraph.SetParentUVE(entityManager, underA.front(), a);
    EXPECT_EQ(sceneGraph.GetChildrenUVE(entityManager, a), underA);

    sceneGraph.SetParentUVE(entityManager, moved, a);
    underA.push_back(moved);
    EXPECT_EQ(sceneGraph.GetChildrenUVE(entityManager, a), underA);
}

TEST_F(SceneGraphUVETest, SetSiblingIndexUVE_MovesOneAndKeepsTheRestInOrder) {
    const EntityUVE parent = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, parent, TransformComponentUVE{});
    std::vector<EntityUVE> c;
    for (int i = 0; i < 4; ++i) {
        const EntityUVE child = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, child, TransformComponentUVE{});
        sceneGraph.SetParentUVE(entityManager, child, parent);
        c.push_back(child);
    }

    ASSERT_TRUE(sceneGraph.SetSiblingIndexUVE(entityManager, c[3], 1U));
    EXPECT_EQ(sceneGraph.GetChildrenUVE(entityManager, parent), (std::vector<EntityUVE>{c[0], c[3], c[1], c[2]}));
    ASSERT_TRUE(sceneGraph.SetSiblingIndexUVE(entityManager, c[0], 99U)); // past the end means last
    EXPECT_EQ(sceneGraph.GetChildrenUVE(entityManager, parent), (std::vector<EntityUVE>{c[3], c[1], c[2], c[0]}));
    EXPECT_EQ(sceneGraph.GetSiblingIndexUVE(entityManager, c[2]), std::optional<std::size_t>{2U});

    const EntityUVE loose = entityManager.CreateEntityUVE();
    EXPECT_FALSE(sceneGraph.SetSiblingIndexUVE(entityManager, loose, 0U));
    EXPECT_FALSE(sceneGraph.GetSiblingIndexUVE(entityManager, loose).has_value());
}

} // namespace
} // namespace UVE::Scene::Tests
