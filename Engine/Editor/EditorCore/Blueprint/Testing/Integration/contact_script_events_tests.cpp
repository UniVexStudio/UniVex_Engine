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

void WriteMountedScript(Asset::IFileSystem& fileSystem, const std::string& fileName,
                            const std::string& script) {
    const auto* const bytes = reinterpret_cast<const std::byte*>(script.data());
    ASSERT_TRUE(fileSystem.WriteFile(fileName, std::vector<std::byte>(bytes, bytes + script.size())));
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
    const std::string script = "var enters = 0\nvar exits = 0\nvar last = \"\"\n"
                               "\non collision_enter(n):\n    enters += 1\n    last = n\n"
                               "\non collision_exit(n):\n    exits += 1\n    last = n\n";
    WriteMountedScript(fileSystem, "bump.uvs", script);

    // Two unit boxes half-overlapping: A is named, B is deliberately not, so A hears "".
    const Scene::Entity boxA = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, boxA, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::ColliderComponent>(boxA, Scene::ColliderComponent{});
    entityManager.AddComponent<Scene::NameComponent>(boxA, Scene::NameComponent{"BoxA"});
    entityManager.AddComponent<Scene::ScriptComponent>(boxA, Scene::ScriptComponent{"bump.uvs"});
    const Scene::Entity boxB = entityManager.CreateEntity();
    Scene::TransformComponent overlapTransform;
    overlapTransform.localPosition = Math::Vector3{0.5F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, boxB, overlapTransform);
    entityManager.AddComponent<Scene::ColliderComponent>(boxB, Scene::ColliderComponent{});
    entityManager.AddComponent<Scene::ScriptComponent>(boxB, Scene::ScriptComponent{"bump.uvs"});

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
    const std::string script = "var enters = 0\nvar exits = 0\nvar last = \"\"\n"
                               "\non overlap_enter(n):\n    enters += 1\n    last = n\n"
                               "\non overlap_exit(n):\n    exits += 1\n    last = n\n";
    WriteMountedScript(fileSystem, "zone.uvs", script);

    const Scene::Entity zone = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, zone, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::AreaComponent>(zone, Scene::AreaComponent{});
    entityManager.AddComponent<Scene::NameComponent>(zone, Scene::NameComponent{"Zone"});
    entityManager.AddComponent<Scene::ScriptComponent>(zone, Scene::ScriptComponent{"zone.uvs"});
    const Scene::Entity player = entityManager.CreateEntity();
    Scene::TransformComponent insideTransform;
    insideTransform.localPosition = Math::Vector3{0.5F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, player, insideTransform);
    entityManager.AddComponent<Scene::ColliderComponent>(player, Scene::ColliderComponent{});
    entityManager.AddComponent<Scene::NameComponent>(player, Scene::NameComponent{"Player"});
    entityManager.AddComponent<Scene::ScriptComponent>(player, Scene::ScriptComponent{"zone.uvs"});

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
