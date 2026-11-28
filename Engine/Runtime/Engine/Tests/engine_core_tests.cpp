// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "motion_montage_asset.h"
#include "motion_montage_editing.h"
#include "montage_sequencer_component.h"
#include "engine_core.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <numbers>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <GL/gl.h>
#include <gtest/gtest.h>

#include "Support/blueprint_test_graph.h"

#include "thread_group_component.h"

#include "localization.h"

#include "auto_translate_component.h"

#include "process_component.h"
#include "transform_component.h"
#include "blueprint_instance.h"

#include "Support/test_scratch.h"

#include "asset_handle.h"
#include "asset_importer.h"
#include "asset_manager.h"
#include "animation_clip_asset.h"
#include "audio_asset.h"
#include "blob_asset.h"
#include "data_table_importer.h"
#include "data_table.h"
#include "i_audio_system.h"
#include "log_sink.h"
#include "logger.h"
#include "i_input_system.h"
#include "mouse_button.h"
#include "aabb.h"
#include "vector3.h"
#include "area_overlap_events.h"
#include "i_collision_system.h"
#include "i_physics_system.h"
#include "physics_constraint_system.h"
#include "projectile_3d_step.h"
#include "hitbox_strike_events.h"
#include "i_raycast_system.h"
#include "platform.h"
#include "i_camera_system.h"
#include "i_light_system.h"
#include "i_render_device.h"
#include "i_render_system.h"
#include "i_checkpoint_manager.h"
#include "i_save_game_system.h"
#include "area_component.h"
#include "audio_source_component.h"
#include "camera_component.h"
#include "character_controller_component.h"
#include "pawn.h"
#include "solid_body_component.h"
#include "collider_component.h"
#include "uve_file_envelope.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"
#include "scene_graph.h"
#include "scene_serializer.h"
#include "animation_sequencer.h"
#include "skeleton_3d.h"
#include "bone_attachment_3d.h"
#include "two_bone_ik_3d.h"
#include "hitbox_3d.h"
#include "hurtbox_3d.h"
#include "health_events.h"
#include "health.h"
#include "decal_3d_events.h"
#include "decal_3d.h"
#include "nav_mesh_volume_3d.h"
#include "nav_seeker_3d.h"
#include "interaction_area_3d.h"
#include "level_streamer_3d.h"
#include "reflection_probe_3d.h"
#include "visibility_region_3d.h"
#include "world_partition_3d.h"
#include "projectile_3d.h"
#include "ray_cast_3d.h"
#include "light_component.h"
#include "mesh_component.h"
#include "particle_emitter_component.h"
#include "primitive_mesh_component.h"
#include "rigid_3d_component.h"
#include "script_component.h"
#include "transform_component.h"
#include "ui_button_component.h"
#include "ui_image_component.h"
#include "ui_text_component.h"
#include "world_transform_component.h"
#include "null_render_device.h"
#include "i_window_manager.h"
#include "engine_project_settings.h"
#include "config_manager.h"
#include "particle_system_asset.h"
#include "vfx_templates.h"

namespace UVE::Core::Tests {
namespace {

EngineConfig MakeTestConfig() {
    EngineConfig config{};
    config.enableConsoleLogging = false;
    config.logFilePath = "uve_engine_core_tests.log";
    config.threadPoolWorkerCount = 2; // keep the whole suite's thread churn small and fast
    config.settingsFilePath = "uve_engine_core_tests.uvsettings"; // never touch a real settings file
    config.assetDatabaseFilePath = "uve_engine_core_tests.uvassetdb"; // never touch a real asset db
    config.projectSettingsFilePath = "uve_engine_core_tests.project.uvsettings"; // nor a real project file
    config.inputMapFilePath = "uve_engine_core_tests.project.uvinput";
    config.headless = true; // NullWindowManager/NullRenderDevice - no display required;
                                // every pre-Increment-20 test opts into this by default, matching
                                // its exact prior (headless-only) behavior. Tests that specifically
                                // exercise the real window/GL backend override this explicitly.
    return config;
}

TEST(EngineCoreTest, InvalidApplicationRenderScaleFallsBackToNativeResolution) {
    EngineConfig config = MakeTestConfig();
    config.renderResolutionScale = std::numeric_limits<double>::quiet_NaN();
    config.dynamicRenderResolutionEnabled = true;
    config.minimumRenderResolutionScale = 1.25;
    config.renderResolutionTargetFrameTimeMilliseconds = std::numeric_limits<double>::quiet_NaN();
    config.settingsFilePath = "uve_engine_core_invalid_render_scale.uvsettings";
    config.projectSettingsFilePath = "uve_engine_core_invalid_render_scale.project.uvsettings";
    config.platformSettingsFilePath = "uve_engine_core_invalid_render_scale.platform.uvsettings";

    EngineCore engine(config);
    engine.Init();
    EXPECT_DOUBLE_EQ(engine.GetConfig().renderResolutionScale, 1.0);
    EXPECT_TRUE(engine.GetConfig().dynamicRenderResolutionEnabled);
    EXPECT_DOUBLE_EQ(engine.GetConfig().minimumRenderResolutionScale,
                     kMinimumRenderResolutionScale);
    EXPECT_DOUBLE_EQ(engine.GetConfig().renderResolutionTargetFrameTimeMilliseconds,
                     kDefaultRenderResolutionTargetFrameTimeMilliseconds);
    engine.Shutdown();

    std::filesystem::remove(config.settingsFilePath);
    std::filesystem::remove(config.projectSettingsFilePath);
    std::filesystem::remove(config.platformSettingsFilePath);
}

TEST(EngineCoreTest, UnsafeProjectShaderCachePathDoesNotOverrideSafeApplicationValue) {
    EngineConfig config = MakeTestConfig();
    config.projectSettingsFilePath = "uve_engine_core_unsafe_shader_cache.project.uvsettings";
    config.settingsFilePath = "uve_engine_core_unsafe_shader_cache.uvsettings";
    config.platformSettingsFilePath = "uve_engine_core_unsafe_shader_cache.platform.uvsettings";
    config.shaderCachePath = "safe_application_cache";
    {
        std::ofstream file(config.projectSettingsFilePath);
        file << R"({"rendering": {"shaders": {"programCacheDirectory": "../outside_cache"}}})";
    }

    EngineCore engine(config);
    engine.Init();
    EXPECT_EQ(engine.GetConfig().shaderCachePath, std::filesystem::path("safe_application_cache"));
    engine.Shutdown();
    std::filesystem::remove(config.projectSettingsFilePath);
    std::filesystem::remove(config.settingsFilePath);
    std::filesystem::remove(config.platformSettingsFilePath);
}

TEST(EngineCoreTest, UnsafeApplicationShaderCachePathFallsBackToEngineDefault) {
    EngineConfig config = MakeTestConfig();
    config.projectSettingsFilePath = "uve_engine_core_unsafe_application_shader_cache.project.uvsettings";
    config.settingsFilePath = "uve_engine_core_unsafe_application_shader_cache.uvsettings";
    config.platformSettingsFilePath = "uve_engine_core_unsafe_application_shader_cache.platform.uvsettings";
    config.shaderCachePath = "../outside_cache";

    EngineCore engine(config);
    engine.Init();
    EXPECT_EQ(engine.GetConfig().shaderCachePath, EngineConfig{}.shaderCachePath);
    engine.Shutdown();
    std::filesystem::remove(config.projectSettingsFilePath);
    std::filesystem::remove(config.settingsFilePath);
    std::filesystem::remove(config.platformSettingsFilePath);
}

TEST(EngineCoreTest, ProjectSettings_OverrideTheApplicationsConfigWhereTheProjectSetsThem) {
    EngineConfig config = MakeTestConfig();
    config.projectSettingsFilePath = "uve_engine_core_tests_overrides.project.uvsettings";
    config.autoSaveIntervalSeconds = 123.0; // the application's choice, kept unless overridden
    {
        std::ofstream file(config.projectSettingsFilePath);
        // Ticks and filter are legal; the auto-save interval is below its minimum and ignored.
        file << R"({"physics": {"common": {"ticksPerSecond": 30},
                                "3d": {"gravity": {"x": 0.0, "y": -3.7, "z": 0.0}}},
                    "rendering": {"shadows": {"filter": 0}},
                    "application": {"save": {"autoSaveInterval": 5.0}}})";
    }
    EngineCore engine(config);
    engine.Init();
    EXPECT_DOUBLE_EQ(engine.GetConfig().fixedUpdateFps, 30.0);
    EXPECT_FLOAT_EQ(engine.GetConfig().gravity.y, -3.7F);
    EXPECT_EQ(engine.GetConfig().maxFixedStepsPerFrame, EngineConfig{}.maxFixedStepsPerFrame);
    EXPECT_EQ(engine.GetConfig().shadowPcfKernelRadius, 0U);
    EXPECT_DOUBLE_EQ(engine.GetConfig().autoSaveIntervalSeconds, 123.0);
    EXPECT_EQ(engine.GetConfig().shadowMapResolution, EngineConfig{}.shadowMapResolution);
    const Config::SettingsDocument& project = engine.GetServices().GetProjectSettings();
    EXPECT_TRUE(project.IsModified(EngineProjectSettingId::kPhysicsTicksPerSecond));
    EXPECT_TRUE(project.IsDirty()); // The unversioned fixture is upgraded in memory for its next save.
    engine.Shutdown();
    std::filesystem::remove(config.projectSettingsFilePath);
}

TEST(EngineCoreTest, UserSettingsOverrideProjectSettingsBeforeDependentSystemsInitialize) {
    EngineConfig config = MakeTestConfig();
    config.projectSettingsFilePath = "uve_engine_core_tests_user_override.project.uvsettings";
    config.settingsFilePath = "uve_engine_core_tests_user_override.uvsettings";
    {
        std::ofstream file(config.projectSettingsFilePath);
        file << R"({"physics": {"common": {"ticksPerSecond": 30}}})";
    }
    {
        std::ofstream file(config.settingsFilePath);
        file << R"({"physics": {"common": {"ticksPerSecond": 120}}})";
    }

    EngineCore engine(config);
    engine.Init();
    EXPECT_DOUBLE_EQ(engine.GetConfig().fixedUpdateFps, 120.0);
    engine.Shutdown();
    std::filesystem::remove(config.projectSettingsFilePath);
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EngineCoreTest, InvalidUserSettingFallsBackToTheValidProjectSetting) {
    EngineConfig config = MakeTestConfig();
    config.projectSettingsFilePath = "uve_engine_core_tests_invalid_user.project.uvsettings";
    config.settingsFilePath = "uve_engine_core_tests_invalid_user.uvsettings";
    {
        std::ofstream file(config.projectSettingsFilePath);
        file << R"({"physics": {"common": {"ticksPerSecond": 30}}})";
    }
    {
        std::ofstream file(config.settingsFilePath);
        file << R"({"physics": {"common": {"ticksPerSecond": 0}}})";
    }

    EngineCore engine(config);
    engine.Init();
    EXPECT_DOUBLE_EQ(engine.GetConfig().fixedUpdateFps, 30.0);
    engine.Shutdown();
    std::filesystem::remove(config.projectSettingsFilePath);
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EngineCoreTest, PlatformAndCommandLineLayersOverrideUserAndProject) {
    EngineConfig config = MakeTestConfig();
    config.projectSettingsFilePath = "uve_engine_core_tests_cli_platform.project.uvsettings";
    config.settingsFilePath = "uve_engine_core_tests_cli_platform.uvsettings";
    config.platformSettingsFilePath = "uve_engine_core_tests_cli_platform.platform.uvsettings";
    config.commandLineArgs = {"--rendering.shadows.mapResolution", "512"};
    {
        std::ofstream file(config.projectSettingsFilePath);
        file << R"({"rendering": {"shadows": {"mapResolution": 4096}}})";
    }
    {
        std::ofstream file(config.settingsFilePath);
        file << R"({"rendering": {"shadows": {"mapResolution": 2048}}})";
    }
    {
        std::ofstream file(config.platformSettingsFilePath);
        file << R"({"rendering": {"shadows": {"mapResolution": 1024}}})";
    }

    EngineCore engine(config);
    engine.Init();
    EXPECT_EQ(engine.GetConfig().shadowMapResolution, 512U);
    engine.Shutdown();
    std::filesystem::remove(config.projectSettingsFilePath);
    std::filesystem::remove(config.settingsFilePath);
    std::filesystem::remove(config.platformSettingsFilePath);
}

TEST(EngineCoreTest, InvalidCommandLineSettingFallsBackToPlatformValue) {
    EngineConfig config = MakeTestConfig();
    config.projectSettingsFilePath = "uve_engine_core_tests_invalid_cli.project.uvsettings";
    config.settingsFilePath = "uve_engine_core_tests_invalid_cli.uvsettings";
    config.platformSettingsFilePath = "uve_engine_core_tests_invalid_cli.platform.uvsettings";
    config.commandLineArgs = {"--rendering.shadows.mapResolution", "1536"};
    {
        std::ofstream file(config.projectSettingsFilePath);
        file << R"({"rendering": {"shadows": {"mapResolution": 4096}}})";
    }
    {
        std::ofstream file(config.settingsFilePath);
        file << R"({"rendering": {"shadows": {"mapResolution": 2048}}})";
    }
    {
        std::ofstream file(config.platformSettingsFilePath);
        file << R"({"rendering": {"shadows": {"mapResolution": 1024}}})";
    }

    EngineCore engine(config);
    engine.Init();
    EXPECT_EQ(engine.GetConfig().shadowMapResolution, 1024U);
    engine.Shutdown();
    std::filesystem::remove(config.projectSettingsFilePath);
    std::filesystem::remove(config.settingsFilePath);
    std::filesystem::remove(config.platformSettingsFilePath);
}

TEST(EngineCoreTest, ProjectSettings_DeclareEngineDefaultsAndNeedARestart) {
    Config::SettingsRegistry registry;
    ASSERT_TRUE(RegisterEngineProjectSettings(registry));
    // 176 settings declared in the GetEngineProjectSettings() table plus the default-player entity
    // registered on its own; the 4 * kLayerCount layer-name settings (Physics, Render, Navigation,
    // Avoidance) are added on top of this. Spelled out because the number silently drifts every time a
    // setting is added or removed - this assertion is the drift detector, so update it deliberately
    // when the table changes (most recently removing the 6 pure-2D settings and LayerSet::Physics2D).
    EXPECT_EQ(registry.GetCount(), 177U + (4U * kLayerCount));
    namespace Id = EngineProjectSettingId;
    ASSERT_NE(registry.Find(Id::kDefaultPlayerEntity), nullptr);
    ASSERT_NE(registry.Find(Id::kShaderProgramCacheDirectory), nullptr);
    EXPECT_EQ(registry.Find(Id::kShaderProgramCacheDirectory)->type, Config::SettingType::FilePath);
    EXPECT_TRUE(registry.Find(Id::kShaderProgramCacheDirectory)
                    ->HasFlag(Config::kSettingFlagPerPlatform));
    EXPECT_EQ(registry.Find(Id::kDefaultPlayerEntity)->type, Config::SettingType::FilePath);
    const EngineConfig defaults{};
    EXPECT_DOUBLE_EQ(registry.GetFloat(Config::ConfigManager{}, Id::kPhysicsTicksPerSecond),
                     defaults.fixedUpdateFps);
    EXPECT_EQ(registry.GetInt(Config::ConfigManager{}, Id::kShadowMapResolution),
              static_cast<std::int64_t>(defaults.shadowMapResolution));
    EXPECT_EQ(registry.GetBool(Config::ConfigManager{}, Id::kHeadless), defaults.headless);
    ASSERT_NE(registry.Find(Id::kShadowMapResolution), nullptr);
    EXPECT_TRUE(registry.Find(Id::kShadowMapResolution)->HasFlag(Config::kSettingFlagPerPlatform));
    ASSERT_NE(registry.Find(Id::kHeadless), nullptr);
    EXPECT_TRUE(registry.Find(Id::kHeadless)->HasFlag(Config::kSettingFlagHidden));
    EXPECT_TRUE(registry.Find(Id::kHeadless)->HasFlag(Config::kSettingFlagNotPersisted));
    for (const Config::SettingDescriptor* descriptor : registry.GetAll()) {
        EXPECT_EQ(Config::ValidateSettingDescriptor(*descriptor), "") << descriptor->id;
        // Everything that overrides EngineConfig is read at startup; layer names and the default
        // player are read when they are used.
        EXPECT_EQ(descriptor->HasFlag(Config::kSettingFlagRestartRequired),
                  !descriptor->id.starts_with("layers.") && !descriptor->id.starts_with("game."))
            << descriptor->id;
    }
}

TEST(EngineCoreTest, ProjectSettings_NameLayersOneToThirtyTwo) {
    EXPECT_EQ(GetLayerNameSettingId(LayerSet::Physics, 0U), "layers.physics.1");
    EXPECT_EQ(GetLayerNameSettingId(LayerSet::Render, 31U), "layers.render.32");
    Config::SettingsDocument document;
    ASSERT_TRUE(RegisterEngineProjectSettings(document.GetRegistry()));
    EXPECT_EQ(GetLayerName(document, LayerSet::Physics, 2U), "");
    ASSERT_TRUE(document.SetValue("layers.physics.3", std::string("Enemies")));
    EXPECT_EQ(GetLayerName(document, LayerSet::Physics, 2U), "Enemies");
    EXPECT_EQ(GetLayerName(document, LayerSet::Render, 2U), "");
    EXPECT_EQ(GetLayerName(document, LayerSet::Physics, kLayerCount), "");
    EXPECT_FALSE(document.SetValue("layers.physics.3", std::string(33, 'x')));
}

TEST(EngineCoreTest, InitialState_IsUninitialized) {
    const EngineCore engine(MakeTestConfig());
    EXPECT_EQ(engine.GetState(), EngineState::Uninitialized);
}

TEST(EngineCoreTest, BuildProfileDefaultsMatchCompiledPolicy) {
    const EngineConfig config{};
#if defined(UVE_PROFILE_DEFAULT_LOG_LEVEL)
    EXPECT_EQ(config.minLogLevel,
              static_cast<Debug::LogLevel>(UVE_PROFILE_DEFAULT_LOG_LEVEL));
#else
    EXPECT_EQ(config.minLogLevel, Debug::LogLevel::Trace);
#endif
#if defined(UVE_PROFILE_ASSERTIONS_ENABLED)
    EXPECT_EQ(UVE_DEBUG, UVE_PROFILE_ASSERTIONS_ENABLED);
#else
    EXPECT_EQ(UVE_DEBUG, 1);
#endif
}

TEST(EngineCoreTest, ParticleEmitterComponents_ReconcileWithRuntimeAcrossFrames) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    const Scene::Entity entity = entityManager.CreateEntity();
    ASSERT_NE(entity, Scene::kInvalidEntity);
    entityManager.AddComponent<Scene::ParticleEmitterComponent>(entity,
                                                                       Scene::ParticleEmitterComponent{32U});

    engine.TickFrame();
    Scene::ParticleRuntimeSnapshot snapshot = engine.GetParticleRuntimeSnapshot();
    ASSERT_EQ(snapshot.instanceCount, 1U);
    ASSERT_EQ(snapshot.instances.size(), 1U);
    EXPECT_EQ(snapshot.instances.front().entity, entity);
    EXPECT_EQ(snapshot.instances.front().maxParticles, 32U);

    entityManager.GetComponent<Scene::ParticleEmitterComponent>(entity).maxParticles = 64U;
    engine.TickFrame();
    snapshot = engine.GetParticleRuntimeSnapshot();
    ASSERT_EQ(snapshot.instances.size(), 1U);
    EXPECT_EQ(snapshot.instances.front().maxParticles, 64U);

    entityManager.RemoveComponent<Scene::ParticleEmitterComponent>(entity);
    engine.TickFrame();
    EXPECT_EQ(engine.GetParticleRuntimeSnapshot().instanceCount, 0U);
}

TEST(EngineCoreTest, ParticleEmitter_AutoEmitsFromTheWorldPose) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Core::EngineServices& services = engine.GetServices();
    Scene::IEntityManager& entityManager = services.GetEntityManager();
    Scene::ISceneGraph& sceneGraph = services.GetSceneGraph();

    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::TransformComponent transform{};
    transform.localPosition = Math::Vector3{0.0F, 4.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, entity, transform);
    Scene::ParticleEmitterComponent emitter{};
    emitter.maxParticles = 32U;
    emitter.emissionRate = 1'000'000.0F;
    emitter.lifetimeSeconds = 2.0F;
    entityManager.AddComponent<Scene::ParticleEmitterComponent>(entity, emitter);

    engine.TickFrame();
    const Scene::ParticleRuntimeSnapshot snapshot = engine.GetParticleRuntimeSnapshot();
    ASSERT_EQ(snapshot.instances.size(), 1U);
    EXPECT_GT(snapshot.instances.front().liveParticles, 0U);
    const std::optional<Scene::ParticleStateSnapshot> particles =
        services.GetParticleRuntime().GetParticleSnapshot(entity);
    ASSERT_TRUE(particles.has_value());
    ASSERT_FALSE(particles->particles.empty());
    EXPECT_NEAR(particles->particles.front().position.x, 0.0F, 1.0F);
    EXPECT_NEAR(particles->particles.front().position.y, 4.0F, 2.0F);

    engine.Shutdown();
}

TEST(EngineCoreTest, ParticleEmitter_EmittingOffDoesNotAutoEmit) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Core::EngineServices& services = engine.GetServices();
    Scene::IEntityManager& entityManager = services.GetEntityManager();
    Scene::ISceneGraph& sceneGraph = services.GetSceneGraph();

    const Scene::Entity entity = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, entity, Scene::TransformComponent{});
    Scene::ParticleEmitterComponent emitter{};
    emitter.maxParticles = 32U;
    emitter.emitting = false;
    emitter.emissionRate = 1'000'000.0F;
    entityManager.AddComponent<Scene::ParticleEmitterComponent>(entity, emitter);

    engine.TickFrame();
    ASSERT_EQ(engine.GetParticleRuntimeSnapshot().instances.size(), 1U);
    EXPECT_EQ(engine.GetParticleRuntimeSnapshot().instances.front().liveParticles, 0U);

    engine.Shutdown();
}

TEST(EngineCoreTest, TickMode_GatesParticleEmittersAgainstThePausedState) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Core::EngineServices& services = engine.GetServices();
    Scene::IEntityManager& entityManager = services.GetEntityManager();
    Scene::ISceneGraph& sceneGraph = services.GetSceneGraph();

    const auto makeEmitter = [&](const std::optional<Scene::TickMode> mode) {
        const Scene::Entity entity = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, entity, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::ParticleEmitterComponent>(entity,
                                                                           Scene::ParticleEmitterComponent{8U});
        if (mode.has_value()) {
            Scene::ProcessComponent process{};
            process.mode = *mode;
            entityManager.AddComponent<Scene::ProcessComponent>(entity, process);
        }
        return entity;
    };
    const Scene::Entity pausable = makeEmitter(std::nullopt); // No component: the default.
    const Scene::Entity always = makeEmitter(Scene::TickMode::Always);
    const Scene::Entity whenPaused = makeEmitter(Scene::TickMode::PausedOnly);
    const Scene::Entity disabled = makeEmitter(Scene::TickMode::Never);

    const auto isEnabled = [&engine](const Scene::Entity entity) {
        for (const Scene::ParticleRuntimeInstanceSnapshot& instance :
             engine.GetParticleRuntimeSnapshot().instances) {
            if (instance.entity == entity) {
                return instance.enabled;
            }
        }
        ADD_FAILURE() << "emitter has no runtime instance";
        return false;
    };

    engine.TickFrame();
    EXPECT_TRUE(isEnabled(pausable));
    EXPECT_TRUE(isEnabled(always));
    EXPECT_FALSE(isEnabled(whenPaused));
    EXPECT_FALSE(isEnabled(disabled));

    // Pausing flips the two pause-sensitive modes and leaves the other two where they were.
    ASSERT_TRUE(engine.SetSimulationExecutionMode(SimulationExecutionMode::Paused));
    engine.TickFrame();
    EXPECT_FALSE(isEnabled(pausable));
    EXPECT_TRUE(isEnabled(always));
    EXPECT_TRUE(isEnabled(whenPaused));
    EXPECT_FALSE(isEnabled(disabled));

    engine.Shutdown();
}

TEST(EngineCoreTest, TickMode_NeverInheritsToAChildWithoutTheComponent) {
    // The inheritance the scene-graph query exists for: the child carries no ProcessComponent,
    // and must still stop because its parent was disabled.
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Core::EngineServices& services = engine.GetServices();
    Scene::IEntityManager& entityManager = services.GetEntityManager();
    Scene::ISceneGraph& sceneGraph = services.GetSceneGraph();

    const Scene::Entity parent = entityManager.CreateEntity();
    const Scene::Entity child = entityManager.CreateEntity();
    for (const Scene::Entity entity : {parent, child}) {
        sceneGraph.AttachTransform(entityManager, entity, Scene::TransformComponent{});
    }
    sceneGraph.SetParent(entityManager, child, parent);
    Scene::ProcessComponent disabled{};
    disabled.mode = Scene::TickMode::Never;
    entityManager.AddComponent<Scene::ProcessComponent>(parent, disabled);
    entityManager.AddComponent<Scene::ParticleEmitterComponent>(child, Scene::ParticleEmitterComponent{8U});

    engine.TickFrame();
    const Scene::ParticleRuntimeSnapshot snapshot = engine.GetParticleRuntimeSnapshot();
    ASSERT_EQ(snapshot.instances.size(), 1U);
    EXPECT_FALSE(snapshot.instances.front().enabled);

    engine.Shutdown();
}

TEST(EngineCoreTest, TickMode_ANeverTickedProjectileDoesNotAdvanceOnAFixedStep) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Core::EngineServices& services = engine.GetServices();
    Scene::IEntityManager& entityManager = services.GetEntityManager();
    Scene::ISceneGraph& sceneGraph = services.GetSceneGraph();

    const auto makeProjectile = [&](const Scene::TickMode mode) {
        const Scene::Entity entity = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, entity, Scene::TransformComponent{});
        Scene::Projectile3DComponent projectile{};
        projectile.velocity = Math::Vector3{10.0F, 0.0F, 0.0F};
        entityManager.AddComponent<Scene::Projectile3DComponent>(entity, projectile);
        Scene::ProcessComponent process{};
        process.mode = mode;
        entityManager.AddComponent<Scene::ProcessComponent>(entity, process);
        return entity;
    };
    const Scene::Entity moving = makeProjectile(Scene::TickMode::Running);
    const Scene::Entity frozen = makeProjectile(Scene::TickMode::Never);

    // One frame so the scene graph resolves the modes, then one explicitly requested fixed step:
    // a single step is the simulation advancing, so Running moves even though play is paused.
    engine.TickFrame();
    ASSERT_TRUE(engine.SetSimulationExecutionMode(SimulationExecutionMode::Paused));
    const float movingBefore = entityManager.GetComponent<Scene::TransformComponent>(moving).localPosition.x;
    const float frozenBefore = entityManager.GetComponent<Scene::TransformComponent>(frozen).localPosition.x;
    ASSERT_TRUE(engine.RequestSingleSimulationStep());
    engine.TickFrame();

    EXPECT_GT(entityManager.GetComponent<Scene::TransformComponent>(moving).localPosition.x, movingBefore);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::TransformComponent>(frozen).localPosition.x,
                    frozenBefore);

    engine.Shutdown();
}

TEST(EngineCoreTest, AutoTranslate_ALabelInheritsItsMenusOptOutWithoutAComponent) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Core::EngineServices& services = engine.GetServices();
    Scene::IEntityManager& entityManager = services.GetEntityManager();
    Scene::ISceneGraph& sceneGraph = services.GetSceneGraph();

    Localization::StringTable filipino{Localization::Locale{"fil", ""}};
    ASSERT_TRUE(filipino.SetTranslation("Play", "Maglaro"));
    ASSERT_TRUE(engine.GetLocalizationService().AddStringTable(std::move(filipino)));
    engine.GetLocalizationService().SetActiveLocale(Localization::Locale{"fil", ""});

    // One menu that opts out; one label under it carrying no Auto Translate component of its own.
    const Scene::Entity menu = entityManager.CreateEntity();
    const Scene::Entity label = entityManager.CreateEntity();
    for (const Scene::Entity entity : {menu, label}) {
        sceneGraph.AttachTransform(entityManager, entity, Scene::TransformComponent{});
    }
    sceneGraph.SetParent(entityManager, label, menu);
    Scene::AutoTranslateComponent optOut{};
    optOut.mode = Scene::LocalizeMode::Literal;
    entityManager.AddComponent<Scene::AutoTranslateComponent>(menu, optOut);
    Scene::UITextComponent text{};
    text.text = "Play";
    entityManager.AddComponent<Scene::UITextComponent>(label, text);

    const auto glyphCount = [&engine] {
        const UI::UIDrawBatch& batch = engine.GetUIRuntime().GetDrawBatch();
        return std::count_if(batch.quads.cbegin(), batch.quads.cend(), [](const UI::UIQuad& quad) {
            return quad.kind == UI::UIDrawItemKind::Glyph;
        });
    };

    engine.TickFrame();
    EXPECT_EQ(glyphCount(), 4) << "the label inherits Disabled, so it draws \"Play\" untranslated";

    // Let the menu translate again, and the same label - still without a component - follows.
    entityManager.GetComponent<Scene::AutoTranslateComponent>(menu).mode = Scene::LocalizeMode::Localized;
    engine.TickFrame();
    EXPECT_EQ(glyphCount(), 7) << "\"Maglaro\"";

    engine.Shutdown();
}

TEST(EngineCoreTest, ThreadGroup_ASubThreadEmitterSimulatesIdenticallyToItsMainThreadTwin) {
    // End to end through real frames: the engine resolves the thread group, marks the emitter
    // worker-eligible, and hands the simulate its thread pool. The twin never leaves the main
    // thread. Same emission, same frames - the particles must come out the same.
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Core::EngineServices& services = engine.GetServices();
    Scene::IEntityManager& entityManager = services.GetEntityManager();
    Scene::ISceneGraph& sceneGraph = services.GetSceneGraph();

    const auto makeEmitter = [&](const Scene::ThreadGroupMode mode) {
        const Scene::Entity entity = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, entity, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::ParticleEmitterComponent>(entity,
                                                                           Scene::ParticleEmitterComponent{32U});
        Scene::ThreadGroupComponent threadGroup{};
        threadGroup.mode = mode;
        entityManager.AddComponent<Scene::ThreadGroupComponent>(entity, threadGroup);
        return entity;
    };
    const Scene::Entity onWorker = makeEmitter(Scene::ThreadGroupMode::SubThread);
    const Scene::Entity onMain = makeEmitter(Scene::ThreadGroupMode::MainThread);

    engine.TickFrame(); // Resolve modes and attach both runtime instances.
    // The engine routed each emitter by its resolved mode. Without this check the comparison below
    // would pass even if nothing ever left the main thread, because the serial path agrees too.
    for (const Scene::ParticleRuntimeInstanceSnapshot& instance : engine.GetParticleRuntimeSnapshot().instances) {
        EXPECT_EQ(instance.workerEligible, instance.entity == onWorker) << "emitter " << instance.entity.index;
    }
    Scene::IParticleRuntime& particles = services.GetParticleRuntime();
    const Scene::ParticleEmission emission{16U, Math::Vector3{1.0F, 2.0F, 3.0F},
                                              Math::Vector3{0.5F, 4.0F, -1.0F}, 5.0F};
    ASSERT_TRUE(particles.EmitDetailed(onWorker, emission).IsAccepted());
    ASSERT_TRUE(particles.EmitDetailed(onMain, emission).IsAccepted());

    for (int frame = 0; frame < 10; ++frame) {
        engine.TickFrame();
    }

    const std::optional<Scene::ParticleStateSnapshot> worker = particles.GetParticleSnapshot(onWorker);
    const std::optional<Scene::ParticleStateSnapshot> main = particles.GetParticleSnapshot(onMain);
    ASSERT_TRUE(worker.has_value());
    ASSERT_TRUE(main.has_value());
    ASSERT_FALSE(worker->particles.empty());
    EXPECT_EQ(worker->particles, main->particles);
    // And they actually moved, so the comparison is not two untouched emissions agreeing.
    EXPECT_NE(worker->particles.front().position, emission.position);

    engine.Shutdown();
}

TEST(EngineCoreTest, Run_BoundedFrames_ReachesShutdownWithCorrectFrameCount) {
    EngineCore engine(MakeTestConfig());
    const int exitCode = engine.Run(10);

    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(engine.GetState(), EngineState::Shutdown);
    EXPECT_EQ(engine.GetFrameStats().frameNumber, 10U);
}

TEST(EngineCoreTest, Run_ZeroFrames_StillInitsAndShutsDownCleanly) {
    EngineCore engine(MakeTestConfig());
    const int exitCode = engine.Run(0);

    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(engine.GetState(), EngineState::Shutdown);
    EXPECT_EQ(engine.GetFrameStats().frameNumber, 0U);
}

TEST(EngineCoreTest, RequestQuit_BeforeRun_PreventsAnyFramesFromRunning) {
    EngineCore engine(MakeTestConfig());
    engine.RequestQuit();

    const int exitCode = engine.Run(50);

    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(engine.GetState(), EngineState::Shutdown);
    EXPECT_EQ(engine.GetFrameStats().frameNumber, 0U);
}

TEST(EngineCoreTest, Shutdown_ClearsLoggerActiveInstance) {
    EngineCore engine(MakeTestConfig());
    engine.Run(1);

    EXPECT_EQ(Debug::Logger::GetActiveInstance(), nullptr);
}

TEST(EngineCoreTest, Timer_TotalTimeStrictlyIncreasesAcrossFrames) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    engine.TickFrame();
    const double firstTotal = engine.GetFrameStats().totalTimeSeconds;
    engine.TickFrame();
    const double secondTotal = engine.GetFrameStats().totalTimeSeconds;

    EXPECT_GT(secondTotal, firstTotal);
    engine.Shutdown();
}

TEST(EngineCoreTest, QueuedEvent_DeliveredDuringTickFrame) {
    struct PingEvent {
        int value = 0;
    };

    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    int received = -1;
    engine.GetServices().GetEventSystem().Subscribe<PingEvent>(
        [&received](const PingEvent& event) { received = event.value; });
    engine.GetServices().GetEventSystem().QueueEvent(PingEvent{99});

    ASSERT_EQ(received, -1);
    engine.TickFrame();
    EXPECT_EQ(received, 99);

    engine.Shutdown();
}

TEST(EngineCoreTest, AreaOverlapLifecycle_QueuesEnteredAndExitedEvents) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    auto& services = engine.GetServices();
    auto& entityManager = services.GetEntityManager();
    auto& sceneGraph = services.GetSceneGraph();

    const Scene::Entity area = entityManager.CreateEntity();
    Scene::TransformComponent areaTransform;
    areaTransform.localPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, area, areaTransform);
    entityManager.AddComponent<Scene::AreaComponent>(
        area, Scene::AreaComponent{Math::Vector3{1.0F, 1.0F, 1.0F}, 1U, 0xFFFFFFFFU});

    const Scene::Entity collider = entityManager.CreateEntity();
    Scene::TransformComponent colliderTransform;
    colliderTransform.localPosition = Math::Vector3{0.5F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, collider, colliderTransform);
    entityManager.AddComponent<Scene::ColliderComponent>(
        collider, Scene::ColliderComponent{Math::Vector3{1.0F, 1.0F, 1.0F}, 1U, 0xFFFFFFFFU});
    sceneGraph.Update(entityManager);

    std::vector<Physics::AreaOverlapPair> entered;
    std::vector<Physics::AreaOverlapPair> exited;
    services.GetEventSystem().Subscribe<Physics::AreaOverlapEnteredEvent>(
        [&entered](const Physics::AreaOverlapEnteredEvent& event) { entered.push_back(event.pair); });
    services.GetEventSystem().Subscribe<Physics::AreaOverlapExitedEvent>(
        [&exited](const Physics::AreaOverlapExitedEvent& event) { exited.push_back(event.pair); });

    engine.TickFrame();
    EXPECT_TRUE(entered.empty());
    EXPECT_TRUE(exited.empty());

    engine.TickFrame();
    ASSERT_EQ(entered.size(), 1U);
    EXPECT_EQ(entered.front().area, area);
    EXPECT_EQ(entered.front().other, collider);
    EXPECT_TRUE(exited.empty());

    entityManager.DestroyEntity(collider);
    engine.TickFrame();
    EXPECT_TRUE(exited.empty());

    engine.TickFrame();
    ASSERT_EQ(exited.size(), 1U);
    EXPECT_EQ(exited.front().area, area);
    EXPECT_EQ(exited.front().other, collider);

    engine.Shutdown();
}

