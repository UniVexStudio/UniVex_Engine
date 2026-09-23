// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/core/engine_core_uve.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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
#include <vector>

#include <GL/gl.h>
#include <gtest/gtest.h>

#include "uve/component/thread_group_component_uve.h"

#include "uve/localization/localization_uve.h"

#include "uve/component/auto_translate_component_uve.h"

#include "uve/component/process_component_uve.h"

#include "Support/test_scratch_uve.h"

#include "uve/asset/asset_handle_uve.h"
#include "uve/asset/asset_importer_uve.h"
#include "uve/asset/asset_manager_uve.h"
#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/asset/audio_asset_uve.h"
#include "uve/asset/blob_asset_uve.h"
#include "uve/asset/data_table_importer_uve.h"
#include "uve/asset/data_table_uve.h"
#include "uve/audio/i_audio_system_uve.h"
#include "uve/logging/log_sink_uve.h"
#include "uve/logging/logger_uve.h"
#include "uve/input/i_input_system_uve.h"
#include "uve/input/mouse_button_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/physics/area_overlap_events_uve.h"
#include "uve/physics/i_collision_system_uve.h"
#include "uve/physics/i_physics_system_uve.h"
#include "uve/physics/physics_constraint_system_uve.h"
#include "uve/physics/i_raycast_system_uve.h"
#include "uve/platform/platform_uve.h"
#include "uve/render_systems/i_camera_system_uve.h"
#include "uve/render_systems/i_light_system_uve.h"
#include "uve/rhi/i_render_device_uve.h"
#include "uve/render_systems/i_render_system_uve.h"
#include "uve/save/i_checkpoint_manager_uve.h"
#include "uve/save/i_save_game_system_uve.h"
#include "uve/component/area_component_uve.h"
#include "uve/component/audio_source_component_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/scene/scene_graph_uve.h"
#include "uve/scene/scene_serializer_uve.h"
#include "uve/nodes/3d/hitbox_3d_uve.h"
#include "uve/nodes/3d/hurtbox_3d_uve.h"
#include "uve/nodes/3d/interaction_area_3d_uve.h"
#include "uve/nodes/3d/level_streamer_3d_uve.h"
#include "uve/nodes/3d/reflection_probe_3d_uve.h"
#include "uve/nodes/3d/visibility_region_3d_uve.h"
#include "uve/nodes/3d/world_partition_3d_uve.h"
#include "uve/nodes/3d/projectile_3d_uve.h"
#include "uve/nodes/3d/ray_cast_3d_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/particle_emitter_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/rigid_body_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/scripting/script_graph_persistence_uve.h"
#include "uve/rhi_null/null_render_device_uve.h"
#include "uve/scripting/script_graph_uve.h"
#include "uve/window/i_window_manager_uve.h"

namespace UVE::Core::Tests {
namespace {

EngineConfigUVE MakeTestConfigUVE() {
    EngineConfigUVE config{};
    config.enableConsoleLogging = false;
    config.logFilePath = "uve_engine_core_tests.log";
    config.threadPoolWorkerCount = 2; // keep the whole suite's thread churn small and fast
    config.settingsFilePath = "uve_engine_core_tests.uvesettings"; // never touch a real settings file
    config.assetDatabaseFilePath = "uve_engine_core_tests.uveassetdb"; // never touch a real asset db
    config.headlessUVE = true; // NullWindowManagerUVE/NullRenderDeviceUVE - no display required;
                                // every pre-Increment-20 test opts into this by default, matching
                                // its exact prior (headless-only) behavior. Tests that specifically
                                // exercise the real window/GL backend override this explicitly.
    return config;
}

TEST(EngineCoreUVETest, InitialState_IsUninitialized) {
    const EngineCoreUVE engine(MakeTestConfigUVE());
    EXPECT_EQ(engine.GetStateUVE(), EngineStateUVE::Uninitialized);
}

TEST(EngineCoreUVETest, BuildProfileDefaultsMatchCompiledPolicy) {
    const EngineConfigUVE config{};
#if defined(UVE_PROFILE_DEFAULT_LOG_LEVEL)
    EXPECT_EQ(config.minLogLevel,
              static_cast<Debug::LogLevelUVE>(UVE_PROFILE_DEFAULT_LOG_LEVEL));
#else
    EXPECT_EQ(config.minLogLevel, Debug::LogLevelUVE::Trace);
#endif
#if defined(UVE_PROFILE_ASSERTIONS_ENABLED)
    EXPECT_EQ(UVE_DEBUG, UVE_PROFILE_ASSERTIONS_ENABLED);
#else
    EXPECT_EQ(UVE_DEBUG, 1);
#endif
}

TEST(EngineCoreUVETest, ParticleEmitterComponents_ReconcileWithRuntimeAcrossFrames) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    ASSERT_NE(entity, Scene::kInvalidEntityUVE);
    entityManager.AddComponentUVE<Scene::ParticleEmitterComponentUVE>(entity,
                                                                       Scene::ParticleEmitterComponentUVE{32U});

    engine.TickFrameUVE();
    Scene::ParticleRuntimeSnapshotUVE snapshot = engine.GetParticleRuntimeSnapshotUVE();
    ASSERT_EQ(snapshot.instanceCount, 1U);
    ASSERT_EQ(snapshot.instances.size(), 1U);
    EXPECT_EQ(snapshot.instances.front().entity, entity);
    EXPECT_EQ(snapshot.instances.front().maxParticles, 32U);

    entityManager.GetComponentUVE<Scene::ParticleEmitterComponentUVE>(entity).maxParticles = 64U;
    engine.TickFrameUVE();
    snapshot = engine.GetParticleRuntimeSnapshotUVE();
    ASSERT_EQ(snapshot.instances.size(), 1U);
    EXPECT_EQ(snapshot.instances.front().maxParticles, 64U);

    entityManager.RemoveComponentUVE<Scene::ParticleEmitterComponentUVE>(entity);
    engine.TickFrameUVE();
    EXPECT_EQ(engine.GetParticleRuntimeSnapshotUVE().instanceCount, 0U);
}

TEST(EngineCoreUVETest, ProcessMode_GatesParticleEmittersAgainstThePausedState) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Core::EngineServicesUVE& services = engine.GetServicesUVE();
    Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = services.GetSceneGraphUVE();

    const auto makeEmitter = [&](const std::optional<Scene::ProcessModeUVE> mode) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, entity, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::ParticleEmitterComponentUVE>(entity,
                                                                           Scene::ParticleEmitterComponentUVE{8U});
        if (mode.has_value()) {
            Scene::ProcessComponentUVE process{};
            process.mode = *mode;
            entityManager.AddComponentUVE<Scene::ProcessComponentUVE>(entity, process);
        }
        return entity;
    };
    const Scene::EntityUVE pausable = makeEmitter(std::nullopt); // No component: the default.
    const Scene::EntityUVE always = makeEmitter(Scene::ProcessModeUVE::Always);
    const Scene::EntityUVE whenPaused = makeEmitter(Scene::ProcessModeUVE::WhenPaused);
    const Scene::EntityUVE disabled = makeEmitter(Scene::ProcessModeUVE::Disabled);

    const auto isEnabled = [&engine](const Scene::EntityUVE entity) {
        for (const Scene::ParticleRuntimeInstanceSnapshotUVE& instance :
             engine.GetParticleRuntimeSnapshotUVE().instances) {
            if (instance.entity == entity) {
                return instance.enabled;
            }
        }
        ADD_FAILURE() << "emitter has no runtime instance";
        return false;
    };

    engine.TickFrameUVE();
    EXPECT_TRUE(isEnabled(pausable));
    EXPECT_TRUE(isEnabled(always));
    EXPECT_FALSE(isEnabled(whenPaused));
    EXPECT_FALSE(isEnabled(disabled));

    // Pausing flips the two pause-sensitive modes and leaves the other two where they were.
    ASSERT_TRUE(engine.SetSimulationExecutionModeUVE(SimulationExecutionModeUVE::Paused));
    engine.TickFrameUVE();
    EXPECT_FALSE(isEnabled(pausable));
    EXPECT_TRUE(isEnabled(always));
    EXPECT_TRUE(isEnabled(whenPaused));
    EXPECT_FALSE(isEnabled(disabled));

    engine.Shutdown();
}

TEST(EngineCoreUVETest, ProcessMode_DisabledInheritsToAChildWithoutTheComponent) {
    // The inheritance the scene-graph query exists for: the child carries no ProcessComponentUVE,
    // and must still stop because its parent was disabled.
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Core::EngineServicesUVE& services = engine.GetServicesUVE();
    Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = services.GetSceneGraphUVE();

    const Scene::EntityUVE parent = entityManager.CreateEntityUVE();
    const Scene::EntityUVE child = entityManager.CreateEntityUVE();
    for (const Scene::EntityUVE entity : {parent, child}) {
        sceneGraph.AttachTransformUVE(entityManager, entity, Scene::TransformComponentUVE{});
    }
    sceneGraph.SetParentUVE(entityManager, child, parent);
    Scene::ProcessComponentUVE disabled{};
    disabled.mode = Scene::ProcessModeUVE::Disabled;
    entityManager.AddComponentUVE<Scene::ProcessComponentUVE>(parent, disabled);
    entityManager.AddComponentUVE<Scene::ParticleEmitterComponentUVE>(child, Scene::ParticleEmitterComponentUVE{8U});

    engine.TickFrameUVE();
    const Scene::ParticleRuntimeSnapshotUVE snapshot = engine.GetParticleRuntimeSnapshotUVE();
    ASSERT_EQ(snapshot.instances.size(), 1U);
    EXPECT_FALSE(snapshot.instances.front().enabled);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, ProcessMode_ADisabledProjectileDoesNotAdvanceOnAFixedStep) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Core::EngineServicesUVE& services = engine.GetServicesUVE();
    Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = services.GetSceneGraphUVE();

    const auto makeProjectile = [&](const Scene::ProcessModeUVE mode) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, entity, Scene::TransformComponentUVE{});
        Scene::Projectile3DNodeComponentUVE projectile{};
        projectile.velocity = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
        entityManager.AddComponentUVE<Scene::Projectile3DNodeComponentUVE>(entity, projectile);
        Scene::ProcessComponentUVE process{};
        process.mode = mode;
        entityManager.AddComponentUVE<Scene::ProcessComponentUVE>(entity, process);
        return entity;
    };
    const Scene::EntityUVE moving = makeProjectile(Scene::ProcessModeUVE::Pausable);
    const Scene::EntityUVE frozen = makeProjectile(Scene::ProcessModeUVE::Disabled);

    // One frame so the scene graph resolves the modes, then one explicitly requested fixed step:
    // a single step is the simulation advancing, so Pausable moves even though play is paused.
    engine.TickFrameUVE();
    ASSERT_TRUE(engine.SetSimulationExecutionModeUVE(SimulationExecutionModeUVE::Paused));
    const float movingBefore = entityManager.GetComponentUVE<Scene::TransformComponentUVE>(moving).localPosition.x;
    const float frozenBefore = entityManager.GetComponentUVE<Scene::TransformComponentUVE>(frozen).localPosition.x;
    ASSERT_TRUE(engine.RequestSingleSimulationStepUVE());
    engine.TickFrameUVE();

    EXPECT_GT(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(moving).localPosition.x, movingBefore);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(frozen).localPosition.x,
                    frozenBefore);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, AutoTranslate_ALabelInheritsItsMenusOptOutWithoutAComponent) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Core::EngineServicesUVE& services = engine.GetServicesUVE();
    Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = services.GetSceneGraphUVE();

    Localization::StringTableUVE filipino{Localization::LocaleUVE{"fil", ""}};
    ASSERT_TRUE(filipino.SetTranslationUVE("Play", "Maglaro"));
    ASSERT_TRUE(engine.GetLocalizationServiceUVE().AddStringTableUVE(std::move(filipino)));
    engine.GetLocalizationServiceUVE().SetActiveLocaleUVE(Localization::LocaleUVE{"fil", ""});

    // One menu that opts out; one label under it carrying no Auto Translate component of its own.
    const Scene::EntityUVE menu = entityManager.CreateEntityUVE();
    const Scene::EntityUVE label = entityManager.CreateEntityUVE();
    for (const Scene::EntityUVE entity : {menu, label}) {
        sceneGraph.AttachTransformUVE(entityManager, entity, Scene::TransformComponentUVE{});
    }
    sceneGraph.SetParentUVE(entityManager, label, menu);
    Scene::AutoTranslateComponentUVE optOut{};
    optOut.mode = Scene::AutoTranslateModeUVE::Disabled;
    entityManager.AddComponentUVE<Scene::AutoTranslateComponentUVE>(menu, optOut);
    Scene::UITextComponentUVE text{};
    text.text = "Play";
    entityManager.AddComponentUVE<Scene::UITextComponentUVE>(label, text);

    const auto glyphCount = [&engine] {
        const UI::UIDrawBatchUVE& batch = engine.GetUIRuntimeUVE().GetDrawBatchUVE();
        return std::count_if(batch.quads.cbegin(), batch.quads.cend(), [](const UI::UIQuadUVE& quad) {
            return quad.kind == UI::UIDrawItemKindUVE::Glyph;
        });
    };

    engine.TickFrameUVE();
    EXPECT_EQ(glyphCount(), 4) << "the label inherits Disabled, so it draws \"Play\" untranslated";

    // Let the menu translate again, and the same label - still without a component - follows.
    entityManager.GetComponentUVE<Scene::AutoTranslateComponentUVE>(menu).mode = Scene::AutoTranslateModeUVE::Always;
    engine.TickFrameUVE();
    EXPECT_EQ(glyphCount(), 7) << "\"Maglaro\"";

    engine.Shutdown();
}

TEST(EngineCoreUVETest, ThreadGroup_ASubThreadEmitterSimulatesIdenticallyToItsMainThreadTwin) {
    // End to end through real frames: the engine resolves the thread group, marks the emitter
    // worker-eligible, and hands the simulate its thread pool. The twin never leaves the main
    // thread. Same emission, same frames - the particles must come out the same.
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Core::EngineServicesUVE& services = engine.GetServicesUVE();
    Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = services.GetSceneGraphUVE();

    const auto makeEmitter = [&](const Scene::ThreadGroupModeUVE mode) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, entity, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::ParticleEmitterComponentUVE>(entity,
                                                                           Scene::ParticleEmitterComponentUVE{32U});
        Scene::ThreadGroupComponentUVE threadGroup{};
        threadGroup.mode = mode;
        entityManager.AddComponentUVE<Scene::ThreadGroupComponentUVE>(entity, threadGroup);
        return entity;
    };
    const Scene::EntityUVE onWorker = makeEmitter(Scene::ThreadGroupModeUVE::SubThread);
    const Scene::EntityUVE onMain = makeEmitter(Scene::ThreadGroupModeUVE::MainThread);

    engine.TickFrameUVE(); // Resolve modes and attach both runtime instances.
    // The engine routed each emitter by its resolved mode. Without this check the comparison below
    // would pass even if nothing ever left the main thread, because the serial path agrees too.
    for (const Scene::ParticleRuntimeInstanceSnapshotUVE& instance : engine.GetParticleRuntimeSnapshotUVE().instances) {
        EXPECT_EQ(instance.workerEligible, instance.entity == onWorker) << "emitter " << instance.entity.index;
    }
    Scene::IParticleRuntimeUVE& particles = services.GetParticleRuntimeUVE();
    const Scene::ParticleEmissionUVE emission{16U, Math::Vector3UVE{1.0F, 2.0F, 3.0F},
                                              Math::Vector3UVE{0.5F, 4.0F, -1.0F}, 5.0F};
    ASSERT_TRUE(particles.EmitDetailedUVE(onWorker, emission).IsAcceptedUVE());
    ASSERT_TRUE(particles.EmitDetailedUVE(onMain, emission).IsAcceptedUVE());

    for (int frame = 0; frame < 10; ++frame) {
        engine.TickFrameUVE();
    }

    const std::optional<Scene::ParticleStateSnapshotUVE> worker = particles.GetParticleSnapshotUVE(onWorker);
    const std::optional<Scene::ParticleStateSnapshotUVE> main = particles.GetParticleSnapshotUVE(onMain);
    ASSERT_TRUE(worker.has_value());
    ASSERT_TRUE(main.has_value());
    ASSERT_FALSE(worker->particles.empty());
    EXPECT_EQ(worker->particles, main->particles);
    // And they actually moved, so the comparison is not two untouched emissions agreeing.
    EXPECT_NE(worker->particles.front().position, emission.position);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, RunUVE_BoundedFrames_ReachesShutdownWithCorrectFrameCount) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    const int exitCode = engine.RunUVE(10);

    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(engine.GetStateUVE(), EngineStateUVE::Shutdown);
    EXPECT_EQ(engine.GetFrameStatsUVE().frameNumber, 10U);
}

TEST(EngineCoreUVETest, RunUVE_ZeroFrames_StillInitsAndShutsDownCleanly) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    const int exitCode = engine.RunUVE(0);

    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(engine.GetStateUVE(), EngineStateUVE::Shutdown);
    EXPECT_EQ(engine.GetFrameStatsUVE().frameNumber, 0U);
}

TEST(EngineCoreUVETest, RequestQuitUVE_BeforeRun_PreventsAnyFramesFromRunning) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.RequestQuitUVE();

    const int exitCode = engine.RunUVE(50);

    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(engine.GetStateUVE(), EngineStateUVE::Shutdown);
    EXPECT_EQ(engine.GetFrameStatsUVE().frameNumber, 0U);
}

TEST(EngineCoreUVETest, Shutdown_ClearsLoggerActiveInstance) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.RunUVE(1);

    EXPECT_EQ(Debug::LoggerUVE::GetActiveInstanceUVE(), nullptr);
}

TEST(EngineCoreUVETest, Timer_TotalTimeStrictlyIncreasesAcrossFrames) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    engine.TickFrameUVE();
    const double firstTotal = engine.GetFrameStatsUVE().totalTimeSeconds;
    engine.TickFrameUVE();
    const double secondTotal = engine.GetFrameStatsUVE().totalTimeSeconds;

    EXPECT_GT(secondTotal, firstTotal);
    engine.Shutdown();
}

TEST(EngineCoreUVETest, QueuedEvent_DeliveredDuringTickFrame) {
    struct PingEventUVE {
        int value = 0;
    };

    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    int received = -1;
    engine.GetServicesUVE().GetEventSystemUVE().Subscribe<PingEventUVE>(
        [&received](const PingEventUVE& event) { received = event.value; });
    engine.GetServicesUVE().GetEventSystemUVE().QueueEvent(PingEventUVE{99});

    ASSERT_EQ(received, -1);
    engine.TickFrameUVE();
    EXPECT_EQ(received, 99);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, AreaOverlapLifecycle_QueuesEnteredAndExitedEvents) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    auto& services = engine.GetServicesUVE();
    auto& entityManager = services.GetEntityManagerUVE();
    auto& sceneGraph = services.GetSceneGraphUVE();

    const Scene::EntityUVE area = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE areaTransform;
    areaTransform.localPosition = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, area, areaTransform);
    entityManager.AddComponentUVE<Scene::AreaComponentUVE>(
        area, Scene::AreaComponentUVE{Math::Vector3UVE{1.0F, 1.0F, 1.0F}, 1U, 0xFFFFFFFFU});

    const Scene::EntityUVE collider = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE colliderTransform;
    colliderTransform.localPosition = Math::Vector3UVE{0.5F, 0.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, collider, colliderTransform);
    entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(
        collider, Scene::ColliderComponentUVE{Math::Vector3UVE{1.0F, 1.0F, 1.0F}, 1U, 0xFFFFFFFFU});
    sceneGraph.UpdateUVE(entityManager);

    std::vector<Physics::AreaOverlapPairUVE> entered;
    std::vector<Physics::AreaOverlapPairUVE> exited;
    services.GetEventSystemUVE().Subscribe<Physics::AreaOverlapEnteredEventUVE>(
        [&entered](const Physics::AreaOverlapEnteredEventUVE& event) { entered.push_back(event.pair); });
    services.GetEventSystemUVE().Subscribe<Physics::AreaOverlapExitedEventUVE>(
        [&exited](const Physics::AreaOverlapExitedEventUVE& event) { exited.push_back(event.pair); });

    engine.TickFrameUVE();
    EXPECT_TRUE(entered.empty());
    EXPECT_TRUE(exited.empty());

    engine.TickFrameUVE();
    ASSERT_EQ(entered.size(), 1U);
    EXPECT_EQ(entered.front().area, area);
    EXPECT_EQ(entered.front().other, collider);
    EXPECT_TRUE(exited.empty());

    entityManager.DestroyEntityUVE(collider);
    engine.TickFrameUVE();
    EXPECT_TRUE(exited.empty());

    engine.TickFrameUVE();
    ASSERT_EQ(exited.size(), 1U);
    EXPECT_EQ(exited.front().area, area);
    EXPECT_EQ(exited.front().other, collider);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, CollisionLifecycle_UpdatesReportBeforeScriptTickEachFrame) {
    // Contrasts directly against AreaOverlapLifecycle_QueuesEnteredAndExitedEvents above: that
    // event-queued path only surfaces a transition on the TickFrameUVE() call *after* the one
    // where the overlap actually began, since QueueEvent()'d events aren't drained until the next
    // frame's Update(). SyncCollisionLifecycleUVE() has no such queue - it's a poll-based binding
    // updated synchronously before SyncScriptRuntimeUVE() runs the same frame - so this test proves
    // the very same TickFrameUVE() call that makes two colliders overlap already reports the
    // transition, with zero added frame of latency.
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    auto& services = engine.GetServicesUVE();
    auto& entityManager = services.GetEntityManagerUVE();
    auto& sceneGraph = services.GetSceneGraphUVE();

    const Scene::EntityUVE bodyA = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE transformA;
    transformA.localPosition = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, bodyA, transformA);
    entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(bodyA, Scene::ColliderComponentUVE{});

    const Scene::EntityUVE bodyB = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE transformB;
    transformB.localPosition = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, bodyB, transformB);
    entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(bodyB, Scene::ColliderComponentUVE{});

    engine.TickFrameUVE();
    EXPECT_TRUE(engine.GetLastCollisionLifecycleReportUVE().transitions.empty());

    transformB.localPosition = Math::Vector3UVE{0.2F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, bodyB, transformB);
    engine.TickFrameUVE();

    const Physics::CollisionLifecycleReportUVE& report = engine.GetLastCollisionLifecycleReportUVE();
    ASSERT_EQ(report.transitions.size(), 1U);
    EXPECT_EQ(report.transitions.front().kind, Physics::CollisionTransitionKindUVE::Entered);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, FrameStats_PopulatedAfterFrames) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    engine.TickFrameUVE();
    engine.TickFrameUVE();

    const FrameStatsUVE& stats = engine.GetFrameStatsUVE();
    EXPECT_EQ(stats.frameNumber, 2U);
    EXPECT_GE(stats.frameTimeSeconds, 0.0);
    EXPECT_GE(stats.deltaTimeSeconds, 0.0);
    EXPECT_GT(stats.fps, 0.0);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, LoopStages_ExecuteInDocumentedOrder) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    engine.GetServicesUVE().GetLoggerUVE().AddSink(std::move(memorySink));

    engine.TickFrameUVE();

    std::vector<std::string> stageOrder;
    for (const Debug::LogMessageUVE& message : memorySinkPtr->GetMessagesUVE()) {
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

TEST(EngineCoreUVETest, MemoryManager_ReachableAndFunctionalAfterInit) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Memory::IMemoryManagerUVE& memoryManager = engine.GetServicesUVE().GetMemoryManagerUVE();
    Memory::IAllocatorUVE& allocator = memoryManager.GetDefaultAllocatorUVE();

    void* const pointer = allocator.AllocateUVE(32, 8, __FILE__, __LINE__);
    ASSERT_NE(pointer, nullptr);
    EXPECT_TRUE(memoryManager.HasLeaksUVE());
    allocator.DeallocateUVE(pointer);
    EXPECT_FALSE(memoryManager.HasLeaksUVE());

    engine.Shutdown();
}

