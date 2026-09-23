//                                      UVE
//                                UniVex Engine
//
// UniVex Engine (UVE) — Proprietary Game Engine
// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// Unauthorized copying, modification, distribution, or use of this code
// in whole or in part is strictly prohibited without express written
// permission from UniVex Studios.
// Violators will be prosecuted to the fullest extent of the law.


#include "uve/core/engine_core_uve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <utility>

#include "uve/asset/asset_bundle_uve.h"
#include "uve/asset/asset_database_uve.h"
#include "uve/asset/asset_importer_uve.h"
#include "uve/asset/asset_import_queue_uve.h"
#include "uve/asset/asset_manager_uve.h"
#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/asset/audio_asset_uve.h"
#include "uve/asset/data_table_pipeline_uve.h"
#include "uve/asset/derived_artifact_cache_uve.h"
#include "uve/asset/file_system_uve.h"
#include "uve/asset/hot_reload_uve.h"
#include "uve/asset/material_asset_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/project_file_index_uve.h"
#include "uve/asset/project_change_watcher_uve.h"
#include "uve/asset/shader_asset_uve.h"
#include "uve/asset/texture_asset_uve.h"
#include "uve/audio/audio_source_system_uve.h"
#include "uve/audio/audio_system_uve.h"
#include "uve/audio/miniaudio_audio_device_uve.h"
#include "uve/audio/null_audio_device_uve.h"
#include "uve/audio/wav_importer_uve.h"
#include "uve/commandline/command_line_uve.h"
#include "uve/config/config_manager_uve.h"
#include "uve/logging/assert_uve.h"
#include "uve/logging/log_sink_uve.h"
#include "uve/logging/logger_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/input/gamepad_input_system_uve.h"
#include "uve/input/input_system_uve.h"
#include "uve/input/mobile_gesture_system_uve.h"
#include "uve/input/mobile_input_system_uve.h"
#include "uve/physics/area_overlap_events_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/nodes/3d/hitbox_3d_uve.h"
#include "uve/nodes/3d/hurtbox_3d_uve.h"
#include "uve/nodes/3d/interaction_area_3d_uve.h"
#include "uve/nodes/3d/level_streamer_3d_uve.h"
#include "uve/nodes/3d/projectile_3d_uve.h"
#include "uve/nodes/3d/reflection_probe_3d_uve.h"
#include "uve/nodes/3d/ray_cast_3d_uve.h"
#include "uve/nodes/3d/spring_arm_3d_uve.h"
#include "uve/nodes/3d/visibility_region_3d_uve.h"
#include "uve/nodes/3d/world_partition_3d_uve.h"
#include "uve/physics/detail/shape_narrow_phase_uve.h"
#include "uve/physics/character_controller_uve.h"
#include "uve/physics/collision_system_uve.h"
#include "uve/physics/physics_system_uve.h"
#include "uve/physics/raycast_system_uve.h"
#include "uve/platform/platform_uve.h"
#include "uve/render_systems/camera_system_uve.h"
#include "uve/render_systems/compute_system_uve.h"
#include "uve/rhi_opengl/gl_render_device_uve.h"
#include "uve/rhi_vulkan/vulkan_render_device_uve.h"
#include "uve/render_systems/light_system_uve.h"
#include "uve/render_systems/mesh_renderer_uve.h"
#include "uve/rhi_null/null_render_device_uve.h"
#include "uve/render_systems/render_system_uve.h"
#include "uve/render_systems/renderer_3d_uve.h"
#include "uve/rhi_shader/shader_manager_uve.h"
#include "uve/save/checkpoint_manager_uve.h"
#include "uve/save/save_game_system_uve.h"
#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/particle_emitter_component_uve.h"
#include "uve/component/rigid_body_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/scene/prefab_system_uve.h"
#include "uve/scene/scene_graph_uve.h"
#include "uve/scene/scene_serializer_uve.h"
#include "uve/scripting/script_asset_loader_uve.h"
#include "uve/scripting/script_builtin_nodes_uve.h"
#include "uve/scripting/script_component_runtime_ownership_uve.h"
#include "uve/threading/thread_pool_uve.h"
#include "uve/utilities/timer_uve.h"
#include "uve/window/adaptive_render_resolution_uve.h"
#include "uve/window/null_window_manager_uve.h"
#if defined(__ANDROID__)
#include "uve/window/android_surface_size_uve.h"
#include "uve/window/android_window_manager_uve.h"
#else
#include "uve/window/window_manager_uve.h"
#endif

namespace UVE::Core {

namespace {

#if defined(__ANDROID__)
constexpr Window::AdaptiveRenderResolutionLimitsUVE kAdaptiveRenderResolutionLimitsUVE{
    Window::kMaximumAndroidSurfaceAxisUVE, Window::kMaximumAndroidRenderTargetPixelsUVE};
#else
// Keep large desktop displays sharp up to 4K while bounding worst-case offscreen allocations.
constexpr Window::AdaptiveRenderResolutionLimitsUVE kAdaptiveRenderResolutionLimitsUVE{8192U, 3840ULL * 2160ULL};
#endif

/// The process mode the scene graph resolved for `entity` on its last update, or the hierarchy
/// default for an entity it has not seen yet (created since, or not a scene-graph node). Asking the
/// scene graph rather than the entity's own ProcessComponentUVE is what makes an entity with no
/// component inherit its ancestors' answer instead of silently ignoring it.
[[nodiscard]] Scene::ProcessModeUVE ResolvedProcessModeUVE(const Scene::ISceneGraphUVE& sceneGraph,
                                                           const Scene::EntityUVE entity) {
    const std::optional<Scene::ResolvedNodeModesUVE> modes = sceneGraph.TryGetResolvedNodeModesUVE(entity);
    return modes.has_value() ? modes->process : Scene::ProcessModeUVE::Pausable;
}

/// An entity's own ordering key, or 0 when it carries no ProcessComponentUVE. Priorities are
/// authored per entity and deliberately NOT inherited: a whole subtree sharing its root's priority
/// would make it impossible to order anything inside it.
[[nodiscard]] Scene::ProcessComponentUVE ProcessSettingsUVE(const Scene::IEntityManagerUVE& entityManager,
                                                            const Scene::EntityUVE entity) {
    return entityManager.HasComponentUVE<Scene::ProcessComponentUVE>(entity)
               ? entityManager.GetComponentUVE<Scene::ProcessComponentUVE>(entity)
               : Scene::ProcessComponentUVE{};
}

/// The entities holding ComponentT whose fixed-step work runs this step, in physicsPriority order.
///
/// A fixed step is evaluated as "not paused" whichever way it came about - normal running, or the
/// single step the editor requests while paused - because either way it IS the simulation
/// advancing. So a fixed step skips only Disabled and WhenPaused entities; the latter never advance
/// physics, because physics does not step while paused.
///
/// Ties keep the ECS iteration order these systems used before priority existed (stable_sort over
/// that order), so a scene that sets no priorities steps in exactly the order it always did.
template <typename ComponentT>
[[nodiscard]] std::vector<Scene::EntityUVE> CollectFixedStepOrderUVE(Scene::IEntityManagerUVE& entityManager,
                                                                     const Scene::ISceneGraphUVE& sceneGraph) {
    std::vector<std::pair<std::int32_t, Scene::EntityUVE>> ordered;
    entityManager.ForEachUVE<ComponentT>([&](const Scene::EntityUVE entity, ComponentT&) {
        if (!Scene::IsProcessingUVE(ResolvedProcessModeUVE(sceneGraph, entity), /*simulationPaused=*/false)) {
            return;
        }
        ordered.emplace_back(ProcessSettingsUVE(entityManager, entity).physicsPriority, entity);
    });
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto& left, const auto& right) { return left.first < right.first; });
    std::vector<Scene::EntityUVE> entities;
    entities.reserve(ordered.size());
    for (const auto& [priority, entity] : ordered) {
        static_cast<void>(priority);
        entities.push_back(entity);
    }
    return entities;
}

} // namespace

EngineCoreUVE::EngineCoreUVE(EngineConfigUVE config) : m_config(std::move(config)) {}

EngineCoreUVE::~EngineCoreUVE() {
    if (m_state == EngineStateUVE::Running) {
        Shutdown();
    }
}

void EngineCoreUVE::TransitionStateUVE(EngineStateUVE newState) {
    UVE_ASSERT(IsValidTransitionUVE(m_state, newState));
    m_state = newState;
}

void EngineCoreUVE::RegisterBuiltInAssetLoadersUVE() {
    m_assetManager->RegisterLoaderUVE<Asset::MeshAssetUVE>(&Asset::LoadMeshAssetUVE);
    m_assetManager->RegisterLoaderUVE<Asset::TextureAssetUVE>(&Asset::LoadTextureAssetUVE);
    m_assetManager->RegisterLoaderUVE<Asset::ShaderAssetUVE>(&Asset::LoadShaderAssetUVE);
    m_assetManager->RegisterLoaderUVE<Asset::MaterialAssetUVE>(&Asset::LoadMaterialAssetUVE);
    m_assetManager->RegisterLoaderUVE<Asset::AudioAssetUVE>(&Asset::LoadAudioAssetUVE);
    m_assetManager->RegisterLoaderUVE<Asset::AnimationClipAssetUVE>(&Asset::LoadAnimationClipAssetUVE);
}