TEST(EngineCoreTest, Decal3DObject_LifetimeRunsOutOnSimulatedTimeAndReportsTheEdgeOnce) {
    // The same 1 kHz fixed-update / short real-sleep discipline the physics end-to-end cases use,
    // because a decal ages on the SIMULATED clock: the test has to actually run simulation time for
    // a lifetime to be spent, and a test that ran no fixed step would prove nothing about it.
    EngineConfig config = MakeTestConfig();
    config.fixedUpdateFps = 1000.0;
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    auto& services = engine.GetServices();
    auto& entityManager = services.GetEntityManager();

    const Scene::Entity fading = entityManager.CreateEntity();
    Scene::Decal3DComponent decal;
    decal.materialAssetPath = "materials/scorch.uemat";
    decal.lifetime = 0.02F; // 20 ms: a few frames of simulated time
    entityManager.AddComponent<Scene::Decal3DComponent>(fading, decal);

    // A permanent decal beside it, which must outlive every frame of this test.
    const Scene::Entity permanent = entityManager.CreateEntity();
    entityManager.AddComponent<Scene::Decal3DComponent>(permanent, Scene::Decal3DComponent{});

    std::vector<Scene::Decal3DExpiredEvent> expired;
    services.GetEventSystem().Subscribe<Scene::Decal3DExpiredEvent>(
        [&expired](const Scene::Decal3DExpiredEvent& event) { expired.push_back(event); });

    for (int frame = 0; frame < 30; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        engine.TickFrame();
    }

    const Scene::Decal3DComponent& faded =
        entityManager.GetComponent<Scene::Decal3DComponent>(fading);
    EXPECT_TRUE(faded.expired) << "20 ms of simulated time has certainly passed";
    EXPECT_FLOAT_EQ(faded.remainingLifetime, 0.0F);
    EXPECT_FALSE(IsDecal3DPainting(faded)) << "an expired decal stops painting";
    EXPECT_FALSE(entityManager.GetComponent<Scene::Decal3DComponent>(permanent).expired)
        << "lifetime 0 is permanent, whatever the clock says";

    ASSERT_EQ(expired.size(), 1U) << "one edge per lifetime, not one per frame";
    EXPECT_EQ(expired[0].decal, fading);
    EXPECT_EQ(expired[0].materialAssetPath, "materials/scorch.uemat")
        << "the material travels with the event: a pooled system may not get a second lookup";

    engine.Shutdown();
}

TEST(EngineCoreTest, NavigationRegion_BakesFromItsVolumeAndDrivesASeekerOnTheFixedStep) {
    // EngineCore::SyncNavigation() is the wiring that makes NavMeshVolume3D and NavSeeker3D
    // real: before it, both were authored data with nothing reading them. This runs the whole path
    // end-to-end on the simulated clock - a region rasterized from its own world volume through the
    // engine's real raycast system, and an agent stepped against the mesh that came out - because a
    // unit test can prove the pieces and only this can prove the engine drives them.
    EngineConfig config = MakeTestConfig();
    config.fixedUpdateFps = 1000.0;
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    // Static ground: a box under the origin, top surface at y = 0.
    const Scene::Entity ground = entityManager.CreateEntity();
    Scene::TransformComponent groundTransform;
    groundTransform.localPosition = Math::Vector3{0.0F, -0.5F, 0.0F};
    sceneGraph.AttachTransform(entityManager, ground, groundTransform);
    entityManager.AddComponent<Scene::ColliderComponent>(
        ground, Scene::ColliderComponent{Math::Vector3{10.0F, 0.5F, 10.0F}});

    // The region: a volume over the floor with 3 m of headroom under its own roof.
    const Scene::Entity region = entityManager.CreateEntity();
    Scene::TransformComponent regionTransform;
    regionTransform.localPosition = Math::Vector3{0.0F, 0.5F, 0.0F};
    sceneGraph.AttachTransform(entityManager, region, regionTransform);
    Scene::NavMeshVolume3DComponent regionComponent;
    regionComponent.boundsHalfExtents = Math::Vector3{8.0F, 2.5F, 8.0F};
    entityManager.AddComponent<Scene::NavMeshVolume3DComponent>(region, regionComponent);

    // The agent: standing on the floor, asked to walk 5 m along +X. Nothing moves its body here -
    // what the engine has to prove is that it publishes a velocity for whatever would.
    const Scene::Entity agent = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, agent, Scene::TransformComponent{});
    Scene::NavSeeker3DComponent seeker;
    seeker.targetPosition = Math::Vector3{5.0F, 0.0F, 0.0F};
    // No velocity ramp: the ramp is the module suite's own subject, and here it would only mean
    // simulating the four tenths of a second it takes to walk the velocity from +4 to -4.
    seeker.acceleration = 0.0F;
    entityManager.AddComponent<Scene::NavSeeker3DComponent>(agent, seeker);

    for (int frame = 0; frame < 40; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        engine.TickFrame();
    }

    const Scene::NavSeeker3DComponent& walking =
        entityManager.GetComponent<Scene::NavSeeker3DComponent>(agent);
    EXPECT_EQ(walking.pathStatus, Scene::NavigationAgentPathStatus::Following)
        << "the region baked, the agent found a route on it and is walking that route";
    EXPECT_GT(walking.desiredVelocity.x, 0.0F) << "the target is straight ahead";
    EXPECT_NEAR(walking.desiredVelocity.y, 0.0F, 1e-3F) << "steering stays in the plane it walks on";
    EXPECT_NEAR(walking.nextPathPosition.x, 5.0F, 0.5F);
    EXPECT_FALSE(walking.targetReached) << "the body has not been moved to the target, only asked to";

    // A new target is a new order: the component is the only thing that has to change, and the next
    // steps route the other way.
    entityManager.GetComponent<Scene::NavSeeker3DComponent>(agent).targetPosition =
        Math::Vector3{-5.0F, 0.0F, 0.0F};
    for (int frame = 0; frame < 10; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        engine.TickFrame();
    }
    const Scene::NavSeeker3DComponent& returning =
        entityManager.GetComponent<Scene::NavSeeker3DComponent>(agent);
    EXPECT_LT(returning.desiredVelocity.x, 0.0F) << "the agent turned around";
    EXPECT_NEAR(returning.nextPathPosition.x, -5.0F, 0.5F);

    engine.Shutdown();
}

TEST(EngineCoreTest, CollisionLifecycle_UpdatesReportBeforeScriptTickEachFrame) {
    // Contrasts directly against AreaOverlapLifecycle_QueuesEnteredAndExitedEvents above: that
    // event-queued path only surfaces a transition on the TickFrame() call *after* the one
    // where the overlap actually began, since QueueEvent()'d events aren't drained until the next
    // frame's Update(). SyncCollisionLifecycle() has no such queue - it's a poll-based binding
    // updated synchronously before SyncScriptRuntime() runs the same frame - so this test proves
    // the very same TickFrame() call that makes two colliders overlap already reports the
    // transition, with zero added frame of latency.
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    auto& services = engine.GetServices();
    auto& entityManager = services.GetEntityManager();
    auto& sceneGraph = services.GetSceneGraph();

    const Scene::Entity bodyA = entityManager.CreateEntity();
    Scene::TransformComponent transformA;
    transformA.localPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, bodyA, transformA);
    entityManager.AddComponent<Scene::ColliderComponent>(bodyA, Scene::ColliderComponent{});

    const Scene::Entity bodyB = entityManager.CreateEntity();
    Scene::TransformComponent transformB;
    transformB.localPosition = Math::Vector3{10.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, bodyB, transformB);
    entityManager.AddComponent<Scene::ColliderComponent>(bodyB, Scene::ColliderComponent{});

    engine.TickFrame();
    EXPECT_TRUE(engine.GetLastCollisionLifecycleReport().transitions.empty());

    transformB.localPosition = Math::Vector3{0.2F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, bodyB, transformB);
    engine.TickFrame();

    const Physics::CollisionLifecycleReport& report = engine.GetLastCollisionLifecycleReport();
    ASSERT_EQ(report.transitions.size(), 1U);
    EXPECT_EQ(report.transitions.front().kind, Physics::CollisionTransitionKind::Entered);

    engine.Shutdown();
}

TEST(EngineCoreTest, FrameStats_PopulatedAfterFrames) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    engine.TickFrame();
    engine.TickFrame();

    const FrameStats& stats = engine.GetFrameStats();
    EXPECT_EQ(stats.frameNumber, 2U);
    EXPECT_GE(stats.frameTimeSeconds, 0.0);
    EXPECT_GE(stats.deltaTimeSeconds, 0.0);
    EXPECT_GT(stats.fps, 0.0);

    engine.Shutdown();
}

TEST(EngineCoreTest, LoopStages_ExecuteInDocumentedOrder) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    engine.GetServices().GetLogger().AddSink(std::move(memorySink));

    engine.TickFrame();

    std::vector<std::string> stageOrder;
    for (const Debug::LogMessage& message : memorySinkPtr->GetMessages()) {
        if (message.message.starts_with("BeginFrame")) {
            stageOrder.emplace_back("BeginFrame");
        } else if (message.message.starts_with("Update:")) {
            stageOrder.emplace_back("Update");
        } else if (message.message.starts_with("LateUpdate:")) {
            stageOrder.emplace_back("LateUpdate");
        } else if (message.message.starts_with("Render")) {
            stageOrder.emplace_back("Render");
        } else if (message.message.starts_with("EndFrame")) {
            stageOrder.emplace_back("EndFrame");
        }
    }

    const std::vector<std::string> expectedOrder{"BeginFrame", "Update", "LateUpdate", "Render", "EndFrame"};
    EXPECT_EQ(stageOrder, expectedOrder);

    engine.Shutdown();
}

TEST(EngineCoreTest, MemoryManager_ReachableAndFunctionalAfterInit) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Memory::IMemoryManager& memoryManager = engine.GetServices().GetMemoryManager();
    Memory::IAllocator& allocator = memoryManager.GetDefaultAllocator();

    void* const pointer = allocator.Allocate(32, 8, __FILE__, __LINE__);
    ASSERT_NE(pointer, nullptr);
    EXPECT_TRUE(memoryManager.HasLeaks());
    allocator.Deallocate(pointer);
    EXPECT_FALSE(memoryManager.HasLeaks());

    engine.Shutdown();
}

TEST(EngineCoreTest, NormalRun_ReportsNoLeaks) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    engine.TickFrame();
    engine.TickFrame();

    // Nothing allocates through MemoryManager on Increment 1's behalf yet, so a normal run
    // must report zero leaks — this also implicitly exercises the debug-only shutdown leak
    // assertion on the happy path (zero leaks, assertion never fires).
    EXPECT_FALSE(engine.GetServices().GetMemoryManager().HasLeaks());

    engine.Shutdown();
}

TEST(EngineCoreTest, ThreadPool_ReachableAndFunctionalAfterInit) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Threading::IThreadPool& threadPool = engine.GetServices().GetThreadPool();
    EXPECT_GE(threadPool.GetWorkerCount(), 1U);

    Threading::JobCounter counter;
    bool jobRan = false;
    threadPool.Submit([&jobRan] { jobRan = true; }, counter);
    counter.Wait();
    EXPECT_TRUE(jobRan);

    engine.Shutdown();
}

TEST(EngineCoreTest, CommandLineAndConfigManager_ReachableAndFunctionalAfterInit) {
    EngineConfig config = MakeTestConfig();
    config.commandLineArgs = {"--server"};
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    EXPECT_TRUE(engine.GetServices().GetCommandLine().HasFlag("server"));

    Config::IConfigManager& configManager = engine.GetServices().GetConfigManager();
    configManager.SetString("editor.theme", "dark");
    EXPECT_EQ(configManager.GetString("editor.theme", ""), "dark");

    engine.Shutdown();
}

TEST(EngineCoreTest, EntityManagerAndSceneGraph_ReachableAndFunctionalAfterInit) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::TransformComponent local;
    local.localPosition = Math::Vector3{1.0F, 2.0F, 3.0F};
    sceneGraph.AttachTransform(entityManager, entity, local);

    engine.TickFrame();

    const Scene::WorldTransformComponent& world =
        entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
    EXPECT_FALSE(world.dirty);
    EXPECT_EQ(world.worldPosition, local.localPosition);

    engine.Shutdown();
}

TEST(EngineCoreTest, AssetDatabaseSceneSerializerPrefabSystem_ReachableAndRoundTripAfterInit) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();
    Asset::IAssetDatabase& assetDatabase = engine.GetServices().GetAssetDatabase();
    Scene::IPrefabSystem& prefabSystem = engine.GetServices().GetPrefabSystem();

    const Scene::Entity source = entityManager.CreateEntity();
    entityManager.AddComponent<Scene::MeshComponent>(
        source, Scene::MeshComponent{Asset::AssetGuid{51}, Asset::AssetGuid{52}});

    const std::filesystem::path prefabPath = "uve_engine_core_tests.uvprefab";
    std::filesystem::remove(prefabPath);
    const Asset::AssetGuid guid = prefabSystem.SavePrefab(entityManager, assetDatabase, source, prefabPath);
    ASSERT_NE(guid, Asset::kInvalidAssetGuid);

    const Scene::Entity instance =
        prefabSystem.Instantiate(entityManager, sceneGraph, assetDatabase, guid, Scene::kInvalidEntity);
    ASSERT_NE(instance, Scene::kInvalidEntity);
    EXPECT_EQ(entityManager.GetComponent<Scene::MeshComponent>(instance).meshGuid, Asset::AssetGuid{51});

    std::filesystem::remove(prefabPath);
    std::filesystem::remove(MakeTestConfig().assetDatabaseFilePath);

    engine.Shutdown();
}

TEST(EngineCoreTest, AssetManagerImporterHotReloadBundle_ReachableAndRoundTripAfterInit) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Asset::IAssetDatabase& assetDatabase = engine.GetServices().GetAssetDatabase();
    Asset::IAssetImporter& importer = engine.GetServices().GetAssetImporter();
    const Asset::AssetImportSourceClassification rawModelClassification =
        importer.ClassifySource("engine_core_tests_character.dae");
    EXPECT_EQ(rawModelClassification.kind, Asset::AssetImportSourceKind::RawModel);
    EXPECT_EQ(rawModelClassification.normalizedExtension, "dae");
    EXPECT_FALSE(rawModelClassification.importerRegistered);
    EXPECT_TRUE(rawModelClassification.requiresFormatSpecificParser);
    EXPECT_EQ(rawModelClassification.diagnostic, "format-specific parser is not registered");
    Asset::IAssetManager& assetManager = engine.GetServices().GetAssetManager();
    static_cast<void>(engine.GetServices().GetHotReload());
    static_cast<void>(engine.GetServices().GetAssetBundle());

    const std::filesystem::path sourcePath = "uve_engine_core_tests_source.txt";
    const std::filesystem::path destinationPath = "uve_engine_core_tests_dest.txt";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    {
        std::ofstream file(sourcePath);
        file << "engine core asset pipeline round trip";
    }

    const Asset::AssetGuid guid = importer.Import(sourcePath, destinationPath, assetDatabase);
    ASSERT_NE(guid, Asset::kInvalidAssetGuid);

    assetManager.RegisterLoader<Asset::BlobAsset>(
        [](const std::filesystem::path& path, Asset::BlobAsset& outValue) {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                return false;
            }
            const std::vector<char> raw((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            outValue.assign(reinterpret_cast<const std::byte*>(raw.data()),
                             reinterpret_cast<const std::byte*>(raw.data()) + raw.size());
            return true;
        });

    {
        // Scoped so the handle releases its reference (and is destroyed) before engine.Shutdown()
        // tears down the AssetManager it points into — a handle must never outlive the
        // manager that produced it, exactly like an IEntityManager& argument never outliving
        // its EntityManager.
        const Asset::AssetHandle<Asset::BlobAsset> handle =
            assetManager.Load<Asset::BlobAsset>(guid, assetDatabase);

        // Ticking a couple of frames exercises HotReload::Poll()/AssetManager::
        // CollectGarbage() running from within EngineCore::Update() while a load is in
        // flight, proving neither crashes nor prematurely collects the still-referenced asset.
        engine.TickFrame();
        engine.TickFrame();

        bool ready = false;
        for (int poll = 0; poll < 200000 && !ready; ++poll) {
            ready = handle.IsReady() || handle.HasFailed();
            if (!ready) {
                std::this_thread::yield();
            }
        }
        ASSERT_TRUE(ready);
        ASSERT_TRUE(handle.IsReady());
        const Asset::BlobAsset* const blob = handle.TryGet();
        ASSERT_NE(blob, nullptr);
        const std::string content(reinterpret_cast<const char*>(blob->data()), blob->size());
        EXPECT_EQ(content, "engine core asset pipeline round trip");
    }

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    engine.Shutdown();
}

TEST(EngineCoreTest, TypedUVEEnvelopeImporters_ComposedAndReachableAfterInit) {
    EngineConfig config = MakeTestConfig();
    const std::filesystem::path root = ::UVE::Tests::ScratchRoot();
    config.assetDatabaseFilePath = root / "uve_engine_core_typed_envelope_tests.uvassetdb";

    constexpr std::array<std::string_view, 4> kTypedEnvelopeExtensions = {
        ".uvmodel", ".uvtex", ".uvshader", ".uvmat"};
    struct Cleanup final {
        std::filesystem::path database;
        std::array<std::filesystem::path, 4> sources;
        std::array<std::filesystem::path, 4> destinations;
        ~Cleanup() {
            std::filesystem::remove(database);
            for (const auto& path : sources) {
                std::filesystem::remove(path);
            }
            for (const auto& path : destinations) {
                std::filesystem::remove(path);
            }
        }
    } cleanup{config.assetDatabaseFilePath, {}, {}};

    for (std::size_t index = 0; index < kTypedEnvelopeExtensions.size(); ++index) {
        const std::string suffix(kTypedEnvelopeExtensions[index]);
        cleanup.sources[index] = root / ("uve_engine_core_typed_source_" + std::to_string(index) + suffix);
        cleanup.destinations[index] = root / ("uve_engine_core_typed_dest_" + std::to_string(index) + suffix);
        std::ofstream source(cleanup.sources[index], std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(source.is_open());
        source << "typed envelope composition proof";
    }

    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Asset::IAssetDatabase& assetDatabase = engine.GetServices().GetAssetDatabase();
    Asset::IAssetImporter& importer = engine.GetServices().GetAssetImporter();
    for (std::size_t index = 0; index < kTypedEnvelopeExtensions.size(); ++index) {
        const Asset::AssetGuid guid =
            importer.Import(cleanup.sources[index], cleanup.destinations[index], assetDatabase);
        ASSERT_NE(guid, Asset::kInvalidAssetGuid) << kTypedEnvelopeExtensions[index];
        EXPECT_EQ(assetDatabase.Resolve(guid), cleanup.destinations[index]);
    }

    engine.Shutdown();
}

TEST(EngineCoreTest, AudioAssetLoader_RegisteredAndReachableThroughBuiltInPipeline) {
    EngineConfig config = MakeTestConfig();
    const std::filesystem::path root = ::UVE::Tests::ScratchRoot();
    config.assetDatabaseFilePath = root / "uve_engine_core_audio_asset_tests.uvassetdb";
    const std::filesystem::path sourcePath = root / "uve_engine_core_audio_asset_tests.wav";
    const std::filesystem::path destinationPath = root / "uve_engine_core_audio_asset_tests.uvaudio";
    std::filesystem::remove(config.assetDatabaseFilePath);
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    struct Cleanup final {
        std::filesystem::path database;
        std::filesystem::path source;
        std::filesystem::path destination;
        ~Cleanup() {
            std::filesystem::remove(database);
            std::filesystem::remove(source);
            std::filesystem::remove(destination);
        }
    } cleanup{config.assetDatabaseFilePath, sourcePath, destinationPath};
    const std::array<unsigned char, 48U> wavBytes{
        'R', 'I', 'F', 'F', 40U, 0U, 0U, 0U, 'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 16U, 0U, 0U, 0U, 1U, 0U, 1U, 0U,
        0x80U, 0xBBU, 0U, 0U, 0x00U, 0x77U, 0x01U, 0U, 2U, 0U, 16U, 0U,
        'd', 'a', 't', 'a', 4U, 0U, 0U, 0U, 0U, 0U, 0xFFU, 0x7FU};
    {
        std::ofstream output(sourcePath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output.write(reinterpret_cast<const char*>(wavBytes.data()),
                     static_cast<std::streamsize>(wavBytes.size()));
    }

    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Asset::IAssetDatabase& assetDatabase = engine.GetServices().GetAssetDatabase();
    Asset::IAssetImporter& importer = engine.GetServices().GetAssetImporter();
    Asset::IAssetManager& assetManager = engine.GetServices().GetAssetManager();
    const Asset::AssetGuid guid = importer.Import(sourcePath, destinationPath, assetDatabase);
    ASSERT_NE(guid, Asset::kInvalidAssetGuid);
    {
        const Asset::AssetHandle<Asset::AudioAsset> handle =
            assetManager.Load<Asset::AudioAsset>(guid, assetDatabase);
        bool terminal = false;
        for (int iteration = 0; iteration < 200000 && !terminal; ++iteration) {
            terminal = handle.IsReady() || handle.HasFailed();
            if (!terminal) {
                std::this_thread::yield();
            }
        }
        ASSERT_TRUE(terminal);
        ASSERT_TRUE(handle.IsReady());
        const Asset::AudioAsset* const audio = handle.TryGet();
        ASSERT_NE(audio, nullptr);
        EXPECT_EQ(audio->channels, 1U);
        EXPECT_EQ(audio->sampleRate, 48000U);
        ASSERT_EQ(audio->samples.size(), 2U);
        EXPECT_FLOAT_EQ(audio->samples[0], 0.0F);
        EXPECT_NEAR(audio->samples[1], 0.9999695F, 1.0e-6F);
    }
    engine.Shutdown();
}
TEST(EngineCoreTest, AnimationAssetLoader_RegisteredAndReachableThroughBuiltInPipeline) {
    EngineConfig config = MakeTestConfig();
    const std::filesystem::path root = ::UVE::Tests::ScratchRoot();
    config.assetDatabaseFilePath = root / "uve_engine_core_animation_asset_tests.uvassetdb";
    const std::filesystem::path sourcePath = root / "uve_engine_core_animation_asset_tests_source.uvanim";
    const std::filesystem::path destinationPath = root / "uve_engine_core_animation_asset_tests_dest.uvanim";
    std::filesystem::remove(config.assetDatabaseFilePath);
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    struct Cleanup final {
        std::filesystem::path database;
        std::filesystem::path source;
        std::filesystem::path destination;
        ~Cleanup() {
            std::filesystem::remove(database);
            std::filesystem::remove(source);
            std::filesystem::remove(destination);
        }
    } cleanup{config.assetDatabaseFilePath, sourcePath, destinationPath};
    Asset::AnimationClipAsset sourceClip;
    sourceClip.clipId = "walk";
    sourceClip.durationSeconds = 1.0;
    sourceClip.samples = {Asset::AnimationAssetSample{
        0.0, Asset::AnimationAssetPose{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{},
                                           Math::Vector3{1.0F, 1.0F, 1.0F}}}};
    sourceClip.events = {Asset::AnimationAssetEvent{0.5, "footstep"}};
    ASSERT_TRUE(Asset::SaveAnimationClipAsset(sourceClip, sourcePath));

    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Asset::IAssetDatabase& assetDatabase = engine.GetServices().GetAssetDatabase();
    Asset::IAssetImporter& importer = engine.GetServices().GetAssetImporter();
    Asset::IAssetManager& assetManager = engine.GetServices().GetAssetManager();
    const Asset::AssetGuid guid = importer.Import(sourcePath, destinationPath, assetDatabase);
    ASSERT_NE(guid, Asset::kInvalidAssetGuid);
    {
        const Asset::AssetHandle<Asset::AnimationClipAsset> handle =
            assetManager.Load<Asset::AnimationClipAsset>(guid, assetDatabase);
        bool terminal = false;
        for (int iteration = 0; iteration < 200000 && !terminal; ++iteration) {
            terminal = handle.IsReady() || handle.HasFailed();
            if (!terminal) {
                std::this_thread::yield();
            }
        }
        ASSERT_TRUE(terminal);
        ASSERT_TRUE(handle.IsReady());
        const Asset::AnimationClipAsset* const clip = handle.TryGet();
        ASSERT_NE(clip, nullptr);
        EXPECT_EQ(clip->clipId, "walk");
        ASSERT_EQ(clip->samples.size(), 1U);
        ASSERT_EQ(clip->events.size(), 1U);
        EXPECT_EQ(clip->events.front().eventId, "footstep");
    }
    engine.Shutdown();
}
TEST(EngineCoreTest, DataTablePipeline_RegisteredAndReachableThroughServicesAfterInit) {
    EngineConfig config = MakeTestConfig();
    const std::filesystem::path root = ::UVE::Tests::ScratchRoot();
    config.assetDatabaseFilePath = root / "uve_engine_core_data_table_tests.uvassetdb";
    const std::filesystem::path sourcePath = root / "uve_engine_core_data_table_tests.csv";
    const std::filesystem::path destinationPath = root / "uve_engine_core_data_table_tests.uvtable";
    std::filesystem::remove(config.assetDatabaseFilePath);
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);

    struct Cleanup final {
        std::filesystem::path database;
        std::filesystem::path source;
        std::filesystem::path destination;
        ~Cleanup() {
            std::filesystem::remove(database);
            std::filesystem::remove(source);
            std::filesystem::remove(destination);
        }
    } cleanup{config.assetDatabaseFilePath, sourcePath, destinationPath};

    {
        std::ofstream output(sourcePath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output << "id,damage\npistol,25\n";
    }

    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Asset::IAssetDatabase& assetDatabase = engine.GetServices().GetAssetDatabase();
    Asset::IAssetImporter& importer = engine.GetServices().GetAssetImporter();
    Asset::IAssetManager& assetManager = engine.GetServices().GetAssetManager();

    Asset::DataTableImportSettings settings;
    settings.tableName = "weapons";
    settings.columns = {Asset::DataTableColumn{"damage", Asset::DataTableColumnType::Integer}};
    const Asset::AssetGuid guid = importer.Import(sourcePath, destinationPath, assetDatabase, settings);
    ASSERT_NE(guid, Asset::kInvalidAssetGuid);

    {
        const Asset::AssetHandle<Asset::DataTable> handle =
            assetManager.Load<Asset::DataTable>(guid, assetDatabase);
        bool terminal = false;
        for (int iteration = 0; iteration < 200000 && !terminal; ++iteration) {
            terminal = handle.IsReady() || handle.HasFailed();
            if (!terminal) {
                std::this_thread::yield();
            }
        }
        ASSERT_TRUE(terminal);
        ASSERT_TRUE(handle.IsReady());
        const Asset::DataTable* const table = handle.TryGet();
        ASSERT_NE(table, nullptr);
        const Asset::DataTableSnapshot snapshot = table->GetSnapshot();
        EXPECT_EQ(snapshot.name, "weapons");
        ASSERT_EQ(snapshot.rows.size(), 1U);
        ASSERT_EQ(snapshot.rows.front().values.size(), 1U);
        EXPECT_EQ(std::get<std::int64_t>(snapshot.rows.front().values.front()), 25);
    }

    engine.Shutdown();
}

TEST(EngineCoreTest, FileSystem_ReachableAndReadWriteRoundTripAfterInit) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Asset::IFileSystem& fileSystem = engine.GetServices().GetFileSystem();

    const std::filesystem::path mountDirectory = "uve_engine_core_tests_vfs_mount";
    std::filesystem::remove_all(mountDirectory);
    std::filesystem::create_directories(mountDirectory);
    fileSystem.MountDirectory("", mountDirectory, 0);

    const std::string text = "engine core vfs round trip";
    const auto* const textBytes = reinterpret_cast<const std::byte*>(text.data());
    const std::vector<std::byte> data(textBytes, textBytes + text.size());
    ASSERT_TRUE(fileSystem.WriteFile("notes.txt", data));

    const std::optional<std::vector<std::byte>> readBack = fileSystem.ReadFile("notes.txt");
    ASSERT_TRUE(readBack.has_value());
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(readBack->data()), readBack->size()), text);

    std::filesystem::remove_all(mountDirectory);
    engine.Shutdown();
}

namespace {

using ::UVE::Tests::TestGraph;
using PinType = Asset::BlueprintPinType;

/// A graph that counts: `readies` once at ready and `ticks` every frame.
[[nodiscard]] TestGraph MakeCounterGraph() {
    TestGraph graph;
    graph.Var("readies", PinType::Int, "0").Var("ticks", PinType::Int, "0");
    const std::uint32_t ready = graph.Node("event_ready");
    graph.Exec(ready, graph.AddToVariable("readies", 1).set);
    const std::uint32_t tick = graph.Node("event_tick");
    graph.Exec(tick, graph.AddToVariable("ticks", 1).set);
    return graph;
}

/// A graph that does not compile: it sets a variable it never declared.
[[nodiscard]] TestGraph MakeBrokenGraph() {
    TestGraph graph;
    const std::uint32_t tick = graph.Node("event_tick");
    graph.Exec(tick, graph.SetVariable("missing", "1"));
    return graph;
}

} // namespace

TEST(EngineCoreTest, BlueprintEntity_CompilesOnceRaisesReadyThenTicksEveryFrame) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Asset::IFileSystem& fileSystem = engine.GetServices().GetFileSystem();
    const std::filesystem::path mountDirectory = "uve_engine_core_tests_blueprint_mount";
    std::filesystem::remove_all(mountDirectory);
    std::filesystem::create_directories(mountDirectory);
    fileSystem.MountDirectory("", mountDirectory, 0);
    const auto write = [&](const char* path, const TestGraph& graph) {
        ASSERT_TRUE(fileSystem.WriteFile(path, graph.Bytes()));
    };
    write("counter.uvbp", MakeCounterGraph());
    write("broken.uvbp", MakeBrokenGraph());

    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    const Scene::Entity entity = entityManager.CreateEntity();
    entityManager.AddComponent<Scene::ScriptComponent>(entity, Scene::ScriptComponent{"counter.uvbp"});

    engine.TickFrame();
    engine.TickFrame();
    engine.TickFrame();
    Blueprint::ScriptInstance* const instance = engine.FindUVScriptInstance(entity);
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(instance->GetField("readies"), Blueprint::Value{std::int64_t{1}});
    EXPECT_EQ(instance->GetField("ticks"), Blueprint::Value{std::int64_t{3}});
    EXPECT_EQ(engine.GetActiveScriptInstanceCount(), 1U);

    // A graph that does not compile: no instance, and it is not retried every frame.
    entityManager.GetComponent<Scene::ScriptComponent>(entity).scriptAssetPath = "broken.uvbp";
    engine.TickFrame();
    engine.TickFrame();
    EXPECT_EQ(engine.FindUVScriptInstance(entity), nullptr);

    // Pointing back at a good Blueprint starts it fresh.
    entityManager.GetComponent<Scene::ScriptComponent>(entity).scriptAssetPath = "counter.uvbp";
    engine.TickFrame();
    ASSERT_NE(engine.FindUVScriptInstance(entity), nullptr);
    EXPECT_EQ(engine.FindUVScriptInstance(entity)->GetField("ticks"), Blueprint::Value{std::int64_t{1}});

    // Saving a changed graph restarts it within half a second - no path change needed. The same
    // goes for one that failed: fixing the file is enough.
    TestGraph version;
    version.Var("version", PinType::Int, "2");
    write("counter.uvbp", version);
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    engine.TickFrame();
    engine.TickFrame();
    ASSERT_NE(engine.FindUVScriptInstance(entity), nullptr);
    EXPECT_EQ(engine.FindUVScriptInstance(entity)->GetField("version"), Blueprint::Value{std::int64_t{2}});

    entityManager.GetComponent<Scene::ScriptComponent>(entity).scriptAssetPath = "broken.uvbp";
    engine.TickFrame();
    EXPECT_EQ(engine.FindUVScriptInstance(entity), nullptr);
    TestGraph fixed;
    fixed.Var("fixed", PinType::Bool, "true");
    write("broken.uvbp", fixed);
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    engine.TickFrame();
    engine.TickFrame();
    ASSERT_NE(engine.FindUVScriptInstance(entity), nullptr);
    EXPECT_EQ(engine.FindUVScriptInstance(entity)->GetField("fixed"), Blueprint::Value{true});

    entityManager.RemoveComponent<Scene::ScriptComponent>(entity);
    engine.TickFrame();
    EXPECT_EQ(engine.FindUVScriptInstance(entity), nullptr);

    // The object's own export values are in place before `ready` runs; changing them restarts it.
    TestGraph exports;
    exports.Var("speed", PinType::Float, "1.0", /*exposed=*/true).Var("seen", PinType::Float, "0.0");
    {
        const std::uint32_t ready = exports.Node("event_ready");
        const std::uint32_t speed = exports.Node("get_variable", {}, "speed");
        const std::uint32_t seen = exports.Node("set_variable", {}, "seen");
        exports.Exec(ready, seen).Wire(speed, 0U, seen, 1U);
    }
    write("exports.uvbp", exports);
    const Scene::Entity exporter = entityManager.CreateEntity();
    entityManager.AddComponent<Scene::ScriptComponent>(
        exporter, Scene::ScriptComponent{"exports.uvbp", {{"speed", "7.5"}, {"gone", "1"}}});
    engine.TickFrame();
    ASSERT_NE(engine.FindUVScriptInstance(exporter), nullptr);
    EXPECT_EQ(engine.FindUVScriptInstance(exporter)->GetField("seen"), Blueprint::Value{7.5});
    entityManager.GetComponent<Scene::ScriptComponent>(exporter).exportValues["speed"] = "2.0";
    engine.TickFrame();
    ASSERT_NE(engine.FindUVScriptInstance(exporter), nullptr);
    EXPECT_EQ(engine.FindUVScriptInstance(exporter)->GetField("seen"), Blueprint::Value{2.0});

    // Release builds: the running programs are written out as C++, one file per distinct program.
    {
        const std::filesystem::path nativeDirectory = mountDirectory / "native";
        const std::optional<std::size_t> written = engine.WriteNativeUVScripts(nativeDirectory);
        ASSERT_TRUE(written.has_value());
        EXPECT_EQ(*written, 1U); // only the exports Blueprint is running at this point
        std::size_t files = 0U;
        for (const auto& entry : std::filesystem::directory_iterator(nativeDirectory)) {
            ++files;
            EXPECT_TRUE(entry.path().filename().string().starts_with("exports_"));
            std::ifstream in(entry.path());
            const std::string text(std::istreambuf_iterator<char>(in), {});
            EXPECT_NE(text.find("Registration kRegistration"), std::string::npos);
        }
        EXPECT_EQ(files, 1U);
    }

    // An object with a transform moves itself through `position`.
    TestGraph mover;
    {
        const std::uint32_t tick = mover.Node("event_tick");
        mover.Exec(tick, mover.Node("add_position", {"", "2,0,0"}));
    }
    write("mover.uvbp", mover);
    const Scene::Entity moving = entityManager.CreateEntity();
    entityManager.AddComponent<Scene::TransformComponent>(moving, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::ScriptComponent>(moving, Scene::ScriptComponent{"mover.uvbp"});
    engine.TickFrame();
    engine.TickFrame();
    ASSERT_NE(engine.FindUVScriptInstance(moving), nullptr);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::TransformComponent>(moving).localPosition.x, 4.0F);

    std::filesystem::remove_all(mountDirectory);
    engine.Shutdown();
}

TEST(EngineCoreTest, ScriptComponentEntity_FindsAProjectBlueprintOnceItIsWritten) {
    // No manual mount: the project directory itself is mounted at the VFS root, so an object's
    // project-relative script path resolves to the file the editor wrote beside the scene.
    const std::filesystem::path projectRoot = "uve_engine_core_tests_project_root";
    std::filesystem::remove_all(projectRoot);
    std::filesystem::create_directories(projectRoot / "scripts");
    EngineConfig config = MakeTestConfig();
    config.projectRootDirectory = projectRoot;
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    const Scene::Entity entity = entityManager.CreateEntity();
    // Only a Blueprint runs: any other kind of file is ignored.
    entityManager.AddComponent<Scene::ScriptComponent>(entity, Scene::ScriptComponent{"scripts/old.txt"});
    engine.TickFrame();
    EXPECT_EQ(engine.GetActiveScriptInstanceCount(), 0U);

    // A Blueprint that does not exist yet fails, and runs once the file is written.
    entityManager.GetComponent<Scene::ScriptComponent>(entity).scriptAssetPath = "scripts/main.uvbp";
    engine.TickFrame();
    EXPECT_EQ(engine.GetActiveScriptInstanceCount(), 0U);
    TestGraph main;
    main.Var("ticks", PinType::Int, "0");
    const std::vector<std::byte> bytes = main.Bytes();
    std::ofstream(projectRoot / "scripts" / "main.uvbp", std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    engine.TickFrame();
    engine.TickFrame();
    EXPECT_EQ(engine.GetActiveScriptInstanceCount(), 1U);

    engine.Shutdown();
    std::filesystem::remove_all(projectRoot);
}

TEST(EngineCoreTest, RenderSystem_ReachableAndFrameLifecycleWorksAfterInit) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Render::IRenderSystem& renderSystem = engine.GetServices().GetRenderSystem();
    EXPECT_EQ(renderSystem.GetFrameIndex(), 0U);

    renderSystem.BeginFrame();
    static_cast<void>(renderSystem.GetFrameCommandBuffer());
    renderSystem.EndFrame();

    EXPECT_EQ(renderSystem.GetFrameIndex(), 1U);

    Render::IRenderDevice& renderDevice = engine.GetServices().GetRenderDevice();
    EXPECT_EQ(renderDevice.GetBackendName(), "Null");

    engine.Shutdown();
}

