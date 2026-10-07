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

#include "uve/logging/log_level_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/platform/platform_uve.h"
#include "uve/platform/application_window_settings_uve.h"

namespace UVE::Core {

/// Render backend EngineCoreUVE::Init() should try first when a window is created. The engine
/// always degrades gracefully through this fixed fallback chain — the requested backend is a
/// starting point, never a hard requirement:
///   Vulkan -> OpenGL -> NullRenderDeviceUVE (engine still runs headless-correct, renders nothing)
/// so picking a backend that the host lacks (no ICD, no GLFW Vulkan support, no display) is a
/// logged warning, never a fail-fast abort. `AutoUVE` = `OpenGLUVE` today: OpenGL remains the
/// production-default backend and Auto exists so future milestones can flip the default without
/// changing the meaning of persisted configurations.
enum class RenderBackendPreferenceUVE : std::uint32_t {
    AutoUVE = 0U,
    OpenGLUVE,
    VulkanUVE,
    NullUVE, ///< Force the null swapchain device (rendering disabled) even with a window.
};

/// Runtime on-demand shader work scheduling. Ahead-of-time compilation and placeholder programs
/// are not currently provided; asynchronous mode preprocesses on workers, then compiles/links on
/// the render thread when ShaderManagerUVE::UpdateUVE() drains completed jobs.
enum class ShaderCompilationModeUVE : std::uint32_t {
    SynchronousOnDemandUVE = 0U,
    AsynchronousOnDemandUVE,
};

/// Post-process anti-aliasing methods the runtime can execute today. Temporal AA is omitted until
/// the renderer has frame-history, motion vectors, and camera-jitter support.
enum class PostProcessAAMethodUVE : std::uint32_t {
    NoneUVE = 0U,
    FastApproximateUVE,
};

/// Frustum-culling mode for the renderer's main colour view. DebugFreezeUVE stops rejecting geometry
/// by frustum so every placed mesh, primitive, surface instance and decal is submitted for the frame
/// - the "is it off-screen or is it missing?" diagnostic. Author visibility flags, world partitioning,
/// visibility regions, LOD/SurfaceInstance draw distances, render layers, Occluder3D occlusion and the
/// shadow cascades' own light-frustum culls all keep running; see Render::CullingSettingsUVE.
enum class FrustumCullingModeUVE : std::uint32_t {
    EnabledUVE = 0U,
    DebugFreezeUVE,
};

/// Spatial reconstruction used when the scene render target is smaller than the presentation target.
/// Temporal upscaling remains unavailable without frame history and motion vectors.
enum class SpatialUpscalingMethodUVE : std::uint32_t {
    BilinearUVE = 0U,
    NearestUVE,
};

/// Tone-mapping operators the built-in final pass can execute. LinearClamp is a direct clip into
/// display range; ACES provides the existing filmic roll-off and remains the default.
enum class ToneMappingMethodUVE : std::uint32_t {
    LinearClampUVE = 0U,
    AcesUVE,
};

/// Bounds shared by the persisted resolution-scale settings and EngineCoreUVE's startup validation.
inline constexpr double kMinimumRenderResolutionScaleUVE = 0.5;
inline constexpr double kMaximumRenderResolutionScaleUVE = 1.0;
inline constexpr double kMinimumRenderResolutionTargetFrameTimeMillisecondsUVE = 8.0;
inline constexpr double kMaximumRenderResolutionTargetFrameTimeMillisecondsUVE = 100.0;
inline constexpr double kDefaultRenderResolutionTargetFrameTimeMillisecondsUVE = 16.6667;

/// Decoded icon pixels staged by the application bootstrap and copied into the native window
/// descriptor when a desktop window is created.
struct EngineApplicationIconImageUVE final {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<std::uint8_t> rgba8;

    [[nodiscard]] bool operator==(const EngineApplicationIconImageUVE&) const = default;
};

/// Configuration passed into EngineCoreUVE's constructor. Every field has a
/// sensible default, so most callers (including tests) can construct one
/// with zero overrides. New fields can be added here in the future without
/// changing EngineCoreUVE's constructor signature.
/// Thread-safety: value type; read only during EngineCoreUVE::Init().
struct EngineConfigUVE {
    /// Target frames per second for the frame pipeline. Informational this
    /// increment — nothing throttles to it yet, since there is no frame
    /// limiter — but consumed by future systems comparing FrameStatsUVE's
    /// actual FPS against this target.
    double targetFps = 60.0;

