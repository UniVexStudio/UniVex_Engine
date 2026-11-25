// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "marker_3d_query.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "quaternion.h"
#include "memory_manager.h"
#include "marker_3d.h"
#include "scene_graph.h"

namespace UVE::Scene::Tests {
namespace {

constexpr float kEpsilon = 1.0e-4F;
constexpr float kHalfTurn = 1.5707963F;

[[nodiscard]] bool SameRotation(const Math::Quaternion& lhs, const Math::Quaternion& rhs) {
    const float dot = lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
    return std::fabs(std::fabs(dot) - 1.0F) <= kEpsilon;
}

class Marker3DQueryTest : public ::testing::Test {
protected:
    [[nodiscard]] Entity MakeMarker(const Math::Vector3& position,
                                          const Marker3DComponent& component) {
        const Entity entity = entityManager.CreateEntity();
        TransformComponent transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        entityManager.AddComponent<Marker3DComponent>(entity, component);
        sceneGraph.Update(entityManager);
        return entity;
    }

    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneGraph sceneGraph;
};

TEST_F(Marker3DQueryTest, AnEmptyQueryFindsEveryLiveMarkerInStableContentOrder) {
    const Entity first = MakeMarker({1.0F, 0.0F, 0.0F}, Marker3DComponent{});
    const Entity second = MakeMarker({2.0F, 0.0F, 0.0F}, Marker3DComponent{});
    const Entity third = MakeMarker({3.0F, 0.0F, 0.0F}, Marker3DComponent{});

    const Marker3DQueryResults results = QueryMarkers3D(entityManager, Marker3DQuery{});

    ASSERT_EQ(results.count, 3U);
    EXPECT_FALSE(results.overflowed);
    EXPECT_TRUE(results.HasAny());
    EXPECT_EQ(results.First(), &results.results[0]);
    EXPECT_EQ(results.results[0].entity, first);
    EXPECT_EQ(results.results[1].entity, second);
    EXPECT_EQ(results.results[2].entity, third);
    for (std::size_t index = 0U; index < results.count; ++index) {
        EXPECT_EQ(results.results[index].name, "Marker");
    }
}

TEST_F(Marker3DQueryTest, TheNameSelectsWhatTheCallerAskedForAndEmptyTakesEverything) {
    Marker3DComponent boss;
    boss.markerName = "Boss view";
    Marker3DComponent cut;
    cut.markerName = "cut A";
    const Entity bossEntity = MakeMarker({0.0F, 0.0F, 0.0F}, boss);
    const Entity defaultEntity = MakeMarker({1.0F, 0.0F, 0.0F}, Marker3DComponent{});
    static_cast<void>(MakeMarker({2.0F, 0.0F, 0.0F}, cut));

    Marker3DQuery query;
    query.name = "Boss view";
    const Marker3DQueryResults bosses = QueryMarkers3D(entityManager, query);
    ASSERT_EQ(bosses.count, 1U);
    EXPECT_EQ(bosses.results[0].entity, bossEntity);
    EXPECT_EQ(bosses.results[0].name, "Boss view");

    query.name = "team red";
    const Marker3DQueryResults none = QueryMarkers3D(entityManager, query);
    EXPECT_EQ(none.count, 0U);
    EXPECT_FALSE(none.HasAny());
    EXPECT_EQ(none.First(), nullptr);

    query.name.clear();
    const Marker3DQueryResults all = QueryMarkers3D(entityManager, query);
    ASSERT_EQ(all.count, 3U);
    EXPECT_EQ(all.results[0].entity, bossEntity);
    EXPECT_EQ(all.results[1].entity, defaultEntity);
}

TEST_F(Marker3DQueryTest, DuplicateNamesComeBackInContentOrderAndFindTakesTheFirst) {
    Marker3DComponent named;
    named.markerName = "cut";
    const Entity first = MakeMarker({0.0F, 0.0F, 0.0F}, named);
    const Entity second = MakeMarker({1.0F, 0.0F, 0.0F}, named);

    Marker3DQuery query;
    query.name = "cut";
    const Marker3DQueryResults results = QueryMarkers3D(entityManager, query);
    ASSERT_EQ(results.count, 2U);
    EXPECT_EQ(results.results[0].entity, first);
    EXPECT_EQ(results.results[1].entity, second);

    const std::optional<Marker3DQueryResult> found = FindMarker3D(entityManager, "cut");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->entity, first);
    EXPECT_EQ(found->name, "cut");
}

TEST_F(Marker3DQueryTest, FindNeedsANameAndYieldsNothingWhenNothingMatches) {
    static_cast<void>(MakeMarker({0.0F, 0.0F, 0.0F}, Marker3DComponent{}));

    EXPECT_FALSE(FindMarker3D(entityManager, "").has_value());
    EXPECT_FALSE(FindMarker3D(entityManager, "missing").has_value());

    const std::optional<Marker3DQueryResult> found = FindMarker3D(entityManager, "Marker");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "Marker");
}

