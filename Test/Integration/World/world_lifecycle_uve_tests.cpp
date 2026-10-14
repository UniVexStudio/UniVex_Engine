// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/world/world_uve.h"
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include <gtest/gtest.h>
#include "uve/component/name_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_lifecycle_events_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/heap_allocator_uve.h"
#include "uve/scene/scene_serializer_uve.h"

namespace {
using namespace UVE::World;
static_assert(!std::is_move_constructible_v<SceneUVE>);
class WorldLifecycleUVETest : public ::testing::Test {
  protected:
    UVE::Memory::HeapAllocatorUVE allocator;
    UVE::Events::EventSystemUVE events;
    std::vector<std::string> trace; // must outlive callbacks retained by world
    int primaryCreated = 0, secondaryCreated = 0;
    WorldUVE world{allocator, events};
    WorldSystemDescUVE SystemUVE(std::string name, WorldPhaseUVE phase = WorldPhaseUVE::BeforeTransforms) {
        WorldSystemDescUVE system;
        system.name = name;
        system.phase = phase;
        system.start = [this, name](WorldSceneContextUVE &context) {
            trace.push_back(std::string(context.scene.GetNameUVE()) + ":start:" + name);
            return true;
        };
        system.tick = [this, name](WorldSceneContextUVE &context, float) {
            trace.push_back(std::string(context.scene.GetNameUVE()) + ":tick:" + name);
        };
        system.stop = [this, name](WorldSceneContextUVE &context) {
            EXPECT_EQ(context.world.GetSceneUVE(context.scene.GetHandleUVE()), &context.scene);
            trace.push_back(std::string(context.scene.GetNameUVE()) + ":stop:" + name);
        };
        return system;
    }
};
TEST_F(WorldLifecycleUVETest, DependenciesPriorityAndRegistrationGiveStableTickOrder) {
    auto later = SystemUVE("later");
    later.priority = -10;
    later.after = {"first"};
    ASSERT_EQ(world.RegisterSystemUVE(std::move(later)), WorldResultUVE::Succeeded);
    ASSERT_EQ(world.RegisterSystemUVE(SystemUVE("first")), WorldResultUVE::Succeeded);
    ASSERT_EQ(world.RegisterSystemUVE(SystemUVE("last", WorldPhaseUVE::AfterTransforms)),
              WorldResultUVE::Succeeded);
    ASSERT_EQ(world.StartUVE(), WorldResultUVE::Succeeded);
    EXPECT_EQ(trace,
              (std::vector<std::string>{"Primary:start:first", "Primary:start:later", "Primary:start:last"}));
    for (int i = 0; i < 3; ++i) {
        trace.clear();
        ASSERT_EQ(world.TryTickUVE(0.25F), WorldResultUVE::Succeeded);
        EXPECT_EQ(trace, (std::vector<std::string>{"Primary:tick:first", "Primary:tick:later",
                                                   "Primary:tick:last"}));
    }
    EXPECT_EQ(world.GetFrameCountUVE(), 3U);
    EXPECT_FLOAT_EQ(world.GetTotalTimeSecondsUVE(), 0.75F);
}
TEST_F(WorldLifecycleUVETest, GraphValidationRunsBeforeAnyLifecycleCallback) {
    auto a = SystemUVE("a");
    a.after = {"b"};
    ASSERT_EQ(world.RegisterSystemUVE(std::move(a)), WorldResultUVE::Succeeded);
    EXPECT_EQ(world.StartUVE(), WorldResultUVE::MissingDependency);
    EXPECT_TRUE(trace.empty());
    auto b = SystemUVE("b");
    b.after = {"a"};
    ASSERT_EQ(world.RegisterSystemUVE(std::move(b)), WorldResultUVE::Succeeded);
    EXPECT_EQ(world.StartUVE(), WorldResultUVE::DependencyCycle);
    EXPECT_TRUE(trace.empty());
    EXPECT_EQ(world.GetStateUVE(), WorldStateUVE::Created);
}
TEST_F(WorldLifecycleUVETest, InvalidDuplicateAndLateRegistrationFailWithoutChangingPlan) {
    EXPECT_EQ(world.RegisterSystemUVE({}), WorldResultUVE::InvalidArgument);
    auto invalid = SystemUVE("bad");
    invalid.phase = static_cast<WorldPhaseUVE>(255);
    EXPECT_EQ(world.RegisterSystemUVE(std::move(invalid)), WorldResultUVE::InvalidArgument);
    ASSERT_EQ(world.RegisterSystemUVE(SystemUVE("valid")), WorldResultUVE::Succeeded);
    EXPECT_EQ(world.RegisterSystemUVE(SystemUVE("valid")), WorldResultUVE::DuplicateName);
    ASSERT_EQ(world.StartUVE(), WorldResultUVE::Succeeded);
    EXPECT_EQ(world.RegisterSystemUVE(SystemUVE("late")), WorldResultUVE::InvalidState);
    EXPECT_EQ(world.UnregisterSystemUVE("valid"), WorldResultUVE::InvalidState);
}
TEST_F(WorldLifecycleUVETest, FailedStartRollsBackIncludingTheFailingSystemAndCanRetry) {
    ASSERT_EQ(world.RegisterSystemUVE(SystemUVE("a")), WorldResultUVE::Succeeded);
    auto bad = SystemUVE("b");
    bad.start = [this](auto &) {
        trace.push_back("failed:b");
        return false;
    };
    ASSERT_EQ(world.RegisterSystemUVE(std::move(bad)), WorldResultUVE::Succeeded);
    EXPECT_EQ(world.StartUVE(), WorldResultUVE::InitializationFailed);
    EXPECT_EQ(trace,
              (std::vector<std::string>{"Primary:start:a", "failed:b", "Primary:stop:b", "Primary:stop:a"}));
    ASSERT_EQ(world.UnregisterSystemUVE("b"), WorldResultUVE::Succeeded);
    EXPECT_EQ(world.StartUVE(), WorldResultUVE::Succeeded);
}
TEST_F(WorldLifecycleUVETest, TransformPropagationIsBetweenWriterAndReaderPhases) {
    auto *primary = world.GetSceneUVE(world.GetPrimarySceneHandleUVE());
    auto &entities = primary->GetEntityManagerUVE();
    const auto entity = entities.CreateEntityUVE();
    primary->GetSceneGraphUVE().AttachTransformUVE(entities, entity, {});
    WorldSystemDescUVE writer = SystemUVE("move");
    writer.tick = [entity](auto &context, float) {
        UVE::Scene::TransformComponentUVE transform;
        transform.localPosition.x = 7.0F;
        context.scene.GetSceneGraphUVE().SetLocalTransformUVE(context.scene.GetEntityManagerUVE(), entity,
                                                              transform);
    };
    auto reader = SystemUVE("read", WorldPhaseUVE::AfterTransforms);
    reader.tick = [entity](auto &context, float) {
        EXPECT_FLOAT_EQ(context.scene.GetEntityManagerUVE()
                            .template GetComponentUVE<UVE::Scene::WorldTransformComponentUVE>(entity)
                            .worldPosition.x,
                        7.0F);
    };
    ASSERT_EQ(world.RegisterSystemUVE(std::move(reader)), WorldResultUVE::Succeeded);
    ASSERT_EQ(world.RegisterSystemUVE(std::move(writer)), WorldResultUVE::Succeeded);
    EXPECT_EQ(world.TryTickUVE(0.01F), WorldResultUVE::Succeeded);
}
TEST_F(WorldLifecycleUVETest, BackwardPhaseDependencyIsRejected) {
    auto before = SystemUVE("before");
    before.after = {"after"};
    ASSERT_EQ(world.RegisterSystemUVE(std::move(before)), WorldResultUVE::Succeeded);
    ASSERT_EQ(world.RegisterSystemUVE(SystemUVE("after", WorldPhaseUVE::AfterTransforms)),
              WorldResultUVE::Succeeded);
    EXPECT_EQ(world.StartUVE(), WorldResultUVE::InvalidArgument);
    EXPECT_TRUE(trace.empty());
}
TEST_F(WorldLifecycleUVETest, ScenesTickInCreationOrderAndPauseIndependently) {
    SceneHandleUVE first, second;
    ASSERT_EQ(world.CreateSceneUVE("first", first), WorldResultUVE::Succeeded);
    ASSERT_EQ(world.CreateSceneUVE("second", second), WorldResultUVE::Succeeded);
    ASSERT_EQ(world.RegisterSystemUVE(SystemUVE("tick")), WorldResultUVE::Succeeded);
    ASSERT_EQ(world.StartUVE(), WorldResultUVE::Succeeded);
    ASSERT_EQ(world.SetSceneEnabledUVE(first, false), WorldResultUVE::Succeeded);
    trace.clear();
    ASSERT_EQ(world.TryTickUVE(0.1F), WorldResultUVE::Succeeded);
    EXPECT_EQ(trace, (std::vector<std::string>{"Primary:tick:tick", "second:tick:tick"}));
    EXPECT_EQ(world.GetSceneUVE(first)->GetFrameCountUVE(), 0U);
    EXPECT_EQ(world.GetSceneUVE(second)->GetFrameCountUVE(), 1U);
    ASSERT_EQ(world.SetSceneEnabledUVE(first, true), WorldResultUVE::Succeeded);
    ASSERT_EQ(world.TryTickUVE(0.1F), WorldResultUVE::Succeeded);
    EXPECT_EQ(world.GetSceneUVE(first)->GetFrameCountUVE(), 1U);
}
TEST_F(WorldLifecycleUVETest, SecondaryEntityEventsCannotAliasPrimaryEntityIds) {
    events.Subscribe<UVE::Scene::EntityCreatedEventUVE>([this](const auto &) { ++primaryCreated; });
    const auto a = world.GetEntityManagerUVE().CreateEntityUVE();
    SceneHandleUVE scene;
    ASSERT_EQ(world.CreateSceneUVE("other", scene), WorldResultUVE::Succeeded);
    auto *other = world.GetSceneUVE(scene);
    other->GetEventSystemUVE().Subscribe<UVE::Scene::EntityCreatedEventUVE>(
        [this](const auto &) { ++secondaryCreated; });
    const auto b = other->GetEntityManagerUVE().CreateEntityUVE();
    EXPECT_EQ(a, b); // equal values, different owning ECSes and event scopes
    EXPECT_EQ(primaryCreated, 1);
    EXPECT_EQ(secondaryCreated, 1);
}
TEST_F(WorldLifecycleUVETest, UnloadStopsInReverseOrderRejectsStaleHandleAndPreservesOtherScenes) {
    ASSERT_EQ(world.RegisterSystemUVE(SystemUVE("a")), WorldResultUVE::Succeeded);
    ASSERT_EQ(world.RegisterSystemUVE(SystemUVE("b")), WorldResultUVE::Succeeded);
    ASSERT_EQ(world.StartUVE(), WorldResultUVE::Succeeded);
    SceneHandleUVE scene;
    ASSERT_EQ(world.CreateSceneUVE("level", scene,
                                   [](auto &value) {
                                       static_cast<void>(value.GetEntityManagerUVE().CreateEntityUVE());
                                       return true;
                                   }),
              WorldResultUVE::Succeeded);
    trace.clear();
    ASSERT_EQ(world.UnloadSceneUVE(scene), WorldResultUVE::Succeeded);
    EXPECT_EQ(trace, (std::vector<std::string>{"level:stop:b", "level:stop:a"}));
    EXPECT_EQ(world.GetSceneUVE(scene), nullptr);
    EXPECT_EQ(world.UnloadSceneUVE(scene), WorldResultUVE::UnknownScene);
    EXPECT_EQ(world.GetSceneCountUVE(), 1U);
    SceneHandleUVE next;
    ASSERT_EQ(world.CreateSceneUVE("next", next), WorldResultUVE::Succeeded);
    EXPECT_EQ(scene.index, next.index);
    EXPECT_NE(scene.generation, next.generation);
    EXPECT_EQ(world.GetSceneUVE(scene), nullptr);
}
TEST_F(WorldLifecycleUVETest, FailedPopulationAndDuplicateNamesPreserveOutputAndExistingScene) {
    auto out = world.GetPrimarySceneHandleUVE();
    const auto before = out;
    EXPECT_EQ(world.CreateSceneUVE("Primary", out), WorldResultUVE::DuplicateName);
    EXPECT_EQ(world.CreateSceneUVE("bad", out,
                                   [](auto &scene) {
                                       static_cast<void>(scene.GetEntityManagerUVE().CreateEntityUVE());
                                       return false;
                                   }),
              WorldResultUVE::InitializationFailed);
    EXPECT_EQ(out, before);
    EXPECT_EQ(world.GetSceneCountUVE(), 1U);
    EXPECT_EQ(world.GetEntityManagerUVE().GetEntityCountUVE(), 0U);
}
TEST_F(WorldLifecycleUVETest, ExistingSceneSnapshotLoadsWithoutAFormatMigration) {
    auto *primary = world.GetSceneUVE(world.GetPrimarySceneHandleUVE());
    auto &entities = primary->GetEntityManagerUVE();
    const auto entity = entities.CreateEntityUVE();
    primary->GetSceneGraphUVE().AttachTransformUVE(entities, entity, {{3.0F, 0.0F, 0.0F}, {}, {1, 1, 1}});
    UVE::Scene::SceneSerializerUVE serializer;
    auto snapshot = serializer.CaptureUVE(entities, {entity}, UVE::Scene::SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    SceneHandleUVE restored;
    ASSERT_EQ(world.CreateSceneUVE(
                  "restored", restored,
                  [&](auto &scene) {
                      return !serializer.RestoreUVE(scene.GetEntityManagerUVE(), *snapshot).empty();
                  }),
              WorldResultUVE::Succeeded);
    ASSERT_EQ(world.TryTickUVE(0.0F), WorldResultUVE::Succeeded);
    auto *scene = world.GetSceneUVE(restored);
    ASSERT_EQ(scene->GetEntityManagerUVE().GetEntityCountUVE(), 1U);
    scene->GetEntityManagerUVE().ForEachUVE<UVE::Scene::WorldTransformComponentUVE>(
        [](auto, const auto &pose) { EXPECT_FLOAT_EQ(pose.worldPosition.x, 3.0F); });
}
TEST_F(WorldLifecycleUVETest, ReentrantWorldMutationIsRejectedButEntityUpdatesRemainPossible) {
    auto system = SystemUVE("guard");
    system.tick = [this](auto &, float) {
        SceneHandleUVE out;
        EXPECT_EQ(world.CreateSceneUVE("nested", out), WorldResultUVE::Busy);
        EXPECT_FALSE(out.IsValidUVE());
        EXPECT_EQ(world.TryTickUVE(0.0F), WorldResultUVE::Busy);
        EXPECT_EQ(world.ShutdownUVE(), WorldResultUVE::Busy);
        EXPECT_EQ(world.RegisterSystemUVE(SystemUVE("nested")), WorldResultUVE::Busy);
    };
    ASSERT_EQ(world.RegisterSystemUVE(std::move(system)), WorldResultUVE::Succeeded);
    EXPECT_EQ(world.TryTickUVE(0.1F), WorldResultUVE::Succeeded);
    EXPECT_EQ(world.GetSceneCountUVE(), 1U);
}
TEST_F(WorldLifecycleUVETest, TickExceptionFailsClosedAndShutdownStillStopsSystems) {
    auto bad = SystemUVE("bad");
    bad.tick = [](auto &, float) { throw std::runtime_error("tick failed"); };
    ASSERT_EQ(world.RegisterSystemUVE(std::move(bad)), WorldResultUVE::Succeeded);
    EXPECT_EQ(world.TryTickUVE(0.1F), WorldResultUVE::SystemFailed);
    EXPECT_EQ(world.GetStateUVE(), WorldStateUVE::Failed);
    EXPECT_EQ(world.GetFrameCountUVE(), 0U);
    EXPECT_EQ(world.TryTickUVE(0.1F), WorldResultUVE::InvalidState);
    EXPECT_EQ(world.ShutdownUVE(), WorldResultUVE::Succeeded);
    EXPECT_EQ(trace.back(), "Primary:stop:bad");
}
TEST_F(WorldLifecycleUVETest, InvalidTimeIsRejectedBeforeStartingOrMutatingCounters) {
    EXPECT_EQ(world.TryTickUVE(-1.0F), WorldResultUVE::InvalidArgument);
    EXPECT_EQ(world.TryTickUVE(std::numeric_limits<float>::quiet_NaN()), WorldResultUVE::InvalidArgument);
    EXPECT_EQ(world.TryTickUVE(std::numeric_limits<float>::infinity()), WorldResultUVE::InvalidArgument);
    EXPECT_EQ(world.GetStateUVE(), WorldStateUVE::Created);
    EXPECT_EQ(world.GetFrameCountUVE(), 0U);
    EXPECT_FLOAT_EQ(world.GetTotalTimeSecondsUVE(), 0.0F);
}
TEST_F(WorldLifecycleUVETest, ForeignSceneIsRejectedAndShutdownIsPermanentIdempotentAndClearsPrimary) {
    WorldUVE other{allocator, events};
    EXPECT_EQ(world.GetSceneUVE(other.GetPrimarySceneHandleUVE()), nullptr);
    EXPECT_EQ(world.UnloadSceneUVE(other.GetPrimarySceneHandleUVE()), WorldResultUVE::UnknownScene);
    const auto primary = world.GetPrimarySceneHandleUVE();
    EXPECT_EQ(world.UnloadSceneUVE(primary), WorldResultUVE::InvalidState);
    static_cast<void>(world.GetEntityManagerUVE().CreateEntityUVE());
    ASSERT_EQ(world.ShutdownUVE(), WorldResultUVE::Succeeded);
    EXPECT_EQ(world.ShutdownUVE(), WorldResultUVE::Succeeded);
    EXPECT_EQ(world.GetSceneCountUVE(), 0U);
    EXPECT_EQ(world.GetEntityManagerUVE().GetEntityCountUVE(), 0U);
    EXPECT_EQ(world.GetSceneUVE(primary), nullptr);
    EXPECT_EQ(world.TryTickUVE(0), WorldResultUVE::InvalidState);
    SceneHandleUVE handle;
    EXPECT_EQ(world.CreateSceneUVE("closed", handle), WorldResultUVE::InvalidState);
}
} // namespace
