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

#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <unordered_map>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "motion_montage_asset.h"
#include "animation_clip_asset.h"
#include "asset_handle.h"
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
#include "engine_config.h"
#include "raycast_audio_occlusion.h"
#include "engine_services.h"
#include "document_scene.h"
#include "i_editor_viewport_host.h"
#include "i_simulation_control.h"
#include "engine_state.h"
#include "frame_stats.h"
#include "dynamic_render_resolution.h"
#include "blueprint_asset.h"
#include "blueprint_object_host.h"
#include "blueprint_instance.h"
#include "engine_version.h"
#include "i_logger.h"
#include "i_event_system.h"
#include "pawn.h"
#include "i_gamepad_input_system.h"
#include "i_input_system.h"
#include "i_mobile_gesture_system.h"
#include "i_mobile_input_system.h"
#include "i_memory_manager.h"
#include "navigation_runtime.h"
#include "area_overlap_lifecycle_tracker.h"
#include "hitbox_lag_compensation.h"
#include "hitbox_strike_lifecycle_tracker.h"
#include "collision_lifecycle_tracker.h"
#include "i_collision_system.h"
#include "i_physics_system.h"
#include "i_physics_query_system.h"
#include "i_raycast_system.h"
#include "physics_constraint_system.h"
#include "physics_query_system.h"
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
#include "skeleton_3d.h"
#include "particle_runtime.h"
#include "vfx_draw_list.h"
#include "vfx_world.h"
#include "i_prefab_system.h"
#include "i_scene_graph.h"
#include "i_scene_serializer.h"
#include "i_thread_pool.h"
#include "localization.h"
#include "ui_runtime.h"
#include "i_timer.h"
#include "i_window_manager.h"

namespace UVE::Core {

/// EngineCore owns the foundational engine services (CommandLine, Logger,
/// MemoryManager, ThreadPool, Timer, EventSystem, EntityManager, SceneGraph,
/// AssetDatabase, ProjectFileIndex, DerivedArtifactCache, ProjectChangeWatcher, SceneSerializer, PrefabSystem,
/// HotReload, AssetManager, AssetImporter, AssetImportQueue, AssetBundle, FileSystem, WindowManager, RenderDevice, ShaderManager,
/// RenderSystem, CameraSystem, MeshRenderer, LightSystem, Renderer3D, CollisionSystem, PhysicsSystem,
/// RaycastSystem, InputSystem, GamepadInputSystem, MobileInputSystem, MobileGestureSystem, AudioDevice, AudioSystem, AudioSourceSystem,
/// SaveGameSystem, CheckpointManager, ConfigManager) and drives the canonical
/// engine lifecycle: Init -> Load -> N x (BeginFrame -> Update -> LateUpdate
/// -> Render -> EndFrame) -> Shutdown. Render() calls Renderer3D::RenderFrame()
/// (extract -> cull -> sort -> record -> submit) whenever
/// SetActiveCamera() has set a valid camera entity; with no active camera
/// set, headless mode retains the no-op trace behavior while a valid window
/// receives a real empty-frame clear and present without creating scene content.
/// Update() runs zero or
/// more fixed PhysicsSystem steps (via Utilities::FixedStepResult)
/// before SceneGraph::Update() each frame — entirely data-driven off
/// which entities have a Rigid3DComponent/ColliderComponent, so a
/// scene with none behaves exactly as it did before Increment 15, no opt-in
/// needed. RaycastSystem (Increment 16) is a stateless, on-demand query
/// service — like CameraSystem/MeshRenderer, it has no Update()-loop hook
/// of its own; callers reach it via GetServices().GetRaycastSystem().
/// GamepadInputSystem and MobileInputSystem are committed before InputSystem, so its
/// existing gamepad-aware action bindings read current device snapshots; MobileGestureSystem
/// consumes the committed mobile snapshot after the mobile service update. All three are
/// backend-neutral and own no platform APIs. InputSystem (Increment 17) is stateful and is driven
/// every frame after the device snapshots are committed, so this frame's key/mouse/gamepad edge state
/// and action-triggered events are settled before the fixed-timestep accumulator, event dispatch, or
/// physics steps that follow it in the same call. AudioSourceSystem/AudioSystem
/// (Increment 18) are driven from LateUpdate(): if SetActiveCamera() has
/// set a valid camera, the audio listener is synced to that camera entity's
/// WorldTransformComponent first (the spec's "AudioListener — Attached
/// to Camera3D by default"); then AudioSourceSystem::Sync() walks every
/// WorldTransformComponent + AudioSourceComponent entity (entirely
/// data-driven, like PhysicsSystem — a scene with none is a cheap no-op);
/// then AudioSystem::Update() recomputes attenuated gain for every live
/// source. ComputeSystem (Part 7.2's engine-level compute consumer) is driven from
/// Render(), as its FIRST statement: any dispatch enqueued on it during this
/// frame is recorded into its own command buffer and submitted BEFORE the
/// renderer opens a single render pass - compute first, graphics after, which
/// is the portable flow the RHI compute slices settled on (Vulkan forbids
/// dispatch inside a rendering instance). A frame with an empty queue submits
/// nothing at all, so scenes that never use compute pay nothing observable.
/// WindowManager/GlRenderDevice (Increment 20): unless
/// EngineConfig::headless is true (also settable via the `--headless` CLI flag), Init()
/// creates a real GLFW3 window and OpenGL 4.6 Core render device; Update()'s first statement
/// (after InputSystem::Update()) pumps window events and checks
/// IWindowManager::IsCloseRequested(), calling RequestQuit() when the configured
/// quit-on-last-window-closed policy is enabled; Render() additionally records and presents a small, explicitly temporary demo
/// triangle proving the window/GL pipeline end-to-end — deliberately outside Renderer3D, which
/// still only ever renders into its own offscreen target regardless of windowed mode (see
/// docs/CODING_STANDARDS.md for the full rendering-evolution roadmap this triangle is the first
/// milestone of). If real window/context creation fails, Init() logs UVE_FATAL and falls back to
/// NullWindowManager/NullRenderDevice for the rest of this run, and Load() reports failure
/// so Run() shuts down cleanly instead of proceeding into a broken windowed session. If a
/// context exists but the required OpenGL entry points cannot be loaded, Init() treats that as a
/// recoverable backend-selection failure and selects NullRenderDevice before shader/frame code
/// runs. A CPU-only build (without desktop GLFW/OpenGL) compiles EngineCore with the null window
/// and render backends; a requested windowed startup warns and continues headlessly.
/// CheckpointManager (Increment 19) is driven from Update()'s final statement:
/// Update(deltaTime, entityManager, every SceneGraph::GetChildren(kInvalidEntity)
/// root) accumulates elapsed time and, once the configured
/// EngineConfig::autoSaveIntervalSeconds elapses, saves the whole scene to
/// Save::kAutoSaveSlotIndex via the composed SaveGameSystem — entirely data-driven, like
/// PhysicsSystem/AudioSourceSystem, so an empty scene with the default 300-second interval
/// never actually writes to disk during a short test run. ShaderManager (Increment 21) is
/// constructed right after RenderDevice, in both headless and windowed mode (it works identically
/// against NullRenderDevice/GlRenderDevice); Init() mounts
/// EngineConfig::shaderSourceRealDirectory under shaderSourceMountPrefix first, so the
/// built-in `.glsl` files resolve through the VFS. Update() calls ShaderManager::Update()
/// every frame (draining background preprocessing, compiling/linking on the main thread, and
/// polling hot-reload) alongside the existing HotReload/AssetManager maintenance calls. The
/// demo triangle above now loads its program from the `basic_3d.glsl` built-in via
/// ShaderManager::CreateProgram() instead of an inline GLSL string; since compilation is
/// asynchronous, the triangle may not actually draw until a frame or two after Init() — Render()
/// guards the draw call on ShaderProgram::IsValid(), so the window's clear color still
/// appears immediately regardless.
/// Thread-safety: not thread-safe. Every method here is intended to be
/// called from a single "engine" thread. The services EngineCore owns
/// each document their own thread-safety contract independently (e.g.
/// Logger is safe to log to from other threads even though
/// EngineCore's own methods are not thread-safe).
class EngineCore final : public ISimulationControl, public IEditorViewportHost {
public:
    /// Distinct process exit code Run() returns if an exception (of any type) escaped
    /// Init(), Load(), or a frame update and was caught at Run()'s own top-level boundary,
    /// instead of propagating out and terminating the process via std::terminate(). Deliberately
    /// distinct from 0 (clean success) and 1 (a reported Load() failure), so a caller, CI, or
    /// crash-reporting tooling can tell "the engine crashed" apart from either of those. Any
    /// other desktop entry point that drives EngineCore's lifecycle outside Run() (see
    /// engine/app/src/editor/main.cpp) returns this same value for the same reason, so the code
    /// means the same thing everywhere it appears.
    static constexpr int kUnhandledExceptionExitCode = 2;