    /// Frequency, in Hz, of the fixed-timestep simulation accumulator (see
    /// UVE::Utilities::ITimerUVE::SetFixedTimestepUVE()). Converted to
    /// seconds-per-step by EngineCoreUVE during Init().
    double fixedUpdateFps = 60.0;

    /// Maximum delta time, in seconds, a single frame may report (see
    /// UVE::Utilities::ITimerUVE::SetMaxDeltaTimeUVE()) — guards against a
    /// spiral of death after a debugger pause or long stall.
    double maxDeltaTimeSeconds = 0.25;

    /// Most fixed steps run in one frame (TimerUVE::SetMaxStepsPerTickUVE): past it, the leftover
    /// time is dropped and the simulation slows down rather than falling ever further behind.
    int maxFixedStepsPerFrame = 8;

    /// Minimum severity a log message must have to reach any sink. Named UVE
    /// profiles provide Trace/Debug/Info/Warning defaults from the build
    /// configuration; integrations outside CMake retain the historical Trace
    /// default.
#if defined(UVE_PROFILE_DEFAULT_LOG_LEVEL)
    Debug::LogLevelUVE minLogLevel =
        static_cast<Debug::LogLevelUVE>(UVE_PROFILE_DEFAULT_LOG_LEVEL);
#else
    Debug::LogLevelUVE minLogLevel = Debug::LogLevelUVE::Trace;
#endif

    /// Path the FileSinkUVE attached during Init() will append to.
    std::filesystem::path logFilePath = "uve_engine.log";

    /// Whether a ConsoleSinkUVE is attached during Init(), in addition to
    /// the FileSinkUVE (which is always attached regardless of this flag).
    bool enableConsoleLogging = true;

    /// Number of ThreadPoolUVE worker threads to spawn during Init(). `0`
    /// (the default) means "auto" — see
    /// UVE::Threading::ThreadPoolUVE::ThreadPoolUVE() for the exact
    /// resolution policy.
    std::size_t threadPoolWorkerCount = 0;

    /// Path ConfigManagerUVE::LoadUVE() is called with during Init(). A
    /// missing file at this path is not an error (see
    /// IConfigManagerUVE::LoadUVE()) — a first-run engine has no settings
    /// file yet.
    std::filesystem::path settingsFilePath = ".uvsettings";

    /// Optional path to an active-target override file. When empty, EngineCoreUVE loads
    /// `<settingsFilePath parent>/platforms/<target>/<settingsFilePath filename>` if present;
    /// target is android, linux, windows, apple, web or unknown. These values only override
    /// descriptors marked PerPlatform and are kept separate from the user settings file.
    std::filesystem::path platformSettingsFilePath{};

    /// Path of the project's own settings file (see EngineServicesUVE::GetProjectSettingsUVE()),
    /// read during Init(). Valid project values override this struct's base values; user,
    /// per-platform and command-line layers may override them in turn. A missing file changes
    /// nothing.
    std::filesystem::path projectSettingsFilePath = "project.uvsettings";

    /// Path of the project's input map (see Core::InputMapDocumentUVE), read during Init() and
    /// registered with the input system. A missing file is an empty map, not an error.
    std::filesystem::path inputMapFilePath = "project.uvinput";

    /// Raw startup argument tokens (excluding the program path) that
    /// CommandLineUVE parses during Init(). Populated by main() from
    /// argv[1..argc); left empty by default so tests can construct an
    /// EngineConfigUVE without a real process argv. A registered EngineConfig
    /// setting can be overridden as `--<setting.id> <value>`; booleans also
    /// accept a presence-only flag, enums accept their label or integer value, and
    /// vectors/colors use comma-separated numbers.
    std::vector<std::string> commandLineArgs = {};

    /// Path AssetDatabaseUVE::LoadUVE() is called with during Init(). A
    /// missing file at this path is not an error (see
    /// IAssetDatabaseUVE::LoadUVE()) — a first-run project has no asset
    /// registry yet.
    std::filesystem::path assetDatabaseFilePath = ".uvassetdb";