TEST(EngineCoreTest, CameraSystem_ReachableAndComputesViewProjectionAfterInit) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();
    const Scene::Entity cameraEntity = entityManager.CreateEntity();
    Scene::TransformComponent local;
    local.localPosition = Math::Vector3{0.0F, 0.0F, 5.0F};
    sceneGraph.AttachTransform(entityManager, cameraEntity, local);
    sceneGraph.Update(entityManager);
    entityManager.AddComponent<Scene::CameraComponent>(cameraEntity);

    Render::ICameraSystem& cameraSystem = engine.GetServices().GetCameraSystem();
    const Math::Matrix4x4 viewProjection =
        cameraSystem.ComputeViewProjection(entityManager, cameraEntity, 16.0F / 9.0F);
    const Math::Frustum frustum = cameraSystem.ExtractFrustum(viewProjection);

    EXPECT_TRUE(
        frustum.Intersects(Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F})));

    engine.Shutdown();
}

TEST(EngineCoreTest, LightSystem_ReachableAndReturnsEmptyListWithNoLightEntityAfterInit) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Render::ILightSystem& lightSystem = engine.GetServices().GetLightSystem();
    const Render::LightList lights =
        lightSystem.ExtractActiveLights(engine.GetServices().GetEntityManager());
    EXPECT_TRUE(lights.Empty());
    EXPECT_EQ(lights.Size(), 0U);

    engine.Shutdown();
}

TEST(EngineCoreTest, Renderer3D_ReachableAfterInit_NoActiveCameraStillNoOps) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    static_cast<void>(engine.GetServices().GetRenderer3D());
    EXPECT_EQ(engine.GetActiveCamera(), Scene::kInvalidEntity);

    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    engine.GetServices().GetLogger().AddSink(std::move(memorySink));

    engine.TickFrame();

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundNoOpTrace =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.message.starts_with("Render (no-op)");
        });
    EXPECT_TRUE(foundNoOpTrace);

    engine.Shutdown();
}

TEST(EngineCoreTest, DocumentScene_IsSeparateFromTheEngineScene) {
    EngineCore engine(MakeTestConfig());
    EXPECT_EQ(engine.CreateDocumentScene(), nullptr); // not before Init()
    engine.Init();
    ASSERT_TRUE(engine.Load());

    EngineServices& level = engine.GetServices();
    std::unique_ptr<DocumentScene> document = engine.CreateDocumentScene();
    ASSERT_NE(document, nullptr);
    EngineServices& services = document->GetServices();

    // The scene half is its own; everything else is the engine's.
    EXPECT_NE(&services.GetEntityManager(), &level.GetEntityManager());
    EXPECT_NE(&services.GetSceneGraph(), &level.GetSceneGraph());
    EXPECT_NE(&services.GetRenderer3D(), &level.GetRenderer3D());
    EXPECT_NE(&services.GetMeshRenderer(), &level.GetMeshRenderer());
    EXPECT_EQ(&services.GetAssetDatabase(), &level.GetAssetDatabase());
    EXPECT_EQ(&services.GetRenderDevice(), &level.GetRenderDevice());
    EXPECT_EQ(&services.GetPrefabSystem(), &level.GetPrefabSystem());

    const std::size_t levelCount = level.GetEntityManager().GetEntityCount();
    const Scene::Entity entity = document->GetEntityManager().CreateEntity();
    EXPECT_TRUE(document->GetEntityManager().IsAlive(entity));
    EXPECT_EQ(document->GetEntityManager().GetEntityCount(), 1U);
    EXPECT_EQ(level.GetEntityManager().GetEntityCount(), levelCount);

    document->GetEntityManager().DestroyEntity(entity);
    engine.TickFrame();
    EXPECT_EQ(level.GetEntityManager().GetEntityCount(), levelCount);

    document.reset();
    engine.Shutdown();
}

TEST(EngineCoreTest, WindowedMode_NoActiveCameraStillClearsDefaultFramebuffer) {
    EngineConfig config = MakeTestConfig();
    config.headless = false;
    config.windowWidth = 64;
    config.windowHeight = 64;
    config.vsyncEnabled = false;
    config.windowGlVersionMajor = 4;
    config.windowGlVersionMinor = 5;

    EngineCore engine(config);
    engine.Init();
    if (!engine.GetServices().GetWindowManager().IsValid()) {
        GTEST_SKIP() << "No display available for windowed EngineCore - skipping (run under "
                        "xvfb-run to exercise this test)";
    }
    ASSERT_TRUE(engine.Load());
    EXPECT_EQ(engine.GetActiveCamera(), Scene::kInvalidEntity);

    std::array<unsigned char, 3> emptyScenePixel{};
    GLenum postRenderGlError = GL_NO_ERROR;
    engine.SetPostRenderCallback([&emptyScenePixel, &postRenderGlError] {
        glFinish();
        glReadBuffer(GL_BACK);
        glReadPixels(32, 32, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, emptyScenePixel.data());
        postRenderGlError = glGetError();
    });

    engine.TickFrame();
    engine.SetPostRenderCallback({});

    EXPECT_EQ(emptyScenePixel[0], 13U);
    EXPECT_EQ(emptyScenePixel[1], 13U);
    EXPECT_EQ(emptyScenePixel[2], 13U);
    EXPECT_EQ(postRenderGlError, GL_NO_ERROR);

    engine.Shutdown();
}

TEST(EngineCoreTest, Renderer3D_ActiveCameraSet_RendersWithoutCrashing) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();
    const Scene::Entity cameraEntity = entityManager.CreateEntity();
    Scene::TransformComponent local;
    local.localPosition = Math::Vector3{0.0F, 0.0F, 5.0F};
    sceneGraph.AttachTransform(entityManager, cameraEntity, local);
    sceneGraph.Update(entityManager);
    entityManager.AddComponent<Scene::CameraComponent>(cameraEntity);

    engine.SetActiveCamera(cameraEntity);
    EXPECT_EQ(engine.GetActiveCamera(), cameraEntity);

    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    engine.GetServices().GetLogger().AddSink(std::move(memorySink));

    engine.TickFrame();

    // With an active camera set, Render() no longer takes the no-op trace path — proves
    // RenderFrame() actually ran instead (an empty scene still begins+ends a render pass).
    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundNoOpTrace =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.message.starts_with("Render (no-op)");
        });
    EXPECT_FALSE(foundNoOpTrace);

    engine.Shutdown();
}

TEST(EngineCoreTest, PhysicsSystemAndCollisionSystem_ReachableAndFunctionalAfterInit) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Physics::ICollisionSystem& collisionSystem = engine.GetServices().GetCollisionSystem();
    Physics::IPhysicsSystem& physicsSystem = engine.GetServices().GetPhysicsSystem();

    EXPECT_TRUE(collisionSystem.DetectCollisions(entityManager).empty());

    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();
    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::TransformComponent local;
    local.localPosition = Math::Vector3{0.0F, 10.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, entity, local);
    entityManager.AddComponent<Scene::Rigid3DComponent>(entity);
    sceneGraph.Update(entityManager);

    physicsSystem.Step(entityManager, sceneGraph, 1.0F / 60.0F);

    const Scene::WorldTransformComponent& world =
        entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
    EXPECT_LT(world.worldPosition.y, 10.0F);

    engine.Shutdown();
}

TEST(EngineCoreTest, PhysicsConstraints_ComposedAndSolvedThroughNormalFixedStep) {
    EngineConfig config = MakeTestConfig();
    config.gravity = {};
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    auto& services = engine.GetServices();
    auto& entityManager = services.GetEntityManager();
    auto& sceneGraph = services.GetSceneGraph();

    const auto makeBody = [&entityManager, &sceneGraph](const Math::Vector3 position) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform;
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
        entityManager.AddComponent<Scene::Rigid3DComponent>(entity,
                                                                      Scene::Rigid3DComponent{1.0F, false});
        return entity;
    };

    const Scene::Entity first = makeBody({0.0F, 0.0F, 0.0F});
    const Scene::Entity second = makeBody({10.0F, 0.0F, 0.0F});
    const Physics::PhysicsConstraintMutationResult added =
        services.GetPhysicsConstraintSystem().AddDistanceConstraint(
            Physics::DistanceConstraint{first, second, {}, {}, 4.0F});
    ASSERT_TRUE(added.IsAccepted());

    ASSERT_TRUE(engine.SetSimulationExecutionMode(SimulationExecutionMode::Paused));
    ASSERT_TRUE(engine.RequestSingleSimulationStep());
    engine.TickFrame();

    const float firstX = entityManager.GetComponent<Scene::WorldTransformComponent>(first).worldPosition.x;
    const float secondX = entityManager.GetComponent<Scene::WorldTransformComponent>(second).worldPosition.x;
    EXPECT_NEAR(secondX - firstX, 4.0F, 1.0e-3F);
    EXPECT_NEAR(firstX, 3.0F, 1.0e-3F);
    EXPECT_NEAR(secondX, 7.0F, 1.0e-3F);

    engine.Shutdown();
}

TEST(EngineCoreTest, RaycastSystem_ReachableAndFunctionalAfterInit) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();
    Physics::IRaycastSystem& raycastSystem = engine.GetServices().GetRaycastSystem();

    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::TransformComponent local;
    local.localPosition = Math::Vector3{5.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, entity, local);
    sceneGraph.Update(entityManager);
    entityManager.AddComponent<Scene::ColliderComponent>(entity, Scene::ColliderComponent{Math::Vector3{1.0F, 1.0F, 1.0F}});

    Physics::RaycastQuery query;
    query.ray = Math::Ray{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 0.0F, 0.0F}};
    query.maxDistance = 100.0F;
    const std::optional<Physics::RaycastHit> hit = raycastSystem.Raycast(entityManager, query);

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, entity);

    engine.Shutdown();
}

TEST(EngineCoreTest, InputSystem_ReachableAndFunctionalAfterInit) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Input::IInputSystem& inputSystem = engine.GetServices().GetInputSystem();

    inputSystem.SetKeyState(Input::KeyCode::Space, true);
    engine.TickFrame();

    EXPECT_TRUE(inputSystem.IsKeyDown(Input::KeyCode::Space));
    EXPECT_TRUE(inputSystem.WasKeyPressedThisFrame(Input::KeyCode::Space));

    engine.TickFrame();
    EXPECT_TRUE(inputSystem.IsKeyDown(Input::KeyCode::Space));
    EXPECT_FALSE(inputSystem.WasKeyPressedThisFrame(Input::KeyCode::Space));

    inputSystem.SetKeyState(Input::KeyCode::Space, false);
    engine.TickFrame();
    EXPECT_FALSE(inputSystem.IsKeyDown(Input::KeyCode::Space));
    EXPECT_TRUE(inputSystem.WasKeyReleasedThisFrame(Input::KeyCode::Space));

    engine.Shutdown();
}

TEST(EngineCoreTest, ExtendedInputServices_ReachableAndUpdatedThroughEngineCore) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Input::IInputSystem& inputSystem = engine.GetServices().GetInputSystem();
    Input::IGamepadInputSystem& gamepad = engine.GetServices().GetGamepadInputSystem();
    Input::IMobileInputSystem& mobile = engine.GetServices().GetMobileInputSystem();
    Input::IMobileGestureSystem& gestures = engine.GetServices().GetMobileGestureSystem();

    Input::InputAction action;
    action.name = "GamepadJump";
    action.type = Input::InputActionType::Button;
    action.positiveBindings.push_back(Input::GamepadButtonBinding(0U, Input::GamepadButton::South));
    inputSystem.RegisterAction(std::move(action));

    gamepad.SetConnected(0U, true);
    gamepad.SetButtonState(0U, Input::GamepadButton::South, true);
    mobile.SetTouchState(0U, true, 42U, Math::Vector2{10.0F, 20.0F}, 0.5F);
    engine.TickFrame();

    EXPECT_TRUE(inputSystem.IsActionTriggered("GamepadJump"));
    EXPECT_EQ(gamepad.GetSnapshot(0U).frameNumber, 1U);
    EXPECT_EQ(mobile.GetSnapshot().frameNumber, 1U);
    EXPECT_EQ(gestures.GetLastReport().count, 0U);

    gamepad.SetButtonState(0U, Input::GamepadButton::South, false);
    mobile.SetTouchState(0U, false, 0U, Math::Vector2{}, 0.0F);
    engine.TickFrame();

    EXPECT_TRUE(inputSystem.IsActionReleased("GamepadJump"));
    const Input::MobileGestureReport report = gestures.GetLastReport();
    ASSERT_EQ(report.count, 1U);
    EXPECT_EQ(report.events[0].type, Input::MobileGestureType::Tap);
    EXPECT_EQ(report.events[0].touchIdentifier, 42U);

    engine.Shutdown();
}

TEST(EngineCoreTest, AudioSystem_ReachableAndFunctionalAfterInit) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Audio::IAudioSystem& audioSystem = engine.GetServices().GetAudioSystem();

    Audio::AudioSourceDesc desc;
    desc.spatial = false;
    const Audio::VoiceHandle source = audioSystem.CreateSource(desc);
    ASSERT_NE(source, Audio::kInvalidVoiceHandle);
    ASSERT_TRUE(audioSystem.Play(source));

    engine.TickFrame();

    EXPECT_EQ(audioSystem.GetSourceState(source), Audio::VoicePlaybackState::Playing);

    engine.Shutdown();
}

TEST(EngineCoreTest, AudioStreamAndPcmEffects_ReachableThroughEngineServices) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Audio::IAudioSystem& audioSystem = engine.GetServices().GetAudioSystem();
    const Audio::VoiceHandle source = audioSystem.CreateSource(Audio::AudioSourceDesc{});
    ASSERT_NE(source, Audio::kInvalidVoiceHandle);

    ASSERT_TRUE(audioSystem.ResetSourceStream(source, 8U, false));
    ASSERT_TRUE(audioSystem.ScheduleSourceStreamWindow(source, 3U));
    Audio::Pcm16StreamWindowPlan plan;
    ASSERT_TRUE(audioSystem.PopSourceStreamWindow(source, plan));
    EXPECT_EQ(plan, (Audio::Pcm16StreamWindowPlan{0U, 3U, 3U, false, false}));

    ASSERT_TRUE(audioSystem.ScheduleSourcePcmGainWindow(
        source, Audio::PcmGainEffectWindow{0U, 2U, 0.25F}));
    std::vector<float> output;
    ASSERT_TRUE(audioSystem.ApplySourcePcmGainEffects(source, {0.8F, -0.4F, 0.5F}, output));
    EXPECT_EQ(output, (std::vector<float>{0.2F, -0.1F, 0.5F}));

    engine.Shutdown();
}

TEST(EngineCoreTest, AudioListener_TracksActiveCameraWorldPosition) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();
    const Scene::Entity cameraEntity = entityManager.CreateEntity();
    Scene::TransformComponent local;
    local.localPosition = Math::Vector3{3.0F, 4.0F, 5.0F};
    sceneGraph.AttachTransform(entityManager, cameraEntity, local);
    sceneGraph.Update(entityManager);
    entityManager.AddComponent<Scene::CameraComponent>(cameraEntity);

    engine.SetActiveCamera(cameraEntity);
    engine.TickFrame();

    Audio::IAudioSystem& audioSystem = engine.GetServices().GetAudioSystem();
    EXPECT_EQ(audioSystem.GetListenerPosition(), (Math::Vector3{3.0F, 4.0F, 5.0F}));

    engine.Shutdown();
}

TEST(EngineCoreTest, FallingRigid3D_TickFrameDrivenPhysicsStep_MovesEntityDownward) {
    // A 1kHz fixed-update rate (1ms fixed step) paired with a short real sleep before each
    // TickFrame() call guarantees the ITimer accumulator crosses at least one fixed step
    // almost every frame — an excessively high fixedUpdateFps would trigger steps just as
    // reliably but make each step's position delta too small to be representable in float at
    // this entity's starting magnitude (10.0), silently rounding to no visible movement. This
    // test only proves EngineCore::Update()'s physics-step wiring moves an entity end-to-end;
    // PhysicsSystem's exact per-step math is already covered by
    // tests/physics/physics_system_uve_tests.cpp.
    EngineConfig config = MakeTestConfig();
    config.fixedUpdateFps = 1000.0;
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::TransformComponent local;
    local.localPosition = Math::Vector3{0.0F, 10.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, entity, local);
    entityManager.AddComponent<Scene::Rigid3DComponent>(entity);

    for (int frame = 0; frame < 30; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        engine.TickFrame();
    }

    const Scene::WorldTransformComponent& world =
        entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
    EXPECT_LT(world.worldPosition.y, 10.0F);

    engine.Shutdown();
}

TEST(EngineCoreTest, CharacterController_FallsUnderGravityLandsOnGroundThenJumpsOnSpace) {
    // Same 1kHz fixed-update / short real-sleep discipline as FallingRigid3D_* above, so
    // EngineCore::Update()'s new SyncCharacterControllers() wiring gets exercised end-to-end
    // (gravity accumulation -> Physics::CharacterController::MoveWithToI -> ground contact),
    // not just PhysicsSystem's own already-covered per-step math.
    EngineConfig config = MakeTestConfig();
    config.fixedUpdateFps = 1000.0;
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    // Static ground: a flat box collider with no Rigid3DComponent, top surface at y=0.5.
    const Scene::Entity ground = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, ground, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::ColliderComponent>(
        ground, Scene::ColliderComponent{Math::Vector3{10.0F, 0.5F, 10.0F}});

    // Character controller entity starting 1 unit above its expected resting height (ground top
    // 0.5 + the default box collider's own 0.5 half-extent = 1.0).
    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::TransformComponent local;
    local.localPosition = Math::Vector3{0.0F, 2.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, entity, local);
    entityManager.AddComponent<Scene::ColliderComponent>(entity, Scene::ColliderComponent{});
    entityManager.AddComponent<Scene::CharacterControllerComponent>(entity);

    // Character input is routed through a mutually possessed player controller; an unpossessed
    // CharacterController intentionally stands still even while the keyboard is active.
    entityManager.AddComponent<Scene::PawnComponent>(entity);
    const Scene::Entity playerController = entityManager.CreateEntity();
    Scene::ControllerComponent controller;
    controller.kind = Scene::ControllerKind::Player;
    entityManager.AddComponent<Scene::ControllerComponent>(playerController, controller);
    ASSERT_TRUE(Scene::PossessController(entityManager, playerController, entity));

    for (int frame = 0; frame < 400; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        engine.TickFrame();
    }

    const Scene::CharacterControllerComponent& afterFall =
        entityManager.GetComponent<Scene::CharacterControllerComponent>(entity);
    EXPECT_TRUE(afterFall.grounded);
    const Scene::WorldTransformComponent& worldAfterFall =
        entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
    EXPECT_NEAR(worldAfterFall.worldPosition.y, 1.0F, 0.35F);

    Input::IInputSystem& inputSystem = engine.GetServices().GetInputSystem();
    inputSystem.SetKeyState(Input::KeyCode::Space, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    engine.TickFrame();

    const Scene::CharacterControllerComponent& afterJump =
        entityManager.GetComponent<Scene::CharacterControllerComponent>(entity);
    EXPECT_GT(afterJump.velocity.y, 0.0F);
    EXPECT_FALSE(afterJump.grounded);

    engine.Shutdown();
}

namespace {

// A box collider whose top face is at `top`, spanning x from `minX` to `maxX`.
Scene::Entity AddFloor(Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
                             const float minX, const float maxX, const float top) {
    const Scene::Entity floor = entityManager.CreateEntity();
    Scene::TransformComponent local;
    local.localPosition = Math::Vector3{(minX + maxX) * 0.5F, top - 0.5F, 0.0F};
    sceneGraph.AttachTransform(entityManager, floor, local);
    entityManager.AddComponent<Scene::ColliderComponent>(
        floor, Scene::ColliderComponent{Math::Vector3{(maxX - minX) * 0.5F, 0.5F, 10.0F}});
    return floor;
}

// A script-driven Character3D (built-in movement off) with a 1 m box, standing on y = `floorTop`.
Scene::Entity AddWalker(Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
                              const Math::Vector3& feet, const Scene::CharacterControllerComponent& controller) {
    const Scene::Entity walker = entityManager.CreateEntity();
    Scene::TransformComponent local;
    local.localPosition = Math::Vector3{feet.x, feet.y + 0.5F, feet.z};
    sceneGraph.AttachTransform(entityManager, walker, local);
    entityManager.AddComponent<Scene::ColliderComponent>(walker, Scene::ColliderComponent{});
    entityManager.AddComponent<Scene::CharacterControllerComponent>(walker, controller);
    return walker;
}

// Walks off a ledge 0.1 m high and reports whether the walker left the floor on the way down.
bool LeavesTheFloorWalkingDownAStep(const float floorSnapLength) {
    EngineConfig config = MakeTestConfig();
    config.fixedUpdateFps = 1000.0;
    EngineCore engine(config);
    engine.Init();
    EXPECT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();
    static_cast<void>(AddFloor(entityManager, sceneGraph, -10.0F, 0.0F, 0.5F));
    static_cast<void>(AddFloor(entityManager, sceneGraph, 0.0F, 10.0F, 0.4F));
    Scene::CharacterControllerComponent controller{};
    controller.builtInMovement = false;
    controller.floorSnapLength = floorSnapLength;
    const Scene::Entity walker =
        AddWalker(entityManager, sceneGraph, Math::Vector3{-1.5F, 0.5F, 0.0F}, controller);

    // Settle on the high floor, then walk right at 2 m/s across the edge at x = 0.
    for (int frame = 0; frame < 20; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        engine.TickFrame();
    }
    EXPECT_TRUE(entityManager.GetComponent<Scene::CharacterControllerComponent>(walker).grounded);
    bool leftTheFloor = false;
    for (int frame = 0; frame < 700; ++frame) {
        entityManager.GetComponent<Scene::CharacterControllerComponent>(walker).velocity.x = 2.0F;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        engine.TickFrame();
        leftTheFloor = leftTheFloor || !entityManager.GetComponent<Scene::CharacterControllerComponent>(walker).grounded;
    }
    const float x = entityManager.GetComponent<Scene::WorldTransformComponent>(walker).worldPosition.x;
    const float y = entityManager.GetComponent<Scene::WorldTransformComponent>(walker).worldPosition.y;
    EXPECT_GT(x, 1.0F) << "never got past the edge";
    EXPECT_NEAR(y, 0.9F, 0.05F) << "not standing on the low floor";
    engine.Shutdown();
    return leftTheFloor;
}

} // namespace

TEST(EngineCoreTest, Character3D_FloorSnapFollowsAStepDownThatWouldOtherwiseLaunchIt) {
    EXPECT_FALSE(LeavesTheFloorWalkingDownAStep(0.2F));
    EXPECT_TRUE(LeavesTheFloorWalkingDownAStep(0.0F));
}

TEST(EngineCoreTest, Character3D_SolidBodyMotionLocksHoldTheirAxis) {
    EngineConfig config = MakeTestConfig();
    config.fixedUpdateFps = 1000.0;
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();
    Scene::CharacterControllerComponent controller{};
    controller.builtInMovement = false;
    controller.motionMode = Scene::CharacterMotionMode::Floating;
    const Scene::Entity body = AddWalker(entityManager, sceneGraph, Math::Vector3{0.0F, 5.0F, 0.0F}, controller);
    Scene::SolidBodyComponent locks{};
    locks.lockMotionX = true;
    entityManager.AddComponent<Scene::SolidBodyComponent>(body, locks);
    for (int frame = 0; frame < 150; ++frame) {
        entityManager.GetComponent<Scene::CharacterControllerComponent>(body).velocity =
            Math::Vector3{3.0F, 0.0F, 2.0F};
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        engine.TickFrame();
    }
    const Math::Vector3 position = entityManager.GetComponent<Scene::WorldTransformComponent>(body).worldPosition;
    EXPECT_FLOAT_EQ(position.x, 0.0F);           // Locked: never moved along X.
    EXPECT_GT(position.z, 0.2F);                 // Free: moved along Z.
    EXPECT_NEAR(position.y, 5.5F, 1.0e-4F);      // Floating: no gravity.
    engine.Shutdown();
}

TEST(EngineCoreTest, AnimationSequencer_PlaysItsClipOnItsParentObject) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();
    Asset::IAssetDatabase& assetDatabase = engine.GetServices().GetAssetDatabase();

    // A two-second clip that slides 10 m along X.
    Asset::AnimationClipAsset clip;
    clip.clipId = "slide";
    clip.durationSeconds = 2.0;
    Asset::AnimationAssetSample start;
    Asset::AnimationAssetSample end;
    end.timeSeconds = 2.0;
    end.pose.position = Math::Vector3{10.0F, 0.0F, 0.0F};
    clip.samples = {start, end};
    const std::filesystem::path clipPath = "uve_engine_core_tests_slide.uvanim";
    ASSERT_TRUE(Asset::SaveAnimationClipAsset(clip, clipPath));
    const Asset::AssetGuid guid = assetDatabase.Register(clipPath);
    ASSERT_NE(guid, Asset::kInvalidAssetGuid);

    // The door is a Object3D; the player is a pure Object under it, with no target set.
    const Scene::Entity door = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, door, Scene::TransformComponent{});
    const Scene::Entity player = entityManager.CreateEntity();
    Scene::AnimationSequencerObjectDefinition definition;
    definition.player.clip = guid;
    definition.player.loopMode = Scene::AnimationLoopMode::Once;
    Scene::ApplyAnimationSequencerObjectDefinition(entityManager, player, definition);
    sceneGraph.SetParent(entityManager, player, door);

    const auto startedAt = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startedAt < std::chrono::seconds(10) &&
           !entityManager.GetComponent<Scene::AnimationSequencerComponent>(player).finished) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        engine.TickFrame();
    }
    const Scene::AnimationSequencerComponent& played =
        entityManager.GetComponent<Scene::AnimationSequencerComponent>(player);
    EXPECT_TRUE(played.finished);
    EXPECT_FALSE(played.isPlaying);
    // A Once clip holds its last pose, and the world transform followed.
    EXPECT_NEAR(entityManager.GetComponent<Scene::TransformComponent>(door).localPosition.x, 10.0F, 1e-4F);
    EXPECT_NEAR(entityManager.GetComponent<Scene::WorldTransformComponent>(door).worldPosition.x, 10.0F, 1e-4F);
    EXPECT_FALSE(entityManager.HasComponent<Scene::TransformComponent>(player));

    std::filesystem::remove(clipPath);
    std::filesystem::remove(MakeTestConfig().assetDatabaseFilePath);
    engine.Shutdown();
}

TEST(EngineCoreTest, AnimationSequencer_PosesTheSkeletonInsideTheCharacterWithASkeletalClip) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();
    Asset::IAssetDatabase& assetDatabase = engine.GetServices().GetAssetDatabase();

    // A one-second skeletal clip raising "Hips" from 1 m to 2 m.
    Asset::AnimationClipAsset clip;
    clip.clipId = "stand";
    clip.durationSeconds = 1.0;
    Asset::AnimationAssetSample start;
    start.pose.position = Math::Vector3{0.0F, 1.0F, 0.0F};
    Asset::AnimationAssetSample end;
    end.timeSeconds = 1.0;
    end.pose.position = Math::Vector3{0.0F, 2.0F, 0.0F};
    clip.bones = {Asset::AnimationAssetBoneTrack{"Hips", {start, end}}};
    const std::filesystem::path clipPath = "uve_engine_core_tests_stand.uvanim";
    ASSERT_TRUE(Asset::SaveAnimationClipAsset(clip, clipPath));
    const Asset::AssetGuid guid = assetDatabase.Register(clipPath);

    // Character > { Skeleton3D, AnimationSequencer }: the player targets its parent, the character,
    // and finds the skeleton inside it.
    const Scene::Entity character = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, character, Scene::TransformComponent{});
    const Scene::Entity skeletonEntity = entityManager.CreateEntity();
    Scene::Skeleton3DObjectDefinition skeletonDefinition;
    skeletonDefinition.skeleton.skeletonAssetPath = "Hero.fbx";
    Scene::SkeletonBone hips;
    hips.name = "Hips";
    hips.localPosition = Math::Vector3{0.0F, 1.0F, 0.0F};
    skeletonDefinition.skeleton.bones = {hips};
    Scene::ApplySkeleton3DObjectDefinition(entityManager, skeletonEntity, skeletonDefinition);
    sceneGraph.SetParent(entityManager, skeletonEntity, character);
    const Scene::Entity player = entityManager.CreateEntity();
    Scene::AnimationSequencerObjectDefinition definition;
    definition.player.clip = guid;
    definition.player.loopMode = Scene::AnimationLoopMode::Once;
    Scene::ApplyAnimationSequencerObjectDefinition(entityManager, player, definition);
    sceneGraph.SetParent(entityManager, player, character);

    const auto startedAt = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startedAt < std::chrono::seconds(10) &&
           !entityManager.GetComponent<Scene::AnimationSequencerComponent>(player).finished) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        engine.TickFrame();
    }
    EXPECT_TRUE(entityManager.GetComponent<Scene::AnimationSequencerComponent>(player).finished);
    const Scene::Skeleton3DComponent& posed =
        entityManager.GetComponent<Scene::Skeleton3DComponent>(skeletonEntity);
    ASSERT_EQ(posed.pose.size(), 1U);
    EXPECT_NEAR(posed.pose[0].position.y, 2.0F, 1e-4F);
    // The character itself was not moved: a skeletal clip poses bones, not objects.
    EXPECT_NEAR(entityManager.GetComponent<Scene::TransformComponent>(character).localPosition.y, 0.0F, 1e-6F);

    std::filesystem::remove(clipPath);
    std::filesystem::remove(MakeTestConfig().assetDatabaseFilePath);
    engine.Shutdown();
}

TEST(EngineCoreTest, BoneAttachment3D_RidesThePosedBoneInTheFrameThePoseArrives) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();
    Asset::IAssetDatabase& assetDatabase = engine.GetServices().GetAssetDatabase();

    // A one-second clip raising "Hand" from the shoulder to a metre above it.
    Asset::AnimationClipAsset clip;
    clip.clipId = "salute";
    clip.durationSeconds = 1.0;
    Asset::AnimationAssetSample start;
    start.pose.position = Math::Vector3{0.5F, 1.0F, 0.0F};
    Asset::AnimationAssetSample end;
    end.timeSeconds = 1.0;
    end.pose.position = Math::Vector3{0.5F, 2.0F, 0.0F};
    clip.bones = {Asset::AnimationAssetBoneTrack{"Hand", {start, end}}};
    const std::filesystem::path clipPath = "uve_engine_core_tests_salute.uvanim";
    ASSERT_TRUE(Asset::SaveAnimationClipAsset(clip, clipPath));
    const Asset::AssetGuid guid = assetDatabase.Register(clipPath);
    ASSERT_NE(guid, Asset::kInvalidAssetGuid);

    // A character at x=10 holding a skeleton, a player that poses it, and an attachment that rides the
    // hand. The attachment is an ordinary child of the CHARACTER, not of the skeleton: being driven by
    // a bone must not cost it its place in the hierarchy, or the character could not carry it around.
    const Scene::Entity character = entityManager.CreateEntity();
    Scene::TransformComponent characterTransform;
    characterTransform.localPosition = Math::Vector3{10.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, character, characterTransform);
    const Scene::Entity skeletonEntity = entityManager.CreateEntity();
    Scene::Skeleton3DObjectDefinition skeletonDefinition;
    skeletonDefinition.skeleton.skeletonAssetPath = "Hero.fbx";
    Scene::SkeletonBone hips;
    hips.name = "Hips";
    Scene::SkeletonBone hand;
    hand.name = "Hand";
    hand.parentIndex = 0;
    hand.localPosition = Math::Vector3{0.5F, 1.0F, 0.0F};
    skeletonDefinition.skeleton.bones = {hips, hand};
    Scene::ApplySkeleton3DObjectDefinition(entityManager, skeletonEntity, skeletonDefinition);
    sceneGraph.SetParent(entityManager, skeletonEntity, character);
    const Scene::Entity player = entityManager.CreateEntity();
    Scene::AnimationSequencerObjectDefinition definition;
    definition.player.clip = guid;
    definition.player.loopMode = Scene::AnimationLoopMode::Once;
    Scene::ApplyAnimationSequencerObjectDefinition(entityManager, player, definition);
    sceneGraph.SetParent(entityManager, player, character);
    const Scene::Entity attachment = entityManager.CreateEntity();
    Scene::BoneAttachment3DComponent attachmentComponent;
    attachmentComponent.skeleton = skeletonEntity;
    attachmentComponent.boneName = "Hand";
    attachmentComponent.localPosition = Math::Vector3{0.0F, 0.1F, 0.0F};
    entityManager.AddComponent<Scene::BoneAttachment3DComponent>(attachment, attachmentComponent);
    sceneGraph.AttachTransform(entityManager, attachment, Scene::TransformComponent{});
    sceneGraph.SetParent(entityManager, attachment, character);

    const auto startedAt = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startedAt < std::chrono::seconds(10) &&
           !entityManager.GetComponent<Scene::AnimationSequencerComponent>(player).finished) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        engine.TickFrame();
    }
    ASSERT_TRUE(entityManager.GetComponent<Scene::AnimationSequencerComponent>(player).finished);

    const Scene::BoneAttachment3DComponent& resolved =
        entityManager.GetComponent<Scene::BoneAttachment3DComponent>(attachment);
    EXPECT_TRUE(resolved.bound);
    EXPECT_EQ(resolved.resolvedBoneIndex, 1U) << "bound by name to the hand";

    // The clip's last pose leaves the hand at (0.5, 2, 0) in the character's frame, which is at x=10,
    // and the attachment sits 10 cm along the bone's own up axis. Asserted on the frame the pose
    // finished in: the attachment pass runs after the pose and before propagation, so it is on the
    // bone in that same frame rather than a frame behind its own animation.
    const Scene::TransformComponent& local = entityManager.GetComponent<Scene::TransformComponent>(attachment);
    EXPECT_NEAR(local.localPosition.x, 0.5F, 1e-3F) << "the transform written is parent-relative";
    EXPECT_NEAR(local.localPosition.y, 2.1F, 1e-3F);
    const Math::Vector3 world =
        entityManager.GetComponent<Scene::WorldTransformComponent>(attachment).worldPosition;
    EXPECT_NEAR(world.x, 10.5F, 1e-3F);
    EXPECT_NEAR(world.y, 2.1F, 1e-3F);
    EXPECT_NEAR(world.z, 0.0F, 1e-3F);

    // Switching the attachment off leaves it where it was: an attachment that is not bound keeps the
    // transform it had, so turning one off is not a teleport.
    entityManager.GetComponent<Scene::BoneAttachment3DComponent>(attachment).enabled = false;
    engine.TickFrame();
    EXPECT_FALSE(entityManager.GetComponent<Scene::BoneAttachment3DComponent>(attachment).bound);
    const Math::Vector3 stayed =
        entityManager.GetComponent<Scene::WorldTransformComponent>(attachment).worldPosition;
    EXPECT_NEAR(stayed.y, 2.1F, 1e-3F);

    std::filesystem::remove(clipPath);
    std::filesystem::remove(MakeTestConfig().assetDatabaseFilePath);
    engine.Shutdown();
}