    explicit EngineCore(EngineConfig config = {});
    ~EngineCore();

    EngineCore(const EngineCore&) = delete;
    EngineCore& operator=(const EngineCore&) = delete;

    /// Constructs CommandLine and Logger first, then loads and resolves the project, user,
    /// active-platform and command-line settings layers before constructing systems that consume
    /// EngineConfig. `--headless` is a hidden, NotPersisted boolean setting in the command-line
    /// layer; project, user and platform stores cannot override it. The remaining services are
    /// initialized in dependency order; CommandLine is pure parsing, and Logger is ready before
    /// migrations or settings loads can log. EntityManager follows EventSystem, since it needs
    /// MemoryManager for allocation and EventSystem for entity lifecycle
    /// events; SceneGraph immediately after, though it has no dependencies
    /// of its own; AssetDatabase right after SceneGraph, needing only
    /// Logger; SceneSerializer and PrefabSystem grouped immediately after,
    /// both stateless; HotReload right after, needing only EventSystem —
    /// constructed before AssetManager (rather than after, as its own Part
    /// 7.4 doc-comment ordering might suggest) because AssetManager takes a
    /// HotReload* constructor argument and construction must stay strictly
    /// forward-dependency (immediately after, RegisterBuiltInAssetLoaders()
    /// registers the built-in MeshAsset/TextureAsset/ShaderAsset/
    /// MaterialAsset/AudioAsset/AnimationClipAsset loaders with it); AssetImporter and AssetBundle grouped immediately
    /// after, both stateless; FileSystem right after, needing AssetBundle
    /// (its bundle-backed mounts read entries through it); WindowManager right after — a real
    /// Window::WindowManager (owning the entire GLFW/GL context lifecycle) unless
    /// EngineConfig::headless, in which case Window::NullWindowManager; CPU-only builds
    /// always use the null window/render backends and warn if a windowed startup was requested; a
    /// real window that fails to create sets a private failure flag Load() checks (see Load()'s own doc
    /// comment) rather than aborting Init() mid-construction (EngineState's transition table
    /// forbids jumping straight from Initializing to ShuttingDown); RenderDevice
    /// right after — Render::GlRenderDevice when a real, valid window exists, otherwise
    /// Render::NullRenderDevice (headless mode, or a real window that failed to create — never
    /// constructs GlRenderDevice against an invalid window); ShaderManager right after — needs
    /// ThreadPool, EventSystem, RenderDevice, and FileSystem (all already constructed by this
    /// point); mounts EngineConfig::shaderSourceRealDirectory under
    /// shaderSourceMountPrefix on IFileSystem first, then constructs
    /// Render::Shader::ShaderManager from an EngineConfig-derived
    /// Render::Shader::ShaderManagerConfig; works identically in headless mode (against
    /// NullRenderDevice) and windowed mode (against GlRenderDevice); RenderSystem right
    /// after, needing RenderDevice (it records and submits command buffers
    /// through it); CameraSystem right after, stateless with no
    /// dependencies of its own (grouped with the rest of engine/render);
    /// MeshRenderer right after, likewise stateless (grouped with the rest
    /// of engine/render); LightSystem right after, likewise stateless
    /// (grouped with the rest of engine/render, constructed right before
    /// Renderer3D since Renderer3D's constructor needs it); Renderer3D right after, needing RenderDevice,
    /// RenderSystem, MeshRenderer, CameraSystem, LightSystem, AssetManager, AssetDatabase,
    /// EventSystem, and EngineConfig::ambientColor — every one of which already exists by this point;
    /// CollisionSystem right after, stateless with no dependencies of its
    /// own; PhysicsSystem right after, needing only CollisionSystem (and
    /// EngineConfig::gravity, already available); RaycastSystem right
    /// after, stateless with no dependencies of its own (grouped with the
    /// rest of engine/physics); InputSystem right after, needing only
    /// EventSystem (composed by reference, to queue InputActionTriggeredEvent);
    /// AudioDevice right after, with no dependencies of its own (a
    /// NullAudioDevice — no real audio hardware/SDK is buildable in this
    /// sandbox); AudioSystem right after, needing AudioDevice (it pushes
    /// computed gain/position through it); AudioSourceSystem right after,
    /// stateful but taking no constructor dependencies (EntityManager and
    /// AudioSystem are passed to Sync() per call, like
    /// MeshRenderer::ExtractRenderQueue()); SaveGameSystem right after, needing
    /// SceneSerializer (composed by reference) and EngineConfig::saveDirectoryPath;
    /// CheckpointManager right after, needing SaveGameSystem (composed by reference) and
    /// EngineConfig::autoSaveIntervalSeconds; the ConfigManager store
    /// is loaded after Logger and before the project/user/platform/command-line
    /// stack is applied, so effective values are settled before dependent systems
    /// are constructed; then builds EngineServices from all thirty-four. Transitions
    /// Uninitialized -> Initializing -> Running.
    void Init();

    /// The engine's asset/subsystem loading hook. Also the point where a real
    /// window/GL-context creation failure detected during Init() (see Init()'s own doc comment)
    /// is surfaced: if so, logs UVE_FATAL and returns false — nothing else needed loading this
    /// increment, so this is otherwise the complete, correct behavior for the one thing this
    /// stage owns today (a fatal-startup check plus logging), not a placeholder for a future one.
    [[nodiscard]] bool Load();

    /// Runs Init() -> Load() -> up to `frameCount` frames (stopping early
    /// if RequestQuit() was called) -> Shutdown(). `frameCount` must be
    /// >= 0. Returns 0 on success, 1 if Load() failed. Deterministic and
    /// headless-friendly — the mode used by both the uve_runtime executable
    /// and the unit test suite.
    ///
    /// Run() is a hard exception boundary: this is the only entry point that guarantees it
    /// never lets an exception escape, regardless of where in Init()/Load()/a frame update it
    /// was thrown, or its type — an uncaught exception here would otherwise unwind straight out
    /// of main() and terminate the process via std::terminate(), skipping Shutdown() entirely
    /// and every subsystem's teardown (GPU/file/OS handles included). If one escapes, Run()
    /// logs it via UVE_FATAL, still runs Shutdown() — in its normal order — if the engine had
    /// already reached EngineState::Running by then (otherwise Shutdown() itself would
    /// dereference subsystems Init() never got to construct; RAII cleans up whatever subset did
    /// construct once this function returns and `this` is destroyed), and returns
    /// kUnhandledExceptionExitCode instead of 0 or 1 — never lets that second exception (from
    /// Shutdown() itself) escape either. Callers do not need their own try/catch around Run().
    int Run(int frameCount);

    /// Runs exactly one frame: BeginFrame -> Update -> LateUpdate -> Render
    /// -> EndFrame, in that order. Exposed publicly so tests can drive
    /// individual frames without a full Run() loop. Must be called only
    /// while GetState() == EngineState::Running.
    void TickFrame();

    /// Requests that a currently-running Run() loop stop after the
    /// current frame completes, without running further frames.
    void RequestQuit() noexcept;

    /// Updates the application clear color used for the empty window and scene passes. Invalid
    /// non-finite/out-of-range RGBA values are rejected without changing the current color.
    [[nodiscard]] bool SetApplicationBackgroundColor(const std::array<float, 4U>& color) noexcept;

    /// True once RequestQuit() has been called (directly, or internally because
    /// IWindowManager::IsCloseRequested() went true - see TickFrame()'s own per-frame
    /// check). Run()'s own loop already honors this internally; this accessor exists for a
    /// caller driving its own manual Init()/Load()/TickFrame() loop instead of Run() (e.g. a
    /// packaged project's standalone runtime, which needs to load a scene between Load() and the
    /// first tick) to still exit correctly when the user closes the window.
    [[nodiscard]] bool IsQuitRequested() const noexcept { return m_quitRequested; }

    /// Diagnostic hook: how many objects are running a Blueprint right now (compiled and
    /// started by SyncScriptRuntime()).
    [[nodiscard]] std::size_t GetActiveScriptInstanceCount() const noexcept;

    /// Test/diagnostic hook: the Blueprint instance running on `entity`, or null when it has none
    /// (no script, not compiled yet, or compile failed).
    [[nodiscard]] Blueprint::BlueprintInstance* FindBlueprintInstance(Scene::Entity entity) noexcept;

    /// Release builds: writes the C++ for every distinct Blueprint program running right now into
    /// `directory` (`<blueprint>_<fingerprint>.native.cpp`), compiled against each object's real host, so
    /// adding those files to the game makes the same scripts run native there. Returns how many
    /// files were written, or nothing if one could not be.
    [[nodiscard]] std::optional<std::size_t> WriteNativeBlueprints(const std::filesystem::path& directory) const;

