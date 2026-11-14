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
#include "uvscript_instance.h"
#include "uvscript_value.h"
#include "vector3.h"

namespace UVE::Integration::Tests {
namespace {

Core::EngineConfigUVE MakeServicesTestConfigUVE() {
    Core::EngineConfigUVE config{};
    config.enableConsoleLogging = false;
    config.logFilePath = "uve_script_services_tests.log";
    config.threadPoolWorkerCount = 2;
    config.settingsFilePath = "uve_script_services_tests.uvsettings";
    config.assetDatabaseFilePath = "uve_script_services_tests.uvassetdb";
    config.projectSettingsFilePath = "uve_script_services_tests.project.uvsettings";
    config.inputMapFilePath = "uve_script_services_tests.project.uvinput";
    config.headlessUVE = true;
    return config;
}

void WriteTextUVE(Asset::IFileSystemUVE& fileSystem, const std::string& path, const std::string& text) {
    const auto* const bytes = reinterpret_cast<const std::byte*>(text.data());
    ASSERT_TRUE(fileSystem.WriteFileUVE(path, std::vector<std::byte>(bytes, bytes + text.size())));
}

TEST(ScriptServicesUVETest, TracesMessagesRemovalAndSaveSlotsWorkThroughTheEngine) {
    Core::EngineCoreUVE engine(MakeServicesTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entities = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();
    Asset::IFileSystemUVE& fileSystem = engine.GetServicesUVE().GetFileSystemUVE();
    const std::filesystem::path mount = "uve_script_services_mount";
    std::filesystem::remove_all(mount);
    std::filesystem::create_directories(mount);
    fileSystem.MountDirectoryUVE("", mount, 0);

    // The shooter traces down onto the floor box, tells the listener, saves, and removes the doomed
    // object and then itself.
    WriteTextUVE(fileSystem, "shooter.uvs",
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
    WriteTextUVE(fileSystem, "listener.uvs",
                 "var heard = \"\"\nvar from = \"\"\n"
                 "\non custom_event(name, payload):\n    heard = name\n    from = payload\n");
    WriteTextUVE(fileSystem, "doomed.uvs", "var x = 0\n\non destroyed:\n    events.emit_to(node(\"Listener\"), \"Bye\", \"Doomed\")\n");

    const auto make = [&](const std::string& name, const std::string& script, const Math::Vector3UVE at) {
        const Scene::EntityUVE entity = entities.CreateEntityUVE();
        Scene::TransformComponentUVE transform;
        transform.localPosition = at;
        sceneGraph.AttachTransformUVE(entities, entity, transform);
        entities.AddComponentUVE<Scene::NameComponentUVE>(entity, Scene::NameComponentUVE{name});
        if (!script.empty()) {
            entities.AddComponentUVE<Scene::ScriptComponentUVE>(entity, Scene::ScriptComponentUVE{script});
        }
        return entity;
    };
    const Scene::EntityUVE floor = make("Floor", "", Math::Vector3UVE{0.0F, -2.0F, 0.0F});
    entities.AddComponentUVE<Scene::ColliderComponentUVE>(floor, Scene::ColliderComponentUVE{});
    const Scene::EntityUVE shooter = make("Shooter", "shooter.uvs", Math::Vector3UVE{});
    const Scene::EntityUVE listener = make("Listener", "listener.uvs", Math::Vector3UVE{10.0F, 0.0F, 0.0F});
    const Scene::EntityUVE doomed = make("Doomed", "doomed.uvs", Math::Vector3UVE{20.0F, 0.0F, 0.0F});

    for (int frame = 0; frame < 30 && entities.IsAliveUVE(shooter); ++frame) {
        engine.TickFrameUVE();
    }
    // One frame more for the message the doomed object sent as it went.
    engine.TickFrameUVE();
    EXPECT_FALSE(entities.IsAliveUVE(shooter)) << "a script may remove its own object";
    EXPECT_FALSE(entities.IsAliveUVE(doomed));
    EXPECT_EQ(engine.FindUVScriptInstanceUVE(shooter), nullptr);
    UVScript::ScriptInstanceUVE* const heard = engine.FindUVScriptInstanceUVE(listener);
    ASSERT_NE(heard, nullptr);
    EXPECT_EQ(heard->GetFieldUVE("heard"), UVScript::ValueUVE{std::string("Bye")}) << "the doomed object heard `destroyed`";
    EXPECT_EQ(heard->GetFieldUVE("from"), UVScript::ValueUVE{std::string("Doomed")});

    // The save slot holds the coins: clear them, then read the slot back.
    ASSERT_EQ(engine.GetScriptServicesUVE().gameData["Coins"], "12");
    engine.GetScriptServicesUVE().gameData.clear();
    ASSERT_TRUE(engine.GetScriptServicesUVE().loadSlot("Slot1"));
    EXPECT_EQ(engine.GetScriptServicesUVE().gameData["Coins"], "12");
    EXPECT_TRUE(engine.GetScriptServicesUVE().slotExists("Slot1"));
    EXPECT_TRUE(engine.GetScriptServicesUVE().deleteSlot("Slot1"));
    EXPECT_FALSE(engine.GetScriptServicesUVE().slotExists("Slot1"));

    // A real trace: straight down onto the floor box's top (a unit box at y = -2: top at -1.5).
    const Core::ScriptServicesUVE::TraceHitUVE hit = engine.GetScriptServicesUVE().trace(
        UVScript::Vec3ValueUVE{0.0, 5.0, 0.0}, UVScript::Vec3ValueUVE{0.0, -5.0, 0.0}, 0.0, Scene::kInvalidEntityUVE);
    ASSERT_TRUE(hit.hit);
    EXPECT_EQ(hit.entity, floor);
    EXPECT_NEAR(hit.point.y, -1.5, 1e-3);
    EXPECT_NEAR(hit.normal.y, 1.0, 1e-3);

    std::filesystem::remove_all(mount);
    std::filesystem::remove(MakeServicesTestConfigUVE().assetDatabaseFilePath);
    engine.Shutdown();
}

TEST(ScriptServicesUVETest, ACameraShakeFadesAndLeavesTheCameraWhereItWas) {
    Core::EngineCoreUVE engine(MakeServicesTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entities = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();
    const Scene::EntityUVE camera = entities.CreateEntityUVE();
    Scene::TransformComponentUVE transform;
    transform.localPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
    sceneGraph.AttachTransformUVE(entities, camera, transform);
    Scene::CameraComponentUVE view;
    view.current = true;
    entities.AddComponentUVE<Scene::CameraComponentUVE>(camera, view);
    engine.TickFrameUVE();   // fills the services
    engine.GetScriptServicesUVE().cameraShake(0.5, 0.05);
    engine.TickFrameUVE();
    const Math::Vector3UVE shaken = entities.GetComponentUVE<Scene::TransformComponentUVE>(camera).localPosition;
    const float moved = std::abs(shaken.x - 1.0F) + std::abs(shaken.y - 2.0F) + std::abs(shaken.z - 3.0F);
    EXPECT_GT(moved, 0.0F) << "the view shakes";
    EXPECT_LE(std::abs(shaken.x - 1.0F), 0.5F);
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(300)) {
        engine.TickFrameUVE();
    }
    engine.TickFrameUVE();
    const Math::Vector3UVE after = entities.GetComponentUVE<Scene::TransformComponentUVE>(camera).localPosition;
    EXPECT_NEAR(after.x, 1.0F, 1e-5F);
    EXPECT_NEAR(after.y, 2.0F, 1e-5F);
    EXPECT_NEAR(after.z, 3.0F, 1e-5F);
    std::filesystem::remove(MakeServicesTestConfigUVE().assetDatabaseFilePath);
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Integration::Tests
