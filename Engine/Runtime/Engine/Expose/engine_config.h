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
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "log_level.h"
#include "vector2.h"
#include "vector3.h"
#include "platform.h"
#include "application_window_settings.h"

namespace UVE::Core {

/// Render backend EngineCore::Init() should try first when a window is created. The engine
/// always degrades gracefully through this fixed fallback chain — the requested backend is a
/// starting point, never a hard requirement:
///   Vulkan -> OpenGL -> NullRenderDevice (engine still runs headless-correct, renders nothing)
/// so picking a backend that the host lacks (no ICD, no GLFW Vulkan support, no display) is a
/// logged warning, never a fail-fast abort. `Auto` = `OpenGL` today: OpenGL remains the
/// production-default backend and Auto exists so future milestones can flip the default without
/// changing the meaning of persisted configurations.
enum class RenderBackendPreference : std::uint32_t {
    Auto = 0U,
    OpenGL,
    Vulkan,
    Null, ///< Force the null swapchain device (rendering disabled) even with a window.
};

/// Runtime on-demand shader work scheduling. Ahead-of-time compilation and placeholder programs
/// are not currently provided; asynchronous mode preprocesses on workers, then compiles/links on
/// the render thread when ShaderManager::Update() drains completed jobs.
enum class ShaderCompilationMode : std::uint32_t {
    SynchronousOnDemand = 0U,
    AsynchronousOnDemand,
};

/// Post-process anti-aliasing methods the runtime can execute today. Temporal AA is omitted until
/// the renderer has frame-history, motion vectors, and camera-jitter support.
enum class PostProcessAAMethod : std::uint32_t {
    None = 0U,
    FastApproximate,
};

/// Frustum-culling mode for the renderer's main colour view. DebugFreeze stops rejecting geometry
/// by frustum so every placed mesh, primitive, surface instance and decal is submitted for the frame
/// - the "is it off-screen or is it missing?" diagnostic. Author visibility flags, world partitioning,
/// visibility regions, LOD/SurfaceInstance draw distances, render layers, Occluder3D occlusion and the
/// shadow cascades' own light-frustum culls all keep running; see Render::CullingSettings.
enum class FrustumCullingMode : std::uint32_t {
    Enabled = 0U,
    DebugFreeze,
};

/// Spatial reconstruction used when the scene render target is smaller than the presentation target.
/// Temporal upscaling remains unavailable without frame history and motion vectors.
enum class SpatialUpscalingMethod : std::uint32_t {
    Bilinear = 0U,
    Nearest,
};

/// Tone-mapping operators the built-in final pass can execute. LinearClamp is a direct clip into
/// display range; ACES provides a high-contrast filmic curve; SoftFilmic provides smooth
/// highlight desaturation and roll-off and is the default.
enum class ToneMappingMethod : std::uint32_t {
    LinearClamp = 0U,
    Aces,
    SoftFilmic,
};

/// Bounds shared by the persisted resolution-scale settings and EngineCore's startup validation.
inline constexpr double kMinimumRenderResolutionScale = 0.5;
inline constexpr double kMaximumRenderResolutionScale = 1.0;
inline constexpr double kMinimumRenderResolutionTargetFrameTimeMilliseconds = 8.0;
inline constexpr double kMaximumRenderResolutionTargetFrameTimeMilliseconds = 100.0;
inline constexpr double kDefaultRenderResolutionTargetFrameTimeMilliseconds = 16.6667;

/// Decoded icon pixels staged by the application bootstrap and copied into the native window
/// descriptor when a desktop window is created.
struct EngineApplicationIconImage final {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<std::uint8_t> rgba8;

    [[nodiscard]] bool operator==(const EngineApplicationIconImage&) const = default;
};

/// Configuration passed into EngineCore's constructor. Every field has a
/// sensible default, so most callers (including tests) can construct one
/// with zero overrides. New fields can be added here in the future without
/// changing EngineCore's constructor signature.
/// Thread-safety: value type; read only during EngineCore::Init().
struct EngineConfig {
    /// Target frames per second for the frame pipeline. Informational this
    /// increment — nothing throttles to it yet, since there is no frame
    /// limiter — but consumed by future systems comparing FrameStats's
    /// actual FPS against this target.
    double targetFps = 60.0;

    /// Frequency, in Hz, of the fixed-timestep simulation accumulator (see
    /// UVE::Utilities::ITimer::SetFixedTimestep()). Converted to
    /// seconds-per-step by EngineCore during Init().
    double fixedUpdateFps = 60.0;

    /// Maximum delta time, in seconds, a single frame may report (see
    /// UVE::Utilities::ITimer::SetMaxDeltaTime()) — guards against a
    /// spiral of death after a debugger pause or long stall.
    double maxDeltaTimeSeconds = 0.10;

    /// Most fixed steps run in one frame (Timer::SetMaxStepsPerTick): past it, the leftover
    /// time is dropped and the simulation slows down rather than falling ever further behind.
    int maxFixedStepsPerFrame = 4;