void EngineCoreUVE::Init() {
    TransitionStateUVE(EngineStateUVE::Initializing);

    // CommandLine first: it has zero dependencies (pure parsing of the
    // args already captured in EngineConfigUVE), and its parsed flags are
    // the kind of thing a later step could plausibly want during its own
    // setup in a future increment.
    m_commandLine = std::make_unique<CommandLine::CommandLineUVE>(m_config.commandLineArgs);

    // --headless overrides EngineConfigUVE::headlessUVE the moment CommandLine exists, before
    // anything below reads it.
    if (m_commandLine->HasFlagUVE("headless")) {
        m_config.headlessUVE = true;
    }

    // Logger second: every later step below, and every other engine
    // system, may need to log or UVE_ASSERT during its own setup. See
    // docs/CODING_STANDARDS.md for the full init/shutdown ordering
    // rationale.
    auto logger = std::make_unique<Debug::LoggerUVE>();
    logger->Init(m_config.minLogLevel);
    if (m_config.enableConsoleLogging) {
        logger->AddSink(std::make_unique<Debug::ConsoleSinkUVE>());
    }
    logger->AddSink(std::make_unique<Debug::FileSinkUVE>(m_config.logFilePath));
    m_logger = std::move(logger);

    UVE_INFO("EngineCoreUVE: initializing UniVex Engine {}", GetEngineVersionUVE().ToStringUVE());

    // MemoryManager third: the next most foundational service after
    // logging — nothing constructed here has a hard dependency on it yet,
    // but future systems will, mirroring the rationale for Logger's own
    // position.
    m_memoryManager = std::make_unique<Memory::MemoryManagerUVE>();

    // ThreadPool fourth: sits alongside Logger/MemoryManager as a
    // foundational service (matching the spec's own Part 7.1 ordering,
    // which lists ThreadPoolUVE before EventSystemUVE/TimerUVE) and after
    // Logger/MemoryManager since its workers may immediately want to log
    // or allocate once real jobs start flowing through it.
    m_threadPool = std::make_unique<Threading::ThreadPoolUVE>(m_config.threadPoolWorkerCount);

    // Timer fifth: Update()/LateUpdate() depend on it; nothing constructed
    // here depends on EventSystem existing yet.
    auto timer = std::make_unique<Utilities::TimerUVE>();
    timer->Reset();
    timer->SetMaxDeltaTimeUVE(m_config.maxDeltaTimeSeconds);
    timer->SetFixedTimestepUVE(m_config.fixedUpdateFps > 0.0 ? (1.0 / m_config.fixedUpdateFps) : 0.0);
    m_timer = std::move(timer);

    // EventSystem sixth: it is the piece most likely to gain future
    // dependents (systems subscribing during their own Init()), so it is
    // constructed after every other foundational service except
    // EntityManager/SceneGraph/ConfigManager, once it is guaranteed nothing
    // else in this list still needs to be built.
    m_eventSystem = std::make_unique<Events::EventSystemUVE>();

    // EntityManager seventh: needs MemoryManager (for chunk allocation) and
    // EventSystem (for entity lifecycle events), so it is built right after
    // both exist.
    m_entityManager = std::make_unique<Scene::EntityManagerUVE>(
        m_memoryManager->GetDefaultAllocatorUVE(), *m_eventSystem);

    // SceneGraph eighth: has no dependencies of its own; grouped
    // immediately after EntityManager for readability.
    m_sceneGraph = std::make_unique<Scene::SceneGraphUVE>();

    // AssetDatabase ninth: needs only Logger, which already exists. Its
    // LoadUVE() call logs its outcome (missing/malformed/success) the same
    // way ConfigManager's does below.
    auto assetDatabase = std::make_unique<Asset::AssetDatabaseUVE>();
    assetDatabase->LoadUVE(m_config.assetDatabaseFilePath);
    m_assetDatabase = std::move(assetDatabase);

    // ProjectFileIndex tenth: holds only an explicit configured content-root
    // and a cached read-only editor snapshot. It never scans until an editor
    // caller requests RefreshUVE(), and has no ownership of AssetDatabase.
    m_projectFileIndex = std::make_unique<Asset::ProjectFileIndexUVE>(m_config.projectContentRootUVE);

    // DerivedArtifactCache eleventh: owns only project-local generated import metadata. It creates
    // its configured root lazily during a successful cache write and has no AssetDatabase ownership.
    m_derivedArtifactCache = std::make_unique<Asset::DerivedArtifactCacheUVE>(m_config.derivedArtifactCacheRootUVE);

    // ProjectChangeWatcher twelfth: interval-driven project-content observation stays separate
    // from the loaded-runtime HotReload poller. It owns no importer, worker, or index refresh policy.
    m_projectChangeWatcher = std::make_unique<Asset::ProjectChangeWatcherUVE>(
        m_config.projectContentRootUVE, m_config.projectChangeWatchPollIntervalSecondsUVE,
        m_config.projectChangeJournalCapacityUVE);

    // SceneSerializer and PrefabSystem thirteenth/fourteenth: both stateless,
    // grouped immediately after AssetDatabase/ProjectFileIndex for readability.
    m_sceneSerializer = std::make_unique<Scene::SceneSerializerUVE>();
    m_prefabSystem = std::make_unique<Scene::PrefabSystemUVE>();

    // HotReload twelfth: needs only EventSystem. Constructed before
    // AssetManager (not after, as its Part 7.4 spec-listing order might
    // suggest) purely because AssetManager's constructor takes a
    // HotReloadUVE* — construction must stay strictly forward-dependency.
    m_hotReload = std::make_unique<Asset::HotReloadUVE>(*m_eventSystem, m_config.hotReloadPollIntervalSecondsUVE);

    // AssetManager thirteenth: needs ThreadPool (to run loads) and
    // EventSystem (to publish AssetLoadCompletedEventUVE), and is always
    // given a real HotReloadUVE* (never null) — EngineConfigUVE::
    // hotReloadEnabledUVE only gates whether Update() calls PollUVE(), not
    // whether HotReloadUVE exists at all.
    m_assetManager = std::make_unique<Asset::AssetManagerUVE>(*m_threadPool, *m_eventSystem, m_hotReload.get());
    RegisterBuiltInAssetLoadersUVE();

    // AssetImporter fourteenth: retains the existing extension-selected synchronous import behavior.
    m_assetImporter = std::make_unique<Asset::AssetImporterUVE>();
    Audio::RegisterWavImporterUVE(*m_assetImporter);

    // Compose the schema-driven Data Table importers and typed loader onto the existing generic
    // services. Registration owns no service or loaded asset state, so EngineCoreUVE remains the
    // sole owner and the generic service boundaries remain unchanged.
    Asset::RegisterDataTablePipelineUVE(*m_assetImporter, *m_assetManager);

    // AssetImportQueue fifteenth: calls the importer at most once per Update() and validates
    // metadata cache hits against source and destination byte fingerprints plus AssetDatabase.
    m_assetImportQueue = std::make_unique<Asset::AssetImportQueueUVE>(
        *m_assetImporter, *m_assetDatabase, *m_derivedArtifactCache);

    // AssetBundle sixteenth: stateless and independent from the queue/cache.
    m_assetBundle = std::make_unique<Asset::AssetBundleUVE>();

    // FileSystem seventeenth: needs AssetBundle, since its bundle-backed
    // mounts read entries through IAssetBundleUVE.
    m_fileSystem = std::make_unique<Asset::FileSystemUVE>(*m_assetBundle);

    // WindowManager seventeenth: a real Window::WindowManagerUVE (owning the entire GLFW/GL
    // context lifecycle — init, create, activate, destroy, terminate) unless
    // EngineConfigUVE::headlessUVE, in which case Window::NullWindowManagerUVE. A real window
    // that fails to create logs UVE_FATAL and sets m_windowCreationFailedUVE rather than aborting
    // Init() mid-construction — EngineStateUVE's transition table forbids jumping straight from
    // Initializing to ShuttingDown, so every remaining service below still finishes constructing
    // normally; Load() checks the flag afterward (see Load()'s doc comment). A window that exists
    // but cannot load the complete GL function table is handled separately below as a recoverable
    // backend-selection failure and falls back to NullRenderDeviceUVE.
    if (m_config.headlessUVE) {
        m_windowManager = std::make_unique<Window::NullWindowManagerUVE>();
    } else {
        Window::WindowDescUVE windowDesc;
        windowDesc.title = m_config.windowTitle;
        windowDesc.width = m_config.windowWidth;
        windowDesc.height = m_config.windowHeight;
        windowDesc.resizable = m_config.windowResizableUVE;
        windowDesc.vsyncEnabled = m_config.vsyncEnabledUVE;
        windowDesc.glVersionMajor = m_config.windowGlVersionMajor;
        windowDesc.glVersionMinor = m_config.windowGlVersionMinor;
#if defined(__ANDROID__)
        if (m_config.nativeWindowHandleUVE == nullptr) {
            UVE_FATAL("EngineCoreUVE: Android window handle is required for a windowed run");
            m_windowManager = std::make_unique<Window::NullWindowManagerUVE>();
            m_windowCreationFailedUVE = true;
        } else {
            m_windowManager = std::make_unique<Window::AndroidWindowManagerUVE>(
                *m_eventSystem, m_config.nativeWindowHandleUVE, windowDesc);
        }
#else
        m_windowManager = std::make_unique<Window::WindowManagerUVE>(*m_eventSystem, windowDesc);
#endif
        if (!m_windowManager->IsValidUVE()) {
            UVE_FATAL("EngineCoreUVE: window creation failed - this run will not proceed past Load()");
            m_windowCreationFailedUVE = true;
        }
    }
    m_windowedRenderingActiveUVE = !m_config.headlessUVE && m_windowManager->IsValidUVE();

    // RenderDevice eighteenth: backend selection ordered by EngineConfigUVE::renderBackendPreferenceUVE
    // when a real valid window/context exists, otherwise Render::NullRenderDeviceUVE. The preference is a
    // *starting point*, never a hard requirement — the chain is Vulkan -> OpenGL -> Null, each
    // fall-through a logged warning, and the null device is one the entire headless test suite
    // already proves the engine survives on. OpenGL remains the production default (AutoUVE
    // resolves to it); VulkanUVE opts into the M1 bootstrap device. A context can exist while
    // the required GL entry points are unavailable on an unusual driver; GlRenderDeviceUVE
    // reports that state without aborting, and the engine selects NullRenderDeviceUVE before
    // ShaderManager or any frame code can call a missing function pointer.
    if (m_windowedRenderingActiveUVE) {
        const RenderBackendPreferenceUVE preference = m_config.renderBackendPreferenceUVE;
        if (preference == RenderBackendPreferenceUVE::VulkanUVE) {
            // Factory-probe create: nullptr (with reason logged) when the host lacks a loader,
            // an ICD, or the GLFW bridge's surface capability. IsUsableUVE() is checked anyway
            // so a future early-inert state stays load-bearing protection here.
            auto vulkanDevice = Render::VulkanRenderDeviceUVE::CreateUVE(*m_windowManager);
            if (vulkanDevice != nullptr && vulkanDevice->IsUsableUVE()) {
                m_renderDevice = std::move(vulkanDevice);
                UVE_INFO("EngineCoreUVE: render backend = {}", m_renderDevice->GetBackendNameUVE());
            } else {
                UVE_WARNING("EngineCoreUVE: requested Vulkan backend is unavailable on this host; trying OpenGL");
            }
        }
        if (m_renderDevice == nullptr && preference != RenderBackendPreferenceUVE::NullUVE) {
            auto glRenderDevice = std::make_unique<Render::GlRenderDeviceUVE>(*m_windowManager);
            if (glRenderDevice->IsUsableUVE()) {
                m_renderDevice = std::move(glRenderDevice);
            } else {
#if defined(__ANDROID__)
                // Android currently ships an EGL/GLES implementation only. If that context or its
                // required entry points are unavailable, do not guess that a Vulkan renderer exists:
                // the Vulkan backend is desktop-windowed only in this build. Keep the engine alive
                // on the inert RHI instead of dereferencing a partial GL function table and
                // crashing during shader/frame startup.
                UVE_WARNING("EngineCoreUVE: OpenGL ES backend is unavailable; Vulkan RHI is not built, using Null backend");
#else
                UVE_WARNING("EngineCoreUVE: OpenGL backend is unavailable; using Null backend");
#endif
            }
        }
        if (m_renderDevice == nullptr) {
            m_windowedRenderingActiveUVE = false;
            m_renderDevice = std::make_unique<Render::NullRenderDeviceUVE>();
        }
    } else {
        m_renderDevice = std::make_unique<Render::NullRenderDeviceUVE>();
    }
    m_presentationSurfaceReadyUVE = m_windowedRenderingActiveUVE;

    // ShaderManager nineteenth: needs ThreadPool, EventSystem, RenderDevice, and FileSystem — all
    // already constructed by this point. Mounts EngineConfigUVE::shaderSourceRealDirectoryUVE
    // under shaderSourceMountPrefixUVE first, so the built-in .glsl files (and any #include
    // closure among them) resolve through the VFS and participate in hot-reload; a
    // missing/unreachable directory is not an error here either — every built-in also carries an
    // embedded string fallback (see Render::Shader::BuiltIn::kBasic3DSource) ShaderManagerUVE uses
    // automatically when the mount doesn't resolve. Works identically in headless mode (against
    // NullRenderDeviceUVE) and windowed mode (against GlRenderDeviceUVE).
    m_fileSystem->MountDirectoryUVE(m_config.shaderSourceMountPrefixUVE, m_config.shaderSourceRealDirectoryUVE, 0);
    Render::Shader::ShaderManagerConfigUVE shaderManagerConfig;
    shaderManagerConfig.cachePath = m_config.shaderCachePath;
    shaderManagerConfig.hotReloadEnabledUVE = m_config.shaderHotReloadEnabledUVE;
    shaderManagerConfig.hotReloadPollIntervalSecondsUVE = m_config.shaderHotReloadPollIntervalSecondsUVE;
#if UVE_DEBUG
    shaderManagerConfig.injectDebugDefineUVE = true;
#else
    shaderManagerConfig.injectDebugDefineUVE = false;
#endif
    m_shaderManager = std::make_unique<Render::Shader::ShaderManagerUVE>(
        *m_threadPool, *m_eventSystem, *m_renderDevice, *m_fileSystem, shaderManagerConfig);

    // RenderSystem twentieth: needs RenderDevice, since it records and
    // submits command buffers through it.
    m_renderSystem = std::make_unique<Render::RenderSystemUVE>(*m_renderDevice);

    // ComputeSystem: needs RenderDevice (it creates compute programs and records dispatches
    // through it). Constructed right after RenderSystem because Render() drains its queue into
    // the frame's own command buffer, before any render pass opens — the outside-pass-markers
    // contract IComputeSystemUVE documents.
    m_computeSystem = std::make_unique<Render::ComputeSystemUVE>(*m_renderDevice);

    // CameraSystem twenty-first: stateless, no dependencies of its own —
    // grouped with the rest of engine/render.
    m_cameraSystem = std::make_unique<Render::CameraSystemUVE>();
    // MeshRenderer twenty-second: stateless, no dependencies of its own —
    // grouped with the rest of engine/render.
    m_meshRenderer = std::make_unique<Render::MeshRendererUVE>();

    // LightSystem twenty-third: stateless, no dependencies of its own — grouped with the rest
    // of engine/render, constructed right before Renderer3D since Renderer3D's constructor
    // needs it.
    m_lightSystem = std::make_unique<Render::LightSystemUVE>();

    // Renderer3D twenty-fourth: needs RenderDevice, RenderSystem, MeshRenderer, CameraSystem,
    // LightSystem, ShaderManager (Increment 26 — compiles the built-in shadow-depth program),
    // AssetManager, AssetDatabase, EventSystem, EngineConfigUVE::ambientColor, and
    // EngineConfigUVE::shadowMapResolution/shadowMapHalfExtent/shadowMapNearPlane/
    // shadowMapFarPlane — every one of which already exists by this point (ShaderManager is
    // constructed twentieth, above). Its offscreen render target is fixed at
    // EngineConfigUVE::renderTargetWidth/Height for this EngineCoreUVE's lifetime, entirely
    // independent of WindowManagerUVE/the real window size — Renderer3DUVE never renders into the
    // window itself (see docs/CODING_STANDARDS.md's rendering-evolution roadmap for how these two
    // eventually connect).
    m_renderer3D = std::make_unique<Render::Renderer3DUVE>(
        *m_renderDevice, *m_renderSystem, *m_meshRenderer, *m_cameraSystem, *m_lightSystem, *m_shaderManager,
        *m_assetManager, *m_assetDatabase, *m_eventSystem, m_config.renderTargetWidth, m_config.renderTargetHeight,
        m_config.ambientColor, m_config.shadowMapResolution, m_config.shadowMapHalfExtent,
        m_config.shadowMapNearPlane, m_config.shadowMapFarPlane, m_config.shadowFrustumPadding,
        m_config.shadowCascadeSplitLambda, m_config.shadowCascadeBlendRatio, m_config.shadowPcfKernelRadius);

    // CollisionSystem twenty-fifth: stateless, no dependencies of its own.
    m_collisionSystem = std::make_unique<Physics::CollisionSystemUVE>();

    // PhysicsConstraintSystem twenty-sixth: EngineCore owns the bounded registry so constraints
    // added through EngineServices participate in the normal fixed-step runtime. It is constructed
    // before PhysicsSystem and reset after it, preventing the PhysicsSystem's non-owning attachment
    // from ever outliving the registry.
    m_physicsConstraintSystem = std::make_unique<Physics::PhysicsConstraintSystemUVE>();

    // PhysicsSystem twenty-seventh: needs CollisionSystem and EngineConfigUVE::gravity. Attach the
    // EngineCore-owned constraint registry before publishing the system through EngineServices.
    auto physicsSystem = std::make_unique<Physics::PhysicsSystemUVE>(*m_collisionSystem, m_config.gravity);
    physicsSystem->SetConstraintSystemUVE(m_physicsConstraintSystem.get());
    m_physicsSystem = std::move(physicsSystem);

    // PhysicsQuerySystem twenty-eighth: stateless façade over existing shape-cast, overlap, and
    // caller-owned character-controller authorities. It borrows CollisionSystem only for controller
    // commands and owns no ECS or scene state.
    m_physicsQuerySystem = std::make_unique<Physics::PhysicsQuerySystemUVE>(*m_collisionSystem);

    // RaycastSystem twenty-ninth: stateless, no dependencies of its own.
    m_raycastSystem = std::make_unique<Physics::RaycastSystemUVE>();

    // ParticleRuntime fifteenth: owns only bounded authored-emitter runtime state; ECS remains
    // authoritative and EngineCore reconciles it before simulation/render extraction.
    m_particleRuntime = std::make_unique<Scene::ParticleRuntimeUVE>();

    // GamepadInputSystem thirtieth: owns only bounded injectable current/previous snapshots.
    m_gamepadInputSystem = std::make_unique<Input::GamepadInputSystemUVE>();

    // MobileInputSystem thirty-first: owns only bounded injectable touch/gyroscope snapshots.
    m_mobileInputSystem = std::make_unique<Input::MobileInputSystemUVE>();

    // MobileGestureSystem thirty-second: borrows MobileInput snapshots and retains only its
    // bounded recognizer state/latest copied report.
    m_mobileGestureSystem = std::make_unique<Input::MobileGestureSystemUVE>(*m_mobileInputSystem);

    // InputSystem thirty-third: needs EventSystem and the already-composed gamepad snapshot service
    // to queue action events and evaluate keyboard/mouse/gamepad bindings.
    m_inputSystem = std::make_unique<Input::InputSystemUVE>(*m_eventSystem, m_gamepadInputSystem.get());
    m_windowManager->AttachInputSystemUVE(m_inputSystem.get());

    // AudioDevice twenty-ninth: no dependencies of its own. Prefers the real miniaudio backend;
    // falls back to NullAudioDeviceUVE (with a warning) on machines with no usable audio output
    // (headless CI, servers) so the whole audio stack above stays functional either way.
    if (auto miniaudioDevice = Audio::MiniaudioAudioDeviceUVE::CreateUVE()) {
        m_audioDevice = std::move(miniaudioDevice);
    } else {
        UVE_WARNING("EngineCoreUVE: no usable audio output device - falling back to NullAudioDeviceUVE "
                    "(all audio playback will be silent but functional)");
        m_audioDevice = std::make_unique<Audio::NullAudioDeviceUVE>();
    }

    // AudioSystem thirtieth: needs AudioDevice (it pushes computed gain/position through it).
    m_audioSystem = std::make_unique<Audio::AudioSystemUVE>(*m_audioDevice);

    // AudioSourceSystem thirty-first: stateful but takes no constructor dependencies —
    // EntityManager and AudioSystem are passed to SyncUVE() per call, like
    // MeshRendererUVE::ExtractRenderQueueUVE().
    m_audioSourceSystem = std::make_unique<Audio::AudioSourceSystemUVE>();

    // ScriptNodeRegistry/ScriptRuntime thirty-second: needs InputSystem to already exist so the
    // real gameplay bindings below have something to wire (Scripting itself has no InputSystem
    // dependency of its own - EngineCoreUVE is what closes that loop). RegisterBuiltInScriptNodesUVE
    // populates the full ~161 built-in node-type descriptor set (same call EditorUVE's own
    // m_visualScriptRegistry already makes) - without it every ScriptComponentUVE's graph would
    // fail compilation with "unknown node type" for every single node.
    if (!Scripting::RegisterBuiltInScriptNodesUVE(m_scriptNodeRegistry)) {
        UVE_ERROR("EngineCoreUVE: failed to register built-in Scripting node types - "
                  "ScriptComponentUVE entities will not run this session");
    }
    m_scriptBindingContext.inputSystem = m_inputSystem.get();
    m_scriptEngineCallBindings = MakeScriptGameplayBindingsUVE(m_scriptBindingContext);

    // SaveGameSystem thirty-second: needs SceneSerializer (composed by reference) and
    // EngineConfigUVE::saveDirectoryPath.
    m_saveGameSystem = std::make_unique<Save::SaveGameSystemUVE>(*m_sceneSerializer, m_config.saveDirectoryPath);

    // CheckpointManager thirty-third: needs SaveGameSystem (composed by reference) and
    // EngineConfigUVE::autoSaveIntervalSecondsUVE. Preserve the canonical engine version in
    // checkpoint metadata; the checkpoint manager overlays playtime for each save.
    const VersionUVE engineVersion = GetEngineVersionUVE();
    Save::GameStateMetadataUVE checkpointMetadata;
    checkpointMetadata.engineVersionMajor = engineVersion.major;
    checkpointMetadata.engineVersionMinor = engineVersion.minor;
    checkpointMetadata.engineVersionPatch = engineVersion.patch;
    checkpointMetadata.engineVersionBuild = engineVersion.build;
    m_checkpointManager = std::make_unique<Save::CheckpointManagerUVE>(
        *m_saveGameSystem, m_config.autoSaveIntervalSecondsUVE, checkpointMetadata);

    // ConfigManager last: it immediately calls LoadUVE(), which logs its
    // outcome (missing/malformed/success) through the Logger constructed
    // above — so Logger must already exist by this point.
    auto configManager = std::make_unique<Config::ConfigManagerUVE>();
    configManager->LoadUVE(m_config.settingsFilePath);
    m_configManager = std::move(configManager);

    m_services.emplace(*m_logger, *m_timer, *m_eventSystem, *m_memoryManager, *m_threadPool,
                                                 *m_commandLine, *m_configManager, *m_entityManager, *m_sceneGraph,
                         *m_assetDatabase, *m_projectFileIndex, *m_derivedArtifactCache, *m_projectChangeWatcher,
                         *m_sceneSerializer,
                         *m_prefabSystem, *m_particleRuntime, *m_hotReload, *m_assetManager, *m_assetImporter, *m_assetImportQueue,
                         *m_assetBundle, *m_fileSystem,

                        *m_renderDevice, *m_shaderManager, *m_renderSystem, *m_computeSystem, *m_cameraSystem,
                        *m_meshRenderer, *m_lightSystem, *m_renderer3D, *m_collisionSystem, *m_physicsSystem,
                        *m_physicsQuerySystem, *m_raycastSystem, *m_physicsConstraintSystem, *m_inputSystem,
                        *m_gamepadInputSystem, *m_mobileInputSystem, *m_mobileGestureSystem,
                        *m_audioDevice, *m_audioSystem,
                        *m_audioSourceSystem, *m_saveGameSystem, *m_checkpointManager,
                        *m_windowManager);

    TransitionStateUVE(EngineStateUVE::Running);
    UVE_INFO("EngineCoreUVE: initialized");
}