TEST(EngineCoreUVETest, NormalRun_ReportsNoLeaks) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    engine.TickFrameUVE();
    engine.TickFrameUVE();

    // Nothing allocates through MemoryManagerUVE on Increment 1's behalf yet, so a normal run
    // must report zero leaks — this also implicitly exercises the debug-only shutdown leak
    // assertion on the happy path (zero leaks, assertion never fires).
    EXPECT_FALSE(engine.GetServicesUVE().GetMemoryManagerUVE().HasLeaksUVE());

    engine.Shutdown();
}

TEST(EngineCoreUVETest, ThreadPool_ReachableAndFunctionalAfterInit) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Threading::IThreadPoolUVE& threadPool = engine.GetServicesUVE().GetThreadPoolUVE();
    EXPECT_GE(threadPool.GetWorkerCountUVE(), 1U);

    Threading::JobCounterUVE counter;
    bool jobRan = false;
    threadPool.SubmitUVE([&jobRan] { jobRan = true; }, counter);
    counter.WaitUVE();
    EXPECT_TRUE(jobRan);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, CommandLineAndConfigManager_ReachableAndFunctionalAfterInit) {
    EngineConfigUVE config = MakeTestConfigUVE();
    config.commandLineArgs = {"--server"};
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    EXPECT_TRUE(engine.GetServicesUVE().GetCommandLineUVE().HasFlagUVE("server"));

    Config::IConfigManagerUVE& configManager = engine.GetServicesUVE().GetConfigManagerUVE();
    configManager.SetStringUVE("editor.theme", "dark");
    EXPECT_EQ(configManager.GetStringUVE("editor.theme", ""), "dark");

    engine.Shutdown();
}

TEST(EngineCoreUVETest, EntityManagerAndSceneGraph_ReachableAndFunctionalAfterInit) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE local;
    local.localPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
    sceneGraph.AttachTransformUVE(entityManager, entity, local);

    engine.TickFrameUVE();

    const Scene::WorldTransformComponentUVE& world =
        entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
    EXPECT_FALSE(world.dirty);
    EXPECT_EQ(world.worldPosition, local.localPosition);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, AssetDatabaseSceneSerializerPrefabSystem_ReachableAndRoundTripAfterInit) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();
    Asset::IAssetDatabaseUVE& assetDatabase = engine.GetServicesUVE().GetAssetDatabaseUVE();
    Scene::IPrefabSystemUVE& prefabSystem = engine.GetServicesUVE().GetPrefabSystemUVE();

    const Scene::EntityUVE source = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<Scene::MeshComponentUVE>(
        source, Scene::MeshComponentUVE{Asset::AssetGuidUVE{51}, Asset::AssetGuidUVE{52}});

    const std::filesystem::path prefabPath = "uve_engine_core_tests.uveprefab";
    std::filesystem::remove(prefabPath);
    const Asset::AssetGuidUVE guid = prefabSystem.SavePrefabUVE(entityManager, assetDatabase, source, prefabPath);
    ASSERT_NE(guid, Asset::kInvalidAssetGuidUVE);

    const Scene::EntityUVE instance =
        prefabSystem.InstantiateUVE(entityManager, sceneGraph, assetDatabase, guid, Scene::kInvalidEntityUVE);
    ASSERT_NE(instance, Scene::kInvalidEntityUVE);
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::MeshComponentUVE>(instance).meshGuid, Asset::AssetGuidUVE{51});

    std::filesystem::remove(prefabPath);
    std::filesystem::remove(MakeTestConfigUVE().assetDatabaseFilePath);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, AssetManagerImporterHotReloadBundle_ReachableAndRoundTripAfterInit) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Asset::IAssetDatabaseUVE& assetDatabase = engine.GetServicesUVE().GetAssetDatabaseUVE();
    Asset::IAssetImporterUVE& importer = engine.GetServicesUVE().GetAssetImporterUVE();
    const Asset::AssetImportSourceClassificationUVE rawModelClassification =
        importer.ClassifySourceUVE("engine_core_tests_character.fbx");
    EXPECT_EQ(rawModelClassification.kind, Asset::AssetImportSourceKindUVE::RawModel);
    EXPECT_EQ(rawModelClassification.normalizedExtension, "fbx");
    EXPECT_FALSE(rawModelClassification.importerRegistered);
    EXPECT_TRUE(rawModelClassification.requiresFormatSpecificParser);
    EXPECT_EQ(rawModelClassification.diagnostic, "format-specific parser is not registered");
    Asset::IAssetManagerUVE& assetManager = engine.GetServicesUVE().GetAssetManagerUVE();
    static_cast<void>(engine.GetServicesUVE().GetHotReloadUVE());
    static_cast<void>(engine.GetServicesUVE().GetAssetBundleUVE());

    const std::filesystem::path sourcePath = "uve_engine_core_tests_source.txt";
    const std::filesystem::path destinationPath = "uve_engine_core_tests_dest.txt";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    {
        std::ofstream file(sourcePath);
        file << "engine core asset pipeline round trip";
    }

    const Asset::AssetGuidUVE guid = importer.ImportUVE(sourcePath, destinationPath, assetDatabase);
    ASSERT_NE(guid, Asset::kInvalidAssetGuidUVE);

    assetManager.RegisterLoaderUVE<Asset::BlobAssetUVE>(
        [](const std::filesystem::path& path, Asset::BlobAssetUVE& outValue) {
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
        // tears down the AssetManagerUVE it points into — a handle must never outlive the
        // manager that produced it, exactly like an IEntityManagerUVE& argument never outliving
        // its EntityManagerUVE.
        const Asset::AssetHandleUVE<Asset::BlobAssetUVE> handle =
            assetManager.LoadUVE<Asset::BlobAssetUVE>(guid, assetDatabase);

        // Ticking a couple of frames exercises HotReloadUVE::PollUVE()/AssetManagerUVE::
        // CollectGarbageUVE() running from within EngineCoreUVE::Update() while a load is in
        // flight, proving neither crashes nor prematurely collects the still-referenced asset.
        engine.TickFrameUVE();
        engine.TickFrameUVE();

        bool ready = false;
        for (int poll = 0; poll < 200000 && !ready; ++poll) {
            ready = handle.IsReadyUVE() || handle.HasFailedUVE();
            if (!ready) {
                std::this_thread::yield();
            }
        }
        ASSERT_TRUE(ready);
        ASSERT_TRUE(handle.IsReadyUVE());
        const Asset::BlobAssetUVE* const blob = handle.TryGetUVE();
        ASSERT_NE(blob, nullptr);
        const std::string content(reinterpret_cast<const char*>(blob->data()), blob->size());
        EXPECT_EQ(content, "engine core asset pipeline round trip");
    }

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    engine.Shutdown();
}

TEST(EngineCoreUVETest, TypedUVEEnvelopeImporters_ComposedAndReachableAfterInit) {
    EngineConfigUVE config = MakeTestConfigUVE();
    const std::filesystem::path root = ::UVE::Tests::ScratchRootUVE();
    config.assetDatabaseFilePath = root / "uve_engine_core_typed_envelope_tests.uveassetdb";

    constexpr std::array<std::string_view, 4> kTypedEnvelopeExtensions = {
        ".uvemodel", ".uvetex", ".uveshader", ".uvemat"};
    struct CleanupUVE final {
        std::filesystem::path database;
        std::array<std::filesystem::path, 4> sources;
        std::array<std::filesystem::path, 4> destinations;
        ~CleanupUVE() {
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

    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Asset::IAssetDatabaseUVE& assetDatabase = engine.GetServicesUVE().GetAssetDatabaseUVE();
    Asset::IAssetImporterUVE& importer = engine.GetServicesUVE().GetAssetImporterUVE();
    for (std::size_t index = 0; index < kTypedEnvelopeExtensions.size(); ++index) {
        const Asset::AssetGuidUVE guid =
            importer.ImportUVE(cleanup.sources[index], cleanup.destinations[index], assetDatabase);
        ASSERT_NE(guid, Asset::kInvalidAssetGuidUVE) << kTypedEnvelopeExtensions[index];
        EXPECT_EQ(assetDatabase.ResolveUVE(guid), cleanup.destinations[index]);
    }

    engine.Shutdown();
}

TEST(EngineCoreUVETest, AudioAssetLoader_RegisteredAndReachableThroughBuiltInPipeline) {
    EngineConfigUVE config = MakeTestConfigUVE();
    const std::filesystem::path root = ::UVE::Tests::ScratchRootUVE();
    config.assetDatabaseFilePath = root / "uve_engine_core_audio_asset_tests.uveassetdb";
    const std::filesystem::path sourcePath = root / "uve_engine_core_audio_asset_tests.wav";
    const std::filesystem::path destinationPath = root / "uve_engine_core_audio_asset_tests.uveaudio";
    std::filesystem::remove(config.assetDatabaseFilePath);
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    struct CleanupUVE final {
        std::filesystem::path database;
        std::filesystem::path source;
        std::filesystem::path destination;
        ~CleanupUVE() {
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

    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Asset::IAssetDatabaseUVE& assetDatabase = engine.GetServicesUVE().GetAssetDatabaseUVE();
    Asset::IAssetImporterUVE& importer = engine.GetServicesUVE().GetAssetImporterUVE();
    Asset::IAssetManagerUVE& assetManager = engine.GetServicesUVE().GetAssetManagerUVE();
    const Asset::AssetGuidUVE guid = importer.ImportUVE(sourcePath, destinationPath, assetDatabase);
    ASSERT_NE(guid, Asset::kInvalidAssetGuidUVE);
    {
        const Asset::AssetHandleUVE<Asset::AudioAssetUVE> handle =
            assetManager.LoadUVE<Asset::AudioAssetUVE>(guid, assetDatabase);
        bool terminal = false;
        for (int iteration = 0; iteration < 200000 && !terminal; ++iteration) {
            terminal = handle.IsReadyUVE() || handle.HasFailedUVE();
            if (!terminal) {
                std::this_thread::yield();
            }
        }
        ASSERT_TRUE(terminal);
        ASSERT_TRUE(handle.IsReadyUVE());
        const Asset::AudioAssetUVE* const audio = handle.TryGetUVE();
        ASSERT_NE(audio, nullptr);
        EXPECT_EQ(audio->channels, 1U);
        EXPECT_EQ(audio->sampleRate, 48000U);
        ASSERT_EQ(audio->samples.size(), 2U);
        EXPECT_FLOAT_EQ(audio->samples[0], 0.0F);
        EXPECT_NEAR(audio->samples[1], 0.9999695F, 1.0e-6F);
    }
    engine.Shutdown();
}
TEST(EngineCoreUVETest, AnimationAssetLoader_RegisteredAndReachableThroughBuiltInPipeline) {
    EngineConfigUVE config = MakeTestConfigUVE();
    const std::filesystem::path root = ::UVE::Tests::ScratchRootUVE();
    config.assetDatabaseFilePath = root / "uve_engine_core_animation_asset_tests.uveassetdb";
    const std::filesystem::path sourcePath = root / "uve_engine_core_animation_asset_tests_source.uveanim";
    const std::filesystem::path destinationPath = root / "uve_engine_core_animation_asset_tests_dest.uveanim";
    std::filesystem::remove(config.assetDatabaseFilePath);
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    struct CleanupUVE final {
        std::filesystem::path database;
        std::filesystem::path source;
        std::filesystem::path destination;
        ~CleanupUVE() {
            std::filesystem::remove(database);
            std::filesystem::remove(source);
            std::filesystem::remove(destination);
        }
    } cleanup{config.assetDatabaseFilePath, sourcePath, destinationPath};
    Asset::AnimationClipAssetUVE sourceClip;
    sourceClip.clipId = "walk";
    sourceClip.durationSeconds = 1.0;
    sourceClip.samples = {Asset::AnimationAssetSampleUVE{
        0.0, Asset::AnimationAssetPoseUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::QuaternionUVE{},
                                           Math::Vector3UVE{1.0F, 1.0F, 1.0F}}}};
    sourceClip.events = {Asset::AnimationAssetEventUVE{0.5, "footstep"}};
    ASSERT_TRUE(Asset::SaveAnimationClipAssetUVE(sourceClip, sourcePath));

    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Asset::IAssetDatabaseUVE& assetDatabase = engine.GetServicesUVE().GetAssetDatabaseUVE();
    Asset::IAssetImporterUVE& importer = engine.GetServicesUVE().GetAssetImporterUVE();
    Asset::IAssetManagerUVE& assetManager = engine.GetServicesUVE().GetAssetManagerUVE();
    const Asset::AssetGuidUVE guid = importer.ImportUVE(sourcePath, destinationPath, assetDatabase);
    ASSERT_NE(guid, Asset::kInvalidAssetGuidUVE);
    {
        const Asset::AssetHandleUVE<Asset::AnimationClipAssetUVE> handle =
            assetManager.LoadUVE<Asset::AnimationClipAssetUVE>(guid, assetDatabase);
        bool terminal = false;
        for (int iteration = 0; iteration < 200000 && !terminal; ++iteration) {
            terminal = handle.IsReadyUVE() || handle.HasFailedUVE();
            if (!terminal) {
                std::this_thread::yield();
            }
        }
        ASSERT_TRUE(terminal);
        ASSERT_TRUE(handle.IsReadyUVE());
        const Asset::AnimationClipAssetUVE* const clip = handle.TryGetUVE();
        ASSERT_NE(clip, nullptr);
        EXPECT_EQ(clip->clipId, "walk");
        ASSERT_EQ(clip->samples.size(), 1U);
        ASSERT_EQ(clip->events.size(), 1U);
        EXPECT_EQ(clip->events.front().eventId, "footstep");
    }
    engine.Shutdown();
}
TEST(EngineCoreUVETest, DataTablePipeline_RegisteredAndReachableThroughServicesAfterInit) {
    EngineConfigUVE config = MakeTestConfigUVE();
    const std::filesystem::path root = ::UVE::Tests::ScratchRootUVE();
    config.assetDatabaseFilePath = root / "uve_engine_core_data_table_tests.uveassetdb";
    const std::filesystem::path sourcePath = root / "uve_engine_core_data_table_tests.csv";
    const std::filesystem::path destinationPath = root / "uve_engine_core_data_table_tests.uvetable";
    std::filesystem::remove(config.assetDatabaseFilePath);
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);

    struct CleanupUVE final {
        std::filesystem::path database;
        std::filesystem::path source;
        std::filesystem::path destination;
        ~CleanupUVE() {
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

    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Asset::IAssetDatabaseUVE& assetDatabase = engine.GetServicesUVE().GetAssetDatabaseUVE();
    Asset::IAssetImporterUVE& importer = engine.GetServicesUVE().GetAssetImporterUVE();
    Asset::IAssetManagerUVE& assetManager = engine.GetServicesUVE().GetAssetManagerUVE();

    Asset::DataTableImportSettingsUVE settings;
    settings.tableName = "weapons";
    settings.columns = {Asset::DataTableColumnUVE{"damage", Asset::DataTableColumnTypeUVE::Integer}};
    const Asset::AssetGuidUVE guid = importer.ImportUVE(sourcePath, destinationPath, assetDatabase, settings);
    ASSERT_NE(guid, Asset::kInvalidAssetGuidUVE);

    {
        const Asset::AssetHandleUVE<Asset::DataTableUVE> handle =
            assetManager.LoadUVE<Asset::DataTableUVE>(guid, assetDatabase);
        bool terminal = false;
        for (int iteration = 0; iteration < 200000 && !terminal; ++iteration) {
            terminal = handle.IsReadyUVE() || handle.HasFailedUVE();
            if (!terminal) {
                std::this_thread::yield();
            }
        }
        ASSERT_TRUE(terminal);
        ASSERT_TRUE(handle.IsReadyUVE());
        const Asset::DataTableUVE* const table = handle.TryGetUVE();
        ASSERT_NE(table, nullptr);
        const Asset::DataTableSnapshotUVE snapshot = table->GetSnapshotUVE();
        EXPECT_EQ(snapshot.name, "weapons");
        ASSERT_EQ(snapshot.rows.size(), 1U);
        ASSERT_EQ(snapshot.rows.front().values.size(), 1U);
        EXPECT_EQ(std::get<std::int64_t>(snapshot.rows.front().values.front()), 25);
    }

    engine.Shutdown();
}

TEST(EngineCoreUVETest, FileSystem_ReachableAndReadWriteRoundTripAfterInit) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Asset::IFileSystemUVE& fileSystem = engine.GetServicesUVE().GetFileSystemUVE();

    const std::filesystem::path mountDirectory = "uve_engine_core_tests_vfs_mount";
    std::filesystem::remove_all(mountDirectory);
    std::filesystem::create_directories(mountDirectory);
    fileSystem.MountDirectoryUVE("", mountDirectory, 0);

    const std::string text = "engine core vfs round trip";
    const auto* const textBytes = reinterpret_cast<const std::byte*>(text.data());
    const std::vector<std::byte> data(textBytes, textBytes + text.size());
    ASSERT_TRUE(fileSystem.WriteFileUVE("notes.txt", data));

    const std::optional<std::vector<std::byte>> readBack = fileSystem.ReadFileUVE("notes.txt");
    ASSERT_TRUE(readBack.has_value());
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(readBack->data()), readBack->size()), text);

    std::filesystem::remove_all(mountDirectory);
    engine.Shutdown();
}

TEST(EngineCoreUVETest, ScriptComponentEntity_ReconcilesAndTicksAgainstScriptRuntimeUVE) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Asset::IFileSystemUVE& fileSystem = engine.GetServicesUVE().GetFileSystemUVE();
    const std::filesystem::path mountDirectory = "uve_engine_core_tests_script_vfs_mount";
    std::filesystem::remove_all(mountDirectory);
    std::filesystem::create_directories(mountDirectory);
    fileSystem.MountDirectoryUVE("", mountDirectory, 0);

    // The simplest real graph that exercises SyncScriptRuntimeUVE()'s full production path (real
    // asset load -> real compile -> real ScriptRuntimeUVE attach -> real per-frame tick against the
    // real engine-owned bindings): one standalone data-producer node reading real keyboard state.
    // input.key_down is a pure query node (no execution-flow pins), so it needs no execution entry
    // point to compile - confirmed against CompileScriptGraphToIrUVE's own existing test coverage
    // for standalone input query nodes in Test/Integration/Scripting/script_graph_uve_tests.cpp.
    Scripting::ScriptGraphSchemaUVE schema;
    ASSERT_TRUE(schema.graph.AddNodeUVE({1U, "input.key_down"}));
    std::vector<Scripting::ScriptPersistenceDiagnosticUVE> encodeDiagnostics;
    const std::string encoded = Scripting::EncodeScriptGraphSchemaUVE(schema, encodeDiagnostics);
    ASSERT_TRUE(encodeDiagnostics.empty());
    ASSERT_FALSE(encoded.empty());

    const auto* const encodedBytes = reinterpret_cast<const std::byte*>(encoded.data());
    const std::vector<std::byte> encodedData(encodedBytes, encodedBytes + encoded.size());
    ASSERT_TRUE(fileSystem.WriteFileUVE("test_script.uvescript", encodedData));

    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<Scene::ScriptComponentUVE>(
        entity, Scene::ScriptComponentUVE{"test_script.uvescript"});

    EXPECT_EQ(engine.GetActiveScriptInstanceCountUVE(), 0U);
    engine.TickFrameUVE();
    EXPECT_EQ(engine.GetActiveScriptInstanceCountUVE(), 1U);

    // A second frame must not re-reconcile (ReconcileUVE rejects a duplicate attach) or regress the
    // attached instance count - proves SyncScriptRuntimeUVE()'s HasInstanceUVE() guard works.
    engine.TickFrameUVE();
    EXPECT_EQ(engine.GetActiveScriptInstanceCountUVE(), 1U);

    std::filesystem::remove_all(mountDirectory);
    engine.Shutdown();
}