    /// Minimum severity a log message must have to reach any sink. Named UVE
    /// profiles provide Trace/Debug/Info/Warning defaults from the build
    /// configuration; integrations outside CMake retain the historical Trace
    /// default.
#if defined(UVE_PROFILE_DEFAULT_LOG_LEVEL)
    Debug::LogLevel minLogLevel =
        static_cast<Debug::LogLevel>(UVE_PROFILE_DEFAULT_LOG_LEVEL);
#else
    Debug::LogLevel minLogLevel = Debug::LogLevel::Trace;
#endif

    /// Path the FileSink attached during Init() will append to.
    std::filesystem::path logFilePath = "uve_engine.log";

    /// Whether a ConsoleSink is attached during Init(), in addition to
    /// the FileSink (which is always attached regardless of this flag).
    bool enableConsoleLogging = true;

    /// Number of ThreadPool worker threads to spawn during Init(). `0`
    /// (the default) means "auto" — see
    /// UVE::Threading::ThreadPool::ThreadPool() for the exact
    /// resolution policy.
    std::size_t threadPoolWorkerCount = 0;

    /// Path ConfigManager::Load() is called with during Init(). A
    /// missing file at this path is not an error (see
    /// IConfigManager::Load()) — a first-run engine has no settings
    /// file yet.
    std::filesystem::path settingsFilePath = ".uvsettings";

    /// Optional path to an active-target override file. When empty, EngineCore loads
    /// `<settingsFilePath parent>/platforms/<target>/<settingsFilePath filename>` if present;
    /// target is android, linux, windows, apple, web or unknown. These values only override
    /// descriptors marked PerPlatform and are kept separate from the user settings file.
    std::filesystem::path platformSettingsFilePath{};

    /// Path of the project's own settings file (see EngineServices::GetProjectSettings()),
    /// read during Init(). Valid project values override this struct's base values; user,
    /// per-platform and command-line layers may override them in turn. A missing file changes
    /// nothing.
    std::filesystem::path projectSettingsFilePath = "project.uvsettings";

    /// Path of the project's input map (see Core::InputMapDocument), read during Init() and
    /// registered with the input system. A missing file is an empty map, not an error.
    std::filesystem::path inputMapFilePath = "project.uvinput";

    /// Raw startup argument tokens (excluding the program path) that
    /// CommandLine parses during Init(). Populated by main() from
    /// argv[1..argc); left empty by default so tests can construct an
    /// EngineConfig without a real process argv. A registered EngineConfig
    /// setting can be overridden as `--<setting.id> <value>`; booleans also
    /// accept a presence-only flag, enums accept their label or integer value, and
    /// vectors/colors use comma-separated numbers.
    std::vector<std::string> commandLineArgs = {};

    /// Path AssetDatabase::Load() is called with during Init(). A
    /// missing file at this path is not an error (see
    /// IAssetDatabase::Load()) — a first-run project has no asset
    /// registry yet.
    std::filesystem::path assetDatabaseFilePath = ".uvassetdb";
    bool cookedAssetPaths = false;
    /// Optional admission limit for counted CPU asset payloads, not a process/GPU memory cap.
    std::size_t assetResidentBudgetBytes = 0;

    /// Root directory the read-only ProjectFileIndex explicitly scans for
    /// editor Asset Browser presentation. Missing or empty roots are valid
    /// empty snapshots and are never created by the index. The root does not
    /// change AssetDatabase ownership, import behavior, or VFS mounts.
    std::filesystem::path projectContentRoot = "assets/";
    /// The project directory, mounted at the root of the virtual file system at the lowest
    /// priority, so a project-relative path such as "scripts/player.uvs" - what a Script
    /// component stores - resolves to the same file the editor wrote. The default is the working
    /// directory, which is exactly where the editor already saves scenes, workspaces and scripts
    /// when nothing else is mounted, so mounting it moves no file. Empty disables the mount.
    std::filesystem::path projectRootDirectory = ".";

    /// Project-local root reserved for derived import metadata only. DerivedArtifactCache
    /// creates this directory lazily on successful cache writes; it never creates source or
    /// destination directories and never changes AssetDatabase ownership.
    std::filesystem::path derivedArtifactCacheRoot = "DerivedData/Import/";

    /// Minimum elapsed engine-update time before ProjectChangeWatcher re-enumerates the
    /// configured project content root. The watcher has no background thread or native OS event
    /// backend in v1; a zero value intentionally requests a scan every engine Update().
    double projectChangeWatchPollIntervalSeconds = 1.0;

    /// Maximum copied project-change entries retained for editor review. When the journal is full,
    /// ProjectChangeWatcher retains newest entries while setting an explicit rescan-required
    /// boundary; stale-cache marking continues for every subsequently observed change.
    std::size_t projectChangeJournalCapacity = 256U;

    /// Whether Update() calls HotReload::Poll() each frame.
    /// AssetManager still tracks/untracks loaded assets with HotReload
    /// regardless of this flag — it only gates whether the poll itself
    /// runs, so flipping it at runtime (by mutating a running
    /// EngineCore's config — not currently exposed, but the field itself
    /// is read fresh every Update()) takes effect immediately.
    bool hotReloadEnabled = true;

    /// Poll interval, in seconds, HotReload waits between checking every
    /// tracked asset's on-disk modification time (see
    /// IHotReload::Poll()).
    double hotReloadPollIntervalSeconds = 1.0;