bool EngineCoreUVE::Load() {
    if (m_windowCreationFailedUVE) {
        UVE_FATAL("EngineCoreUVE: Load() aborting - window/GL context creation failed during Init()");
        return false;
    }
    UVE_INFO("EngineCoreUVE: Load() - nothing to load this increment");
    return true;
}

void EngineCoreUVE::BeginFrame() {
    m_frameStartTime = std::chrono::steady_clock::now();
    m_timer->Tick();
    ++m_frameStats.frameNumber;
    m_frameStats.deltaTimeSeconds = m_timer->GetDeltaTimeUVE();
    m_frameStats.totalTimeSeconds = m_timer->GetTotalTimeUVE();
    UVE_TRACE("BeginFrame {}", m_frameStats.frameNumber);
}

void EngineCoreUVE::SyncParticleRuntimeUVE() {
    if (m_particleRuntime == nullptr) {
        return;
    }

    std::vector<Scene::EntityUVE> authoredEmitters;
    const bool simulationPaused = m_simulationExecutionMode == SimulationExecutionModeUVE::Paused;
    m_entityManager->ForEachUVE<Scene::ParticleEmitterComponentUVE>(
        [this, &authoredEmitters, simulationPaused](const Scene::EntityUVE entity,
                                                    const Scene::ParticleEmitterComponentUVE& component) {
            authoredEmitters.push_back(entity);
            const Scene::ParticleRuntimeSnapshotUVE currentSnapshot = m_particleRuntime->GetSnapshotUVE();
            bool budgetMatches = false;
            for (const Scene::ParticleRuntimeInstanceSnapshotUVE& instance : currentSnapshot.instances) {
                if (instance.entity == entity) {
                    budgetMatches = instance.maxParticles == component.maxParticles;
                    break;
                }
            }
            if (!m_particleRuntime->HasInstanceUVE(entity)) {
                static_cast<void>(m_particleRuntime->AttachDetailedUVE(entity, component));
            } else if (!budgetMatches) {
                static_cast<void>(m_particleRuntime->DetachDetailedUVE(entity));
                static_cast<void>(m_particleRuntime->AttachDetailedUVE(entity, component));
            }
            // Same rule as scripts: a Pausable emitter freezes mid-flight while the simulation is
            // paused, rather than continuing to integrate behind a paused game.
            static_cast<void>(m_particleRuntime->SetEnabledDetailedUVE(
                entity, Scene::IsProcessingUVE(ResolvedProcessModeUVE(*m_sceneGraph, entity), simulationPaused)));
        });

    const Scene::ParticleRuntimeSnapshotUVE runtimeSnapshot = m_particleRuntime->GetSnapshotUVE();
    for (const Scene::ParticleRuntimeInstanceSnapshotUVE& instance : runtimeSnapshot.instances) {
        if (!m_entityManager->IsAliveUVE(instance.entity) ||
            std::find(authoredEmitters.begin(), authoredEmitters.end(), instance.entity) == authoredEmitters.end()) {
            static_cast<void>(m_particleRuntime->DetachDetailedUVE(instance.entity));
        }
    }

    const float deltaSeconds = static_cast<float>(m_timer->GetDeltaTimeUVE());
    if (deltaSeconds > 0.0F) {
        static_cast<void>(m_particleRuntime->SimulateDetailedUVE(deltaSeconds, m_config.gravity));
    }
}

void EngineCoreUVE::SyncUIRuntimeUVE() {
    if (m_inputSystem == nullptr) {
        return;
    }
    m_uiRuntime.TickUVE(*m_entityManager, *m_inputSystem);
}

void EngineCoreUVE::SyncScriptRuntimeUVE() {
    m_entityManager->ForEachUVE<Scene::ScriptComponentUVE>(
        [this](const Scene::EntityUVE entity, const Scene::ScriptComponentUVE& component) {
            if (m_scriptRuntime.HasInstanceUVE(entity) ||
                m_scriptReconcileFailedEntities.contains(entity)) {
                return;
            }
            const Scripting::ScriptAssetLoadResultUVE loaded = Scripting::ScriptAssetLoaderUVE::LoadSchemaUVE(
                component, *m_fileSystem, Scripting::ScriptGraphPersistenceLimitsUVE{});
            if (loaded.IsNoScriptUVE()) {
                // No script assigned yet - a normal authoring state, not a failure. Skip silently
                // and re-check next frame in case a path gets assigned later.
                return;
            }
            if (!loaded.IsLoadedUVE()) {
                m_scriptReconcileFailedEntities.insert(entity);
                UVE_ERROR("EngineCoreUVE: ScriptComponentUVE on entity failed to load its script asset "
                          "\"{}\": {}",
                          component.scriptAssetPath, loaded.message);
                return;
            }
            const Scripting::ScriptComponentRuntimeOwnershipResultUVE reconciled =
                Scripting::ScriptComponentRuntimeOwnershipUVE::ReconcileUVE(
                    component, loaded.schema->graph, m_scriptNodeRegistry, m_scriptRuntime, entity);
            if (!reconciled.IsAcceptedUVE()) {
                m_scriptReconcileFailedEntities.insert(entity);
                UVE_ERROR("EngineCoreUVE: ScriptComponentUVE on entity failed to attach to ScriptRuntimeUVE: {}",
                          reconciled.message);
            }
        });

    // Process mode decides which instances tick this frame, and priority decides their order.
    // Pausable - the default - stops while the simulation is paused, which is what pausing Play
    // mode now means for scripts; before this, pausing stopped physics and left every script
    // running. WhenPaused and Always are how a pause menu keeps working over a stopped world.
    const bool simulationPaused = m_simulationExecutionMode == SimulationExecutionModeUVE::Paused;
    m_entityManager->ForEachUVE<Scene::ScriptComponentUVE>(
        [this, simulationPaused](const Scene::EntityUVE entity, const Scene::ScriptComponentUVE&) {
            if (!m_scriptRuntime.HasInstanceUVE(entity)) {
                return;
            }
            static_cast<void>(m_scriptRuntime.SetEnabledUVE(
                entity, Scene::IsProcessingUVE(ResolvedProcessModeUVE(*m_sceneGraph, entity), simulationPaused)));
            static_cast<void>(
                m_scriptRuntime.SetPriorityUVE(entity, ProcessSettingsUVE(*m_entityManager, entity).priority));
        });

    static_cast<void>(m_scriptRuntime.TickUVE(
        Scripting::ScriptVmExecutionOptionsUVE{.engineCallBindings = &m_scriptEngineCallBindings}));
}

void EngineCoreUVE::SyncCharacterControllersUVE(const float fixedDeltaTimeSeconds) {
    if (fixedDeltaTimeSeconds <= 0.0F) {
        return;
    }

    Math::Vector3UVE horizontalInput{};
    if (m_inputSystem->IsKeyDownUVE(Input::KeyCodeUVE::W)) {
        horizontalInput.z -= 1.0F;
    }
    if (m_inputSystem->IsKeyDownUVE(Input::KeyCodeUVE::S)) {
        horizontalInput.z += 1.0F;
    }
    if (m_inputSystem->IsKeyDownUVE(Input::KeyCodeUVE::A)) {
        horizontalInput.x -= 1.0F;
    }
    if (m_inputSystem->IsKeyDownUVE(Input::KeyCodeUVE::D)) {
        horizontalInput.x += 1.0F;
    }
    const float horizontalInputLengthSquared =
        horizontalInput.x * horizontalInput.x + horizontalInput.z * horizontalInput.z;
    if (horizontalInputLengthSquared > 1.0F) {
        const float inverseLength = 1.0F / std::sqrt(horizontalInputLengthSquared);
        horizontalInput.x *= inverseLength;
        horizontalInput.z *= inverseLength;
    }
    const bool jumpRequested = m_inputSystem->WasKeyPressedThisFrameUVE(Input::KeyCodeUVE::Space);

    // Collected and ordered rather than iterated in place: controllers push against each other
    // through MoveWithToIUVE, so which one moves first changes the outcome, and physicsPriority is
    // how an author decides that instead of archetype storage order deciding it for them.
    for (const Scene::EntityUVE entity :
         CollectFixedStepOrderUVE<Scene::CharacterControllerComponentUVE>(*m_entityManager, *m_sceneGraph)) {
        Scene::CharacterControllerComponentUVE& characterController =
            m_entityManager->GetComponentUVE<Scene::CharacterControllerComponentUVE>(entity);
        if (!m_entityManager->HasComponentUVE<Scene::ColliderComponentUVE>(entity)) {
            continue;
        }
        if (m_entityManager->HasComponentUVE<Scene::RigidBodyComponentUVE>(entity) &&
            !m_entityManager->GetComponentUVE<Scene::RigidBodyComponentUVE>(entity).isKinematic) {
            continue;
        }

        if (jumpRequested && characterController.isGrounded) {
            characterController.verticalVelocity =
                std::sqrt(2.0F * std::abs(m_config.gravity.y) * characterController.gravityScale *
                          characterController.jumpHeight);
            characterController.isGrounded = false;
        } else {
            characterController.verticalVelocity +=
                m_config.gravity.y * characterController.gravityScale * fixedDeltaTimeSeconds;
        }

        const Math::Vector3UVE desiredDisplacement{
            horizontalInput.x * characterController.moveSpeed * fixedDeltaTimeSeconds,
            characterController.verticalVelocity * fixedDeltaTimeSeconds,
            horizontalInput.z * characterController.moveSpeed * fixedDeltaTimeSeconds};

        Physics::CharacterControllerInputUVE controllerInput{};
        controllerInput.entity = entity;
        controllerInput.desiredDisplacement = desiredDisplacement;
        controllerInput.maximumStepHeight = 0.3F;
        const Physics::CharacterControllerMoveResultUVE result = Physics::CharacterControllerUVE::MoveWithToIUVE(
            *m_entityManager, *m_sceneGraph, *m_collisionSystem, controllerInput);
        if (!result.IsAcceptedUVE()) {
            continue;
        }

        characterController.isGrounded = result.grounded;
        if (result.grounded && characterController.verticalVelocity < 0.0F) {
            // A small, consistent downward "stick to ground" velocity (rather than exactly
            // zero) keeps every subsequent step's displacement driving slightly into the
            // surface - otherwise a resting controller's next-step displacement can shrink to
            // whatever one frame of gravity alone produces, which is sometimes too small for
            // MoveWithToIUVE's own contact detection to register consistently, flickering
            // isGrounded true/false every other fixed step even though the position never
            // visibly moves.
            characterController.verticalVelocity = -0.5F;
        }
    }
}