TEST_F(Marker3DQueryTest, ThePoseIsTheObjectsWorldPoseComposedWithTheAuthoredOffset) {
    Math::Quaternion quarterTurn{};
    ASSERT_TRUE(Math::TryMakeEuler(Math::Vector3{0.0F, kHalfTurn, 0.0F}, quarterTurn));

    Marker3DComponent component;
    component.localPosition = Math::Vector3{0.0F, 0.5F, 0.0F};
    component.localRotation = quarterTurn;

    const Entity entity = entityManager.CreateEntity();
    TransformComponent transform{};
    transform.localPosition = Math::Vector3{4.0F, 0.0F, 0.0F};
    transform.localRotation = quarterTurn;
    sceneGraph.AttachTransform(entityManager, entity, transform);
    entityManager.AddComponent<Marker3DComponent>(entity, component);
    sceneGraph.Update(entityManager);

    const Marker3DQueryResults results = QueryMarkers3D(entityManager, Marker3DQuery{});
    ASSERT_EQ(results.count, 1U);
    const Marker3DPose& pose = results.results[0].pose;
    EXPECT_NEAR(pose.position.x, 4.0F, kEpsilon);
    EXPECT_NEAR(pose.position.y, 0.5F, kEpsilon);
    EXPECT_NEAR(pose.position.z, 0.0F, kEpsilon);
    EXPECT_TRUE(SameRotation(pose.rotation, Math::Multiply(quarterTurn, quarterTurn)));
}

TEST_F(Marker3DQueryTest, ADisabledMarkerIsNotAViewpointAndNeitherIsAnInvalidOne) {
    Marker3DComponent disabled;
    disabled.enabled = false;
    static_cast<void>(MakeMarker({0.0F, 0.0F, 0.0F}, disabled));

    Marker3DComponent emptyName;
    emptyName.markerName.clear();
    static_cast<void>(MakeMarker({1.0F, 0.0F, 0.0F}, emptyName));

    Marker3DComponent notFinite;
    notFinite.localPosition.y = std::numeric_limits<float>::quiet_NaN();
    static_cast<void>(MakeMarker({2.0F, 0.0F, 0.0F}, notFinite));

    Marker3DComponent degenerate;
    degenerate.localRotation = Math::Quaternion{0.0F, 0.0F, 0.0F, 0.0F};
    static_cast<void>(MakeMarker({3.0F, 0.0F, 0.0F}, degenerate));

    const Entity live = MakeMarker({4.0F, 0.0F, 0.0F}, Marker3DComponent{});
    const Marker3DQueryResults results = QueryMarkers3D(entityManager, Marker3DQuery{});
    ASSERT_EQ(results.count, 1U);
    EXPECT_EQ(results.results[0].entity, live);
}

TEST_F(Marker3DQueryTest, AMarkerWithoutAWorldTransformIsSkippedUntilTheSweepReachesIt) {
    const Entity entity = entityManager.CreateEntity();
    entityManager.AddComponent<Marker3DComponent>(entity, Marker3DComponent{});
    EXPECT_EQ(QueryMarkers3D(entityManager, Marker3DQuery{}).count, 0U);

    sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
    sceneGraph.Update(entityManager);
    EXPECT_EQ(QueryMarkers3D(entityManager, Marker3DQuery{}).count, 1U);
}

TEST_F(Marker3DQueryTest, TheResultListIsBoundedAndKeepsTheFirstMarkersInContentOrder) {
    std::vector<Entity> created;
    for (std::size_t index = 0U; index < kMaximumMarker3DQueryResults + 1U; ++index) {
        created.push_back(
            MakeMarker({static_cast<float>(index), 0.0F, 0.0F}, Marker3DComponent{}));
    }

    const Marker3DQueryResults results = QueryMarkers3D(entityManager, Marker3DQuery{});
    ASSERT_EQ(results.count, kMaximumMarker3DQueryResults);
    EXPECT_TRUE(results.overflowed);
    for (std::size_t index = 0U; index < results.count; ++index) {
        EXPECT_EQ(results.results[index].entity, created[index]);
    }

    const Marker3DQueryResults again = QueryMarkers3D(entityManager, Marker3DQuery{});
    ASSERT_EQ(again.count, kMaximumMarker3DQueryResults);
    EXPECT_EQ(again.results[0].entity, created[0]);
    EXPECT_EQ(again.results[again.count - 1U].entity, created[kMaximumMarker3DQueryResults - 1U]);
}

TEST_F(Marker3DQueryTest, AnEmptyWorldAnswersWithAnEmptyListAndNoNullEntries) {
    const Marker3DQueryResults results = QueryMarkers3D(entityManager, Marker3DQuery{});
    EXPECT_EQ(results.count, 0U);
    EXPECT_FALSE(results.overflowed);
    EXPECT_FALSE(results.HasAny());
    EXPECT_EQ(results.First(), nullptr);
    EXPECT_FALSE(FindMarker3D(entityManager, "Marker").has_value());
}

} // namespace
} // namespace UVE::Scene::Tests