    /// Fixed dimensions, in pixels, of Renderer3D's offscreen color/depth render target
    /// (see Render::Renderer3D). No WindowManager/swapchain exists yet in this sandbox to
    /// resize against, so these are set once at Init() and cannot change for a running
    /// EngineCore's lifetime.
    std::uint32_t renderTargetWidth = 1280;
    std::uint32_t renderTargetHeight = 720;

    /// Global flat ambient light term Renderer3D adds to every rendered item's final color
    /// (Render::Renderer3D's `ambientColor` constructor parameter), independent of whether any
    /// LightComponent-bearing entity exists this frame (see Render::LightSystem, Increment
    /// 23). No "scene settings" component/system exists in this engine yet, so — exactly like
    /// `gravity` below — an engine-config-level constant is this engine's only existing precedent
    /// for a whole-scene tunable. Low, desaturated default so an unlit scene isn't pitch black.
    Math::Vector3 ambientColor{0.05F, 0.05F, 0.05F};

    /// Width/height, in texels, of the persistent depth-only texture Renderer3D renders its
    /// directional-light shadow depth pre-pass into (see Render::Renderer3D, Increment 26). A
    /// square texture, sized once at construction — like renderTargetWidth/Height above, this
    /// can't change for a running EngineCore's lifetime.
    std::uint32_t shadowMapResolution = 2048;

    /// Half-size, in world units, of the fixed orthographic box Renderer3D's shadow depth
    /// pre-pass projects through, centered on the shadow-casting light entity's world position.
    /// Not fitted to the camera's visible frustum (that's future work, e.g. cascaded shadow
    /// maps) — a fixed box is the simplest choice that needs no new frustum-fitting/AABB math,
    /// matching this engine's "no unnecessary new math" precedent.
    float shadowMapHalfExtent = 20.0F;

    /// Near/far planes of that same orthographic shadow projection, in world units measured from
    /// the shadow-casting light entity along its forward direction.
    float shadowMapNearPlane = 0.1F;
    float shadowMapFarPlane = 100.0F;

    /// Extra extent, in light-view world units, added to each fitted directional shadow-frustum
    /// bound. Prevents edge clipping caused by small camera movement or floating-point rounding;
    /// negative caller values are clamped to zero by Renderer3D.
    float shadowFrustumPadding = 1.0F;

    /// Default authored LightEmitter3D depth bias for components with a negative/inherit bias.
    /// Renderer3D scales this value by 0.025 before sending it to the shadow shader.
    float shadowBiasDefault = 0.1F;
    /// Default angle-aware normal-bias multiplier for LightEmitter3D components that inherit it.
    float shadowNormalBiasDefault = 1.0F;

    /// Default practical-split blend for directional shadows whose per-light split value is unset.
    /// `0` distributes splits uniformly over the camera depth range; `1` uses fully logarithmic
    /// spacing. New DirectionalLight3D components inherit this setting; explicit per-light values
    /// continue to override it. The default preserves the previous 0.6 per-light behavior.
    float shadowCascadeSplitLambda = 0.6F;

    /// Fraction of each non-final cascade range used to cross-fade into the next cascade. The
    /// renderer clamps this to `[0, 0.25]`; zero preserves the Increment 30 hard cascade boundary.
    float shadowCascadeBlendRatio = 0.1F;

    /// Radius, in shadow-map texels, of the square percentage-closer-filtering kernel the
    /// canonical directional-shadow material shader uses. `0` preserves a single hard comparison;
    /// `1` (the default) produces a 3x3 soft-shadow kernel. Renderer3D clamps larger values to
    /// `2` so the per-fragment sampling cost remains bounded at 25 depth comparisons.
    std::uint32_t shadowPcfKernelRadius = 1;

    /// Acceleration PhysicsSystem (see Physics::PhysicsSystem) applies to every
    /// non-kinematic Rigid3DComponent each fixed step, scaled by its own gravityScale.
    /// Earth-like default, Y-up (matching this engine's convention throughout).
    Math::Vector3 gravity{0.0F, -9.81F, 0.0F};

    /// Directory Save::SaveGameSystem reads/writes numbered save slots (and
    /// Save::CheckpointManager's reserved auto-save/checkpoint slot) under. Created lazily on
    /// first write — a first-run engine has no save directory yet, mirroring
    /// settingsFilePath/assetDatabaseFilePath's own "missing is not an error" contract.
    std::filesystem::path saveDirectoryPath = "saves/";

    /// Auto-save interval, in seconds, CheckpointManager::Update() waits between writes to
    /// its reserved auto-save slot (see Save::kAutoSaveSlotIndex). The spec's "every 5
    /// minutes" example; test suites override this to a small value to avoid a real wait.
    double autoSaveIntervalSeconds = 300.0;

    /// When true, EngineCore::Init() constructs Window::NullWindowManager and
    /// Render::NullRenderDevice instead of the real GLFW3/OpenGL backends — no window, no GL
    /// context, safe to run with no display attached (CI, this project's own test suite). The
    /// hidden, NotPersisted `headless` setting maps the `--headless` presence-only CLI flag into
    /// the highest-priority command-line layer during Init(); project/user/platform files cannot
    /// override it.
    bool headless = false;

