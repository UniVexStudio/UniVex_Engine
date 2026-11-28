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

#include <cstdio>
#include <gtest/gtest.h>

#include "Support/blueprint_test_graph.h"

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
    using PinType = Asset::BlueprintPinType;
    ::UVE::Tests::TestGraph shooterGraph;
    shooterGraph.Var("hit", PinType::Bool, "false").Var("where", PinType::Vector, "0,0,0").Var("frames", PinType::Int, "0");
    {
        const std::uint32_t tick = shooterGraph.Node("event_tick");
        const ::UVE::Tests::TestGraph::Increment frame = shooterGraph.AddToVariable("frames", 1);
        const std::uint32_t second = shooterGraph.Node("get_variable", {}, "frames");
        const std::uint32_t isSecond = shooterGraph.Node("int_equal", {"", "2"});
        const std::uint32_t branch = shooterGraph.Node("branch");
        const std::uint32_t trace = shooterGraph.Node("line_trace", {"", "0,5,0", "0,-5,0"});
        const std::uint32_t setHit = shooterGraph.Node("set_variable", {}, "hit");
        const std::uint32_t setWhere = shooterGraph.Node("set_variable", {}, "where");
        const std::uint32_t listenerNode = shooterGraph.Node("find_object", {"Listener"});
        const std::uint32_t hitName = shooterGraph.Node("object_get_name");
        const std::uint32_t shot = shooterGraph.Node("call_custom_event_on", {"", "", "Shot"});
        const std::uint32_t coins = shooterGraph.Node("save_set_int", {"", "Coins", "12"});
        const std::uint32_t write = shooterGraph.Node("save_game", {"", "Slot1"});
        const std::uint32_t doomed = shooterGraph.Node("find_object", {"Doomed"});
        const std::uint32_t destroy = shooterGraph.Node("destroy_object");
        const std::uint32_t destroySelf = shooterGraph.Node("destroy_self");
        shooterGraph.Exec(tick, frame.set).Exec(frame.set, branch);
        shooterGraph.Wire(second, 0U, isSecond, 0U).Wire(isSecond, 0U, branch, 1U);
        shooterGraph.Wire(branch, 0U, trace, 0U);
        shooterGraph.Wire(trace, 1U, setHit, 1U).Wire(trace, 2U, setWhere, 1U);
        shooterGraph.Exec(trace, setHit).Exec(setHit, setWhere).Exec(setWhere, shot);
        shooterGraph.Wire(listenerNode, 0U, shot, 1U).Wire(trace, 4U, hitName, 0U).Wire(hitName, 0U, shot, 3U);
        shooterGraph.Exec(shot, coins).Exec(coins, write).Exec(write, destroy);
        shooterGraph.Wire(doomed, 0U, destroy, 1U).Exec(destroy, destroySelf);
    }
    ASSERT_TRUE(fileSystem.WriteFile("shooter.uvbp", shooterGraph.Bytes()));
    ::UVE::Tests::TestGraph listenerGraph;
    listenerGraph.Var("heard", PinType::String, "").Var("from", PinType::String, "");
    {
        const std::uint32_t event = listenerGraph.Node("event_custom");
        const std::uint32_t heard = listenerGraph.Node("set_variable", {}, "heard");
        const std::uint32_t from = listenerGraph.Node("set_variable", {}, "from");
        listenerGraph.Exec(event, heard).Exec(heard, from).Wire(event, 1U, heard, 1U).Wire(event, 2U, from, 1U);
    }
    ASSERT_TRUE(fileSystem.WriteFile("listener.uvbp", listenerGraph.Bytes()));
    ::UVE::Tests::TestGraph doomedGraph;
    doomedGraph.Var("x", PinType::Int, "0");
    {
        const std::uint32_t destroyed = doomedGraph.Node("event_destroyed");
        const std::uint32_t target = doomedGraph.Node("find_object", {"Listener"});
        const std::uint32_t bye = doomedGraph.Node("call_custom_event_on", {"", "", "Bye", "Doomed"});
        doomedGraph.Exec(destroyed, bye).Wire(target, 0U, bye, 1U);
    }
    ASSERT_TRUE(fileSystem.WriteFile("doomed.uvbp", doomedGraph.Bytes()));

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
    const Scene::Entity shooter = make("Shooter", "shooter.uvbp", Math::Vector3{});
    const Scene::Entity listener = make("Listener", "listener.uvbp", Math::Vector3{10.0F, 0.0F, 0.0F});
    const Scene::Entity doomed = make("Doomed", "doomed.uvbp", Math::Vector3{20.0F, 0.0F, 0.0F});

    for (int frame = 0; frame < 30 && entities.IsAlive(shooter); ++frame) {
        engine.TickFrame();
    }
    // One frame more for the message the doomed object sent as it went.
    engine.TickFrame();
    EXPECT_FALSE(entities.IsAlive(shooter)) << "a script may remove its own object";
    EXPECT_FALSE(entities.IsAlive(doomed));
    EXPECT_EQ(engine.FindBlueprintInstance(shooter), nullptr);
    Blueprint::BlueprintInstance* const heard = engine.FindBlueprintInstance(listener);
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
