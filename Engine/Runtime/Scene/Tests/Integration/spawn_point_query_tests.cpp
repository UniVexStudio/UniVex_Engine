// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "spawn_point_query.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "collider_component.h"
#include "hierarchy_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "quaternion.h"
#include "memory_manager.h"
#include "spawn_point_3d.h"
#include "scene_graph.h"

namespace UVE::Scene::Tests {
namespace {

constexpr float kEpsilon = 1.0e-4F;
constexpr float kHalfTurn = 1.5707963F; // a quarter turn about Y, the tests' fixed rotation

[[nodiscard]] bool SameRotation(const Math::Quaternion& lhs,
                                   const Math::Quaternion& rhs) {
    // q and -q are the same rotation, so only the magnitude of the dot product is meaningful.
    const float dot = lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
    return std::fabs(std::fabs(dot) - 1.0F) <= kEpsilon;
}

class SpawnPointQueryTest : public ::testing::Test {
protected:
    /// A root-level spawn point at `position`, swept so its world pose is real.
    [[nodiscard]] Entity MakeSpawn(const Math::Vector3& position,
                                         const SpawnPoint3DComponent& component) {
        const Entity entity = entityManager.CreateEntity();
        TransformComponent transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        entityManager.AddComponent<SpawnPoint3DComponent>(entity, component);
        sceneGraph.Update(entityManager);
        return entity;
    }

    [[nodiscard]] const SpawnPoint3DComponent& Spawn(const Entity entity) const {
        return entityManager.GetComponent<SpawnPoint3DComponent>(entity);
    }

