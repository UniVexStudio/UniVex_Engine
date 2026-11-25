//                                      UVE
//                                UniVex Engine
//
// UniVex Engine (UVE) — Proprietary Game Engine
// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// Unauthorized copying, modification, distribution, or use of this code
// in whole or in part is strictly prohibited without express written
// permission from UniVex Studios.
// Violators will be prosecuted to the fullest extent of the law.


#pragma once

#include "i_asset_bundle.h"
#include "i_asset_database.h"
#include "i_asset_importer.h"
#include "i_asset_import_queue.h"
#include "i_asset_manager.h"
#include "i_derived_artifact_cache.h"
#include "i_file_system.h"
#include "i_hot_reload.h"
#include "i_project_file_index.h"
#include "i_project_change_watcher.h"
#include "i_audio_device.h"
#include "i_audio_source_system.h"
#include "i_audio_system.h"
#include "i_command_line.h"
#include "i_config_manager.h"
#include "settings_document.h"
#include "input_map_document.h"
#include "i_logger.h"
#include "i_event_system.h"
#include "i_gamepad_input_system.h"
#include "i_input_system.h"
#include "i_mobile_gesture_system.h"
#include "i_mobile_input_system.h"
#include "i_memory_manager.h"
#include "i_collision_system.h"
#include "i_physics_system.h"
#include "i_physics_query_system.h"
#include "physics_constraint_system.h"
#include "i_raycast_system.h"
#include "i_camera_system.h"
#include "i_compute_system.h"
#include "i_light_system.h"
#include "i_mesh_renderer.h"
#include "i_render_device.h"
#include "i_render_system.h"
#include "i_renderer_3d.h"
#include "i_shader_manager.h"
#include "i_checkpoint_manager.h"
#include "i_save_game_system.h"
#include "i_entity_manager.h"
#include "i_particle_runtime.h"
#include "i_prefab_system.h"
#include "i_scene_graph.h"
#include "i_scene_serializer.h"
#include "i_thread_pool.h"
#include "i_timer.h"
#include "i_window_manager.h"

namespace UVE::VFX {
class VfxWorld;
} // namespace UVE::VFX