void EngineCoreUVE::SyncProjectile3DNodesUVE(const float fixedDeltaTimeSeconds) {
    if (fixedDeltaTimeSeconds <= 0.0F) {
        return;
    }

    for (const Scene::EntityUVE entity :
         CollectFixedStepOrderUVE<Scene::Projectile3DNodeComponentUVE>(*m_entityManager, *m_sceneGraph)) {
        Scene::Projectile3DNodeComponentUVE& projectile =
            m_entityManager->GetComponentUVE<Scene::Projectile3DNodeComponentUVE>(entity);
        if (!projectile.active || !m_entityManager->HasComponentUVE<Scene::TransformComponentUVE>(entity)) {
            continue;
        }

        projectile.velocity += projectile.acceleration * fixedDeltaTimeSeconds;

        Scene::TransformComponentUVE localTransform =
            m_entityManager->GetComponentUVE<Scene::TransformComponentUVE>(entity);
        localTransform.localPosition += projectile.velocity * fixedDeltaTimeSeconds;
        m_sceneGraph->SetLocalTransformUVE(*m_entityManager, entity, localTransform);

        projectile.remainingLifetime -= fixedDeltaTimeSeconds;
        if (projectile.remainingLifetime <= 0.0F) {
            projectile.remainingLifetime = 0.0F;
            projectile.active = false;
        }
    }
}

void EngineCoreUVE::SyncCollisionLifecycleUVE() {
    const std::vector<Physics::CollisionPairUVE> pairs = m_collisionSystem->DetectCollisionsUVE(*m_entityManager);
    m_collisionLifecycleReport = m_collisionLifecycleTracker.UpdateUVE(pairs);
    m_scriptBindingContext.collisionTransitionsThisTick = &m_collisionLifecycleReport.transitions;
}

void EngineCoreUVE::SyncRayCast3DNodesUVE() {
    m_entityManager->ForEachUVE<Scene::RayCast3DNodeComponentUVE>(
        [this](const Scene::EntityUVE entity, Scene::RayCast3DNodeComponentUVE& rayCast) {
            if (!rayCast.enabled || !m_entityManager->HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
                rayCast.hit = false;
                return;
            }

            const auto& worldTransform = m_entityManager->GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
            Physics::RaycastQueryUVE query{};
            query.ray.origin = worldTransform.worldPosition;
            query.ray.direction = Math::RotateVectorUVE(worldTransform.worldRotation, rayCast.direction);
            query.maxDistance = rayCast.length;
            query.layerMask = rayCast.collisionMask;
            query.ignoreEntity = entity;

            const std::optional<Physics::RaycastHitUVE> result = m_raycastSystem->RaycastUVE(*m_entityManager, query);
            if (!result.has_value()) {
                rayCast.hit = false;
                return;
            }

            rayCast.hit = true;
            rayCast.hitPosition = result->point;
            rayCast.hitNormal = result->normal;
            rayCast.hitEntity = result->entity;
        });
}

void EngineCoreUVE::SyncSpringArm3DNodesUVE(const float fixedDeltaTimeSeconds) {
    for (const Scene::EntityUVE entity :
         CollectFixedStepOrderUVE<Scene::SpringArm3DNodeComponentUVE>(*m_entityManager, *m_sceneGraph)) {
        Scene::SpringArm3DNodeComponentUVE& springArm =
            m_entityManager->GetComponentUVE<Scene::SpringArm3DNodeComponentUVE>(entity);
        if (!springArm.enabled || !Scene::IsSpringArm3DNodeComponentValidUVE(springArm) ||
            !m_entityManager->HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
            continue;
        }

        const auto& worldTransform =
            m_entityManager->GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
        Physics::RaycastQueryUVE query{};
        query.ray.origin = worldTransform.worldPosition;
        // The arm extends along the pivot's local +Z - behind it, since the camera
        // convention looks down -Z (same convention SyncRayCast3DNodesUVE applies to the
        // authored ray direction).
        query.ray.direction =
            Math::RotateVectorUVE(worldTransform.worldRotation, {0.0F, 0.0F, 1.0F});
        query.maxDistance = springArm.armLength;
        query.layerMask = springArm.collisionMask;
        query.ignoreEntity = entity;

        const std::optional<Physics::RaycastHitUVE> result =
            m_raycastSystem->RaycastUVE(*m_entityManager, query);
        const float targetLength = Scene::ResolveSpringArm3DTargetUVE(
            result.has_value() ? std::optional<float>{result->distance} : std::nullopt,
            springArm.margin, springArm.armLength);

        const float previousLength = springArm.currentLength;
        springArm.currentLength = Scene::ResolveSpringArm3DLengthUVE(
            previousLength, targetLength, springArm.smoothing, fixedDeltaTimeSeconds);
        const float lengthDelta = springArm.currentLength - previousLength;
        if (lengthDelta == 0.0F || !m_entityManager->HasComponentUVE<Scene::TransformComponentUVE>(entity)) {
            continue;
        }

        // Every direct child rides the delta along the arm's local Z; because the shift is
        // the change in length and not an absolute rewrite, authored child offsets survive
        // and an unobstructed arm restores the authored pose exactly.
        for (const Scene::EntityUVE child :
             m_sceneGraph->GetChildrenUVE(*m_entityManager, entity)) {
            if (!m_entityManager->HasComponentUVE<Scene::TransformComponentUVE>(child)) {
                continue;
            }
            Scene::TransformComponentUVE childTransform =
                m_entityManager->GetComponentUVE<Scene::TransformComponentUVE>(child);
            childTransform.localPosition.z += lengthDelta;
            m_sceneGraph->SetLocalTransformUVE(*m_entityManager, child, childTransform);
        }
    }
}

namespace {

/// Snapshot of one hurtbox's world-space strike volume, taken once per frame by
/// EngineCoreUVE::SyncHitbox3DNodesUVE() pass 1.
struct HurtboxCandidateUVE final {
    Scene::EntityUVE entity;
    Math::Vector3UVE center;
    Math::Vector3UVE halfExtents;
    Math::QuaternionUVE rotation;
    std::uint32_t collisionLayer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    std::string damageChannel;
};

/// Snapshot of one character-controller interactor's overlap volume, taken once per frame by
/// EngineCoreUVE::SyncInteractionArea3DNodesUVE() pass 1: world pose plus broad-phase half
/// extents and layer/mask from the entity's own ColliderComponentUVE.
struct InteractionInteractorCandidateUVE final {
    Scene::EntityUVE entity;
    Math::Vector3UVE center;
    Math::Vector3UVE halfExtents;
    Math::QuaternionUVE rotation;
    std::uint32_t collisionLayer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
};

} // namespace

void EngineCoreUVE::SyncHitbox3DNodesUVE() {
    // Pass 1 (read-only): snapshot every enabled, valid hurtbox that has a world transform, so
    // the mutation pass can evaluate every hitbox against a stable candidate set without
    // holding ECS iteration open across a second ForEachUVE. The candidate set is deliberately
    // unbounded - capping it would mean silently pretending hurtboxes beyond the cap do not
    // exist; only the per-hitbox strike LIST is bounded, and it reports its own overflow.
    std::vector<HurtboxCandidateUVE> candidates;
    m_entityManager->ForEachUVE<Scene::WorldTransformComponentUVE, Scene::Hurtbox3DNodeComponentUVE>(
        [&candidates](const Scene::EntityUVE entity, const Scene::WorldTransformComponentUVE& worldTransform,
                      const Scene::Hurtbox3DNodeComponentUVE& hurtbox) {
            if (!hurtbox.enabled || !Scene::IsHurtbox3DNodeComponentValidUVE(hurtbox)) {
                return;
            }
            HurtboxCandidateUVE candidate;
            candidate.entity = entity;
            candidate.center = worldTransform.worldPosition;
            candidate.halfExtents = hurtbox.halfExtents;
            if (!Math::TryNormalizeUVE(worldTransform.worldRotation, candidate.rotation)) {
                candidate.rotation = {}; // degenerate rotation falls back to identity
            }
            candidate.collisionLayer = hurtbox.collisionLayer;
            candidate.collisionMask = hurtbox.collisionMask;
            candidate.damageChannel = hurtbox.damageChannel;
            candidates.push_back(std::move(candidate));
        });

    // Pass 2: refresh every hitbox's runtime strike state against that snapshot.
    m_entityManager->ForEachUVE<Scene::Hitbox3DNodeComponentUVE>(
        [this, &candidates](const Scene::EntityUVE entity, Scene::Hitbox3DNodeComponentUVE& hitbox) {
            hitbox.strikeCount = 0U;
            hitbox.strikesTruncated = false;
            if (!hitbox.enabled || !Scene::IsHitbox3DNodeComponentValidUVE(hitbox) ||
                !m_entityManager->HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
                return;
            }

            const Scene::WorldTransformComponentUVE& worldTransform =
                m_entityManager->GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
            Math::QuaternionUVE hitboxRotation{};
            if (!Math::TryNormalizeUVE(worldTransform.worldRotation, hitboxRotation)) {
                hitboxRotation = {}; // degenerate rotation falls back to identity
            }

            for (const HurtboxCandidateUVE& candidate : candidates) {
                if (candidate.entity == entity) {
                    continue; // a hitbox never strikes a hurtbox on its own entity
                }
                if ((candidate.collisionLayer & hitbox.collisionMask) == 0U ||
                    (hitbox.collisionLayer & candidate.collisionMask) == 0U) {
                    continue; // symmetric layer/mask acceptance, AreaOverlapSystemUVE-style
                }
                if (candidate.damageChannel != hitbox.damageChannel) {
                    continue; // a strike requires matching damage channels
                }
                const std::optional<Math::PenetrationUVE> penetration =
                    Physics::Detail::ComputeOrientedBoxOrientedBoxPenetrationUVE(
                        worldTransform.worldPosition, hitbox.halfExtents, hitboxRotation,
                        candidate.center, candidate.halfExtents, candidate.rotation);
                if (!penetration.has_value()) {
                    continue; // no overlap (touching boundaries are not strikes either)
                }
                if (hitbox.strikeCount >= Scene::kMaximumHitbox3DStrikesUVE) {
                    hitbox.strikesTruncated = true;
                    break;
                }
                hitbox.strikes[hitbox.strikeCount] =
                    Scene::Hitbox3DStrikeUVE{candidate.entity, penetration->depth};
                ++hitbox.strikeCount;
            }
        });
}

