//                                      UVE
//                                UniVex Engine
//
// UniVex Engine (UVE) — Proprietary Game Engine
// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// Unauthorized copying, modification, distribution, or use of this code
// in whole or in part is strictly prohibited without express written
// permission from UniVex Studios.
// Violators will be prosecuted to the fullest extent of the law.


#include "engine_services.h"

namespace UVE::Core {

EngineServices::EngineServices(Debug::ILogger& logger, Utilities::ITimer& timer,
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
                                      Window::IWindowManager& windowManager) noexcept
    : m_logger(&logger), m_timer(&timer), m_eventSystem(&eventSystem),
      m_memoryManager(&memoryManager), m_threadPool(&threadPool), m_commandLine(&commandLine),
      m_configManager(&configManager), m_projectSettings(&projectSettings), m_inputMap(&inputMap), m_entityManager(&entityManager), m_sceneGraph(&sceneGraph),
      m_assetDatabase(&assetDatabase), m_projectFileIndex(&projectFileIndex),
      m_derivedArtifactCache(&derivedArtifactCache), m_projectChangeWatcher(&projectChangeWatcher),
      m_sceneSerializer(&sceneSerializer),
      m_prefabSystem(&prefabSystem), m_particleRuntime(&particleRuntime), m_hotReload(&hotReload),
      m_assetManager(&assetManager),
      m_assetImporter(&assetImporter), m_assetImportQueue(&assetImportQueue), m_assetBundle(&assetBundle),
      m_fileSystem(&fileSystem),
      m_renderDevice(&renderDevice), m_shaderManager(&shaderManager), m_renderSystem(&renderSystem),
      m_computeSystem(&computeSystem), m_cameraSystem(&cameraSystem),
      m_meshRenderer(&meshRenderer), m_lightSystem(&lightSystem), m_renderer3D(&renderer3D),
      m_collisionSystem(&collisionSystem),
      m_physicsSystem(&physicsSystem), m_physicsQuerySystem(&physicsQuerySystem),
      m_raycastSystem(&raycastSystem), m_physicsConstraintSystem(&physicsConstraintSystem),
      m_inputSystem(&inputSystem), m_gamepadInputSystem(&gamepadInputSystem),
      m_mobileInputSystem(&mobileInputSystem), m_mobileGestureSystem(&mobileGestureSystem),
      m_audioDevice(&audioDevice), m_audioSystem(&audioSystem), m_audioSourceSystem(&audioSourceSystem),
      m_saveGameSystem(&saveGameSystem), m_checkpointManager(&checkpointManager),
      m_windowManager(&windowManager) {}

Debug::ILogger& EngineServices::GetLogger() const noexcept {
    return *m_logger;
}

Utilities::ITimer& EngineServices::GetTimer() const noexcept {
    return *m_timer;
}

Events::IEventSystem& EngineServices::GetEventSystem() const noexcept {
    return *m_eventSystem;
}

Memory::IMemoryManager& EngineServices::GetMemoryManager() const noexcept {
    return *m_memoryManager;
}

Threading::IThreadPool& EngineServices::GetThreadPool() const noexcept {
    return *m_threadPool;
}

CommandLine::ICommandLine& EngineServices::GetCommandLine() const noexcept {
    return *m_commandLine;
}

Config::IConfigManager& EngineServices::GetConfigManager() const noexcept {
    return *m_configManager;
}

Config::SettingsDocument& EngineServices::GetProjectSettings() const noexcept {
    return *m_projectSettings;
}

Core::InputMapDocument& EngineServices::GetInputMap() const noexcept {
    return *m_inputMap;
}

Scene::IEntityManager& EngineServices::GetEntityManager() const noexcept {
    return *m_entityManager;
}

Scene::ISceneGraph& EngineServices::GetSceneGraph() const noexcept {
    return *m_sceneGraph;
}

Asset::IAssetDatabase& EngineServices::GetAssetDatabase() const noexcept {
    return *m_assetDatabase;
}

Asset::IProjectFileIndex& EngineServices::GetProjectFileIndex() const noexcept {
    return *m_projectFileIndex;
}

Asset::IDerivedArtifactCache& EngineServices::GetDerivedArtifactCache() const noexcept {
    return *m_derivedArtifactCache;
}

Asset::IProjectChangeWatcher& EngineServices::GetProjectChangeWatcher() const noexcept {
    return *m_projectChangeWatcher;
}

Scene::ISceneSerializer& EngineServices::GetSceneSerializer() const noexcept {
    return *m_sceneSerializer;
}

Scene::IPrefabSystem& EngineServices::GetPrefabSystem() const noexcept {
    return *m_prefabSystem;
}

Scene::IParticleRuntime& EngineServices::GetParticleRuntime() const noexcept {
    return *m_particleRuntime;
}

Asset::IHotReload& EngineServices::GetHotReload() const noexcept {
    return *m_hotReload;
}

Asset::IAssetManager& EngineServices::GetAssetManager() const noexcept {
    return *m_assetManager;
}

Asset::IAssetImporter& EngineServices::GetAssetImporter() const noexcept {
    return *m_assetImporter;
}

Asset::IAssetImportQueue& EngineServices::GetAssetImportQueue() const noexcept {
    return *m_assetImportQueue;
}

Asset::IAssetBundle& EngineServices::GetAssetBundle() const noexcept {
    return *m_assetBundle;
}

Asset::IFileSystem& EngineServices::GetFileSystem() const noexcept {
    return *m_fileSystem;
}

Render::IRenderDevice& EngineServices::GetRenderDevice() const noexcept {
    return *m_renderDevice;
}

Render::Shader::IShaderManager& EngineServices::GetShaderManager() const noexcept {
    return *m_shaderManager;
}

Render::IRenderSystem& EngineServices::GetRenderSystem() const noexcept {
    return *m_renderSystem;
}

Render::IComputeSystem& EngineServices::GetComputeSystem() const noexcept {
    return *m_computeSystem;
}

Render::ICameraSystem& EngineServices::GetCameraSystem() const noexcept {
    return *m_cameraSystem;
}

Render::IMeshRenderer& EngineServices::GetMeshRenderer() const noexcept {
    return *m_meshRenderer;
}

Render::ILightSystem& EngineServices::GetLightSystem() const noexcept {
    return *m_lightSystem;
}

Render::IRenderer3D& EngineServices::GetRenderer3D() const noexcept {
    return *m_renderer3D;
}

Physics::ICollisionSystem& EngineServices::GetCollisionSystem() const noexcept {
    return *m_collisionSystem;
}

Physics::IPhysicsSystem& EngineServices::GetPhysicsSystem() const noexcept {
    return *m_physicsSystem;
}

Physics::IPhysicsQuerySystem& EngineServices::GetPhysicsQuerySystem() const noexcept {
    return *m_physicsQuerySystem;
}

Physics::IRaycastSystem& EngineServices::GetRaycastSystem() const noexcept {
    return *m_raycastSystem;
}

Physics::PhysicsConstraintSystem& EngineServices::GetPhysicsConstraintSystem() const noexcept {
    return *m_physicsConstraintSystem;
}

Input::IInputSystem& EngineServices::GetInputSystem() const noexcept {
    return *m_inputSystem;
}

Input::IGamepadInputSystem& EngineServices::GetGamepadInputSystem() const noexcept {
    return *m_gamepadInputSystem;
}

Input::IMobileInputSystem& EngineServices::GetMobileInputSystem() const noexcept {
    return *m_mobileInputSystem;
}

Input::IMobileGestureSystem& EngineServices::GetMobileGestureSystem() const noexcept {
    return *m_mobileGestureSystem;
}

Audio::IAudioDevice& EngineServices::GetAudioDevice() const noexcept {
    return *m_audioDevice;
}

Audio::IAudioSystem& EngineServices::GetAudioSystem() const noexcept {
    return *m_audioSystem;
}

Audio::IAudioSourceSystem& EngineServices::GetAudioSourceSystem() const noexcept {
    return *m_audioSourceSystem;
}

Save::ISaveGameSystem& EngineServices::GetSaveGameSystem() const noexcept {
    return *m_saveGameSystem;
}

Save::ICheckpointManager& EngineServices::GetCheckpointManager() const noexcept {
    return *m_checkpointManager;
}

Window::IWindowManager& EngineServices::GetWindowManager() const noexcept {
    return *m_windowManager;
}

void EngineServices::SetVfxWorld(VFX::VfxWorld* const vfxWorld) noexcept {
    m_vfxWorld = vfxWorld;
}

VFX::VfxWorld* EngineServices::TryGetVfxWorld() const noexcept {
    return m_vfxWorld;
}

EngineServices EngineServices::WithScene(Scene::IEntityManager& entityManager,
                                                  Scene::ISceneGraph& sceneGraph,
                                                  Render::ICameraSystem& cameraSystem,
                                                  Render::IMeshRenderer& meshRenderer,
                                                  Render::ILightSystem& lightSystem,
                                                  Render::IRenderer3D& renderer3D) const noexcept {
    EngineServices copy = *this;
    copy.m_entityManager = &entityManager;
    copy.m_sceneGraph = &sceneGraph;
    copy.m_cameraSystem = &cameraSystem;
    copy.m_meshRenderer = &meshRenderer;
    copy.m_lightSystem = &lightSystem;
    copy.m_renderer3D = &renderer3D;
    return copy;
}

} // namespace UVE::Core