    /// Application-level policies resolved from the project manifest before engine startup.
    /// `applicationIdentifier` keys the process single-instance lock; the user-data directory
    /// is the resolved per-user or project-portable root. CrashReporter writes platform-native
    /// Windows minidumps and bounded POSIX fatal-signal reports below the configured directory.
    std::string applicationIdentifier;
    std::filesystem::path userDataDirectoryPath;
    bool quitOnLastWindowClosed = true;
    bool enforceSingleInstance = false;
    bool crashHandlerEnabled = true;
    std::filesystem::path crashDumpDirectory = "crash-dumps";
    std::string symbolUploadEndpoint;

    /// Boot-splash presentation policy. The standalone runtime draws the background and optional
    /// imported image through the UI overlay, then restores the project's active camera.
    std::filesystem::path bootSplashImagePath;
    std::array<float, 4U> backgroundColor{0.05F, 0.05F, 0.05F, 1.0F};
    double splashFadeSeconds = 0.25;
    double splashMinimumDisplaySeconds = 1.0;
    bool splashSkippable = true;
    bool skipSplashInEditorPlayMode = true;

    /// Product/project identity used by the window title formatter. `windowTitle` remains the
    /// product-name fallback and legacy caller-provided title.
    std::string windowTitle = "UniVex Engine";
    std::string projectDisplayName;
    /// Supports {productName}, {projectName}, and {sceneName}; literal text is preserved.
    std::string windowTitleFormat = "{productName}";
    std::vector<EngineApplicationIconImage> windowIcons;

    /// Requested window size, in pixels, at creation time (the OS/window manager may still clamp
    /// or override it). Unused when headless is true.
    std::uint32_t windowWidth = 1280;
    std::uint32_t windowHeight = 720;

    /// Whether the real window is user-resizable. Remaining window policy is consumed by the
    /// platform backend at construction; unsupported platform-specific choices are retained in
    /// config but reported/fallback by that backend.
    bool windowResizable = true;
    Platform::WindowMode windowMode = Platform::WindowMode::Windowed;
    bool windowBorderless = false;
    bool windowAlwaysOnTop = false;
    bool windowTransparent = false;
    std::uint32_t windowMinimumWidth = 0U;
    std::uint32_t windowMinimumHeight = 0U;
    std::uint32_t windowMaximumWidth = 0U;
    std::uint32_t windowMaximumHeight = 0U;
    bool windowPositionSpecified = false;
    std::int32_t windowPositionX = 0;
    std::int32_t windowPositionY = 0;
    std::string windowMonitorName;
    bool highDpiAware = true;
    bool perMonitorScaling = true;
    double contentScaleOverride = 0.0;
    Platform::StretchMode stretchMode = Platform::StretchMode::Disabled;
    Platform::AspectPolicy aspectPolicy = Platform::AspectPolicy::Keep;
    bool integerOnlyScaling = false;
    Platform::DisplayOrientation orientation = Platform::DisplayOrientation::Auto;
    std::vector<Platform::DisplayOrientation> allowedOrientations{
        Platform::DisplayOrientation::Landscape, Platform::DisplayOrientation::Portrait};

    /// `vsyncEnabled` remains the source-compatible legacy toggle. Explicit enum selections
    /// set `vsyncModeExplicit`; otherwise EngineCore maps the legacy bool to Off/On.
    bool vsyncEnabled = true;
    Platform::VSyncMode vsyncMode = Platform::VSyncMode::On;
    bool vsyncModeExplicit = false;
    std::uint32_t focusedFrameRateCap = 0U;
    std::uint32_t unfocusedFrameRateCap = 0U;
    bool allowDisplaySleep = true;
    std::filesystem::path cursorImagePath;
    std::uint32_t cursorHotspotX = 0U;
    std::uint32_t cursorHotspotY = 0U;
    std::vector<std::uint8_t> cursorRgba8;
    std::uint32_t cursorImageWidth = 0U;
    std::uint32_t cursorImageHeight = 0U;
    bool cursorVisible = true;
    bool cursorConfinedToWindow = false;
    bool appendSceneNameInEditorPlayMode = false;
    bool editorPlayMode = false;

    /// Requested OpenGL context version, forwarded to Window::WindowDesc::glVersionMajor/Minor.
    /// The production default is OpenGL 4.6 Core, per the approved architecture decision.
    /// Configurable (not hardcoded) because GL context availability is a real driver/platform
    /// fact — this project's own development sandbox (Mesa llvmpipe under Xvfb) caps at 4.5 Core
    /// and fails to create a 4.6 context; overriding these two fields is how a caller targets
    /// that sandbox specifically without changing what real hardware/CI requests by default.
    std::uint32_t windowGlVersionMajor = 4;
    std::uint32_t windowGlVersionMinor = 6;

    /// Runtime shader compilation is on-demand. The async default prepares source on the worker
    /// pool, then compiles/links through the RHI on the render thread as completed jobs are drained;
    /// the synchronous mode completes the work in each Create* call and can stall that caller.
    /// Ahead-of-time compilation and placeholder programs are not supported yet.
    ShaderCompilationMode shaderCompilationMode = ShaderCompilationMode::AsynchronousOnDemand;

