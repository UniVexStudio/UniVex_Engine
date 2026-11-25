// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The world a script reaches through the engine: traces against real colliders, messages to
// other scripts, removal at the end of the frame (self-removal included), and save slots.

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "camera_component.h"
#include "collider_component.h"
#include "engine_core.h"
#include "i_file_system.h"
#include "name_component.h"
#include "scene_graph.h"
#include "script_component.h"
#include "transform_component.h"
#include "blueprint_instance.h"
#include "blueprint_value.h"
#include "vector3.h"

namespace UVE::Integration::Tests {
namespace {

Core::EngineConfig MakeServicesTestConfig() {
    Core::EngineConfig config{};
    config.enableConsoleLogging = false;
    config.logFilePath = "uve_script_services_tests.log";
    config.threadPoolWorkerCount = 2;
    config.settingsFilePath = "uve_script_services_tests.uvsettings";
    config.assetDatabaseFilePath = "uve_script_services_tests.uvassetdb";
    config.projectSettingsFilePath = "uve_script_services_tests.project.uvsettings";
    config.inputMapFilePath = "uve_script_services_tests.project.uvinput";
    config.headless = true;
    return config;
}

void WriteText(Asset::IFileSystem& fileSystem, const std::string& path, const std::string& text) {
    const auto* const bytes = reinterpret_cast<const std::byte*>(text.data());
    ASSERT_TRUE(fileSystem.WriteFile(path, std::vector<std::byte>(bytes, bytes + text.size())));
}

TEST(ScriptServicesTest, TracesMessagesRemovalAndSaveSlotsWorkThroughTheEngine) {
    Core::EngineCore engine(MakeServicesTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entities = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();
    Asset::IFileSystem& fileSystem = engine.GetServices().GetFileSystem();
    const std::filesystem::path mount = "uve_script_services_mount";
    std::filesystem::remove_all(mount);
    std::filesystem::create_directories(mount);
    fileSystem.MountDirectory("", mount, 0);

    // The shooter traces down onto the floor box, tells the listener, saves, and removes the doomed
    // object and then itself.
    WriteText(fileSystem, "shooter.uvs",
                 "var hit = false\nvar where = vec3(0.0, 0.0, 0.0)\nvar saved = false\nvar frames = 0\n"
                 "\non tick(dt):\n"
                 "    frames += 1\n"
                 "    if frames == 2:\n"
                 "        hit = trace.line(vec3(0.0, 5.0, 0.0), vec3(0.0, -5.0, 0.0))\n"
                 "        where = trace.location()\n"
                 "        events.emit_to(node(\"Listener\"), \"Shot\", object.name(trace.object()))\n"
                 "        save.set_int(\"Coins\", 12)\n"
                 "        saved = save.write(\"Slot1\")\n"
                 "        world.destroy(node(\"Doomed\"))\n"
                 "        world.destroy_self()\n");
    WriteText(fileSystem, "listener.uvs",
                 "var heard = \"\"\nvar from = \"\"\n"
                 "\non custom_event(name, payload):\n    heard = name\n    from = payload\n");
    WriteText(fileSystem, "doomed.uvs", "var x = 0\n\non destroyed:\n    events.emit_to(node(\"Listener\"), \"Bye\", \"Doomed\")\n");

    const auto make = [&](const std::string& name, const std::string& script, const Math::Vector3 at) {
        const Scene::Entity entity = entities.CreateEntity();
        Scene::TransformComponent transform;
        transform.localPosition = at;
        sceneGraph.AttachTransform(entities, entity, transform);
        entities.AddComponent<Scene::NameComponent>(entity, Scene::NameComponent{name});
        if (!script.empty()) {
            entities.AddComponent<Scene::ScriptComponent>(entity, Scene::ScriptComponent{script});
        }
        return entity;
    };
    const Scene::Entity floor = make("Floor", "", Math::Vector3{0.0F, -2.0F, 0.0F});
    entities.AddComponent<Scene::ColliderComponent>(floor, Scene::ColliderComponent{});
    const Scene::Entity shooter = make("Shooter", "shooter.uvs", Math::Vector3{});
    const Scene::Entity listener = make("Listener", "listener.uvs", Math::Vector3{10.0F, 0.0F, 0.0F});
    const Scene::Entity doomed = make("Doomed", "doomed.uvs", Math::Vector3{20.0F, 0.0F, 0.0F});

    for (int frame = 0; frame < 30 && entities.IsAlive(shooter); ++frame) {
        engine.TickFrame();
    }
    // One frame more for the message the doomed object sent as it went.
    engine.TickFrame();
    EXPECT_FALSE(entities.IsAlive(shooter)) << "a script may remove its own object";
    EXPECT_FALSE(entities.IsAlive(doomed));
    EXPECT_EQ(engine.FindUVScriptInstance(shooter), nullptr);
    Blueprint::ScriptInstance* const heard = engine.FindUVScriptInstance(listener);
    ASSERT_NE(heard, nullptr);
    EXPECT_EQ(heard->GetField("heard"), Blueprint::Value{std::string("Bye")}) << "the doomed object heard `destroyed`";
    EXPECT_EQ(heard->GetField("from"), Blueprint::Value{std::string("Doomed")});

    // The save slot holds the coins: clear them, then read the slot back.
    ASSERT_EQ(engine.GetScriptServices().gameData["Coins"], "12");
    engine.GetScriptServices().gameData.clear();
    ASSERT_TRUE(engine.GetScriptServices().loadSlot("Slot1"));
    EXPECT_EQ(engine.GetScriptServices().gameData["Coins"], "12");
    EXPECT_TRUE(engine.GetScriptServices().slotExists("Slot1"));
    EXPECT_TRUE(engine.GetScriptServices().deleteSlot("Slot1"));
    EXPECT_FALSE(engine.GetScriptServices().slotExists("Slot1"));

    // A real trace: straight down onto the floor box's top (a unit box at y = -2: top at -1.5).
    const Core::ScriptServices::TraceHit hit = engine.GetScriptServices().trace(
        Blueprint::Vec3Value{0.0, 5.0, 0.0}, Blueprint::Vec3Value{0.0, -5.0, 0.0}, 0.0, Scene::kInvalidEntity);
    ASSERT_TRUE(hit.hit);
    EXPECT_EQ(hit.entity, floor);
    EXPECT_NEAR(hit.point.y, -1.5, 1e-3);
    EXPECT_NEAR(hit.normal.y, 1.0, 1e-3);

    std::filesystem::remove_all(mount);
    std::filesystem::remove(MakeServicesTestConfig().assetDatabaseFilePath);
    engine.Shutdown();
}

TEST(ScriptServicesTest, ACameraShakeFadesAndLeavesTheCameraWhereItWas) {
    Core::EngineCore engine(MakeServicesTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entities = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();
    const Scene::Entity camera = entities.CreateEntity();
    Scene::TransformComponent transform;
    transform.localPosition = Math::Vector3{1.0F, 2.0F, 3.0F};
    sceneGraph.AttachTransform(entities, camera, transform);
    Scene::CameraComponent view;
    view.current = true;
    entities.AddComponent<Scene::CameraComponent>(camera, view);
    engine.TickFrame();   // fills the services
    engine.GetScriptServices().cameraShake(0.5, 0.05);
    engine.TickFrame();
    const Math::Vector3 shaken = entities.GetComponent<Scene::TransformComponent>(camera).localPosition;
    const float moved = std::abs(shaken.x - 1.0F) + std::abs(shaken.y - 2.0F) + std::abs(shaken.z - 3.0F);
    EXPECT_GT(moved, 0.0F) << "the view shakes";
    EXPECT_LE(std::abs(shaken.x - 1.0F), 0.5F);
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(300)) {
        engine.TickFrame();
    }
    engine.TickFrame();
    const Math::Vector3 after = entities.GetComponent<Scene::TransformComponent>(camera).localPosition;
    EXPECT_NEAR(after.x, 1.0F, 1e-5F);
    EXPECT_NEAR(after.y, 2.0F, 1e-5F);
    EXPECT_NEAR(after.z, 3.0F, 1e-5F);
    std::filesystem::remove(MakeServicesTestConfig().assetDatabaseFilePath);
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Integration::Tests