void EngineCoreUVE::SyncInteractionArea3DNodesUVE() {
    // Pass 1 (read-only): snapshot every interactor - a character controller carrying a valid
    // collider and a world transform - so the mutation pass evaluates every area against a
    // stable set without holding ECS iteration open across a second ForEachUVE. The set is
    // deliberately unbounded (same call SyncHitbox3DNodesUVE() made for hurtboxes): capping it
    // would silently pretend interactors beyond the cap do not exist; only each area's stored
    // list is bounded, and it reports its own overflow.
    std::vector<InteractionInteractorCandidateUVE> interactors;
    std::vector<Scene::EntityUVE> interactorEntities;
    m_entityManager->ForEachUVE<Scene::WorldTransformComponentUVE,
                                Scene::CharacterControllerComponentUVE,
                                Scene::ColliderComponentUVE>(
        [&interactors, &interactorEntities](
            const Scene::EntityUVE entity, const Scene::WorldTransformComponentUVE& worldTransform,
            const Scene::CharacterControllerComponentUVE&, const Scene::ColliderComponentUVE& collider) {
            if (!Scene::IsColliderComponentValidUVE(collider)) {
                return; // an invalid collider (e.g. a zero layer) can never interact
            }
            InteractionInteractorCandidateUVE candidate;
            candidate.entity = entity;
            candidate.center = worldTransform.worldPosition;
            candidate.halfExtents = Scene::GetColliderLocalHalfExtentsUVE(collider);
            if (!Math::TryNormalizeUVE(worldTransform.worldRotation, candidate.rotation)) {
                candidate.rotation = {}; // degenerate rotation falls back to identity
            }
            candidate.collisionLayer = collider.collisionLayer;
            candidate.collisionMask = collider.collisionMask;
            interactorEntities.push_back(entity);
            interactors.push_back(std::move(candidate));
        });

    const std::optional<Scene::EntityUVE> primaryInteractor =
        Scene::ResolvePrimaryInteractorUVE(interactorEntities);
    const Scene::EntityUVE primaryEntity =
        primaryInteractor.value_or(Scene::kInvalidEntityUVE);

    // Pass 2: refresh every area's runtime state against that snapshot, gathering the primary
    // interactor's focus candidates on the way. Every gate fails closed: anything that stops an
    // area participating this frame clears its runtime state in full, never leaving a stale
    // interactor list or focus flag behind.
    std::vector<Scene::InteractionFocusCandidateUVE> focusCandidates;
    m_entityManager->ForEachUVE<Scene::InteractionArea3DNodeComponentUVE>(
        [this, &interactors, primaryEntity, &focusCandidates](
            const Scene::EntityUVE entity, Scene::InteractionArea3DNodeComponentUVE& area) {
            area.interactorCount = 0U;
            area.interactorsTruncated = false;
            area.focusedByPrimaryInteractor = false;
            if (!area.enabled || !Scene::IsInteractionArea3DNodeComponentValidUVE(area) ||
                !m_entityManager->HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
                return;
            }

            const Scene::WorldTransformComponentUVE& worldTransform =
                m_entityManager->GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
            Math::QuaternionUVE areaRotation{};
            if (!Math::TryNormalizeUVE(worldTransform.worldRotation, areaRotation)) {
                areaRotation = {}; // degenerate rotation falls back to identity
            }
            const std::size_t candidateCap = Scene::ResolveInteractionAreaCandidateCapUVE(
                area.maximumCandidates, Scene::kMaximumInteractionAreaCandidatesUVE);

            for (const InteractionInteractorCandidateUVE& interactor : interactors) {
                if (interactor.entity == entity) {
                    continue; // an area never lists the interactor living on its own entity
                }
                if ((interactor.collisionLayer & area.collisionMask) == 0U ||
                    (area.collisionLayer & interactor.collisionMask) == 0U) {
                    continue; // symmetric layer/mask acceptance, AreaOverlapSystemUVE's rule
                }
                const std::optional<Math::PenetrationUVE> penetration =
                    Physics::Detail::ComputeOrientedBoxOrientedBoxPenetrationUVE(
                        worldTransform.worldPosition, area.halfExtents, areaRotation,
                        interactor.center, interactor.halfExtents, interactor.rotation);
                if (!penetration.has_value()) {
                    continue; // no overlap (touching boundaries are not overlaps either)
                }
                if (interactor.entity == primaryEntity) {
                    // Rank by squared center distance - nearest center is the focus candidate;
                    // no sqrt needed for a comparison.
                    focusCandidates.push_back(Scene::InteractionFocusCandidateUVE{
                        entity,
                        Math::LengthSquaredUVE(worldTransform.worldPosition - interactor.center)});
                }
                if (area.interactorCount >= candidateCap) {
                    area.interactorsTruncated = true;
                    break;
                }
                area.interactors[area.interactorCount] = interactor.entity;
                ++area.interactorCount;
            }
        });

    // Pass 3: exactly one area gets the primary interactor's focus - nearest center wins, ties
    // deterministically by (index,generation); a scene with no eligible interactor focuses
    // nothing (ResolvePrimaryInteractorUVE already failed closed above) and stale focus flags
    // were all cleared in pass 2.
    const std::optional<Scene::EntityUVE> focusedArea =
        Scene::ResolveInteractionFocusUVE(focusCandidates);
    if (focusedArea.has_value() &&
        m_entityManager->HasComponentUVE<Scene::InteractionArea3DNodeComponentUVE>(*focusedArea)) {
            m_entityManager->GetComponentUVE<Scene::InteractionArea3DNodeComponentUVE>(*focusedArea)
            .focusedByPrimaryInteractor = true;
    }
}

void EngineCoreUVE::SyncLevelStreamer3DNodesUVE() {
    // Bookkeeping hygiene first: Play/Stop rebuilds every document handle, so a streamer entity
    // that no longer exists must drop its remembered roots AND its failure latch before anything
    // else looks at them - subsequent detection is always through IsAliveUVE(), never blind
    // destruction of a possibly-recycled handle.
    for (auto it = m_levelStreamerLoadedRoots.begin(); it != m_levelStreamerLoadedRoots.end();) {
        if (!m_entityManager->IsAliveUVE(it->first)) {
            it = m_levelStreamerLoadedRoots.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_levelStreamerLoadFailures.begin(); it != m_levelStreamerLoadFailures.end();) {
        if (!m_entityManager->IsAliveUVE(it->first)) {
            it = m_levelStreamerLoadFailures.erase(it);
        } else {
            ++it;
        }
    }

    // Pass 1 (read-only): the viewer point cloud for this tick. The active camera is the Unreal
    // convention (streaming follows the eye); every character controller is a Frostbite-style
    // listener source (split-screen clients each drag their own content in). Finite world poses
    // only - a degenerate camera/contoller pose simply stops being a viewer, it never poisons
    // the decision for everyone else.
    std::vector<Math::Vector3UVE> viewerPositions;
    if (m_activeCamera != Scene::kInvalidEntityUVE &&
        m_entityManager->HasComponentUVE<Scene::WorldTransformComponentUVE>(m_activeCamera)) {
        const Scene::WorldTransformComponentUVE& cameraWorld =
            m_entityManager->GetComponentUVE<Scene::WorldTransformComponentUVE>(m_activeCamera);
        viewerPositions.push_back(cameraWorld.worldPosition);
    }
    m_entityManager->ForEachUVE<Scene::WorldTransformComponentUVE,
                                Scene::CharacterControllerComponentUVE>(
        [&viewerPositions](const Scene::EntityUVE, const Scene::WorldTransformComponentUVE& world,
                           const Scene::CharacterControllerComponentUVE&) {
            viewerPositions.push_back(world.worldPosition);
        });

    // Pass 2 (read-only snapshot): the safe traversal. The verdict pass below can CREATE
    // entities (levelPath restores any component mix, including another streamer) and DESTROY
    // them (an unload), so iterating LevelStreamer3D components directly while mutating would
    // walk over an exploding candlebox: snapshot handles + world poses here, then close the
    // ForEachUVE before any state moves.
    struct StreamerSnapshotUVE final {
        Scene::EntityUVE entity;
        Math::Vector3UVE worldPosition;
    };
    std::vector<StreamerSnapshotUVE> streamers;
    m_entityManager->ForEachUVE<Scene::WorldTransformComponentUVE,
                                Scene::LevelStreamer3DNodeComponentUVE>(
        [&streamers](const Scene::EntityUVE entity, const Scene::WorldTransformComponentUVE& world,
                     const Scene::LevelStreamer3DNodeComponentUVE&) {
            if (world.dirty) {
                return; // a streamer whose world pose is out of date must never fake distance math
            }
            streamers.push_back(StreamerSnapshotUVE{entity, world.worldPosition});
        });

    // Pass 3: the verdict per streamer, then the state change it orders. Load requests are
    // time-sliced by kMaximumLevelStreamer3DLoadsPerTickUVE - over-budget requests simply remain
    // pending, and the same verdict re-issues next tick (Frostbite burst budgeting).
    std::size_t loadsIssuedThisTick = 0U;
    for (const StreamerSnapshotUVE& snapshot : streamers) {
        if (!m_entityManager->IsAliveUVE(snapshot.entity) ||
            !m_entityManager->HasComponentUVE<Scene::LevelStreamer3DNodeComponentUVE>(snapshot.entity)) {
            continue; // died inside this very pass (unloaded as part of a parent streamer's subtree)
        }
        Scene::LevelStreamer3DNodeComponentUVE& streamer =
            m_entityManager->GetComponentUVE<Scene::LevelStreamer3DNodeComponentUVE>(snapshot.entity);
        if (!Scene::IsLevelStreamer3DNodeComponentValidUVE(streamer)) {
            continue; // an invalid authoring piece never decides anything
        }

        Scene::LevelStreamer3DStreamingFrameUVE frame;
        frame.enabled = streamer.enabled;
        frame.loaded = streamer.loaded;
        frame.loadRequested = streamer.loadRequested;
        frame.loadDistance = streamer.loadDistance;
        frame.unloadDistance = streamer.unloadDistance;
        if (const std::optional<float> nearest =
                Scene::ResolveLevelStreamer3DNearestViewerDistanceSquaredUVE(
                    snapshot.worldPosition, viewerPositions);
            nearest.has_value()) {
            frame.hasViewer = true;
            frame.nearestViewerDistanceSquared = *nearest;
        }

        const Scene::LevelStreamer3DActionUVE action =
            Scene::ResolveLevelStreamer3DStreamingActionUVE(frame);
        if (action == Scene::LevelStreamer3DActionUVE::RequestLoad) {
            if (m_levelStreamerLoadFailures.find(snapshot.entity) != m_levelStreamerLoadFailures.end()) {
                continue; // latched earlier failure: fail-closed, never a retry storm
            }
            if (loadsIssuedThisTick >= Scene::kMaximumLevelStreamer3DLoadsPerTickUVE) {
                continue; // over budget: stay pending - the same verdict re-issues next tick
            }
            ++loadsIssuedThisTick;
            streamer.loadRequested = true; // set for the duration of the synchronous call
            const std::vector<Scene::EntityUVE> freshRoots =
                m_sceneSerializer->LoadUVE(*m_entityManager, std::filesystem::path{streamer.levelPath});
            if (freshRoots.empty()) {
                streamer.loadRequested = false;
                streamer.loaded = false;
                m_levelStreamerLoadFailures.emplace(snapshot.entity, true);
                UVE_ERROR("EngineCoreUVE: LevelStreamer3D could not load '{}' - latched off "
                          "until the session restarts (fail-closed, never retried)",
                          streamer.levelPath);
                continue;
            }
            m_levelStreamerLoadedRoots[snapshot.entity] = freshRoots;
            streamer.loaded = true;
            streamer.loadRequested = false;
            continue;
        }
        if (action == Scene::LevelStreamer3DActionUVE::RequestUnload) {
            const auto roots = m_levelStreamerLoadedRoots.find(snapshot.entity);
            if (roots != m_levelStreamerLoadedRoots.end()) {
                // Destroy deepest-first so a parent's destruction never observes an already-
                // dead child handle; IsAliveUVE() keeps possibly-recycled handles out of the way.
                const std::function<void(Scene::EntityUVE)> destroySubtree =
                    [this, &destroySubtree](const Scene::EntityUVE current) {
                        if (!m_entityManager->IsAliveUVE(current)) {
                            return;
                        }
                        const std::vector<Scene::EntityUVE> children =
                            m_sceneGraph->GetChildrenUVE(*m_entityManager, current);
                        for (const Scene::EntityUVE child : children) {
                            destroySubtree(child);
                        }
                        m_entityManager->DestroyEntityUVE(current);
                    };
                for (const Scene::EntityUVE root : roots->second) {
                    destroySubtree(root);
                }
                m_levelStreamerLoadedRoots.erase(roots);
            }
            m_levelStreamerLoadFailures.erase(snapshot.entity); // unloaded: a future load may retry
            streamer.loaded = false;
            streamer.loadRequested = false;
            continue;
        }
        // LevelStreamer3DActionUVE::None - the hysteresis band holds the line, nothing moves.
    }
}

void EngineCoreUVE::SyncReflectionProbe3DNodesUVE() {
    // Pass 1 (read-only): the eye position for this tick. Reflections only exist for the camera.
    std::optional<Math::Vector3UVE> cameraPosition;
    if (m_activeCamera != Scene::kInvalidEntityUVE &&
        m_entityManager->HasComponentUVE<Scene::WorldTransformComponentUVE>(m_activeCamera)) {
        const Math::Vector3UVE& position =
            m_entityManager->GetComponentUVE<Scene::WorldTransformComponentUVE>(m_activeCamera)
                .worldPosition;
        if (std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z)) {
            cameraPosition = position;
        }
    }

    // Pass 2 (read-only snapshot): snapshots BEFORE any mutation - the sync never creates or
    // destroys entities, but it DOES rewrite runtime fields on the components it walks, and a
    // ForEachUVE-compatible snapshot pass keeps iterator safety explicit (streamer convention).
    struct ProbeSnapshotUVE final {
        Scene::EntityUVE entity;
        Math::Vector3UVE worldPosition;
        Math::QuaternionUVE worldRotation;
    };
    std::vector<ProbeSnapshotUVE> probes;
    m_entityManager->ForEachUVE<Scene::WorldTransformComponentUVE,
                                Scene::ReflectionProbe3DNodeComponentUVE>(
        [&probes](const Scene::EntityUVE entity, const Scene::WorldTransformComponentUVE& world,
                  const Scene::ReflectionProbe3DNodeComponentUVE&) {
            if (world.dirty) {
                return; // a stale world pose must never fake influence math
            }
            Math::QuaternionUVE rotation{};
            if (!Math::TryNormalizeUVE(world.worldRotation, rotation)) {
                rotation = {}; // degenerate rotation falls back to identity
            }
            probes.push_back(ProbeSnapshotUVE{entity, world.worldPosition, rotation});
        });

    // Pass 3: per probe, the influence weight on the eye plus the capture verdict. The local
    // offset undoes translation and then orientation with the conjugate (unit-quaternion
    // inverse) - the same pair editor code uses for world->local math.
    struct CaptureCandidateUVE final {
        Scene::EntityUVE entity;
        float cameraDistanceSquared = 0.0F;
    };
    std::vector<CaptureCandidateUVE> candidates;
    for (const ProbeSnapshotUVE& snapshot : probes) {
        Scene::ReflectionProbe3DNodeComponentUVE& probe =
            m_entityManager->GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(
                snapshot.entity);
        if (!Scene::IsReflectionProbe3DNodeComponentValidUVE(probe)) {
            probe.cameraInfluenceWeight = 0.0F;
            continue; // invalid authoring never influences, never captures
        }

        Scene::ReflectionProbe3DCaptureFrameUVE frame;
        frame.enabled = probe.enabled;
        frame.updateMode = probe.updateMode;
        frame.updateRequested = probe.updateRequested;
        frame.capturedOnce = probe.capturedOnce;
        float cameraDistanceSquared =
            std::numeric_limits<float>::max();
        if (cameraPosition.has_value()) {
            const Math::Vector3UVE offset = *cameraPosition - snapshot.worldPosition;
            // Pass 2 normalized the world rotation; the conjugate of a unit quaternion is its
            // inverse, so this undoes orientation with no re-normalization needed.
            const Math::QuaternionUVE inverse{
                -snapshot.worldRotation.x, -snapshot.worldRotation.y, -snapshot.worldRotation.z,
                snapshot.worldRotation.w};
            const Math::Vector3UVE localPoint = Math::RotateVectorUVE(inverse, offset);
            frame.cameraInfluenceWeight = Scene::ResolveReflectionProbe3DInfluenceWeightUVE(
                localPoint, Math::Vector3UVE{probe.size.x * 0.5F, probe.size.y * 0.5F,
                                             probe.size.z * 0.5F});
            frame.hasCameraViewer = true;
            cameraDistanceSquared = Math::LengthSquaredUVE(offset);
        }
        probe.cameraInfluenceWeight = frame.hasCameraViewer ? frame.cameraInfluenceWeight : 0.0F;

        if (Scene::ResolveReflectionProbe3DCaptureActionUVE(frame) ==
            Scene::ReflectionProbe3DCaptureActionUVE::Capture) {
            candidates.push_back(CaptureCandidateUVE{snapshot.entity, cameraDistanceSquared});
        }
    }

    // Budgeted service. naive nearest-first STARVES the far probe under continuous demand (with
    // more constant candidates than budget, the nearest always win), so the primary key is the
    // WAIT AGE first - the oldest waiter is served before anyone nearer - then camera distance,
    // then (index,generation). Deterministic across runs AND starvation-free, both measured.
    struct RankedCandidateUVE final {
        Scene::EntityUVE entity;
        float cameraDistanceSquared = 0.0F;
        std::uint32_t waitTicks = 0;
    };
    std::vector<RankedCandidateUVE> ranked;
    ranked.reserve(candidates.size());
    for (const CaptureCandidateUVE& candidate : candidates) {
        const Scene::ReflectionProbe3DNodeComponentUVE& waiting =
            m_entityManager->GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(
                candidate.entity);
        ranked.push_back(
            RankedCandidateUVE{candidate.entity, candidate.cameraDistanceSquared, waiting.captureWaitTicks});
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const RankedCandidateUVE& lhs, const RankedCandidateUVE& rhs) {
                  if (lhs.waitTicks != rhs.waitTicks) {
                      return lhs.waitTicks > rhs.waitTicks;
                  }
                  if (lhs.cameraDistanceSquared != rhs.cameraDistanceSquared) {
                      return lhs.cameraDistanceSquared < rhs.cameraDistanceSquared;
                  }
                  if (lhs.entity.index != rhs.entity.index) {
                      return lhs.entity.index < rhs.entity.index;
                  }
                  return lhs.entity.generation < rhs.entity.generation;
              });
    // Nobody's capture request DIES on an over-budget tick: Once probes simply have not captured
    // once yet, EveryFrame probes re-decide next tick, and OnDemand keeps its latch set until
    // serviced - while captureWaitTicks makes their queue position increasingly undeniable.
    const std::size_t servicedCount =
        std::min(ranked.size(), Scene::kMaximumReflectionProbeCapturesPerTickUVE);
    for (std::size_t i = 0; i < ranked.size(); ++i) {
        Scene::ReflectionProbe3DNodeComponentUVE& probe =
            m_entityManager->GetComponentUVE<Scene::ReflectionProbe3DNodeComponentUVE>(
                ranked[i].entity);
        if (i < servicedCount) {
            probe.capturedOnce = true;
            probe.updateRequested = false;
            ++probe.captureGeneration;
            probe.captureWaitTicks = 0;
        } else {
            ++probe.captureWaitTicks;
        }
    }
}