    /// Diagnostic/test hook: the collision enter/exit transitions computed by
    /// SyncCollisionLifecycle() on the most recent Update() call.
    [[nodiscard]] const Physics::CollisionLifecycleReport& GetLastCollisionLifecycleReport() const noexcept {
        return m_collisionLifecycleReport;
    }

    /// Transitions Running -> ShuttingDown -> Shutdown, tearing down
    /// ConfigManager, then CheckpointManager, then SaveGameSystem, then AudioSourceSystem, then AudioSystem, then AudioDevice, then InputSystem, then RaycastSystem, then PhysicsSystem, then CollisionSystem, then Renderer3D, then LightSystem, then MeshRenderer, then CameraSystem, then RenderSystem, then ShaderManager, then
    /// RenderDevice, then WindowManager (in that order — every GL object RenderDevice owns must
    /// be destroyed while WindowManager's context is still valid, before WindowManager's own
    /// destructor tears the context itself down), then FileSystem, then AssetBundle, then AssetImporter,
    /// then AssetManager (its destructor blocks until every in-flight load
    /// job finishes), then HotReload, then PrefabSystem, then
    /// SceneSerializer, then AssetDatabase, then SceneGraph, then
    /// EntityManager (its destructor frees every remaining live entity's
    /// component memory, which must happen before MemoryManager's leak
    /// check below), then EventSystem, then Timer, then ThreadPool (its
    /// destructor blocks until every worker drains and joins), then
    /// MemoryManager (logging its leak report — and, in debug builds,
    /// UVE_ASSERTing zero active allocations — before it is destroyed),
    /// then Logger, then CommandLine — the exact reverse of Init()'s
    /// construction order — logging the final message before the logger
    /// itself is torn down.
    void Shutdown();

    [[nodiscard]] EngineState GetState() const noexcept;
    /// The configuration in effect: the one the engine was constructed with, with the project's
    /// settings applied over it once Init() has read them.
    [[nodiscard]] const EngineConfig& GetConfig() const noexcept { return m_config; }
    [[nodiscard]] const FrameStats& GetFrameStats() const noexcept;
    [[nodiscard]] Scene::ParticleRuntimeSnapshot GetParticleRuntimeSnapshot() const;
    [[nodiscard]] VFX::VfxWorld& GetVfxWorld() noexcept;
    [[nodiscard]] const VFX::VfxWorld& GetVfxWorld() const noexcept;

    /// Requests normal fixed simulation or a held simulation state. Frame maintenance and rendering
    /// continue in both modes. Returns false outside EngineState::Running.
    [[nodiscard]] bool SetSimulationExecutionMode(SimulationExecutionMode mode) noexcept override;
    [[nodiscard]] SimulationExecutionMode GetSimulationExecutionMode() const noexcept override;

    /// Queues one fixed physics step while paused. The request is consumed by Update(), never from
    /// the caller's stack frame, and a second pending request is rejected.
    [[nodiscard]] bool RequestSingleSimulationStep() noexcept override;

    /// Submits a rewound shot for this frame's hitbox sync: the report's pairs merge into the
    /// live scan BEFORE the lifecycle tracker diffs it, so a lagged hit enters exactly like a
    /// live one (same Entered event, same damage path). Drains on the next hitbox sync - the
    /// same frame when submitted during simulation, the frame after when submitted later -
    /// and clears at Shutdown. Empty reports are harmless (nothing merges, nothing diffs).
    /// Resolve the report first against GetLagCompensator() at the shooter's view tick
    /// (see GetLagTick for the numbering); this only queues it.
    void SubmitLagShot(Physics::LagShotReport report);

    /// The newest lag tick recorded: 1-based engine frames (++ per hitbox sync, first record
    /// is tick 1, 0 means nothing recorded yet). Tick 0 is never a legitimate view - the
    /// Gameplay wiring fails it closed.
    ///
    /// Numbering is engine frames, not prediction ticks: the dedicated server maps them (it
    /// drives both clocks, so after each server frame it notes which prediction tick it
    /// applied alongside GetLagTick(), and resolves each shooter's shot against the
    /// engine tick recorded with that shooter's ack). Record history holds 64 frames.
    [[nodiscard]] Physics::LagTick GetLagTick() const noexcept;

    /// The engine's lag compensator: per-frame pose history, recorded automatically at the
    /// top of every hitbox sync. Gameplay resolves lagged shots against it, then submits
    /// them via SubmitLagShot(). Non-const: resolves update the compensator's stats.
    [[nodiscard]] Physics::HitboxLagCompensator& GetLagCompensator() noexcept;
    /// Inspection (stats, history size) without resolve rights.
    [[nodiscard]] const Physics::HitboxLagCompensator& GetLagCompensator() const noexcept;

    /// Marks an editor-owned transient simulation session. While active, checkpoint/save-game
    /// advancement is skipped independently from normal or paused fixed simulation execution.
    [[nodiscard]] bool SetTransientSimulationSessionActive(bool active) noexcept override;
    [[nodiscard]] bool IsTransientSimulationSessionActive() const noexcept override;
    [[nodiscard]] bool SetEditorPlaySceneName(std::string_view sceneName) noexcept override;
    [[nodiscard]] bool SetSimulationSpeedScale(float speedScale) noexcept override;
    [[nodiscard]] float GetSimulationSpeedScale() const noexcept override;

    /// Sets the entity Render() passes to Renderer3D::RenderFrame() as the camera to render
    /// from, starting with the next frame. Passing Scene::kInvalidEntity (the default) reverts
    /// Render() to its original no-op trace — this is the sole opt-in switch that keeps every
    /// frame-loop test/sample app predating this increment byte-identical unless it explicitly
    /// calls this.
    void SetActiveCamera(Scene::Entity cameraEntity) noexcept;

    /// The entity most recently passed to SetActiveCamera(), or Scene::kInvalidEntity if
    /// never called.
    [[nodiscard]] Scene::Entity GetActiveCamera() const noexcept;

    /// Tells Render()/SyncAdaptiveRenderResolution() to size and present the active camera's
    /// frame for `region` (a pixel sub-rect of the presentation surface, GL bottom-left origin -
    /// see Render::ViewportRect's own convention) instead of the full window. This is how an
    /// embedding editor - which draws its own docked panels around a 3D viewport that is only part
    /// of the window - keeps the render target's aspect ratio, resolution, and on-screen placement
    /// tracking that viewport panel's actual pixel footprint rather than the window's, every frame.
    /// std::nullopt (the default) restores ordinary full-window behavior; every standalone
    /// runtime/test path that never calls this is unaffected. Set (or cleared) once per frame,
    /// before TickFrame() - see Editor::DrawViewportPanel()'s own per-frame call.
    void SetEditorViewportRegion(std::optional<Render::ViewportRect> region) noexcept override;
    /// Where, in window coordinates, the editor shows the game's view (its UI drawn from there):
    /// the UI's pointer is measured from it. Nothing (the default) is the window's own corner.
    void SetUIPointerOrigin(std::optional<Math::Vector2> origin) noexcept { m_uiPointerOrigin = origin; }
    /// The size of the editor's view the game's UI is drawn into; nothing outside the editor.
    void SetUIViewportSize(std::optional<Math::Vector2> size) noexcept { m_uiViewportSize = size; }
    /// The device's own screen (its primary monitor): what "auto" screen size means.
    [[nodiscard]] Math::Vector2 DeviceScreenSize();
    /// Who answers a script's Set Show Mouse Cursor (the editor, for its own view). Without one the
    /// window's pointer is shown or hidden.
    void SetMouseCursorHandler(std::function<void(bool show)> handler) { m_mouseCursorHandler = std::move(handler); }
    /// Whether the application has focus, when it has windows of its own besides the main one (the
    /// editor's Entity Editor): the unfocused frame cap then applies only when none of them has it.
    void SetFocusProvider(std::function<bool()> provider) { m_focusProvider = std::move(provider); }
    /// Changes the frame-rate caps after Init (0: no cap of its own; vsync paces).
    void SetFrameRateCaps(const std::uint32_t focused, const std::uint32_t unfocused) noexcept {
        m_config.focusedFrameRateCap = focused;
        m_config.unfocusedFrameRateCap = unfocused;
    }
    [[nodiscard]] bool HasFocusProvider() const noexcept { return static_cast<bool>(m_focusProvider); }
    [[nodiscard]] bool HasMouseCursorHandler() const noexcept { return static_cast<bool>(m_mouseCursorHandler); }
    /// Who answers a script's Quit Game (the editor stops Play), once the frame's scripts are
    /// done. Without one the engine quits.
    void SetQuitGameHandler(std::function<void()> handler) { m_quitGameHandler = std::move(handler); }
    /// Who answers a script's Open Level, after the frame it was asked in: true when the level
    /// file exists and will load. Without one, Open Level answers false.
    void SetOpenLevelHandler(std::function<bool(const std::string& path)> handler) { m_openLevelHandler = std::move(handler); }
    /// The level the game is playing, as Get Current Level Name answers it.
    void SetCurrentLevelName(std::string name) { m_currentLevelName = std::move(name); }
    /// What scripts share across the game: the game data (Set/Get Game Data) and the clock.
    [[nodiscard]] ScriptServices& GetScriptServices() noexcept { return m_scriptServices; }
    /// Starts the game's script clock again and forgets queued messages, removals and saved
    /// values: called when Play starts.
    void ResetScriptGameState();