TEST(EngineCoreUVETest, RenderSystem_ReachableAndFrameLifecycleWorksAfterInit) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Render::IRenderSystemUVE& renderSystem = engine.GetServicesUVE().GetRenderSystemUVE();
    EXPECT_EQ(renderSystem.GetFrameIndexUVE(), 0U);

    renderSystem.BeginFrameUVE();
    static_cast<void>(renderSystem.GetFrameCommandBufferUVE());
    renderSystem.EndFrameUVE();

    EXPECT_EQ(renderSystem.GetFrameIndexUVE(), 1U);

    Render::IRenderDeviceUVE& renderDevice = engine.GetServicesUVE().GetRenderDeviceUVE();
    EXPECT_EQ(renderDevice.GetBackendNameUVE(), "Null");

    engine.Shutdown();
}

TEST(EngineCoreUVETest, CameraSystem_ReachableAndComputesViewProjectionAfterInit) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();
    const Scene::EntityUVE cameraEntity = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE local;
    local.localPosition = Math::Vector3UVE{0.0F, 0.0F, 5.0F};
    sceneGraph.AttachTransformUVE(entityManager, cameraEntity, local);
    sceneGraph.UpdateUVE(entityManager);
    entityManager.AddComponentUVE<Scene::CameraComponentUVE>(cameraEntity);

    Render::ICameraSystemUVE& cameraSystem = engine.GetServicesUVE().GetCameraSystemUVE();
    const Math::Matrix4x4UVE viewProjection =
        cameraSystem.ComputeViewProjectionUVE(entityManager, cameraEntity, 16.0F / 9.0F);
    const Math::FrustumUVE frustum = cameraSystem.ExtractFrustumUVE(viewProjection);

    EXPECT_TRUE(
        frustum.IntersectsUVE(Math::AabbUVE::FromCenterExtentsUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F})));

    engine.Shutdown();
}

TEST(EngineCoreUVETest, LightSystem_ReachableAndReturnsSentinelWithNoLightEntityAfterInit) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Render::ILightSystemUVE& lightSystem = engine.GetServicesUVE().GetLightSystemUVE();
    const Render::LightListUVE lights =
        lightSystem.ExtractActiveLightsUVE(engine.GetServicesUVE().GetEntityManagerUVE());
    for (const Render::LightDataUVE& slot : lights) {
        EXPECT_FLOAT_EQ(slot.intensity, 0.0F);
    }

    engine.Shutdown();
}

TEST(EngineCoreUVETest, Renderer3D_ReachableAfterInit_NoActiveCameraStillNoOps) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    static_cast<void>(engine.GetServicesUVE().GetRenderer3DUVE());
    EXPECT_EQ(engine.GetActiveCameraUVE(), Scene::kInvalidEntityUVE);

    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    engine.GetServicesUVE().GetLoggerUVE().AddSink(std::move(memorySink));

    engine.TickFrameUVE();

    const std::vector<Debug::LogMessageUVE> messages = memorySinkPtr->GetMessagesUVE();
    const bool foundNoOpTrace =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessageUVE& message) {
            return message.message.starts_with("Render (no-op)");
        });
    EXPECT_TRUE(foundNoOpTrace);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, WindowedMode_NoActiveCameraStillClearsDefaultFramebuffer) {
    EngineConfigUVE config = MakeTestConfigUVE();
    config.headlessUVE = false;
    config.windowWidth = 64;
    config.windowHeight = 64;
    config.vsyncEnabledUVE = false;
    config.windowGlVersionMajor = 4;
    config.windowGlVersionMinor = 5;

    EngineCoreUVE engine(config);
    engine.Init();
    if (!engine.GetServicesUVE().GetWindowManagerUVE().IsValidUVE()) {
        GTEST_SKIP() << "No display available for windowed EngineCoreUVE - skipping (run under "
                        "xvfb-run to exercise this test)";
    }
    ASSERT_TRUE(engine.Load());
    EXPECT_EQ(engine.GetActiveCameraUVE(), Scene::kInvalidEntityUVE);

    std::array<unsigned char, 3> emptyScenePixel{};
    GLenum postRenderGlError = GL_NO_ERROR;
    engine.SetPostRenderCallbackUVE([&emptyScenePixel, &postRenderGlError] {
        glFinish();
        glReadBuffer(GL_BACK);
        glReadPixels(32, 32, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, emptyScenePixel.data());
        postRenderGlError = glGetError();
    });

    engine.TickFrameUVE();
    engine.SetPostRenderCallbackUVE({});

    EXPECT_EQ(emptyScenePixel[0], 13U);
    EXPECT_EQ(emptyScenePixel[1], 13U);
    EXPECT_EQ(emptyScenePixel[2], 13U);
    EXPECT_EQ(postRenderGlError, GL_NO_ERROR);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, Renderer3D_ActiveCameraSet_RendersWithoutCrashing) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();
    const Scene::EntityUVE cameraEntity = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE local;
    local.localPosition = Math::Vector3UVE{0.0F, 0.0F, 5.0F};
    sceneGraph.AttachTransformUVE(entityManager, cameraEntity, local);
    sceneGraph.UpdateUVE(entityManager);
    entityManager.AddComponentUVE<Scene::CameraComponentUVE>(cameraEntity);

    engine.SetActiveCameraUVE(cameraEntity);
    EXPECT_EQ(engine.GetActiveCameraUVE(), cameraEntity);

    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    engine.GetServicesUVE().GetLoggerUVE().AddSink(std::move(memorySink));

    engine.TickFrameUVE();

    // With an active camera set, Render() no longer takes the no-op trace path — proves
    // RenderFrameUVE() actually ran instead (an empty scene still begins+ends a render pass).
    const std::vector<Debug::LogMessageUVE> messages = memorySinkPtr->GetMessagesUVE();
    const bool foundNoOpTrace =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessageUVE& message) {
            return message.message.starts_with("Render (no-op)");
        });
    EXPECT_FALSE(foundNoOpTrace);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, PhysicsSystemAndCollisionSystem_ReachableAndFunctionalAfterInit) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Physics::ICollisionSystemUVE& collisionSystem = engine.GetServicesUVE().GetCollisionSystemUVE();
    Physics::IPhysicsSystemUVE& physicsSystem = engine.GetServicesUVE().GetPhysicsSystemUVE();

    EXPECT_TRUE(collisionSystem.DetectCollisionsUVE(entityManager).empty());

    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE local;
    local.localPosition = Math::Vector3UVE{0.0F, 10.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, entity, local);
    entityManager.AddComponentUVE<Scene::RigidBodyComponentUVE>(entity);
    sceneGraph.UpdateUVE(entityManager);

    physicsSystem.StepUVE(entityManager, sceneGraph, 1.0F / 60.0F);

    const Scene::WorldTransformComponentUVE& world =
        entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
    EXPECT_LT(world.worldPosition.y, 10.0F);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, PhysicsConstraints_ComposedAndSolvedThroughNormalFixedStep) {
    EngineConfigUVE config = MakeTestConfigUVE();
    config.gravity = {};
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    auto& services = engine.GetServicesUVE();
    auto& entityManager = services.GetEntityManagerUVE();
    auto& sceneGraph = services.GetSceneGraphUVE();

    const auto makeBody = [&entityManager, &sceneGraph](const Math::Vector3UVE position) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform;
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        sceneGraph.UpdateUVE(entityManager);
        entityManager.AddComponentUVE<Scene::RigidBodyComponentUVE>(entity,
                                                                      Scene::RigidBodyComponentUVE{1.0F, false});
        return entity;
    };

    const Scene::EntityUVE first = makeBody({0.0F, 0.0F, 0.0F});
    const Scene::EntityUVE second = makeBody({10.0F, 0.0F, 0.0F});
    const Physics::PhysicsConstraintMutationResultUVE added =
        services.GetPhysicsConstraintSystemUVE().AddDistanceConstraintUVE(
            Physics::DistanceConstraintUVE{first, second, {}, {}, 4.0F});
    ASSERT_TRUE(added.IsAcceptedUVE());

    ASSERT_TRUE(engine.SetSimulationExecutionModeUVE(SimulationExecutionModeUVE::Paused));
    ASSERT_TRUE(engine.RequestSingleSimulationStepUVE());
    engine.TickFrameUVE();

    const float firstX = entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(first).worldPosition.x;
    const float secondX = entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(second).worldPosition.x;
    EXPECT_NEAR(secondX - firstX, 4.0F, 1.0e-3F);
    EXPECT_NEAR(firstX, 3.0F, 1.0e-3F);
    EXPECT_NEAR(secondX, 7.0F, 1.0e-3F);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, RaycastSystem_ReachableAndFunctionalAfterInit) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();
    Physics::IRaycastSystemUVE& raycastSystem = engine.GetServicesUVE().GetRaycastSystemUVE();

    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE local;
    local.localPosition = Math::Vector3UVE{5.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, entity, local);
    sceneGraph.UpdateUVE(entityManager);
    entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(entity, Scene::ColliderComponentUVE{Math::Vector3UVE{1.0F, 1.0F, 1.0F}});

    Physics::RaycastQueryUVE query;
    query.ray = Math::RayUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{1.0F, 0.0F, 0.0F}};
    query.maxDistance = 100.0F;
    const std::optional<Physics::RaycastHitUVE> hit = raycastSystem.RaycastUVE(entityManager, query);

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, entity);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, InputSystem_ReachableAndFunctionalAfterInit) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Input::IInputSystemUVE& inputSystem = engine.GetServicesUVE().GetInputSystemUVE();

    inputSystem.SetKeyStateUVE(Input::KeyCodeUVE::Space, true);
    engine.TickFrameUVE();

    EXPECT_TRUE(inputSystem.IsKeyDownUVE(Input::KeyCodeUVE::Space));
    EXPECT_TRUE(inputSystem.WasKeyPressedThisFrameUVE(Input::KeyCodeUVE::Space));

    engine.TickFrameUVE();
    EXPECT_TRUE(inputSystem.IsKeyDownUVE(Input::KeyCodeUVE::Space));
    EXPECT_FALSE(inputSystem.WasKeyPressedThisFrameUVE(Input::KeyCodeUVE::Space));

    inputSystem.SetKeyStateUVE(Input::KeyCodeUVE::Space, false);
    engine.TickFrameUVE();
    EXPECT_FALSE(inputSystem.IsKeyDownUVE(Input::KeyCodeUVE::Space));
    EXPECT_TRUE(inputSystem.WasKeyReleasedThisFrameUVE(Input::KeyCodeUVE::Space));

    engine.Shutdown();
}

TEST(EngineCoreUVETest, ExtendedInputServices_ReachableAndUpdatedThroughEngineCore) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Input::IInputSystemUVE& inputSystem = engine.GetServicesUVE().GetInputSystemUVE();
    Input::IGamepadInputSystemUVE& gamepad = engine.GetServicesUVE().GetGamepadInputSystemUVE();
    Input::IMobileInputSystemUVE& mobile = engine.GetServicesUVE().GetMobileInputSystemUVE();
    Input::IMobileGestureSystemUVE& gestures = engine.GetServicesUVE().GetMobileGestureSystemUVE();

    Input::InputActionUVE action;
    action.name = "GamepadJump";
    action.type = Input::InputActionTypeUVE::Button;
    action.positiveBindings.push_back(Input::GamepadButtonBindingUVE(0U, Input::GamepadButtonUVE::South));
    inputSystem.RegisterActionUVE(std::move(action));

    gamepad.SetConnectedUVE(0U, true);
    gamepad.SetButtonStateUVE(0U, Input::GamepadButtonUVE::South, true);
    mobile.SetTouchStateUVE(0U, true, 42U, Math::Vector2UVE{10.0F, 20.0F}, 0.5F);
    engine.TickFrameUVE();

    EXPECT_TRUE(inputSystem.IsActionTriggeredUVE("GamepadJump"));
    EXPECT_EQ(gamepad.GetSnapshotUVE(0U).frameNumber, 1U);
    EXPECT_EQ(mobile.GetSnapshotUVE().frameNumber, 1U);
    EXPECT_EQ(gestures.GetLastReportUVE().count, 0U);

    gamepad.SetButtonStateUVE(0U, Input::GamepadButtonUVE::South, false);
    mobile.SetTouchStateUVE(0U, false, 0U, Math::Vector2UVE{}, 0.0F);
    engine.TickFrameUVE();

    EXPECT_TRUE(inputSystem.IsActionReleasedUVE("GamepadJump"));
    const Input::MobileGestureReportUVE report = gestures.GetLastReportUVE();
    ASSERT_EQ(report.count, 1U);
    EXPECT_EQ(report.events[0].type, Input::MobileGestureTypeUVE::Tap);
    EXPECT_EQ(report.events[0].touchIdentifier, 42U);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, AudioSystem_ReachableAndFunctionalAfterInit) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Audio::IAudioSystemUVE& audioSystem = engine.GetServicesUVE().GetAudioSystemUVE();

    Audio::AudioSourceDescUVE desc;
    desc.spatial = false;
    const Audio::VoiceHandleUVE source = audioSystem.CreateSourceUVE(desc);
    ASSERT_NE(source, Audio::kInvalidVoiceHandleUVE);
    ASSERT_TRUE(audioSystem.PlayUVE(source));

    engine.TickFrameUVE();

    EXPECT_EQ(audioSystem.GetSourceStateUVE(source), Audio::VoicePlaybackStateUVE::Playing);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, AudioStreamAndPcmEffects_ReachableThroughEngineServices) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Audio::IAudioSystemUVE& audioSystem = engine.GetServicesUVE().GetAudioSystemUVE();
    const Audio::VoiceHandleUVE source = audioSystem.CreateSourceUVE(Audio::AudioSourceDescUVE{});
    ASSERT_NE(source, Audio::kInvalidVoiceHandleUVE);

    ASSERT_TRUE(audioSystem.ResetSourceStreamUVE(source, 8U, false));
    ASSERT_TRUE(audioSystem.ScheduleSourceStreamWindowUVE(source, 3U));
    Audio::Pcm16StreamWindowPlanUVE plan;
    ASSERT_TRUE(audioSystem.PopSourceStreamWindowUVE(source, plan));
    EXPECT_EQ(plan, (Audio::Pcm16StreamWindowPlanUVE{0U, 3U, 3U, false, false}));

    ASSERT_TRUE(audioSystem.ScheduleSourcePcmGainWindowUVE(
        source, Audio::PcmGainEffectWindowUVE{0U, 2U, 0.25F}));
    std::vector<float> output;
    ASSERT_TRUE(audioSystem.ApplySourcePcmGainEffectsUVE(source, {0.8F, -0.4F, 0.5F}, output));
    EXPECT_EQ(output, (std::vector<float>{0.2F, -0.1F, 0.5F}));

    engine.Shutdown();
}

TEST(EngineCoreUVETest, AudioListener_TracksActiveCameraWorldPosition) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();
    const Scene::EntityUVE cameraEntity = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE local;
    local.localPosition = Math::Vector3UVE{3.0F, 4.0F, 5.0F};
    sceneGraph.AttachTransformUVE(entityManager, cameraEntity, local);
    sceneGraph.UpdateUVE(entityManager);
    entityManager.AddComponentUVE<Scene::CameraComponentUVE>(cameraEntity);

    engine.SetActiveCameraUVE(cameraEntity);
    engine.TickFrameUVE();

    Audio::IAudioSystemUVE& audioSystem = engine.GetServicesUVE().GetAudioSystemUVE();
    EXPECT_EQ(audioSystem.GetListenerPositionUVE(), (Math::Vector3UVE{3.0F, 4.0F, 5.0F}));

    engine.Shutdown();
}