void EngineCoreUVE::SyncWorldPartition3DNodesUVE() {
    // The viewer cloud is identical to the streamer's: the eye plus every character controller,
    // finite poses only. Rebuild it here instead of sharing scratch so the two syncs stay
    // symmetric with each other while remaining readable one after the other in LateUpdate.
    std::vector<Math::Vector3UVE> viewerPositions;
    if (m_activeCamera != Scene::kInvalidEntityUVE &&
        m_entityManager->HasComponentUVE<Scene::WorldTransformComponentUVE>(m_activeCamera)) {
        viewerPositions.push_back(
            m_entityManager->GetComponentUVE<Scene::WorldTransformComponentUVE>(m_activeCamera)
                .worldPosition);
    }
    m_entityManager->ForEachUVE<Scene::WorldTransformComponentUVE,
                                Scene::CharacterControllerComponentUVE>(
        [&viewerPositions](const Scene::EntityUVE, const Scene::WorldTransformComponentUVE& world,
                           const Scene::CharacterControllerComponentUVE&) {
            viewerPositions.push_back(world.worldPosition);
        });

    std::vector<Scene::EntityUVE> partitions;
    m_entityManager->ForEachUVE<Scene::WorldPartition3DNodeComponentUVE>(
        [&partitions](const Scene::EntityUVE entity, const Scene::WorldPartition3DNodeComponentUVE&) {
            partitions.push_back(entity);
        });

    for (const Scene::EntityUVE partition : partitions) {
        // The whole partition is rebuilt every tick from its live subtree, so nothing can point
        // at a stale member: components are mutable only through this one place per tick.
        if (!m_entityManager->IsAliveUVE(partition) ||
            !m_entityManager->HasComponentUVE<Scene::WorldPartition3DNodeComponentUVE>(partition)) {
            continue; // Play/Stop mid-walk tore the world down under our feet - stay out of it
        }
        Scene::WorldPartition3DNodeComponentUVE& config =
            m_entityManager->GetComponentUVE<Scene::WorldPartition3DNodeComponentUVE>(partition);
        if (!Scene::IsWorldPartition3DNodeComponentValidUVE(config) ||
            !m_entityManager->HasComponentUVE<Scene::WorldTransformComponentUVE>(partition)) {
            continue; // an invalid partition configures nothing
        }
        const Math::Vector3UVE gridOrigin =
            m_entityManager->GetComponentUVE<Scene::WorldTransformComponentUVE>(partition)
                .worldPosition;

        // Breadth-first member collection: all descendants that carry a mesh. Nested partitions
        // stop the walk - a partition deep inside another's volume self-manages, so only the
        // closest owning partition ever stamps a membership it does not understand.
        struct MemberSnapshotUVE final {
            Scene::EntityUVE entity;
            Math::Vector3UVE worldPosition;
            std::optional<Scene::WorldPartition3DCellIdUVE> cell;
        };
        std::vector<MemberSnapshotUVE> members;
        std::vector<Scene::EntityUVE> pending;
        pending.push_back(partition);
        while (!pending.empty()) {
            const Scene::EntityUVE current = pending.back();
            pending.pop_back();
            if (!m_entityManager->IsAliveUVE(current)) {
                continue; // the scene graph hands over handles; destruction can invalidate them
            }
            const std::vector<Scene::EntityUVE> children =
                m_sceneGraph->GetChildrenUVE(*m_entityManager, current);
            for (const Scene::EntityUVE child : children) {
                if (!m_entityManager->IsAliveUVE(child)) {
                    continue;
                }
                if (m_entityManager->HasComponentUVE<Scene::WorldPartition3DNodeComponentUVE>(
                        child)) {
                    continue; // closest-ancestor-wins: the inner partition claims its subtree
                }
                if (m_entityManager->HasComponentUVE<Scene::MeshComponentUVE>(child) &&
                    m_entityManager->HasComponentUVE<Scene::WorldTransformComponentUVE>(child)) {
                    const Scene::WorldTransformComponentUVE& world =
                        m_entityManager->GetComponentUVE<Scene::WorldTransformComponentUVE>(child);
                    members.push_back(MemberSnapshotUVE{
                        child, world.worldPosition,
                        Scene::ResolveWorldPartition3DCellIdForPositionUVE(
                            config, gridOrigin, world.worldPosition)});
                }
                pending.push_back(child);
            }
        }

        // Membership attachment is idempotent: the engine owns this runtime component on every
        // mesh-carrying descendant, attaches when absent, and re-stamps the owner when an entity
        // has moved between partitions between ticks. AddComponentUVE mutates the ECS, but the
        // member list was snapshotted above, so nothing being iterated here can be invalidated.
        for (const MemberSnapshotUVE& member : members) {
            if (!m_entityManager->HasComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
                    member.entity)) {
                m_entityManager->AddComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
                    member.entity, Scene::WorldPartition3DMembershipComponentUVE{});
            }
            auto& membership =
                m_entityManager->GetComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
                    member.entity);
            membership.partition = partition;
        }

        if (!config.enabled) {
            // A disabled partition willingly shows its whole subtree - fail-open instead of
            // leaving yesterday's fade behind. loadedCellCount reads 0, never a stale count.
            for (const MemberSnapshotUVE& member : members) {
                auto& membership =
                    m_entityManager->GetComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
                        member.entity);
                membership.live = true;
            }
            config.loadedCellCount = 0U;
            continue;
        }

        // Pass over occupied cells: priority = nearest member's squared distance to the nearest
        // viewer; a cell with no members cannot exist here by construction. The first
        // maximumLoadedCells are live - the budget acceptance is EXACT, members past it fade.
        struct OccupiedCellUVE final {
            Scene::WorldPartition3DCellIdUVE id;
            float nearestDistanceSquared = std::numeric_limits<float>::max();
        };
        std::vector<OccupiedCellUVE> occupied;
        std::vector<float> memberDistances;
        memberDistances.reserve(members.size());
        for (const MemberSnapshotUVE& member : members) {
            memberDistances.push_back(
                Scene::ResolveLevelStreamer3DNearestViewerDistanceSquaredUVE(
                    member.worldPosition, viewerPositions)
                    .value_or(std::numeric_limits<float>::max()));
        }
        for (std::size_t i = 0U; i < members.size(); ++i) {
            if (!members[i].cell.has_value()) {
                continue; // outside the partitioned volume: unmanaged, always renders
            }
            auto found = std::find_if(occupied.begin(), occupied.end(),
                                      [&members, i](const OccupiedCellUVE& cell) {
                                          return cell.id == *members[i].cell;
                                      });
            if (found == occupied.end()) {
                occupied.push_back(OccupiedCellUVE{*members[i].cell, memberDistances[i]});
            } else if (memberDistances[i] < found->nearestDistanceSquared) {
                found->nearestDistanceSquared = memberDistances[i];
            }
        }
        std::sort(occupied.begin(), occupied.end(),
                  [&config](const OccupiedCellUVE& lhs, const OccupiedCellUVE& rhs) {
                      if (lhs.nearestDistanceSquared != rhs.nearestDistanceSquared) {
                          return lhs.nearestDistanceSquared < rhs.nearestDistanceSquared;
                      }
                      return Scene::ResolveWorldPartition3DCellLinearIndexUVE(
                                 lhs.id, config.cellCounts) <
                             Scene::ResolveWorldPartition3DCellLinearIndexUVE(
                                 rhs.id, config.cellCounts);
                  });
        const std::size_t liveCells =
            std::min<std::size_t>(occupied.size(), config.maximumLoadedCells);
        // Admission wholly-cellular: every member of an admitted cell goes live together, and
        // every member outside it fades together. Unmanaged members never join the verdict.
        for (std::size_t i = 0U; i < members.size(); ++i) {
            bool live = true;
            if (members[i].cell.has_value()) {
                live = false;
                for (std::size_t c = 0U; c < liveCells; ++c) {
                    if (occupied[c].id == *members[i].cell) {
                        live = true;
                        break;
                    }
                }
            }
            auto& membership =
                m_entityManager->GetComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
                    members[i].entity);
            membership.live = live;
        }
        config.loadedCellCount = static_cast<std::uint32_t>(liveCells);
    }
}