    /// Root directory the read-only ProjectFileIndexUVE explicitly scans for
    /// editor Asset Browser presentation. Missing or empty roots are valid
    /// empty snapshots and are never created by the index. The root does not
    /// change AssetDatabaseUVE ownership, import behavior, or VFS mounts.
    std::filesystem::path projectContentRootUVE = "assets/";
    /// The project directory, mounted at the root of the virtual file system at the lowest
    /// priority, so a project-relative path such as "scripts/player.uvs" - what a Script
    /// component stores - resolves to the same file the editor wrote. The default is the working
    /// directory, which is exactly where the editor already saves scenes, workspaces and scripts
    /// when nothing else is mounted, so mounting it moves no file. Empty disables the mount.
    std::filesystem::path projectRootDirectoryUVE = ".";

    /// Project-local root reserved for derived import metadata only. DerivedArtifactCacheUVE
    /// creates this directory lazily on successful cache writes; it never creates source or
    /// destination directories and never changes AssetDatabaseUVE ownership.
    std::filesystem::path derivedArtifactCacheRootUVE = "DerivedData/Import/";

    /// Minimum elapsed engine-update time before ProjectChangeWatcherUVE re-enumerates the
    /// configured project content root. The watcher has no background thread or native OS event
    /// backend in v1; a zero value intentionally requests a scan every engine Update().
    double projectChangeWatchPollIntervalSecondsUVE = 1.0;

    /// Maximum copied project-change entries retained for editor review. When the journal is full,
    /// ProjectChangeWatcherUVE retains newest entries while setting an explicit rescan-required
    /// boundary; stale-cache marking continues for every subsequently observed change.
    std::size_t projectChangeJournalCapacityUVE = 256U;

    /// Whether Update() calls HotReloadUVE::PollUVE() each frame.
    /// AssetManagerUVE still tracks/untracks loaded assets with HotReloadUVE
    /// regardless of this flag — it only gates whether the poll itself
    /// runs, so flipping it at runtime (by mutating a running
    /// EngineCoreUVE's config — not currently exposed, but the field itself
    /// is read fresh every Update()) takes effect immediately.
    bool hotReloadEnabledUVE = true;

    /// Poll interval, in seconds, HotReloadUVE waits between checking every
    /// tracked asset's on-disk modification time (see
    /// IHotReloadUVE::PollUVE()).
    double hotReloadPollIntervalSecondsUVE = 1.0;

    /// Fixed dimensions, in pixels, of Renderer3DUVE's offscreen color/depth render target
    /// (see Render::Renderer3DUVE). No WindowManagerUVE/swapchain exists yet in this sandbox to
    /// resize against, so these are set once at Init() and cannot change for a running
    /// EngineCoreUVE's lifetime.
    std::uint32_t renderTargetWidth = 1280;
    std::uint32_t renderTargetHeight = 720;

    /// Global flat ambient light term Renderer3DUVE adds to every rendered item's final color
    /// (Render::Renderer3DUVE's `ambientColor` constructor parameter), independent of whether any
    /// LightComponentUVE-bearing entity exists this frame (see Render::LightSystemUVE, Increment
    /// 23). No "scene settings" component/system exists in this engine yet, so — exactly like
    /// `gravity` below — an engine-config-level constant is this engine's only existing precedent
    /// for a whole-scene tunable. Low, desaturated default so an unlit scene isn't pitch black.
    Math::Vector3UVE ambientColor{0.05F, 0.05F, 0.05F};

    /// Width/height, in texels, of the persistent depth-only texture Renderer3DUVE renders its
    /// directional-light shadow depth pre-pass into (see Render::Renderer3DUVE, Increment 26). A
    /// square texture, sized once at construction — like renderTargetWidth/Height above, this
    /// can't change for a running EngineCoreUVE's lifetime.
    std::uint32_t shadowMapResolution = 2048;

    /// Half-size, in world units, of the fixed orthographic box Renderer3DUVE's shadow depth
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
    /// negative caller values are clamped to zero by Renderer3DUVE.
    float shadowFrustumPadding = 1.0F;

    /// Default authored LightEmitter3D depth bias for components with a negative/inherit bias.
    /// Renderer3DUVE scales this value by 0.025 before sending it to the shadow shader.
    float shadowBiasDefaultUVE = 0.1F;
    /// Default angle-aware normal-bias multiplier for LightEmitter3D components that inherit it.
    float shadowNormalBiasDefaultUVE = 1.0F;

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
    /// `1` (the default) produces a 3x3 soft-shadow kernel. Renderer3DUVE clamps larger values to
    /// `2` so the per-fragment sampling cost remains bounded at 25 depth comparisons.
    std::uint32_t shadowPcfKernelRadius = 1;