TEST(EngineCoreUVETest, FallingRigidBody_TickFrameUVEDrivenPhysicsStep_MovesEntityDownward) {
    // A 1kHz fixed-update rate (1ms fixed step) paired with a short real sleep before each
    // TickFrameUVE() call guarantees the ITimerUVE accumulator crosses at least one fixed step
    // almost every frame — an excessively high fixedUpdateFps would trigger steps just as
    // reliably but make each step's position delta too small to be representable in float at
    // this entity's starting magnitude (10.0), silently rounding to no visible movement. This
    // test only proves EngineCoreUVE::Update()'s physics-step wiring moves an entity end-to-end;
    // PhysicsSystemUVE's exact per-step math is already covered by
    // tests/physics/physics_system_uve_tests.cpp.
    EngineConfigUVE config = MakeTestConfigUVE();
    config.fixedUpdateFps = 1000.0;
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE local;
    local.localPosition = Math::Vector3UVE{0.0F, 10.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, entity, local);
    entityManager.AddComponentUVE<Scene::RigidBodyComponentUVE>(entity);

    for (int frame = 0; frame < 30; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        engine.TickFrameUVE();
    }

    const Scene::WorldTransformComponentUVE& world =
        entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
    EXPECT_LT(world.worldPosition.y, 10.0F);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, CharacterController_FallsUnderGravityLandsOnGroundThenJumpsOnSpace) {
    // Same 1kHz fixed-update / short real-sleep discipline as FallingRigidBody_* above, so
    // EngineCoreUVE::Update()'s new SyncCharacterControllersUVE() wiring gets exercised end-to-end
    // (gravity accumulation -> Physics::CharacterControllerUVE::MoveWithToIUVE -> ground contact),
    // not just PhysicsSystemUVE's own already-covered per-step math.
    EngineConfigUVE config = MakeTestConfigUVE();
    config.fixedUpdateFps = 1000.0;
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    // Static ground: a flat box collider with no RigidBodyComponentUVE, top surface at y=0.5.
    const Scene::EntityUVE ground = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, ground, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(
        ground, Scene::ColliderComponentUVE{Math::Vector3UVE{10.0F, 0.5F, 10.0F}});

    // Character controller entity starting 1 unit above its expected resting height (ground top
    // 0.5 + the default box collider's own 0.5 half-extent = 1.0).
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE local;
    local.localPosition = Math::Vector3UVE{0.0F, 2.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, entity, local);
    entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(entity, Scene::ColliderComponentUVE{});
    entityManager.AddComponentUVE<Scene::CharacterControllerComponentUVE>(entity);

    for (int frame = 0; frame < 400; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        engine.TickFrameUVE();
    }

    const Scene::CharacterControllerComponentUVE& afterFall =
        entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(entity);
    EXPECT_TRUE(afterFall.isGrounded);
    const Scene::WorldTransformComponentUVE& worldAfterFall =
        entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
    EXPECT_NEAR(worldAfterFall.worldPosition.y, 1.0F, 0.35F);

    Input::IInputSystemUVE& inputSystem = engine.GetServicesUVE().GetInputSystemUVE();
    inputSystem.SetKeyStateUVE(Input::KeyCodeUVE::Space, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    engine.TickFrameUVE();

    const Scene::CharacterControllerComponentUVE& afterJump =
        entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(entity);
    EXPECT_GT(afterJump.verticalVelocity, 0.0F);
    EXPECT_FALSE(afterJump.isGrounded);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, RayCast3DNode_HitsRealGroundColliderExcludesItselfAndMissesBeyondLength) {
    // EngineCoreUVE::SyncRayCast3DNodesUVE() is new wiring: previously RayCast3DNodeComponentUVE
    // was pure authored data with nothing evaluating it. This proves a real per-frame raycast
    // against a real Physics::RaycastSystemUVE + real colliders, not a mocked query.
    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    // Static ground: a flat box collider centered at the origin, top surface at y=0.5.
    const Scene::EntityUVE ground = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, ground, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(
        ground, Scene::ColliderComponentUVE{Math::Vector3UVE{10.0F, 0.5F, 10.0F}});

    // The raycasting entity sits 5 units above the ground, has its own collider (to prove
    // self-exclusion really works - without it, the closest "hit" would be itself at distance 0),
    // and casts a local-space "-Y" ray far enough to reach the ground.
    const Scene::EntityUVE caster = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE casterTransform;
    casterTransform.localPosition = Math::Vector3UVE{0.0F, 5.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, caster, casterTransform);
    entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(caster, Scene::ColliderComponentUVE{});
    Scene::RayCast3DNodeComponentUVE rayCast;
    rayCast.direction = Math::Vector3UVE{0.0F, -1.0F, 0.0F};
    rayCast.length = 10.0F;
    entityManager.AddComponentUVE<Scene::RayCast3DNodeComponentUVE>(caster, rayCast);

    engine.TickFrameUVE();

    const Scene::RayCast3DNodeComponentUVE& afterHit =
        entityManager.GetComponentUVE<Scene::RayCast3DNodeComponentUVE>(caster);
    EXPECT_TRUE(afterHit.hit);
    EXPECT_EQ(afterHit.hitEntity, ground);
    EXPECT_NEAR(afterHit.hitPosition.y, 0.5F, 0.01F);
    EXPECT_NEAR(afterHit.hitNormal.y, 1.0F, 0.01F);

    // Shortening the ray so it can't reach the ground (top at y=0.5, caster at y=5, so a length of
    // 1.0 falls well short) must report a clean miss, not a stale hit from the previous frame.
    Scene::RayCast3DNodeComponentUVE& live = entityManager.GetComponentUVE<Scene::RayCast3DNodeComponentUVE>(caster);
    live.length = 1.0F;
    engine.TickFrameUVE();
    EXPECT_FALSE(entityManager.GetComponentUVE<Scene::RayCast3DNodeComponentUVE>(caster).hit);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, Hitbox3DNode_StrikesOverlappingHurtboxAndClearsWhenGatedOrApart) {
    // EngineCoreUVE::SyncHitbox3DNodesUVE() is new wiring: previously Hitbox3DNodeComponentUVE
    // and Hurtbox3DNodeComponentUVE were pure authored data with nothing evaluating them. This
    // proves the real per-frame pairing - exact oriented-box overlap with symmetric layer/mask
    // acceptance, damage-channel equality, and self-exclusion - and that every gate clears
    // stale strikes instead of keeping the previous frame's list.
    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    // Attacker at the origin with default combat volumes (half extents 0.5, layer 1, full mask,
    // channel "default"); victim parked 0.5 units away so the boxes overlap 0.5 units along X.
    const Scene::EntityUVE attacker = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, attacker, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::Hitbox3DNodeComponentUVE>(
        attacker, Scene::Hitbox3DNodeComponentUVE{});

    const Scene::EntityUVE victim = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE victimTransform;
    victimTransform.localPosition = Math::Vector3UVE{0.5F, 0.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, victim, victimTransform);
    entityManager.AddComponentUVE<Scene::Hurtbox3DNodeComponentUVE>(
        victim, Scene::Hurtbox3DNodeComponentUVE{});

    engine.TickFrameUVE();
    {
        const Scene::Hitbox3DNodeComponentUVE& afterStrike =
            entityManager.GetComponentUVE<Scene::Hitbox3DNodeComponentUVE>(attacker);
        ASSERT_EQ(afterStrike.strikeCount, 1U);
        EXPECT_EQ(afterStrike.strikes[0U].hurtboxEntity, victim);
        EXPECT_NEAR(afterStrike.strikes[0U].penetrationDepth, 0.5F, 0.01F);
        EXPECT_FALSE(afterStrike.strikesTruncated);
    }

    // Moving the hurtbox far out of range must clear the strike, not keep the stale one.
    Scene::TransformComponentUVE victimLive =
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(victim);
    victimLive.localPosition = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, victim, victimLive);
    engine.TickFrameUVE();
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::Hitbox3DNodeComponentUVE>(attacker).strikeCount, 0U);

    // Back in range but on a different damage channel: no strike.
    victimLive.localPosition = Math::Vector3UVE{0.5F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, victim, victimLive);
    Scene::Hurtbox3DNodeComponentUVE& hurtboxLive =
        entityManager.GetComponentUVE<Scene::Hurtbox3DNodeComponentUVE>(victim);
    hurtboxLive.damageChannel = "environment";
    engine.TickFrameUVE();
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::Hitbox3DNodeComponentUVE>(attacker).strikeCount, 0U);

    // Same channel again but the hurtbox's mask no longer accepts the hitbox's layer: no strike
    // (acceptance is symmetric - both sides must accept each other).
    hurtboxLive.damageChannel = "default";
    hurtboxLive.collisionMask = 0U;
    engine.TickFrameUVE();
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::Hitbox3DNodeComponentUVE>(attacker).strikeCount, 0U);

    // Mask restored but the hitbox itself is disabled: no strike, and none may linger.
    hurtboxLive.collisionMask = 0xFFFFFFFFU;
    Scene::Hitbox3DNodeComponentUVE& hitboxLive =
        entityManager.GetComponentUVE<Scene::Hitbox3DNodeComponentUVE>(attacker);
    hitboxLive.enabled = false;
    engine.TickFrameUVE();
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::Hitbox3DNodeComponentUVE>(attacker).strikeCount, 0U);

    // Re-enabled: the strike returns. A hurtbox on the attacker's OWN entity must not add a
    // second (self) strike - the victim stays the only recorded hit.
    hitboxLive.enabled = true;
    entityManager.AddComponentUVE<Scene::Hurtbox3DNodeComponentUVE>(
        attacker, Scene::Hurtbox3DNodeComponentUVE{});
    engine.TickFrameUVE();
    {
        const Scene::Hitbox3DNodeComponentUVE& afterStrike =
            entityManager.GetComponentUVE<Scene::Hitbox3DNodeComponentUVE>(attacker);
        ASSERT_EQ(afterStrike.strikeCount, 1U);
        EXPECT_EQ(afterStrike.strikes[0U].hurtboxEntity, victim);
    }

    engine.Shutdown();
}

TEST(EngineCoreUVETest, InteractionArea3DNode_TracksInteractorsFocusesTheNearestAndClearsWhenGated) {
    // EngineCoreUVE::SyncInteractionArea3DNodesUVE() is new wiring: previously
    // InteractionArea3DNodeComponentUVE was pure authored data with nothing evaluating it - a
    // game had to hand-roll the whole "what can I interact with" loop. This proves the real
    // per-frame behaviour: the character-controller player populates overlapping areas'
    // interactor lists, exactly one area - the nearest - earns focusedByPrimaryInteractor,
    // the bounded list reports its own overflow, and every gate clears stale state instead of
    // keeping the previous frame's answers.
    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    // The player: a character controller with the default box collider (0.5 half extents) at the
    // origin. Area A is one step away; area B starts out of range.
    const Scene::EntityUVE player = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, player, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(player, Scene::ColliderComponentUVE{});
    entityManager.AddComponentUVE<Scene::CharacterControllerComponentUVE>(player);

    const Scene::EntityUVE areaA = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE transformA;
    transformA.localPosition = Math::Vector3UVE{0.4F, 0.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, areaA, transformA);
    entityManager.AddComponentUVE<Scene::InteractionArea3DNodeComponentUVE>(
        areaA, Scene::InteractionArea3DNodeComponentUVE{});

    const Scene::EntityUVE areaB = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE transformB;
    transformB.localPosition = Math::Vector3UVE{30.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, areaB, transformB);
    entityManager.AddComponentUVE<Scene::InteractionArea3DNodeComponentUVE>(
        areaB, Scene::InteractionArea3DNodeComponentUVE{});

    engine.TickFrameUVE();
    {
        const Scene::InteractionArea3DNodeComponentUVE& live =
            entityManager.GetComponentUVE<Scene::InteractionArea3DNodeComponentUVE>(areaA);
        ASSERT_EQ(live.interactorCount, 1U);
        EXPECT_EQ(live.interactors[0U], player);
        EXPECT_FALSE(live.interactorsTruncated);
        EXPECT_TRUE(live.focusedByPrimaryInteractor);
    }
    {
        const Scene::InteractionArea3DNodeComponentUVE& live =
            entityManager.GetComponentUVE<Scene::InteractionArea3DNodeComponentUVE>(areaB);
        EXPECT_EQ(live.interactorCount, 0U);
        EXPECT_FALSE(live.focusedByPrimaryInteractor);
    }

    // Pull B barely into range (its center lands farther than A's): both list the player, but
    // only the NEAREST one keeps the focus.
    transformB.localPosition = Math::Vector3UVE{0.45F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, areaB, transformB);
    engine.TickFrameUVE();
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::InteractionArea3DNodeComponentUVE>(areaB)
                  .interactorCount,
              1U);
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::InteractionArea3DNodeComponentUVE>(areaA)
                    .focusedByPrimaryInteractor);
    EXPECT_FALSE(entityManager.GetComponentUVE<Scene::InteractionArea3DNodeComponentUVE>(areaB)
                     .focusedByPrimaryInteractor);

    // B ends up the nearer of the two: the focus must MOVE to it, not stick to first-come.
    transformB.localPosition = Math::Vector3UVE{0.1F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, areaB, transformB);
    engine.TickFrameUVE();
    EXPECT_FALSE(entityManager.GetComponentUVE<Scene::InteractionArea3DNodeComponentUVE>(areaA)
                     .focusedByPrimaryInteractor);
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::InteractionArea3DNodeComponentUVE>(areaB)
                    .focusedByPrimaryInteractor);

    // Disabling B hands the focus back to A - nothing stale may survive the gate.
    entityManager.GetComponentUVE<Scene::InteractionArea3DNodeComponentUVE>(areaB).enabled = false;
    engine.TickFrameUVE();
    {
        const Scene::InteractionArea3DNodeComponentUVE& liveB =
            entityManager.GetComponentUVE<Scene::InteractionArea3DNodeComponentUVE>(areaB);
        EXPECT_EQ(liveB.interactorCount, 0U);
        EXPECT_FALSE(liveB.focusedByPrimaryInteractor);
    }
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::InteractionArea3DNodeComponentUVE>(areaA)
                    .focusedByPrimaryInteractor);

    // The player's collider mask stops accepting either side: participation ends, and with no
    // interactor left the focus goes away entirely - fail-closed, not last-known.
    entityManager.GetComponentUVE<Scene::InteractionArea3DNodeComponentUVE>(areaB).enabled = true;
    Scene::ColliderComponentUVE& playerCollider =
        entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(player);
    playerCollider.collisionMask = 0U;
    engine.TickFrameUVE();
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::InteractionArea3DNodeComponentUVE>(areaA)
                  .interactorCount,
              0U);
    EXPECT_FALSE(entityManager.GetComponentUVE<Scene::InteractionArea3DNodeComponentUVE>(areaA)
                     .focusedByPrimaryInteractor);
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::InteractionArea3DNodeComponentUVE>(areaB)
                  .interactorCount,
              0U);

    // Mask restored and B moved back out of range, plus a second controller on the same spot:
    // with an authored candidate budget of one, the bounded list keeps exactly one entry and
    // REPORTS its overflow instead of silently dropping the extra interactor or overwriting past
    // the cap.
    playerCollider.collisionMask = 0xFFFFFFFFU;
    transformB.localPosition = Math::Vector3UVE{30.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, areaB, transformB);
    Scene::InteractionArea3DNodeComponentUVE& liveA =
        entityManager.GetComponentUVE<Scene::InteractionArea3DNodeComponentUVE>(areaA);
    liveA.maximumCandidates = 1U;
    const Scene::EntityUVE secondPlayer = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, secondPlayer, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(
        secondPlayer, Scene::ColliderComponentUVE{});
    entityManager.AddComponentUVE<Scene::CharacterControllerComponentUVE>(secondPlayer);
    engine.TickFrameUVE();
    {
        const Scene::InteractionArea3DNodeComponentUVE& afterFill =
            entityManager.GetComponentUVE<Scene::InteractionArea3DNodeComponentUVE>(areaA);
        EXPECT_EQ(afterFill.interactorCount, 1U);
        EXPECT_TRUE(afterFill.interactorsTruncated);
        // The primary interactor is the first content-ordered controller, and the focus verdict
        // is independent of the bounded list: with B out of range, A is the only area the
        // primary overlaps, so it stays focused regardless of the truncated extra entry.
        EXPECT_TRUE(afterFill.focusedByPrimaryInteractor);
    }

    // A controller WITHOUT a collider has no overlap volume and must never appear.
    const Scene::EntityUVE colliderless = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, colliderless, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::CharacterControllerComponentUVE>(colliderless);
    engine.TickFrameUVE();
    {
        const Scene::InteractionArea3DNodeComponentUVE& afterFill =
            entityManager.GetComponentUVE<Scene::InteractionArea3DNodeComponentUVE>(areaA);
        EXPECT_EQ(afterFill.interactorCount, 1U);
        for (std::size_t index = 0; index < afterFill.interactorCount; ++index) {
            EXPECT_NE(afterFill.interactors[index], colliderless);
        }
    }

    engine.Shutdown();
}

TEST(EngineCoreUVETest, Hitbox3DNode_HurtboxRotationIsHonoredByExactObbOverlap) {
    // The strike test must be an exact ORIENTED-box test, not a conservative axis-aligned one.
    // Both boxes are long thin rods along X; the hurtbox sits 2 units to the side. Unrotated it
    // clearly overlaps (x in [0.5, 3.5] vs the hitbox's [-1.5, 1.5]); rotated 90 degrees about Z
    // its long axis becomes vertical (x only in [1.9, 2.1]) and it must NOT strike. A pairing
    // that ignored rotation would fail this test in the always-overlapping direction.
    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    const Scene::EntityUVE attacker = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, attacker, Scene::TransformComponentUVE{});
    Scene::Hitbox3DNodeComponentUVE hitbox;
    hitbox.halfExtents = Math::Vector3UVE{1.5F, 0.1F, 0.1F};
    entityManager.AddComponentUVE<Scene::Hitbox3DNodeComponentUVE>(attacker, hitbox);

    const Scene::EntityUVE victim = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE victimTransform;
    victimTransform.localPosition = Math::Vector3UVE{2.0F, 0.0F, 0.0F};
    victimTransform.localRotation =
        Math::QuaternionUVE{0.0F, 0.0F, 0.70710678F, 0.70710678F}; // 90 degrees about +Z
    sceneGraph.AttachTransformUVE(entityManager, victim, victimTransform);
    Scene::Hurtbox3DNodeComponentUVE hurtbox;
    hurtbox.halfExtents = Math::Vector3UVE{1.5F, 0.1F, 0.1F};
    entityManager.AddComponentUVE<Scene::Hurtbox3DNodeComponentUVE>(victim, hurtbox);

    engine.TickFrameUVE();
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::Hitbox3DNodeComponentUVE>(attacker).strikeCount, 0U);

    // Removing the rotation turns the hurtbox back along X: it must strike. The recorded depth
    // is the minimum-translation-axis penetration, i.e. the thinnest separating direction - for
    // two horizontal rods stacked in Y that is the Y axis (0.1 + 0.1 extents, fully overlapped),
    // not the 1.0-unit X overlap.
    Scene::TransformComponentUVE victimLive =
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(victim);
    victimLive.localRotation = Math::QuaternionUVE{};
    sceneGraph.SetLocalTransformUVE(entityManager, victim, victimLive);
    engine.TickFrameUVE();
    {
        const Scene::Hitbox3DNodeComponentUVE& afterStrike =
            entityManager.GetComponentUVE<Scene::Hitbox3DNodeComponentUVE>(attacker);
        ASSERT_EQ(afterStrike.strikeCount, 1U);
        EXPECT_EQ(afterStrike.strikes[0U].hurtboxEntity, victim);
        EXPECT_NEAR(afterStrike.strikes[0U].penetrationDepth, 0.2F, 0.01F);
    }

    engine.Shutdown();
}

TEST(EngineCoreUVETest, Projectile3DNode_IntegratesVelocityAccelerationAndExpiresAfterLifetime) {
    // EngineCoreUVE::SyncProjectile3DNodesUVE() is new wiring: previously
    // Projectile3DNodeComponentUVE was pure authored data with nothing moving it or expiring it.
    EngineConfigUVE config = MakeTestConfigUVE();
    config.fixedUpdateFps = 1000.0;
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, entity, Scene::TransformComponentUVE{});
    Scene::Projectile3DNodeComponentUVE projectile;
    projectile.velocity = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    projectile.acceleration = Math::Vector3UVE{0.0F, -1.0F, 0.0F};
    projectile.maxLifetime = 0.05F;
    projectile.remainingLifetime = 0.05F;
    entityManager.AddComponentUVE<Scene::Projectile3DNodeComponentUVE>(entity, projectile);

    // A few fixed steps at 1kHz (~6ms of simulated time) - enough to move and to have accumulated
    // some deceleration from `acceleration`, nowhere near the 50ms lifetime yet.
    for (int frame = 0; frame < 3; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        engine.TickFrameUVE();
    }

    {
        const Scene::TransformComponentUVE& transform =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
        EXPECT_GT(transform.localPosition.y, 0.0F);
        const Scene::Projectile3DNodeComponentUVE& live =
            entityManager.GetComponentUVE<Scene::Projectile3DNodeComponentUVE>(entity);
        EXPECT_LT(live.velocity.y, 1.0F);
        EXPECT_TRUE(live.active);
    }

    // Far more real time than the 50ms lifetime, so it must have fully expired by now regardless
    // of scheduling jitter.
    for (int frame = 0; frame < 100; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        engine.TickFrameUVE();
    }

    const Scene::Projectile3DNodeComponentUVE& afterExpiry =
        entityManager.GetComponentUVE<Scene::Projectile3DNodeComponentUVE>(entity);
    EXPECT_FALSE(afterExpiry.active);
    EXPECT_FLOAT_EQ(afterExpiry.remainingLifetime, 0.0F);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, SaveGameSystemAndCheckpointManager_ReachableAndFunctionalAfterInit) {
    EngineConfigUVE config = MakeTestConfigUVE();
    config.saveDirectoryPath = "uve_engine_core_tests_saves";
    std::filesystem::remove_all(config.saveDirectoryPath);

    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Save::ISaveGameSystemUVE& saveGameSystem = engine.GetServicesUVE().GetSaveGameSystemUVE();
    Save::ICheckpointManagerUVE& checkpointManager = engine.GetServicesUVE().GetCheckpointManagerUVE();

    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    ASSERT_TRUE(saveGameSystem.SaveUVE(0, entityManager, {entity}, Save::GameStateMetadataUVE{}));
    EXPECT_TRUE(saveGameSystem.HasSaveUVE(0));

    EXPECT_TRUE(checkpointManager.CheckpointUVE(entityManager, {entity}));
    EXPECT_TRUE(saveGameSystem.HasSaveUVE(Save::kManualCheckpointSlotIndexUVE));
    EXPECT_FALSE(saveGameSystem.HasSaveUVE(Save::kAutoSaveSlotIndexUVE));
    EXPECT_TRUE(saveGameSystem.HasSaveUVE(0));

    const std::optional<Save::GameStateMetadataUVE> checkpointMetadata =
        saveGameSystem.GetSaveMetadataUVE(Save::kManualCheckpointSlotIndexUVE);
    ASSERT_TRUE(checkpointMetadata.has_value());
    const VersionUVE engineVersion = engine.GetEngineVersionUVE();
    EXPECT_EQ(checkpointMetadata->engineVersionMajor, engineVersion.major);
    EXPECT_EQ(checkpointMetadata->engineVersionMinor, engineVersion.minor);
    EXPECT_EQ(checkpointMetadata->engineVersionPatch, engineVersion.patch);
    EXPECT_EQ(checkpointMetadata->engineVersionBuild, engineVersion.build);

    engine.Shutdown();
    std::filesystem::remove_all(config.saveDirectoryPath);
}

TEST(EngineCoreUVETest, CheckpointManager_AutoSavesAfterConfiguredInterval_TickFrameUVEDriven) {
    EngineConfigUVE config = MakeTestConfigUVE();
    config.saveDirectoryPath = "uve_engine_core_tests_autosave_saves";
    config.autoSaveIntervalSecondsUVE = std::numeric_limits<double>::min(); // smallest positive interval
    std::filesystem::remove_all(config.saveDirectoryPath);

    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Save::ISaveGameSystemUVE& saveGameSystem = engine.GetServicesUVE().GetSaveGameSystemUVE();

    for (int frame = 0; frame < 3; ++frame) {
        engine.TickFrameUVE();
    }

    EXPECT_TRUE(saveGameSystem.HasSaveUVE(Save::kAutoSaveSlotIndexUVE));
    EXPECT_TRUE(saveGameSystem.ListUsedSlotsUVE().empty());

    engine.Shutdown();
    std::filesystem::remove_all(config.saveDirectoryPath);
}