void EngineCoreUVE::SyncVisibilityRegion3DNodesUVE() {
    // The viewer cloud is identical to the streamer's and the world partition's: the eye plus
    // every character controller, finite poses only. Regions activate on ANY viewer inside.
    std::vector<Math::Vector3UVE> viewerPositions;
    if (m_activeCamera != Scene::kInvalidEntityUVE &&
        m_entityManager->HasComponentUVE<Scene::WorldTransformComponentUVE>(m_activeCamera)) {
        viewerPositions.push_back(
            m_entityManager->GetComponentUVE<Scene::WorldTransformComponentUVE>(m_activeCamera)
                .worldPosition);
    }
    m_entityManager->ForEachUVE<Scene::WorldTransformComponentUVE,
                                Scene::CharacterControllerComponentUVE>(
        [&viewerPositions](const Scene::EntityUVE, const Scene::WorldTransformComponentUVE& world,
                           const Scene::CharacterControllerComponentUVE&) {
            viewerPositions.push_back(world.worldPosition);
        });

    struct RegionSnapshotUVE final {
        Scene::EntityUVE entity;
        Math::Vector3UVE origin;
    };
    std::vector<RegionSnapshotUVE> regions;
    m_entityManager->ForEachUVE<Scene::VisibilityRegion3DNodeComponentUVE>(
        [this, &regions](const Scene::EntityUVE entity,
                         Scene::VisibilityRegion3DNodeComponentUVE&) {
            regions.push_back(RegionSnapshotUVE{
                entity,
                m_entityManager->HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)
                    ? m_entityManager->GetComponentUVE<Scene::WorldTransformComponentUVE>(entity)
                          .worldPosition
                    : Math::Vector3UVE{}});
        });
    // NO early-out on an empty region list: Pass 1 must still sweep memberships so a region
    // destroyed THIS tick rebrands its orphan members (live + kInvalidEntityUVE) instead of
    // leaving the stale fade binding forever.

    // active is recomputed for every region, ENABLED OR NOT: a disabled region stays dormant on
    // its members through Pass 1, but its flag still reads honestly for the inspector, and the
    // tick it is re-enabled the verdict is already fresh instead of one frame stale. No viewers
    // at all means fail-open-active: an empty play-in-editor world shows everything.
    for (const RegionSnapshotUVE& region : regions) {
        auto& config =
            m_entityManager->GetComponentUVE<Scene::VisibilityRegion3DNodeComponentUVE>(
                region.entity);
        config.active =
            viewerPositions.empty() ||
            Scene::ResolveVisibilityRegion3DAnyViewerInsideUVE(config, region.origin,
                                                               viewerPositions);
    }

    // Pass 1: sweep existing memberships. The snapshot doubles as iteration stability - the ECS
    // is not mutated here, but a by-value copy survives whatever a mid-draw Play/Stop does.
    struct MembershipSnapshotUVE final {
        Scene::EntityUVE entity;
        Scene::EntityUVE region;
    };
    std::vector<MembershipSnapshotUVE> members;
    m_entityManager->ForEachUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
        [&members](const Scene::EntityUVE entity,
                   const Scene::VisibilityRegion3DMembershipComponentUVE& membership) {
            members.push_back(MembershipSnapshotUVE{entity, membership.region});
        });
    for (const MembershipSnapshotUVE& member : members) {
        if (!m_entityManager->IsAliveUVE(member.entity)) {
            continue; // the membership dies with its mesh
        }
        auto& membership =
            m_entityManager->GetComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
                member.entity);
        const bool ownerAlive =
            member.region != Scene::kInvalidEntityUVE &&
            m_entityManager->IsAliveUVE(member.region) &&
            m_entityManager->HasComponentUVE<Scene::VisibilityRegion3DNodeComponentUVE>(
                member.region);
        if (!ownerAlive) {
            // The owner is gone or disowned its component: rebrand so Pass 2 can rehome the mesh
            // THIS tick, and fail open exactly the way the renderer gate would anyway.
            membership.region = Scene::kInvalidEntityUVE;
            membership.live = true;
            continue;
        }
        const Scene::VisibilityRegion3DNodeComponentUVE& ownerConfig =
            m_entityManager->GetComponentUVE<Scene::VisibilityRegion3DNodeComponentUVE>(
                member.region);
        const Math::Vector3UVE ownerOrigin =
            m_entityManager->HasComponentUVE<Scene::WorldTransformComponentUVE>(member.region)
                ? m_entityManager->GetComponentUVE<Scene::WorldTransformComponentUVE>(member.region)
                      .worldPosition
                : Math::Vector3UVE{};
        const bool stillManaged =
            ownerConfig.enabled && Scene::IsVisibilityRegion3DNodeComponentValidUVE(ownerConfig) &&
            m_entityManager->HasComponentUVE<Scene::MeshComponentUVE>(member.entity) &&
            m_entityManager->HasComponentUVE<Scene::WorldTransformComponentUVE>(member.entity) &&
            Scene::ResolveVisibilityRegion3DLayerGateUVE(
                ownerConfig.visibilityLayers,
                m_entityManager->GetComponentUVE<Scene::MeshComponentUVE>(member.entity)
                    .visibilityLayers) &&
            Scene::ResolveVisibilityRegion3DContainsPointUVE(
                ownerConfig, ownerOrigin,
                m_entityManager->GetComponentUVE<Scene::WorldTransformComponentUVE>(member.entity)
                    .worldPosition);
        // Managed: the owner's current verdict. Released (walked out, layer gate closed,
        // disabled, mesh component removed): back to live THIS tick - no frame of stale dark.
        membership.live = !stillManaged || ownerConfig.active;
    }

    // Pass 2: discovery. An un-owned mesh (never stamped, or Pass 1 rebranded it to kInvalid on
    // a dead owner) inside an enabled region whose layer gate passes becomes its member. With
    // overlapping regions the mesh stamps the NEAREST one's center; ties resolve in the
    // iteration order the ECS hands regions over, which is ascending entity id - deterministic
    // for content that does not overlap rooms halfway.
    std::vector<Scene::EntityUVE> meshes;
    m_entityManager->ForEachUVE<Scene::MeshComponentUVE, Scene::WorldTransformComponentUVE>(
        [this, &meshes](const Scene::EntityUVE entity, const Scene::MeshComponentUVE&,
                        const Scene::WorldTransformComponentUVE&) {
            const bool unowned =
                !m_entityManager->HasComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
                    entity) ||
                m_entityManager
                        ->GetComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(entity)
                        .region == Scene::kInvalidEntityUVE;
            if (unowned) {
                meshes.push_back(entity);
            }
        });
    for (const Scene::EntityUVE mesh : meshes) {
        const Math::Vector3UVE position =
            m_entityManager->GetComponentUVE<Scene::WorldTransformComponentUVE>(mesh)
                .worldPosition;
        const std::uint32_t meshLayers =
            m_entityManager->GetComponentUVE<Scene::MeshComponentUVE>(mesh).visibilityLayers;
        float bestDistanceSquared = std::numeric_limits<float>::max();
        Scene::EntityUVE bestRegion = Scene::kInvalidEntityUVE;
        for (const RegionSnapshotUVE& region : regions) {
            const Scene::VisibilityRegion3DNodeComponentUVE& config =
                m_entityManager->GetComponentUVE<Scene::VisibilityRegion3DNodeComponentUVE>(
                    region.entity);
            if (!config.enabled || !Scene::IsVisibilityRegion3DNodeComponentValidUVE(config) ||
                !Scene::ResolveVisibilityRegion3DLayerGateUVE(config.visibilityLayers,
                                                              meshLayers) ||
                !Scene::ResolveVisibilityRegion3DContainsPointUVE(config, region.origin,
                                                                  position)) {
                continue;
            }
            const float dx = position.x - region.origin.x;
            const float dy = position.y - region.origin.y;
            const float dz = position.z - region.origin.z;
            const float distanceSquared = dx * dx + dy * dy + dz * dz;
            if (bestRegion == Scene::kInvalidEntityUVE || distanceSquared < bestDistanceSquared) {
                bestDistanceSquared = distanceSquared;
                bestRegion = region.entity;
            }
        }
        if (bestRegion == Scene::kInvalidEntityUVE) {
            continue; // outside every region: the mesh stays unmanaged and renders as ever
        }
        const bool activeNow =
            m_entityManager->GetComponentUVE<Scene::VisibilityRegion3DNodeComponentUVE>(bestRegion)
                .active;
        if (!m_entityManager->HasComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
                mesh)) {
            m_entityManager->AddComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
                mesh, Scene::VisibilityRegion3DMembershipComponentUVE{});
        }
        auto& membership =
            m_entityManager->GetComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
                mesh);
        membership.region = bestRegion;
        membership.live = activeNow;
    }
}

void EngineCoreUVE::SyncAdaptiveRenderResolutionUVE() {
    if (!m_windowedRenderingActiveUVE || !m_presentationSurfaceReadyUVE || !m_renderDevice->IsUsableUVE()) {
        return;
    }

    // An active editor viewport region means the render target should track that panel's own
    // pixel footprint - not the full window - so its aspect ratio, resolution, and downstream
    // adaptive-resolution scaling all match what will actually be displayed there (see
    // SetEditorViewportRegionUVE()'s own doc comment). Absent one (every standalone runtime/test
    // path), this is exactly the previous full-window behavior.
    const std::uint32_t drawableWidth =
        m_editorViewportRegionUVE.has_value() ? m_editorViewportRegionUVE->width : m_windowManager->GetWidthUVE();
    const std::uint32_t drawableHeight =
        m_editorViewportRegionUVE.has_value() ? m_editorViewportRegionUVE->height : m_windowManager->GetHeightUVE();
    if (drawableWidth == 0U || drawableHeight == 0U) {
        return;
    }

    const Window::AdaptiveRenderResolutionUVE desiredResolution =
        Window::ComputeAdaptiveRenderResolutionUVE(drawableWidth, drawableHeight,
                                                    kAdaptiveRenderResolutionLimitsUVE);
    if (desiredResolution.width == 0U || desiredResolution.height == 0U) {
        return;
    }
    if (!m_renderer3D->ResizeTargetsUVE(desiredResolution.width, desiredResolution.height) &&
        !m_adaptiveResizeFailureLoggedUVE) {
        UVE_WARNING("EngineCoreUVE: adaptive render-target resize failed; retaining the previous target");
        m_adaptiveResizeFailureLoggedUVE = true;
    }
}

void EngineCoreUVE::Update() {
    m_windowManager->PollEventsUVE();
    if (!m_windowedRenderingActiveUVE) {
        m_presentationSurfaceReadyUVE = false;
    } else {
        const std::uint32_t drawableWidth = m_windowManager->GetWidthUVE();
        const std::uint32_t drawableHeight = m_windowManager->GetHeightUVE();
        m_presentationSurfaceReadyUVE = drawableWidth > 0U && drawableHeight > 0U;
        if (!m_presentationSurfaceReadyUVE) {
#if defined(__ANDROID__)
            // Android may transiently lose its ANativeWindow/EGLSurface between lifecycle commands.
            // Keep the engine alive and let the backend recreate the surface instead of converting a
            // recoverable presentation pause into permanent backend loss.
            m_presentationSurfaceReadyUVE = m_windowManager->TryRecoverSurfaceUVE();
#else
            m_windowedRenderingActiveUVE = false;
#endif
        }
        if ((!m_presentationSurfaceReadyUVE && !m_windowedRenderingActiveUVE) ||
            (m_presentationSurfaceReadyUVE && !m_renderDevice->IsUsableUVE())) {
            m_windowedRenderingActiveUVE = false;
            m_presentationSurfaceReadyUVE = false;
            if (!m_graphicsBackendLossLoggedUVE) {
                UVE_WARNING("EngineCoreUVE: graphics backend became unusable; rendering is disabled for this run");
                m_graphicsBackendLossLoggedUVE = true;
            }
        }
    }
    SyncAdaptiveRenderResolutionUVE();
    m_gamepadInputSystem->UpdateUVE();
    m_mobileInputSystem->UpdateUVE();
    m_mobileGestureSystem->UpdateUVE(static_cast<float>(m_timer->GetDeltaTimeUVE()));
    m_inputSystem->UpdateUVE();
    if (m_windowManager->IsCloseRequestedUVE()) {
        RequestQuitUVE();
    }

    Utilities::FixedStepResultUVE fixedStep{};
    if (m_simulationExecutionMode == SimulationExecutionModeUVE::Running) {
        fixedStep = m_timer->AdvanceFixedStepUVE();
    } else {
        m_timer->DiscardFixedStepAccumulatorUVE();
        if (m_singleSimulationStepPending) {
            fixedStep.stepsToRun = 1;
            m_singleSimulationStepPending = false;
        }
    }
    UVE_TRACE("Update: {} fixed step(s), alpha={}", fixedStep.stepsToRun, fixedStep.alpha);
    m_eventSystem->DispatchQueuedUVE();

    const float fixedDeltaTimeSeconds =
        m_config.fixedUpdateFps > 0.0 ? static_cast<float>(1.0 / m_config.fixedUpdateFps) : 0.0F;
    for (int step = 0; step < fixedStep.stepsToRun; ++step) {
        m_physicsSystem->StepUVE(*m_entityManager, *m_sceneGraph, fixedDeltaTimeSeconds);
        SyncCharacterControllersUVE(fixedDeltaTimeSeconds);
        SyncProjectile3DNodesUVE(fixedDeltaTimeSeconds);
        SyncSpringArm3DNodesUVE(fixedDeltaTimeSeconds);
    }

    m_sceneGraph->UpdateUVE(*m_entityManager);
    // The fraction of a fixed step already elapsed, handed to the renderer so it can draw between
    // the last two simulated poses instead of snapping to the newest one. Measured on a 144 Hz
    // display against 60 Hz physics: 58% of frames previously received no new pose at all, and
    // per-frame movement jumped between 0 and 16.7 cm at 10 m/s. This is the value that fixes it,
    // and it has been computed every frame since the timer was written - only ever logged.
    m_renderer3D->SetPhysicsInterpolationAlphaUVE(static_cast<float>(fixedStep.alpha));
    SyncParticleRuntimeUVE();
    SyncUIRuntimeUVE();
    SyncCollisionLifecycleUVE();
    SyncRayCast3DNodesUVE();
    SyncHitbox3DNodesUVE();
    SyncInteractionArea3DNodesUVE();
    SyncLevelStreamer3DNodesUVE();
    SyncReflectionProbe3DNodesUVE();
    SyncWorldPartition3DNodesUVE();
    SyncVisibilityRegion3DNodesUVE();
    SyncScriptRuntimeUVE();

    if (m_config.hotReloadEnabledUVE) {
        m_hotReload->PollUVE(*m_assetManager, *m_assetDatabase, m_timer->GetDeltaTimeUVE());
    }

    // Increment 61: portable project-content change detection is distinct from the loaded-asset
    // hot reloader. It can only journal changes and mark matching derived metadata stale; it never
    // refreshes the editor index, enqueues imports, or mutates authoring state automatically.
    static_cast<void>(m_projectChangeWatcher->PollUVE(m_timer->GetDeltaTimeUVE(), *m_assetDatabase,
                                                       *m_derivedArtifactCache));
    m_assetManager->CollectGarbageUVE();

    // Increment 60: one deterministic, main-thread import job at most per engine Update().
    // Queues only progress after callers explicitly enqueue/retry; no file watcher or background
    // import worker is introduced by this maintenance seam.
    static_cast<void>(m_assetImportQueue->TickUVE());

    if (m_renderDevice->IsUsableUVE()) {
        m_shaderManager->UpdateUVE(m_timer->GetDeltaTimeUVE());
    }

    if (!m_transientSimulationSessionActive) {
        m_checkpointManager->UpdateUVE(
            m_timer->GetDeltaTimeUVE(), *m_entityManager,
            m_sceneGraph->GetChildrenUVE(*m_entityManager, Scene::kInvalidEntityUVE));
    }
}

void EngineCoreUVE::PublishAreaOverlapLifecycleEventsUVE() {
    const Physics::AreaOverlapQueryResultUVE snapshot =
        Physics::AreaOverlapSystemUVE::QueryUVE(*m_entityManager);
    const Physics::AreaOverlapLifecycleReportUVE report =
        m_areaOverlapLifecycleTracker.UpdateUVE(snapshot);

    for (const Physics::AreaOverlapTransitionUVE& transition : report.transitions) {
        if (transition.kind == Physics::AreaOverlapTransitionKindUVE::Entered) {
            m_eventSystem->QueueEvent(Physics::AreaOverlapEnteredEventUVE{transition.pair});
        } else {
            m_eventSystem->QueueEvent(Physics::AreaOverlapExitedEventUVE{transition.pair});
        }
    }
}

void EngineCoreUVE::LateUpdate() {
    if (m_frameStats.deltaTimeSeconds > 0.0) {
        const double instantaneousFps = 1.0 / m_frameStats.deltaTimeSeconds;
        constexpr double kFpsSmoothingFactor = 0.1;
        m_frameStats.fps = (m_frameStats.fps <= 0.0)
                                ? instantaneousFps
                                : (m_frameStats.fps * (1.0 - kFpsSmoothingFactor) +
                                   instantaneousFps * kFpsSmoothingFactor);
    }
    UVE_TRACE("LateUpdate: fps={}", m_frameStats.fps);

    PublishAreaOverlapLifecycleEventsUVE();

    if (m_activeCamera != Scene::kInvalidEntityUVE) {
        const auto& worldTransform =
            m_entityManager->GetComponentUVE<Scene::WorldTransformComponentUVE>(m_activeCamera);
        m_audioSystem->SetListenerPositionUVE(worldTransform.worldPosition);
        m_audioSystem->SetListenerOrientationUVE(
            Math::RotateVectorUVE(worldTransform.worldRotation, Math::Vector3UVE{0.0F, 0.0F, -1.0F}),
            Math::RotateVectorUVE(worldTransform.worldRotation, Math::Vector3UVE{0.0F, 1.0F, 0.0F}));
    }
    m_audioSourceSystem->SyncUVE(*m_entityManager, *m_audioSystem);
    m_audioSystem->UpdateUVE();
}