    /// Acceleration PhysicsSystemUVE (see Physics::PhysicsSystemUVE) applies to every
    /// non-kinematic Rigid3DComponentUVE each fixed step, scaled by its own gravityScale.
    /// Earth-like default, Y-up (matching this engine's convention throughout).
    Math::Vector3UVE gravity{0.0F, -9.81F, 0.0F};

    /// Directory Save::SaveGameSystemUVE reads/writes numbered save slots (and
    /// Save::CheckpointManagerUVE's reserved auto-save/checkpoint slot) under. Created lazily on
    /// first write — a first-run engine has no save directory yet, mirroring
    /// settingsFilePath/assetDatabaseFilePath's own "missing is not an error" contract.
    std::filesystem::path saveDirectoryPath = "saves/";

    /// Auto-save interval, in seconds, CheckpointManagerUVE::UpdateUVE() waits between writes to
    /// its reserved auto-save slot (see Save::kAutoSaveSlotIndexUVE). The spec's "every 5
    /// minutes" example; test suites override this to a small value to avoid a real wait.
    double autoSaveIntervalSecondsUVE = 300.0;

    /// When true, EngineCoreUVE::Init() constructs Window::NullWindowManagerUVE and
    /// Render::NullRenderDeviceUVE instead of the real GLFW3/OpenGL backends — no window, no GL
    /// context, safe to run with no display attached (CI, this project's own test suite). The
    /// hidden, NotPersisted `headless` setting maps the `--headless` presence-only CLI flag into
    /// the highest-priority command-line layer during Init(); project/user/platform files cannot
    /// override it.
    bool headlessUVE = false;

    /// Application-level policies resolved from the project manifest before engine startup.
    /// `applicationIdentifierUVE` keys the process single-instance lock; the user-data directory
    /// is the resolved per-user or project-portable root. CrashReporterUVE writes platform-native
    /// Windows minidumps and bounded POSIX fatal-signal reports below the configured directory.
    std::string applicationIdentifierUVE;
    std::filesystem::path userDataDirectoryPathUVE;
    bool quitOnLastWindowClosedUVE = true;
    bool enforceSingleInstanceUVE = false;
    bool crashHandlerEnabledUVE = true;
    std::filesystem::path crashDumpDirectoryUVE = "crash-dumps";
    std::string symbolUploadEndpointUVE;

    /// Boot-splash presentation policy. The standalone runtime draws the background and optional
    /// imported image through the UI overlay, then restores the project's active camera.
    std::filesystem::path bootSplashImagePathUVE;
    std::array<float, 4U> backgroundColorUVE{0.05F, 0.05F, 0.05F, 1.0F};
    double splashFadeSecondsUVE = 0.25;
    double splashMinimumDisplaySecondsUVE = 1.0;
    bool splashSkippableUVE = true;
    bool skipSplashInEditorPlayModeUVE = true;

    /// Product/project identity used by the window title formatter. `windowTitle` remains the
    /// product-name fallback and legacy caller-provided title.
    std::string windowTitle = "UniVex Engine";
    std::string projectDisplayNameUVE;
    /// Supports {productName}, {projectName}, and {sceneName}; literal text is preserved.
    std::string windowTitleFormatUVE = "{productName}";
    std::vector<EngineApplicationIconImageUVE> windowIconsUVE;

    /// Requested window size, in pixels, at creation time (the OS/window manager may still clamp
    /// or override it). Unused when headlessUVE is true.
    std::uint32_t windowWidth = 1280;
    std::uint32_t windowHeight = 720;

