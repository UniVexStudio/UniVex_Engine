// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/marker_3d_query_uve.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/marker_3d_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Scene::Tests {
namespace {

constexpr float kEpsilonUVE = 1.0e-4F;
constexpr float kHalfTurnUVE = 1.5707963F;

[[nodiscard]] bool SameRotationUVE(const Math::QuaternionUVE& lhs, const Math::QuaternionUVE& rhs) {
    const float dot = lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
    return std::fabs(std::fabs(dot) - 1.0F) <= kEpsilonUVE;
}

class Marker3DQueryUVETest : public ::testing::Test {
protected:
    [[nodiscard]] EntityUVE MakeMarkerUVE(const Math::Vector3UVE& position,
                                          const Marker3DComponentUVE& component) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        TransformComponentUVE transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        entityManager.AddComponentUVE<Marker3DComponentUVE>(entity, component);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneGraphUVE sceneGraph;
};

TEST_F(Marker3DQueryUVETest, AnEmptyQueryFindsEveryLiveMarkerInStableContentOrder) {
    const EntityUVE first = MakeMarkerUVE({1.0F, 0.0F, 0.0F}, Marker3DComponentUVE{});
    const EntityUVE second = MakeMarkerUVE({2.0F, 0.0F, 0.0F}, Marker3DComponentUVE{});
    const EntityUVE third = MakeMarkerUVE({3.0F, 0.0F, 0.0F}, Marker3DComponentUVE{});

    const Marker3DQueryResultsUVE results = QueryMarkers3DUVE(entityManager, Marker3DQueryUVE{});

    ASSERT_EQ(results.count, 3U);
    EXPECT_FALSE(results.overflowed);
    EXPECT_TRUE(results.HasAnyUVE());
    EXPECT_EQ(results.FirstUVE(), &results.results[0]);
    EXPECT_EQ(results.results[0].entity, first);
    EXPECT_EQ(results.results[1].entity, second);
    EXPECT_EQ(results.results[2].entity, third);
    for (std::size_t index = 0U; index < results.count; ++index) {
        EXPECT_EQ(results.results[index].name, "Marker");
    }
}

TEST_F(Marker3DQueryUVETest, TheNameSelectsWhatTheCallerAskedForAndEmptyTakesEverything) {
    Marker3DComponentUVE boss;
    boss.markerName = "Boss view";
    Marker3DComponentUVE cut;
    cut.markerName = "cut A";
    const EntityUVE bossEntity = MakeMarkerUVE({0.0F, 0.0F, 0.0F}, boss);
    const EntityUVE defaultEntity = MakeMarkerUVE({1.0F, 0.0F, 0.0F}, Marker3DComponentUVE{});
    static_cast<void>(MakeMarkerUVE({2.0F, 0.0F, 0.0F}, cut));

    Marker3DQueryUVE query;
    query.name = "Boss view";
    const Marker3DQueryResultsUVE bosses = QueryMarkers3DUVE(entityManager, query);
    ASSERT_EQ(bosses.count, 1U);
    EXPECT_EQ(bosses.results[0].entity, bossEntity);
    EXPECT_EQ(bosses.results[0].name, "Boss view");

    query.name = "team red";
    const Marker3DQueryResultsUVE none = QueryMarkers3DUVE(entityManager, query);
    EXPECT_EQ(none.count, 0U);
    EXPECT_FALSE(none.HasAnyUVE());
    EXPECT_EQ(none.FirstUVE(), nullptr);

    query.name.clear();
    const Marker3DQueryResultsUVE all = QueryMarkers3DUVE(entityManager, query);
    ASSERT_EQ(all.count, 3U);
    EXPECT_EQ(all.results[0].entity, bossEntity);
    EXPECT_EQ(all.results[1].entity, defaultEntity);
}

TEST_F(Marker3DQueryUVETest, DuplicateNamesComeBackInContentOrderAndFindTakesTheFirst) {
    Marker3DComponentUVE named;
    named.markerName = "cut";
    const EntityUVE first = MakeMarkerUVE({0.0F, 0.0F, 0.0F}, named);
    const EntityUVE second = MakeMarkerUVE({1.0F, 0.0F, 0.0F}, named);

    Marker3DQueryUVE query;
    query.name = "cut";
    const Marker3DQueryResultsUVE results = QueryMarkers3DUVE(entityManager, query);
    ASSERT_EQ(results.count, 2U);
    EXPECT_EQ(results.results[0].entity, first);
    EXPECT_EQ(results.results[1].entity, second);

    const std::optional<Marker3DQueryResultUVE> found = FindMarker3DUVE(entityManager, "cut");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->entity, first);
    EXPECT_EQ(found->name, "cut");
}

TEST_F(Marker3DQueryUVETest, FindNeedsANameAndYieldsNothingWhenNothingMatches) {
    static_cast<void>(MakeMarkerUVE({0.0F, 0.0F, 0.0F}, Marker3DComponentUVE{}));

    EXPECT_FALSE(FindMarker3DUVE(entityManager, "").has_value());
    EXPECT_FALSE(FindMarker3DUVE(entityManager, "missing").has_value());

    const std::optional<Marker3DQueryResultUVE> found = FindMarker3DUVE(entityManager, "Marker");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "Marker");
}