    /// Registers a non-owning callback invoked after the renderer has submitted the frame's scene
    /// work but immediately before the window back buffer is presented. This is a generic
    /// application-overlay seam: callers own all callback captures and must clear it before their
    /// captured state is destroyed. Headless runs never invoke the callback.
    void SetPostRenderCallback(std::function<void()> callback);

    /// Returns the service container bundling Logger/Timer/EventSystem/
    /// MemoryManager/ThreadPool/CommandLine/ConfigManager/EntityManager/
    /// SceneGraph/AssetDatabase/SceneSerializer/PrefabSystem/HotReload/
    /// AssetManager/AssetImporter/AssetBundle/FileSystem/RenderDevice/ShaderManager/
    /// RenderSystem/CameraSystem/MeshRenderer/LightSystem/Renderer3D/CollisionSystem/
    /// PhysicsSystem/RaycastSystem/InputSystem/GamepadInputSystem/MobileInputSystem/MobileGestureSystem/
    /// AudioDevice/AudioSystem/AudioSourceSystem/SaveGameSystem/CheckpointManager/WindowManager references. Valid only
    /// between Init() and Shutdown(). UVE_ASSERTs the services exist; also throws
    /// std::bad_optional_access in Release if called before Init() (or after Shutdown()) rather
    /// than dereferencing an empty std::optional.
    [[nodiscard]] EngineServices& GetServices();

    /// A new scene beside the engine's (its own entities, graph, events and renderer, configured
    /// like the engine's), for an editor document that must not touch the level. Null before Init().
    [[nodiscard]] std::unique_ptr<DocumentScene> CreateDocumentScene();

    /// Returns the current UI draw batch/font atlas (SyncUIRuntime() ticks it once per real
    /// frame during Update()) - for a host that wants to composite authored Canvas/UIText/UIImage/
    /// UIButton content itself (the editor's view draws it over its own image, in the view's pixels).
    [[nodiscard]] const UI::UIRuntime& GetUIRuntime() const noexcept { return m_uiRuntime; }

    /// The locale and string tables authored UI text is translated through. Owned here alongside
    /// the UI runtime that consumes it, and starts empty: with no table installed every text draws
    /// exactly as authored, so a project that never localizes pays nothing and sees no change.
    /// A host installs tables (see Localization::TryParseStringTableJson) and selects a locale.
    [[nodiscard]] Localization::LocalizationService& GetLocalizationService() noexcept {
        return m_localizationService;
    }
    [[nodiscard]] const Localization::LocalizationService& GetLocalizationService() const noexcept {
        return m_localizationService;
    }

    /// Returns this build's engine version — the single source of truth
    /// future systems (assets, plugins, projects, crash reports, Hub
    /// integration) are expected to read.
    [[nodiscard]] static Version GetEngineVersion() noexcept;

private:
    /// A Renderer3D over the given render systems and event bus, configured from m_config the
    /// same way for the engine's own scene and for every document scene.
    [[nodiscard]] std::unique_ptr<Render::IRenderer3D> MakeRenderer3D(Render::IMeshRenderer& meshRenderer,
                                                                          Render::ICameraSystem& cameraSystem,
                                                                          Render::ILightSystem& lightSystem,
                                                                          Events::IEventSystem& eventSystem);
    /// Registers the built-in MeshAsset/TextureAsset/ShaderAsset/MaterialAsset/
    /// AudioAsset/AnimationClipAsset
    /// loaders with AssetManager (Part 7.2's rendering-facing asset types). Called once from
    /// Init(), immediately after AssetManager is constructed. A private orchestration step,
    /// not a new service — AssetManager itself stays generic and unaware of these concrete
    /// asset types; only EngineCore's composition root knows about both.
    void RegisterBuiltInAssetLoaders();

    /// Ticks the timer, advances the frame counter, and records this
    /// frame's start instant (used by EndFrame() to compute frameTime).
    void BeginFrame();

    /// First commits GamepadInputSystem and MobileInputSystem, consumes the copied mobile
    /// snapshot through MobileGestureSystem, then calls InputSystem::Update — settling this
    /// frame's key/mouse/gamepad edge state and queueing any newly-triggered action's
    /// InputActionTriggeredEvent — before anything else,
    /// so the event dispatch that follows in this same call delivers it same-frame. Immediately
    /// after, calls IWindowManager::PollEvents() (a no-op for NullWindowManager) and, if
    /// IsCloseRequested() is now true, calls RequestQuit() — so a real window's OS close
    /// button drives the exact same graceful-shutdown path Run() already uses for any other
    /// quit request. Then advances
    /// the fixed-timestep accumulator, dispatches every event
    /// queued via IEventSystem::QueueEvent() since the last dispatch,
    /// runs zero or more PhysicsSystem::Step() calls (one per whole
    /// fixed step FixedStepResult::stepsToRun reports this frame — zero
    /// on a fast frame that hasn't accumulated a full step yet), then runs
    /// SceneGraph::Update() (transform-dirty-flag propagation) — after
    /// event dispatch and physics, so reparenting done by an event handler
    /// and positions moved by physics this frame are both picked up, and
    /// before LateUpdate()/Render(), so anything reading world transforms
    /// later in the frame sees up-to-date values. Each PhysicsSystem::Step()
    /// call already propagates its own intermediate world-transform updates
    /// internally, so this final Update() call only needs to catch
    /// anything non-physics that moved this frame. Finally drives
    /// CheckpointManager::Update() with every current scene-graph root (see
    /// Save::ICheckpointManager), so any auto-save this frame captures the just-updated world
    /// state, not last frame's. Also calls ShaderManager::Update() (Increment 21) alongside
    /// the existing HotReload::Poll()/AssetManager::CollectGarbage() maintenance
    /// calls, draining any completed background shader preprocessing, compiling/linking on this
    /// (the main) thread, and polling hot-reload-tracked programs for on-disk changes. If a real
    /// graphics backend loses its native surface/context, shader maintenance is skipped after the
    /// backend reports unusable so no follow-up GL call is issued.
    void Update();
    /// Reconciles authored ParticleEmitterComponent values with the existing bounded particle
    /// runtime, auto-emits from each ticking emitter's world pose, simulates one frame under
    /// configured gravity, and leaves renderer extraction read-only.
    void SyncParticleRuntime();
    /// Reconciles ParticleEmitterComponent entities that reference a `.uvfx` particle system
    /// asset with VfxWorld, updates attached owner poses and velocities, polls `.uvfx` hot
    /// reload when enabled, and steps VfxWorld simulation when not paused.
    void SyncVfxRuntime();
    /// Ages every decal by the step's simulated seconds and queues one Decal3DExpiredEvent per
    /// decal that runs out. Lifetime is simulation time, so a paused game freezes decals and the
    /// same scene expires the same decals on the same step.
    void SyncDecal3DObjects(float simulatedDeltaSeconds);

    /// Ticks UIRuntime once per real frame (not the fixed-step loop, so UI responsiveness tracks
    /// real input latency): hit-tests every live UIButtonComponent against the real
    /// IInputSystem mouse state and rebuilds the CPU-side UIDrawBatch snapshot. No GPU
    /// resource is touched here - rendering that batch is a later phase.
    void SyncUIRuntime();

