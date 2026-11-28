// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Whole-engine contact edges: collision and Area3D overlap transitions reach both parties'
// scripts as `collision_enter/exit` / `overlap_enter/exit`, each carrying the other object's
// name. Lives here rather than beside the animation-event test because uve_engine_tests is
// desktop-GL-gated while these need only a headless EngineCore.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Support/blueprint_test_graph.h"

#include "i_file_system.h"
#include "area_component.h"
#include "collider_component.h"
#include "name_component.h"
#include "script_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "engine_core.h"
#include "entity_manager.h"
#include "vector3.h"
#include "scene_graph.h"
#include "blueprint_instance.h"
#include "blueprint_value.h"

namespace UVE::Integration::Tests {
namespace {

Core::EngineConfig MakeHeadlessTestConfig() {
    Core::EngineConfig config{};
    config.enableConsoleLogging = false;
    config.logFilePath = "uve_contact_script_events_tests.log";
    config.threadPoolWorkerCount = 2;
    config.settingsFilePath = "uve_contact_script_events_tests.uvsettings";
    config.assetDatabaseFilePath = "uve_contact_script_events_tests.uvassetdb";
    config.projectSettingsFilePath = "uve_contact_script_events_tests.project.uvsettings";
    config.inputMapFilePath = "uve_contact_script_events_tests.project.uvinput";
    config.headless = true;
    return config;
}

/// A graph that counts the two events it hears (`enter` and `exit`) and remembers the name each one carried.
[[nodiscard]] ::UVE::Tests::TestGraph MakeContactCounter(const char* enterEvent, const char* exitEvent) {
    using PinType = Asset::BlueprintPinType;
    ::UVE::Tests::TestGraph graph;
    graph.Var("enters", PinType::Int, "0").Var("exits", PinType::Int, "0").Var("last", PinType::String, "");
    for (const auto& [event, counter] : {std::pair<const char*, const char*>{enterEvent, "enters"},
                                         std::pair<const char*, const char*>{exitEvent, "exits"}}) {
        const std::uint32_t heard = graph.Node(event);
        const ::UVE::Tests::TestGraph::Increment count = graph.AddToVariable(counter, 1);
        const std::uint32_t last = graph.Node("set_variable", {}, "last");
        graph.Exec(heard, count.set).Exec(count.set, last).Wire(heard, 1U, last, 1U);
    }
    return graph;
}

void WriteMountedScript(Asset::IFileSystem& fileSystem, const std::string& fileName, const ::UVE::Tests::TestGraph& graph) {
    ASSERT_TRUE(fileSystem.WriteFile(fileName, graph.Bytes()));
}

TEST(ContactScriptEventsTest, CollisionScriptEvents_BothBodiesHearEnterThenExitWithTheOthersName) {
    Core::EngineCore engine(MakeHeadlessTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    Asset::IFileSystem& fileSystem = engine.GetServices().GetFileSystem();
    const std::filesystem::path mountDirectory = "uve_contact_script_events_bump_mount";
    std::filesystem::remove_all(mountDirectory);
    std::filesystem::create_directories(mountDirectory);
    fileSystem.MountDirectory("", mountDirectory, 0);
    WriteMountedScript(fileSystem, "bump.uvbp", MakeContactCounter("event_collision_enter", "event_collision_exit"));

    // Two unit boxes half-overlapping: A is named, B is deliberately not, so A hears "".
    const Scene::Entity boxA = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, boxA, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::ColliderComponent>(boxA, Scene::ColliderComponent{});
    entityManager.AddComponent<Scene::NameComponent>(boxA, Scene::NameComponent{"BoxA"});
    entityManager.AddComponent<Scene::ScriptComponent>(boxA, Scene::ScriptComponent{"bump.uvbp"});
    const Scene::Entity boxB = entityManager.CreateEntity();
    Scene::TransformComponent overlapTransform;
    overlapTransform.localPosition = Math::Vector3{0.5F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, boxB, overlapTransform);
    entityManager.AddComponent<Scene::ColliderComponent>(boxB, Scene::ColliderComponent{});
    entityManager.AddComponent<Scene::ScriptComponent>(boxB, Scene::ScriptComponent{"bump.uvbp"});

    const auto startedAt = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startedAt < std::chrono::seconds(5)) {
        engine.TickFrame();
        Blueprint::ScriptInstance* const a = engine.FindUVScriptInstance(boxA);
        if (a != nullptr && a->GetField("enters") == Blueprint::Value{std::int64_t{1}}) {
            break;
        }
    }
    Blueprint::ScriptInstance* const a = engine.FindUVScriptInstance(boxA);
    Blueprint::ScriptInstance* const b = engine.FindUVScriptInstance(boxB);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a->GetField("enters"), Blueprint::Value{std::int64_t{1}});
    EXPECT_EQ(a->GetField("last"), Blueprint::Value{std::string{}});
    EXPECT_EQ(b->GetField("enters"), Blueprint::Value{std::int64_t{1}});
    EXPECT_EQ(b->GetField("last"), Blueprint::Value{std::string{"BoxA"}});

