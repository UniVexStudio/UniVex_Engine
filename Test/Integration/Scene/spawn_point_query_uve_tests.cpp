// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/spawn_point_query_uve.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "uve/component/collider_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/spawn_point_3d_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Scene::Tests {
namespace {

constexpr float kEpsilonUVE = 1.0e-4F;
constexpr float kHalfTurnUVE = 1.5707963F; // a quarter turn about Y, the tests' fixed rotation

[[nodiscard]] bool SameRotationUVE(const Math::QuaternionUVE& lhs,
                                   const Math::QuaternionUVE& rhs) {
    // q and -q are the same rotation, so only the magnitude of the dot product is meaningful.
    const float dot = lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
    return std::fabs(std::fabs(dot) - 1.0F) <= kEpsilonUVE;
}

class SpawnPointQueryUVETest : public ::testing::Test {
protected:
    /// A root-level spawn point at `position`, swept so its world pose is real.
    [[nodiscard]] EntityUVE MakeSpawnUVE(const Math::Vector3UVE& position,
                                         const SpawnPoint3DComponentUVE& component) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        TransformComponentUVE transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        entityManager.AddComponentUVE<SpawnPoint3DComponentUVE>(entity, component);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    [[nodiscard]] const SpawnPoint3DComponentUVE& SpawnUVE(const EntityUVE entity) const {
        return entityManager.GetComponentUVE<SpawnPoint3DComponentUVE>(entity);
    }