TEST(EngineCoreUVETest, SimulationControl_PausesStepsQueuesOneStepAndSuppressesTransientCheckpoints) {
    EngineConfigUVE config = MakeTestConfigUVE();
    config.saveDirectoryPath = "uve_engine_core_tests_transient_saves";
    config.autoSaveIntervalSecondsUVE = std::numeric_limits<double>::min(); // smallest positive interval
    std::filesystem::remove_all(config.saveDirectoryPath);

    EngineCoreUVE engine(config);
    EXPECT_FALSE(engine.SetSimulationExecutionModeUVE(SimulationExecutionModeUVE::Paused));
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Save::ISaveGameSystemUVE& saveGameSystem = engine.GetServicesUVE().GetSaveGameSystemUVE();
    Save::ICheckpointManagerUVE& checkpointManager = engine.GetServicesUVE().GetCheckpointManagerUVE();
    ASSERT_TRUE(engine.SetTransientSimulationSessionActiveUVE(true));
    ASSERT_TRUE(engine.SetSimulationExecutionModeUVE(SimulationExecutionModeUVE::Paused));
    EXPECT_TRUE(engine.RequestSingleSimulationStepUVE());
    EXPECT_FALSE(engine.RequestSingleSimulationStepUVE());
    engine.TickFrameUVE();
    EXPECT_EQ(engine.GetSimulationExecutionModeUVE(), SimulationExecutionModeUVE::Paused);
    EXPECT_FALSE(saveGameSystem.HasSaveUVE(Save::kAutoSaveSlotIndexUVE));
    EXPECT_DOUBLE_EQ(checkpointManager.GetElapsedSinceLastSaveSecondsUVE(), 0.0);
    EXPECT_DOUBLE_EQ(checkpointManager.GetTotalPlaytimeSecondsUVE(), 0.0);

    ASSERT_TRUE(engine.SetSimulationExecutionModeUVE(SimulationExecutionModeUVE::Running));
    ASSERT_TRUE(engine.SetTransientSimulationSessionActiveUVE(false));
    // A positive interval must observe a nonzero timer delta; allow the same three-frame cadence
    // used by the neighboring autosave integration test after the paused frame.
    for (int frame = 0; frame < 3; ++frame) {
        engine.TickFrameUVE();
    }
    EXPECT_TRUE(saveGameSystem.HasSaveUVE(Save::kAutoSaveSlotIndexUVE));

    engine.Shutdown();
    std::filesystem::remove_all(config.saveDirectoryPath);
}

TEST(EngineCoreUVETest, HeadlessCommandLineFlag_ForcesHeadlessAndUsesNullWindowManager) {
    // headlessUVE starts false here specifically to prove the --headless CLI flag itself forces
    // it (Init() reads CommandLineUVE before anything else consults the flag), not that the
    // config's own default already happened to be headless.
    EngineConfigUVE config = MakeTestConfigUVE();
    config.headlessUVE = false;
    config.commandLineArgs = {"--headless"};

    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    EXPECT_EQ(engine.GetServicesUVE().GetWindowManagerUVE().GetBackendNameUVE(), "Null");

    for (int frame = 0; frame < 3; ++frame) {
        engine.TickFrameUVE();
    }
    EXPECT_EQ(engine.GetFrameStatsUVE().frameNumber, 3U);

    engine.Shutdown();
}

// Needs a real (possibly virtual, e.g. Xvfb) X display and GL context. Skips cleanly with a clear
// message when unavailable, so the same uve_tests binary runs cleanly with or without a display -
// same GTEST_SKIP() pattern as WindowManagerUVETest/GlRenderDeviceUVETest.
TEST(EngineCoreUVETest, WindowedMode_ReachesRunningAndPresentsEmptyRendererScene) {
    EngineConfigUVE config = MakeTestConfigUVE();
    config.headlessUVE = false;
    config.windowWidth = 64;
    config.windowHeight = 64;
    config.vsyncEnabledUVE = false;
    // This sandbox's Mesa/llvmpipe GLX stack caps at OpenGL 4.5 Core (confirmed by direct
    // testing - 4.6 fails with GLXBadFBConfig); see the identical override + rationale in
    // tests/window/window_manager_uve_tests.cpp. The shipped production default (4, 6) is
    // untouched.
    config.windowGlVersionMajor = 4;
    config.windowGlVersionMinor = 5;

    EngineCoreUVE engine(config);
    engine.Init();
    if (!engine.GetServicesUVE().GetWindowManagerUVE().IsValidUVE()) {
        GTEST_SKIP() << "No display available for windowed EngineCoreUVE - skipping (run under "
                        "xvfb-run to exercise this test)";
    }
    ASSERT_TRUE(engine.Load());
    EXPECT_EQ(engine.GetServicesUVE().GetWindowManagerUVE().GetBackendNameUVE(), "GLFW3");

    // An active identity camera ensures this is an empty *renderer scene* test rather than a
    // no-camera EngineCore no-op test.
    EngineServicesUVE& services = engine.GetServicesUVE();
    Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = services.GetSceneGraphUVE();
    const Scene::EntityUVE camera = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, camera, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::CameraComponentUVE>(camera);
    engine.SetActiveCameraUVE(camera);

    std::array<unsigned char, 3> scenePixel{};
    GLenum postRenderGlError = GL_NO_ERROR;
    engine.SetPostRenderCallbackUVE([&scenePixel, &postRenderGlError] {
        glFinish();
        glReadBuffer(GL_BACK);
        glReadPixels(32, 32, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, scenePixel.data());
        postRenderGlError = glGetError();
    });

    // A few frames let Renderer3DUVE complete scene, tone-mapping, and presentation work.
    for (int frame = 0; frame < 3; ++frame) {
        engine.TickFrameUVE();
    }
    engine.SetPostRenderCallbackUVE({});
    EXPECT_EQ(engine.GetStateUVE(), EngineStateUVE::Running);
    const Render::Renderer3DFrameDiagnosticsUVE diagnostics =
        engine.GetServicesUVE().GetRenderer3DUVE().GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(diagnostics.renderTargetWidth, 64U);
    EXPECT_EQ(diagnostics.renderTargetHeight, 64U);

    // With no document render items, Renderer3DUVE presents its deterministic charcoal
    // tone-mapped environment. This is intentionally not a demo-geometry assertion: visible
    // content enters only through ECS extraction into the renderer.
    EXPECT_EQ(scenePixel[0], 11U);
    EXPECT_EQ(scenePixel[1], 11U);
    EXPECT_EQ(scenePixel[2], 11U);
    EXPECT_EQ(postRenderGlError, GL_NO_ERROR);

    engine.Shutdown();
}

// Exercises the actual EngineCore -> Renderer3DUVE -> tone-mapping -> GLFW default-framebuffer
// path. The fixed positions are intentionally far from geometry edges so a future expected-pixel
// assertion cannot be satisfied by the background alone.
TEST(EngineCoreUVETest, WindowedMode_PresentsDeterministicPrimitiveFixtureToDefaultFramebuffer) {
    EngineConfigUVE config = MakeTestConfigUVE();
    config.headlessUVE = false;
    config.windowWidth = 128U;
    config.windowHeight = 96U;
    config.renderTargetWidth = 128U;
    config.renderTargetHeight = 96U;
    config.vsyncEnabledUVE = false;
    config.windowGlVersionMajor = 4U;
    config.windowGlVersionMinor = 5U;
    config.ambientColor = Math::Vector3UVE{0.45F, 0.45F, 0.45F};

    EngineCoreUVE engine(config);
    engine.Init();
    if (!engine.GetServicesUVE().GetWindowManagerUVE().IsValidUVE()) {
        GTEST_SKIP() << "No display available for windowed EngineCoreUVE - skipping (run under "
                        "xvfb-run to exercise this test)";
    }
    ASSERT_TRUE(engine.Load());

    EngineServicesUVE& services = engine.GetServicesUVE();
    Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = services.GetSceneGraphUVE();
    const Scene::EntityUVE camera = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, camera, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::CameraComponentUVE>(camera);
    engine.SetActiveCameraUVE(camera);

    Math::QuaternionUVE planeFacingCamera{};
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{1.0F, 0.0F, 0.0F},
                                          std::numbers::pi_v<float> * 0.5F, planeFacingCamera));
    const auto makePrimitive = [&entityManager, &sceneGraph](const Math::Vector3UVE position,
                                                               const Math::QuaternionUVE rotation,
                                                               const Math::Vector3UVE scale,
                                                               const Scene::PrimitiveMeshKindUVE kind,
                                                               const Math::Vector3UVE color) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform{};
        transform.localPosition = position;
        transform.localRotation = rotation;
        transform.localScale = scale;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        entityManager.AddComponentUVE<Scene::PrimitiveMeshComponentUVE>(
            entity, Scene::PrimitiveMeshComponentUVE{kind, color});
    };
    makePrimitive(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, planeFacingCamera, Math::Vector3UVE{6.0F, 6.0F, 1.0F},
                  Scene::PrimitiveMeshKindUVE::Plane, Math::Vector3UVE{0.10F, 0.72F, 0.20F});
    makePrimitive(Math::Vector3UVE{-3.5F, 0.0F, -8.0F}, Math::QuaternionUVE{}, Math::Vector3UVE{1.0F, 1.0F, 1.0F},
                  Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{0.88F, 0.10F, 0.06F});
    makePrimitive(Math::Vector3UVE{-1.8F, 0.0F, -7.0F}, Math::QuaternionUVE{}, Math::Vector3UVE{1.4F, 1.4F, 1.4F},
                  Scene::PrimitiveMeshKindUVE::UVSphere, Math::Vector3UVE{0.06F, 0.20F, 0.90F});
    sceneGraph.UpdateUVE(entityManager);

    std::array<unsigned char, 3> cube{};
    std::array<unsigned char, 3> plane{};
    std::array<unsigned char, 3> sphere{};
    GLenum postRenderGlError = GL_NO_ERROR;
    engine.SetPostRenderCallbackUVE([&cube, &plane, &sphere, &postRenderGlError] {
        // The callback runs after Renderer3DUVE’s default-framebuffer tone-map pass and before
        // EngineCoreUVE requests SwapBuffersUVE(). GL_BACK is therefore the exact presentation
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
        engine.TickFrameUVE();
    }
    engine.SetPostRenderCallbackUVE({});

    const Render::Renderer3DFrameDiagnosticsUVE diagnostics = services.GetRenderer3DUVE().GetLastFrameDiagnosticsUVE();
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

// Phase U3a coverage: proves UIRuntimeUVE's authored screen-space UI (image/button/text) actually
// reaches real pixels on the default framebuffer through Renderer3DUVE's new "UIOverlay" pass, and
// that a real mouse click (via IInputSystemUVE's real Set*StateUVE() live-state model, exactly like
// a real GLFW callback would drive it) flips UIButtonComponentUVE::wasClickedThisFrame and visibly
// changes the button's rendered color - the exact end-to-end claim Phase U3a's own plan requires.
// Real (non-headless) GLFW/GL backend under Xvfb, matching every other WindowedMode_*/UI-adjacent
// test in this file. glReadPixels uses GL's bottom-left-origin convention; UIRuntimeUVE/UIOverlay
// use top-left-origin, y-down pixel space (matching GetMousePositionUVE()'s own convention), so
// sample rows are converted via `windowHeight - uiY`.
TEST(EngineCoreUVETest, WindowedMode_UIOverlayRendersAuthoredUIAndRegistersARealButtonClick) {
    EngineConfigUVE config = MakeTestConfigUVE();
    config.headlessUVE = false;
    config.windowWidth = 160U;
    config.windowHeight = 120U;
    config.renderTargetWidth = 160U;
    config.renderTargetHeight = 120U;
    config.vsyncEnabledUVE = false;
    config.windowGlVersionMajor = 4U;
    config.windowGlVersionMinor = 5U;

    EngineCoreUVE engine(config);
    engine.Init();
    if (!engine.GetServicesUVE().GetWindowManagerUVE().IsValidUVE()) {
        GTEST_SKIP() << "No display available for windowed EngineCoreUVE - skipping (run under "
                        "xvfb-run to exercise this test)";
    }
    ASSERT_TRUE(engine.Load());

    EngineServicesUVE& services = engine.GetServicesUVE();
    Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = services.GetSceneGraphUVE();
    Input::IInputSystemUVE& inputSystem = services.GetInputSystemUVE();
    // WindowManagerUVE re-polls the *real* OS cursor/button state every frame (matching real
    // gameplay use) and would otherwise overwrite this test's directly-injected mouse state on the
    // very next TickFrameUVE() - detaching it here is what lets IInputSystemUVE's live-state model
    // (the same Set*StateUVE() calls a real GLFW callback would make) drive input deterministically
    // against this still-real GL window/context.
    services.GetWindowManagerUVE().AttachInputSystemUVE(nullptr);

    const Scene::EntityUVE camera = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, camera, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::CameraComponentUVE>(camera);
    engine.SetActiveCameraUVE(camera);

    const Scene::EntityUVE imageEntity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<Scene::UIImageComponentUVE>(
        imageEntity, Scene::UIImageComponentUVE{Asset::AssetGuidUVE{}, Math::Vector2UVE{10.0F, 10.0F},
                                                 Math::Vector2UVE{20.0F, 20.0F}, Math::Vector3UVE{0.90F, 0.10F, 0.10F},
                                                 1.0F});

    const Scene::EntityUVE buttonEntity = entityManager.CreateEntityUVE();
    Scene::UIButtonComponentUVE buttonComponent{};
    buttonComponent.positionPixels = Math::Vector2UVE{60.0F, 10.0F};
    buttonComponent.sizePixels = Math::Vector2UVE{40.0F, 20.0F};
    buttonComponent.normalColor = Math::Vector3UVE{0.10F, 0.85F, 0.10F};
    buttonComponent.hoverColor = Math::Vector3UVE{0.10F, 0.10F, 0.85F};
    buttonComponent.pressedColor = Math::Vector3UVE{0.85F, 0.85F, 0.10F};
    entityManager.AddComponentUVE<Scene::UIButtonComponentUVE>(buttonEntity, buttonComponent);

    const Scene::EntityUVE textEntity = entityManager.CreateEntityUVE();
    Scene::UITextComponentUVE textComponent{};
    textComponent.text = "Hi";
    textComponent.positionPixels = Math::Vector2UVE{10.0F, 60.0F};
    textComponent.fontSize = 24.0F;
    textComponent.color = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
    entityManager.AddComponentUVE<Scene::UITextComponentUVE>(textEntity, textComponent);

    const auto sampleTopLeftPixelUVE = [windowHeight = config.windowHeight](float uiX, float uiY) {
        std::array<unsigned char, 3> pixel{};
        const int glX = static_cast<int>(uiX);
        const int glY = static_cast<int>(windowHeight) - static_cast<int>(uiY);
        glReadPixels(glX, glY, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel.data());
        return pixel;
    };

    // Frame 1 (baseline): mouse well away from the button, nothing pressed.
    inputSystem.SetMousePositionUVE(Math::Vector2UVE{0.0F, 0.0F});
    std::array<unsigned char, 3> imagePixel{};
    std::array<unsigned char, 3> buttonPixelBaseline{};
    unsigned char maxTextBrightness = 0U;
    engine.SetPostRenderCallbackUVE([&] {
        glFinish();
        glReadBuffer(GL_BACK);
        imagePixel = sampleTopLeftPixelUVE(20.0F, 20.0F);
        buttonPixelBaseline = sampleTopLeftPixelUVE(80.0F, 20.0F);
        for (float sampleX = 8.0F; sampleX <= 40.0F; sampleX += 2.0F) {
            for (float sampleY = 55.0F; sampleY <= 90.0F; sampleY += 2.0F) {
                const std::array<unsigned char, 3> pixel = sampleTopLeftPixelUVE(sampleX, sampleY);
                const unsigned char brightness = static_cast<unsigned char>(
                    (static_cast<int>(pixel[0]) + static_cast<int>(pixel[1]) + static_cast<int>(pixel[2])) / 3);
                maxTextBrightness = std::max(maxTextBrightness, brightness);
            }
        }
    });
    engine.TickFrameUVE();
    engine.SetPostRenderCallbackUVE({});

    EXPECT_FALSE(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(buttonEntity).isHovered);
    EXPECT_FALSE(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(buttonEntity).wasClickedThisFrame);
    EXPECT_GT(imagePixel[0], static_cast<unsigned char>(imagePixel[1] + 20U))
        << "image RGB=" << static_cast<int>(imagePixel[0]) << ',' << static_cast<int>(imagePixel[1]) << ','
        << static_cast<int>(imagePixel[2]);
    EXPECT_GT(buttonPixelBaseline[1], static_cast<unsigned char>(buttonPixelBaseline[0] + 20U))
        << "button(normal) RGB=" << static_cast<int>(buttonPixelBaseline[0]) << ','
        << static_cast<int>(buttonPixelBaseline[1]) << ',' << static_cast<int>(buttonPixelBaseline[2]);
    EXPECT_GT(maxTextBrightness, 90U) << "max sampled text-region brightness=" << static_cast<int>(maxTextBrightness);

    // Frame 2: a real mouse move + press over the button - IInputSystemUVE's live-state model is
    // exactly what a real GLFW cursor/mouse-button callback would drive.
    inputSystem.SetMousePositionUVE(Math::Vector2UVE{80.0F, 20.0F});
    inputSystem.SetMouseButtonStateUVE(Input::MouseButtonUVE::Left, true);
    std::array<unsigned char, 3> buttonPixelPressed{};
    engine.SetPostRenderCallbackUVE([&] {
        glFinish();
        glReadBuffer(GL_BACK);
        buttonPixelPressed = sampleTopLeftPixelUVE(80.0F, 20.0F);
    });
    engine.TickFrameUVE();
    engine.SetPostRenderCallbackUVE({});

    const Scene::UIButtonComponentUVE afterClick = entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(buttonEntity);
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
    inputSystem.SetMouseButtonStateUVE(Input::MouseButtonUVE::Left, false);
    std::array<unsigned char, 3> buttonPixelHover{};
    engine.SetPostRenderCallbackUVE([&] {
        glFinish();
        glReadBuffer(GL_BACK);
        buttonPixelHover = sampleTopLeftPixelUVE(80.0F, 20.0F);
    });
    engine.TickFrameUVE();
    engine.SetPostRenderCallbackUVE({});

    const Scene::UIButtonComponentUVE afterRelease = entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(buttonEntity);
    EXPECT_TRUE(afterRelease.isHovered);
    EXPECT_FALSE(afterRelease.wasClickedThisFrame) << "a stale click latched past the frame it occurred on";
    EXPECT_GT(buttonPixelHover[2], static_cast<unsigned char>(buttonPixelHover[0] + 20U))
        << "button(hover) RGB=" << static_cast<int>(buttonPixelHover[0]) << ','
        << static_cast<int>(buttonPixelHover[1]) << ',' << static_cast<int>(buttonPixelHover[2]);

    engine.Shutdown();
}

// Gap A audit coverage: verifies the render target/framebuffer dimensions (audit checkpoint #2)
// actually track SetEditorViewportRegionUVE()'s region - not the window - and cleanly revert once
// the region is cleared. Real windowed GLFW/GL backend under Xvfb, matching every other
// WindowedMode_* test's verification method in this file (not NullRenderDeviceUVE, since this
// exercises the real adaptive-resolution/window-manager code path SyncAdaptiveRenderResolutionUVE()
// only takes when m_windowedRenderingActiveUVE is true).
TEST(EngineCoreUVETest, SetEditorViewportRegionUVE_DrivesRenderTargetToRegionNotWindow) {
    EngineConfigUVE config = MakeTestConfigUVE();
    config.headlessUVE = false;
    config.windowWidth = 200U;
    config.windowHeight = 150U;
    config.vsyncEnabledUVE = false;
    config.windowGlVersionMajor = 4U;
    config.windowGlVersionMinor = 5U;

    EngineCoreUVE engine(config);
    engine.Init();
    if (!engine.GetServicesUVE().GetWindowManagerUVE().IsValidUVE()) {
        GTEST_SKIP() << "No display available for windowed EngineCoreUVE - skipping (run under "
                        "xvfb-run to exercise this test)";
    }
    ASSERT_TRUE(engine.Load());

    EngineServicesUVE& services = engine.GetServicesUVE();
    Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = services.GetSceneGraphUVE();
    const Scene::EntityUVE camera = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, camera, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::CameraComponentUVE>(camera);
    engine.SetActiveCameraUVE(camera);

    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    services.GetLoggerUVE().AddSink(std::move(memorySink));

    // Baseline: no region set yet, so the target tracks the 200x150 window as before this fix.
    engine.TickFrameUVE();
    const Render::Renderer3DFrameDiagnosticsUVE windowDrivenDiagnostics =
        services.GetRenderer3DUVE().GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(windowDrivenDiagnostics.renderTargetWidth, 200U);
    EXPECT_EQ(windowDrivenDiagnostics.renderTargetHeight, 150U);

    // A region clearly smaller than, and offset within, the window - proves the target tracks the
    // region's own dimensions ("very narrow"/"resized side panels" editor layouts), not the window.
    engine.SetEditorViewportRegionUVE(Render::ViewportRectUVE{20U, 10U, 90U, 60U});
    engine.TickFrameUVE();
    const Render::Renderer3DFrameDiagnosticsUVE regionDrivenDiagnostics =
        services.GetRenderer3DUVE().GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(regionDrivenDiagnostics.renderTargetWidth, 90U);
    EXPECT_EQ(regionDrivenDiagnostics.renderTargetHeight, 60U);

    // Clearing the region reverts to window-driven sizing - proves no stale state leaks into a
    // frame the editor no longer wants confined (e.g. switching back to a full/maximized layout).
    engine.SetEditorViewportRegionUVE(std::nullopt);
    engine.TickFrameUVE();
    const Render::Renderer3DFrameDiagnosticsUVE revertedDiagnostics =
        services.GetRenderer3DUVE().GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(revertedDiagnostics.renderTargetWidth, 200U);
    EXPECT_EQ(revertedDiagnostics.renderTargetHeight, 150U);

    const std::vector<Debug::LogMessageUVE> messages = memorySinkPtr->GetMessagesUVE();
    const bool foundViewportRejection =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessageUVE& message) {
            return message.message.find("does not fit within") != std::string::npos;
        });
    EXPECT_FALSE(foundViewportRejection);

    engine.Shutdown();
}