    /// How many spawn points are still live right now.
    [[nodiscard]] std::size_t LiveSpawnCount() {
        std::size_t live = 0U;
        entityManager.ForEach<SpawnPoint3DComponent>(
            [&live](const Entity, SpawnPoint3DComponent& spawnPoint) {
                if (spawnPoint.enabled) {
                    ++live;
                }
            });
        return live;
    }

    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneGraph sceneGraph;
};

TEST_F(SpawnPointQueryTest, AnEmptyQueryFindsEveryLivePointInStableContentOrder) {
    const Entity first = MakeSpawn({1.0F, 0.0F, 0.0F}, SpawnPoint3DComponent{});
    const Entity second = MakeSpawn({2.0F, 0.0F, 0.0F}, SpawnPoint3DComponent{});
    const Entity third = MakeSpawn({3.0F, 0.0F, 0.0F}, SpawnPoint3DComponent{});

    const SpawnPoint3DQueryResults results =
        QuerySpawnPoints(entityManager, SpawnPoint3DQuery{});

    ASSERT_EQ(results.count, 3U);
    EXPECT_FALSE(results.overflowed);
    EXPECT_TRUE(results.HasAny());
    EXPECT_EQ(results.First(), &results.results[0]);
    EXPECT_EQ(results.results[0].entity, first);
    EXPECT_EQ(results.results[1].entity, second);
    EXPECT_EQ(results.results[2].entity, third);
    for (const SpawnPoint3DQueryResult& result : results.results) {
        if (result.entity == kInvalidEntity) {
            continue; // the unused tail of the bounded array
        }
        EXPECT_EQ(result.tag, "spawn");
        EXPECT_FALSE(result.oneShot);
        EXPECT_FALSE(result.consumed);
    }
}

TEST_F(SpawnPointQueryTest, TheTagSelectsWhatTheCallerAskedForAndEmptyTakesEverything) {
    SpawnPoint3DComponent checkpoint;
    checkpoint.spawnTag = "checkpoint";
    SpawnPoint3DComponent levelStart;
    levelStart.spawnTag = "level start";
    const Entity checkpointEntity = MakeSpawn({0.0F, 0.0F, 0.0F}, checkpoint);
    const Entity defaultEntity = MakeSpawn({1.0F, 0.0F, 0.0F}, SpawnPoint3DComponent{});
    static_cast<void>(MakeSpawn({2.0F, 0.0F, 0.0F}, levelStart));

    SpawnPoint3DQuery query;
    query.tag = "checkpoint";
    const SpawnPoint3DQueryResults checkpoints = QuerySpawnPoints(entityManager, query);
    ASSERT_EQ(checkpoints.count, 1U);
    EXPECT_EQ(checkpoints.results[0].entity, checkpointEntity);
    EXPECT_EQ(checkpoints.results[0].tag, "checkpoint");

    // A tag that matches nothing is an empty answer, not an error and not a fallback.
    query.tag = "team red";
    const SpawnPoint3DQueryResults none = QuerySpawnPoints(entityManager, query);
    EXPECT_EQ(none.count, 0U);
    EXPECT_FALSE(none.HasAny());
    EXPECT_EQ(none.First(), nullptr);

    // The empty tag is "any", which is what the editor's play entry and a plain respawn ask for.
    query.tag.clear();
    const SpawnPoint3DQueryResults all = QuerySpawnPoints(entityManager, query);
    ASSERT_EQ(all.count, 3U);
    EXPECT_EQ(all.results[0].entity, checkpointEntity);
    EXPECT_EQ(all.results[1].entity, defaultEntity);
}

TEST_F(SpawnPointQueryTest, ResultsComeBackInContentOrderNotCreationOrPoolOrder) {
    std::vector<Entity> created;
    for (int index = 0; index < 3; ++index) {
        created.push_back(
            MakeSpawn({static_cast<float>(index), 0.0F, 0.0F}, SpawnPoint3DComponent{}));
    }

    const SpawnPoint3DQueryResults results =
        QuerySpawnPoints(entityManager, SpawnPoint3DQuery{});
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

TEST_F(SpawnPointQueryTest, ThePoseIsTheObjectsWorldPoseComposedWithTheAuthoredOffset) {
    Math::Quaternion quarterTurn{};
    ASSERT_TRUE(Math::TryMakeEuler(Math::Vector3{0.0F, kHalfTurn, 0.0F}, quarterTurn));

    SpawnPoint3DComponent component;
    component.localPosition = Math::Vector3{0.0F, 0.5F, 0.0F};
    component.localRotation = quarterTurn;

    const Entity entity = entityManager.CreateEntity();
    TransformComponent transform{};
    transform.localPosition = Math::Vector3{4.0F, 0.0F, 0.0F};
    transform.localRotation = quarterTurn;
    sceneGraph.AttachTransform(entityManager, entity, transform);
    entityManager.AddComponent<SpawnPoint3DComponent>(entity, component);
    sceneGraph.Update(entityManager);

    const SpawnPoint3DQueryResults results =
        QuerySpawnPoints(entityManager, SpawnPoint3DQuery{});
    ASSERT_EQ(results.count, 1U);
    const SpawnPoint3DPose& pose = results.results[0].pose;
    // Position: the object's world position plus the offset, which is "up" in the point's own
    // frame and therefore still world +Y after a turn about Y.
    EXPECT_NEAR(pose.position.x, 4.0F, kEpsilon);
    EXPECT_NEAR(pose.position.y, 0.5F, kEpsilon);
    EXPECT_NEAR(pose.position.z, 0.0F, kEpsilon);
    // Rotation: the object's world turn composed with the local turn - a half turn about Y.
    EXPECT_TRUE(SameRotation(pose.rotation, Math::Multiply(quarterTurn, quarterTurn)));
}

TEST_F(SpawnPointQueryTest, ADisabledPointIsNotASpawnAndNeitherIsAnInvalidOne) {
    SpawnPoint3DComponent disabled;
    disabled.enabled = false;
    static_cast<void>(MakeSpawn({0.0F, 0.0F, 0.0F}, disabled));

    SpawnPoint3DComponent emptyTag;
    emptyTag.spawnTag.clear();
    static_cast<void>(MakeSpawn({1.0F, 0.0F, 0.0F}, emptyTag));

    SpawnPoint3DComponent notFinite;
    notFinite.localPosition.y = std::numeric_limits<float>::quiet_NaN();
    static_cast<void>(MakeSpawn({2.0F, 0.0F, 0.0F}, notFinite));

    SpawnPoint3DComponent degenerate;
    degenerate.localRotation = Math::Quaternion{0.0F, 0.0F, 0.0F, 0.0F};
    static_cast<void>(MakeSpawn({3.0F, 0.0F, 0.0F}, degenerate));

    const Entity live = MakeSpawn({4.0F, 0.0F, 0.0F}, SpawnPoint3DComponent{});
    const SpawnPoint3DQueryResults results =
        QuerySpawnPoints(entityManager, SpawnPoint3DQuery{});
    ASSERT_EQ(results.count, 1U);
    EXPECT_EQ(results.results[0].entity, live);
}

TEST_F(SpawnPointQueryTest, APointWithoutAWorldTransformIsSkippedUntilTheSweepReachesIt) {
    const Entity entity = entityManager.CreateEntity();
    entityManager.AddComponent<SpawnPoint3DComponent>(entity, SpawnPoint3DComponent{});
    // Authored, but never given a pose or swept: there is no world transform to hand out yet.
    EXPECT_EQ(QuerySpawnPoints(entityManager, SpawnPoint3DQuery{}).count, 0U);

    sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
    sceneGraph.Update(entityManager);
    EXPECT_EQ(QuerySpawnPoints(entityManager, SpawnPoint3DQuery{}).count, 1U);
}

TEST_F(SpawnPointQueryTest, OneShotConsumptionSpendsOnlyWhatWasHandedOut) {
    SpawnPoint3DComponent checkpoint;
    checkpoint.oneShot = true;
    const Entity spent = MakeSpawn({0.0F, 0.0F, 0.0F}, checkpoint);
    const Entity reusable = MakeSpawn({1.0F, 0.0F, 0.0F}, SpawnPoint3DComponent{});

    SpawnPoint3DQuery query;
    query.consumeOneShot = true;
    const SpawnPoint3DQueryResults first = QuerySpawnPoints(entityManager, query);
    ASSERT_EQ(first.count, 2U);
    EXPECT_TRUE(first.results[0].consumed);
    EXPECT_EQ(first.results[0].entity, spent);
    EXPECT_FALSE(first.results[1].consumed);
    EXPECT_FALSE(Spawn(spent).enabled);
    EXPECT_TRUE(Spawn(reusable).enabled);

    // The second call cannot spawn from the spent checkpoint again.
    const SpawnPoint3DQueryResults second = QuerySpawnPoints(entityManager, query);
    ASSERT_EQ(second.count, 1U);
    EXPECT_EQ(second.results[0].entity, reusable);
    EXPECT_FALSE(second.results[0].consumed);

    // The reusable point is consumed by nobody: asking again still finds it.
    const SpawnPoint3DQueryResults third = QuerySpawnPoints(entityManager, query);
    ASSERT_EQ(third.count, 1U);
    EXPECT_EQ(third.results[0].entity, reusable);
}

TEST_F(SpawnPointQueryTest, AQueryThatDoesNotAskToConsumeNeverSpendsAnything) {
    SpawnPoint3DComponent checkpoint;
    checkpoint.oneShot = true;
    const Entity entity = MakeSpawn({0.0F, 0.0F, 0.0F}, checkpoint);

    const SpawnPoint3DQueryResults results =
        QuerySpawnPoints(entityManager, SpawnPoint3DQuery{});
    ASSERT_EQ(results.count, 1U);
    EXPECT_TRUE(results.results[0].oneShot);
    EXPECT_FALSE(results.results[0].consumed);
    EXPECT_TRUE(Spawn(entity).enabled);
}

TEST_F(SpawnPointQueryTest, ConsumeSpawnPointSpendsOnlyALiveOneShotPoint) {
    SpawnPoint3DComponent checkpoint;
    checkpoint.oneShot = true;
    const Entity oneShot = MakeSpawn({0.0F, 0.0F, 0.0F}, checkpoint);
    const Entity reusable = MakeSpawn({1.0F, 0.0F, 0.0F}, SpawnPoint3DComponent{});

    // A reusable point is never spent, and consuming it changes nothing.
    EXPECT_FALSE(ConsumeSpawnPoint(entityManager, reusable));
    EXPECT_TRUE(Spawn(reusable).enabled);

    EXPECT_TRUE(ConsumeSpawnPoint(entityManager, oneShot));
    EXPECT_FALSE(Spawn(oneShot).enabled);
    // Already spent: the second call does not pretend to spend it again.
    EXPECT_FALSE(ConsumeSpawnPoint(entityManager, oneShot));

    // Nothing to spend: an invalid handle and an entity that is not a spawn point both refuse.
    EXPECT_FALSE(ConsumeSpawnPoint(entityManager, kInvalidEntity));
    const Entity plain = entityManager.CreateEntity();
    EXPECT_FALSE(ConsumeSpawnPoint(entityManager, plain));
}

TEST_F(SpawnPointQueryTest, TheResultListIsBoundedAndKeepsTheFirstPointsInContentOrder) {
    // One more than the storage can carry, so the overflow is exactly one point.
    std::vector<Entity> created;
    for (std::size_t index = 0U; index < kMaximumSpawnPointQueryResults + 1U; ++index) {
        created.push_back(MakeSpawn({static_cast<float>(index), 0.0F, 0.0F},
                                       SpawnPoint3DComponent{}));
    }

    const SpawnPoint3DQueryResults results =
        QuerySpawnPoints(entityManager, SpawnPoint3DQuery{});
    ASSERT_EQ(results.count, kMaximumSpawnPointQueryResults);
    EXPECT_TRUE(results.overflowed);
    // The bound keeps the FIRST points in content order, so what fell off is the last one - a
    // caller that pages through results gets a stable, repeatable order.
    for (std::size_t index = 0U; index < results.count; ++index) {
        EXPECT_EQ(results.results[index].entity, created[index]);
    }

    // The same query asked again answers the same way (nothing left behind by the bound).
    const SpawnPoint3DQueryResults again =
        QuerySpawnPoints(entityManager, SpawnPoint3DQuery{});
    ASSERT_EQ(again.count, kMaximumSpawnPointQueryResults);
    EXPECT_EQ(again.results[0].entity, created[0]);
    EXPECT_EQ(again.results[again.count - 1U].entity,
              created[kMaximumSpawnPointQueryResults - 1U]);
}

TEST_F(SpawnPointQueryTest, ThePointThatOverflowedIsNotSpentBecauseItWasNeverHandedOut) {
    for (std::size_t index = 0U; index < kMaximumSpawnPointQueryResults + 1U; ++index) {
        SpawnPoint3DComponent checkpoint;
        checkpoint.oneShot = true;
        static_cast<void>(
            MakeSpawn({static_cast<float>(index), 0.0F, 0.0F}, checkpoint));
    }

    SpawnPoint3DQuery query;
    query.consumeOneShot = true;
    const SpawnPoint3DQueryResults results = QuerySpawnPoints(entityManager, query);
    ASSERT_EQ(results.count, kMaximumSpawnPointQueryResults);
    EXPECT_TRUE(results.overflowed);
    for (const SpawnPoint3DQueryResult& result : results.results) {
        EXPECT_TRUE(result.consumed);
    }
    EXPECT_EQ(LiveSpawnCount(), 1U); // the overflowed one; next call can still reach it

    const SpawnPoint3DQueryResults next = QuerySpawnPoints(entityManager, query);
    ASSERT_EQ(next.count, 1U);
    EXPECT_TRUE(next.results[0].consumed);
    EXPECT_EQ(LiveSpawnCount(), 0U);
}

TEST_F(SpawnPointQueryTest, AnEmptyWorldAnswersWithAnEmptyListAndNoNullEntries) {
    const SpawnPoint3DQueryResults results =
        QuerySpawnPoints(entityManager, SpawnPoint3DQuery{});
    EXPECT_EQ(results.count, 0U);
    EXPECT_FALSE(results.overflowed);
    EXPECT_FALSE(results.HasAny());
    EXPECT_EQ(results.First(), nullptr);
}

} // namespace
} // namespace UVE::Scene::Tests