TEST(EngineCoreTest, TwoBoneIK3D_SolvesTheReachingLimbBeforeTheAttachmentFollowsIt) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();
    Asset::IAssetDatabase& assetDatabase = engine.GetServices().GetAssetDatabase();

    // A one-second clip lifting the hips 20 cm: a real driver poses the skeleton, which is the gate the
    // IK pass reads - a limb whose pose nothing wrote this frame must not be solved a second time.
    Asset::AnimationClipAsset clip;
    clip.clipId = "crouch";
    clip.durationSeconds = 1.0;
    Asset::AnimationAssetSample start;
    Asset::AnimationAssetSample end;
    end.timeSeconds = 1.0;
    end.pose.position = Math::Vector3{0.0F, 0.2F, 0.0F};
    clip.bones = {Asset::AnimationAssetBoneTrack{"Hips", {start, end}}};
    const std::filesystem::path clipPath = "uve_engine_core_tests_crouch.uvanim";
    ASSERT_TRUE(Asset::SaveAnimationClipAsset(clip, clipPath));
    const Asset::AssetGuid guid = assetDatabase.Register(clipPath);
    ASSERT_NE(guid, Asset::kInvalidAssetGuid);

    // A four-bone arm: the shoulder a metre above the hips, the elbow a metre above that, the wrist a
    // metre above that. The chain solves the two upper bones so the WRIST's own origin reaches the
    // target, and the hand's rotation is left to the animation.
    const Scene::Entity character = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, character, Scene::TransformComponent{});
    const Scene::Entity skeletonEntity = entityManager.CreateEntity();
    Scene::Skeleton3DObjectDefinition skeletonDefinition;
    skeletonDefinition.skeleton.skeletonAssetPath = "Archer.fbx";
    Scene::SkeletonBone hips;
    hips.name = "Hips";
    Scene::SkeletonBone upperArm;
    upperArm.name = "UpperArm";
    upperArm.parentIndex = 0;
    upperArm.localPosition = Math::Vector3{0.0F, 1.0F, 0.0F};
    Scene::SkeletonBone forearm;
    forearm.name = "Forearm";
    forearm.parentIndex = 1;
    forearm.localPosition = Math::Vector3{0.0F, 1.0F, 0.0F};
    Scene::SkeletonBone handBone;
    handBone.name = "Hand";
    handBone.parentIndex = 2;
    handBone.localPosition = Math::Vector3{0.0F, 1.0F, 0.0F};
    skeletonDefinition.skeleton.bones = {hips, upperArm, forearm, handBone};
    Scene::ApplySkeleton3DObjectDefinition(entityManager, skeletonEntity, skeletonDefinition);
    sceneGraph.SetParent(entityManager, skeletonEntity, character);

    const Scene::Entity player = entityManager.CreateEntity();
    Scene::AnimationSequencerObjectDefinition playerDefinition;
    playerDefinition.player.clip = guid;
    playerDefinition.player.loopMode = Scene::AnimationLoopMode::Once;
    Scene::ApplyAnimationSequencerObjectDefinition(entityManager, player, playerDefinition);
    sceneGraph.SetParent(entityManager, player, character);

    const Scene::Entity target = entityManager.CreateEntity();
    Scene::TransformComponent targetTransform;
    targetTransform.localPosition = Math::Vector3{1.0F, 1.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, target, targetTransform);
    const Scene::Entity pole = entityManager.CreateEntity();
    Scene::TransformComponent poleTransform;
    poleTransform.localPosition = Math::Vector3{0.0F, 0.0F, 2.0F};
    sceneGraph.AttachTransform(entityManager, pole, poleTransform);

    const Scene::Entity chainEntity = entityManager.CreateEntity();
    Scene::ApplyTwoBoneIK3DObjectDefinition(entityManager, chainEntity, {});
    Scene::TwoBoneIK3DComponent& chain = entityManager.GetComponent<Scene::TwoBoneIK3DComponent>(chainEntity);
    chain.skeleton = skeletonEntity;
    chain.rootBoneName = "UpperArm";
    chain.middleBoneName = "Forearm";
    chain.endBoneName = "Hand";
    chain.target = target;
    chain.poleTarget = pole;
    sceneGraph.SetParent(entityManager, chainEntity, character);

    // Something in the hand: an attachment on the bone the IK drives. It is an ordinary child of the
    // character, so the frame it ends up in says which pass wrote the bone last.
    const Scene::Entity attachment = entityManager.CreateEntity();
    Scene::BoneAttachment3DComponent attachmentComponent;
    attachmentComponent.skeleton = skeletonEntity;
    attachmentComponent.boneName = "Hand";
    entityManager.AddComponent<Scene::BoneAttachment3DComponent>(attachment, attachmentComponent);
    sceneGraph.AttachTransform(entityManager, attachment, Scene::TransformComponent{});
    sceneGraph.SetParent(entityManager, attachment, character);

    const auto startedAt = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startedAt < std::chrono::seconds(10) &&
           !entityManager.GetComponent<Scene::AnimationSequencerComponent>(player).finished) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        engine.TickFrame();
    }
    ASSERT_TRUE(entityManager.GetComponent<Scene::AnimationSequencerComponent>(player).finished);

    const Scene::TwoBoneIK3DComponent& solved =
        entityManager.GetComponent<Scene::TwoBoneIK3DComponent>(chainEntity);
    EXPECT_TRUE(solved.solved);
    EXPECT_TRUE(solved.reached) << "the target is about a metre from the shoulder and the arm is two long";
    EXPECT_NEAR(solved.endToTargetDistanceMetres, 0.0F, 1e-3F);
    EXPECT_EQ(solved.resolvedRootBoneIndex, 1U);
    EXPECT_EQ(solved.resolvedMiddleBoneIndex, 2U);
    EXPECT_EQ(solved.resolvedEndBoneIndex, 3U);

    // The attachment rides the wrist the IK just moved, in the frame the pose arrived: halfway through
    // the clip the hand is two metres up the limb, and the only thing that puts it on the target is the
    // solve that ran before the attachment pass in this same tick.
    const Scene::BoneAttachment3DComponent& bound =
        entityManager.GetComponent<Scene::BoneAttachment3DComponent>(attachment);
    EXPECT_TRUE(bound.bound);
    EXPECT_EQ(bound.resolvedBoneIndex, 3U);
    const Math::Vector3 hand =
        entityManager.GetComponent<Scene::WorldTransformComponent>(attachment).worldPosition;
    EXPECT_NEAR(hand.x, 1.0F, 1e-3F) << "the wrist is on the target, and the attachment is on the wrist";
    EXPECT_NEAR(hand.y, 1.0F, 1e-3F);
    EXPECT_NEAR(hand.z, 0.0F, 1e-3F);
    EXPECT_GT(std::abs(hand.y - 3.2F), 1.0F)
        << "the hand is nowhere near where the animation alone would have left it";
    EXPECT_NEAR(entityManager.GetComponent<Scene::TransformComponent>(attachment).localPosition.z, 0.0F, 1e-3F);

    std::filesystem::remove(clipPath);
    std::filesystem::remove(MakeTestConfig().assetDatabaseFilePath);
    engine.Shutdown();
}

TEST(EngineCoreTest, AnimationSequencer_RootMotionMovesTheCharacterThroughTheSkeletonsFrame) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();
    Asset::IAssetDatabase& assetDatabase = engine.GetServices().GetAssetDatabase();

    // "Hips" run 2 m along +Z in one second.
    Asset::AnimationClipAsset clip;
    clip.clipId = "run";
    clip.durationSeconds = 1.0;
    Asset::AnimationAssetSample start;
    start.pose.position = Math::Vector3{0.0F, 1.0F, 0.0F};
    Asset::AnimationAssetSample end;
    end.timeSeconds = 1.0;
    end.pose.position = Math::Vector3{0.0F, 1.0F, 2.0F};
    clip.bones = {Asset::AnimationAssetBoneTrack{"Hips", {start, end}}};
    const std::filesystem::path clipPath = "uve_engine_core_tests_run.uvanim";
    ASSERT_TRUE(Asset::SaveAnimationClipAsset(clip, clipPath));
    const Asset::AssetGuid guid = assetDatabase.Register(clipPath);

    // The skeleton is turned a quarter left inside the character, so its +Z is the world's +X.
    const Scene::Entity character = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, character, Scene::TransformComponent{});
    const Scene::Entity skeletonEntity = entityManager.CreateEntity();
    Scene::Skeleton3DObjectDefinition skeletonDefinition;
    skeletonDefinition.skeleton.skeletonAssetPath = "Hero.fbx";
    Scene::SkeletonBone hips;
    hips.name = "Hips";
    hips.localPosition = Math::Vector3{0.0F, 1.0F, 0.0F};
    skeletonDefinition.skeleton.bones = {hips};
    Scene::ApplySkeleton3DObjectDefinition(entityManager, skeletonEntity, skeletonDefinition);
    sceneGraph.SetParent(entityManager, skeletonEntity, character);
    auto& skeletonTransform = entityManager.GetComponent<Scene::TransformComponent>(skeletonEntity);
    ASSERT_TRUE(Math::TryMakeAxisAngle(Math::Vector3{0.0F, 1.0F, 0.0F}, 1.5707963F,
                                          skeletonTransform.localRotation));
    const Scene::Entity player = entityManager.CreateEntity();
    Scene::AnimationSequencerObjectDefinition definition;
    definition.player.clip = guid;
    definition.player.loopMode = Scene::AnimationLoopMode::Once;
    definition.mixer.rootMotion = Scene::AnimationRootMotionMode::ApplyToTarget;
    Scene::ApplyAnimationSequencerObjectDefinition(entityManager, player, definition);
    sceneGraph.SetParent(entityManager, player, character);

    const auto startedAt = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startedAt < std::chrono::seconds(10) &&
           !entityManager.GetComponent<Scene::AnimationSequencerComponent>(player).finished) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        engine.TickFrame();
    }
    ASSERT_TRUE(entityManager.GetComponent<Scene::AnimationSequencerComponent>(player).finished);
    const Math::Vector3 moved = entityManager.GetComponent<Scene::TransformComponent>(character).localPosition;
    EXPECT_NEAR(moved.x, 2.0F, 1e-3F) << "the clip's 2 m, turned into the world by the skeleton";
    EXPECT_NEAR(moved.z, 0.0F, 1e-3F);
    const Scene::Skeleton3DComponent& posed =
        entityManager.GetComponent<Scene::Skeleton3DComponent>(skeletonEntity);
    ASSERT_EQ(posed.pose.size(), 1U);
    EXPECT_NEAR(posed.pose[0].position.z, 0.0F, 1e-4F) << "the skeleton runs in place";

    std::filesystem::remove(clipPath);
    std::filesystem::remove(MakeTestConfig().assetDatabaseFilePath);
    engine.Shutdown();
}

TEST(EngineCoreTest, AnimationSequencer_SendsClipEventsToTheScriptOfTheObjectItAnimates) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();
    Asset::IAssetDatabase& assetDatabase = engine.GetServices().GetAssetDatabase();

    Asset::IFileSystem& fileSystem = engine.GetServices().GetFileSystem();
    const std::filesystem::path mountDirectory = "uve_engine_core_tests_anim_events_mount";
    std::filesystem::remove_all(mountDirectory);
    std::filesystem::create_directories(mountDirectory);
    fileSystem.MountDirectory("", mountDirectory, 0);
    // steps += 1 and last = <the event's name>, on every animation event.
    TestGraph feet;
    feet.Var("steps", PinType::Int, "0").Var("last", PinType::String, "");
    {
        const std::uint32_t event = feet.Node("event_animation");
        const TestGraph::Increment step = feet.AddToVariable("steps", 1);
        const std::uint32_t last = feet.Node("set_variable", {}, "last");
        feet.Exec(event, step.set).Exec(step.set, last).Wire(event, 1U, last, 1U);
    }
    ASSERT_TRUE(fileSystem.WriteFile("feet.uvbp", feet.Bytes()));

    // A one-second object clip with two footsteps.
    Asset::AnimationClipAsset clip;
    clip.clipId = "walk";
    clip.durationSeconds = 1.0;
    Asset::AnimationAssetSample start;
    Asset::AnimationAssetSample end;
    end.timeSeconds = 1.0;
    end.pose.position = Math::Vector3{1.0F, 0.0F, 0.0F};
    clip.samples = {start, end};
    clip.events = {Asset::AnimationAssetEvent{0.25, "step_left"}, Asset::AnimationAssetEvent{0.75, "step_right"}};
    const std::filesystem::path clipPath = "uve_engine_core_tests_walk_events.uvanim";
    ASSERT_TRUE(Asset::SaveAnimationClipAsset(clip, clipPath));
    const Asset::AssetGuid guid = assetDatabase.Register(clipPath);

    const Scene::Entity character = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, character, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::ScriptComponent>(character, Scene::ScriptComponent{"feet.uvbp"});
    const Scene::Entity player = entityManager.CreateEntity();
    Scene::AnimationSequencerObjectDefinition definition;
    definition.player.clip = guid;
    definition.player.loopMode = Scene::AnimationLoopMode::Once;
    Scene::ApplyAnimationSequencerObjectDefinition(entityManager, player, definition);
    sceneGraph.SetParent(entityManager, player, character);

    const auto startedAt = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startedAt < std::chrono::seconds(10) &&
           !entityManager.GetComponent<Scene::AnimationSequencerComponent>(player).finished) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        engine.TickFrame();
    }
    ASSERT_TRUE(entityManager.GetComponent<Scene::AnimationSequencerComponent>(player).finished);
    Blueprint::ScriptInstance* const instance = engine.FindUVScriptInstance(character);
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(instance->GetField("steps"), Blueprint::Value{std::int64_t{2}}) << "each footstep once";
    EXPECT_EQ(instance->GetField("last"), Blueprint::Value{std::string{"step_right"}});

    std::filesystem::remove(clipPath);
    std::filesystem::remove_all(mountDirectory);
    std::filesystem::remove(MakeTestConfig().assetDatabaseFilePath);
    engine.Shutdown();
}

TEST(EngineCoreTest, MontageSequencer_PlaysAMontageFromTheScriptAndSendsItsNotifies) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();
    Asset::IAssetDatabase& assetDatabase = engine.GetServices().GetAssetDatabase();
    Asset::IFileSystem& fileSystem = engine.GetServices().GetFileSystem();
    const std::filesystem::path mountDirectory = "uve_engine_core_tests_montage_mount";
    std::filesystem::remove_all(mountDirectory);
    std::filesystem::create_directories(mountDirectory);
    fileSystem.MountDirectory("", mountDirectory, 0);
    // Plays "Attack" at ready; counts the "hit" notifies; keeps the name of the montage that ended.
    TestGraph fighter;
    fighter.Var("hits", PinType::Int, "0").Var("ended", PinType::String, "");
    {
        const std::uint32_t ready = fighter.Node("event_ready");
        fighter.Exec(ready, fighter.Node("montage_play", {"", "Attack"}));
        const std::uint32_t notify = fighter.Node("event_montage_notify");
        const std::uint32_t isHit = fighter.Node("string_equal", {"", "hit"});
        const std::uint32_t branch = fighter.Node("branch");
        const TestGraph::Increment hit = fighter.AddToVariable("hits", 1);
        fighter.Wire(notify, 1U, isHit, 0U).Wire(isHit, 0U, branch, 1U).Exec(notify, branch).Exec(branch, hit.set);
        const std::uint32_t endedEvent = fighter.Node("event_montage_ended");
        const std::uint32_t ended = fighter.Node("set_variable", {}, "ended");
        fighter.Exec(endedEvent, ended).Wire(endedEvent, 1U, ended, 1U);
    }
    ASSERT_TRUE(fileSystem.WriteFile("fighter.uvbp", fighter.Bytes()));

    // A half-second swing of the hips, in a montage with one "hit" notify.
    Asset::AnimationClipAsset clip;
    clip.clipId = "swing";
    clip.durationSeconds = 0.5;
    Asset::AnimationAssetSample start;
    start.pose.position = Math::Vector3{0.0F, 1.0F, 0.0F};
    Asset::AnimationAssetSample end = start;
    end.timeSeconds = 0.5;
    end.pose.position = Math::Vector3{0.0F, 2.0F, 0.0F};
    clip.bones = {Asset::AnimationAssetBoneTrack{"Hips", {start, end}}};
    const std::filesystem::path clipPath = "uve_engine_core_tests_swing.uvanim";
    ASSERT_TRUE(Asset::SaveAnimationClipAsset(clip, clipPath));
    const Asset::AssetGuid clipGuid = assetDatabase.Register(clipPath);
    Asset::MotionMontageAsset montage;
    Asset::AddMontageSegment(montage, clipGuid, 0.5);
    Asset::AddMontageNotify(montage, "hit", 0.2, 0U);
    montage.blendInSeconds = 0.0;
    montage.blendOutSeconds = 0.0;
    const std::filesystem::path montagePath = "Attack.uvmontage";
    ASSERT_TRUE(Asset::SaveMotionMontageAsset(montage, montagePath));
    const Asset::AssetGuid montageGuid = assetDatabase.Register(montagePath);

    const Scene::Entity character = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, character, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::ScriptComponent>(character, Scene::ScriptComponent{"fighter.uvbp"});
    const Scene::Entity skeletonEntity = entityManager.CreateEntity();
    Scene::Skeleton3DObjectDefinition skeletonDefinition;
    Scene::SkeletonBone hips;
    hips.name = "Hips";
    hips.localPosition = Math::Vector3{0.0F, 1.0F, 0.0F};
    skeletonDefinition.skeleton.bones = {hips};
    Scene::ApplySkeleton3DObjectDefinition(entityManager, skeletonEntity, skeletonDefinition);
    sceneGraph.SetParent(entityManager, skeletonEntity, character);
    const Scene::Entity montages = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, montages, Scene::TransformComponent{});
    Scene::MontageSequencerComponent slots;
    slots.montage1 = montageGuid;
    entityManager.AddComponent<Scene::MontageSequencerComponent>(montages, slots);
    sceneGraph.SetParent(entityManager, montages, character);

    bool posedMidway = false;
    const auto startedAt = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startedAt < std::chrono::seconds(10)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        engine.TickFrame();
        const auto& pose = entityManager.GetComponent<Scene::Skeleton3DComponent>(skeletonEntity).pose;
        posedMidway = posedMidway || (!pose.empty() && pose[0].position.y > 1.05F);
        Blueprint::ScriptInstance* const instance = engine.FindUVScriptInstance(character);
        if (instance != nullptr && instance->GetField("ended") == Blueprint::Value{std::string{"Attack"}}) {
            break;
        }
    }
    Blueprint::ScriptInstance* const instance = engine.FindUVScriptInstance(character);
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(instance->GetField("ended"), Blueprint::Value{std::string{"Attack"}});
    EXPECT_EQ(instance->GetField("hits"), Blueprint::Value{std::int64_t{1}}) << "the hit notify once";
    EXPECT_TRUE(posedMidway) << "the swing posed the skeleton while it played";
    EXPECT_TRUE(entityManager.GetComponent<Scene::Skeleton3DComponent>(skeletonEntity).pose.empty())
        << "nothing else animates it, so it is back at rest";

    std::filesystem::remove(clipPath);
    std::filesystem::remove(montagePath);
    std::filesystem::remove_all(mountDirectory);
    std::filesystem::remove(MakeTestConfig().assetDatabaseFilePath);
    engine.Shutdown();
}

TEST(EngineCoreTest, RayCast3DObject_HitsRealGroundColliderExcludesItselfAndMissesBeyondLength) {
    // EngineCore::SyncRayCast3DObjects() is new wiring: previously RayCast3DComponent
    // was pure authored data with nothing evaluating it. This proves a real per-frame raycast
    // against a real Physics::RaycastSystem + real colliders, not a mocked query.
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    // Static ground: a flat box collider centered at the origin, top surface at y=0.5.
    const Scene::Entity ground = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, ground, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::ColliderComponent>(
        ground, Scene::ColliderComponent{Math::Vector3{10.0F, 0.5F, 10.0F}});

    // The raycasting entity sits 5 units above the ground, has its own collider (to prove
    // self-exclusion really works - without it, the closest "hit" would be itself at distance 0),
    // and casts a local-space "-Y" ray far enough to reach the ground.
    const Scene::Entity caster = entityManager.CreateEntity();
    Scene::TransformComponent casterTransform;
    casterTransform.localPosition = Math::Vector3{0.0F, 5.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, caster, casterTransform);
    entityManager.AddComponent<Scene::ColliderComponent>(caster, Scene::ColliderComponent{});
    Scene::RayCast3DComponent rayCast;
    rayCast.direction = Math::Vector3{0.0F, -1.0F, 0.0F};
    rayCast.length = 10.0F;
    entityManager.AddComponent<Scene::RayCast3DComponent>(caster, rayCast);

    engine.TickFrame();

    const Scene::RayCast3DComponent& afterHit =
        entityManager.GetComponent<Scene::RayCast3DComponent>(caster);
    EXPECT_TRUE(afterHit.hit);
    EXPECT_EQ(afterHit.hitEntity, ground);
    EXPECT_NEAR(afterHit.hitPosition.y, 0.5F, 0.01F);
    EXPECT_NEAR(afterHit.hitNormal.y, 1.0F, 0.01F);

    // Shortening the ray so it can't reach the ground (top at y=0.5, caster at y=5, so a length of
    // 1.0 falls well short) must report a clean miss, not a stale hit from the previous frame.
    Scene::RayCast3DComponent& live = entityManager.GetComponent<Scene::RayCast3DComponent>(caster);
    live.length = 1.0F;
    engine.TickFrame();
    EXPECT_FALSE(entityManager.GetComponent<Scene::RayCast3DComponent>(caster).hit);

    engine.Shutdown();
}

TEST(EngineCoreTest, RayCast3DObject_ExclusionsReachTheQueryAndAnInvalidListClearsTheWholeResult) {
    // The authored exclusions are not decoration: SyncRayCast3DObjects hands the component's live
    // prefix to the query as Physics::RaycastQuery::excludedEntities, so naming an object really
    // does take it out of the cast. A list the component's own rule refuses (a live reference behind
    // an empty slot) is a gate like any other, and every gate clears the WHOLE result - a consumer
    // that reads hitEntity without checking hit must never see last frame's object.
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    // Two flat blockers under the caster, the nearer one first: tops at y=4.5 and y=3.5.
    const Scene::Entity nearer = entityManager.CreateEntity();
    Scene::TransformComponent nearerTransform;
    nearerTransform.localPosition = Math::Vector3{0.0F, 4.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, nearer, nearerTransform);
    entityManager.AddComponent<Scene::ColliderComponent>(
        nearer, Scene::ColliderComponent{Math::Vector3{10.0F, 0.5F, 10.0F}});

    const Scene::Entity farther = entityManager.CreateEntity();
    Scene::TransformComponent fartherTransform;
    fartherTransform.localPosition = Math::Vector3{0.0F, 3.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, farther, fartherTransform);
    entityManager.AddComponent<Scene::ColliderComponent>(
        farther, Scene::ColliderComponent{Math::Vector3{10.0F, 0.5F, 10.0F}});

    const Scene::Entity caster = entityManager.CreateEntity();
    Scene::TransformComponent casterTransform;
    casterTransform.localPosition = Math::Vector3{0.0F, 5.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, caster, casterTransform);
    Scene::RayCast3DComponent rayCast;
    rayCast.direction = Math::Vector3{0.0F, -1.0F, 0.0F};
    rayCast.length = 10.0F;
    entityManager.AddComponent<Scene::RayCast3DComponent>(caster, rayCast);

    engine.TickFrame();
    EXPECT_TRUE(entityManager.GetComponent<Scene::RayCast3DComponent>(caster).hit);
    EXPECT_EQ(entityManager.GetComponent<Scene::RayCast3DComponent>(caster).hitEntity, nearer);

    // Naming the nearest blocker moves the ray onto the next one - and the exclusions array is a
    // prefix, so one live slot is exactly one exclusion.
    Scene::RayCast3DComponent& live =
        entityManager.GetComponent<Scene::RayCast3DComponent>(caster);
    live.exclusions[0] = nearer;
    engine.TickFrame();
    const Scene::RayCast3DComponent& skippingNearest =
        entityManager.GetComponent<Scene::RayCast3DComponent>(caster);
    EXPECT_TRUE(skippingNearest.hit);
    EXPECT_EQ(skippingNearest.hitEntity, farther);
    EXPECT_NEAR(skippingNearest.hitPosition.y, 3.5F, 0.01F);

    // Both named: nothing is left in the ray's path.
    live.exclusions[1] = farther;
    engine.TickFrame();
    EXPECT_FALSE(entityManager.GetComponent<Scene::RayCast3DComponent>(caster).hit);

    // A hole in the list is refused by the component's validator, which is a gate like any other:
    // the stale hit is cleared rather than kept.
    live.exclusions[0] = Scene::kInvalidEntity;
    engine.TickFrame();
    const Scene::RayCast3DComponent& invalidList =
        entityManager.GetComponent<Scene::RayCast3DComponent>(caster);
    EXPECT_FALSE(invalidList.hit);
    EXPECT_EQ(invalidList.hitEntity, Scene::kInvalidEntity);
    EXPECT_EQ(invalidList.hitPosition, Math::Vector3{});

    // Switching the ray off clears the whole result too - no hit, no point, no normal, no entity.
    live.exclusions = Scene::MakeEmptyEntityReferences<Scene::kMaximumRayCastExclusions>();
    live.enabled = false;
    engine.TickFrame();
    const Scene::RayCast3DComponent& disabled =
        entityManager.GetComponent<Scene::RayCast3DComponent>(caster);
    EXPECT_FALSE(disabled.hit);
    EXPECT_EQ(disabled.hitEntity, Scene::kInvalidEntity);
    EXPECT_EQ(disabled.hitNormal, Math::Vector3{});

    // And nothing is sticky: an enabled ray with a cleared list finds the nearest blocker again.
    live.enabled = true;
    engine.TickFrame();
    EXPECT_TRUE(entityManager.GetComponent<Scene::RayCast3DComponent>(caster).hit);
    EXPECT_EQ(entityManager.GetComponent<Scene::RayCast3DComponent>(caster).hitEntity, nearer);

    engine.Shutdown();
}

TEST(EngineCoreTest, Hitbox3DObject_StrikesOverlappingHurtboxAndClearsWhenGatedOrApart) {
    // EngineCore::SyncHitbox3DObjects() is new wiring: previously Hitbox3DComponent
    // and Hurtbox3DComponent were pure authored data with nothing evaluating them. This
    // proves the real per-frame pairing - exact oriented-box overlap with symmetric layer/mask
    // acceptance, damage-channel equality, and self-exclusion - and that every gate clears
    // stale strikes instead of keeping the previous frame's list.
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    // Attacker at the origin with default combat volumes (half extents 0.5, layer 1, full mask,
    // channel "default"); victim parked 0.5 units away so the boxes overlap 0.5 units along X.
    const Scene::Entity attacker = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, attacker, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::Hitbox3DComponent>(
        attacker, Scene::Hitbox3DComponent{});

    const Scene::Entity victim = entityManager.CreateEntity();
    Scene::TransformComponent victimTransform;
    victimTransform.localPosition = Math::Vector3{0.5F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, victim, victimTransform);
    entityManager.AddComponent<Scene::Hurtbox3DComponent>(
        victim, Scene::Hurtbox3DComponent{});

    engine.TickFrame();
    {
        const Scene::Hitbox3DComponent& afterStrike =
            entityManager.GetComponent<Scene::Hitbox3DComponent>(attacker);
        ASSERT_EQ(afterStrike.strikeCount, 1U);
        EXPECT_EQ(afterStrike.strikes[0U].hurtboxEntity, victim);
        EXPECT_NEAR(afterStrike.strikes[0U].penetrationDepth, 0.5F, 0.01F);
        EXPECT_FALSE(afterStrike.strikesTruncated);
    }

    // Moving the hurtbox far out of range must clear the strike, not keep the stale one.
    Scene::TransformComponent victimLive =
        entityManager.GetComponent<Scene::TransformComponent>(victim);
    victimLive.localPosition = Math::Vector3{10.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, victim, victimLive);
    engine.TickFrame();
    EXPECT_EQ(entityManager.GetComponent<Scene::Hitbox3DComponent>(attacker).strikeCount, 0U);

    // Back in range but on a different damage channel: no strike.
    victimLive.localPosition = Math::Vector3{0.5F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, victim, victimLive);
    Scene::Hurtbox3DComponent& hurtboxLive =
        entityManager.GetComponent<Scene::Hurtbox3DComponent>(victim);
    hurtboxLive.damageChannel = "environment";
    engine.TickFrame();
    EXPECT_EQ(entityManager.GetComponent<Scene::Hitbox3DComponent>(attacker).strikeCount, 0U);

    // Same channel again but the hurtbox's mask no longer accepts the hitbox's layer: no strike
    // (acceptance is symmetric - both sides must accept each other).
    hurtboxLive.damageChannel = "default";
    hurtboxLive.collisionMask = 0U;
    engine.TickFrame();
    EXPECT_EQ(entityManager.GetComponent<Scene::Hitbox3DComponent>(attacker).strikeCount, 0U);

    // Mask restored but the hitbox itself is disabled: no strike, and none may linger.
    hurtboxLive.collisionMask = 0xFFFFFFFFU;
    Scene::Hitbox3DComponent& hitboxLive =
        entityManager.GetComponent<Scene::Hitbox3DComponent>(attacker);
    hitboxLive.enabled = false;
    engine.TickFrame();
    EXPECT_EQ(entityManager.GetComponent<Scene::Hitbox3DComponent>(attacker).strikeCount, 0U);

    // Re-enabled: the strike returns. A hurtbox on the attacker's OWN entity must not add a
    // second (self) strike - the victim stays the only recorded hit.
    hitboxLive.enabled = true;
    entityManager.AddComponent<Scene::Hurtbox3DComponent>(
        attacker, Scene::Hurtbox3DComponent{});
    engine.TickFrame();
    {
        const Scene::Hitbox3DComponent& afterStrike =
            entityManager.GetComponent<Scene::Hitbox3DComponent>(attacker);
        ASSERT_EQ(afterStrike.strikeCount, 1U);
        EXPECT_EQ(afterStrike.strikes[0U].hurtboxEntity, victim);
    }

    engine.Shutdown();
}

TEST(EngineCoreTest, Hitbox3DObject_StrikeEdgesReachTheEventSystemOnceAndEndWhenGated) {
    // The component pair above proves the state - "this hitbox is touching this hurtbox right now".
    // What a gameplay system can actually act on is the edge: this strike started, this strike
    // ended. The engine owns the diff and queues one typed event per edge, so damage-on-Entered
    // fires once per hit instead of once per frame, and an exit is reported even when the reason is
    // a gate rather than the boxes moving apart.
    //
    // Queued events surface one TickFrame() later, the same contract
    // AreaOverlapLifecycle_QueuesEnteredAndExitedEvents locks: QueueEvent()'d events are not
    // drained until the next frame's DispatchQueued(). Every observation below therefore
    // happens on the tick AFTER the one that caused it - which is the contract, not a quirk.
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    auto& services = engine.GetServices();
    auto& entityManager = services.GetEntityManager();
    auto& sceneGraph = services.GetSceneGraph();

    const Scene::Entity blade = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, blade, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::Hitbox3DComponent>(
        blade, Scene::Hitbox3DComponent{Math::Vector3{1.0F, 1.0F, 1.0F}});

    // 1.5 m apart, so the two 1 m half-extent boxes overlap by exactly 0.5 m along X and the
    // minimum-translation axis is unambiguous - coincident boxes tie on all three axes and their
    // minimum translation is the full 2 m, which is a much weaker thing to assert.
    const Scene::Entity victim = entityManager.CreateEntity();
    Scene::TransformComponent victimTransform;
    victimTransform.localPosition = Math::Vector3{1.5F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, victim, victimTransform);
    entityManager.AddComponent<Scene::Hurtbox3DComponent>(
        victim, Scene::Hurtbox3DComponent{Math::Vector3{1.0F, 1.0F, 1.0F}});

    std::vector<Physics::Hitbox3DStrikeEnteredEvent> entered;
    std::vector<Physics::Hitbox3DStrikeExitedEvent> exited;
    services.GetEventSystem().Subscribe<Physics::Hitbox3DStrikeEnteredEvent>(
        [&entered](const Physics::Hitbox3DStrikeEnteredEvent& event) { entered.push_back(event); });
    services.GetEventSystem().Subscribe<Physics::Hitbox3DStrikeExitedEvent>(
        [&exited](const Physics::Hitbox3DStrikeExitedEvent& event) { exited.push_back(event); });

    engine.TickFrame();
    // The pairing is real on this tick and the edge is queued - but not delivered yet.
    EXPECT_EQ(entityManager.GetComponent<Scene::Hitbox3DComponent>(blade).strikeCount, 1U);
    EXPECT_TRUE(entered.empty());
    EXPECT_TRUE(exited.empty());

    engine.TickFrame();
    ASSERT_EQ(entered.size(), 1U) << "a 1 m box and a 1 m box 1.5 m apart overlap by 0.5 m";
    EXPECT_EQ(entered[0].strike.hitbox, blade);
    EXPECT_EQ(entered[0].strike.hurtbox, victim);
    EXPECT_NEAR(entered[0].strike.penetrationDepth, 0.5F, 1.0e-4F);
    EXPECT_NEAR(entered[0].strike.axis.x, 1.0F, 1.0e-4F)
        << "a consequence that pushes needs the direction, not only the fact";
    EXPECT_TRUE(exited.empty());

    // Still overlapping on the next tick: same pair, no second edge, and nothing to deliver.
    engine.TickFrame();
    EXPECT_EQ(entered.size(), 1U);
    EXPECT_TRUE(exited.empty());

    // The hurtbox is switched off without moving: the strike ends because the gate closed, and the
    // exit is queued on that tick while the volumes still overlap geometrically.
    entityManager.GetComponent<Scene::Hurtbox3DComponent>(victim).enabled = false;
    engine.TickFrame();
    EXPECT_EQ(entityManager.GetComponent<Scene::Hitbox3DComponent>(blade).strikeCount, 0U);
    EXPECT_TRUE(exited.empty());
    engine.TickFrame();
    ASSERT_EQ(exited.size(), 1U);
    EXPECT_EQ(exited[0].strike.hitbox, blade);
    EXPECT_EQ(exited[0].strike.hurtbox, victim);
    EXPECT_EQ(entered.size(), 1U);

    // Switched back on while still overlapping: a fresh strike with a fresh edge, because the
    // previous one was ended by the gate rather than paused by it.
    entityManager.GetComponent<Scene::Hurtbox3DComponent>(victim).enabled = true;
    engine.TickFrame();
    EXPECT_EQ(entityManager.GetComponent<Scene::Hitbox3DComponent>(blade).strikeCount, 1U);
    engine.TickFrame();
    EXPECT_EQ(entered.size(), 2U);
    EXPECT_EQ(exited.size(), 1U);

    engine.Shutdown();
}

TEST(EngineCoreTest, InteractionArea3DObject_TracksInteractorsFocusesTheNearestAndClearsWhenGated) {
    // EngineCore::SyncInteractionArea3DObjects() is new wiring: previously
    // InteractionArea3DComponent was pure authored data with nothing evaluating it - a
    // game had to hand-roll the whole "what can I interact with" loop. This proves the real
    // per-frame behaviour: the character-controller player populates overlapping areas'
    // interactor lists, exactly one area - the nearest - earns focusedByPrimaryInteractor,
    // the bounded list reports its own overflow, and every gate clears stale state instead of
    // keeping the previous frame's answers.
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    // The player: a character controller with the default box collider (0.5 half extents) at the
    // origin. Area A is one step away; area B starts out of range.
    const Scene::Entity player = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, player, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::ColliderComponent>(player, Scene::ColliderComponent{});
    entityManager.AddComponent<Scene::CharacterControllerComponent>(player);

    const Scene::Entity areaA = entityManager.CreateEntity();
    Scene::TransformComponent transformA;
    transformA.localPosition = Math::Vector3{0.4F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, areaA, transformA);
    entityManager.AddComponent<Scene::InteractionArea3DComponent>(
        areaA, Scene::InteractionArea3DComponent{});

    const Scene::Entity areaB = entityManager.CreateEntity();
    Scene::TransformComponent transformB;
    transformB.localPosition = Math::Vector3{30.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, areaB, transformB);
    entityManager.AddComponent<Scene::InteractionArea3DComponent>(
        areaB, Scene::InteractionArea3DComponent{});

    engine.TickFrame();
    {
        const Scene::InteractionArea3DComponent& live =
            entityManager.GetComponent<Scene::InteractionArea3DComponent>(areaA);
        ASSERT_EQ(live.interactorCount, 1U);
        EXPECT_EQ(live.interactors[0U], player);
        EXPECT_FALSE(live.interactorsTruncated);
        EXPECT_TRUE(live.focusedByPrimaryInteractor);
    }
    {
        const Scene::InteractionArea3DComponent& live =
            entityManager.GetComponent<Scene::InteractionArea3DComponent>(areaB);
        EXPECT_EQ(live.interactorCount, 0U);
        EXPECT_FALSE(live.focusedByPrimaryInteractor);
    }

    // Pull B barely into range (its center lands farther than A's): both list the player, but
    // only the NEAREST one keeps the focus.
    transformB.localPosition = Math::Vector3{0.45F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, areaB, transformB);
    engine.TickFrame();
    EXPECT_EQ(entityManager.GetComponent<Scene::InteractionArea3DComponent>(areaB)
                  .interactorCount,
              1U);
    EXPECT_TRUE(entityManager.GetComponent<Scene::InteractionArea3DComponent>(areaA)
                    .focusedByPrimaryInteractor);
    EXPECT_FALSE(entityManager.GetComponent<Scene::InteractionArea3DComponent>(areaB)
                     .focusedByPrimaryInteractor);

    // B ends up the nearer of the two: the focus must MOVE to it, not stick to first-come.
    transformB.localPosition = Math::Vector3{0.1F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, areaB, transformB);
    engine.TickFrame();
    EXPECT_FALSE(entityManager.GetComponent<Scene::InteractionArea3DComponent>(areaA)
                     .focusedByPrimaryInteractor);
    EXPECT_TRUE(entityManager.GetComponent<Scene::InteractionArea3DComponent>(areaB)
                    .focusedByPrimaryInteractor);

    // Disabling B hands the focus back to A - nothing stale may survive the gate.
    entityManager.GetComponent<Scene::InteractionArea3DComponent>(areaB).enabled = false;
    engine.TickFrame();
    {
        const Scene::InteractionArea3DComponent& liveB =
            entityManager.GetComponent<Scene::InteractionArea3DComponent>(areaB);
        EXPECT_EQ(liveB.interactorCount, 0U);
        EXPECT_FALSE(liveB.focusedByPrimaryInteractor);
    }
    EXPECT_TRUE(entityManager.GetComponent<Scene::InteractionArea3DComponent>(areaA)
                    .focusedByPrimaryInteractor);

    // The player's collider mask stops accepting either side: participation ends, and with no
    // interactor left the focus goes away entirely - fail-closed, not last-known.
    entityManager.GetComponent<Scene::InteractionArea3DComponent>(areaB).enabled = true;
    Scene::ColliderComponent& playerCollider =
        entityManager.GetComponent<Scene::ColliderComponent>(player);
    playerCollider.collisionMask = 0U;
    engine.TickFrame();
    EXPECT_EQ(entityManager.GetComponent<Scene::InteractionArea3DComponent>(areaA)
                  .interactorCount,
              0U);
    EXPECT_FALSE(entityManager.GetComponent<Scene::InteractionArea3DComponent>(areaA)
                     .focusedByPrimaryInteractor);
    EXPECT_EQ(entityManager.GetComponent<Scene::InteractionArea3DComponent>(areaB)
                  .interactorCount,
              0U);

    // Mask restored and B moved back out of range, plus a second controller on the same spot:
    // with an authored candidate budget of one, the bounded list keeps exactly one entry and
    // REPORTS its overflow instead of silently dropping the extra interactor or overwriting past
    // the cap.
    playerCollider.collisionMask = 0xFFFFFFFFU;
    transformB.localPosition = Math::Vector3{30.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, areaB, transformB);
    Scene::InteractionArea3DComponent& liveA =
        entityManager.GetComponent<Scene::InteractionArea3DComponent>(areaA);
    liveA.maximumCandidates = 1U;
    const Scene::Entity secondPlayer = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, secondPlayer, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::ColliderComponent>(
        secondPlayer, Scene::ColliderComponent{});
    entityManager.AddComponent<Scene::CharacterControllerComponent>(secondPlayer);
    engine.TickFrame();
    {
        const Scene::InteractionArea3DComponent& afterFill =
            entityManager.GetComponent<Scene::InteractionArea3DComponent>(areaA);
        EXPECT_EQ(afterFill.interactorCount, 1U);
        EXPECT_TRUE(afterFill.interactorsTruncated);
        // The primary interactor is the first content-ordered controller, and the focus verdict
        // is independent of the bounded list: with B out of range, A is the only area the
        // primary overlaps, so it stays focused regardless of the truncated extra entry.
        EXPECT_TRUE(afterFill.focusedByPrimaryInteractor);
    }

    // A controller WITHOUT a collider has no overlap volume and must never appear.
    const Scene::Entity colliderless = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, colliderless, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::CharacterControllerComponent>(colliderless);
    engine.TickFrame();
    {
        const Scene::InteractionArea3DComponent& afterFill =
            entityManager.GetComponent<Scene::InteractionArea3DComponent>(areaA);
        EXPECT_EQ(afterFill.interactorCount, 1U);
        for (std::size_t index = 0; index < afterFill.interactorCount; ++index) {
            EXPECT_NE(afterFill.interactors[index], colliderless);
        }
    }

    engine.Shutdown();
}