    /// Whether the real window is user-resizable. Remaining window policy is consumed by the
    /// platform backend at construction; unsupported platform-specific choices are retained in
    /// config but reported/fallback by that backend.
    bool windowResizableUVE = true;
    Platform::WindowModeUVE windowModeUVE = Platform::WindowModeUVE::Windowed;
    bool windowBorderlessUVE = false;
    bool windowAlwaysOnTopUVE = false;
    bool windowTransparentUVE = false;
    std::uint32_t windowMinimumWidthUVE = 0U;
    std::uint32_t windowMinimumHeightUVE = 0U;
    std::uint32_t windowMaximumWidthUVE = 0U;
    std::uint32_t windowMaximumHeightUVE = 0U;
    bool windowPositionSpecifiedUVE = false;
    std::int32_t windowPositionXUVE = 0;
    std::int32_t windowPositionYUVE = 0;
    std::string windowMonitorNameUVE;
    bool highDpiAwareUVE = true;
    bool perMonitorScalingUVE = true;
    double contentScaleOverrideUVE = 0.0;
    Platform::StretchModeUVE stretchModeUVE = Platform::StretchModeUVE::Disabled;
    Platform::AspectPolicyUVE aspectPolicyUVE = Platform::AspectPolicyUVE::Keep;
    bool integerOnlyScalingUVE = false;
    Platform::DisplayOrientationUVE orientationUVE = Platform::DisplayOrientationUVE::Auto;
    std::vector<Platform::DisplayOrientationUVE> allowedOrientationsUVE{
        Platform::DisplayOrientationUVE::Landscape, Platform::DisplayOrientationUVE::Portrait};

    /// `vsyncEnabledUVE` remains the source-compatible legacy toggle. Explicit enum selections
    /// set `vsyncModeExplicitUVE`; otherwise EngineCore maps the legacy bool to Off/On.
    bool vsyncEnabledUVE = true;
    Platform::VSyncModeUVE vsyncModeUVE = Platform::VSyncModeUVE::On;
    bool vsyncModeExplicitUVE = false;
    std::uint32_t focusedFrameRateCapUVE = 0U;
    std::uint32_t unfocusedFrameRateCapUVE = 0U;
    bool allowDisplaySleepUVE = true;
    std::filesystem::path cursorImagePathUVE;
    std::uint32_t cursorHotspotXUVE = 0U;
    std::uint32_t cursorHotspotYUVE = 0U;
    std::vector<std::uint8_t> cursorRgba8UVE;
    std::uint32_t cursorImageWidthUVE = 0U;
    std::uint32_t cursorImageHeightUVE = 0U;
    bool cursorVisibleUVE = true;
    bool cursorConfinedToWindowUVE = false;
    bool appendSceneNameInEditorPlayModeUVE = false;
    bool editorPlayModeUVE = false;

    /// Requested OpenGL context version, forwarded to Window::WindowDescUVE::glVersionMajor/Minor.
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
    ShaderCompilationModeUVE shaderCompilationModeUVE = ShaderCompilationModeUVE::AsynchronousOnDemandUVE;

    /// Request VK_LAYER_KHRONOS_validation when the Vulkan backend is selected. The layer is
    /// enabled only when installed; otherwise Vulkan logs a warning and continues without it.
#if UVE_DEBUG
    bool vulkanValidationLayersEnabledUVE = true;
#else
    bool vulkanValidationLayersEnabledUVE = false;
#endif

    /// Directory relative to the process working directory where Shader::ShaderManagerUVE persists
    /// compiled OpenGL program binaries (keyed by a hash of each program's fully resolved —
    /// post-#include, post-macro — source). A per-platform subdirectory is appended automatically;
    /// the cache directory is created lazily on first write. EngineCore rejects absolute/rooted paths
    /// and any `..` component; this lexical policy does not treat symlinks inside the working
    /// directory as a sandbox boundary.
    std::filesystem::path shaderCachePath = "shader_cache/";

    /// Whether ShaderManagerUVE::UpdateUVE() polls loaded shader programs' source file (and
    /// #include closure) modification times each frame and hot-swaps a recompiled program when
    /// one changed. Debug builds default to on (iteration speed); Release defaults to off (no
    /// reason a shipped build ever re-reads shader source off disk).
#if UVE_DEBUG
    bool shaderHotReloadEnabledUVE = true;
#else
    bool shaderHotReloadEnabledUVE = false;
#endif

    /// Poll interval, in seconds, ShaderManagerUVE waits between checking every hot-reload-tracked
    /// program's dependency closure for an on-disk modification time change — the shader-specific
    /// analogue of hotReloadPollIntervalSecondsUVE (kept as its own field since the two subsystems
    /// are otherwise unrelated and may want independent tuning later).
    double shaderHotReloadPollIntervalSecondsUVE = 1.0;

