#include "asset_resident_size.h"
//                                      UVE
//                                UniVex Engine
//
// UniVex Engine (UVE) — Proprietary Game Engine
// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// Unauthorized copying, modification, distribution, or use of this code
// in whole or in part is strictly prohibited without express written
// permission from UniVex Studios.
// Violators will be prosecuted to the fullest extent of the law.


#include "engine_core.h"

#include "blueprint_asset.h"
#include "blueprint_nodes.h"
#include "widget_blueprint_asset.h"

#include "raycast_audio_occlusion.h"

#include "frame_pacing.h"
#include "frame_trace.h"
#include "render_backend_selection.h"
#include "legacy_extension_migration.h"
#include "png_metadata.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <random>
#include <sstream>
#include <tuple>
#include <exception>
#include <filesystem>
#include <cstdio>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <vector>
#include <utility>

#include "asset_bundle.h"
#include "asset_database.h"
#include "asset_importer.h"
#include "ttf_importer.h"
#include "asset_import_queue.h"
#include "asset_manager.h"
#include "animation_clip_asset.h"
#include "audio_asset.h"
#include "data_table_pipeline.h"
#include "derived_artifact_cache.h"
#include "file_system.h"
#include "hot_reload.h"
#include "material_asset.h"
#include "mesh_asset.h"
#include "project_file_index.h"
#include "project_change_watcher.h"
#include "shader_asset.h"
#include "texture_asset.h"
#include "audio_source_system.h"
#include "audio_system.h"
#include "miniaudio_audio_device.h"
#include "null_audio_device.h"
#include "wav_importer.h"
#include "compressed_audio_importer.h"
#include "command_line.h"
#include "config_manager.h"
#include "engine_project_settings.h"
#include "shader_cache_path_policy.h"
#include "blueprint_codegen.h"
#include "blueprint_compiler.h"
#include "engine_assert.h"
#include "log_sink.h"
#include "logger.h"
#include "logging_macros.h"
#include "event_system.h"
#include "gamepad_input_system.h"
#include "input_system.h"
#include "ai_events.h"
#include "perception.h"
#include "utility_ai.h"
#include "attribute_events.h"
#include "gameplay_tags.h"
#include "cinematic_events.h"
#include "cinematic.h"
#include "gameplay_attributes.h"
#include "gameplay_input.h"
#include "status_effects.h"
#include "health_events.h"
#include "interact_requested_event.h"
#include "trigger_events.h"
#include "trigger_volume.h"
#include "camera_follow.h"
#include "pawn.h"
#include "pawn_events.h"
#include "mobile_gesture_system.h"
#include "mobile_input_system.h"
#include "area_3d_runtime.h"
#include "i_raycast_system.h"
#include "raycast_query.h"
#include "i_physics_query_system.h"
#include "shape_cast_system.h"
#include "raycast_hit.h"
#include "area_overlap_events.h"
#include "matrix4x4.h"
#include "quaternion.h"
#include "memory_manager.h"
#include "animation_driver_component.h"
#include "animation_graph_component.h"
#include "animation_sequencer_component.h"
#include "camera_component.h"
#include "audio_source_component.h"
#include "hierarchy_component.h"
#include "canvas_component.h"
#include "ui_view_3d_component.h"
#include "light_component.h"
#include "world_environment_3d.h"
#include "directional_light_3d.h"
#include "scalar.h"
#include "quaternion.h"
#include "ui_button_component.h"
#include "ui_checkbox_component.h"
#include "ui_dropdown_component.h"
#include "ui_slider_component.h"
#include "ui_text_input_component.h"
#include "name_component.h"
#include "mesh_component.h"
#include "process_component.h"
#include "abstract_animation_objects_3d.h"
#include "animation_sequencer.h"
#include "audio_listener_3d.h"
#include "camera_3d.h"
#include "csg_shape_3d.h"
#include "grid_map_3d.h"
#include "two_bone_ik_3d.h"
#include "full_body_ik_3d.h"
#include "skeleton_3d.h"
#include "animation_graph.h"
#include "motion_montage.h"
#include "motion_montage_editing.h"
#include "montage_sequencer_component.h"
#include "decal_3d_events.h"
#include "decal_3d.h"
#include "nav_mesh_volume_3d.h"
#include "nav_seeker_3d.h"
#include "hitbox_3d.h"
#include "hurtbox_3d.h"
#include "interaction_area_3d.h"
#include "joint_3d.h"
#include "label_3d.h"
#include "health.h"
#include "multi_mesh_instance_3d.h"
#include "nav_obstacle_3d.h"
#include "physical_bone_3d.h"
#include "player_3d.h"
#include "character_3d.h"
#include "kinematic_3d.h"
#include "level_streamer_3d.h"
#include "particle_emitter_3d.h"
#include "path_3d.h"
#include "path_follow_3d.h"
#include "projectile_3d.h"
#include "reflection_probe_3d.h"
#include "remote_transform_3d.h"
#include "ray_cast_3d.h"
#include "shape_cast_3d.h"
#include "soft_body_3d.h"
#include "script.h"
#include "smart_object_3d.h"
#include "spring_arm_3d.h"
#include "spring_bone_simulator_3d.h"
#include "ragdoll_3d.h"
#include "scene_content_upgrade.h"
#include "sprite_3d.h"
#include "state_tree_3d.h"
#include "vehicle_3d.h"
#include "visibility_region_3d.h"
#include "visible_on_screen_notifier_3d.h"
#include "world_partition_3d.h"
#include "gi_probes_3d.h"
#include "xr_objects_3d.h"
#include "ui_extended_containers.h"
#include "ui_extended_controls.h"
#include "shape_narrow_phase.h"
#include "character_body_motion.h"
#include "character_controller.h"
#include "character_world_query.h"
#include "interaction_area.h"
#include "kinematic_body.h"
#include "projectile_3d_step.h"
#include "ray_cast_3d_step.h"
#include "hitbox_strike_events.h"
#include "hitbox_strike.h"
#include "hitbox_strike_lifecycle_tracker.h"
#include "spring_arm.h"
#include "collision_system.h"
#include "physics_system.h"
#include "raycast_system.h"
#include "platform.h"
#include "camera_system.h"
#include "compute_system.h"
#if UVE_HAS_DESKTOP_GL
#include "gl_render_device.h"
#endif
#include "vulkan_render_device.h"
#include "light_system.h"
#include "mesh_renderer.h"
#include "null_render_device.h"
#include "render_system.h"
#include "renderer_3d.h"
#include "shader_manager.h"
#include "checkpoint_manager.h"
#include "save_game_system.h"
#include "character_controller_component.h"
#include "montage_library_asset.h"
#include "collider_component.h"
#include "particle_emitter_component.h"
#include "particle_render_bridge.h"
#include "rigid_3d_component.h"
#include "solid_body_component.h"
#include "script_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "bone_attachment_pass.h"
#include "two_bone_ik_pass.h"
#include "full_body_ik_pass.h"
#include "prefab_system.h"
#include "scene_graph.h"
#include "scene_serializer.h"
#include "thread_pool.h"
#include "timer.h"
#include "adaptive_render_resolution.h"
#include "null_window_manager.h"
#include "presentation_layout.h"
#include "window_title_format.h"
#if defined(__ANDROID__)
#include "android_surface_size.h"
#include "android_window_manager.h"
#elif UVE_HAS_DESKTOP_GL
#include "window_manager.h"
#endif

namespace UVE::Core {

namespace {

#if defined(__ANDROID__)
constexpr Window::AdaptiveRenderResolutionLimits kAdaptiveRenderResolutionLimits{
    Window::kMaximumAndroidSurfaceAxis, Window::kMaximumAndroidRenderTargetPixels};
#else
// Keep large desktop displays sharp up to 4K while bounding worst-case offscreen allocations.
constexpr Window::AdaptiveRenderResolutionLimits kAdaptiveRenderResolutionLimits{8192U, 3840ULL * 2160ULL};
#endif

[[nodiscard]] Window::PresentationLayout ComputeWindowPresentationLayout(
    const EngineConfig& config, const std::uint32_t framebufferWidth, const std::uint32_t framebufferHeight) noexcept {
    return Window::ComputePresentationLayout(framebufferWidth, framebufferHeight, config.windowWidth,
                                                config.windowHeight, config.aspectPolicy,
                                                config.integerOnlyScaling);
}

[[nodiscard]] bool LoadCursorImage(const EngineConfig& config, std::vector<std::uint8_t>& outPixels,
                                      std::uint32_t& outWidth, std::uint32_t& outHeight) {
    outPixels.clear();
    outWidth = 0U;
    outHeight = 0U;
    if (config.cursorImagePath.empty()) {
        return true;
    }
    std::filesystem::path requestedPath = config.cursorImagePath;
    if (!requestedPath.is_absolute()) {
        if (config.projectContentRoot.empty()) {
            return false;
        }
        requestedPath = config.projectContentRoot / requestedPath;
    }

    std::error_code error;
    const std::filesystem::path resolvedPath = std::filesystem::canonical(requestedPath, error);
    if (error || !std::filesystem::is_regular_file(resolvedPath, error) || error) {
        return false;
    }
    if (!config.projectContentRoot.empty()) {
        const std::filesystem::path canonicalRoot = std::filesystem::canonical(config.projectContentRoot, error);
        if (error) {
            return false;
        }
        const std::filesystem::path relative = resolvedPath.lexically_relative(canonicalRoot);
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..") {
            return false;
        }
    }
    std::string extension = resolvedPath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (extension != ".png") {
        return false;
    }

    constexpr std::uintmax_t kMaximumCursorFileBytes = 64ULL * 1024ULL * 1024ULL;
    const std::uintmax_t fileSize = std::filesystem::file_size(resolvedPath, error);
    if (error || fileSize == 0U || fileSize > kMaximumCursorFileBytes) {
        return false;
    }
    std::ifstream input(resolvedPath, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(fileSize));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size()) || input.bad()) {
        return false;
    }
    const std::optional<Asset::PngMetadata> metadata = Asset::ParsePngMetadata(bytes);
    constexpr std::uint64_t kMaximumCursorDecodedBytes = 64ULL * 1024ULL * 1024ULL;
    if (!metadata || metadata->width == 0U || metadata->height == 0U || metadata->width > 8192U ||
        metadata->height > 8192U ||
        !Asset::ValidatePngRgba8PixelBudget(*metadata, kMaximumCursorDecodedBytes)) {
        return false;
    }
    Asset::PngRgba8Image image;
    if (!Asset::DecodePngRgba8Image(bytes, image)) {
        return false;
    }
    outWidth = image.width;
    outHeight = image.height;
    outPixels.reserve(image.pixels.size());
    for (const std::byte pixel : image.pixels) {
        outPixels.push_back(std::to_integer<std::uint8_t>(pixel));
    }
    return true;
}

/// The process mode the scene graph resolved for `entity` on its last update, or the hierarchy
/// default for an entity it has not seen yet (created since, or not a scene-graph object). Asking the
/// scene graph rather than the entity's own ProcessComponent is what makes an entity with no
/// component inherit its ancestors' answer instead of silently ignoring it.
[[nodiscard]] Scene::TickMode ResolvedTickMode(const Scene::ISceneGraph& sceneGraph,
                                                           const Scene::Entity entity) {
    const std::optional<Scene::ResolvedObjectModes> modes = sceneGraph.TryGetResolvedObjectModes(entity);
    return modes.has_value() ? modes->process : Scene::TickMode::Running;
}

/// An entity's own ordering key, or 0 when it carries no ProcessComponent. Priorities are
/// authored per entity and deliberately NOT inherited: a whole subtree sharing its root's priority
/// would make it impossible to order anything inside it.
[[nodiscard]] Scene::ProcessComponent ProcessSettings(const Scene::IEntityManager& entityManager,
                                                            const Scene::Entity entity) {
    return entityManager.HasComponent<Scene::ProcessComponent>(entity)
               ? entityManager.GetComponent<Scene::ProcessComponent>(entity)
               : Scene::ProcessComponent{};
}

/// The entities holding ComponentT whose fixed-step work runs this step, in physicsPriority order.
///
/// A fixed step is evaluated as "not paused" whichever way it came about - normal running, or the
/// single step the editor requests while paused - because either way it IS the simulation
/// advancing. So a fixed step skips only Disabled and PausedOnly entities; the latter never advance
/// physics, because physics does not step while paused.
///
/// Ties keep the ECS iteration order these systems used before priority existed (stable_sort over
/// that order), so a scene that sets no priorities steps in exactly the order it always did.
template <typename ComponentT>
[[nodiscard]] std::vector<Scene::Entity> CollectFixedStepOrder(Scene::IEntityManager& entityManager,
                                                                     const Scene::ISceneGraph& sceneGraph) {
    std::vector<std::pair<std::int32_t, Scene::Entity>> ordered;
    entityManager.ForEach<ComponentT>([&](const Scene::Entity entity, ComponentT&) {
        if (!Scene::IsTicking(ResolvedTickMode(sceneGraph, entity), /*simulationPaused=*/false)) {
            return;
        }
        ordered.emplace_back(ProcessSettings(entityManager, entity).physicsPriority, entity);
    });
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto& left, const auto& right) { return left.first < right.first; });
    std::vector<Scene::Entity> entities;
    entities.reserve(ordered.size());
    for (const auto& [priority, entity] : ordered) {
        static_cast<void>(priority);
        entities.push_back(entity);
    }
    return entities;
}

} // namespace

EngineCore::EngineCore(EngineConfig config) : m_config(std::move(config)) {
    if (!RegisterEngineProjectSettings(m_projectSettings.GetRegistry())) {
        throw std::logic_error("Failed to register the engine's project settings.");
    }
}

EngineCore::~EngineCore() {
    if (m_state == EngineState::Running) {
        Shutdown();
    }
}

void EngineCore::TransitionState(EngineState newState) {
    UVE_ASSERT(IsValidTransition(m_state, newState));
    m_state = newState;
}

void EngineCore::RegisterBuiltInAssetLoaders() {
    m_assetManager->RegisterLoader<Asset::MeshAsset>(&Asset::LoadMeshAsset,
        [](const Asset::MeshAsset& value) { return Asset::AssetResidentBytes(value); });
    // The source-aware reader, not the strict envelope one: a material's texture slot may name the
    // PNG the author picked as easily as a published .uvtex, and a loader that only understands the
    // envelope turns the first case into the white fallback - a surface showing its flat colour
    // where its albedo map should be. Cook-time and pack tools keep the strict reader, because a
    // cooked project must reference published textures only.
    m_assetManager->RegisterLoader<Asset::TextureAsset>(&Asset::LoadTextureAssetSource,
        [](const Asset::TextureAsset& value) { return Asset::AssetResidentBytes(value); });
    m_assetManager->RegisterLoader<Asset::ShaderAsset>(&Asset::LoadShaderAsset,
        [](const Asset::ShaderAsset& value) { return Asset::AssetResidentBytes(value); });
    m_assetManager->RegisterLoader<Asset::MaterialAsset>(&Asset::LoadMaterialAsset,
        [](const Asset::MaterialAsset& value) { return Asset::AssetResidentBytes(value); });
    m_assetManager->RegisterLoader<Asset::AudioAsset>(&Asset::LoadAudioAsset,
        [](const Asset::AudioAsset& value) { return Asset::AssetResidentBytes(value); });
    m_assetManager->RegisterLoader<Asset::AnimationClipAsset>(&Asset::LoadAnimationClipAsset,
        [](const Asset::AnimationClipAsset& value) { return Asset::AssetResidentBytes(value); });
}