TEST(EngineCoreTest, Hitbox3DObject_HurtboxRotationIsHonoredByExactObbOverlap) {
    // The strike test must be an exact ORIENTED-box test, not a conservative axis-aligned one.
    // Both boxes are long thin rods along X; the hurtbox sits 2 units to the side. Unrotated it
    // clearly overlaps (x in [0.5, 3.5] vs the hitbox's [-1.5, 1.5]); rotated 90 degrees about Z
    // its long axis becomes vertical (x only in [1.9, 2.1]) and it must NOT strike. A pairing
    // that ignored rotation would fail this test in the always-overlapping direction.
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    const Scene::Entity attacker = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, attacker, Scene::TransformComponent{});
    Scene::Hitbox3DComponent hitbox;
    hitbox.halfExtents = Math::Vector3{1.5F, 0.1F, 0.1F};
    entityManager.AddComponent<Scene::Hitbox3DComponent>(attacker, hitbox);

    const Scene::Entity victim = entityManager.CreateEntity();
    Scene::TransformComponent victimTransform;
    victimTransform.localPosition = Math::Vector3{2.0F, 0.0F, 0.0F};
    victimTransform.localRotation =
        Math::Quaternion{0.0F, 0.0F, 0.70710678F, 0.70710678F}; // 90 degrees about +Z
    sceneGraph.AttachTransform(entityManager, victim, victimTransform);
    Scene::Hurtbox3DComponent hurtbox;
    hurtbox.halfExtents = Math::Vector3{1.5F, 0.1F, 0.1F};
    entityManager.AddComponent<Scene::Hurtbox3DComponent>(victim, hurtbox);

    engine.TickFrame();
    EXPECT_EQ(entityManager.GetComponent<Scene::Hitbox3DComponent>(attacker).strikeCount, 0U);

    // Removing the rotation turns the hurtbox back along X: it must strike. The recorded depth
    // is the minimum-translation-axis penetration, i.e. the thinnest separating direction - for
    // two horizontal rods stacked in Y that is the Y axis (0.1 + 0.1 extents, fully overlapped),
    // not the 1.0-unit X overlap.
    Scene::TransformComponent victimLive =
        entityManager.GetComponent<Scene::TransformComponent>(victim);
    victimLive.localRotation = Math::Quaternion{};
    sceneGraph.SetLocalTransform(entityManager, victim, victimLive);
    engine.TickFrame();
    {
        const Scene::Hitbox3DComponent& afterStrike =
            entityManager.GetComponent<Scene::Hitbox3DComponent>(attacker);
        ASSERT_EQ(afterStrike.strikeCount, 1U);
        EXPECT_EQ(afterStrike.strikes[0U].hurtboxEntity, victim);
        EXPECT_NEAR(afterStrike.strikes[0U].penetrationDepth, 0.2F, 0.01F);
    }

    engine.Shutdown();
}

TEST(EngineCoreTest, Projectile3DObject_StopsOnAWallAndQueuesTheHitAsATypedEvent) {
    // The engine's half of the hit decision, end to end: the swept sphere finds the wall, the
    // authored Stop policy halts the projectile at the contact, and the contact is delivered as a
    // typed event the gameplay side can subscribe to - nothing here decides what a hit *means*.
    EngineConfig config = MakeTestConfig();
    config.fixedUpdateFps = 1000.0;
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    auto& services = engine.GetServices();
    auto& entityManager = services.GetEntityManager();
    auto& sceneGraph = services.GetSceneGraph();

    // The wall's near face is at x = 0.5, so a 0.1m sphere touches it when its centre reaches
    // x = 0.4. The projectile starts 5mm short of that, so the first fixed step is the step that
    // resolves the contact.
    const Scene::Entity wall = entityManager.CreateEntity();
    Scene::TransformComponent wallTransform;
    wallTransform.localPosition = Math::Vector3{1.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, wall, wallTransform);
    entityManager.AddComponent<Scene::ColliderComponent>(
        wall, Scene::ColliderComponent{Math::Vector3{0.5F, 0.5F, 0.5F}});

    const Scene::Entity bullet = entityManager.CreateEntity();
    Scene::TransformComponent bulletTransform;
    bulletTransform.localPosition = Math::Vector3{0.395F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, bullet, bulletTransform);
    Scene::Projectile3DComponent projectile;
    projectile.velocity = Math::Vector3{6.0F, 0.0F, 0.0F};
    projectile.radius = 0.1F;
    entityManager.AddComponent<Scene::Projectile3DComponent>(bullet, projectile);
    sceneGraph.Update(entityManager);

    std::vector<Physics::Projectile3DHitEvent> hits;
    services.GetEventSystem().Subscribe<Physics::Projectile3DHitEvent>(
        [&hits](const Physics::Projectile3DHitEvent& event) { hits.push_back(event); });

    for (int frame = 0; frame < 4; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        engine.TickFrame();
    }

    const Scene::Projectile3DComponent& after =
        entityManager.GetComponent<Scene::Projectile3DComponent>(bullet);
    EXPECT_FALSE(after.active);
    EXPECT_NEAR(after.velocity.x, 0.0F, 1.0e-4F);
    EXPECT_TRUE(after.hit);
    EXPECT_EQ(after.hitEntity, wall);
    EXPECT_NEAR(after.impactSpeed, 6.0F, 1.0e-2F);
    EXPECT_NEAR(after.hitPosition.x, 0.5F, 1.0e-2F);

    ASSERT_FALSE(hits.empty()) << "the contact has to reach gameplay as an event, not only as state";
    EXPECT_EQ(hits.front().projectile, bullet);
    EXPECT_EQ(hits.front().hitEntity, wall);
    EXPECT_EQ(hits.front().policy, Scene::Projectile3DHitPolicy::Stop);
    EXPECT_TRUE(hits.front().stopped);
    EXPECT_NEAR(hits.front().impactSpeed, 6.0F, 1.0e-2F);

    engine.Shutdown();
}

TEST(EngineCoreTest, Projectile3DObject_BouncesOffAWallAndReportsThatItSurvivedTheHit) {
    EngineConfig config = MakeTestConfig();
    config.fixedUpdateFps = 1000.0;
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    auto& services = engine.GetServices();
    auto& entityManager = services.GetEntityManager();
    auto& sceneGraph = services.GetSceneGraph();

    const Scene::Entity wall = entityManager.CreateEntity();
    Scene::TransformComponent wallTransform;
    wallTransform.localPosition = Math::Vector3{1.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, wall, wallTransform);
    entityManager.AddComponent<Scene::ColliderComponent>(
        wall, Scene::ColliderComponent{Math::Vector3{0.5F, 0.5F, 0.5F}});

    const Scene::Entity ball = entityManager.CreateEntity();
    Scene::TransformComponent ballTransform;
    ballTransform.localPosition = Math::Vector3{0.395F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, ball, ballTransform);
    Scene::Projectile3DComponent projectile;
    projectile.velocity = Math::Vector3{6.0F, 0.0F, 0.0F};
    projectile.radius = 0.1F;
    projectile.hitPolicy = Scene::Projectile3DHitPolicy::Bounce;
    projectile.restitution = 1.0F;
    projectile.friction = 0.0F;
    entityManager.AddComponent<Scene::Projectile3DComponent>(ball, projectile);
    sceneGraph.Update(entityManager);

    std::vector<Physics::Projectile3DHitEvent> hits;
    services.GetEventSystem().Subscribe<Physics::Projectile3DHitEvent>(
        [&hits](const Physics::Projectile3DHitEvent& event) { hits.push_back(event); });

    for (int frame = 0; frame < 4; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        engine.TickFrame();
    }

    const Scene::Projectile3DComponent& after =
        entityManager.GetComponent<Scene::Projectile3DComponent>(ball);
    EXPECT_TRUE(after.active) << "a bounce keeps flying";
    EXPECT_EQ(after.bounceCount, 1U);
    EXPECT_NEAR(after.velocity.x, -6.0F, 1.0e-2F);
    EXPECT_LT(entityManager.GetComponent<Scene::TransformComponent>(ball).localPosition.x, 0.4F);

    ASSERT_FALSE(hits.empty());
    EXPECT_EQ(hits.front().policy, Scene::Projectile3DHitPolicy::Bounce);
    EXPECT_FALSE(hits.front().stopped);
    EXPECT_EQ(hits.front().bounceCount, 1U);

    engine.Shutdown();
}

TEST(EngineCoreTest, Projectile3DObject_IntegratesVelocityAccelerationAndExpiresAfterLifetime) {
    // EngineCore::SyncProjectile3DObjects() is new wiring: previously
    // Projectile3DComponent was pure authored data with nothing moving it or expiring it.
    EngineConfig config = MakeTestConfig();
    config.fixedUpdateFps = 1000.0;
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    const Scene::Entity entity = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, entity, Scene::TransformComponent{});
    Scene::Projectile3DComponent projectile;
    projectile.velocity = Math::Vector3{0.0F, 1.0F, 0.0F};
    projectile.acceleration = Math::Vector3{0.0F, -1.0F, 0.0F};
    projectile.maxLifetime = 0.05F;
    projectile.remainingLifetime = 0.05F;
    entityManager.AddComponent<Scene::Projectile3DComponent>(entity, projectile);

    // A few fixed steps at 1kHz (~6ms of simulated time) - enough to move and to have accumulated
    // some deceleration from `acceleration`, nowhere near the 50ms lifetime yet.
    for (int frame = 0; frame < 3; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        engine.TickFrame();
    }

    {
        const Scene::TransformComponent& transform =
            entityManager.GetComponent<Scene::TransformComponent>(entity);
        EXPECT_GT(transform.localPosition.y, 0.0F);
        const Scene::Projectile3DComponent& live =
            entityManager.GetComponent<Scene::Projectile3DComponent>(entity);
        EXPECT_LT(live.velocity.y, 1.0F);
        EXPECT_TRUE(live.active);
    }

    // Far more real time than the 50ms lifetime, so it must have fully expired by now regardless
    // of scheduling jitter.
    for (int frame = 0; frame < 100; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        engine.TickFrame();
    }

    const Scene::Projectile3DComponent& afterExpiry =
        entityManager.GetComponent<Scene::Projectile3DComponent>(entity);
    EXPECT_FALSE(afterExpiry.active);
    EXPECT_FLOAT_EQ(afterExpiry.remainingLifetime, 0.0F);

    engine.Shutdown();
}

TEST(EngineCoreTest, SaveGameSystemAndCheckpointManager_ReachableAndFunctionalAfterInit) {
    EngineConfig config = MakeTestConfig();
    config.saveDirectoryPath = "uve_engine_core_tests_saves";
    std::filesystem::remove_all(config.saveDirectoryPath);

    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Save::ISaveGameSystem& saveGameSystem = engine.GetServices().GetSaveGameSystem();
    Save::ICheckpointManager& checkpointManager = engine.GetServices().GetCheckpointManager();

    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    const Scene::Entity entity = entityManager.CreateEntity();
    ASSERT_TRUE(saveGameSystem.Save(0, entityManager, {entity}, Save::GameStateMetadata{}));
    EXPECT_TRUE(saveGameSystem.HasSave(0));

    EXPECT_TRUE(checkpointManager.Checkpoint(entityManager, {entity}));
    EXPECT_TRUE(saveGameSystem.HasSave(Save::kManualCheckpointSlotIndex));
    EXPECT_FALSE(saveGameSystem.HasSave(Save::kAutoSaveSlotIndex));
    EXPECT_TRUE(saveGameSystem.HasSave(0));

    const std::optional<Save::GameStateMetadata> checkpointMetadata =
        saveGameSystem.GetSaveMetadata(Save::kManualCheckpointSlotIndex);
    ASSERT_TRUE(checkpointMetadata.has_value());
    const Version engineVersion = engine.GetEngineVersion();
    EXPECT_EQ(checkpointMetadata->engineVersionMajor, engineVersion.major);
    EXPECT_EQ(checkpointMetadata->engineVersionMinor, engineVersion.minor);
    EXPECT_EQ(checkpointMetadata->engineVersionPatch, engineVersion.patch);
    EXPECT_EQ(checkpointMetadata->engineVersionBuild, engineVersion.build);

    engine.Shutdown();
    std::filesystem::remove_all(config.saveDirectoryPath);
}

TEST(EngineCoreTest, CheckpointManager_AutoSavesAfterConfiguredInterval_TickFrameDriven) {
    EngineConfig config = MakeTestConfig();
    config.saveDirectoryPath = "uve_engine_core_tests_autosave_saves";
    config.autoSaveIntervalSeconds = std::numeric_limits<double>::min(); // smallest positive interval
    std::filesystem::remove_all(config.saveDirectoryPath);

    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Save::ISaveGameSystem& saveGameSystem = engine.GetServices().GetSaveGameSystem();

    for (int frame = 0; frame < 3; ++frame) {
        engine.TickFrame();
    }

    EXPECT_TRUE(saveGameSystem.HasSave(Save::kAutoSaveSlotIndex));
    EXPECT_TRUE(saveGameSystem.ListUsedSlots().empty());

    engine.Shutdown();
    std::filesystem::remove_all(config.saveDirectoryPath);
}

TEST(EngineCoreTest, SimulationControl_PausesStepsQueuesOneStepAndSuppressesTransientCheckpoints) {
    EngineConfig config = MakeTestConfig();
    config.saveDirectoryPath = "uve_engine_core_tests_transient_saves";
    config.autoSaveIntervalSeconds = std::numeric_limits<double>::min(); // smallest positive interval
    std::filesystem::remove_all(config.saveDirectoryPath);

    EngineCore engine(config);
    EXPECT_FALSE(engine.SetSimulationExecutionMode(SimulationExecutionMode::Paused));
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Save::ISaveGameSystem& saveGameSystem = engine.GetServices().GetSaveGameSystem();
    Save::ICheckpointManager& checkpointManager = engine.GetServices().GetCheckpointManager();
    ASSERT_TRUE(engine.SetTransientSimulationSessionActive(true));
    ASSERT_TRUE(engine.SetSimulationExecutionMode(SimulationExecutionMode::Paused));
    EXPECT_TRUE(engine.RequestSingleSimulationStep());
    EXPECT_FALSE(engine.RequestSingleSimulationStep());
    engine.TickFrame();
    EXPECT_EQ(engine.GetSimulationExecutionMode(), SimulationExecutionMode::Paused);
    EXPECT_FALSE(saveGameSystem.HasSave(Save::kAutoSaveSlotIndex));
    EXPECT_DOUBLE_EQ(checkpointManager.GetElapsedSinceLastSaveSeconds(), 0.0);
    EXPECT_DOUBLE_EQ(checkpointManager.GetTotalPlaytimeSeconds(), 0.0);

    ASSERT_TRUE(engine.SetSimulationExecutionMode(SimulationExecutionMode::Running));
    ASSERT_TRUE(engine.SetTransientSimulationSessionActive(false));
    // A positive interval must observe a nonzero timer delta; allow the same three-frame cadence
    // used by the neighboring autosave integration test after the paused frame.
    for (int frame = 0; frame < 3; ++frame) {
        engine.TickFrame();
    }
    EXPECT_TRUE(saveGameSystem.HasSave(Save::kAutoSaveSlotIndex));

    engine.Shutdown();
    std::filesystem::remove_all(config.saveDirectoryPath);
}

TEST(EngineCoreTest, HeadlessCommandLineFlag_ForcesHeadlessAndUsesNullWindowManager) {
    // headless starts false here specifically to prove the hidden `headless` descriptor maps
    // the presence-only --headless flag through the highest-priority command-line settings layer.
    EngineConfig config = MakeTestConfig();
    config.headless = false;
    config.commandLineArgs = {"--headless"};

    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    EXPECT_EQ(engine.GetServices().GetWindowManager().GetBackendName(), "Null");

    for (int frame = 0; frame < 3; ++frame) {
        engine.TickFrame();
    }
    EXPECT_EQ(engine.GetFrameStats().frameNumber, 3U);

    engine.Shutdown();
}

// Needs a real (possibly virtual, e.g. Xvfb) X display and GL context. Skips cleanly with a clear
// message when unavailable, so the same uve_tests binary runs cleanly with or without a display -
// same GTEST_SKIP() pattern as WindowManagerTest/GlRenderDeviceTest.
TEST(EngineCoreTest, WindowedMode_ReachesRunningAndPresentsEmptyRendererScene) {
    EngineConfig config = MakeTestConfig();
    config.headless = false;
    config.windowWidth = 64;
    config.windowHeight = 64;
    config.vsyncEnabled = false;
    // This sandbox's Mesa/llvmpipe GLX stack caps at OpenGL 4.5 Core (confirmed by direct
    // testing - 4.6 fails with GLXBadFBConfig); see the identical override + rationale in
    // tests/window/window_manager_uve_tests.cpp. The shipped production default (4, 6) is
    // untouched.
    config.windowGlVersionMajor = 4;
    config.windowGlVersionMinor = 5;

    EngineCore engine(config);
    engine.Init();
    if (!engine.GetServices().GetWindowManager().IsValid()) {
        GTEST_SKIP() << "No display available for windowed EngineCore - skipping (run under "
                        "xvfb-run to exercise this test)";
    }
    ASSERT_TRUE(engine.Load());
    EXPECT_EQ(engine.GetServices().GetWindowManager().GetBackendName(), "GLFW3");

    // An active identity camera ensures this is an empty *renderer scene* test rather than a
    // no-camera EngineCore no-op test.
    EngineServices& services = engine.GetServices();
    Scene::IEntityManager& entityManager = services.GetEntityManager();
    Scene::ISceneGraph& sceneGraph = services.GetSceneGraph();
    const Scene::Entity camera = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, camera, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::CameraComponent>(camera);
    engine.SetActiveCamera(camera);

    std::array<unsigned char, 3> scenePixel{};
    GLenum postRenderGlError = GL_NO_ERROR;
    engine.SetPostRenderCallback([&scenePixel, &postRenderGlError] {
        glFinish();
        glReadBuffer(GL_BACK);
        glReadPixels(32, 32, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, scenePixel.data());
        postRenderGlError = glGetError();
    });

    // A few frames let Renderer3D complete scene, tone-mapping, and presentation work.
    for (int frame = 0; frame < 10; ++frame) {
        engine.TickFrame();
    }
    engine.SetPostRenderCallback({});
    EXPECT_EQ(engine.GetState(), EngineState::Running);
    const Render::Renderer3DFrameDiagnostics diagnostics =
        engine.GetServices().GetRenderer3D().GetLastFrameDiagnostics();
    EXPECT_EQ(diagnostics.renderTargetWidth, engine.GetServices().GetWindowManager().GetWidth());
    EXPECT_EQ(diagnostics.renderTargetHeight, engine.GetServices().GetWindowManager().GetHeight());

    // With no document render items, Renderer3D presents its deterministic charcoal
    // tone-mapped environment. This is intentionally not a demo-geometry assertion: visible
    // content enters only through ECS extraction into the renderer.
    EXPECT_EQ(scenePixel[0], 11U);
    EXPECT_EQ(scenePixel[1], 11U);
    EXPECT_EQ(scenePixel[2], 11U);
    EXPECT_EQ(postRenderGlError, GL_NO_ERROR);

    engine.Shutdown();
}

// Exercises the actual EngineCore -> Renderer3D -> tone-mapping -> GLFW default-framebuffer
// path. The fixed positions are intentionally far from geometry edges so a future expected-pixel
// assertion cannot be satisfied by the background alone.
TEST(EngineCoreTest, WindowedMode_PresentsDeterministicPrimitiveFixtureToDefaultFramebuffer) {
    EngineConfig config = MakeTestConfig();
    config.headless = false;
    config.windowWidth = 128U;
    config.windowHeight = 96U;
    config.renderTargetWidth = 128U;
    config.renderTargetHeight = 96U;
    config.vsyncEnabled = false;
    config.windowGlVersionMajor = 4U;
    config.windowGlVersionMinor = 5U;
    config.ambientColor = Math::Vector3{0.45F, 0.45F, 0.45F};

    EngineCore engine(config);
    engine.Init();
    if (!engine.GetServices().GetWindowManager().IsValid()) {
        GTEST_SKIP() << "No display available for windowed EngineCore - skipping (run under "
                        "xvfb-run to exercise this test)";
    }
    ASSERT_TRUE(engine.Load());

    EngineServices& services = engine.GetServices();
    Scene::IEntityManager& entityManager = services.GetEntityManager();
    Scene::ISceneGraph& sceneGraph = services.GetSceneGraph();
    const Scene::Entity camera = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, camera, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::CameraComponent>(camera);
    engine.SetActiveCamera(camera);

    Math::Quaternion planeFacingCamera{};
    ASSERT_TRUE(Math::TryMakeAxisAngle(Math::Vector3{1.0F, 0.0F, 0.0F},
                                          std::numbers::pi_v<float> * 0.5F, planeFacingCamera));
    const auto makePrimitive = [&entityManager, &sceneGraph](const Math::Vector3 position,
                                                               const Math::Quaternion rotation,
                                                               const Math::Vector3 scale,
                                                               const Scene::PrimitiveMeshKind kind,
                                                               const Math::Vector3 color) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform{};
        transform.localPosition = position;
        transform.localRotation = rotation;
        transform.localScale = scale;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        entityManager.AddComponent<Scene::PrimitiveMeshComponent>(
            entity, Scene::PrimitiveMeshComponent{kind, color});
    };
    makePrimitive(Math::Vector3{0.0F, 0.0F, -10.0F}, planeFacingCamera, Math::Vector3{6.0F, 6.0F, 1.0F},
                  Scene::PrimitiveMeshKind::Plane, Math::Vector3{0.10F, 0.72F, 0.20F});
    makePrimitive(Math::Vector3{-3.5F, 0.0F, -8.0F}, Math::Quaternion{}, Math::Vector3{1.0F, 1.0F, 1.0F},
                  Scene::PrimitiveMeshKind::Cube, Math::Vector3{0.88F, 0.10F, 0.06F});
    makePrimitive(Math::Vector3{-1.8F, 0.0F, -7.0F}, Math::Quaternion{}, Math::Vector3{1.4F, 1.4F, 1.4F},
                  Scene::PrimitiveMeshKind::UVSphere, Math::Vector3{0.06F, 0.20F, 0.90F});
    sceneGraph.Update(entityManager);

    std::array<unsigned char, 3> cube{};
    std::array<unsigned char, 3> plane{};
    std::array<unsigned char, 3> sphere{};
    GLenum postRenderGlError = GL_NO_ERROR;
    engine.SetPostRenderCallback([&cube, &plane, &sphere, &postRenderGlError] {
        // The callback runs after Renderer3D’s default-framebuffer tone-map pass and before
        // EngineCore requests SwapBuffers(). GL_BACK is therefore the exact presentation
        // surface being handed to the window manager, unlike post-swap front/back reads.
        glFinish();
        glReadBuffer(GL_BACK);
        // These center-of-raster samples are intentionally well inside the fixed fixture's
        // projected geometry: red cube, green plane, then blue UV sphere.
        //
        // Derived from the fixture rather than observed: camera at the origin looking down -Z,
        // 60 degrees vertical FOV, 128x96 (aspect 4:3), so half-height at distance t is
        // t*tan(30deg) and half-width is that times the aspect. That puts the cube's near face at
        // x 19.7-30.8 / y 42.5-53.5, the sphere at x 34-51 / y 39-57, and the 6x6 plane at
        // x 38.9-89.1 / y 24.2-71.8, with the cube and sphere both nearer than the plane and not
        // overlapping each other. Each sample below is the middle of one of those.
        //
        // These coordinates were previously exactly double these values, which only landed on
        // geometry because the built-in fullscreen-triangle shaders halved their texture
        // coordinate and so magnified the lower-left quarter of every post-process source across
        // the whole target - see the vertex shaders in built_in_shaders_uve.cpp.
        glReadPixels(25, 48, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, cube.data());
        glReadPixels(75, 48, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, plane.data());
        glReadPixels(43, 48, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, sphere.data());
        postRenderGlError = glGetError();
    });
    for (int frame = 0; frame < 12; ++frame) {
        engine.TickFrame();
    }
    engine.SetPostRenderCallback({});

    const Render::Renderer3DFrameDiagnostics diagnostics = services.GetRenderer3D().GetLastFrameDiagnostics();
    EXPECT_EQ(diagnostics.primitiveCandidates, 3U);
    EXPECT_EQ(diagnostics.primitiveItemsExtracted, 3U);
    EXPECT_EQ(diagnostics.primitiveDrawCallsRecorded, 3U);
    EXPECT_EQ(diagnostics.glDrawCallsIssued, 3U);
    EXPECT_TRUE(diagnostics.toneMappingPassRecorded);

    EXPECT_GT(cube[0], static_cast<unsigned char>(cube[1] + 12U)) << "cube RGB=" << static_cast<int>(cube[0])
                                                                     << ',' << static_cast<int>(cube[1]) << ','
                                                                     << static_cast<int>(cube[2]);
    EXPECT_GT(plane[1], static_cast<unsigned char>(plane[0] + 12U)) << "plane RGB=" << static_cast<int>(plane[0])
                                                                      << ',' << static_cast<int>(plane[1]) << ','
                                                                      << static_cast<int>(plane[2]);
    EXPECT_GT(sphere[2], static_cast<unsigned char>(sphere[0] + 12U)) << "sphere RGB=" << static_cast<int>(sphere[0])
                                                                        << ',' << static_cast<int>(sphere[1]) << ','
                                                                        << static_cast<int>(sphere[2]);
    EXPECT_EQ(postRenderGlError, GL_NO_ERROR);

    engine.Shutdown();
}

// Phase U3a coverage: proves UIRuntime's authored screen-space UI (image/button/text) actually
// reaches real pixels on the default framebuffer through Renderer3D's new "UIOverlay" pass, and
// that a real mouse click (via IInputSystem's real Set*State() live-state model, exactly like
// a real GLFW callback would drive it) flips UIButtonComponent::wasClickedThisFrame and visibly
// changes the button's rendered color - the exact end-to-end claim Phase U3a's own plan requires.
// Real (non-headless) GLFW/GL backend under Xvfb, matching every other WindowedMode_*/UI-adjacent
// test in this file. glReadPixels uses GL's bottom-left-origin convention; UIRuntime/UIOverlay
// use top-left-origin, y-down pixel space (matching GetMousePosition()'s own convention), so
// sample rows are converted via `windowHeight - uiY`.
TEST(EngineCoreTest, WindowedMode_UIOverlayRendersAuthoredUIAndRegistersARealButtonClick) {
    EngineConfig config = MakeTestConfig();
    config.headless = false;
    config.windowWidth = 160U;
    config.windowHeight = 120U;
    config.renderTargetWidth = 160U;
    config.renderTargetHeight = 120U;
    config.vsyncEnabled = false;
    config.windowGlVersionMajor = 4U;
    config.windowGlVersionMinor = 5U;

    // This fixture asserts pixels on frame one, not eventual shader readiness. Request the
    // production synchronous policy explicitly: the default async policy may legitimately
    // still be preprocessing UIOverlay when its first Update runs on a loaded CI worker.
    // Keep every pixel and real-input assertion below unchanged.
    config.shaderCompilationMode = ShaderCompilationMode::SynchronousOnDemand;

    EngineCore engine(config);
    engine.Init();
    if (!engine.GetServices().GetWindowManager().IsValid()) {
        GTEST_SKIP() << "No display available for windowed EngineCore - skipping (run under "
                        "xvfb-run to exercise this test)";
    }
    ASSERT_TRUE(engine.Load());

    EngineServices& services = engine.GetServices();
    Scene::IEntityManager& entityManager = services.GetEntityManager();
    Scene::ISceneGraph& sceneGraph = services.GetSceneGraph();
    Input::IInputSystem& inputSystem = services.GetInputSystem();
    // WindowManager re-polls the *real* OS cursor/button state every frame (matching real
    // gameplay use) and would otherwise overwrite this test's directly-injected mouse state on the
    // very next TickFrame() - detaching it here is what lets IInputSystem's live-state model
    // (the same Set*State() calls a real GLFW callback would make) drive input deterministically
    // against this still-real GL window/context.
    services.GetWindowManager().AttachInputSystem(nullptr);

    const Scene::Entity camera = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, camera, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::CameraComponent>(camera);
    engine.SetActiveCamera(camera);

    const Scene::Entity imageEntity = entityManager.CreateEntity();
    entityManager.AddComponent<Scene::UIImageComponent>(
        imageEntity, Scene::UIImageComponent{Asset::AssetGuid{},
                                                 Math::Rect{Math::Vector2{10.0F, 10.0F},
                                                               Math::Vector2{20.0F, 20.0F}},
                                                 Math::Vector3{0.90F, 0.10F, 0.10F}, 1.0F});

    const Scene::Entity buttonEntity = entityManager.CreateEntity();
    Scene::UIButtonComponent buttonComponent{};
    buttonComponent.rect.position = Math::Vector2{60.0F, 10.0F};
    buttonComponent.rect.size = Math::Vector2{40.0F, 20.0F};
    buttonComponent.normalColor = Math::Vector3{0.10F, 0.85F, 0.10F};
    buttonComponent.hoverColor = Math::Vector3{0.10F, 0.10F, 0.85F};
    buttonComponent.pressedColor = Math::Vector3{0.85F, 0.85F, 0.10F};
    entityManager.AddComponent<Scene::UIButtonComponent>(buttonEntity, buttonComponent);

    const Scene::Entity textEntity = entityManager.CreateEntity();
    Scene::UITextComponent textComponent{};
    textComponent.text = "Hi";
    textComponent.positionPixels = Math::Vector2{10.0F, 60.0F};
    textComponent.fontSize = 24.0F;
    textComponent.color = Math::Vector3{1.0F, 1.0F, 1.0F};
    entityManager.AddComponent<Scene::UITextComponent>(textEntity, textComponent);

    const auto sampleTopLeftPixel = [windowHeight = config.windowHeight](float uiX, float uiY) {
        std::array<unsigned char, 3> pixel{};
        const int glX = static_cast<int>(uiX);
        const int glY = static_cast<int>(windowHeight) - static_cast<int>(uiY);
        glReadPixels(glX, glY, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel.data());
        return pixel;
    };

    // Frame 1 (baseline): mouse well away from the button, nothing pressed.
    inputSystem.SetMousePosition(Math::Vector2{0.0F, 0.0F});
    std::array<unsigned char, 3> imagePixel{};
    std::array<unsigned char, 3> buttonPixelBaseline{};
    unsigned char maxTextBrightness = 0U;
    engine.SetPostRenderCallback([&] {
        glFinish();
        glReadBuffer(GL_BACK);
        imagePixel = sampleTopLeftPixel(20.0F, 20.0F);
        buttonPixelBaseline = sampleTopLeftPixel(80.0F, 20.0F);
        for (float sampleX = 8.0F; sampleX <= 40.0F; sampleX += 2.0F) {
            for (float sampleY = 55.0F; sampleY <= 90.0F; sampleY += 2.0F) {
                const std::array<unsigned char, 3> pixel = sampleTopLeftPixel(sampleX, sampleY);
                const unsigned char brightness = static_cast<unsigned char>(
                    (static_cast<int>(pixel[0]) + static_cast<int>(pixel[1]) + static_cast<int>(pixel[2])) / 3);
                maxTextBrightness = std::max(maxTextBrightness, brightness);
            }
        }
    });
    engine.TickFrame();
    engine.SetPostRenderCallback({});

    EXPECT_FALSE(entityManager.GetComponent<Scene::UIButtonComponent>(buttonEntity).isHovered);
    EXPECT_FALSE(entityManager.GetComponent<Scene::UIButtonComponent>(buttonEntity).wasClickedThisFrame);
    EXPECT_GT(imagePixel[0], static_cast<unsigned char>(imagePixel[1] + 20U))
        << "image RGB=" << static_cast<int>(imagePixel[0]) << ',' << static_cast<int>(imagePixel[1]) << ','
        << static_cast<int>(imagePixel[2]);
    EXPECT_GT(buttonPixelBaseline[1], static_cast<unsigned char>(buttonPixelBaseline[0] + 20U))
        << "button(normal) RGB=" << static_cast<int>(buttonPixelBaseline[0]) << ','
        << static_cast<int>(buttonPixelBaseline[1]) << ',' << static_cast<int>(buttonPixelBaseline[2]);
    EXPECT_GT(maxTextBrightness, 90U) << "max sampled text-region brightness=" << static_cast<int>(maxTextBrightness);

    // Frame 2: a real mouse move + press over the button - IInputSystem's live-state model is
    // exactly what a real GLFW cursor/mouse-button callback would drive.
    inputSystem.SetMousePosition(Math::Vector2{80.0F, 20.0F});
    inputSystem.SetMouseButtonState(Input::MouseButton::Left, true);
    std::array<unsigned char, 3> buttonPixelPressed{};
    engine.SetPostRenderCallback([&] {
        glFinish();
        glReadBuffer(GL_BACK);
        buttonPixelPressed = sampleTopLeftPixel(80.0F, 20.0F);
    });
    engine.TickFrame();
    engine.SetPostRenderCallback({});

    const Scene::UIButtonComponent afterClick = entityManager.GetComponent<Scene::UIButtonComponent>(buttonEntity);
    EXPECT_TRUE(afterClick.isHovered);
    EXPECT_TRUE(afterClick.wasClickedThisFrame) << "a real mouse press over the button did not register";
    EXPECT_GT(buttonPixelPressed[0], static_cast<unsigned char>(buttonPixelPressed[2] + 20U))
        << "button(pressed) RGB=" << static_cast<int>(buttonPixelPressed[0]) << ','
        << static_cast<int>(buttonPixelPressed[1]) << ',' << static_cast<int>(buttonPixelPressed[2]);
    EXPECT_GT(buttonPixelPressed[1], static_cast<unsigned char>(buttonPixelPressed[2] + 20U))
        << "button(pressed) RGB=" << static_cast<int>(buttonPixelPressed[0]) << ','
        << static_cast<int>(buttonPixelPressed[1]) << ',' << static_cast<int>(buttonPixelPressed[2]);

    // Frame 3: release, mouse still hovering - wasClickedThisFrame must not stay latched, and the
    // rendered color must move to the distinct hover color (not normal, not pressed).
    inputSystem.SetMouseButtonState(Input::MouseButton::Left, false);
    std::array<unsigned char, 3> buttonPixelHover{};
    engine.SetPostRenderCallback([&] {
        glFinish();
        glReadBuffer(GL_BACK);
        buttonPixelHover = sampleTopLeftPixel(80.0F, 20.0F);
    });
    engine.TickFrame();
    engine.SetPostRenderCallback({});

    const Scene::UIButtonComponent afterRelease = entityManager.GetComponent<Scene::UIButtonComponent>(buttonEntity);
    EXPECT_TRUE(afterRelease.isHovered);
    EXPECT_FALSE(afterRelease.wasClickedThisFrame) << "a stale click latched past the frame it occurred on";
    EXPECT_GT(buttonPixelHover[2], static_cast<unsigned char>(buttonPixelHover[0] + 20U))
        << "button(hover) RGB=" << static_cast<int>(buttonPixelHover[0]) << ','
        << static_cast<int>(buttonPixelHover[1]) << ',' << static_cast<int>(buttonPixelHover[2]);

    engine.Shutdown();
}