namespace UVE::Core {

/// EngineServices is the engine's central dependency-provider / service
/// container: a small bundle of references to the core engine services
/// (Logger, Timer, EventSystem, MemoryManager, ThreadPool, CommandLine,
/// ConfigManager, EntityManager, SceneGraph, AssetDatabase, ProjectFileIndex, DerivedArtifactCache,
/// ProjectChangeWatcher, SceneSerializer, PrefabSystem, HotReload, AssetManager, AssetImporter, AssetImportQueue, AssetBundle,
/// FileSystem, RenderDevice, ShaderManager, RenderSystem, ComputeSystem, CameraSystem,
/// MeshRenderer, LightSystem, Renderer3D, CollisionSystem, PhysicsSystem,
/// PhysicsQuerySystem, RaycastSystem, PhysicsConstraintSystem, InputSystem, GamepadInputSystem, MobileInputSystem, MobileGestureSystem, AudioDevice, AudioSystem, AudioSourceSystem,
/// SaveGameSystem, CheckpointManager, WindowManager, ParticleRuntime), built once
/// EngineCore has constructed all forty. Any future subsystem that
/// needs access to one of these should receive an EngineServices&
/// (obtained from EngineCore::GetServices()) rather than a raw global
/// pointer — the logging macros' internal active-instance pointer remains
/// the one intentional exception to that rule (see
/// docs/CODING_STANDARDS.md).
/// Referenced through the
/// ILogger/ITimer/IEventSystem/IMemoryManager/IThreadPool/
/// ICommandLine/IConfigManager/IEntityManager/ISceneGraph/
/// IAssetDatabase/IProjectFileIndex/IDerivedArtifactCache/IProjectChangeWatcher/ISceneSerializer/IPrefabSystem/IHotReload/
/// IAssetManager/IAssetImporter/IAssetImportQueue/IAssetBundle/IFileSystem/
/// IRenderDevice/Shader::IShaderManager/IRenderSystem/IComputeSystem/
/// ICameraSystem/IMeshRenderer/ILightSystem/
/// IRenderer3D/ICollisionSystem/IPhysicsSystem/IPhysicsQuerySystem/
/// IRaycastSystem/PhysicsConstraintSystem/IInputSystem/IGamepadInputSystem/IMobileInputSystem/
/// IMobileGestureSystem/IAudioDevice/IAudioSystem/IAudioSourceSystem/
/// ISaveGameSystem/ICheckpointManager/IWindowManager/IParticleRuntime interfaces,
/// not the concrete types, so a future substitute implementation of any of
/// the forty requires no change here.
/// Thread-safety: EngineServices itself holds only non-owning pointers
/// and has no mutable state of its own; the thread-safety of each accessor
/// is whatever the referenced service documents.
class EngineServices final {
public:
    EngineServices(Debug::ILogger& logger, Utilities::ITimer& timer,
                       Events::IEventSystem& eventSystem,
                       Memory::IMemoryManager& memoryManager,
                       Threading::IThreadPool& threadPool,
                       CommandLine::ICommandLine& commandLine,
                       Config::IConfigManager& configManager,
                       Config::SettingsDocument& projectSettings,
                       Core::InputMapDocument& inputMap,
                       Scene::IEntityManager& entityManager,
                       Scene::ISceneGraph& sceneGraph,
                       Asset::IAssetDatabase& assetDatabase,
                       Asset::IProjectFileIndex& projectFileIndex,
                       Asset::IDerivedArtifactCache& derivedArtifactCache,
                       Asset::IProjectChangeWatcher& projectChangeWatcher,
                       Scene::ISceneSerializer& sceneSerializer,
                       Scene::IPrefabSystem& prefabSystem,
                       Scene::IParticleRuntime& particleRuntime,
                       Asset::IHotReload& hotReload,
                       Asset::IAssetManager& assetManager,
                       Asset::IAssetImporter& assetImporter,
                       Asset::IAssetImportQueue& assetImportQueue,
                       Asset::IAssetBundle& assetBundle,
                       Asset::IFileSystem& fileSystem,
                       Render::IRenderDevice& renderDevice,
                       Render::Shader::IShaderManager& shaderManager,
                       Render::IRenderSystem& renderSystem,
                       Render::IComputeSystem& computeSystem,
                       Render::ICameraSystem& cameraSystem,
                       Render::IMeshRenderer& meshRenderer,
                       Render::ILightSystem& lightSystem,
                       Render::IRenderer3D& renderer3D,
                       Physics::ICollisionSystem& collisionSystem,
                       Physics::IPhysicsSystem& physicsSystem,
                       Physics::IPhysicsQuerySystem& physicsQuerySystem,
                       Physics::IRaycastSystem& raycastSystem,
                       Physics::PhysicsConstraintSystem& physicsConstraintSystem,
                       Input::IInputSystem& inputSystem,
                       Input::IGamepadInputSystem& gamepadInputSystem,
                       Input::IMobileInputSystem& mobileInputSystem,
                       Input::IMobileGestureSystem& mobileGestureSystem,
                       Audio::IAudioDevice& audioDevice,
                       Audio::IAudioSystem& audioSystem,
                       Audio::IAudioSourceSystem& audioSourceSystem,
                       Save::ISaveGameSystem& saveGameSystem,
                       Save::ICheckpointManager& checkpointManager,
                       Window::IWindowManager& windowManager) noexcept;