    /// Request VK_LAYER_KHRONOS_validation when the Vulkan backend is selected. The layer is
    /// enabled only when installed; otherwise Vulkan logs a warning and continues without it.
#if UVE_DEBUG
    bool vulkanValidationLayersEnabled = true;
#else
    bool vulkanValidationLayersEnabled = false;
#endif

    /// Directory relative to the process working directory where Shader::ShaderManager persists
    /// compiled OpenGL program binaries (keyed by a hash of each program's fully resolved —
    /// post-#include, post-macro — source). A per-platform subdirectory is appended automatically;
    /// the cache directory is created lazily on first write. EngineCore rejects absolute/rooted paths
    /// and any `..` component; this lexical policy does not treat symlinks inside the working
    /// directory as a sandbox boundary.
    std::filesystem::path shaderCachePath = "shader_cache/";

    /// Whether ShaderManager::Update() polls loaded shader programs' source file (and
    /// #include closure) modification times each frame and hot-swaps a recompiled program when
    /// one changed. Debug builds default to on (iteration speed); Release defaults to off (no
    /// reason a shipped build ever re-reads shader source off disk).
#if UVE_DEBUG
    bool shaderHotReloadEnabled = true;
#else
    bool shaderHotReloadEnabled = false;
#endif

    /// Poll interval, in seconds, ShaderManager waits between checking every hot-reload-tracked
    /// program's dependency closure for an on-disk modification time change — the shader-specific
    /// analogue of hotReloadPollIntervalSeconds (kept as its own field since the two subsystems
    /// are otherwise unrelated and may want independent tuning later).
    double shaderHotReloadPollIntervalSeconds = 1.0;

    /// Virtual-filesystem mount prefix ShaderManager's #include resolver and built-in shader
    /// loader resolve paths against (e.g. "shaders/basic_3d.glsl"), and the real directory it's
    /// mounted from during Init(). A shader file missing at this location is not an error — every
    /// built-in also carries an embedded C++ string fallback (see
    /// Render::Shader::BuiltIn::kBasic3DSource) used automatically when the mount doesn't
    /// resolve, so the engine still runs (without hot-reload) if launched from a working
    /// directory where the source tree isn't reachable.
    std::string shaderSourceMountPrefix = "shaders";
    std::filesystem::path shaderSourceRealDirectory = "Engine/Runtime/RHI/Shader/built_in/";

    /// Optional borrowed native window handle for platform backends. Android uses this as an
    /// ANativeWindow* supplied by NativeActivity; the engine never takes ownership or destroys it.
    /// Desktop callers leave it null and continue through the GLFW WindowManager path.
    void* nativeWindowHandle = nullptr;

    /// Preferred render backend when a real window is created (ignored when headless is
    /// true — headless always uses NullRenderDevice). `Auto` resolves to `OpenGL`,
    /// the production default; `Vulkan` opts into the milestone-1 Vulkan bootstrap device
    /// (window surface + clear-color present; see Render::VulkanRenderDevice) with the
    /// documented Vulkan -> OpenGL -> Null fallback chain when the host lacks it. Appended after
    /// the pre-existing config fields so their aggregate-construction order remains unchanged.
    RenderBackendPreference renderBackendPreference = RenderBackendPreference::Auto;

    /// Maximum fraction of the display/viewport dimensions used for the 3D scene render target.
    /// With dynamic resolution disabled this is the fixed scale; when enabled it is the starting
    /// and upper scale. The final image is still presented at the normal output size.
    double renderResolutionScale = kMaximumRenderResolutionScale;
    bool dynamicRenderResolutionEnabled = false;
    double minimumRenderResolutionScale = kMinimumRenderResolutionScale;
    double renderResolutionTargetFrameTimeMilliseconds =
        kDefaultRenderResolutionTargetFrameTimeMilliseconds;

    /// Spatial resampling used to fit the scene render target to the presentation size.
    SpatialUpscalingMethod spatialUpscalingMethod = SpatialUpscalingMethod::Bilinear;
    /// Operator used to map the HDR scene color into display range in the final full-screen pass.
    ToneMappingMethod toneMappingMethod = ToneMappingMethod::SoftFilmic;
    /// Unsharp-mask strength applied to the final scene reconstruction before exposure/tone mapping; 0 disables it.
    float sharpeningAmount = 0.0F;
    /// Add sub-LSB screen-space noise before output quantization to reduce visible banding.
    bool ditheringEnabled = true;

    /// Fast approximate AA is applied in the Renderer3D tone-mapping shader when selected.
    /// Temporal AA is not available until the engine supports frame history and camera jitter.
    PostProcessAAMethod postProcessAAMethod = PostProcessAAMethod::None;
    /// Edge-direction taps used only by fast approximate AA: 0 = low (3), 1 = medium (5),
    /// 2 = high (7), in addition to the filter's fixed four-neighbour contrast probes.
    std::uint32_t screenSpaceAAQuality = 1U;