    /// Runs every object's Blueprint for this frame (see SyncBlueprints). An object whose script
    /// path is not a Blueprint is reported once in m_scriptReconcileFailedEntities and skipped.
    void SyncScriptRuntime();
    /// Makes a Widget Blueprint's widgets (hidden) for a script's `ui.create`.
    [[nodiscard]] Scene::Entity CreateWidget(const std::string& path);
    /// Buttons clicked, sliders and check boxes changed, text boxes committed this frame: their
    /// `ui_*` events, to every scripted object above them.
    void RaiseWidgetScriptEvents();
    /// Stands each 3D View's asset on its own stage and turns it (UIView3DComponent).
    void SyncUIView3DStages(float deltaSeconds);
    /// The reference resolution of the first shown widget that scales with the screen, if any.
    [[nodiscard]] std::optional<Math::Vector2> ScaledCanvasReference();
    /// A widget's Stage (the child named Stage of its root Canvas), or none.
    [[nodiscard]] Scene::Entity FindWidgetStage(Scene::Entity widget) const;
    [[nodiscard]] Scene::Entity FindNamedUnder(Scene::Entity root, std::string_view name) const;
    /// `entity`'s position and rotation in `ancestor`'s space, from their local transforms.
    [[nodiscard]] std::optional<std::pair<Math::Vector3, Math::Quaternion>> PoseUnder(Scene::Entity entity,
                                                                                             Scene::Entity ancestor) const;
    /// Takes off a Stage what must not act from it: cameras, scripts, bodies, suns, environments.
    void StripWidgetStage(Scene::Entity stage);
    /// Compiles each object's text script once, drops the
    void SyncBlueprints(bool simulationPaused);
    /// Reconciles script components with instances in m_blueprints so event receivers are ready
    /// before any physics or animation step evaluates.
    void ReconcileBlueprintInstances();
    /// What a script slot runs: a Blueprint graph (.uvbp, or a widget's .uvwbp), compiled straight from the graph.
    /// `hash` identifies the file's bytes, so a changed or reappearing file is noticed.
    struct ScriptFile final {
        std::uint64_t hash = 0U;
        std::optional<Asset::BlueprintAsset> graph;
        std::string objectName;
        std::string objectKind;
    };
    [[nodiscard]] std::optional<ScriptFile> LoadScriptFile(const std::string& path) const;

    /// Steps every Character3D (CharacterControllerComponent) once per fixed step: velocity
    /// from the built-in movement when it is on (keyboard, with air control, coyote time and a
    /// jump buffer) or as a script left it, gravity unless Floating, SolidBody3D's motion locks,
    /// then Physics::CharacterController::MoveWithToI with the body's step height, slide
    /// count and push settings, a snap down to the floor after walking off a step, and the floor
    /// and ceiling state written back. An entity missing a ColliderComponent, or whose optional
    /// Rigid3DComponent isn't kinematic, is skipped - this function never adds or removes
    /// components.
    void SyncCharacterControllers(float fixedDeltaTimeSeconds);
    /// Moves every Kinematic3D object by its authored target velocity - eased by its interpolation,
    /// through the world rather than around it, pushing the rigid bodies it walks into. Runs before
    /// the character step, so a character standing on a platform is carried the same step the
    /// platform moves.
    void SyncKinematic3DObjects(float fixedDeltaTimeSeconds);

    /// Plays every AnimationSequencer and evaluates every AnimationGraph whose update runs on this clock
    /// (`physicsStep` true: the fixed step; false: once per frame), writing into each one's target
    /// object - the set target, or the object's parent - through its transform. Clips load
    /// asynchronously; until one is ready its player waits. Runs only while the simulation runs.
    void SyncAnimation(float deltaSeconds, bool physicsStep);

    /// Puts every ticking BoneAttachment3D on its bone, through
    /// Scene::SyncBoneAttachment3DObjects() - that function owns the resolution rules (which
    /// bone, in whose frame, refused when what) and reports what it did.
    ///
    /// This seam owns the order the call is made in: after the animation step that posed the
    /// skeleton, before SceneGraph::Update() propagates world transforms, so an attachment
    /// lands on the bone in the same frame the pose arrives instead of a frame behind its own
    /// animation.
    void SyncBoneAttachment3DObjects();

    /// Steps every ragdoll through Scene::UpdateRagdoll3DObjects() - that function owns the
    /// rules (which bodies, which limit, which contact, fixed-step clock, when to sleep) and writes
    /// the result into its skeleton's runtime pose.
    ///
    /// This seam owns the order: after every animation pass has composed a pose, before
    /// SceneGraph::Update() propagates world transforms. A corpse that is still driven by a
    /// clip must see the ragdoll win the pose for the frame, and a skinned mesh must follow it in
    /// the same frame rather than a frame behind the body that is falling.
    void SyncRagdoll3DObjects();
    /// Runs the scene-document upgrade (retired component names, legacy asset paths) once per
    /// project and upgrade revision, backing up every file it rewrites under Saved/UpgradeBackup.
    void UpgradeProjectContentIfNeeded();

    /// Positions every root follow camera from its target through Scene::UpdateCameraFollow() -
    /// that function owns the skip rules (parked, dead, transformless, parented) and the pose
    /// math. This seam owns the order: after every mover has stepped, before
    /// SceneGraph::Update() propagates, so the camera's world transform is current for the
    /// same frame's render. Runs ungated like the bone attachments, so a paused simulation still
    /// frames its target instead of freezing mid-cutscene.
    void SyncCameraFollow();

    /// Steps every live Projectile3DComponent entity (that also has a transform): the motion is
    /// `Physics::StepProjectile3D()` - `velocity` accumulates `acceleration * dt`, the sphere of
    /// `radius` is swept along that step in world space against the layers `collisionMask` accepts
    /// (never its own entity), the contact is resolved through the component's authored `hitPolicy`
    /// (Stop halts it, Bounce reflects it through `restitution`/`friction`), and `remainingLifetime`
    /// counts down to clear `active`. The contact is written back into the component's runtime hit
    /// fields, and every resolved contact is queued as a `Physics::Projectile3DHitEvent` with its
    /// evidence - damage, effects and despawning are gameplay's, and it decides them from that. The
    /// engine never destroys the entity itself.
    void SyncProjectile3DObjects(float fixedDeltaTimeSeconds);

    /// Diffs a fresh Physics::ICollisionSystem::DetectCollisions() snapshot against the
    /// previous tick's via m_collisionLifecycleTracker, storing the resulting enter/exit
    /// transitions in m_collisionLifecycleReport, before scripts run so they see this tick's
    /// results. The stored report is drained to scripts after the tick loop (see
    /// SyncBlueprints), never here: slots do not exist before the first tick, so raising here
    /// would drop every frame-1 enter.
    void SyncCollisionLifecycle();
    /// Raises one contact event to both parties' scripts - each hears the other object's name.
    /// Dead or scriptless parties are skipped silently; the report keeps its transitions (the
    /// GetLastCollisionLifecycleReport hook reads them after the frame).
    void RaiseContactScriptEvent(Scene::Entity first, Scene::Entity second,
                                    std::string_view event);

    /// Casts a real ray for every live, enabled RayCast3DComponent entity (that also has a
    /// WorldTransformComponent) through IRaycastSystem, writing the closest result back into
    /// hit/hitPosition/hitNormal/hitEntity - previously this object type existed only as authored
    /// data with nothing evaluating it. The authored `direction` is treated as local-space and
    /// rotated by the entity's world rotation (Math::RotateVector), matching
    /// LightSystem's own local-to-world direction convention.
    ///
    /// Every gate fails closed and clears the WHOLE result, not just the flag: a disabled,
    /// malformed or unswept ray has no hit, no point, no normal and no entity, so a consumer that
    /// reads hitEntity without checking hit first can never act on last frame's hit. Malformed
    /// means the component fails its own validator (a degenerate direction, a non-positive length,
    /// an exclusion slot that names nothing, a duplicated exclusion).
    ///
    /// `exclusions` is consumed: the declared prefix of the authored entity references is handed to
    /// IRaycastSystem as Physics::RaycastQuery::excludedEntities, where it is checked before
    /// the layer mask - an exclusion is not a mask, so no layer can bring an excluded entity back.
    /// Those references are remapped through the scene file's local-id table on load (like the
    /// visibility parent and hierarchy parent), which is what makes them authored data rather than
    /// a runtime handle that would mean something else after the next load.
    void SyncRayCast3DObjects();

    /// Simulates every live, enabled, valid SpringArm3D object, one ray per arm per fixed step,
    /// through Physics::StepSpringArm3D - where the cast along the arm's local +Z, the target
    /// under its margin, the motion law (retraction snaps so a camera never clips for one smooth
    /// frame's sake; extension blends at the authored `smoothing` per second so the camera springs
    /// back instead of popping the way Godot's SpringArm3D does; a disabled arm hands its length
    /// back) and the children riding the delta all live, together, and are pinned by their own
    /// tests. This tick only decides which arms run, in what order, and that an arm the step
    /// refuses keeps the length it had.
    ///
    /// Runs inside the fixed-step loop (with character controllers and projectiles) because
    /// extension is dt-dependent and the raycast must see the same simulated collider poses the
    /// physics step just produced.
    void SyncSpringArm3DObjects(float fixedDeltaTimeSeconds);

