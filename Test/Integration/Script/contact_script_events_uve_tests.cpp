// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Whole-engine contact edges: collision and Area3D overlap transitions reach both parties'
// scripts as `collision_enter/exit` / `overlap_enter/exit`, each carrying the other object's
// name. Lives here rather than beside the animation-event test because uve_engine_tests is
// desktop-GL-gated while these need only a headless EngineCoreUVE.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/i_file_system_uve.h"
#include "uve/component/area_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/core/engine_core_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/scene/scene_graph_uve.h"
#include "uve/uvscript/uvscript_instance_uve.h"
#include "uve/uvscript/uvscript_value_uve.h"

namespace UVE::Integration::Tests {
namespace {

Core::EngineConfigUVE MakeHeadlessTestConfigUVE() {
    Core::EngineConfigUVE config{};
    config.enableConsoleLogging = false;
    config.logFilePath = "uve_contact_script_events_tests.log";
    config.threadPoolWorkerCount = 2;
    config.settingsFilePath = "uve_contact_script_events_tests.uvsettings";
    config.assetDatabaseFilePath = "uve_contact_script_events_tests.uvassetdb";
    config.projectSettingsFilePath = "uve_contact_script_events_tests.project.uvsettings";
    config.inputMapFilePath = "uve_contact_script_events_tests.project.uvinput";
    config.headlessUVE = true;
    return config;
}

void WriteMountedScriptUVE(Asset::IFileSystemUVE& fileSystem, const std::string& fileName,
                            const std::string& script) {
    const auto* const bytes = reinterpret_cast<const std::byte*>(script.data());
    ASSERT_TRUE(fileSystem.WriteFileUVE(fileName, std::vector<std::byte>(bytes, bytes + script.size())));
}

TEST(ContactScriptEventsUVETest, CollisionScriptEvents_BothBodiesHearEnterThenExitWithTheOthersName) {
    Core::EngineCoreUVE engine(MakeHeadlessTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    Asset::IFileSystemUVE& fileSystem = engine.GetServicesUVE().GetFileSystemUVE();
    const std::filesystem::path mountDirectory = "uve_contact_script_events_bump_mount";
    std::filesystem::remove_all(mountDirectory);
    std::filesystem::create_directories(mountDirectory);
    fileSystem.MountDirectoryUVE("", mountDirectory, 0);
    const std::string script = "var enters = 0\nvar exits = 0\nvar last = \"\"\n"
                               "\non collision_enter(n):\n    enters += 1\n    last = n\n"
                               "\non collision_exit(n):\n    exits += 1\n    last = n\n";
    WriteMountedScriptUVE(fileSystem, "bump.uvs", script);

    // Two unit boxes half-overlapping: A is named, B is deliberately not, so A hears "".
    const Scene::EntityUVE boxA = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, boxA, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(boxA, Scene::ColliderComponentUVE{});
    entityManager.AddComponentUVE<Scene::NameComponentUVE>(boxA, Scene::NameComponentUVE{"BoxA"});
    entityManager.AddComponentUVE<Scene::ScriptComponentUVE>(boxA, Scene::ScriptComponentUVE{"bump.uvs"});
    const Scene::EntityUVE boxB = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE overlapTransform;
    overlapTransform.localPosition = Math::Vector3UVE{0.5F, 0.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, boxB, overlapTransform);
    entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(boxB, Scene::ColliderComponentUVE{});
    entityManager.AddComponentUVE<Scene::ScriptComponentUVE>(boxB, Scene::ScriptComponentUVE{"bump.uvs"});

    const auto startedAt = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startedAt < std::chrono::seconds(5)) {
        engine.TickFrameUVE();
        UVScript::ScriptInstanceUVE* const a = engine.FindUVScriptInstanceUVE(boxA);
        if (a != nullptr && a->GetFieldUVE("enters") == UVScript::ValueUVE{std::int64_t{1}}) {
            break;
        }
    }
    UVScript::ScriptInstanceUVE* const a = engine.FindUVScriptInstanceUVE(boxA);
    UVScript::ScriptInstanceUVE* const b = engine.FindUVScriptInstanceUVE(boxB);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a->GetFieldUVE("enters"), UVScript::ValueUVE{std::int64_t{1}});
    EXPECT_EQ(a->GetFieldUVE("last"), UVScript::ValueUVE{std::string{}});
    EXPECT_EQ(b->GetFieldUVE("enters"), UVScript::ValueUVE{std::int64_t{1}});
    EXPECT_EQ(b->GetFieldUVE("last"), UVScript::ValueUVE{std::string{"BoxA"}});