    /// Screen-space ambient occlusion controls. These are global renderer defaults; an active
    /// WorldEnvironment retains its existing enable/radius/intensity values as local overrides.
    bool ssaoEnabled = true;
    float ssaoRadius = 0.5F;
    float ssaoIntensity = 1.0F;
    float ssaoPower = 1.0F;
    /// 0 = Low (4 samples), 1 = Medium (8), 2 = High (12); High preserves the existing kernel cost.
    std::uint32_t ssaoQuality = 2U;
    /// Apply the depth-aware separable bilateral AO blur to smooth half-resolution kernel noise.
    bool ssaoBlurEnabled = true;

    /// Frustum culling for the renderer's main colour view. DebugFreeze submits everything the
    /// scene placed, wherever the camera looks; Enabled preserves the shipped culling behaviour.
    FrustumCullingMode frustumCullingMode = FrustumCullingMode::Enabled;

    /// GPU occlusion culling for the main colour view. An object the previous frame proved fully
    /// hidden behind geometry already drawn is skipped. Conservative by construction: a skip needs
    /// a query that returned zero samples AND an unchanged view projection since that query was
    /// issued, so a camera cut never trusts a stale verdict. Off by default because it costs a
    /// query and a proxy draw per candidate; on for a scene dense enough to pay for them.
    bool gpuOcclusionCullingEnabled = false;

    /// Extended Part 1 Project Settings (Physics 3D, Input, Audio, Animation, Navigation,
    /// Networking, Localisation, Quality Tiers, and Packaging).
    double defaultLinearDamping = 0.01;
    double defaultAngularDamping = 0.05;
    double sleepThreshold = 0.05;
    double sleepTimeSeconds = 0.5;
    bool allowSleepingDefault = true;
    std::int64_t physicsPositionIterations = 6;
    std::int64_t physicsVelocityIterations = 8;
    double physicsContactOffset = 0.01;
    double physicsRestOffset = 0.0;
    double physicsPenetrationSlop = 0.005;
    double physicsMaxDepenetrationVelocity = 10.0;
    std::int64_t physicsCcdDefaultMode = 0;
    std::int64_t physicsBroadphaseAlgorithm = 0;
    double physicsBroadphaseHalfExtent = 1000.0;
    double defaultPhysicsFriction = 0.5;
    double defaultPhysicsRestitution = 0.0;
    std::uint32_t defaultCollisionLayer = 1U;
    std::uint32_t defaultCollisionMask = 0xFFFFFFFFU;
    bool triggerCallbacksEnabled = true;
    bool triggersReportOnStaticBodies = false;
    bool raycastHitBackfaces = false;
    bool raycastHitTriggers = false;
    double raycastDefaultMaxDistance = 1000.0;
    bool physicsInterpolationDefault = true;
    bool physicsDebugDrawColliders = false;
    bool physicsDeterministicMode = true;
    std::int64_t physicsFixedRandomSeed = 1337;

    double inputDefaultDeadzone = 0.2;
    double inputDefaultSensitivity = 1.0;
    bool inputAnalogByDefault = true;
    std::uint32_t inputEnabledDeviceMask = 0x1FU;
    std::string inputGamepadDatabasePath;
    std::int64_t inputGamepadDeadzoneShape = 1;
    double inputGamepadDeadzoneInner = 0.15;
    double inputGamepadDeadzoneOuter = 0.95;
    bool inputHapticsEnabled = true;
    double inputHapticsStrength = 1.0;
    bool inputMouseRaw = false;
    bool inputMouseAcceleration = false;
    double inputMouseSensitivity = 1.0;
    bool inputMouseInvertX = false;
    bool inputMouseInvertY = false;
    bool inputEmulateMouseFromTouch = true;
    bool inputEmulateTouchFromMouse = false;
    std::int64_t inputMaxTouchPoints = 10;
    bool inputImeEnabled = true;
    bool inputVirtualKeyboardAutoShow = true;
    bool inputAccumulatedPolling = true;
    std::string inputDefaultActionSet = "gameplay";
    std::string inputUserRebindsPath = "user_input.uvinput";

    std::int64_t audioDriverPreference = 0;
    std::int64_t audioSampleRate = 48000;
    std::int64_t audioBufferSizeFrames = 512;
    std::int64_t audioChannelLayout = 1;
    double audioMasterVolume = 1.0;
    double audioMusicVolume = 1.0;
    double audioSfxVolume = 1.0;
    double audioVoiceVolume = 1.0;
    double audioDopplerFactor = 1.0;
    double audioSpeedOfSound = 343.0;
    std::int64_t audioAttenuationModel = 0;
    double audioDefaultMinDistance = 1.0;
    double audioDefaultMaxDistance = 50.0;
    std::int64_t audioMaxVoices = 64;
    std::int64_t audioVoiceStealingPolicy = 0;
    std::int64_t audioStreamingThresholdKb = 512;
    bool audioMuteOnFocusLoss = false;

    double animationDefaultBlendTime = 0.2;
    std::int64_t animationDefaultBlendMode = 0;
    double animationUpdateRateHz = 60.0;
    bool animationFixedTimestep = true;
    bool animationRootMotionDefault = false;
    Math::Vector3 animationLodDistanceTiers{15.0F, 35.0F, 70.0F};
    double animationCompressionTolerance = 0.001;
    bool animationKeyframeReduction = true;
    std::int64_t animationSkinningMethod = 1;
    std::int64_t animationMaxBoneInfluences = 4;
    double animationStateMachineDefaultTransition = 0.15;
    std::int64_t animationIkDefaultIterations = 10;