    /// Virtual-filesystem mount prefix ShaderManagerUVE's #include resolver and built-in shader
    /// loader resolve paths against (e.g. "shaders/basic_3d.glsl"), and the real directory it's
    /// mounted from during Init(). A shader file missing at this location is not an error — every
    /// built-in also carries an embedded C++ string fallback (see
    /// Render::Shader::BuiltIn::kBasic3DSource) used automatically when the mount doesn't
    /// resolve, so the engine still runs (without hot-reload) if launched from a working
    /// directory where the source tree isn't reachable.
    std::string shaderSourceMountPrefixUVE = "shaders";
    std::filesystem::path shaderSourceRealDirectoryUVE = "engine/render/shader/built_in/";

    /// Optional borrowed native window handle for platform backends. Android uses this as an
    /// ANativeWindow* supplied by NativeActivity; the engine never takes ownership or destroys it.
    /// Desktop callers leave it null and continue through the GLFW WindowManagerUVE path.
    void* nativeWindowHandleUVE = nullptr;

    /// Preferred render backend when a real window is created (ignored when headlessUVE is
    /// true — headless always uses NullRenderDeviceUVE). `AutoUVE` resolves to `OpenGLUVE`,
    /// the production default; `VulkanUVE` opts into the milestone-1 Vulkan bootstrap device
    /// (window surface + clear-color present; see Render::VulkanRenderDeviceUVE) with the
    /// documented Vulkan -> OpenGL -> Null fallback chain when the host lacks it. Appended after
    /// the pre-existing config fields so their aggregate-construction order remains unchanged.
    RenderBackendPreferenceUVE renderBackendPreferenceUVE = RenderBackendPreferenceUVE::AutoUVE;

    /// Maximum fraction of the display/viewport dimensions used for the 3D scene render target.
    /// With dynamic resolution disabled this is the fixed scale; when enabled it is the starting
    /// and upper scale. The final image is still presented at the normal output size.
    double renderResolutionScaleUVE = kMaximumRenderResolutionScaleUVE;
    bool dynamicRenderResolutionEnabledUVE = false;
    double minimumRenderResolutionScaleUVE = kMinimumRenderResolutionScaleUVE;
    double renderResolutionTargetFrameTimeMillisecondsUVE =
        kDefaultRenderResolutionTargetFrameTimeMillisecondsUVE;

    /// Spatial resampling used to fit the scene render target to the presentation size.
    SpatialUpscalingMethodUVE spatialUpscalingMethodUVE = SpatialUpscalingMethodUVE::BilinearUVE;
    /// Operator used to map the HDR scene color into display range in the final full-screen pass.
    ToneMappingMethodUVE toneMappingMethodUVE = ToneMappingMethodUVE::AcesUVE;
    /// Unsharp-mask strength applied to the final scene reconstruction before exposure/tone mapping; 0 disables it.
    float sharpeningAmountUVE = 0.0F;
    /// Add sub-LSB screen-space noise before output quantization to reduce visible banding.
    bool ditheringEnabledUVE = true;

    /// Fast approximate AA is applied in the Renderer3DUVE tone-mapping shader when selected.
    /// Temporal AA is not available until the engine supports frame history and camera jitter.
    PostProcessAAMethodUVE postProcessAAMethodUVE = PostProcessAAMethodUVE::NoneUVE;
    /// Edge-direction taps used only by fast approximate AA: 0 = low (3), 1 = medium (5),
    /// 2 = high (7), in addition to the filter's fixed four-neighbour contrast probes.
    std::uint32_t screenSpaceAAQualityUVE = 1U;

    /// Screen-space ambient occlusion controls. These are global renderer defaults; an active
    /// WorldEnvironment retains its existing enable/radius/intensity values as local overrides.
    bool ssaoEnabledUVE = true;
    float ssaoRadiusUVE = 0.5F;
    float ssaoIntensityUVE = 1.0F;
    float ssaoPowerUVE = 1.0F;
    /// 0 = Low (4 samples), 1 = Medium (8), 2 = High (12); High preserves the existing kernel cost.
    std::uint32_t ssaoQualityUVE = 2U;
    /// Apply the small Gaussian AO blur while compositing; disabled by default to preserve current output.
    bool ssaoBlurEnabledUVE = false;

    /// Frustum culling for the renderer's main colour view. DebugFreezeUVE submits everything the
    /// scene placed, wherever the camera looks; EnabledUVE preserves the shipped culling behaviour.
    FrustumCullingModeUVE frustumCullingModeUVE = FrustumCullingModeUVE::EnabledUVE;
};

} // namespace UVE::Core