// A region whose on-screen destination sub-rect is a THIN SLICE of the window (very narrow and
// very wide/short cases) - the two extreme "editor layout" shapes the audit specifically asked to
// be exercised, beyond the more moderate rect above.
TEST(EngineCoreUVETest, SetEditorViewportRegionUVE_HandlesVeryNarrowAndVeryWideRegions) {
    EngineConfigUVE config = MakeTestConfigUVE();
    config.headlessUVE = false;
    config.windowWidth = 200U;
    config.windowHeight = 150U;
    config.vsyncEnabledUVE = false;
    config.windowGlVersionMajor = 4U;
    config.windowGlVersionMinor = 5U;

    EngineCoreUVE engine(config);
    engine.Init();
    if (!engine.GetServicesUVE().GetWindowManagerUVE().IsValidUVE()) {
        GTEST_SKIP() << "No display available for windowed EngineCoreUVE - skipping (run under "
                        "xvfb-run to exercise this test)";
    }
    ASSERT_TRUE(engine.Load());

    EngineServicesUVE& services = engine.GetServicesUVE();
    Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = services.GetSceneGraphUVE();
    const Scene::EntityUVE camera = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, camera, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::CameraComponentUVE>(camera);
    engine.SetActiveCameraUVE(camera);

    // Very narrow: a 10px-wide sliver on the right (e.g. a Scene panel dragged nearly closed).
    engine.SetEditorViewportRegionUVE(Render::ViewportRectUVE{180U, 0U, 10U, 150U});
    engine.TickFrameUVE();
    const Render::Renderer3DFrameDiagnosticsUVE narrowDiagnostics =
        services.GetRenderer3DUVE().GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(narrowDiagnostics.renderTargetWidth, 10U);
    EXPECT_EQ(narrowDiagnostics.renderTargetHeight, 150U);

    // Very wide/short: the full width, a thin 8px strip in height (e.g. a bottom dock nearly
    // maximized over the viewport).
    engine.SetEditorViewportRegionUVE(Render::ViewportRectUVE{0U, 0U, 200U, 8U});
    engine.TickFrameUVE();
    const Render::Renderer3DFrameDiagnosticsUVE wideDiagnostics =
        services.GetRenderer3DUVE().GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(wideDiagnostics.renderTargetWidth, 200U);
    EXPECT_EQ(wideDiagnostics.renderTargetHeight, 8U);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, PostRenderCallback_HeadlessModeDoesNotInvokeOverlay) {
    EngineConfigUVE config = MakeTestConfigUVE();
    config.headlessUVE = true;
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    int callbackCount = 0;
    engine.SetPostRenderCallbackUVE([&callbackCount] { ++callbackCount; });
    engine.TickFrameUVE();
    EXPECT_EQ(callbackCount, 0);

    engine.SetPostRenderCallbackUVE({});
    engine.Shutdown();
}

// A-1 (P0): RunUVE() is a hard exception boundary - see its doc comment in engine_core_uve.h.
// TickFrameUVE_UncaughtSubscriberException below proves the underlying mechanism is real (an
// exception genuinely propagates out of a frame update when nothing catches it) using the real
// EngineCoreUVE via IEventSystemUVE's public Subscribe()/QueueEvent() API - the same real
// mechanism RunUVE()'s internal frame loop drives every frame via TickFrameUVE().
//
// The three RunUVEExceptionBoundaryHarness tests further down cover all three cases the fix
// requires (throw during Init(), during Load(), during a frame update) against a small,
// self-contained harness rather than the real EngineCoreUVE::RunUVE(): EngineCoreUVE constructs
// roughly 34 concrete subsystems directly in Init(), each already written (per the D-1/SYS-1
// audits) to avoid throwing wherever avoidable, so there is no deterministic, non-flaky way to
// make Init() or Load() throw through EngineCoreUVE's public API alone. The harness reuses the
// real EngineStateUVE/IsValidTransitionUVE/EngineCoreUVE::kUnhandledExceptionExitCodeUVE from
// production code and reproduces RunUVE()'s documented contract exactly, so it verifies that
// contract's logic even though it cannot substitute for exercising EngineCoreUVE::Init()/Load()
// themselves.
TEST(EngineCoreUVETest, TickFrameUVE_UncaughtSubscriberException_PropagatesInsteadOfBeingSwallowed) {
    struct BoomEventUVE {};

    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    engine.GetServicesUVE().GetEventSystemUVE().Subscribe<BoomEventUVE>(
        [](const BoomEventUVE&) { throw std::runtime_error("subscriber exploded"); });
    engine.GetServicesUVE().GetEventSystemUVE().QueueEvent(BoomEventUVE{});

    // TickFrameUVE() itself has no exception boundary by design - RunUVE() is the boundary (see
    // its doc comment), so a caller driving frames directly (this test, and the editor's
    // hand-rolled loop in engine/app/src/editor/main.cpp) is responsible for its own. Confirms
    // the exception is genuinely uncaught here, not silently swallowed somewhere internally.
    EXPECT_THROW(engine.TickFrameUVE(), std::runtime_error);
    EXPECT_EQ(engine.GetStateUVE(), EngineStateUVE::Running); // still mid-run; clean up explicitly
    engine.Shutdown();
}

namespace {

/// Reproduces EngineCoreUVE::RunUVE()'s documented exception-boundary contract in isolation
/// (see the comment above TickFrameUVE_UncaughtSubscriberException_PropagatesInsteadOfBeingSwallowed
/// for why): catches any exception thrown by an injected Init()/Load()/frame-update hook, calls a
/// Shutdown()-equivalent only if state had actually reached Running (an exception during Init()
/// itself must not force it - see RunUVE()'s doc comment), never lets a second exception from
/// that Shutdown()-equivalent escape either, and returns EngineCoreUVE::kUnhandledExceptionExitCodeUVE.
class RunUVEExceptionBoundaryHarnessUVE final {
public:
    std::function<void()> onInit;
    std::function<bool()> onLoad = [] { return true; };
    std::function<void()> onFrameUpdate;

    int RunUVE(int frameCount) {
        try {
            TransitionUVE(EngineStateUVE::Initializing);
            if (onInit) {
                onInit();
            }
            TransitionUVE(EngineStateUVE::Running);
            if (onLoad && !onLoad()) {
                ShutdownUVE();
                return 1;
            }
            for (int frame = 0; frame < frameCount; ++frame) {
                if (onFrameUpdate) {
                    onFrameUpdate();
                }
            }
            ShutdownUVE();
            return 0;
        } catch (const std::exception&) {
            // Production logs via UVE_FATAL here; nothing to assert on in this harness.
        } catch (...) {
        }
        if (m_state == EngineStateUVE::Running) {
            try {
                ShutdownUVE();
            } catch (...) {
                m_shutdownThrew = true;
            }
        }
        return EngineCoreUVE::kUnhandledExceptionExitCodeUVE;
    }

    [[nodiscard]] bool ShutdownRanUVE() const noexcept { return m_shutdownRan; }
    [[nodiscard]] bool ShutdownThrewUVE() const noexcept { return m_shutdownThrew; }
    [[nodiscard]] EngineStateUVE GetStateUVE() const noexcept { return m_state; }

private:
    void TransitionUVE(EngineStateUVE newState) {
        // Mirrors EngineCoreUVE::TransitionStateUVE()'s own UVE_ASSERT(IsValidTransitionUVE(...))
        // guard - a failure here is a bug in this harness's own test-hook wiring, not in the
        // property under test, so a plain non-fatal EXPECT_TRUE is enough to surface it.
        EXPECT_TRUE(IsValidTransitionUVE(m_state, newState));
        m_state = newState;
    }

    void ShutdownUVE() {
        TransitionUVE(EngineStateUVE::ShuttingDown);
        m_shutdownRan = true;
        TransitionUVE(EngineStateUVE::Shutdown);
    }

    EngineStateUVE m_state = EngineStateUVE::Uninitialized;
    bool m_shutdownRan = false;
    bool m_shutdownThrew = false;
};

} // namespace

TEST(RunUVEExceptionBoundaryHarnessUVETest, ThrowDuringInit_ReturnsDistinctCodeAndSkipsUnsafeShutdown) {
    RunUVEExceptionBoundaryHarnessUVE harness;
    harness.onInit = [] { throw std::runtime_error("boom during init"); };

    EXPECT_EQ(harness.RunUVE(1), EngineCoreUVE::kUnhandledExceptionExitCodeUVE);
    // State never reached Running - the real EngineCoreUVE::Shutdown() would dereference
    // subsystems Init() never got to construct, so it must not run here either.
    EXPECT_FALSE(harness.ShutdownRanUVE());
    EXPECT_EQ(harness.GetStateUVE(), EngineStateUVE::Initializing);
}

TEST(RunUVEExceptionBoundaryHarnessUVETest, ThrowDuringLoad_RunsShutdownInNormalOrderAndReturnsDistinctCode) {
    RunUVEExceptionBoundaryHarnessUVE harness;
    harness.onLoad = []() -> bool { throw std::runtime_error("boom during load"); };

    EXPECT_EQ(harness.RunUVE(1), EngineCoreUVE::kUnhandledExceptionExitCodeUVE);
    EXPECT_TRUE(harness.ShutdownRanUVE());
    EXPECT_FALSE(harness.ShutdownThrewUVE());
    EXPECT_EQ(harness.GetStateUVE(), EngineStateUVE::Shutdown);
}

TEST(RunUVEExceptionBoundaryHarnessUVETest, ThrowDuringFrameUpdate_RunsShutdownInNormalOrderAndReturnsDistinctCode) {
    RunUVEExceptionBoundaryHarnessUVE harness;
    int framesRun = 0;
    harness.onFrameUpdate = [&framesRun] {
        ++framesRun;
        if (framesRun == 2) {
            throw std::runtime_error("boom mid-loop");
        }
    };

    EXPECT_EQ(harness.RunUVE(5), EngineCoreUVE::kUnhandledExceptionExitCodeUVE);
    EXPECT_EQ(framesRun, 2); // stopped exactly at the throwing frame, not all 5
    EXPECT_TRUE(harness.ShutdownRanUVE());
    EXPECT_EQ(harness.GetStateUVE(), EngineStateUVE::Shutdown);
}


// ---------------------------------------------------------------------------
// CS2: ComputeSystemUVE wired into the frame loop. The queue is drained in
// Render(), BEFORE any render pass opens, into its own submitted command
// buffer - the RHI compute slices' outside-pass-markers contract, enforced at
// the engine level rather than left to each caller.
// ---------------------------------------------------------------------------

// The headless engine config brings up NullRenderDeviceUVE, whose spy hook keeps the most
// recently submitted command list - the same evidence channel RenderSystemUVE's own tests use.
// Reached by dynamic_cast rather than a new production accessor: the engine exposes the device
// through its interface, and inventing a test-only getter on EngineCoreUVE to see one backend's
// spy would put test scaffolding into the engine's public surface.
[[nodiscard]] const std::vector<Render::RecordedCommandUVE>& LastSubmittedCommandsUVE(
    EngineCoreUVE& engine) {
    auto* const nullDevice =
        dynamic_cast<Render::NullRenderDeviceUVE*>(&engine.GetServicesUVE().GetRenderDeviceUVE());
    UVE_ASSERT(nullDevice != nullptr && "headless EngineCoreUVE must run on NullRenderDeviceUVE");
    return nullDevice->GetLastSubmittedCommandsUVE();
}

TEST(EngineCoreUVETest, ComputeSystem_ReachableThroughServicesAfterInit) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Render::IComputeSystemUVE& computeSystem = engine.GetServicesUVE().GetComputeSystemUVE();
    EXPECT_EQ(computeSystem.GetQueuedDispatchCountUVE(), 0U);
    EXPECT_EQ(computeSystem.GetDiagnosticsUVE().dispatchesRecorded, 0U);

    engine.Shutdown();
}

TEST(EngineCoreUVETest, TickFrame_QueuedComputeDispatch_IsSubmittedBeforeAnyRenderPass) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Render::IComputeSystemUVE& computeSystem = engine.GetServicesUVE().GetComputeSystemUVE();
    Render::IRenderDeviceUVE& renderDevice = engine.GetServicesUVE().GetRenderDeviceUVE();

    Render::ComputeProgramDescUVE programDesc;
    programDesc.sourceCode = "engine-frame-loop compute kernel (recorded by the Null backend)";
    programDesc.debugName = "frame-loop-kernel";
    const Render::PipelineHandleUVE program = computeSystem.CreateProgramUVE(programDesc);
    ASSERT_NE(program, Render::kInvalidPipelineHandleUVE);

    const Render::BufferHandleUVE storage =
        renderDevice.CreateBufferUVE(Render::BufferDescUVE{64U, Render::BufferUsageUVE::Storage});
    ASSERT_NE(storage, Render::kInvalidBufferHandleUVE);

    Render::ComputeDispatchDescUVE dispatch;
    dispatch.program = program;
    dispatch.groupCountX = 3U;
    dispatch.groupCountY = 2U;
    dispatch.groupCountZ = 1U;
    dispatch.storageBuffers.push_back(Render::ComputeStorageBufferBindingUVE{storage, 0U});
    ASSERT_TRUE(computeSystem.EnqueueDispatchUVE(dispatch));
    ASSERT_EQ(computeSystem.GetQueuedDispatchCountUVE(), 1U);

    engine.TickFrameUVE();

    // The queue drained through the frame loop - no test touched a command buffer directly.
    EXPECT_EQ(computeSystem.GetQueuedDispatchCountUVE(), 0U);
    EXPECT_EQ(computeSystem.GetDiagnosticsUVE().dispatchesRecorded, 1U);

    // The compute work went out as its OWN submission, with no pass markers around it: the Null
    // spy keeps the most recent submitted list, and with no active camera the renderer records
    // nothing this frame, so what remains is exactly the compute buffer.
    const auto& submitted = LastSubmittedCommandsUVE(engine);
    ASSERT_EQ(submitted.size(), 3U);
    ASSERT_TRUE(std::holds_alternative<Render::BindPipelineCommandUVE>(submitted[0]));
    EXPECT_EQ(std::get<Render::BindPipelineCommandUVE>(submitted[0]).pipeline, program);
    ASSERT_TRUE(std::holds_alternative<Render::BindStorageBufferCommandUVE>(submitted[1]));
    EXPECT_EQ(std::get<Render::BindStorageBufferCommandUVE>(submitted[1]).buffer, storage);
    ASSERT_TRUE(std::holds_alternative<Render::DispatchCommandUVE>(submitted[2]));
    EXPECT_EQ(std::get<Render::DispatchCommandUVE>(submitted[2]).groupCountX, 3U);
    EXPECT_EQ(std::get<Render::DispatchCommandUVE>(submitted[2]).groupCountY, 2U);

    // No render pass command may appear in the compute submission at all.
    for (const Render::RecordedCommandUVE& command : submitted) {
        EXPECT_FALSE(std::holds_alternative<Render::BeginRenderPassCommandUVE>(command));
        EXPECT_FALSE(std::holds_alternative<Render::EndRenderPassCommandUVE>(command));
    }

    computeSystem.DestroyProgramUVE(program);
    renderDevice.DestroyBufferUVE(storage);
    engine.Shutdown();
}

TEST(EngineCoreUVETest, TickFrame_EmptyComputeQueue_SubmitsNothingExtra) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    // A frame with no queued compute must not submit an empty compute buffer - scenes that never
    // touch compute pay nothing observable.
    engine.TickFrameUVE();

    EXPECT_TRUE(LastSubmittedCommandsUVE(engine).empty());
    EXPECT_EQ(engine.GetServicesUVE().GetComputeSystemUVE().GetDiagnosticsUVE().dispatchesRecorded, 0U);

    engine.Shutdown();
}

#if UVE_DEBUG
TEST(EngineCoreUVEDeathTest, ShutdownBeforeInit_TriggersInvalidTransitionAssert) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    EXPECT_DEATH({ engine.Shutdown(); }, "");
}

TEST(EngineCoreUVEDeathTest, GetServicesUVEBeforeInit_Asserts) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    EXPECT_DEATH({ static_cast<void>(engine.GetServicesUVE()); }, "");
}
#else
TEST(EngineCoreUVETest, GetServicesUVEBeforeInit_ThrowsBadOptionalAccessInsteadOfDereferencingEmptyOptional) {
    EngineCoreUVE engine(MakeTestConfigUVE());
    EXPECT_THROW({ static_cast<void>(engine.GetServicesUVE()); }, std::bad_optional_access);
}
#endif

// LevelStreamer3D helpers: a tiny authored level document (one transformed root) written to a
// temp path through the production serializer, plus an enabled streamer component pointing at
// it. Keeping the streamed content OUT of the fixture's own document is the entire point of the
// slice, so the file is the real interchange - not an in-memory shortcut.
namespace {

const std::filesystem::path kStreamerTestLevelPath = "uve_engine_core_streamer_test_level.uvescene";

// Writes `kStreamerTestLevelPath` and returns true on success. Two entities: a root at (0,0,0)
// with a child offset at (1,2,3), so "one loaded level" is measurable as +2 entities.
bool WriteStreamerTestLevelFileUVE() {
    std::filesystem::remove(kStreamerTestLevelPath);
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE tempManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    Scene::SceneGraphUVE tempGraph;
    Scene::SceneSerializerUVE serializer;

    const Scene::EntityUVE root = tempManager.CreateEntityUVE();
    tempGraph.AttachTransformUVE(tempManager, root, Scene::TransformComponentUVE{});
    const Scene::EntityUVE child = tempManager.CreateEntityUVE();
    Scene::TransformComponentUVE childTransform;
    childTransform.localPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
    tempGraph.AttachTransformUVE(tempManager, child, childTransform);
    tempGraph.SetParentUVE(tempManager, child, root);
    const bool saved =
        serializer.SaveUVE(tempManager, {root}, kStreamerTestLevelPath, Asset::AssetKindUVE::Scene);
    if (!saved) {
        std::filesystem::remove(kStreamerTestLevelPath);
    }
    return saved;
}

struct StreamerTestCleanupUVE {
    ~StreamerTestCleanupUVE() { std::filesystem::remove(kStreamerTestLevelPath); }
};

Scene::EntityUVE CreateEnabledStreamerAtUVE(Scene::IEntityManagerUVE& entityManager,
                                            Scene::ISceneGraphUVE& sceneGraph,
                                            const Math::Vector3UVE& position) {
    const Scene::EntityUVE streamer = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE transform;
    transform.localPosition = position;
    sceneGraph.AttachTransformUVE(entityManager, streamer, transform);
    Scene::LevelStreamer3DNodeComponentUVE component;
    component.levelPath = kStreamerTestLevelPath.string();
    component.loadDistance = 10.0F;
    component.unloadDistance = 20.0F;
    component.enabled = true;
    entityManager.AddComponentUVE<Scene::LevelStreamer3DNodeComponentUVE>(streamer, component);
    return streamer;
}

Scene::EntityUVE CreateWatchingCameraAtUVE(Scene::IEntityManagerUVE& entityManager,
                                           Scene::ISceneGraphUVE& sceneGraph,
                                           const Math::Vector3UVE& position) {
    const Scene::EntityUVE camera = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE transform;
    transform.localPosition = position;
    sceneGraph.AttachTransformUVE(entityManager, camera, transform);
    entityManager.AddComponentUVE<Scene::CameraComponentUVE>(camera, Scene::CameraComponentUVE{});
    return camera;
}

} // namespace