    double navCellSize = 0.3;
    double navCellHeight = 0.2;
    double navAgentRadius = 0.5;
    double navAgentHeight = 2.0;
    double navAgentMaxSlopeDegrees = 45.0;
    double navAgentMaxClimb = 0.4;
    double navRegionMinSize = 8.0;
    double navRegionMergeSize = 20.0;
    double navEdgeMaxLength = 12.0;
    double navEdgeMaxError = 1.3;
    double navDetailSampleDistance = 6.0;
    double navDetailMaxError = 1.0;
    bool navStringPulling = true;
    bool navPathSmoothing = false;
    bool navCorridorOptimization = true;
    bool navAvoidanceEnabled = true;
    double navNeighborDistance = 5.0;
    double navTimeHorizon = 1.5;
    std::int64_t navMaxNeighbors = 8;
    bool navAsyncBaking = true;
    double navBakeBudgetMs = 2.0;

    std::int64_t networkDefaultPort = 7777;
    std::string networkBindAddress = "0.0.0.0";
    std::int64_t networkMaxConnections = 64;
    std::int64_t networkMaxPacketSizeBytes = 1200;
    std::int64_t networkChannelCount = 4;
    double networkTickRateHz = 60.0;
    double networkSendRateHz = 30.0;
    double networkInterpolationBufferMs = 100.0;
    bool networkCompressionEnabled = true;
    std::int64_t networkCompressionThresholdBytes = 128;
    bool networkEncryptionEnabled = false;
    std::string networkCertificatePath;
    double networkTimeoutSeconds = 10.0;
    double networkKeepaliveIntervalSeconds = 1.0;
    double networkRelevancyDistanceMetres = 150.0;
    double networkDefaultPriority = 1.0;
    double networkLagCompensationWindowMs = 250.0;
    double networkSimLatencyMs = 0.0;
    double networkSimJitterMs = 0.0;
    double networkSimPacketLossPercent = 0.0;
    double networkSimDuplicatePercent = 0.0;

    std::string localisationDefaultLocale = "en";
    std::string localisationFallbackLocale = "en";
    std::vector<std::string> localisationSupportedLocales{"en"};
    std::string localisationTranslationsDir = "Content/Locales";
    bool pseudolocalisationEnabled = false;
    double pseudolocalisationExpansion = 0.3;
    bool rightToLeftLayout = false;
    std::string localisationDefaultFontAsset;
    std::int64_t localisationFormattingMode = 0;
    bool warnOnUntranslatedStrings = true;

    bool caseSensitiveAssetPaths = true;
    std::int64_t maxPathLengthWarning = 240;
    std::int64_t packMountPriority = 100;
    std::int64_t activeQualityTier = 2; // 0=Low, 1=Medium, 2=High, 3=Ultra
    Platform::RendererProfile requestedRendererProfile = Platform::DefaultRendererProfileForHost();
    bool autoDetectQualityTier = false;
    bool allowRuntimeQualitySwitching = true;
    std::vector<std::string> platformFeatureTags{"desktop", "gpu_compute"};

    std::string packagingDefaultPreset = "Shipping";
    std::string packagingTargetArch = "x86_64";
    std::int64_t packagingBuildConfig = 2; // 0=Debug, 1=Development, 2=Shipping
    std::vector<std::string> packagingIncludeFilters{"*"};
    std::vector<std::string> packagingExcludeFilters{"*.tmp", "*.bak"};
    std::int64_t packagingCompressionMode = 1;
    std::int64_t packagingDebugSymbolsPolicy = 1;
    std::string packagingCodeSigningIdentity;
    bool packagingEncryptAssets = false;
    std::int64_t packagingPackMode = 0;
    std::string packagingPostBuildCommand;
    bool packagingAutoIncrementBuild = false;

    /// Extended Part 1.3 Rendering & Part 6.5/6.8/6.9 Project Settings.
    std::int64_t renderingMethod = 0; // 0=Forward, 1=ForwardPlus, 2=Deferred, 3=Mobile
    std::int64_t colorSpaceMode = 0; // 0=LinearSrgb, 1=GammaSrgb
    bool hdrRenderingEnabled = false;
    std::int64_t hdrTransferFunction = 0; // 0=PQ_ST2084, 1=HLG, 2=scRGB
    double hdrPeakLuminanceNits = 1000.0;
    double hdrPaperWhiteNits = 200.0;
    std::int64_t renderTargetFormat = 2; // 0=RGBA8, 1=RGB10A2, 2=RGBA16F
    bool multiThreadedRendering = true;
    std::int64_t commandBufferThreadCount = 4;

    std::int64_t msaa3dSamples = 1;
    std::int64_t taaSampleCount = 8;
    std::int64_t taaJitterPattern = 0; // 0=Halton23, 1=BlueNoise, 2=Uniform2x
    double taaSharpeningAmount = 0.25;
    bool alphaToCoverageEnabled = false;