    /// How many spawn points are still live right now.
    [[nodiscard]] std::size_t LiveSpawnCountUVE() {
        std::size_t live = 0U;
        entityManager.ForEachUVE<SpawnPoint3DComponentUVE>(
            [&live](const EntityUVE, SpawnPoint3DComponentUVE& spawnPoint) {
                if (spawnPoint.enabled) {
                    ++live;
                }
            });
        return live;
    }

    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneGraphUVE sceneGraph;
};

TEST_F(SpawnPointQueryUVETest, AnEmptyQueryFindsEveryLivePointInStableContentOrder) {
    const EntityUVE first = MakeSpawnUVE({1.0F, 0.0F, 0.0F}, SpawnPoint3DComponentUVE{});
    const EntityUVE second = MakeSpawnUVE({2.0F, 0.0F, 0.0F}, SpawnPoint3DComponentUVE{});
    const EntityUVE third = MakeSpawnUVE({3.0F, 0.0F, 0.0F}, SpawnPoint3DComponentUVE{});

    const SpawnPoint3DQueryResultsUVE results =
        QuerySpawnPointsUVE(entityManager, SpawnPoint3DQueryUVE{});

    ASSERT_EQ(results.count, 3U);
    EXPECT_FALSE(results.overflowed);
    EXPECT_TRUE(results.HasAnyUVE());
    EXPECT_EQ(results.FirstUVE(), &results.results[0]);
    EXPECT_EQ(results.results[0].entity, first);
    EXPECT_EQ(results.results[1].entity, second);
    EXPECT_EQ(results.results[2].entity, third);
    for (const SpawnPoint3DQueryResultUVE& result : results.results) {
        if (result.entity == kInvalidEntityUVE) {
            continue; // the unused tail of the bounded array
        }
        EXPECT_EQ(result.tag, "spawn");
        EXPECT_FALSE(result.oneShot);
        EXPECT_FALSE(result.consumed);
    }
}

TEST_F(SpawnPointQueryUVETest, TheTagSelectsWhatTheCallerAskedForAndEmptyTakesEverything) {
    SpawnPoint3DComponentUVE checkpoint;
    checkpoint.spawnTag = "checkpoint";
    SpawnPoint3DComponentUVE levelStart;
    levelStart.spawnTag = "level start";
    const EntityUVE checkpointEntity = MakeSpawnUVE({0.0F, 0.0F, 0.0F}, checkpoint);
    const EntityUVE defaultEntity = MakeSpawnUVE({1.0F, 0.0F, 0.0F}, SpawnPoint3DComponentUVE{});
    static_cast<void>(MakeSpawnUVE({2.0F, 0.0F, 0.0F}, levelStart));

    SpawnPoint3DQueryUVE query;
    query.tag = "checkpoint";
    const SpawnPoint3DQueryResultsUVE checkpoints = QuerySpawnPointsUVE(entityManager, query);
    ASSERT_EQ(checkpoints.count, 1U);
    EXPECT_EQ(checkpoints.results[0].entity, checkpointEntity);
    EXPECT_EQ(checkpoints.results[0].tag, "checkpoint");

    // A tag that matches nothing is an empty answer, not an error and not a fallback.
    query.tag = "team red";
    const SpawnPoint3DQueryResultsUVE none = QuerySpawnPointsUVE(entityManager, query);
    EXPECT_EQ(none.count, 0U);
    EXPECT_FALSE(none.HasAnyUVE());
    EXPECT_EQ(none.FirstUVE(), nullptr);

    // The empty tag is "any", which is what the editor's play entry and a plain respawn ask for.
    query.tag.clear();
    const SpawnPoint3DQueryResultsUVE all = QuerySpawnPointsUVE(entityManager, query);
    ASSERT_EQ(all.count, 3U);
    EXPECT_EQ(all.results[0].entity, checkpointEntity);
    EXPECT_EQ(all.results[1].entity, defaultEntity);
}

TEST_F(SpawnPointQueryUVETest, ResultsComeBackInContentOrderNotCreationOrPoolOrder) {
    std::vector<EntityUVE> created;
    for (int index = 0; index < 3; ++index) {
        created.push_back(
            MakeSpawnUVE({static_cast<float>(index), 0.0F, 0.0F}, SpawnPoint3DComponentUVE{}));
    }

    const SpawnPoint3DQueryResultsUVE results =
        QuerySpawnPointsUVE(entityManager, SpawnPoint3DQueryUVE{});
    ASSERT_EQ(results.count, created.size());
    for (std::size_t index = 0U; index < created.size(); ++index) {
        EXPECT_EQ(results.results[index].entity, created[index]);
        if (index > 0U) {
            // Ascending (index, generation) - the property, independent of pool order.
            EXPECT_LT(results.results[index - 1U].entity.index,
                      results.results[index].entity.index);
        }
    }
}

TEST_F(SpawnPointQueryUVETest, ThePoseIsTheObjectsWorldPoseComposedWithTheAuthoredOffset) {
    Math::QuaternionUVE quarterTurn{};
    ASSERT_TRUE(Math::TryMakeEulerUVE(Math::Vector3UVE{0.0F, kHalfTurnUVE, 0.0F}, quarterTurn));

    SpawnPoint3DComponentUVE component;
    component.localPosition = Math::Vector3UVE{0.0F, 0.5F, 0.0F};
    component.localRotation = quarterTurn;

    const EntityUVE entity = entityManager.CreateEntityUVE();
    TransformComponentUVE transform{};
    transform.localPosition = Math::Vector3UVE{4.0F, 0.0F, 0.0F};
    transform.localRotation = quarterTurn;
    sceneGraph.AttachTransformUVE(entityManager, entity, transform);
    entityManager.AddComponentUVE<SpawnPoint3DComponentUVE>(entity, component);
    sceneGraph.UpdateUVE(entityManager);

    const SpawnPoint3DQueryResultsUVE results =
        QuerySpawnPointsUVE(entityManager, SpawnPoint3DQueryUVE{});
    ASSERT_EQ(results.count, 1U);
    const SpawnPoint3DPoseUVE& pose = results.results[0].pose;
    // Position: the object's world position plus the offset, which is "up" in the point's own
    // frame and therefore still world +Y after a turn about Y.
    EXPECT_NEAR(pose.position.x, 4.0F, kEpsilonUVE);
    EXPECT_NEAR(pose.position.y, 0.5F, kEpsilonUVE);
    EXPECT_NEAR(pose.position.z, 0.0F, kEpsilonUVE);
    // Rotation: the object's world turn composed with the local turn - a half turn about Y.
    EXPECT_TRUE(SameRotationUVE(pose.rotation, Math::MultiplyUVE(quarterTurn, quarterTurn)));
}

TEST_F(SpawnPointQueryUVETest, ADisabledPointIsNotASpawnAndNeitherIsAnInvalidOne) {
    SpawnPoint3DComponentUVE disabled;
    disabled.enabled = false;
    static_cast<void>(MakeSpawnUVE({0.0F, 0.0F, 0.0F}, disabled));

    SpawnPoint3DComponentUVE emptyTag;
    emptyTag.spawnTag.clear();
    static_cast<void>(MakeSpawnUVE({1.0F, 0.0F, 0.0F}, emptyTag));

    SpawnPoint3DComponentUVE notFinite;
    notFinite.localPosition.y = std::numeric_limits<float>::quiet_NaN();
    static_cast<void>(MakeSpawnUVE({2.0F, 0.0F, 0.0F}, notFinite));

    SpawnPoint3DComponentUVE degenerate;
    degenerate.localRotation = Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 0.0F};
    static_cast<void>(MakeSpawnUVE({3.0F, 0.0F, 0.0F}, degenerate));

