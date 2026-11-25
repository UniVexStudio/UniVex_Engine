// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "world.h"
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include <gtest/gtest.h>
#include "name_component.h"
#include "world_transform_component.h"
#include "entity_lifecycle_events.h"
#include "event_system.h"
#include "heap_allocator.h"
#include "scene_serializer.h"

namespace {
using namespace UVE::World;
static_assert(!std::is_move_constructible_v<Scene>);
class WorldLifecycleTest : public ::testing::Test {
  protected:
    UVE::Memory::HeapAllocator allocator;
    UVE::Events::EventSystem events;
    std::vector<std::string> trace; // must outlive callbacks retained by world
    int primaryCreated = 0, secondaryCreated = 0;
    World world{allocator, events};
    WorldSystemDesc System(std::string name, WorldPhase phase = WorldPhase::BeforeTransforms) {
        WorldSystemDesc system;
        system.name = name;
        system.phase = phase;
        system.start = [this, name](WorldSceneContext &context) {
            trace.push_back(std::string(context.scene.GetName()) + ":start:" + name);
            return true;
        };
        system.tick = [this, name](WorldSceneContext &context, float) {
            trace.push_back(std::string(context.scene.GetName()) + ":tick:" + name);
        };
        system.stop = [this, name](WorldSceneContext &context) {
            EXPECT_EQ(context.world.GetScene(context.scene.GetHandle()), &context.scene);
            trace.push_back(std::string(context.scene.GetName()) + ":stop:" + name);
        };
        return system;
    }
};
TEST_F(WorldLifecycleTest, DependenciesPriorityAndRegistrationGiveStableTickOrder) {
    auto later = System("later");
    later.priority = -10;
    later.after = {"first"};
    ASSERT_EQ(world.RegisterSystem(std::move(later)), WorldResult::Succeeded);
    ASSERT_EQ(world.RegisterSystem(System("first")), WorldResult::Succeeded);
    ASSERT_EQ(world.RegisterSystem(System("last", WorldPhase::AfterTransforms)),
              WorldResult::Succeeded);
    ASSERT_EQ(world.Start(), WorldResult::Succeeded);
    EXPECT_EQ(trace,
              (std::vector<std::string>{"Primary:start:first", "Primary:start:later", "Primary:start:last"}));
    for (int i = 0; i < 3; ++i) {
        trace.clear();
        ASSERT_EQ(world.TryTick(0.25F), WorldResult::Succeeded);
        EXPECT_EQ(trace, (std::vector<std::string>{"Primary:tick:first", "Primary:tick:later",
                                                   "Primary:tick:last"}));
    }
    EXPECT_EQ(world.GetFrameCount(), 3U);
    EXPECT_FLOAT_EQ(world.GetTotalTimeSeconds(), 0.75F);
}
TEST_F(WorldLifecycleTest, GraphValidationRunsBeforeAnyLifecycleCallback) {
    auto a = System("a");
    a.after = {"b"};
    ASSERT_EQ(world.RegisterSystem(std::move(a)), WorldResult::Succeeded);
    EXPECT_EQ(world.Start(), WorldResult::MissingDependency);
    EXPECT_TRUE(trace.empty());
    auto b = System("b");
    b.after = {"a"};
    ASSERT_EQ(world.RegisterSystem(std::move(b)), WorldResult::Succeeded);
    EXPECT_EQ(world.Start(), WorldResult::DependencyCycle);
    EXPECT_TRUE(trace.empty());
    EXPECT_EQ(world.GetState(), WorldState::Created);
}
TEST_F(WorldLifecycleTest, InvalidDuplicateAndLateRegistrationFailWithoutChangingPlan) {
    EXPECT_EQ(world.RegisterSystem({}), WorldResult::InvalidArgument);
    auto invalid = System("bad");
    invalid.phase = static_cast<WorldPhase>(255);
    EXPECT_EQ(world.RegisterSystem(std::move(invalid)), WorldResult::InvalidArgument);
    ASSERT_EQ(world.RegisterSystem(System("valid")), WorldResult::Succeeded);
    EXPECT_EQ(world.RegisterSystem(System("valid")), WorldResult::DuplicateName);
    ASSERT_EQ(world.Start(), WorldResult::Succeeded);
    EXPECT_EQ(world.RegisterSystem(System("late")), WorldResult::InvalidState);
    EXPECT_EQ(world.UnregisterSystem("valid"), WorldResult::InvalidState);
}
TEST_F(WorldLifecycleTest, FailedStartRollsBackIncludingTheFailingSystemAndCanRetry) {
    ASSERT_EQ(world.RegisterSystem(System("a")), WorldResult::Succeeded);
    auto bad = System("b");
    bad.start = [this](auto &) {
        trace.push_back("failed:b");
        return false;
    };
    ASSERT_EQ(world.RegisterSystem(std::move(bad)), WorldResult::Succeeded);
    EXPECT_EQ(world.Start(), WorldResult::InitializationFailed);
    EXPECT_EQ(trace,
              (std::vector<std::string>{"Primary:start:a", "failed:b", "Primary:stop:b", "Primary:stop:a"}));
    ASSERT_EQ(world.UnregisterSystem("b"), WorldResult::Succeeded);
    EXPECT_EQ(world.Start(), WorldResult::Succeeded);
}
TEST_F(WorldLifecycleTest, TransformPropagationIsBetweenWriterAndReaderPhases) {
    auto *primary = world.GetScene(world.GetPrimarySceneHandle());
    auto &entities = primary->GetEntityManager();
    const auto entity = entities.CreateEntity();
    primary->GetSceneGraph().AttachTransform(entities, entity, {});
    WorldSystemDesc writer = System("move");
    writer.tick = [entity](auto &context, float) {
        UVE::Scene::TransformComponent transform;
        transform.localPosition.x = 7.0F;
        context.scene.GetSceneGraph().SetLocalTransform(context.scene.GetEntityManager(), entity,
                                                              transform);
    };
    auto reader = System("read", WorldPhase::AfterTransforms);
    reader.tick = [entity](auto &context, float) {
        EXPECT_FLOAT_EQ(context.scene.GetEntityManager()
                            .template GetComponent<UVE::Scene::WorldTransformComponent>(entity)
                            .worldPosition.x,
                        7.0F);
    };
    ASSERT_EQ(world.RegisterSystem(std::move(reader)), WorldResult::Succeeded);
    ASSERT_EQ(world.RegisterSystem(std::move(writer)), WorldResult::Succeeded);
    EXPECT_EQ(world.TryTick(0.01F), WorldResult::Succeeded);
}
TEST_F(WorldLifecycleTest, BackwardPhaseDependencyIsRejected) {
    auto before = System("before");
    before.after = {"after"};
    ASSERT_EQ(world.RegisterSystem(std::move(before)), WorldResult::Succeeded);
    ASSERT_EQ(world.RegisterSystem(System("after", WorldPhase::AfterTransforms)),
              WorldResult::Succeeded);
    EXPECT_EQ(world.Start(), WorldResult::InvalidArgument);
    EXPECT_TRUE(trace.empty());
}
TEST_F(WorldLifecycleTest, ScenesTickInCreationOrderAndPauseIndependently) {
    SceneHandle first, second;
    ASSERT_EQ(world.CreateScene("first", first), WorldResult::Succeeded);
    ASSERT_EQ(world.CreateScene("second", second), WorldResult::Succeeded);
    ASSERT_EQ(world.RegisterSystem(System("tick")), WorldResult::Succeeded);
    ASSERT_EQ(world.Start(), WorldResult::Succeeded);
    ASSERT_EQ(world.SetSceneEnabled(first, false), WorldResult::Succeeded);
    trace.clear();
    ASSERT_EQ(world.TryTick(0.1F), WorldResult::Succeeded);
    EXPECT_EQ(trace, (std::vector<std::string>{"Primary:tick:tick", "second:tick:tick"}));
    EXPECT_EQ(world.GetScene(first)->GetFrameCount(), 0U);
    EXPECT_EQ(world.GetScene(second)->GetFrameCount(), 1U);
    ASSERT_EQ(world.SetSceneEnabled(first, true), WorldResult::Succeeded);
    ASSERT_EQ(world.TryTick(0.1F), WorldResult::Succeeded);
    EXPECT_EQ(world.GetScene(first)->GetFrameCount(), 1U);
}
TEST_F(WorldLifecycleTest, SecondaryEntityEventsCannotAliasPrimaryEntityIds) {
    events.Subscribe<UVE::Scene::EntityCreatedEvent>([this](const auto &) { ++primaryCreated; });
    const auto a = world.GetEntityManager().CreateEntity();
    SceneHandle scene;
    ASSERT_EQ(world.CreateScene("other", scene), WorldResult::Succeeded);
    auto *other = world.GetScene(scene);
    other->GetEventSystem().Subscribe<UVE::Scene::EntityCreatedEvent>(
        [this](const auto &) { ++secondaryCreated; });
    const auto b = other->GetEntityManager().CreateEntity();
    EXPECT_EQ(a, b); // equal values, different owning ECSes and event scopes
    EXPECT_EQ(primaryCreated, 1);
    EXPECT_EQ(secondaryCreated, 1);
}
TEST_F(WorldLifecycleTest, UnloadStopsInReverseOrderRejectsStaleHandleAndPreservesOtherScenes) {
    ASSERT_EQ(world.RegisterSystem(System("a")), WorldResult::Succeeded);
    ASSERT_EQ(world.RegisterSystem(System("b")), WorldResult::Succeeded);
    ASSERT_EQ(world.Start(), WorldResult::Succeeded);
    SceneHandle scene;
    ASSERT_EQ(world.CreateScene("level", scene,
                                   [](auto &value) {
                                       static_cast<void>(value.GetEntityManager().CreateEntity());
                                       return true;
                                   }),
              WorldResult::Succeeded);
    trace.clear();
    ASSERT_EQ(world.UnloadScene(scene), WorldResult::Succeeded);
    EXPECT_EQ(trace, (std::vector<std::string>{"level:stop:b", "level:stop:a"}));
    EXPECT_EQ(world.GetScene(scene), nullptr);
    EXPECT_EQ(world.UnloadScene(scene), WorldResult::UnknownScene);
    EXPECT_EQ(world.GetSceneCount(), 1U);
    SceneHandle next;
    ASSERT_EQ(world.CreateScene("next", next), WorldResult::Succeeded);
    EXPECT_EQ(scene.index, next.index);
    EXPECT_NE(scene.generation, next.generation);
    EXPECT_EQ(world.GetScene(scene), nullptr);
}
TEST_F(WorldLifecycleTest, FailedPopulationAndDuplicateNamesPreserveOutputAndExistingScene) {
    auto out = world.GetPrimarySceneHandle();
    const auto before = out;
    EXPECT_EQ(world.CreateScene("Primary", out), WorldResult::DuplicateName);
    EXPECT_EQ(world.CreateScene("bad", out,
                                   [](auto &scene) {
                                       static_cast<void>(scene.GetEntityManager().CreateEntity());
                                       return false;
                                   }),
              WorldResult::InitializationFailed);
    EXPECT_EQ(out, before);
    EXPECT_EQ(world.GetSceneCount(), 1U);
    EXPECT_EQ(world.GetEntityManager().GetEntityCount(), 0U);
}
TEST_F(WorldLifecycleTest, ExistingSceneSnapshotLoadsWithoutAFormatMigration) {
    auto *primary = world.GetScene(world.GetPrimarySceneHandle());
    auto &entities = primary->GetEntityManager();
    const auto entity = entities.CreateEntity();
    primary->GetSceneGraph().AttachTransform(entities, entity, {{3.0F, 0.0F, 0.0F}, {}, {1, 1, 1}});
    UVE::Scene::SceneSerializer serializer;
    auto snapshot = serializer.Capture(entities, {entity}, UVE::Scene::SceneAssetType::Scene);
    ASSERT_TRUE(snapshot.has_value());
    SceneHandle restored;
    ASSERT_EQ(world.CreateScene(
                  "restored", restored,
                  [&](auto &scene) {
                      return !serializer.Restore(scene.GetEntityManager(), *snapshot).empty();
                  }),
              WorldResult::Succeeded);
    ASSERT_EQ(world.TryTick(0.0F), WorldResult::Succeeded);
    auto *scene = world.GetScene(restored);
    ASSERT_EQ(scene->GetEntityManager().GetEntityCount(), 1U);
    scene->GetEntityManager().ForEach<UVE::Scene::WorldTransformComponent>(
        [](auto, const auto &pose) { EXPECT_FLOAT_EQ(pose.worldPosition.x, 3.0F); });
}
TEST_F(WorldLifecycleTest, ReentrantWorldMutationIsRejectedButEntityUpdatesRemainPossible) {
    auto system = System("guard");
    system.tick = [this](auto &, float) {
        SceneHandle out;
        EXPECT_EQ(world.CreateScene("nested", out), WorldResult::Busy);
        EXPECT_FALSE(out.IsValid());
        EXPECT_EQ(world.TryTick(0.0F), WorldResult::Busy);
        EXPECT_EQ(world.Shutdown(), WorldResult::Busy);
        EXPECT_EQ(world.RegisterSystem(System("nested")), WorldResult::Busy);
    };
    ASSERT_EQ(world.RegisterSystem(std::move(system)), WorldResult::Succeeded);
    EXPECT_EQ(world.TryTick(0.1F), WorldResult::Succeeded);
    EXPECT_EQ(world.GetSceneCount(), 1U);
}
TEST_F(WorldLifecycleTest, TickExceptionFailsClosedAndShutdownStillStopsSystems) {
    auto bad = System("bad");
    bad.tick = [](auto &, float) { throw std::runtime_error("tick failed"); };
    ASSERT_EQ(world.RegisterSystem(std::move(bad)), WorldResult::Succeeded);
    EXPECT_EQ(world.TryTick(0.1F), WorldResult::SystemFailed);
    EXPECT_EQ(world.GetState(), WorldState::Failed);
    EXPECT_EQ(world.GetFrameCount(), 0U);
    EXPECT_EQ(world.TryTick(0.1F), WorldResult::InvalidState);
    EXPECT_EQ(world.Shutdown(), WorldResult::Succeeded);
    EXPECT_EQ(trace.back(), "Primary:stop:bad");
}
TEST_F(WorldLifecycleTest, InvalidTimeIsRejectedBeforeStartingOrMutatingCounters) {
    EXPECT_EQ(world.TryTick(-1.0F), WorldResult::InvalidArgument);
    EXPECT_EQ(world.TryTick(std::numeric_limits<float>::quiet_NaN()), WorldResult::InvalidArgument);
    EXPECT_EQ(world.TryTick(std::numeric_limits<float>::infinity()), WorldResult::InvalidArgument);
    EXPECT_EQ(world.GetState(), WorldState::Created);
    EXPECT_EQ(world.GetFrameCount(), 0U);
    EXPECT_FLOAT_EQ(world.GetTotalTimeSeconds(), 0.0F);
}
TEST_F(WorldLifecycleTest, ForeignSceneIsRejectedAndShutdownIsPermanentIdempotentAndClearsPrimary) {
    World other{allocator, events};
    EXPECT_EQ(world.GetScene(other.GetPrimarySceneHandle()), nullptr);
    EXPECT_EQ(world.UnloadScene(other.GetPrimarySceneHandle()), WorldResult::UnknownScene);
    const auto primary = world.GetPrimarySceneHandle();
    EXPECT_EQ(world.UnloadScene(primary), WorldResult::InvalidState);
    static_cast<void>(world.GetEntityManager().CreateEntity());
    ASSERT_EQ(world.Shutdown(), WorldResult::Succeeded);
    EXPECT_EQ(world.Shutdown(), WorldResult::Succeeded);
    EXPECT_EQ(world.GetSceneCount(), 0U);
    EXPECT_EQ(world.GetEntityManager().GetEntityCount(), 0U);
    EXPECT_EQ(world.GetScene(primary), nullptr);
    EXPECT_EQ(world.TryTick(0), WorldResult::InvalidState);
    SceneHandle handle;
    EXPECT_EQ(world.CreateScene("closed", handle), WorldResult::InvalidState);
}
} // namespace