    std::int64_t shadowAtlasSize = 4096;
    std::int64_t shadowAtlasSubdivision = 4;
    std::int64_t directionalShadowSplitCount = 3;
    double shadowMaxDistance = 150.0;
    std::int64_t shadowDepthFormatBits = 32;
    std::int64_t pointSpotShadowResolution = 1024;
    bool contactShadowsEnabled = false;
    double contactShadowsLength = 0.15;
    std::int64_t contactShadowsStepCount = 16;
    /// How far behind the ray a depth sample may sit and still count as touching geometry, in metres.
    double contactShadowsThickness = 0.1;
    /// Fraction of the light a fully occluded ray removes, 0..1. Below 1.0 on purpose: contact
    /// shadow refines a real shadow, and at full strength the seam reads as a hole.
    double contactShadowsStrength = 0.8;

    bool ssilEnabled = false;
    std::int64_t ssilQuality = 1;
    std::int64_t ssilFrameCount = 4;
    bool ssrEnabled = false;
    std::int64_t ssrMaxSteps = 32;
    double ssrFadeIn = 0.1;
    double ssrFadeOut = 0.8;
    double ssrDepthTolerance = 0.2;
    std::int64_t reflectionProbeAtlasSize = 2048;
    std::int64_t reflectionProbeDefaultResolution = 128;
    std::int64_t voxelGiQuality = 1;
    std::int64_t voxelGiBounceCount = 2;
    double voxelGiUpdateBudgetMs = 2.0;
    double lightmapResolutionScale = 1.0;
    std::int64_t lightmapBounceCount = 3;
    bool lightmapDenoiserEnabled = true;
    bool lightmapDirectionalEnabled = true;
    double lightmapTexelDensity = 16.0;
    double lightProbeDefaultDensity = 2.0;

    bool autoExposureEnabled = false;
    double autoExposureMinLuminance = -4.0;
    double autoExposureMaxLuminance = 8.0;
    double autoExposureSpeedUp = 3.0;
    double autoExposureSpeedDown = 1.0;
    std::string colorGradingLutAsset;
    std::int64_t colorGradingLutSize = 32;
    bool colorGradingPreTonemap = true;
    bool volumetricFogEnabled = false;
    std::int64_t volumetricFogFroxelResolution = 128;
    double volumetricFogDepthDistribution = 2.0;
    bool volumetricFogTemporalReprojection = true;
    double volumetricFogAnisotropy = 0.2;

    double defaultNearClipPlane = 0.1;
    double defaultFarClipPlane = 1000.0;
    bool occlusionCullingEnabled = true;
    std::int64_t occlusionCullingMethod = 0; // 0=RasterSoftware, 1=HiZHardware
    std::int64_t occlusionBufferResolution = 256;
    double globalLodBias = 0.0;
    double minLodScreenCoverage = 0.02;
    std::int64_t lodCrossfadeMode = 1; // 0=None, 1=Dither, 2=AlphaBlend
    bool meshLodAutoGenerateDefault = false;
    std::int64_t instancingMinCount = 4;
    std::int64_t perInstanceDataBudgetKb = 4096;
    bool staticBatchingEnabled = true;
    std::int64_t staticBatchVertexBudget = 65536;
    bool indirectDrawEnabled = true;
    std::int64_t maxDrawCallsWarning = 3000;
    std::int64_t maxTrianglesWarning = 2500000;

    std::int64_t defaultTextureFilter = 2; // 0=Nearest, 1=Bilinear, 2=Trilinear
    std::int64_t defaultAnisotropyLevel = 8;
    double globalMipLodBias = 0.0;
    bool textureStreamingEnabled = true;
    std::int64_t textureStreamingBudgetMb = 512;
    double textureStreamingPressureMipBias = 1.0;
    std::int64_t maxTextureSizePerTier = 4096;
    std::int64_t defaultTextureRepeatMode = 1; // 0=Disabled, 1=Enabled, 2=Mirrored

    std::int64_t defaultSkyMode = 3; // 0=None, 1=Colour, 2=Gradient, 3=Physical, 4=Panorama
    std::int64_t defaultSkyResolution = 512;
    std::int64_t defaultSkyUpdateMode = 2; // 0=Realtime, 1=Once, 2=OnDemand
    std::string defaultWorldEnvironmentAsset;

    std::string defaultPhysicsMaterialAsset;
    double runtimeUiScale = 1.0;
    bool runtimeSubtitlesEnabled = true;
    double runtimeCaptionFontScale = 1.0;
    bool runtimeHighContrastUi = false;
    bool runtimeReduceMotion = false;
    bool telemetryAnalyticsOptIn = false;
    std::int64_t crashReportSubmissionPolicy = 0; // 0=AskEachTime, 1=Always, 2=Never
    bool anonymousUsageStatsOptIn = false;
    /// Opt-in lightweight UI-host mode (used by UniVex Hub & Setup): installs a zero-cost no-op
    /// IRenderer3D instead of compiling 3D shaders and allocating shadow/post-process targets.
    bool skip3dRenderer = false;
    /// Opt-in silent UI-host mode (used by UniVex Hub & Setup): uses NullAudioDevice directly
    /// instead of opening an OS audio backend stream (miniaudio/AAudio).
    bool skipAudio = false;
};

} // namespace UVE::Core