void EngineCoreUVE::Render() {
    if (!m_renderDevice->IsUsableUVE() ||
        (m_windowedRenderingActiveUVE && !m_presentationSurfaceReadyUVE)) {
        return;
    }
    // Compute first, graphics after: any dispatch enqueued on ComputeSystemUVE this frame is
    // recorded into its OWN command buffer and submitted before the renderer opens a single
    // render pass. That ordering is the portable flow the RHI compute slices settled on (Vulkan
    // forbids dispatch inside a rendering instance), and giving compute a separate buffer keeps
    // it independent of which render path runs below — including the no-camera path, where the
    // renderer never records anything at all. An empty queue costs one unused command buffer and
    // no submission, so scenes that never touch compute pay nothing observable.
    // Note the early return above: while the device is unusable (or a windowed surface is not
    // ready yet) this is never reached, so queued dispatches WAIT rather than being recorded
    // against a device that cannot execute them - they run on the first frame that renders
    // again. Callers that enqueue every frame regardless should check
    // IRenderDeviceUVE::IsUsableUVE() or call ClearQueueUVE() themselves; the engine does not
    // silently discard work a caller explicitly asked for.
    if (m_computeSystem->GetQueuedDispatchCountUVE() > 0U) {
        std::unique_ptr<Render::ICommandBufferUVE> computeCommands = m_renderDevice->CreateCommandBufferUVE();
        if (computeCommands != nullptr) {
            const std::size_t recordedDispatches = m_computeSystem->ExecuteQueuedDispatchesUVE(*computeCommands);
            if (recordedDispatches > 0U) {
                m_renderDevice->SubmitUVE(std::move(computeCommands));
            }
        } else {
            // No buffer, no honest way to record: drop the queue loudly rather than carrying
            // stale work into a later frame where its resources may already be gone.
            UVE_WARNING("EngineCoreUVE: render device returned no command buffer for {} queued "
                        "compute dispatch(es) - dropping them",
                        m_computeSystem->GetQueuedDispatchCountUVE());
            m_computeSystem->ClearQueueUVE();
        }
    }

    m_renderer3D->SetUIRuntimeUVE(&m_uiRuntime);
    if (m_activeCamera != Scene::kInvalidEntityUVE) {
        const bool hasParticles = m_particleRuntime != nullptr && m_particleRuntime->GetInstanceCountUVE() > 0U;
        if (m_editorViewportRegionUVE.has_value()) {
            m_renderer3D->RenderFrameToRegionUVE(*m_entityManager, m_activeCamera, *m_editorViewportRegionUVE,
                                                  hasParticles ? m_particleRuntime.get() : nullptr);
        } else if (hasParticles) {
            m_renderer3D->RenderFrameWithParticleRuntimeUVE(*m_entityManager, m_activeCamera, *m_particleRuntime);
        } else {
            m_renderer3D->RenderFrameUVE(*m_entityManager, m_activeCamera);
        }
    } else {
        UVE_TRACE("Render (no-op)");
        if (m_windowedRenderingActiveUVE) {
            // An empty editor/game window still needs a real presented frame. Clear the
            // backend's default framebuffer without creating a scene, camera, mesh, or light.
            // This keeps the no-scene state visible and preserves the empty-scene contract.
            // Confined to the editor viewport region (when set) so an empty-scene editor window
            // clears only its own 3D viewport panel, matching every other render path this frame.
            m_renderSystem->BeginFrameUVE();
            Render::ICommandBufferUVE& commandBuffer = m_renderSystem->GetFrameCommandBufferUVE();
            Render::RenderPassDescUVE passDesc;
            passDesc.colorAttachment = Render::kInvalidTextureHandleUVE;
            passDesc.depthAttachment = Render::kInvalidTextureHandleUVE;
            passDesc.colorLoadOp = Render::LoadOpUVE::Clear;
            passDesc.depthLoadOp = Render::LoadOpUVE::DontCare;
            passDesc.clearColor = {0.05F, 0.05F, 0.05F, 1.0F};
            passDesc.viewportOverride = m_editorViewportRegionUVE;
            commandBuffer.BeginRenderPassUVE(passDesc);
            commandBuffer.EndRenderPassUVE();
            m_renderSystem->EndFrameUVE();
        }
    }

    if (m_windowedRenderingActiveUVE && m_presentationSurfaceReadyUVE) {
        if (m_postRenderCallback) {
            m_postRenderCallback();
        }
        m_renderDevice->PresentUVE();
    }
}

void EngineCoreUVE::EndFrame() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    m_frameStats.frameTimeSeconds = std::chrono::duration<double>(now - m_frameStartTime).count();
    UVE_TRACE("EndFrame {}: delta={} total={} fps={} frameTime={}", m_frameStats.frameNumber,
              m_frameStats.deltaTimeSeconds, m_frameStats.totalTimeSeconds, m_frameStats.fps,
              m_frameStats.frameTimeSeconds);
}

void EngineCoreUVE::TickFrameUVE() {
    UVE_ASSERT(m_state == EngineStateUVE::Running);
    BeginFrame();
    Update();
    LateUpdate();
    Render();
    EndFrame();
}

int EngineCoreUVE::RunUVE(int frameCount) {
    UVE_ASSERT(frameCount >= 0);
    try {
        Init();
        if (!Load()) {
            Shutdown();
            return 1;
        }
        for (int frameIndex = 0; frameIndex < frameCount && !m_quitRequested; ++frameIndex) {
            TickFrameUVE();
        }
        Shutdown();
        return 0;
    } catch (const std::exception& exception) {
        UVE_FATAL("EngineCoreUVE: RunUVE() caught an unhandled exception - shutting down: {}", exception.what());
    } catch (...) {
        UVE_FATAL("EngineCoreUVE: RunUVE() caught an unhandled non-std::exception - shutting down");
    }

    // Reached only via one of the catches above. Shutdown() tears down every subsystem Init()
    // constructed, in reverse order, and is only safe to call once m_state has actually reached
    // Running (IsValidTransitionUVE() only allows Running -> ShuttingDown) - an exception thrown
    // partway through Init() itself leaves m_state at Initializing, where subsystems Init() had
    // not yet reached are still the null/empty state their declarations default-initialize them
    // to, so Shutdown()'s unconditional dereferences (e.g. m_eventSystem->Clear()) would
    // themselves crash. In that case teardown is left to EngineCoreUVE's own destructor, which
    // runs normally once this function returns and `this` goes out of scope in the caller, and
    // whose RAII members already know how to unwind whatever subset of construction completed.
    if (m_state == EngineStateUVE::Running) {
        try {
            Shutdown();
        } catch (const std::exception& exception) {
            UVE_FATAL("EngineCoreUVE: Shutdown() itself threw while recovering from the exception above: {}",
                       exception.what());
        } catch (...) {
            UVE_FATAL("EngineCoreUVE: Shutdown() itself threw a non-std::exception while recovering from "
                       "the exception above");
        }
    }
    return kUnhandledExceptionExitCodeUVE;
}

void EngineCoreUVE::RequestQuitUVE() noexcept {
    m_quitRequested = true;
}

std::size_t EngineCoreUVE::GetActiveScriptInstanceCountUVE() const noexcept {
    return m_scriptRuntime.GetInstanceCountUVE();
}

void EngineCoreUVE::Shutdown() {
    TransitionStateUVE(EngineStateUVE::ShuttingDown);
    m_simulationExecutionMode = SimulationExecutionModeUVE::Running;
    m_singleSimulationStepPending = false;
    m_transientSimulationSessionActive = false;
    UVE_INFO("EngineCoreUVE: shutting down");

    // Exact reverse of Init()'s construction order: ConfigManager, then
    // CheckpointManager, then SaveGameSystem, then AudioSourceSystem, then AudioSystem, then AudioDevice, then
    // InputSystem, then MobileGestureSystem, then MobileInputSystem, then GamepadInputSystem, then
    // RaycastSystem, then PhysicsSystem, then CollisionSystem, then Renderer3D, then LightSystem, then MeshRenderer, then CameraSystem, then RenderSystem, then ShaderManager, then RenderDevice
    // (destroying the demo triangle's shader program and vertex buffer first, if windowed rendering
    // was active), then WindowManager,
    // then FileSystem, then AssetBundle, then AssetImporter, then
    // AssetManager (its destructor blocks until every in-flight load job
    // finishes), then HotReload, then PrefabSystem, then SceneSerializer,
    // then AssetDatabase, then SceneGraph, then EntityManager, then
    // EventSystem, then Timer, then ThreadPool, then MemoryManager, then
    // Logger, then CommandLine. The final log message is emitted before the
    // logger itself is torn down, so it is guaranteed to be recorded.
    m_services.reset();
    m_configManager.reset();
    m_checkpointManager.reset();
    m_saveGameSystem.reset();
    m_audioSourceSystem.reset();
    m_audioSystem.reset();
    m_audioDevice.reset();
    m_inputSystem.reset();
    m_mobileGestureSystem.reset();
    m_mobileInputSystem.reset();
    m_gamepadInputSystem.reset();
    m_raycastSystem.reset();
    m_physicsQuerySystem.reset();
    m_physicsSystem.reset();
    m_physicsConstraintSystem.reset();
    m_collisionSystem.reset();
    m_areaOverlapLifecycleTracker.ResetUVE();
    m_renderer3D.reset();
    m_lightSystem.reset();
    m_meshRenderer.reset();
    m_cameraSystem.reset();
    m_renderSystem.reset();

    m_shaderManager.reset();
    m_renderDevice.reset();

    // WindowManager right after RenderDevice — every GL object RenderDevice owned is already
    // destroyed above, so it's safe for WindowManager's destructor to tear down the GL context
    // (and, for the real backend, terminate GLFW) now.
    m_windowManager.reset();

    m_fileSystem.reset();
    m_assetBundle.reset();
    m_assetImportQueue.reset();
    m_assetImporter.reset();
    m_assetManager.reset();
    m_hotReload.reset();
    m_prefabSystem.reset();
    m_sceneSerializer.reset();
    m_projectFileIndex.reset();
    m_projectChangeWatcher.reset();
    m_derivedArtifactCache.reset();
    m_assetDatabase.reset();
    m_sceneGraph.reset();

    // EntityManagerUVE's destructor frees every remaining live entity's
    // component memory — this must happen before MemoryManager's leak
    // report below, or a perfectly correct program would show false-
    // positive leaks for every still-alive entity at shutdown.
    m_entityManager.reset();

    m_eventSystem->Clear();
    m_eventSystem.reset();
    m_timer.reset();

    // ThreadPoolUVE's destructor blocks until every worker has drained its
    // queue and joined — no jobs are silently dropped on shutdown.
    m_threadPool.reset();

    // Leak report must run while the logger is still alive; the debug-only
    // assertion turns a leak into an immediate development-time failure
    // without ever affecting Release builds.
    m_memoryManager->LogLeakReportUVE();
#if UVE_DEBUG
    UVE_ASSERT(m_memoryManager->GetActiveAllocationCountUVE() == 0);
#endif
    m_memoryManager.reset();

    UVE_INFO("EngineCoreUVE: shutdown complete");
    m_logger->Shutdown();
    m_logger.reset();
    m_commandLine.reset();

    TransitionStateUVE(EngineStateUVE::Shutdown);
}

EngineStateUVE EngineCoreUVE::GetStateUVE() const noexcept {
    return m_state;
}

const FrameStatsUVE& EngineCoreUVE::GetFrameStatsUVE() const noexcept {
    return m_frameStats;
}

Scene::ParticleRuntimeSnapshotUVE EngineCoreUVE::GetParticleRuntimeSnapshotUVE() const {
    return m_particleRuntime != nullptr ? m_particleRuntime->GetSnapshotUVE() : Scene::ParticleRuntimeSnapshotUVE{};
}

bool EngineCoreUVE::SetSimulationExecutionModeUVE(const SimulationExecutionModeUVE mode) noexcept {
    if (m_state != EngineStateUVE::Running) {
        return false;
    }
    m_simulationExecutionMode = mode;
    if (mode == SimulationExecutionModeUVE::Running) {
        m_singleSimulationStepPending = false;
    }
    return true;
}

SimulationExecutionModeUVE EngineCoreUVE::GetSimulationExecutionModeUVE() const noexcept {
    return m_simulationExecutionMode;
}

bool EngineCoreUVE::RequestSingleSimulationStepUVE() noexcept {
    if (m_state != EngineStateUVE::Running || m_simulationExecutionMode != SimulationExecutionModeUVE::Paused ||
        m_singleSimulationStepPending) {
        return false;
    }
    m_singleSimulationStepPending = true;
    return true;
}

bool EngineCoreUVE::SetTransientSimulationSessionActiveUVE(const bool active) noexcept {
    if (m_state != EngineStateUVE::Running) {
        return false;
    }
    m_transientSimulationSessionActive = active;
    return true;
}

bool EngineCoreUVE::IsTransientSimulationSessionActiveUVE() const noexcept {
    return m_transientSimulationSessionActive;
}

void EngineCoreUVE::SetActiveCameraUVE(Scene::EntityUVE cameraEntity) noexcept {
    m_activeCamera = cameraEntity;
}

Scene::EntityUVE EngineCoreUVE::GetActiveCameraUVE() const noexcept {
    return m_activeCamera;
}

void EngineCoreUVE::SetEditorViewportRegionUVE(std::optional<Render::ViewportRectUVE> region) noexcept {
    m_editorViewportRegionUVE = region;
}

void EngineCoreUVE::SetPostRenderCallbackUVE(std::function<void()> callback) {
    m_postRenderCallback = std::move(callback);
}

EngineServicesUVE& EngineCoreUVE::GetServicesUVE() {
    UVE_ASSERT(m_services.has_value());
    return m_services.value(); // throws std::bad_optional_access in Release if called before Init()
}

VersionUVE EngineCoreUVE::GetEngineVersionUVE() noexcept {
    return VersionUVE{0, 1, 0, 1};
}

} // namespace UVE::Core