    /// Bakes each enabled NavMeshVolume3D's volume into a cached navmesh and steps every ticking
    /// NavSeeker3D against the mesh under it, once per fixed step.
    ///
    /// The tick owns WHAT and WHEN, the runtime owns the rules: which region an agent is standing on
    /// (its own volume first, else the nearest within the agent's off-mesh tolerance), when a region
    /// is rasterized or re-rasterized (first sight, a moved or resized volume, changed bake
    /// settings, or `rebuildRequested`), and every field the agent publishes back into its
    /// component - `desiredVelocity`, `nextPathPosition`, `pathStatus`, `pathChanged`,
    /// `targetReached`. That is Navigation::NavigationRuntime::Sync()'s job, not this
    /// function's, which is what makes the whole navigation step testable without an EngineCore.
    ///
    /// Runs after the movers in the fixed step, so an agent plans from where the bodies actually are
    /// this step; the velocity it publishes is what a mover or a script applies on the next one. The
    /// same Process physicsPriority ordering the other movers use decides which agent steers first,
    /// and an agent the step skips (Disabled, PausedOnly, no transform) is left exactly as authored.
    void SyncNavigation(float fixedDeltaTimeSeconds);

    /// The combat pairing, every frame: the scan itself is Physics::SyncHitboxes3D() (the
    /// hurtbox snapshot and every gate it enforces are documented there and testable on their own),
    /// and what the tick adds is the consequence contract - the report is diffed against the
    /// previous tick's through m_hitboxStrikeLifecycleTracker, and each enter/exit transition is
    /// queued as a typed Physics::Hitbox3DStrikeEnteredEvent / Hitbox3DStrikeExitedEvent.
    ///
    /// So there are two answers, and they are different questions: the per-hitbox strike list is
    /// STATE ("I am touching these right now" - runtime-only, never serialized, refreshed every
    /// frame), and the events are the EDGE ("this hit started", "this hit ended"). Damage,
    /// knockback, i-frames and hit reactions are gameplay's - it decides them from the events, which
    /// carry what struck what, how deeply, along which axis and on which channel. The engine never
    /// applies a consequence of its own.
    ///
    /// Lag compensation rides along: every sync first records the settled poses under the
    /// next 1-based lag tick, then drains SubmitLagShot()'s inbox into the live report
    /// before the tracker diffs it. An empty inbox diffs exactly the live scan, so games
    /// that never submit lag shots behave bit-for-bit as before.
    void SyncHitbox3DObjects();
    /// Ticks gameplay attribute pools (regen plus status effects) and queues the attribute and
    /// health events the tick produced. Frame-rate: drift is delta-scaled, so wall-clock status
    /// durations stay fair at any frame rate.
    void SyncGameplayAttributes(float deltaSeconds);
    /// Steps playing cinematics: advances each timeline, queues the CinematicEventFired keys
    /// the playhead passed, and cuts the active camera to the live shot. Autoplay shots start
    /// themselves on the first running frame; cinematics without cuts never touch the camera.
    void SyncCinematic(float deltaSeconds);
    /// Evaluates every AI brain against its blackboard (a missing blackboard reads as empty, so
    /// board-less priority brains work) and queues AiActionSelected when the selection changes.
    void SyncAiBrains(float);
    /// Decays noise emitters, then senses for every perception component with a blackboard and a
    /// world transform: watched-tagged entities resolve through sight (range, cone, raycast line of
    /// sight), emitters through hearing, and both write the sensor's blackboard.
    void SyncPerception(float deltaSeconds);

    /// The interaction scan, new wiring for previously unconsumed authored data (the
    /// Established interactor/focus loop Godot leaves every game to hand-roll out of
    /// Area3D signals): every frame, every character-controller entity that has a
    /// ColliderComponent and a world transform is an interactor, a possessed Player3D is the
    /// PRIMARY interactor when one exists (else the first in (index,generation) order via
    /// Scene::ResolvePrimaryInteractor), and every InteractionArea3D object's
    /// runtime state is refreshed against them. The full contract: only enabled, valid areas
    /// participate (everything else fails closed - a disabled or invalid area ends the frame
    /// with zero interactors, never stale ones, SyncHitbox3DObjects's discipline); both
    /// volumes are exact oriented boxes (world position/rotation + authored halfExtents, world
    /// scale intentionally not applied - the ColliderComponent/AreaComponent world-shape
    /// convention - degenerate rotations fall back to identity); an overlap requires symmetric
    /// layer/mask acceptance (AreaOverlapSystem's rule) and an area never lists the
    /// interactor living on its own entity; overlap is the exact 15-axis oriented-box test from
    /// Physics::Detail, and touching boundaries do not count. Each area stores its interacting
    /// candidates into a bounded list (the authored maximumCandidates clamped to
    /// kMaximumInteractionAreaCandidates by Scene::ResolveInteractionAreaCandidateCap,
    /// overflow flagged) and exactly one area - the one nearest the primary interactor,
    /// ties broken by (index,generation) via Scene::ResolveInteractionFocus - is marked
    /// focusedByPrimaryInteractor. Runtime state is never serialized. Interact (the Interact
    /// action or E) queues Gameplay::InteractRequestedEvent for the possessed player when
    /// they are in the focused area. Prompt UI is still gameplay. interactionTag does not
    /// filter the scan.
    ///
    /// The contract above is implemented in Physics::SyncInteractionAreas(), which this calls:
    /// the tick owns WHEN the scan runs (it is in the fixed-step order), the seam owns what the
    /// scan means - which is what makes every clause of the contract testable without standing up
    /// an EngineCore. The Objects/3D layer keeps holding pure authoring data plus the
    /// dependency-free resolvers; the Physics include the exact overlap test needs never leaks
    /// into it. The returned InteractionAreaScanResult carries the frame's accounting
    /// (interactors, areas visited/refreshed/truncated, primary interactor, focused area) for
    /// callers and tests that want the numbers rather than the area components.
    void SyncInteractionArea3DObjects();

    /// The LevelStreamer3D consumer: pure per-tick streaming verdicts on LevelStreamer3D objects
    /// (Godot has no built-in counterpart at all; the standard streaming volumes are the inspiration).
    /// Pass 1 (read-only) collects the viewer point cloud for the tick - the active camera's world
    /// position plus every character-controller entity's world position, finite poses only
    /// (Frostbite's listener-model multi-source). Pass 2 applies Scene::
    /// ResolveLevelStreamer3DStreamingAction's measured verdicts per streamer: a load request
    /// synchronously deserializes levelPath through the scene serializer and remembers the fresh
    /// roots in m_levelStreamerLoadedRoots; an unload request destroys those remembered subtrees.
    /// At most kMaximumLevelStreamer3DLoadsPerTick loads START each tick - the rest carry over
    /// next tick so a teleport across the map costs a bounded burst (Frostbite time-slicing; the
    /// budget is verified by engine test). A failed load latches m_levelStreamerLoadFailures so it
    /// is retried never again this session - fail-closed and loud, not a retry storm. Honest
    /// boundaries: loading is synchronous today (big levels take the hit in one tick; async
    /// streaming is real follow-up, and component.loadRequested documents the future contract);
    /// unload bookkeeping survives Play/Stop naturally because everything is validated through
    /// IsAlive() before destruction.
    void SyncLevelStreamer3DObjects();

    /// The ReflectionProbe3D consumer: the capture scheduler plus per-camera influence mixer
    /// (Godot bakes all probes and ends at the face with a hard clip; this instead resolves a
    /// first-class blend weight per probe every tick and time-slices expensive captures).
    /// Pass 1 (read-only) snapshots live probes; pass 2 computes each probe's influence weight
    /// on the active camera via Scene::ResolveReflectionProbe3DInfluenceWeight (translation
    /// undoes the probe world position, the conjugate of its world rotation undoes orientation;
    /// world scale is deliberately NOT folded in - the weight stays the authored box's weight.
    /// Capture requests are budgeted by kMaximumReflectionProbeCapturesPerTick and serviced
    /// oldest-waiter-first - not naive nearest-first, which measurably starves a farther probe
    /// under continuous demand - with camera distance (squared, no sqrt) and (index,generation)
    /// as the tie-breaks. Stragglers age their captureWaitTicks and re-request on the next tick.
    /// A serviced capture flips capturedOnce, clears the
    /// OnDemand latch, and bumps captureGeneration - the runtime contract Renderer3D binds
    /// against when it renders six 2D cubemap faces. This sync stays CPU-only: engine tests must
    /// still pass without a GPU. The imagery lives in the renderer, not in the generation counter.
    void SyncReflectionProbe3DObjects();