    const EntityUVE live = MakeSpawnUVE({4.0F, 0.0F, 0.0F}, SpawnPoint3DComponentUVE{});
    const SpawnPoint3DQueryResultsUVE results =
        QuerySpawnPointsUVE(entityManager, SpawnPoint3DQueryUVE{});
    ASSERT_EQ(results.count, 1U);
    EXPECT_EQ(results.results[0].entity, live);
}

TEST_F(SpawnPointQueryUVETest, APointWithoutAWorldTransformIsSkippedUntilTheSweepReachesIt) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<SpawnPoint3DComponentUVE>(entity, SpawnPoint3DComponentUVE{});
    // Authored, but never given a pose or swept: there is no world transform to hand out yet.
    EXPECT_EQ(QuerySpawnPointsUVE(entityManager, SpawnPoint3DQueryUVE{}).count, 0U);

    sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
    sceneGraph.UpdateUVE(entityManager);
    EXPECT_EQ(QuerySpawnPointsUVE(entityManager, SpawnPoint3DQueryUVE{}).count, 1U);
}

TEST_F(SpawnPointQueryUVETest, OneShotConsumptionSpendsOnlyWhatWasHandedOut) {
    SpawnPoint3DComponentUVE checkpoint;
    checkpoint.oneShot = true;
    const EntityUVE spent = MakeSpawnUVE({0.0F, 0.0F, 0.0F}, checkpoint);
    const EntityUVE reusable = MakeSpawnUVE({1.0F, 0.0F, 0.0F}, SpawnPoint3DComponentUVE{});

    SpawnPoint3DQueryUVE query;
    query.consumeOneShot = true;
    const SpawnPoint3DQueryResultsUVE first = QuerySpawnPointsUVE(entityManager, query);
    ASSERT_EQ(first.count, 2U);
    EXPECT_TRUE(first.results[0].consumed);
    EXPECT_EQ(first.results[0].entity, spent);
    EXPECT_FALSE(first.results[1].consumed);
    EXPECT_FALSE(SpawnUVE(spent).enabled);
    EXPECT_TRUE(SpawnUVE(reusable).enabled);

    // The second call cannot spawn from the spent checkpoint again.
    const SpawnPoint3DQueryResultsUVE second = QuerySpawnPointsUVE(entityManager, query);
    ASSERT_EQ(second.count, 1U);
    EXPECT_EQ(second.results[0].entity, reusable);
    EXPECT_FALSE(second.results[0].consumed);

    // The reusable point is consumed by nobody: asking again still finds it.
    const SpawnPoint3DQueryResultsUVE third = QuerySpawnPointsUVE(entityManager, query);
    ASSERT_EQ(third.count, 1U);
    EXPECT_EQ(third.results[0].entity, reusable);
}

TEST_F(SpawnPointQueryUVETest, AQueryThatDoesNotAskToConsumeNeverSpendsAnything) {
    SpawnPoint3DComponentUVE checkpoint;
    checkpoint.oneShot = true;
    const EntityUVE entity = MakeSpawnUVE({0.0F, 0.0F, 0.0F}, checkpoint);

    const SpawnPoint3DQueryResultsUVE results =
        QuerySpawnPointsUVE(entityManager, SpawnPoint3DQueryUVE{});
    ASSERT_EQ(results.count, 1U);
    EXPECT_TRUE(results.results[0].oneShot);
    EXPECT_FALSE(results.results[0].consumed);
    EXPECT_TRUE(SpawnUVE(entity).enabled);
}