    [[nodiscard]] Debug::ILogger& GetLogger() const noexcept;
    [[nodiscard]] Utilities::ITimer& GetTimer() const noexcept;
    [[nodiscard]] Events::IEventSystem& GetEventSystem() const noexcept;
    [[nodiscard]] Memory::IMemoryManager& GetMemoryManager() const noexcept;
    [[nodiscard]] Threading::IThreadPool& GetThreadPool() const noexcept;
    [[nodiscard]] CommandLine::ICommandLine& GetCommandLine() const noexcept;
    [[nodiscard]] Config::IConfigManager& GetConfigManager() const noexcept;
    /// The project's settings (project.uvsettings): the settings that belong to the project and
    /// ship with it, as opposed to one person's editor preferences in GetConfigManager().
    [[nodiscard]] Config::SettingsDocument& GetProjectSettings() const noexcept;
    /// The project's input map (project.uvinput): its named actions and their bindings.
    [[nodiscard]] Core::InputMapDocument& GetInputMap() const noexcept;
    [[nodiscard]] Scene::IEntityManager& GetEntityManager() const noexcept;
    [[nodiscard]] Scene::ISceneGraph& GetSceneGraph() const noexcept;
    [[nodiscard]] Asset::IAssetDatabase& GetAssetDatabase() const noexcept;
    [[nodiscard]] Asset::IProjectFileIndex& GetProjectFileIndex() const noexcept;
    [[nodiscard]] Asset::IDerivedArtifactCache& GetDerivedArtifactCache() const noexcept;
    [[nodiscard]] Asset::IProjectChangeWatcher& GetProjectChangeWatcher() const noexcept;
    [[nodiscard]] Scene::ISceneSerializer& GetSceneSerializer() const noexcept;
    [[nodiscard]] Scene::IPrefabSystem& GetPrefabSystem() const noexcept;
    [[nodiscard]] Scene::IParticleRuntime& GetParticleRuntime() const noexcept;
    [[nodiscard]] Asset::IHotReload& GetHotReload() const noexcept;
    [[nodiscard]] Asset::IAssetManager& GetAssetManager() const noexcept;
    [[nodiscard]] Asset::IAssetImporter& GetAssetImporter() const noexcept;
    [[nodiscard]] Asset::IAssetImportQueue& GetAssetImportQueue() const noexcept;
    [[nodiscard]] Asset::IAssetBundle& GetAssetBundle() const noexcept;
    [[nodiscard]] Asset::IFileSystem& GetFileSystem() const noexcept;
    [[nodiscard]] Render::IRenderDevice& GetRenderDevice() const noexcept;
    [[nodiscard]] Render::Shader::IShaderManager& GetShaderManager() const noexcept;
    [[nodiscard]] Render::IRenderSystem& GetRenderSystem() const noexcept;
    [[nodiscard]] Render::IComputeSystem& GetComputeSystem() const noexcept;
    [[nodiscard]] Render::ICameraSystem& GetCameraSystem() const noexcept;
    [[nodiscard]] Render::IMeshRenderer& GetMeshRenderer() const noexcept;
    [[nodiscard]] Render::ILightSystem& GetLightSystem() const noexcept;
    [[nodiscard]] Render::IRenderer3D& GetRenderer3D() const noexcept;
    [[nodiscard]] Physics::ICollisionSystem& GetCollisionSystem() const noexcept;
    [[nodiscard]] Physics::IPhysicsSystem& GetPhysicsSystem() const noexcept;
    [[nodiscard]] Physics::IPhysicsQuerySystem& GetPhysicsQuerySystem() const noexcept;
    [[nodiscard]] Physics::IRaycastSystem& GetRaycastSystem() const noexcept;
    [[nodiscard]] Physics::PhysicsConstraintSystem& GetPhysicsConstraintSystem() const noexcept;
    [[nodiscard]] Input::IInputSystem& GetInputSystem() const noexcept;
    [[nodiscard]] Input::IGamepadInputSystem& GetGamepadInputSystem() const noexcept;
    [[nodiscard]] Input::IMobileInputSystem& GetMobileInputSystem() const noexcept;
    [[nodiscard]] Input::IMobileGestureSystem& GetMobileGestureSystem() const noexcept;
    [[nodiscard]] Audio::IAudioDevice& GetAudioDevice() const noexcept;
    [[nodiscard]] Audio::IAudioSystem& GetAudioSystem() const noexcept;
    [[nodiscard]] Audio::IAudioSourceSystem& GetAudioSourceSystem() const noexcept;
    [[nodiscard]] Save::ISaveGameSystem& GetSaveGameSystem() const noexcept;
    [[nodiscard]] Save::ICheckpointManager& GetCheckpointManager() const noexcept;
    [[nodiscard]] Window::IWindowManager& GetWindowManager() const noexcept;