    entityManager.GetComponent<Scene::TransformComponent>(boxB).localPosition =
        Math::Vector3{5.0F, 0.0F, 0.0F};
    entityManager.GetComponent<Scene::WorldTransformComponent>(boxB).dirty = true;
    const auto exitStartedAt = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - exitStartedAt < std::chrono::seconds(5)) {
        engine.TickFrame();
        if (a->GetField("exits") == Blueprint::Value{std::int64_t{1}}) {
            break;
        }
    }
    EXPECT_EQ(a->GetField("exits"), Blueprint::Value{std::int64_t{1}});
    EXPECT_EQ(b->GetField("exits"), Blueprint::Value{std::int64_t{1}});
    EXPECT_EQ(a->GetField("enters"), Blueprint::Value{std::int64_t{1}}) << "no double-enter";

    std::filesystem::remove_all(mountDirectory);
    std::filesystem::remove(MakeHeadlessTestConfig().assetDatabaseFilePath);
    engine.Shutdown();
}

TEST(ContactScriptEventsTest, OverlapScriptEvents_AreaAndBodyHearEnterThenExit) {
    Core::EngineCore engine(MakeHeadlessTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    Asset::IFileSystem& fileSystem = engine.GetServices().GetFileSystem();
    const std::filesystem::path mountDirectory = "uve_contact_script_events_zone_mount";
    std::filesystem::remove_all(mountDirectory);
    std::filesystem::create_directories(mountDirectory);
    fileSystem.MountDirectory("", mountDirectory, 0);
    WriteMountedScript(fileSystem, "zone.uvbp", MakeContactCounter("event_overlap_enter", "event_overlap_exit"));

    const Scene::Entity zone = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, zone, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::AreaComponent>(zone, Scene::AreaComponent{});
    entityManager.AddComponent<Scene::NameComponent>(zone, Scene::NameComponent{"Zone"});
    entityManager.AddComponent<Scene::ScriptComponent>(zone, Scene::ScriptComponent{"zone.uvbp"});
    const Scene::Entity player = entityManager.CreateEntity();
    Scene::TransformComponent insideTransform;
    insideTransform.localPosition = Math::Vector3{0.5F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, player, insideTransform);
    entityManager.AddComponent<Scene::ColliderComponent>(player, Scene::ColliderComponent{});
    entityManager.AddComponent<Scene::NameComponent>(player, Scene::NameComponent{"Player"});
    entityManager.AddComponent<Scene::ScriptComponent>(player, Scene::ScriptComponent{"zone.uvbp"});

    const auto startedAt = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startedAt < std::chrono::seconds(5)) {
        engine.TickFrame();
        Blueprint::ScriptInstance* const z = engine.FindUVScriptInstance(zone);
        if (z != nullptr && z->GetField("enters") == Blueprint::Value{std::int64_t{1}}) {
            break;
        }
    }
    Blueprint::ScriptInstance* const z = engine.FindUVScriptInstance(zone);
    Blueprint::ScriptInstance* const p = engine.FindUVScriptInstance(player);
    ASSERT_NE(z, nullptr);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(z->GetField("enters"), Blueprint::Value{std::int64_t{1}});
    EXPECT_EQ(z->GetField("last"), Blueprint::Value{std::string{"Player"}});
    EXPECT_EQ(p->GetField("enters"), Blueprint::Value{std::int64_t{1}});
    EXPECT_EQ(p->GetField("last"), Blueprint::Value{std::string{"Zone"}});

    entityManager.GetComponent<Scene::TransformComponent>(player).localPosition =
        Math::Vector3{5.0F, 0.0F, 0.0F};
    entityManager.GetComponent<Scene::WorldTransformComponent>(player).dirty = true;
    const auto exitStartedAt = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - exitStartedAt < std::chrono::seconds(5)) {
        engine.TickFrame();
        if (z->GetField("exits") == Blueprint::Value{std::int64_t{1}}) {
            break;
        }
    }
    EXPECT_EQ(z->GetField("exits"), Blueprint::Value{std::int64_t{1}});
    EXPECT_EQ(p->GetField("exits"), Blueprint::Value{std::int64_t{1}});

    std::filesystem::remove_all(mountDirectory);
    std::filesystem::remove(MakeHeadlessTestConfig().assetDatabaseFilePath);
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Integration::Tests