// Gap A audit coverage: verifies the render target/framebuffer dimensions (audit checkpoint #2)
// actually track SetEditorViewportRegion()'s region - not the window - and cleanly revert once
// the region is cleared. Real windowed GLFW/GL backend under Xvfb, matching every other
// WindowedMode_* test's verification method in this file (not NullRenderDevice, since this
// exercises the real adaptive-resolution/window-manager code path SyncAdaptiveRenderResolution()
// only takes when m_windowedRenderingActive is true).
TEST(EngineCoreTest, SetEditorViewportRegion_DrivesRenderTargetToRegionNotWindow) {
    EngineConfig config = MakeTestConfig();
    config.headless = false;
    config.windowWidth = 200U;
    config.windowHeight = 150U;
    config.vsyncEnabled = false;
    config.windowGlVersionMajor = 4U;
    config.windowGlVersionMinor = 5U;

    EngineCore engine(config);
    engine.Init();
    if (!engine.GetServices().GetWindowManager().IsValid()) {
        GTEST_SKIP() << "No display available for windowed EngineCore - skipping (run under "
                        "xvfb-run to exercise this test)";
    }
    ASSERT_TRUE(engine.Load());

    EngineServices& services = engine.GetServices();
    Scene::IEntityManager& entityManager = services.GetEntityManager();
    Scene::ISceneGraph& sceneGraph = services.GetSceneGraph();
    const Scene::Entity camera = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, camera, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::CameraComponent>(camera);
    engine.SetActiveCamera(camera);

    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    services.GetLogger().AddSink(std::move(memorySink));

    // Baseline: no region set yet, so the target tracks the 200x150 window as before this fix.
    engine.TickFrame();
    const Render::Renderer3DFrameDiagnostics windowDrivenDiagnostics =
        services.GetRenderer3D().GetLastFrameDiagnostics();
    EXPECT_EQ(windowDrivenDiagnostics.renderTargetWidth, 200U);
    EXPECT_EQ(windowDrivenDiagnostics.renderTargetHeight, 150U);

    // A region clearly smaller than, and offset within, the window - proves the target tracks the
    // region's own dimensions ("very narrow"/"resized side panels" editor layouts), not the window.
    engine.SetEditorViewportRegion(Render::ViewportRect{Math::Vector2i{20, 10}, Math::Vector2i{90, 60}});
    engine.TickFrame();
    const Render::Renderer3DFrameDiagnostics regionDrivenDiagnostics =
        services.GetRenderer3D().GetLastFrameDiagnostics();
    EXPECT_EQ(regionDrivenDiagnostics.renderTargetWidth, 90U);
    EXPECT_EQ(regionDrivenDiagnostics.renderTargetHeight, 60U);

    // Clearing the region reverts to window-driven sizing - proves no stale state leaks into a
    // frame the editor no longer wants confined (e.g. switching back to a full/maximized layout).
    engine.SetEditorViewportRegion(std::nullopt);
    engine.TickFrame();
    const Render::Renderer3DFrameDiagnostics revertedDiagnostics =
        services.GetRenderer3D().GetLastFrameDiagnostics();
    EXPECT_EQ(revertedDiagnostics.renderTargetWidth, 200U);
    EXPECT_EQ(revertedDiagnostics.renderTargetHeight, 150U);

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundViewportRejection =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.message.contains("does not fit within");
        });
    EXPECT_FALSE(foundViewportRejection);

    engine.Shutdown();
}

// The configured fixed scene-render scale must change only the 3D render target; the native
// presentation surface remains at the window's drawable resolution. This is a windowed GL
// integration check and skips when no display/context is available.
TEST(EngineCoreTest, FixedRenderScaleScalesSceneTargetWithoutChangingWindowSize) {
    EngineConfig config = MakeTestConfig();
    config.headless = false;
    config.windowWidth = 200U;
    config.windowHeight = 150U;
    config.renderResolutionScale = 0.5;
    config.settingsFilePath = "uve_engine_core_fixed_render_scale.uvsettings";
    config.projectSettingsFilePath = "uve_engine_core_fixed_render_scale.project.uvsettings";
    config.platformSettingsFilePath = "uve_engine_core_fixed_render_scale.platform.uvsettings";
    config.vsyncEnabled = false;
    config.windowGlVersionMajor = 4U;
    config.windowGlVersionMinor = 5U;

    EngineCore engine(config);
    engine.Init();
    if (!engine.GetServices().GetWindowManager().IsValid()) {
        GTEST_SKIP() << "No display available for windowed EngineCore - skipping (run under "
                        "xvfb-run to exercise this test)";
    }
    ASSERT_TRUE(engine.Load());

    EngineServices& services = engine.GetServices();
    Scene::IEntityManager& entityManager = services.GetEntityManager();
    Scene::ISceneGraph& sceneGraph = services.GetSceneGraph();
    const Scene::Entity camera = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, camera, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::CameraComponent>(camera);
    engine.SetActiveCamera(camera);

    engine.TickFrame();
    const Render::Renderer3DFrameDiagnostics diagnostics =
        services.GetRenderer3D().GetLastFrameDiagnostics();
    EXPECT_EQ(diagnostics.renderTargetWidth, 100U);
    EXPECT_EQ(diagnostics.renderTargetHeight, 75U);
    EXPECT_EQ(services.GetWindowManager().GetWidth(), 200U);
    EXPECT_EQ(services.GetWindowManager().GetHeight(), 150U);

    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
    std::filesystem::remove(config.projectSettingsFilePath);
    std::filesystem::remove(config.platformSettingsFilePath);
}

// A region whose on-screen destination sub-rect is a THIN SLICE of the window (very narrow and
// very wide/short cases) - the two extreme "editor layout" shapes the audit specifically asked to
// be exercised, beyond the more moderate rect above.
TEST(EngineCoreTest, SetEditorViewportRegion_HandlesVeryNarrowAndVeryWideRegions) {
    EngineConfig config = MakeTestConfig();
    config.headless = false;
    config.windowWidth = 200U;
    config.windowHeight = 150U;
    config.vsyncEnabled = false;
    config.windowGlVersionMajor = 4U;
    config.windowGlVersionMinor = 5U;

    EngineCore engine(config);
    engine.Init();
    if (!engine.GetServices().GetWindowManager().IsValid()) {
        GTEST_SKIP() << "No display available for windowed EngineCore - skipping (run under "
                        "xvfb-run to exercise this test)";
    }
    ASSERT_TRUE(engine.Load());

    EngineServices& services = engine.GetServices();
    Scene::IEntityManager& entityManager = services.GetEntityManager();
    Scene::ISceneGraph& sceneGraph = services.GetSceneGraph();
    const Scene::Entity camera = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, camera, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::CameraComponent>(camera);
    engine.SetActiveCamera(camera);

    // Very narrow: a 10px-wide sliver on the right (e.g. a Scene panel dragged nearly closed).
    engine.SetEditorViewportRegion(Render::ViewportRect{Math::Vector2i{180, 0}, Math::Vector2i{10, 150}});
    engine.TickFrame();
    const Render::Renderer3DFrameDiagnostics narrowDiagnostics =
        services.GetRenderer3D().GetLastFrameDiagnostics();
    EXPECT_EQ(narrowDiagnostics.renderTargetWidth, 10U);
    EXPECT_EQ(narrowDiagnostics.renderTargetHeight, 150U);

    // Very wide/short: the full width, a thin 8px strip in height (e.g. a bottom dock nearly
    // maximized over the viewport).
    engine.SetEditorViewportRegion(Render::ViewportRect{Math::Vector2i{0, 0}, Math::Vector2i{200, 8}});
    engine.TickFrame();
    const Render::Renderer3DFrameDiagnostics wideDiagnostics =
        services.GetRenderer3D().GetLastFrameDiagnostics();
    EXPECT_EQ(wideDiagnostics.renderTargetWidth, 200U);
    EXPECT_EQ(wideDiagnostics.renderTargetHeight, 8U);

    engine.Shutdown();
}

TEST(EngineCoreTest, PostRenderCallback_HeadlessModeDoesNotInvokeOverlay) {
    EngineConfig config = MakeTestConfig();
    config.headless = true;
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    int callbackCount = 0;
    engine.SetPostRenderCallback([&callbackCount] { ++callbackCount; });
    engine.TickFrame();
    EXPECT_EQ(callbackCount, 0);

    engine.SetPostRenderCallback({});
    engine.Shutdown();
}

// A-1 (P0): Run() is a hard exception boundary - see its doc comment in engine_core_uve.h.
// TickFrame_UncaughtSubscriberException below proves the underlying mechanism is real (an
// exception genuinely propagates out of a frame update when nothing catches it) using the real
// EngineCore via IEventSystem's public Subscribe()/QueueEvent() API - the same real
// mechanism Run()'s internal frame loop drives every frame via TickFrame().
//
// The three RunExceptionBoundaryHarness tests further down cover all three cases the fix
// requires (throw during Init(), during Load(), during a frame update) against a small,
// self-contained harness rather than the real EngineCore::Run(): EngineCore constructs
// roughly 34 concrete subsystems directly in Init(), each already written (per the D-1/SYS-1
// audits) to avoid throwing wherever avoidable, so there is no deterministic, non-flaky way to
// make Init() or Load() throw through EngineCore's public API alone. The harness reuses the
// real EngineState/IsValidTransition/EngineCore::kUnhandledExceptionExitCode from
// production code and reproduces Run()'s documented contract exactly, so it verifies that
// contract's logic even though it cannot substitute for exercising EngineCore::Init()/Load()
// themselves.
TEST(EngineCoreTest, TickFrame_UncaughtSubscriberException_PropagatesInsteadOfBeingSwallowed) {
    struct BoomEvent {};

    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    engine.GetServices().GetEventSystem().Subscribe<BoomEvent>(
        [](const BoomEvent&) { throw std::runtime_error("subscriber exploded"); });
    engine.GetServices().GetEventSystem().QueueEvent(BoomEvent{});

    // TickFrame() itself has no exception boundary by design - Run() is the boundary (see
    // its doc comment), so a caller driving frames directly (this test, and the editor's
    // hand-rolled loop in engine/app/src/editor/main.cpp) is responsible for its own. Confirms
    // the exception is genuinely uncaught here, not silently swallowed somewhere internally.
    EXPECT_THROW(engine.TickFrame(), std::runtime_error);
    EXPECT_EQ(engine.GetState(), EngineState::Running); // still mid-run; clean up explicitly
    engine.Shutdown();
}

namespace {

/// Reproduces EngineCore::Run()'s documented exception-boundary contract in isolation
/// (see the comment above TickFrame_UncaughtSubscriberException_PropagatesInsteadOfBeingSwallowed
/// for why): catches any exception thrown by an injected Init()/Load()/frame-update hook, calls a
/// Shutdown()-equivalent only if state had actually reached Running (an exception during Init()
/// itself must not force it - see Run()'s doc comment), never lets a second exception from
/// that Shutdown()-equivalent escape either, and returns EngineCore::kUnhandledExceptionExitCode.
class RunExceptionBoundaryHarness final {
public:
    std::function<void()> onInit;
    std::function<bool()> onLoad = [] { return true; };
    std::function<void()> onFrameUpdate;

    int Run(int frameCount) {
        try {
            Transition(EngineState::Initializing);
            if (onInit) {
                onInit();
            }
            Transition(EngineState::Running);
            if (onLoad && !onLoad()) {
                Shutdown();
                return 1;
            }
            for (int frame = 0; frame < frameCount; ++frame) {
                if (onFrameUpdate) {
                    onFrameUpdate();
                }
            }
            Shutdown();
            return 0;
        } catch (const std::exception&) {
            // Production logs via UVE_FATAL here; nothing to assert on in this harness.
        } catch (...) {
        }
        if (m_state == EngineState::Running) {
            try {
                Shutdown();
            } catch (...) {
                m_shutdownThrew = true;
            }
        }
        return EngineCore::kUnhandledExceptionExitCode;
    }

    [[nodiscard]] bool ShutdownRan() const noexcept { return m_shutdownRan; }
    [[nodiscard]] bool ShutdownThrew() const noexcept { return m_shutdownThrew; }
    [[nodiscard]] EngineState GetState() const noexcept { return m_state; }

private:
    void Transition(EngineState newState) {
        // Mirrors EngineCore::TransitionState()'s own UVE_ASSERT(IsValidTransition(...))
        // guard - a failure here is a bug in this harness's own test-hook wiring, not in the
        // property under test, so a plain non-fatal EXPECT_TRUE is enough to surface it.
        EXPECT_TRUE(IsValidTransition(m_state, newState));
        m_state = newState;
    }

    void Shutdown() {
        Transition(EngineState::ShuttingDown);
        m_shutdownRan = true;
        Transition(EngineState::Shutdown);
    }

    EngineState m_state = EngineState::Uninitialized;
    bool m_shutdownRan = false;
    bool m_shutdownThrew = false;
};

} // namespace

TEST(RunExceptionBoundaryHarnessTest, ThrowDuringInit_ReturnsDistinctCodeAndSkipsUnsafeShutdown) {
    RunExceptionBoundaryHarness harness;
    harness.onInit = [] { throw std::runtime_error("boom during init"); };

    EXPECT_EQ(harness.Run(1), EngineCore::kUnhandledExceptionExitCode);
    // State never reached Running - the real EngineCore::Shutdown() would dereference
    // subsystems Init() never got to construct, so it must not run here either.
    EXPECT_FALSE(harness.ShutdownRan());
    EXPECT_EQ(harness.GetState(), EngineState::Initializing);
}

TEST(RunExceptionBoundaryHarnessTest, ThrowDuringLoad_RunsShutdownInNormalOrderAndReturnsDistinctCode) {
    RunExceptionBoundaryHarness harness;
    harness.onLoad = []() -> bool { throw std::runtime_error("boom during load"); };

    EXPECT_EQ(harness.Run(1), EngineCore::kUnhandledExceptionExitCode);
    EXPECT_TRUE(harness.ShutdownRan());
    EXPECT_FALSE(harness.ShutdownThrew());
    EXPECT_EQ(harness.GetState(), EngineState::Shutdown);
}

TEST(RunExceptionBoundaryHarnessTest, ThrowDuringFrameUpdate_RunsShutdownInNormalOrderAndReturnsDistinctCode) {
    RunExceptionBoundaryHarness harness;
    int framesRun = 0;
    harness.onFrameUpdate = [&framesRun] {
        ++framesRun;
        if (framesRun == 2) {
            throw std::runtime_error("boom mid-loop");
        }
    };

    EXPECT_EQ(harness.Run(5), EngineCore::kUnhandledExceptionExitCode);
    EXPECT_EQ(framesRun, 2); // stopped exactly at the throwing frame, not all 5
    EXPECT_TRUE(harness.ShutdownRan());
    EXPECT_EQ(harness.GetState(), EngineState::Shutdown);
}


// ---------------------------------------------------------------------------
// CS2: ComputeSystem wired into the frame loop. The queue is drained in
// Render(), BEFORE any render pass opens, into its own submitted command
// buffer - the RHI compute slices' outside-pass-markers contract, enforced at
// the engine level rather than left to each caller.
// ---------------------------------------------------------------------------

// The headless engine config brings up NullRenderDevice, whose spy hook keeps the most
// recently submitted command list - the same evidence channel RenderSystem's own tests use.
// Reached by dynamic_cast rather than a new production accessor: the engine exposes the device
// through its interface, and inventing a test-only getter on EngineCore to see one backend's
// spy would put test scaffolding into the engine's public surface.
[[nodiscard]] const std::vector<Render::RecordedCommand>& LastSubmittedCommands(
    EngineCore& engine) {
    auto* const nullDevice =
        dynamic_cast<Render::NullRenderDevice*>(&engine.GetServices().GetRenderDevice());
    UVE_ASSERT(nullDevice != nullptr && "headless EngineCore must run on NullRenderDevice");
    return nullDevice->GetLastSubmittedCommands();
}

TEST(EngineCoreTest, ComputeSystem_ReachableThroughServicesAfterInit) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Render::IComputeSystem& computeSystem = engine.GetServices().GetComputeSystem();
    EXPECT_EQ(computeSystem.GetQueuedDispatchCount(), 0U);
    EXPECT_EQ(computeSystem.GetDiagnostics().dispatchesRecorded, 0U);

    engine.Shutdown();
}

TEST(EngineCoreTest, TickFrame_QueuedComputeDispatch_IsSubmittedBeforeAnyRenderPass) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Render::IComputeSystem& computeSystem = engine.GetServices().GetComputeSystem();
    Render::IRenderDevice& renderDevice = engine.GetServices().GetRenderDevice();

    Render::ComputeProgramDesc programDesc;
    programDesc.sourceCode = "engine-frame-loop compute kernel (recorded by the Null backend)";
    programDesc.debugName = "frame-loop-kernel";
    const Render::PipelineHandle program = computeSystem.CreateProgram(programDesc);
    ASSERT_NE(program, Render::kInvalidPipelineHandle);

    const Render::BufferHandle storage =
        renderDevice.CreateBuffer(Render::BufferDesc{64U, Render::BufferUsage::Storage});
    ASSERT_NE(storage, Render::kInvalidBufferHandle);

    Render::ComputeDispatchDesc dispatch;
    dispatch.program = program;
    dispatch.groupCountX = 3U;
    dispatch.groupCountY = 2U;
    dispatch.groupCountZ = 1U;
    dispatch.storageBuffers.push_back(Render::ComputeStorageBufferBinding{storage, 0U});
    ASSERT_TRUE(computeSystem.EnqueueDispatch(dispatch));
    ASSERT_EQ(computeSystem.GetQueuedDispatchCount(), 1U);

    engine.TickFrame();

    // The queue drained through the frame loop - no test touched a command buffer directly.
    EXPECT_EQ(computeSystem.GetQueuedDispatchCount(), 0U);
    EXPECT_EQ(computeSystem.GetDiagnostics().dispatchesRecorded, 1U);

    // The compute work went out as its OWN submission, with no pass markers around it: the Null
    // spy keeps the most recent submitted list, and with no active camera the renderer records
    // nothing this frame, so what remains is exactly the compute buffer.
    const auto& submitted = LastSubmittedCommands(engine);
    ASSERT_EQ(submitted.size(), 3U);
    ASSERT_TRUE(std::holds_alternative<Render::BindPipelineCommand>(submitted[0]));
    EXPECT_EQ(std::get<Render::BindPipelineCommand>(submitted[0]).pipeline, program);
    ASSERT_TRUE(std::holds_alternative<Render::BindStorageBufferCommand>(submitted[1]));
    EXPECT_EQ(std::get<Render::BindStorageBufferCommand>(submitted[1]).buffer, storage);
    ASSERT_TRUE(std::holds_alternative<Render::DispatchCommand>(submitted[2]));
    EXPECT_EQ(std::get<Render::DispatchCommand>(submitted[2]).groupCountX, 3U);
    EXPECT_EQ(std::get<Render::DispatchCommand>(submitted[2]).groupCountY, 2U);

    // No render pass command may appear in the compute submission at all.
    for (const Render::RecordedCommand& command : submitted) {
        EXPECT_FALSE(std::holds_alternative<Render::BeginRenderPassCommand>(command));
        EXPECT_FALSE(std::holds_alternative<Render::EndRenderPassCommand>(command));
    }

    computeSystem.DestroyProgram(program);
    renderDevice.DestroyBuffer(storage);
    engine.Shutdown();
}

TEST(EngineCoreTest, TickFrame_EmptyComputeQueue_SubmitsNothingExtra) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    // A frame with no queued compute must not submit an empty compute buffer - scenes that never
    // touch compute pay nothing observable.
    engine.TickFrame();

    EXPECT_TRUE(LastSubmittedCommands(engine).empty());
    EXPECT_EQ(engine.GetServices().GetComputeSystem().GetDiagnostics().dispatchesRecorded, 0U);

    engine.Shutdown();
}

#if UVE_DEBUG
TEST(EngineCoreDeathTest, ShutdownBeforeInit_TriggersInvalidTransitionAssert) {
    EngineCore engine(MakeTestConfig());
    EXPECT_DEATH({ engine.Shutdown(); }, "");
}

TEST(EngineCoreDeathTest, GetServicesBeforeInit_Asserts) {
    EngineCore engine(MakeTestConfig());
    EXPECT_DEATH({ static_cast<void>(engine.GetServices()); }, "");
}
#else
TEST(EngineCoreTest, GetServicesBeforeInit_ThrowsBadOptionalAccessInsteadOfDereferencingEmptyOptional) {
    EngineCore engine(MakeTestConfig());
    EXPECT_THROW({ static_cast<void>(engine.GetServices()); }, std::bad_optional_access);
}
#endif

// LevelStreamer3D helpers: a tiny authored level document (one transformed root) written to a
// temp path through the production serializer, plus an enabled streamer component pointing at
// it. Keeping the streamed content OUT of the fixture's own document is the entire point of the
// slice, so the file is the real interchange - not an in-memory shortcut.
namespace {

const std::filesystem::path kStreamerTestLevelPath = "uve_engine_core_streamer_test_level.uvscene";

// Writes `kStreamerTestLevelPath` and returns true on success. Two entities: a root at (0,0,0)
// with a child offset at (1,2,3), so "one loaded level" is measurable as +2 entities.
bool WriteStreamerTestLevelFile() {
    std::filesystem::remove(kStreamerTestLevelPath);
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager tempManager(memoryManager.GetDefaultAllocator(), eventSystem);
    Scene::SceneGraph tempGraph;
    Scene::SceneSerializer serializer;

    const Scene::Entity root = tempManager.CreateEntity();
    tempGraph.AttachTransform(tempManager, root, Scene::TransformComponent{});
    const Scene::Entity child = tempManager.CreateEntity();
    Scene::TransformComponent childTransform;
    childTransform.localPosition = Math::Vector3{1.0F, 2.0F, 3.0F};
    tempGraph.AttachTransform(tempManager, child, childTransform);
    tempGraph.SetParent(tempManager, child, root);
    const bool saved =
        serializer.Save(tempManager, {root}, kStreamerTestLevelPath, Asset::AssetKind::Scene);
    if (!saved) {
        std::filesystem::remove(kStreamerTestLevelPath);
    }
    return saved;
}

struct StreamerTestCleanup {
    ~StreamerTestCleanup() { std::filesystem::remove(kStreamerTestLevelPath); }
};

Scene::Entity CreateEnabledStreamerAt(Scene::IEntityManager& entityManager,
                                            Scene::ISceneGraph& sceneGraph,
                                            const Math::Vector3& position) {
    const Scene::Entity streamer = entityManager.CreateEntity();
    Scene::TransformComponent transform;
    transform.localPosition = position;
    sceneGraph.AttachTransform(entityManager, streamer, transform);
    Scene::LevelStreamer3DComponent component;
    component.levelPath = kStreamerTestLevelPath.string();
    component.loadDistance = 10.0F;
    component.unloadDistance = 20.0F;
    component.enabled = true;
    entityManager.AddComponent<Scene::LevelStreamer3DComponent>(streamer, component);
    return streamer;
}

Scene::Entity CreateWatchingCameraAt(Scene::IEntityManager& entityManager,
                                           Scene::ISceneGraph& sceneGraph,
                                           const Math::Vector3& position) {
    const Scene::Entity camera = entityManager.CreateEntity();
    Scene::TransformComponent transform;
    transform.localPosition = position;
    sceneGraph.AttachTransform(entityManager, camera, transform);
    entityManager.AddComponent<Scene::CameraComponent>(camera, Scene::CameraComponent{});
    return camera;
}

} // namespace

TEST(EngineCoreTest, LevelStreamer3D_NearViewerLoadsContentFarViewerUnloadsItHysteresisHoldsBetween) {
    // The full distance-driven contract measured end-to-end: a viewer inside the load radius
    // pulls the level document in (+2 entities, streamer.flags flip), the hysteresis band between
    // loadDistance and unloadDistance changes nothing, leaving the unload radius destroys the
    // loaded subtree (-2 entities), and loadRequested is false between ticks - the synchronous
    // model's documented invariant.
    ASSERT_TRUE(WriteStreamerTestLevelFile()) << "the streamed level document must save";
    StreamerTestCleanup cleanup;

    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    const Scene::Entity streamer =
        CreateEnabledStreamerAt(entityManager, sceneGraph, Math::Vector3{0.0F, 0.0F, 0.0F});
    const Scene::Entity camera =
        CreateWatchingCameraAt(entityManager, sceneGraph, Math::Vector3{100.0F, 0.0F, 0.0F});
    engine.SetActiveCamera(camera);

    const std::size_t baseline = entityManager.GetEntityCount();
    engine.TickFrame();
    {
        const Scene::LevelStreamer3DComponent& live =
            entityManager.GetComponent<Scene::LevelStreamer3DComponent>(streamer);
        EXPECT_FALSE(live.loaded) << "a viewer 100 units out must never trigger the load";
        EXPECT_FALSE(live.loadRequested) << "loadRequested is false between ticks, always";
    }
    EXPECT_EQ(entityManager.GetEntityCount(), baseline);

    // The camera is now effectively on top of the streamer: one tick streams the level in.
    sceneGraph.SetLocalTransform(entityManager, camera, [&] {
        Scene::TransformComponent t;
        t.localPosition = Math::Vector3{0.5F, 0.0F, 0.0F};
        return t;
    }());
    engine.TickFrame();
    {
        const Scene::LevelStreamer3DComponent& live =
            entityManager.GetComponent<Scene::LevelStreamer3DComponent>(streamer);
        EXPECT_TRUE(live.loaded) << "a viewer inside the load radius streams the level in";
        EXPECT_FALSE(live.loadRequested);
    }
    EXPECT_EQ(entityManager.GetEntityCount(), baseline + 2U)
        << "the two authored entities of the level document appeared";

    // Hysteresis: at 15 units (inside unload, outside load) a loaded level does nothing.
    sceneGraph.SetLocalTransform(entityManager, camera, [&] {
        Scene::TransformComponent t;
        t.localPosition = Math::Vector3{15.0F, 0.0F, 0.0F};
        return t;
    }());
    engine.TickFrame();
    EXPECT_TRUE(
        entityManager.GetComponent<Scene::LevelStreamer3DComponent>(streamer).loaded)
        << "the band between 10 and 20 keeps a loaded level in place";
    EXPECT_EQ(entityManager.GetEntityCount(), baseline + 2U);

    // Past the unload radius the subtree is destroyed - real content lifecycle, not a hidden flag.
    sceneGraph.SetLocalTransform(entityManager, camera, [&] {
        Scene::TransformComponent t;
        t.localPosition = Math::Vector3{25.0F, 0.0F, 0.0F};
        return t;
    }());
    engine.TickFrame();
    {
        const Scene::LevelStreamer3DComponent& live =
            entityManager.GetComponent<Scene::LevelStreamer3DComponent>(streamer);
        EXPECT_FALSE(live.loaded) << "past the unload radius the level unloads";
        EXPECT_FALSE(live.loadRequested);
    }
    EXPECT_EQ(entityManager.GetEntityCount(), baseline)
        << "the streamer's two loaded entities are destroyed on unload";
}

TEST(EngineCoreTest, LevelStreamer3D_CharacterControllerServesAsViewerWithoutAnyCamera) {
    // The viewer model is Frostbite listener-style: ANY character controller pulls content,
    // even in a world no camera ever sees.
    ASSERT_TRUE(WriteStreamerTestLevelFile());
    StreamerTestCleanup cleanup;

    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    const Scene::Entity streamer =
        CreateEnabledStreamerAt(entityManager, sceneGraph, Math::Vector3{});
    const Scene::Entity player = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, player, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::CharacterControllerComponent>(player);

    const std::size_t baseline = entityManager.GetEntityCount();
    engine.TickFrame();
    EXPECT_TRUE(
        entityManager.GetComponent<Scene::LevelStreamer3DComponent>(streamer).loaded)
        << "a standing controller with no active camera still streams the level in";
    EXPECT_EQ(entityManager.GetEntityCount(), baseline + 2U);
}

TEST(EngineCoreTest, LevelStreamer3D_DisablingTheStreamerPullsItsContentWhileTheViewerWatches) {
    // The authored toggle beats distance, matching the common convention a disabled streaming
    // volume stops holding its level - measured while the camera stands inside the load radius.
    ASSERT_TRUE(WriteStreamerTestLevelFile());
    StreamerTestCleanup cleanup;

    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    const Scene::Entity streamer =
        CreateEnabledStreamerAt(entityManager, sceneGraph, Math::Vector3{});
    const Scene::Entity camera =
        CreateWatchingCameraAt(entityManager, sceneGraph, Math::Vector3{1.0F, 0.0F, 0.0F});
    engine.SetActiveCamera(camera);
    engine.TickFrame();
    ASSERT_TRUE(
        entityManager.GetComponent<Scene::LevelStreamer3DComponent>(streamer).loaded);
    const std::size_t loadedCount = entityManager.GetEntityCount();

    entityManager.GetComponent<Scene::LevelStreamer3DComponent>(streamer).enabled = false;
    engine.TickFrame();
    EXPECT_FALSE(
        entityManager.GetComponent<Scene::LevelStreamer3DComponent>(streamer).loaded)
        << "disabling unloads even with the viewer inside the load radius";
    EXPECT_EQ(entityManager.GetEntityCount(), loadedCount - 2U)
        << "the disabled streamer's subtree is gone the same tick";
}

TEST(EngineCoreTest, LevelStreamer3D_FailedLoadLatchesClosedAndNeverRetriesInSession) {
    // The honest fail-closed path: levelPath points at a file that does not exist. The first
    // tick attempts and fails (loaded stays false, loadRequested stays false between ticks);
    // the latch means every later tick decides nothing either - never a retry storm - and the
    // streamer never marks itself loaded on an empty restore.
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    const Scene::Entity streamer = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, streamer, Scene::TransformComponent{});
    Scene::LevelStreamer3DComponent component;
    component.levelPath = "uve_engine_core_streamer_file_that_does_not_exist.uvscene";
    component.loadDistance = 10.0F;
    component.unloadDistance = 20.0F;
    component.enabled = true;
    entityManager.AddComponent<Scene::LevelStreamer3DComponent>(streamer, component);
    const Scene::Entity camera =
        CreateWatchingCameraAt(entityManager, sceneGraph, Math::Vector3{1.0F, 0.0F, 0.0F});
    engine.SetActiveCamera(camera);

    const std::size_t baseline = entityManager.GetEntityCount();
    for (int tick = 0; tick < 3; ++tick) {
        engine.TickFrame();
        const Scene::LevelStreamer3DComponent& live =
            entityManager.GetComponent<Scene::LevelStreamer3DComponent>(streamer);
        EXPECT_FALSE(live.loaded) << "tick " << tick << ": a missing file never marks loaded";
        EXPECT_FALSE(live.loadRequested) << "tick " << tick << ": no request survives the tick";
        EXPECT_EQ(entityManager.GetEntityCount(), baseline)
            << "tick " << tick << ": nothing materializes out of a missing file";
    }

    // The sibling with valid data is unaffected: a latch is per streamer, never global.
    ASSERT_TRUE(WriteStreamerTestLevelFile());
    StreamerTestCleanup cleanup;
    const Scene::Entity good =
        CreateEnabledStreamerAt(entityManager, sceneGraph, Math::Vector3{});
    engine.TickFrame();
    EXPECT_TRUE(entityManager.GetComponent<Scene::LevelStreamer3DComponent>(good).loaded)
        << "a failed sibling never blocks an unrelated streamer";
    EXPECT_FALSE(
        entityManager.GetComponent<Scene::LevelStreamer3DComponent>(streamer).loaded)
        << "the latched streamer stays closed even alongside a success";
}

TEST(EngineCoreTest, LevelStreamer3D_LoadBudgetCarriesOverflowIntoTheNextTick) {
    // The time-slicing contract: 5 identical streamers demand a load on the same tick with a
    // budget of 4 - exactly 4 stream in on tick one, and the straggler follows on tick two with
    // no other state stirring. This is the measurable answer to the hitch problem.
    ASSERT_TRUE(WriteStreamerTestLevelFile());
    StreamerTestCleanup cleanup;

    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    std::array<Scene::Entity, 5U> streamers;
    for (Scene::Entity& target : streamers) {
        target = CreateEnabledStreamerAt(
            entityManager, sceneGraph, Math::Vector3{100.0F, 0.0F, 0.0F});
    }
    const Scene::Entity camera =
        CreateWatchingCameraAt(entityManager, sceneGraph, Math::Vector3{100.0F, 0.0F, 0.0F});
    engine.SetActiveCamera(camera);

    const auto countLoaded = [&entityManager, &streamers]() -> std::size_t {
        std::size_t loaded = 0;
        for (const Scene::Entity s : streamers) {
            if (entityManager.GetComponent<Scene::LevelStreamer3DComponent>(s).loaded) {
                ++loaded;
            }
        }
        return loaded;
    };

    engine.TickFrame();
    EXPECT_EQ(countLoaded(), Scene::kMaximumLevelStreamer3DLoadsPerTick)
        << "the budget capped tick one, and the overflow stayed pending instead of dropping";
    engine.TickFrame();
    EXPECT_EQ(countLoaded(), 5U) << "tick two consumed the carry-over - the straggler loads";
    engine.TickFrame();
    EXPECT_EQ(countLoaded(), 5U) << "a further tick sits at steady state, nothing more moved";
}

namespace {

Scene::Entity CreateReflectionProbeAt(Scene::IEntityManager& entityManager,
                                             Scene::ISceneGraph& sceneGraph,
                                             const Math::Vector3& position,
                                              Scene::ReflectionProbeUpdateMode updateMode) {
    const Scene::Entity probe = entityManager.CreateEntity();
    Scene::TransformComponent transform;
    transform.localPosition = position;
    sceneGraph.AttachTransform(entityManager, probe, transform);
    Scene::ReflectionProbe3DComponent component;
    component.updateMode = updateMode;
    entityManager.AddComponent<Scene::ReflectionProbe3DComponent>(probe, component);
    return probe;
}

std::uint32_t TotalProbeGenerations(Scene::IEntityManager& entityManager,
                                       std::span<const Scene::Entity> probes) {
    std::uint32_t total = 0;
    for (const Scene::Entity probe : probes) {
        total += entityManager.GetComponent<Scene::ReflectionProbe3DComponent>(probe)
                     .captureGeneration;
    }
    return total;
}

} // namespace

TEST(EngineCoreTest, ReflectionProbe3D_OnceCapturesOnFirstTickThenStaysSilentForever) {
    // The measurable answer to the standard "capture scene on load" convention: first Tick resolves
    // exactly one capture (capturedOnce flips, captureGeneration hits 1), every subsequent tick
    // touches nothing - no re-capture, no latch drift, cost paid exactly once.
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    const Scene::Entity probe = CreateReflectionProbeAt(
        entityManager, sceneGraph, Math::Vector3{}, Scene::ReflectionProbeUpdateMode::Once);

    engine.TickFrame();
    {
        const Scene::ReflectionProbe3DComponent& live =
            entityManager.GetComponent<Scene::ReflectionProbe3DComponent>(probe);
        EXPECT_TRUE(live.capturedOnce) << "the first tick resolves the one-and-only capture";
        EXPECT_EQ(live.captureGeneration, 1U);
    }
    engine.TickFrame();
    engine.TickFrame();
    const Scene::ReflectionProbe3DComponent& after =
        entityManager.GetComponent<Scene::ReflectionProbe3DComponent>(probe);
    EXPECT_EQ(after.captureGeneration, 1U) << "later ticks revisit nothing - once means once";
}

TEST(EngineCoreTest, ReflectionProbe3D_EveryFrameRecapturesOnlyWhileTheCameraIsInside) {
    // A camera parked inside the probe sees one fresh capture per tick; walking it outside the
    // influence box stops the churn COMPLETELY the same tick - the save Godot's Always mode
    // cannot express, measured as captureGeneration flatlines.
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    const Scene::Entity probe = CreateReflectionProbeAt(
        entityManager, sceneGraph, Math::Vector3{},
        Scene::ReflectionProbeUpdateMode::EveryFrame);
    const Scene::Entity camera =
        CreateWatchingCameraAt(entityManager, sceneGraph, Math::Vector3{});
    engine.SetActiveCamera(camera);

    engine.TickFrame();
    const std::uint32_t insideOne =
        entityManager.GetComponent<Scene::ReflectionProbe3DComponent>(probe)
            .captureGeneration;
    EXPECT_GE(insideOne, 1U) << "camera inside: the probe keeps its imagery current";
    engine.TickFrame();
    const std::uint32_t insideTwo =
        entityManager.GetComponent<Scene::ReflectionProbe3DComponent>(probe)
            .captureGeneration;
    EXPECT_GT(insideTwo, insideOne) << "camera inside: each tick refreshes";

    // Walk out (default probe size 5 => half extent 2.5; 10 units is comfortably beyond) and the
    // meter falls dead on the very first outside tick.
    Scene::TransformComponent farTransform;
    farTransform.localPosition = Math::Vector3{10.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, camera, farTransform);
    engine.TickFrame();
    engine.TickFrame();
    EXPECT_EQ(entityManager.GetComponent<Scene::ReflectionProbe3DComponent>(probe)
                  .captureGeneration,
              insideTwo) << "camera outside: no capture at all - the eye sees nothing of it";
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::ReflectionProbe3DComponent>(probe)
                        .cameraInfluenceWeight,
                    0.0F) << "outside the face the camera's blend weight reads exactly zero";
}