    /// The WorldPartition3D consumer: cell-based visibility for a partition's own subtree
    /// (Godot has no built-in equivalent at all; this is World Partition translated into
    /// an in-document budget). Pass 1 walks each enabled, valid partition's descendants
    /// breadth-first (a nested WorldPartition3D manages its own subtree - closest ancestor wins,
    /// so an inner partition is never re-partitioned by an outer one), marks every descendant
    /// carrying a MeshComponent with the engine-owned WorldPartition3DMembershipComponent,
    /// and resolves its cell. Pass 2 ranks the partition's OCCUPIED cells by the nearest member
    /// squared distance to the nearest viewer (the same camera/controller cloud the streamer
    /// uses - a level far away has a live floor but a dead interior), admits exactly
    /// maximumLoadedCells of them, and flips membership.live so MeshRenderer drops the rest
    /// at candidate-build time. loadedCellCount is always <= maximumLoadedCells the same tick it
    /// is written. Honest boundary: membership stamps a derived verdict about THIS tick; the
    /// authored scene is never rewritten for it (that is exactly why VisibilityComponent's
    /// authored `visible` is not the carrier here), and an orphan membership after a partition's
    /// death fails OPEN through the pure resolver check in the renderer gate rather than hiding
    /// content forever.
    void SyncWorldPartition3DObjects();

    /// The VisibilityRegion3D consumer: interior culling for the meshes standing inside each
    /// authored visibility box (Godot has no built-in equivalent at all - Godot's
    /// VisibilityNotifier3D answers "is the box on screen", not "should this room's contents
    /// render"). Each region's `active` is recomputed from the same viewer cloud the streamer
    /// and world partition use: active while any viewer stands inside its box, or while there
    /// are no viewers at all (fail-open: an empty world shows everything). Pass 1 sweeps the
    /// existing memberships: a member that walked OUT of the box, whose layer gate closed, whose
    /// region got disabled, or whose region died goes back to live (dead regions rebrand the
    /// membership to kInvalidEntity so Pass 2 can rehome the mesh that same tick) - released
    /// content must never be stuck hidden a tick later. Pass 2 discovers un-owned meshes inside
    /// an enabled region whose mesh visibilityLayers share a bit with the region mask and stamps
    /// the engine-owned VisibilityRegion3DMembershipComponent with the NEAREST containing
    /// region (ties resolve in entity-id order, so overlapping-room scenes are deterministic).
    /// MeshRenderer drops !live members at candidate-build time and counts them in
    /// regionCulledEntities. Honest boundary: like the world partition's membership, this is a
    /// derived verdict about THIS tick; authored VisibilityComponent.visible stays untouched.
    void SyncVisibilityRegion3DObjects();

    /// Recomputes the bounded aspect-preserving render target from the live drawable size and
    /// transactionally resizes Renderer3D before the frame's scene work begins.
    void SyncAdaptiveRenderResolution();

    void PublishAreaOverlapLifecycleEvents();

    /// Recomputes FrameStats::fps (an exponential moving average of
    /// 1/deltaTime). Then, if SetActiveCamera() has set a valid camera entity, syncs the audio
    /// listener to that entity's WorldTransformComponent (the spec's "AudioListener —
    /// Attached to Camera3D by default"); with no active camera set, the listener simply stays
    /// wherever it was last set manually. Then runs AudioSourceSystem::Sync() (entirely
    /// data-driven off which entities have a WorldTransformComponent + AudioSourceComponent,
    /// so a scene with none is a cheap no-op) followed by AudioSystem::Update() (recomputing
    /// attenuated gain for every live source). The documented hook point for future post-Update,
    /// pre-Render systems (camera follow, animation retargeting).
    void LateUpdate();

    /// Calls Renderer3D::RenderFrame(*m_entityManager, m_activeCamera) when
    /// m_activeCamera is valid; otherwise logs the no-op trace line and, for a valid window,
    /// clears the real default framebuffer so an empty scene is visible without fake content. When a
    /// real window/GL device is active (see m_windowedRenderingActive), invokes the optional
    /// post-render editor callback after the scene tone-mapping pass and then presents exactly once.
    /// If the backend reports unusable, the render path returns before scene/shader/present work.
    void Render();

    /// Computes this frame's wall-clock frameTimeSeconds and records it
    /// into FrameStats.
    void EndFrame();

    /// Asserts IsValidTransition(m_state, newState), then applies it.
    void TransitionState(EngineState newState);

    EngineConfig m_config;
    DynamicRenderResolutionController m_dynamicRenderResolutionController;
    double m_currentRenderResolutionScale = kMaximumRenderResolutionScale;
    EngineState m_state = EngineState::Uninitialized;
    SimulationExecutionMode m_simulationExecutionMode = SimulationExecutionMode::Running;
    bool m_singleSimulationStepPending = false;
    bool m_transientSimulationSessionActive = false;
    float m_simulationSpeedScale = 1.0F;
    bool m_graphicsBackendLossLogged = false;
    bool m_adaptiveResizeFailureLogged = false;
    bool m_integerScaleFallbackLogged = false;
    std::string m_editorPlaySceneName;
    std::optional<std::chrono::steady_clock::time_point> m_nextFrameDeadline;
    std::uint32_t m_lastFrameRateCap = 0U;
    std::optional<Render::ViewportRect> m_editorViewportRegion;

    std::unique_ptr<CommandLine::ICommandLine> m_commandLine;
    std::unique_ptr<Debug::ILogger> m_logger;
    std::unique_ptr<Memory::IMemoryManager> m_memoryManager;
    std::unique_ptr<Threading::IThreadPool> m_threadPool;
    std::unique_ptr<Utilities::ITimer> m_timer;
    std::unique_ptr<Events::IEventSystem> m_eventSystem;
    std::unique_ptr<Scene::IEntityManager> m_entityManager;
    std::unique_ptr<Scene::ISceneGraph> m_sceneGraph;
    std::unique_ptr<Asset::IAssetDatabase> m_assetDatabase;
    std::unique_ptr<Asset::IProjectFileIndex> m_projectFileIndex;
    std::unique_ptr<Asset::IDerivedArtifactCache> m_derivedArtifactCache;
    std::unique_ptr<Asset::IProjectChangeWatcher> m_projectChangeWatcher;
    std::unique_ptr<Scene::ISceneSerializer> m_sceneSerializer;
    std::optional<Math::Vector2> m_uiPointerOrigin;
    std::optional<Math::Vector2> m_uiViewportSize;
    std::optional<Math::Vector2> m_deviceScreenSize;
    std::function<void(bool)> m_mouseCursorHandler;
    std::function<bool()> m_focusProvider;
    std::chrono::steady_clock::time_point m_uiView3DClock{};
    std::unique_ptr<Scene::IPrefabSystem> m_prefabSystem;
    std::unique_ptr<Asset::IHotReload> m_hotReload;
    std::unique_ptr<Asset::IAssetManager> m_assetManager;
    std::unique_ptr<Asset::IAssetImporter> m_assetImporter;
    std::unique_ptr<Asset::IAssetImportQueue> m_assetImportQueue;
    std::unique_ptr<Asset::IAssetBundle> m_assetBundle;
    std::unique_ptr<Asset::IFileSystem> m_fileSystem;
    std::unique_ptr<Window::IWindowManager> m_windowManager;
    std::unique_ptr<Render::IRenderDevice> m_renderDevice;
    std::unique_ptr<Render::Shader::IShaderManager> m_shaderManager;
    std::unique_ptr<Render::IRenderSystem> m_renderSystem;
    std::unique_ptr<Render::IComputeSystem> m_computeSystem;
    std::unique_ptr<Render::ICameraSystem> m_cameraSystem;
    std::unique_ptr<Render::IMeshRenderer> m_meshRenderer;
    std::unique_ptr<Render::ILightSystem> m_lightSystem;
    std::unique_ptr<Render::IRenderer3D> m_renderer3D;
    std::unique_ptr<Physics::ICollisionSystem> m_collisionSystem;
    std::unique_ptr<Physics::PhysicsConstraintSystem> m_physicsConstraintSystem;
    std::unique_ptr<Physics::IPhysicsSystem> m_physicsSystem;
    std::unique_ptr<Physics::IPhysicsQuerySystem> m_physicsQuerySystem;
    std::unique_ptr<Physics::IRaycastSystem> m_raycastSystem;
    std::unique_ptr<Scene::ParticleRuntime> m_particleRuntime;
    /// Unused fraction of a particle per emitter, so a rate below the frame rate still emits.
    std::unordered_map<Scene::Entity, float> m_particleEmitRemainder;
    std::unique_ptr<VFX::VfxWorld> m_vfxWorld;
    Render::VfxDrawList m_vfxDrawList;
    struct VfxEntityInstanceState final {
        VFX::VfxHandle handle{};
        std::string systemAssetPath;
        std::uint32_t seed = 0U;
        Math::Vector3 lastWorldPosition{};
        bool hasLastWorldPosition = false;
        bool wasEmitting = false;
    };
    std::unordered_map<Scene::Entity, VfxEntityInstanceState> m_vfxEntityInstances;