void EngineCore::Init() {
    TransitionState(EngineState::Initializing);

    // CommandLine first: it has no dependencies and parses the raw startup tokens. Registered
    // setting flags (including --headless) are converted to typed values after the settings schema
    // is ready, then resolved at the top of SettingsStack.
    m_commandLine = std::make_unique<CommandLine::CommandLine>(m_config.commandLineArgs);

    // Logger second: every later step below, and every other engine
    // system, may need to log or UVE_ASSERT during its own setup. See
    // docs/CODING_STANDARDS.md for the full init/shutdown ordering
    // rationale.
    auto logger = std::make_unique<Debug::Logger>();
    logger->Init(m_config.minLogLevel);
    if (m_config.enableConsoleLogging) {
        logger->AddSink(std::make_unique<Debug::ConsoleSink>());
    }
    logger->AddSink(std::make_unique<Debug::FileSink>(m_config.logFilePath));
    m_logger = std::move(logger);

    UVE_INFO("EngineCore: initializing UniVex Engine {}", GetEngineVersion().ToString());

    // Projects saved before the .uve* -> .uv* rename are moved over before anything reads them:
    // the config files by name, the content folder file by file, then the asset registry's paths.
    const std::filesystem::path platformSettingsPath = GetPlatformSettingsFilePath(m_config);
    const std::array<const std::filesystem::path*, 5U> settingsFiles{
        &m_config.projectSettingsFilePath, &m_config.settingsFilePath, &platformSettingsPath, &m_config.inputMapFilePath,
        &m_config.assetDatabaseFilePath};
    for (const std::filesystem::path* const file : settingsFiles) {
        static_cast<void>(Asset::MigrateLegacyFile(*file));
    }
    static_cast<void>(Asset::MigrateLegacyContent(m_config.projectContentRoot));
    static_cast<void>(Asset::RewriteLegacyExtensionsInTextFile(m_config.assetDatabaseFilePath));
    UpgradeProjectContentIfNeeded();

    // Load the project and user stores before constructing any system that consumes the effective
    // EngineConfig fields. The application-supplied config remains the base; valid settings then
    // resolve project -> user -> active platform -> command line. The platform file is optional and
    // separate from user preferences; it is loaded only when present to avoid a warning on first run.
    if (!m_projectSettings.Load(m_config.projectSettingsFilePath)) {
        UVE_WARNING("EngineCore: project settings \"{}\" could not be read; continuing without project overrides",
                    m_config.projectSettingsFilePath.string());
    }
    auto configManager = std::make_unique<Config::ConfigManager>();
    configManager->Load(m_config.settingsFilePath);
    m_configManager = std::move(configManager);

    Config::ConfigManager platformSettings;
    std::error_code platformSettingsError;
    if (std::filesystem::exists(platformSettingsPath, platformSettingsError)) {
        static_cast<void>(platformSettings.Load(platformSettingsPath));
    } else if (platformSettingsError) {
        UVE_WARNING("EngineCore: platform settings \"{}\" could not be checked: {}",
                    platformSettingsPath.string(), platformSettingsError.message());
    }

    Config::ConfigManager commandLineSettings;
    PopulateEngineCommandLineSettings(m_projectSettings.GetRegistry(), *m_commandLine, commandLineSettings);

    Config::SettingsStack settings(m_projectSettings.GetRegistry());
    if (!AttachEngineSettingsLayers(settings, m_projectSettings.GetStore(), *m_configManager, platformSettings,
                                       commandLineSettings)) {
        throw std::logic_error("Failed to attach engine settings layers.");
    }
    const std::filesystem::path initialLogFilePath = m_config.logFilePath;
    const std::filesystem::path callerShaderCachePath = m_config.shaderCachePath;
    const bool callerSuppliedSafeAbsoluteShaderCache =
        !callerShaderCachePath.empty() && callerShaderCachePath.is_absolute() &&
        callerShaderCachePath.native().find(std::filesystem::path::value_type{}) ==
            std::filesystem::path::string_type::npos &&
        std::ranges::none_of(callerShaderCachePath, [](const std::filesystem::path& component) {
            return component == std::filesystem::path("..");
        });
    ApplyEngineSettings(settings, m_config);
    if (m_config.logFilePath != initialLogFilePath && !m_config.logFilePath.empty()) {
        auto updatedLogger = std::make_unique<Debug::Logger>();
        updatedLogger->Init(m_config.minLogLevel);
        if (m_config.enableConsoleLogging) {
            updatedLogger->AddSink(std::make_unique<Debug::ConsoleSink>());
        }
        updatedLogger->AddSink(std::make_unique<Debug::FileSink>(m_config.logFilePath));
        m_logger = std::move(updatedLogger);
    }
    const bool acceptCallerAbsoluteShaderCache =
        callerSuppliedSafeAbsoluteShaderCache && m_config.shaderCachePath == callerShaderCachePath;
    if (!acceptCallerAbsoluteShaderCache && !IsSafeShaderCachePath(m_config.shaderCachePath)) {
        const std::filesystem::path defaultShaderCachePath = EngineConfig{}.shaderCachePath;
        UVE_WARNING("EngineCore: shader-cache directory must be a non-empty relative path without parent "
                    "segments; using \"{}\"",
                    defaultShaderCachePath.string());
        m_config.shaderCachePath = defaultShaderCachePath;
    }
    switch (m_config.shaderCompilationMode) {
    case ShaderCompilationMode::SynchronousOnDemand:
    case ShaderCompilationMode::AsynchronousOnDemand:
        break;
    default:
        UVE_WARNING("EngineCore: invalid shader compilation mode {}; using asynchronous on-demand",
                    static_cast<std::uint32_t>(m_config.shaderCompilationMode));
        m_config.shaderCompilationMode = ShaderCompilationMode::AsynchronousOnDemand;
        break;
    }
    switch (m_config.spatialUpscalingMethod) {
    case SpatialUpscalingMethod::Bilinear:
    case SpatialUpscalingMethod::Nearest:
        break;
    default:
        UVE_WARNING("EngineCore: invalid spatial upscaling method {}; using bilinear",
                    static_cast<std::uint32_t>(m_config.spatialUpscalingMethod));
        m_config.spatialUpscalingMethod = SpatialUpscalingMethod::Bilinear;
        break;
    }
    switch (m_config.toneMappingMethod) {
    case ToneMappingMethod::LinearClamp:
    case ToneMappingMethod::Aces:
    case ToneMappingMethod::SoftFilmic:
        break;
    default:
        UVE_WARNING("EngineCore: invalid tone-mapping method {}; using SoftFilmic",
                    static_cast<std::uint32_t>(m_config.toneMappingMethod));
        m_config.toneMappingMethod = ToneMappingMethod::SoftFilmic;
        break;
    }
    switch (m_config.postProcessAAMethod) {
    case PostProcessAAMethod::None:
    case PostProcessAAMethod::FastApproximate:
        break;
    default:
        UVE_WARNING("EngineCore: invalid post-process AA method {}; disabling AA",
                    static_cast<std::uint32_t>(m_config.postProcessAAMethod));
        m_config.postProcessAAMethod = PostProcessAAMethod::None;
        break;
    }
    if (m_config.screenSpaceAAQuality > 2U) {
        UVE_WARNING("EngineCore: screen-space AA quality {} is outside [0, 2]; clamping to 2",
                    m_config.screenSpaceAAQuality);
        m_config.screenSpaceAAQuality = 2U;
    }
    if (!std::isfinite(m_config.ssaoRadius) || m_config.ssaoRadius < 0.01F ||
        m_config.ssaoRadius > 10.0F) {
        UVE_WARNING("EngineCore: SSAO radius {} is outside [0.01, 10]; using 0.5", m_config.ssaoRadius);
        m_config.ssaoRadius = 0.5F;
    }
    if (!std::isfinite(m_config.ssaoIntensity) || m_config.ssaoIntensity < 0.0F ||
        m_config.ssaoIntensity > 4.0F) {
        UVE_WARNING("EngineCore: SSAO intensity {} is outside [0, 4]; using 1", m_config.ssaoIntensity);
        m_config.ssaoIntensity = 1.0F;
    }
    if (!std::isfinite(m_config.ssaoPower) || m_config.ssaoPower < 0.1F || m_config.ssaoPower > 4.0F) {
        UVE_WARNING("EngineCore: SSAO power {} is outside [0.1, 4]; using 1", m_config.ssaoPower);
        m_config.ssaoPower = 1.0F;
    }
    if (m_config.ssaoQuality > 2U) {
        UVE_WARNING("EngineCore: SSAO quality {} is outside [0, 2]; clamping to 2", m_config.ssaoQuality);
        m_config.ssaoQuality = 2U;
    }
    if (!std::isfinite(m_config.renderResolutionScale) ||
        m_config.renderResolutionScale < kMinimumRenderResolutionScale ||
        m_config.renderResolutionScale > kMaximumRenderResolutionScale) {
        UVE_WARNING("EngineCore: invalid render-resolution scale {}; expected [{}, {}]; using 1.0",
                    m_config.renderResolutionScale, kMinimumRenderResolutionScale,
                    kMaximumRenderResolutionScale);
        m_config.renderResolutionScale = kMaximumRenderResolutionScale;
    }
    if (!std::isfinite(m_config.minimumRenderResolutionScale) ||
        m_config.minimumRenderResolutionScale < kMinimumRenderResolutionScale ||
        m_config.minimumRenderResolutionScale > kMaximumRenderResolutionScale) {
        UVE_WARNING("EngineCore: invalid minimum render-resolution scale {}; using {}",
                    m_config.minimumRenderResolutionScale, kMinimumRenderResolutionScale);
        m_config.minimumRenderResolutionScale = kMinimumRenderResolutionScale;
    }
    if (m_config.minimumRenderResolutionScale > m_config.renderResolutionScale) {
        UVE_WARNING("EngineCore: minimum render-resolution scale {} exceeds maximum {}; clamping minimum",
                    m_config.minimumRenderResolutionScale, m_config.renderResolutionScale);
        m_config.minimumRenderResolutionScale = m_config.renderResolutionScale;
    }
    if (!std::isfinite(m_config.renderResolutionTargetFrameTimeMilliseconds) ||
        m_config.renderResolutionTargetFrameTimeMilliseconds <
            kMinimumRenderResolutionTargetFrameTimeMilliseconds ||
        m_config.renderResolutionTargetFrameTimeMilliseconds >
            kMaximumRenderResolutionTargetFrameTimeMilliseconds) {
        UVE_WARNING("EngineCore: invalid render-resolution target frame time {}; using {} ms",
                    m_config.renderResolutionTargetFrameTimeMilliseconds,
                    kDefaultRenderResolutionTargetFrameTimeMilliseconds);
        m_config.renderResolutionTargetFrameTimeMilliseconds =
            kDefaultRenderResolutionTargetFrameTimeMilliseconds;
    }
    const DynamicRenderResolutionSettings dynamicResolutionSettings{
        m_config.minimumRenderResolutionScale,
        m_config.renderResolutionScale,
        m_config.renderResolutionTargetFrameTimeMilliseconds};
    if (!m_dynamicRenderResolutionController.Configure(dynamicResolutionSettings)) {
        UVE_WARNING("EngineCore: dynamic render-resolution policy is invalid; using configured fixed scale");
        m_config.dynamicRenderResolutionEnabled = false;
    }
    m_currentRenderResolutionScale = m_config.dynamicRenderResolutionEnabled
                                            ? m_dynamicRenderResolutionController.GetCurrentScale()
                                            : m_config.renderResolutionScale;
    if (m_config.windowTransparent) {
        m_config.backgroundColor[3] = 0.0F;
    }
    if (!m_config.cursorImagePath.empty() && m_config.cursorRgba8.empty()) {
        if (!LoadCursorImage(m_config, m_config.cursorRgba8, m_config.cursorImageWidth,
                                m_config.cursorImageHeight) ||
            m_config.cursorHotspotX >= m_config.cursorImageWidth ||
            m_config.cursorHotspotY >= m_config.cursorImageHeight) {
            UVE_WARNING("EngineCore: configured cursor image could not be loaded or its hotspot is outside "
                        "the image; using the system cursor");
            m_config.cursorRgba8.clear();
            m_config.cursorImageWidth = 0U;
            m_config.cursorImageHeight = 0U;
            m_config.cursorHotspotX = 0U;
            m_config.cursorHotspotY = 0U;
        }
    }

    // MemoryManager third: the next most foundational service after
    // logging — nothing constructed here has a hard dependency on it yet,
    // but future systems will, mirroring the rationale for Logger's own
    // position.
    m_memoryManager = std::make_unique<Memory::MemoryManager>();

    // ThreadPool fourth: sits alongside Logger/MemoryManager as a
    // foundational service (matching the spec's own Part 7.1 ordering,
    // which lists ThreadPool before EventSystem/Timer) and after
    // Logger/MemoryManager since its workers may immediately want to log
    // or allocate once real jobs start flowing through it.
    m_threadPool = std::make_unique<Threading::ThreadPool>(m_config.threadPoolWorkerCount);

    // Timer fifth: Update()/LateUpdate() depend on it; nothing constructed
    // here depends on EventSystem existing yet.
    auto timer = std::make_unique<Utilities::Timer>();
    timer->Reset();
    timer->SetMaxDeltaTime(m_config.maxDeltaTimeSeconds);
    timer->SetFixedTimestep(m_config.fixedUpdateFps > 0.0 ? (1.0 / m_config.fixedUpdateFps) : 0.0);
    timer->SetMaxStepsPerTick(m_config.maxFixedStepsPerFrame);
    m_timer = std::move(timer);

    // EventSystem sixth: it is the piece most likely to gain future
    // dependents (systems subscribing during their own Init()), so it is
    // constructed after every other foundational service except
    // EntityManager/SceneGraph, once it is guaranteed nothing else in this
    // list still needs to be built.
    m_eventSystem = std::make_unique<Events::EventSystem>();

    // EntityManager seventh: needs MemoryManager (for chunk allocation) and
    // EventSystem (for entity lifecycle events), so it is built right after
    // both exist.
    m_entityManager = std::make_unique<Scene::EntityManager>(
        m_memoryManager->GetDefaultAllocator(), *m_eventSystem);

    // SceneGraph eighth: has no dependencies of its own; grouped
    // immediately after EntityManager for readability.
    m_sceneGraph = std::make_unique<Scene::SceneGraph>();

    // AssetDatabase ninth: needs only Logger, which already exists. Its
    // Load() call logs its outcome (missing/malformed/success) the same
    // way ConfigManager's does below.
    auto assetDatabase = std::make_unique<Asset::AssetDatabase>();
    const bool catalogLoaded = assetDatabase->Load(m_config.assetDatabaseFilePath);
    if (m_config.cookedAssetPaths && !catalogLoaded) throw std::runtime_error("Cooked asset catalog unavailable");
    if (m_config.cookedAssetPaths && !assetDatabase->RootPaths(m_config.projectRootDirectory))
        throw std::runtime_error("Invalid cooked asset catalog paths");
    if (catalogLoaded && !m_config.cookedAssetPaths &&
        assetDatabase->RelocateMovedContent(m_config.projectContentRoot) > 0U) {
        static_cast<void>(assetDatabase->Save());
    }
    m_assetDatabase = std::move(assetDatabase);

    // ProjectFileIndex tenth: holds only an explicit configured content-root
    // and a cached read-only editor snapshot. It never scans until an editor
    // caller requests Refresh(), and has no ownership of AssetDatabase.
    m_projectFileIndex = std::make_unique<Asset::ProjectFileIndex>(m_config.projectContentRoot);

    // DerivedArtifactCache eleventh: owns only project-local generated import metadata. It creates
    // its configured root lazily during a successful cache write and has no AssetDatabase ownership.
    m_derivedArtifactCache = std::make_unique<Asset::DerivedArtifactCache>(m_config.derivedArtifactCacheRoot);

    // ProjectChangeWatcher twelfth: interval-driven project-content observation stays separate
    // from the loaded-runtime HotReload poller. It owns no importer, worker, or index refresh policy.
    m_projectChangeWatcher = std::make_unique<Asset::ProjectChangeWatcher>(
        m_config.projectContentRoot, m_config.projectChangeWatchPollIntervalSeconds,
        m_config.projectChangeJournalCapacity);

    // SceneSerializer and PrefabSystem thirteenth/fourteenth: both stateless,
    // grouped immediately after AssetDatabase/ProjectFileIndex for readability.
    m_sceneSerializer = std::make_unique<Scene::SceneSerializer>();
    m_prefabSystem = std::make_unique<Scene::PrefabSystem>();

    // HotReload twelfth: needs only EventSystem. Constructed before
    // AssetManager (not after, as its Part 7.4 spec-listing order might
    // suggest) purely because AssetManager's constructor takes a
    // HotReload* — construction must stay strictly forward-dependency.
    m_hotReload = std::make_unique<Asset::HotReload>(*m_eventSystem, m_config.hotReloadPollIntervalSeconds);

    // AssetManager thirteenth: needs ThreadPool (to run loads) and
    // EventSystem (to publish AssetLoadCompletedEvent), and is always
    // given a real HotReload* (never null) — EngineConfig::
    // hotReloadEnabled only gates whether Update() calls Poll(), not
    // whether HotReload exists at all.
    auto assetManager = std::make_unique<Asset::AssetManager>(*m_threadPool, *m_eventSystem, m_hotReload.get());
    assetManager->SetResidentBudget(m_config.assetResidentBudgetBytes);
    m_assetManager = std::move(assetManager);
    RegisterBuiltInAssetLoaders();

    // AssetImporter fourteenth: retains the existing extension-selected synchronous import behavior.
    m_assetImporter = std::make_unique<Asset::AssetImporter>();
    Audio::RegisterWavImporter(*m_assetImporter);
    Audio::RegisterCompressedAudioImporters(*m_assetImporter);
    Asset::RegisterTtfImporter(*m_assetImporter);

    // Compose the schema-driven Data Table importers and typed loader onto the existing generic
    // services. Registration owns no service or loaded asset state, so EngineCore remains the
    // sole owner and the generic service boundaries remain unchanged.
    Asset::RegisterDataTablePipeline(*m_assetImporter, *m_assetManager);

    // AssetImportQueue fifteenth: calls the importer at most once per Update() and validates
    // metadata cache hits against source and destination byte fingerprints plus AssetDatabase.
    m_assetImportQueue = std::make_unique<Asset::AssetImportQueue>(
        *m_assetImporter, *m_assetDatabase, *m_derivedArtifactCache);

    // AssetBundle sixteenth: stateless and independent from the queue/cache.
    m_assetBundle = std::make_unique<Asset::AssetBundle>();

    // FileSystem seventeenth: needs AssetBundle, since its bundle-backed
    // mounts read entries through IAssetBundle.
    m_fileSystem = std::make_unique<Asset::FileSystem>(*m_assetBundle);

    Window::WindowDesc windowDesc;
    windowDesc.title = Window::FormatWindowTitle(
        m_config.windowTitleFormat, m_config.windowTitle, m_config.projectDisplayName, {},
        m_config.editorPlayMode, m_config.appendSceneNameInEditorPlayMode);
    windowDesc.icons.reserve(m_config.windowIcons.size());
    for (const Core::EngineApplicationIconImage& icon : m_config.windowIcons) {
        windowDesc.icons.push_back(Window::WindowIcon{icon.width, icon.height, icon.rgba8});
    }
    windowDesc.width = m_config.windowWidth;
    windowDesc.height = m_config.windowHeight;
    windowDesc.resizable = m_config.windowResizable;
    windowDesc.vsyncEnabled = m_config.vsyncEnabled;
    windowDesc.vsyncModeExplicit = m_config.vsyncModeExplicit;
    windowDesc.vsyncMode = m_config.vsyncMode;
    windowDesc.mode = m_config.windowMode;
    windowDesc.borderless = m_config.windowBorderless;
    windowDesc.alwaysOnTop = m_config.windowAlwaysOnTop;
    windowDesc.transparent = m_config.windowTransparent;
    windowDesc.minimumWidth = m_config.windowMinimumWidth;
    windowDesc.minimumHeight = m_config.windowMinimumHeight;
    windowDesc.maximumWidth = m_config.windowMaximumWidth;
    windowDesc.maximumHeight = m_config.windowMaximumHeight;
    windowDesc.initialPositionSpecified = m_config.windowPositionSpecified;
    windowDesc.initialPositionX = m_config.windowPositionX;
    windowDesc.initialPositionY = m_config.windowPositionY;
    windowDesc.monitorName = m_config.windowMonitorName;
    windowDesc.highDpiAware = m_config.highDpiAware;
    windowDesc.perMonitorScaling = m_config.perMonitorScaling;
    windowDesc.contentScaleOverride = m_config.contentScaleOverride;
    windowDesc.stretchMode = m_config.stretchMode;
    windowDesc.aspectPolicy = m_config.aspectPolicy;
    windowDesc.integerOnlyScaling = m_config.integerOnlyScaling;
    windowDesc.orientation = m_config.orientation;
    windowDesc.allowedOrientations = m_config.allowedOrientations;
    windowDesc.focusedFrameRateCap = m_config.focusedFrameRateCap;
    windowDesc.unfocusedFrameRateCap = m_config.unfocusedFrameRateCap;
    windowDesc.allowDisplaySleep = m_config.allowDisplaySleep;
    windowDesc.cursorImageWidth = m_config.cursorImageWidth;
    windowDesc.cursorImageHeight = m_config.cursorImageHeight;
    windowDesc.cursorRgba8 = m_config.cursorRgba8;
    windowDesc.cursorHotspotX = m_config.cursorHotspotX;
    windowDesc.cursorHotspotY = m_config.cursorHotspotY;
    windowDesc.cursorVisible = m_config.cursorVisible;
    windowDesc.cursorConfinedToWindow = m_config.cursorConfinedToWindow;
    windowDesc.glVersionMajor = m_config.windowGlVersionMajor;
    windowDesc.glVersionMinor = m_config.windowGlVersionMinor;

#if UVE_HAS_DESKTOP_GL
    // WindowManager seventeenth: a real Window::WindowManager (owning the entire GLFW/GL
    // context lifecycle — init, create, activate, destroy, terminate) unless
    // EngineConfig::headless, in which case Window::NullWindowManager. A real window
    // that fails to create logs UVE_FATAL and sets m_windowCreationFailed rather than aborting
    // Init() mid-construction — EngineState's transition table forbids jumping straight from
    // Initializing to ShuttingDown, so every remaining service below still finishes constructing
    // normally; Load() checks the flag afterward (see Load()'s doc comment). A window that exists
    // but cannot load the complete GL function table is handled separately below as a recoverable
    // backend-selection failure and falls back to NullRenderDevice.
    if (m_config.headless) {
        m_windowManager = std::make_unique<Window::NullWindowManager>(windowDesc);
    } else {
#if defined(__ANDROID__)
        if (m_config.nativeWindowHandle == nullptr) {
            UVE_FATAL("EngineCore: Android window handle is required for a windowed run");
            m_windowManager = std::make_unique<Window::NullWindowManager>();
            m_windowCreationFailed = true;
        } else {
            m_windowManager = std::make_unique<Window::AndroidWindowManager>(
                *m_eventSystem, m_config.nativeWindowHandle, windowDesc);
        }
#else
        m_windowManager = std::make_unique<Window::WindowManager>(*m_eventSystem, windowDesc);
#endif
        if (!m_windowManager->IsValid()) {
            UVE_FATAL("EngineCore: window creation failed - this run will not proceed past Load()");
            m_windowCreationFailed = true;
        }
    }
    m_windowedRenderingActive = !m_config.headless && m_windowManager->IsValid();

    // RenderDevice eighteenth: use the tested preference-to-fallback order when a real window
    // exists. A preferred backend is only a starting point; every plan terminates in Null so an
    // unavailable driver/device never prevents the engine from completing startup.
    if (m_windowedRenderingActive) {
        const RenderBackendAttemptOrder attemptOrder =
            ResolveRenderBackendAttemptOrder(m_config.renderBackendPreference);
        for (std::size_t attempt = 0U; attempt < attemptOrder.count && m_renderDevice == nullptr; ++attempt) {
            switch (attemptOrder.backends[attempt]) {
            case RenderBackendKind::Vulkan: {
                // Factory-probe create: nullptr (with reason logged) when the host lacks a loader,
                // an ICD, or the GLFW bridge's surface capability. IsUsable() remains a guard
                // against a future early-inert state.
                auto vulkanDevice = Render::VulkanRenderDevice::Create(
                    *m_windowManager, m_config.vulkanValidationLayersEnabled);
                if (vulkanDevice != nullptr && vulkanDevice->IsUsable()) {
                    m_renderDevice = std::move(vulkanDevice);
                    UVE_INFO("EngineCore: render backend = {}", m_renderDevice->GetBackendName());
                } else {
                    UVE_WARNING("EngineCore: requested Vulkan backend is unavailable on this host; trying next backend");
                }
                break;
            }
            case RenderBackendKind::OpenGL: {
                auto glRenderDevice = std::make_unique<Render::GlRenderDevice>(*m_windowManager);
                if (glRenderDevice->IsUsable()) {
                    m_renderDevice = std::move(glRenderDevice);
                    UVE_INFO("EngineCore: render backend = {}", m_renderDevice->GetBackendName());
                } else {
#if defined(__ANDROID__)
                    UVE_WARNING("EngineCore: OpenGL ES backend is unavailable; trying the next backend");
#else
                    UVE_WARNING("EngineCore: OpenGL backend is unavailable; trying the next backend");
#endif
                }
                break;
            }
            case RenderBackendKind::Null:
                m_windowedRenderingActive = false;
                m_renderDevice = std::make_unique<Render::NullRenderDevice>();
                break;
            }
        }
        if (m_renderDevice == nullptr) {
            // Defensive fallback if a future plan omits Null or contains no attempts.
            m_windowedRenderingActive = false;
            m_renderDevice = std::make_unique<Render::NullRenderDevice>();
        }
    } else {
        m_renderDevice = std::make_unique<Render::NullRenderDevice>();
    }
    m_presentationSurfaceReady = m_windowedRenderingActive;
#else
    // CPU-only builds still support a real headless EngineCore runtime. Keep the full settings
    // bootstrap and Null-backed systems available for servers/tests; a requested windowed run
    // degrades to the null backend rather than making EngineCore itself unbuildable.
    if (!m_config.headless) {
        UVE_WARNING("EngineCore: desktop window/rendering support is unavailable in this build; using the null backend");
    }
    m_windowManager = std::make_unique<Window::NullWindowManager>(windowDesc);
    m_windowedRenderingActive = false;
    m_renderDevice = std::make_unique<Render::NullRenderDevice>();
    m_presentationSurfaceReady = false;
#endif

    // ShaderManager nineteenth: needs ThreadPool, EventSystem, RenderDevice, and FileSystem — all
    // already constructed by this point. Mounts EngineConfig::shaderSourceRealDirectory
    // under shaderSourceMountPrefix first, so the built-in .glsl files (and any #include
    // closure among them) resolve through the VFS and participate in hot-reload; a
    // missing/unreachable directory is not an error here either — every built-in also carries an
    // embedded string fallback (see Render::Shader::BuiltIn::kBasic3DSource) ShaderManager uses
    // automatically when the mount doesn't resolve. Works identically in headless mode (against
    // NullRenderDevice) and windowed mode (against GlRenderDevice).
    m_fileSystem->MountDirectory(m_config.shaderSourceMountPrefix, m_config.shaderSourceRealDirectory, 0);
    // The project itself, beneath everything else: without it no project-relative asset path -
    // an object's script above all - could be read at runtime, so scripts were saved but never ran.
    if (!m_config.projectRootDirectory.empty()) {
        m_fileSystem->MountDirectory("", m_config.projectRootDirectory, -100);
    }
    Render::Shader::ShaderManagerConfig shaderManagerConfig;
    shaderManagerConfig.cachePath = m_config.shaderCachePath;
    shaderManagerConfig.compileSynchronously =
        m_config.shaderCompilationMode == ShaderCompilationMode::SynchronousOnDemand;
    shaderManagerConfig.hotReloadEnabled = m_config.shaderHotReloadEnabled;
    shaderManagerConfig.hotReloadPollIntervalSeconds = m_config.shaderHotReloadPollIntervalSeconds;
#if UVE_DEBUG
    shaderManagerConfig.injectDebugDefine = true;
#else
    shaderManagerConfig.injectDebugDefine = false;
#endif
    m_shaderManager = std::make_unique<Render::Shader::ShaderManager>(
        *m_threadPool, *m_eventSystem, *m_renderDevice, *m_fileSystem, shaderManagerConfig);

    // RenderSystem twentieth: needs RenderDevice, since it records and
    // submits command buffers through it.
    m_renderSystem = std::make_unique<Render::RenderSystem>(*m_renderDevice);

    // ComputeSystem: needs RenderDevice (it creates compute programs and records dispatches
    // through it). Constructed right after RenderSystem because Render() drains its queue into
    // the frame's own command buffer, before any render pass opens — the outside-pass-markers
    // contract IComputeSystem documents.
    m_computeSystem = std::make_unique<Render::ComputeSystem>(*m_renderDevice);

    // CameraSystem twenty-first: stateless, no dependencies of its own —
    // grouped with the rest of engine/render.
    m_cameraSystem = std::make_unique<Render::CameraSystem>();
    // MeshRenderer twenty-second: stateless, no dependencies of its own —
    // grouped with the rest of engine/render.
    m_meshRenderer = std::make_unique<Render::MeshRenderer>();

    // LightSystem twenty-third: stateless, no dependencies of its own — grouped with the rest
    // of engine/render, constructed right before Renderer3D since Renderer3D's constructor
    // needs it.
    m_lightSystem = std::make_unique<Render::LightSystem>();

    // Renderer3D twenty-fourth: needs RenderDevice, RenderSystem, MeshRenderer, CameraSystem,
    // LightSystem, ShaderManager (Increment 26 — compiles the built-in shadow-depth program),
    // AssetManager, AssetDatabase, EventSystem, EngineConfig::ambientColor, and
    // EngineConfig::shadowMapResolution/shadowMapHalfExtent/shadowMapNearPlane/
    // shadowMapFarPlane — every one of which already exists by this point (ShaderManager is
    // constructed twentieth, above). Its offscreen render target is fixed at
    // EngineConfig::renderTargetWidth/Height for this EngineCore's lifetime, entirely
    // independent of WindowManager/the real window size — Renderer3D never renders into the
    // window itself (see docs/CODING_STANDARDS.md's rendering-evolution roadmap for how these two
    // eventually connect).
    m_renderer3D = MakeRenderer3D(*m_meshRenderer, *m_cameraSystem, *m_lightSystem, *m_eventSystem);

    // CollisionSystem twenty-fifth: stateless, no dependencies of its own.
    m_collisionSystem = std::make_unique<Physics::CollisionSystem>();

    // PhysicsConstraintSystem twenty-sixth: EngineCore owns the bounded registry so constraints
    // added through EngineServices participate in the normal fixed-step runtime. It is constructed
    // before PhysicsSystem and reset after it, preventing the PhysicsSystem's non-owning attachment
    // from ever outliving the registry.
    m_physicsConstraintSystem = std::make_unique<Physics::PhysicsConstraintSystem>();

    // PhysicsSystem twenty-seventh: needs CollisionSystem and EngineConfig::gravity. Attach the
    // EngineCore-owned constraint registry before publishing the system through EngineServices.
    auto physicsSystem = std::make_unique<Physics::PhysicsSystem>(*m_collisionSystem, m_config.gravity);
    physicsSystem->SetConstraintSystem(m_physicsConstraintSystem.get());
    // Island-parallel contact resolution borrows the engine's pool, the same way the asset pipeline
    // and the particle runtime do. It changes how long a step takes, never what it computes: the
    // islands share no bodies, so their solve order cannot be observed in the result.
    physicsSystem->SetThreadPool(m_threadPool.get());
    const auto isSettingCustomized = [&settings](const std::string_view settingId) noexcept {
        const auto resolved = settings.Resolve(settingId);
        return resolved.has_value() && resolved->source != Config::SettingValueSource::EngineDefault;
    };
    const auto syncPhysicsSimulationSettings =
        [this](Physics::IPhysicsSystem& targetPhysics, const bool forceApplyAll) noexcept {
            const EngineConfig defaultConfig{};
            Physics::PhysicsSimulationSettings simSettings = targetPhysics.GetSimulationSettings();
            if (forceApplyAll || m_config.defaultLinearDamping != defaultConfig.defaultLinearDamping ||
                m_projectSettings.IsModified("physics.common.defaultLinearDamping")) {
                simSettings.defaultLinearDamping =
                    static_cast<float>(std::max(0.0, m_config.defaultLinearDamping));
            }
            if (forceApplyAll || m_config.defaultAngularDamping != defaultConfig.defaultAngularDamping ||
                m_projectSettings.IsModified("physics.common.defaultAngularDamping")) {
                simSettings.defaultAngularDamping =
                    static_cast<float>(std::max(0.0, m_config.defaultAngularDamping));
            }
            if (forceApplyAll || m_config.sleepThreshold != defaultConfig.sleepThreshold ||
                m_config.allowSleepingDefault != defaultConfig.allowSleepingDefault ||
                m_projectSettings.IsModified("physics.common.sleepThreshold") ||
                m_projectSettings.IsModified("physics.common.allowSleepingDefault")) {
                simSettings.sleepThreshold = static_cast<float>(std::max(0.0, m_config.sleepThreshold));
                simSettings.allowSleeping = m_config.allowSleepingDefault;
            }
            if (forceApplyAll || m_config.physicsPositionIterations != defaultConfig.physicsPositionIterations ||
                m_projectSettings.IsModified("physics.solver.positionIterations")) {
                simSettings.solverIterations =
                    static_cast<std::size_t>(std::max<std::int64_t>(1, m_config.physicsPositionIterations));
            }
            if (forceApplyAll || m_config.physicsPenetrationSlop != defaultConfig.physicsPenetrationSlop ||
                m_projectSettings.IsModified("physics.contacts.penetrationSlop")) {
                simSettings.penetrationSlop =
                    static_cast<float>(std::max(0.0, m_config.physicsPenetrationSlop));
            }
            if (forceApplyAll ||
                m_config.physicsMaxDepenetrationVelocity != defaultConfig.physicsMaxDepenetrationVelocity ||
                m_projectSettings.IsModified("physics.contacts.maxDepenetrationVelocity")) {
                simSettings.maxDepenetrationVelocity =
                    static_cast<float>(std::max(0.0, m_config.physicsMaxDepenetrationVelocity));
            }
            targetPhysics.SetGravity(m_config.gravity);
            targetPhysics.SetSimulationSettings(simSettings);
        };
    const bool anyPhysicsSettingCustomized =
        isSettingCustomized("physics.common.defaultLinearDamping") ||
        isSettingCustomized("physics.common.defaultAngularDamping") ||
        isSettingCustomized("physics.common.sleepThreshold") ||
        isSettingCustomized("physics.common.allowSleepingDefault") ||
        isSettingCustomized("physics.solver.positionIterations") ||
        isSettingCustomized("physics.contacts.penetrationSlop") ||
        isSettingCustomized("physics.contacts.maxDepenetrationVelocity");
    syncPhysicsSimulationSettings(*physicsSystem, anyPhysicsSettingCustomized);
    {
        Physics::PhysicsSimulationSettings simSettings = physicsSystem->GetSimulationSettings();
        // On by default here, off by default in the struct: the engine always owns a pool, and a
        // tool that constructs a PhysicsSystem directly usually does not. The step falls back to
        // single-threaded execution on its own when the pool has one worker or the scene is small.
        simSettings.parallelStepping.enabled = true;
        physicsSystem->SetSimulationSettings(simSettings);
    }
    m_physicsSystem = std::move(physicsSystem);

    // PhysicsQuerySystem twenty-eighth: stateless façade over existing shape-cast, overlap, and
    // caller-owned character-controller authorities. It borrows CollisionSystem only for controller
    // commands and owns no ECS or scene state.
    m_physicsQuerySystem = std::make_unique<Physics::PhysicsQuerySystem>(*m_collisionSystem);

    // RaycastSystem twenty-ninth: stateless, no dependencies of its own.
    m_raycastSystem = std::make_unique<Physics::RaycastSystem>();

    // ParticleRuntime fifteenth: owns only bounded authored-emitter runtime state; ECS remains
    // authoritative and EngineCore reconciles it before simulation/render extraction.
    m_particleRuntime = std::make_unique<Scene::ParticleRuntime>();
    m_vfxWorld = std::make_unique<VFX::VfxWorld>();
    if (!m_config.projectRootDirectory.empty()) {
        m_vfxWorld->SetProjectRoot(m_config.projectRootDirectory);
    }

    // NavigationRuntime: owns the baked navmesh per region entity and one steering state per agent
    // entity. Nothing else in the engine keeps a navmesh, so a rebuild is this object's business.
    m_navigationRuntime = std::make_unique<Navigation::NavigationRuntime>();
    {
        Navigation::NavigationRuntimeConfig navRuntimeConfig{};
        navRuntimeConfig.globalAvoidanceEnabled = m_config.navAvoidanceEnabled;
        navRuntimeConfig.maxAvoidanceNeighbors =
            static_cast<std::size_t>(std::max<std::int64_t>(1, m_config.navMaxNeighbors));
        navRuntimeConfig.maxNeighborDistanceMetres =
            static_cast<float>(std::max(0.1, m_config.navNeighborDistance));
        m_navigationRuntime->SetRuntimeConfig(navRuntimeConfig);
    }

    // GamepadInputSystem thirtieth: owns only bounded injectable current/previous snapshots.
    m_gamepadInputSystem = std::make_unique<Input::GamepadInputSystem>(
        static_cast<float>(m_config.inputGamepadDeadzoneInner));

    // MobileInputSystem thirty-first: owns only bounded injectable touch/gyroscope snapshots.
    m_mobileInputSystem = std::make_unique<Input::MobileInputSystem>();

    // MobileGestureSystem thirty-second: borrows MobileInput snapshots and retains only its
    // bounded recognizer state/latest copied report.
    m_mobileGestureSystem = std::make_unique<Input::MobileGestureSystem>(*m_mobileInputSystem);

    // InputSystem thirty-third: needs EventSystem and the already-composed gamepad snapshot service
    // to queue action events and evaluate keyboard/mouse/gamepad bindings.
    m_inputSystem = std::make_unique<Input::InputSystem>(*m_eventSystem, m_gamepadInputSystem.get());
    // The project's actions, so gameplay asks for "jump" and the project decides what jump is.
    if (!m_inputMap.Load(m_config.inputMapFilePath)) {
        UVE_WARNING("EngineCore: input map \"{}\" could not be read; no project actions are registered",
                    m_config.inputMapFilePath.string());
    }
    Gameplay::RegisterDefaultGameplayActions(*m_inputSystem);
    m_inputMap.Apply(*m_inputSystem);
    m_windowManager->AttachInputSystem(m_inputSystem.get());

    // AudioDevice twenty-ninth: no dependencies of its own. Lightweight UI hosts (Hub/Setup) opt
    // into skipAudio to avoid opening an unused OS audio stream; otherwise prefers miniaudio
    // and falls back to NullAudioDevice when no audio output exists.
    if (m_config.skipAudio) {
        m_audioDevice = std::make_unique<Audio::NullAudioDevice>();
    } else {
        Audio::MiniaudioAudioDevice::Options audioDeviceOptions{};
        if (m_config.audioSampleRate >= 8000 && m_config.audioSampleRate <= 192000) {
            audioDeviceOptions.outputSampleRate = static_cast<std::uint32_t>(m_config.audioSampleRate);
        }
        if (auto miniaudioDevice = Audio::MiniaudioAudioDevice::Create(audioDeviceOptions)) {
            m_audioDevice = std::move(miniaudioDevice);
        } else {
            UVE_WARNING("EngineCore: no usable audio output device - falling back to NullAudioDevice "
                        "(all audio playback will be silent but functional)");
            m_audioDevice = std::make_unique<Audio::NullAudioDevice>();
        }
    }

    // AudioSystem thirtieth: needs AudioDevice (it pushes computed gain/position through it).
    m_audioSystem = std::make_unique<Audio::AudioSystem>(*m_audioDevice);
    // Bus filters and reverb delays are built for the project's rate; without this they would assume
    // the 48 kHz default and every cutoff would be off by the ratio between the two.
    static_cast<void>(m_audioSystem->SetMixerSampleRate(
        static_cast<float>(m_config.audioSampleRate)));
    const auto syncAudioBusVolume = [this](const std::string_view busName, const double volume,
                                              const bool configured) noexcept {
        if (!configured || m_audioSystem == nullptr) {
            return;
        }
        // The mixer's ceiling is 1: a value above it must still register the bus at full volume
        // instead of failing validation and silently leaving every source on Master.
        const float clampedVolume = static_cast<float>(std::clamp(volume, 0.0, 1.0));
        if (!m_audioSystem->SetMixerGroupVolume(busName, clampedVolume)) {
            static_cast<void>(m_audioSystem->RegisterMixerGroup(busName, clampedVolume, 1.0F));
        }
    };
    {
        syncAudioBusVolume("Master", m_config.audioMasterVolume, true);
        syncAudioBusVolume("Music", m_config.audioMusicVolume, true);
        syncAudioBusVolume("SFX", m_config.audioSfxVolume, true);
        syncAudioBusVolume("Voice", m_config.audioVoiceVolume, true);
    }

    // Physics-driven occlusion: the audio system declares what a blocker looks like and this
    // adapter answers with the raycast system, which was constructed before audio for exactly this
    // kind of wiring. With no provider installed the mixer is unchanged, so this is opt-in by
    // construction rather than by configuration.
    m_audioOcclusion = std::make_unique<Core::RaycastAudioOcclusion>(*m_entityManager, *m_raycastSystem);
    m_audioSystem->SetOcclusionProvider(m_audioOcclusion.get());

    // AudioSourceSystem thirty-first: stateful but takes no constructor dependencies —
    // EntityManager and AudioSystem are passed to Sync() per call, like
    // MeshRenderer::ExtractRenderQueue().
    m_audioSourceSystem = std::make_unique<Audio::AudioSourceSystem>();

    if (const auto fallbackLocale = Localization::TryParseLocale(m_config.localisationFallbackLocale)) {
        m_localizationService.SetFallbackLocale(*fallbackLocale);
    }
    if (const auto defaultLocale = Localization::TryParseLocale(m_config.localisationDefaultLocale)) {
        m_localizationService.SetActiveLocale(*defaultLocale);
    }

    // Subscribe live project settings observers so runtime edits to Physics, Audio, Navigation,
    // Input, and Localisation in m_projectSettings immediately propagate into the active subsystems.
    m_projectSettingsSubscriptions.clear();
    if (const auto sub = m_projectSettings.SubscribeToCategory(
            "Physics",
            [this, syncPhysicsSimulationSettings](const Config::SettingChangedEvent& event) {
                ApplyEngineProjectSettings(m_projectSettings, m_config);
                if (m_physicsSystem != nullptr) {
                    syncPhysicsSimulationSettings(*m_physicsSystem, true);
                }
                if (m_timer != nullptr) {
                    if (event.id == EngineProjectSettingId::kPhysicsTicksPerSecond) {
                        m_timer->SetFixedTimestep(
                            m_config.fixedUpdateFps > 0.0 ? (1.0 / m_config.fixedUpdateFps) : 0.0);
                    } else if (event.id == EngineProjectSettingId::kPhysicsMaxFrameTime) {
                        m_timer->SetMaxDeltaTime(m_config.maxDeltaTimeSeconds);
                    } else if (event.id == EngineProjectSettingId::kPhysicsMaxStepsPerFrame) {
                        m_timer->SetMaxStepsPerTick(m_config.maxFixedStepsPerFrame);
                    }
                }
            });
        sub.IsValid()) {
        m_projectSettingsSubscriptions.push_back(sub);
    }
    if (const auto sub = m_projectSettings.SubscribeToCategory(
            "Audio",
            [this, syncAudioBusVolume](const Config::SettingChangedEvent& event) {
                ApplyEngineProjectSettings(m_projectSettings, m_config);
                if (event.id == "audio.buses.masterVolume") {
                    syncAudioBusVolume("Master", m_config.audioMasterVolume, true);
                } else if (event.id == "audio.buses.musicVolume") {
                    syncAudioBusVolume("Music", m_config.audioMusicVolume, true);
                } else if (event.id == "audio.buses.sfxVolume") {
                    syncAudioBusVolume("SFX", m_config.audioSfxVolume, true);
                } else if (event.id == "audio.buses.voiceVolume") {
                    syncAudioBusVolume("Voice", m_config.audioVoiceVolume, true);
                }
            });
        sub.IsValid()) {
        m_projectSettingsSubscriptions.push_back(sub);
    }
    if (const auto sub = m_projectSettings.SubscribeToCategory(
            "Navigation",
            [this](const Config::SettingChangedEvent&) {
                ApplyEngineProjectSettings(m_projectSettings, m_config);
                if (m_navigationRuntime != nullptr) {
                    Navigation::NavigationRuntimeConfig navRuntimeConfig =
                        m_navigationRuntime->GetRuntimeConfig();
                    navRuntimeConfig.globalAvoidanceEnabled = m_config.navAvoidanceEnabled;
                    navRuntimeConfig.maxAvoidanceNeighbors =
                        static_cast<std::size_t>(std::max<std::int64_t>(1, m_config.navMaxNeighbors));
                    navRuntimeConfig.maxNeighborDistanceMetres =
                        static_cast<float>(std::max(0.1, m_config.navNeighborDistance));
                    m_navigationRuntime->SetRuntimeConfig(navRuntimeConfig);
                }
            });
        sub.IsValid()) {
        m_projectSettingsSubscriptions.push_back(sub);
    }
    if (const auto sub = m_projectSettings.SubscribeToCategory(
            "Input",
            [this](const Config::SettingChangedEvent&) {
                ApplyEngineProjectSettings(m_projectSettings, m_config);
                if (m_gamepadInputSystem != nullptr) {
                    m_gamepadInputSystem->SetDeadZone(
                        static_cast<float>(m_config.inputGamepadDeadzoneInner));
                }
            });
        sub.IsValid()) {
        m_projectSettingsSubscriptions.push_back(sub);
    }
    if (const auto sub = m_projectSettings.SubscribeToCategory(
            "Localisation",
            [this](const Config::SettingChangedEvent& event) {
                ApplyEngineProjectSettings(m_projectSettings, m_config);
                if (event.id == "localisation.fallbackLocale") {
                    if (const auto fallbackLocale =
                            Localization::TryParseLocale(m_config.localisationFallbackLocale)) {
                        m_localizationService.SetFallbackLocale(*fallbackLocale);
                    }
                } else if (event.id == "localisation.defaultLocale") {
                    if (const auto defaultLocale =
                            Localization::TryParseLocale(m_config.localisationDefaultLocale)) {
                        m_localizationService.SetActiveLocale(*defaultLocale);
                    }
                }
            });
        sub.IsValid()) {
        m_projectSettingsSubscriptions.push_back(sub);
    }
    if (const auto sub = m_projectSettings.SubscribeToCategory(
            "Quality",
            [this](const Config::SettingChangedEvent& event) {
                ApplyEngineProjectSettings(m_projectSettings, m_config);
                if (m_renderer3D != nullptr && event.id == "quality.activePreset") {
                    m_renderer3D->SetScalabilityMode(
                        static_cast<std::int32_t>(m_config.activeQualityTier));
                }
            });
        sub.IsValid()) {
        m_projectSettingsSubscriptions.push_back(sub);
    }

    // SaveGameSystem thirty-second: needs SceneSerializer (composed by reference) and
    // EngineConfig::saveDirectoryPath.
    m_saveGameSystem = std::make_unique<Save::SaveGameSystem>(*m_sceneSerializer, m_config.saveDirectoryPath);

    // CheckpointManager thirty-third: needs SaveGameSystem (composed by reference) and
    // EngineConfig::autoSaveIntervalSeconds. Preserve the canonical engine version in
    // checkpoint metadata; the checkpoint manager overlays playtime for each save.
    const Version engineVersion = GetEngineVersion();
    Save::GameStateMetadata checkpointMetadata;
    checkpointMetadata.engineVersionMajor = engineVersion.major;
    checkpointMetadata.engineVersionMinor = engineVersion.minor;
    checkpointMetadata.engineVersionPatch = engineVersion.patch;
    checkpointMetadata.engineVersionBuild = engineVersion.build;
    m_checkpointManager = std::make_unique<Save::CheckpointManager>(
        *m_saveGameSystem, m_config.autoSaveIntervalSeconds, checkpointMetadata);

    m_services.emplace(*m_logger, *m_timer, *m_eventSystem, *m_memoryManager, *m_threadPool,
                                                 *m_commandLine, *m_configManager, m_projectSettings, m_inputMap, *m_entityManager, *m_sceneGraph,
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
    m_services->SetVfxWorld(m_vfxWorld.get());

    TransitionState(EngineState::Running);
    UVE_INFO("EngineCore: initialized");
}

bool EngineCore::Load() {
    if (m_windowCreationFailed) {
        UVE_FATAL("EngineCore: Load() aborting - window/GL context creation failed during Init()");
        return false;
    }
    UVE_INFO("EngineCore: Load() - nothing to load this increment");
    return true;
}

void EngineCore::BeginFrame() {
    m_frameStartTime = std::chrono::steady_clock::now();
    m_timer->Tick();
    ++m_frameStats.frameNumber;
    m_frameStats.deltaTimeSeconds = m_timer->GetDeltaTime();
    m_frameStats.totalTimeSeconds = m_timer->GetTotalTime();
    UVE_TRACE("BeginFrame {}", m_frameStats.frameNumber);
}

void EngineCore::SyncDecal3DObjects(const float simulatedDeltaSeconds) {
    // One pass, one edge per decal that runs out. The component keeps the flag (so the renderer
    // skips it before touching any geometry) and the event is queued once, because
    // AdvanceDecal3DLifetime reports the crossing rather than the state - a decal that is
    // already expired returns false forever after, so a listener cannot see the same expiry twice.
    m_entityManager->ForEach<Scene::Decal3DComponent>(
        [this, simulatedDeltaSeconds](const Scene::Entity entity, Scene::Decal3DComponent& decal) {
            // The walk hands out the live component, so the countdown is written in place: no
            // second lookup that could disagree with what the walk just decided to visit.
            if (Scene::AdvanceDecal3DLifetime(decal, simulatedDeltaSeconds)) {
                m_eventSystem->QueueEvent(Scene::Decal3DExpiredEvent{entity, decal.materialAssetPath});
            }
        });
}

void EngineCore::SyncParticleRuntime() {
    if (m_particleRuntime == nullptr) {
        return;
    }

    std::vector<Scene::Entity> authoredEmitters;
    std::unordered_set<Scene::Entity> authoredEmitterSet;
    const bool simulationPaused = m_simulationExecutionMode == SimulationExecutionMode::Paused;
    const Scene::ParticleRuntimeSnapshot initialSnapshot = m_particleRuntime->GetSnapshot();
    std::unordered_map<Scene::Entity, std::uint32_t> existingBudgets;
    existingBudgets.reserve(initialSnapshot.instances.size());
    for (const Scene::ParticleRuntimeInstanceSnapshot& instance : initialSnapshot.instances) {
        existingBudgets.emplace(instance.entity, instance.maxParticles);
    }
    m_entityManager->ForEach<Scene::ParticleEmitterComponent>(
        [this, &authoredEmitters, &authoredEmitterSet, &existingBudgets, simulationPaused](
            const Scene::Entity entity, const Scene::ParticleEmitterComponent& component) {
            if (!component.systemAssetPath.empty()) {
                return;
            }
            authoredEmitters.push_back(entity);
            authoredEmitterSet.insert(entity);
            const auto existingIt = existingBudgets.find(entity);
            const bool exists = existingIt != existingBudgets.end();
            const bool budgetMatches = exists && existingIt->second == component.maxParticles;
            if (!exists) {
                static_cast<void>(m_particleRuntime->AttachDetailed(entity, component));
            } else if (!budgetMatches) {
                static_cast<void>(m_particleRuntime->DetachDetailed(entity));
                m_particleEmitRemainder.erase(entity);
                static_cast<void>(m_particleRuntime->AttachDetailed(entity, component));
            }
            // Same rule as scripts: a Running emitter freezes mid-flight while the simulation is
            // paused, rather than continuing to integrate behind a paused game.
            static_cast<void>(m_particleRuntime->SetEnabledDetailed(
                entity, Scene::IsTicking(ResolvedTickMode(*m_sceneGraph, entity), simulationPaused)));
            // Thread Group: only an emitter resolved to Sub Thread is simulated on a worker. The
            // default is Main Thread, so nothing leaves the main thread unless an author put it
            // there - and a Main Thread ancestor keeps its whole subtree here.
            const std::optional<Scene::ResolvedObjectModes> modes = m_sceneGraph->TryGetResolvedObjectModes(entity);
            static_cast<void>(m_particleRuntime->SetWorkerEligibleDetailed(
                entity, modes.has_value() && modes->threadGroup == Scene::ThreadGroupMode::SubThread));
        });

    for (const Scene::ParticleRuntimeInstanceSnapshot& instance : initialSnapshot.instances) {
        if (!m_entityManager->IsAlive(instance.entity) || !authoredEmitterSet.contains(instance.entity)) {
            static_cast<void>(m_particleRuntime->DetachDetailed(instance.entity));
            m_particleEmitRemainder.erase(instance.entity);
        }
    }

    const float deltaSeconds = static_cast<float>(m_timer->GetDeltaTime());
    if (deltaSeconds > 0.0F) {
        if (!authoredEmitters.empty()) {
            const Scene::ParticleRuntimeSnapshot emitSnapshot = m_particleRuntime->GetSnapshot();
            std::unordered_map<Scene::Entity, const Scene::ParticleRuntimeInstanceSnapshot*> emitMap;
            emitMap.reserve(emitSnapshot.instances.size());
            for (const Scene::ParticleRuntimeInstanceSnapshot& instance : emitSnapshot.instances) {
                emitMap.emplace(instance.entity, &instance);
            }
            for (const Scene::Entity entity : authoredEmitters) {
                if (!m_entityManager->IsAlive(entity) ||
                    !m_entityManager->HasComponent<Scene::ParticleEmitterComponent>(entity)) {
                    m_particleEmitRemainder.erase(entity);
                    continue;
                }
                const Scene::ParticleEmitterComponent& component =
                    m_entityManager->GetComponent<Scene::ParticleEmitterComponent>(entity);
                if (!Scene::IsParticleEmitterComponentValid(component)) {
                    m_particleEmitRemainder.erase(entity);
                    continue;
                }
                const bool ticking =
                    Scene::IsTicking(ResolvedTickMode(*m_sceneGraph, entity), simulationPaused);
                if (!ticking || !Scene::ParticleEmitter3D::IsEmitting(component)) {
                    m_particleEmitRemainder.erase(entity);
                    continue;
                }
                if (!m_entityManager->HasComponent<Scene::WorldTransformComponent>(entity)) {
                    m_particleEmitRemainder.erase(entity);
                    continue;
                }
                const Scene::WorldTransformComponent& worldTransform =
                    m_entityManager->GetComponent<Scene::WorldTransformComponent>(entity);
                if (worldTransform.dirty || !Math::IsFinite(worldTransform.worldPosition)) {
                    continue;
                }

                const auto instIt = emitMap.find(entity);
                if (instIt == emitMap.end() || !instIt->second->enabled) {
                    m_particleEmitRemainder.erase(entity);
                    continue;
                }
                const std::uint32_t liveParticles = instIt->second->liveParticles;

                const std::uint32_t count = Scene::ParticleEmitter3D::AccumulateSpawnCount(
                    component, deltaSeconds, m_particleEmitRemainder[entity], liveParticles);
                if (count == 0U) {
                    continue;
                }
                static_cast<void>(m_particleRuntime->EmitDetailed(
                    entity, Scene::ParticleEmission{count, worldTransform.worldPosition, Math::Vector3{},
                                                       component.lifetimeSeconds}));
            }
        }
        static_cast<void>(m_particleRuntime->SimulateDetailed(deltaSeconds, m_config.gravity, m_threadPool.get()));
    }
}

void EngineCore::SyncVfxRuntime() {
    if (m_vfxWorld == nullptr) {
        return;
    }

    if (!m_config.projectRootDirectory.empty() &&
        m_vfxWorld->GetProjectRoot() != m_config.projectRootDirectory) {
        m_vfxWorld->SetProjectRoot(m_config.projectRootDirectory);
    }

    const bool simulationPaused = m_simulationExecutionMode == SimulationExecutionMode::Paused;
    const float rawDeltaSeconds = m_timer != nullptr ? static_cast<float>(m_timer->GetDeltaTime()) : 0.0F;
    const float scaledDeltaSeconds =
        simulationPaused ? 0.0F : std::max(0.0F, rawDeltaSeconds * m_simulationSpeedScale);

    std::unordered_set<Scene::Entity> activeVfxEntities;
    m_entityManager->ForEach<Scene::ParticleEmitterComponent>(
        [this, &activeVfxEntities, scaledDeltaSeconds](const Scene::Entity entity,
                                                       const Scene::ParticleEmitterComponent& component) {
            if (component.systemAssetPath.empty() || !Scene::IsParticleEmitterComponentValid(component)) {
                return;
            }
            activeVfxEntities.insert(entity);

            VfxEntityInstanceState& state = m_vfxEntityInstances[entity];
            VFX::VfxOwner owner{};
            owner.qualityLevel = m_vfxWorld->GetQualityLevel();
            if (m_entityManager->HasComponent<Scene::WorldTransformComponent>(entity)) {
                const Scene::WorldTransformComponent& world =
                    m_entityManager->GetComponent<Scene::WorldTransformComponent>(entity);
                if (!world.dirty && Math::IsFinite(world.worldPosition)) {
                    owner.position = world.worldPosition;
                    Math::Quaternion rot = world.worldRotation;
                    if (!Math::TryNormalize(rot, rot)) {
                        rot = Math::Quaternion{};
                    }
                    owner.rotation = rot;
                    if (state.hasLastWorldPosition && scaledDeltaSeconds > 1e-6F) {
                        owner.velocity =
                            (world.worldPosition - state.lastWorldPosition) * (1.0F / scaledDeltaSeconds);
                    }
                    state.lastWorldPosition = world.worldPosition;
                    state.hasLastWorldPosition = true;
                }
            }

            const bool assetOrSeedChanged =
                state.systemAssetPath != component.systemAssetPath || state.seed != component.seed;
            if (assetOrSeedChanged && state.handle.IsValid()) {
                static_cast<void>(m_vfxWorld->Kill(state.handle));
                state.handle = VFX::kInvalidVfxHandle;
                state.wasEmitting = false;
            }
            state.systemAssetPath = component.systemAssetPath;
            state.seed = component.seed;

            const bool shouldEmit = component.emitting && component.autoPlay;
            const std::uint64_t attachKey =
                (static_cast<std::uint64_t>(entity.generation) << 32ULL) |
                static_cast<std::uint64_t>(entity.index);

            if (shouldEmit) {
                if (!state.handle.IsValid() || !m_vfxWorld->IsAlive(state.handle) || !state.wasEmitting) {
                    if (state.handle.IsValid() && m_vfxWorld->IsAlive(state.handle)) {
                        static_cast<void>(m_vfxWorld->Kill(state.handle));
                    }
                    state.handle =
                        m_vfxWorld->Spawn(component.systemAssetPath, owner, attachKey, component.seed);
                } else {
                    static_cast<void>(m_vfxWorld->SetOwner(state.handle, owner));
                }
                state.wasEmitting = true;
            } else {
                if (state.handle.IsValid() && m_vfxWorld->IsAlive(state.handle)) {
                    if (state.wasEmitting) {
                        static_cast<void>(m_vfxWorld->Stop(state.handle));
                    }
                    if (m_vfxWorld->IsAlive(state.handle)) {
                        static_cast<void>(m_vfxWorld->SetOwner(state.handle, owner));
                    }
                }
                state.wasEmitting = false;
            }
        });

    for (auto it = m_vfxEntityInstances.begin(); it != m_vfxEntityInstances.end();) {
        if (!activeVfxEntities.contains(it->first)) {
            if (it->second.handle.IsValid()) {
                static_cast<void>(m_vfxWorld->Kill(it->second.handle));
            }
            it = m_vfxEntityInstances.erase(it);
        } else {
            ++it;
        }
    }

    if (m_config.hotReloadEnabled) {
        static_cast<void>(m_vfxWorld->PollHotReload());
    }

    if (!simulationPaused && scaledDeltaSeconds > 0.0F) {
        VFX::VfxFrameView frameView{};
        if (m_activeCamera != Scene::kInvalidEntity &&
            m_entityManager->IsAlive(m_activeCamera) &&
            m_entityManager->HasComponent<Scene::CameraComponent>(m_activeCamera) &&
            m_entityManager->HasComponent<Scene::WorldTransformComponent>(m_activeCamera)) {
            const Scene::CameraComponent& camComp =
                m_entityManager->GetComponent<Scene::CameraComponent>(m_activeCamera);
            const Scene::WorldTransformComponent& camWorld =
                m_entityManager->GetComponent<Scene::WorldTransformComponent>(m_activeCamera);
            if (!camWorld.dirty && Math::IsFinite(camWorld.worldPosition)) {
                frameView.hasCamera = true;
                frameView.cameraPosition = camWorld.worldPosition;
                frameView.frustum = Math::Frustum::FromViewProjection(
                    Scene::Camera3D::BuildViewProjectionMatrix(camComp, camWorld));
            }
        }
        m_vfxWorld->Tick(scaledDeltaSeconds, frameView);
    }
}

void EngineCore::SyncUIRuntime() {
    if (m_inputSystem == nullptr) {
        return;
    }

    UI::UICoordinateTransform uiTransform;
    std::uint32_t anchorFramebufferWidth = 0U;
    std::uint32_t anchorFramebufferHeight = 0U;
    float anchorContentScaleX = 1.0F;
    float anchorContentScaleY = 1.0F;
    if (m_windowedRenderingActive && m_windowManager != nullptr) {
        const std::uint32_t framebufferWidth = m_windowManager->GetWidth();
        const std::uint32_t framebufferHeight = m_windowManager->GetHeight();
        anchorFramebufferWidth = framebufferWidth;
        anchorFramebufferHeight = framebufferHeight;
        if (framebufferWidth > 0U && framebufferHeight > 0U) {
            float contentScaleX = 1.0F;
            float contentScaleY = 1.0F;
            m_windowManager->GetContentScale(contentScaleX, contentScaleY);
            if (!(contentScaleX > 0.0F) || !std::isfinite(contentScaleX)) {
                contentScaleX = 1.0F;
            }
            if (!(contentScaleY > 0.0F) || !std::isfinite(contentScaleY)) {
                contentScaleY = 1.0F;
            }
            anchorContentScaleX = contentScaleX;
            anchorContentScaleY = contentScaleY;

            std::optional<Render::ViewportRect> outputRegion = m_editorViewportRegion;
            Window::PresentationLayout viewportLayout;
            bool hasProjectViewportLayout = false;
            if (!outputRegion.has_value() && m_config.stretchMode == Platform::StretchMode::Viewport) {
                viewportLayout = ComputeWindowPresentationLayout(m_config, framebufferWidth, framebufferHeight);
                hasProjectViewportLayout = true;
                if (viewportLayout.width > 0U && viewportLayout.height > 0U &&
                    (viewportLayout.x != 0U || viewportLayout.y != 0U ||
                     viewportLayout.width != framebufferWidth || viewportLayout.height != framebufferHeight)) {
                    outputRegion = Render::ViewportRect{
                        Math::Vector2i{static_cast<std::int32_t>(viewportLayout.x),
                                          static_cast<std::int32_t>(viewportLayout.y)},
                        Math::Vector2i{static_cast<std::int32_t>(viewportLayout.width),
                                          static_cast<std::int32_t>(viewportLayout.height)}};
                }
            }

            std::uint32_t projectionWidth = 0U;
            std::uint32_t projectionHeight = 0U;
            if (outputRegion.has_value()) {
                projectionWidth = static_cast<std::uint32_t>(outputRegion->size.x);
                projectionHeight = static_cast<std::uint32_t>(outputRegion->size.y);
            } else {
                const std::uint32_t drawableWidth =
                    hasProjectViewportLayout && viewportLayout.width > 0U ? viewportLayout.width : framebufferWidth;
                const std::uint32_t drawableHeight =
                    hasProjectViewportLayout && viewportLayout.height > 0U ? viewportLayout.height : framebufferHeight;
                const Window::AdaptiveRenderResolution renderSize =
                    Window::ComputeAdaptiveRenderResolution(drawableWidth, drawableHeight,
                                                               kAdaptiveRenderResolutionLimits,
                                                               m_currentRenderResolutionScale);
                projectionWidth = renderSize.width;
                projectionHeight = renderSize.height;
            }

            if (projectionWidth > 0U && projectionHeight > 0U) {
                const float projectionScaleX = outputRegion.has_value()
                                                   ? 1.0F
                                                   : static_cast<float>(projectionWidth) /
                                                         static_cast<float>(framebufferWidth);
                const float projectionScaleY = outputRegion.has_value()
                                                   ? 1.0F
                                                   : static_cast<float>(projectionHeight) /
                                                         static_cast<float>(framebufferHeight);
                const float inputScaleX = contentScaleX * projectionScaleX;
                const float inputScaleY = contentScaleY * projectionScaleY;
                const float inputOffsetX =
                    outputRegion.has_value() ? static_cast<float>(outputRegion->position.x) : 0.0F;
                const float inputOffsetY =
                    outputRegion.has_value() ? static_cast<float>(outputRegion->position.y) : 0.0F;

                switch (m_config.stretchMode) {
                case Platform::StretchMode::Disabled:
                    uiTransform.scaleX = inputScaleX;
                    uiTransform.scaleY = inputScaleY;
                    uiTransform.inputScaleX = inputScaleX;
                    uiTransform.inputScaleY = inputScaleY;
                    uiTransform.inputOffsetX = inputOffsetX;
                    uiTransform.inputOffsetY = inputOffsetY;
                    break;
                case Platform::StretchMode::CanvasItems: {
                    const Window::PresentationLayout canvasLayout = Window::ComputePresentationLayout(
                        projectionWidth, projectionHeight, m_config.windowWidth, m_config.windowHeight,
                        m_config.aspectPolicy, m_config.integerOnlyScaling);
                    uiTransform.scaleX = static_cast<float>(canvasLayout.scaleX);
                    uiTransform.scaleY = static_cast<float>(canvasLayout.scaleY);
                    uiTransform.offsetX = static_cast<float>(canvasLayout.x);
                    uiTransform.offsetY = static_cast<float>(canvasLayout.y);
                    uiTransform.inputScaleX = inputScaleX;
                    uiTransform.inputScaleY = inputScaleY;
                    uiTransform.inputOffsetX = inputOffsetX;
                    uiTransform.inputOffsetY = inputOffsetY;
                    if (!canvasLayout.integerScaleAvailable && !m_integerScaleFallbackLogged) {
                        UVE_WARNING("EngineCore: integer-only canvas scaling cannot fit below the reference "
                                    "resolution; using fractional aspect-preserving scaling");
                        m_integerScaleFallbackLogged = true;
                    }
                    break;
                }
                case Platform::StretchMode::Viewport:
                    if (outputRegion.has_value()) {
                        if (hasProjectViewportLayout && !m_editorViewportRegion.has_value()) {
                            uiTransform.scaleX = static_cast<float>(viewportLayout.scaleX);
                            uiTransform.scaleY = static_cast<float>(viewportLayout.scaleY);
                        } else {
                            const Window::PresentationLayout viewportCanvasLayout =
                                Window::ComputePresentationLayout(
                                    projectionWidth, projectionHeight, m_config.windowWidth, m_config.windowHeight,
                                    m_config.aspectPolicy, m_config.integerOnlyScaling);
                            uiTransform.scaleX = static_cast<float>(viewportCanvasLayout.scaleX);
                            uiTransform.scaleY = static_cast<float>(viewportCanvasLayout.scaleY);
                            uiTransform.offsetX = static_cast<float>(viewportCanvasLayout.x);
                            uiTransform.offsetY = static_cast<float>(viewportCanvasLayout.y);
                        }
                    } else if (hasProjectViewportLayout) {
                        uiTransform.scaleX = static_cast<float>(viewportLayout.scaleX) * projectionScaleX;
                        uiTransform.scaleY = static_cast<float>(viewportLayout.scaleY) * projectionScaleY;
                        uiTransform.offsetX = static_cast<float>(viewportLayout.x) * projectionScaleX;
                        uiTransform.offsetY = static_cast<float>(viewportLayout.y) * projectionScaleY;
                    }
                    uiTransform.inputScaleX = inputScaleX;
                    uiTransform.inputScaleY = inputScaleY;
                    uiTransform.inputOffsetX = inputOffsetX;
                    uiTransform.inputOffsetY = inputOffsetY;
                    break;
                }
            }
        }
    }
    // The authored-space size roots anchor against, from the same metrics as the transform so
    // the two can never disagree about which space is which: the reference resolution whenever a
    // stretch mode scales authored pixels onto the target (a shrunken editor viewport region
    // still resolves against the full design space), device points when stretching is off and
    // authored pixels are device pixels. Headless frames set nothing and keep the last size.
    if (anchorFramebufferWidth > 0U && anchorFramebufferHeight > 0U) {
        Math::Vector2 anchorViewport{};
        if (m_config.stretchMode == Platform::StretchMode::Disabled) {
            anchorViewport = Math::Vector2{static_cast<float>(anchorFramebufferWidth) / anchorContentScaleX,
                                              static_cast<float>(anchorFramebufferHeight) / anchorContentScaleY};
        } else {
            anchorViewport = Math::Vector2{static_cast<float>(m_config.windowWidth),
                                              static_cast<float>(m_config.windowHeight)};
        }
        m_uiRuntime.SetViewportSize(anchorViewport);
    }
    if (m_uiPointerOrigin.has_value()) {
        uiTransform.inputOffsetX = m_uiPointerOrigin->x * uiTransform.inputScaleX;
        uiTransform.inputOffsetY = m_uiPointerOrigin->y * uiTransform.inputScaleY;
    }
    // Scale With Screen: a canvas laid out for a reference resolution is fitted to the screen it is
    // shown on - the editor's view, the window, a phone - whole and centred, so what the Designer
    // showed is what any player sees.
    if (const std::optional<Math::Vector2> reference = ScaledCanvasReference()) {
        Math::Vector2 target{0.0F, 0.0F};
        if (m_uiViewportSize.has_value()) {
            target = *m_uiViewportSize;
        } else if (m_windowManager != nullptr) {
            float contentX = 1.0F;
            float contentY = 1.0F;
            m_windowManager->GetContentScale(contentX, contentY);
            target = Math::Vector2{static_cast<float>(m_windowManager->GetWidth()) / std::max(contentX, 0.01F),
                                      static_cast<float>(m_windowManager->GetHeight()) / std::max(contentY, 0.01F)};
        }
        if (target.x > 0.0F && target.y > 0.0F && reference->x > 0.0F && reference->y > 0.0F) {
            const float fit = std::min(target.x / reference->x, target.y / reference->y);
            uiTransform.scaleX = fit;
            uiTransform.scaleY = fit;
            uiTransform.offsetX = (target.x - reference->x * fit) * 0.5F;
            uiTransform.offsetY = (target.y - reference->y * fit) * 0.5F;
            m_uiRuntime.SetViewportSize(*reference);
        }
    }
    if (std::isfinite(m_config.runtimeUiScale) && m_config.runtimeUiScale > 0.0 &&
        std::abs(m_config.runtimeUiScale - 1.0) > 1.0e-4) {
        const float uiScale = static_cast<float>(std::clamp(m_config.runtimeUiScale, 0.25, 4.0));
        uiTransform.scaleX *= uiScale;
        uiTransform.scaleY *= uiScale;
        const Math::Vector2 currentViewport = m_uiRuntime.GetViewportSize();
        if (currentViewport.x > 0.0F && currentViewport.y > 0.0F) {
            m_uiRuntime.SetViewportSize(Math::Vector2{currentViewport.x / uiScale, currentViewport.y / uiScale});
        }
    }
    m_uiRuntime.SetCoordinateTransform(uiTransform);

    // Whether a text is translated is its resolved Auto Translate mode, asked of the scene graph so
    // a label with no component of its own follows the menu it sits in. An entity the scene graph
    // has not resolved yet takes the hierarchy default, Always.
    const UI::UITextLocalization localization{
        &m_localizationService, [this](const Scene::Entity entity) {
            const std::optional<Scene::ResolvedObjectModes> modes = m_sceneGraph->TryGetResolvedObjectModes(entity);
            return !modes.has_value() || modes->autoTranslate == Scene::LocalizeMode::Localized;
        }};
    // UI tweens run on the real frame clock, ungated by pause: a pause menu must fade in while
    // the simulation stands still.
    if (m_timer != nullptr) {
        m_uiRuntime.SetDeltaTime(static_cast<float>(m_timer->GetDeltaTime()));
    }
    {
        // The stage turns on the real frame clock, like UI tweens: a menu keeps turning while paused.
        const auto now = std::chrono::steady_clock::now();
        const float dt = m_uiView3DClock.time_since_epoch().count() == 0
                             ? 0.0F
                             : std::chrono::duration<float>(now - m_uiView3DClock).count();
        m_uiView3DClock = now;
        SyncUIView3DStages(dt);
    }
    m_uiRuntime.Tick(*m_entityManager, *m_inputSystem, localization);
}

std::optional<Math::Vector2> EngineCore::ScaledCanvasReference() {
    // The first shown top-level Canvas that scales with the screen decides the fit.
    std::optional<Math::Vector2> reference;
    m_entityManager->ForEach<Scene::CanvasComponent>([&](const Scene::Entity entity, const Scene::CanvasComponent& canvas) {
        if (reference.has_value() || !canvas.visible || !canvas.scaleWithScreen ||
            canvas.renderMode != Scene::CanvasRenderMode::ScreenSpaceOverlay) {
            return;
        }
        if (m_entityManager->HasComponent<Scene::HierarchyComponent>(entity)) {
            const Scene::Entity parent = m_entityManager->GetComponent<Scene::HierarchyComponent>(entity).parent;
            if (parent != Scene::kInvalidEntity && m_entityManager->IsAlive(parent) &&
                m_entityManager->HasComponent<Scene::CanvasComponent>(parent)) {
                return;   // a screen inside a widget: its widget's root decides
            }
        }
        reference = canvas.referenceResolution.x > 0.0F && canvas.referenceResolution.y > 0.0F ? canvas.referenceResolution
                                                                                              : DeviceScreenSize();
    });
    return reference;
}

Math::Vector2 EngineCore::DeviceScreenSize() {
    // The device's own screen (the primary monitor), read once.
    if (!m_deviceScreenSize.has_value()) {
        Math::Vector2 size{1920.0F, 1080.0F};
        if (m_windowManager != nullptr) {
            for (const Window::MonitorInfo& monitor : m_windowManager->EnumerateMonitors()) {
                if (monitor.width > 0U && monitor.height > 0U) {
                    size = Math::Vector2{static_cast<float>(monitor.width), static_cast<float>(monitor.height)};
                    if (monitor.isPrimary) {
                        break;
                    }
                }
            }
        }
        m_deviceScreenSize = size;
    }
    return *m_deviceScreenSize;
}

Scene::Entity EngineCore::FindWidgetStage(Scene::Entity widget) const {
    // The widget's root Canvas (the topmost Canvas above it), then its child named Stage.
    Scene::Entity root = Scene::kInvalidEntity;
    for (int depth = 0; depth < 256 && m_entityManager->IsAlive(widget); ++depth) {
        if (m_entityManager->HasComponent<Scene::CanvasComponent>(widget)) {
            root = widget;
        }
        if (!m_entityManager->HasComponent<Scene::HierarchyComponent>(widget)) {
            break;
        }
        const Scene::Entity parent = m_entityManager->GetComponent<Scene::HierarchyComponent>(widget).parent;
        if (parent == widget) {
            break;
        }
        widget = parent;
    }
    if (root == Scene::kInvalidEntity) {
        return Scene::kInvalidEntity;
    }
    for (const Scene::Entity child : m_sceneGraph->GetChildren(*m_entityManager, root)) {
        if (m_entityManager->HasComponent<Scene::NameComponent>(child) &&
            m_entityManager->GetComponent<Scene::NameComponent>(child).name == Scene::kWidgetStageName) {
            return child;
        }
    }
    return Scene::kInvalidEntity;
}

Scene::Entity EngineCore::FindNamedUnder(const Scene::Entity root, const std::string_view name) const {
    std::vector<Scene::Entity> open{root};
    for (std::size_t i = 0U; i < open.size() && i < 4096U; ++i) {
        const Scene::Entity entity = open[i];
        if (entity != root && m_entityManager->HasComponent<Scene::NameComponent>(entity) &&
            m_entityManager->GetComponent<Scene::NameComponent>(entity).name == name) {
            return entity;
        }
        const std::vector<Scene::Entity> children = m_sceneGraph->GetChildren(*m_entityManager, entity);
        open.insert(open.end(), children.begin(), children.end());
    }
    return Scene::kInvalidEntity;
}

std::optional<std::pair<Math::Vector3, Math::Quaternion>> EngineCore::PoseUnder(Scene::Entity entity,
                                                                                           const Scene::Entity ancestor) const {
    // The entity's place in `ancestor`'s space: its local transforms composed up to it.
    Math::Vector3 position{0.0F, 0.0F, 0.0F};
    Math::Quaternion rotation{};
    for (int depth = 0; depth < 256; ++depth) {
        if (entity == ancestor) {
            return std::make_pair(position, rotation);
        }
        if (!m_entityManager->IsAlive(entity) || !m_entityManager->HasComponent<Scene::TransformComponent>(entity)) {
            return std::nullopt;
        }
        const Scene::TransformComponent& local = m_entityManager->GetComponent<Scene::TransformComponent>(entity);
        Math::Quaternion localRotation = local.localRotation;
        if (local.rotationEditMode == Scene::RotationEditMode::Euler) {
            static_cast<void>(Math::TryMakeEuler(local.localEulerRadians, localRotation));
        }
        const Math::Vector3 scaled{position.x * local.localScale.x, position.y * local.localScale.y, position.z * local.localScale.z};
        position = Math::RotateVector(localRotation, scaled) + local.localPosition;
        rotation = Math::Multiply(localRotation, rotation);
        if (!m_entityManager->HasComponent<Scene::HierarchyComponent>(entity)) {
            return std::nullopt;
        }
        entity = m_entityManager->GetComponent<Scene::HierarchyComponent>(entity).parent;
    }
    return std::nullopt;
}

void EngineCore::StripWidgetStage(const Scene::Entity stage) {
    std::vector<Scene::Entity> open{stage};
    for (std::size_t i = 0U; i < open.size() && i < 4096U; ++i) {
        const Scene::Entity entity = open[i];
        if (m_entityManager->HasComponent<Scene::CameraComponent>(entity)) {
            m_entityManager->RemoveComponent<Scene::CameraComponent>(entity);
        }
        if (m_entityManager->HasComponent<Scene::ScriptComponent>(entity)) {
            m_entityManager->RemoveComponent<Scene::ScriptComponent>(entity);
        }
        if (m_entityManager->HasComponent<Scene::Rigid3DComponent>(entity)) {
            m_entityManager->RemoveComponent<Scene::Rigid3DComponent>(entity);
        }
        if (m_entityManager->HasComponent<Scene::CharacterControllerComponent>(entity)) {
            m_entityManager->RemoveComponent<Scene::CharacterControllerComponent>(entity);
        }
        if (m_entityManager->HasComponent<Scene::DirectionalLight3DComponent>(entity)) {
            m_entityManager->RemoveComponent<Scene::DirectionalLight3DComponent>(entity);
        }
        if (m_entityManager->HasComponent<Scene::WorldEnvironment3DComponent>(entity)) {
            m_entityManager->RemoveComponent<Scene::WorldEnvironment3DComponent>(entity);
        }
        if (m_entityManager->HasComponent<Scene::LightComponent>(entity) &&
            m_entityManager->GetComponent<Scene::LightComponent>(entity).type == Scene::LightType::Directional) {
            // A sun would light the whole level: on a Stage, a light is local.
            Scene::LightComponent& light = m_entityManager->GetComponent<Scene::LightComponent>(entity);
            light.type = Scene::LightType::Point;
            light.range = std::max(light.range, 12.0F);
        }
        const std::vector<Scene::Entity> children = m_sceneGraph->GetChildren(*m_entityManager, entity);
        open.insert(open.end(), children.begin(), children.end());
    }
}

void EngineCore::SyncUIView3DStages(const float deltaSeconds) {
    // Each 3D View's asset stands on a stage of its own, far below the level: made when the view
    // names it, remade when it names another (or the stage was taken away, as leaving Play does),
    // and turned by its spin every frame. Only what the picture needs is kept on it: its scripts,
    // bodies, controllers and cameras are taken off, so it neither falls nor plays nor takes the view.
    std::vector<Scene::Entity> views;
    m_entityManager->ForEach<Scene::UIView3DComponent>(
        [&views](const Scene::Entity entity, const Scene::UIView3DComponent&) { views.push_back(entity); });
    if (views.empty()) {
        return;
    }
    const std::function<void(Scene::Entity)> destroy = [this, &destroy](const Scene::Entity root) {
        if (!m_entityManager->IsAlive(root)) {
            return;
        }
        for (const Scene::Entity child : m_sceneGraph->GetChildren(*m_entityManager, root)) {
            destroy(child);
        }
        m_entityManager->DestroyEntity(root);
    };
    const std::function<void(Scene::Entity)> strip = [this, &strip](const Scene::Entity entity) {
        if (!m_entityManager->IsAlive(entity)) {
            return;
        }
        if (m_entityManager->HasComponent<Scene::ScriptComponent>(entity)) {
            m_entityManager->RemoveComponent<Scene::ScriptComponent>(entity);
        }
        if (m_entityManager->HasComponent<Scene::CharacterControllerComponent>(entity)) {
            m_entityManager->RemoveComponent<Scene::CharacterControllerComponent>(entity);
        }
        if (m_entityManager->HasComponent<Scene::Rigid3DComponent>(entity)) {
            m_entityManager->RemoveComponent<Scene::Rigid3DComponent>(entity);
        }
        if (m_entityManager->HasComponent<Scene::CameraComponent>(entity)) {
            m_entityManager->RemoveComponent<Scene::CameraComponent>(entity);
        }
        for (const Scene::Entity child : m_sceneGraph->GetChildren(*m_entityManager, entity)) {
            strip(child);
        }
    };
    std::uint32_t nextSlot = 0U;
    for (const Scene::Entity entity : views) {
        Scene::UIView3DComponent& view = m_entityManager->GetComponent<Scene::UIView3DComponent>(entity);
        view.stageSlot = nextSlot++;
        const bool lost = view.stageRoot != Scene::kInvalidEntity && !m_entityManager->IsAlive(view.stageRoot);
        if (view.stagedAssetPath != view.assetPath || lost) {
            destroy(view.stageRoot);
            view.stageRoot = Scene::kInvalidEntity;
            view.stagedAssetPath = view.assetPath;
            if (!view.assetPath.empty() && m_sceneSerializer != nullptr) {
                if (std::optional<std::vector<std::byte>> bytes = m_fileSystem->ReadFile(view.assetPath)) {
                    const std::vector<Scene::Entity> roots = m_sceneSerializer->Restore(
                        *m_entityManager, Scene::SceneSnapshot{std::move(*bytes), Scene::SceneAssetType::Prefab});
                    if (roots.size() == 1U) {
                        view.stageRoot = roots.front();
                        strip(view.stageRoot);
                    }
                } else {
                    UVE_ERROR("EngineCore: the 3D View's \"{}\" cannot be read", view.assetPath);
                }
            }
        }
        // The widget's own Stage (the Stage tab's world), when this view stands its asset on it.
        const Math::Vector3 stagePosition = Scene::UIView3DStagePosition(view.stageSlot);
        const Scene::Entity stage = view.useWidgetStage ? FindWidgetStage(entity) : Scene::kInvalidEntity;
        if (stage != view.widgetStage) {
            view.widgetStage = stage;
            view.stageCameraValid = false;
            if (stage != Scene::kInvalidEntity) {
                // Its camera is read once, relative to the Stage, and then taken off: it must
                // never become the game's camera. What would light or shade the whole level from
                // down there (a sun, an environment) is taken off too.
                const Scene::Entity camera = FindNamedUnder(stage, Scene::kWidgetStageCameraName);
                if (camera != Scene::kInvalidEntity) {
                    if (const std::optional<std::pair<Math::Vector3, Math::Quaternion>> pose = PoseUnder(camera, stage)) {
                        view.stageCameraLocalPosition = pose->first;
                        view.stageCameraLocalRotation = pose->second;
                        view.stageCameraValid = true;
                        if (m_entityManager->HasComponent<Scene::CameraComponent>(camera)) {
                            view.stageCameraFieldOfViewDegrees =
                                m_entityManager->GetComponent<Scene::CameraComponent>(camera).fieldOfViewDegrees;
                        }
                    }
                }
                StripWidgetStage(stage);
            }
        }
        if (m_entityManager->IsAlive(stage) && m_entityManager->HasComponent<Scene::TransformComponent>(stage)) {
            Scene::TransformComponent& stageTransform = m_entityManager->GetComponent<Scene::TransformComponent>(stage);
            if (!(stageTransform.localPosition == stagePosition)) {
                stageTransform.localPosition = stagePosition;
                if (m_entityManager->HasComponent<Scene::WorldTransformComponent>(stage)) {
                    m_entityManager->GetComponent<Scene::WorldTransformComponent>(stage).dirty = true;
                }
            }
            view.stageCameraPosition = stagePosition + view.stageCameraLocalPosition;
            view.stageCameraRotation = view.stageCameraLocalRotation;
        } else {
            view.stageCameraValid = false;
        }
        if (!m_entityManager->IsAlive(view.stageRoot) ||
            !m_entityManager->HasComponent<Scene::TransformComponent>(view.stageRoot)) {
            continue;
        }
        if (std::isfinite(deltaSeconds) && deltaSeconds > 0.0F) {
            view.spinYawDegrees = std::fmod(view.spinYawDegrees + view.spinDegreesPerSecond * std::min(deltaSeconds, 0.1F), 360.0F);
        }
        // On the Stage the asset stands on its ModelSpot (and moves with it); alone, on the view's stage.
        const Scene::Entity spot =
            m_entityManager->IsAlive(stage) ? FindNamedUnder(stage, Scene::kWidgetModelSpotName) : Scene::kInvalidEntity;
        const Scene::Entity parent = m_entityManager->HasComponent<Scene::HierarchyComponent>(view.stageRoot)
                                            ? m_entityManager->GetComponent<Scene::HierarchyComponent>(view.stageRoot).parent
                                            : Scene::kInvalidEntity;
        if (spot != Scene::kInvalidEntity && parent != spot) {
            m_sceneGraph->SetParent(*m_entityManager, view.stageRoot, spot);
        } else if (spot == Scene::kInvalidEntity && parent != Scene::kInvalidEntity) {
            m_sceneGraph->SetParent(*m_entityManager, view.stageRoot, Scene::kInvalidEntity);
        }
        Scene::TransformComponent& transform = m_entityManager->GetComponent<Scene::TransformComponent>(view.stageRoot);
        transform.localPosition = spot != Scene::kInvalidEntity ? Math::Vector3{0.0F, 0.0F, 0.0F} : stagePosition;
        Math::Quaternion turn{};
        if (Math::TryMakeAxisAngle(Math::Vector3{0.0F, 1.0F, 0.0F},
                                      (view.yawDegrees + view.spinYawDegrees) * (Math::kPi / 180.0F), turn)) {
            transform.localRotation = turn;
            transform.rotationEditMode = Scene::RotationEditMode::Quaternion;
        }
        // Marked stale so the scene graph recomputes the world transform the renderer reads.
        if (m_entityManager->HasComponent<Scene::WorldTransformComponent>(view.stageRoot)) {
            m_entityManager->GetComponent<Scene::WorldTransformComponent>(view.stageRoot).dirty = true;
        }
    }
}

namespace {

[[nodiscard]] bool IsUVScriptPath(const std::string_view path) noexcept {
    return Scene::Script::IsUVScriptPath(path);
}

} // namespace

void EngineCore::SyncScriptRuntime() {
    // Process mode decides which scripts run this frame, and priority decides their order.
    // Running - the default - stops while the simulation is paused; PausedOnly and Always are
    // how a pause menu keeps working over a stopped world.
    SyncUVScripts(m_simulationExecutionMode == SimulationExecutionMode::Paused);
}

std::optional<std::string> EngineCore::ReadUVScriptSource(const std::string& path) const {
    const std::optional<std::vector<std::byte>> bytes = m_fileSystem->ReadFile(path);
    if (!bytes.has_value()) {
        return std::nullopt;
    }
    if (path.ends_with(".uvbp")) {
        // A Blueprint Class: the UVScript its graph translates to.
        const std::optional<Asset::BlueprintAsset> blueprint = Asset::DecodeBlueprintAsset(*bytes);
        if (!blueprint.has_value()) {
            return std::nullopt;
        }
        const std::string stem = std::filesystem::path(path).stem().string();
        return Asset::TranslateBlueprintToUVScript(*blueprint, stem, blueprint->parentKind).source;
    }
    if (Asset::IsWidgetBlueprintPath(path)) {
        // A Widget Blueprint: the UVScript its graph translates to, run on the widget's Canvas.
        const std::optional<Asset::BlueprintAsset> graph = Asset::ReadWidgetBlueprintGraph(*bytes);
        if (!graph.has_value()) {
            return std::nullopt;
        }
        const std::string stem = std::filesystem::path(path).stem().string();
        return Asset::TranslateBlueprintToUVScript(*graph, stem, graph->parentKind).source;
    }
    return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

void EngineCore::ReconcileUVScriptInstances() {
    // Edits are picked up by re-reading every script in use a few times a second: a script whose
    // text changed is dropped (and so recompiled and restarted below), and a failed one is retried.
    // The files are small and few, so this is cheaper than wiring every writer to a notification.
#if !UVE_SHIPPING
    if (m_config.hotReloadEnabled) {
        constexpr auto kRecheckInterval = std::chrono::milliseconds(500);
        if (const auto now = std::chrono::steady_clock::now(); now - m_uvScriptLastRecheck >= kRecheckInterval) {
            m_uvScriptLastRecheck = now;
            const auto readText = [this](const std::string& path) { return ReadUVScriptSource(path); };
            std::erase_if(m_uvScripts,
                          [&readText](const auto& entry) { return readText(entry.second.path) != entry.second.source; });
            std::erase_if(m_uvScriptFailedSources, [this, &readText](const auto& entry) {
                if (readText(entry.first) == entry.second) {
                    return false;
                }
                std::erase_if(m_scriptReconcileFailedEntities,
                              [&entry](const auto& failed) { return failed.second == entry.first; });
                return true;
            });
        }
    }
#endif

    // Drop instances whose object is gone, lost its script, or now points at another file.
    std::erase_if(m_uvScripts, [this](const auto& entry) {
        const Scene::Entity entity = entry.first;
        return !m_entityManager->IsAlive(entity) ||
               !m_entityManager->HasComponent<Scene::ScriptComponent>(entity) ||
               m_entityManager->GetComponent<Scene::ScriptComponent>(entity).scriptAssetPath != entry.second.path ||
               m_entityManager->GetComponent<Scene::ScriptComponent>(entity).exportValues !=
                   entry.second.exportValues;
    });

    m_entityManager->ForEach<Scene::ScriptComponent>(
        [this](const Scene::Entity entity, const Scene::ScriptComponent& component) {
            if (!IsUVScriptPath(component.scriptAssetPath)) {
                // Object-graph scripts were removed; say so once per object rather than run nothing
                // silently.
                const auto reported = m_scriptReconcileFailedEntities.find(entity);
                if (!component.scriptAssetPath.empty() && (reported == m_scriptReconcileFailedEntities.end() ||
                                                           reported->second != component.scriptAssetPath)) {
                    m_scriptReconcileFailedEntities.insert_or_assign(entity, component.scriptAssetPath);
                    UVE_WARNING("EngineCore: script \"{}\" is not a UVScript (.uvs) file and will not run",
                             component.scriptAssetPath);
                }
                return;
            }
            if (m_uvScripts.contains(entity)) {
                return;
            }
            if (const auto failed = m_scriptReconcileFailedEntities.find(entity);
                failed != m_scriptReconcileFailedEntities.end()) {
                if (failed->second == component.scriptAssetPath) {
                    return;
                }
                m_scriptReconcileFailedEntities.erase(failed);
            }
            const auto fail = [&](const std::string& why) {
                m_scriptReconcileFailedEntities.insert_or_assign(entity, component.scriptAssetPath);
                UVE_ERROR("EngineCore: script \"{}\": {}", component.scriptAssetPath, why);
            };
            const std::optional<std::string> read = ReadUVScriptSource(component.scriptAssetPath);
            if (!read.has_value()) {
                fail("cannot be read");
                m_uvScriptFailedSources.insert_or_assign(component.scriptAssetPath, std::nullopt);
                return;
            }
            const std::string& source = *read;
            auto host = std::make_unique<BlueprintObjectHost>(
                *m_entityManager, m_inputSystem.get(), entity, m_audioSourceSystem.get(), m_audioSystem.get(),
                [this](const Scene::Entity other) { return FindUVScriptInstance(other); });
            host->SetWidgetFactory([this](const std::string& path) { return CreateWidget(path); });
            host->SetServices(&m_scriptServices);
            host->SetCursorHandler([this](const bool show) {
                // The editor frees or captures its own view's pointer; a game shows or hides the window's.
                if (m_mouseCursorHandler) {
                    m_mouseCursorHandler(show);
                } else if (m_windowManager != nullptr) {
                    m_windowManager->SetCursorVisible(show);
                }
            });
            const Blueprint::CompileResult compiled = Blueprint::CompileUVScriptSource(source, *host);
            if (!compiled.IsSuccess()) {
                for (const Blueprint::Diagnostic& diagnostic : compiled.diagnostics) {
                    UVE_ERROR("{}:{}:{}: {}", component.scriptAssetPath, diagnostic.at.line, diagnostic.at.column,
                              diagnostic.message);
                }
                fail("did not compile");
                m_uvScriptFailedSources.insert_or_assign(component.scriptAssetPath, source);
                return;
            }
            UVScriptSlot slot;
            slot.path = component.scriptAssetPath;
            slot.source = source;
            slot.program = compiled.program;
            slot.instance = std::make_unique<Blueprint::ScriptInstance>(compiled.program, *host);
            // The object's own values for the script's exports, before `ready` sees them. A value
            // for a field the script no longer exports, or of the wrong type, is skipped.
            slot.exportValues = component.exportValues;
            for (const Blueprint::FieldInfo& field : Blueprint::GetProgramFields(*compiled.program)) {
                const auto stored = component.exportValues.find(field.name);
                if (field.kind != Blueprint::FieldKind::Export || stored == component.exportValues.end()) {
                    continue;
                }
                if (const std::optional<Blueprint::Value> value = Blueprint::ParseValueText(stored->second, field.type)) {
                    static_cast<void>(slot.instance->SetField(field.name, *value));
                }
            }
            slot.host = std::move(host);
            m_uvScripts.emplace(entity, std::move(slot));
        });
}

void EngineCore::SyncUVScripts(const bool simulationPaused) {
    if (!m_scriptServices.trace) {
        FillScriptServices();
    }
    // Last frame's shake comes off first, so scripts see (and move) the camera where it really is.
    if (m_cameraShakeTarget != Scene::kInvalidEntity) {
        if (m_entityManager->IsAlive(m_cameraShakeTarget) && m_entityManager->HasComponent<Scene::TransformComponent>(m_cameraShakeTarget)) {
            Scene::TransformComponent& transform = m_entityManager->GetComponent<Scene::TransformComponent>(m_cameraShakeTarget);
            transform.localPosition = transform.localPosition - m_cameraShakeOffset;
            if (m_entityManager->HasComponent<Scene::WorldTransformComponent>(m_cameraShakeTarget)) {
                m_entityManager->GetComponent<Scene::WorldTransformComponent>(m_cameraShakeTarget).dirty = true;
            }
        }
        m_cameraShakeTarget = Scene::kInvalidEntity;
        m_cameraShakeOffset = Math::Vector3{};
    }
    ReconcileUVScriptInstances();
    // Add Movement Input is per frame: what the scripts ask for now replaces last frame's.
    m_entityManager->ForEach<Scene::CharacterControllerComponent>(
        [](const Scene::Entity, Scene::CharacterControllerComponent& body) { body.scriptMove = Math::Vector3{}; });

    std::vector<Scene::Entity> order;
    order.reserve(m_uvScripts.size());
    for (const auto& [entity, slot] : m_uvScripts) {
        order.push_back(entity);
    }

    std::ranges::stable_sort(order, [this](const Scene::Entity a, const Scene::Entity b) {
        return ProcessSettings(*m_entityManager, a).priority < ProcessSettings(*m_entityManager, b).priority;
    });
    const double dt = m_timer ? static_cast<double>(m_timer->GetDeltaTime()) : 0.0;
    for (const Scene::Entity entity : order) {
        const auto it = m_uvScripts.find(entity);
        if (it == m_uvScripts.end() ||
            !Scene::IsTicking(ResolvedTickMode(*m_sceneGraph, entity), simulationPaused)) {
            continue;
        }
        Blueprint::ScriptInstance& instance = *it->second.instance;
        if (!it->second.readyRaised) {
            it->second.readyRaised = true;
            static_cast<void>(instance.RaiseEvent("ready"));
        }
        const Blueprint::Value args[] = {dt};
        static_cast<void>(instance.RaiseEvent("tick", args));
        instance.Advance(dt);
    }

    // Collision transitions computed earlier this frame reach scripts now that every script has
    // ticked: raising during SyncCollisionLifecycle would drop frame-1 enters (slots do not
    // exist before the first tick). Exactly-once per Update - the report is recomputed every
    // Update, so there is nothing to clear and no stale double-raise while paused.
    for (const Physics::CollisionTransition& transition : m_collisionLifecycleReport.transitions) {
        RaiseContactScriptEvent(
            transition.pair.first, transition.pair.second,
            transition.kind == Physics::CollisionTransitionKind::Entered ? "collision_enter" : "collision_exit");
    }
    RaiseWidgetScriptEvents();
    FinishScriptFrame(dt, simulationPaused);
}

void EngineCore::ResetScriptGameState() {
    m_scriptGameTime = 0.0;
    m_scriptGameDelta = 0.0;
    m_pendingScriptEvents.clear();
    m_pendingScriptDestroys.clear();
    m_pendingLevel.reset();
    m_pendingQuit = false;
    m_cameraShakeRemaining = 0.0;
    m_scriptServices.gameData.clear();
}

namespace {

/// A save slot's file: its name kept to letters, digits, '-' and '_'.
[[nodiscard]] std::string SaveSlotPath(const std::string& slot) {
    std::string clean;
    for (const char c : slot) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_') {
            clean.push_back(c);
        }
    }
    return clean.empty() ? std::string{} : "Saved/" + clean + ".uvsave";
}

/// Game data as text: a header line, then one `key<TAB>value` line each, with `\`, tab and
/// newline escaped so any text round-trips.
[[nodiscard]] std::string EncodeGameData(const std::map<std::string, std::string>& data) {
    const auto escape = [](const std::string& text) {
        std::string out;
        for (const char c : text) {
            if (c == '\\') {
                out += "\\\\";
            } else if (c == '\t') {
                out += "\\t";
            } else if (c == '\n') {
                out += "\\n";
            } else {
                out.push_back(c);
            }
        }
        return out;
    };
    std::string out = "UVSAVE 1\n";
    for (const auto& [key, value] : data) {
        out += escape(key) + "\t" + escape(value) + "\n";
    }
    return out;
}

[[nodiscard]] std::optional<std::map<std::string, std::string>> DecodeGameData(const std::string& text) {
    const auto unescape = [](const std::string& field) {
        std::string out;
        for (std::size_t i = 0U; i < field.size(); ++i) {
            if (field[i] == '\\' && i + 1U < field.size()) {
                const char next = field[++i];
                out.push_back(next == 't' ? '\t' : next == 'n' ? '\n' : next);
            } else {
                out.push_back(field[i]);
            }
        }
        return out;
    };
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != "UVSAVE 1") {
        return std::nullopt;
    }
    std::map<std::string, std::string> data;
    while (std::getline(in, line)) {
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            continue;
        }
        data[unescape(line.substr(0U, tab))] = unescape(line.substr(tab + 1U));
    }
    return data;
}

} // namespace