TEST_F(SpawnPointQueryUVETest, ConsumeSpawnPointSpendsOnlyALiveOneShotPoint) {
    SpawnPoint3DComponentUVE checkpoint;
    checkpoint.oneShot = true;
    const EntityUVE oneShot = MakeSpawnUVE({0.0F, 0.0F, 0.0F}, checkpoint);
    const EntityUVE reusable = MakeSpawnUVE({1.0F, 0.0F, 0.0F}, SpawnPoint3DComponentUVE{});

    // A reusable point is never spent, and consuming it changes nothing.
    EXPECT_FALSE(ConsumeSpawnPointUVE(entityManager, reusable));
    EXPECT_TRUE(SpawnUVE(reusable).enabled);

    EXPECT_TRUE(ConsumeSpawnPointUVE(entityManager, oneShot));
    EXPECT_FALSE(SpawnUVE(oneShot).enabled);
    // Already spent: the second call does not pretend to spend it again.
    EXPECT_FALSE(ConsumeSpawnPointUVE(entityManager, oneShot));

    // Nothing to spend: an invalid handle and an entity that is not a spawn point both refuse.
    EXPECT_FALSE(ConsumeSpawnPointUVE(entityManager, kInvalidEntityUVE));
    const EntityUVE plain = entityManager.CreateEntityUVE();
    EXPECT_FALSE(ConsumeSpawnPointUVE(entityManager, plain));
}

TEST_F(SpawnPointQueryUVETest, TheResultListIsBoundedAndKeepsTheFirstPointsInContentOrder) {
    // One more than the storage can carry, so the overflow is exactly one point.
    std::vector<EntityUVE> created;
    for (std::size_t index = 0U; index < kMaximumSpawnPointQueryResultsUVE + 1U; ++index) {
        created.push_back(MakeSpawnUVE({static_cast<float>(index), 0.0F, 0.0F},
                                       SpawnPoint3DComponentUVE{}));
    }

    const SpawnPoint3DQueryResultsUVE results =
        QuerySpawnPointsUVE(entityManager, SpawnPoint3DQueryUVE{});
    ASSERT_EQ(results.count, kMaximumSpawnPointQueryResultsUVE);
    EXPECT_TRUE(results.overflowed);
    // The bound keeps the FIRST points in content order, so what fell off is the last one - a
    // caller that pages through results gets a stable, repeatable order.
    for (std::size_t index = 0U; index < results.count; ++index) {
        EXPECT_EQ(results.results[index].entity, created[index]);
    }

    // The same query asked again answers the same way (nothing left behind by the bound).
    const SpawnPoint3DQueryResultsUVE again =
        QuerySpawnPointsUVE(entityManager, SpawnPoint3DQueryUVE{});
    ASSERT_EQ(again.count, kMaximumSpawnPointQueryResultsUVE);
    EXPECT_EQ(again.results[0].entity, created[0]);
    EXPECT_EQ(again.results[again.count - 1U].entity,
              created[kMaximumSpawnPointQueryResultsUVE - 1U]);
}

TEST_F(SpawnPointQueryUVETest, ThePointThatOverflowedIsNotSpentBecauseItWasNeverHandedOut) {
    for (std::size_t index = 0U; index < kMaximumSpawnPointQueryResultsUVE + 1U; ++index) {
        SpawnPoint3DComponentUVE checkpoint;
        checkpoint.oneShot = true;
        static_cast<void>(
            MakeSpawnUVE({static_cast<float>(index), 0.0F, 0.0F}, checkpoint));
    }

    SpawnPoint3DQueryUVE query;
    query.consumeOneShot = true;
    const SpawnPoint3DQueryResultsUVE results = QuerySpawnPointsUVE(entityManager, query);
    ASSERT_EQ(results.count, kMaximumSpawnPointQueryResultsUVE);
    EXPECT_TRUE(results.overflowed);
    for (const SpawnPoint3DQueryResultUVE& result : results.results) {
        EXPECT_TRUE(result.consumed);
    }
    EXPECT_EQ(LiveSpawnCountUVE(), 1U); // the overflowed one; next call can still reach it

    const SpawnPoint3DQueryResultsUVE next = QuerySpawnPointsUVE(entityManager, query);
    ASSERT_EQ(next.count, 1U);
    EXPECT_TRUE(next.results[0].consumed);
    EXPECT_EQ(LiveSpawnCountUVE(), 0U);
}

TEST_F(SpawnPointQueryUVETest, AnEmptyWorldAnswersWithAnEmptyListAndNoNullEntries) {
    const SpawnPoint3DQueryResultsUVE results =
        QuerySpawnPointsUVE(entityManager, SpawnPoint3DQueryUVE{});
    EXPECT_EQ(results.count, 0U);
    EXPECT_FALSE(results.overflowed);
    EXPECT_FALSE(results.HasAnyUVE());
    EXPECT_EQ(results.FirstUVE(), nullptr);
}

} // namespace
} // namespace UVE::Scene::Tests