    /// Baked navmeshes by region entity, and one steering state per agent entity. Owned here so the
    /// caches live exactly as long as the session that built them; a scene teardown clears them
    /// through the same object.
    std::unique_ptr<Navigation::NavigationRuntime> m_navigationRuntime;
    UI::UIRuntime m_uiRuntime;
    Localization::LocalizationService m_localizationService;
    Physics::AreaOverlapLifecycleTracker m_areaOverlapLifecycleTracker;
    Physics::Hitbox3DStrikeLifecycleTracker m_hitboxStrikeLifecycleTracker;
    /// Rewound pose history, recorded automatically every frame (see SyncHitbox3DObjects).
    /// Never reset: stale poses fail the resolve's IsAlive/generation gates and evict within
    /// 64 frames, so scene transitions heal on their own.
    Physics::HitboxLagCompensator m_hitboxLagCompensator;
    /// Submitted lag shots awaiting the next hitbox sync, drained pre-tracker every frame.
    std::vector<Physics::LagShotReport> m_pendingLagShots;
    /// Engine-frame lag numbering, 1-based (++ then record; 0 = nothing recorded yet).
    Physics::LagTick m_lagTick = 0U;
    Scene::PossessionLifecycleTracker m_possessionLifecycleTracker;
    Physics::CollisionLifecycleTracker m_collisionLifecycleTracker;
    Physics::CollisionLifecycleReport m_collisionLifecycleReport;
    std::unique_ptr<Input::IInputSystem> m_inputSystem;
    std::unique_ptr<Input::IGamepadInputSystem> m_gamepadInputSystem;
    std::unique_ptr<Input::IMobileInputSystem> m_mobileInputSystem;
    std::unique_ptr<Input::IMobileGestureSystem> m_mobileGestureSystem;
    std::unique_ptr<Audio::IAudioDevice> m_audioDevice;
    std::unique_ptr<Audio::IAudioSystem> m_audioSystem;
    std::unique_ptr<Audio::IAudioSourceSystem> m_audioSourceSystem;
    /// Concrete physics-backed occlusion provider. Borrowed by m_audioSystem, which is why it is
    /// declared after it and reset only once the system is gone.
    std::unique_ptr<Core::RaycastAudioOcclusion> m_audioOcclusion;
    std::unordered_map<Scene::Entity, std::string> m_scriptReconcileFailedEntities;
    /// An object running a Blueprint: the path it was compiled from, so a changed path recompiles.
    struct BlueprintSlot final {
        std::string path;
        /// The hash of the file it was compiled from; a file that now reads differently is recompiled.
        std::uint64_t hash = 0U;
        /// The object's export values it started with; changing them restarts the script.
        std::map<std::string, std::string> exportValues;
        std::shared_ptr<const Blueprint::Program> program;
        std::unique_ptr<BlueprintObjectHost> host;
        std::unique_ptr<Blueprint::BlueprintInstance> instance;
        bool readyRaised = false;
    };
    std::unordered_map<Scene::Entity, BlueprintSlot> m_blueprints;
    /// What every script reaches beyond its object (filled in by FillScriptServices).
    ScriptServices m_scriptServices;
    /// Script messages waiting for the end of this frame's handlers: target, name, payload.
    std::vector<std::tuple<Scene::Entity, std::string, std::string>> m_pendingScriptEvents;
    /// Objects scripts asked to remove, removed once every script has run.
    std::vector<Scene::Entity> m_pendingScriptDestroys;
    std::optional<std::string> m_pendingLevel;
    bool m_pendingQuit = false;
    /// Sounds Play Sound made, each removed when it is over.
    std::vector<Scene::Entity> m_oneShotSounds;
    /// The buttons the pointer was over last frame, for On Hovered / On Unhovered.
    std::unordered_set<Scene::Entity> m_hoveredButtons;
    /// Camera Shake: how strong, how long, how long is left, and what is on the camera now.
    double m_cameraShakeStrength = 0.0;
    double m_cameraShakeDuration = 0.0;
    double m_cameraShakeRemaining = 0.0;
    Scene::Entity m_cameraShakeTarget = Scene::kInvalidEntity;
    Math::Vector3 m_cameraShakeOffset{};
    double m_scriptGameTime = 0.0;
    double m_scriptGameDelta = 0.0;
    std::function<void()> m_quitGameHandler;
    std::function<bool(const std::string&)> m_openLevelHandler;
    std::string m_currentLevelName;
    void FillScriptServices();
    /// Raises queued messages and timers, then removes what scripts destroyed.
    void FinishScriptFrame(double dt, bool simulationPaused);
    /// The hash of each script path that failed (none when it could not be read), so an edit to it -
    /// or the file appearing - is retried.
    std::unordered_map<std::string, std::optional<std::uint64_t>> m_blueprintFailedSources;
    /// When Blueprint files were last re-read for edits. Wall time, not frame time: a paused or
    /// fixed-step game still picks up a saved script.
    std::chrono::steady_clock::time_point m_blueprintLastRecheck{};
    /// Clips the animation objects play, by asset guid. Declared after m_assetManager so the handles
    /// release before the manager is destroyed; clips no object references any more are dropped
    /// each frame.
    std::unordered_map<std::uint64_t, Asset::AssetHandle<Asset::AnimationClipAsset>> m_animationClips;
    /// The Motion Montages MontageSequencers play, by asset guid; reread when the file changes (the
    /// Motion Montage Editor saves while the game runs in the editor).
    struct MontageCacheEntry final {
        std::filesystem::path path;
        std::filesystem::file_time_type written{};
        std::optional<Asset::MotionMontageAsset> montage;
    };
    std::unordered_map<std::uint64_t, MontageCacheEntry> m_animationMontages;
    /// IK on a skeleton nothing animates (an editor authoring a rig, a prop with no clip). A modifier blends over
    /// the pose a driver wrote, so applying it to a pose that is not rewritten each frame would blend the same
    /// solve again every frame and creep. `base` is the pose the author left; `written` is what the modifiers
    /// last wrote over it. While the pose still equals `written` the author has not touched it, so the next solve
    /// starts again from `base`; any other pose is the author's new base.
    struct AuthoredIkPose final {
        std::vector<Scene::SkeletonBonePose> base;
        std::vector<Scene::SkeletonBonePose> written;
    };
    std::unordered_map<Scene::Entity, AuthoredIkPose> m_authoredIkPoses;
    std::unique_ptr<Save::ISaveGameSystem> m_saveGameSystem;
    std::unique_ptr<Save::ICheckpointManager> m_checkpointManager;
    std::unique_ptr<Config::IConfigManager> m_configManager;
    // The project's settings file, declared at construction and read at the start of Init(), before
    // anything reads the EngineConfig fields it overrides.
    Config::SettingsDocument m_projectSettings;
    std::vector<Config::SettingsObserverSubscription> m_projectSettingsSubscriptions;
    // The project's input map, read and registered with the input system during Init().
    InputMapDocument m_inputMap;
    std::optional<EngineServices> m_services;

    FrameStats m_frameStats;
    std::chrono::steady_clock::time_point m_frameStartTime;
    bool m_quitRequested = false;
    Scene::Entity m_activeCamera = Scene::kInvalidEntity;

    /// LevelStreamer3D runtime bookkeeping (never serialized, keyed by the streamer entity):
    /// the fresh roots each loaded streamer currently owns (for subtree destruction on unload),
    /// and the per-streamer load-failure latch that makes the fail-closed contract measured by
    /// tests - a 3-tuple state (streamer -> roots, failure-flagged) is intentionally all the
    /// state the synchronous path needs; the future async path will own the in-flight work set.
    std::unordered_map<Scene::Entity, std::vector<Scene::Entity>> m_levelStreamerLoadedRoots;
    std::unordered_map<Scene::Entity, bool> m_levelStreamerLoadFailures;

    /// True iff Init() constructed a real, valid WindowManager + GlRenderDevice pair (i.e.
    /// !EngineConfig::headless and window/context creation succeeded). Gates
    /// Update()'s window-event pump/close-check and Render()'s final Present() call — both stay
    /// exact no-ops when this is false, matching every prior increment's headless behavior.
    bool m_windowedRenderingActive = false;

    /// True while the native presentation surface is known to be drawable. Android may clear this
    /// for a transient EGL surface loss and restore it through IWindowManager recovery without
    /// tearing down the entire engine or selecting the NullRenderDevice.
    bool m_presentationSurfaceReady = false;

    /// Set by Init() if a real (non-headless) window/GL-context creation attempt failed. Checked
    /// by Load(), which fails in that case so Run() shuts the engine down cleanly rather than
    /// proceeding into a broken windowed session.
    bool m_windowCreationFailed = false;

    /// Optional application-owned drawing invoked after Renderer3D tone-maps the scene and
    /// before RenderDevice::Present(). No editor/UI type enters core.
    std::function<void()> m_postRenderCallback;
};

} // namespace UVE::Core