TEST_F(Marker3DQueryUVETest, ThePoseIsTheObjectsWorldPoseComposedWithTheAuthoredOffset) {
    Math::QuaternionUVE quarterTurn{};
    ASSERT_TRUE(Math::TryMakeEulerUVE(Math::Vector3UVE{0.0F, kHalfTurnUVE, 0.0F}, quarterTurn));

    Marker3DComponentUVE component;
    component.localPosition = Math::Vector3UVE{0.0F, 0.5F, 0.0F};
    component.localRotation = quarterTurn;

    const EntityUVE entity = entityManager.CreateEntityUVE();
    TransformComponentUVE transform{};
    transform.localPosition = Math::Vector3UVE{4.0F, 0.0F, 0.0F};
    transform.localRotation = quarterTurn;
    sceneGraph.AttachTransformUVE(entityManager, entity, transform);
    entityManager.AddComponentUVE<Marker3DComponentUVE>(entity, component);
    sceneGraph.UpdateUVE(entityManager);

    const Marker3DQueryResultsUVE results = QueryMarkers3DUVE(entityManager, Marker3DQueryUVE{});
    ASSERT_EQ(results.count, 1U);
    const Marker3DPoseUVE& pose = results.results[0].pose;
    EXPECT_NEAR(pose.position.x, 4.0F, kEpsilonUVE);
    EXPECT_NEAR(pose.position.y, 0.5F, kEpsilonUVE);
    EXPECT_NEAR(pose.position.z, 0.0F, kEpsilonUVE);
    EXPECT_TRUE(SameRotationUVE(pose.rotation, Math::MultiplyUVE(quarterTurn, quarterTurn)));
}

TEST_F(Marker3DQueryUVETest, ADisabledMarkerIsNotAViewpointAndNeitherIsAnInvalidOne) {
    Marker3DComponentUVE disabled;
    disabled.enabled = false;
    static_cast<void>(MakeMarkerUVE({0.0F, 0.0F, 0.0F}, disabled));

    Marker3DComponentUVE emptyName;
    emptyName.markerName.clear();
    static_cast<void>(MakeMarkerUVE({1.0F, 0.0F, 0.0F}, emptyName));

    Marker3DComponentUVE notFinite;
    notFinite.localPosition.y = std::numeric_limits<float>::quiet_NaN();
    static_cast<void>(MakeMarkerUVE({2.0F, 0.0F, 0.0F}, notFinite));

    Marker3DComponentUVE degenerate;
    degenerate.localRotation = Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 0.0F};
    static_cast<void>(MakeMarkerUVE({3.0F, 0.0F, 0.0F}, degenerate));

    const EntityUVE live = MakeMarkerUVE({4.0F, 0.0F, 0.0F}, Marker3DComponentUVE{});
    const Marker3DQueryResultsUVE results = QueryMarkers3DUVE(entityManager, Marker3DQueryUVE{});
    ASSERT_EQ(results.count, 1U);
    EXPECT_EQ(results.results[0].entity, live);
}

TEST_F(Marker3DQueryUVETest, AMarkerWithoutAWorldTransformIsSkippedUntilTheSweepReachesIt) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<Marker3DComponentUVE>(entity, Marker3DComponentUVE{});
    EXPECT_EQ(QueryMarkers3DUVE(entityManager, Marker3DQueryUVE{}).count, 0U);

    sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
    sceneGraph.UpdateUVE(entityManager);
    EXPECT_EQ(QueryMarkers3DUVE(entityManager, Marker3DQueryUVE{}).count, 1U);
}

TEST_F(Marker3DQueryUVETest, TheResultListIsBoundedAndKeepsTheFirstMarkersInContentOrder) {
    std::vector<EntityUVE> created;
    for (std::size_t index = 0U; index < kMaximumMarker3DQueryResultsUVE + 1U; ++index) {
        created.push_back(
            MakeMarkerUVE({static_cast<float>(index), 0.0F, 0.0F}, Marker3DComponentUVE{}));
    }

    const Marker3DQueryResultsUVE results = QueryMarkers3DUVE(entityManager, Marker3DQueryUVE{});
    ASSERT_EQ(results.count, kMaximumMarker3DQueryResultsUVE);
    EXPECT_TRUE(results.overflowed);
    for (std::size_t index = 0U; index < results.count; ++index) {
        EXPECT_EQ(results.results[index].entity, created[index]);
    }

    const Marker3DQueryResultsUVE again = QueryMarkers3DUVE(entityManager, Marker3DQueryUVE{});
    ASSERT_EQ(again.count, kMaximumMarker3DQueryResultsUVE);
    EXPECT_EQ(again.results[0].entity, created[0]);
    EXPECT_EQ(again.results[again.count - 1U].entity, created[kMaximumMarker3DQueryResultsUVE - 1U]);
}

TEST_F(Marker3DQueryUVETest, AnEmptyWorldAnswersWithAnEmptyListAndNoNullEntries) {
    const Marker3DQueryResultsUVE results = QueryMarkers3DUVE(entityManager, Marker3DQueryUVE{});
    EXPECT_EQ(results.count, 0U);
    EXPECT_FALSE(results.overflowed);
    EXPECT_FALSE(results.HasAnyUVE());
    EXPECT_EQ(results.FirstUVE(), nullptr);
    EXPECT_FALSE(FindMarker3DUVE(entityManager, "Marker").has_value());
}

} // namespace
} // namespace UVE::Scene::Tests