void EngineCore::FillScriptServices() {
    ScriptServices& s = m_scriptServices;
    const auto toEngine = [](const Blueprint::Vec3Value& v) {
        return Math::Vector3{static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
    };
    const auto toScript = [](const Math::Vector3& v) {
        return Blueprint::Vec3Value{static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)};
    };
    s.trace = [this, toEngine, toScript](const Blueprint::Vec3Value& from, const Blueprint::Vec3Value& to, const double radius,
                                         const Scene::Entity ignore) {
        ScriptServices::TraceHit out;
        const Math::Vector3 a = toEngine(from);
        const Math::Vector3 b = toEngine(to);
        const Math::Vector3 d{b.x - a.x, b.y - a.y, b.z - a.z};
        const float length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (!(length > 1e-6F) || !std::isfinite(length)) {
            return out;
        }
        const Math::Ray ray{a, Math::Vector3{d.x / length, d.y / length, d.z / length}};
        if (radius > 0.0 && m_physicsQuerySystem != nullptr) {
            Physics::SphereCastQuery query;
            query.ray = ray;
            query.radius = static_cast<float>(radius);
            query.maxDistance = length;
            query.ignoreEntity = ignore;
            if (const std::optional<Physics::SphereCastHit> hit = m_physicsQuerySystem->SphereCast(*m_entityManager, query)) {
                out.hit = true;
                out.entity = hit->entity;
                out.point = toScript(hit->center);
                out.normal = toScript(hit->normal);
                out.distance = hit->distance;
            }
        } else if (m_raycastSystem != nullptr) {
            Physics::RaycastQuery query;
            query.ray = ray;
            query.maxDistance = length;
            query.ignoreEntity = ignore;
            if (const std::optional<Physics::RaycastHit> hit = m_raycastSystem->Raycast(*m_entityManager, query)) {
                out.hit = true;
                out.entity = hit->entity;
                out.point = toScript(hit->point);
                out.normal = toScript(hit->normal);
                out.distance = hit->distance;
            }
        }
        return out;
    };
    s.spawn = [this, toEngine](const std::string& path, const Blueprint::Vec3Value& position, const Blueprint::Vec3Value& rotation) {
        if (m_sceneSerializer == nullptr || path.empty()) {
            return Scene::kInvalidEntity;
        }
        std::optional<std::vector<std::byte>> bytes = m_fileSystem->ReadFile(path);
        if (!bytes.has_value()) {
            UVE_ERROR("EngineCore: Spawn: \"{}\" cannot be read", path);
            return Scene::kInvalidEntity;
        }
        const std::vector<Scene::Entity> roots =
            m_sceneSerializer->Restore(*m_entityManager, Scene::SceneSnapshot{std::move(*bytes), Scene::SceneAssetType::Prefab});
        if (roots.size() != 1U) {
            std::vector<Scene::Entity> toDestroy = roots;
            for (std::size_t idx = 0U; idx < toDestroy.size(); ++idx) {
                for (const Scene::Entity child : m_sceneGraph->GetChildren(*m_entityManager, toDestroy[idx])) {
                    toDestroy.push_back(child);
                }
            }
            for (auto it = toDestroy.rbegin(); it != toDestroy.rend(); ++it) {
                if (m_entityManager->IsAlive(*it)) {
                    m_entityManager->DestroyEntity(*it);
                }
            }
            return Scene::kInvalidEntity;
        }
        const Scene::Entity root = roots.front();
        if (m_entityManager->HasComponent<Scene::TransformComponent>(root)) {
            Scene::TransformComponent& transform = m_entityManager->GetComponent<Scene::TransformComponent>(root);
            transform.localPosition = toEngine(position);
            const Math::Vector3 degrees = toEngine(rotation);
            constexpr float kRadians = 0.017453292F;
            Math::Quaternion turned{};
            if (Math::TryMakeEuler(Math::Vector3{degrees.x * kRadians, degrees.y * kRadians, degrees.z * kRadians}, turned)) {
                transform.localRotation = turned;
            }
            if (m_entityManager->HasComponent<Scene::WorldTransformComponent>(root)) {
                m_entityManager->GetComponent<Scene::WorldTransformComponent>(root).dirty = true;
            }
        }
        return root;
    };
    s.destroy = [this](const Scene::Entity entity) {
        if (m_entityManager->IsAlive(entity) && std::ranges::find(m_pendingScriptDestroys, entity) == m_pendingScriptDestroys.end()) {
            m_pendingScriptDestroys.push_back(entity);
        }
    };
    s.time = [this] { return m_scriptGameTime; };
    s.delta = [this] { return m_scriptGameDelta; };
    s.setParent = [this](const Scene::Entity child, const Scene::Entity parent) {
        if (m_sceneGraph == nullptr || !m_entityManager->IsAlive(child) || child == parent) {
            return false;
        }
        // Never under its own child: that would make a loop.
        for (Scene::Entity up = parent; up != Scene::kInvalidEntity && m_entityManager->IsAlive(up);) {
            if (up == child) {
                return false;
            }
            if (!m_entityManager->HasComponent<Scene::HierarchyComponent>(up)) {
                break;
            }
            const Scene::Entity next = m_entityManager->GetComponent<Scene::HierarchyComponent>(up).parent;
            if (next == up) {
                break;
            }
            up = next;
        }
        m_sceneGraph->SetParent(*m_entityManager, child, parent);
        return true;
    };
    s.timeScale = [this] { return static_cast<double>(m_simulationSpeedScale); };
    s.setTimeScale = [this](const double scale) { static_cast<void>(SetSimulationSpeedScale(std::clamp(static_cast<float>(scale), 0.1F, 32.0F))); };
    s.paused = [this] { return m_simulationExecutionMode == SimulationExecutionMode::Paused; };
    s.setPaused = [this](const bool paused) {
        static_cast<void>(SetSimulationExecutionMode(paused ? SimulationExecutionMode::Paused : SimulationExecutionMode::Running));
    };
    // Once this frame's scripts are done: the handler may tear the whole level down.
    s.quit = [this] { m_pendingQuit = true; };
    s.openLevel = [this](const std::string& path) {
        if (!m_openLevelHandler || path.empty() || !m_fileSystem->HasFile(path)) {
            return false;
        }
        m_pendingLevel = path;
        return true;
    };
    s.levelName = [this] { return m_currentLevelName.empty() ? m_editorPlaySceneName : m_currentLevelName; };
    s.emit = [this](const Scene::Entity target, const std::string& name, const std::string& payload) {
        // A runaway script that emits from its own handler is held to a sane number per frame.
        if (m_pendingScriptEvents.size() < 4096U) {
            m_pendingScriptEvents.emplace_back(target, name, payload);
        }
    };
    s.playSound = [this, toEngine](const std::string& path, const Blueprint::Vec3Value& position, const double volume, const bool spatial) {
        if (m_audioSourceSystem == nullptr || m_audioSystem == nullptr || m_sceneGraph == nullptr) {
            return false;
        }
        // A short-lived object of its own, removed once the sound is over.
        const Scene::Entity sound = m_entityManager->CreateEntity();
        Scene::TransformComponent transform;
        transform.localPosition = toEngine(position);
        m_sceneGraph->AttachTransform(*m_entityManager, sound, transform);
        // Its world position before the voice is made, which reads it once.
        m_sceneGraph->Update(*m_entityManager);
        Scene::AudioSourceComponent source;
        source.audioAssetPath = path;
        source.volume = static_cast<float>(std::min(volume, 4.0));
        source.spatial = spatial;
        source.playOnAwake = false;
        source.looping = false;
        m_entityManager->AddComponent<Scene::AudioSourceComponent>(sound, source);
        if (!m_audioSourceSystem->PlayEntity(sound, *m_entityManager, *m_audioSystem)) {
            m_entityManager->DestroyEntity(sound);
            return false;
        }
        m_oneShotSounds.push_back(sound);
        return true;
    };
    s.burstParticles = [this](const Scene::Entity emitter, const std::int64_t count) {
        if (!m_entityManager->IsAlive(emitter) || !m_entityManager->HasComponent<Scene::ParticleEmitterComponent>(emitter)) {
            return false;
        }
        // Owed particles: the emitter's next step lets them out (up to its own cap).
        m_particleEmitRemainder[emitter] += static_cast<float>(std::min<std::int64_t>(count, 10000));
        return true;
    };
    s.activateCamera = [this](const Scene::Entity camera) {
        if (!m_entityManager->IsAlive(camera) || !m_entityManager->HasComponent<Scene::CameraComponent>(camera)) {
            return false;
        }
        m_entityManager->ForEach<Scene::CameraComponent>(
            [camera](const Scene::Entity entity, Scene::CameraComponent& view) { view.current = entity == camera; });
        return true;
    };
    s.cameraShake = [this](const double strength, const double seconds) {
        // A stronger shake takes over; a weaker one never cuts a strong one short.
        if (strength >= m_cameraShakeStrength * (m_cameraShakeDuration > 0.0 ? m_cameraShakeRemaining / m_cameraShakeDuration : 0.0)) {
            m_cameraShakeStrength = strength;
            m_cameraShakeDuration = seconds;
            m_cameraShakeRemaining = seconds;
        }
    };
    const auto currentCamera = [this] {
        Scene::Entity found = Scene::kInvalidEntity;
        m_entityManager->ForEach<Scene::CameraComponent>([&found](const Scene::Entity entity, const Scene::CameraComponent& view) {
            if (found == Scene::kInvalidEntity && view.current) {
                found = entity;
            }
        });
        return found;
    };
    s.cameraForward = [this, currentCamera, toScript] {
        const Scene::Entity camera = currentCamera();
        if (camera == Scene::kInvalidEntity || !m_entityManager->HasComponent<Scene::WorldTransformComponent>(camera)) {
            return Blueprint::Vec3Value{0.0, 0.0, -1.0};
        }
        return toScript(Math::RotateVector(m_entityManager->GetComponent<Scene::WorldTransformComponent>(camera).worldRotation,
                                              Math::Vector3{0.0F, 0.0F, -1.0F}));
    };
    s.cameraLocation = [this, currentCamera, toScript] {
        const Scene::Entity camera = currentCamera();
        if (camera == Scene::kInvalidEntity || !m_entityManager->HasComponent<Scene::WorldTransformComponent>(camera)) {
            return Blueprint::Vec3Value{};
        }
        return toScript(m_entityManager->GetComponent<Scene::WorldTransformComponent>(camera).worldPosition);
    };
    s.playAnimation = [this](const Scene::Entity holder, const std::string& name) {
        if (!m_entityManager->IsAlive(holder) || !m_entityManager->HasComponent<Scene::AnimationSequencerComponent>(holder)) {
            return false;
        }
        Scene::AnimationSequencerComponent& player = m_entityManager->GetComponent<Scene::AnimationSequencerComponent>(holder);
        if (!name.empty()) {
            std::vector<Asset::AssetGuid> candidates = player.library;
            candidates.push_back(player.clip);
            Asset::AssetGuid chosen = Asset::kInvalidAssetGuid;
            for (const Asset::AssetGuid guid : candidates) {
                if (guid != Asset::kInvalidAssetGuid && m_assetDatabase != nullptr && m_assetDatabase->Resolve(guid).stem().string() == name) {
                    chosen = guid;
                    break;
                }
            }
            if (chosen == Asset::kInvalidAssetGuid) {
                UVE_WARNING("EngineCore: Play Animation: no clip \"{}\" in the player's list", name);
                return false;
            }
            player.clip = chosen;
        }
        player.playRequested = true;
        return true;
    };
    s.saveSlot = [this](const std::string& slot) {
        const std::string path = SaveSlotPath(slot);
        if (path.empty()) {
            return false;
        }
        const std::string text = EncodeGameData(m_scriptServices.gameData);
        std::vector<std::byte> bytes(text.size());
        std::memcpy(bytes.data(), text.data(), text.size());
        return m_fileSystem->WriteFile(path, bytes);
    };
    s.loadSlot = [this](const std::string& slot) {
        const std::string path = SaveSlotPath(slot);
        const std::optional<std::vector<std::byte>> bytes = path.empty() ? std::nullopt : m_fileSystem->ReadFile(path);
        if (!bytes.has_value()) {
            return false;
        }
        std::optional<std::map<std::string, std::string>> data =
            DecodeGameData(std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
        if (!data.has_value()) {
            return false;
        }
        m_scriptServices.gameData = std::move(*data);
        return true;
    };
    s.slotExists = [this](const std::string& slot) {
        const std::string path = SaveSlotPath(slot);
        return !path.empty() && m_fileSystem->HasFile(path);
    };
    s.deleteSlot = [this](const std::string& slot) {
        const std::string path = SaveSlotPath(slot);
        const std::filesystem::path real = path.empty() ? std::filesystem::path{} : m_fileSystem->ResolveRealPath(path);
        std::error_code error;
        return !real.empty() && std::filesystem::remove(real, error);
    };
}

void EngineCore::FinishScriptFrame(const double dt, const bool simulationPaused) {
    m_scriptGameDelta = simulationPaused ? 0.0 : dt * static_cast<double>(m_simulationSpeedScale);
    m_scriptGameTime += m_scriptGameDelta;
    const auto instanceOf = [this](const Scene::Entity entity) -> Blueprint::ScriptInstance* {
        const auto slot = m_uvScripts.find(entity);
        return slot != m_uvScripts.end() ? slot->second.instance.get() : nullptr;
    };
    // Timers run on game time, so a paused game's timers wait too.
    if (m_scriptGameDelta > 0.0) {
        std::vector<Scene::Entity> timed;
        for (const auto& [entity, slot] : m_uvScripts) {
            if (slot.host != nullptr && slot.host->HasTimers()) {
                timed.push_back(entity);
            }
        }
        for (const Scene::Entity entity : timed) {
            const auto slot = m_uvScripts.find(entity);
            if (slot == m_uvScripts.end() || slot->second.host == nullptr) {
                continue;
            }
            for (const std::string& id : slot->second.host->AdvanceTimers(m_scriptGameDelta)) {
                if (Blueprint::ScriptInstance* const instance = instanceOf(entity)) {
                    const Blueprint::Value args[] = {id};
                    static_cast<void>(instance->RaiseEvent("timer", args));
                }
            }
        }
    }
    // Messages, in the order they were sent; one sent from a handler here runs in the next pass.
    for (int pass = 0; pass < 8 && !m_pendingScriptEvents.empty(); ++pass) {
        std::vector<std::tuple<Scene::Entity, std::string, std::string>> events;
        std::swap(events, m_pendingScriptEvents);
        for (const auto& [target, name, payload] : events) {
            if (Blueprint::ScriptInstance* const instance = instanceOf(target); instance != nullptr && m_entityManager->IsAlive(target)) {
                const Blueprint::Value args[] = {name, payload};
                static_cast<void>(instance->RaiseEvent("custom_event", args));
            }
        }
    }
    // Messages still waiting (sent by the last pass) run next frame.
    // Removals last: each removed object (and what is under it) hears `destroyed` first.
    for (int pass = 0; pass < 4 && !m_pendingScriptDestroys.empty(); ++pass) {
        std::vector<Scene::Entity> doomed;
        std::swap(doomed, m_pendingScriptDestroys);
        const std::function<void(Scene::Entity)> destroy = [this, &destroy, &instanceOf](const Scene::Entity root) {
            if (!m_entityManager->IsAlive(root)) {
                return;
            }
            for (const Scene::Entity child : m_sceneGraph->GetChildren(*m_entityManager, root)) {
                destroy(child);
            }
            if (Blueprint::ScriptInstance* const instance = instanceOf(root)) {
                static_cast<void>(instance->RaiseEvent("destroyed"));
            }
            // The script goes with its object, before the object: no handler outlives what it drives.
            m_uvScripts.erase(root);
            m_entityManager->DestroyEntity(root);
        };
        for (const Scene::Entity entity : doomed) {
            destroy(entity);
        }
    }
    // One-shot sounds that have finished go.
    std::erase_if(m_oneShotSounds, [this](const Scene::Entity sound) {
        if (!m_entityManager->IsAlive(sound)) {
            return true;
        }
        if (m_audioSourceSystem != nullptr && m_audioSystem != nullptr && m_audioSourceSystem->IsEntityPlaying(sound, *m_audioSystem)) {
            return false;
        }
        m_entityManager->DestroyEntity(sound);
        return true;
    });
    // The shake, put on the current camera until the start of the next frame.
    if (m_cameraShakeRemaining > 0.0 && m_cameraShakeDuration > 0.0) {
        Scene::Entity camera = Scene::kInvalidEntity;
        m_entityManager->ForEach<Scene::CameraComponent>([&camera](const Scene::Entity entity, const Scene::CameraComponent& view) {
            if (camera == Scene::kInvalidEntity && view.current) {
                camera = entity;
            }
        });
        if (camera != Scene::kInvalidEntity && m_entityManager->HasComponent<Scene::TransformComponent>(camera)) {
            static std::mt19937 random{std::random_device{}()};
            std::uniform_real_distribution<float> spread(-1.0F, 1.0F);
            const float amplitude = static_cast<float>(m_cameraShakeStrength * (m_cameraShakeRemaining / m_cameraShakeDuration));
            m_cameraShakeOffset = Math::Vector3{spread(random) * amplitude, spread(random) * amplitude, spread(random) * amplitude * 0.3F};
            m_cameraShakeTarget = camera;
            Scene::TransformComponent& transform = m_entityManager->GetComponent<Scene::TransformComponent>(camera);
            transform.localPosition = transform.localPosition + m_cameraShakeOffset;
            if (m_entityManager->HasComponent<Scene::WorldTransformComponent>(camera)) {
                m_entityManager->GetComponent<Scene::WorldTransformComponent>(camera).dirty = true;
            }
        }
        m_cameraShakeRemaining = std::max(0.0, m_cameraShakeRemaining - (m_scriptGameDelta > 0.0 ? m_scriptGameDelta : dt));
    }
    if (m_pendingQuit) {
        m_pendingQuit = false;
        m_pendingLevel.reset();
        if (m_quitGameHandler) {
            m_quitGameHandler();
        } else {
            RequestQuit();
        }
        return;
    }
    if (m_pendingLevel.has_value()) {
        const std::string level = std::move(*m_pendingLevel);
        m_pendingLevel.reset();
        if (m_openLevelHandler) {
            static_cast<void>(m_openLevelHandler(level));
        }
    }
}

Scene::Entity EngineCore::CreateWidget(const std::string& path) {
    if (!Asset::IsWidgetBlueprintPath(path) || m_sceneSerializer == nullptr) {
        UVE_ERROR("EngineCore: \"{}\" is not a Widget Blueprint", path);
        return Scene::kInvalidEntity;
    }
    std::optional<std::vector<std::byte>> bytes = m_fileSystem->ReadFile(path);
    if (!bytes.has_value()) {
        UVE_ERROR("EngineCore: Widget Blueprint \"{}\" cannot be read", path);
        return Scene::kInvalidEntity;
    }
    const std::vector<Scene::Entity> roots =
        m_sceneSerializer->Restore(*m_entityManager, Scene::SceneSnapshot{std::move(*bytes), Scene::SceneAssetType::Prefab});
    if (roots.size() != 1U) {
        std::vector<Scene::Entity> toDestroy = roots;
        for (std::size_t idx = 0U; idx < toDestroy.size(); ++idx) {
            for (const Scene::Entity child : m_sceneGraph->GetChildren(*m_entityManager, toDestroy[idx])) {
                toDestroy.push_back(child);
            }
        }
        for (auto it = toDestroy.rbegin(); it != toDestroy.rend(); ++it) {
            if (m_entityManager->IsAlive(*it)) {
                m_entityManager->DestroyEntity(*it);
            }
        }
        return Scene::kInvalidEntity;
    }
    const Scene::Entity root = roots.front();
    // Made hidden: Add to Viewport shows it. Its graph is the file itself.
    if (m_entityManager->HasComponent<Scene::CanvasComponent>(root)) {
        m_entityManager->GetComponent<Scene::CanvasComponent>(root).visible = false;
    }
    if (m_entityManager->HasComponent<Scene::ScriptComponent>(root)) {
        m_entityManager->GetComponent<Scene::ScriptComponent>(root).scriptAssetPath = path;
    }
    return root;
}

void EngineCore::RaiseWidgetScriptEvents() {
    // A widget that was used tells every scripted object above it (and itself): a Widget
    // Blueprint's graph sits on its root Canvas and hears each of its buttons by name.
    const auto raise = [this](Scene::Entity widget, const std::string_view event, const std::span<const Blueprint::Value> args) {
        for (int depth = 0; depth < 256 && m_entityManager->IsAlive(widget); ++depth) {
            if (const auto slot = m_uvScripts.find(widget); slot != m_uvScripts.end() && slot->second.instance != nullptr) {
                static_cast<void>(slot->second.instance->RaiseEvent(event, args));
            }
            if (!m_entityManager->HasComponent<Scene::HierarchyComponent>(widget)) {
                break;
            }
            const Scene::Entity parent = m_entityManager->GetComponent<Scene::HierarchyComponent>(widget).parent;
            if (parent == widget) {
                break;
            }
            widget = parent;
        }
    };
    const auto nameOf = [this](const Scene::Entity entity) {
        return m_entityManager->HasComponent<Scene::NameComponent>(entity)
                   ? m_entityManager->GetComponent<Scene::NameComponent>(entity).name
                   : std::string{};
    };
    std::vector<std::pair<Scene::Entity, std::string_view>> used;
    std::vector<std::pair<Scene::Entity, std::string>> committed;
    std::unordered_set<Scene::Entity> hovered;
    m_entityManager->ForEach<Scene::UIButtonComponent>([&used, &hovered, this](const Scene::Entity entity, const Scene::UIButtonComponent& button) {
        if (button.wasClickedThisFrame) {
            used.emplace_back(entity, "ui_clicked");
        }
        if (button.isHovered) {
            hovered.insert(entity);
            if (!m_hoveredButtons.contains(entity)) {
                used.emplace_back(entity, "ui_hovered");
            }
        }
    });
    for (const Scene::Entity entity : m_hoveredButtons) {
        if (!hovered.contains(entity) && m_entityManager->IsAlive(entity)) {
            used.emplace_back(entity, "ui_unhovered");
        }
    }
    m_hoveredButtons = std::move(hovered);
    m_entityManager->ForEach<Scene::UISliderComponent>([&used](const Scene::Entity entity, const Scene::UISliderComponent& slider) {
        if (slider.wasChangedThisFrame) {
            used.emplace_back(entity, "ui_changed");
        }
    });
    m_entityManager->ForEach<Scene::UICheckboxComponent>([&used](const Scene::Entity entity, const Scene::UICheckboxComponent& box) {
        if (box.wasToggledThisFrame) {
            used.emplace_back(entity, "ui_changed");
        }
    });
    m_entityManager->ForEach<Scene::UIDropdownComponent>([&used](const Scene::Entity entity, const Scene::UIDropdownComponent& box) {
        if (box.wasSelectionChangedThisFrame) {
            used.emplace_back(entity, "ui_changed");
        }
    });
    m_entityManager->ForEach<Scene::UITextInputComponent>([&committed](const Scene::Entity entity, const Scene::UITextInputComponent& field) {
        if (field.wasSubmittedThisFrame) {
            committed.emplace_back(entity, field.text);
        }
    });
    for (const auto& [entity, event] : used) {
        const Blueprint::Value args[] = {nameOf(entity)};
        raise(entity, event, args);
    }
    for (const auto& [entity, text] : committed) {
        const Blueprint::Value args[] = {nameOf(entity), text};
        raise(entity, "ui_committed", args);
    }
}

std::optional<std::size_t> EngineCore::WriteNativeUVScripts(const std::filesystem::path& directory) const {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    std::unordered_set<std::uint64_t> written;
    for (const auto& [entity, slot] : m_uvScripts) {
        const std::uint64_t fingerprint = Blueprint::GetProgramFingerprint(*slot.program);
        if (!written.insert(fingerprint).second) {
            continue;
        }
        char suffix[24];
        std::snprintf(suffix, sizeof(suffix), "_%016llx", static_cast<unsigned long long>(fingerprint));
        const std::filesystem::path file =
            directory / (std::filesystem::path{slot.path}.stem().string() + suffix + ".uvs.cpp");
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << Blueprint::GenerateUVScriptNativeCpp(*slot.program, slot.path);
        if (!out) {
            UVE_ERROR("EngineCore: could not write native script \"{}\"", file.generic_string());
            return std::nullopt;
        }
    }
    return written.size();
}

Blueprint::ScriptInstance* EngineCore::FindUVScriptInstance(const Scene::Entity entity) noexcept {
    const auto it = m_uvScripts.find(entity);
    return it == m_uvScripts.end() ? nullptr : it->second.instance.get();
}

void EngineCore::SyncAnimation(const float deltaSeconds, const bool physicsStep) {
    if (!(deltaSeconds >= 0.0F)) {
        return;
    }
    // Clamping per-frame animation step delta prevents the playhead from leaping across
    // multiple frames or producing sudden frame pops whenever an OS hitch occurs.
    const float animDeltaSeconds = physicsStep ? deltaSeconds : std::min(deltaSeconds, 0.066F);
    const Scene::AnimationClipResolver clipFor = [this](const Asset::AssetGuid guid)
        -> const Asset::AnimationClipAsset* {
        if (guid == Asset::kInvalidAssetGuid) {
            return nullptr;
        }
        auto it = m_animationClips.find(guid.value);
        if (it == m_animationClips.end()) {
            it = m_animationClips
                     .emplace(guid.value, m_assetManager->Load<Asset::AnimationClipAsset>(guid, *m_assetDatabase))
                     .first;
        }
        return it->second.TryGet();
    };
    // The object an animation moves: its target when that is a live object with a transform, otherwise
    // its parent when that has one. A pure Object parent (the scene root) gives nothing to move.
    const auto resolveTarget = [this](const Scene::Entity self,
                                      const Scene::Entity target) -> Scene::TransformComponent* {
        Scene::Entity chosen = target;
        if (chosen == Scene::kInvalidEntity || !m_entityManager->IsAlive(chosen)) {
            chosen = m_entityManager->HasComponent<Scene::HierarchyComponent>(self)
                         ? m_entityManager->GetComponent<Scene::HierarchyComponent>(self).parent
                         : Scene::kInvalidEntity;
        }
        if (chosen == Scene::kInvalidEntity || chosen == self || !m_entityManager->IsAlive(chosen) ||
            !m_entityManager->HasComponent<Scene::TransformComponent>(chosen)) {
            return nullptr;
        }
        if (m_entityManager->HasComponent<Scene::WorldTransformComponent>(chosen)) {
            m_entityManager->GetComponent<Scene::WorldTransformComponent>(chosen).dirty = true;
        }
        return &m_entityManager->GetComponent<Scene::TransformComponent>(chosen);
    };
    const auto resolveSkeleton = [this](const Scene::Entity self,
                                        const Scene::Entity target) -> Scene::Entity {
        Scene::Entity root = target;
        if (root == Scene::kInvalidEntity || !m_entityManager->IsAlive(root)) {
            root = m_entityManager->HasComponent<Scene::HierarchyComponent>(self)
                       ? m_entityManager->GetComponent<Scene::HierarchyComponent>(self).parent
                       : Scene::kInvalidEntity;
        }
        if (root == Scene::kInvalidEntity || !m_entityManager->IsAlive(root)) {
            return Scene::kInvalidEntity;
        }
        // Breadth first, so the skeleton nearest the target wins.
        std::vector<Scene::Entity> queue{root};
        for (std::size_t next = 0U; next < queue.size() && next < 4096U; ++next) {
            const Scene::Entity candidate = queue[next];
            if (m_entityManager->HasComponent<Scene::Skeleton3DComponent>(candidate)) {
                return candidate;
            }
            const std::vector<Scene::Entity> children = m_sceneGraph->GetChildren(*m_entityManager, candidate);
            queue.insert(queue.end(), children.begin(), children.end());
        }
        return Scene::kInvalidEntity;
    };
    // A clip's tracks for other objects: each object under the animated one (or that object
    // itself) whose name matches, posed in its own local space.
    const auto poseObjectTracks = [this](const Scene::Entity self, const Scene::Entity target,
                                         const Asset::AnimationClipAsset& clip, const double timeSeconds) {
        if (clip.objects.empty()) {
            return;
        }
        Scene::Entity root = target;
        if (root == Scene::kInvalidEntity || !m_entityManager->IsAlive(root)) {
            root = m_entityManager->HasComponent<Scene::HierarchyComponent>(self)
                       ? m_entityManager->GetComponent<Scene::HierarchyComponent>(self).parent
                       : Scene::kInvalidEntity;
        }
        if (root == Scene::kInvalidEntity || !m_entityManager->IsAlive(root)) {
            return;
        }
        std::vector<Scene::Entity> queue{root};
        for (std::size_t next = 0U; next < queue.size() && next < 4096U; ++next) {
            const Scene::Entity candidate = queue[next];
            if (candidate != self && m_entityManager->HasComponent<Scene::NameComponent>(candidate) &&
                m_entityManager->HasComponent<Scene::TransformComponent>(candidate)) {
                const std::string& name = m_entityManager->GetComponent<Scene::NameComponent>(candidate).name;
                for (const Asset::AnimationAssetBoneTrack& track : clip.objects) {
                    if (track.bone == name && !track.samples.empty()) {
                        Scene::WriteAnimatedPose(Scene::SampleAnimationTrack(track.samples, timeSeconds), true, true, true,
                                                    m_entityManager->GetComponent<Scene::TransformComponent>(candidate));
                        if (m_entityManager->HasComponent<Scene::WorldTransformComponent>(candidate)) {
                            m_entityManager->GetComponent<Scene::WorldTransformComponent>(candidate).dirty = true;
                        }
                        break;
                    }
                }
            }
            const std::vector<Scene::Entity> children = m_sceneGraph->GetChildren(*m_entityManager, candidate);
            queue.insert(queue.end(), children.begin(), children.end());
        }
    };
    // Root motion is measured in the skeleton's space; the target moves in its parent's. Both go
    // through world space, composed from the local transforms up the hierarchy so a transform
    // edited this frame (not yet propagated to the world transform) still counts.
    // A Character3D is driven by velocity, so collisions still apply.
    struct Frame {
        Math::Quaternion rotation{};
        Math::Vector3 scale{1.0F, 1.0F, 1.0F};
    };
    const auto worldFrameOf = [this](Scene::Entity entity) {
        Frame frame;
        for (int depth = 0; depth < 1024 && entity != Scene::kInvalidEntity && m_entityManager->IsAlive(entity);
             ++depth) {
            if (m_entityManager->HasComponent<Scene::TransformComponent>(entity)) {
                const auto& local = m_entityManager->GetComponent<Scene::TransformComponent>(entity);
                frame.rotation = Math::Multiply(local.localRotation, frame.rotation);
                frame.scale = Math::Vector3{frame.scale.x * local.localScale.x, frame.scale.y * local.localScale.y,
                                               frame.scale.z * local.localScale.z};
            }
            entity = m_entityManager->HasComponent<Scene::HierarchyComponent>(entity)
                         ? m_entityManager->GetComponent<Scene::HierarchyComponent>(entity).parent
                         : Scene::kInvalidEntity;
        }
        return frame;
    };
    const auto applyRootMotion = [this, &resolveTarget, &worldFrameOf](
                                     const Scene::Entity self, const Scene::Entity target,
                                     const Scene::Entity skeletonEntity, const Math::Vector3& delta,
                                     const float stepSeconds) {
        // Root motion drives gameplay movement only while the simulation runs, and a zero step must
        // not overwrite a controller's velocity (knockback, script movement).
        if (m_simulationExecutionMode != SimulationExecutionMode::Running ||
            Math::LengthSquared(delta) <= 1.0e-10F) {
            return;
        }
        const Frame skeletonFrame = worldFrameOf(skeletonEntity);
        const Math::Vector3 world = Math::RotateVector(
            skeletonFrame.rotation, Math::Vector3{delta.x * skeletonFrame.scale.x, delta.y * skeletonFrame.scale.y,
                                                     delta.z * skeletonFrame.scale.z});
        Scene::TransformComponent* const moved = resolveTarget(self, target);
        if (moved == nullptr) {
            return;
        }
        Scene::Entity movedEntity = target;
        if (movedEntity == Scene::kInvalidEntity || !m_entityManager->IsAlive(movedEntity)) {
            movedEntity = m_entityManager->GetComponent<Scene::HierarchyComponent>(self).parent;
        }
        if (m_entityManager->HasComponent<Scene::CharacterControllerComponent>(movedEntity)) {
            if (stepSeconds > 0.0F) {
                auto& body = m_entityManager->GetComponent<Scene::CharacterControllerComponent>(movedEntity);
                body.velocity.x = world.x / stepSeconds;
                body.velocity.z = world.z / stepSeconds;
            }
            return;
        }
        const Scene::Entity parent = m_entityManager->HasComponent<Scene::HierarchyComponent>(movedEntity)
                                            ? m_entityManager->GetComponent<Scene::HierarchyComponent>(movedEntity).parent
                                            : Scene::kInvalidEntity;
        const Frame parentFrame = worldFrameOf(parent);
        Math::Quaternion inverse{};
        if (!Math::TryInverse(parentFrame.rotation, inverse)) {
            inverse = Math::Quaternion{};
        }
        const Math::Vector3 unrotated = Math::RotateVector(inverse, world);
        const auto safe = [](const float value) { return std::abs(value) > 1.0e-6F ? value : 1.0F; };
        const Math::Vector3 local{unrotated.x / safe(parentFrame.scale.x), unrotated.y / safe(parentFrame.scale.y),
                                     unrotated.z / safe(parentFrame.scale.z)};
        moved->localPosition = moved->localPosition + local;
    };
    // The skeletons this pass poses, in the order they were posed. TwoBoneIK3D runs at the end of
    // this function and only touches these: its result is a rotation blended over the pose the
    // drivers wrote, so solving a skeleton the drivers did not write this pass would blend the same
    // solve over its own previous result and let a limb creep toward the target instead of reaching
    // it. Which skeletons were posed is exactly what the drivers just decided.
    std::vector<Scene::Entity> posedSkeletons;
    // A player or tree loaded without its mixer (an older save) runs with the mixer's defaults.
    const auto mixerOf = [this](const Scene::Entity entity) {
        return Scene::ResolveAnimationDriver(*m_entityManager, entity);
    };
    const Scene::AnimationProcessCallback callback =
        physicsStep ? Scene::AnimationProcessCallback::Physics : Scene::AnimationProcessCallback::Frame;
    // Clip events the playhead passed go to the player's script and to its target's.
    // An event is gameplay, not authoring: it exists to run the author's code at a specific frame
    // of a clip. The pose above evaluates while the editor is paused, so the playhead still moves
    // through the clip - but raising the event then would run gameplay side effects (spawn,
    // damage, sound) from an editor that is explicitly not simulating. Gate the events, not the
    // pose.
    const auto raiseAnimationEvents = [this](const Scene::Entity self, Scene::Entity target,
                                             const std::vector<std::string>& events) {
        if (events.empty() || m_simulationExecutionMode != SimulationExecutionMode::Running) {
            return;
        }
        if (target == Scene::kInvalidEntity || !m_entityManager->IsAlive(target)) {
            target = m_entityManager->HasComponent<Scene::HierarchyComponent>(self)
                         ? m_entityManager->GetComponent<Scene::HierarchyComponent>(self).parent
                         : Scene::kInvalidEntity;
        }
        for (const Scene::Entity listener : {self, target}) {
            const auto slot = m_uvScripts.find(listener);
            if (listener == Scene::kInvalidEntity || slot == m_uvScripts.end() || slot->second.instance == nullptr) {
                continue;
            }
            for (const std::string& name : events) {
                const Blueprint::Value args[] = {Blueprint::Value{name}};
                static_cast<void>(slot->second.instance->RaiseEvent("animation_event", args));
            }
        }
    };

    // Montages, first half: a script asks through the MontageSequencer (montage.play / jump_to / stop);
    // the request is carried out here, where the montage files are, and the montage steps once a
    // frame. Its pose goes to the character's AnimationGraph when the graph has a Slot node for it
    // (the graph lays it where the slot is); otherwise it is blended over the whole body after the
    // drivers below (an attack over the walk).
    struct MontageFrame final {
        Scene::Entity player = Scene::kInvalidEntity;
        Scene::Entity skeleton = Scene::kInvalidEntity;
        std::vector<Scene::SkeletonBonePose> pose;
        float weight = 0.0F;
        bool viaSlot = false;
        bool ended = false;
    };
    std::vector<MontageFrame> montageFrames;
    if (!physicsStep) {
        m_entityManager->ForEach<Scene::AnimationGraphComponent>(
            [](const Scene::Entity, Scene::AnimationGraphComponent& graph) { graph.slotPoses.clear(); });
        const auto montageFor = [this](const Asset::AssetGuid guid) -> const Asset::MotionMontageAsset* {
            if (guid == Asset::kInvalidAssetGuid || !m_assetDatabase) {
                return nullptr;
            }
            MontageCacheEntry& entry = m_animationMontages[guid.value];
            const std::filesystem::path path = m_assetDatabase->Resolve(guid);
            std::error_code error;
            const std::filesystem::file_time_type written = std::filesystem::last_write_time(path, error);
            if (path.empty() || error) {
                return entry.montage.has_value() ? &*entry.montage : nullptr;
            }
            if (path != entry.path || written != entry.written) {
                entry.path = path;
                entry.written = written;
                Asset::MotionMontageAsset loaded;
                if (Asset::LoadMotionMontageAsset(path, loaded)) {
                    entry.montage = std::move(loaded);
                } else {
                    entry.montage.reset();
                }
            }
            return entry.montage.has_value() ? &*entry.montage : nullptr;
        };
        const auto lower = [](std::string text) {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return text;
        };
        // A montage event reaches the MontageSequencer's script and the script of the object it is under.
        const auto raiseMontageEvent = [this](const Scene::Entity self, const char* event, const std::string& name) {
            if (m_simulationExecutionMode != SimulationExecutionMode::Running) {
                return;
            }
            const Scene::Entity parent = m_entityManager->HasComponent<Scene::HierarchyComponent>(self)
                                                ? m_entityManager->GetComponent<Scene::HierarchyComponent>(self).parent
                                                : Scene::kInvalidEntity;
            for (const Scene::Entity listener : {self, parent}) {
                const auto slot = m_uvScripts.find(listener);
                if (listener == Scene::kInvalidEntity || slot == m_uvScripts.end() || slot->second.instance == nullptr) {
                    continue;
                }
                const Blueprint::Value args[] = {Blueprint::Value{name}};
                static_cast<void>(slot->second.instance->RaiseEvent(event, args));
            }
        };
        for (const Scene::Entity entity :
             CollectFixedStepOrder<Scene::MontageSequencerComponent>(*m_entityManager, *m_sceneGraph)) {
            Scene::MontageSequencerComponent& player = m_entityManager->GetComponent<Scene::MontageSequencerComponent>(entity);
            std::vector<Scene::MontageEvent> events;
            if (player.request != Scene::MontageRequest::None) {
                const Scene::MontageRequest request = player.request;
                player.request = Scene::MontageRequest::None;
                if (request == Scene::MontageRequest::Play) {
                    const std::string wanted = lower(player.requestMontage);
                    // Its slots, then its linked library's montages (by the library's name or the file's).
                    std::vector<std::pair<Asset::AssetGuid, std::string>> offered;
                    for (std::size_t i = 0U; i < 8U; ++i) {
                        offered.emplace_back(player.MontageAt(i), std::string{});
                    }
                    if (player.libraryRef != Asset::AssetGuid{}) {
                        Asset::MontageLibraryAsset library;
                        const std::filesystem::path libraryPath = m_assetDatabase->Resolve(player.libraryRef);
                        if (!libraryPath.empty() && Asset::LoadMontageLibraryAsset(libraryPath, library)) {
                            for (const Asset::MontageLibraryEntry& entry : library.entries) {
                                offered.emplace_back(entry.montage, entry.name);
                            }
                        }
                    }
                    for (const auto& [guid, libraryName] : offered) {
                        const Asset::MotionMontageAsset* const montage = montageFor(guid);
                        if (montage == nullptr || (lower(m_assetDatabase->Resolve(guid).stem().string()) != wanted &&
                                                   lower(libraryName) != wanted)) {
                            continue;
                        }
                        if (player.IsPlaying() && player.playing != guid) {
                            player.playback = Scene::MontagePlayback{};   // another montage takes over
                        }
                        if (Scene::StartMontage(*montage, player.playback, player.requestSection)) {
                            player.playing = guid;
                            player.playingName = m_assetDatabase->Resolve(guid).stem().string();
                        }
                        break;
                    }
                } else if (const Asset::MotionMontageAsset* const montage = montageFor(player.playing)) {
                    if (request == Scene::MontageRequest::JumpTo) {
                        static_cast<void>(Scene::JumpToMontageSection(*montage, player.playback, player.requestSection));
                    } else {
                        Scene::StopMontage(*montage, player.playback, events);
                    }
                }
            }
            const Asset::MotionMontageAsset* const montage = montageFor(player.playing);
            const Scene::Entity skeletonEntity = resolveSkeleton(entity, Scene::kInvalidEntity);
            if (montage != nullptr && player.IsPlaying()) {
                Scene::StepMontage(*montage, player.playback, animDeltaSeconds, events);
                player.curveValues.clear();
                for (const Asset::MontageCurve& curve : montage->curves) {
                    player.curveValues.emplace_back(curve.name,
                                                    Asset::EvaluateMontageCurve(*montage, curve.name, player.playback.time));
                }
                if (skeletonEntity != Scene::kInvalidEntity) {
                    MontageFrame frame;
                    frame.player = entity;
                    frame.skeleton = skeletonEntity;
                    frame.weight = player.playback.weight;
                    frame.pose = Scene::PoseMontage(*montage, player.playback.time, clipFor,
                                                       m_entityManager->GetComponent<Scene::Skeleton3DComponent>(skeletonEntity));
                    // A graph posing the same skeleton with a Slot node for this montage's slot takes it.
                    m_entityManager->ForEach<Scene::AnimationGraphComponent>(
                        [&](const Scene::Entity graphEntity, Scene::AnimationGraphComponent& graph) {
                            if (frame.viaSlot || frame.pose.empty() ||
                                resolveSkeleton(graphEntity, mixerOf(graphEntity).target) != skeletonEntity) {
                                return;
                            }
                            // A graph linked to another MontageSequencer leaves this one's montages alone.
                            if (graph.montageSequencer != Scene::kInvalidEntity && graph.montageSequencer != entity &&
                                m_entityManager->IsAlive(graph.montageSequencer)) {
                                return;
                            }
                            const bool hasSlot = std::any_of(graph.nodes.begin(), graph.nodes.end(),
                                                             [&](const Scene::AnimationGraphNode& node) {
                                return node.kind == Scene::AnimationGraphNodeKind::Slot &&
                                       (node.parameter.empty() ? std::string("DefaultSlot") : node.parameter) == montage->slot;
                            });
                            if (!hasSlot) {
                                return;
                            }
                            Scene::AnimationGraphSlotPose slot;
                            slot.slot = montage->slot;
                            slot.weight = frame.weight;
                            for (const Scene::SkeletonBonePose& bone : frame.pose) {
                                slot.bones.push_back(Scene::AnimationGraphSlotBone{bone.position, bone.rotation, bone.scale});
                            }
                            graph.slotPoses.push_back(std::move(slot));
                            frame.viaSlot = true;
                        });
                    montageFrames.push_back(std::move(frame));
                }
            }
            const std::string endedPlayingName = player.playingName;
            for (const Scene::MontageEvent& event : events) {
                switch (event.kind) {
                    case Scene::MontageEvent::Kind::Notify: raiseMontageEvent(entity, "montage_notify", event.name); break;
                    case Scene::MontageEvent::Kind::NotifyBegin: raiseMontageEvent(entity, "montage_notify_begin", event.name); break;
                    case Scene::MontageEvent::Kind::NotifyEnd: raiseMontageEvent(entity, "montage_notify_end", event.name); break;
                    case Scene::MontageEvent::Kind::Ended: raiseMontageEvent(entity, "montage_ended", endedPlayingName); break;
                }
            }
            if (!m_entityManager->IsAlive(entity) ||
                !m_entityManager->HasComponent<Scene::MontageSequencerComponent>(entity)) {
                continue;
            }
            Scene::MontageSequencerComponent& livePlayer =
                m_entityManager->GetComponent<Scene::MontageSequencerComponent>(entity);
            if (!livePlayer.IsPlaying() && livePlayer.playing != Asset::kInvalidAssetGuid) {
                // Ended: an undriven skeleton goes back to its rest pose (after the drivers, below).
                if (skeletonEntity != Scene::kInvalidEntity && m_entityManager->IsAlive(skeletonEntity)) {
                    MontageFrame frame;
                    frame.player = entity;
                    frame.skeleton = skeletonEntity;
                    frame.ended = true;
                    montageFrames.push_back(std::move(frame));
                }
                livePlayer.playing = Asset::kInvalidAssetGuid;
                livePlayer.playingName.clear();
                livePlayer.curveValues.clear();
            }
        }
    }

    for (const Scene::Entity entity :
         CollectFixedStepOrder<Scene::AnimationSequencerComponent>(*m_entityManager, *m_sceneGraph)) {
        if (!m_entityManager->IsAlive(entity) ||
            !m_entityManager->HasComponent<Scene::AnimationSequencerComponent>(entity)) {
            continue;
        }
        Scene::AnimationSequencerComponent& player =
            m_entityManager->GetComponent<Scene::AnimationSequencerComponent>(entity);
        const Scene::AnimationDriverComponent mixer = mixerOf(entity);
        if (!Scene::IsAnimationDriverRunningOnClock(mixer, callback)) {
            continue;
        }
        const Asset::AnimationClipAsset* const clip = clipFor(player.clip);
        if (clip == nullptr) {
            continue; // not set, still loading, or failed - the player waits
        }
        // A skeletal clip poses a skeleton: the target when it is one, else the first Skeleton3D
        // under it (a character's AnimationSequencer targets the character; its skeleton is inside).
        if (clip->IsSkeletal()) {
            const Scene::Entity skeletonEntity = resolveSkeleton(entity, mixer.target);
            if (skeletonEntity == Scene::kInvalidEntity) {
                continue;
            }
            Scene::Skeleton3DComponent& skeleton =
                m_entityManager->GetComponent<Scene::Skeleton3DComponent>(skeletonEntity);
            if (player.playRequested) {
                // A script's Play Animation: from the start, through the mixer's transition.
                player.playRequested = false;
                Scene::PlayAnimationSequencer(player, Scene::TransformComponent{}, clip->durationSeconds);
            } else if (!player.hasStartPose && player.autoplay &&
                m_simulationExecutionMode == SimulationExecutionMode::Running) {
                Scene::PlayAnimationSequencer(player, Scene::TransformComponent{}, clip->durationSeconds);
            } else if (player.isPlaying && player.playingClip != player.clip) {
                // Switched mid-play (a script, a state change): the new clip takes over from the
                // pose that is there, through the mixer's transition.
                Scene::PlayAnimationSequencer(player, Scene::TransformComponent{}, clip->durationSeconds);
            }
            const bool posed = Scene::StepSkeletalAnimationSequencer(player, *clip, animDeltaSeconds * mixer.speedScale,
                                                                     skeleton, mixer);
            if (posed) {
                posedSkeletons.push_back(skeletonEntity);
                poseObjectTracks(entity, mixer.target, *clip, static_cast<double>(player.currentTimeSeconds));
            }
            const std::vector<std::string> firedEvents = player.firedEvents;
            const Math::Vector3 rootMotionDelta = player.rootMotionDelta;
            raiseAnimationEvents(entity, mixer.target, firedEvents);
            if (posed && mixer.rootMotion == Scene::AnimationRootMotionMode::ApplyToTarget &&
                m_entityManager->IsAlive(entity) && m_entityManager->IsAlive(skeletonEntity)) {
                applyRootMotion(entity, mixer.target, skeletonEntity, rootMotionDelta, animDeltaSeconds);
            }
            continue;
        }
        // Resolved before playback starts, so a player with nothing to move never "plays".
        Scene::TransformComponent* const target = resolveTarget(entity, mixer.target);
        if (target == nullptr) {
            continue;
        }
        if (player.playRequested) {
            player.playRequested = false;
            Scene::PlayAnimationSequencer(player, *target, clip->durationSeconds);
        } else if (!player.hasStartPose && player.autoplay &&
            m_simulationExecutionMode == SimulationExecutionMode::Running) {
            Scene::PlayAnimationSequencer(player, *target, clip->durationSeconds);
        }
        if (Scene::StepAnimationSequencer(player, *clip, animDeltaSeconds * mixer.speedScale, *target, mixer)) {
            poseObjectTracks(entity, mixer.target, *clip, static_cast<double>(player.currentTimeSeconds));
        }
        const std::vector<std::string> firedEvents = player.firedEvents;
        raiseAnimationEvents(entity, mixer.target, firedEvents);
    }

    for (const Scene::Entity entity :
         CollectFixedStepOrder<Scene::AnimationGraphComponent>(*m_entityManager, *m_sceneGraph)) {
        if (!m_entityManager->IsAlive(entity) ||
            !m_entityManager->HasComponent<Scene::AnimationGraphComponent>(entity)) {
            continue;
        }
        const Scene::AnimationDriverComponent mixer = mixerOf(entity);
        if (mixer.processCallback != callback) {
            continue;
        }
        Scene::AnimationGraphComponent& tree = m_entityManager->GetComponent<Scene::AnimationGraphComponent>(entity);
        // Parameters fed from the character: the Character Controller on the tree's target or an
        // object above the tree (the character it sits in), searched up.
        if (std::any_of(tree.parameters.begin(), tree.parameters.end(), [](const Scene::AnimationParameter& p) {
                return p.source != Scene::AnimationParameterSource::Script;
            })) {
            const auto findCharacter = [this](Scene::Entity from) {
                for (int depth = 0; depth < 32 && from != Scene::kInvalidEntity && m_entityManager->IsAlive(from); ++depth) {
                    if (m_entityManager->HasComponent<Scene::CharacterControllerComponent>(from)) {
                        return from;
                    }
                    from = m_entityManager->HasComponent<Scene::HierarchyComponent>(from)
                               ? m_entityManager->GetComponent<Scene::HierarchyComponent>(from).parent
                               : Scene::kInvalidEntity;
                }
                return Scene::kInvalidEntity;
            };
            Scene::Entity character = findCharacter(mixer.target);
            if (character == Scene::kInvalidEntity) {
                character = findCharacter(entity);
            }
            if (character != Scene::kInvalidEntity) {
                const auto& controller = m_entityManager->GetComponent<Scene::CharacterControllerComponent>(character);
                const Math::Quaternion facing = m_entityManager->HasComponent<Scene::TransformComponent>(character)
                                                       ? m_entityManager->GetComponent<Scene::TransformComponent>(character).localRotation
                                                       : Math::Quaternion{};
                static_cast<void>(Scene::WriteCharacterAnimationParameters(tree, controller.realVelocity, controller.upDirection, facing,
                                                                             controller.grounded));
            }
        }
        // A character's tree poses its skeleton, bone by bone; with no skeleton under the target it
        // animates the target object itself.
        const Scene::Entity skeletonEntity = resolveSkeleton(entity, mixer.target);
        if (skeletonEntity != Scene::kInvalidEntity) {
            Scene::Skeleton3DComponent& skeleton =
                m_entityManager->GetComponent<Scene::Skeleton3DComponent>(skeletonEntity);
            const bool posed = Scene::StepSkeletalAnimationGraph(tree, clipFor, animDeltaSeconds * mixer.speedScale,
                                                                   skeleton, mixer);
            if (posed) {
                posedSkeletons.push_back(skeletonEntity);
            }
            const std::vector<std::string> firedEvents = tree.firedEvents;
            const Math::Vector3 rootMotionDelta = tree.rootMotionDelta;
            raiseAnimationEvents(entity, mixer.target, firedEvents);
            if (posed && mixer.rootMotion == Scene::AnimationRootMotionMode::ApplyToTarget &&
                m_entityManager->IsAlive(entity) && m_entityManager->IsAlive(skeletonEntity)) {
                applyRootMotion(entity, mixer.target, skeletonEntity, rootMotionDelta, animDeltaSeconds);
            }
            continue;
        }
        Scene::TransformComponent* const target = resolveTarget(entity, mixer.target);
        if (target != nullptr) {
            static_cast<void>(
                Scene::StepAnimationGraph(tree, clipFor, animDeltaSeconds * mixer.speedScale, *target, mixer));
            const std::vector<std::string> firedEvents = tree.firedEvents;
            raiseAnimationEvents(entity, mixer.target, firedEvents);
        }
    }

    // Montages, second half: a montage no Slot node took is blended over the whole body, over what
    // the drivers posed (or the rest pose when nothing did).
    for (const MontageFrame& frame : montageFrames) {
        if (frame.viaSlot || !m_entityManager->IsAlive(frame.skeleton) ||
            !m_entityManager->HasComponent<Scene::Skeleton3DComponent>(frame.skeleton)) {
            continue;
        }
        Scene::Skeleton3DComponent& skeleton = m_entityManager->GetComponent<Scene::Skeleton3DComponent>(frame.skeleton);
        const bool driven = std::find(posedSkeletons.begin(), posedSkeletons.end(), frame.skeleton) != posedSkeletons.end();
        if (frame.ended) {
            if (!driven) {
                skeleton.pose.clear();
            }
            continue;
        }
        if (!driven) {
            skeleton.pose.clear();
        }
        if (Scene::BlendBonePoses(skeleton, frame.pose, frame.weight) && !driven) {
            posedSkeletons.push_back(frame.skeleton);
        }
    }

    // Bone modifiers correct the pose the drivers just wrote, in the same pass that wrote it: IK
    // pulls a limb onto the target its animation cannot know about (a foot on uneven ground, a hand
    // on a moving prop), and it has to happen before the attachment pass puts anything in that hand.
    // The report is discarded on purpose - a refused chain recorded why in its own runtime fields.
    //
    // IK is authoring too. A skeleton no clip animates - a rig being edited, a prop with no player - is not
    // "posed" by any driver, and the IK passes used to skip it: the limb only followed its target in the game,
    // where a clip was running, and could not be set up in the editor at all. Such a skeleton is now solved from
    // the pose the author left (see AuthoredIkPose), on the frame clock, with nothing about gravity or the
    // simulation involved.
    std::vector<Scene::Entity> authoredSkeletons;
    if (!physicsStep) {
        const auto consider = [&](const Scene::Entity skeletonEntity) {
            if (!m_entityManager->IsAlive(skeletonEntity) ||
                !m_entityManager->HasComponent<Scene::Skeleton3DComponent>(skeletonEntity) ||
                std::find(posedSkeletons.begin(), posedSkeletons.end(), skeletonEntity) != posedSkeletons.end() ||
                std::find(authoredSkeletons.begin(), authoredSkeletons.end(), skeletonEntity) != authoredSkeletons.end()) {
                return;
            }
            Scene::Skeleton3DComponent& skeleton = m_entityManager->GetComponent<Scene::Skeleton3DComponent>(skeletonEntity);
            if (!skeleton.enabled || skeleton.bones.empty()) {
                return;
            }
            const auto samePose = [](const std::vector<Scene::SkeletonBonePose>& left,
                                     const std::vector<Scene::SkeletonBonePose>& right) {
                if (left.size() != right.size()) {
                    return false;
                }
                for (std::size_t index = 0U; index < left.size(); ++index) {
                    const Scene::SkeletonBonePose& a = left[index];
                    const Scene::SkeletonBonePose& b = right[index];
                    if (a.position.x != b.position.x || a.position.y != b.position.y || a.position.z != b.position.z ||
                        a.rotation.x != b.rotation.x || a.rotation.y != b.rotation.y || a.rotation.z != b.rotation.z ||
                        a.rotation.w != b.rotation.w || a.scale.x != b.scale.x || a.scale.y != b.scale.y ||
                        a.scale.z != b.scale.z) {
                        return false;
                    }
                }
                return true;
            };
            AuthoredIkPose& entry = m_authoredIkPoses[skeletonEntity];
            if (samePose(skeleton.pose, entry.written)) {
                skeleton.pose = entry.base;
            } else {
                entry.base = skeleton.pose;
            }
            authoredSkeletons.push_back(skeletonEntity);
        };
        m_entityManager->ForEach<Scene::TwoBoneIK3DComponent>(
            [&consider](const Scene::Entity, const Scene::TwoBoneIK3DComponent& chain) { consider(chain.skeleton); });
        m_entityManager->ForEach<Scene::FullBodyIK3DComponent>(
            [&consider](const Scene::Entity, const Scene::FullBodyIK3DComponent& solve) { consider(solve.skeleton); });
        for (auto it = m_authoredIkPoses.begin(); it != m_authoredIkPoses.end();) {
            it = std::find(authoredSkeletons.begin(), authoredSkeletons.end(), it->first) == authoredSkeletons.end()
                     ? m_authoredIkPoses.erase(it)
                     : std::next(it);
        }
        posedSkeletons.insert(posedSkeletons.end(), authoredSkeletons.begin(), authoredSkeletons.end());
    }
    static_cast<void>(Scene::SyncTwoBoneIK3DObjects(*m_entityManager, *m_sceneGraph, posedSkeletons));
    // The whole-body solve runs after the limb chains: the chains have planted what they were asked
    // to, and this reconciles those placements with every other pull in one pose. Its report is
    // discarded for the same reason - a refused solve recorded why in its own runtime fields.
    static_cast<void>(Scene::SyncFullBodyIK3DObjects(*m_entityManager, *m_sceneGraph, posedSkeletons));
    for (const Scene::Entity skeletonEntity : authoredSkeletons) {
        m_authoredIkPoses[skeletonEntity].written =
            m_entityManager->GetComponent<Scene::Skeleton3DComponent>(skeletonEntity).pose;
    }

    if (!physicsStep && m_animationClips.size() > 64U) {
        // Drop clips nothing references any more, so an unused clip does not stay loaded indefinitely.
        std::unordered_set<std::uint64_t> referenced;
        m_entityManager->ForEach<Scene::AnimationSequencerComponent>(
            [&referenced](const Scene::Entity, const Scene::AnimationSequencerComponent& player) {
                if (player.clip != Asset::kInvalidAssetGuid) {
                    referenced.insert(player.clip.value);
                }
                if (player.playingClip != Asset::kInvalidAssetGuid) {
                    referenced.insert(player.playingClip.value);
                }
                for (const Asset::AssetGuid& libClip : player.library) {
                    if (libClip != Asset::kInvalidAssetGuid) {
                        referenced.insert(libClip.value);
                    }
                }
            });
        m_entityManager->ForEach<Scene::AnimationGraphComponent>(
            [&referenced, this](const Scene::Entity, const Scene::AnimationGraphComponent& tree) {
                for (const Scene::AnimationGraphNode& node : tree.nodes) {
                    if (node.clip != Asset::kInvalidAssetGuid) {
                        referenced.insert(node.clip.value);
                    }
                }
                if (tree.sequencer != Scene::kInvalidEntity && m_entityManager->IsAlive(tree.sequencer) &&
                    m_entityManager->HasComponent<Scene::AnimationSequencerComponent>(tree.sequencer)) {
                    const auto& seq = m_entityManager->GetComponent<Scene::AnimationSequencerComponent>(tree.sequencer);
                    if (seq.clip != Asset::kInvalidAssetGuid) {
                        referenced.insert(seq.clip.value);
                    }
                    for (const Asset::AssetGuid& libClip : seq.library) {
                        if (libClip != Asset::kInvalidAssetGuid) {
                            referenced.insert(libClip.value);
                        }
                    }
                }
            });
        m_entityManager->ForEach<Scene::MontageSequencerComponent>(
            [&referenced, this](const Scene::Entity, const Scene::MontageSequencerComponent& player) {
                const auto markMontage = [&referenced, this](const Asset::AssetGuid guid) {
                    if (guid == Asset::kInvalidAssetGuid) {
                        return;
                    }
                    const auto it = m_animationMontages.find(guid.value);
                    if (it == m_animationMontages.end() || !it->second.montage.has_value()) {
                        return;
                    }
                    for (const Asset::MontageSegment& segment : it->second.montage->segments) {
                        if (segment.clip != Asset::kInvalidAssetGuid) {
                            referenced.insert(segment.clip.value);
                        }
                    }
                };
                markMontage(player.playing);
                for (std::size_t i = 0U; i < 8U; ++i) {
                    markMontage(player.MontageAt(i));
                }
            });
        std::erase_if(m_animationClips, [&referenced](const auto& entry) { return !referenced.contains(entry.first); });
    }
}

void EngineCore::SyncBoneAttachment3DObjects() {
    // The resolution rules live with the scene they act on (Scene::SyncBoneAttachment3DObjects).
    // What this seam is for is the ORDER: called after the animation step that posed the skeleton and
    // before the graph propagates world transforms, so an attachment lands on the bone in the frame
    // the pose arrives instead of a frame behind its own animation. The report is discarded on
    // purpose - an attachment that could not resolve recorded why in its own runtime fields, which is
    // what the Inspector reads and what a test can pin.
    static_cast<void>(Scene::SyncBoneAttachment3DObjects(*m_entityManager, *m_sceneGraph));
    static_cast<void>(Scene::UpdateSpringBoneSimulator3DObjects(
        *m_entityManager, static_cast<float>(m_frameStats.deltaTimeSeconds)));
}

void EngineCore::UpgradeProjectContentIfNeeded() {
    // Bump when UpgradeSceneContent learns a new rewrite: every project then runs it once more.
    // The engine version alone is not enough - fix builds of one version can add rewrites.
    constexpr int kContentUpgradeRevision = 1;
    const std::filesystem::path& projectRoot = m_config.projectRootDirectory;
    if (m_config.cookedAssetPaths) {
        return; // a shipped install is read-only content; only an authoring project is upgraded
    }
    if (projectRoot.empty() || projectRoot == std::filesystem::path{"."}) {
        return; // no project (tests, tools): nothing to upgrade and nowhere to keep a backup
    }
    std::error_code error;
    if (!std::filesystem::is_directory(m_config.projectContentRoot, error)) {
        return;
    }
    const std::filesystem::path marker = projectRoot / "Saved" / "ContentUpgrade.revision";
    {
        std::ifstream in(marker);
        int stored = 0;
        if (in >> stored && stored >= kContentUpgradeRevision) {
            return;
        }
    }
    const std::filesystem::path backupRoot =
        projectRoot / "Saved" / "UpgradeBackup" / ("revision-" + std::to_string(kContentUpgradeRevision));
    const Scene::SceneContentUpgradeReport report =
        Scene::UpgradeSceneContent(m_config.projectContentRoot, backupRoot);
    if (report.failed > 0U) {
        // Leave the marker unwritten so the next open tries the files that failed again.
        return;
    }
    std::filesystem::create_directories(marker.parent_path(), error);
    std::ofstream out(marker, std::ios::trunc);
    out << kContentUpgradeRevision << '\n';
}

void EngineCore::SyncRagdoll3DObjects() {
    // Normal ragdoll simulation runs only in Running mode (scaled by m_simulationSpeedScale), so ragdolls never
    // collapse in Edit mode or while paused; ragdolls marked `editorPreviewSimulating` still step for the live
    // editor preview.
    const float rawDt = static_cast<float>(m_frameStats.deltaTimeSeconds);
    if (m_simulationExecutionMode == SimulationExecutionMode::Running) {
        static_cast<void>(Scene::UpdateRagdoll3DObjects(
            *m_entityManager, rawDt * m_simulationSpeedScale, /*editorPreviewOnly=*/false));
    } else {
        // An Edit-mode preview in a level with nothing to land on would fall out of view at once.
        // The Entity Editor already gives its preview a studio floor at Y=0; a level gets the same
        // floor only when it has no collider of its own, so authored ground always wins.
        bool hasPreview = false;
        m_entityManager->ForEach<Scene::Ragdoll3DComponent>(
            [&hasPreview](const Scene::Entity, const Scene::Ragdoll3DComponent& ragdoll) {
                hasPreview = hasPreview || ragdoll.editorPreviewSimulating;
            });
        if (!hasPreview) {
            return;
        }
        // A character's own capsule is not ground: a level holding a Character3D would otherwise
        // never get the preview floor.
        bool hasWorldCollider = false;
        m_entityManager->ForEach<Scene::ColliderComponent>(
            [this, &hasWorldCollider](const Scene::Entity entity, const Scene::ColliderComponent&) {
                if (!m_entityManager->HasComponent<Scene::CharacterControllerComponent>(entity)) {
                    hasWorldCollider = true;
                }
            });
        const std::optional<float> previewFloor =
            hasWorldCollider ? std::nullopt : std::optional<float>{0.0F};
        static_cast<void>(Scene::UpdateRagdoll3DObjects(
            *m_entityManager, rawDt, /*editorPreviewOnly=*/true, previewFloor));
    }
}

void EngineCore::SyncCameraFollow() {
    // The skip rules live with the cameras they act on (Scene::UpdateCameraFollow). What this
    // seam is for is the ORDER: after every mover has stepped and before the graph propagates
    // world transforms, so the follow reads the freshest target pose available and the camera's
    // own world transform is current for this frame's render.
    Scene::UpdateCameraFollow(*m_entityManager);
    static_cast<void>(Scene::UpdatePathFollow3DObjects(
        *m_entityManager, static_cast<float>(m_frameStats.deltaTimeSeconds)));
    static_cast<void>(Scene::UpdateSprite3DObjects(
        *m_entityManager, static_cast<float>(m_frameStats.deltaTimeSeconds)));
    static_cast<void>(Scene::UpdateXRObjects3D(*m_entityManager));
    static_cast<void>(Scene::UpdateUIExtendedObjects(
        *m_entityManager, static_cast<float>(m_frameStats.deltaTimeSeconds)));
    static_cast<void>(Scene::UpdateRemoteTransform3DObjects(*m_entityManager));
}

void EngineCore::SyncKinematic3DObjects(const float fixedDeltaTimeSeconds) {
    if (fixedDeltaTimeSeconds <= 0.0F) {
        return;
    }

    // Platforms move before the characters that ride them. A character measures how far its platform
    // travelled since the last step, so the platform's move for this step has to already be in the
    // world when the character asks - otherwise the rider follows one step behind, forever.
    //
    // Collected and ordered rather than iterated in place: two platforms can shove the same crate,
    // and the order they meet it in changes the outcome, so physicsPriority is how an author decides
    // that instead of archetype storage order deciding it for them.
    for (const Scene::Entity entity :
         CollectFixedStepOrder<Scene::Kinematic3DComponent>(*m_entityManager, *m_sceneGraph)) {
        // One call takes the body from its authored target velocity to moved-and-written-back: the
        // easing, the swept move through the world, the push into whatever it walked into, and the
        // velocity it actually ended up with. A body the mover refuses stays exactly where it is.
        static_cast<void>(Physics::StepKinematicBody(*m_entityManager, *m_sceneGraph, *m_collisionSystem,
                                                        entity, fixedDeltaTimeSeconds));
    }
}

void EngineCore::SyncCharacterControllers(const float fixedDeltaTimeSeconds) {
    // Possession is timeless: fill before resolving so a single editor step drives the player
    // even when no variable frame ran Maintain first.
    Scene::MaintainPlayerPossession(*m_entityManager);
    if (fixedDeltaTimeSeconds <= 0.0F) {
        return;
    }

    // Motion comes from possession, not the keyboard: unpossessed bodies stand still, and the
    // resolver below is where each character's pawn input becomes its step's intent.
    // Collected and ordered rather than iterated in place: two characters can push the same body,
    // and the order they meet it in changes the outcome, so physicsPriority is how an author
    // decides that instead of archetype storage order deciding it for them.
    for (const Scene::Entity entity :
         CollectFixedStepOrder<Scene::CharacterControllerComponent>(*m_entityManager, *m_sceneGraph)) {
        const Physics::CharacterMotionInput motion =
            Scene::ResolveCharacterMotion(*m_entityManager, entity);
        // One call takes the body from intent to moved-and-written-back: built-in movement (or,
        // with that off, the velocity a script set), gravity, the move through the world with its
        // step-up, floor snap and platform carry, and the state the next step reads. The bridge
        // refuses a body it cannot step rather than half-moving it, so a refusal is simply a body
        // that stays where it is.
        const Physics::Character3DStepResult report = Physics::StepCharacter3D(
            *m_entityManager, *m_sceneGraph, *m_collisionSystem, entity, motion, m_config.gravity.y,
            fixedDeltaTimeSeconds);
        // A script's jump is one press: the step that saw it took it.
        if (m_entityManager->HasComponent<Scene::CharacterControllerComponent>(entity)) {
            m_entityManager->GetComponent<Scene::CharacterControllerComponent>(entity).scriptJump = false;
        }
        if (!report.stepped) {
            continue;
        }

        // Pushing is the one thing a character does to *other* bodies, so it stays out of the mover
        // and off the read-only world seam: the contacts the move reported are handed to the rigid
        // bodies that were in the way.
        const Scene::CharacterControllerComponent& settings =
            m_entityManager->GetComponent<Scene::CharacterControllerComponent>(entity);
        if (settings.pushRigidBodies) {
            static_cast<void>(Physics::PushBodiesFromCharacterMove(
                *m_entityManager, report.motion.collisions,
                Physics::CharacterDynamicPushPolicy{true, settings.pushStrength, settings.maxPushSpeed,
                                                       fixedDeltaTimeSeconds}));
        }
    }
}

void EngineCore::SyncProjectile3DObjects(const float fixedDeltaTimeSeconds) {
    if (fixedDeltaTimeSeconds <= 0.0F) {
        return;
    }

    for (const Scene::Entity entity :
         CollectFixedStepOrder<Scene::Projectile3DComponent>(*m_entityManager, *m_sceneGraph)) {
        // One step is one answer: the seam integrates, sweeps the sphere the projectile actually
        // is, resolves the contact through the authored policy and writes the result back. The
        // engine core's job is what happens next - telling the rest of the game that it hit
        // something.
        const Physics::Projectile3DStepResult result =
            Physics::StepProjectile3D(*m_entityManager, *m_sceneGraph, entity, fixedDeltaTimeSeconds);
        if (!result.hasHit) {
            continue;
        }
        // The engine decided the motion; gameplay decides what it means. The contact is queued
        // with its evidence - what was hit, where, how hard, and whether the projectile survived
        // it - on the same bus the area-overlap transitions use.
        m_eventSystem->QueueEvent(Physics::Projectile3DHitEvent{
            entity, result.hitEntity, result.hitPosition, result.hitNormal, result.impactSpeed,
            result.appliedPolicy, result.stoppedOnHit, result.bounceCount});
    }
}

void EngineCore::SyncCollisionLifecycle() {
    const std::vector<Physics::CollisionPair> pairs = m_collisionSystem->DetectCollisions(*m_entityManager);
    m_collisionLifecycleReport = m_collisionLifecycleTracker.Update(pairs);
}

void EngineCore::RaiseContactScriptEvent(const Scene::Entity first, const Scene::Entity second,
                                               const std::string_view event) {
    const Scene::Entity listeners[] = {first, second};
    const Scene::Entity others[] = {second, first};
    for (std::size_t i = 0U; i < 2U; ++i) {
        if (listeners[i] == Scene::kInvalidEntity || !m_entityManager->IsAlive(listeners[i])) {
            continue;
        }
        const auto slot = m_uvScripts.find(listeners[i]);
        if (slot == m_uvScripts.end() || slot->second.instance == nullptr) {
            continue;
        }
        const bool otherNamed = others[i] != Scene::kInvalidEntity && m_entityManager->IsAlive(others[i]) &&
                                m_entityManager->HasComponent<Scene::NameComponent>(others[i]);
        const Blueprint::Value args[] = {Blueprint::Value{
            otherNamed ? m_entityManager->GetComponent<Scene::NameComponent>(others[i]).name
                       : std::string{}}};
        static_cast<void>(slot->second.instance->RaiseEvent(event, args));
    }
}

void EngineCore::SyncRayCast3DObjects() {
    m_entityManager->ForEach<Scene::RayCast3DComponent>(
        [this](const Scene::Entity entity, Scene::RayCast3DComponent&) {
            static_cast<void>(Physics::StepRayCast3D(*m_entityManager, *m_raycastSystem, entity));
        });
    static_cast<void>(Scene::UpdateShapeCast3DObjects(*m_entityManager));
}

void EngineCore::SyncSpringArm3DObjects(const float fixedDeltaTimeSeconds) {
    // Collected and ordered rather than iterated in place, for the same reason the other movers
    // are: two arms on one rig have an order, and Process physicsPriority is how an author decides
    // it instead of archetype storage order deciding it for them.
    for (const Scene::Entity entity :
         CollectFixedStepOrder<Scene::SpringArm3DComponent>(*m_entityManager, *m_sceneGraph)) {
        // One call takes the arm from its authored state to cast-and-committed: the ray along its
        // own axis, the target length under the margin, the motion law (retraction snaps, extension
        // springs, a disabled arm hands its length back), and every direct child riding the delta.
        // An arm the step refuses keeps the length it had.
        static_cast<void>(Physics::StepSpringArm3D(*m_entityManager, *m_sceneGraph, *m_raycastSystem,
                                                      entity, fixedDeltaTimeSeconds));
    }
}

void EngineCore::SyncNavigation(const float fixedDeltaTimeSeconds) {
    if (m_navigationRuntime == nullptr) {
        return;
    }
    // The listing is this function's job because the ORDER is a frame decision: agents are movers,
    // so they are gathered and sorted by Process physicsPriority exactly like the character,
    // kinematic and spring-arm steps above, and the same tick-mode gates apply.
    const std::vector<Scene::Entity> agents =
        CollectFixedStepOrder<Scene::NavSeeker3DComponent>(*m_entityManager, *m_sceneGraph);
    const Navigation::NavigationSyncReport report =
        m_navigationRuntime->Sync(*m_entityManager, *m_raycastSystem, agents, fixedDeltaTimeSeconds);
    static_cast<void>(Scene::ApplyNavigationObstacles3DToAgents(*m_entityManager));
    // A bake is hundreds of rays, so it is worth a log line: a region that re-rasterizes every frame
    // (a moving volume, or a request nobody clears) is otherwise only visible as a frame-time cliff.
    if (report.bakes > 0U) {
        UVE_TRACE("Navigation: {} region mesh(es) baked, {} refused, {} agent(s) stepped, {} without a mesh",
                  report.bakes, report.bakeRefusals, report.agents, report.agentsWithoutMesh);
    }
}

void EngineCore::SyncPerception(const float deltaSeconds) {
    for (const Scene::Entity entity :
         CollectFixedStepOrder<Scene::NoiseEmitterComponent>(*m_entityManager, *m_sceneGraph)) {
        Scene::DecayNoiseEmitter(
            m_entityManager->GetComponent<Scene::NoiseEmitterComponent>(entity), deltaSeconds);
    }
    for (const Scene::Entity entity :
         CollectFixedStepOrder<Scene::PerceptionComponent>(*m_entityManager, *m_sceneGraph)) {
        if (!m_entityManager->HasComponent<Scene::BlackboardComponent>(entity) ||
            !m_entityManager->HasComponent<Scene::WorldTransformComponent>(entity)) {
            continue; // nowhere to record, or nowhere to stand: sensing needs both
        }
        const Scene::PerceptionComponent& sensor =
            m_entityManager->GetComponent<Scene::PerceptionComponent>(entity);
        const Scene::WorldTransformComponent& sensorWorld =
            m_entityManager->GetComponent<Scene::WorldTransformComponent>(entity);
        std::vector<Scene::SightTarget> targets;
        for (const Scene::Entity tagged :
             CollectFixedStepOrder<Scene::GameplayTagComponent>(*m_entityManager, *m_sceneGraph)) {
            if (tagged == entity ||
                !Scene::HasGameplayTag(
                    m_entityManager->GetComponent<Scene::GameplayTagComponent>(tagged),
                    sensor.watchedTag) ||
                !m_entityManager->HasComponent<Scene::WorldTransformComponent>(tagged)) {
                continue;
            }
            targets.push_back(Scene::SightTarget{
                tagged, m_entityManager->GetComponent<Scene::WorldTransformComponent>(tagged)
                            .worldPosition});
        }
        std::vector<Scene::HeardNoise> noises;
        for (const Scene::Entity emitter :
             CollectFixedStepOrder<Scene::NoiseEmitterComponent>(*m_entityManager, *m_sceneGraph)) {
            if (!m_entityManager->HasComponent<Scene::WorldTransformComponent>(emitter)) {
                continue;
            }
            const float loudness =
                m_entityManager->GetComponent<Scene::NoiseEmitterComponent>(emitter).loudness;
            if (loudness <= 0.0F) {
                continue;
            }
            noises.push_back(Scene::HeardNoise{
                m_entityManager->GetComponent<Scene::WorldTransformComponent>(emitter)
                    .worldPosition,
                loudness});
        }
        const Math::Vector3 forward = Math::RotateVector(
            sensorWorld.worldRotation, Math::Vector3{0.0F, 0.0F, -1.0F});
        const Scene::SightOcclusionQuery occluded =
            [this, entity](const Math::Vector3& from, const Math::Vector3& to,
                           const Scene::Entity target) {
                const Math::Vector3 delta = to - from;
                const float distance =
                    std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
                Physics::RaycastQuery query;
                query.ray.origin = from;
                query.ray.direction = delta * (1.0F / distance);
                query.maxDistance = distance;
                query.ignoreEntity = entity;
                query.excludedEntities = std::span<const Scene::Entity>(&target, 1);
                return m_raycastSystem->Raycast(*m_entityManager, query).has_value();
            };
        const Scene::SightResult sight = Scene::SenseSight(
            sensorWorld.worldPosition, forward, sensor, targets, occluded);
        const Scene::HearingResult hearing = Scene::SenseHearing(
            sensorWorld.worldPosition, sensor.hearingRadiusMetres, noises);
        Scene::WritePerceptionResults(
            m_entityManager->GetComponent<Scene::BlackboardComponent>(entity),
            sensor.watchedTag, sight, hearing);
    }
}

void EngineCore::SyncAiBrains(const float deltaSeconds) {
    static_cast<void>(Scene::UpdateSmartObjectAndWorldQuery3D(*m_entityManager, deltaSeconds));
    static_cast<void>(Scene::UpdateStateTree3DObjects(*m_entityManager, deltaSeconds));
    const Scene::BlackboardComponent empty;
    for (const Scene::Entity entity :
         CollectFixedStepOrder<Scene::AiBrainComponent>(*m_entityManager, *m_sceneGraph)) {
        Scene::AiBrainComponent& brain =
            m_entityManager->GetComponent<Scene::AiBrainComponent>(entity);
        const Scene::BlackboardComponent& board =
            m_entityManager->HasComponent<Scene::BlackboardComponent>(entity)
                ? m_entityManager->GetComponent<Scene::BlackboardComponent>(entity)
                : empty;
        const Scene::AiActionSelection selection = Scene::SelectAiAction(brain, board);
        brain.currentScore = selection.score;
        if (selection.changed) {
            brain.currentAction = selection.actionId;
            m_eventSystem->QueueEvent(
                Ai::AiActionSelected{entity, selection.actionId, selection.score});
        }
    }
}

void EngineCore::SyncCinematic(const float deltaSeconds) {
    for (const Scene::Entity entity :
         CollectFixedStepOrder<Scene::CinematicComponent>(*m_entityManager, *m_sceneGraph)) {
        Scene::CinematicComponent& cinematic =
            m_entityManager->GetComponent<Scene::CinematicComponent>(entity);
        // One-shots outlive the shot that fired them, so the sweep runs whether playing or not.
        for (auto voice = cinematic.liveOneShots.begin(); voice != cinematic.liveOneShots.end();) {
            const Audio::VoiceHandle handle{*voice};
            if (m_audioSystem->GetSourceState(handle) == Audio::VoicePlaybackState::Stopped) {
                m_audioSystem->DestroySource(handle);
                voice = cinematic.liveOneShots.erase(voice);
            } else {
                ++voice;
            }
        }
        const auto triggerAnimations = [this](const std::vector<Scene::CinematicAnimationCue>& cues) {
            for (const Scene::CinematicAnimationCue& cue : cues) {
                if (!m_entityManager->IsAlive(cue.target) ||
                    !m_entityManager->HasComponent<Scene::AnimationSequencerComponent>(cue.target)) {
                    continue;
                }
                Scene::AnimationSequencerComponent& player =
                    m_entityManager->GetComponent<Scene::AnimationSequencerComponent>(cue.target);
                if (cue.clip != Asset::kInvalidAssetGuid) {
                    player.clip = cue.clip;
                }
                // Same load-and-wait rule as SyncAnimation's clipFor: an unready clip skips.
                const Asset::AnimationClipAsset* clip = nullptr;
                if (player.clip != Asset::kInvalidAssetGuid) {
                    auto cached = m_animationClips.find(player.clip.value);
                    if (cached == m_animationClips.end()) {
                        cached = m_animationClips
                                     .emplace(player.clip.value,
                                              m_assetManager->Load<Asset::AnimationClipAsset>(
                                                  player.clip, *m_assetDatabase))
                                     .first;
                    }
                    clip = cached->second.TryGet();
                }
                if (clip == nullptr) {
                    continue;
                }
                const Scene::TransformComponent targetNow =
                    m_entityManager->HasComponent<Scene::TransformComponent>(cue.target)
                        ? m_entityManager->GetComponent<Scene::TransformComponent>(cue.target)
                        : Scene::TransformComponent{};
                Scene::PlayAnimationSequencer(player, targetNow, clip->durationSeconds);
            }
        };
        const auto fireAudio = [this, &cinematic](const std::vector<Scene::CinematicAudioCue>& cues) {
            for (const Scene::CinematicAudioCue& cue : cues) {
                if (cinematic.liveOneShots.size() >= Scene::kMaximumCinematicLiveOneShots) {
                    break; // dense loops must not pile up voices without bound
                }
                Audio::AudioSourceDesc desc;
                desc.audioAssetPath = cue.audioAssetPath;
                desc.volume = cue.volume;
                desc.spatial = cue.spatial;
                desc.minDistance = cue.minDistance;
                desc.maxDistance = cue.maxDistance;
                const Audio::VoiceHandle voice = m_audioSystem->CreateSource(desc);
                if (voice == Audio::kInvalidVoiceHandle) {
                    continue;
                }
                if (cue.spatial) {
                    m_audioSystem->SetSourcePosition(voice, cue.position);
                }
                if (!m_audioSystem->Play(voice)) {
                    m_audioSystem->DestroySource(voice);
                    continue;
                }
                cinematic.liveOneShots.push_back(voice.value);
            }
        };
        if (cinematic.autoplay && !cinematic.isPlaying && !cinematic.finished) {
            const Scene::CinematicPlayResult opened = Scene::PlayCinematic(cinematic);
            for (const std::string& eventId : opened.firedEventIds) {
                m_eventSystem->QueueEvent(Gameplay::CinematicEventFired{entity, eventId, 0.0});
            }
            triggerAnimations(opened.animationCues);
            fireAudio(opened.audioCues);
        }
        const Scene::CinematicStepResult result = Scene::StepCinematic(cinematic, deltaSeconds);
        for (const std::string& eventId : result.firedEventIds) {
            m_eventSystem->QueueEvent(
                Gameplay::CinematicEventFired{entity, eventId, cinematic.currentTimeSeconds});
        }
        triggerAnimations(result.firedAnimationCues);
        fireAudio(result.firedAudioCues);
        if (result.activeCamera != Scene::kInvalidEntity) {
            SetActiveCamera(result.activeCamera);
        }
        if (result.cameraPose.has_value() && result.activeCamera != Scene::kInvalidEntity &&
            m_entityManager->HasComponent<Scene::TransformComponent>(result.activeCamera)) {
            // The move track poses the live shot's camera; the graph propagates it to the world
            // transform Render reads, later this same frame.
            Scene::WriteCinematicCameraPose(
                *result.cameraPose,
                m_entityManager->GetComponent<Scene::TransformComponent>(result.activeCamera));
        }
    }
}

void EngineCore::SyncGameplayAttributes(const float deltaSeconds) {
    for (const Scene::Entity entity :
         CollectFixedStepOrder<Scene::GameplayAttributesComponent>(*m_entityManager, *m_sceneGraph)) {
        Scene::GameplayAttributesComponent& attributes =
            m_entityManager->GetComponent<Scene::GameplayAttributesComponent>(entity);
        Scene::StatusEffectsComponent* status = nullptr;
        if (m_entityManager->HasComponent<Scene::StatusEffectsComponent>(entity)) {
            status = &m_entityManager->GetComponent<Scene::StatusEffectsComponent>(entity);
        }
        const std::vector<Scene::GameplayAttributeTickResult> results =
            Scene::TickGameplayAttributes(attributes, status, deltaSeconds);
        Scene::HealthComponent* health = nullptr;
        if (m_entityManager->HasComponent<Scene::HealthComponent>(entity)) {
            health = &m_entityManager->GetComponent<Scene::HealthComponent>(entity);
        }
        for (const Scene::GameplayAttributeTickResult& result : results) {
            if (result.attributeId == Scene::kReservedHealthAttributeId) {
                // The tick computes health-bound effects but never applies them - it cannot see
                // hit points. Damage lands here through the strike applier and restorative
                // magnitudes through the heal applier, each with its own event.
                if (health != nullptr && result.amount > 0.0F) {
                    const Scene::HealthDamageResult damage =
                        Scene::ApplyHealthDamage(*health, result.amount);
                    if (damage.applied) {
                        m_eventSystem->QueueEvent(Gameplay::HealthDamagedEvent{
                            entity, Scene::kInvalidEntity, Scene::kInvalidEntity, damage.amount,
                            damage.remaining});
                        if (damage.depleted) {
                            m_eventSystem->QueueEvent(Gameplay::HealthDepletedEvent{entity});
                        }
                    }
                }
                if (health != nullptr && result.amount < 0.0F) {
                    const Scene::HealthDamageResult heal =
                        Scene::ApplyHealthHeal(*health, -result.amount);
                    if (heal.applied) {
                        m_eventSystem->QueueEvent(Gameplay::HealthHealedEvent{
                            entity, result.effectId, heal.amount, heal.remaining});
                    }
                }
                continue;
            }
            if (!result.applied) {
                continue;
            }
            if (!result.effectId.empty()) {
                m_eventSystem->QueueEvent(Gameplay::AttributeDamagedEvent{
                    entity, result.attributeId, result.amount, result.remaining});
            }
            if (result.depleted) {
                m_eventSystem->QueueEvent(
                    Gameplay::AttributeDepletedEvent{entity, result.attributeId});
            }
        }
    }
}

void EngineCore::SubmitLagShot(Physics::LagShotReport report) {
    m_pendingLagShots.push_back(std::move(report));
}

Physics::LagTick EngineCore::GetLagTick() const noexcept {
    return m_lagTick;
}

Physics::HitboxLagCompensator& EngineCore::GetLagCompensator() noexcept {
    return m_hitboxLagCompensator;
}

const Physics::HitboxLagCompensator& EngineCore::GetLagCompensator() const noexcept {
    return m_hitboxLagCompensator;
}

void EngineCore::SyncHitbox3DObjects() {
    // Lag history records first, while the scene graph's poses are settled: one snapshot per
    // frame under a 1-based engine tick (prediction ticks map to these app-side - the engine
    // owns the numbering, the server owns the mapping). Recording is side-effect-free, so a
    // game that never resolves lag shots pays only the snapshot.
    ++m_lagTick;
    static_cast<void>(m_hitboxLagCompensator.Record(m_lagTick, *m_entityManager));

    // The scan itself is Physics::SyncHitboxes3D(): the hurtbox snapshot, every fail-closed
    // gate, the symmetric layer/mask acceptance, the damage-channel equality, the exact
    // oriented-box overlap and the bounded per-hitbox strike list. The tick owns WHEN this runs,
    // not what the rules are - and the rules are what needed to be testable on their own.
    Physics::Hitbox3DSyncReport report = Physics::SyncHitboxes3D(*m_entityManager);

    // Submitted lag shots merge here - BEFORE the tracker diffs, never as a second update -
    // so a rewound pair that was already active stays silent instead of exiting every live
    // strike. An empty inbox leaves the report exactly the live scan.
    for (const Physics::LagShotReport& pending : m_pendingLagShots) {
        report.strikes.insert(report.strikes.end(), pending.pairs.begin(), pending.pairs.end());
    }
    m_pendingLagShots.clear();

    // The per-hitbox strike list says "I am touching these right now". What a consequence cares
    // about is the edge - this hit started, this hit ended - so the report is diffed against the
    // previous tick's, and the transitions become the typed events gameplay subscribes to. Damage,
    // knockback and i-frames stay gameplay's: the engine resolves the pairing and says so once.
    const Physics::Hitbox3DStrikeLifecycleReport lifecycle =
        m_hitboxStrikeLifecycleTracker.Update(report);
    for (const Physics::Hitbox3DStrikeTransition& transition : lifecycle.transitions) {
        if (transition.kind == Physics::Hitbox3DStrikeTransitionKind::Entered) {
            m_eventSystem->QueueEvent(Physics::Hitbox3DStrikeEnteredEvent{transition.strike});
            const Scene::HealthDamageResult damage =
                Scene::ApplyHitboxStrikeToHealth(*m_entityManager, transition.strike.hurtbox);
            if (damage.applied) {
                m_eventSystem->QueueEvent(Gameplay::HealthDamagedEvent{
                    damage.entity, transition.strike.hitbox, transition.strike.hurtbox, damage.amount,
                    damage.remaining});
                if (damage.depleted) {
                    m_eventSystem->QueueEvent(Gameplay::HealthDepletedEvent{damage.entity});
                }
            }
        } else {
            m_eventSystem->QueueEvent(Physics::Hitbox3DStrikeExitedEvent{transition.strike});
        }
    }
}

void EngineCore::SyncInteractionArea3DObjects() {
    // The whole per-frame contract lives in Physics::SyncInteractionAreas(): the interactor
    // snapshot (character controllers with a valid collider and a world pose), the per-area
    // refresh behind every fail-closed gate, the symmetric layer/mask acceptance, the exact
    // oriented-box overlap, the bounded candidate list and its overflow flag, and the single
    // deterministic focus. It is a seam rather than more code here because the tick owns WHEN this
    // runs, not what it means - and because the interaction rules are exactly what needed to be
    // testable without standing up an EngineCore.
    static_cast<void>(Physics::SyncInteractionAreas(*m_entityManager));
}

void EngineCore::SyncLevelStreamer3DObjects() {
    // Bookkeeping hygiene first: Play/Stop rebuilds every document handle, so a streamer entity
    // that no longer exists must drop its remembered roots AND its failure latch before anything
    // else looks at them - subsequent detection is always through IsAlive(), never blind
    // destruction of a possibly-recycled handle.
    for (auto it = m_levelStreamerLoadedRoots.begin(); it != m_levelStreamerLoadedRoots.end();) {
        if (!m_entityManager->IsAlive(it->first)) {
            it = m_levelStreamerLoadedRoots.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_levelStreamerLoadFailures.begin(); it != m_levelStreamerLoadFailures.end();) {
        if (!m_entityManager->IsAlive(it->first)) {
            it = m_levelStreamerLoadFailures.erase(it);
        } else {
            ++it;
        }
    }

    // Pass 1 (read-only): the viewer point cloud for this tick. The active camera is the common
    // convention (streaming follows the eye); every character controller is a Frostbite-style
    // listener source (split-screen clients each drag their own content in). Finite world poses
    // only - a degenerate camera/contoller pose simply stops being a viewer, it never poisons
    // the decision for everyone else.
    std::vector<Math::Vector3> viewerPositions;
    if (m_activeCamera != Scene::kInvalidEntity &&
        m_entityManager->HasComponent<Scene::WorldTransformComponent>(m_activeCamera)) {
        const Scene::WorldTransformComponent& cameraWorld =
            m_entityManager->GetComponent<Scene::WorldTransformComponent>(m_activeCamera);
        viewerPositions.push_back(cameraWorld.worldPosition);
    }
    m_entityManager->ForEach<Scene::WorldTransformComponent,
                                Scene::CharacterControllerComponent>(
        [&viewerPositions](const Scene::Entity, const Scene::WorldTransformComponent& world,
                           const Scene::CharacterControllerComponent&) {
            viewerPositions.push_back(world.worldPosition);
        });

    // Pass 2 (read-only snapshot): the safe traversal. The verdict pass below can CREATE
    // entities (levelPath restores any component mix, including another streamer) and DESTROY
    // them (an unload), so iterating LevelStreamer3D components directly while mutating would
    // walk over an exploding candlebox: snapshot handles + world poses here, then close the
    // ForEach before any state moves.
    struct StreamerSnapshot final {
        Scene::Entity entity;
        Math::Vector3 worldPosition;
    };
    std::vector<StreamerSnapshot> streamers;
    m_entityManager->ForEach<Scene::WorldTransformComponent,
                                Scene::LevelStreamer3DComponent>(
        [&streamers](const Scene::Entity entity, const Scene::WorldTransformComponent& world,
                     const Scene::LevelStreamer3DComponent&) {
            if (world.dirty) {
                return; // a streamer whose world pose is out of date must never fake distance math
            }
            streamers.push_back(StreamerSnapshot{entity, world.worldPosition});
        });

    // Pass 3: the verdict per streamer, then the state change it orders. Load requests are
    // time-sliced by kMaximumLevelStreamer3DLoadsPerTick - over-budget requests simply remain
    // pending, and the same verdict re-issues next tick (Frostbite burst budgeting).
    std::size_t loadsIssuedThisTick = 0U;
    for (const StreamerSnapshot& snapshot : streamers) {
        if (!m_entityManager->IsAlive(snapshot.entity) ||
            !m_entityManager->HasComponent<Scene::LevelStreamer3DComponent>(snapshot.entity)) {
            continue; // died inside this very pass (unloaded as part of a parent streamer's subtree)
        }
        Scene::LevelStreamer3DComponent& streamer =
            m_entityManager->GetComponent<Scene::LevelStreamer3DComponent>(snapshot.entity);
        if (!Scene::IsLevelStreamer3DObjectComponentValid(streamer)) {
            continue; // an invalid authoring piece never decides anything
        }

        const Scene::LevelStreamer3DAction action =
            Scene::LevelStreamer3D::EvaluateAction(streamer, snapshot.worldPosition,
                                                         viewerPositions);
        if (action == Scene::LevelStreamer3DAction::RequestLoad) {
            if (m_levelStreamerLoadFailures.find(snapshot.entity) != m_levelStreamerLoadFailures.end()) {
                continue; // latched earlier failure: fail-closed, never a retry storm
            }
            if (loadsIssuedThisTick >= Scene::kMaximumLevelStreamer3DLoadsPerTick) {
                continue; // over budget: stay pending - the same verdict re-issues next tick
            }
            ++loadsIssuedThisTick;
            streamer.loadRequested = true; // set for the duration of the synchronous call
            const std::vector<Scene::Entity> freshRoots =
                m_sceneSerializer->Load(*m_entityManager, std::filesystem::path{streamer.levelPath});
            if (freshRoots.empty()) {
                streamer.loadRequested = false;
                streamer.loaded = false;
                m_levelStreamerLoadFailures.emplace(snapshot.entity, true);
                UVE_ERROR("EngineCore: LevelStreamer3D could not load '{}' - latched off "
                          "until the session restarts (fail-closed, never retried)",
                          streamer.levelPath);
                continue;
            }
            m_levelStreamerLoadedRoots[snapshot.entity] = freshRoots;
            streamer.loaded = true;
            streamer.loadRequested = false;
            continue;
        }
        if (action == Scene::LevelStreamer3DAction::RequestUnload) {
            const auto roots = m_levelStreamerLoadedRoots.find(snapshot.entity);
            if (roots != m_levelStreamerLoadedRoots.end()) {
                // Destroy deepest-first so a parent's destruction never observes an already-
                // dead child handle; IsAlive() keeps possibly-recycled handles out of the way.
                const std::function<void(Scene::Entity)> destroySubtree =
                    [this, &destroySubtree](const Scene::Entity current) {
                        if (!m_entityManager->IsAlive(current)) {
                            return;
                        }
                        const std::vector<Scene::Entity> children =
                            m_sceneGraph->GetChildren(*m_entityManager, current);
                        for (const Scene::Entity child : children) {
                            destroySubtree(child);
                        }
                        m_entityManager->DestroyEntity(current);
                    };
                for (const Scene::Entity root : roots->second) {
                    destroySubtree(root);
                }
                m_levelStreamerLoadedRoots.erase(roots);
            }
            m_levelStreamerLoadFailures.erase(snapshot.entity); // unloaded: a future load may retry
            streamer.loaded = false;
            streamer.loadRequested = false;
            continue;
        }
        // LevelStreamer3DAction::None - the hysteresis band holds the line, nothing moves.
    }
}

void EngineCore::SyncReflectionProbe3DObjects() {
    // Pass 1 (read-only): the eye position for this tick. Reflections only exist for the camera.
    std::optional<Math::Vector3> cameraPosition;
    if (m_activeCamera != Scene::kInvalidEntity &&
        m_entityManager->HasComponent<Scene::WorldTransformComponent>(m_activeCamera)) {
        const Math::Vector3& position =
            m_entityManager->GetComponent<Scene::WorldTransformComponent>(m_activeCamera)
                .worldPosition;
        if (std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z)) {
            cameraPosition = position;
        }
    }

    // Pass 2 (read-only snapshot): snapshots BEFORE any mutation - the sync never creates or
    // destroys entities, but it DOES rewrite runtime fields on the components it walks, and a
    // ForEach-compatible snapshot pass keeps iterator safety explicit (streamer convention).
    struct ProbeSnapshot final {
        Scene::Entity entity;
        Math::Vector3 worldPosition;
        Math::Quaternion worldRotation;
    };
    std::vector<ProbeSnapshot> probes;
    m_entityManager->ForEach<Scene::WorldTransformComponent,
                                Scene::ReflectionProbe3DComponent>(
        [&probes](const Scene::Entity entity, const Scene::WorldTransformComponent& world,
                  const Scene::ReflectionProbe3DComponent&) {
            if (world.dirty) {
                return; // a stale world pose must never fake influence math
            }
            Math::Quaternion rotation{};
            if (!Math::TryNormalize(world.worldRotation, rotation)) {
                rotation = {}; // degenerate rotation falls back to identity
            }
            probes.push_back(ProbeSnapshot{entity, world.worldPosition, rotation});
        });

    // Pass 3: per probe, the influence weight on the eye plus the capture verdict. The local
    // offset undoes translation and then orientation with the conjugate (unit-quaternion
    // inverse) - the same pair editor code uses for world->local math.
    struct CaptureCandidate final {
        Scene::Entity entity;
        float cameraDistanceSquared = 0.0F;
    };
    std::vector<CaptureCandidate> candidates;
    for (const ProbeSnapshot& snapshot : probes) {
        Scene::ReflectionProbe3DComponent& probe =
            m_entityManager->GetComponent<Scene::ReflectionProbe3DComponent>(
                snapshot.entity);
        if (!Scene::IsReflectionProbe3DObjectComponentValid(probe)) {
            probe.cameraInfluenceWeight = 0.0F;
            continue; // invalid authoring never influences, never captures
        }

        Scene::ReflectionProbe3DCaptureFrame frame;
        frame.enabled = probe.enabled;
        frame.updateMode = probe.updateMode;
        frame.updateRequested = probe.updateRequested;
        frame.capturedOnce = probe.capturedOnce;
        float cameraDistanceSquared =
            std::numeric_limits<float>::max();
        if (cameraPosition.has_value()) {
            const Math::Vector3 offset = *cameraPosition - snapshot.worldPosition;
            // Pass 2 normalized the world rotation; the conjugate of a unit quaternion is its
            // inverse, so this undoes orientation with no re-normalization needed.
            const Math::Quaternion inverse{
                -snapshot.worldRotation.x, -snapshot.worldRotation.y, -snapshot.worldRotation.z,
                snapshot.worldRotation.w};
            const Math::Vector3 localPoint = Math::RotateVector(inverse, offset);
            frame.cameraInfluenceWeight = Scene::ResolveReflectionProbe3DInfluenceWeight(
                localPoint, Math::Vector3{probe.size.x * 0.5F, probe.size.y * 0.5F,
                                             probe.size.z * 0.5F});
            frame.hasCameraViewer = true;
            cameraDistanceSquared = Math::LengthSquared(offset);
        }
        probe.cameraInfluenceWeight = frame.hasCameraViewer ? frame.cameraInfluenceWeight : 0.0F;

        if (Scene::ResolveReflectionProbe3DCaptureAction(frame) ==
            Scene::ReflectionProbe3DCaptureAction::Capture) {
            candidates.push_back(CaptureCandidate{snapshot.entity, cameraDistanceSquared});
        }
    }

    // Budgeted service. naive nearest-first STARVES the far probe under continuous demand (with
    // more constant candidates than budget, the nearest always win), so the primary key is the
    // WAIT AGE first - the oldest waiter is served before anyone nearer - then camera distance,
    // then (index,generation). Deterministic across runs AND starvation-free, both measured.
    struct RankedCandidate final {
        Scene::Entity entity;
        float cameraDistanceSquared = 0.0F;
        std::uint32_t waitTicks = 0;
    };
    std::vector<RankedCandidate> ranked;
    ranked.reserve(candidates.size());
    for (const CaptureCandidate& candidate : candidates) {
        const Scene::ReflectionProbe3DComponent& waiting =
            m_entityManager->GetComponent<Scene::ReflectionProbe3DComponent>(
                candidate.entity);
        ranked.push_back(
            RankedCandidate{candidate.entity, candidate.cameraDistanceSquared, waiting.captureWaitTicks});
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const RankedCandidate& lhs, const RankedCandidate& rhs) {
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
        std::min(ranked.size(), Scene::kMaximumReflectionProbeCapturesPerTick);
    for (std::size_t i = 0; i < ranked.size(); ++i) {
        Scene::ReflectionProbe3DComponent& probe =
            m_entityManager->GetComponent<Scene::ReflectionProbe3DComponent>(
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

void EngineCore::SyncWorldPartition3DObjects() {
    // The viewer cloud is identical to the streamer's: the eye plus every character controller,
    // finite poses only. Rebuild it here instead of sharing scratch so the two syncs stay
    // symmetric with each other while remaining readable one after the other in LateUpdate.
    std::vector<Math::Vector3> viewerPositions;
    if (m_activeCamera != Scene::kInvalidEntity &&
        m_entityManager->HasComponent<Scene::WorldTransformComponent>(m_activeCamera)) {
        viewerPositions.push_back(
            m_entityManager->GetComponent<Scene::WorldTransformComponent>(m_activeCamera)
                .worldPosition);
    }
    m_entityManager->ForEach<Scene::WorldTransformComponent,
                                Scene::CharacterControllerComponent>(
        [&viewerPositions](const Scene::Entity, const Scene::WorldTransformComponent& world,
                           const Scene::CharacterControllerComponent&) {
            viewerPositions.push_back(world.worldPosition);
        });

    std::vector<Scene::Entity> partitions;
    m_entityManager->ForEach<Scene::WorldPartition3DComponent>(
        [&partitions](const Scene::Entity entity, const Scene::WorldPartition3DComponent&) {
            partitions.push_back(entity);
        });

    for (const Scene::Entity partition : partitions) {
        // The whole partition is rebuilt every tick from its live subtree, so nothing can point
        // at a stale member: components are mutable only through this one place per tick.
        if (!m_entityManager->IsAlive(partition) ||
            !m_entityManager->HasComponent<Scene::WorldPartition3DComponent>(partition)) {
            continue; // Play/Stop mid-walk tore the world down under our feet - stay out of it
        }
        Scene::WorldPartition3DComponent& config =
            m_entityManager->GetComponent<Scene::WorldPartition3DComponent>(partition);
        if (!Scene::IsWorldPartition3DObjectComponentValid(config) ||
            !m_entityManager->HasComponent<Scene::WorldTransformComponent>(partition)) {
            continue; // an invalid partition configures nothing
        }
        const Math::Vector3 gridOrigin =
            m_entityManager->GetComponent<Scene::WorldTransformComponent>(partition)
                .worldPosition;

        // Breadth-first member collection: all descendants that carry a drawable the vis-budget
        // can skip. Nested partitions stop the walk - a partition deep inside another's volume
        // self-manages, so only the closest owning partition ever stamps a membership it does
        // not understand.
        struct MemberSnapshot final {
            Scene::Entity entity;
            Math::Vector3 worldPosition;
            std::optional<Scene::WorldPartition3DCellId> cell;
        };
        std::vector<MemberSnapshot> members;
        std::vector<Scene::Entity> pending;
        pending.push_back(partition);
        while (!pending.empty()) {
            const Scene::Entity current = pending.back();
            pending.pop_back();
            if (!m_entityManager->IsAlive(current)) {
                continue; // the scene graph hands over handles; destruction can invalidate them
            }
            const std::vector<Scene::Entity> children =
                m_sceneGraph->GetChildren(*m_entityManager, current);
            for (const Scene::Entity child : children) {
                if (!m_entityManager->IsAlive(child)) {
                    continue;
                }
                if (m_entityManager->HasComponent<Scene::WorldPartition3DComponent>(
                        child)) {
                    continue; // closest-ancestor-wins: the inner partition claims its subtree
                }
                if (Scene::CarriesWorldPartition3DDrawable(*m_entityManager, child) &&
                    m_entityManager->HasComponent<Scene::WorldTransformComponent>(child)) {
                    const Scene::WorldTransformComponent& world =
                        m_entityManager->GetComponent<Scene::WorldTransformComponent>(child);
                    members.push_back(MemberSnapshot{
                        child, world.worldPosition,
                        Scene::ResolveWorldPartition3DCellIdForPosition(
                            config, gridOrigin, world.worldPosition)});
                }
                pending.push_back(child);
            }
        }

        // Membership attachment is idempotent: the engine owns this runtime component on every
        // drawable descendant, attaches when absent, and re-stamps the owner when an entity has
        // moved between partitions between ticks. AddComponent mutates the ECS, but the member
        // list was snapshotted above, so nothing being iterated here can be invalidated.
        for (const MemberSnapshot& member : members) {
            if (!m_entityManager->HasComponent<Scene::WorldPartition3DMembershipComponent>(
                    member.entity)) {
                m_entityManager->AddComponent<Scene::WorldPartition3DMembershipComponent>(
                    member.entity, Scene::WorldPartition3DMembershipComponent{});
            }
            auto& membership =
                m_entityManager->GetComponent<Scene::WorldPartition3DMembershipComponent>(
                    member.entity);
            membership.partition = partition;
        }

        if (!config.enabled) {
            // A disabled partition willingly shows its whole subtree - fail-open instead of
            // leaving yesterday's fade behind. loadedCellCount reads 0, never a stale count.
            for (const MemberSnapshot& member : members) {
                auto& membership =
                    m_entityManager->GetComponent<Scene::WorldPartition3DMembershipComponent>(
                        member.entity);
                membership.live = true;
            }
            config.loadedCellCount = 0U;
            continue;
        }

        // Occupied cells: priority = nearest member's squared distance to the nearest viewer.
        // The first maximumLoadedCells stay drawn - this is a vis-budget, not a file load.
        std::vector<Scene::WorldPartition3DOccupiedCell> occupied;
        std::vector<float> memberDistances;
        memberDistances.reserve(members.size());
        for (const MemberSnapshot& member : members) {
            memberDistances.push_back(
                Scene::ResolveLevelStreamer3DNearestViewerDistanceSquared(
                    member.worldPosition, viewerPositions)
                    .value_or(std::numeric_limits<float>::max()));
        }
        for (std::size_t i = 0U; i < members.size(); ++i) {
            if (!members[i].cell.has_value()) {
                continue; // outside the partitioned volume: unmanaged, always renders
            }
            auto found = std::find_if(occupied.begin(), occupied.end(),
                                      [&members, i](const Scene::WorldPartition3DOccupiedCell& cell) {
                                          return cell.id == *members[i].cell;
                                      });
            if (found == occupied.end()) {
                occupied.push_back(
                    Scene::WorldPartition3DOccupiedCell{*members[i].cell, memberDistances[i]});
            } else if (memberDistances[i] < found->nearestDistanceSquared) {
                found->nearestDistanceSquared = memberDistances[i];
            }
        }
        Scene::SortWorldPartition3DOccupiedCells(occupied, config.cellCounts);
        const std::size_t liveCells = Scene::CountWorldPartition3DAdmittedCells(
            occupied.size(), config.maximumLoadedCells);
        // Admission wholly-cellular: every member of an admitted cell goes live together, and
        // every member outside it fades together. Unmanaged members never join the verdict.
        for (std::size_t i = 0U; i < members.size(); ++i) {
            const bool live =
                !members[i].cell.has_value() ||
                Scene::IsWorldPartition3DCellAdmitted(*members[i].cell, occupied,
                                                         config.maximumLoadedCells);
            auto& membership =
                m_entityManager->GetComponent<Scene::WorldPartition3DMembershipComponent>(
                    members[i].entity);
            membership.live = live;
        }
        config.loadedCellCount = static_cast<std::uint32_t>(liveCells);
    }
}

void EngineCore::SyncVisibilityRegion3DObjects() {
    // The viewer cloud is identical to the streamer's and the world partition's: the eye plus
    // every character controller, finite poses only. Regions activate on ANY viewer inside.
    std::vector<Math::Vector3> viewerPositions;
    if (m_activeCamera != Scene::kInvalidEntity &&
        m_entityManager->HasComponent<Scene::WorldTransformComponent>(m_activeCamera)) {
        viewerPositions.push_back(
            m_entityManager->GetComponent<Scene::WorldTransformComponent>(m_activeCamera)
                .worldPosition);
    }
    m_entityManager->ForEach<Scene::WorldTransformComponent,
                                Scene::CharacterControllerComponent>(
        [&viewerPositions](const Scene::Entity, const Scene::WorldTransformComponent& world,
                           const Scene::CharacterControllerComponent&) {
            viewerPositions.push_back(world.worldPosition);
        });

    struct RegionSnapshot final {
        Scene::Entity entity;
        Math::Vector3 origin;
    };
    std::vector<RegionSnapshot> regions;
    m_entityManager->ForEach<Scene::VisibilityRegion3DComponent>(
        [this, &regions](const Scene::Entity entity,
                         Scene::VisibilityRegion3DComponent&) {
            regions.push_back(RegionSnapshot{
                entity,
                m_entityManager->HasComponent<Scene::WorldTransformComponent>(entity)
                    ? m_entityManager->GetComponent<Scene::WorldTransformComponent>(entity)
                          .worldPosition
                    : Math::Vector3{}});
        });
    // NO early-out on an empty region list: Pass 1 must still sweep memberships so a region
    // destroyed THIS tick rebrands its orphan members (live + kInvalidEntity) instead of
    // leaving the stale fade binding forever.

    // active is recomputed for every region, ENABLED OR NOT: a disabled region stays dormant on
    // its members through Pass 1, but its flag still reads honestly for the inspector, and the
    // tick it is re-enabled the verdict is already fresh instead of one frame stale. No viewers
    // at all means fail-open-active: an empty play-in-editor world shows everything.
    for (const RegionSnapshot& region : regions) {
        auto& config =
            m_entityManager->GetComponent<Scene::VisibilityRegion3DComponent>(
                region.entity);
        config.active =
            viewerPositions.empty() ||
            Scene::ResolveVisibilityRegion3DAnyViewerInside(config, region.origin,
                                                               viewerPositions);
    }

    // Pass 1: sweep existing memberships. The snapshot doubles as iteration stability - the ECS
    // is not mutated here, but a by-value copy survives whatever a mid-draw Play/Stop does.
    struct MembershipSnapshot final {
        Scene::Entity entity;
        Scene::Entity region;
    };
    std::vector<MembershipSnapshot> members;
    m_entityManager->ForEach<Scene::VisibilityRegion3DMembershipComponent>(
        [&members](const Scene::Entity entity,
                   const Scene::VisibilityRegion3DMembershipComponent& membership) {
            members.push_back(MembershipSnapshot{entity, membership.region});
        });
    for (const MembershipSnapshot& member : members) {
        if (!m_entityManager->IsAlive(member.entity)) {
            continue; // the membership dies with its mesh
        }
        auto& membership =
            m_entityManager->GetComponent<Scene::VisibilityRegion3DMembershipComponent>(
                member.entity);
        const bool ownerAlive =
            member.region != Scene::kInvalidEntity &&
            m_entityManager->IsAlive(member.region) &&
            m_entityManager->HasComponent<Scene::VisibilityRegion3DComponent>(
                member.region);
        if (!ownerAlive) {
            // The owner is gone or disowned its component: rebrand so Pass 2 can rehome the mesh
            // THIS tick, and fail open exactly the way the renderer gate would anyway.
            membership.region = Scene::kInvalidEntity;
            membership.live = true;
            continue;
        }
        const Scene::VisibilityRegion3DComponent& ownerConfig =
            m_entityManager->GetComponent<Scene::VisibilityRegion3DComponent>(
                member.region);
        const Math::Vector3 ownerOrigin =
            m_entityManager->HasComponent<Scene::WorldTransformComponent>(member.region)
                ? m_entityManager->GetComponent<Scene::WorldTransformComponent>(member.region)
                      .worldPosition
                : Math::Vector3{};
        const bool stillManaged =
            ownerConfig.enabled && Scene::IsVisibilityRegion3DObjectComponentValid(ownerConfig) &&
            Scene::CarriesVisibilityRegion3DDrawable(*m_entityManager, member.entity) &&
            m_entityManager->HasComponent<Scene::WorldTransformComponent>(member.entity) &&
            Scene::ResolveVisibilityRegion3DLayerGate(
                ownerConfig.visibilityLayers,
                Scene::ResolveVisibilityRegion3DDrawableLayers(*m_entityManager, member.entity)) &&
            Scene::ResolveVisibilityRegion3DContainsPoint(
                ownerConfig, ownerOrigin,
                m_entityManager->GetComponent<Scene::WorldTransformComponent>(member.entity)
                    .worldPosition);
        // Managed: the owner's current verdict. Released (walked out, layer gate closed,
        // disabled, mesh component removed): back to live THIS tick - no frame of stale dark.
        membership.live = !stillManaged || ownerConfig.active;
    }

    // Pass 2: discovery. An un-owned drawable (never stamped, or Pass 1 rebranded it to kInvalid
    // on a dead owner) inside an enabled region whose layer gate passes becomes its member. With
    // overlapping regions the drawable stamps the NEAREST one's center; ties resolve in the
    // iteration order the ECS hands regions over, which is ascending entity id - deterministic
    // for content that does not overlap rooms halfway.
    if (regions.empty()) {
        return;
    }
    std::vector<Scene::Entity> drawables;
    m_entityManager->ForEach<Scene::WorldTransformComponent>(
        [this, &drawables](const Scene::Entity entity, const Scene::WorldTransformComponent&) {
            if (!Scene::CarriesVisibilityRegion3DDrawable(*m_entityManager, entity)) {
                return;
            }
            const bool unowned =
                !m_entityManager->HasComponent<Scene::VisibilityRegion3DMembershipComponent>(
                    entity) ||
                m_entityManager
                        ->GetComponent<Scene::VisibilityRegion3DMembershipComponent>(entity)
                        .region == Scene::kInvalidEntity;
            if (unowned) {
                drawables.push_back(entity);
            }
        });
    for (const Scene::Entity drawable : drawables) {
        const Math::Vector3 position =
            m_entityManager->GetComponent<Scene::WorldTransformComponent>(drawable)
                .worldPosition;
        const std::uint32_t meshLayers =
            Scene::ResolveVisibilityRegion3DDrawableLayers(*m_entityManager, drawable);
        float bestDistanceSquared = std::numeric_limits<float>::max();
        Scene::Entity bestRegion = Scene::kInvalidEntity;
        for (const RegionSnapshot& region : regions) {
            const Scene::VisibilityRegion3DComponent& config =
                m_entityManager->GetComponent<Scene::VisibilityRegion3DComponent>(
                    region.entity);
            if (!config.enabled || !Scene::IsVisibilityRegion3DObjectComponentValid(config) ||
                !Scene::ResolveVisibilityRegion3DLayerGate(config.visibilityLayers,
                                                              meshLayers) ||
                !Scene::ResolveVisibilityRegion3DContainsPoint(config, region.origin,
                                                                  position)) {
                continue;
            }
            const float dx = position.x - region.origin.x;
            const float dy = position.y - region.origin.y;
            const float dz = position.z - region.origin.z;
            const float distanceSquared = dx * dx + dy * dy + dz * dz;
            if (bestRegion == Scene::kInvalidEntity || distanceSquared < bestDistanceSquared) {
                bestDistanceSquared = distanceSquared;
                bestRegion = region.entity;
            }
        }
        if (bestRegion == Scene::kInvalidEntity) {
            continue; // outside every region: the drawable stays unmanaged and renders as ever
        }
        const bool activeNow =
            m_entityManager->GetComponent<Scene::VisibilityRegion3DComponent>(bestRegion)
                .active;
        if (!m_entityManager->HasComponent<Scene::VisibilityRegion3DMembershipComponent>(
                drawable)) {
            m_entityManager->AddComponent<Scene::VisibilityRegion3DMembershipComponent>(
                drawable, Scene::VisibilityRegion3DMembershipComponent{});
        }
        auto& membership =
            m_entityManager->GetComponent<Scene::VisibilityRegion3DMembershipComponent>(
                drawable);
        membership.region = bestRegion;
        membership.live = activeNow;
    }
}

void EngineCore::SyncAdaptiveRenderResolution() {
    if (!m_windowedRenderingActive || !m_presentationSurfaceReady || !m_renderDevice->IsUsable()) {
        return;
    }

    // An active editor viewport region means the render target should track that panel's own
    // pixel footprint - not the full window - so its aspect ratio, resolution, and downstream
    // adaptive-resolution scaling all match what will actually be displayed there (see
    // SetEditorViewportRegion()'s own doc comment). Absent one (every standalone runtime/test
    // path), this is exactly the previous full-window behavior.
    std::uint32_t drawableWidth =
        m_editorViewportRegion.has_value() ? static_cast<std::uint32_t>(m_editorViewportRegion->size.x)
                                              : m_windowManager->GetWidth();
    std::uint32_t drawableHeight =
        m_editorViewportRegion.has_value() ? static_cast<std::uint32_t>(m_editorViewportRegion->size.y)
                                               : m_windowManager->GetHeight();
    if (!m_editorViewportRegion.has_value() &&
        m_config.stretchMode == Platform::StretchMode::Viewport) {
        const Window::PresentationLayout layout =
            ComputeWindowPresentationLayout(m_config, drawableWidth, drawableHeight);
        if (layout.width > 0U && layout.height > 0U) {
            drawableWidth = layout.width;
            drawableHeight = layout.height;
            if (!layout.integerScaleAvailable && !m_integerScaleFallbackLogged) {
                UVE_WARNING("EngineCore: integer-only viewport scaling cannot fit below the reference "
                            "resolution; using fractional aspect-preserving scaling until an integer scale is available");
                m_integerScaleFallbackLogged = true;
            }
        }
    }
    if (drawableWidth == 0U || drawableHeight == 0U) {
        return;
    }

    const Window::AdaptiveRenderResolution desiredResolution =
        Window::ComputeAdaptiveRenderResolution(drawableWidth, drawableHeight,
                                                    kAdaptiveRenderResolutionLimits,
                                                    m_currentRenderResolutionScale);
    if (desiredResolution.width == 0U || desiredResolution.height == 0U) {
        return;
    }
    if (!m_renderer3D->ResizeTargets(desiredResolution.width, desiredResolution.height) &&
        !m_adaptiveResizeFailureLogged) {
        UVE_WARNING("EngineCore: adaptive render-target resize failed; retaining the previous target");
        m_adaptiveResizeFailureLogged = true;
    }
}

void EngineCore::Update() {
    m_windowManager->PollEvents();
    if (!m_windowedRenderingActive) {
        m_presentationSurfaceReady = false;
    } else {
        const std::uint32_t drawableWidth = m_windowManager->GetWidth();
        const std::uint32_t drawableHeight = m_windowManager->GetHeight();
        m_presentationSurfaceReady = drawableWidth > 0U && drawableHeight > 0U;
        if (!m_presentationSurfaceReady) {
            // A minimized desktop window or an Android lifecycle pause reports a 0x0 surface.
            // Keep the engine and graphics backend alive so rendering resumes when the surface returns.
            m_presentationSurfaceReady = m_windowManager->TryRecoverSurface();
        }
        if ((!m_presentationSurfaceReady && !m_windowedRenderingActive) ||
            (m_presentationSurfaceReady && !m_renderDevice->IsUsable())) {
            m_windowedRenderingActive = false;
            m_presentationSurfaceReady = false;
            if (!m_graphicsBackendLossLogged) {
                UVE_WARNING("EngineCore: graphics backend became unusable; rendering is disabled for this run");
                m_graphicsBackendLossLogged = true;
            }
        }
    }
    SyncAdaptiveRenderResolution();
    m_gamepadInputSystem->Update();
    m_mobileInputSystem->Update();
    m_mobileGestureSystem->Update(static_cast<float>(m_timer->GetDeltaTime()));
    m_inputSystem->Update();
    if (m_simulationExecutionMode == SimulationExecutionMode::Running) {
        const Gameplay::GameplayInput gameplayInput = Gameplay::CollectGameplayInput(*m_inputSystem);
        Scene::MaintainPlayerPossession(*m_entityManager);
        Scene::RouteGameplayInput(*m_entityManager, gameplayInput);
        const Scene::PossessionLifecycleReport possessionReport =
            m_possessionLifecycleTracker.Update(*m_entityManager);
        for (const Scene::PossessionTransition& transition : possessionReport.transitions) {
            if (transition.kind == Scene::PossessionTransitionKind::Possessed) {
                m_eventSystem->QueueEvent(
                    Gameplay::PawnPossessedEvent{transition.controller, transition.pawn});
                // The queued event is for game code; the engine's own flagged follow cameras
                // consume the same transition inline, with no subscription round-trip.
                Scene::RetargetPossessionFollowers(*m_entityManager, transition.controller,
                                                      transition.pawn);
            } else {
                m_eventSystem->QueueEvent(
                    Gameplay::PawnUnpossessedEvent{transition.controller, transition.pawn});
                Scene::ReleasePossessionFollowers(*m_entityManager, transition.controller,
                                                     transition.pawn);
            }
        }
        const Scene::Entity player = Scene::ResolvePossessedPlayer(*m_entityManager);
        if (player != Scene::kInvalidEntity &&
            m_entityManager->HasComponent<Scene::PlayerComponent>(player) &&
            m_entityManager->HasComponent<Scene::TransformComponent>(player)) {
            Scene::PlayerComponent& playerState =
                m_entityManager->GetComponent<Scene::PlayerComponent>(player);
            Scene::TransformComponent body =
                m_entityManager->GetComponent<Scene::TransformComponent>(player);
            const Scene::Entity lookTarget = Scene::FindPlayerLookTarget(*m_entityManager, player);
            Scene::TransformComponent lookTransform{};
            Scene::TransformComponent* lookPointer = nullptr;
            if (lookTarget != Scene::kInvalidEntity &&
                m_entityManager->HasComponent<Scene::TransformComponent>(lookTarget)) {
                lookTransform = m_entityManager->GetComponent<Scene::TransformComponent>(lookTarget);
                lookPointer = &lookTransform;
            }
            Scene::ApplyPlayerLook(playerState, body, lookPointer, gameplayInput.lookPointer,
                                      gameplayInput.lookStick,
                                      static_cast<float>(m_timer->GetDeltaTime()));
            m_sceneGraph->SetLocalTransform(*m_entityManager, player, body);
            if (lookPointer != nullptr) {
                m_sceneGraph->SetLocalTransform(*m_entityManager, lookTarget, lookTransform);
            }
        }
    }
    if (m_config.quitOnLastWindowClosed && m_windowManager->IsCloseRequested()) {
        RequestQuit();
    }

    Utilities::FixedStepResult fixedStep{};
    if (m_simulationExecutionMode == SimulationExecutionMode::Running) {
        fixedStep = m_timer->AdvanceFixedStep();
        if (m_simulationSpeedScale > 1.0F && fixedStep.stepsToRun > 0) {
            fixedStep.stepsToRun = std::clamp(
                static_cast<int>(std::lround(static_cast<float>(fixedStep.stepsToRun) * m_simulationSpeedScale)),
                1, 32);
        }
    } else {
        m_timer->DiscardFixedStepAccumulator();
        if (m_singleSimulationStepPending) {
            fixedStep.stepsToRun = 1;
            m_singleSimulationStepPending = false;
        }
    }
    UVE_TRACE("Update: {} fixed step(s), alpha={}", fixedStep.stepsToRun, fixedStep.alpha);
    m_eventSystem->DispatchQueued();
    ReconcileUVScriptInstances();

    const float fixedDeltaTimeSeconds =
        m_config.fixedUpdateFps > 0.0 ? static_cast<float>(1.0 / m_config.fixedUpdateFps) : 0.0F;
    // ---- Simulation pass: only while the simulation clock runs -------------------------------
    // Every one of these integrates over time - gravity, contacts, buoyancy, spring joints, the
    // character controller's sweep - so every one of them is a fixed step or none at all. Frozen
    // here means a body authored in mid-air stays authored in mid-air, which is the whole reason
    // an editor can be used to place one.
    for (int step = 0; step < fixedStep.stepsToRun; ++step) {
        static_cast<void>(Scene::UpdateJoint3DObjects(
            *m_entityManager, *m_sceneGraph, fixedDeltaTimeSeconds));
        static_cast<void>(Scene::UpdateVehicle3DObjects(
            *m_entityManager, fixedDeltaTimeSeconds));
        static_cast<void>(Scene::UpdatePhysicalBone3DObjects(
            *m_entityManager, m_config.gravity, fixedDeltaTimeSeconds));
        static_cast<void>(Scene::UpdateSoftBody3DObjects(
            *m_entityManager, m_config.gravity, fixedDeltaTimeSeconds));
        m_physicsSystem->Step(*m_entityManager, *m_sceneGraph, fixedDeltaTimeSeconds);
        SyncKinematic3DObjects(fixedDeltaTimeSeconds);
        SyncCharacterControllers(fixedDeltaTimeSeconds);
        SyncAnimation(fixedDeltaTimeSeconds, /*physicsStep=*/true);
        SyncProjectile3DObjects(fixedDeltaTimeSeconds);
        SyncSpringArm3DObjects(fixedDeltaTimeSeconds);
        SyncNavigation(fixedDeltaTimeSeconds);
    }

    // ---- Authoring pass: every frame, simulated or not ----------------------------------------
    // Animation evaluation is not simulation. Posing a skeleton, blending a clip, solving an IK
    // target and skinning the mesh are all functions of the values the author just typed, and an
    // editor that stops computing them the moment the simulation is paused has taken away the
    // authoring tools in the name of stopping gravity. This call used to sit inside the Running
    // check below, next to the AI and gameplay syncs it has nothing to do with, so pausing froze
    // the pose: an IK target could be dragged with no result, and a character being edited could
    // not be posed at all. It runs unconditionally now, on the frame clock, and the pose is the
    // one thing in this function that survives a pause.
    const float frameSimScale =
        (m_simulationExecutionMode == SimulationExecutionMode::Running) ? m_simulationSpeedScale : 1.0F;
    SyncAnimation(static_cast<float>(m_timer->GetDeltaTime()) * frameSimScale, /*physicsStep=*/false);

    if (m_simulationExecutionMode == SimulationExecutionMode::Running) {
        const float scaledFrameDelta = static_cast<float>(m_timer->GetDeltaTime()) * m_simulationSpeedScale;
        SyncGameplayAttributes(scaledFrameDelta);
        SyncCinematic(scaledFrameDelta);
        SyncPerception(scaledFrameDelta);
        SyncAiBrains(scaledFrameDelta);
    }
    // Ragdolls finalize the skeleton pose before bone attachments & spring bones sample it, so
    // weapons/helmets attached to a simulated limb stay glued in the exact same frame.
    SyncRagdoll3DObjects();
    SyncBoneAttachment3DObjects();
    SyncCameraFollow();
    m_sceneGraph->Update(*m_entityManager);
    // The fraction of a fixed step already elapsed, handed to the renderer so it can draw between
    // the last two simulated poses instead of snapping to the newest one. Measured on a 144 Hz
    // display against 60 Hz physics: 58% of frames previously received no new pose at all, and
    // per-frame movement jumped between 0 and 16.7 cm at 10 m/s. This is the value that fixes it,
    // and it has been computed every frame since the timer was written - only ever logged.
    m_renderer3D->SetPhysicsInterpolationAlpha(
        m_config.physicsInterpolationDefault ? static_cast<float>(fixedStep.alpha) : 1.0F);
    SyncParticleRuntime();
    SyncVfxRuntime();
    SyncUIRuntime();
    // Decals age on the SIMULATED clock - the fixed step times the steps that actually ran - so a
    // frame that ran no step does not shorten a decal's life, and a paused simulation freezes it.
    SyncDecal3DObjects(fixedDeltaTimeSeconds * static_cast<float>(fixedStep.stepsToRun));
    SyncCollisionLifecycle();
    SyncRayCast3DObjects();
    SyncHitbox3DObjects();
    SyncInteractionArea3DObjects();
    if (m_simulationExecutionMode == SimulationExecutionMode::Running) {
        const Gameplay::GameplayInput gameplayInput = Gameplay::CollectGameplayInput(*m_inputSystem);
        if (gameplayInput.interactPressed) {
            const Scene::Entity player = Scene::ResolvePossessedPlayer(*m_entityManager);
            const Scene::Entity area = Scene::FindFocusedInteractionArea(*m_entityManager);
            if (player != Scene::kInvalidEntity && area != Scene::kInvalidEntity &&
                m_entityManager->HasComponent<Scene::InteractionArea3DComponent>(area) &&
                Scene::InteractionArea3D::HasInteractor(
                    m_entityManager->GetComponent<Scene::InteractionArea3DComponent>(area), player)) {
                m_eventSystem->QueueEvent(Gameplay::InteractRequestedEvent{player, area});
            }
        }
    }
    SyncLevelStreamer3DObjects();
    SyncReflectionProbe3DObjects();
    SyncWorldPartition3DObjects();
    SyncVisibilityRegion3DObjects();
    {
        std::optional<Math::Frustum> activeFrustum{};
        if (m_activeCamera != Scene::kInvalidEntity &&
            m_entityManager->IsAlive(m_activeCamera) &&
            m_entityManager->HasComponent<Scene::CameraComponent>(m_activeCamera) &&
            m_entityManager->HasComponent<Scene::WorldTransformComponent>(m_activeCamera)) {
            activeFrustum = Math::Frustum::FromViewProjection(
                Scene::Camera3D::BuildViewProjectionMatrix(
                    m_entityManager->GetComponent<Scene::CameraComponent>(m_activeCamera),
                    m_entityManager->GetComponent<Scene::WorldTransformComponent>(
                        m_activeCamera)));
        }
        static_cast<void>(Scene::UpdateVisibleOnScreenNotifier3DObjects(
            *m_entityManager, activeFrustum.has_value() ? &(*activeFrustum) : nullptr));
    }
    SyncScriptRuntime();

    if (m_config.hotReloadEnabled) {
        m_hotReload->Poll(*m_assetManager, *m_assetDatabase, m_timer->GetDeltaTime());
    }

    // Increment 61: portable project-content change detection is distinct from the loaded-asset
    // hot reloader. It can only journal changes and mark matching derived metadata stale; it never
    // refreshes the editor index, enqueues imports, or mutates authoring state automatically.
    static_cast<void>(m_projectChangeWatcher->Poll(m_timer->GetDeltaTime(), *m_assetDatabase,
                                                       *m_derivedArtifactCache));
    m_assetManager->CollectGarbage();

    // Increment 60: one deterministic, main-thread import job at most per engine Update().
    // Queues only progress after callers explicitly enqueue/retry; no file watcher or background
    // import worker is introduced by this maintenance seam.
    static_cast<void>(m_assetImportQueue->Tick());

    if (m_renderDevice->IsUsable()) {
        m_shaderManager->Update(m_timer->GetDeltaTime());
    }

    if (!m_transientSimulationSessionActive) {
        m_checkpointManager->Update(
            m_timer->GetDeltaTime(), *m_entityManager,
            m_sceneGraph->GetChildren(*m_entityManager, Scene::kInvalidEntity));
    }
}

void EngineCore::PublishAreaOverlapLifecycleEvents() {
    const Physics::AreaOverlapQueryResult snapshot =
        Physics::AreaOverlapSystem::Query(*m_entityManager);
    static_cast<void>(Physics::ApplyArea3DOccupancy(*m_entityManager, snapshot));
    const Physics::AreaOverlapLifecycleReport report =
        m_areaOverlapLifecycleTracker.Update(snapshot);

    for (const Physics::AreaOverlapTransition& transition : report.transitions) {
        if (transition.kind == Physics::AreaOverlapTransitionKind::Entered) {
            m_eventSystem->QueueEvent(Physics::AreaOverlapEnteredEvent{transition.pair});
        } else {
            m_eventSystem->QueueEvent(Physics::AreaOverlapExitedEvent{transition.pair});
        }
        // LateUpdate runs after scripts ticked, so slots exist even on frame 1 - no drain needed.
        RaiseContactScriptEvent(
            transition.pair.area, transition.pair.other,
            transition.kind == Physics::AreaOverlapTransitionKind::Entered ? "overlap_enter" : "overlap_exit");
    }

    // Triggers author what the edges MEAN: one-shot, every-enter and while-occupied fire
    // policies with cooldowns and an explicit re-arm, evaluated against the same snapshot and
    // transitions above. A zero first-frame delta freezes trigger clocks but never drops edges.
    const std::vector<Scene::TriggerFiredResult> triggerFires = Scene::UpdateTriggerVolumes(
        *m_entityManager, snapshot, report.transitions, static_cast<float>(m_frameStats.deltaTimeSeconds));
    for (const Scene::TriggerFiredResult& fired : triggerFires) {
        m_eventSystem->QueueEvent(
            Gameplay::TriggerFiredEvent{fired.trigger, fired.interactor, fired.firedCount});
    }
}

void EngineCore::LateUpdate() {
    if (m_frameStats.deltaTimeSeconds > 0.0) {
        const double instantaneousFps = 1.0 / m_frameStats.deltaTimeSeconds;
        constexpr double kFpsSmoothingFactor = 0.1;
        m_frameStats.fps = (m_frameStats.fps <= 0.0)
                                ? instantaneousFps
                                : (m_frameStats.fps * (1.0 - kFpsSmoothingFactor) +
                                   instantaneousFps * kFpsSmoothingFactor);
    }
    UVE_TRACE("LateUpdate: fps={}", m_frameStats.fps);

    PublishAreaOverlapLifecycleEvents();

    Scene::ResolvedAudioListener3DPose listenerPose{};
    if (Scene::AudioListener3D::TryResolveActiveListenerPose(
            *m_entityManager, m_activeCamera, listenerPose)) {
        m_audioSystem->SetListenerPosition(listenerPose.position);
        m_audioSystem->SetListenerOrientation(listenerPose.forward, listenerPose.up);
    }
    m_audioSourceSystem->Sync(*m_entityManager, *m_audioSystem);
    m_audioSystem->Update(static_cast<float>(m_frameStats.deltaTimeSeconds));
}

void EngineCore::Render() {
    if (!m_renderDevice->IsUsable() ||
        (m_windowedRenderingActive && !m_presentationSurfaceReady)) {
        return;
    }
    // Compute first, graphics after: any dispatch enqueued on ComputeSystem this frame is
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
    // IRenderDevice::IsUsable() or call ClearQueue() themselves; the engine does not
    // silently discard work a caller explicitly asked for.
    if (m_computeSystem->GetQueuedDispatchCount() > 0U) {
        std::unique_ptr<Render::ICommandBuffer> computeCommands = m_renderDevice->CreateCommandBuffer();
        if (computeCommands != nullptr) {
            const std::size_t recordedDispatches = m_computeSystem->ExecuteQueuedDispatches(*computeCommands);
            if (recordedDispatches > 0U) {
                m_renderDevice->Submit(std::move(computeCommands));
            }
        } else {
            // No buffer, no honest way to record: drop the queue loudly rather than carrying
            // stale work into a later frame where its resources may already be gone.
            UVE_WARNING("EngineCore: render device returned no command buffer for {} queued "
                        "compute dispatch(es) - dropping them",
                        m_computeSystem->GetQueuedDispatchCount());
            m_computeSystem->ClearQueue();
        }
    }

    m_renderer3D->SetUIRuntime(&m_uiRuntime);
    if (m_activeCamera != Scene::kInvalidEntity) {
        if (m_vfxWorld != nullptr) {
            Math::Vector3 cameraPos{};
            Math::Vector3 cameraForward{0.0F, 0.0F, -1.0F};
            if (m_entityManager->IsAlive(m_activeCamera) &&
                m_entityManager->HasComponent<Scene::WorldTransformComponent>(m_activeCamera)) {
                const Scene::WorldTransformComponent& camWorld =
                    m_entityManager->GetComponent<Scene::WorldTransformComponent>(m_activeCamera);
                cameraPos = camWorld.worldPosition;
                Math::Quaternion rot = camWorld.worldRotation;
                if (Math::TryNormalize(rot, rot)) {
                    cameraForward = Math::RotateVector(rot, Math::Vector3{0.0F, 0.0F, -1.0F});
                }
            }
            Render::ParticleRenderBridge::ExtractVfx(*m_vfxWorld, m_vfxDrawList, cameraPos, cameraForward);
            m_renderer3D->SetVfxDrawList(&m_vfxDrawList);
        } else {
            m_vfxDrawList.Clear();
            m_renderer3D->SetVfxDrawList(nullptr);
        }
        const bool hasParticles = m_particleRuntime != nullptr && m_particleRuntime->GetInstanceCount() > 0U;
        std::optional<Render::ViewportRect> contentRegion = m_editorViewportRegion;
        if (!contentRegion.has_value() && m_windowedRenderingActive &&
            m_config.stretchMode == Platform::StretchMode::Viewport) {
            const Window::PresentationLayout layout =
                ComputeWindowPresentationLayout(m_config, m_windowManager->GetWidth(),
                                                   m_windowManager->GetHeight());
            if (!layout.integerScaleAvailable && !m_integerScaleFallbackLogged) {
                UVE_WARNING("EngineCore: integer-only viewport scaling cannot fit below the reference "
                            "resolution; using fractional aspect-preserving scaling until an integer scale is available");
                m_integerScaleFallbackLogged = true;
            }
            if (layout.width > 0U && layout.height > 0U &&
                (layout.x != 0U || layout.y != 0U || layout.width != m_windowManager->GetWidth() ||
                 layout.height != m_windowManager->GetHeight())) {
                contentRegion = Render::ViewportRect{
                    Math::Vector2i{static_cast<std::int32_t>(layout.x), static_cast<std::int32_t>(layout.y)},
                    Math::Vector2i{static_cast<std::int32_t>(layout.width),
                                      static_cast<std::int32_t>(layout.height)}};
            }
        }
        if (contentRegion.has_value() && !m_editorViewportRegion.has_value()) {
            // Clear the bars before the scaled viewport writes its own region. These are two
            // ordered command submissions drained by the same Present() call.
            m_renderSystem->BeginFrame();
            Render::ICommandBuffer& clearCommands = m_renderSystem->GetFrameCommandBuffer();
            Render::RenderPassDesc clearDesc;
            clearDesc.colorAttachment = Render::kInvalidTextureHandle;
            clearDesc.depthAttachment = Render::kInvalidTextureHandle;
            clearDesc.colorLoadOp = Render::LoadOp::Clear;
            clearDesc.depthLoadOp = Render::LoadOp::DontCare;
            clearDesc.clearColor = m_config.backgroundColor;
            clearCommands.BeginRenderPass(clearDesc);
            clearCommands.EndRenderPass();
            m_renderSystem->EndFrame();
        }
        if (contentRegion.has_value()) {
            m_renderer3D->RenderFrameToRegion(*m_entityManager, m_activeCamera, *contentRegion,
                                                  hasParticles ? m_particleRuntime.get() : nullptr);
        } else if (hasParticles) {
            m_renderer3D->RenderFrameWithParticleRuntime(*m_entityManager, m_activeCamera, *m_particleRuntime);
        } else {
            m_renderer3D->RenderFrame(*m_entityManager, m_activeCamera);
        }
    } else {
        UVE_TRACE("Render (no-op)");
        if (m_windowedRenderingActive) {
            // An empty editor/game window still needs a real presented frame. Clear the
            // backend's default framebuffer without creating a scene, camera, mesh, or light.
            // This keeps the no-scene state visible and preserves the empty-scene contract.
            // Confined to the editor viewport region (when set) so an empty-scene editor window
            // clears only its own 3D viewport panel, matching every other render path this frame.
            m_renderSystem->BeginFrame();
            Render::ICommandBuffer& commandBuffer = m_renderSystem->GetFrameCommandBuffer();
            Render::RenderPassDesc passDesc;
            passDesc.colorAttachment = Render::kInvalidTextureHandle;
            passDesc.depthAttachment = Render::kInvalidTextureHandle;
            passDesc.colorLoadOp = Render::LoadOp::Clear;
            passDesc.depthLoadOp = Render::LoadOp::DontCare;
            passDesc.clearColor = m_config.backgroundColor;
            passDesc.viewportOverride = m_editorViewportRegion;
            commandBuffer.BeginRenderPass(passDesc);
            commandBuffer.EndRenderPass();
            m_renderSystem->EndFrame();
        }
    }

    if (m_windowedRenderingActive && m_presentationSurfaceReady) {
        if (m_postRenderCallback) {
            m_postRenderCallback();
        }
        m_renderDevice->Present();
    }
}

void EngineCore::EndFrame() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    m_frameStats.frameTimeSeconds = std::chrono::duration<double>(now - m_frameStartTime).count();
    if (m_config.dynamicRenderResolutionEnabled && m_windowedRenderingActive &&
        m_presentationSurfaceReady &&
        m_dynamicRenderResolutionController.ObserveFrameTime(m_frameStats.frameTimeSeconds)) {
        m_currentRenderResolutionScale =
            m_dynamicRenderResolutionController.GetCurrentScale();
        UVE_INFO("EngineCore: dynamic 3D render scale adjusted to {} (smoothed frame time {} ms, target {} ms)",
                 m_currentRenderResolutionScale,
                 m_dynamicRenderResolutionController.GetSmoothedFrameTimeMilliseconds(),
                 m_config.renderResolutionTargetFrameTimeMilliseconds);
    }
    UVE_TRACE("EndFrame {}: delta={} total={} fps={} frameTime={}", m_frameStats.frameNumber,
              m_frameStats.deltaTimeSeconds, m_frameStats.totalTimeSeconds, m_frameStats.fps,
              m_frameStats.frameTimeSeconds);
}

void EngineCore::TickFrame() {
    UVE_ASSERT(m_state == EngineState::Running);
    const bool windowedRenderingActive = m_windowedRenderingActive && m_windowManager != nullptr;
    const bool focused = windowedRenderingActive && (m_windowManager->IsFocused() || (m_focusProvider && m_focusProvider()));
    const std::uint32_t frameRateCap = SelectFrameRateCap(
        windowedRenderingActive, focused, m_config.focusedFrameRateCap,
        m_config.unfocusedFrameRateCap);
    auto now = std::chrono::steady_clock::now();
    const FramePacingDecision pacingDecision = ComputeFramePacingDecision(
        frameRateCap, m_lastFrameRateCap, m_nextFrameDeadline, now);
    if (pacingDecision.frameRateCap == 0U) {
        m_nextFrameDeadline.reset();
        m_lastFrameRateCap = 0U;
    } else {
        if (pacingDecision.sleepUntil.has_value()) {
            // The OS wakes a sleeper late (a millisecond or more, far more on some systems): sleep to
            // just short of the deadline, then yield the last stretch, so frames come evenly spaced
            // instead of jittering around the cap - uneven frames read as stutter even at a high rate.
            constexpr auto kSpinMargin = std::chrono::microseconds(1500);
            const auto deadline = *pacingDecision.sleepUntil;
            if (deadline - now > kSpinMargin) {
                std::this_thread::sleep_until(deadline - kSpinMargin);
            }
            while (std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
            now = std::chrono::steady_clock::now();
        }
        m_nextFrameDeadline = now + pacingDecision.framePeriod;
        m_lastFrameRateCap = pacingDecision.frameRateCap;
    }
    BeginFrame();
    const bool traceFrames = FrameTrace::Instance().IsEnabled();
    const std::chrono::steady_clock::time_point updateBegin =
        traceFrames ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    Update();
    const std::chrono::steady_clock::time_point lateUpdateBegin =
        traceFrames ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    LateUpdate();
    const std::chrono::steady_clock::time_point renderBegin =
        traceFrames ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    Render();
    const std::chrono::steady_clock::time_point renderEnd =
        traceFrames ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    EndFrame();
    if (traceFrames) {
        const auto secondsBetween = [](const std::chrono::steady_clock::time_point from,
                                       const std::chrono::steady_clock::time_point to) {
            return std::chrono::duration<double>(to - from).count();
        };
        FrameTrace::Instance().Record(m_frameStats.frameNumber, m_frameStats.frameTimeSeconds,
                                      m_frameStats.deltaTimeSeconds,
                                      secondsBetween(updateBegin, lateUpdateBegin),
                                      secondsBetween(lateUpdateBegin, renderBegin),
                                      secondsBetween(renderBegin, renderEnd));
    }
}

int EngineCore::Run(int frameCount) {
    UVE_ASSERT(frameCount >= 0);
    try {
        Init();
        if (!Load()) {
            Shutdown();
            return 1;
        }
        for (int frameIndex = 0; frameIndex < frameCount && !m_quitRequested; ++frameIndex) {
            TickFrame();
        }
        Shutdown();
        return 0;
    } catch (const std::exception& exception) {
        UVE_FATAL("EngineCore: Run() caught an unhandled exception - shutting down: {}", exception.what());
    } catch (...) {
        UVE_FATAL("EngineCore: Run() caught an unhandled non-std::exception - shutting down");
    }

    // Reached only via one of the catches above. Shutdown() tears down every subsystem Init()
    // constructed, in reverse order, and is only safe to call once m_state has actually reached
    // Running (IsValidTransition() only allows Running -> ShuttingDown) - an exception thrown
    // partway through Init() itself leaves m_state at Initializing, where subsystems Init() had
    // not yet reached are still the null/empty state their declarations default-initialize them
    // to, so Shutdown()'s unconditional dereferences (e.g. m_eventSystem->Clear()) would
    // themselves crash. In that case teardown is left to EngineCore's own destructor, which
    // runs normally once this function returns and `this` goes out of scope in the caller, and
    // whose RAII members already know how to unwind whatever subset of construction completed.
    if (m_state == EngineState::Running) {
        try {
            Shutdown();
        } catch (const std::exception& exception) {
            UVE_FATAL("EngineCore: Shutdown() itself threw while recovering from the exception above: {}",
                       exception.what());
        } catch (...) {
            UVE_FATAL("EngineCore: Shutdown() itself threw a non-std::exception while recovering from "
                       "the exception above");
        }
    }
    return kUnhandledExceptionExitCode;
}

void EngineCore::RequestQuit() noexcept {
    m_quitRequested = true;
}

bool EngineCore::SetApplicationBackgroundColor(const std::array<float, 4U>& color) noexcept {
    for (const float channel : color) {
        if (!std::isfinite(channel) || channel < 0.0F || channel > 1.0F) {
            return false;
        }
    }
    m_config.backgroundColor = color;
    if (m_renderer3D != nullptr) {
        m_renderer3D->SetSceneClearColor(color);
    }
    return true;
}

std::size_t EngineCore::GetActiveScriptInstanceCount() const noexcept {
    return m_uvScripts.size();
}

void EngineCore::Shutdown() {
    TransitionState(EngineState::ShuttingDown);
    m_simulationExecutionMode = SimulationExecutionMode::Running;
    m_singleSimulationStepPending = false;
    m_transientSimulationSessionActive = false;
    m_simulationSpeedScale = 1.0F;
    UVE_INFO("EngineCore: shutting down");

    // Exact reverse of Init()'s construction order: CheckpointManager,
    // then SaveGameSystem, then AudioSourceSystem, then AudioSystem, then AudioDevice, then
    // InputSystem, then MobileGestureSystem, then MobileInputSystem, then GamepadInputSystem, then
    // RaycastSystem, then PhysicsSystem, then CollisionSystem, then Renderer3D, then LightSystem, then MeshRenderer, then CameraSystem, then RenderSystem, then ShaderManager, then RenderDevice
    // (destroying the demo triangle's shader program and vertex buffer first, if windowed rendering
    // was active), then WindowManager,
    // then FileSystem, then AssetBundle, then AssetImporter, then
    // AssetManager (its destructor blocks until every in-flight load job
    // finishes), then HotReload, then PrefabSystem, then SceneSerializer,
    // then AssetDatabase, then SceneGraph, then EntityManager, then
    // EventSystem, then Timer, then ThreadPool, then MemoryManager, then
    // ConfigManager (loaded early so its settings can override project values),
    // then Logger, then CommandLine. The final log message is emitted before the
    // logger itself is torn down, so it is guaranteed to be recorded.
    for (const Config::SettingsObserverSubscription& sub : m_projectSettingsSubscriptions) {
        static_cast<void>(m_projectSettings.Unsubscribe(sub));
    }
    m_projectSettingsSubscriptions.clear();
    m_uvScriptFailedSources.clear();
    m_uvScripts.clear(); // hosts point into the entity manager, so they go first
    m_vfxEntityInstances.clear();
    m_vfxDrawList.Clear();
    m_services.reset();
    m_checkpointManager.reset();
    m_saveGameSystem.reset();
    m_audioSourceSystem.reset();
    // The provider is borrowed by the audio system, so it goes after the system that holds it.
    m_audioSystem.reset();
    m_audioOcclusion.reset();
    m_audioDevice.reset();
    m_inputSystem.reset();
    m_mobileGestureSystem.reset();
    m_mobileInputSystem.reset();
    m_gamepadInputSystem.reset();
    m_navigationRuntime.reset();
    m_vfxWorld.reset();
    m_particleRuntime.reset();
    m_particleEmitRemainder.clear();
    m_raycastSystem.reset();
    m_physicsQuerySystem.reset();
    m_physicsSystem.reset();
    m_physicsConstraintSystem.reset();
    m_collisionSystem.reset();
    m_areaOverlapLifecycleTracker.Reset();
    m_hitboxStrikeLifecycleTracker.Reset();
    m_pendingLagShots.clear();
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
    m_animationClips.clear(); // the handles release into the manager, so they go first
    m_assetManager.reset();
    m_hotReload.reset();
    m_prefabSystem.reset();
    m_sceneSerializer.reset();
    m_projectFileIndex.reset();
    m_projectChangeWatcher.reset();
    m_derivedArtifactCache.reset();
    m_assetDatabase.reset();
    m_sceneGraph.reset();

    // EntityManager's destructor frees every remaining live entity's
    // component memory — this must happen before MemoryManager's leak
    // report below, or a perfectly correct program would show false-
    // positive leaks for every still-alive entity at shutdown.
    m_entityManager.reset();

    m_eventSystem->Clear();
    m_eventSystem.reset();
    m_timer.reset();

    // ThreadPool's destructor blocks until every worker has drained its
    // queue and joined — no jobs are silently dropped on shutdown.
    m_threadPool.reset();

    // Leak report must run while the logger is still alive; the debug-only
    // assertion turns a leak into an immediate development-time failure
    // without ever affecting Release builds.
    m_memoryManager->LogLeakReport();
#if UVE_DEBUG
    UVE_ASSERT(m_memoryManager->GetActiveAllocationCount() == 0);
#endif
    m_memoryManager.reset();
    m_configManager.reset();

    static_cast<void>(FrameTrace::Instance().Flush());

    UVE_INFO("EngineCore: shutdown complete");
    m_logger->Shutdown();
    m_logger.reset();
    m_commandLine.reset();

    TransitionState(EngineState::Shutdown);
}

EngineState EngineCore::GetState() const noexcept {
    return m_state;
}

const FrameStats& EngineCore::GetFrameStats() const noexcept {
    return m_frameStats;
}

Scene::ParticleRuntimeSnapshot EngineCore::GetParticleRuntimeSnapshot() const {
    return m_particleRuntime != nullptr ? m_particleRuntime->GetSnapshot() : Scene::ParticleRuntimeSnapshot{};
}

VFX::VfxWorld& EngineCore::GetVfxWorld() noexcept {
    UVE_ASSERT(m_vfxWorld != nullptr);
    return *m_vfxWorld;
}

const VFX::VfxWorld& EngineCore::GetVfxWorld() const noexcept {
    UVE_ASSERT(m_vfxWorld != nullptr);
    return *m_vfxWorld;
}

bool EngineCore::SetSimulationExecutionMode(const SimulationExecutionMode mode) noexcept {
    if (m_state != EngineState::Running) {
        return false;
    }
    m_simulationExecutionMode = mode;
    if (mode == SimulationExecutionMode::Running) {
        m_singleSimulationStepPending = false;
    }
    return true;
}

SimulationExecutionMode EngineCore::GetSimulationExecutionMode() const noexcept {
    return m_simulationExecutionMode;
}

bool EngineCore::RequestSingleSimulationStep() noexcept {
    if (m_state != EngineState::Running || m_simulationExecutionMode != SimulationExecutionMode::Paused ||
        m_singleSimulationStepPending) {
        return false;
    }
    m_singleSimulationStepPending = true;
    return true;
}

bool EngineCore::SetTransientSimulationSessionActive(const bool active) noexcept {
    if (m_state != EngineState::Running) {
        return false;
    }
    if (active && !m_transientSimulationSessionActive) {
        // Play starts: a fresh clock, no messages from last time, the game data empty.
        ResetScriptGameState();
    }
    m_transientSimulationSessionActive = active;
    return true;
}

bool EngineCore::IsTransientSimulationSessionActive() const noexcept {
    return m_transientSimulationSessionActive;
}

bool EngineCore::SetEditorPlaySceneName(const std::string_view sceneName) noexcept {
    if (m_state != EngineState::Running || sceneName.size() > Window::kMaximumWindowTitleBytes ||
        sceneName.find('\0') != std::string_view::npos) {
        return false;
    }
    const bool previousEditorPlayMode = m_config.editorPlayMode;
    try {
        std::string candidateSceneName{sceneName};
        m_config.editorPlayMode = !candidateSceneName.empty();
        const std::string title = Window::FormatWindowTitle(
            m_config.windowTitleFormat, m_config.windowTitle, m_config.projectDisplayName,
            candidateSceneName, m_config.editorPlayMode, m_config.appendSceneNameInEditorPlayMode);
        m_editorPlaySceneName = std::move(candidateSceneName);
        if (m_windowManager != nullptr) {
            m_windowManager->SetWindowTitle(title);
        }
        return true;
    } catch (...) {
        m_config.editorPlayMode = previousEditorPlayMode;
        return false;
    }
}

bool EngineCore::SetSimulationSpeedScale(const float speedScale) noexcept {
    if (m_state != EngineState::Running || !std::isfinite(speedScale) || speedScale < 0.1F || speedScale > 32.0F) {
        return false;
    }
    m_simulationSpeedScale = speedScale;
    return true;
}

float EngineCore::GetSimulationSpeedScale() const noexcept {
    return m_simulationSpeedScale;
}

void EngineCore::SetActiveCamera(Scene::Entity cameraEntity) noexcept {
    m_activeCamera = cameraEntity;
}

Scene::Entity EngineCore::GetActiveCamera() const noexcept {
    return m_activeCamera;
}

void EngineCore::SetEditorViewportRegion(std::optional<Render::ViewportRect> region) noexcept {
    // ViewportRect is a signed rect now (Math::RectInt); the old four-uint32 struct made a
    // negative region unrepresentable, so clamp one here to keep that invariant for the
    // projection/drawable-size readers rather than letting a cast wrap it.
    if (region.has_value()) {
        region->position = Math::Max(region->position, Math::Vector2i{0, 0});
        region->size = Math::Max(region->size, Math::Vector2i{0, 0});
    }
    m_editorViewportRegion = region;
}

void EngineCore::SetPostRenderCallback(std::function<void()> callback) {
    m_postRenderCallback = std::move(callback);
}

EngineServices& EngineCore::GetServices() {
    UVE_ASSERT(m_services.has_value());
    return m_services.value(); // throws std::bad_optional_access in Release if called before Init()
}

Version EngineCore::GetEngineVersion() noexcept {
    return Version{0, 5, 3, 0};
}


void ApplyEngineConfigToRenderer3D(const EngineConfig& config, Render::IRenderer3D& renderer) {
    renderer.SetSceneClearColor(config.backgroundColor);
    Render::PostProcessSettings postProcessSettings;
    postProcessSettings.nearestUpscalingEnabled =
        config.spatialUpscalingMethod == SpatialUpscalingMethod::Nearest;
    postProcessSettings.acesToneMappingEnabled = config.toneMappingMethod != ToneMappingMethod::LinearClamp;
    postProcessSettings.toneMappingMethod = static_cast<std::uint32_t>(config.toneMappingMethod);
    postProcessSettings.sharpeningAmount = config.sharpeningAmount;
    postProcessSettings.ditheringEnabled = config.ditheringEnabled;
    postProcessSettings.fastApproximateAAEnabled =
        config.postProcessAAMethod == PostProcessAAMethod::FastApproximate;
    postProcessSettings.fastApproximateAAQuality = config.screenSpaceAAQuality;
    postProcessSettings.ssaoEnabled = config.ssaoEnabled;
    postProcessSettings.ssaoRadius = config.ssaoRadius;
    postProcessSettings.ssaoIntensity = config.ssaoIntensity;
    postProcessSettings.ssaoPower = config.ssaoPower;
    postProcessSettings.ssaoQuality = config.ssaoQuality;
    postProcessSettings.ssaoBlurEnabled = config.ssaoBlurEnabled;
    // Contact shadows: the enable/length/steps settings predate the pass that consumes them, so
    // this is where they finally reach a renderer.
    postProcessSettings.contactShadowsEnabled = config.contactShadowsEnabled;
    postProcessSettings.contactShadowLengthMeters = static_cast<float>(config.contactShadowsLength);
    postProcessSettings.contactShadowThicknessMeters = static_cast<float>(config.contactShadowsThickness);
    postProcessSettings.contactShadowStrength = static_cast<float>(config.contactShadowsStrength);
    postProcessSettings.contactShadowStepCount =
        static_cast<std::uint32_t>(std::max<std::int64_t>(config.contactShadowsStepCount, 0));
    postProcessSettings.ssrEnabled = config.ssrEnabled;
    postProcessSettings.ssrMaxSteps = static_cast<std::uint32_t>(std::max<std::int64_t>(config.ssrMaxSteps, 1));
    postProcessSettings.ssrDepthTolerance = static_cast<float>(config.ssrDepthTolerance);
    postProcessSettings.ssrFadeIn = static_cast<float>(config.ssrFadeIn);
    postProcessSettings.ssrFadeOut = static_cast<float>(config.ssrFadeOut);
    renderer.SetPostProcessSettings(postProcessSettings);
    renderer.SetShadowBiasDefaults(config.shadowBiasDefault, config.shadowNormalBiasDefault);
    // The project's "Point/Spot Cube Resolution" is the requested atlas tile edge. Spot shadows use
    // it as the tile size; point lights will use the same number for their six faces once their
    // atlas path exists, so one authored setting keeps meaning one thing.
    renderer.SetShadowAtlasTileResolution(
        static_cast<std::uint32_t>(std::max<std::int64_t>(config.pointSpotShadowResolution, 0)));
    Render::CullingSettings cullingSettings;
    cullingSettings.frustumCullingEnabled = config.frustumCullingMode == FrustumCullingMode::Enabled;
    cullingSettings.gpuOcclusionCullingEnabled = config.gpuOcclusionCullingEnabled;
    renderer.SetCullingSettings(cullingSettings);
    Render::SamplerWrap materialWrap = Render::SamplerWrap::Repeat;
    if (config.defaultTextureRepeatMode == 0) {
        materialWrap = Render::SamplerWrap::ClampToEdge;
    } else if (config.defaultTextureRepeatMode == 2) {
        materialWrap = Render::SamplerWrap::MirroredRepeat;
    }
    renderer.SetMaterialSamplerWrap(
        materialWrap,
        static_cast<float>(std::clamp<std::int64_t>(config.defaultAnisotropyLevel, 1, 16)));
    if (config.requestedRendererProfile != Platform::RendererProfile::ForwardPlus ||
        renderer.GetRendererProfileResolution().requested != config.requestedRendererProfile) {
        renderer.SetRendererProfile(config.requestedRendererProfile);
    }
    if (config.activeQualityTier != EngineConfig{}.activeQualityTier) {
        renderer.SetScalabilityMode(static_cast<std::int32_t>(config.activeQualityTier));
    }
}

namespace {

/// Lightweight zero-cost renderer installed when EngineConfig::skip3dRenderer is true (Hub,
/// Setup): satisfies EngineServices's non-null IRenderer3D contract without compiling 3D
/// shaders or allocating shadow/HDR/post-process render targets.
class NoOpRenderer3D final : public Render::IRenderer3D {
public:
    void RenderFrame(Scene::IEntityManager& /*entityManager*/, Scene::Entity /*cameraEntity*/) override {}
    [[nodiscard]] bool ResizeTargets(const std::uint32_t /*width*/, const std::uint32_t /*height*/) override {
        return true;
    }
    void SetPhysicsInterpolationAlpha(const float /*alpha*/) noexcept override {}
    [[nodiscard]] Render::Renderer3DFrameDiagnostics GetLastFrameDiagnostics() const noexcept override {
        return {};
    }
};

} // namespace

std::unique_ptr<Render::IRenderer3D> EngineCore::MakeRenderer3D(Render::IMeshRenderer& meshRenderer,
                                                                  Render::ICameraSystem& cameraSystem,
                                                                  Render::ILightSystem& lightSystem,
                                                                  Events::IEventSystem& eventSystem) {
    if (m_config.skip3dRenderer) {
        return std::make_unique<NoOpRenderer3D>();
    }
    std::unique_ptr<Render::IRenderer3D> renderer = std::make_unique<Render::Renderer3D>(
        *m_renderDevice, *m_renderSystem, meshRenderer, cameraSystem, lightSystem, *m_shaderManager,
        *m_assetManager, *m_assetDatabase, eventSystem, m_config.renderTargetWidth, m_config.renderTargetHeight,
        m_config.ambientColor, m_config.shadowMapResolution, m_config.shadowMapHalfExtent,
        m_config.shadowMapNearPlane, m_config.shadowMapFarPlane, m_config.shadowFrustumPadding,
        m_config.shadowCascadeSplitLambda, m_config.shadowCascadeBlendRatio, m_config.shadowPcfKernelRadius);
    ApplyEngineConfigToRenderer3D(m_config, *renderer);
    return renderer;
}


namespace {

/// The engine's DocumentScene: owns the scene half, borrows the rest from the engine.
class OwnedDocumentScene final : public DocumentScene {
public:
    OwnedDocumentScene(Memory::IAllocator& allocator, std::unique_ptr<Events::EventSystem> events)
        : m_events(std::move(events)), m_entities(allocator, *m_events) {}

    void Bind(const EngineServices& engineServices, std::unique_ptr<Render::IRenderer3D> renderer) {
        m_renderer = std::move(renderer);
        m_services.emplace(engineServices.WithScene(m_entities, m_graph, m_cameraSystem, m_meshRenderer,
                                                       m_lightSystem, *m_renderer));
    }
    [[nodiscard]] Render::IMeshRenderer& MeshRenderer() noexcept { return m_meshRenderer; }
    [[nodiscard]] Render::ICameraSystem& CameraSystem() noexcept { return m_cameraSystem; }
    [[nodiscard]] Render::ILightSystem& LightSystem() noexcept { return m_lightSystem; }
    [[nodiscard]] Events::IEventSystem& Events() noexcept { return *m_events; }

    ~OwnedDocumentScene() override {
        m_services.reset();
        m_renderer.reset();
    }

    [[nodiscard]] EngineServices& GetServices() noexcept override { return *m_services; }

private:
    std::unique_ptr<Events::EventSystem> m_events;
    Scene::EntityManager m_entities;
    Scene::SceneGraph m_graph;
    Render::MeshRenderer m_meshRenderer;
    Render::CameraSystem m_cameraSystem;
    Render::LightSystem m_lightSystem;
    std::unique_ptr<Render::IRenderer3D> m_renderer;
    std::optional<EngineServices> m_services;
};

} // namespace

std::unique_ptr<DocumentScene> EngineCore::CreateDocumentScene() {
    if (!m_services.has_value()) {
        return nullptr;
    }
    auto scene = std::make_unique<OwnedDocumentScene>(m_memoryManager->GetDefaultAllocator(),
                                                         std::make_unique<Events::EventSystem>());
    scene->Bind(*m_services, MakeRenderer3D(scene->MeshRenderer(), scene->CameraSystem(),
                                                  scene->LightSystem(), scene->Events()));
    return scene;
}

} // namespace UVE::Core