TEST(EngineCoreTest, ReflectionProbe3D_CameraInfluenceWeightTracksCameraPositionExactly) {
    // The first-class blend weight: dead center reads 1.0 (perfectly influential), halfway to the
    // face 0.5, and passing the face snaps to 0.0 - each measured through the public component
    // state, which is exactly the number a shading pass would blend with.
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    Scene::ReflectionProbe3DComponent probeTemplate;
    probeTemplate.size = Math::Vector3{4.0F, 4.0F, 4.0F}; // half extent 2 on every axis
    probeTemplate.updateMode = Scene::ReflectionProbeUpdateMode::EveryFrame;
    const Scene::Entity probe = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, probe, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::ReflectionProbe3DComponent>(probe, probeTemplate);
    const Scene::Entity camera =
        CreateWatchingCameraAt(entityManager, sceneGraph, Math::Vector3{});
    engine.SetActiveCamera(camera);

    engine.TickFrame();
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::ReflectionProbe3DComponent>(probe)
                        .cameraInfluenceWeight,
                    1.0F) << "dead center: completely influential";

    Scene::TransformComponent halfTransform;
    halfTransform.localPosition = Math::Vector3{1.0F, 0.0F, 0.0F}; // halfway: 1/2 of half-extent
    sceneGraph.SetLocalTransform(entityManager, camera, halfTransform);
    engine.TickFrame();
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::ReflectionProbe3DComponent>(probe)
                        .cameraInfluenceWeight,
                    0.5F) << "halfway: exactly the linear falloff midpoint";

    Scene::TransformComponent faceTransform;
    faceTransform.localPosition = Math::Vector3{2.0F, 0.0F, 0.0F}; // exactly ON the face
    sceneGraph.SetLocalTransform(entityManager, camera, faceTransform);
    engine.TickFrame();
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::ReflectionProbe3DComponent>(probe)
                        .cameraInfluenceWeight,
                    0.0F) << "the face itself counts as outside - no floating sliver of weight";
}

TEST(EngineCoreTest, ReflectionProbe3D_OnDemandServicesTheLatchThenClearsIt) {
    // The inspector "Recapture" contract: latched, the next tick resolves exactly one capture
    // and CLEARS the latch - second tick does nothing, until latched again.
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    const Scene::Entity probe = CreateReflectionProbeAt(
        entityManager, sceneGraph, Math::Vector3{},
        Scene::ReflectionProbeUpdateMode::OnDemand);

    engine.TickFrame();
    EXPECT_EQ(entityManager.GetComponent<Scene::ReflectionProbe3DComponent>(probe)
                  .captureGeneration,
              0U) << "without the latch OnDemand captures nothing";

    entityManager.GetComponent<Scene::ReflectionProbe3DComponent>(probe).updateRequested =
        true;
    engine.TickFrame();
    {
        const Scene::ReflectionProbe3DComponent& live =
            entityManager.GetComponent<Scene::ReflectionProbe3DComponent>(probe);
        EXPECT_EQ(live.captureGeneration, 1U) << "the latch delivered exactly one capture";
        EXPECT_FALSE(live.updateRequested) << "and cleared itself the same tick it fired";
    }

    engine.TickFrame();
    EXPECT_EQ(entityManager.GetComponent<Scene::ReflectionProbe3DComponent>(probe)
                  .captureGeneration,
              1U) << "no latch, no capture - the probe waits for the next demand";
}

TEST(EngineCoreTest, ReflectionProbe3D_CaptureBudgetAgesOutOfStarvationNeverNearestOnly) {
    // Three EveryFrame probes with a budget of two, all containing the camera: naive nearest-
    // first would starve the farthest one forever under continuous demand, so priority is the
    // WAIT AGE first, distance second. Measured: tick one takes the two nearest by tie-break,
    // far one tick ages visibly (captureWaitTicks 1), and on tick two the aged waiter cuts the
    // line ahead of nearer-but-fresher askers.
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    // Distances 0.5 / 1.0 / 1.5 on +X keep d^2 distinct (no ties), while the default box of
    // half extent 2.5 still contains the origin - all three demand a capture every tick.
    const Scene::Entity nearProbe = CreateReflectionProbeAt(
        entityManager, sceneGraph, Math::Vector3{0.5F, 0.0F, 0.0F},
        Scene::ReflectionProbeUpdateMode::EveryFrame);
    const Scene::Entity midProbe = CreateReflectionProbeAt(
        entityManager, sceneGraph, Math::Vector3{1.0F, 0.0F, 0.0F},
        Scene::ReflectionProbeUpdateMode::EveryFrame);
    const Scene::Entity farProbe = CreateReflectionProbeAt(
        entityManager, sceneGraph, Math::Vector3{1.5F, 0.0F, 0.0F},
        Scene::ReflectionProbeUpdateMode::EveryFrame);
    const Scene::Entity camera =
        CreateWatchingCameraAt(entityManager, sceneGraph, Math::Vector3{});
    engine.SetActiveCamera(camera);
    const std::array<Scene::Entity, 3U> probes{nearProbe, midProbe, farProbe};

    engine.TickFrame();
    EXPECT_EQ(TotalProbeGenerations(entityManager, probes),
              Scene::kMaximumReflectionProbeCapturesPerTick)
        << "tick one serves exactly the budget, never the whole queue";
    EXPECT_EQ(entityManager.GetComponent<Scene::ReflectionProbe3DComponent>(nearProbe)
                  .captureGeneration,
              1U);
    EXPECT_EQ(entityManager.GetComponent<Scene::ReflectionProbe3DComponent>(midProbe)
                  .captureGeneration,
              1U);
    EXPECT_EQ(entityManager.GetComponent<Scene::ReflectionProbe3DComponent>(farProbe)
                  .captureGeneration,
              0U) << "the farthest one waits (distances tie-free)";
    EXPECT_EQ(entityManager.GetComponent<Scene::ReflectionProbe3DComponent>(farProbe)
                  .captureWaitTicks,
              1U) << "and its wait age shows in its own component state";

    engine.TickFrame();
    // Tick two: far now beats both nearer probes by age and captures ahead of them.
    EXPECT_EQ(entityManager.GetComponent<Scene::ReflectionProbe3DComponent>(farProbe)
                  .captureGeneration,
              1U) << "the aged waiter cuts the line - starvation is impossible";
    EXPECT_EQ(entityManager.GetComponent<Scene::ReflectionProbe3DComponent>(farProbe)
                  .captureWaitTicks,
              0U) << "service resets the age";
    EXPECT_EQ(TotalProbeGenerations(entityManager, probes),
              2U * Scene::kMaximumReflectionProbeCapturesPerTick)
        << "every tick still stays within budget";
}

namespace {

Scene::Entity CreateWorldPartitionAt(Scene::IEntityManager& entityManager,
                                           Scene::ISceneGraph& sceneGraph,
                                           const Math::Vector3& position,
                                           const float cellSize,
                                           const std::array<std::uint32_t, 3U>& counts,
                                           const std::uint32_t maximumLoadedCells) {
    const Scene::Entity partition = entityManager.CreateEntity();
    Scene::TransformComponent transform;
    transform.localPosition = position;
    sceneGraph.AttachTransform(entityManager, partition, transform);
    Scene::WorldPartition3DComponent component;
    component.cellSize = cellSize;
    component.cellCounts = counts;
    component.maximumLoadedCells = maximumLoadedCells;
    entityManager.AddComponent<Scene::WorldPartition3DComponent>(partition, component);
    return partition;
}

Scene::Entity CreatePartitionChildAt(Scene::IEntityManager& entityManager,
                                           Scene::ISceneGraph& sceneGraph,
                                           const Scene::Entity parent,
                                           const Math::Vector3& position,
                                           const bool withMesh) {
    const Scene::Entity child = entityManager.CreateEntity();
    Scene::TransformComponent transform;
    transform.localPosition = position;
    sceneGraph.AttachTransform(entityManager, child, transform);
    sceneGraph.SetParent(entityManager, child, parent);
    if (withMesh) {
        entityManager.AddComponent<Scene::MeshComponent>(child, Scene::MeshComponent{});
    }
    return child;
}

} // namespace

TEST(EngineCoreTest, WorldPartition3D_MembershipAttachesToDrawablesAndOutsideStaysUnmanaged) {
    // The engine-owned runtime state attaches to drawable descendants (mesh and primitive mesh
    // here; a transform-only child and a light stay clean). The partition beyond its volume is
    // nobody's business (membership there exists but its live verdict is always true), and
    // loadedCellCount only counts cells actually inside the volume.
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    const Scene::Entity partition = CreateWorldPartitionAt(
        entityManager, sceneGraph, Math::Vector3{}, 10.0F, {2U, 1U, 2U}, 2U);
    const Scene::Entity insideMesh =
        CreatePartitionChildAt(entityManager, sceneGraph, partition,
                                  Math::Vector3{5.0F, 0.0F, 5.0F}, /*withMesh=*/true);
    const Scene::Entity meshlessChild =
        CreatePartitionChildAt(entityManager, sceneGraph, partition,
                                  Math::Vector3{5.0F, 0.0F, 5.0F}, /*withMesh=*/false);
    const Scene::Entity primitiveChild =
        CreatePartitionChildAt(entityManager, sceneGraph, partition,
                                  Math::Vector3{5.0F, 0.0F, 5.0F}, /*withMesh=*/false);
    entityManager.AddComponent<Scene::PrimitiveMeshComponent>(primitiveChild,
                                                                   Scene::PrimitiveMeshComponent{});
    const Scene::Entity lightChild =
        CreatePartitionChildAt(entityManager, sceneGraph, partition,
                                  Math::Vector3{5.0F, 0.0F, 5.0F}, /*withMesh=*/false);
    entityManager.AddComponent<Scene::LightComponent>(lightChild, Scene::LightComponent{});
    const Scene::Entity outsideMesh =
        CreatePartitionChildAt(entityManager, sceneGraph, partition,
                                  Math::Vector3{500.0F, 0.0F, 500.0F}, /*withMesh=*/true);
    const Scene::Entity camera =
        CreateWatchingCameraAt(entityManager, sceneGraph, Math::Vector3{5.0F, 0.0F, 5.0F});
    engine.SetActiveCamera(camera);

    engine.TickFrame();

    ASSERT_TRUE(entityManager.HasComponent<Scene::WorldPartition3DMembershipComponent>(
        insideMesh));
    const Scene::WorldPartition3DMembershipComponent& insideMembership =
        entityManager.GetComponent<Scene::WorldPartition3DMembershipComponent>(insideMesh);
    EXPECT_TRUE(insideMembership.live) << "inside cell (0,0,0) with budget 2: live";
    EXPECT_EQ(insideMembership.partition, partition) << "the engine stamps the deciding owner";

    EXPECT_FALSE(entityManager.HasComponent<Scene::WorldPartition3DMembershipComponent>(
        meshlessChild)) << "no drawable, no membership - authoritatively not every descendant";
    ASSERT_TRUE(entityManager.HasComponent<Scene::WorldPartition3DMembershipComponent>(
        primitiveChild))
        << "a primitive mesh is a drawable the vis-budget can skip";
    EXPECT_FALSE(entityManager.HasComponent<Scene::WorldPartition3DMembershipComponent>(
        lightChild))
        << "lights are not partition drawables";

    ASSERT_TRUE(entityManager.HasComponent<Scene::WorldPartition3DMembershipComponent>(
        outsideMesh));
    EXPECT_TRUE(entityManager.GetComponent<Scene::WorldPartition3DMembershipComponent>(
                    outsideMesh)
                    .live)
        << "outside the partition volume: never culled by a partition that cannot see it";

    EXPECT_EQ(entityManager.GetComponent<Scene::WorldPartition3DComponent>(partition)
                  .loadedCellCount,
              1U) << "the unmanaged outside point creates no occupied cell";
}

TEST(EngineCoreTest, WorldPartition3D_BudgetAdmitsNearestCellOnlyAndCameraMoveFlipsTheVerdict) {
    // maximumLoadedCells is the hard contract: with one slot and two occupied cells, only the
    // serially-nearest cell's member renders - move the camera to the other cell and the verdict
    // flips seat afterwards, deterministically (nothing depends on iteration order because the
    // distance key breaks the tie cleanly).
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    const Scene::Entity partition = CreateWorldPartitionAt(
        entityManager, sceneGraph, Math::Vector3{}, 10.0F, {2U, 1U, 1U}, 1U);
    const Scene::Entity nearMesh =
        CreatePartitionChildAt(entityManager, sceneGraph, partition,
                                  Math::Vector3{5.0F, 0.0F, 0.0F}, /*withMesh=*/true);
    const Scene::Entity farMesh =
        CreatePartitionChildAt(entityManager, sceneGraph, partition,
                                  Math::Vector3{15.0F, 0.0F, 0.0F}, /*withMesh=*/true);
    const Scene::Entity camera =
        CreateWatchingCameraAt(entityManager, sceneGraph, Math::Vector3{});
    engine.SetActiveCamera(camera);

    engine.TickFrame();
    EXPECT_TRUE(entityManager.GetComponent<Scene::WorldPartition3DMembershipComponent>(
                    nearMesh)
                    .live) << "cell (0,0,0): the nearest occupied cell earns the slot";
    EXPECT_FALSE(entityManager.GetComponent<Scene::WorldPartition3DMembershipComponent>(
                     farMesh)
                     .live) << "cell (1,0,0): out of budget, out of the frame";
    EXPECT_EQ(entityManager.GetComponent<Scene::WorldPartition3DComponent>(partition)
                  .loadedCellCount,
              1U);

    Scene::TransformComponent farCameraTransform;
    farCameraTransform.localPosition = Math::Vector3{15.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, camera, farCameraTransform);
    engine.TickFrame();
    EXPECT_FALSE(entityManager.GetComponent<Scene::WorldPartition3DMembershipComponent>(
                     nearMesh)
                     .live) << "the verdict flips with the camera, not with sentiment";
    EXPECT_TRUE(entityManager.GetComponent<Scene::WorldPartition3DMembershipComponent>(
                    farMesh)
                    .live);
}

TEST(EngineCoreTest, WorldPartition3D_NestedPartitionsTheInnerOneOwnsItsSubtree) {
    // Closest-ancestor-wins: a partition inside another partition manages its own subtree, and
    // nothing the outer walk touches ever rewrites that decision - measured as the inner child's
    // stamped owner being the INNER partition, not the outer.
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    const Scene::Entity outer = CreateWorldPartitionAt(
        entityManager, sceneGraph, Math::Vector3{}, 50.0F, {2U, 1U, 1U}, 2U);
    const Scene::Entity inner = CreateWorldPartitionAt(
        entityManager, sceneGraph, Math::Vector3{}, 10.0F, {2U, 1U, 1U}, 2U);
    sceneGraph.SetParent(entityManager, inner, outer);
    const Scene::Entity innerMesh =
        CreatePartitionChildAt(entityManager, sceneGraph, inner,
                                  Math::Vector3{5.0F, 0.0F, 0.0F}, /*withMesh=*/true);
    const Scene::Entity outerMesh =
        CreatePartitionChildAt(entityManager, sceneGraph, outer,
                                  Math::Vector3{25.0F, 0.0F, 0.0F}, /*withMesh=*/true);
    const Scene::Entity camera =
        CreateWatchingCameraAt(entityManager, sceneGraph, Math::Vector3{});
    engine.SetActiveCamera(camera);

    engine.TickFrame();
    EXPECT_EQ(entityManager.GetComponent<Scene::WorldPartition3DMembershipComponent>(
                  innerMesh)
                  .partition,
              inner) << "the inner partition decides its own subtree";
    EXPECT_EQ(entityManager.GetComponent<Scene::WorldPartition3DMembershipComponent>(
                  outerMesh)
                  .partition,
              outer) << "the outer one only ever touches its own descendants";
}

TEST(EngineCoreTest, WorldPartition3D_DisabledReleasesEveryMemberAndClearsTheLiveCount) {
    // The toggle must show EVERYTHING, because leaving a stale fade behind after switching the
    // system off is the honest failure mode exactly backwards - fail open, measured everywhere.
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    const Scene::Entity partition = CreateWorldPartitionAt(
        entityManager, sceneGraph, Math::Vector3{}, 10.0F, {2U, 1U, 1U}, 1U);
    const Scene::Entity farMesh =
        CreatePartitionChildAt(entityManager, sceneGraph, partition,
                                  Math::Vector3{15.0F, 0.0F, 0.0F}, /*withMesh=*/true);
    static_cast<void>(CreatePartitionChildAt(entityManager, sceneGraph, partition,
                                                Math::Vector3{5.0F, 0.0F, 0.0F},
                                                /*withMesh=*/true));
    const Scene::Entity camera =
        CreateWatchingCameraAt(entityManager, sceneGraph, Math::Vector3{});
    engine.SetActiveCamera(camera);

    engine.TickFrame();
    ASSERT_FALSE(entityManager.GetComponent<Scene::WorldPartition3DMembershipComponent>(
                     farMesh)
                     .live)
        << "setup: budget 1, far cell out of budget";

    entityManager.GetComponent<Scene::WorldPartition3DComponent>(partition).enabled =
        false;
    engine.TickFrame();
    EXPECT_TRUE(entityManager.GetComponent<Scene::WorldPartition3DMembershipComponent>(
                    farMesh)
                    .live)
        << "disabled: every member renders, no stale fade lingers";
    EXPECT_EQ(entityManager.GetComponent<Scene::WorldPartition3DComponent>(partition)
                  .loadedCellCount,
              0U) << "and the live cell count zeroes instead of freezing";
}

TEST(EngineCoreTest, WorldPartition3D_MembershipFollowsSubtreeGrowthNotStaleSnapshots) {
    // The state lives in the sync, not in a one-shot: a mesh added between ticks earns its
    // membership on the NEXT tick, and destroying a member mid-run leaves nothing stale behind -
    // measured as the live-set moving exactly with the mutate-then-tick cadence.
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    const Scene::Entity partition = CreateWorldPartitionAt(
        entityManager, sceneGraph, Math::Vector3{}, 10.0F, {2U, 1U, 1U}, 2U);
    const Scene::Entity camera =
        CreateWatchingCameraAt(entityManager, sceneGraph, Math::Vector3{});
    engine.SetActiveCamera(camera);

    engine.TickFrame();
    const Scene::Entity added =
        CreatePartitionChildAt(entityManager, sceneGraph, partition,
                                  Math::Vector3{5.0F, 0.0F, 0.0F}, /*withMesh=*/true);
    EXPECT_FALSE(entityManager.HasComponent<Scene::WorldPartition3DMembershipComponent>(
        added)) << "mid-tick birth: nothing attaches before the follow-up tick's sync";

    engine.TickFrame();
    ASSERT_TRUE(entityManager.HasComponent<Scene::WorldPartition3DMembershipComponent>(
        added));
    EXPECT_TRUE(entityManager.GetComponent<Scene::WorldPartition3DMembershipComponent>(
                    added)
                    .live);

    // Destroy it and the partition must still tick cleanly with nothing stale behind.
    entityManager.DestroyEntity(added);
    engine.TickFrame();
    EXPECT_EQ(entityManager.GetComponent<Scene::WorldPartition3DComponent>(partition)
                  .loadedCellCount,
              0U) << "a dead member unbooks its whole cell - no ghost occupancy";
}

namespace {

Scene::Entity CreateVisibilityRegionAt(Scene::IEntityManager& entityManager,
                                             Scene::ISceneGraph& sceneGraph,
                                             const Math::Vector3& position,
                                             const Math::Vector3& halfExtents,
                                             const std::uint32_t layers) {
    const Scene::Entity region = entityManager.CreateEntity();
    Scene::TransformComponent transform;
    transform.localPosition = position;
    sceneGraph.AttachTransform(entityManager, region, transform);
    Scene::VisibilityRegion3DComponent component;
    component.halfExtents = halfExtents;
    component.visibilityLayers = layers;
    entityManager.AddComponent<Scene::VisibilityRegion3DComponent>(region, component);
    return region;
}

Scene::Entity CreateStandaloneMeshAt(Scene::IEntityManager& entityManager,
                                           Scene::ISceneGraph& sceneGraph,
                                           const Math::Vector3& position,
                                           const std::uint32_t layers) {
    const Scene::Entity mesh = entityManager.CreateEntity();
    Scene::TransformComponent transform;
    transform.localPosition = position;
    sceneGraph.AttachTransform(entityManager, mesh, transform);
    Scene::MeshComponent component{};
    component.visibilityLayers = layers;
    entityManager.AddComponent<Scene::MeshComponent>(mesh, component);
    return mesh;
}

} // namespace

TEST(EngineCoreTest, VisibilityRegion3D_CameraOutsideTheRoomSkipsItsInteriorCameraInsideShowsIt) {
    // The headline claim Godot cannot answer: interior contents cost zero render while nobody
    // stands inside the room, and reappear the tick the eye enters - with the region's own
    // active flag readable as the same verdict the membership carries.
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    static_cast<void>(CreateVisibilityRegionAt(entityManager, sceneGraph, Math::Vector3{},
                                                  Math::Vector3{5.0F, 5.0F, 5.0F},
                                                  0xFFFFFFFFU));
    const Scene::Entity content = CreateStandaloneMeshAt(
        entityManager, sceneGraph, Math::Vector3{1.0F, 0.0F, 0.0F}, 0x00000001U);
    const Scene::Entity primitive = entityManager.CreateEntity();
    Scene::TransformComponent primitiveTransform;
    primitiveTransform.localPosition = Math::Vector3{1.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, primitive, primitiveTransform);
    entityManager.AddComponent<Scene::PrimitiveMeshComponent>(primitive,
                                                                   Scene::PrimitiveMeshComponent{});
    const Scene::Entity light = entityManager.CreateEntity();
    Scene::TransformComponent lightTransform;
    lightTransform.localPosition = Math::Vector3{1.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, light, lightTransform);
    entityManager.AddComponent<Scene::LightComponent>(light, Scene::LightComponent{});
    const Scene::Entity outside = CreateStandaloneMeshAt(
        entityManager, sceneGraph, Math::Vector3{50.0F, 0.0F, 0.0F}, 0x00000001U);
    const Scene::Entity camera =
        CreateWatchingCameraAt(entityManager, sceneGraph, Math::Vector3{50.0F, 0.0F, 0.0F});
    engine.SetActiveCamera(camera);

    engine.TickFrame();
    ASSERT_TRUE(entityManager.HasComponent<Scene::VisibilityRegion3DMembershipComponent>(
        content));
    EXPECT_FALSE(entityManager.GetComponent<Scene::VisibilityRegion3DMembershipComponent>(
                     content)
                     .live)
        << "camera outside: interior content is skipped with zero render work";
    EXPECT_FALSE(entityManager.HasComponent<Scene::VisibilityRegion3DMembershipComponent>(
        outside)) << "content outside every region is not a member of anything";
    ASSERT_TRUE(entityManager.HasComponent<Scene::VisibilityRegion3DMembershipComponent>(
        primitive))
        << "a primitive mesh inside the room is a drawable the region can skip";
    EXPECT_FALSE(entityManager.GetComponent<Scene::VisibilityRegion3DMembershipComponent>(
                     primitive)
                     .live);
    EXPECT_FALSE(entityManager.HasComponent<Scene::VisibilityRegion3DMembershipComponent>(
        light))
        << "lights are not region drawables";

    Scene::TransformComponent enterTransform;
    enterTransform.localPosition = Math::Vector3{1.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, camera, enterTransform);
    engine.TickFrame();
    EXPECT_TRUE(entityManager.GetComponent<Scene::VisibilityRegion3DMembershipComponent>(
                    content)
                    .live)
        << "the tick the eye steps in, the room draws again - no one-frame stale dark";
}

TEST(EngineCoreTest, VisibilityRegion3D_LayerGateKeepsUninvitedMeshesOutOfTheRoom) {
    // The roadmap's own phrase: the REGION's visibilityLayers gate what renders inside it. A
    // props-layer mesh is managed by the props region; an NPC on the default layer standing in
    // the same room is NOT, so nobody has to special-object the walk-through cases.
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    static_cast<void>(CreateVisibilityRegionAt(entityManager, sceneGraph, Math::Vector3{},
                                                  Math::Vector3{5.0F, 5.0F, 5.0F},
                                                  0x00000002U)); // props only
    const Scene::Entity prop = CreateStandaloneMeshAt(
        entityManager, sceneGraph, Math::Vector3{1.0F, 0.0F, 0.0F}, 0x00000002U);
    const Scene::Entity npc = CreateStandaloneMeshAt(
        entityManager, sceneGraph, Math::Vector3{-1.0F, 0.0F, 0.0F}, 0x00000001U);
    const Scene::Entity camera =
        CreateWatchingCameraAt(entityManager, sceneGraph, Math::Vector3{50.0F, 0.0F, 0.0F});
    engine.SetActiveCamera(camera);

    engine.TickFrame();
    ASSERT_TRUE(entityManager.HasComponent<Scene::VisibilityRegion3DMembershipComponent>(
        prop));
    EXPECT_FALSE(entityManager.GetComponent<Scene::VisibilityRegion3DMembershipComponent>(
                     prop)
                     .live)
        << "same room, right layer: skipped while the room is empty";
    EXPECT_FALSE(entityManager.HasComponent<Scene::VisibilityRegion3DMembershipComponent>(
        npc)) << "same room, wrong layer: the room simply does not claim this mesh";
}

TEST(EngineCoreTest, VisibilityRegion3D_WalkingOutReleasesTheVerdictOnTheVeryNextTick) {
    // The regression that decides this slice is honest: a member that LEAVES the box must not
    // keep last tick's fade. Membership refreshes per tick, and the live flag goes back to true
    // the same tick the mesh crosses out of the room - never a frame of dark behind it.
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    static_cast<void>(CreateVisibilityRegionAt(entityManager, sceneGraph, Math::Vector3{},
                                                  Math::Vector3{5.0F, 5.0F, 5.0F},
                                                  0xFFFFFFFFU));
    const Scene::Entity content = CreateStandaloneMeshAt(
        entityManager, sceneGraph, Math::Vector3{1.0F, 0.0F, 0.0F}, 0x00000001U);
    const Scene::Entity camera =
        CreateWatchingCameraAt(entityManager, sceneGraph, Math::Vector3{50.0F, 0.0F, 0.0F});
    engine.SetActiveCamera(camera);

    engine.TickFrame();
    ASSERT_FALSE(entityManager.GetComponent<Scene::VisibilityRegion3DMembershipComponent>(
                     content)
                     .live)
        << "setup: inside an inactive room, skipped";

    Scene::TransformComponent doorTransform;
    doorTransform.localPosition = Math::Vector3{10.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, content, doorTransform);
    engine.TickFrame();
    ASSERT_TRUE(entityManager.HasComponent<Scene::VisibilityRegion3DMembershipComponent>(
        content))
        << "the engine leaves the marker; the VERDICT is what is released";
    EXPECT_TRUE(entityManager.GetComponent<Scene::VisibilityRegion3DMembershipComponent>(
                    content)
                    .live) << "out of the room: rendered again the very next tick, not someday";
}

TEST(EngineCoreTest, VisibilityRegion3D_DisabledReleasesAndDestroyingTheRegionRehomesItsMembers) {
    // Two fail-open paths at once: disabling a region shows its content from the next tick, and
    // destroying the region lets the membership rehome to ANOTHER containing region (or nobody)
    // without a stale fade ever binding the mesh again.
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    const Scene::Entity roomA =
        CreateVisibilityRegionAt(entityManager, sceneGraph, Math::Vector3{},
                                    Math::Vector3{5.0F, 5.0F, 5.0F}, 0xFFFFFFFFU);
    const Scene::Entity content = CreateStandaloneMeshAt(
        entityManager, sceneGraph, Math::Vector3{1.0F, 0.0F, 0.0F}, 0x00000001U);
    const Scene::Entity camera =
        CreateWatchingCameraAt(entityManager, sceneGraph, Math::Vector3{50.0F, 0.0F, 0.0F});
    engine.SetActiveCamera(camera);

    engine.TickFrame();
    ASSERT_FALSE(entityManager.GetComponent<Scene::VisibilityRegion3DMembershipComponent>(
                     content)
                     .live);

    entityManager.GetComponent<Scene::VisibilityRegion3DComponent>(roomA).enabled =
        false;
    engine.TickFrame();
    EXPECT_TRUE(entityManager.GetComponent<Scene::VisibilityRegion3DMembershipComponent>(
                    content)
                    .live) << "disabled: released the same tick, no stale fade";

    // Re-enable, then destroy: the membership rebrands to kInvalidEntity and stays live.
    entityManager.GetComponent<Scene::VisibilityRegion3DComponent>(roomA).enabled =
        true;
    engine.TickFrame();
    ASSERT_FALSE(entityManager.GetComponent<Scene::VisibilityRegion3DMembershipComponent>(
                     content)
                     .live);
    entityManager.DestroyEntity(roomA);
    engine.TickFrame();
    const Scene::VisibilityRegion3DMembershipComponent& membership =
        entityManager.GetComponent<Scene::VisibilityRegion3DMembershipComponent>(content);
    EXPECT_TRUE(membership.live)
        << "dead region: fail open on the render path AND in the recorded verdict";
    EXPECT_EQ(membership.region, Scene::kInvalidEntity)
        << "ownership is reassigned - content is rehomeable, never orphan-hidden";
}

TEST(EngineCoreTest, VisibilityRegion3D_OverlappingRoomsTheNearestCenterOwnsTheMesh) {
    // Overlapping rooms need ONE deterministic owner per mesh; otherwise the culling of a double-
    // booked room was history-dependent. Measured: the nearest region center stamps it, and a
    // second tick answers the same way without oscillation.
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    static_cast<void>(CreateVisibilityRegionAt(entityManager, sceneGraph, Math::Vector3{},
                                                  Math::Vector3{6.0F, 6.0F, 6.0F},
                                                  0xFFFFFFFFU)); // big, centered at 0
    const Scene::Entity small =
        CreateVisibilityRegionAt(entityManager, sceneGraph,
                                    Math::Vector3{4.0F, 0.0F, 0.0F},
                                    Math::Vector3{2.0F, 2.0F, 2.0F}, 0xFFFFFFFFU);
    const Scene::Entity content = CreateStandaloneMeshAt(
        entityManager, sceneGraph, Math::Vector3{4.5F, 0.0F, 0.0F}, 0x00000001U);
    const Scene::Entity camera =
        CreateWatchingCameraAt(entityManager, sceneGraph, Math::Vector3{50.0F, 0.0F, 0.0F});
    engine.SetActiveCamera(camera);

    engine.TickFrame();
    EXPECT_EQ(entityManager.GetComponent<Scene::VisibilityRegion3DMembershipComponent>(
                  content)
                  .region,
              small) << "inside both rooms: the NEAREST center owns the mesh";
    engine.TickFrame();
    EXPECT_EQ(entityManager.GetComponent<Scene::VisibilityRegion3DMembershipComponent>(
                  content)
                  .region,
              small) << "second tick: same owner, no oscillation between overlapping rooms";
}

TEST(EngineCoreTest, VisibilityRegion3D_NoViewersAtAllFailsOpenWithActiveRegions) {
    // Empty worlds show everything: with no camera and no controller, regions report active and
    // their members report live. The fail-open belongs to the POLICY (the sync), while the pure
    // function stays measurable - asserted separately in the object tests.
    EngineConfig config = MakeTestConfig();
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

    const Scene::Entity room =
        CreateVisibilityRegionAt(entityManager, sceneGraph, Math::Vector3{},
                                    Math::Vector3{5.0F, 5.0F, 5.0F}, 0xFFFFFFFFU);
    const Scene::Entity content = CreateStandaloneMeshAt(
        entityManager, sceneGraph, Math::Vector3{1.0F, 0.0F, 0.0F}, 0x00000001U);

    engine.TickFrame();
    EXPECT_TRUE(entityManager.GetComponent<Scene::VisibilityRegion3DComponent>(room)
                    .active)
        << "no viewer anywhere: active, because managing nothing hides nothing";
    ASSERT_TRUE(entityManager.HasComponent<Scene::VisibilityRegion3DMembershipComponent>(
        content));
    EXPECT_TRUE(entityManager.GetComponent<Scene::VisibilityRegion3DMembershipComponent>(
                    content)
                    .live);
}


[[nodiscard]] Scene::Entity MakeLagShooter(Scene::IEntityManager& entityManager,
                                                 Scene::ISceneGraph& sceneGraph,
                                                 const Math::Vector3& position) {
    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::TransformComponent transform{};
    transform.localPosition = position;
    sceneGraph.AttachTransform(entityManager, entity, transform);
    Scene::Hitbox3DComponent hitbox{};
    hitbox.halfExtents = Math::Vector3{1.0F, 1.0F, 1.0F};
    hitbox.damageChannel = "melee";
    entityManager.AddComponent<Scene::Hitbox3DComponent>(entity, hitbox);
    return entity;
}

[[nodiscard]] Scene::Entity MakeLagTarget(Scene::IEntityManager& entityManager,
                                               Scene::ISceneGraph& sceneGraph,
                                               const Math::Vector3& position) {
    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::TransformComponent transform{};
    transform.localPosition = position;
    sceneGraph.AttachTransform(entityManager, entity, transform);
    Scene::Hurtbox3DComponent hurtbox{};
    hurtbox.halfExtents = Math::Vector3{1.0F, 1.0F, 1.0F};
    hurtbox.damageChannel = "melee";
    entityManager.AddComponent<Scene::Hurtbox3DComponent>(entity, hurtbox);
    entityManager.AddComponent<Scene::HealthComponent>(entity, Scene::HealthComponent{});
    return entity;
}

void MoveLagEntity(Scene::IEntityManager& entityManager,
                      Scene::ISceneGraph& sceneGraph, const Scene::Entity entity,
                      const Math::Vector3& position) {
    Scene::TransformComponent transform =
        entityManager.GetComponent<Scene::TransformComponent>(entity);
    transform.localPosition = position;
    sceneGraph.SetLocalTransform(entityManager, entity, transform);
}

[[nodiscard]] float HealthOf(Scene::IEntityManager& entityManager,
                               const Scene::Entity entity) {
    return entityManager.GetComponent<Scene::HealthComponent>(entity).health;
}

TEST(EngineCoreTest, LagTick_RecordsEveryFrame) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    // 1-based engine frames: nothing recorded before the first sync.
    EXPECT_EQ(engine.GetLagTick(), 0U);
    EXPECT_EQ(engine.GetLagCompensator().HistorySize(), 0U);
    engine.TickFrame();
    engine.TickFrame();
    engine.TickFrame();
    EXPECT_EQ(engine.GetLagTick(), 3U);
    EXPECT_EQ(engine.GetLagCompensator().HistorySize(), 3U);

    engine.Shutdown();
}

TEST(EngineCoreTest, LagShot_MergedBeforeTrackerDamages) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Core::EngineServices& services = engine.GetServices();
    Scene::IEntityManager& entityManager = services.GetEntityManager();
    Scene::ISceneGraph& sceneGraph = services.GetSceneGraph();

    const Scene::Entity shooter =
        MakeLagShooter(entityManager, sceneGraph, Math::Vector3{0.0F, 0.0F, 0.0F});
    const Scene::Entity target =
        MakeLagTarget(entityManager, sceneGraph, Math::Vector3{1.5F, 0.0F, 0.0F});

    // Frame 1 records the overlap - and live-hits it the same frame (entered, 100 -> 99).
    engine.TickFrame();
    ASSERT_EQ(engine.GetLagTick(), 1U);
    EXPECT_FLOAT_EQ(HealthOf(entityManager, target), 99.0F);

    // Escape: frame 2 records the miss, and the live strike exits without further damage.
    MoveLagEntity(entityManager, sceneGraph, target, Math::Vector3{20.0F, 0.0F, 0.0F});
    engine.TickFrame();
    ASSERT_EQ(engine.GetLagTick(), 2U);
    EXPECT_FLOAT_EQ(HealthOf(entityManager, target), 99.0F);

    // The lagged shot, resolved against tick 1 through the engine's own compensator...
    Physics::LagShotReport compensated =
        engine.GetLagCompensator().ResolveShot(shooter, 1U, entityManager);
    ASSERT_EQ(compensated.pairs.size(), 1U);

    // ...re-enters on frame 3 exactly like a live strike: damage plus the typed event.
    std::vector<Gameplay::HealthDamagedEvent> damaged;
    const Events::EventSubscription damagedSub =
        services.GetEventSystem().Subscribe<Gameplay::HealthDamagedEvent>(
            [&](const Gameplay::HealthDamagedEvent& event) { damaged.push_back(event); });
    static_cast<void>(damagedSub);
    engine.SubmitLagShot(std::move(compensated));
    engine.TickFrame();
    EXPECT_FLOAT_EQ(HealthOf(entityManager, target), 98.0F);
    services.GetEventSystem().DispatchQueued();
    ASSERT_EQ(damaged.size(), 1U);
    EXPECT_EQ(damaged[0].hurtbox, target);
    EXPECT_EQ(damaged[0].hitbox, shooter);

    engine.Shutdown();
}