    entityManager.GetComponentUVE<Scene::TransformComponentUVE>(boxB).localPosition =
        Math::Vector3UVE{5.0F, 0.0F, 0.0F};
    entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(boxB).dirty = true;
    const auto exitStartedAt = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - exitStartedAt < std::chrono::seconds(5)) {
        engine.TickFrameUVE();
        if (a->GetFieldUVE("exits") == UVScript::ValueUVE{std::int64_t{1}}) {
            break;
        }
    }
    EXPECT_EQ(a->GetFieldUVE("exits"), UVScript::ValueUVE{std::int64_t{1}});
    EXPECT_EQ(b->GetFieldUVE("exits"), UVScript::ValueUVE{std::int64_t{1}});
    EXPECT_EQ(a->GetFieldUVE("enters"), UVScript::ValueUVE{std::int64_t{1}}) << "no double-enter";

    std::filesystem::remove_all(mountDirectory);
    std::filesystem::remove(MakeHeadlessTestConfigUVE().assetDatabaseFilePath);
    engine.Shutdown();
}

TEST(ContactScriptEventsUVETest, OverlapScriptEvents_AreaAndBodyHearEnterThenExit) {
    Core::EngineCoreUVE engine(MakeHeadlessTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    Asset::IFileSystemUVE& fileSystem = engine.GetServicesUVE().GetFileSystemUVE();
    const std::filesystem::path mountDirectory = "uve_contact_script_events_zone_mount";
    std::filesystem::remove_all(mountDirectory);
    std::filesystem::create_directories(mountDirectory);
    fileSystem.MountDirectoryUVE("", mountDirectory, 0);
    const std::string script = "var enters = 0\nvar exits = 0\nvar last = \"\"\n"
                               "\non overlap_enter(n):\n    enters += 1\n    last = n\n"
                               "\non overlap_exit(n):\n    exits += 1\n    last = n\n";
    WriteMountedScriptUVE(fileSystem, "zone.uvs", script);

    const Scene::EntityUVE zone = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, zone, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::AreaComponentUVE>(zone, Scene::AreaComponentUVE{});
    entityManager.AddComponentUVE<Scene::NameComponentUVE>(zone, Scene::NameComponentUVE{"Zone"});
    entityManager.AddComponentUVE<Scene::ScriptComponentUVE>(zone, Scene::ScriptComponentUVE{"zone.uvs"});
    const Scene::EntityUVE player = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE insideTransform;
    insideTransform.localPosition = Math::Vector3UVE{0.5F, 0.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, player, insideTransform);
    entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(player, Scene::ColliderComponentUVE{});
    entityManager.AddComponentUVE<Scene::NameComponentUVE>(player, Scene::NameComponentUVE{"Player"});
    entityManager.AddComponentUVE<Scene::ScriptComponentUVE>(player, Scene::ScriptComponentUVE{"zone.uvs"});

    const auto startedAt = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startedAt < std::chrono::seconds(5)) {
        engine.TickFrameUVE();
        UVScript::ScriptInstanceUVE* const z = engine.FindUVScriptInstanceUVE(zone);
        if (z != nullptr && z->GetFieldUVE("enters") == UVScript::ValueUVE{std::int64_t{1}}) {
            break;
        }
    }
    UVScript::ScriptInstanceUVE* const z = engine.FindUVScriptInstanceUVE(zone);
    UVScript::ScriptInstanceUVE* const p = engine.FindUVScriptInstanceUVE(player);
    ASSERT_NE(z, nullptr);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(z->GetFieldUVE("enters"), UVScript::ValueUVE{std::int64_t{1}});
    EXPECT_EQ(z->GetFieldUVE("last"), UVScript::ValueUVE{std::string{"Player"}});
    EXPECT_EQ(p->GetFieldUVE("enters"), UVScript::ValueUVE{std::int64_t{1}});
    EXPECT_EQ(p->GetFieldUVE("last"), UVScript::ValueUVE{std::string{"Zone"}});

    entityManager.GetComponentUVE<Scene::TransformComponentUVE>(player).localPosition =
        Math::Vector3UVE{5.0F, 0.0F, 0.0F};
    entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(player).dirty = true;
    const auto exitStartedAt = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - exitStartedAt < std::chrono::seconds(5)) {
        engine.TickFrameUVE();
        if (z->GetFieldUVE("exits") == UVScript::ValueUVE{std::int64_t{1}}) {
            break;
        }
    }
    EXPECT_EQ(z->GetFieldUVE("exits"), UVScript::ValueUVE{std::int64_t{1}});
    EXPECT_EQ(p->GetFieldUVE("exits"), UVScript::ValueUVE{std::int64_t{1}});

    std::filesystem::remove_all(mountDirectory);
    std::filesystem::remove(MakeHeadlessTestConfigUVE().assetDatabaseFilePath);
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Integration::Tests