TEST(EngineCoreUVETest, LevelStreamer3D_NearViewerLoadsContentFarViewerUnloadsItHysteresisHoldsBetween) {
    // The full distance-driven contract measured end-to-end: a viewer inside the load radius
    // pulls the level document in (+2 entities, streamer.flags flip), the hysteresis band between
    // loadDistance and unloadDistance changes nothing, leaving the unload radius destroys the
    // loaded subtree (-2 entities), and loadRequested is false between ticks - the synchronous
    // model's documented invariant.
    ASSERT_TRUE(WriteStreamerTestLevelFileUVE()) << "the streamed level document must save";
    StreamerTestCleanupUVE cleanup;

    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    const Scene::EntityUVE streamer =
        CreateEnabledStreamerAtUVE(entityManager, sceneGraph, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    const Scene::EntityUVE camera =
        CreateWatchingCameraAtUVE(entityManager, sceneGraph, Math::Vector3UVE{100.0F, 0.0F, 0.0F});
    engine.SetActiveCameraUVE(camera);

    const std::size_t baseline = entityManager.GetEntityCountUVE();
    engine.TickFrameUVE();
    {
        const Scene::LevelStreamer3DNodeComponentUVE& live =
            entityManager.GetComponentUVE<Scene::LevelStreamer3DNodeComponentUVE>(streamer);
        EXPECT_FALSE(live.loaded) << "a viewer 100 units out must never trigger the load";
        EXPECT_FALSE(live.loadRequested) << "loadRequested is false between ticks, always";
    }
    EXPECT_EQ(entityManager.GetEntityCountUVE(), baseline);

    // The camera is now effectively on top of the streamer: one tick streams the level in.
    sceneGraph.SetLocalTransformUVE(entityManager, camera, [&] {
        Scene::TransformComponentUVE t;
        t.localPosition = Math::Vector3UVE{0.5F, 0.0F, 0.0F};
        return t;
    }());
    engine.TickFrameUVE();
    {
        const Scene::LevelStreamer3DNodeComponentUVE& live =
            entityManager.GetComponentUVE<Scene::LevelStreamer3DNodeComponentUVE>(streamer);
        EXPECT_TRUE(live.loaded) << "a viewer inside the load radius streams the level in";
        EXPECT_FALSE(live.loadRequested);
    }
    EXPECT_EQ(entityManager.GetEntityCountUVE(), baseline + 2U)
        << "the two authored entities of the level document appeared";

    // Hysteresis: at 15 units (inside unload, outside load) a loaded level does nothing.
    sceneGraph.SetLocalTransformUVE(entityManager, camera, [&] {
        Scene::TransformComponentUVE t;
        t.localPosition = Math::Vector3UVE{15.0F, 0.0F, 0.0F};
        return t;
    }());
    engine.TickFrameUVE();
    EXPECT_TRUE(
        entityManager.GetComponentUVE<Scene::LevelStreamer3DNodeComponentUVE>(streamer).loaded)
        << "the band between 10 and 20 keeps a loaded level in place";
    EXPECT_EQ(entityManager.GetEntityCountUVE(), baseline + 2U);

    // Past the unload radius the subtree is destroyed - real content lifecycle, not a hidden flag.
    sceneGraph.SetLocalTransformUVE(entityManager, camera, [&] {
        Scene::TransformComponentUVE t;
        t.localPosition = Math::Vector3UVE{25.0F, 0.0F, 0.0F};
        return t;
    }());
    engine.TickFrameUVE();
    {
        const Scene::LevelStreamer3DNodeComponentUVE& live =
            entityManager.GetComponentUVE<Scene::LevelStreamer3DNodeComponentUVE>(streamer);
        EXPECT_FALSE(live.loaded) << "past the unload radius the level unloads";
        EXPECT_FALSE(live.loadRequested);
    }
    EXPECT_EQ(entityManager.GetEntityCountUVE(), baseline)
        << "the streamer's two loaded entities are destroyed on unload";
}

TEST(EngineCoreUVETest, LevelStreamer3D_CharacterControllerServesAsViewerWithoutAnyCamera) {
    // The viewer model is Frostbite listener-style: ANY character controller pulls content,
    // even in a world no camera ever sees.
    ASSERT_TRUE(WriteStreamerTestLevelFileUVE());
    StreamerTestCleanupUVE cleanup;

    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    const Scene::EntityUVE streamer =
        CreateEnabledStreamerAtUVE(entityManager, sceneGraph, Math::Vector3UVE{});
    const Scene::EntityUVE player = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, player, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::CharacterControllerComponentUVE>(player);

    const std::size_t baseline = entityManager.GetEntityCountUVE();
    engine.TickFrameUVE();
    EXPECT_TRUE(
        entityManager.GetComponentUVE<Scene::LevelStreamer3DNodeComponentUVE>(streamer).loaded)
        << "a standing controller with no active camera still streams the level in";
    EXPECT_EQ(entityManager.GetEntityCountUVE(), baseline + 2U);
}

TEST(EngineCoreUVETest, LevelStreamer3D_DisablingTheStreamerPullsItsContentWhileTheViewerWatches) {
    // The authored toggle beats distance, matching the Unreal convention a disabled streaming
    // volume stops holding its level - measured while the camera stands inside the load radius.
    ASSERT_TRUE(WriteStreamerTestLevelFileUVE());
    StreamerTestCleanupUVE cleanup;

    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    const Scene::EntityUVE streamer =
        CreateEnabledStreamerAtUVE(entityManager, sceneGraph, Math::Vector3UVE{});
    const Scene::EntityUVE camera =
        CreateWatchingCameraAtUVE(entityManager, sceneGraph, Math::Vector3UVE{1.0F, 0.0F, 0.0F});
    engine.SetActiveCameraUVE(camera);
    engine.TickFrameUVE();
    ASSERT_TRUE(
        entityManager.GetComponentUVE<Scene::LevelStreamer3DNodeComponentUVE>(streamer).loaded);
    const std::size_t loadedCount = entityManager.GetEntityCountUVE();

    entityManager.GetComponentUVE<Scene::LevelStreamer3DNodeComponentUVE>(streamer).enabled = false;
    engine.TickFrameUVE();
    EXPECT_FALSE(
        entityManager.GetComponentUVE<Scene::LevelStreamer3DNodeComponentUVE>(streamer).loaded)
        << "disabling unloads even with the viewer inside the load radius";
    EXPECT_EQ(entityManager.GetEntityCountUVE(), loadedCount - 2U)
        << "the disabled streamer's subtree is gone the same tick";
}

TEST(EngineCoreUVETest, LevelStreamer3D_FailedLoadLatchesClosedAndNeverRetriesInSession) {
    // The honest fail-closed path: levelPath points at a file that does not exist. The first
    // tick attempts and fails (loaded stays false, loadRequested stays false between ticks);
    // the latch means every later tick decides nothing either - never a retry storm - and the
    // streamer never marks itself loaded on an empty restore.
    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    const Scene::EntityUVE streamer = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, streamer, Scene::TransformComponentUVE{});
    Scene::LevelStreamer3DNodeComponentUVE component;
    component.levelPath = "uve_engine_core_streamer_file_that_does_not_exist.uvescene";
    component.loadDistance = 10.0F;
    component.unloadDistance = 20.0F;
    component.enabled = true;
    entityManager.AddComponentUVE<Scene::LevelStreamer3DNodeComponentUVE>(streamer, component);
    const Scene::EntityUVE camera =
        CreateWatchingCameraAtUVE(entityManager, sceneGraph, Math::Vector3UVE{1.0F, 0.0F, 0.0F});
    engine.SetActiveCameraUVE(camera);

    const std::size_t baseline = entityManager.GetEntityCountUVE();
    for (int tick = 0; tick < 3; ++tick) {
        engine.TickFrameUVE();
        const Scene::LevelStreamer3DNodeComponentUVE& live =
            entityManager.GetComponentUVE<Scene::LevelStreamer3DNodeComponentUVE>(streamer);
        EXPECT_FALSE(live.loaded) << "tick " << tick << ": a missing file never marks loaded";
        EXPECT_FALSE(live.loadRequested) << "tick " << tick << ": no request survives the tick";
        EXPECT_EQ(entityManager.GetEntityCountUVE(), baseline)
            << "tick " << tick << ": nothing materializes out of a missing file";
    }

    // The sibling with valid data is unaffected: a latch is per streamer, never global.
    ASSERT_TRUE(WriteStreamerTestLevelFileUVE());
    StreamerTestCleanupUVE cleanup;
    const Scene::EntityUVE good =
        CreateEnabledStreamerAtUVE(entityManager, sceneGraph, Math::Vector3UVE{});
    engine.TickFrameUVE();
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::LevelStreamer3DNodeComponentUVE>(good).loaded)
        << "a failed sibling never blocks an unrelated streamer";
    EXPECT_FALSE(
        entityManager.GetComponentUVE<Scene::LevelStreamer3DNodeComponentUVE>(streamer).loaded)
        << "the latched streamer stays closed even alongside a success";
}

TEST(EngineCoreUVETest, LevelStreamer3D_LoadBudgetCarriesOverflowIntoTheNextTick) {
    // The time-slicing contract: 5 identical streamers demand a load on the same tick with a
    // budget of 4 - exactly 4 stream in on tick one, and the straggler follows on tick two with
    // no other state stirring. This is the measurable answer to the hitch problem.
    ASSERT_TRUE(WriteStreamerTestLevelFileUVE());
    StreamerTestCleanupUVE cleanup;

    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    std::array<Scene::EntityUVE, 5U> streamers;
    for (Scene::EntityUVE& target : streamers) {
        target = CreateEnabledStreamerAtUVE(
            entityManager, sceneGraph, Math::Vector3UVE{100.0F, 0.0F, 0.0F});
    }
    const Scene::EntityUVE camera =
        CreateWatchingCameraAtUVE(entityManager, sceneGraph, Math::Vector3UVE{100.0F, 0.0F, 0.0F});
    engine.SetActiveCameraUVE(camera);

    const auto countLoaded = [&entityManager, &streamers]() -> std::size_t {
        std::size_t loaded = 0;
        for (const Scene::EntityUVE s : streamers) {
            if (entityManager.GetComponentUVE<Scene::LevelStreamer3DNodeComponentUVE>(s).loaded) {
                ++loaded;
            }
        }
        return loaded;
    };

    engine.TickFrameUVE();
    EXPECT_EQ(countLoaded(), Scene::kMaximumLevelStreamer3DLoadsPerTickUVE)
        << "the budget capped tick one, and the overflow stayed pending instead of dropping";
    engine.TickFrameUVE();
    EXPECT_EQ(countLoaded(), 5U) << "tick two consumed the carry-over - the straggler loads";
    engine.TickFrameUVE();
    EXPECT_EQ(countLoaded(), 5U) << "a further tick sits at steady state, nothing more moved";
}

namespace {

Scene::EntityUVE CreateReflectionProbeAtUVE(Scene::IEntityManagerUVE& entityManager,
                                             Scene::ISceneGraphUVE& sceneGraph,
                                             const Math::Vector3UVE& position,
                                              Scene::ReflectionProbeUpdateModeUVE updateMode) {
    const Scene::EntityUVE probe = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE transform;
    transform.localPosition = position;
    sceneGraph.AttachTransformUVE(entityManager, probe, transform);
    Scene::ReflectionProbe3DNodeComponentUVE component;
    component.updateMode = updateMode;
    entityManager.AddComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(probe, component);
    return probe;
}

std::uint32_t TotalProbeGenerationsUVE(Scene::IEntityManagerUVE& entityManager,
                                       std::span<const Scene::EntityUVE> probes) {
    std::uint32_t total = 0;
    for (const Scene::EntityUVE probe : probes) {
        total += entityManager.GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(probe)
                     .captureGeneration;
    }
    return total;
}

} // namespace

TEST(EngineCoreUVETest, ReflectionProbe3D_OnceCapturesOnFirstTickThenStaysSilentForever) {
    // The measurable answer to Unreal's "capture scene on load" convention: first Tick resolves
    // exactly one capture (capturedOnce flips, captureGeneration hits 1), every subsequent tick
    // touches nothing - no re-capture, no latch drift, cost paid exactly once.
    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    const Scene::EntityUVE probe = CreateReflectionProbeAtUVE(
        entityManager, sceneGraph, Math::Vector3UVE{}, Scene::ReflectionProbeUpdateModeUVE::Once);

    engine.TickFrameUVE();
    {
        const Scene::ReflectionProbe3DNodeComponentUVE& live =
            entityManager.GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(probe);
        EXPECT_TRUE(live.capturedOnce) << "the first tick resolves the one-and-only capture";
        EXPECT_EQ(live.captureGeneration, 1U);
    }
    engine.TickFrameUVE();
    engine.TickFrameUVE();
    const Scene::ReflectionProbe3DNodeComponentUVE& after =
        entityManager.GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(probe);
    EXPECT_EQ(after.captureGeneration, 1U) << "later ticks revisit nothing - once means once";
}

TEST(EngineCoreUVETest, ReflectionProbe3D_EveryFrameRecapturesOnlyWhileTheCameraIsInside) {
    // A camera parked inside the probe sees one fresh capture per tick; walking it outside the
    // influence box stops the churn COMPLETELY the same tick - the save Godot's Always mode
    // cannot express, measured as captureGeneration flatlines.
    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    const Scene::EntityUVE probe = CreateReflectionProbeAtUVE(
        entityManager, sceneGraph, Math::Vector3UVE{},
        Scene::ReflectionProbeUpdateModeUVE::EveryFrame);
    const Scene::EntityUVE camera =
        CreateWatchingCameraAtUVE(entityManager, sceneGraph, Math::Vector3UVE{});
    engine.SetActiveCameraUVE(camera);

    engine.TickFrameUVE();
    const std::uint32_t insideOne =
        entityManager.GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(probe)
            .captureGeneration;
    EXPECT_GE(insideOne, 1U) << "camera inside: the probe keeps its imagery current";
    engine.TickFrameUVE();
    const std::uint32_t insideTwo =
        entityManager.GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(probe)
            .captureGeneration;
    EXPECT_GT(insideTwo, insideOne) << "camera inside: each tick refreshes";

    // Walk out (default probe size 5 => half extent 2.5; 10 units is comfortably beyond) and the
    // meter falls dead on the very first outside tick.
    Scene::TransformComponentUVE farTransform;
    farTransform.localPosition = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, camera, farTransform);
    engine.TickFrameUVE();
    engine.TickFrameUVE();
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(probe)
                  .captureGeneration,
              insideTwo) << "camera outside: no capture at all - the eye sees nothing of it";
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(probe)
                        .cameraInfluenceWeight,
                    0.0F) << "outside the face the camera's blend weight reads exactly zero";
}

TEST(EngineCoreUVETest, ReflectionProbe3D_CameraInfluenceWeightTracksCameraPositionExactly) {
    // The first-class blend weight: dead center reads 1.0 (perfectly influential), halfway to the
    // face 0.5, and passing the face snaps to 0.0 - each measured through the public component
    // state, which is exactly the number a shading pass would blend with.
    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    Scene::ReflectionProbe3DNodeComponentUVE probeTemplate;
    probeTemplate.size = Math::Vector3UVE{4.0F, 4.0F, 4.0F}; // half extent 2 on every axis
    probeTemplate.updateMode = Scene::ReflectionProbeUpdateModeUVE::EveryFrame;
    const Scene::EntityUVE probe = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, probe, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(probe, probeTemplate);
    const Scene::EntityUVE camera =
        CreateWatchingCameraAtUVE(entityManager, sceneGraph, Math::Vector3UVE{});
    engine.SetActiveCameraUVE(camera);

    engine.TickFrameUVE();
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(probe)
                        .cameraInfluenceWeight,
                    1.0F) << "dead center: completely influential";

    Scene::TransformComponentUVE halfTransform;
    halfTransform.localPosition = Math::Vector3UVE{1.0F, 0.0F, 0.0F}; // halfway: 1/2 of half-extent
    sceneGraph.SetLocalTransformUVE(entityManager, camera, halfTransform);
    engine.TickFrameUVE();
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(probe)
                        .cameraInfluenceWeight,
                    0.5F) << "halfway: exactly the linear falloff midpoint";

    Scene::TransformComponentUVE faceTransform;
    faceTransform.localPosition = Math::Vector3UVE{2.0F, 0.0F, 0.0F}; // exactly ON the face
    sceneGraph.SetLocalTransformUVE(entityManager, camera, faceTransform);
    engine.TickFrameUVE();
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(probe)
                        .cameraInfluenceWeight,
                    0.0F) << "the face itself counts as outside - no floating sliver of weight";
}

TEST(EngineCoreUVETest, ReflectionProbe3D_OnDemandServicesTheLatchThenClearsIt) {
    // The inspector "Recapture" contract: latched, the next tick resolves exactly one capture
    // and CLEARS the latch - second tick does nothing, until latched again.
    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    const Scene::EntityUVE probe = CreateReflectionProbeAtUVE(
        entityManager, sceneGraph, Math::Vector3UVE{},
        Scene::ReflectionProbeUpdateModeUVE::OnDemand);

    engine.TickFrameUVE();
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(probe)
                  .captureGeneration,
              0U) << "without the latch OnDemand captures nothing";

    entityManager.GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(probe).updateRequested =
        true;
    engine.TickFrameUVE();
    {
        const Scene::ReflectionProbe3DNodeComponentUVE& live =
            entityManager.GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(probe);
        EXPECT_EQ(live.captureGeneration, 1U) << "the latch delivered exactly one capture";
        EXPECT_FALSE(live.updateRequested) << "and cleared itself the same tick it fired";
    }

    engine.TickFrameUVE();
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(probe)
                  .captureGeneration,
              1U) << "no latch, no capture - the probe waits for the next demand";
}

TEST(EngineCoreUVETest, ReflectionProbe3D_CaptureBudgetAgesOutOfStarvationNeverNearestOnly) {
    // Three EveryFrame probes with a budget of two, all containing the camera: naive nearest-
    // first would starve the farthest one forever under continuous demand, so priority is the
    // WAIT AGE first, distance second. Measured: tick one takes the two nearest by tie-break,
    // far one tick ages visibly (captureWaitTicks 1), and on tick two the aged waiter cuts the
    // line ahead of nearer-but-fresher askers.
    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    // Distances 0.5 / 1.0 / 1.5 on +X keep d^2 distinct (no ties), while the default box of
    // half extent 2.5 still contains the origin - all three demand a capture every tick.
    const Scene::EntityUVE nearProbe = CreateReflectionProbeAtUVE(
        entityManager, sceneGraph, Math::Vector3UVE{0.5F, 0.0F, 0.0F},
        Scene::ReflectionProbeUpdateModeUVE::EveryFrame);
    const Scene::EntityUVE midProbe = CreateReflectionProbeAtUVE(
        entityManager, sceneGraph, Math::Vector3UVE{1.0F, 0.0F, 0.0F},
        Scene::ReflectionProbeUpdateModeUVE::EveryFrame);
    const Scene::EntityUVE farProbe = CreateReflectionProbeAtUVE(
        entityManager, sceneGraph, Math::Vector3UVE{1.5F, 0.0F, 0.0F},
        Scene::ReflectionProbeUpdateModeUVE::EveryFrame);
    const Scene::EntityUVE camera =
        CreateWatchingCameraAtUVE(entityManager, sceneGraph, Math::Vector3UVE{});
    engine.SetActiveCameraUVE(camera);
    const std::array<Scene::EntityUVE, 3U> probes{nearProbe, midProbe, farProbe};

    engine.TickFrameUVE();
    EXPECT_EQ(TotalProbeGenerationsUVE(entityManager, probes),
              Scene::kMaximumReflectionProbeCapturesPerTickUVE)
        << "tick one serves exactly the budget, never the whole queue";
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(nearProbe)
                  .captureGeneration,
              1U);
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(midProbe)
                  .captureGeneration,
              1U);
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(farProbe)
                  .captureGeneration,
              0U) << "the farthest one waits (distances tie-free)";
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(farProbe)
                  .captureWaitTicks,
              1U) << "and its wait age shows in its own component state";

    engine.TickFrameUVE();
    // Tick two: far now beats both nearer probes by age and captures ahead of them.
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(farProbe)
                  .captureGeneration,
              1U) << "the aged waiter cuts the line - starvation is impossible";
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(farProbe)
                  .captureWaitTicks,
              0U) << "service resets the age";
    EXPECT_EQ(TotalProbeGenerationsUVE(entityManager, probes),
              2U * Scene::kMaximumReflectionProbeCapturesPerTickUVE)
        << "every tick still stays within budget";
}

namespace {

Scene::EntityUVE CreateWorldPartitionAtUVE(Scene::IEntityManagerUVE& entityManager,
                                           Scene::ISceneGraphUVE& sceneGraph,
                                           const Math::Vector3UVE& position,
                                           const float cellSize,
                                           const std::array<std::uint32_t, 3U>& counts,
                                           const std::uint32_t maximumLoadedCells) {
    const Scene::EntityUVE partition = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE transform;
    transform.localPosition = position;
    sceneGraph.AttachTransformUVE(entityManager, partition, transform);
    Scene::WorldPartition3DNodeComponentUVE component;
    component.cellSize = cellSize;
    component.cellCounts = counts;
    component.maximumLoadedCells = maximumLoadedCells;
    entityManager.AddComponentUVE<Scene::WorldPartition3DNodeComponentUVE>(partition, component);
    return partition;
}

Scene::EntityUVE CreatePartitionChildAtUVE(Scene::IEntityManagerUVE& entityManager,
                                           Scene::ISceneGraphUVE& sceneGraph,
                                           const Scene::EntityUVE parent,
                                           const Math::Vector3UVE& position,
                                           const bool withMesh) {
    const Scene::EntityUVE child = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE transform;
    transform.localPosition = position;
    sceneGraph.AttachTransformUVE(entityManager, child, transform);
    sceneGraph.SetParentUVE(entityManager, child, parent);
    if (withMesh) {
        entityManager.AddComponentUVE<Scene::MeshComponentUVE>(child, Scene::MeshComponentUVE{});
    }
    return child;
}

} // namespace