TEST(EngineCoreTest, LagShot_ReplayedSubmitStaysSilent) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Core::EngineServices& services = engine.GetServices();
    Scene::IEntityManager& entityManager = services.GetEntityManager();
    Scene::ISceneGraph& sceneGraph = services.GetSceneGraph();

    const Scene::Entity shooter =
        MakeLagShooter(entityManager, sceneGraph, Math::Vector3{0.0F, 0.0F, 0.0F});
    const Scene::Entity target =
        MakeLagTarget(entityManager, sceneGraph, Math::Vector3{1.5F, 0.0F, 0.0F});
    engine.TickFrame();
    MoveLagEntity(entityManager, sceneGraph, target, Math::Vector3{20.0F, 0.0F, 0.0F});
    engine.TickFrame();
    ASSERT_FLOAT_EQ(HealthOf(entityManager, target), 99.0F);

    std::vector<Gameplay::HealthDamagedEvent> damaged;
    const Events::EventSubscription damagedSub =
        services.GetEventSystem().Subscribe<Gameplay::HealthDamagedEvent>(
            [&](const Gameplay::HealthDamagedEvent& event) { damaged.push_back(event); });
    static_cast<void>(damagedSub);

    // Frame 3 re-enters compensated (99 -> 98); frame 4 replays the same report - the pair
    // is already active, so the merge stays silent.
    engine.SubmitLagShot(
        engine.GetLagCompensator().ResolveShot(shooter, 1U, entityManager));
    engine.TickFrame();
    EXPECT_FLOAT_EQ(HealthOf(entityManager, target), 98.0F);
    engine.SubmitLagShot(
        engine.GetLagCompensator().ResolveShot(shooter, 1U, entityManager));
    engine.TickFrame();
    EXPECT_FLOAT_EQ(HealthOf(entityManager, target), 98.0F);

    services.GetEventSystem().DispatchQueued();
    EXPECT_EQ(damaged.size(), 1U);

    engine.Shutdown();
}

TEST(EngineCoreTest, LagShot_EmptyHistoryResolvesEmpty) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Core::EngineServices& services = engine.GetServices();
    Scene::IEntityManager& entityManager = services.GetEntityManager();
    Scene::ISceneGraph& sceneGraph = services.GetSceneGraph();

    const Scene::Entity shooter =
        MakeLagShooter(entityManager, sceneGraph, Math::Vector3{0.0F, 0.0F, 0.0F});
    const Scene::Entity target =
        MakeLagTarget(entityManager, sceneGraph, Math::Vector3{20.0F, 0.0F, 0.0F});

    // No frames ran, so no history exists: the resolve is empty, and submitting it is a
    // no-op the first real frame shrugs off.
    Physics::LagShotReport empty =
        engine.GetLagCompensator().ResolveShot(shooter, 5U, entityManager);
    EXPECT_TRUE(empty.pairs.empty());
    std::vector<Gameplay::HealthDamagedEvent> damaged;
    const Events::EventSubscription damagedSub =
        services.GetEventSystem().Subscribe<Gameplay::HealthDamagedEvent>(
            [&](const Gameplay::HealthDamagedEvent& event) { damaged.push_back(event); });
    static_cast<void>(damagedSub);
    engine.SubmitLagShot(std::move(empty));
    engine.TickFrame();
    EXPECT_EQ(engine.GetLagTick(), 1U);
    EXPECT_FLOAT_EQ(HealthOf(entityManager, target), 100.0F);
    services.GetEventSystem().DispatchQueued();
    EXPECT_TRUE(damaged.empty());

    engine.Shutdown();
}

TEST(EngineCoreTest, LagShot_FractionalResolveSubmits) {
    EngineCore engine(MakeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Core::EngineServices& services = engine.GetServices();
    Scene::IEntityManager& entityManager = services.GetEntityManager();
    Scene::ISceneGraph& sceneGraph = services.GetSceneGraph();

    const Scene::Entity shooter =
        MakeLagShooter(entityManager, sceneGraph, Math::Vector3{0.0F, 0.0F, 0.0F});
    const Scene::Entity target =
        MakeLagTarget(entityManager, sceneGraph, Math::Vector3{4.0F, 0.0F, 0.0F});
    engine.TickFrame();
    EXPECT_FLOAT_EQ(HealthOf(entityManager, target), 100.0F);
    MoveLagEntity(entityManager, sceneGraph, target, Math::Vector3{-4.0F, 0.0F, 0.0F});
    engine.TickFrame();
    EXPECT_FLOAT_EQ(HealthOf(entityManager, target), 100.0F);

    // Neither recorded frame overlaps, but halfway the target stood inside the shooter:
    // the interpolated report enters through the inbox like any lagged shot.
    Physics::LagShotReport between =
        engine.GetLagCompensator().ResolveShotAt(shooter, 1U, 0.5F, entityManager);
    ASSERT_EQ(between.pairs.size(), 1U);
    EXPECT_TRUE(between.interpolated);
    engine.SubmitLagShot(std::move(between));
    engine.TickFrame();
    EXPECT_FLOAT_EQ(HealthOf(entityManager, target), 99.0F);

    engine.Shutdown();
}

namespace {

[[nodiscard]] std::uint32_t ComputeCrc32ForPng(const std::uint8_t* const data,
                                                   const std::size_t length) noexcept {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0U; i < length; ++i) {
        crc ^= static_cast<std::uint32_t>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = -(crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

[[nodiscard]] std::uint32_t ComputeAdler32ForPng(const std::uint8_t* const data,
                                                     const std::size_t length) noexcept {
    std::uint32_t a = 1U;
    std::uint32_t b = 0U;
    constexpr std::uint32_t kModAdler = 65521U;
    for (std::size_t i = 0U; i < length; ++i) {
        a = (a + data[i]) % kModAdler;
        b = (b + a) % kModAdler;
    }
    return (b << 16U) | a;
}

void AppendBigEndianUint32(std::vector<std::uint8_t>& out, const std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void AppendPngChunk(std::vector<std::uint8_t>& out,
                       const char (&type)[5],
                       const std::vector<std::uint8_t>& payload) {
    AppendBigEndianUint32(out, static_cast<std::uint32_t>(payload.size()));
    const std::size_t typeOffset = out.size();
    out.push_back(static_cast<std::uint8_t>(type[0]));
    out.push_back(static_cast<std::uint8_t>(type[1]));
    out.push_back(static_cast<std::uint8_t>(type[2]));
    out.push_back(static_cast<std::uint8_t>(type[3]));
    out.insert(out.end(), payload.begin(), payload.end());
    const std::uint32_t crc = ComputeCrc32ForPng(out.data() + typeOffset, 4U + payload.size());
    AppendBigEndianUint32(out, crc);
}

[[nodiscard]] bool WriteGlBottomUpRgbaCropToPng(const std::filesystem::path& filePath,
                                                   const std::vector<std::uint8_t>& glBottomUpRgba,
                                                   const std::uint32_t fullWidth,
                                                   const std::uint32_t fullHeight,
                                                   const std::uint32_t cropX,
                                                   const std::uint32_t cropYTopDown,
                                                   const std::uint32_t cropWidth,
                                                   const std::uint32_t cropHeight) {
    if (cropWidth == 0U || cropHeight == 0U || cropX + cropWidth > fullWidth ||
        cropYTopDown + cropHeight > fullHeight ||
        glBottomUpRgba.size() < static_cast<std::size_t>(fullWidth) * fullHeight * 4U) {
        return false;
    }

    std::vector<std::uint8_t> rawScanlines;
    const std::size_t rowBytes = static_cast<std::size_t>(cropWidth) * 4U;
    rawScanlines.reserve((rowBytes + 1U) * cropHeight);
    for (std::uint32_t rowTop = 0U; rowTop < cropHeight; ++rowTop) {
        const std::uint32_t srcTopY = cropYTopDown + rowTop;
        const std::uint32_t srcGlY = fullHeight - 1U - srcTopY;
        rawScanlines.push_back(0U); // filter type 0 (None)
        const std::size_t srcOffset =
            (static_cast<std::size_t>(srcGlY) * fullWidth + cropX) * 4U;
        rawScanlines.insert(rawScanlines.end(),
                            glBottomUpRgba.begin() + static_cast<std::ptrdiff_t>(srcOffset),
                            glBottomUpRgba.begin() + static_cast<std::ptrdiff_t>(srcOffset + rowBytes));
    }

    std::vector<std::uint8_t> zlibStream;
    zlibStream.push_back(0x78U);
    zlibStream.push_back(0x01U);
    std::size_t pos = 0U;
    while (pos < rawScanlines.size()) {
        const std::size_t blockLen = std::min<std::size_t>(65535U, rawScanlines.size() - pos);
        const bool isFinal = (pos + blockLen) == rawScanlines.size();
        zlibStream.push_back(isFinal ? 0x01U : 0x00U);
        const auto len16 = static_cast<std::uint16_t>(blockLen);
        const auto nlen16 = static_cast<std::uint16_t>(~len16);
        zlibStream.push_back(static_cast<std::uint8_t>(len16 & 0xFFU));
        zlibStream.push_back(static_cast<std::uint8_t>((len16 >> 8U) & 0xFFU));
        zlibStream.push_back(static_cast<std::uint8_t>(nlen16 & 0xFFU));
        zlibStream.push_back(static_cast<std::uint8_t>((nlen16 >> 8U) & 0xFFU));
        zlibStream.insert(zlibStream.end(),
                          rawScanlines.begin() + static_cast<std::ptrdiff_t>(pos),
                          rawScanlines.begin() + static_cast<std::ptrdiff_t>(pos + blockLen));
        pos += blockLen;
    }
    const std::uint32_t adler = ComputeAdler32ForPng(rawScanlines.data(), rawScanlines.size());
    AppendBigEndianUint32(zlibStream, adler);

    std::vector<std::uint8_t> pngBytes = {0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU};
    std::vector<std::uint8_t> ihdr;
    AppendBigEndianUint32(ihdr, cropWidth);
    AppendBigEndianUint32(ihdr, cropHeight);
    ihdr.push_back(8U); // bit depth
    ihdr.push_back(6U); // color type RGBA
    ihdr.push_back(0U); // compression
    ihdr.push_back(0U); // filter
    ihdr.push_back(0U); // interlace
    AppendPngChunk(pngBytes, "IHDR", ihdr);
    AppendPngChunk(pngBytes, "IDAT", zlibStream);
    AppendPngChunk(pngBytes, "IEND", {});

    std::error_code ec;
    std::filesystem::create_directories(filePath.parent_path(), ec);
    std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(pngBytes.data()),
              static_cast<std::streamsize>(pngBytes.size()));
    return out.good();
}

[[nodiscard]] Asset::ParticleFloatInput MakeConstFloatInput(const float value) noexcept {
    Asset::ParticleFloatInput input;
    input.kind = Asset::ParticleValueKind::Constant;
    input.constant = value;
    return input;
}

[[nodiscard]] Asset::ParticleFloatInput MakeRandomFloatInput(const float minVal,
                                                                   const float maxVal) noexcept {
    Asset::ParticleFloatInput input;
    input.kind = Asset::ParticleValueKind::RandomRange;
    input.min = minVal;
    input.max = maxVal;
    return input;
}

[[nodiscard]] Asset::ParticleVectorInput MakeConstVectorInput(
    const Math::Vector3& value) noexcept {
    Asset::ParticleVectorInput input;
    input.kind = Asset::ParticleValueKind::Constant;
    input.constant = value;
    return input;
}

[[nodiscard]] Asset::ParticleVectorInput MakeRandomVectorInput(
    const Math::Vector3& minVal,
    const Math::Vector3& maxVal) noexcept {
    Asset::ParticleVectorInput input;
    input.kind = Asset::ParticleValueKind::RandomRange;
    input.min = minVal;
    input.max = maxVal;
    return input;
}

[[nodiscard]] Asset::ParticleColorInput MakeConstColorInput(const Math::Color& color,
                                                                  const float alpha = 1.0F) noexcept {
    Asset::ParticleColorInput input;
    input.kind = Asset::ParticleValueKind::Constant;
    input.constant = color;
    input.alpha = alpha;
    return input;
}

[[nodiscard]] Asset::ParticleColorInput MakeGradientColorInput(
    const Asset::ParticleGradient& gradient) {
    Asset::ParticleColorInput input;
    input.kind = Asset::ParticleValueKind::Gradient;
    input.gradient = gradient;
    return input;
}

} // namespace

TEST(EngineCoreTest, VfxRuntime_ParticleEmitterWithSystemAssetPathSpawnsInVfxWorldNotLegacyRuntime) {
    const std::filesystem::path scratchDir = UVE::Tests::MakeTestCaseDirectory("engine_vfx_emitter_sync");
    const std::filesystem::path sparksPath = scratchDir / "Sparks.uvfx";
    const Asset::ParticleSystemAsset sparksAsset =
        VFX::CreateParticleTemplate(VFX::ParticleTemplateKind::Sparks);
    ASSERT_TRUE(Asset::SaveParticleSystemAsset(sparksAsset, sparksPath));

    EngineConfig config = MakeTestConfig();
    config.projectRootDirectory = scratchDir;
    EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    EngineServices& services = engine.GetServices();
    ASSERT_NE(services.TryGetVfxWorld(), nullptr);
    Scene::IEntityManager& entityManager = services.GetEntityManager();
    Scene::ISceneGraph& sceneGraph = services.GetSceneGraph();

    const Scene::Entity camera = entityManager.CreateEntity();
    Scene::TransformComponent camTransform{};
    camTransform.localPosition = Math::Vector3{0.0F, 1.0F, 5.0F};
    sceneGraph.AttachTransform(entityManager, camera, camTransform);
    entityManager.AddComponent<Scene::CameraComponent>(camera);
    engine.SetActiveCamera(camera);

    const Scene::Entity emitterEntity = entityManager.CreateEntity();
    Scene::TransformComponent emitterTransform{};
    emitterTransform.localPosition = Math::Vector3{0.0F, 0.5F, 0.0F};
    sceneGraph.AttachTransform(entityManager, emitterEntity, emitterTransform);
    Scene::ParticleEmitterComponent emitterComp{};
    emitterComp.systemAssetPath = "Sparks.uvfx";
    emitterComp.seed = 42U;
    emitterComp.autoPlay = true;
    emitterComp.emitting = true;
    entityManager.AddComponent<Scene::ParticleEmitterComponent>(emitterEntity, emitterComp);

    for (int frame = 0; frame < 6; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
        engine.TickFrame();
    }

    EXPECT_EQ(services.GetParticleRuntime().GetInstanceCount(), 0U);
    EXPECT_EQ(engine.GetVfxWorld().GetStats().activeInstanceCount, 1U);
    EXPECT_GT(engine.GetVfxWorld().GetStats().totalLiveParticles, 0U);

    entityManager.DestroyEntity(emitterEntity);
    engine.TickFrame();
    EXPECT_EQ(engine.GetVfxWorld().GetStats().activeInstanceCount, 0U);

    engine.Shutdown();
}

TEST(EngineCoreTest, WindowedMode_VfxSpritesRenderAdditiveAlphaSoftAndTemplateLevelWithScreenshots) {
    const std::filesystem::path scratchDir = UVE::Tests::MakeTestCaseDirectory("engine_vfx_windowed_v3a");
    constexpr std::uint32_t kWinW = 320U;
    constexpr std::uint32_t kWinH = 240U;

    EngineConfig config = MakeTestConfig();
    config.headless = false;
    config.windowWidth = kWinW;
    config.windowHeight = kWinH;
    config.renderTargetWidth = kWinW;
    config.renderTargetHeight = kWinH;
    config.vsyncEnabled = false;
    config.windowGlVersionMajor = 4U;
    config.windowGlVersionMinor = 5U;
    config.projectRootDirectory = scratchDir;
    config.backgroundColor = {0.04F, 0.05F, 0.08F, 1.0F};
    config.ambientColor = Math::Vector3{0.35F, 0.38F, 0.45F};
    config.shaderCompilationMode = ShaderCompilationMode::SynchronousOnDemand;

    EngineCore engine(config);
    engine.Init();
    if (!engine.GetServices().GetWindowManager().IsValid()) {
        GTEST_SKIP() << "No display available for windowed EngineCore - skipping (run under "
                        "xvfb-run to exercise this test)";
    }
    ASSERT_TRUE(engine.Load());

    EngineServices& services = engine.GetServices();
    Scene::IEntityManager& entityManager = services.GetEntityManager();
    Scene::ISceneGraph& sceneGraph = services.GetSceneGraph();

    const Scene::Entity camera = entityManager.CreateEntity();
    Scene::TransformComponent camTransform{};
    camTransform.localPosition = Math::Vector3{0.0F, 1.1F, 4.5F};
    sceneGraph.AttachTransform(entityManager, camera, camTransform);
    entityManager.AddComponent<Scene::CameraComponent>(camera);
    engine.SetActiveCamera(camera);

    const Scene::Entity sun = entityManager.CreateEntity();
    Scene::TransformComponent sunTransform{};
    Math::Quaternion sunRot{};
    static_cast<void>(Math::TryMakeEuler(Math::Vector3{-0.8F, -0.4F, 0.0F}, sunRot));
    sunTransform.localRotation = sunRot;
    sceneGraph.AttachTransform(entityManager, sun, sunTransform);
    Scene::LightComponent sunLight{};
    sunLight.type = Scene::LightType::Directional;
    sunLight.color = Math::Color{1.0F, 0.96F, 0.90F};
    sunLight.intensity = 1.6F;
    entityManager.AddComponent<Scene::LightComponent>(sun, sunLight);

    const std::filesystem::path shotsDir = UVE::Tests::RepositoryRoot() / "Docs/Plans/vfx_shots";

    // -------------------------------------------------------------------------
    // 1. Additive sparks (`v3a_additive_sparks.png`):
    //    Verify overlapping additive particles accumulate brightness above a single particle.
    // -------------------------------------------------------------------------
    {
        Asset::ParticleSystemAsset additiveSystem =
            VFX::CreateParticleTemplate(VFX::ParticleTemplateKind::Sparks);
        ASSERT_FALSE(additiveSystem.emitters.empty());
        Asset::ParticleEmitterAsset& em = additiveSystem.emitters[0];
        em.maxParticles = 600U;
        em.emitterUpdate.clear();
        Asset::ParticleModule burstMod;
        burstMod.id = 1U;
        burstMod.type = Asset::ParticleModuleType::SpawnBurst;
        burstMod.floats["Time"] = MakeConstFloatInput(0.0F);
        burstMod.ints["Count"] = 90;
        burstMod.ints["Cycles"] = 1;
        burstMod.floats["Interval"] = MakeConstFloatInput(1.0F);
        em.emitterUpdate.push_back(burstMod);

        Asset::ParticleModule rateMod;
        rateMod.id = 2U;
        rateMod.type = Asset::ParticleModuleType::SpawnRate;
        rateMod.floats["Rate"] = MakeConstFloatInput(160.0F);
        em.emitterUpdate.push_back(rateMod);

        for (Asset::ParticleModule& mod : em.particleSpawn) {
            if (mod.type == Asset::ParticleModuleType::InitialSize) {
                mod.floats["Size"] = MakeConstFloatInput(0.32F);
                mod.vectors["Size"] =
                    MakeConstVectorInput(Math::Vector3{1.0F, 1.0F, 1.0F});
            }
            if (mod.type == Asset::ParticleModuleType::VelocityInCone) {
                mod.floats["Angle"] = MakeConstFloatInput(35.0F);
                mod.floats["Speed"] = MakeRandomFloatInput(1.5F, 3.8F);
            }
        }
        for (Asset::ParticleModule& mod : em.render) {
            if (mod.type == Asset::ParticleModuleType::SpriteRenderer) {
                mod.ints["Facing"] = 0; // CameraPlane so billboards are large and crisp
                mod.ints["Blend"] = 0;  // Additive
                mod.floats["Emissive"] = MakeConstFloatInput(2.0F);
            }
        }

        const std::filesystem::path additivePath = scratchDir / "AdditiveSparks.uvfx";
        ASSERT_TRUE(Asset::SaveParticleSystemAsset(additiveSystem, additivePath));

        const Scene::Entity sparkEmitter = entityManager.CreateEntity();
        Scene::TransformComponent sparkTransform{};
        sparkTransform.localPosition = Math::Vector3{0.0F, 0.85F, 0.0F};
        sceneGraph.AttachTransform(entityManager, sparkEmitter, sparkTransform);
        Scene::ParticleEmitterComponent sparkComp{};
        sparkComp.systemAssetPath = "AdditiveSparks.uvfx";
        sparkComp.seed = 101U;
        sparkComp.autoPlay = true;
        sparkComp.emitting = true;
        entityManager.AddComponent<Scene::ParticleEmitterComponent>(sparkEmitter, sparkComp);

        engine.TickFrame();
        engine.GetVfxWorld().Tick(0.18F);

        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(kWinW) * kWinH * 4U, 0U);
        engine.SetPostRenderCallback([&] {
            glFinish();
            glReadBuffer(GL_BACK);
            glReadPixels(0, 0, static_cast<GLsizei>(kWinW), static_cast<GLsizei>(kWinH),
                         GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        });
        engine.TickFrame();
        engine.SetPostRenderCallback({});

        std::uint32_t maxR = 0U;
        std::uint32_t brightPixelCount = 0U;
        for (std::size_t idx = 0U; idx < rgba.size(); idx += 4U) {
            maxR = std::max<std::uint32_t>(maxR, rgba[idx]);
            if (rgba[idx] > 80U || rgba[idx + 1U] > 60U) {
                ++brightPixelCount;
            }
        }
        EXPECT_GT(maxR, 180U);
        EXPECT_GT(brightPixelCount, 40U);
        EXPECT_TRUE(WriteGlBottomUpRgbaCropToPng(
            shotsDir / "v3a_additive_sparks.png", rgba, kWinW, kWinH, 40U, 20U, 240U, 200U));

        entityManager.DestroyEntity(sparkEmitter);
        engine.TickFrame();
    }

    // -------------------------------------------------------------------------
    // 2. Alpha-blended sorted smoke (`v3a_alpha_smoke.png`):
    //    Rising alpha-blended colored smoke puffs sorted back-to-front by camera depth.
    // -------------------------------------------------------------------------
    {
        Asset::ParticleSystemAsset layeredSmoke =
            VFX::CreateParticleTemplate(VFX::ParticleTemplateKind::Smoke);
        ASSERT_FALSE(layeredSmoke.emitters.empty());
        Asset::ParticleEmitterAsset& smokeEm = layeredSmoke.emitters[0];
        smokeEm.maxParticles = 400U;
        smokeEm.emitterUpdate.clear();
        {
            Asset::ParticleModule burst;
            burst.id = 1U;
            burst.type = Asset::ParticleModuleType::SpawnBurst;
            burst.floats["Time"] = MakeConstFloatInput(0.0F);
            burst.ints["Count"] = 45;
            burst.ints["Cycles"] = 1;
            smokeEm.emitterUpdate.push_back(burst);

            Asset::ParticleModule rate;
            rate.id = 2U;
            rate.type = Asset::ParticleModuleType::SpawnRate;
            rate.floats["Rate"] = MakeConstFloatInput(60.0F);
            smokeEm.emitterUpdate.push_back(rate);
        }
        for (Asset::ParticleModule& mod : smokeEm.particleSpawn) {
            if (mod.type == Asset::ParticleModuleType::InitialSize) {
                mod.vectors["Size"] =
                    MakeRandomVectorInput(Math::Vector3{0.65F, 0.65F, 0.65F},
                                             Math::Vector3{1.10F, 1.10F, 1.10F});
            }
            if (mod.type == Asset::ParticleModuleType::InitialColor) {
                mod.colors["Color"] =
                    MakeConstColorInput(Math::Color{0.75F, 0.82F, 0.92F}, 0.75F);
            }
        }
        for (Asset::ParticleModule& mod : smokeEm.particleUpdate) {
            if (mod.type == Asset::ParticleModuleType::ColorOverLife) {
                Asset::ParticleGradient grad;
                grad.stops = {
                    Asset::ParticleGradientStop{0.0F, Math::Color{0.95F, 0.55F, 0.20F}, 0.80F},
                    Asset::ParticleGradientStop{0.5F, Math::Color{0.55F, 0.68F, 0.88F}, 0.65F},
                    Asset::ParticleGradientStop{1.0F, Math::Color{0.25F, 0.32F, 0.45F}, 0.0F},
                };
                mod.colors["Color"] = MakeGradientColorInput(grad);
            }
        }
        for (Asset::ParticleModule& mod : smokeEm.render) {
            if (mod.type == Asset::ParticleModuleType::SpriteRenderer) {
                mod.ints["Blend"] = 1; // AlphaBlend
                mod.floats["Emissive"] = MakeConstFloatInput(1.2F);
            }
        }

        const std::filesystem::path smokePath = scratchDir / "AlphaSmoke.uvfx";
        ASSERT_TRUE(Asset::SaveParticleSystemAsset(layeredSmoke, smokePath));

        const Scene::Entity smokeEntity = entityManager.CreateEntity();
        Scene::TransformComponent smokeTransform{};
        smokeTransform.localPosition = Math::Vector3{0.0F, 0.4F, 0.0F};
        sceneGraph.AttachTransform(entityManager, smokeEntity, smokeTransform);
        Scene::ParticleEmitterComponent smokeComp{};
        smokeComp.systemAssetPath = "AlphaSmoke.uvfx";
        smokeComp.seed = 202U;
        smokeComp.autoPlay = true;
        smokeComp.emitting = true;
        entityManager.AddComponent<Scene::ParticleEmitterComponent>(smokeEntity, smokeComp);

        engine.TickFrame();
        engine.GetVfxWorld().Tick(0.35F);

        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(kWinW) * kWinH * 4U, 0U);
        engine.SetPostRenderCallback([&] {
            glFinish();
            glReadBuffer(GL_BACK);
            glReadPixels(0, 0, static_cast<GLsizei>(kWinW), static_cast<GLsizei>(kWinH),
                         GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        });
        engine.TickFrame();
        engine.SetPostRenderCallback({});

        std::uint32_t smokeChangedPixels = 0U;
        for (std::size_t idx = 0U; idx < rgba.size(); idx += 4U) {
            if (rgba[idx] > 40U || rgba[idx + 1U] > 40U || rgba[idx + 2U] > 50U) {
                ++smokeChangedPixels;
            }
        }
        EXPECT_GT(smokeChangedPixels, 200U);
        EXPECT_TRUE(WriteGlBottomUpRgbaCropToPng(
            shotsDir / "v3a_alpha_smoke.png", rgba, kWinW, kWinH, 40U, 15U, 240U, 210U));

        entityManager.DestroyEntity(smokeEntity);
        engine.TickFrame();
    }

    // -------------------------------------------------------------------------
    // 3. Soft particles meeting a floor plane (`v3a_soft_particles.png`):
    //    Place a visible floor plane and soft particles intersecting the floor with SoftDistance > 0.
    // -------------------------------------------------------------------------
    {
        const Scene::Entity floorEntity = entityManager.CreateEntity();
        Scene::TransformComponent floorTransform{};
        floorTransform.localPosition = Math::Vector3{0.0F, 0.45F, 0.0F};
        floorTransform.localScale = Math::Vector3{5.0F, 0.25F, 5.0F};
        sceneGraph.AttachTransform(entityManager, floorEntity, floorTransform);
        entityManager.AddComponent<Scene::PrimitiveMeshComponent>(
            floorEntity,
            Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::Cube,
                                             Math::Vector3{0.22F, 0.28F, 0.36F}});

        Asset::ParticleSystemAsset softSystem =
            VFX::CreateParticleTemplate(VFX::ParticleTemplateKind::Smoke);
        ASSERT_FALSE(softSystem.emitters.empty());
        Asset::ParticleEmitterAsset& softEm = softSystem.emitters[0];
        softEm.emitterUpdate.clear();
        {
            Asset::ParticleModule burst;
            burst.id = 1U;
            burst.type = Asset::ParticleModuleType::SpawnBurst;
            burst.floats["Time"] = MakeConstFloatInput(0.0F);
            burst.ints["Count"] = 35;
            burst.ints["Cycles"] = 1;
            softEm.emitterUpdate.push_back(burst);
        }
        for (Asset::ParticleModule& mod : softEm.particleSpawn) {
            if (mod.type == Asset::ParticleModuleType::ShapeSphere) {
                mod.floats["Radius"] = MakeConstFloatInput(0.85F);
            }
            if (mod.type == Asset::ParticleModuleType::InitialSize) {
                mod.vectors["Size"] =
                    MakeConstVectorInput(Math::Vector3{1.15F, 1.15F, 1.15F});
            }
            if (mod.type == Asset::ParticleModuleType::InitialColor) {
                mod.colors["Color"] =
                    MakeConstColorInput(Math::Color{0.35F, 0.85F, 0.95F}, 0.80F);
            }
        }
        for (Asset::ParticleModule& mod : softEm.particleUpdate) {
            if (mod.type == Asset::ParticleModuleType::ColorOverLife) {
                Asset::ParticleGradient grad;
                grad.stops = {
                    Asset::ParticleGradientStop{0.0F, Math::Color{0.35F, 0.90F, 1.0F}, 0.85F},
                    Asset::ParticleGradientStop{1.0F, Math::Color{0.20F, 0.65F, 0.95F}, 0.55F},
                };
                mod.colors["Color"] = MakeGradientColorInput(grad);
            }
        }
        for (Asset::ParticleModule& mod : softEm.render) {
            if (mod.type == Asset::ParticleModuleType::SpriteRenderer) {
                mod.ints["Blend"] = 1; // AlphaBlend
                mod.floats["SoftDistance"] = MakeConstFloatInput(1.25F);
                mod.floats["Emissive"] = MakeConstFloatInput(1.3F);
            }
        }

        const std::filesystem::path softPath = scratchDir / "SoftParticles.uvfx";
        ASSERT_TRUE(Asset::SaveParticleSystemAsset(softSystem, softPath));

        const Scene::Entity softEmitter = entityManager.CreateEntity();
        Scene::TransformComponent softTransform{};
        softTransform.localPosition = Math::Vector3{0.0F, 0.65F, 0.3F};
        sceneGraph.AttachTransform(entityManager, softEmitter, softTransform);
        Scene::ParticleEmitterComponent softComp{};
        softComp.systemAssetPath = "SoftParticles.uvfx";
        softComp.seed = 303U;
        softComp.autoPlay = true;
        softComp.emitting = true;
        entityManager.AddComponent<Scene::ParticleEmitterComponent>(softEmitter, softComp);

        engine.TickFrame();
        engine.GetVfxWorld().Tick(0.15F);

        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(kWinW) * kWinH * 4U, 0U);
        engine.SetPostRenderCallback([&] {
            glFinish();
            glReadBuffer(GL_BACK);
            glReadPixels(0, 0, static_cast<GLsizei>(kWinW), static_cast<GLsizei>(kWinH),
                         GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        });
        engine.TickFrame();
        engine.SetPostRenderCallback({});

        const Render::Renderer3DFrameDiagnostics diag =
            services.GetRenderer3D().GetLastFrameDiagnostics();
        EXPECT_TRUE(diag.vfxPassRecorded);
        EXPECT_GT(diag.vfxInstancesDrawn, 0U);
        EXPECT_TRUE(WriteGlBottomUpRgbaCropToPng(
            shotsDir / "v3a_soft_particles.png", rgba, kWinW, kWinH, 30U, 20U, 260U, 200U));

        entityManager.DestroyEntity(softEmitter);
        entityManager.DestroyEntity(floorEntity);
        engine.TickFrame();
    }

    // -------------------------------------------------------------------------
    // 4. Level with Fountain and Sparks templates (`v3a_fountain_sparks_level.png`):
    //    Write Fountain.uvfx and Sparks.uvfx to disk, place two ParticleEmitter3D entities
    //    in front of the camera above a floor plane, simulate, and capture screenshot.
    // -------------------------------------------------------------------------
    {
        Asset::ParticleSystemAsset fountainAsset =
            VFX::CreateParticleTemplate(VFX::ParticleTemplateKind::Fountain);
        for (Asset::ParticleEmitterAsset& em : fountainAsset.emitters) {
            for (Asset::ParticleModule& mod : em.particleSpawn) {
                if (mod.type == Asset::ParticleModuleType::InitialSize) {
                    mod.floats["Size"] = MakeConstFloatInput(0.24F);
                    mod.vectors["Size"] =
                        MakeConstVectorInput(Math::Vector3{1.0F, 1.0F, 1.0F});
                }
            }
        }
        Asset::ParticleSystemAsset sparksAsset =
            VFX::CreateParticleTemplate(VFX::ParticleTemplateKind::Sparks);
        for (Asset::ParticleEmitterAsset& em : sparksAsset.emitters) {
            for (Asset::ParticleModule& mod : em.particleSpawn) {
                if (mod.type == Asset::ParticleModuleType::InitialSize) {
                    mod.floats["Size"] = MakeConstFloatInput(0.22F);
                    mod.vectors["Size"] =
                        MakeConstVectorInput(Math::Vector3{1.0F, 1.0F, 1.0F});
                }
            }
            for (Asset::ParticleModule& mod : em.render) {
                if (mod.type == Asset::ParticleModuleType::SpriteRenderer) {
                    mod.ints["Facing"] = 0;
                    mod.floats["Emissive"] = MakeConstFloatInput(2.2F);
                }
            }
        }

        ASSERT_TRUE(Asset::SaveParticleSystemAsset(fountainAsset, scratchDir / "Fountain.uvfx"));
        ASSERT_TRUE(Asset::SaveParticleSystemAsset(sparksAsset, scratchDir / "Sparks.uvfx"));

        const Scene::Entity floorEntity = entityManager.CreateEntity();
        Scene::TransformComponent floorTransform{};
        floorTransform.localPosition = Math::Vector3{0.0F, -0.35F, 0.0F};
        floorTransform.localScale = Math::Vector3{6.0F, 0.2F, 4.0F};
        sceneGraph.AttachTransform(entityManager, floorEntity, floorTransform);
        entityManager.AddComponent<Scene::PrimitiveMeshComponent>(
            floorEntity,
            Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::Cube,
                                             Math::Vector3{0.18F, 0.22F, 0.28F}});

        const Scene::Entity fountainEntity = entityManager.CreateEntity();
        Scene::TransformComponent fountainTransform{};
        fountainTransform.localPosition = Math::Vector3{-1.05F, 0.0F, 0.0F};
        sceneGraph.AttachTransform(entityManager, fountainEntity, fountainTransform);
        Scene::ParticleEmitterComponent fountainComp{};
        fountainComp.systemAssetPath = "Fountain.uvfx";
        fountainComp.seed = 11U;
        fountainComp.autoPlay = true;
        fountainComp.emitting = true;
        entityManager.AddComponent<Scene::ParticleEmitterComponent>(fountainEntity, fountainComp);

        const Scene::Entity sparksEntity = entityManager.CreateEntity();
        Scene::TransformComponent sparksTransform{};
        sparksTransform.localPosition = Math::Vector3{1.05F, 0.25F, 0.0F};
        sceneGraph.AttachTransform(entityManager, sparksEntity, sparksTransform);
        Scene::ParticleEmitterComponent sparksComp{};
        sparksComp.systemAssetPath = "Sparks.uvfx";
        sparksComp.seed = 22U;
        sparksComp.autoPlay = true;
        sparksComp.emitting = true;
        entityManager.AddComponent<Scene::ParticleEmitterComponent>(sparksEntity, sparksComp);

        engine.TickFrame();
        for (int step = 0; step < 8; ++step) {
            engine.GetVfxWorld().Tick(0.05F);
        }

        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(kWinW) * kWinH * 4U, 0U);
        engine.SetPostRenderCallback([&] {
            glFinish();
            glReadBuffer(GL_BACK);
            glReadPixels(0, 0, static_cast<GLsizei>(kWinW), static_cast<GLsizei>(kWinH),
                         GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        });
        engine.TickFrame();
        engine.SetPostRenderCallback({});

        const Render::Renderer3DFrameDiagnostics diag =
            services.GetRenderer3D().GetLastFrameDiagnostics();
        EXPECT_TRUE(diag.vfxPassRecorded);
        EXPECT_GE(diag.vfxBatchesExtracted, 2U);
        EXPECT_GT(diag.vfxInstancesDrawn, 20U);

        // Verify both left half (Fountain, blue-cyan) and right half (Sparks, warm orange) rendered particles
        std::uint32_t leftBluePixels = 0U;
        std::uint32_t rightWarmPixels = 0U;
        for (std::uint32_t y = 20U; y < kWinH - 20U; ++y) {
            for (std::uint32_t x = 10U; x < kWinW / 2U; ++x) {
                const std::size_t idx = (static_cast<std::size_t>(y) * kWinW + x) * 4U;
                if (rgba[idx + 2U] > 90U && rgba[idx + 2U] > rgba[idx]) {
                    ++leftBluePixels;
                }
            }
            for (std::uint32_t x = kWinW / 2U; x < kWinW - 10U; ++x) {
                const std::size_t idx = (static_cast<std::size_t>(y) * kWinW + x) * 4U;
                if (rgba[idx] > 90U && rgba[idx] > rgba[idx + 2U]) {
                    ++rightWarmPixels;
                }
            }
        }
        EXPECT_GT(leftBluePixels, 15U);
        EXPECT_GT(rightWarmPixels, 15U);
        EXPECT_TRUE(WriteGlBottomUpRgbaCropToPng(
            shotsDir / "v3a_fountain_sparks_level.png", rgba, kWinW, kWinH, 16U, 16U, 288U, 208U));
    }

    engine.Shutdown();
}

} // namespace
} // namespace UVE::Core::Tests