    void SetVfxWorld(VFX::VfxWorld* vfxWorld) noexcept;
    [[nodiscard]] VFX::VfxWorld* TryGetVfxWorld() const noexcept;

    /// These services with the scene half replaced: another scene's entities and graph, drawn by
    /// its own renderer and render systems (see DocumentScene). Everything else is shared.
    [[nodiscard]] EngineServices WithScene(Scene::IEntityManager& entityManager,
                                                 Scene::ISceneGraph& sceneGraph,
                                                 Render::ICameraSystem& cameraSystem,
                                                 Render::IMeshRenderer& meshRenderer,
                                                 Render::ILightSystem& lightSystem,
                                                 Render::IRenderer3D& renderer3D) const noexcept;

private:
    Debug::ILogger* m_logger;
    Utilities::ITimer* m_timer;
    Events::IEventSystem* m_eventSystem;
    Memory::IMemoryManager* m_memoryManager;
    Threading::IThreadPool* m_threadPool;
    CommandLine::ICommandLine* m_commandLine;
    Config::IConfigManager* m_configManager;
    Config::SettingsDocument* m_projectSettings;
    Core::InputMapDocument* m_inputMap;
    Scene::IEntityManager* m_entityManager;
    Scene::ISceneGraph* m_sceneGraph;
    Asset::IAssetDatabase* m_assetDatabase;
    Asset::IProjectFileIndex* m_projectFileIndex;
    Asset::IDerivedArtifactCache* m_derivedArtifactCache;
    Asset::IProjectChangeWatcher* m_projectChangeWatcher;
    Scene::ISceneSerializer* m_sceneSerializer;
    Scene::IPrefabSystem* m_prefabSystem;
    Scene::IParticleRuntime* m_particleRuntime;
    Asset::IHotReload* m_hotReload;
    Asset::IAssetManager* m_assetManager;
    Asset::IAssetImporter* m_assetImporter;
    Asset::IAssetImportQueue* m_assetImportQueue;
    Asset::IAssetBundle* m_assetBundle;
    Asset::IFileSystem* m_fileSystem;
    Render::IRenderDevice* m_renderDevice;
    Render::Shader::IShaderManager* m_shaderManager;
    Render::IRenderSystem* m_renderSystem;
    Render::IComputeSystem* m_computeSystem;
    Render::ICameraSystem* m_cameraSystem;
    Render::IMeshRenderer* m_meshRenderer;
    Render::ILightSystem* m_lightSystem;
    Render::IRenderer3D* m_renderer3D;
    Physics::ICollisionSystem* m_collisionSystem;
    Physics::IPhysicsSystem* m_physicsSystem;
    Physics::IPhysicsQuerySystem* m_physicsQuerySystem;
    Physics::IRaycastSystem* m_raycastSystem;
    Physics::PhysicsConstraintSystem* m_physicsConstraintSystem;
    Input::IInputSystem* m_inputSystem;
    Input::IGamepadInputSystem* m_gamepadInputSystem;
    Input::IMobileInputSystem* m_mobileInputSystem;
    Input::IMobileGestureSystem* m_mobileGestureSystem;
    Audio::IAudioDevice* m_audioDevice;
    Audio::IAudioSystem* m_audioSystem;
    Audio::IAudioSourceSystem* m_audioSourceSystem;
    Save::ISaveGameSystem* m_saveGameSystem;
    Save::ICheckpointManager* m_checkpointManager;
    Window::IWindowManager* m_windowManager;
    VFX::VfxWorld* m_vfxWorld = nullptr;
};

} // namespace UVE::Core