TEST(EngineCoreUVETest, WorldPartition3D_MembershipAttachesOnlyToMeshesAndOutsideStaysUnmanaged) {
    // The engine-owned runtime state attaches to mesh-carrying descendants only (a transform-only
    // child must stay clean), the partition beyond its volume is nobody's business (membership
    // there exists but its live verdict is always true - volume-rejection must never hide
    // geometry), and loadedCellCount only counts cells actually inside the volume.
    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    const Scene::EntityUVE partition = CreateWorldPartitionAtUVE(
        entityManager, sceneGraph, Math::Vector3UVE{}, 10.0F, {2U, 1U, 2U}, 2U);
    const Scene::EntityUVE insideMesh =
        CreatePartitionChildAtUVE(entityManager, sceneGraph, partition,
                                  Math::Vector3UVE{5.0F, 0.0F, 5.0F}, /*withMesh=*/true);
    const Scene::EntityUVE meshlessChild =
        CreatePartitionChildAtUVE(entityManager, sceneGraph, partition,
                                  Math::Vector3UVE{5.0F, 0.0F, 5.0F}, /*withMesh=*/false);
    const Scene::EntityUVE outsideMesh =
        CreatePartitionChildAtUVE(entityManager, sceneGraph, partition,
                                  Math::Vector3UVE{500.0F, 0.0F, 500.0F}, /*withMesh=*/true);
    const Scene::EntityUVE camera =
        CreateWatchingCameraAtUVE(entityManager, sceneGraph, Math::Vector3UVE{5.0F, 0.0F, 5.0F});
    engine.SetActiveCameraUVE(camera);

    engine.TickFrameUVE();

    ASSERT_TRUE(entityManager.HasComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
        insideMesh));
    const Scene::WorldPartition3DMembershipComponentUVE& insideMembership =
        entityManager.GetComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(insideMesh);
    EXPECT_TRUE(insideMembership.live) << "inside cell (0,0,0) with budget 2: live";
    EXPECT_EQ(insideMembership.partition, partition) << "the engine stamps the deciding owner";

    EXPECT_FALSE(entityManager.HasComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
        meshlessChild)) << "no mesh, no membership - authoritatively not every descendant";

    ASSERT_TRUE(entityManager.HasComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
        outsideMesh));
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
                    outsideMesh)
                    .live)
        << "outside the partition volume: never culled by a partition that cannot see it";

    EXPECT_EQ(entityManager.GetComponentUVE<Scene::WorldPartition3DNodeComponentUVE>(partition)
                  .loadedCellCount,
              1U) << "the unmanaged outside point creates no occupied cell";
}

TEST(EngineCoreUVETest, WorldPartition3D_BudgetAdmitsNearestCellOnlyAndCameraMoveFlipsTheVerdict) {
    // maximumLoadedCells is the hard contract: with one slot and two occupied cells, only the
    // serially-nearest cell's member renders - move the camera to the other cell and the verdict
    // flips seat afterwards, deterministically (nothing depends on iteration order because the
    // distance key breaks the tie cleanly).
    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    const Scene::EntityUVE partition = CreateWorldPartitionAtUVE(
        entityManager, sceneGraph, Math::Vector3UVE{}, 10.0F, {2U, 1U, 1U}, 1U);
    const Scene::EntityUVE nearMesh =
        CreatePartitionChildAtUVE(entityManager, sceneGraph, partition,
                                  Math::Vector3UVE{5.0F, 0.0F, 0.0F}, /*withMesh=*/true);
    const Scene::EntityUVE farMesh =
        CreatePartitionChildAtUVE(entityManager, sceneGraph, partition,
                                  Math::Vector3UVE{15.0F, 0.0F, 0.0F}, /*withMesh=*/true);
    const Scene::EntityUVE camera =
        CreateWatchingCameraAtUVE(entityManager, sceneGraph, Math::Vector3UVE{});
    engine.SetActiveCameraUVE(camera);

    engine.TickFrameUVE();
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
                    nearMesh)
                    .live) << "cell (0,0,0): the nearest occupied cell earns the slot";
    EXPECT_FALSE(entityManager.GetComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
                     farMesh)
                     .live) << "cell (1,0,0): out of budget, out of the frame";
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::WorldPartition3DNodeComponentUVE>(partition)
                  .loadedCellCount,
              1U);

    Scene::TransformComponentUVE farCameraTransform;
    farCameraTransform.localPosition = Math::Vector3UVE{15.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, camera, farCameraTransform);
    engine.TickFrameUVE();
    EXPECT_FALSE(entityManager.GetComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
                     nearMesh)
                     .live) << "the verdict flips with the camera, not with sentiment";
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
                    farMesh)
                    .live);
}

TEST(EngineCoreUVETest, WorldPartition3D_NestedPartitionsTheInnerOneOwnsItsSubtree) {
    // Closest-ancestor-wins: a partition inside another partition manages its own subtree, and
    // nothing the outer walk touches ever rewrites that decision - measured as the inner child's
    // stamped owner being the INNER partition, not the outer.
    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    const Scene::EntityUVE outer = CreateWorldPartitionAtUVE(
        entityManager, sceneGraph, Math::Vector3UVE{}, 50.0F, {2U, 1U, 1U}, 2U);
    const Scene::EntityUVE inner = CreateWorldPartitionAtUVE(
        entityManager, sceneGraph, Math::Vector3UVE{}, 10.0F, {2U, 1U, 1U}, 2U);
    sceneGraph.SetParentUVE(entityManager, inner, outer);
    const Scene::EntityUVE innerMesh =
        CreatePartitionChildAtUVE(entityManager, sceneGraph, inner,
                                  Math::Vector3UVE{5.0F, 0.0F, 0.0F}, /*withMesh=*/true);
    const Scene::EntityUVE outerMesh =
        CreatePartitionChildAtUVE(entityManager, sceneGraph, outer,
                                  Math::Vector3UVE{25.0F, 0.0F, 0.0F}, /*withMesh=*/true);
    const Scene::EntityUVE camera =
        CreateWatchingCameraAtUVE(entityManager, sceneGraph, Math::Vector3UVE{});
    engine.SetActiveCameraUVE(camera);

    engine.TickFrameUVE();
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
                  innerMesh)
                  .partition,
              inner) << "the inner partition decides its own subtree";
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
                  outerMesh)
                  .partition,
              outer) << "the outer one only ever touches its own descendants";
}

TEST(EngineCoreUVETest, WorldPartition3D_DisabledReleasesEveryMemberAndClearsTheLiveCount) {
    // The toggle must show EVERYTHING, because leaving a stale fade behind after switching the
    // system off is the honest failure mode exactly backwards - fail open, measured everywhere.
    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    const Scene::EntityUVE partition = CreateWorldPartitionAtUVE(
        entityManager, sceneGraph, Math::Vector3UVE{}, 10.0F, {2U, 1U, 1U}, 1U);
    const Scene::EntityUVE farMesh =
        CreatePartitionChildAtUVE(entityManager, sceneGraph, partition,
                                  Math::Vector3UVE{15.0F, 0.0F, 0.0F}, /*withMesh=*/true);
    static_cast<void>(CreatePartitionChildAtUVE(entityManager, sceneGraph, partition,
                                                Math::Vector3UVE{5.0F, 0.0F, 0.0F},
                                                /*withMesh=*/true));
    const Scene::EntityUVE camera =
        CreateWatchingCameraAtUVE(entityManager, sceneGraph, Math::Vector3UVE{});
    engine.SetActiveCameraUVE(camera);

    engine.TickFrameUVE();
    ASSERT_FALSE(entityManager.GetComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
                     farMesh)
                     .live)
        << "setup: budget 1, far cell out of budget";

    entityManager.GetComponentUVE<Scene::WorldPartition3DNodeComponentUVE>(partition).enabled =
        false;
    engine.TickFrameUVE();
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
                    farMesh)
                    .live)
        << "disabled: every member renders, no stale fade lingers";
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::WorldPartition3DNodeComponentUVE>(partition)
                  .loadedCellCount,
              0U) << "and the live cell count zeroes instead of freezing";
}

TEST(EngineCoreUVETest, WorldPartition3D_MembershipFollowsSubtreeGrowthNotStaleSnapshots) {
    // The state lives in the sync, not in a one-shot: a mesh added between ticks earns its
    // membership on the NEXT tick, and destroying a member mid-run leaves nothing stale behind -
    // measured as the live-set moving exactly with the mutate-then-tick cadence.
    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    const Scene::EntityUVE partition = CreateWorldPartitionAtUVE(
        entityManager, sceneGraph, Math::Vector3UVE{}, 10.0F, {2U, 1U, 1U}, 2U);
    const Scene::EntityUVE camera =
        CreateWatchingCameraAtUVE(entityManager, sceneGraph, Math::Vector3UVE{});
    engine.SetActiveCameraUVE(camera);

    engine.TickFrameUVE();
    const Scene::EntityUVE added =
        CreatePartitionChildAtUVE(entityManager, sceneGraph, partition,
                                  Math::Vector3UVE{5.0F, 0.0F, 0.0F}, /*withMesh=*/true);
    EXPECT_FALSE(entityManager.HasComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
        added)) << "mid-tick birth: nothing attaches before the follow-up tick's sync";

    engine.TickFrameUVE();
    ASSERT_TRUE(entityManager.HasComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
        added));
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
                    added)
                    .live);

    // Destroy it and the partition must still tick cleanly with nothing stale behind.
    entityManager.DestroyEntityUVE(added);
    engine.TickFrameUVE();
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::WorldPartition3DNodeComponentUVE>(partition)
                  .loadedCellCount,
              0U) << "a dead member unbooks its whole cell - no ghost occupancy";
}

namespace {

Scene::EntityUVE CreateVisibilityRegionAtUVE(Scene::IEntityManagerUVE& entityManager,
                                             Scene::ISceneGraphUVE& sceneGraph,
                                             const Math::Vector3UVE& position,
                                             const Math::Vector3UVE& halfExtents,
                                             const std::uint32_t layers) {
    const Scene::EntityUVE region = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE transform;
    transform.localPosition = position;
    sceneGraph.AttachTransformUVE(entityManager, region, transform);
    Scene::VisibilityRegion3DNodeComponentUVE component;
    component.halfExtents = halfExtents;
    component.visibilityLayers = layers;
    entityManager.AddComponentUVE<Scene::VisibilityRegion3DNodeComponentUVE>(region, component);
    return region;
}

Scene::EntityUVE CreateStandaloneMeshAtUVE(Scene::IEntityManagerUVE& entityManager,
                                           Scene::ISceneGraphUVE& sceneGraph,
                                           const Math::Vector3UVE& position,
                                           const std::uint32_t layers) {
    const Scene::EntityUVE mesh = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE transform;
    transform.localPosition = position;
    sceneGraph.AttachTransformUVE(entityManager, mesh, transform);
    Scene::MeshComponentUVE component{};
    component.visibilityLayers = layers;
    entityManager.AddComponentUVE<Scene::MeshComponentUVE>(mesh, component);
    return mesh;
}

} // namespace

TEST(EngineCoreUVETest, VisibilityRegion3D_CameraOutsideTheRoomSkipsItsInteriorCameraInsideShowsIt) {
    // The headline claim Godot cannot answer: interior contents cost zero render while nobody
    // stands inside the room, and reappear the tick the eye enters - with the region's own
    // active flag readable as the same verdict the membership carries.
    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    static_cast<void>(CreateVisibilityRegionAtUVE(entityManager, sceneGraph, Math::Vector3UVE{},
                                                  Math::Vector3UVE{5.0F, 5.0F, 5.0F},
                                                  0xFFFFFFFFU));
    const Scene::EntityUVE content = CreateStandaloneMeshAtUVE(
        entityManager, sceneGraph, Math::Vector3UVE{1.0F, 0.0F, 0.0F}, 0x00000001U);
    const Scene::EntityUVE outside = CreateStandaloneMeshAtUVE(
        entityManager, sceneGraph, Math::Vector3UVE{50.0F, 0.0F, 0.0F}, 0x00000001U);
    const Scene::EntityUVE camera =
        CreateWatchingCameraAtUVE(entityManager, sceneGraph, Math::Vector3UVE{50.0F, 0.0F, 0.0F});
    engine.SetActiveCameraUVE(camera);

    engine.TickFrameUVE();
    ASSERT_TRUE(entityManager.HasComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
        content));
    EXPECT_FALSE(entityManager.GetComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
                     content)
                     .live)
        << "camera outside: interior content is skipped with zero render work";
    EXPECT_FALSE(entityManager.HasComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
        outside)) << "content outside every region is not a member of anything";

    Scene::TransformComponentUVE enterTransform;
    enterTransform.localPosition = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, camera, enterTransform);
    engine.TickFrameUVE();
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
                    content)
                    .live)
        << "the tick the eye steps in, the room draws again - no one-frame stale dark";
}

TEST(EngineCoreUVETest, VisibilityRegion3D_LayerGateKeepsUninvitedMeshesOutOfTheRoom) {
    // The roadmap's own phrase: the REGION's visibilityLayers gate what renders inside it. A
    // props-layer mesh is managed by the props region; an NPC on the default layer standing in
    // the same room is NOT, so nobody has to special-node the walk-through cases.
    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    static_cast<void>(CreateVisibilityRegionAtUVE(entityManager, sceneGraph, Math::Vector3UVE{},
                                                  Math::Vector3UVE{5.0F, 5.0F, 5.0F},
                                                  0x00000002U)); // props only
    const Scene::EntityUVE prop = CreateStandaloneMeshAtUVE(
        entityManager, sceneGraph, Math::Vector3UVE{1.0F, 0.0F, 0.0F}, 0x00000002U);
    const Scene::EntityUVE npc = CreateStandaloneMeshAtUVE(
        entityManager, sceneGraph, Math::Vector3UVE{-1.0F, 0.0F, 0.0F}, 0x00000001U);
    const Scene::EntityUVE camera =
        CreateWatchingCameraAtUVE(entityManager, sceneGraph, Math::Vector3UVE{50.0F, 0.0F, 0.0F});
    engine.SetActiveCameraUVE(camera);

    engine.TickFrameUVE();
    ASSERT_TRUE(entityManager.HasComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
        prop));
    EXPECT_FALSE(entityManager.GetComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
                     prop)
                     .live)
        << "same room, right layer: skipped while the room is empty";
    EXPECT_FALSE(entityManager.HasComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
        npc)) << "same room, wrong layer: the room simply does not claim this mesh";
}

TEST(EngineCoreUVETest, VisibilityRegion3D_WalkingOutReleasesTheVerdictOnTheVeryNextTick) {
    // The regression that decides this slice is honest: a member that LEAVES the box must not
    // keep last tick's fade. Membership refreshes per tick, and the live flag goes back to true
    // the same tick the mesh crosses out of the room - never a frame of dark behind it.
    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    static_cast<void>(CreateVisibilityRegionAtUVE(entityManager, sceneGraph, Math::Vector3UVE{},
                                                  Math::Vector3UVE{5.0F, 5.0F, 5.0F},
                                                  0xFFFFFFFFU));
    const Scene::EntityUVE content = CreateStandaloneMeshAtUVE(
        entityManager, sceneGraph, Math::Vector3UVE{1.0F, 0.0F, 0.0F}, 0x00000001U);
    const Scene::EntityUVE camera =
        CreateWatchingCameraAtUVE(entityManager, sceneGraph, Math::Vector3UVE{50.0F, 0.0F, 0.0F});
    engine.SetActiveCameraUVE(camera);

    engine.TickFrameUVE();
    ASSERT_FALSE(entityManager.GetComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
                     content)
                     .live)
        << "setup: inside an inactive room, skipped";

    Scene::TransformComponentUVE doorTransform;
    doorTransform.localPosition = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, content, doorTransform);
    engine.TickFrameUVE();
    ASSERT_TRUE(entityManager.HasComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
        content))
        << "the engine leaves the marker; the VERDICT is what is released";
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
                    content)
                    .live) << "out of the room: rendered again the very next tick, not someday";
}

TEST(EngineCoreUVETest, VisibilityRegion3D_DisabledReleasesAndDestroyingTheRegionRehomesItsMembers) {
    // Two fail-open paths at once: disabling a region shows its content from the next tick, and
    // destroying the region lets the membership rehome to ANOTHER containing region (or nobody)
    // without a stale fade ever binding the mesh again.
    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    const Scene::EntityUVE roomA =
        CreateVisibilityRegionAtUVE(entityManager, sceneGraph, Math::Vector3UVE{},
                                    Math::Vector3UVE{5.0F, 5.0F, 5.0F}, 0xFFFFFFFFU);
    const Scene::EntityUVE content = CreateStandaloneMeshAtUVE(
        entityManager, sceneGraph, Math::Vector3UVE{1.0F, 0.0F, 0.0F}, 0x00000001U);
    const Scene::EntityUVE camera =
        CreateWatchingCameraAtUVE(entityManager, sceneGraph, Math::Vector3UVE{50.0F, 0.0F, 0.0F});
    engine.SetActiveCameraUVE(camera);

    engine.TickFrameUVE();
    ASSERT_FALSE(entityManager.GetComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
                     content)
                     .live);

    entityManager.GetComponentUVE<Scene::VisibilityRegion3DNodeComponentUVE>(roomA).enabled =
        false;
    engine.TickFrameUVE();
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
                    content)
                    .live) << "disabled: released the same tick, no stale fade";

    // Re-enable, then destroy: the membership rebrands to kInvalidEntityUVE and stays live.
    entityManager.GetComponentUVE<Scene::VisibilityRegion3DNodeComponentUVE>(roomA).enabled =
        true;
    engine.TickFrameUVE();
    ASSERT_FALSE(entityManager.GetComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
                     content)
                     .live);
    entityManager.DestroyEntityUVE(roomA);
    engine.TickFrameUVE();
    const Scene::VisibilityRegion3DMembershipComponentUVE& membership =
        entityManager.GetComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(content);
    EXPECT_TRUE(membership.live)
        << "dead region: fail open on the render path AND in the recorded verdict";
    EXPECT_EQ(membership.region, Scene::kInvalidEntityUVE)
        << "ownership is reassigned - content is rehomeable, never orphan-hidden";
}

TEST(EngineCoreUVETest, VisibilityRegion3D_OverlappingRoomsTheNearestCenterOwnsTheMesh) {
    // Overlapping rooms need ONE deterministic owner per mesh; otherwise the culling of a double-
    // booked room was history-dependent. Measured: the nearest region center stamps it, and a
    // second tick answers the same way without oscillation.
    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    static_cast<void>(CreateVisibilityRegionAtUVE(entityManager, sceneGraph, Math::Vector3UVE{},
                                                  Math::Vector3UVE{6.0F, 6.0F, 6.0F},
                                                  0xFFFFFFFFU)); // big, centered at 0
    const Scene::EntityUVE small =
        CreateVisibilityRegionAtUVE(entityManager, sceneGraph,
                                    Math::Vector3UVE{4.0F, 0.0F, 0.0F},
                                    Math::Vector3UVE{2.0F, 2.0F, 2.0F}, 0xFFFFFFFFU);
    const Scene::EntityUVE content = CreateStandaloneMeshAtUVE(
        entityManager, sceneGraph, Math::Vector3UVE{4.5F, 0.0F, 0.0F}, 0x00000001U);
    const Scene::EntityUVE camera =
        CreateWatchingCameraAtUVE(entityManager, sceneGraph, Math::Vector3UVE{50.0F, 0.0F, 0.0F});
    engine.SetActiveCameraUVE(camera);

    engine.TickFrameUVE();
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
                  content)
                  .region,
              small) << "inside both rooms: the NEAREST center owns the mesh";
    engine.TickFrameUVE();
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
                  content)
                  .region,
              small) << "second tick: same owner, no oscillation between overlapping rooms";
}

TEST(EngineCoreUVETest, VisibilityRegion3D_NoViewersAtAllFailsOpenWithActiveRegions) {
    // Empty worlds show everything: with no camera and no controller, regions report active and
    // their members report live. The fail-open belongs to the POLICY (the sync), while the pure
    // function stays measurable - asserted separately in the node tests.
    EngineConfigUVE config = MakeTestConfigUVE();
    EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

    const Scene::EntityUVE room =
        CreateVisibilityRegionAtUVE(entityManager, sceneGraph, Math::Vector3UVE{},
                                    Math::Vector3UVE{5.0F, 5.0F, 5.0F}, 0xFFFFFFFFU);
    const Scene::EntityUVE content = CreateStandaloneMeshAtUVE(
        entityManager, sceneGraph, Math::Vector3UVE{1.0F, 0.0F, 0.0F}, 0x00000001U);

    engine.TickFrameUVE();
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::VisibilityRegion3DNodeComponentUVE>(room)
                    .active)
        << "no viewer anywhere: active, because managing nothing hides nothing";
    ASSERT_TRUE(entityManager.HasComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
        content));
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
                    content)
                    .live);
}

} // namespace
} // namespace UVE::Core::Tests
