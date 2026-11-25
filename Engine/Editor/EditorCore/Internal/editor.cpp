// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "montage_sequencer_component.h"
#include "glfw_window_handle.h"
#include "shell/editor_shell.h"
#include "shell/widget/editor_widget_icons.h"
#include "logging_macros.h"
#include "animation_clip_asset.h"
#include "animation_library_asset.h"
#include "asset_import_queue.h"
#include "montage_library_asset.h"
#include "fbx_mesh_converter.h"
#include "gltf_metadata.h"
#include "editor.h"
#include "editor_entity_thumbnail.h"
#include "model_animations.h"

#include "animation_graph_editing.h"
#include "editor_settings.h"
#include "editor_settings_binding.h"
#include "editor_render_stats.h"
#include "trs.h"

#include "editor_entity_label.h"
#include "editor_text_search.h"
#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <functional>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <GLFW/glfw3.h>

#include "bmp_metadata.h"
#include "jpeg_metadata.h"
#include "mesh_asset.h"
#include "matrix4x4.h"
#include "retarget_humanoid.h"
#include "skeleton_asset.h"
#include "mesh_skinning.h"
#include "material_asset.h"
#include "shader_asset.h"
#include "png_metadata.h"
#include "tga_metadata.h"
#include "texture_asset.h"
#include "uve_file_envelope.h"
#include "i_config_manager.h"
#include "raycast_query.h"
#include "render_resource_descs.h"
#include "editor_project_package.h"
#include "area_component.h"
#include "camera_component.h"
#include "character_controller_component.h"
#include "collider_component.h"
#include "all_objects_3d.h"
#include "preview_lighting.h"
#include "all_objects_canvas.h"
#include "scene_object_type.h"
#include "spawn_point_query.h"
#include "editor_content_catalogue.h"
#include "engine_project_settings.h"
#include "key_code.h"
#include "scene_folder.h"
#include "scene_root.h"
#include "hierarchy_component.h"
#include "light_component.h"
#include "editor_internal_entity_component.h"
#include "name_component.h"
#include "primitive_mesh_component.h"
#include "prefab_instance_component.h"
#include "script_component.h"
#include "ui_button_component.h"
#include "canvas_component.h"
#include "ui_text_component.h"
#include "widget_blueprint_asset.h"
#include "blueprint_asset.h"
#include "visibility_component.h"
#include "world_transform_component.h"

namespace UVE::Editor {

namespace {

constexpr float kVectorEpsilon = 0.00001F;
/// The bottom dock's default height (Default Layout puts it back).
constexpr float kAssetsPanelHeight = 192.0F;
/// The longest name the editor lets an object have (bytes).
constexpr std::size_t kMaximumEditorEntityNameBytes = 96U;
constexpr float kMinimumLocalScale = 0.001F;

[[nodiscard]] Math::Vector3 PrimitiveColliderHalfExtents(const Scene::PrimitiveMeshKind kind) noexcept {
    // Sourced from the object policies rather than restated here. These half-extents are the same
    // authored defaults BoxMesh3D/SphereMesh3D/PlaneMesh3D attach at creation, and a second copy
    // of them is a second place to edit: changing a primitive's collider in its definition while
    // this switch kept the old number would give an entity different collision depending on
    // whether it was created as that kind or converted to it - a difference nothing would report.
    switch (kind) {
        case Scene::PrimitiveMeshKind::Cube:
            return Scene::BoxMesh3D::DefaultColliderHalfExtents();
        case Scene::PrimitiveMeshKind::UVSphere:
            return Scene::SphereMesh3D::DefaultColliderHalfExtents();
        case Scene::PrimitiveMeshKind::Plane:
            return Scene::PlaneMesh3D::DefaultColliderHalfExtents();
        case Scene::PrimitiveMeshKind::Capsule:
        case Scene::PrimitiveMeshKind::Cylinder:
            return Math::Vector3{0.5F, 1.0F, 0.5F};
    }
    return Scene::BoxMesh3D::DefaultColliderHalfExtents();
}

// Half-extents alone cannot describe a round shape. A capsule or cylinder collider carries radius
// and height as well, and the field defaults (radius 0.5, height 1.0) put the two capsule caps
// exactly together - a sphere with no barrel. A primitive mesh wrapped by a collider born on the
// defaults is therefore wrapped in a ball, which is why picking a Capsule mesh used to produce a
// collider that did not look like the thing on screen.
//
// One function for both paths that can create such a pairing - the mesh being changed under an
// existing collider, and a collider being added to an existing mesh - so the two cannot drift into
// different ideas of what "the size of the mesh" means.
void FitColliderToPrimitiveKind(Scene::ColliderComponent& collider,
                                   const Scene::PrimitiveMeshKind kind) noexcept {
    const Math::Vector3 halfExtents = PrimitiveColliderHalfExtents(kind);
    collider.halfExtents = halfExtents;
    if (collider.shapeType == Scene::ColliderShapeType::Capsule ||
        collider.shapeType == Scene::ColliderShapeType::Cylinder) {
        collider.radius = halfExtents.x;
        collider.height = halfExtents.y * 2.0F;
    } else if (collider.shapeType == Scene::ColliderShapeType::Sphere) {
        collider.radius = halfExtents.x;
    }
}

/// Square resolution rendered for each Asset Table mesh and entity thumbnail (see
/// Editor::GetMeshThumbnail), and for each material thumbnail. Several times the size a tile or
/// an Inspector slot shows: PRISM shrinks a picture by averaging the pixels under each screen pixel
/// (Canvas::resample), so the extra resolution is supersampling - smooth silhouettes, no stair steps -
/// and a Large tile or a high-DPI screen still has detail to spare.
constexpr int kMeshThumbnailSize = 384;
/// See kMeshThumbnailSize.
constexpr int kMaterialThumbnailSize = 384;

constexpr float kMinimum2DCanvasZoom = 0.10F;
constexpr float kMaximum2DCanvasZoom = 4.00F;

// Side-panel widths derived from the editor's visual reference (a 1280px-wide window shows the
// Scene panel at ~216px and the Inspector at ~256px): the proportional term hits those exact
// values at 1280, and the clamps keep both sensible on very small and very large windows. Narrower
// than the previous 0.19/0.22 (243/281 at 1280) so the center viewport - the primary workspace -
// keeps the majority of the width instead of being squeezed by the side panels.

[[nodiscard]] bool IsWhitespaceOnly(const std::string_view value) noexcept {
    return std::all_of(value.begin(), value.end(), [](const char character) noexcept {
        return std::isspace(static_cast<unsigned char>(character)) != 0;
    });
}

[[nodiscard]] bool AreTransformsEqual(const Scene::TransformComponent& lhs,
                                         const Scene::TransformComponent& rhs) noexcept {
    return lhs.localPosition.x == rhs.localPosition.x && lhs.localPosition.y == rhs.localPosition.y &&
           lhs.localPosition.z == rhs.localPosition.z && lhs.localRotation.x == rhs.localRotation.x &&
           lhs.localRotation.y == rhs.localRotation.y && lhs.localRotation.z == rhs.localRotation.z &&
           lhs.localRotation.w == rhs.localRotation.w && lhs.localScale.x == rhs.localScale.x &&
           lhs.localScale.y == rhs.localScale.y && lhs.localScale.z == rhs.localScale.z;
}

[[nodiscard]] std::filesystem::path MakeRecoveryPath(const std::filesystem::path& scenePath) {
    std::filesystem::path recoveryPath = scenePath;
    recoveryPath += ".editor-recovery";
    return recoveryPath;
}

[[nodiscard]] bool IsRawSourceImagePath(const std::filesystem::path& path) {
    std::string extension = path.extension().generic_string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" ||
           extension == ".tga";
}

bool DecodeRawImageThumbnailPixels(const std::filesystem::path& absolutePath,
                                                     std::uint32_t& outWidth, std::uint32_t& outHeight,
                                                     std::vector<std::byte>& outPixels);

[[nodiscard]] bool IsFinite(const float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] Scene::Objects::SceneObjectKind ToSceneObjectKind(const EditorEntityKind kind) noexcept {
    using Kind = Scene::Objects::SceneObjectKind;
    switch (kind) {
        case EditorEntityKind::Empty:
            return Kind::Object3D;
        case EditorEntityKind::Camera:
            return Kind::Camera3D;
        case EditorEntityKind::DirectionalLight:
            return Kind::Light3D;
        case EditorEntityKind::CollisionBox:
            return Kind::Collider3D;
        case EditorEntityKind::Cube:
            return Kind::BoxMesh3D;
        case EditorEntityKind::UVSphere:
            return Kind::SphereMesh3D;
        case EditorEntityKind::Plane:
            return Kind::PlaneMesh3D;
    }
    return Kind::Object3D;
}

}

Editor::Editor(Core::EngineServices& services, std::filesystem::path activeScenePath,
                     const std::size_t historyCapacity, Core::ISimulationControl* const simulationControl)
    : m_services(&services),
      m_simulationControl(simulationControl),
      m_activeScenePath(std::move(activeScenePath)),
      m_historyCapacity(std::max<std::size_t>(std::size_t{1U}, historyCapacity)) {
    if (!RegisterEditorSettings(m_settingsRegistry)) {
        throw std::logic_error("Failed to register the editor settings.");
    }
    RegisterEditorCommands();
}

Editor::~Editor() {
    Shutdown();
    StopBackgroundModelImportWorker();
}

void Editor::SetPrismUiEnabled(const bool enabled, std::filesystem::path assetDir) {
    m_prismUiRequested = enabled;
    m_prismAssetDir = std::move(assetDir);
}

void Editor::SetProjectFilePath(std::filesystem::path projectFile) {
    m_projectFilePath = std::move(projectFile);
}

void Editor::SetRendererProfile(const Platform::RendererProfile profile) {
    m_services->GetRenderer3D().SetRendererProfile(profile);
    if (m_projectFilePath.empty()) {
        return;
    }
    const Platform::EditorProjectPackageLoadResult loaded =
        Platform::EditorProjectPackageCodec::Load(m_projectFilePath);
    if (!loaded.IsAccepted()) {
        UVE_WARNING("Editor: could not load {} to persist renderer profile: {}",
                    m_projectFilePath.string(), loaded.result.message);
        return;
    }
    if (loaded.package->rendererProfile == profile) {
        return;
    }
    Platform::EditorProjectPackage updated = *loaded.package;
    updated.rendererProfile = profile;
    updated.revision = loaded.package->revision + 1U;
    const Platform::EditorProjectPackageResult saved =
        Platform::EditorProjectPackageCodec::ApplyUpdate(
            m_projectFilePath, loaded.package->revision, updated);
    if (!saved.IsAccepted()) {
        UVE_WARNING("Editor: could not persist renderer profile to {}: {}",
                    m_projectFilePath.string(), saved.message);
    }
}

Render::RendererProfileResolution Editor::GetRendererProfileResolution() const noexcept {
    return m_services->GetRenderer3D().GetRendererProfileResolution();
}

void Editor::SetSafeAreaCallback(std::function<std::array<float, 4>()> callback) {
    m_safeAreaCallback = std::move(callback);
}

std::array<float, 4> Editor::SafeAreaInsets() const {
    return m_safeAreaCallback ? m_safeAreaCallback() : std::array<float, 4>{0.0F, 0.0F, 0.0F, 0.0F};
}

bool Editor::SavePrismScreenshot(const std::filesystem::path& path) {
    return m_levelShell != nullptr && m_levelShell->SaveScreenshot(path);
}

void Editor::Init() {
    if (m_state != EditorState::Uninitialized) {
        return;
    }

    Window::IWindowManager& windowManager = m_services->GetWindowManager();
    // Initialize the editor's UI assets and thumbnail renderers before constructing the PRISM
    // shell so any panel/preview built during startup sees a ready GL renderer.
    if (windowManager.IsValid() && windowManager.GetNativeWindowHandle() != nullptr) {
        static_cast<void>(m_uiAssets.Initialize());
        m_meshThumbnailRenderer.Initialize();
        m_materialThumbnailRenderer.Initialize();
        m_uiInitialized = true;
    }
    if (m_prismUiRequested && windowManager.IsValid() && windowManager.GetNativeWindowHandle() != nullptr) {
        // The level editor is PRISM's.
        auto shell = std::make_unique<EditorShell>(*this);
        if (shell->Initialize(Window::GlfwWindowFromNativeHandle(windowManager.GetNativeWindowHandle()),
                                 m_prismAssetDir)) {
            m_levelShell = std::move(shell);
        } else {
            UVE_ERROR("Editor: the PRISM editor UI could not start (assets: {})", m_prismAssetDir.string());
        }
    }

    m_state = EditorState::Running;
    LoadSessionSettings();
    LoadSharedShelves();
    // An editor opens frozen. Physics is not a preview of the editor - it is the thing the author
    // is trying to author around, and a level that starts shuffling under gravity the moment it is
    // opened cannot be placed in: a Rigid3D spawned above the ground is on the floor before the
    // next frame, with no snapshot to put it back and no undo entry that owns a simulation-driven
    // transform. Other engines reach the same place from the other direction - its physics ticks with a
    // zero delta in edit mode, so nothing integrates - and this is that answer stated plainly.
    // Play sets Running (EnterPlayMode) and stopping Play restores this frozen state
    // (StopPlayMode), so the only way simulation runs is when the author asked for it.
    if (m_simulationControl != nullptr) {
        static_cast<void>(m_simulationControl->SetSimulationExecutionMode(
            Core::SimulationExecutionMode::Paused));
    }
    // A document always has exactly one scene root - the structural anchor at the top of the
    // hierarchy. A fresh editor start is an empty document with just the root.
    static_cast<void>(EnsureDocumentSceneRoot());
    static_cast<void>(EnsureDocumentLayout());
    ClearHistory();
    m_sceneDirty = false;
}

void Editor::Tick() {
    if (m_state != EditorState::Running) {
        return;
    }

    TickExternalContentWatcher();

    const Asset::ProjectChangeSnapshot changeSnapshot = m_services->GetProjectChangeWatcher().GetSnapshot();
    const bool firstProjectIndexRefresh = !m_projectFileSnapshotInitialized;
    const bool newProjectChangeBaseline = changeSnapshot.latestSequence > m_projectFileLastObservedChangeSequence;
    const bool pendingRescanRetry = changeSnapshot.rescanRequired && !m_projectFileRefreshAttemptedForRescan;
    if (firstProjectIndexRefresh || newProjectChangeBaseline || pendingRescanRetry) {
        m_projectFileLastObservedChangeSequence = changeSnapshot.latestSequence;
        RefreshProjectFileIndex();
    }
    PollModelImportJobs();
    // In interactive PRISM sessions, animation extraction runs one-by-one on the background worker;
    // headless/test sessions drain any directly queued clips synchronously here.
    if (!m_pendingModelAnimations.empty()) {
        ProcessPendingModelAnimationImports(m_startupLoading ? 0.12 : 0.025);
    }
    if (m_startupLoading) {
        UpdateStartupLoading();
    }

    PruneSelection();
    if (m_hierarchyRenameEntity != Scene::kInvalidEntity &&
        (!IsAuthoringCommandAllowed() || !HasSingleDocumentSelection() ||
         !IsDocumentEntity(m_hierarchyRenameEntity) || m_hierarchyRenameEntity != m_selectedEntity)) {
        CancelHierarchyRename();
    }

    if (m_extendedPreferences.autosaveEnabled && m_sceneDirty && !m_activeScenePath.empty() &&
        m_playModeState == EditorPlayModeState::Edit && IsAuthoringCommandAllowed()) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - m_lastAutosaveTime).count();
        if (elapsed >= m_extendedPreferences.autosaveIntervalSeconds) {
            m_lastAutosaveTime = now;
            static_cast<void>(SaveScene());
        }
    }
}

bool Editor::EnterPlayMode() {
    // The document is an entity while the Entity Editor is open; the scene plays, not it.
    if (m_entityDocumentActive) {
        return false;
    }
    // A colour still being picked belongs to the document being played; finish it first.
    static_cast<void>(CommitComponentPropertyPreview());
    if (m_state != EditorState::Running || m_playModeState != EditorPlayModeState::Edit ||
        m_simulationControl == nullptr || !IsAuthoringCommandAllowed()) {
        return false;
    }

    // Saved before the snapshot, so the saved file and the state Play restores are the same.
    if (m_playSaveSceneFirst && m_sceneDirty && !m_activeScenePath.empty()) {
        static_cast<void>(SaveScene());
    }
    const std::vector<Scene::Entity> roots = GetDocumentRoots();
    PlayModeSession session{};
    session.capturedEmptyDocument = roots.empty();
    if (!session.capturedEmptyDocument) {
        const std::optional<Scene::SceneSnapshot> snapshot =
            m_services->GetSceneSerializer().Capture(
                m_services->GetEntityManager(), roots, Asset::AssetKind::Scene);
        if (!snapshot.has_value()) {
            return false;
        }
        session.documentSnapshot = *snapshot;
    }
    session.dirtyBefore = m_sceneDirty;
    session.selectionBefore = CaptureSelectionPaths(roots);

    if (!m_simulationControl->SetTransientSimulationSessionActive(true)) {
        return false;
    }
    if (!m_simulationControl->SetSimulationExecutionMode(Core::SimulationExecutionMode::Running)) {
        static_cast<void>(m_simulationControl->SetTransientSimulationSessionActive(false));
        return false;
    }

    m_playModeSession = std::move(session);
    m_playModeState = EditorPlayModeState::Playing;
    static_cast<void>(m_simulationControl->SetEditorPlaySceneName(m_activeScenePath.stem().string()));
    // Play-entry spawn resolution, inside the snapshot's protection and never allowed to fail
    // entering Play itself: a scene with no player or no enabled spawn point simply plays from
    // the authored poses.
    static_cast<void>(ApplyPlayEntrySpawn());
    m_workspaceBeforePlayMode = m_activeWorkspace;
    if (m_playSwitchToGame) {
        m_activeWorkspace = EditorWorkspace::Game;
        SetGameMouseCaptured(true);
    }
    if (m_playPauseOnStart) {
        static_cast<void>(PausePlayMode());
    }
    BeginEditorPlayBootSplash();
    return true;
}

void Editor::BeginEditorPlayBootSplash() {
    m_playBootSplashActive = false;
    m_playBootSplashSkipRequested = false;
    m_playBootSplashFadeSeconds = 0.25;
    m_playBootSplashMinimumSeconds = 1.0;
    m_playBootSplashSkippable = true;
    m_playBootSplashBackground = {0.05F, 0.05F, 0.05F, 1.0F};
    m_playBootSplashImagePath.clear();
    m_playBootSplashImageDimensions = Math::Vector2{};
    if (m_services == nullptr) {
        return;
    }

    try {
        const Config::SettingsDocument& settings = m_services->GetProjectSettings();
        const auto readValue = [&settings](const std::string_view id) {
            return settings.GetValue(id);
        };
        const std::optional<Config::SettingValue> skipValue =
            readValue(Core::EngineProjectSettingId::kSkipBootSplashInEditorPlayMode);
        const bool* const skipInEditor =
            skipValue.has_value() ? std::get_if<bool>(&*skipValue) : nullptr;
        if (skipInEditor == nullptr || *skipInEditor) {
            return;
        }

        const std::optional<Config::SettingValue> fadeValue =
            readValue(Core::EngineProjectSettingId::kBootSplashFadeSeconds);
        if (fadeValue.has_value()) {
            if (const double* const seconds = std::get_if<double>(&*fadeValue); seconds != nullptr &&
                std::isfinite(*seconds) && *seconds >= 0.0 && *seconds <= 60.0) {
                m_playBootSplashFadeSeconds = *seconds;
            }
        }
        const std::optional<Config::SettingValue> minimumValue =
            readValue(Core::EngineProjectSettingId::kBootSplashMinimumDisplaySeconds);
        if (minimumValue.has_value()) {
            if (const double* const seconds = std::get_if<double>(&*minimumValue); seconds != nullptr &&
                std::isfinite(*seconds) && *seconds >= 0.0 && *seconds <= 600.0) {
                m_playBootSplashMinimumSeconds = *seconds;
            }
        }
        const std::optional<Config::SettingValue> skippableValue =
            readValue(Core::EngineProjectSettingId::kBootSplashSkippable);
        if (skippableValue.has_value()) {
            if (const bool* const skippable = std::get_if<bool>(&*skippableValue); skippable != nullptr) {
                m_playBootSplashSkippable = *skippable;
            }
        }
        const std::optional<Config::SettingValue> backgroundValue =
            readValue(Core::EngineProjectSettingId::kBootBackgroundColor);
        if (backgroundValue.has_value()) {
            if (const Config::SettingColor* const color =
                    std::get_if<Config::SettingColor>(&*backgroundValue);
                color != nullptr) {
                m_playBootSplashBackground = {color->r, color->g, color->b, color->a};
            }
        }
        const std::optional<Config::SettingValue> imageValue =
            readValue(Core::EngineProjectSettingId::kBootSplashImage);
        if (imageValue.has_value()) {
            if (const std::string* const imagePath = std::get_if<std::string>(&*imageValue);
                imagePath != nullptr && !imagePath->empty()) {
                const std::filesystem::path relativeImage{*imagePath};
                const bool unsafePath = relativeImage.is_absolute() || relativeImage.has_root_name() ||
                                        relativeImage.has_root_directory() ||
                                        std::any_of(relativeImage.begin(), relativeImage.end(),
                                                    [](const std::filesystem::path& component) {
                                                        return component == "..";
                                                    });
                if (!unsafePath) {
                    m_playBootSplashImagePath = relativeImage.lexically_normal();
                }
            }
        }
        if (!m_playBootSplashImagePath.empty()) {
            const std::filesystem::path contentRoot =
                m_services->GetProjectFileIndex().GetSnapshot().contentRoot;
            const std::filesystem::path absoluteImage = contentRoot / m_playBootSplashImagePath;
            std::error_code pathError;
            const std::filesystem::path canonicalRoot = std::filesystem::canonical(contentRoot, pathError);
            const std::filesystem::path canonicalImage = pathError
                                                             ? std::filesystem::path{}
                                                             : std::filesystem::canonical(absoluteImage, pathError);
            const bool regularFile = !pathError && std::filesystem::is_regular_file(canonicalImage, pathError) &&
                                     !pathError;
            const std::filesystem::path relativeCanonical =
                regularFile ? canonicalImage.lexically_relative(canonicalRoot) : std::filesystem::path{};
            const bool insideContentRoot = regularFile && !relativeCanonical.empty() &&
                                           !relativeCanonical.is_absolute() &&
                                           *relativeCanonical.begin() != "..";
            if (!insideContentRoot) {
                m_playBootSplashImagePath.clear();
            } else {
                Asset::TextureAsset importedTexture;
                std::uint32_t width = 0U;
                std::uint32_t height = 0U;
                std::vector<std::byte> pixels;
                std::string extension = absoluteImage.extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                               [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
                bool imageRead = false;
                if (extension == ".uvtex") {
                    imageRead = Asset::LoadTextureAsset(canonicalImage, importedTexture);
                    if (imageRead) {
                        width = importedTexture.width;
                        height = importedTexture.height;
                    }
                } else {
                    imageRead = DecodeRawImageThumbnailPixels(canonicalImage, width, height, pixels);
                }
                if (imageRead && width > 0U && height > 0U) {
                    m_playBootSplashImageDimensions = Math::Vector2{static_cast<float>(width),
                                                                        static_cast<float>(height)};
                } else {
                    m_playBootSplashImagePath.clear();
                }
            }
        }
        m_playBootSplashStart = std::chrono::steady_clock::now();
        m_playBootSplashActive = true;
    } catch (...) {
        // Boot presentation is optional and must never roll back an otherwise-valid Play session.
        m_playBootSplashActive = false;
        m_playBootSplashImagePath.clear();
    }
}

bool Editor::ApplyPlayEntrySpawn() {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    Scene::ISceneGraph& sceneGraph = m_services->GetSceneGraph();

    Scene::MaintainPlayerPossession(entityManager);
    Scene::Entity player = Scene::ResolvePossessedPlayer(entityManager);
    if (player == Scene::kInvalidEntity) {
        const std::string relative = GetDefaultPlayerEntity();
        if (!relative.empty()) {
            const std::filesystem::path absolute =
                m_services->GetProjectFileIndex().GetSnapshot().contentRoot /
                std::filesystem::path{relative};
            std::error_code error;
            if (std::filesystem::is_regular_file(absolute, error)) {
                const Asset::AssetGuid guid = m_services->GetAssetDatabase().Register(absolute);
                if (guid != Asset::kInvalidAssetGuid) {
                    static_cast<void>(m_services->GetPrefabSystem().Instantiate(
                        entityManager, sceneGraph, m_services->GetAssetDatabase(), guid,
                        GetDocumentSceneRoot()));
                    sceneGraph.Update(entityManager);
                    Scene::MaintainPlayerPossession(entityManager);
                    player = Scene::ResolvePossessedPlayer(entityManager);
                }
            }
        }
    }
    if (player == Scene::kInvalidEntity) {
        player = Scene::ResolvePlayCharacter(entityManager);
        if (player != Scene::kInvalidEntity) {
            if (!entityManager.HasComponent<Scene::PlayerComponent>(player)) {
                entityManager.AddComponent<Scene::PlayerComponent>(player, Scene::PlayerComponent{});
            }
            if (!entityManager.HasComponent<Scene::PawnComponent>(player)) {
                entityManager.AddComponent<Scene::PawnComponent>(player, Scene::PawnComponent{});
            }
            if (!entityManager.HasComponent<Scene::ControllerComponent>(player)) {
                Scene::ControllerComponent possession{};
                possession.kind = Scene::ControllerKind::Player;
                entityManager.AddComponent<Scene::ControllerComponent>(player, possession);
            }
            static_cast<void>(Scene::PossessController(entityManager, player, player));
        }
    }
    if (player == Scene::kInvalidEntity) {
        return false;
    }
    Scene::MakePlayerCameraCurrent(entityManager, player);

    // The candidates come from the shared spawn query - enabled, valid, world-posed, in stable
    // content order - rather than from a second copy of those rules living here. The tag is left
    // empty (a scene's play entry takes the first live spawn point whatever it is called), and
    // consumption is deliberately NOT asked for: a one-shot point is only spent once the player
    // has actually been moved, below, so a refused teleport leaves its checkpoint intact.
    const Scene::SpawnPoint3DQueryResults spawns =
        Scene::QuerySpawnPoints(entityManager, Scene::SpawnPoint3DQuery{});
    if (!spawns.HasAny()) {
        return false;
    }
    const Scene::SpawnPoint3DQueryResult& spawnPoint = spawns.results[0];
    const std::optional<Scene::SpawnPoint3DPose> spawnPose{spawnPoint.pose};

    // World-space spawn pose -> the player's LOCAL pose under its own parent, via the sweep's
    // exact inverse. A root-level player passes identity TRS and the pose falls straight
    // through.
    const auto& playerHierarchy = entityManager.GetComponent<Scene::HierarchyComponent>(player);
    Math::Vector3 parentPosition{};
    Math::Quaternion parentRotation{};
    Math::Vector3 parentScale{1.0F, 1.0F, 1.0F};
    const bool hasParent = playerHierarchy.parent != Scene::kInvalidEntity;
    if (hasParent &&
        entityManager.HasComponent<Scene::WorldTransformComponent>(playerHierarchy.parent)) {
        const Scene::WorldTransformComponent& parentWorld =
            entityManager.GetComponent<Scene::WorldTransformComponent>(playerHierarchy.parent);
        parentPosition = parentWorld.worldPosition;
        parentRotation = parentWorld.worldRotation;
        parentScale = parentWorld.worldScale;
    }
    const std::optional<Scene::SpawnPoint3DPose> playerLocalPose =
        Scene::ResolveSpawnPointPlayerLocal(*spawnPose, parentPosition, parentRotation, parentScale);
    if (!playerLocalPose.has_value()) {
        return false;
    }

    Scene::TransformComponent playerTransform =
        entityManager.GetComponent<Scene::TransformComponent>(player);
    playerTransform.localPosition = playerLocalPose->position;
    playerTransform.localRotation = playerLocalPose->rotation;
    // The documented rule for a simulation write that sets rotation directly: the quaternion is
    // now the truth, so the authored (and now stale) Euler cache must stop replaying.
    playerTransform.rotationEditMode = Scene::RotationEditMode::Quaternion;
    m_services->GetSceneGraph().SetLocalTransform(entityManager, player, playerTransform);

    // The teleport succeeded, so a one-shot point is spent now - never before.
    static_cast<void>(Scene::ConsumeSpawnPoint(entityManager, spawnPoint.entity));
    return true;
}

bool Editor::PausePlayMode() {
    if (m_state != EditorState::Running || m_playModeState != EditorPlayModeState::Playing ||
        m_simulationControl == nullptr ||
        !m_simulationControl->SetSimulationExecutionMode(Core::SimulationExecutionMode::Paused)) {
        return false;
    }
    SetGameMouseCaptured(false);
    m_playModeState = EditorPlayModeState::Paused;
    return true;
}

bool Editor::ResumePlayMode() {
    if (m_state != EditorState::Running || m_playModeState != EditorPlayModeState::Paused ||
        m_simulationControl == nullptr ||
        !m_simulationControl->SetSimulationExecutionMode(Core::SimulationExecutionMode::Running)) {
        return false;
    }
    m_playModeState = EditorPlayModeState::Playing;
    if (m_activeWorkspace == EditorWorkspace::Game) {
        SetGameMouseCaptured(true);
    }
    return true;
}

bool Editor::StepPlayMode() {
    return m_state == EditorState::Running && m_playModeState == EditorPlayModeState::Paused &&
           m_simulationControl != nullptr && m_simulationControl->RequestSingleSimulationStep();
}

bool Editor::StopPlayMode() {
    if (m_state != EditorState::Running || m_playModeState == EditorPlayModeState::Edit ||
        !m_playModeSession.has_value() || m_simulationControl == nullptr ||
        !m_simulationControl->SetSimulationExecutionMode(Core::SimulationExecutionMode::Paused)) {
        return false;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const std::vector<Scene::Entity> transientRoots = GetDocumentRoots();
    std::optional<Scene::SceneSnapshot> transientSnapshot;
    if (!transientRoots.empty()) {
        transientSnapshot = m_services->GetSceneSerializer().Capture(
            entityManager, transientRoots, Asset::AssetKind::Scene);
        if (!transientSnapshot.has_value()) {
            return false;
        }
    }

    const PlayModeSession& session = *m_playModeSession;
    ClearDocumentScene();
    std::vector<Scene::Entity> restoredRoots;
    if (!session.capturedEmptyDocument) {
        restoredRoots = m_services->GetSceneSerializer().Restore(entityManager, session.documentSnapshot);
        if (restoredRoots.empty()) {
            ClearDocumentScene();
            if (transientSnapshot.has_value()) {
                static_cast<void>(m_services->GetSceneSerializer().Restore(entityManager, *transientSnapshot));
            }
            return false;
        }
    }

    RestoreSelection(ResolveSelectionPaths(session.selectionBefore, restoredRoots));
    m_sceneDirty = session.dirtyBefore;
    // Back to frozen, not back to Running. The simulation clock is the author's switch: Play turns
    // it on, and everything that is not Play leaves it off. This used to restore Running, so
    // pressing Stop left a live physics world running in an editor that had just told the author
    // it was done simulating - the restored snapshot would begin drifting again the next frame.
    if (!m_simulationControl->SetSimulationExecutionMode(Core::SimulationExecutionMode::Paused) ||
        !m_simulationControl->SetTransientSimulationSessionActive(false)) {
        return false;
    }

    m_playModeSession.reset();
    m_playModeState = EditorPlayModeState::Edit;
    m_playFastForwardEnabled = false;
    static_cast<void>(m_simulationControl->SetSimulationSpeedScale(1.0F));
    SetGameMouseCaptured(false);
    static_cast<void>(m_simulationControl->SetEditorPlaySceneName({}));
    m_playBootSplashActive = false;
    if (m_activeWorkspace == EditorWorkspace::Game) {
        m_activeWorkspace = m_workspaceBeforePlayMode;
    }
    return true;
}

EditorPlayModeState Editor::GetPlayModeState() const noexcept {
    return m_playModeState;
}

bool Editor::SetPlayFastForward(const bool enabled) noexcept {
    if (m_simulationControl == nullptr) {
        return false;
    }
    m_playFastForwardEnabled = enabled;
    const float targetScale = enabled ? m_playFastForwardMultiplier : 1.0F;
    static_cast<void>(m_simulationControl->SetSimulationSpeedScale(targetScale));
    return true;
}

void Editor::SetPlayFastForwardMultiplier(const float multiplier) noexcept {
    m_playFastForwardMultiplier = std::clamp(multiplier, 1.5F, 16.0F);
    if (m_playFastForwardEnabled && m_simulationControl != nullptr) {
        static_cast<void>(m_simulationControl->SetSimulationSpeedScale(m_playFastForwardMultiplier));
    }
}

void Editor::SetGameMouseCaptured(const bool captured) {
    if (m_gameMouseCaptured == captured) {
        return;
    }
    m_gameMouseCaptured = captured;
    UVE_INFO("Editor: {}", captured ? "the game holds the mouse (Esc releases it)" : "the mouse is the editor's again");
    if (m_services == nullptr) {
        return;
    }
    Window::IWindowManager& windowManager = m_services->GetWindowManager();
    if (!windowManager.IsValid()) {
        return;
    }
    auto* const nativeWindow = Window::GlfwWindowFromNativeHandle(windowManager.GetNativeWindowHandle());
    if (nativeWindow == nullptr) {
        return;
    }
    if (captured) {
        glfwSetInputMode(nativeWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        // A quick Escape is kept until the editor looks, so letting go of the mouse never misses it.
        glfwSetInputMode(nativeWindow, GLFW_STICKY_KEYS, GLFW_TRUE);
        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(nativeWindow, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
    } else {
        glfwSetInputMode(nativeWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(nativeWindow, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        }
    }
}

bool Editor::IsAnyEditorWindowFocused() const {
    return m_levelShell != nullptr && m_levelShell->IsAnyWindowFocused();
}

void Editor::SetViewportFlying(const bool flying) {
    if (m_viewportFlying == flying) {
        return;
    }
    m_viewportFlying = flying;
    // A game already holding the mouse keeps it: flying only hides the editor's own pointer.
    if (m_gameMouseCaptured || m_services == nullptr) {
        return;
    }
    Window::IWindowManager& windowManager = m_services->GetWindowManager();
    auto* const nativeWindow = windowManager.IsValid() ? Window::GlfwWindowFromNativeHandle(windowManager.GetNativeWindowHandle()) : nullptr;
    if (nativeWindow == nullptr) {
        return;
    }
    // Hidden and held where it was: the mouse can turn the view as far as it likes without
    // leaving the viewport, and the pointer comes back where it went away.
    glfwSetInputMode(nativeWindow, GLFW_CURSOR, flying ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(nativeWindow, GLFW_RAW_MOUSE_MOTION, flying ? GLFW_TRUE : GLFW_FALSE);
    }
}

bool Editor::SaveScene() {
    // While the Entity Editor is open the document is the entity: saving writes the entity.
    if (m_entityDocumentActive) {
        return SaveEntityEditor();
    }
    if (!IsAuthoringCommandAllowed() || m_activeScenePath.empty()) {
        return false;
    }

    const std::vector<Scene::Entity> roots = GetDocumentRoots();
    const bool saved = m_services->GetSceneSerializer().Save(
        m_services->GetEntityManager(), roots, m_activeScenePath, Asset::AssetKind::Scene);
    if (saved) {
        m_sceneDirty = false;
    }
    return saved;
}

std::size_t Editor::SaveAll() {
    std::size_t saved = 0U;
    std::vector<std::string> failed;
    if (m_sceneDirty && (!m_activeScenePath.empty() || m_entityDocumentActive)) {
        if (SaveScene()) {
            ++saved;
        } else {
            failed.emplace_back(m_entityDocumentActive ? GetEntityEditorAssetPath().filename().string()
                                                       : m_activeScenePath.filename().string());
        }
    }
    // The other document (the open entity while the level is active, or the level from the entity).
    if (m_entityEditSession.has_value() && m_otherDocument.sceneDirty) {
        const bool otherIsEntity = !m_entityDocumentActive;
        const bool ok = otherIsEntity ? SaveEntityEditor() : [this] {
            // Leave the entity's scope for the level's save.
            SwapDocumentState();
            m_entityDocumentActive = false;
            const bool levelSaved = !m_activeScenePath.empty() && SaveScene();
            m_entityDocumentActive = true;
            SwapDocumentState();
            return levelSaved;
        }();
        if (ok) {
            ++saved;
        } else {
            failed.emplace_back(otherIsEntity ? GetEntityEditorAssetPath().filename().string()
                                              : m_activeScenePath.filename().string());
        }
    }
    if (m_openUVScript.has_value() && m_openUVScript->IsDirty()) {
        if (SaveOpenUVScript()) {
            ++saved;
        } else {
            failed.emplace_back(std::filesystem::path{m_openUVScript->path}.filename().string());
        }
    }
    if (IsVfxEditorDirty()) {
        if (SaveVfxEditor()) {
            ++saved;
        } else {
            failed.emplace_back(m_vfxEdit->path.filename().string());
        }
    }
    if (!failed.empty()) {
        std::string names;
        for (const std::string& name : failed) {
            names += (names.empty() ? "" : ", ") + name;
        }
        m_contentStatusMessage = "Could not save " + names + ".";
    } else {
        m_contentStatusMessage = saved == 0U ? "Nothing to save." : "Saved " + std::to_string(saved) + (saved == 1U ? " file." : " files.");
    }
    return saved;
}

bool Editor::SaveSelectedPrefab(const std::filesystem::path& path) {
    if (!IsLifecycleCommandAllowed() || path.empty() || !IsDocumentEntity(m_selectedEntity)) {
        return false;
    }
    const Asset::AssetGuid guid = m_services->GetPrefabSystem().SavePrefab(
        m_services->GetEntityManager(), m_services->GetAssetDatabase(), m_selectedEntity, path);
    return guid != Asset::kInvalidAssetGuid;
}

std::filesystem::path Editor::MakeUniqueContentPath(const std::filesystem::path& directory,
                                                          const std::string_view stem,
                                                          const std::string_view extension) {
    const std::string base{stem.empty() ? std::string_view{"New"} : stem};
    std::error_code error;
    std::filesystem::path candidate = directory / (base + std::string{extension});
    for (int suffix = 2; std::filesystem::exists(candidate, error); ++suffix) {
        candidate = directory / (base + " " + std::to_string(suffix) + std::string{extension});
    }
    return candidate;
}

std::optional<std::filesystem::path> Editor::CreateContentCatalogueItem(
    const std::string_view itemId, const std::filesystem::path& directory) {
    const ContentCatalogueItem* const item = FindContentCatalogueItem(itemId);
    std::error_code error;
    if (item == nullptr || !IsAuthoringCommandAllowed() || directory.empty() ||
        !std::filesystem::is_directory(directory, error)) {
        return std::nullopt;
    }

    if (item->action == ContentCatalogueAction::Folder) {
        const std::filesystem::path folder = MakeUniqueContentPath(directory, "New Folder", "");
        if (!std::filesystem::create_directory(folder, error) || error) {
            return std::nullopt;
        }
        return folder;
    }
    if (item->action == ContentCatalogueAction::ScriptAsset) {
        const std::string_view stem = item->fileStem.empty() ? item->label : item->fileStem;
        const std::filesystem::path path = MakeUniqueContentPath(directory, stem, ".uvs");
        const std::string text = "entity " + path.stem().generic_string() + " : Object\n\non ready:\n    print(\"Ready\")\n\non tick(dt):\n    pass\n";
        if (!WriteProjectTextFile(path.generic_string(), text)) {
            return std::nullopt;
        }
        return path;
    }
    if (item->action == ContentCatalogueAction::MaterialAsset) {
        const std::string_view stem = item->fileStem.empty() ? item->label : item->fileStem;
        const std::filesystem::path path = MakeUniqueContentPath(directory, stem, ".uvmaterial");
        Asset::MaterialAsset mat{};
        mat.kind = Asset::MaterialKind::Standard;
        if (!Asset::SaveMaterialAsset(mat, path)) {
            return std::nullopt;
        }
        m_services->GetAssetDatabase().Register(path);
        return path;
    }
    if (item->action == ContentCatalogueAction::ShaderAsset) {
        const std::string_view stem = item->fileStem.empty() ? item->label : item->fileStem;
        const std::filesystem::path path = MakeUniqueContentPath(directory, stem, ".uvshader");
        Asset::ShaderAsset shader{};
        shader.stage = Asset::ShaderStageKind::Fragment;
        shader.entryPointName = "main";
        shader.sourceCode =
            "// UniVex Custom Shader\n"
            "#version 450\n\n"
            "layout(location = 0) in vec2 v_TexCoord;\n"
            "layout(location = 0) out vec4 outColor;\n\n"
            "void main() {\n"
            "    outColor = vec4(1.0, 1.0, 1.0, 1.0);\n"
            "}\n";
        if (!Asset::SaveShaderAsset(shader, path)) {
            return std::nullopt;
        }
        m_services->GetAssetDatabase().Register(path);
        return path;
    }
    if (item->action == ContentCatalogueAction::SceneAsset) {
        const std::string_view stem = item->fileStem.empty() ? item->label : item->fileStem;
        const std::filesystem::path path = MakeUniqueContentPath(directory, stem, ".uvscene");
        Scene::IEntityManager& entityManager = m_services->GetEntityManager();
        Scene::ISceneGraph& sceneGraph = m_services->GetSceneGraph();
        const Scene::Entity root = CreateObjectDefinitionEntityInternal(
            Scene::SceneRootObjectDefinition{}, Scene::ApplySceneRootObjectDefinition);
        const Scene::Entity viewport = CreateObjectDefinitionEntityInternal(
            Scene::Viewport3DObjectDefinition{}, Scene::ApplyViewport3DObjectDefinition);
        // A new scene is its root and its Viewport, and nothing else. The World folder used to be
        // created here, so every level opened with an empty folder above an empty level; it now
        // appears with the first object that needs somewhere to live.
        if (root == Scene::kInvalidEntity || viewport == Scene::kInvalidEntity) {
            if (root != Scene::kInvalidEntity) DestroyDocumentSubtree(root);
            if (viewport != Scene::kInvalidEntity) DestroyDocumentSubtree(viewport);
            return std::nullopt;
        }
        sceneGraph.SetParent(entityManager, viewport, root);
        const bool saved = m_services->GetSceneSerializer().Save(entityManager, {root}, path, Scene::SceneAssetType::Scene);
        DestroyDocumentSubtree(root);
        InvalidateHierarchyFilterCache();
        return saved ? std::optional<std::filesystem::path>{path} : std::nullopt;
    }

    if (item->action == ContentCatalogueAction::MontageAsset) {
        const std::string_view stem = item->fileStem.empty() ? item->label : item->fileStem;
        const std::filesystem::path path = MakeUniqueContentPath(directory, stem, ".uvmontage");
        if (!Asset::SaveMotionMontageAsset(Asset::MotionMontageAsset{}, path)) {
            return std::nullopt;
        }
        static_cast<void>(m_services->GetAssetDatabase().Register(path));
        return path;
    }

    if (item->action == ContentCatalogueAction::MontageLibraryAsset) {
        const std::string_view stem = item->fileStem.empty() ? item->label : item->fileStem;
        const std::filesystem::path path = MakeUniqueContentPath(directory, stem, ".uvmontagelib");
        Asset::MontageLibraryAsset library;
        library.libraryId = path.stem().generic_string().empty() ? std::string{"montages"} : path.stem().generic_string();
        if (!Asset::SaveMontageLibraryAsset(library, path)) {
            return std::nullopt;
        }
        static_cast<void>(m_services->GetAssetDatabase().Register(path));
        return path;
    }

    if (item->action == ContentCatalogueAction::LibraryAsset) {
        const std::string_view stem = item->fileStem.empty() ? item->label : item->fileStem;
        const std::filesystem::path path = MakeUniqueContentPath(directory, stem, ".uvanimlib");
        Asset::AnimationLibraryAsset library;
        library.libraryId = path.stem().generic_string();
        if (library.libraryId.empty()) {
            library.libraryId = "library";
        }
        if (!Asset::SaveAnimationLibraryAsset(library, path)) {
            return std::nullopt;
        }
        return path;
    }

    if (item->objects.empty()) {
        return std::nullopt;
    }

    // The tree is built in the live entity manager, saved, and destroyed again in this call: it is
    // never a document object, never selected and never in the undo history.
    const std::string_view stem = item->fileStem.empty() ? item->label : item->fileStem;
    const std::filesystem::path path = MakeUniqueContentPath(directory, stem, ".uventity");
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    Scene::ISceneGraph& sceneGraph = m_services->GetSceneGraph();
    std::vector<Scene::Entity> built;
    built.reserve(item->objects.size());
    bool complete = true;
    for (const ContentCatalogueObject& object : item->objects) {
        const Scene::Entity entity = CreateSceneObjectEntityInternal(object.kind);
        if (entity == Scene::kInvalidEntity || object.parent >= static_cast<std::int32_t>(built.size()) ||
            (built.empty() != (object.parent < 0))) {
            if (entity != Scene::kInvalidEntity) {
                built.push_back(entity);
            }
            complete = false;
            break;
        }
        const std::string name = built.empty() ? path.stem().string() : std::string{object.name};
        if (!name.empty()) {
            entityManager.GetComponent<Scene::NameComponent>(entity).name = name;
        }
        if (object.parent >= 0) {
            sceneGraph.SetParent(entityManager, entity, built[static_cast<std::size_t>(object.parent)]);
        }
        built.push_back(entity);
    }

    Asset::AssetGuid guid = Asset::kInvalidAssetGuid;
    if (complete) {
        guid = m_services->GetPrefabSystem().SavePrefab(entityManager, m_services->GetAssetDatabase(),
                                                             built.front(), path);
    }
    if (!built.empty()) {
        DestroyDocumentSubtree(built.front());
    }
    InvalidateHierarchyFilterCache();
    if (guid == Asset::kInvalidAssetGuid) {
        return std::nullopt;
    }
    return path;
}

std::optional<std::filesystem::path> Editor::CreateWidgetBlueprintAsset(const std::filesystem::path& directory,
                                                                             const std::string_view stem) {
    if (!IsAuthoringCommandAllowed()) {
        return std::nullopt;
    }
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const std::filesystem::path path = MakeUniqueContentPath(directory, stem, ".uvwbp");
    // The script slot names the file as the engine reads it: relative to the project root.
    std::string projectPath;
    if (const std::filesystem::path probe = m_services->GetFileSystem().ResolveRealPath("uve_root_probe"); !probe.empty()) {
        const std::filesystem::path root = std::filesystem::weakly_canonical(probe.parent_path(), error);
        const std::filesystem::path relative = std::filesystem::weakly_canonical(path, error).lexically_relative(root);
        if (!relative.empty() && *relative.begin() != "..") {
            projectPath = relative.generic_string();
        }
    }
    if (projectPath.empty()) {
        const std::filesystem::path content = m_services->GetProjectFileIndex().GetSnapshot().contentRoot;
        const std::filesystem::path relative = path.lexically_relative(content.parent_path());
        projectPath = relative.empty() || *relative.begin() == ".." ? path.filename().generic_string() : relative.generic_string();
    }

    // Built in the live entity manager, saved and destroyed again here, like a catalogue item.
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    Scene::ISceneGraph& sceneGraph = m_services->GetSceneGraph();
    const Scene::Entity root = CreateSceneObjectEntityInternal(Scene::Objects::SceneObjectKind::Canvas);
    if (root == Scene::kInvalidEntity) {
        return std::nullopt;
    }
    const auto make = [&](const Scene::Objects::SceneObjectKind kind, const std::string& name, const Scene::Entity parent) {
        const Scene::Entity entity = CreateSceneObjectEntityInternal(kind);
        if (entity != Scene::kInvalidEntity) {
            entityManager.GetComponent<Scene::NameComponent>(entity).name = name;
            sceneGraph.SetParent(entityManager, entity, parent);
        }
        return entity;
    };
    entityManager.GetComponent<Scene::NameComponent>(root).name = path.stem().string();
    if (entityManager.HasComponent<Scene::CanvasComponent>(root)) {
        // Laid out on this device's screen, fitted to whatever screen it is shown on.
        entityManager.GetComponent<Scene::CanvasComponent>(root).scaleWithScreen = true;
    }
    Scene::ScriptComponent script;
    script.scriptAssetPath = projectPath;
    if (entityManager.HasComponent<Scene::ScriptComponent>(root)) {
        entityManager.GetComponent<Scene::ScriptComponent>(root) = script;
    } else {
        entityManager.AddComponent<Scene::ScriptComponent>(root, script);
    }
    const Scene::Entity title = make(Scene::Objects::SceneObjectKind::UIText, "Title", root);
    if (title != Scene::kInvalidEntity && entityManager.HasComponent<Scene::UITextComponent>(title)) {
        Scene::UITextComponent& text = entityManager.GetComponent<Scene::UITextComponent>(title);
        text.text = "Hello, Widget";
        text.positionPixels = Math::Vector2{80.0F, 60.0F};
        text.fontSize = 36.0F;
    }
    const Scene::Entity start = make(Scene::Objects::SceneObjectKind::UIButton, "Start", root);
    if (start != Scene::kInvalidEntity && entityManager.HasComponent<Scene::UIButtonComponent>(start)) {
        Scene::UIButtonComponent& button = entityManager.GetComponent<Scene::UIButtonComponent>(start);
        button.rect = Math::Rect{Math::Vector2{80.0F, 130.0F}, Math::Vector2{220.0F, 56.0F}};
        button.normalColor = Math::Vector3{0.16F, 0.42F, 0.86F};
        button.hoverColor = Math::Vector3{0.24F, 0.52F, 0.95F};
        button.pressedColor = Math::Vector3{0.10F, 0.30F, 0.66F};
    }
    const Scene::Entity label = make(Scene::Objects::SceneObjectKind::UIText, "StartLabel", start != Scene::kInvalidEntity ? start : root);
    if (label != Scene::kInvalidEntity && entityManager.HasComponent<Scene::UITextComponent>(label)) {
        Scene::UITextComponent& text = entityManager.GetComponent<Scene::UITextComponent>(label);
        text.text = "Start";
        text.fontSize = 22.0F;
        // Centred on its button, however the button is sized.
        text.fillParent = true;
        text.horizontalAlign = Scene::UITextHorizontalAlign::Center;
        text.verticalAlign = Scene::UITextVerticalAlign::Middle;
    }

    const Asset::AssetGuid guid =
        m_services->GetPrefabSystem().SavePrefab(entityManager, m_services->GetAssetDatabase(), root, path);
    DestroyDocumentSubtree(root);
    InvalidateHierarchyFilterCache();
    if (guid == Asset::kInvalidAssetGuid) {
        return std::nullopt;
    }
    // Its graph: Event Construct, and a click on Start sets the title.
    Asset::BlueprintAsset graph;
    graph.parentKind = "Canvas";
    Asset::BlueprintNode construct;
    construct.id = 1U;
    construct.type = "ui_on_construct";
    Asset::BlueprintNode clicked;
    clicked.id = 2U;
    clicked.type = "ui_on_clicked";
    clicked.y = 180.0F;
    clicked.inputValues = {"Start"};
    Asset::BlueprintNode setText;
    setText.id = 3U;
    setText.type = "ui_set_text";
    setText.x = 320.0F;
    setText.y = 180.0F;
    setText.inputValues = {"", "Title", "Clicked!"};
    graph.nodes = {construct, clicked, setText};
    graph.wires = {Asset::BlueprintWire{2U, 0U, 3U, 0U}};
    if (!Asset::SaveWidgetBlueprintGraph(path, graph)) {
        return std::nullopt;
    }
    return path;
}

Scene::Entity Editor::PlaceEntityAsset(const std::filesystem::path& path, Scene::Entity parent) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    std::error_code error;
    if (!IsAuthoringCommandAllowed() || (extension != ".uventity" && extension != ".uvprefab") ||
        !std::filesystem::is_regular_file(path, error)) {
        return Scene::kInvalidEntity;
    }
    if (parent == Scene::kInvalidEntity || !IsDocumentEntity(parent)) {
        parent = ResolveNewObjectParent();
    }

    const EditorSelectionSnapshot selectionBefore = CaptureSelectionSnapshot();
    const bool dirtyBefore = m_sceneDirty;
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const std::string rootName = MakeUniqueDocumentEntityName(path.stem().string());
    const Asset::AssetGuid guid = m_services->GetAssetDatabase().Register(path);
    const Scene::Entity root = m_services->GetPrefabSystem().Instantiate(
        entityManager, m_services->GetSceneGraph(), m_services->GetAssetDatabase(), guid, parent);
    if (root == Scene::kInvalidEntity) {
        return Scene::kInvalidEntity;
    }
    if (entityManager.HasComponent<Scene::NameComponent>(root)) {
        entityManager.GetComponent<Scene::NameComponent>(root).name = rootName;
    }
    // Where the entity sat in the Entity Editor is where its author put it to look at it, not part of the
    // entity: the level places it, so it starts at its parent's origin and the placement rule below moves it.
    // Without this an entity saved after a nudge in its editor landed that far from where it was dropped.
    // (Rotation and scale are the entity's own and stay. Inside an Entity Editor session the offset of a nested
    // entity is the author's layout, so it is left alone there.)
    if (!m_entityDocumentActive && entityManager.HasComponent<Scene::TransformComponent>(root)) {
        Scene::TransformComponent rootTransform = entityManager.GetComponent<Scene::TransformComponent>(root);
        rootTransform.localPosition = Math::Vector3{};
        m_services->GetSceneGraph().SetLocalTransform(entityManager, root, rootTransform);
    }
    InvalidateHierarchyFilterCache();
    PlaceNewDocumentObject(root);

    const std::optional<Scene::SceneSnapshot> snapshot = CaptureSubtree(root);
    if (!snapshot.has_value()) {
        DestroyDocumentSubtree(root);
        RestoreSelection(selectionBefore);
        m_sceneDirty = dirtyBefore;
        return Scene::kInvalidEntity;
    }
    SelectEntity(root);
    m_sceneDirty = true;
    RecordHistory(SceneObjectCreationHistoryEntry{*snapshot, Scene::ResolveSceneObjectKind(entityManager, root),
                                                      root, selectionBefore, CaptureSelectionSnapshot(),
                                                      dirtyBefore, true, parent});
    return root;
}

bool Editor::SetDefaultPlayerEntity(const std::filesystem::path& contentRelativePath) {
    Config::SettingsDocument& project = m_services->GetProjectSettings();
    const std::string value = contentRelativePath.generic_string();
    if (!project.SetValue(Core::EngineProjectSettingId::kDefaultPlayerEntity, Config::SettingValue{value})) {
        return false;
    }
    return SaveProjectSettings();
}

std::string Editor::GetDefaultPlayerEntity() const {
    const std::optional<Config::SettingValue> value =
        m_services->GetProjectSettings().GetValue(Core::EngineProjectSettingId::kDefaultPlayerEntity);
    const auto* const text = value.has_value() ? std::get_if<std::string>(&*value) : nullptr;
    return text != nullptr ? *text : std::string{};
}

bool Editor::RefreshSelectedPrefab() {
    if (!IsLifecycleCommandAllowed() || !IsDocumentEntity(m_selectedEntity)) {
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent<Scene::PrefabInstanceComponent>(m_selectedEntity)) {
        return false;
    }
    const Scene::PrefabRefreshResult result = m_services->GetPrefabSystem().RefreshInstance(
        entityManager, m_services->GetSceneGraph(), m_services->GetAssetDatabase(), m_selectedEntity);
    if (!result.IsSuccess()) {
        return false;
    }
    if (result.code == Scene::PrefabRefreshCode::Refreshed) {
        SelectEntity(result.rootEntity);
        m_sceneDirty = true;
        InvalidateHierarchyFilterCache();
    }
    return true;
}

bool Editor::DiscardSelectedPrefabOverridesAndRefresh() {
    if (!IsLifecycleCommandAllowed() || !IsDocumentEntity(m_selectedEntity)) {
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent<Scene::PrefabInstanceComponent>(m_selectedEntity)) {
        return false;
    }
    Scene::PrefabInstanceComponent cleared =
        entityManager.GetComponent<Scene::PrefabInstanceComponent>(m_selectedEntity);
    if (cleared.overrides.empty()) {
        return RefreshSelectedPrefab();
    }
    cleared.overrides.clear();
    entityManager.AddComponent<Scene::PrefabInstanceComponent>(m_selectedEntity, cleared);
    const Scene::PrefabRefreshResult result = m_services->GetPrefabSystem().RefreshInstance(
        entityManager, m_services->GetSceneGraph(), m_services->GetAssetDatabase(), m_selectedEntity, true);
    if (!result.IsSuccess()) {
        return false;
    }
    if (result.code == Scene::PrefabRefreshCode::Refreshed) {
        SelectEntity(result.rootEntity);
        InvalidateHierarchyFilterCache();
    }
    m_sceneDirty = true;
    return true;
}

bool Editor::LoadScene() {
    if (m_entityDocumentActive) {
        return false;
    }
    static_cast<void>(CommitComponentPropertyPreview());
    if (!IsAuthoringCommandAllowed() || m_activeScenePath.empty() ||
        !std::filesystem::exists(m_activeScenePath)) {
        return false;
    }

    const std::filesystem::path recoveryPath = MakeRecoveryPath(m_activeScenePath);
    std::error_code error;
    std::filesystem::remove(recoveryPath, error);

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const std::vector<Scene::Entity> documentRoots = GetDocumentRoots();
    if (!m_services->GetSceneSerializer().Save(
            entityManager, documentRoots, recoveryPath, Asset::AssetKind::Scene)) {
        return false;
    }

    ClearHistory();
    ClearDocumentScene();
    const std::vector<Scene::Entity> loadedRoots =
        m_services->GetSceneSerializer().Load(entityManager, m_activeScenePath);
    if (loadedRoots.empty()) {
        ClearDocumentScene();
        static_cast<void>(m_services->GetSceneSerializer().Load(entityManager, recoveryPath));
        std::filesystem::remove(recoveryPath, error);
        // Even a total load failure must not leave a rootless document behind.
        static_cast<void>(EnsureDocumentSceneRoot());
        return false;
    }

    std::filesystem::remove(recoveryPath, error);
    // Auto-migrate: a loaded document must end with exactly one scene root. Files saved before
    // the root existed are legitimately multi-root (every Add-Object used to create a new root),
    // so wrap their top-level entities under a fresh SceneRoot; files that already carry the
    // root pass through untouched. Either way the in-memory document afterwards holds the
    // one-root invariant every other document seam relies on.
    bool migrated = false;
    const Scene::Entity sceneRoot = EnsureDocumentSceneRoot();
    if (sceneRoot != Scene::kInvalidEntity) {
        Scene::ISceneGraph& sceneGraph = m_services->GetSceneGraph();

        // Strip any marker other than the one root this document keeps. A .uvscene is plain JSON
        // on disk, so a file can arrive carrying two of them - a badly resolved merge, a
        // hand-edit, a future tool - and the loop below would then reparent the second one UNDER
        // the first, leaving a root inside a root. That is not cosmetic: the editor refuses to
        // delete or reparent a scene root, so the surplus would be an entity the user has no way
        // to remove.
        //
        // The marker is stripped rather than the entity destroyed. The surplus root may well have
        // children, and discarding authored content to repair a structural mistake is the wrong
        // trade - demoted to an ordinary object it keeps its name, its transform and its subtree,
        // and the migration below folds it under the real root like any other top-level entity.
        std::vector<Scene::Entity> surplusRoots;
        entityManager.ForEach<Scene::SceneRootComponent>(
            [&surplusRoots, sceneRoot](const Scene::Entity entity, const Scene::SceneRootComponent&) {
                if (entity != sceneRoot) {
                    surplusRoots.push_back(entity);
                }
            });
        for (const Scene::Entity surplus : surplusRoots) {
            entityManager.RemoveComponent<Scene::SceneRootComponent>(surplus);
            migrated = true; // the document no longer matches the bytes it was loaded from
        }

        for (const Scene::Entity topLevel : GetDocumentRoots()) {
            if (topLevel != sceneRoot) {
                sceneGraph.SetParent(entityManager, topLevel, sceneRoot);
                migrated = true;
            }
        }
    }
    // Levels saved before the Outliner layout get its Viewport3D, and their objects a folder.
    migrated = EnsureDocumentLayout() || migrated;
    ClearSelection();
    ClearHistory();
    m_sceneDirty = migrated; // a wrapped legacy file no longer matches its bytes on disk
    InvalidateHierarchyFilterCache();
    return true;
}

bool Editor::OpenSceneAsset(const std::filesystem::path& path) {
    std::error_code error;
    if (!IsAuthoringCommandAllowed() || path.empty() || path.extension() != ".uvscene" ||
        !std::filesystem::is_regular_file(path, error)) {
        return false;
    }
    const std::filesystem::path previous = m_activeScenePath;
    m_activeScenePath = path;
    if (LoadScene()) {
        SetViewportOrthographic(false);
        m_viewportOverlayState.view = ViewportView::User;
        return true;
    }
    m_activeScenePath = previous;
    return false;
}

void Editor::SelectEntity(const Scene::Entity entity) noexcept {
    if (!IsAuthoringCommandAllowed()) {
        return;
    }
    if (!IsDocumentEntity(entity)) {
        ClearSelection();
        return;
    }
    // An object inside a placed entity is not a row the Outliner shows, so selecting it would leave
    // the tree with nothing highlighted and the Inspector describing something the author cannot
    // see. The instance is the thing the level addresses, so that is what gets selected.
    const Scene::Entity row = ResolveHierarchyRowOwner(entity);
    RestoreSelection(EditorSelectionSnapshot{{row}, row});
}

void Editor::ToggleEntitySelection(const Scene::Entity entity) noexcept {
    if (!IsAuthoringCommandAllowed() || !IsDocumentEntity(entity)) {
        return;
    }

    const Scene::Entity row = ResolveHierarchyRowOwner(entity);
    const auto selectedIt = std::find(m_selectedEntities.begin(), m_selectedEntities.end(), row);
    if (selectedIt == m_selectedEntities.end()) {
        m_selectedEntities.push_back(row);
        m_selectedEntity = row;
        CancelHierarchyRename();
        return;
    }

    const bool removedActive = row == m_selectedEntity;
    m_selectedEntities.erase(selectedIt);
    if (m_selectedEntities.empty()) {
        m_selectedEntity = Scene::kInvalidEntity;
    } else if (removedActive) {
        m_selectedEntity = m_selectedEntities.back();
    }
    CancelHierarchyRename();
}

void Editor::ClearSelection() noexcept {
    m_selectedEntities.clear();
    m_selectedEntity = Scene::kInvalidEntity;
    CancelHierarchyRename();
}

const std::vector<Scene::Entity>& Editor::GetSelectedEntities() const noexcept {
    return m_selectedEntities;
}

bool Editor::HasSingleDocumentSelection() const noexcept {
    return m_selectedEntities.size() == 1U && m_selectedEntities.front() == m_selectedEntity &&
           IsDocumentEntity(m_selectedEntity);
}

bool Editor::SetSelectedLocalTransform(const Scene::TransformComponent& transform) {
    if (!IsAuthoringCommandAllowed() || !HasSingleDocumentSelection() ||
        !IsTransformFinite(transform)) {
        return false;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent<Scene::TransformComponent>(m_selectedEntity)) {
        return false;
    }

    const EditorSelectionSnapshot selectionBefore = CaptureSelectionSnapshot();
    const Scene::TransformComponent before =
        entityManager.GetComponent<Scene::TransformComponent>(m_selectedEntity);
    if (AreTransformsEqual(before, transform)) {
        return false;
    }

    const bool dirtyBefore = m_sceneDirty;
    if (!ApplyLocalTransform(m_selectedEntity, transform)) {
        return false;
    }

    m_sceneDirty = true;
    RecordHistory(TransformHistoryEntry{
        m_selectedEntity, before, transform, selectionBefore, CaptureSelectionSnapshot(), dirtyBefore, true});
    return true;
}

bool Editor::SetSelectedPrimitiveMesh(const Scene::PrimitiveMeshComponent& primitive) {
    if (!IsAuthoringCommandAllowed() || !HasSingleDocumentSelection() ||
        !Scene::IsPrimitiveMeshComponentValid(primitive)) {
        return false;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent<Scene::PrimitiveMeshComponent>(m_selectedEntity)) {
        return false;
    }

    const Scene::PrimitiveMeshComponent before =
        entityManager.GetComponent<Scene::PrimitiveMeshComponent>(m_selectedEntity);
    if (before.kind == primitive.kind && before.baseColor == primitive.baseColor) {
        return false;
    }

    const EditorSelectionSnapshot selectionBefore = CaptureSelectionSnapshot();
    const bool dirtyBefore = m_sceneDirty;
    if (!ApplyPrimitiveMeshState(m_selectedEntity, primitive)) {
        return false;
    }

    m_sceneDirty = true;
    RecordHistory(PrimitiveAppearanceHistoryEntry{
        m_selectedEntity, before, primitive, selectionBefore, CaptureSelectionSnapshot(), dirtyBefore, true});
    return true;
}

bool Editor::SetSelectedMeshSource(const Asset::AssetGuid model,
                                         const std::optional<Scene::PrimitiveMeshKind> shape) {
    if (!IsAuthoringCommandAllowed() || !HasSingleDocumentSelection()) {
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent<Scene::MeshComponent>(m_selectedEntity)) {
        return false;
    }
    const Scene::MeshComponent beforeMesh = entityManager.GetComponent<Scene::MeshComponent>(m_selectedEntity);
    std::optional<Scene::PrimitiveMeshComponent> beforeShape;
    if (entityManager.HasComponent<Scene::PrimitiveMeshComponent>(m_selectedEntity)) {
        beforeShape = entityManager.GetComponent<Scene::PrimitiveMeshComponent>(m_selectedEntity);
    }
    Scene::MeshComponent afterMesh = beforeMesh;
    std::optional<Scene::PrimitiveMeshComponent> afterShape;
    if (shape.has_value()) {
        afterMesh.meshGuid = Asset::kInvalidAssetGuid;
        Scene::PrimitiveMeshComponent primitive = beforeShape.value_or(Scene::PrimitiveMeshComponent{});
        primitive.kind = *shape;
        // The surface keeps the material the mesh had.
        if (primitive.materialGuid == Asset::kInvalidAssetGuid) {
            primitive.materialGuid = beforeMesh.materialGuid;
        }
        afterShape = primitive;
    } else {
        afterMesh.meshGuid = model;
        // The model keeps the material the shape had.
        if (afterMesh.materialGuid == Asset::kInvalidAssetGuid && beforeShape.has_value()) {
            afterMesh.materialGuid = beforeShape->materialGuid;
        }
    }
    const bool sameShape = beforeShape.has_value() == afterShape.has_value() &&
                           (!afterShape.has_value() || beforeShape->kind == afterShape->kind);
    if (afterMesh.meshGuid == beforeMesh.meshGuid && sameShape) {
        return false;
    }
    const EditorSelectionSnapshot selectionBefore = CaptureSelectionSnapshot();
    const bool dirtyBefore = m_sceneDirty;
    std::optional<Scene::SkinComponent> beforeSkin;
    if (entityManager.HasComponent<Scene::SkinComponent>(m_selectedEntity)) {
        beforeSkin = entityManager.GetComponent<Scene::SkinComponent>(m_selectedEntity);
    }
    if (!ApplyMeshSourceState(m_selectedEntity, afterMesh, afterShape)) {
        return false;
    }
    Scene::Entity syncedSkeletonEntity = Scene::kInvalidEntity;
    std::optional<Scene::Skeleton3DComponent> beforeSkeleton;
    std::optional<Scene::Skeleton3DComponent> afterSkeleton;
    Scene::Entity createdSkeletonEntity = Scene::kInvalidEntity;
    std::optional<Scene::SceneSnapshot> createdSkeletonSnapshot;
    // A skinned mesh takes the bones of the model it now draws. When a rigged model is assigned to
    // a MeshInstance3D (e.g. under Player3D / Character3D / Entity), ensure a matching Skeleton3D
    // loaded from its sibling .uvskel (A-pose once retargeted) stands beside it so the renderer
    // skins into the skeleton's rest pose instead of falling back to raw unskinned T-pose vertices.
    if (!shape.has_value() && model != Asset::kInvalidAssetGuid) {
        if (const std::optional<Asset::MeshAsset> meshAsset = LoadEntityMeshAsset(m_selectedEntity);
            meshAsset.has_value() && !meshAsset->joints.empty()) {
            const Asset::ProjectFileSnapshot project = m_services->GetProjectFileIndex().GetSnapshot();
            std::filesystem::path resolvedModel = m_services->GetAssetDatabase().Resolve(model);
            std::error_code ec;
            if (resolvedModel.extension() != ".uvmodel" || !std::filesystem::is_regular_file(resolvedModel, ec)) {
                const std::filesystem::path rel = resolvedModel.lexically_relative(project.contentRoot);
                if (!rel.empty() && *rel.begin() != "..") {
                    const std::filesystem::path imported = GetImportedModelPath(rel);
                    if (std::filesystem::is_regular_file(imported, ec)) {
                        resolvedModel = imported;
                    }
                }
            }
            const std::optional<Asset::GltfSkeleton> modelSkeleton =
                ReadSkeletonForModelFile(resolvedModel, Scene::kMaximumSkeletonBones);
            if (modelSkeleton.has_value() && !modelSkeleton->joints.empty()) {
                // Recover the cleanest asset path for Skeleton3DComponent::skeletonAssetPath so
                // future Retarget / Undo runs keep this skeleton synchronized automatically.
                std::string skelAssetPath;
                const std::filesystem::path relToContent = resolvedModel.lexically_relative(project.contentRoot);
                if (!relToContent.empty() && *relToContent.begin() != "..") {
                    skelAssetPath = relToContent.generic_string();
                } else {
                    const std::filesystem::path cacheRoot = m_services->GetDerivedArtifactCache().GetCacheRoot();
                    std::filesystem::path importedRoot =
                        cacheRoot.has_filename() ? cacheRoot.parent_path() : cacheRoot.parent_path().parent_path();
                    importedRoot /= "Imported";
                    const std::filesystem::path relToImported = resolvedModel.lexically_relative(importedRoot);
                    if (!relToImported.empty() && *relToImported.begin() != ".." && relToImported.extension() == ".uvmodel") {
                        std::filesystem::path sourceRel = relToImported;
                        sourceRel.replace_extension();
                        skelAssetPath = sourceRel.generic_string();
                    } else {
                        skelAssetPath = resolvedModel.generic_string();
                    }
                }

                // Locate an existing Skeleton3D in SkinForEntity's scope (self, child, parent, sibling).
                Scene::ISceneGraph& sceneGraph = m_services->GetSceneGraph();
                Scene::Entity existingSkel = Scene::kInvalidEntity;
                if (entityManager.HasComponent<Scene::Skeleton3DComponent>(m_selectedEntity)) {
                    existingSkel = m_selectedEntity;
                } else {
                    for (const Scene::Entity child : sceneGraph.GetChildren(entityManager, m_selectedEntity)) {
                        if (entityManager.HasComponent<Scene::Skeleton3DComponent>(child)) {
                            existingSkel = child;
                            break;
                        }
                    }
                    if (existingSkel == Scene::kInvalidEntity &&
                        entityManager.HasComponent<Scene::HierarchyComponent>(m_selectedEntity)) {
                        const Scene::Entity parent =
                            entityManager.GetComponent<Scene::HierarchyComponent>(m_selectedEntity).parent;
                        if (parent != Scene::kInvalidEntity) {
                            if (entityManager.HasComponent<Scene::Skeleton3DComponent>(parent)) {
                                existingSkel = parent;
                            } else {
                                for (const Scene::Entity sibling : sceneGraph.GetChildren(entityManager, parent)) {
                                    if (entityManager.HasComponent<Scene::Skeleton3DComponent>(sibling)) {
                                        existingSkel = sibling;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }

                const auto fillBones = [&modelSkeleton](Scene::Skeleton3DComponent& comp, const std::string& path) {
                    comp.skeletonAssetPath = path;
                    comp.bones.clear();
                    comp.bones.reserve(modelSkeleton->joints.size());
                    for (const Asset::GltfJoint& joint : modelSkeleton->joints) {
                        comp.bones.push_back(Scene::SkeletonBone{
                            joint.name, joint.parentIndex, joint.translation, joint.rotation, joint.scale});
                    }
                    comp.pose.clear();
                };

                if (existingSkel != Scene::kInvalidEntity) {
                    Scene::Skeleton3DComponent& skelComp =
                        entityManager.GetComponent<Scene::Skeleton3DComponent>(existingSkel);
                    bool anyOverlap = false;
                    if (!skelComp.bones.empty()) {
                        Scene::SkeletonBoneLookup lookup;
                        lookup.Build(skelComp);
                        std::uint32_t unused = 0U;
                        for (const Asset::MeshJoint& joint : meshAsset->joints) {
                            if (lookup.TryFind(joint.name, unused)) {
                                anyOverlap = true;
                                break;
                            }
                        }
                    }
                    if (skelComp.bones.empty() || !anyOverlap) {
                        syncedSkeletonEntity = existingSkel;
                        beforeSkeleton = skelComp;
                        fillBones(skelComp, skelAssetPath);
                        afterSkeleton = skelComp;
                    }
                } else {
                    Scene::Skeleton3DObjectDefinition skelDef;
                    fillBones(skelDef.skeleton, skelAssetPath);
                    const Scene::Entity skelEntity =
                        CreateDocumentEntityShellInternal(MakeUniqueDocumentEntityName("Skeleton3D"));
                    if (skelEntity != Scene::kInvalidEntity) {
                        Scene::ApplySkeleton3DObjectDefinition(entityManager, skelEntity, skelDef);
                        Scene::SetSceneObjectKind(entityManager, skelEntity, Scene::Objects::SceneObjectKind::Skeleton3D);
                        sceneGraph.SetParent(entityManager, skelEntity, m_selectedEntity);
                        InvalidateHierarchyFilterCache();
                        createdSkeletonEntity = skelEntity;
                        createdSkeletonSnapshot = CaptureSubtree(skelEntity);
                    }
                }
            }
        }
        if (entityManager.HasComponent<Scene::SkinComponent>(m_selectedEntity)) {
            auto skin = entityManager.GetComponent<Scene::SkinComponent>(m_selectedEntity);
            AutoPopulateSkinFromModel(m_selectedEntity, skin);
            entityManager.AddComponent<Scene::SkinComponent>(m_selectedEntity, skin);
        } else if (const std::optional<Asset::MeshAsset> meshAsset = LoadEntityMeshAsset(m_selectedEntity);
                   meshAsset.has_value() && !meshAsset->joints.empty()) {
            Scene::SkinComponent skin;
            if (AutoPopulateSkinFromModel(m_selectedEntity, skin)) {
                entityManager.AddComponent<Scene::SkinComponent>(m_selectedEntity, skin);
            } else {
                m_contentStatusMessage = "This model is rigged: use Place in Scene on it in the Asset Table to get "
                                         "its skeleton, or add a Skeleton3D and set its Source to this model.";
            }
        }
    }
    std::optional<Scene::SkinComponent> afterSkin;
    if (entityManager.HasComponent<Scene::SkinComponent>(m_selectedEntity)) {
        afterSkin = entityManager.GetComponent<Scene::SkinComponent>(m_selectedEntity);
    }
    m_sceneDirty = true;
    RecordHistory(MeshSourceHistoryEntry{m_selectedEntity,
                                               beforeMesh,
                                               beforeShape,
                                               afterMesh,
                                               afterShape,
                                               beforeSkin,
                                               afterSkin,
                                               syncedSkeletonEntity,
                                               beforeSkeleton,
                                               afterSkeleton,
                                               createdSkeletonEntity,
                                               createdSkeletonSnapshot,
                                               selectionBefore,
                                               CaptureSelectionSnapshot(),
                                               dirtyBefore,
                                               true});
    return true;
}

bool Editor::ApplyMeshSourceState(const Scene::Entity entity, const Scene::MeshComponent& mesh,
                                        const std::optional<Scene::PrimitiveMeshComponent>& shape) {
    if (!IsDocumentEntity(entity)) {
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent<Scene::MeshComponent>(entity)) {
        return false;
    }
    entityManager.GetComponent<Scene::MeshComponent>(entity) = mesh;
    if (shape.has_value()) {
        entityManager.AddComponent<Scene::PrimitiveMeshComponent>(entity, *shape);
        if (entityManager.HasComponent<Scene::ColliderComponent>(entity)) {
            FitColliderToPrimitiveKind(entityManager.GetComponent<Scene::ColliderComponent>(entity), shape->kind);
        }
    } else if (entityManager.HasComponent<Scene::PrimitiveMeshComponent>(entity)) {
        entityManager.RemoveComponent<Scene::PrimitiveMeshComponent>(entity);
    }
    InvalidateHierarchyFilterCache();
    return true;
}

bool Editor::IsSceneComponentValueValid(
    const EditorSceneComponentKind kind, const EditorSceneComponentValue& value) const noexcept {
    return std::visit(
        [kind](const auto& typedValue) noexcept {
            using ValueType = std::decay_t<decltype(typedValue)>;
            if constexpr (std::is_same_v<ValueType, Scene::CameraComponent>) {
                return kind == EditorSceneComponentKind::Camera && Scene::IsCameraComponentValid(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::MeshComponent>) {
                return kind == EditorSceneComponentKind::Mesh && Scene::IsMeshComponentValid(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::LightComponent>) {
                return kind == EditorSceneComponentKind::Light && Scene::IsLightComponentValid(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::ColliderComponent>) {
                return kind == EditorSceneComponentKind::Collider && Scene::IsColliderComponentValid(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::Rigid3DComponent>) {
                return kind == EditorSceneComponentKind::Rigid3D && Scene::IsRigid3DComponentValid(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::AudioSourceComponent>) {
                return kind == EditorSceneComponentKind::AudioSource && Scene::IsAudioSourceComponentValid(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::ParticleEmitterComponent>) {
                return kind == EditorSceneComponentKind::ParticleEmitter &&
                       Scene::IsParticleEmitterComponentValid(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::ScriptComponent>) {
                return kind == EditorSceneComponentKind::Script && Scene::IsScriptComponentValid(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::AnimationSequencerComponent>) {
                return kind == EditorSceneComponentKind::AnimationSequencer &&
                       Scene::IsAnimationSequencerComponentValid(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::WorldEnvironment3DComponent>) {
                return kind == EditorSceneComponentKind::WorldEnvironment &&
                       Scene::IsWorldEnvironment3DObjectComponentValid(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::CharacterControllerComponent>) {
                return kind == EditorSceneComponentKind::CharacterController &&
                       Scene::IsCharacterControllerComponentValid(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::CanvasComponent>) {
                return kind == EditorSceneComponentKind::Canvas && Scene::IsCanvasComponentValid(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::UITextComponent>) {
                return kind == EditorSceneComponentKind::UIText && Scene::IsUITextComponentValid(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::UIImageComponent>) {
                return kind == EditorSceneComponentKind::UIImage && Scene::IsUIImageComponentValid(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::UIButtonComponent>) {
                return kind == EditorSceneComponentKind::UIButton && Scene::IsUIButtonComponentValid(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::ProcessComponent>) {
                return kind == EditorSceneComponentKind::Process &&
                       Scene::IsProcessComponentValid(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::ThreadGroupComponent>) {
                return kind == EditorSceneComponentKind::ThreadGroup &&
                       Scene::IsThreadGroupComponentValid(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::AutoTranslateComponent>) {
                return kind == EditorSceneComponentKind::AutoTranslate &&
                       Scene::IsAutoTranslateComponentValid(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::ObjectMetadataComponent>) {
                return kind == EditorSceneComponentKind::ObjectMetadata &&
                       Scene::IsObjectMetadataComponentValid(typedValue);            } else if constexpr (std::is_same_v<ValueType, Scene::PhysicsInterpolationComponent>) {
                return kind == EditorSceneComponentKind::PhysicsInterpolation &&
                       Scene::IsPhysicsInterpolationComponentValid(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::EditorDescriptionComponent>) {
                return kind == EditorSceneComponentKind::EditorDescription &&
                       Scene::IsEditorDescriptionComponentValid(typedValue);
            } else {
                return false;
            }
        },
        value);
}

bool Editor::AreSceneComponentValuesEqual(const EditorSceneComponentValue& lhs,
                                                 const EditorSceneComponentValue& rhs) const noexcept {
    return std::visit(
        [](const auto& left, const auto& right) noexcept {
            using LeftType = std::decay_t<decltype(left)>;
            using RightType = std::decay_t<decltype(right)>;
            if constexpr (!std::is_same_v<LeftType, RightType>) {
                return false;
            } else if constexpr (std::is_same_v<LeftType, Scene::CameraComponent>) {
                return left.fieldOfViewDegrees == right.fieldOfViewDegrees && left.nearPlane == right.nearPlane &&
                       left.farPlane == right.farPlane && left.projection == right.projection &&
                       left.orthographicSize == right.orthographicSize && left.current == right.current;
            } else if constexpr (std::is_same_v<LeftType, Scene::MeshComponent>) {
                return left.meshGuid == right.meshGuid && left.materialGuid == right.materialGuid;
            } else if constexpr (std::is_same_v<LeftType, Scene::LightComponent>) {
                return left.color == right.color && left.intensity == right.intensity && left.type == right.type &&
                       left.range == right.range && left.spotAngleDegrees == right.spotAngleDegrees;
            } else if constexpr (std::is_same_v<LeftType, Scene::ColliderComponent>) {
                return left.halfExtents == right.halfExtents && left.collisionLayer == right.collisionLayer &&
                       left.collisionMask == right.collisionMask && left.friction == right.friction &&
                       left.restitution == right.restitution && left.density == right.density &&
                       left.shapeType == right.shapeType && left.radius == right.radius &&
                       left.height == right.height && left.disabled == right.disabled;
            } else if constexpr (std::is_same_v<LeftType, Scene::Rigid3DComponent>) {
                return left.mass == right.mass && left.isKinematic == right.isKinematic &&
                       left.velocity == right.velocity && left.angularVelocity == right.angularVelocity &&
                       left.torque == right.torque && left.inverseInertia == right.inverseInertia &&
                       left.drag == right.drag && left.gravityScale == right.gravityScale;
            } else if constexpr (std::is_same_v<LeftType, Scene::AudioSourceComponent>) {
                return left.audioAssetPath == right.audioAssetPath && left.mixerGroup == right.mixerGroup &&
                       left.volume == right.volume && left.looping == right.looping && left.pitch == right.pitch &&
                       left.spatial == right.spatial && left.minDistance == right.minDistance &&
                       left.maxDistance == right.maxDistance && left.attenuationCurve == right.attenuationCurve &&
                       left.playOnAwake == right.playOnAwake;
            } else if constexpr (std::is_same_v<LeftType, Scene::ParticleEmitterComponent>) {
                return left.maxParticles == right.maxParticles && left.emitting == right.emitting &&
                       left.emissionRate == right.emissionRate && left.lifetimeSeconds == right.lifetimeSeconds &&
                       left.systemAssetPath == right.systemAssetPath && left.autoPlay == right.autoPlay &&
                       left.seed == right.seed;
            } else if constexpr (std::is_same_v<LeftType, Scene::ScriptComponent>) {
                return left.scriptAssetPath == right.scriptAssetPath && left.exportValues == right.exportValues;
            } else if constexpr (std::is_same_v<LeftType, Scene::AnimationSequencerComponent>) {
                return left.HasSameSettings(right);
            } else if constexpr (std::is_same_v<LeftType, Scene::WorldEnvironment3DComponent>) {
                return left.skyAssetPath == right.skyAssetPath && left.ambientSource == right.ambientSource &&
                       left.ambientColor == right.ambientColor && left.fogColor == right.fogColor &&
                       left.ambientEnergy == right.ambientEnergy &&
                       left.exposure == right.exposure && left.fogDensity == right.fogDensity &&
                       left.fogEnabled == right.fogEnabled &&
                       left.postProcessingEnabled == right.postProcessingEnabled;
            } else if constexpr (std::is_same_v<LeftType, Scene::CharacterControllerComponent>) {
                return left.moveSpeed == right.moveSpeed && left.jumpHeight == right.jumpHeight &&
                       left.gravityScale == right.gravityScale;
            } else if constexpr (std::is_same_v<LeftType, Scene::CanvasComponent>) {
                return left.visible == right.visible && left.sortOrder == right.sortOrder;
            } else if constexpr (std::is_same_v<LeftType, Scene::UITextComponent>) {
                return left.text == right.text && left.positionPixels == right.positionPixels &&
                       left.fontSize == right.fontSize && left.color == right.color && left.alpha == right.alpha;
            } else if constexpr (std::is_same_v<LeftType, Scene::UIImageComponent>) {
                return left.textureAssetGuid == right.textureAssetGuid && left.rect == right.rect &&
                       left.tintColor == right.tintColor && left.alpha == right.alpha;
            } else if constexpr (std::is_same_v<LeftType, Scene::UIButtonComponent>) {
                return left.rect == right.rect && left.normalColor == right.normalColor &&
                       left.hoverColor == right.hoverColor && left.pressedColor == right.pressedColor;
            } else if constexpr (std::is_same_v<LeftType, Scene::PhysicsInterpolationComponent>) {
                // Only `mode` is the authored, comparable field - the pose members are
                // runtime-computed and never part of an authoring diff (see the component's own
                // doc comment).
                return left.mode == right.mode;
            } else if constexpr (std::is_same_v<LeftType, Scene::EditorDescriptionComponent>) {
                return left.description == right.description;
            } else if constexpr (std::is_same_v<LeftType, Scene::ProcessComponent>) {
                return left.mode == right.mode && left.priority == right.priority &&
                       left.physicsPriority == right.physicsPriority;
            } else if constexpr (std::is_same_v<LeftType, Scene::ThreadGroupComponent>) {
                return left.mode == right.mode && left.order == right.order;
            } else if constexpr (std::is_same_v<LeftType, Scene::AutoTranslateComponent>) {
                return left.mode == right.mode;
            } else if constexpr (std::is_same_v<LeftType, Scene::ObjectMetadataComponent>) {
                return left.entries == right.entries;
            } else {
                return false;
            }
        },
        lhs,
        rhs);
}

bool Editor::ApplySceneComponentState(
    const Scene::Entity entity, const EditorSceneComponentKind kind,
    const std::optional<EditorSceneComponentValue>& value) {
    if (!IsDocumentEntity(entity)) {
        return false;
    }

    const auto apply = [&]<typename T>() {
        Scene::IEntityManager& entityManager = m_services->GetEntityManager();
        if (!value.has_value()) {
            if (!entityManager.HasComponent<T>(entity)) {
                return false;
            }
            entityManager.RemoveComponent<T>(entity);
            return true;
        }
        const T* const typedValue = std::get_if<T>(&*value);
        if (typedValue == nullptr) {
            return false;
        }
        if (entityManager.HasComponent<T>(entity)) {
            entityManager.GetComponent<T>(entity) = *typedValue;
        } else {
            entityManager.AddComponent<T>(entity, *typedValue);
        }
        return true;
    };

    switch (kind) {
        case EditorSceneComponentKind::Camera: {
            const bool applied = apply.template operator()<Scene::CameraComponent>();
            if (applied && value.has_value()) {
                Scene::IEntityManager& entityManager = m_services->GetEntityManager();
                if (entityManager.HasComponent<Scene::CameraComponent>(entity) &&
                    entityManager.GetComponent<Scene::CameraComponent>(entity).current) {
                    Scene::MakeCameraCurrent(entityManager, entity);
                }
            }
            return applied;
        }
        case EditorSceneComponentKind::Mesh:
            return apply.template operator()<Scene::MeshComponent>();
        case EditorSceneComponentKind::Light:
            return apply.template operator()<Scene::LightComponent>();
        case EditorSceneComponentKind::Collider: {
            Scene::IEntityManager& entityManager = m_services->GetEntityManager();
            const bool existed = entityManager.HasComponent<Scene::ColliderComponent>(entity);
            const bool applied = apply.template operator()<Scene::ColliderComponent>();
            // Born on an object that is already a primitive mesh, the collider takes that mesh's
            // shape rather than the field defaults - the same rule the Inspector applies when it is
            // the mesh that changes. Only on the frame it is added: an author who then resizes it
            // keeps what they set, because nothing here runs again for this entity.
            if (applied && !existed && entityManager.HasComponent<Scene::ColliderComponent>(entity) &&
                entityManager.HasComponent<Scene::PrimitiveMeshComponent>(entity)) {
                FitColliderToPrimitiveKind(
                    entityManager.GetComponent<Scene::ColliderComponent>(entity),
                    entityManager.GetComponent<Scene::PrimitiveMeshComponent>(entity).kind);
            }
            return applied;
        }
        case EditorSceneComponentKind::Rigid3D:
            return apply.template operator()<Scene::Rigid3DComponent>();
        case EditorSceneComponentKind::AudioSource:
            return apply.template operator()<Scene::AudioSourceComponent>();
        case EditorSceneComponentKind::ParticleEmitter:
            return apply.template operator()<Scene::ParticleEmitterComponent>();
        case EditorSceneComponentKind::Script:
            return apply.template operator()<Scene::ScriptComponent>();
        case EditorSceneComponentKind::AnimationSequencer:
            return apply.template operator()<Scene::AnimationSequencerComponent>();
        case EditorSceneComponentKind::WorldEnvironment:
            return apply.template operator()<Scene::WorldEnvironment3DComponent>();
        case EditorSceneComponentKind::CharacterController:
            return apply.template operator()<Scene::CharacterControllerComponent>();
        case EditorSceneComponentKind::Canvas:
            return apply.template operator()<Scene::CanvasComponent>();
        case EditorSceneComponentKind::UIText:
            return apply.template operator()<Scene::UITextComponent>();
        case EditorSceneComponentKind::UIImage:
            return apply.template operator()<Scene::UIImageComponent>();
        case EditorSceneComponentKind::UIButton:
            return apply.template operator()<Scene::UIButtonComponent>();
        case EditorSceneComponentKind::PhysicsInterpolation:
            return apply.template operator()<Scene::PhysicsInterpolationComponent>();
        case EditorSceneComponentKind::EditorDescription:
            return apply.template operator()<Scene::EditorDescriptionComponent>();
        case EditorSceneComponentKind::Process:
            return apply.template operator()<Scene::ProcessComponent>();
        case EditorSceneComponentKind::ThreadGroup:
            return apply.template operator()<Scene::ThreadGroupComponent>();
        case EditorSceneComponentKind::AutoTranslate:
            return apply.template operator()<Scene::AutoTranslateComponent>();
        case EditorSceneComponentKind::ObjectMetadata:
            return apply.template operator()<Scene::ObjectMetadataComponent>();
    }
    return false;
}

bool Editor::SetSelectedSceneComponent(const EditorSceneComponentKind kind,
                                              const EditorSceneComponentValue& value) {
    if (!IsAuthoringCommandAllowed() || !HasSingleDocumentSelection() ||
        !IsSceneComponentValueValid(kind, value)) {
        return false;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    std::optional<EditorSceneComponentValue> before;
    switch (kind) {
        case EditorSceneComponentKind::Camera:
            if (entityManager.HasComponent<Scene::CameraComponent>(m_selectedEntity)) {
                before = entityManager.GetComponent<Scene::CameraComponent>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKind::Mesh:
            if (entityManager.HasComponent<Scene::MeshComponent>(m_selectedEntity)) {
                before = entityManager.GetComponent<Scene::MeshComponent>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKind::Light:
            if (entityManager.HasComponent<Scene::LightComponent>(m_selectedEntity)) {
                before = entityManager.GetComponent<Scene::LightComponent>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKind::Collider:
            if (entityManager.HasComponent<Scene::ColliderComponent>(m_selectedEntity)) {
                before = entityManager.GetComponent<Scene::ColliderComponent>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKind::Rigid3D:
            if (entityManager.HasComponent<Scene::Rigid3DComponent>(m_selectedEntity)) {
                before = entityManager.GetComponent<Scene::Rigid3DComponent>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKind::AudioSource:
            if (entityManager.HasComponent<Scene::AudioSourceComponent>(m_selectedEntity)) {
                before = entityManager.GetComponent<Scene::AudioSourceComponent>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKind::ParticleEmitter:
            if (entityManager.HasComponent<Scene::ParticleEmitterComponent>(m_selectedEntity)) {
                before = entityManager.GetComponent<Scene::ParticleEmitterComponent>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKind::Script:
            if (entityManager.HasComponent<Scene::ScriptComponent>(m_selectedEntity)) {
                before = entityManager.GetComponent<Scene::ScriptComponent>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKind::AnimationSequencer:
            if (entityManager.HasComponent<Scene::AnimationSequencerComponent>(m_selectedEntity)) {
                before = entityManager.GetComponent<Scene::AnimationSequencerComponent>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKind::WorldEnvironment:
            if (entityManager.HasComponent<Scene::WorldEnvironment3DComponent>(m_selectedEntity)) {
                before = entityManager.GetComponent<Scene::WorldEnvironment3DComponent>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKind::CharacterController:
            if (entityManager.HasComponent<Scene::CharacterControllerComponent>(m_selectedEntity)) {
                before = entityManager.GetComponent<Scene::CharacterControllerComponent>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKind::Canvas:
            if (entityManager.HasComponent<Scene::CanvasComponent>(m_selectedEntity)) {
                before = entityManager.GetComponent<Scene::CanvasComponent>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKind::UIText:
            if (entityManager.HasComponent<Scene::UITextComponent>(m_selectedEntity)) {
                before = entityManager.GetComponent<Scene::UITextComponent>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKind::UIImage:
            if (entityManager.HasComponent<Scene::UIImageComponent>(m_selectedEntity)) {
                before = entityManager.GetComponent<Scene::UIImageComponent>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKind::UIButton:
            if (entityManager.HasComponent<Scene::UIButtonComponent>(m_selectedEntity)) {
                before = entityManager.GetComponent<Scene::UIButtonComponent>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKind::PhysicsInterpolation:
            if (entityManager.HasComponent<Scene::PhysicsInterpolationComponent>(m_selectedEntity)) {
                before = entityManager.GetComponent<Scene::PhysicsInterpolationComponent>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKind::EditorDescription:
            if (entityManager.HasComponent<Scene::EditorDescriptionComponent>(m_selectedEntity)) {
                before = entityManager.GetComponent<Scene::EditorDescriptionComponent>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKind::Process:
            if (entityManager.HasComponent<Scene::ProcessComponent>(m_selectedEntity)) {
                before = entityManager.GetComponent<Scene::ProcessComponent>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKind::ThreadGroup:
            if (entityManager.HasComponent<Scene::ThreadGroupComponent>(m_selectedEntity)) {
                before = entityManager.GetComponent<Scene::ThreadGroupComponent>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKind::AutoTranslate:
            if (entityManager.HasComponent<Scene::AutoTranslateComponent>(m_selectedEntity)) {
                before = entityManager.GetComponent<Scene::AutoTranslateComponent>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKind::ObjectMetadata:
            if (entityManager.HasComponent<Scene::ObjectMetadataComponent>(m_selectedEntity)) {
                before = entityManager.GetComponent<Scene::ObjectMetadataComponent>(m_selectedEntity);
            }
            break;
    }
    if (before.has_value() && AreSceneComponentValuesEqual(*before, value)) {
        return false;
    }

    const EditorSelectionSnapshot selectionBefore = CaptureSelectionSnapshot();
    const bool dirtyBefore = m_sceneDirty;
    if (!ApplySceneComponentState(m_selectedEntity, kind, value)) {
        return false;
    }
    m_sceneDirty = true;
    RecordHistory(SceneComponentHistoryEntry{
        m_selectedEntity, kind, before, value, selectionBefore, CaptureSelectionSnapshot(), dirtyBefore, true});
    return true;
}

bool Editor::RemoveSelectedSceneComponent(const EditorSceneComponentKind kind) {
    if (!IsAuthoringCommandAllowed() || !HasSingleDocumentSelection()) {
        return false;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    std::optional<EditorSceneComponentValue> before;
    switch (kind) {
        case EditorSceneComponentKind::Camera:
            if (entityManager.HasComponent<Scene::CameraComponent>(m_selectedEntity)) before = entityManager.GetComponent<Scene::CameraComponent>(m_selectedEntity);
            break;
        case EditorSceneComponentKind::Mesh:
            if (entityManager.HasComponent<Scene::MeshComponent>(m_selectedEntity)) before = entityManager.GetComponent<Scene::MeshComponent>(m_selectedEntity);
            break;
        case EditorSceneComponentKind::Light:
            if (entityManager.HasComponent<Scene::LightComponent>(m_selectedEntity)) before = entityManager.GetComponent<Scene::LightComponent>(m_selectedEntity);
            break;
        case EditorSceneComponentKind::Collider:
            if (entityManager.HasComponent<Scene::ColliderComponent>(m_selectedEntity)) before = entityManager.GetComponent<Scene::ColliderComponent>(m_selectedEntity);
            break;
        case EditorSceneComponentKind::Rigid3D:
            if (entityManager.HasComponent<Scene::Rigid3DComponent>(m_selectedEntity)) before = entityManager.GetComponent<Scene::Rigid3DComponent>(m_selectedEntity);
            break;
        case EditorSceneComponentKind::AudioSource:
            if (entityManager.HasComponent<Scene::AudioSourceComponent>(m_selectedEntity)) before = entityManager.GetComponent<Scene::AudioSourceComponent>(m_selectedEntity);
            break;
        case EditorSceneComponentKind::ParticleEmitter:
            if (entityManager.HasComponent<Scene::ParticleEmitterComponent>(m_selectedEntity)) before = entityManager.GetComponent<Scene::ParticleEmitterComponent>(m_selectedEntity);
            break;
        case EditorSceneComponentKind::Script:
            if (entityManager.HasComponent<Scene::ScriptComponent>(m_selectedEntity)) before = entityManager.GetComponent<Scene::ScriptComponent>(m_selectedEntity);
            break;
        case EditorSceneComponentKind::AnimationSequencer:
            if (entityManager.HasComponent<Scene::AnimationSequencerComponent>(m_selectedEntity)) before = entityManager.GetComponent<Scene::AnimationSequencerComponent>(m_selectedEntity);
            break;
        case EditorSceneComponentKind::WorldEnvironment:
            if (entityManager.HasComponent<Scene::WorldEnvironment3DComponent>(m_selectedEntity)) before = entityManager.GetComponent<Scene::WorldEnvironment3DComponent>(m_selectedEntity);
            break;
        case EditorSceneComponentKind::CharacterController:
            if (entityManager.HasComponent<Scene::CharacterControllerComponent>(m_selectedEntity)) before = entityManager.GetComponent<Scene::CharacterControllerComponent>(m_selectedEntity);
            break;
        case EditorSceneComponentKind::Canvas:
            if (entityManager.HasComponent<Scene::CanvasComponent>(m_selectedEntity)) before = entityManager.GetComponent<Scene::CanvasComponent>(m_selectedEntity);
            break;
        case EditorSceneComponentKind::UIText:
            if (entityManager.HasComponent<Scene::UITextComponent>(m_selectedEntity)) before = entityManager.GetComponent<Scene::UITextComponent>(m_selectedEntity);
            break;
        case EditorSceneComponentKind::UIImage:
            if (entityManager.HasComponent<Scene::UIImageComponent>(m_selectedEntity)) before = entityManager.GetComponent<Scene::UIImageComponent>(m_selectedEntity);
            break;
        case EditorSceneComponentKind::UIButton:
            if (entityManager.HasComponent<Scene::UIButtonComponent>(m_selectedEntity)) before = entityManager.GetComponent<Scene::UIButtonComponent>(m_selectedEntity);
            break;
        case EditorSceneComponentKind::PhysicsInterpolation:
            if (entityManager.HasComponent<Scene::PhysicsInterpolationComponent>(m_selectedEntity)) before = entityManager.GetComponent<Scene::PhysicsInterpolationComponent>(m_selectedEntity);
            break;
        case EditorSceneComponentKind::EditorDescription:
            if (entityManager.HasComponent<Scene::EditorDescriptionComponent>(m_selectedEntity)) before = entityManager.GetComponent<Scene::EditorDescriptionComponent>(m_selectedEntity);
            break;
        case EditorSceneComponentKind::Process:
            if (entityManager.HasComponent<Scene::ProcessComponent>(m_selectedEntity)) before = entityManager.GetComponent<Scene::ProcessComponent>(m_selectedEntity);
            break;
        case EditorSceneComponentKind::ThreadGroup:
            if (entityManager.HasComponent<Scene::ThreadGroupComponent>(m_selectedEntity)) before = entityManager.GetComponent<Scene::ThreadGroupComponent>(m_selectedEntity);
            break;
        case EditorSceneComponentKind::AutoTranslate:
            if (entityManager.HasComponent<Scene::AutoTranslateComponent>(m_selectedEntity)) before = entityManager.GetComponent<Scene::AutoTranslateComponent>(m_selectedEntity);
            break;
        case EditorSceneComponentKind::ObjectMetadata:
            if (entityManager.HasComponent<Scene::ObjectMetadataComponent>(m_selectedEntity)) before = entityManager.GetComponent<Scene::ObjectMetadataComponent>(m_selectedEntity);
            break;
    }
    if (!before.has_value()) {
        return false;
    }

    const EditorSelectionSnapshot selectionBefore = CaptureSelectionSnapshot();
    const bool dirtyBefore = m_sceneDirty;
    if (!ApplySceneComponentState(m_selectedEntity, kind, std::nullopt)) {
        return false;
    }
    m_sceneDirty = true;
    RecordHistory(SceneComponentHistoryEntry{
        m_selectedEntity, kind, before, std::nullopt, selectionBefore, CaptureSelectionSnapshot(), dirtyBefore, true});
    return true;
}

bool Editor::SetSelectedEntityName(std::string name) {
    if (!IsAuthoringCommandAllowed() || !HasSingleDocumentSelection() ||
        !IsEntityNameValid(name)) {
        return false;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    std::optional<std::string> beforeName;
    if (entityManager.HasComponent<Scene::NameComponent>(m_selectedEntity)) {
        beforeName = entityManager.GetComponent<Scene::NameComponent>(m_selectedEntity).name;
        if (*beforeName == name) {
            return false;
        }
    }

    const EditorSelectionSnapshot selectionBefore = CaptureSelectionSnapshot();
    const bool dirtyBefore = m_sceneDirty;
    const std::optional<std::string> afterName{std::move(name)};
    if (!ApplyEntityNameState(m_selectedEntity, afterName)) {
        return false;
    }

    m_sceneDirty = true;
    RecordHistory(NameHistoryEntry{
        m_selectedEntity, beforeName, afterName, selectionBefore, CaptureSelectionSnapshot(), dirtyBefore, true});
    return true;
}

bool Editor::ComputeTranslatedTransform(const Scene::Entity entity,
                                              const Math::Vector3& worldDelta,
                                              const Scene::TransformComponent& source,
                                              Scene::TransformComponent& outTransform) const {
    if (!IsFiniteVector(worldDelta)) {
        return false;
    }
    Math::Vector3 localDelta{};
    if (!ComputeLocalDeltaForWorldDelta(entity, worldDelta, localDelta)) {
        return false;
    }
    outTransform = source;
    outTransform.localPosition += localDelta;
    return true;
}

bool Editor::ComputeGestureTransform(const EditorToolSessionMode mode,
                                           const EditorTransformAxis axis, const float amount,
                                           const Scene::TransformComponent& source,
                                           Scene::TransformComponent& outTransform) const {
    if (!IsFinite(amount)) {
        return false;
    }

    outTransform = source;
    switch (mode) {
        case EditorToolSessionMode::Translate: {
            if (axis == EditorTransformAxis::None) {
                return false;
            }
            // Snap the DISTANCE along the axis, then build the delta from it - snapping the
            // resulting vector per component would quantise a diagonal axis differently.
            const float snappedDistance =
                m_transformSnappingSettings.enabled
                    ? SnapScalar(amount, m_transformSnappingSettings.translateStep)
                    : amount;
            return ComputeTranslatedTransform(m_selectedEntity,
                                                 GetAxisVector(axis) * snappedDistance, source,
                                                 outTransform);
        }
        case EditorToolSessionMode::Rotate: {
            if (axis == EditorTransformAxis::None) {
                return false;
            }
            const float rotateStepRadians =
                (m_transformSnappingSettings.rotateStepDegrees * std::numbers::pi_v<float>) / 180.0F;
            const float snappedRadians = m_transformSnappingSettings.enabled
                                             ? SnapScalar(amount, rotateStepRadians)
                                             : amount;
            Math::Quaternion localRotation{};
            if (!ComputeLocalRotationForWorldAxis(m_selectedEntity, source.localRotation,
                                                     GetAxisVector(axis), snappedRadians,
                                                     localRotation)) {
                return false;
            }
            outTransform.localRotation = localRotation;
            return true;
        }
        case EditorToolSessionMode::Scale: {
            const float snappedDelta = m_transformSnappingSettings.scaleEnabled
                                           ? SnapScalar(amount, m_transformSnappingSettings.scaleStep)
                                           : amount;
            if (axis == EditorTransformAxis::None) {
                // Uniform: every component moves by the same additive offset. The command rejects
                // as a whole if any result is invalid; it never clamps one component or silently
                // turns the request into a proportional scale.
                if (!IsFinite(snappedDelta)) {
                    return false;
                }
                outTransform.localScale.x += snappedDelta;
                outTransform.localScale.y += snappedDelta;
                outTransform.localScale.z += snappedDelta;
                return IsFinite(outTransform.localScale.x) && IsFinite(outTransform.localScale.y) &&
                       IsFinite(outTransform.localScale.z) &&
                       outTransform.localScale.x >= kMinimumLocalScale &&
                       outTransform.localScale.y >= kMinimumLocalScale &&
                       outTransform.localScale.z >= kMinimumLocalScale;
            }
            float* component = nullptr;
            switch (axis) {
                case EditorTransformAxis::X: component = &outTransform.localScale.x; break;
                case EditorTransformAxis::Y: component = &outTransform.localScale.y; break;
                case EditorTransformAxis::Z: component = &outTransform.localScale.z; break;
                case EditorTransformAxis::None: return false;
            }
            *component += snappedDelta;
            return IsFinite(*component) && *component >= kMinimumLocalScale;
        }
    }
    return false;
}

/// The shared guard the four public axis commands used to each repeat: valid editor state, a
/// single document entity selected, and that entity actually carrying a transform to read. It
/// reads the LIVE transform as the source, which is what an incremental command means.
bool Editor::TryComputeSelectedGestureTransform(const EditorToolSessionMode mode,
                                                      const EditorTransformAxis axis,
                                                      const float amount,
                                                      Scene::TransformComponent& outTransform) const {
    if (!IsAuthoringCommandAllowed() || !HasSingleDocumentSelection()) {
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent<Scene::TransformComponent>(m_selectedEntity)) {
        return false;
    }
    const Scene::TransformComponent live =
        entityManager.GetComponent<Scene::TransformComponent>(m_selectedEntity);
    return ComputeGestureTransform(mode, axis, amount, live, outTransform);
}

// ---------------------------------------------------------------------------------------------
// Transform gestures.
//
// A pointer drag is not a sequence of commands. Every public transform command records a history
// entry, so driving one from a drag would push an undo step per mouse-move frame and leave the
// user pressing Ctrl+Z several hundred times to get back where they started. A gesture is one
// transaction: many previews, then a single history entry from where the drag began to where it
// ended.
//
// The two properties that make this correct, and that are easy to get wrong:
//
//   * Previews are computed from the BASELINE captured at Begin, never from the live transform.
//     A drag reports the total offset from the press point every frame, so re-applying an
//     incremental command each frame would compound it into a runaway.
//   * Previews go through ApplyLocalTransform, the same scene-graph write the commands use,
//     but deliberately NOT through SetSelectedLocalTransform - that is the one that records
//     history, which is precisely what a preview must not do.
// ---------------------------------------------------------------------------------------------

bool Editor::BeginTransformGesture(const EditorToolSessionMode mode) {
    if (!IsAuthoringCommandAllowed() || !HasSingleDocumentSelection()) {
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent<Scene::TransformComponent>(m_selectedEntity)) {
        return false;
    }
    const Scene::TransformComponent baseline =
        entityManager.GetComponent<Scene::TransformComponent>(m_selectedEntity);
    // m_sceneDirty travels with the baseline so a cancelled gesture restores the document's
    // unsaved state as well as its transform - a drag that is abandoned must not leave the scene
    // looking modified.
    return m_toolSession.Begin(m_selectedEntity, mode, baseline, m_sceneDirty);
}

bool Editor::PreviewTransformGesture(const EditorTransformAxis axis, const float totalAmount) {
    if (m_toolSession.GetPhase() != EditorToolSessionPhase::Previewing) {
        return false;
    }
    const std::optional<EditorToolSessionSnapshot>& snapshot = m_toolSession.GetSnapshot();
    if (!snapshot.has_value() || !IsAuthoringCommandAllowed()) {
        return false;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const Scene::Entity entity = snapshot->entity;
    if (!entityManager.IsAlive(entity) ||
        !entityManager.HasComponent<Scene::TransformComponent>(entity)) {
        // The gesture's target went away underneath it. Discard rather than cancel: there is no
        // live transform left to compare against, so no restore can be claimed.
        m_toolSession.Discard();
        return false;
    }

    Scene::TransformComponent updated{};
    if (!ComputeGestureTransform(snapshot->mode, axis, totalAmount, snapshot->baselineTransform,
                                    updated)) {
        return false;
    }
    if (!IsTransformFinite(updated) || !ApplyLocalTransform(entity, updated)) {
        return false;
    }
    m_sceneDirty = true;
    return m_toolSession.RecordPreviewApplied(updated);
}

bool Editor::PreviewTranslateGesture(const Math::Vector3& totalWorldDelta) {
    if (m_toolSession.GetPhase() != EditorToolSessionPhase::Previewing) {
        return false;
    }
    const std::optional<EditorToolSessionSnapshot>& snapshot = m_toolSession.GetSnapshot();
    if (!snapshot.has_value() || snapshot->mode != EditorToolSessionMode::Translate ||
        !IsAuthoringCommandAllowed() || !IsFiniteVector(totalWorldDelta)) {
        return false;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const Scene::Entity entity = snapshot->entity;
    if (!entityManager.IsAlive(entity) ||
        !entityManager.HasComponent<Scene::TransformComponent>(entity)) {
        m_toolSession.Discard();
        return false;
    }

    // A plane drag has no single axis, so each component is quantised on its own - which lands on
    // the same lattice a pair of axis drags would have reached.
    Math::Vector3 worldDelta = totalWorldDelta;
    if (m_transformSnappingSettings.enabled) {
        const float step = m_transformSnappingSettings.translateStep;
        worldDelta = Math::Vector3{SnapScalar(worldDelta.x, step), SnapScalar(worldDelta.y, step),
                                      SnapScalar(worldDelta.z, step)};
    }

    Scene::TransformComponent updated{};
    if (!ComputeTranslatedTransform(entity, worldDelta, snapshot->baselineTransform, updated)) {
        return false;
    }
    if (!IsTransformFinite(updated) || !ApplyLocalTransform(entity, updated)) {
        return false;
    }
    m_sceneDirty = true;
    return m_toolSession.RecordPreviewApplied(updated);
}

bool Editor::PreviewTransformGestureValue(const Scene::TransformComponent& transform) {
    if (m_toolSession.GetPhase() != EditorToolSessionPhase::Previewing) {
        return false;
    }
    const std::optional<EditorToolSessionSnapshot>& snapshot = m_toolSession.GetSnapshot();
    if (!snapshot.has_value() || !IsAuthoringCommandAllowed() || !IsTransformFinite(transform)) {
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const Scene::Entity entity = snapshot->entity;
    if (!entityManager.IsAlive(entity) ||
        !entityManager.HasComponent<Scene::TransformComponent>(entity)) {
        m_toolSession.Discard();
        return false;
    }
    if (!ApplyLocalTransform(entity, transform)) {
        return false;
    }
    m_sceneDirty = true;
    return m_toolSession.RecordPreviewApplied(transform);
}

bool Editor::CommitTransformGesture() {
    if (m_toolSession.GetPhase() != EditorToolSessionPhase::Previewing) {
        return false;
    }
    const std::optional<EditorToolSessionSnapshot>& live = m_toolSession.GetSnapshot();
    if (!live.has_value()) {
        return false;
    }
    const bool changed = !AreTransformsEqual(live->baselineTransform, live->lastAppliedTransform);

    const std::optional<EditorToolSessionSnapshot> snapshot = m_toolSession.Commit(changed);
    if (!snapshot.has_value()) {
        return false;
    }
    if (!changed) {
        // A press and release that never moved anything. Restoring the dirty flag matters: a
        // no-op gesture must not mark an unmodified scene as needing a save.
        m_sceneDirty = snapshot->baselineDirty;
        return true;
    }

    // One entry for the whole drag. Selection cannot change while a gesture owns the pointer, so
    // the same snapshot describes both sides of it.
    const EditorSelectionSnapshot selection = CaptureSelectionSnapshot();
    m_sceneDirty = true;
    RecordHistory(TransformHistoryEntry{snapshot->entity, snapshot->baselineTransform,
                                              snapshot->lastAppliedTransform, selection, selection,
                                              snapshot->baselineDirty, true});
    return true;
}

bool Editor::CancelTransformGesture() {
    if (m_toolSession.GetPhase() != EditorToolSessionPhase::Previewing) {
        return false;
    }
    const std::optional<EditorToolSessionSnapshot>& live = m_toolSession.GetSnapshot();
    if (!live.has_value()) {
        return false;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const Scene::Entity entity = live->entity;
    if (!entityManager.IsAlive(entity) ||
        !entityManager.HasComponent<Scene::TransformComponent>(entity)) {
        m_toolSession.Discard();
        return false;
    }

    const Scene::TransformComponent current =
        entityManager.GetComponent<Scene::TransformComponent>(entity);
    const std::optional<EditorToolSessionSnapshot> snapshot = m_toolSession.Cancel(current);
    if (!snapshot.has_value()) {
        // ExternalTransformConflict: something else moved this entity since the last preview, so
        // the baseline is stale and writing it back would silently discard that change. The
        // session has already cleared; the external value is left exactly as it stands.
        return false;
    }

    if (!ApplyLocalTransform(entity, snapshot->baselineTransform)) {
        m_toolSession.MarkRestoreFailed();
        return false;
    }
    m_sceneDirty = snapshot->baselineDirty;
    return true;
}

bool Editor::TranslateSelectedAlongAxis(const EditorTransformAxis axis, const float worldDistance) {
    Scene::TransformComponent updated{};
    if (!TryComputeSelectedGestureTransform(EditorToolSessionMode::Translate, axis, worldDistance,
                                               updated)) {
        return false;
    }
    return SetSelectedLocalTransform(updated);
}

bool Editor::RotateSelectedAroundWorldAxis(const EditorTransformAxis axis, const float radians) {
    Scene::TransformComponent updated{};
    if (!TryComputeSelectedGestureTransform(EditorToolSessionMode::Rotate, axis, radians, updated)) {
        return false;
    }
    return SetSelectedLocalTransform(updated);
}

bool Editor::ScaleSelectedAlongAxis(const EditorTransformAxis axis,
                                          const float localScaleDelta) {
    if (axis == EditorTransformAxis::None) {
        return false; // None means "uniform" to the shared helper; this command is per-axis only
    }
    Scene::TransformComponent updated{};
    if (!TryComputeSelectedGestureTransform(EditorToolSessionMode::Scale, axis, localScaleDelta,
                                               updated)) {
        return false;
    }
    return SetSelectedLocalTransform(updated);
}

bool Editor::ScaleSelectedUniformly(const float localScaleOffset) {
    Scene::TransformComponent updated{};
    if (!TryComputeSelectedGestureTransform(EditorToolSessionMode::Scale,
                                               EditorTransformAxis::None, localScaleOffset, updated)) {
        return false;
    }
    return SetSelectedLocalTransform(updated);
}

bool Editor::IsReparentModeChangeAllowed() const noexcept {
    return IsAuthoringCommandAllowed();
}

bool Editor::SetReparentTransformMode(const EditorReparentTransformMode mode) {
    if (!IsReparentModeChangeAllowed()) {
        return false;
    }
    m_reparentTransformMode = mode;
    return true;
}

EditorReparentTransformMode Editor::GetReparentTransformMode() const noexcept {
    return m_reparentTransformMode;
}

bool Editor::SetTransformSnappingSettings(const EditorTransformSnappingSettings& settings) {
    if (!IsAuthoringCommandAllowed() || !AreTransformSnappingSettingsValid(settings)) {
        return false;
    }
    const EditorTransformSnappingSettings previous = m_transformSnappingSettings;
    m_transformSnappingSettings = settings;
    m_viewportOverlayState.snapEnabled = settings.enabled;
    namespace Id = EditorSettingId;
    NotifyEditorSettingChanged(Id::kSnapEnabled, previous.enabled, settings.enabled);
    NotifyEditorSettingChanged(Id::kSnapTranslateStep, static_cast<double>(previous.translateStep),
                                  static_cast<double>(settings.translateStep));
    NotifyEditorSettingChanged(Id::kSnapRotateStepDegrees, static_cast<double>(previous.rotateStepDegrees),
                                  static_cast<double>(settings.rotateStepDegrees));
    NotifyEditorSettingChanged(Id::kSnapScaleStep, static_cast<double>(previous.scaleStep),
                                  static_cast<double>(settings.scaleStep));
    NotifyEditorSettingChanged(Id::kSnapScaleEnabled, previous.scaleEnabled, settings.scaleEnabled);
    return true;
}

const EditorTransformSnappingSettings& Editor::GetTransformSnappingSettings() const noexcept {
    return m_transformSnappingSettings;
}

std::optional<EditorSelectionBounds> Editor::TryGetSelectedBounds() const {
    return TryGetEntityBounds(m_selectedEntity);
}

std::optional<EditorSelectionBounds> Editor::TryGetEntityBounds(const Scene::Entity entity) const {
    if (m_state != EditorState::Running || !IsDocumentEntity(entity)) {
        return std::nullopt;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent<Scene::TransformComponent>(entity) ||
        !entityManager.HasComponent<Scene::WorldTransformComponent>(entity) ||
        !entityManager.HasComponent<Scene::ColliderComponent>(entity)) {
        return std::nullopt;
    }

    const Scene::WorldTransformComponent& worldTransform =
        entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
    const Scene::ColliderComponent& collider =
        entityManager.GetComponent<Scene::ColliderComponent>(entity);
    if (worldTransform.dirty || !IsFiniteVector(worldTransform.worldPosition) ||
        !IsFiniteVector(worldTransform.worldScale) || !IsFiniteVector(collider.halfExtents) ||
        collider.halfExtents.x <= kVectorEpsilon || collider.halfExtents.y <= kVectorEpsilon ||
        collider.halfExtents.z <= kVectorEpsilon ||
        std::abs(worldTransform.worldScale.x) <= kVectorEpsilon ||
        std::abs(worldTransform.worldScale.y) <= kVectorEpsilon ||
        std::abs(worldTransform.worldScale.z) <= kVectorEpsilon) {
        return std::nullopt;
    }

    Math::Quaternion normalizedRotation{};
    if (!Math::TryNormalize(worldTransform.worldRotation, normalizedRotation)) {
        return std::nullopt;
    }

    constexpr std::array<Math::Vector3, 8> kCornerSigns{
        Math::Vector3{-1.0F, -1.0F, -1.0F},
        Math::Vector3{1.0F, -1.0F, -1.0F},
        Math::Vector3{1.0F, 1.0F, -1.0F},
        Math::Vector3{-1.0F, 1.0F, -1.0F},
        Math::Vector3{-1.0F, -1.0F, 1.0F},
        Math::Vector3{1.0F, -1.0F, 1.0F},
        Math::Vector3{1.0F, 1.0F, 1.0F},
        Math::Vector3{-1.0F, 1.0F, 1.0F},
    };

    EditorSelectionBounds bounds{};
    bounds.worldCenter = worldTransform.worldPosition;
    for (std::size_t index = 0U; index < kCornerSigns.size(); ++index) {
        const Math::Vector3 localCorner = kCornerSigns[index] * collider.halfExtents;
        const Math::Vector3 scaledCorner = localCorner * worldTransform.worldScale;
        bounds.worldCorners[index] = worldTransform.worldPosition +
                                     Math::RotateVector(normalizedRotation, scaledCorner);
        if (!IsFiniteVector(bounds.worldCorners[index])) {
            return std::nullopt;
        }
    }
    return bounds;
}

std::optional<EditorEntityKind> ParseEditorEntityKind(const std::string_view value) noexcept {
    if (value == "object3d" || value == "empty" || value == "node3d") {
        return EditorEntityKind::Empty;
    }
    if (value == "camera") {
        return EditorEntityKind::Camera;
    }
    if (value == "directionalLight") {
        return EditorEntityKind::DirectionalLight;
    }
    if (value == "collisionBox") {
        return EditorEntityKind::CollisionBox;
    }
    if (value == "cube") {
        return EditorEntityKind::Cube;
    }
    if (value == "uvSphere") {
        return EditorEntityKind::UVSphere;
    }
    if (value == "plane") {
        return EditorEntityKind::Plane;
    }
    return std::nullopt;
}

Scene::Entity Editor::CreateDocumentEntity(const EditorEntityKind kind) {
    if (!IsAuthoringCommandAllowed()) {
        return Scene::kInvalidEntity;
    }

    const EditorSelectionSnapshot selectionBefore = CaptureSelectionSnapshot();
    const bool dirtyBefore = m_sceneDirty;
    const Scene::Entity entity = CreateDocumentEntityInternal(kind, std::nullopt);
    if (entity == Scene::kInvalidEntity) {
        return Scene::kInvalidEntity;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const std::string createdName = entityManager.GetComponent<Scene::NameComponent>(entity).name;
    SelectEntity(entity);
    m_sceneDirty = true;
    RecordHistory(CreationHistoryEntry{
        kind, createdName, entity, selectionBefore, CaptureSelectionSnapshot(), dirtyBefore, true});
    return entity;
}

Scene::Entity Editor::CreateSceneObjectEntityInternal(const Scene::Objects::SceneObjectKind kind) {
    Scene::Entity entity = Scene::kInvalidEntity;
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const auto createObjectWithComponent = [this, &entityManager, kind](auto component) {
        // These kinds carry a component but no ObjectDefinition of their own, so they have no
        // `defaultName` to route through. Going via EditorEntityKind::Empty alone resolved every
        // one of them to Object3DObjectDefinition::defaultName - a freshly added RayCast3D, a
        // Marker3D and a LevelStreamer3D all appeared in the Outliner as "Object3D", "Object3D 2",
        // "Object3D 3", with nothing but their component list telling them apart. The registry's
        // displayName is already the kind's own name and unique per kind, so it is the name to use.
        // It still goes through MakeUniqueDocumentEntityName, matching the 29 kinds that DO have
        // a definition (CreateObjectDefinitionEntityInternal does the same), so adding two of a
        // kind yields "RayCast3D" then "RayCast3D 2" rather than a duplicate name.
        const Scene::Objects::SceneObjectDescriptor* const descriptor =
            Scene::Objects::FindSceneObjectDescriptor(kind);
        std::optional<std::string> explicitName;
        if (descriptor != nullptr) {
            explicitName = MakeUniqueDocumentEntityName(descriptor->displayName);
        }
        Scene::Entity created = CreateDocumentEntityInternal(EditorEntityKind::Empty, explicitName);
        if (created != Scene::kInvalidEntity) {
            using Component = std::decay_t<decltype(component)>;
            entityManager.AddComponent<Component>(created, std::move(component));
        }
        return created;
    };

    switch (kind) {
        // Every case creates from its own ObjectDefinition (Engine/Runtime/Objects/3D or
        // Objects/UI — one .h + .cpp per kind holds the recipe: components to attach,
        // authored defaults, default entity name). No object-kind-specific recipe is authored in
        // this switch anymore.
        case Scene::Objects::SceneObjectKind::Object3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::Object3DObjectDefinition{},
                                                            Scene::ApplyObject3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::Camera3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::Camera3DObjectDefinition{},
                                                            Scene::ApplyCamera3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::MeshInstance3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::MeshInstance3DObjectDefinition{},
                                                            Scene::ApplyMeshInstance3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::BoxMesh3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::BoxMesh3DObjectDefinition{},
                                                            Scene::ApplyBoxMesh3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::SphereMesh3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::SphereMesh3DObjectDefinition{},
                                                            Scene::ApplySphereMesh3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::PlaneMesh3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::PlaneMesh3DObjectDefinition{},
                                                            Scene::ApplyPlaneMesh3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::Light3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::Light3DObjectDefinition{},
                                                            Scene::ApplyLight3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::Collider3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::Collider3DObjectDefinition{},
                                                            Scene::ApplyCollider3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::Character3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::Character3DObjectDefinition{},
                                                            Scene::ApplyCharacter3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::Player3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::Player3DObjectDefinition{},
                                                            Scene::ApplyPlayer3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::Rigid3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::Rigid3DObjectDefinition{},
                                                            Scene::ApplyRigid3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::AnimationSequencer:
            entity = CreateObjectDefinitionEntityInternal(Scene::AnimationSequencerObjectDefinition{},
                                                            Scene::ApplyAnimationSequencerObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::AudioSource3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::AudioSource3DObjectDefinition{},
                                                            Scene::ApplyAudioSource3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::ParticleEmitter3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::ParticleEmitter3DObjectDefinition{},
                                                            Scene::ApplyParticleEmitter3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::Script:
            entity = CreateObjectDefinitionEntityInternal(Scene::ScriptObjectDefinition{},
                                                            Scene::ApplyScriptObjectDefinition);
            break;
        // Canvas family — the four UI kinds promoted out of the Inspector-only world, so
        // UI authoring uses the same Add-Object entry point (definitions live in
        // Engine/Runtime/Objects/UI).
        case Scene::Objects::SceneObjectKind::Canvas:
            entity = CreateObjectDefinitionEntityInternal(Scene::CanvasObjectDefinition{},
                                                            Scene::ApplyCanvasObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::UIText:
            entity = CreateObjectDefinitionEntityInternal(Scene::UITextObjectDefinition{},
                                                            Scene::ApplyUITextObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::UIImage:
            entity = CreateObjectDefinitionEntityInternal(Scene::UIImageObjectDefinition{},
                                                            Scene::ApplyUIImageObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::UIButton:
            entity = CreateObjectDefinitionEntityInternal(Scene::UIButtonObjectDefinition{},
                                                            Scene::ApplyUIButtonObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::Area3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::Area3DObjectDefinition{},
                                                            Scene::ApplyArea3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::RayCast3D:
            entity = createObjectWithComponent(Scene::RayCast3DComponent{});
            break;
        case Scene::Objects::SceneObjectKind::Static3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::Static3DObjectDefinition{},
                                                            Scene::ApplyStatic3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::Kinematic3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::Kinematic3DObjectDefinition{},
                                                            Scene::ApplyKinematic3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::NavMeshVolume3D:
            entity = CreateObjectDefinitionEntityInternal(
                Scene::NavMeshVolume3DObjectDefinition{},
                Scene::ApplyNavMeshVolume3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::NavSeeker3D:
            entity = CreateObjectDefinitionEntityInternal(
                Scene::NavSeeker3DObjectDefinition{},
                Scene::ApplyNavSeeker3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::Skeleton3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::Skeleton3DObjectDefinition{},
                                                            Scene::ApplySkeleton3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::BoneAttachment3D:
            entity = createObjectWithComponent(Scene::BoneAttachment3DComponent{});
            break;
        case Scene::Objects::SceneObjectKind::MontageSequencer:
            entity = createObjectWithComponent(Scene::MontageSequencerComponent{});
            break;
        case Scene::Objects::SceneObjectKind::TwoBoneIK3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::TwoBoneIK3DObjectDefinition{},
                                                            Scene::ApplyTwoBoneIK3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::FullBodyIK3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::FullBodyIK3DObjectDefinition{},
                                                            Scene::ApplyFullBodyIK3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::Ragdoll3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::Ragdoll3DObjectDefinition{},
                                                            Scene::ApplyRagdoll3DObjectDefinition);
            break;
        // SpringArm3D carries its own component like its neighbours, but its recipe
        // (Object3D baseline plus seeding currentLength to the authored armLength the same way the
        // deserializer seeds it) is a definition's worth of behaviour, so it reads like the rest.
        case Scene::Objects::SceneObjectKind::SpringArm3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::SpringArm3DObjectDefinition{},
                                                            Scene::ApplySpringArm3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::Marker3D:
            entity = CreateObjectDefinitionEntityInternal(
                Scene::Marker3DObjectDefinition{},
                Scene::ApplyMarker3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::Hitbox3D:
            entity = createObjectWithComponent(Scene::Hitbox3DComponent{});
            break;
        case Scene::Objects::SceneObjectKind::Hurtbox3D:
            entity = createObjectWithComponent(Scene::Hurtbox3DComponent{});
            break;
        case Scene::Objects::SceneObjectKind::Projectile3D:
            entity = createObjectWithComponent(Scene::Projectile3DComponent{});
            break;
        case Scene::Objects::SceneObjectKind::InteractionArea3D:
            entity = createObjectWithComponent(Scene::InteractionArea3DComponent{});
            break;
        case Scene::Objects::SceneObjectKind::WorldEnvironment3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::WorldEnvironmentObjectDefinition{},
                                                            Scene::ApplyWorldEnvironmentObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::Sky:
            entity = CreateObjectDefinitionEntityInternal(Scene::Sky3DObjectDefinition{},
                                                            Scene::ApplySky3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::DirectionalLight3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::DirectionalLight3DObjectDefinition{},
                                                            Scene::ApplyDirectionalLight3DObjectDefinition);
            if (entity != Scene::kInvalidEntity &&
                entityManager.HasComponent<Scene::TransformComponent>(entity)) {
                const std::optional<Math::Quaternion> rotation =
                    Scene::TryMakePreviewSunRotation(m_viewportOverlayState.previewSunAzimuth,
                                                        m_viewportOverlayState.previewSunAltitude);
                if (rotation.has_value()) {
                    auto transform = entityManager.GetComponent<Scene::TransformComponent>(entity);
                    transform.localRotation = *rotation;
                    m_services->GetSceneGraph().SetLocalTransform(entityManager, entity, transform);
                }
            }
            break;
        case Scene::Objects::SceneObjectKind::ReflectionProbe3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::ReflectionProbe3DObjectDefinition{},
                                                            Scene::ApplyReflectionProbe3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::Decal3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::Decal3DObjectDefinition{},
                                                            Scene::ApplyDecal3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::FogVolume3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::FogVolume3DObjectDefinition{},
                                                            Scene::ApplyFogVolume3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::LODGroup3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::LodGroup3DObjectDefinition{},
                                                            Scene::ApplyLodGroup3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::Occluder3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::Occluder3DObjectDefinition{},
                                                            Scene::ApplyOccluder3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::VisibilityRegion3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::VisibilityRegion3DObjectDefinition{},
                                                            Scene::ApplyVisibilityRegion3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::SpawnPoint3D:
            entity = createObjectWithComponent(Scene::SpawnPoint3DComponent{});
            break;
        case Scene::Objects::SceneObjectKind::LevelStreamer3D:
            entity = CreateObjectDefinitionEntityInternal(
                Scene::LevelStreamer3DObjectDefinition{},
                Scene::ApplyLevelStreamer3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::WorldPartition3D:
            entity = CreateObjectDefinitionEntityInternal(Scene::WorldPartition3DObjectDefinition{},
                                                            Scene::ApplyWorldPartition3DObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::AnimationGraph:
            entity = CreateObjectDefinitionEntityInternal(Scene::AnimationGraphObjectDefinition{},
                                                            Scene::ApplyAnimationGraphObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::Folder:
            entity = CreateObjectDefinitionEntityInternal(Scene::FolderObjectDefinition{},
                                                            Scene::ApplyFolderObjectDefinition);
            break;
        case Scene::Objects::SceneObjectKind::Viewport3D:
            // The level's 3D Viewport is created by the document layout (EnsureDocumentLayout).
            return Scene::kInvalidEntity;
        case Scene::Objects::SceneObjectKind::SceneRoot:
            // The scene root is created by the document lifecycle
            // (EnsureDocumentSceneRoot), never through the library path.
            return Scene::kInvalidEntity;
    }

    if (entity != Scene::kInvalidEntity) {
        // Typed at once, so snapshots taken of it (undo, prefab save) bring the type back with it.
        Scene::SetSceneObjectKind(entityManager, entity, kind);
    }
    return entity;
}

Scene::Entity Editor::CreateDocumentSceneObject(
    const Scene::Objects::SceneObjectKind kind, const Scene::Entity explicitParent) {
    if (!IsAuthoringCommandAllowed() || m_selectedEntities.size() > 1U) {
        return Scene::kInvalidEntity;
    }
    const Scene::Objects::SceneObjectDescriptor* descriptor =
        Scene::Objects::FindSceneObjectDescriptor(kind);
    if (descriptor == nullptr || !descriptor->libraryCreatable) {
        return Scene::kInvalidEntity;
    }

    // The level has one DirectionalLight3D and one WorldEnvironment at its top.
    if (IsOutlinerLayoutActive() && IsTopLevelSingletonKind(kind) &&
        FindTopLevelObject(kind) != Scene::kInvalidEntity) {
        return Scene::kInvalidEntity;
    }

    const EditorSelectionSnapshot selectionBefore = CaptureSelectionSnapshot();
    const bool dirtyBefore = m_sceneDirty;
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();

    // New objects join the hierarchy instead of becoming document roots, and are placed before the
    // snapshot below is taken, so undo and redo restore the same place.
    Scene::Entity parentObject = explicitParent;
    if (m_entityDocumentActive) {
        const Scene::Entity entityRoot = GetEntityEditorRoot();
        if (parentObject == Scene::kInvalidEntity || !IsDocumentEntity(parentObject) ||
            parentObject == GetDocumentSceneRoot()) {
            parentObject = entityRoot;
        }
    } else if (parentObject == Scene::kInvalidEntity || !IsDocumentEntity(parentObject)) {
        parentObject = ResolveNewObjectParentFor(kind);
    }

    const Scene::Entity entity = CreateSceneObjectEntityInternal(kind);
    if (entity == Scene::kInvalidEntity) {
        return Scene::kInvalidEntity;
    }
    // A new AnimationGraph's starting nodes get their parameters too, named after them, like every
    // node added to it later (the gameplay sets a node by its name).
    if (entityManager.HasComponent<Scene::AnimationGraphComponent>(entity)) {
        Scene::AnimationGraphComponent& graph = entityManager.GetComponent<Scene::AnimationGraphComponent>(entity);
        std::vector<std::uint32_t> ids;
        for (const Scene::AnimationGraphNode& node : graph.nodes) {
            ids.push_back(node.id);
        }
        for (const std::uint32_t id : ids) {
            BindNewNodeParameters(graph, id);
        }
    }
    if (parentObject != Scene::kInvalidEntity) {
        m_services->GetSceneGraph().SetParent(entityManager, entity, parentObject);
        InvalidateHierarchyFilterCache();

        // Auto-link skeleton references for BoneAttachment3D and TwoBoneIK3D if present in hierarchy
        Scene::Entity foundSkeleton = Scene::kInvalidEntity;
        if (entityManager.HasComponent<Scene::Skeleton3DComponent>(parentObject)) {
            foundSkeleton = parentObject;
        } else {
            for (const Scene::Entity child : m_services->GetSceneGraph().GetChildren(entityManager, parentObject)) {
                if (entityManager.HasComponent<Scene::Skeleton3DComponent>(child)) {
                    foundSkeleton = child;
                    break;
                }
            }
            if (foundSkeleton == Scene::kInvalidEntity) {
                for (Scene::Entity cur = parentObject, p = Scene::kInvalidEntity;
                     cur != Scene::kInvalidEntity && TryGetDocumentParent(cur, p) && p != Scene::kInvalidEntity;
                     cur = p) {
                    if (entityManager.HasComponent<Scene::Skeleton3DComponent>(p)) {
                        foundSkeleton = p;
                        break;
                    }
                }
            }
        }
        if (foundSkeleton != Scene::kInvalidEntity) {
            if (kind == Scene::Objects::SceneObjectKind::BoneAttachment3D &&
                entityManager.HasComponent<Scene::BoneAttachment3DComponent>(entity)) {
                Scene::BoneAttachment3DComponent attachment =
                    entityManager.GetComponent<Scene::BoneAttachment3DComponent>(entity);
                attachment.skeleton = foundSkeleton;
                attachment.boneIndex = Scene::kInvalidSkeletonBoneIndex;
                attachment.boneName.clear();
                entityManager.GetComponent<Scene::BoneAttachment3DComponent>(entity) = attachment;
            } else if (kind == Scene::Objects::SceneObjectKind::TwoBoneIK3D &&
                       entityManager.HasComponent<Scene::TwoBoneIK3DComponent>(entity)) {
                Scene::TwoBoneIK3DComponent ik =
                    entityManager.GetComponent<Scene::TwoBoneIK3DComponent>(entity);
                ik.skeleton = foundSkeleton;
                ik.rootBoneIndex = Scene::kInvalidSkeletonBoneIndex;
                ik.rootBoneName.clear();
                ik.middleBoneIndex = Scene::kInvalidSkeletonBoneIndex;
                ik.middleBoneName.clear();
                ik.endBoneIndex = Scene::kInvalidSkeletonBoneIndex;
                ik.endBoneName.clear();
                entityManager.GetComponent<Scene::TwoBoneIK3DComponent>(entity) = ik;
            } else if (kind == Scene::Objects::SceneObjectKind::FullBodyIK3D &&
                       entityManager.HasComponent<Scene::FullBodyIK3DComponent>(entity)) {
                Scene::FullBodyIK3DComponent solve =
                    entityManager.GetComponent<Scene::FullBodyIK3DComponent>(entity);
                solve.skeleton = foundSkeleton;
                solve.rootBoneIndex = Scene::kInvalidSkeletonBoneIndex;
                solve.rootBoneName.clear();
                entityManager.GetComponent<Scene::FullBodyIK3DComponent>(entity) = solve;
            } else if (kind == Scene::Objects::SceneObjectKind::Ragdoll3D &&
                       entityManager.HasComponent<Scene::Ragdoll3DComponent>(entity)) {
                Scene::Ragdoll3DComponent ragdoll =
                    entityManager.GetComponent<Scene::Ragdoll3DComponent>(entity);
                ragdoll.skeleton = foundSkeleton;
                ragdoll.bones.clear();
                ragdoll.initialized = false;
                entityManager.GetComponent<Scene::Ragdoll3DComponent>(entity) = ragdoll;
                static_cast<void>(Scene::InitializeRagdoll3D(entityManager, entity));
            }
        }
    }
    PlaceNewDocumentObject(entity);

    const std::optional<Scene::SceneSnapshot> snapshot = CaptureSubtree(entity);
    if (!snapshot.has_value()) {
        DestroyDocumentSubtree(entity);
        RestoreSelection(selectionBefore);
        m_sceneDirty = dirtyBefore;
        return Scene::kInvalidEntity;
    }

    SelectEntity(entity);
    m_sceneDirty = true;
    RecordHistory(SceneObjectCreationHistoryEntry{
        *snapshot, kind, entity, selectionBefore, CaptureSelectionSnapshot(), dirtyBefore, true,
        parentObject});
    return entity;
}

Scene::Entity Editor::DuplicateSelectedEntity() {
    if (!IsLifecycleCommandAllowed() || !IsDocumentEntity(m_selectedEntity) ||
        IsStructuralRoot(m_selectedEntity)) {
        return Scene::kInvalidEntity;
    }

    const Scene::Entity source = m_selectedEntity;
    const std::optional<Scene::SceneSnapshot> snapshot = CaptureSubtree(source);
    if (!snapshot.has_value()) {
        return Scene::kInvalidEntity;
    }

    Scene::Entity originalParent = Scene::kInvalidEntity;
    if (!TryGetDocumentParent(source, originalParent)) {
        return Scene::kInvalidEntity;
    }

    const Scene::Entity duplicate = RestoreSubtreeUnderParent(*snapshot, originalParent);
    if (duplicate == Scene::kInvalidEntity) {
        return Scene::kInvalidEntity;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    // The copy goes right below the object it copies, not to the end of the list.
    Scene::ISceneGraph& sceneGraph = m_services->GetSceneGraph();
    const std::size_t duplicateIndex = sceneGraph.GetSiblingIndex(entityManager, source).value_or(0U) + 1U;
    static_cast<void>(sceneGraph.SetSiblingIndex(entityManager, duplicate, duplicateIndex));
    std::optional<std::string> duplicateRootName;
    if (entityManager.HasComponent<Scene::NameComponent>(source)) {
        const std::string& sourceName = entityManager.GetComponent<Scene::NameComponent>(source).name;
        if (IsEntityNameValid(sourceName)) {
            duplicateRootName = MakeUniqueDocumentEntityName(sourceName);
            if (!ApplyEntityNameState(duplicate, duplicateRootName)) {
                DestroyDocumentSubtree(duplicate);
                return Scene::kInvalidEntity;
            }
        }
    }

    const EditorSelectionSnapshot selectionBefore = CaptureSelectionSnapshot();
    const bool dirtyBefore = m_sceneDirty;
    SelectEntity(duplicate);
    m_sceneDirty = true;
    InvalidateHierarchyFilterCache();
    RecordHistory(DuplicationHistoryEntry{
        std::move(*snapshot), originalParent, duplicate, std::move(duplicateRootName), selectionBefore,
        CaptureSelectionSnapshot(), dirtyBefore, true, duplicateIndex});
    return duplicate;
}

bool Editor::DeleteSelectedEntity() {
    if (!IsLifecycleCommandAllowed() || !IsDocumentEntity(m_selectedEntity) ||
        IsStructuralRoot(m_selectedEntity)) {
        return false;
    }

    const EditorSelectionSnapshot selectionBefore = CaptureSelectionSnapshot();
    const bool dirtyBefore = m_sceneDirty;
    std::optional<DeletionHistoryEntry> deletion = DestroyForDeletion(m_selectedEntity);
    if (!deletion.has_value()) {
        return false;
    }
    const Scene::Entity selectionAfter = IsDocumentEntity(deletion->originalParent)
                                                ? deletion->originalParent
                                                : Scene::kInvalidEntity;
    RestoreSelection(EditorSelectionSnapshot{
        selectionAfter == Scene::kInvalidEntity ? std::vector<Scene::Entity>{}
                                                    : std::vector<Scene::Entity>{selectionAfter},
        selectionAfter});
    m_sceneDirty = true;
    InvalidateHierarchyFilterCache();
    deletion->selectionBefore = selectionBefore;
    deletion->selectionAfter = CaptureSelectionSnapshot();
    deletion->dirtyBefore = dirtyBefore;
    deletion->dirtyAfter = true;
    RecordHistory(std::move(*deletion));
    return true;
}

std::optional<Editor::DeletionHistoryEntry> Editor::DestroyForDeletion(const Scene::Entity target) {
    std::optional<Scene::SceneSnapshot> snapshot = CaptureSubtree(target);
    if (!snapshot.has_value()) {
        return std::nullopt;
    }

    Scene::Entity originalParent = Scene::kInvalidEntity;
    if (!TryGetDocumentParent(target, originalParent)) {
        return std::nullopt;
    }

    const std::size_t siblingIndex =
        m_services->GetSceneGraph().GetSiblingIndex(m_services->GetEntityManager(), target).value_or(0U);
    if (m_hierarchyRenameEntity == target) {
        CancelHierarchyRename();
    }
    ClearEntityContextToolbar();
    m_hierarchyPendingRowOpen.erase(target);
    DestroyDocumentSubtree(target);

    DeletionHistoryEntry deletion{};
    deletion.snapshot = std::move(*snapshot);
    deletion.originalParent = originalParent;
    deletion.activeEntity = target;
    deletion.siblingIndex = siblingIndex;
    return deletion;
}

std::vector<Scene::Entity> Editor::CollectDeletableSelection() {
    std::unordered_set<Scene::Entity> deletable;
    for (const Scene::Entity entity : m_selectedEntities) {
        if (!IsStructuralRoot(entity) && ObjectActionBlockReason(entity).empty()) {
            deletable.insert(entity);
        }
    }
    std::vector<Scene::Entity> roots;
    for (const Scene::Entity entity : m_selectedEntities) {
        if (!deletable.contains(entity) || std::find(roots.begin(), roots.end(), entity) != roots.end()) {
            continue;
        }
        // The ancestry ends with the entity itself; a deleted ancestor takes it along.
        const std::vector<Scene::Entity> ancestry = GetDocumentAncestry(entity);
        const bool underDeleted = !ancestry.empty() &&
                                  std::any_of(ancestry.begin(), ancestry.end() - 1,
                                              [&deletable](const Scene::Entity ancestor) {
                                                  return deletable.contains(ancestor);
                                              });
        if (!underDeleted) {
            roots.push_back(entity);
        }
    }
    return roots;
}

bool Editor::DeleteSelectedEntities() {
    const std::vector<Scene::Entity> roots = CollectDeletableSelection();
    if (roots.empty()) {
        return false;
    }
    if (roots.size() == 1U) {
        const EditorSelectionSnapshot selection = CaptureSelectionSnapshot();
        RestoreSelection(EditorSelectionSnapshot{{roots.front()}, roots.front()});
        if (DeleteSelectedEntity()) {
            return true;
        }
        RestoreSelection(selection);
        return false;
    }

    MultiDeletionHistoryEntry entry{};
    entry.selectionBefore = CaptureSelectionSnapshot();
    entry.dirtyBefore = m_sceneDirty;
    for (const Scene::Entity root : roots) {
        std::optional<DeletionHistoryEntry> deletion = DestroyForDeletion(root);
        if (deletion.has_value()) {
            entry.deletions.push_back(std::move(*deletion));
        }
    }
    if (entry.deletions.empty()) {
        return false;
    }

    // As a single Delete does: the shared parent stays selected when every root had the same one.
    const Scene::Entity parent = entry.deletions.front().originalParent;
    const bool sharedParent = IsDocumentEntity(parent) &&
                              std::all_of(entry.deletions.begin(), entry.deletions.end(),
                                          [parent](const DeletionHistoryEntry& deletion) {
                                              return deletion.originalParent == parent;
                                          });
    RestoreSelection(EditorSelectionSnapshot{
        sharedParent ? std::vector<Scene::Entity>{parent} : std::vector<Scene::Entity>{},
        sharedParent ? parent : Scene::kInvalidEntity});
    m_sceneDirty = true;
    InvalidateHierarchyFilterCache();
    entry.selectionAfter = CaptureSelectionSnapshot();
    entry.dirtyAfter = true;
    RecordHistory(std::move(entry));
    return true;
}

std::string Editor::ObjectActionBlockReason(const Scene::Entity entity) {
    if (!IsAuthoringCommandAllowed()) {
        return "Stop Play first: the scene is edited only while it is not running.";
    }
    if (!IsDocumentEntity(entity)) {
        return "This object is gone.";
    }
    if (IsSceneRootEntity(entity)) {
        return "The scene root holds the whole scene.";
    }
    if (entity == GetDocumentViewport()) {
        return "The Viewport is the level itself: every object lives in it.";
    }
    if (m_entityDocumentActive && entity == GetEntityEditorRoot()) {
        return "This is the entity itself: it is the top of its own tree.";
    }
    if (ResolveHierarchyRowOwner(entity) != entity) {
        return "It is inside a placed entity: open the entity to change what is inside it.";
    }
    if (!IsDocumentSubtree(entity)) {
        return "Something under it is not part of this document.";
    }
    return {};
}

bool Editor::CopySelectedObject() {
    if (!IsLifecycleCommandAllowed() || !ObjectActionBlockReason(m_selectedEntity).empty()) {
        return false;
    }
    std::optional<Scene::SceneSnapshot> snapshot = CaptureSubtree(m_selectedEntity);
    if (!snapshot.has_value()) {
        return false;
    }
    m_objectClipboard = std::move(*snapshot);
    return true;
}

bool Editor::CutSelectedObject() {
    return CopySelectedObject() && DeleteSelectedEntity();
}

bool Editor::CanPasteObject() const noexcept {
    return m_objectClipboard.has_value() && IsAuthoringCommandAllowed();
}

Scene::Entity Editor::PasteObject(const bool asChild) {
    if (!CanPasteObject()) {
        return Scene::kInvalidEntity;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    Scene::ISceneGraph& sceneGraph = m_services->GetSceneGraph();
    const Scene::Entity anchor = HasSingleDocumentSelection() && IsDocumentEntity(m_selectedEntity)
                                        ? m_selectedEntity
                                        : Scene::kInvalidEntity;
    Scene::Entity parent = Scene::kInvalidEntity;
    std::optional<std::size_t> index;
    if (anchor == Scene::kInvalidEntity) {
        parent = m_entityDocumentActive ? GetEntityEditorRoot() : ResolveNewObjectParent();
    } else if (asChild || IsStructuralRoot(anchor)) {
        // Into the row itself; a placed entity takes nothing in from the level.
        if (IsSealedHierarchyRow(anchor) || ResolveHierarchyRowOwner(anchor) != anchor) {
            return Scene::kInvalidEntity;
        }
        parent = anchor;
    } else {
        if (ResolveHierarchyRowOwner(anchor) != anchor || !TryGetDocumentParent(anchor, parent)) {
            return Scene::kInvalidEntity;
        }
        index = sceneGraph.GetSiblingIndex(entityManager, anchor).value_or(0U) + 1U;
    }
    if (parent != Scene::kInvalidEntity && !IsDocumentEntity(parent)) {
        return Scene::kInvalidEntity;
    }
    const Scene::Entity pasted = RestoreSubtreeUnderParent(*m_objectClipboard, parent);
    if (pasted == Scene::kInvalidEntity) {
        return Scene::kInvalidEntity;
    }
    const std::size_t siblingIndex = index.has_value() ? *index : sceneGraph.GetSiblingIndex(entityManager, pasted).value_or(0U);
    if (index.has_value()) {
        static_cast<void>(sceneGraph.SetSiblingIndex(entityManager, pasted, siblingIndex));
    }
    std::optional<std::string> pastedName;
    if (entityManager.HasComponent<Scene::NameComponent>(pasted)) {
        const std::string sourceName = entityManager.GetComponent<Scene::NameComponent>(pasted).name;
        if (IsEntityNameValid(sourceName)) {
            // The copy itself already holds the name; a fresh one is free of both.
            static_cast<void>(ApplyEntityNameState(pasted, std::nullopt));
            pastedName = MakeUniqueDocumentEntityName(sourceName);
            if (!ApplyEntityNameState(pasted, pastedName)) {
                DestroyDocumentSubtree(pasted);
                return Scene::kInvalidEntity;
            }
        }
    }
    const EditorSelectionSnapshot selectionBefore = CaptureSelectionSnapshot();
    const bool dirtyBefore = m_sceneDirty;
    SelectEntity(pasted);
    m_sceneDirty = true;
    InvalidateHierarchyFilterCache();
    RecordHistory(DuplicationHistoryEntry{*m_objectClipboard, parent, pasted, std::move(pastedName), selectionBefore,
                                                CaptureSelectionSnapshot(), dirtyBefore, true, siblingIndex});
    return pasted;
}

bool Editor::ReparentSelectedEntity(const Scene::Entity newParent) {
    return ReparentDocumentEntity(m_selectedEntity, newParent);
}

bool Editor::ComputeKeepWorldLocalTransform(const Scene::Entity entity,
                                                    const Scene::Entity newParent,
                                                    Scene::TransformComponent& outTransform) const {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!IsDocumentEntity(entity) || !entityManager.HasComponent<Scene::WorldTransformComponent>(entity)) {
        return false;
    }
    const Scene::WorldTransformComponent& sourceWorld =
        entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
    Math::Quaternion sourceRotation{};
    if (sourceWorld.dirty || !IsFiniteVector(sourceWorld.worldPosition) ||
        !IsFiniteVector(sourceWorld.worldScale) || !Math::TryNormalize(sourceWorld.worldRotation, sourceRotation)) {
        return false;
    }

    // No parent and a pure-Object parent both compose as identity, which falls straight through.
    const std::optional<Scene::WorldTransformComponent> parentWorld = TryGetComposingParentWorld(newParent);
    Math::Quaternion parentRotation{};
    if (!parentWorld.has_value() || parentWorld->dirty || !IsFiniteVector(parentWorld->worldPosition) ||
        !IsFiniteVector(parentWorld->worldScale) ||
        !Math::TryNormalize(parentWorld->worldRotation, parentRotation) ||
        parentWorld->worldScale.x < kMinimumLocalScale || parentWorld->worldScale.y < kMinimumLocalScale ||
        parentWorld->worldScale.z < kMinimumLocalScale) {
        return false;
    }
    const bool nonUniform = std::abs(parentWorld->worldScale.x - parentWorld->worldScale.y) > kVectorEpsilon ||
                            std::abs(parentWorld->worldScale.x - parentWorld->worldScale.z) > kVectorEpsilon ||
                            std::abs(parentWorld->worldScale.y - parentWorld->worldScale.z) > kVectorEpsilon;
    const bool rotated = std::abs(parentRotation.x) > kVectorEpsilon ||
                         std::abs(parentRotation.y) > kVectorEpsilon ||
                         std::abs(parentRotation.z) > kVectorEpsilon ||
                         std::abs(std::abs(parentRotation.w) - 1.0F) > kVectorEpsilon;
    if (nonUniform && rotated) {
        return false;
    }
    const Math::Vector3 parentPosition = parentWorld->worldPosition;
    const Math::Vector3 parentScale = parentWorld->worldScale;

    Math::Quaternion parentInverse{};
    if (!Math::TryInverse(parentRotation, parentInverse)) {
        return false;
    }
    const Math::Trs parentTrs{parentPosition, parentRotation, parentScale};
    if (!Math::TryInverseTransformPoint(parentTrs, sourceWorld.worldPosition,
                                            outTransform.localPosition)) {
        return false;
    }
    if (!Math::TryNormalize(Math::Multiply(parentInverse, sourceRotation), outTransform.localRotation)) {
        return false;
    }
    outTransform.localScale = Math::Vector3{
        sourceWorld.worldScale.x / parentScale.x, sourceWorld.worldScale.y / parentScale.y,
        sourceWorld.worldScale.z / parentScale.z};
    return IsTransformFinite(outTransform) && outTransform.localScale.x >= kMinimumLocalScale &&
           outTransform.localScale.y >= kMinimumLocalScale && outTransform.localScale.z >= kMinimumLocalScale;
}

bool Editor::ReparentDocumentEntity(const Scene::Entity entity, const Scene::Entity newParent) {
    if (!IsLifecycleCommandAllowed() || IsStructuralRoot(entity) || !IsReparentableObject(entity) ||
        !IsDocumentSubtree(entity) ||
        (newParent != Scene::kInvalidEntity && !IsHierarchyObject(newParent)) ||
        entity == newParent || DoesSubtreeContainEntity(entity, newParent)) {
        return false;
    }
    // One-root documents: "move to document root" means becoming a direct child of the
    // scene root - nothing but the root itself may sit at top level.
    // In the Entity Editor the entity's root plays that part.
    // In the level, "the top" means the object's folder, or the Viewport3D for a folder.
    const auto defaultParent = [this, entity] {
        Scene::IEntityManager& manager = m_services->GetEntityManager();
        if (GetDocumentViewport() == Scene::kInvalidEntity) {
            return EnsureDocumentSceneRoot();
        }
        if (IsTopLevelSingletonKind(Scene::ResolveSceneObjectKind(manager, entity))) {
            return EnsureDocumentSceneRoot();
        }
        return manager.HasComponent<Scene::FolderComponent>(entity) ? GetDocumentViewport()
                                                                          : ResolveObjectFolder();
    };
    const Scene::Entity effectiveParent =
        newParent != Scene::kInvalidEntity ? newParent
        : m_entityDocumentActive     ? GetEntityEditorRoot()
                                              : defaultParent();
    if (effectiveParent == Scene::kInvalidEntity || entity == effectiveParent ||
        DoesSubtreeContainEntity(entity, effectiveParent) || !IsAllowedOutlinerParent(entity, effectiveParent)) {
        return false;
    }
    Scene::Entity parentBefore = Scene::kInvalidEntity;
    if (!TryGetDocumentParent(entity, parentBefore) || parentBefore == effectiveParent) {
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    // A pure Object has no transform: it moves in the hierarchy and nothing else changes.
    const bool spatial = entityManager.HasComponent<Scene::TransformComponent>(entity);
    const Scene::TransformComponent localBefore =
        spatial ? entityManager.GetComponent<Scene::TransformComponent>(entity) : Scene::TransformComponent{};
    Scene::TransformComponent localAfter = localBefore;
    if (spatial && m_reparentTransformMode == EditorReparentTransformMode::KeepWorld &&
        !ComputeKeepWorldLocalTransform(entity, effectiveParent, localAfter)) {
        return false;
    }
    const EditorSelectionSnapshot selectionBefore = CaptureSelectionSnapshot();
    const bool dirtyBefore = m_sceneDirty;
    const std::size_t siblingIndexBefore =
        m_services->GetSceneGraph().GetSiblingIndex(entityManager, entity).value_or(0U);
    m_services->GetSceneGraph().SetParent(entityManager, entity, effectiveParent);
    if (spatial && !ApplyLocalTransform(entity, localAfter)) {
        m_services->GetSceneGraph().SetParent(entityManager, entity, parentBefore);
        static_cast<void>(ApplyLocalTransform(entity, localBefore));
        return false;
    }
    RestoreSelection(EditorSelectionSnapshot{{entity}, entity});
    m_sceneDirty = true;
    InvalidateHierarchyFilterCache();
    // The parent the entity actually got. Recording `newParent` would make redo of "move to
    // document root" set no parent at all, leaving a stray beside the scene root instead of under it.
    RecordHistory(ReparentHistoryEntry{
        entity, parentBefore, effectiveParent, localBefore, localAfter, selectionBefore,
        CaptureSelectionSnapshot(), dirtyBefore, true, siblingIndexBefore,
        m_services->GetSceneGraph().GetSiblingIndex(entityManager, entity).value_or(0U)});
    return true;
}

bool Editor::CanMoveDocumentEntity(const Scene::Entity entity, const EditorSiblingMove move) {
    if (!IsLifecycleCommandAllowed() || !IsDocumentEntity(entity) || IsStructuralRoot(entity)) {
        return false;
    }
    Scene::Entity parent = Scene::kInvalidEntity;
    if (!TryGetDocumentParent(entity, parent)) {
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const std::optional<std::size_t> index = m_services->GetSceneGraph().GetSiblingIndex(entityManager, entity);
    if (!index.has_value()) {
        return false;
    }
    const std::size_t last = m_services->GetSceneGraph().GetChildren(entityManager, parent).size() - 1U;
    return move == EditorSiblingMove::Up || move == EditorSiblingMove::ToTop ? *index > 0U : *index < last;
}

bool Editor::AddSkyToEntity(const Scene::Entity entity) {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!IsDocumentEntity(entity) || !IsAuthoringCommandAllowed()) {
        return false;
    }
    if (!entityManager.HasComponent<Scene::Sky3DComponent>(entity)) {
        entityManager.AddComponent<Scene::Sky3DComponent>(entity, Scene::Sky3DComponent{});
        m_sceneDirty = true;
    }
    return true;
}

bool Editor::RemoveSkyFromEntity(const Scene::Entity entity) {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!IsDocumentEntity(entity) || !IsAuthoringCommandAllowed() || !entityManager.HasComponent<Scene::Sky3DComponent>(entity)) {
        return false;
    }
    entityManager.RemoveComponent<Scene::Sky3DComponent>(entity);
    m_sceneDirty = true;
    return true;
}

bool Editor::PlaceDocumentEntity(const Scene::Entity entity, const Scene::Entity target, const int where) {
    if (entity == target || !IsDocumentEntity(entity) || !IsDocumentEntity(target) || !IsAuthoringCommandAllowed()) {
        return false;
    }
    if (where == 1) {
        return ReparentDocumentEntity(entity, target);
    }
    Scene::Entity parent = Scene::kInvalidEntity;
    if (!TryGetDocumentParent(target, parent)) {
        return false;
    }
    Scene::Entity current = Scene::kInvalidEntity;
    static_cast<void>(TryGetDocumentParent(entity, current));
    if (current != parent && !ReparentDocumentEntity(entity, parent)) {
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    Scene::ISceneGraph& sceneGraph = m_services->GetSceneGraph();
    const std::optional<std::size_t> from = sceneGraph.GetSiblingIndex(entityManager, entity);
    const std::optional<std::size_t> at = sceneGraph.GetSiblingIndex(entityManager, target);
    if (!from.has_value() || !at.has_value()) {
        return false;
    }
    // The index the entity ends at, counted with it already out of the list.
    std::size_t to = *at + (where == 2 ? 1U : 0U);
    if (*from < to) {
        --to;
    }
    if (to == *from) {
        return true;
    }
    const EditorSelectionSnapshot selectionBefore = CaptureSelectionSnapshot();
    const bool dirtyBefore = m_sceneDirty;
    if (!sceneGraph.SetSiblingIndex(entityManager, entity, to)) {
        return false;
    }
    m_sceneDirty = true;
    InvalidateHierarchyFilterCache();
    const Scene::TransformComponent local =
        entityManager.HasComponent<Scene::TransformComponent>(entity)
            ? entityManager.GetComponent<Scene::TransformComponent>(entity)
            : Scene::TransformComponent{};
    RecordHistory(ReparentHistoryEntry{entity, parent, parent, local, local, selectionBefore,
                                             CaptureSelectionSnapshot(), dirtyBefore, true, *from, to});
    return true;
}

bool Editor::MoveDocumentEntity(const Scene::Entity entity, const EditorSiblingMove move) {
    if (!CanMoveDocumentEntity(entity, move)) {
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    Scene::ISceneGraph& sceneGraph = m_services->GetSceneGraph();
    Scene::Entity parent = Scene::kInvalidEntity;
    static_cast<void>(TryGetDocumentParent(entity, parent));
    const std::size_t before = sceneGraph.GetSiblingIndex(entityManager, entity).value_or(0U);
    const std::size_t last = sceneGraph.GetChildren(entityManager, parent).size() - 1U;
    std::size_t after = before;
    switch (move) {
        case EditorSiblingMove::Up:
            after = before - 1U;
            break;
        case EditorSiblingMove::Down:
            after = before + 1U;
            break;
        case EditorSiblingMove::ToTop:
            after = 0U;
            break;
        case EditorSiblingMove::ToBottom:
            after = last;
            break;
    }
    const EditorSelectionSnapshot selectionBefore = CaptureSelectionSnapshot();
    const bool dirtyBefore = m_sceneDirty;
    if (!sceneGraph.SetSiblingIndex(entityManager, entity, after)) {
        return false;
    }
    RestoreSelection(EditorSelectionSnapshot{{entity}, entity});
    m_sceneDirty = true;
    InvalidateHierarchyFilterCache();
    // Recorded as a move to the same parent, so undo and redo share the reparent path.
    const Scene::TransformComponent local =
        entityManager.HasComponent<Scene::TransformComponent>(entity)
            ? entityManager.GetComponent<Scene::TransformComponent>(entity)
            : Scene::TransformComponent{};
    RecordHistory(ReparentHistoryEntry{entity, parent, parent, local, local, selectionBefore,
                                             CaptureSelectionSnapshot(), dirtyBefore, true, before, after});
    return true;
}

bool Editor::Undo() {
    // An edit still in flight is finished first, so undo takes back that edit rather than the
    // one before it underneath a live value.
    static_cast<void>(CommitComponentPropertyPreview());
    if (!IsAuthoringCommandAllowed() || m_undoHistory.empty()) {
        return false;
    }

    HistoryEntry entry = std::move(m_undoHistory.back());
    m_undoHistory.pop_back();
    if (!UndoHistoryEntry(entry)) {
        ClearHistory();
        return false;
    }

    m_redoHistory.push_back(std::move(entry));
    return true;
}

bool Editor::Redo() {
    static_cast<void>(CommitComponentPropertyPreview());
    if (!IsAuthoringCommandAllowed() || m_redoHistory.empty()) {
        return false;
    }

    HistoryEntry entry = std::move(m_redoHistory.back());
    m_redoHistory.pop_back();
    if (!RedoHistoryEntry(entry)) {
        ClearHistory();
        return false;
    }

    m_undoHistory.push_back(std::move(entry));
    return true;
}

bool Editor::CanUndo() const noexcept {
    return IsAuthoringCommandAllowed() && !m_undoHistory.empty();
}

bool Editor::CanRedo() const noexcept {
    return IsAuthoringCommandAllowed() && !m_redoHistory.empty();
}

bool Editor::ApplyLocalTransform(const Scene::Entity entity,
                                       const Scene::TransformComponent& transform) {
    if (!IsDocumentEntity(entity) || !IsTransformFinite(transform)) {
        return false;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent<Scene::TransformComponent>(entity)) {
        return false;
    }

    m_services->GetSceneGraph().SetLocalTransform(entityManager, entity, transform);
    return true;
}

bool Editor::ApplyPrimitiveMeshState(const Scene::Entity entity,
                                            const Scene::PrimitiveMeshComponent& primitive) {
    if (!IsDocumentEntity(entity) || !Scene::IsPrimitiveMeshComponentValid(primitive)) {
        return false;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent<Scene::PrimitiveMeshComponent>(entity)) {
        return false;
    }
    entityManager.GetComponent<Scene::PrimitiveMeshComponent>(entity) = primitive;
    if (entityManager.HasComponent<Scene::ColliderComponent>(entity)) {
        FitColliderToPrimitiveKind(entityManager.GetComponent<Scene::ColliderComponent>(entity),
                                      primitive.kind);
    }
    return true;
}

bool Editor::ApplyEntityNameState(const Scene::Entity entity,
                                          const std::optional<std::string>& name) {

    if (!IsDocumentEntity(entity) || (name.has_value() && !IsEntityNameValid(*name))) {
        return false;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const bool hasName = entityManager.HasComponent<Scene::NameComponent>(entity);
    if (!name.has_value()) {
        if (!hasName) {
            return false;
        }
        entityManager.RemoveComponent<Scene::NameComponent>(entity);
        InvalidateHierarchyFilterCache();
        return true;
    }

    if (hasName) {
        entityManager.GetComponent<Scene::NameComponent>(entity).name = *name;
    } else {
        entityManager.AddComponent<Scene::NameComponent>(entity, Scene::NameComponent{*name});
    }
    InvalidateHierarchyFilterCache();
    return true;
}

bool Editor::IsDocumentSubtree(const Scene::Entity root) const {
    if (!IsDocumentEntity(root)) {
        return false;
    }

    // A document subtree must never absorb the editor-owned viewport camera, even if a caller
    // externally attempts an invalid reparent. Detect malformed cycles before any traversal caller
    // can act on the subtree.
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    std::vector<Scene::Entity> pending{root};
    std::vector<Scene::Entity> visited;
    while (!pending.empty()) {
        const Scene::Entity current = pending.back();
        pending.pop_back();
        if (!IsDocumentEntity(current) ||
            std::find(visited.begin(), visited.end(), current) != visited.end()) {
            return false;
        }
        visited.push_back(current);
        const std::vector<Scene::Entity> children =
            m_services->GetSceneGraph().GetChildren(entityManager, current);
        pending.insert(pending.end(), children.begin(), children.end());
    }
    return true;
}

bool Editor::DoesSubtreeContainEntity(const Scene::Entity root,
                                            const Scene::Entity candidate) const {
    if (candidate == Scene::kInvalidEntity || !IsDocumentSubtree(root)) {
        return false;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    std::vector<Scene::Entity> pending{root};
    while (!pending.empty()) {
        const Scene::Entity current = pending.back();
        pending.pop_back();
        if (current == candidate) {
            return true;
        }
        const std::vector<Scene::Entity> children =
            m_services->GetSceneGraph().GetChildren(entityManager, current);
        pending.insert(pending.end(), children.begin(), children.end());
    }
    return false;
}

std::optional<Scene::SceneSnapshot> Editor::CaptureSubtree(const Scene::Entity root) {
    if (!IsDocumentSubtree(root)) {
        return std::nullopt;
    }

    return m_services->GetSceneSerializer().Capture(
        m_services->GetEntityManager(), {root}, Asset::AssetKind::Scene);
}

Scene::Entity Editor::RestoreSubtreeUnderParent(const Scene::SceneSnapshot& snapshot,
                                                         const Scene::Entity parent) {
    if (parent != Scene::kInvalidEntity && !IsDocumentEntity(parent)) {
        return Scene::kInvalidEntity;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    std::vector<Scene::Entity> restoredRoots = m_services->GetSceneSerializer().Restore(entityManager, snapshot);
    if (restoredRoots.size() != 1U || !IsDocumentEntity(restoredRoots.front())) {
        for (const Scene::Entity restoredRoot : restoredRoots) {
            if (IsDocumentEntity(restoredRoot)) {
                DestroyDocumentSubtree(restoredRoot);
            }
        }
        return Scene::kInvalidEntity;
    }

    const Scene::Entity restoredRoot = restoredRoots.front();
    if (parent != Scene::kInvalidEntity) {
        if (!entityManager.HasComponent<Scene::HierarchyComponent>(restoredRoot)) {
            DestroyDocumentSubtree(restoredRoot);
            return Scene::kInvalidEntity;
        }
        m_services->GetSceneGraph().SetParent(entityManager, restoredRoot, parent);
    }
    return restoredRoot;
}

bool Editor::TryGetDocumentParent(const Scene::Entity entity, Scene::Entity& outParent) const {
    outParent = Scene::kInvalidEntity;
    if (!IsDocumentEntity(entity)) {
        return false;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent<Scene::HierarchyComponent>(entity)) {
        return true;
    }
    const Scene::Entity parent = entityManager.GetComponent<Scene::HierarchyComponent>(entity).parent;
    if (parent == Scene::kInvalidEntity) {
        return true;
    }
    if (!IsDocumentEntity(parent)) {
        return false;
    }
    outParent = parent;
    return true;
}

std::string_view Editor::GetObjectTypeName(const Scene::Entity entity) const {
    if (!IsDocumentEntity(entity)) {
        return {};
    }
    const Scene::Objects::SceneObjectKind kind = Scene::ResolveSceneObjectKind(m_services->GetEntityManager(), entity);
    // A UI object reads as the widget it is (a Combo Box, a 3D View ...), not as the Image it was made from.
    if (kind == Scene::Objects::SceneObjectKind::UIImage || kind == Scene::Objects::SceneObjectKind::UIText ||
        kind == Scene::Objects::SceneObjectKind::UIButton) {
        if (const std::optional<WidgetKind> widget = ClassifyWidget(m_services->GetEntityManager(), entity)) {
            return WidgetKindLabel(*widget);
        }
    }
    const Scene::Objects::SceneObjectDescriptor* const descriptor = Scene::Objects::FindSceneObjectDescriptor(kind);
    return descriptor != nullptr ? descriptor->displayName : std::string_view{};
}

std::string Editor::GetOutlinerTypeTag(const Scene::Entity entity) const {
    if (!IsDocumentEntity(entity)) {
        return {};
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (entityManager.HasComponent<Scene::PrimitiveMeshComponent>(entity)) {
        switch (entityManager.GetComponent<Scene::PrimitiveMeshComponent>(entity).kind) {
            case Scene::PrimitiveMeshKind::Plane:
                return std::string{Scene::PlaneMesh3DObjectDefinition::defaultName};
            case Scene::PrimitiveMeshKind::UVSphere:
                return std::string{Scene::SphereMesh3DObjectDefinition::defaultName};
            case Scene::PrimitiveMeshKind::Cube:
                return std::string{Scene::BoxMesh3DObjectDefinition::defaultName};
            case Scene::PrimitiveMeshKind::Capsule:
                return "CapsuleMesh";
            case Scene::PrimitiveMeshKind::Cylinder:
                return "CylinderMesh";
        }
    }
    if (entityManager.HasComponent<Scene::CameraComponent>(entity)) {
        return std::string{Scene::Camera3DObjectDefinition::defaultName};
    }
    if (entityManager.HasComponent<Scene::LightComponent>(entity) &&
        entityManager.GetComponent<Scene::LightComponent>(entity).type == Scene::LightType::Directional) {
        return std::string{Scene::Light3DObjectDefinition::defaultName};
    }
    if (entityManager.HasComponent<Scene::ColliderComponent>(entity)) {
        return std::string{Scene::Collider3DObjectDefinition::defaultName};
    }
    return {};
}

std::vector<Scene::Entity> Editor::GetDocumentAncestry(const Scene::Entity entity) const {
    if (!IsDocumentEntity(entity)) {
        return {};
    }

    std::vector<Scene::Entity> ancestry;
    Scene::Entity current = entity;
    while (current != Scene::kInvalidEntity) {
        if (!IsDocumentEntity(current) ||
            std::find(ancestry.begin(), ancestry.end(), current) != ancestry.end()) {
            return {};
        }
        ancestry.push_back(current);

        Scene::Entity parent = Scene::kInvalidEntity;
        if (!TryGetDocumentParent(current, parent)) {
            return {};
        }
        current = parent;
    }

    std::reverse(ancestry.begin(), ancestry.end());
    return ancestry;
}

std::vector<Scene::Entity> Editor::GetEligibleReparentParents(const Scene::Entity entity) {
    if (!IsDocumentSubtree(entity)) {
        return {};
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    std::vector<Scene::Entity> excludedSubtree;
    std::vector<Scene::Entity> pending{entity};
    while (!pending.empty()) {
        const Scene::Entity current = pending.back();
        pending.pop_back();
        if (!IsDocumentEntity(current) ||
            std::find(excludedSubtree.begin(), excludedSubtree.end(), current) != excludedSubtree.end()) {
            return {};
        }
        excludedSubtree.push_back(current);
        const std::vector<Scene::Entity> children =
            m_services->GetSceneGraph().GetChildren(entityManager, current);
        pending.insert(pending.end(), children.begin(), children.end());
    }

    std::vector<Scene::Entity> candidates;
    std::vector<Scene::Entity> visited;
    const auto visit = [this, &entityManager, &excludedSubtree, &candidates, &visited](
                           const auto& self, const Scene::Entity current) -> void {
        if (!IsDocumentEntity(current) || std::find(visited.begin(), visited.end(), current) != visited.end()) {
            return;
        }
        visited.push_back(current);
        if (std::find(excludedSubtree.begin(), excludedSubtree.end(), current) == excludedSubtree.end()) {
            candidates.push_back(current);
        }
        // Never descend into a placed entity: its contents are not rows of this tree, so they
        // cannot be offered as places to move something to.
        if (IsSealedHierarchyRow(current)) {
            return;
        }
        for (const Scene::Entity child : m_services->GetSceneGraph().GetChildren(entityManager, current)) {
            self(self, child);
        }
    };

    for (const Scene::Entity root : GetDocumentRoots()) {
        visit(visit, root);
    }
    std::erase_if(candidates, [this, entity](const Scene::Entity parent) {
        return !IsAllowedOutlinerParent(entity, parent);
    });
    return candidates;
}

std::string Editor::GetHierarchyCandidateLabel(const Scene::Entity entity) const {
    return GetEntityDisplayLabel(entity);
}

bool Editor::IsLifecycleCommandAllowed() const noexcept {
    return IsAuthoringCommandAllowed() && HasSingleDocumentSelection();
}

bool Editor::IsAuthoringCommandAllowed() const noexcept {
    // The Retarget preview has a world of its own: the level stays authorable beside it.
    return m_state == EditorState::Running && m_playModeState == EditorPlayModeState::Edit && !m_retargetSceneActive;
}

Editor::EditorSelectionSnapshot Editor::CaptureSelectionSnapshot() const {
    EditorSelectionSnapshot selection{};
    for (const Scene::Entity entity : m_selectedEntities) {
        if (IsDocumentEntity(entity) &&
            std::find(selection.entities.begin(), selection.entities.end(), entity) == selection.entities.end()) {
            selection.entities.push_back(entity);
        }
    }
    if (IsDocumentEntity(m_selectedEntity) &&
        std::find(selection.entities.begin(), selection.entities.end(), m_selectedEntity) != selection.entities.end()) {
        selection.activeEntity = m_selectedEntity;
    } else if (!selection.entities.empty()) {
        selection.activeEntity = selection.entities.back();
    }
    return selection;
}

void Editor::RestoreSelection(EditorSelectionSnapshot selection) noexcept {
    std::vector<Scene::Entity> restored;
    restored.reserve(selection.entities.size());
    for (const Scene::Entity entity : selection.entities) {
        if (IsDocumentEntity(entity) && std::find(restored.begin(), restored.end(), entity) == restored.end()) {
            restored.push_back(entity);
        }
    }

    const bool activeValid = IsDocumentEntity(selection.activeEntity) &&
                             std::find(restored.begin(), restored.end(), selection.activeEntity) != restored.end();
    const Scene::Entity restoredActive = activeValid
                                                ? selection.activeEntity
                                                : (restored.empty() ? Scene::kInvalidEntity : restored.back());
    const bool changed = restored != m_selectedEntities || restoredActive != m_selectedEntity;
    m_selectedEntities = std::move(restored);
    m_selectedEntity = restoredActive;
    if (changed) {
        CancelHierarchyRename();
    }
}

void Editor::PruneSelection() noexcept {
    RestoreSelection(CaptureSelectionSnapshot());
}

bool Editor::IsEntitySelected(const Scene::Entity entity) const noexcept {
    return std::find(m_selectedEntities.begin(), m_selectedEntities.end(), entity) != m_selectedEntities.end();
}

Editor::EditorSelectionPaths Editor::CaptureSelectionPaths(
    const std::vector<Scene::Entity>& roots) const {
    EditorSelectionPaths paths{};
    const EditorSelectionSnapshot selection = CaptureSelectionSnapshot();
    const auto capturePath = [this, &roots](const Scene::Entity entity) -> std::optional<EditorSelectionPath> {
        for (std::size_t rootIndex = 0U; rootIndex < roots.size(); ++rootIndex) {
            EditorSelectionPath path{};
            path.rootIndex = rootIndex;
            if (FindSelectionPath(roots[rootIndex], entity, path.childIndices)) {
                return path;
            }
        }
        return std::nullopt;
    };

    for (const Scene::Entity entity : selection.entities) {
        if (const std::optional<EditorSelectionPath> path = capturePath(entity); path.has_value()) {
            paths.entityPaths.push_back(*path);
        }
    }
    paths.activePath = capturePath(selection.activeEntity);
    return paths;
}

Editor::EditorSelectionSnapshot Editor::ResolveSelectionPaths(
    const EditorSelectionPaths& paths, const std::vector<Scene::Entity>& roots) const {
    EditorSelectionSnapshot selection{};
    for (const EditorSelectionPath& path : paths.entityPaths) {
        const Scene::Entity entity = ResolveSelectionPath(path, roots);
        if (IsDocumentEntity(entity) &&
            std::find(selection.entities.begin(), selection.entities.end(), entity) == selection.entities.end()) {
            selection.entities.push_back(entity);
        }
    }
    if (paths.activePath.has_value()) {
        const Scene::Entity active = ResolveSelectionPath(*paths.activePath, roots);
        if (std::find(selection.entities.begin(), selection.entities.end(), active) != selection.entities.end()) {
            selection.activeEntity = active;
        }
    }
    if (selection.activeEntity == Scene::kInvalidEntity && !selection.entities.empty()) {
        selection.activeEntity = selection.entities.back();
    }
    return selection;
}

Scene::Entity Editor::ResolveSelectionPath(const EditorSelectionPath& path,
                                                     const std::vector<Scene::Entity>& roots) const {
    if (path.rootIndex >= roots.size() || !IsDocumentEntity(roots[path.rootIndex])) {
        return Scene::kInvalidEntity;
    }

    Scene::Entity resolved = roots[path.rootIndex];
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    for (const std::size_t childIndex : path.childIndices) {
        const std::vector<Scene::Entity> children =
            m_services->GetSceneGraph().GetChildren(entityManager, resolved);
        if (childIndex >= children.size() || !IsDocumentEntity(children[childIndex])) {
            return Scene::kInvalidEntity;
        }
        resolved = children[childIndex];
    }
    return resolved;
}

bool Editor::FindSelectionPath(const Scene::Entity current, const Scene::Entity target,
                                     std::vector<std::size_t>& inOutChildIndices) const {
    if (current == target) {
        return true;
    }
    if (!IsDocumentEntity(current)) {
        return false;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const std::vector<Scene::Entity> children =
        m_services->GetSceneGraph().GetChildren(entityManager, current);
    for (std::size_t childIndex = 0U; childIndex < children.size(); ++childIndex) {
        inOutChildIndices.push_back(childIndex);
        if (FindSelectionPath(children[childIndex], target, inOutChildIndices)) {
            return true;
        }
        inOutChildIndices.pop_back();
    }
    return false;
}

Scene::Entity Editor::CreateDocumentEntityShellInternal(const std::string_view name) {
    if (name.empty() || !IsEntityNameValid(name)) {
        return Scene::kInvalidEntity;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    Scene::ISceneGraph& sceneGraph = m_services->GetSceneGraph();
    const Scene::Entity entity = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, entity, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::NameComponent>(entity, Scene::NameComponent{std::string{name}});
    InvalidateHierarchyFilterCache();
    return entity;
}

template <typename Definition, typename ApplyFunc>
Scene::Entity Editor::CreateObjectDefinitionEntityInternal(const Definition& definition,
                                                                  ApplyFunc applyDefinition) {
    Scene::Entity entity = CreateDocumentEntityShellInternal(
        MakeUniqueDocumentEntityName(definition.defaultName));
    if (entity != Scene::kInvalidEntity) {
        applyDefinition(m_services->GetEntityManager(), entity, definition);
    }
    return entity;
}

Scene::Entity Editor::CreateDocumentEntityInternal(
    const EditorEntityKind kind, const std::optional<std::string>& explicitName) {
    switch (kind) {
        case EditorEntityKind::Empty:
        case EditorEntityKind::Camera:
        case EditorEntityKind::DirectionalLight:
        case EditorEntityKind::CollisionBox:
        case EditorEntityKind::Cube:
        case EditorEntityKind::UVSphere:
        case EditorEntityKind::Plane:
            break;
        default:
            return Scene::kInvalidEntity;
    }

    const std::string defaultName = GetDefaultEntityName(kind);
    const std::string name = explicitName.has_value() ? *explicitName : MakeUniqueDocumentEntityName(defaultName);
    if (defaultName.empty() || !IsEntityNameValid(name)) {
        return Scene::kInvalidEntity;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const Scene::Entity entity = CreateDocumentEntityShellInternal(name);
    if (entity == Scene::kInvalidEntity) {
        return Scene::kInvalidEntity;
    }

    // The per-kind recipes below deliberately live with their object definitions in
    // Engine/Runtime/Objects/3D (one .h + .cpp per kind), not inline here: these legacy editor
    // entity kinds are alternate doors into the exact same recipes the scene-object Add-Object list
    // uses, so a kind's defaults have one home, not two.
    switch (kind) {
        case EditorEntityKind::Empty:
            break;
        case EditorEntityKind::Camera:
            Scene::ApplyCamera3DObjectDefinition(entityManager, entity, Scene::Camera3DObjectDefinition{});
            break;
        case EditorEntityKind::DirectionalLight:
            Scene::ApplyLight3DObjectDefinition(entityManager, entity, Scene::Light3DObjectDefinition{});
            break;
        case EditorEntityKind::CollisionBox:
            Scene::ApplyCollider3DObjectDefinition(entityManager, entity, Scene::Collider3DObjectDefinition{});
            break;
        case EditorEntityKind::Cube:
            Scene::ApplyBoxMesh3DObjectDefinition(entityManager, entity, Scene::BoxMesh3DObjectDefinition{});
            break;
        case EditorEntityKind::UVSphere:
            Scene::ApplySphereMesh3DObjectDefinition(entityManager, entity, Scene::SphereMesh3DObjectDefinition{});
            break;
        case EditorEntityKind::Plane:
            Scene::ApplyPlaneMesh3DObjectDefinition(entityManager, entity, Scene::PlaneMesh3DObjectDefinition{});
            break;
        default:
            return Scene::kInvalidEntity;
    }

    // These legacy kinds are the same objects the Add Object list makes, and are typed as those.
    Scene::SetSceneObjectKind(entityManager, entity, ToSceneObjectKind(kind));

    // Same hierarchy-joining rule as CreateDocumentSceneObject - never a new document root.
    if (entity != Scene::kInvalidEntity) {
        const Scene::Entity parentObject = ResolveNewObjectParent();
        if (parentObject != Scene::kInvalidEntity) {
            m_services->GetSceneGraph().SetParent(entityManager, entity, parentObject);
            InvalidateHierarchyFilterCache();
        }
        PlaceNewDocumentObject(entity);
    }
    return entity;
}

Scene::Entity Editor::ResolveNewObjectParent() {
    // In the level, an object always lives in a folder: under the selection when it is in one, else in
    // the folder new objects go to.
    if (IsOutlinerLayoutActive() && GetDocumentViewport() != Scene::kInvalidEntity) {
        Scene::IEntityManager& entityManager = m_services->GetEntityManager();
        if (m_newObjectsUnderSelection && m_selectedEntity != Scene::kInvalidEntity &&
            IsDocumentSubtree(m_selectedEntity) && m_selectedEntity != GetDocumentViewport()) {
            for (Scene::Entity cursor = m_selectedEntity, parent = Scene::kInvalidEntity;
                 cursor != Scene::kInvalidEntity && TryGetDocumentParent(cursor, parent); cursor = parent) {
                if (entityManager.HasComponent<Scene::FolderComponent>(cursor) &&
                    !entityManager.HasComponent<Scene::OutlinerViewport3DComponent>(cursor)) {
                    if (cursor == m_selectedEntity) {
                        m_lastUsedFolder = cursor;
                    }
                    // A placed entity selected inside a folder is still a sealed row: the new object
                    // goes in that folder, beside the entity, not behind its wall.
                    return ResolvePlaceableParent(m_selectedEntity);
                }
            }
        }
        return ResolveObjectFolder();
    }
    if (m_newObjectsUnderSelection && m_selectedEntity != Scene::kInvalidEntity &&
        IsDocumentSubtree(m_selectedEntity)) {
        return ResolvePlaceableParent(m_selectedEntity);
    }
    // In the Entity Editor, new objects belong to the entity. Nothing is sealed there, so the root
    // is returned as it stands - that is the whole point of opening the entity.
    if (m_entityDocumentActive) {
        const Scene::Entity entityRoot = GetEntityEditorRoot();
        if (entityRoot != Scene::kInvalidEntity) {
            return entityRoot;
        }
    }
    return EnsureDocumentSceneRoot();
}

Scene::Entity Editor::ResolveNewObjectParentFor(const Scene::Objects::SceneObjectKind kind) {
    if (!IsOutlinerLayoutActive() || GetDocumentViewport() == Scene::kInvalidEntity) {
        return ResolveNewObjectParent();
    }
    if (IsTopLevelSingletonKind(kind)) {
        return EnsureDocumentSceneRoot();
    }
    if (kind == Scene::Objects::SceneObjectKind::Folder) {
        // Into the selected folder (or the Viewport itself); otherwise the Viewport.
        Scene::IEntityManager& entityManager = m_services->GetEntityManager();
        if (m_selectedEntity != Scene::kInvalidEntity && IsDocumentSubtree(m_selectedEntity) &&
            entityManager.HasComponent<Scene::FolderComponent>(m_selectedEntity)) {
            return ResolvePlaceableParent(m_selectedEntity);
        }
        return GetDocumentViewport();
    }
    return ResolveNewObjectParent();
}

Scene::Entity Editor::ResolvePlaceableParent(const Scene::Entity candidate) {
    if (candidate == Scene::kInvalidEntity) {
        return candidate;
    }
    // An ordinary row - not a placed entity and not inside one - is already a legitimate parent.
    const Scene::Entity row = ResolveHierarchyRowOwner(candidate);
    if (row == candidate && !IsSealedHierarchyRow(candidate)) {
        return candidate;
    }
    // Otherwise the request names a row the level does not own the inside of. The level parent of
    // that row is the nearest place a new object can live and be seen; anything above it is already
    // unsealed, so one step is enough.
    Scene::Entity parent = Scene::kInvalidEntity;
    if (TryGetDocumentParent(row, parent) && parent != Scene::kInvalidEntity) {
        return parent;
    }
    return candidate;
}

bool Editor::IsOutlinerLayoutActive() const noexcept {
    return !m_retargetSceneActive;
}

Scene::Entity Editor::GetDocumentViewport() {
    if (!IsOutlinerLayoutActive()) {
        return Scene::kInvalidEntity;
    }
    Scene::Entity found = Scene::kInvalidEntity;
    m_services->GetEntityManager().ForEach<Scene::OutlinerViewport3DComponent>(
        [&found](const Scene::Entity entity, const Scene::OutlinerViewport3DComponent&) {
            if (found == Scene::kInvalidEntity) {
                found = entity;
            }
        });
    return found;
}

bool Editor::IsTopLevelSingletonKind(const Scene::Objects::SceneObjectKind kind) const noexcept {
    return kind == Scene::Objects::SceneObjectKind::DirectionalLight3D ||
           kind == Scene::Objects::SceneObjectKind::WorldEnvironment3D;
}

Scene::Entity Editor::FindTopLevelObject(const Scene::Objects::SceneObjectKind kind) {
    const Scene::Entity sceneRoot = GetDocumentSceneRoot();
    if (sceneRoot == Scene::kInvalidEntity) {
        return Scene::kInvalidEntity;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    for (const Scene::Entity child : m_services->GetSceneGraph().GetChildren(entityManager, sceneRoot)) {
        if (Scene::ResolveSceneObjectKind(entityManager, child) == kind) {
            return child;
        }
    }
    return Scene::kInvalidEntity;
}

bool Editor::EnsureDocumentLayout() {
    if (!IsOutlinerLayoutActive()) {
        return false;
    }
    const Scene::Entity sceneRoot = EnsureDocumentSceneRoot();
    if (sceneRoot == Scene::kInvalidEntity) {
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    Scene::ISceneGraph& sceneGraph = m_services->GetSceneGraph();
    bool changed = false;
    Scene::Entity viewport = GetDocumentViewport();
    if (viewport == Scene::kInvalidEntity) {
        viewport = CreateDocumentEntityShellInternal("Viewport3D");
        if (viewport == Scene::kInvalidEntity) {
            return false;
        }
        Scene::ApplyViewport3DObjectDefinition(entityManager, viewport, Scene::Viewport3DObjectDefinition{});
        Scene::SetSceneObjectKind(entityManager, viewport, Scene::Objects::SceneObjectKind::Viewport3D);
        sceneGraph.SetParent(entityManager, viewport, sceneRoot);
        changed = true;
    }
    if (Scene::Entity parent = Scene::kInvalidEntity;
        !TryGetDocumentParent(viewport, parent) || parent != sceneRoot) {
        sceneGraph.SetParent(entityManager, viewport, sceneRoot);
        changed = true;
    }
    static_cast<void>(sceneGraph.SetSiblingIndex(entityManager, viewport, 0U));
    // The Viewport is the open level: it carries the name of its asset in the Asset Table.
    if (const std::string stem = m_activeScenePath.stem().string(); !stem.empty()) {
        if (!entityManager.HasComponent<Scene::NameComponent>(viewport)) {
            entityManager.AddComponent<Scene::NameComponent>(viewport, Scene::NameComponent{stem});
        } else if (entityManager.GetComponent<Scene::NameComponent>(viewport).name != stem) {
            entityManager.GetComponent<Scene::NameComponent>(viewport).name = stem;
            changed = true;
        }
    }

    // Anything at the top that is not one of the three moves into a folder; a folder moves into the
    // Viewport. A second DirectionalLight3D or WorldEnvironment is an ordinary object there.
    bool seenLight = false;
    bool seenEnvironment = false;
    std::vector<Scene::Entity> strays;
    for (const Scene::Entity child : sceneGraph.GetChildren(entityManager, sceneRoot)) {
        if (child == viewport) {
            continue;
        }
        const Scene::Objects::SceneObjectKind kind = Scene::ResolveSceneObjectKind(entityManager, child);
        if (kind == Scene::Objects::SceneObjectKind::DirectionalLight3D && !seenLight) {
            seenLight = true;
        } else if (kind == Scene::Objects::SceneObjectKind::WorldEnvironment3D && !seenEnvironment) {
            seenEnvironment = true;
        } else {
            strays.push_back(child);
        }
    }
    Scene::Entity world = Scene::kInvalidEntity;
    for (const Scene::Entity stray : strays) {
        if (entityManager.HasComponent<Scene::FolderComponent>(stray)) {
            sceneGraph.SetParent(entityManager, stray, viewport);
        } else {
            if (world == Scene::kInvalidEntity) {
                world = ResolveObjectFolder();
            }
            sceneGraph.SetParent(entityManager, stray, world);
        }
        changed = true;
    }
    // No folder is created for an empty level. Opening a scene added an empty "World" before the
    // author had made anything; ResolveObjectFolder() still makes one, but only when an object
    // is actually placed and needs a folder to sit in.
    InvalidateHierarchyFilterCache();
    return changed;
}

Scene::Entity Editor::ResolveObjectFolder() {
    const Scene::Entity viewport = GetDocumentViewport();
    if (viewport == Scene::kInvalidEntity) {
        return EnsureDocumentSceneRoot();
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    Scene::ISceneGraph& sceneGraph = m_services->GetSceneGraph();
    const auto inViewport = [&](const Scene::Entity folder) {
        for (Scene::Entity cursor = folder, parent = Scene::kInvalidEntity;
             cursor != Scene::kInvalidEntity && TryGetDocumentParent(cursor, parent); cursor = parent) {
            if (parent == viewport) {
                return true;
            }
        }
        return false;
    };
    if (m_lastUsedFolder != Scene::kInvalidEntity && entityManager.IsAlive(m_lastUsedFolder) &&
        entityManager.HasComponent<Scene::FolderComponent>(m_lastUsedFolder) && inViewport(m_lastUsedFolder)) {
        return m_lastUsedFolder;
    }
    for (const Scene::Entity child : sceneGraph.GetChildren(entityManager, viewport)) {
        if (entityManager.HasComponent<Scene::FolderComponent>(child)) {
            return m_lastUsedFolder = child;
        }
    }
    const Scene::Entity world = CreateDocumentEntityShellInternal(MakeUniqueDocumentEntityName("World"));
    if (world == Scene::kInvalidEntity) {
        return viewport;
    }
    Scene::ApplyFolderObjectDefinition(entityManager, world, Scene::FolderObjectDefinition{});
    Scene::SetSceneObjectKind(entityManager, world, Scene::Objects::SceneObjectKind::Folder);
    sceneGraph.SetParent(entityManager, world, viewport);
    InvalidateHierarchyFilterCache();
    return m_lastUsedFolder = world;
}

bool Editor::IsAllowedOutlinerParent(const Scene::Entity entity, const Scene::Entity parent) {
    if (!IsOutlinerLayoutActive() || GetDocumentViewport() == Scene::kInvalidEntity) {
        return true;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const Scene::Entity sceneRoot = GetDocumentSceneRoot();
    const Scene::Entity viewport = GetDocumentViewport();
    if (entity == viewport) {
        return parent == sceneRoot;
    }
    const Scene::Objects::SceneObjectKind kind = Scene::ResolveSceneObjectKind(entityManager, entity);
    if (parent == sceneRoot) {
        // Only the level's own sun and environment sit beside the Viewport, one of each.
        const Scene::Entity existing = IsTopLevelSingletonKind(kind) ? FindTopLevelObject(kind) : entity;
        return IsTopLevelSingletonKind(kind) && (existing == Scene::kInvalidEntity || existing == entity);
    }
    if (parent == viewport) {
        return entityManager.HasComponent<Scene::FolderComponent>(entity);
    }
    // A placed entity is one row and its contents belong to the Entity Editor. Moving something
    // under it - or under anything already inside it - would hide that object from every window
    // while it kept existing and colliding. The drop target is skipped in the panel; this is the
    // rule behind it, so a menu, a script or a future caller cannot go around the wall.
    if (IsSealedHierarchyRow(parent) || ResolveHierarchyRowOwner(parent) != parent) {
        return false;
    }
    // Anywhere else in the Viewport's tree is inside a folder.
    for (Scene::Entity cursor = parent, up = Scene::kInvalidEntity;
         cursor != Scene::kInvalidEntity && TryGetDocumentParent(cursor, up); cursor = up) {
        if (cursor == viewport) {
            return true;
        }
        if (up == viewport) {
            return true;
        }
    }
    return false;
}

void Editor::PlaceNewDocumentObject(const Scene::Entity entity) {
    if (m_newObjectPlacement != EditorNewObjectPlacement::ViewFocus || !m_viewportCameraFocus.has_value()) {
        return;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent<Scene::TransformComponent>(entity) ||
        !entityManager.HasComponent<Scene::HierarchyComponent>(entity)) {
        return; // A pure Object has no place in space.
    }
    // The object sits at its parent's origin; move it by the offset from there to the focus, taken
    // into the parent's space. A parent whose world transform is not resolved yet leaves it be.
    const std::optional<Scene::WorldTransformComponent> parentWorld =
        TryGetComposingParentWorld(entityManager.GetComponent<Scene::HierarchyComponent>(entity).parent);
    Math::Vector3 localPosition{};
    if (!parentWorld.has_value() || parentWorld->dirty ||
        !ComputeLocalDeltaForWorldDelta(entity, *m_viewportCameraFocus - parentWorld->worldPosition,
                                           localPosition)) {
        return;
    }
    Scene::TransformComponent transform = entityManager.GetComponent<Scene::TransformComponent>(entity);
    transform.localPosition = localPosition;
    m_services->GetSceneGraph().SetLocalTransform(entityManager, entity, transform);
}

void Editor::SetViewportCameraFocus(const Math::Vector3& focus) noexcept {
    if (IsFiniteVector(focus)) {
        m_viewportCameraFocus = focus;
    }
}

void Editor::RecordHistory(HistoryEntry entry) {
    m_redoHistory.clear();
    if (m_undoHistory.size() >= m_historyCapacity) {
        m_undoHistory.pop_front();
    }
    m_undoHistory.push_back(std::move(entry));
}

void Editor::ClearHistory() noexcept {
    m_undoHistory.clear();
    m_redoHistory.clear();
}

bool Editor::UndoHistoryEntry(HistoryEntry& entry) {
    return std::visit(
        [this](auto& typedEntry) -> bool {
            using EntryType = std::decay_t<decltype(typedEntry)>;
            if constexpr (std::is_same_v<EntryType, TransformHistoryEntry>) {
                if (!ApplyLocalTransform(typedEntry.entity, typedEntry.before)) {
                    return false;
                }
                RestoreSelection(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                return true;
            } else if constexpr (std::is_same_v<EntryType, NameHistoryEntry>) {
                if (!ApplyEntityNameState(typedEntry.entity, typedEntry.beforeName)) {
                    return false;
                }
                RestoreSelection(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                return true;
            } else if constexpr (std::is_same_v<EntryType, PrimitiveAppearanceHistoryEntry>) {
                if (!ApplyPrimitiveMeshState(typedEntry.entity, typedEntry.before)) {
                    return false;
                }
                RestoreSelection(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                return true;
            } else if constexpr (std::is_same_v<EntryType, MeshSourceHistoryEntry>) {
                if (!ApplyMeshSourceState(typedEntry.entity, typedEntry.beforeMesh, typedEntry.beforeShape)) {
                    return false;
                }
                Scene::IEntityManager& em = m_services->GetEntityManager();
                if (typedEntry.createdSkeletonEntity != Scene::kInvalidEntity &&
                    IsDocumentEntity(typedEntry.createdSkeletonEntity)) {
                    DestroyDocumentSubtree(typedEntry.createdSkeletonEntity);
                    typedEntry.createdSkeletonEntity = Scene::kInvalidEntity;
                }
                if (typedEntry.syncedSkeletonEntity != Scene::kInvalidEntity &&
                    typedEntry.beforeSkeleton.has_value() &&
                    IsDocumentEntity(typedEntry.syncedSkeletonEntity) &&
                    em.HasComponent<Scene::Skeleton3DComponent>(typedEntry.syncedSkeletonEntity)) {
                    em.GetComponent<Scene::Skeleton3DComponent>(typedEntry.syncedSkeletonEntity) =
                        *typedEntry.beforeSkeleton;
                }
                if (typedEntry.beforeSkin.has_value()) {
                    em.AddComponent<Scene::SkinComponent>(typedEntry.entity, *typedEntry.beforeSkin);
                } else if (em.HasComponent<Scene::SkinComponent>(typedEntry.entity)) {
                    em.RemoveComponent<Scene::SkinComponent>(typedEntry.entity);
                }
                InvalidateHierarchyFilterCache();
                RestoreSelection(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                return true;
            } else if constexpr (std::is_same_v<EntryType, SceneComponentHistoryEntry>) {
                if (!ApplySceneComponentState(typedEntry.entity, typedEntry.kind, typedEntry.before)) {
                    return false;
                }
                RestoreSelection(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                return true;
            } else if constexpr (std::is_same_v<EntryType, ComponentPropertyHistoryEntry>) {
                if (!ApplyComponentPropertySnapshot(typedEntry.entity, typedEntry.metadata,
                                                       typedEntry.before.Get())) {
                    return false;
                }
                RestoreSelection(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                return true;
            } else if constexpr (std::is_same_v<EntryType, CreationHistoryEntry>) {
                if (!IsDocumentEntity(typedEntry.activeEntity)) {
                    return false;
                }
                DestroyDocumentSubtree(typedEntry.activeEntity);
                typedEntry.activeEntity = Scene::kInvalidEntity;
                RestoreSelection(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                return true;
            } else if constexpr (std::is_same_v<EntryType, SceneObjectCreationHistoryEntry>) {
                if (!IsDocumentEntity(typedEntry.activeEntity)) {
                    return false;
                }
                DestroyDocumentSubtree(typedEntry.activeEntity);
                typedEntry.activeEntity = Scene::kInvalidEntity;
                RestoreSelection(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                return true;
            } else if constexpr (std::is_same_v<EntryType, DuplicationHistoryEntry>) {
                if (!IsDocumentEntity(typedEntry.activeEntity)) {
                    return false;
                }
                DestroyDocumentSubtree(typedEntry.activeEntity);
                typedEntry.activeEntity = Scene::kInvalidEntity;
                RestoreSelection(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                return true;
            } else if constexpr (std::is_same_v<EntryType, DeletionHistoryEntry>) {
                if (typedEntry.activeEntity != Scene::kInvalidEntity &&
                    m_services->GetEntityManager().IsAlive(typedEntry.activeEntity)) {
                    return false;
                }
                const Scene::Entity restored = RestoreSubtreeUnderParent(typedEntry.snapshot, typedEntry.originalParent);
                if (restored == Scene::kInvalidEntity) {
                    return false;
                }
                static_cast<void>(m_services->GetSceneGraph().SetSiblingIndex(
                    m_services->GetEntityManager(), restored, typedEntry.siblingIndex));
                typedEntry.activeEntity = restored;
                typedEntry.selectionBefore = EditorSelectionSnapshot{{restored}, restored};
                RestoreSelection(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                return true;
            } else if constexpr (std::is_same_v<EntryType, MultiDeletionHistoryEntry>) {
                for (const DeletionHistoryEntry& deletion : typedEntry.deletions) {
                    if (deletion.activeEntity != Scene::kInvalidEntity &&
                        m_services->GetEntityManager().IsAlive(deletion.activeEntity)) {
                        return false;
                    }
                }
                std::vector<Scene::Entity> restoredRoots(typedEntry.deletions.size(), Scene::kInvalidEntity);
                Scene::Entity restoredActive = Scene::kInvalidEntity;
                for (std::size_t index = typedEntry.deletions.size(); index-- > 0U;) {
                    DeletionHistoryEntry& deletion = typedEntry.deletions[index];
                    const Scene::Entity restored = RestoreSubtreeUnderParent(deletion.snapshot, deletion.originalParent);
                    if (restored == Scene::kInvalidEntity) {
                        // Leave no half-done undo behind: remove the subtrees this undo already brought back.
                        for (std::size_t r = index + 1U; r < restoredRoots.size(); ++r) {
                            if (IsDocumentEntity(restoredRoots[r])) {
                                DestroyDocumentSubtree(restoredRoots[r]);
                            }
                        }
                        return false;
                    }
                    static_cast<void>(m_services->GetSceneGraph().SetSiblingIndex(
                        m_services->GetEntityManager(), restored, deletion.siblingIndex));
                    if (deletion.activeEntity != Scene::kInvalidEntity &&
                        deletion.activeEntity == typedEntry.selectionBefore.activeEntity) {
                        restoredActive = restored;
                    }
                    deletion.activeEntity = restored;
                    restoredRoots[index] = restored;
                }
                typedEntry.selectionBefore = EditorSelectionSnapshot{std::move(restoredRoots), restoredActive};
                RestoreSelection(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                InvalidateHierarchyFilterCache();
                return true;
            } else {
                // A move among siblings keeps its parent, so it needs neither a reparent nor a
                // transform - and a pure Object, which has no transform, can make one.
                const bool reparented = typedEntry.parentBefore != typedEntry.parentAfter;
                if (reparented ? (!IsReparentableObject(typedEntry.entity) ||
                                  (typedEntry.parentBefore != Scene::kInvalidEntity &&
                                   !IsHierarchyObject(typedEntry.parentBefore)) ||
                                  DoesSubtreeContainEntity(typedEntry.entity, typedEntry.parentBefore))
                               : !IsHierarchyObject(typedEntry.entity)) {
                    return false;
                }
                if (reparented) {
                    m_services->GetSceneGraph().SetParent(
                        m_services->GetEntityManager(), typedEntry.entity, typedEntry.parentBefore);
                    if (HasSceneGraphObject(typedEntry.entity) &&
                        !ApplyLocalTransform(typedEntry.entity, typedEntry.localTransformBefore)) {
                        return false;
                    }
                }
                static_cast<void>(m_services->GetSceneGraph().SetSiblingIndex(
                    m_services->GetEntityManager(), typedEntry.entity, typedEntry.siblingIndexBefore));
                RestoreSelection(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                InvalidateHierarchyFilterCache();
                return true;
            }
        },
        entry);
}

bool Editor::RedoHistoryEntry(HistoryEntry& entry) {
    return std::visit(
        [this](auto& typedEntry) -> bool {
            using EntryType = std::decay_t<decltype(typedEntry)>;
            if constexpr (std::is_same_v<EntryType, TransformHistoryEntry>) {
                if (!ApplyLocalTransform(typedEntry.entity, typedEntry.after)) {
                    return false;
                }
                RestoreSelection(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                return true;
            } else if constexpr (std::is_same_v<EntryType, NameHistoryEntry>) {
                if (!ApplyEntityNameState(typedEntry.entity, typedEntry.afterName)) {
                    return false;
                }
                RestoreSelection(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                return true;
            } else if constexpr (std::is_same_v<EntryType, PrimitiveAppearanceHistoryEntry>) {
                if (!ApplyPrimitiveMeshState(typedEntry.entity, typedEntry.after)) {
                    return false;
                }
                RestoreSelection(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                return true;
            } else if constexpr (std::is_same_v<EntryType, MeshSourceHistoryEntry>) {
                if (!ApplyMeshSourceState(typedEntry.entity, typedEntry.afterMesh, typedEntry.afterShape)) {
                    return false;
                }
                Scene::IEntityManager& em = m_services->GetEntityManager();
                if (typedEntry.createdSkeletonSnapshot.has_value() &&
                    (typedEntry.createdSkeletonEntity == Scene::kInvalidEntity ||
                     !em.IsAlive(typedEntry.createdSkeletonEntity))) {
                    const Scene::Entity restored =
                        RestoreSubtreeUnderParent(*typedEntry.createdSkeletonSnapshot, typedEntry.entity);
                    if (restored != Scene::kInvalidEntity) {
                        typedEntry.createdSkeletonEntity = restored;
                    }
                }
                if (typedEntry.syncedSkeletonEntity != Scene::kInvalidEntity &&
                    typedEntry.afterSkeleton.has_value() &&
                    IsDocumentEntity(typedEntry.syncedSkeletonEntity) &&
                    em.HasComponent<Scene::Skeleton3DComponent>(typedEntry.syncedSkeletonEntity)) {
                    em.GetComponent<Scene::Skeleton3DComponent>(typedEntry.syncedSkeletonEntity) =
                        *typedEntry.afterSkeleton;
                }
                if (typedEntry.afterSkin.has_value()) {
                    em.AddComponent<Scene::SkinComponent>(typedEntry.entity, *typedEntry.afterSkin);
                } else if (em.HasComponent<Scene::SkinComponent>(typedEntry.entity)) {
                    em.RemoveComponent<Scene::SkinComponent>(typedEntry.entity);
                }
                InvalidateHierarchyFilterCache();
                RestoreSelection(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                return true;
            } else if constexpr (std::is_same_v<EntryType, SceneComponentHistoryEntry>) {
                if (!ApplySceneComponentState(typedEntry.entity, typedEntry.kind, typedEntry.after)) {
                    return false;
                }
                RestoreSelection(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                return true;
            } else if constexpr (std::is_same_v<EntryType, ComponentPropertyHistoryEntry>) {
                if (!ApplyComponentPropertySnapshot(typedEntry.entity, typedEntry.metadata,
                                                       typedEntry.after.Get())) {
                    return false;
                }
                RestoreSelection(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                return true;
            } else if constexpr (std::is_same_v<EntryType, CreationHistoryEntry>) {
                if (typedEntry.activeEntity != Scene::kInvalidEntity &&
                    m_services->GetEntityManager().IsAlive(typedEntry.activeEntity)) {
                    return false;
                }
                const Scene::Entity recreated =
                    CreateDocumentEntityInternal(typedEntry.kind, std::optional<std::string>{typedEntry.name});
                if (recreated == Scene::kInvalidEntity) {
                    return false;
                }
                typedEntry.activeEntity = recreated;
                typedEntry.selectionAfter = EditorSelectionSnapshot{{recreated}, recreated};
                RestoreSelection(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                return true;
            } else if constexpr (std::is_same_v<EntryType, SceneObjectCreationHistoryEntry>) {
                if (typedEntry.activeEntity != Scene::kInvalidEntity &&
                    m_services->GetEntityManager().IsAlive(typedEntry.activeEntity)) {
                    return false;
                }
                Scene::Entity redoParent = typedEntry.createdUnderParent;
                if (redoParent == Scene::kInvalidEntity || !IsDocumentEntity(redoParent)) {
                    // The original parent is gone (deleted by later history, say) - re-home
                    // under the scene root rather than dropping the object to document top level.
                    redoParent = EnsureDocumentSceneRoot();
                }
                const Scene::Entity restored =
                    RestoreSubtreeUnderParent(typedEntry.snapshot, redoParent);
                if (restored == Scene::kInvalidEntity) {
                    return false;
                }
                typedEntry.activeEntity = restored;
                typedEntry.selectionAfter = EditorSelectionSnapshot{{restored}, restored};
                RestoreSelection(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                InvalidateHierarchyFilterCache();
                return true;
            } else if constexpr (std::is_same_v<EntryType, DuplicationHistoryEntry>) {
                if (typedEntry.activeEntity != Scene::kInvalidEntity &&
                    m_services->GetEntityManager().IsAlive(typedEntry.activeEntity)) {
                    return false;
                }
                const Scene::Entity restored = RestoreSubtreeUnderParent(typedEntry.snapshot, typedEntry.originalParent);
                if (restored == Scene::kInvalidEntity) {
                    return false;
                }
                if (typedEntry.duplicateRootName.has_value() &&
                    !ApplyEntityNameState(restored, typedEntry.duplicateRootName)) {
                    DestroyDocumentSubtree(restored);
                    return false;
                }
                static_cast<void>(m_services->GetSceneGraph().SetSiblingIndex(
                    m_services->GetEntityManager(), restored, typedEntry.siblingIndex));
                typedEntry.activeEntity = restored;
                typedEntry.selectionAfter = EditorSelectionSnapshot{{restored}, restored};
                RestoreSelection(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                return true;
            } else if constexpr (std::is_same_v<EntryType, DeletionHistoryEntry>) {
                if (!IsDocumentEntity(typedEntry.activeEntity)) {
                    return false;
                }
                DestroyDocumentSubtree(typedEntry.activeEntity);
                typedEntry.activeEntity = Scene::kInvalidEntity;
                RestoreSelection(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                return true;
            } else if constexpr (std::is_same_v<EntryType, MultiDeletionHistoryEntry>) {
                for (const DeletionHistoryEntry& deletion : typedEntry.deletions) {
                    if (!IsDocumentEntity(deletion.activeEntity)) {
                        return false;
                    }
                }
                for (DeletionHistoryEntry& deletion : typedEntry.deletions) {
                    DestroyDocumentSubtree(deletion.activeEntity);
                    deletion.activeEntity = Scene::kInvalidEntity;
                }
                RestoreSelection(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                InvalidateHierarchyFilterCache();
                return true;
            } else {
                const bool reparented = typedEntry.parentBefore != typedEntry.parentAfter;
                if (reparented ? (!IsReparentableObject(typedEntry.entity) ||
                                  (typedEntry.parentAfter != Scene::kInvalidEntity &&
                                   !IsHierarchyObject(typedEntry.parentAfter)) ||
                                  DoesSubtreeContainEntity(typedEntry.entity, typedEntry.parentAfter))
                               : !IsHierarchyObject(typedEntry.entity)) {
                    return false;
                }
                if (reparented) {
                    m_services->GetSceneGraph().SetParent(
                        m_services->GetEntityManager(), typedEntry.entity, typedEntry.parentAfter);
                    if (HasSceneGraphObject(typedEntry.entity) &&
                        !ApplyLocalTransform(typedEntry.entity, typedEntry.localTransformAfter)) {
                        return false;
                    }
                }
                static_cast<void>(m_services->GetSceneGraph().SetSiblingIndex(
                    m_services->GetEntityManager(), typedEntry.entity, typedEntry.siblingIndexAfter));
                RestoreSelection(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                InvalidateHierarchyFilterCache();
                return true;
            }
        },
        entry);
}

bool Editor::IsStructuralRoot(const Scene::Entity entity) {
    return IsSceneRootEntity(entity) ||
           (entity != Scene::kInvalidEntity && entity == GetDocumentViewport()) ||
           (m_entityDocumentActive && entity != Scene::kInvalidEntity && entity == GetEntityEditorRoot());
}

bool Editor::IsSceneRootEntity(const Scene::Entity entity) const {
    return entity != Scene::kInvalidEntity && m_services->GetEntityManager().IsAlive(entity) &&
           m_services->GetEntityManager().HasComponent<Scene::SceneRootComponent>(entity);
}

Scene::Entity Editor::GetDocumentSceneRoot() {
    Scene::Entity found = Scene::kInvalidEntity;
    m_services->GetEntityManager().ForEach<Scene::SceneRootComponent>(
        [&found](const Scene::Entity entity, const Scene::SceneRootComponent&) {
            if (found == Scene::kInvalidEntity) {
                found = entity; // markers are guarded to exactly one; first hit is THE root
            }
        });
    return found;
}

Scene::Entity Editor::EnsureDocumentSceneRoot() {
    const Scene::Entity existing = GetDocumentSceneRoot();
    if (existing != Scene::kInvalidEntity) {
        return existing;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const Scene::Entity root =
        CreateDocumentEntityShellInternal(Scene::SceneRootObjectDefinition::defaultName);
    if (root == Scene::kInvalidEntity) {
        return Scene::kInvalidEntity;
    }
    Scene::ApplySceneRootObjectDefinition(entityManager, root, Scene::SceneRootObjectDefinition{});
    return root;
}

std::vector<Scene::Entity> Editor::GetDocumentRoots() {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    std::vector<Scene::Entity> roots =
        m_services->GetSceneGraph().GetChildren(entityManager, Scene::kInvalidEntity);
    // Internal engine/editor infrastructure entities (e.g. the Viewport's hidden free-look-camera
    // proxy) are also scene roots (AttachTransform always creates one), but must never be
    // treated as document content - this is the one place that decision needs to be made, since
    // every other document-root consumer (Play-mode snapshot capture/restore, the Scene Hierarchy
    // panel, ClearDocumentScene, etc.) already reaches roots exclusively through this function.
    roots.erase(std::remove_if(roots.begin(), roots.end(),
                               [&entityManager](const Scene::Entity entity) {
                                   return entityManager.HasComponent<Scene::EditorInternalEntityComponent>(
                                       entity);
                               }),
               roots.end());
    // The scene root leads. The entity manager iterates in archetype order, which it documents as
    // unspecified and which moves whenever an object's component set changes; without this the root
    // could sit anywhere among stray top-level entities, in the outliner, in the reparent list, and
    // in the order a Play-mode snapshot is captured and restored.
    std::stable_partition(roots.begin(), roots.end(), [&entityManager](const Scene::Entity entity) {
        return entityManager.HasComponent<Scene::SceneRootComponent>(entity);
    });
    return roots;
}

EditorState Editor::GetState() const noexcept {
    return m_state;
}

Scene::Entity Editor::GetSelectedEntity() const noexcept {
    return m_selectedEntity;
}

Scene::Entity Editor::GetPreviewCamera() const noexcept {
    if (m_previewCamera == Scene::kInvalidEntity || m_services == nullptr) {
        return Scene::kInvalidEntity;
    }
    if (!Scene::IsDocumentCameraEntity(m_services->GetEntityManager(), m_previewCamera)) {
        return Scene::kInvalidEntity;
    }
    return m_previewCamera;
}

void Editor::SetPreviewCamera(const Scene::Entity entity) {
    if (m_services == nullptr || !Scene::IsDocumentCameraEntity(m_services->GetEntityManager(), entity)) {
        return;
    }
    m_previewCamera = entity;
}

void Editor::ClearPreviewCamera() noexcept {
    m_previewCamera = Scene::kInvalidEntity;
}

namespace {

[[nodiscard]] bool IsFiniteVector3(const Math::Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool IsViewportBookmarkWellFormed(const EditorViewportBookmark& bookmark) noexcept {
    return IsFiniteVector3(bookmark.target) && std::isfinite(bookmark.yawRadians) &&
           std::isfinite(bookmark.pitchRadians) && std::isfinite(bookmark.distance) &&
           bookmark.distance > 0.0F;
}

} // namespace

bool Editor::SetViewportBookmark(const std::size_t slot,
                                       const EditorViewportBookmark& bookmark) noexcept {
    // The clamping stays with the camera on apply (see ResolveOrbitBookmarkFromLook's doc):
    // rejecting a straight-pole pose here would throw away a perfectly valid look direction for
    // a UI-level preference, and callers composing from real look vectors should not have to
    // special-case the poles.
    if (slot >= kEditorViewportBookmarkSlotCount || !IsViewportBookmarkWellFormed(bookmark)) {
        return false;
    }
    m_viewportBookmarks[slot] = bookmark;
    return true;
}

std::optional<EditorViewportBookmark> Editor::GetViewportBookmark(
    const std::size_t slot) const noexcept {
    if (slot >= kEditorViewportBookmarkSlotCount) {
        return std::nullopt;
    }
    return m_viewportBookmarks[slot];
}

bool Editor::ClearViewportBookmark(const std::size_t slot) noexcept {
    if (slot >= kEditorViewportBookmarkSlotCount) {
        return false;
    }
    const bool wasOccupied = m_viewportBookmarks[slot].has_value();
    m_viewportBookmarks[slot].reset();
    return wasOccupied;
}

std::optional<EditorViewportBookmark> Editor::ComposeMarker3DFocusBookmark(
    const Scene::Entity entity) const {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent<Scene::Marker3DComponent>(entity) ||
        !entityManager.HasComponent<Scene::WorldTransformComponent>(entity)) {
        return std::nullopt;
    }
    const Scene::Marker3DComponent& marker =
        entityManager.GetComponent<Scene::Marker3DComponent>(entity);
    if (!marker.enabled || !Scene::IsMarker3DObjectComponentValid(marker)) {
        return std::nullopt;
    }
    const Scene::WorldTransformComponent& worldTransform =
        entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
    Math::Quaternion worldRotation{};
    if (!Math::TryNormalize(worldTransform.worldRotation, worldRotation)) {
        return std::nullopt; // a degenerate object rotation gives no meaningful viewpoint
    }
    const std::optional<Scene::Marker3DPose> pose = Scene::ComposeMarker3DPose(
        worldTransform.worldPosition, worldRotation, marker.localPosition, marker.localRotation);
    if (!pose.has_value()) {
        return std::nullopt;
    }
    // The engine camera convention looks down -Z (see the SpringArm3D module doc), so the
    // marker's facing is its composed rotation applied to -Z.
    const Math::Vector3 forward =
        Math::RotateVector(pose->rotation, Math::Vector3{0.0F, 0.0F, -1.0F});
    return ResolveOrbitBookmarkFromLook(pose->position, forward, kEditorMarkerFocusDistance);
}

std::optional<Math::Vector3> Editor::ResolveEntityFocusTarget(
    const Scene::Entity entity) const {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent<Scene::WorldTransformComponent>(entity)) {
        return std::nullopt;
    }
    const Math::Vector3& worldPosition =
        entityManager.GetComponent<Scene::WorldTransformComponent>(entity).worldPosition;
    if (!IsFiniteVector3(worldPosition)) {
        return std::nullopt;
    }
    return worldPosition;
}

std::optional<EditorViewportBookmark> Editor::ResolveOrbitBookmarkFromLook(
    const Math::Vector3& eye, const Math::Vector3& forward, const float distance) noexcept {
    // The OrbitCamera clamps pitch to +/-1.5533 rad (~89 deg) when a pose is handed to it, so an
    // exact up/down look doesn't exist as yaw/pitch state. This inverse solves the exact angle
    // anyway (the honest inverse of the camera's own forward formula) and the camera performs
    // the final clamp on apply; the yaw=0 pole convention stays, not because the true yaw is
    // unknowable there but because expressing it would need the very clamped state we avoid.
    // That keeps the composed marker round trip exact (a fly-to-target's eye re-derives to
    // bit-comparable values through both directions of the math) - the same measured-precision
    // contract the SpawnPoint3D and SpringArm3D resolvers keep.
    constexpr float kOrbitPitchLimitRadians = 1.5707964F; // exactly pi/2 - see the clamp note
    constexpr float kDegenerateForwardEpsilon = 1.0e-6F;
    if (!IsFiniteVector3(eye) || !IsFiniteVector3(forward) || !std::isfinite(distance) ||
        distance <= 0.0F) {
        return std::nullopt;
    }
    const float forwardLengthSquared = Math::LengthSquared(forward);
    if (forwardLengthSquared < kDegenerateForwardEpsilon * kDegenerateForwardEpsilon) {
        return std::nullopt;
    }
    const float forwardLength = std::sqrt(forwardLengthSquared);
    const Math::Vector3 direction = forward * (1.0F / forwardLength);

    // OrbitCamera: eye = target + offset(yaw,pitch) * distance with offset =
    // (cos(yaw)cos(pitch), sin(pitch), sin(yaw)cos(pitch)); the look direction (target - eye)
    // is therefore -offset, i.e. sin(pitch) = -dir.y and (cos,sin)(yaw)*cos(pitch) = (-x,-z).
    EditorViewportBookmark bookmark{};
    bookmark.pitchRadians =
        std::clamp(std::asin(std::clamp(-direction.y, -1.0F, 1.0F)), -kOrbitPitchLimitRadians,
                   kOrbitPitchLimitRadians);
    // Degenerate by construction: the solvable pole convention. At the true pole the
    // atan2 input is (0,0) and yaw is genuinely unobservable - fixing it to 0 keeps the
    // fn total without inventing an angle the forward cannot encode.
    const float cosPitch = std::cos(bookmark.pitchRadians);
    if (cosPitch < kDegenerateForwardEpsilon) {
        bookmark.yawRadians = 0.0F;
    } else {
        bookmark.yawRadians = std::atan2(-direction.z, -direction.x);
    }
    bookmark.target = eye + direction * distance;
    bookmark.distance = distance;
    return bookmark;
}

Editor2DCanvasState Editor::Get2DCanvasState() const noexcept {
    return m_2dCanvasState;
}
bool Editor::Set2DCanvasZoom(const float zoom) noexcept {
    if (!std::isfinite(zoom) || zoom < kMinimum2DCanvasZoom || zoom > kMaximum2DCanvasZoom) {
        return false;
    }
    m_2dCanvasState.zoom = zoom;
    return true;
}
void Editor::Reset2DCanvasView() noexcept {
    m_2dCanvasState = Editor2DCanvasState{};
    m_2dCanvasPanning = false;
}
bool Editor::IsSceneDirty() const noexcept {

    return m_sceneDirty;
}

EditorToolSessionPhase Editor::GetToolSessionPhase() const noexcept {
    return m_toolSession.GetPhase();
}

EditorToolSessionOutcome Editor::GetLastToolSessionOutcome() const noexcept {
    return m_toolSession.GetLastOutcome();
}

const std::optional<Asset::AssetRecord>& Editor::GetSelectedAsset() const noexcept {
    return m_selectedAsset;
}

const std::optional<Asset::ProjectFileEntry>& Editor::GetSelectedProjectFile() const noexcept {
    return m_selectedProjectFile;
}

const std::string& Editor::GetAssetFilter() const noexcept {
    return m_assetFilter;
}

const std::filesystem::path& Editor::GetActiveScenePath() const noexcept {
    return m_activeScenePath;
}

void Editor::SetActiveScenePath(std::filesystem::path path) {
    if (!path.empty()) {
        m_activeScenePath = std::move(path);
    }
}

bool Editor::OpenScriptGraphForEntity(const Scene::Entity entity) {
    return OpenUVScriptForEntity(entity);
}

bool Editor::WriteProjectTextFile(const std::filesystem::path& path, const std::string_view text) {
    const auto writeAtomically = [text](const std::filesystem::path& target) {
        const std::filesystem::path temporaryPath = target.string() + ".tmp";
        std::error_code error;
        if (!target.parent_path().empty()) {
            std::filesystem::create_directories(target.parent_path(), error);
        }
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return false;
        }
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.flush();
        const bool outputGood = output.good();
        output.close();
        if (!outputGood) {
            std::filesystem::remove(temporaryPath, error);
            return false;
        }
        std::filesystem::rename(temporaryPath, target, error);
        if (error) {
            std::filesystem::remove(target, error);
            error.clear();
            std::filesystem::rename(temporaryPath, target, error);
        }
        if (error) {
            std::filesystem::remove(temporaryPath, error);
            return false;
        }
        return true;
    };

    const std::string virtualPath = path.generic_string();
    Asset::IFileSystem& fileSystem = m_services->GetFileSystem();
    const std::filesystem::path resolvedPath = fileSystem.ResolveRealPath(virtualPath);
    if (!resolvedPath.empty()) {
        return writeAtomically(resolvedPath);
    }
    std::vector<std::byte> bytes(text.size());
    if (!text.empty()) {
        std::memcpy(bytes.data(), text.data(), text.size());
    }
    if (fileSystem.WriteFile(virtualPath, bytes)) {
        return true;
    }
    // Some editor test/legacy configurations have no mounted project directory. Preserve the
    // established raw-path behavior as a compatibility fallback after the native VFS attempt.
    return writeAtomically(path);
}

std::optional<std::string> Editor::ReadProjectTextFile(const std::filesystem::path& path) const {
    // Asked first, because a VFS read that misses logs an error - and "is this name free?" is an
    // ordinary question here, not a failure.
    const Asset::IFileSystem& fileSystem = m_services->GetFileSystem();
    if (const std::optional<std::vector<std::byte>> bytes =
            fileSystem.HasFile(path.generic_string()) ? fileSystem.ReadFile(path.generic_string()) : std::nullopt;
        bytes.has_value()) {
        return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return std::nullopt;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void Editor::SetColorPickerPreferences(ColorPickerPreferences preferences) {
    const bool previousAdvancedOpen = m_colorPickerPreferences.advancedOpen;
    const auto sanitize = [](std::vector<EditorColor>& colors, const std::size_t cap) {
        std::erase_if(colors, [](const EditorColor& color) {
            return !std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b) ||
                   !std::isfinite(color.a);
        });
        for (EditorColor& color : colors) {
            color = EditorColor{std::clamp(color.r, 0.0F, 1.0F), std::clamp(color.g, 0.0F, 1.0F),
                                   std::clamp(color.b, 0.0F, 1.0F), std::clamp(color.a, 0.0F, 1.0F)};
        }
        if (colors.size() > cap) {
            colors.resize(cap);
        }
    };
    sanitize(preferences.saved, kMaxSavedColors);
    sanitize(preferences.recents, kMaxRecentColors);
    m_colorPickerPreferences = std::move(preferences);
    NotifyEditorSettingChanged(EditorSettingId::kColorPickerAdvancedOpen, previousAdvancedOpen,
                                  m_colorPickerPreferences.advancedOpen);
}

void Editor::SetExtendedPreferences(EditorExtendedPreferences preferences) noexcept {
    if (preferences.undoHistoryDepth > 0) {
        m_historyCapacity = static_cast<std::size_t>(std::clamp<std::int64_t>(preferences.undoHistoryDepth, 1, 4096));
        while (m_undoHistory.size() > m_historyCapacity) {
            m_undoHistory.pop_front();
        }
    }
    m_extendedPreferences = std::move(preferences);
}

void Editor::Shutdown() {
    StopBackgroundModelImportWorker();
    if (m_state == EditorState::Shutdown || m_state == EditorState::Uninitialized) {
        return;
    }
    static_cast<void>(CommitComponentPropertyPreview());
    // An open entity is closed without saving (nothing here can ask), so the scene is back in
    // place for whatever runs after the editor.
    if (m_entityEditSession.has_value()) {
        static_cast<void>(CloseEntityEditor(false));
    }
    CloseRetargetWindow();
    CloseMontageEditor();

    // An interactive session keeps its editor preferences (panels, snapping, grid, Inspector folds,
    // favourites) without the author having to remember "Save Editor Preferences" first. Headless
    // runs - tests, tools - have no UI and leave the stored preferences alone.
    if (m_uiInitialized) {
        static_cast<void>(SaveSessionSettings());
    }
    static_cast<void>(SaveSharedShelves());
    // Project settings and the input map changed in this session belong to the project, whatever
    // ran the editor.
    static_cast<void>(SaveProjectSettings());
    static_cast<void>(SaveInputMap());

    if (m_playModeState != EditorPlayModeState::Edit) {
        if (!StopPlayMode() && m_simulationControl != nullptr) {
            static_cast<void>(m_simulationControl->SetSimulationExecutionMode(
                Core::SimulationExecutionMode::Running));
            static_cast<void>(m_simulationControl->SetTransientSimulationSessionActive(false));
            m_playModeSession.reset();
            m_playModeState = EditorPlayModeState::Edit;
        }
    }
    if (m_uiInitialized) {
        ClearTextureThumbnailCache();
        ClearMeshThumbnailCache();
        ClearMaterialThumbnailCache();
        ClearVfxThumbnailCache();
        CloseVfxEditor();
        m_meshThumbnailRenderer.Shutdown();
        m_materialThumbnailRenderer.Shutdown();
        m_uiAssets.Shutdown();
        if (m_levelShell != nullptr) {
            m_levelShell->Shutdown();
            m_levelShell.reset();
        }
        m_uiInitialized = false;
    }

    ClearSelection();
    ClearHistory();
    m_state = EditorState::Shutdown;
}

bool Editor::IsDocumentEntity(const Scene::Entity entity) const noexcept {
    return entity != Scene::kInvalidEntity && m_services->GetEntityManager().IsAlive(entity);
}

bool Editor::HasSceneGraphObject(const Scene::Entity entity) const noexcept {
    if (!IsDocumentEntity(entity)) {
        return false;
    }

    const Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    return entityManager.HasComponent<Scene::TransformComponent>(entity) &&
           entityManager.HasComponent<Scene::HierarchyComponent>(entity) &&
           entityManager.HasComponent<Scene::WorldTransformComponent>(entity);
}

bool Editor::IsReparentableObject(const Scene::Entity entity) const noexcept {
    // A spatial object carries the full transform set; a pure Object (AnimationSequencer, the scene root)
    // carries none of it. Anything in between is a half-built entity and is refused.
    if (HasSceneGraphObject(entity)) {
        return true;
    }
    const Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    return IsHierarchyObject(entity) && !entityManager.HasComponent<Scene::TransformComponent>(entity) &&
           !entityManager.HasComponent<Scene::WorldTransformComponent>(entity);
}

bool Editor::IsHierarchyObject(const Scene::Entity entity) const noexcept {
    return IsDocumentEntity(entity) &&
           m_services->GetEntityManager().HasComponent<Scene::HierarchyComponent>(entity);
}

std::optional<Scene::WorldTransformComponent> Editor::TryGetComposingParentWorld(
    const Scene::Entity parent) const {
    // Identity, already resolved: what a child with no parent, or under a pure Object, composes from.
    const Scene::WorldTransformComponent identity{
        Math::Vector3{}, Math::Quaternion{}, Math::Vector3{1.0F, 1.0F, 1.0F}, false};
    if (parent == Scene::kInvalidEntity) {
        return identity;
    }
    if (!IsDocumentEntity(parent)) {
        return std::nullopt;
    }
    const Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    // Spatial means both halves, exactly as the scene graph's sweep decides it.
    if (!entityManager.HasComponent<Scene::TransformComponent>(parent) ||
        !entityManager.HasComponent<Scene::WorldTransformComponent>(parent)) {
        return identity;
    }
    return entityManager.GetComponent<Scene::WorldTransformComponent>(parent);
}

bool Editor::IsEntityNameValid(const std::string_view name) const noexcept {
    return !name.empty() && name.size() <= kMaximumEditorEntityNameBytes && !IsWhitespaceOnly(name);
}

std::string Editor::GetEntityDisplayLabel(const Scene::Entity entity) const {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (entityManager.IsAlive(entity) && entityManager.HasComponent<Scene::NameComponent>(entity)) {
        const std::string& name = entityManager.GetComponent<Scene::NameComponent>(entity).name;
        if (!name.empty()) {
            return name;
        }
    }
    return EntityLabel(entity);
}

std::string Editor::GetDefaultEntityName(const EditorEntityKind kind) const {
    // The names themselves live with each object kind's definition in Engine/Runtime/Objects/3D —
    // this legacy-kind mapper only picks which definition to ask, never authors a name itself.
    switch (kind) {
        case EditorEntityKind::Empty:
            return std::string{Scene::Object3DObjectDefinition::defaultName};
        case EditorEntityKind::Camera:
            return std::string{Scene::Camera3DObjectDefinition::defaultName};
        case EditorEntityKind::DirectionalLight:
            return std::string{Scene::Light3DObjectDefinition::defaultName};
        case EditorEntityKind::CollisionBox:
            return std::string{Scene::Collider3DObjectDefinition::defaultName};
        case EditorEntityKind::Cube:
            return std::string{Scene::BoxMesh3DObjectDefinition::defaultName};
        case EditorEntityKind::UVSphere:
            return std::string{Scene::SphereMesh3DObjectDefinition::defaultName};
        case EditorEntityKind::Plane:
            return std::string{Scene::PlaneMesh3DObjectDefinition::defaultName};
    }
    return {};
}

std::string Editor::MakeUniqueDocumentEntityName(const std::string_view baseName) const {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    std::vector<std::string> names;
    entityManager.ForEach<Scene::NameComponent>(
        [this, &names](const Scene::Entity entity, Scene::NameComponent& component) {
            if (IsDocumentEntity(entity)) {
                names.push_back(component.name);
            }
        });

    const auto isUsed = [&names](const std::string_view candidate) {
        return std::any_of(names.begin(), names.end(), [candidate](const std::string& name) {
            return name == candidate;
        });
    };
    if (!isUsed(baseName)) {
        return std::string{baseName};
    }

    for (std::size_t suffix = 2U;; ++suffix) {
        const std::string candidate = std::string{baseName} + " " + std::to_string(suffix);
        if (!isUsed(candidate)) {
            return candidate;
        }
    }
}

bool Editor::IsTransformFinite(const Scene::TransformComponent& transform) const noexcept {
    return IsFiniteVector(transform.localPosition) && IsFinite(transform.localRotation.x) &&
           IsFinite(transform.localRotation.y) && IsFinite(transform.localRotation.z) &&
           IsFinite(transform.localRotation.w) && IsFiniteVector(transform.localScale);
}

bool Editor::IsQuaternionFinite(const Math::Quaternion& quaternion) const noexcept {
    return Math::IsFinite(quaternion);
}

bool Editor::AreTransformSnappingSettingsValid(
    const EditorTransformSnappingSettings& settings) const noexcept {
    // Written as "inside the range" so NaN, which compares false, is refused too.
    const auto inRange = [](const float step, const float maximum) {
        return step >= kMinimumTransformSnapStep && step <= maximum;
    };
    return inRange(settings.translateStep, kMaximumTransformSnapTranslateStep) &&
           inRange(settings.rotateStepDegrees, kMaximumTransformSnapRotateStepDegrees) &&
           inRange(settings.scaleStep, kMaximumTransformSnapScaleStep);
}

float Editor::SnapScalar(const float value, const float increment) const noexcept {
    if (!IsFinite(value) || !IsFinite(increment) || increment <= kVectorEpsilon) {
        return value;
    }
    const float snapped = std::round(value / increment) * increment;
    return IsFinite(snapped) ? snapped : value;
}

bool Editor::IsFiniteVector(const Math::Vector3& vector) const noexcept {
    return IsFinite(vector.x) && IsFinite(vector.y) && IsFinite(vector.z);
}

Math::Vector3 Editor::GetAxisVector(const EditorTransformAxis axis) const noexcept {
    switch (axis) {
        case EditorTransformAxis::X:
            return Math::Vector3{1.0F, 0.0F, 0.0F};
        case EditorTransformAxis::Y:
            return Math::Vector3{0.0F, 1.0F, 0.0F};
        case EditorTransformAxis::Z:
            return Math::Vector3{0.0F, 0.0F, 1.0F};
        case EditorTransformAxis::None:
            return Math::Vector3{};
    }
    return Math::Vector3{};
}

bool Editor::ComputeLocalRotationForWorldAxis(const Scene::Entity entity,
                                                        const Math::Quaternion& initialLocalRotation,
                                                        const Math::Vector3& worldAxis, const float radians,
                                                        Math::Quaternion& outLocalRotation) const {
    if (!IsDocumentEntity(entity) || !IsQuaternionFinite(initialLocalRotation) ||
        !IsFinite(radians) || !IsFiniteVector(worldAxis) ||
        Math::LengthSquared(worldAxis) <= kVectorEpsilon) {
        return false;
    }

    Math::Quaternion initialNormalized{};
    Math::Quaternion worldDelta{};
    if (!Math::TryNormalize(initialLocalRotation, initialNormalized) ||
        !Math::TryMakeAxisAngle(worldAxis, radians, worldDelta)) {
        return false;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent<Scene::HierarchyComponent>(entity)) {
        return false;
    }

    const Scene::HierarchyComponent& hierarchy =
        entityManager.GetComponent<Scene::HierarchyComponent>(entity);
    Math::Quaternion localDelta = worldDelta;
    if (hierarchy.parent != Scene::kInvalidEntity) {
        const std::optional<Scene::WorldTransformComponent> composingParent =
            TryGetComposingParentWorld(hierarchy.parent);
        if (!composingParent.has_value()) {
            return false;
        }

        const Scene::WorldTransformComponent& parentWorld = *composingParent;
        if (parentWorld.dirty) {
            return false;
        }
        // One Trs value op conjugates the world-axis delta into the parent frame -
        // byte-identical to the quaternion sandwich it replaces (see
        // TryInverseTransformRotation).
        const Math::Trs parentTrs{
            parentWorld.worldPosition, parentWorld.worldRotation, parentWorld.worldScale};
        if (!Math::TryInverseTransformRotation(parentTrs, worldDelta, localDelta)) {
            return false;
        }
    }

    return Math::TryNormalize(Math::Multiply(localDelta, initialNormalized), outLocalRotation);
}

bool Editor::ComputeLocalDeltaForWorldDelta(const Scene::Entity entity,
                                                   const Math::Vector3& worldDelta,
                                                   Math::Vector3& outLocalDelta) const {
    if (!IsDocumentEntity(entity) || !IsFiniteVector(worldDelta)) {
        return false;
    }

    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent<Scene::HierarchyComponent>(entity)) {
        return false;
    }

    const Scene::HierarchyComponent& hierarchy =
        entityManager.GetComponent<Scene::HierarchyComponent>(entity);
    if (hierarchy.parent == Scene::kInvalidEntity) {
        outLocalDelta = worldDelta;
        return true;
    }

    const std::optional<Scene::WorldTransformComponent> composingParent =
        TryGetComposingParentWorld(hierarchy.parent);
    if (!composingParent.has_value()) {
        return false;
    }

    const Scene::WorldTransformComponent& parentWorld = *composingParent;
    if (parentWorld.dirty || !IsFiniteVector(parentWorld.worldScale) ||
        std::abs(parentWorld.worldScale.x) <= kVectorEpsilon ||
        std::abs(parentWorld.worldScale.y) <= kVectorEpsilon ||
        std::abs(parentWorld.worldScale.z) <= kVectorEpsilon) {
        return false;
    }

    const Math::Trs parentTrs{
        parentWorld.worldPosition, parentWorld.worldRotation, parentWorld.worldScale};
    if (!Math::TryInverseTransformDirection(parentTrs, worldDelta, outLocalDelta)) {
        return false;
    }
    return IsFiniteVector(outLocalDelta);
}

void Editor::DestroyDocumentSubtree(const Scene::Entity root) {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const std::vector<Scene::Entity> children =
        m_services->GetSceneGraph().GetChildren(entityManager, root);
    for (const Scene::Entity child : children) {
        DestroyDocumentSubtree(child);
    }
    if (entityManager.IsAlive(root)) {
        entityManager.DestroyEntity(root);
    }
}

void Editor::ClearDocumentScene() {
    const std::vector<Scene::Entity> roots = GetDocumentRoots();
    for (const Scene::Entity root : roots) {
        DestroyDocumentSubtree(root);
    }
    ClearSelection();
}

void Editor::ApplyLayoutPreset(const EditorLayoutPreset preset) noexcept {
    switch (preset) {
        case EditorLayoutPreset::Default:
            m_scenePanelVisible = true;
            m_inspectorPanelVisible = true;
            m_bottomDockVisible = true;
            m_activeRightPanelTab = EditorRightPanelTab::Inspector;
            m_activeBottomDock = EditorBottomDock::FileSystem;
            // The panel widths go back to their defaults too - "Default Layout" that kept a
            // half-collapsed Outliner would not be the default.
            m_scenePanelWidth = -1.0F;
            m_inspectorPanelWidth = -1.0F;
            m_bottomDockHeight = kAssetsPanelHeight;
            break;
        case EditorLayoutPreset::FocusViewport:
            m_scenePanelVisible = false;
            m_inspectorPanelVisible = false;
            m_bottomDockVisible = false;
            break;
        case EditorLayoutPreset::ContentReview:
            m_scenePanelVisible = true;
            m_inspectorPanelVisible = true;
            m_bottomDockVisible = true;
            m_activeRightPanelTab = EditorRightPanelTab::Import;
            m_activeBottomDock = EditorBottomDock::FileSystem;
            break;
    }
}

void Editor::LoadSessionSettings() {
    Config::IConfigManager& config = m_services->GetConfigManager();
    constexpr std::int64_t kSessionVersion = 1;
    const std::int64_t version = config.GetInt("editor.sessionSettingsVersion", 0);
    if (version > kSessionVersion) {
        return;
    }
    // Aliases marked Deprecated move their legacy values to the replacement ids before the editor
    // reads the current descriptors; legal values already at the new ids win.
    static_cast<void>(m_settingsRegistry.MigrateDeprecatedValues(config));

    // Inspector folds used to be stored as objects (`<id>.<index>.key/.open`). Convert that
    // legacy shape once to the StringList descriptor's ordered `key`/state records before the
    // registry reads it; current list entries are leaf strings at `<id>.<index>`.
    const std::string foldId{EditorSettingId::kInspectorFolds};
    const std::int64_t legacyFoldCount = std::clamp(
        config.GetInt(foldId + ".count", 0), std::int64_t{0},
        static_cast<std::int64_t>(kMaxRememberedInspectorFolds));
    bool hasLegacyFoldShape = legacyFoldCount > 0 && !config.HasKey(foldId + ".0");
    for (std::size_t index = 0U; index < kMaxRememberedInspectorFolds && !hasLegacyFoldShape; ++index) {
        const std::string prefix = foldId + "." + std::to_string(index);
        hasLegacyFoldShape = config.HasKey(prefix + ".key") || config.HasKey(prefix + ".open");
    }
    if (hasLegacyFoldShape) {
        Config::SettingStringList encodedFolds;
        encodedFolds.reserve(static_cast<std::size_t>(legacyFoldCount));
        for (std::int64_t index = 0; index < legacyFoldCount; ++index) {
            const std::string prefix = foldId + "." + std::to_string(index) + ".";
            const std::string key = config.GetString(prefix + "key", "");
            if (!key.empty()) {
                encodedFolds.push_back(std::string{config.GetBool(prefix + "open", true) ? "1:" : "0:"} + key);
            }
        }
        for (std::size_t index = 0U; index < kMaxRememberedInspectorFolds; ++index) {
            const std::string prefix = foldId + "." + std::to_string(index);
            static_cast<void>(config.RemoveKey(prefix + ".key"));
            static_cast<void>(config.RemoveKey(prefix + ".open"));
        }
        static_cast<void>(m_settingsRegistry.SetValue(config, foldId, encodedFolds));
    }

    // Every value read through the registry is legal for its setting - anything missing, mistyped
    // or out of range comes back as that one setting's default - so each binding applies it.
    for (const Config::SettingDescriptor* descriptor : m_settingsRegistry.GetAll()) {
        // A renamed setting's old name is an alias for the migration above only: its value was
        // just moved to the new name, and applying it again would overwrite what the new name
        // already holds.
        if (descriptor->HasFlag(Config::kSettingFlagDeprecated)) {
            continue;
        }
        if (const std::optional<Config::SettingValue> value = m_settingsRegistry.GetValue(config, descriptor->id)) {
            static_cast<void>(SetEditorSetting(descriptor->id, *value));
        }
    }
    // The host owns the viewport's axis defaults. Adopt a complete stored palette only; otherwise
    // leave the current state untouched. A new editor starts unset so the host can seed it, while
    // an already-seeded host palette must not be replaced by missing or invalid stored data.
    constexpr std::string_view kLegacyAxisPaletteSetKey = "editor.viewport.axisColors.set";
    const bool hasLegacyAxisMarker = config.HasKey(kLegacyAxisPaletteSetKey);
    const bool shouldRestoreAxisPalette = hasLegacyAxisMarker
                                              ? config.GetBool(kLegacyAxisPaletteSetKey, false)
                                              : true;
    if (shouldRestoreAxisPalette) {
        const std::optional<Config::SettingValue> x =
            m_settingsRegistry.GetStoredValue(config, EditorSettingId::kViewportAxisColorX);
        const std::optional<Config::SettingValue> y =
            m_settingsRegistry.GetStoredValue(config, EditorSettingId::kViewportAxisColorY);
        const std::optional<Config::SettingValue> z =
            m_settingsRegistry.GetStoredValue(config, EditorSettingId::kViewportAxisColorZ);
        if (x && y && z) {
            const auto toAxisColor = [](const Config::SettingValue& value) {
                const Config::SettingColor& color = std::get<Config::SettingColor>(value);
                return ViewportAxisColor{color.r, color.g, color.b};
            };
            static_cast<void>(SetViewportAxisColors(toAxisColor(*x), toAxisColor(*y), toAxisColor(*z)));
        }
    } else {
        static_cast<void>(m_settingsRegistry.ClearValue(config, EditorSettingId::kViewportAxisColorX));
        static_cast<void>(m_settingsRegistry.ClearValue(config, EditorSettingId::kViewportAxisColorY));
        static_cast<void>(m_settingsRegistry.ClearValue(config, EditorSettingId::kViewportAxisColorZ));
    }
    if (hasLegacyAxisMarker) {
        static_cast<void>(config.RemoveKey(kLegacyAxisPaletteSetKey));
    }

    // Recent "+ Add" items; an id the catalogue no longer has is dropped rather than kept.
    m_contentCreateRecent.clear();
    const std::int64_t recentCount = std::clamp(config.GetInt("editor.content.recent.count", 0), std::int64_t{0},
                                                std::int64_t{5});
    for (std::int64_t index = recentCount - 1; index >= 0; --index) {
        const std::string id = config.GetString("editor.content.recent." + std::to_string(index), "");
        if (FindContentCatalogueItem(id) != nullptr) {
            PushContentCreateRecent(m_contentCreateRecent, id);
        }
    }
    // Personal shelves: a name and its items each, bounded like the pins. The team's shelves come
    // from the project (LoadSharedShelves) and are left alone here; a personal shelf whose name
    // a team shelf has is kept under a new name rather than dropped.
    m_contentShelves.RemoveAll(false);
    const std::int64_t shelfCount =
        std::clamp(config.GetInt("editor.shelves.count", 0), std::int64_t{0},
                   static_cast<std::int64_t>(ContentShelves::kMaxShelves));
    for (std::int64_t index = 0; index < shelfCount; ++index) {
        const std::string prefix = "editor.shelves." + std::to_string(index) + ".";
        const std::string storedName = config.GetString(prefix + "name", "");
        const std::string name = storedName.empty() ? std::string{} : m_contentShelves.Create(storedName);
        if (name.empty()) {
            continue;
        }
        const std::int64_t itemCount =
            std::clamp(config.GetInt(prefix + "items.count", 0), std::int64_t{0},
                       static_cast<std::int64_t>(ContentShelves::kMaxItemsPerShelf));
        for (std::int64_t item = 0; item < itemCount; ++item) {
            const std::string stored = config.GetString(prefix + "items." + std::to_string(item), "");
            if (!stored.empty()) {
                static_cast<void>(m_contentShelves.AddItem(name, stored));
            }
        }
    }
}

bool Editor::SaveSessionSettings() {
    if (m_state != EditorState::Running ||
        !AreTransformSnappingSettingsValid(m_transformSnappingSettings)) {
        return false;
    }
    Config::IConfigManager& config = m_services->GetConfigManager();
    config.SetInt("editor.sessionSettingsVersion", 1);
    // Each value was accepted by a setter whose range is the one its setting declares, so the
    // registry takes it. The exception is the Game workspace, which a session is never restored
    // into: that write is refused and the last restorable workspace stays stored.
    for (const Config::SettingDescriptor* descriptor : m_settingsRegistry.GetAll()) {
        if (const std::optional<Config::SettingValue> value = GetEditorSetting(descriptor->id)) {
            static_cast<void>(m_settingsRegistry.SetValue(config, descriptor->id, *value));
        }
    }
    // The axis palette has no editor-owned default. Store all three descriptor values when the
    // host has seeded or restored a complete palette; absence means "ask the host for its defaults".
    if (m_viewportOverlayState.axisColorsValid) {
        const auto toSettingColor = [](const ViewportAxisColor color) {
            return Config::SettingColor{color.r, color.g, color.b};
        };
        static_cast<void>(m_settingsRegistry.SetValue(
            config, EditorSettingId::kViewportAxisColorX, toSettingColor(m_viewportOverlayState.axisColorX)));
        static_cast<void>(m_settingsRegistry.SetValue(
            config, EditorSettingId::kViewportAxisColorY, toSettingColor(m_viewportOverlayState.axisColorY)));
        static_cast<void>(m_settingsRegistry.SetValue(
            config, EditorSettingId::kViewportAxisColorZ, toSettingColor(m_viewportOverlayState.axisColorZ)));
    } else {
        static_cast<void>(m_settingsRegistry.ClearValue(config, EditorSettingId::kViewportAxisColorX));
        static_cast<void>(m_settingsRegistry.ClearValue(config, EditorSettingId::kViewportAxisColorY));
        static_cast<void>(m_settingsRegistry.ClearValue(config, EditorSettingId::kViewportAxisColorZ));
    }
    static_cast<void>(config.RemoveKey("editor.viewport.axisColors.set"));
    config.SetInt("editor.content.recent.count", static_cast<std::int64_t>(m_contentCreateRecent.size()));
    for (std::size_t index = 0U; index < m_contentCreateRecent.size(); ++index) {
        config.SetString("editor.content.recent." + std::to_string(index), m_contentCreateRecent[index]);
    }
    std::vector<ContentShelf> shelves;
    std::ranges::copy_if(m_contentShelves.GetAll(), std::back_inserter(shelves),
                         [](const ContentShelf& shelf) { return !shelf.shared; });
    config.SetInt("editor.shelves.count", static_cast<std::int64_t>(shelves.size()));
    for (std::size_t index = 0U; index < shelves.size(); ++index) {
        const std::string prefix = "editor.shelves." + std::to_string(index) + ".";
        config.SetString(prefix + "name", shelves[index].name);
        config.SetInt(prefix + "items.count", static_cast<std::int64_t>(shelves[index].items.size()));
        for (std::size_t item = 0U; item < shelves[index].items.size(); ++item) {
            config.SetString(prefix + "items." + std::to_string(item), shelves[index].items[item].generic_string());
        }
    }
    return config.Save();
}

bool Editor::IsHierarchyFilterActive() const noexcept {
    return !m_hierarchyFilter.empty();
}

bool Editor::IsHierarchyEntityVisible(const Scene::Entity entity) const {
    return !IsHierarchyFilterActive() ||
           std::find(m_cachedHierarchyVisibleEntities.begin(), m_cachedHierarchyVisibleEntities.end(), entity) !=
               m_cachedHierarchyVisibleEntities.end();
}

bool Editor::IsSealedHierarchyRow(const Scene::Entity entity) const noexcept {
    if (m_entityDocumentActive || !IsDocumentEntity(entity)) {
        return false;
    }
    const Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent<Scene::PrefabInstanceComponent>(entity)) {
        return false;
    }
    // A record that names no asset is not an instance of anything - a half-restored entity, or one
    // whose source was never registered. It owns its children like any other object.
    return entityManager.GetComponent<Scene::PrefabInstanceComponent>(entity).sourcePrefabGuid !=
           Asset::kInvalidAssetGuid;
}

Scene::Entity Editor::ResolveHierarchyRowOwner(const Scene::Entity entity) const noexcept {
    if (!IsDocumentEntity(entity)) {
        return entity;
    }
    Scene::Entity cursor = entity;
    Scene::Entity parent = Scene::kInvalidEntity;
    // Bounded by the entity count so a malformed parent link cannot hang selection. The nearest
    // sealed ancestor wins: a placed entity inside another placed entity belongs to the inner one.
    for (std::size_t guard = 0U; guard < 4096U && TryGetDocumentParent(cursor, parent) &&
                                 parent != Scene::kInvalidEntity;
         ++guard) {
        if (IsSealedHierarchyRow(parent)) {
            return parent;
        }
        cursor = parent;
    }
    return entity;
}

void Editor::InvalidateHierarchyFilterCache() noexcept {
    m_hierarchyFilterCacheDirty = true;
}

void Editor::CancelHierarchyRename() noexcept {
    m_hierarchyRenameEntity = Scene::kInvalidEntity;
    m_hierarchyRenameBuffer.clear();
    m_hierarchyRenameFocusRequested = false;
}

void Editor::RebuildHierarchyFilterCache() {
    if (!m_hierarchyFilterCacheDirty && m_cachedHierarchyFilter == m_hierarchyFilter) {
        return;
    }
    m_cachedHierarchyVisibleEntities.clear();
    m_cachedHierarchyFilter = m_hierarchyFilter;
    m_hierarchyFilterCacheDirty = false;
    if (!IsHierarchyFilterActive()) {
        return;
    }
    const auto visit = [this](const auto& self, const Scene::Entity entity) -> bool {
        if (!IsDocumentEntity(entity)) {
            return false;
        }
        const std::string displayLabel = GetEntityDisplayLabel(entity);
        const std::string typeTag = GetOutlinerTypeTag(entity);
        const bool hasTypeQuery = m_hierarchyFilter.rfind("type:", 0U) == 0U;
        const std::string_view typeQuery = hasTypeQuery ? std::string_view{m_hierarchyFilter}.substr(5U) : std::string_view{};
        // The object's type ("Static3D") or its older specialised tag ("Collision Box") both count.
        const bool typeMatches = !hasTypeQuery || ContainsCaseInsensitive(typeTag, typeQuery) ||
                                 ContainsCaseInsensitive(GetObjectTypeName(entity), typeQuery);
        Scene::Entity parent = Scene::kInvalidEntity;
        const bool isRoot = !TryGetDocumentParent(entity, parent) || parent == Scene::kInvalidEntity;
        const bool nameMatches = hasTypeQuery || ContainsCaseInsensitive(displayLabel, m_hierarchyFilter) ||
                                 (m_hierarchyFilter == "root" && isRoot);
        bool visible = typeMatches && nameMatches;
        Scene::IEntityManager& entityManager = m_services->GetEntityManager();
        // A sealed row shows none of its children, so a search must not reveal it because one of
        // them matched: the match would open a row that still draws nothing.
        if (!IsSealedHierarchyRow(entity)) {
            for (const Scene::Entity child : m_services->GetSceneGraph().GetChildren(entityManager, entity)) {
                visible = self(self, child) || visible;
            }
        }
        if (visible) {
            m_cachedHierarchyVisibleEntities.push_back(entity);
        }
        return visible;
    };
    for (const Scene::Entity root : GetDocumentRoots()) {
        static_cast<void>(visit(visit, root));
    }
}

Editor::AssetTableItemType Editor::ClassifyAssetTableEntry(
    const Asset::ProjectFileEntry& entry) {
    if (entry.kind == Asset::ProjectFileEntryKind::Directory) {
        return AssetTableItemType::Folder;
    }

    std::string extension = entry.relativePath.extension().generic_string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (extension == ".uvscene") {
        return AssetTableItemType::Scene;
    }
    if (extension == ".uvprefab") {
        return AssetTableItemType::Prefab;
    }
    if (extension == ".uventity") {
        return AssetTableItemType::Entity;
    }
    if (extension == ".uvbundle") {
        return AssetTableItemType::Bundle;
    }
    // Model sources are shown as what they are - a mesh - and imported automatically behind the
    // scenes (see QueueModelAutoImports); a rigged one is relabelled Model by the caller, which
    // knows the file's contents.
    if (extension == ".uvmodel" || IsModelSourcePath(entry.relativePath)) {
        return AssetTableItemType::Mesh;
    }
    if (extension == ".uvtex") {
        return AssetTableItemType::Texture;
    }
    // Raw, not-yet-imported source images. Godot-style engines preview these directly rather than
    // requiring an import step first; this repo already has standalone decoders for all four
    // (uve/asset/{png,jpeg,bmp,tga}_metadata_uve.h) that GetTextureThumbnail() falls back to
    // when the file isn't a `.uvtex` envelope. Reusing Texture rather than adding a new enum value
    // since both content-browser call sites already dispatch thumbnails on this exact type.
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" ||
        extension == ".tga") {
        return AssetTableItemType::Texture;
    }
    if (extension == ".uvshader") {
        return AssetTableItemType::Shader;
    }
    // Both spellings: every importer writes `.uvmat`, while the Inspector's own "New Material"
    // flow creates `.uvmaterial`. A material the editor itself just created must still classify as
    // one here or it would get a generic File icon and never a material preview.
    if (extension == ".uvmat" || extension == ".uvmaterial") {
        return AssetTableItemType::Material;
    }
    if (extension == ".uvsave") {
        return AssetTableItemType::Save;
    }
    if (extension == ".uvbp") {
        return AssetTableItemType::Blueprint;
    }
    if (extension == ".uvwbp") {
        return AssetTableItemType::WidgetBlueprint;
    }
    if (extension == ".uvs") {
        return AssetTableItemType::Script;
    }
    // A clip (a take imported from an FBX, or authored): the same Animation an animation-only
    // model source shows as.
    if (extension == ".uvanim") {
        return AssetTableItemType::Animation;
    }
    // A library of clips: its own browser type so it opens in the Animation Library window.
    if (extension == ".uvanimlib") {
        return AssetTableItemType::AnimationLibrary;
    }
    if (extension == ".uvfx") {
        return AssetTableItemType::ParticleSystem;
    }
    if (extension == ".uvmontage") {
        return AssetTableItemType::MotionMontage;
    }
    if (extension == ".uvmontagelib") {
        return AssetTableItemType::MontageLibrary;
    }
    // The envelope the audio importer writes, and every source format an importer can turn into
    // one: WAV, plus the compressed Ogg/Vorbis, MP3 and FLAC streams RegisterCompressedAudioImporters
    // reads (compressed_audio_importer_uve.h). A container with no registered importer - .m4a, .aac,
    // .wma - deliberately stays a plain File: showing it as audio would promise playback the engine
    // cannot give it.
    if (extension == ".uvaudio" || extension == ".wav" || extension == ".ogg" || extension == ".mp3" ||
        extension == ".flac") {
        return AssetTableItemType::Audio;
    }
    if (extension == ".ttf" || extension == ".otf") {
        return AssetTableItemType::Font;
    }
    return AssetTableItemType::File;
}

Editor::AssetTableItemType Editor::GetAssetTableEntryType(
    const Asset::ProjectFileEntry& entry) const {
    const AssetTableItemType extensionType = ClassifyAssetTableEntry(entry);
    if (entry.kind != Asset::ProjectFileEntryKind::File || !IsModelSourcePath(entry.relativePath)) {
        return extensionType;
    }
    const EditorModelSourceInfo* const source = FindModelSourceInfo(entry.relativePath);
    if (source == nullptr) {
        return extensionType;
    }
    if (source->animationOnly) {
        return AssetTableItemType::Animation;
    }
    if (source->rigged) {
        return AssetTableItemType::Model;
    }
    return extensionType;
}

const char* Editor::GetAssetTableItemTypeLabel(const AssetTableItemType type) noexcept {
    switch (type) {
        case AssetTableItemType::Folder:
            return "Folder";
        case AssetTableItemType::Scene:
            return "Viewport3D"; // a level: it opens as the Viewport3D in the Outliner
        case AssetTableItemType::Prefab:
            return "Prefab";
        case AssetTableItemType::Entity:
            return "Entity";
        case AssetTableItemType::Bundle:
            return "Bundle";
        case AssetTableItemType::Mesh:
            return "Mesh";
        case AssetTableItemType::Model:
            return "Model";
        case AssetTableItemType::Texture:
            return "Texture";
        case AssetTableItemType::Shader:
            return "Shader";
        case AssetTableItemType::Material:
            return "Material";
        case AssetTableItemType::Save:
            return "Save";
        case AssetTableItemType::Animation:
            return "Animation";
        case AssetTableItemType::AnimationLibrary:
            return "Animation Library";
        case AssetTableItemType::ParticleSystem:
            return "Particle System";
        case AssetTableItemType::MotionMontage:
            return "Motion Montage";
        case AssetTableItemType::MontageLibrary:
            return "Motion Montage Library";
        case AssetTableItemType::Blueprint:
            return "Blueprint";
        case AssetTableItemType::WidgetBlueprint:
            return "Widget Blueprint";
        case AssetTableItemType::Script:
            return "Script";
        case AssetTableItemType::Audio:
            return "Audio";
        case AssetTableItemType::Font:
            return "Font";
        case AssetTableItemType::File:
            return "File";
    }
    return "File";
}

const char* Editor::GetAssetTableFocusLabel(const AssetTableTypeFocus focus) noexcept {
    switch (focus) {
        case AssetTableTypeFocus::All:
            return "All";
        case AssetTableTypeFocus::Folders:
            return "Folders";
        case AssetTableTypeFocus::Scene:
            return "Scene";
        case AssetTableTypeFocus::Prefab:
            return "Prefab";
        case AssetTableTypeFocus::Bundle:
            return "Bundle";
        case AssetTableTypeFocus::Mesh:
            return "Mesh";
        case AssetTableTypeFocus::Texture:
            return "Texture";
        case AssetTableTypeFocus::Shader:
            return "Shader";
        case AssetTableTypeFocus::Material:
            return "Material";
        case AssetTableTypeFocus::Save:
            return "Save";
        case AssetTableTypeFocus::Registered:
            return "Registered";
        case AssetTableTypeFocus::OtherFiles:
            return "Other Files";
    }
    return "All";
}

bool Editor::DoesAssetTableEntryMatchFocus(const Asset::ProjectFileEntry& entry) const {
    const AssetTableItemType type = GetAssetTableEntryType(entry);
    switch (m_assetTableTypeFocus) {
        case AssetTableTypeFocus::All:
            return true;
        case AssetTableTypeFocus::Folders:
            return type == AssetTableItemType::Folder;
        case AssetTableTypeFocus::Scene:
            return type == AssetTableItemType::Scene;
        case AssetTableTypeFocus::Prefab:
            return type == AssetTableItemType::Prefab || type == AssetTableItemType::Entity;
        case AssetTableTypeFocus::Bundle:
            return type == AssetTableItemType::Bundle;
        case AssetTableTypeFocus::Mesh:
            return type == AssetTableItemType::Mesh || type == AssetTableItemType::Model;
        case AssetTableTypeFocus::Texture:
            return type == AssetTableItemType::Texture;
        case AssetTableTypeFocus::Shader:
            return type == AssetTableItemType::Shader;
        case AssetTableTypeFocus::Material:
            return type == AssetTableItemType::Material;
        case AssetTableTypeFocus::Save:
            return type == AssetTableItemType::Save;
        case AssetTableTypeFocus::Registered:
            return entry.kind == Asset::ProjectFileEntryKind::File && entry.registeredAssetGuid.has_value();
        case AssetTableTypeFocus::OtherFiles:
            return type == AssetTableItemType::File;
    }
    return false;
}

bool Editor::IsAssetTableDirectoryInSnapshot(const Asset::ProjectFileSnapshot& snapshot,
                                                        const std::filesystem::path& directory) const {
    if (directory.empty()) {
        return true;
    }
    return std::any_of(snapshot.entries.begin(), snapshot.entries.end(), [&directory](const Asset::ProjectFileEntry& entry) {
        return entry.kind == Asset::ProjectFileEntryKind::Directory && entry.relativePath == directory;
    });
}

void Editor::ReconcileAssetTableDirectory(const Asset::ProjectFileSnapshot& snapshot) noexcept {
    if (!IsAssetTableDirectoryInSnapshot(snapshot, m_assetTableDirectory)) {
        m_assetTableDirectory.clear();
    }
}

bool Editor::IsProjectPathFavorited(const std::filesystem::path& relativePath) const {
    return std::find(m_favoriteProjectPaths.begin(), m_favoriteProjectPaths.end(), relativePath) !=
           m_favoriteProjectPaths.end();
}

std::filesystem::path Editor::GetSharedShelvesPath() const {
    return m_services->GetProjectSettings().GetPath().parent_path() / "project.uvshelves";
}

void Editor::LoadSharedShelves() {
    const std::filesystem::path path = GetSharedShelvesPath();
    std::error_code error;
    const auto writeTime = std::filesystem::last_write_time(path, error);
    if (error) {
        m_contentShelves.RemoveAll(true); // no file: the team has no shelves
        m_sharedShelvesWriteTime.reset();
        return;
    }
    m_sharedShelvesWriteTime = writeTime;
    std::ifstream file(path, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    std::string problem;
    if (!ReadSharedShelvesText(text, m_contentShelves, problem)) {
        m_contentStatusMessage = "Could not read the team's shelves (" + path.filename().string() + "): " + problem + ".";
    }
}

bool Editor::SaveSharedShelves() {
    const std::filesystem::path path = GetSharedShelvesPath();
    std::error_code error;
    const bool anyShared = std::ranges::any_of(m_contentShelves.GetAll(), &ContentShelf::shared);
    // A project that never had a team shelf does not get an empty file.
    if (!anyShared && !std::filesystem::exists(path, error)) {
        return true;
    }
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << WriteSharedShelvesText(m_contentShelves);
        if (!file) {
            m_contentStatusMessage = "Could not save the team's shelves to " + path.filename().string() + ".";
            return false;
        }
    }
    const auto writeTime = std::filesystem::last_write_time(path, error);
    m_sharedShelvesWriteTime = error ? std::nullopt : std::optional{writeTime};
    return true;
}

void Editor::ReloadSharedShelvesIfChanged() {
    std::error_code error;
    const auto writeTime = std::filesystem::last_write_time(GetSharedShelvesPath(), error);
    const std::optional<std::filesystem::file_time_type> now = error ? std::nullopt : std::optional{writeTime};
    if (now != m_sharedShelvesWriteTime) {
        LoadSharedShelves();
    }
}

ContentFileFacts Editor::GetContentFileFacts(const std::filesystem::path& contentRoot,
                                                                 const Asset::ProjectFileEntry& entry,
                                                                 const std::uint64_t refreshGeneration) {
    if (refreshGeneration != m_contentFileFactsGeneration) {
        m_contentFileFacts.clear();
        m_contentFileFactsGeneration = refreshGeneration;
    }
    const std::string key = entry.relativePath.generic_string();
    if (const auto it = m_contentFileFacts.find(key); it != m_contentFileFacts.end()) {
        return it->second;
    }
    ContentFileFacts facts;
    std::error_code error;
    const std::filesystem::path absolute = contentRoot / entry.relativePath;
    if (entry.kind == Asset::ProjectFileEntryKind::File) {
        const std::uintmax_t size = std::filesystem::file_size(absolute, error);
        facts.size = error ? 0U : size;
    }
    const std::filesystem::file_time_type written = std::filesystem::last_write_time(absolute, error);
    if (!error) {
        facts.modified = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::file_clock::to_sys(written).time_since_epoch())
                             .count();
    }
    m_contentFileFacts.emplace(key, facts);
    return facts;
}

void Editor::ToggleProjectPathFavorite(const std::filesystem::path& relativePath) {
    const auto it = std::find(m_favoriteProjectPaths.begin(), m_favoriteProjectPaths.end(), relativePath);
    if (it != m_favoriteProjectPaths.end()) {
        m_favoriteProjectPaths.erase(it);
    } else {
        m_favoriteProjectPaths.push_back(relativePath);
    }
}

namespace {

// Reads `absolutePath` and decodes it as a raw, not-yet-imported source image using this engine's
// own standalone codec primitives (the same decoders the real import pipeline uses, called directly
// rather than through the full AssetImporter registry/metadata-sidecar machinery, since a
// thumbnail only ever needs pixels). Returns false for an unrecognized extension or malformed file -
// each decoder already bounds/validates its own input, so no extra size/sanity checks are needed here.
bool DecodeRawImageThumbnailPixels(const std::filesystem::path& absolutePath, std::uint32_t& outWidth,
                                       std::uint32_t& outHeight, std::vector<std::byte>& outPixels) {
    std::ifstream file(absolutePath, std::ios::binary);
    if (!file) {
        return false;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff fileSize = file.tellg();
    if (fileSize <= 0) {
        return false;
    }
    file.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), fileSize)) {
        return false;
    }

    std::string extension = absolutePath.extension().generic_string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });

    if (extension == ".png") {
        Asset::PngRgba8Image image;
        if (!Asset::DecodePngRgba8Image(bytes, image)) return false;
        outWidth = image.width; outHeight = image.height; outPixels = std::move(image.pixels);
        return true;
    }
    if (extension == ".jpg" || extension == ".jpeg") {
        Asset::JpegRgba8Image image;
        if (!Asset::DecodeJpegRgba8Image(bytes, image)) return false;
        outWidth = image.width; outHeight = image.height; outPixels = std::move(image.pixels);
        return true;
    }
    if (extension == ".bmp") {
        Asset::BmpRgba8Image image;
        if (!Asset::DecodeBmpRgba8Image(bytes, image)) return false;
        outWidth = image.width; outHeight = image.height; outPixels = std::move(image.pixels);
        return true;
    }
    if (extension == ".tga") {
        Asset::TgaRgba8Image image;
        if (!Asset::DecodeTgaRgba8Image(bytes, image)) return false;
        outWidth = image.width; outHeight = image.height; outPixels = std::move(image.pixels);
        return true;
    }
    return false;
}

/// Decodes one texture a material references into the RGBA8 bytes a material preview samples,
/// reusing the exact lookup GetTextureThumbnail uses: a `.uvtex` asset when the file is one,
/// otherwise a raw PNG/JPEG/BMP/TGA source (the formats the material inspector's texture picker
/// accepts, which stay un-imported until something needs them). `rawImageSrgb` is the colour space
/// assumed only for those raw sources; a `.uvtex` carries its own. A missing, unreadable or
/// unsupported file simply leaves `outImage` invalid, and the preview falls back to the material's
/// flat value for that slot - a preview must never hide the rest of a material because one map is
/// broken.
[[nodiscard]] bool LoadMaterialPreviewImage(const std::filesystem::path& absolutePath, const bool rawImageSrgb,
                                               MaterialThumbnailImage& outImage) {
    std::error_code error;
    if (absolutePath.empty() || !std::filesystem::is_regular_file(absolutePath, error)) {
        return false;
    }
    // Every supported format's decoder refuses images far smaller than this; the cap exists only so
    // the int narrowing below is provably safe.
    constexpr std::uint32_t kMaximumPreviewImageDimension = 32768U;
    Asset::TextureAsset texture;
    if (!IsRawSourceImagePath(absolutePath) && Asset::LoadTextureAsset(absolutePath, texture)) {
        if (texture.format != Asset::TextureAssetFormat::RGBA8Unorm || texture.width == 0U ||
            texture.height == 0U || texture.width > kMaximumPreviewImageDimension ||
            texture.height > kMaximumPreviewImageDimension || texture.pixels.empty()) {
            return false;
        }
        outImage.width = static_cast<int>(texture.width);
        outImage.height = static_cast<int>(texture.height);
        outImage.rgba8.resize(texture.pixels.size());
        std::memcpy(outImage.rgba8.data(), texture.pixels.data(), texture.pixels.size());
        outImage.srgb = texture.colorSpace == Asset::TextureAssetColorSpace::Srgb;
        return outImage.IsValid();
    }
    std::uint32_t rawWidth = 0U;
    std::uint32_t rawHeight = 0U;
    std::vector<std::byte> rawPixels;
    if (!DecodeRawImageThumbnailPixels(absolutePath, rawWidth, rawHeight, rawPixels) || rawWidth == 0U ||
        rawHeight == 0U || rawWidth > kMaximumPreviewImageDimension ||
        rawHeight > kMaximumPreviewImageDimension || rawPixels.empty()) {
        return false;
    }
    outImage.width = static_cast<int>(rawWidth);
    outImage.height = static_cast<int>(rawHeight);
    outImage.rgba8.resize(rawPixels.size());
    std::memcpy(outImage.rgba8.data(), rawPixels.data(), rawPixels.size());
    outImage.srgb = rawImageSrgb;
    return outImage.IsValid();
}

[[nodiscard]] std::filesystem::path ResolveThumbnailAssetPath(const std::filesystem::path& contentRoot,
                                                                 const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) {
        return path;
    }
    std::error_code error;
    const std::filesystem::path underRoot = contentRoot / path;
    if (std::filesystem::is_regular_file(underRoot, error)) {
        return underRoot;
    }
    error.clear();
    if (std::filesystem::is_regular_file(path, error)) {
        return path;
    }
    return underRoot;
}

} // namespace

std::uintptr_t Editor::GetTextureThumbnail(const std::filesystem::path& relativePath) {
    const std::string cacheKey = relativePath.generic_string();
    const auto cachedIt = m_textureThumbnailCache.find(cacheKey);
    if (cachedIt != m_textureThumbnailCache.end()) {
        return cachedIt->second;
    }
    const Asset::ProjectFileSnapshot snapshot = m_services->GetProjectFileIndex().GetSnapshot();
    const std::filesystem::path absolutePath = ResolveThumbnailAssetPath(snapshot.contentRoot, relativePath);
    std::uintptr_t textureId = 0U;
    Asset::TextureAsset texture;
    if (!IsRawSourceImagePath(absolutePath) && Asset::LoadTextureAsset(absolutePath, texture) &&
        texture.width > 0U && texture.height > 0U &&
        texture.format == Asset::TextureAssetFormat::RGBA8Unorm) {
        textureId = EditorUiAssets::UploadDynamicTexture(reinterpret_cast<const std::uint8_t*>(texture.pixels.data()),
                                                                static_cast<int>(texture.width),
                                                                static_cast<int>(texture.height));
    } else {
        // Not a `.uvtex` envelope - it may still be a raw, un-imported source image.
        std::uint32_t rawWidth = 0U;
        std::uint32_t rawHeight = 0U;
        std::vector<std::byte> rawPixels;
        if (DecodeRawImageThumbnailPixels(absolutePath, rawWidth, rawHeight, rawPixels) && rawWidth > 0U &&
            rawHeight > 0U) {
            textureId = EditorUiAssets::UploadDynamicTexture(reinterpret_cast<const std::uint8_t*>(rawPixels.data()),
                                                                    static_cast<int>(rawWidth),
                                                                    static_cast<int>(rawHeight));
        }
    }
    m_textureThumbnailCache.emplace(cacheKey, textureId);
    return textureId;
}

void Editor::ClearTextureThumbnailCache() noexcept {
    for (auto& [path, textureId] : m_textureThumbnailCache) {
        EditorUiAssets::DeleteDynamicTexture(textureId);
    }
    m_textureThumbnailCache.clear();
}

namespace {

/// A helper bone rather than a part of the body (the root on the ground, IK targets, sockets):
/// left out of a thumbnail's skeleton, where a line to it would cut across the figure.
[[nodiscard]] bool IsThumbnailHelperBone(const std::string& name) {
    const Retarget::HumanoidReference& reference = Retarget::GetHumanoidReference();
    for (std::size_t bone = 0U; bone < reference.info.size() && bone < reference.skeleton.bones.size(); ++bone) {
        if (reference.skeleton.bones[bone].name == name) {
            const Retarget::HumanoidBoneKind kind = reference.info[bone].kind;
            return kind == Retarget::HumanoidBoneKind::Root || kind == Retarget::HumanoidBoneKind::IK ||
                   kind == Retarget::HumanoidBoneKind::Socket;
        }
    }
    return name.size() > 2U && (name[0] == 'i' || name[0] == 'I') && (name[1] == 'k' || name[1] == 'K') &&
           (name[2] == '_' || std::isupper(static_cast<unsigned char>(name[2])) != 0);
}

/// Stands `mesh` in its skeleton's rest pose when it has one (`skeletonFile`, joints matched by
/// name - a retargeted character rests in the A-pose there, its mesh still bound in its own pose),
/// and lists its bones in that pose. A pose that would fling the skin off the model (another unit
/// scale, say) is not used: the mesh then shows as bound, and its bones with it.
void PoseMeshForThumbnail(Asset::MeshAsset& mesh, const std::filesystem::path& skeletonFile,
                             std::vector<MeshThumbnailBone>& outBones) {
    outBones.clear();
    const std::size_t count = mesh.joints.size();
    if (count == 0U) {
        return;
    }
    // Each joint's own rest, from the binds (bind(parent)^-1 * bind(joint)).
    std::vector<Math::Matrix4x4> bind(count, Math::Matrix4x4::Identity());
    for (std::size_t joint = 0U; joint < count; ++joint) {
        static_cast<void>(Math::TryInverse(mesh.joints[joint].inverseBindMatrix, bind[joint]));
    }
    std::vector<Math::Matrix4x4> locals(count);
    for (std::size_t joint = 0U; joint < count; ++joint) {
        const std::uint32_t parent = mesh.joints[joint].parentIndex;
        locals[joint] = parent < joint ? mesh.joints[parent].inverseBindMatrix * bind[joint] : bind[joint];
    }
    std::vector<Math::Matrix4x4> world = bind;
    bool posed = false;
    Asset::SkeletonAsset skeleton;
    std::error_code error;
    if (!skeletonFile.empty() && std::filesystem::is_regular_file(skeletonFile, error) && Asset::LoadSkeletonAsset(skeletonFile, skeleton)) {
        std::unordered_map<std::string, std::size_t> byName;
        for (std::size_t joint = 0U; joint < skeleton.joints.size(); ++joint) {
            byName.emplace(skeleton.joints[joint].name, joint);
        }
        std::vector<Math::Matrix4x4> poseLocals = locals;
        std::size_t matched = 0U;
        for (std::size_t joint = 0U; joint < count; ++joint) {
            if (const auto found = byName.find(mesh.joints[joint].name); found != byName.end()) {
                const Asset::SkeletonJoint& rest = skeleton.joints[found->second];
                poseLocals[joint] = Math::Matrix4x4::ComposeTrs(rest.position, rest.rotation, rest.scale);
                ++matched;
            }
        }
        std::vector<Math::Matrix4x4> skinning;
        std::vector<Asset::MeshVertex> vertices;
        if (matched * 2U >= count && Asset::TryResolvePose(mesh.joints, poseLocals, skinning) &&
            Asset::TrySkinMesh(mesh, skinning, vertices) && !vertices.empty()) {
            Math::Aabb bounds{vertices.front().position, vertices.front().position};
            for (const Asset::MeshVertex& vertex : vertices) {
                bounds.min = Math::Vector3{std::min(bounds.min.x, vertex.position.x), std::min(bounds.min.y, vertex.position.y),
                                              std::min(bounds.min.z, vertex.position.z)};
                bounds.max = Math::Vector3{std::max(bounds.max.x, vertex.position.x), std::max(bounds.max.y, vertex.position.y),
                                              std::max(bounds.max.z, vertex.position.z)};
            }
            const Math::Vector3 before = mesh.localBounds.GetExtents();
            const Math::Vector3 after = bounds.GetExtents();
            const float ratio = after.y / std::max(before.y, 1.0e-6F);
            if (ratio > 0.6F && ratio < 1.6F) {
                mesh.vertices = std::move(vertices);
                mesh.localBounds = bounds;
                for (std::size_t joint = 0U; joint < count; ++joint) {
                    const std::uint32_t parent = mesh.joints[joint].parentIndex;
                    world[joint] = parent < joint ? world[parent] * poseLocals[joint] : poseLocals[joint];
                }
                posed = true;
            }
        }
    }
    static_cast<void>(posed);
    for (std::size_t joint = 0U; joint < count; ++joint) {
        const std::uint32_t parent = mesh.joints[joint].parentIndex;
        if (parent >= joint || IsThumbnailHelperBone(mesh.joints[joint].name) || IsThumbnailHelperBone(mesh.joints[parent].name)) {
            continue;
        }
        const Math::Vector3 head = Math::TransformPoint(world[parent], Math::Vector3{});
        const Math::Vector3 tail = Math::TransformPoint(world[joint], Math::Vector3{});
        outBones.push_back(MeshThumbnailBone{{head.x, head.y, head.z}, {tail.x, tail.y, tail.z}});
    }
}

} // namespace

std::uintptr_t Editor::GetMeshThumbnail(const std::filesystem::path& relativePath) {
    const std::string cacheKey = relativePath.generic_string();
    const auto now = std::chrono::steady_clock::now();
    std::uintptr_t oldTexture = 0U;
    if (const auto cachedIt = m_meshThumbnailCache.find(cacheKey); cachedIt != m_meshThumbnailCache.end()) {
        MeshThumbnailEntry& entry = cachedIt->second;
        // Looked at again at most twice a second: drawn again when a file it came from changed,
        // and tried again when there was nothing to draw yet.
        if (now - entry.checkedAt < std::chrono::milliseconds{500}) {
            return entry.texture;
        }
        entry.checkedAt = now;
        bool stale = entry.texture == 0U;
        for (const auto& [file, written] : entry.sources) {
            std::error_code error;
            const std::filesystem::file_time_type current = std::filesystem::last_write_time(file, error);
            stale = stale || error || current != written;
        }
        if (!stale) {
            return entry.texture;
        }
        // Freed only after the new picture is made, so it gets a new texture id and every view
        // that copied the old picture by its id copies the new one.
        oldTexture = entry.texture;
        m_meshThumbnailCache.erase(cachedIt);
    }
    const Asset::ProjectFileSnapshot snapshot = m_services->GetProjectFileIndex().GetSnapshot();
    // A model source previews its imported mesh; until that exists there is nothing to render yet.
    const std::filesystem::path absolutePath =
        IsModelSourcePath(relativePath) ? GetImportedModelPath(relativePath)
                                           : ResolveThumbnailAssetPath(snapshot.contentRoot, relativePath);
    MeshThumbnailEntry entry;
    entry.checkedAt = now;
    const auto remember = [&entry](const std::filesystem::path& file) {
        std::error_code error;
        const std::filesystem::file_time_type written = std::filesystem::last_write_time(file, error);
        if (!error) {
            entry.sources.emplace_back(file, written);
        }
    };
    remember(absolutePath);
    Asset::MeshAsset mesh;
    std::string extension = relativePath.extension().string();
    std::ranges::transform(extension, extension.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto buildLook = [&](const Asset::AssetGuid materialGuid, Asset::IAssetDatabase& assetDatabase,
                               std::vector<std::unique_ptr<MaterialThumbnailImage>>& images,
                               const bool markSeeThroughSkip) -> MeshThumbnailLook {
        MeshThumbnailLook look;
        if (materialGuid == Asset::kInvalidAssetGuid) {
            return look;
        }
        const std::filesystem::path materialPath = assetDatabase.Resolve(materialGuid);
        Asset::MaterialAsset material;
        if (materialPath.empty() || !Asset::LoadMaterialAsset(materialPath, material)) {
            return look;
        }
        remember(materialPath);
        look.colour = {material.albedoColor.r, material.albedoColor.g, material.albedoColor.b};
        look.metallic = material.metallic;
        look.roughness = material.roughness;
        look.dielectricSpecular = material.dielectricSpecular;
        look.normalScale = material.normalScale;
        look.flipGreen = material.normalFlipGreen;
        look.occlusionStrength = material.occlusionStrength;
        look.emissiveColor = {material.emissiveColor.r, material.emissiveColor.g, material.emissiveColor.b};
        look.emissiveEnergy = material.emissiveEnergy;
        look.uvScale = {material.uvScale.x, material.uvScale.y};
        look.uvOffset = {material.uvOffset.x, material.uvOffset.y};
        look.unshaded = material.unshaded;
        if (markSeeThroughSkip) {
            look.skip = material.isTransparent || material.alphaCutoff > 0.0F;
        }
        const auto loadMap = [&](const Asset::AssetGuid texGuid, const bool srgb,
                                 const MaterialThumbnailImage*& slotPtr) {
            if (texGuid == Asset::kInvalidAssetGuid) {
                return;
            }
            const std::filesystem::path texturePath = assetDatabase.Resolve(texGuid);
            auto image = std::make_unique<MaterialThumbnailImage>();
            if (!texturePath.empty() && LoadMaterialPreviewImage(texturePath, srgb, *image)) {
                remember(texturePath);
                slotPtr = image.get();
                images.push_back(std::move(image));
            }
        };
        loadMap(material.albedoTexture, true, look.albedo);
        loadMap(material.normalTexture, false, look.normal);
        loadMap(material.metallicRoughnessTexture, false, look.metallicRoughness);
        loadMap(material.aoTexture, false, look.ao);
        loadMap(material.emissiveTexture, true, look.emissive);
        return look;
    };

    if (extension == ".uventity" || extension == ".uvprefab") {
        // An entity: its meshes and primitives where it puts them, seen by the thumbnail's own
        // camera (its "Thumbnail Camera" when it has one) - never the viewport's.
        Asset::IAssetDatabase& assetDatabase = m_services->GetAssetDatabase();
        std::vector<std::unique_ptr<MaterialThumbnailImage>> images;
        const auto lookOf = [&](const Asset::AssetGuid materialGuid) {
            return buildLook(materialGuid, assetDatabase, images, false);
        };
        const auto pose = [&](Asset::MeshAsset& model, const std::filesystem::path& file) {
            std::filesystem::path skeletonFile = file;
            skeletonFile.replace_extension(".uvskel");
            std::vector<MeshThumbnailBone> unused;
            PoseMeshForThumbnail(model, skeletonFile, unused);
            remember(skeletonFile);
        };
        EntityThumbnailScene scene;
        if (BuildEntityThumbnailScene(absolutePath, assetDatabase, lookOf, pose, scene)) {
            for (const std::filesystem::path& file : scene.sources) {
                remember(file);
            }
            const float direction[3] = {scene.viewDirection ? scene.viewDirection->x : 0.0F, scene.viewDirection ? scene.viewDirection->y : 0.0F,
                                        scene.viewDirection ? scene.viewDirection->z : 0.0F};
            entry.texture = m_meshThumbnailRenderer.RenderThumbnail(scene.mesh, kMeshThumbnailSize, kMeshThumbnailSize, scene.looks, {},
                                                                        scene.viewDirection ? direction : nullptr);
        }
    } else if (Asset::LoadMeshAsset(absolutePath, mesh)) {
        // In its skeleton's rest pose (the A-pose once retargeted), with its bones drawn over it.
        std::filesystem::path skeletonFile = absolutePath;
        skeletonFile.replace_extension(".uvskel");
        std::vector<MeshThumbnailBone> bones;
        PoseMeshForThumbnail(mesh, skeletonFile, bones);
        remember(skeletonFile);
        // Each material slot with full PBR maps/scalars. A see-through slot (eyelashes, hair cards)
        // is left out; a slot whose material cannot be read stays neutral grey.
        Asset::IAssetDatabase& assetDatabase = m_services->GetAssetDatabase();
        std::vector<MeshThumbnailLook> looks(mesh.materialGuids.size());
        std::vector<std::unique_ptr<MaterialThumbnailImage>> images;
        for (std::size_t slot = 0U; slot < mesh.materialGuids.size(); ++slot) {
            looks[slot] = buildLook(mesh.materialGuids[slot], assetDatabase, images, true);
        }
        // Never draw nothing: when every slot is see-through, draw them all.
        if (!looks.empty() && std::ranges::all_of(looks, [](const MeshThumbnailLook& look) { return look.skip; })) {
            for (MeshThumbnailLook& look : looks) {
                look.skip = false;
            }
        }
        entry.texture = m_meshThumbnailRenderer.RenderThumbnail(mesh, kMeshThumbnailSize, kMeshThumbnailSize, looks, bones);
    }
    const std::uintptr_t texture = entry.texture;
    m_meshThumbnailCache.emplace(cacheKey, std::move(entry));
    if (oldTexture != 0U) {
        ++m_meshThumbnailGeneration;
    }
    EditorUiAssets::DeleteDynamicTexture(oldTexture);
    return texture;
}

void Editor::ClearMeshThumbnailCache() noexcept {
    for (auto& [path, entry] : m_meshThumbnailCache) {
        EditorUiAssets::DeleteDynamicTexture(entry.texture);
    }
    m_meshThumbnailCache.clear();
    ++m_meshThumbnailGeneration;
}

std::uintptr_t Editor::GetMaterialThumbnail(const std::filesystem::path& relativePath) {
    const std::string cacheKey = relativePath.generic_string();
    const auto cachedIt = m_materialThumbnailCache.find(cacheKey);
    if (cachedIt != m_materialThumbnailCache.end()) {
        return cachedIt->second;
    }
    const Asset::ProjectFileSnapshot snapshot = m_services->GetProjectFileIndex().GetSnapshot();
    const std::filesystem::path materialFile = ResolveThumbnailAssetPath(snapshot.contentRoot, relativePath);
    Asset::MaterialAsset material;
    std::uintptr_t textureId = 0U;
    if (!Asset::LoadMaterialAsset(materialFile, material)) {
        UVE_WARNING("Editor: failed to load material thumbnail source \"{}\"", materialFile.string());
        m_materialThumbnailCache.emplace(cacheKey, 0U);
        return 0U;
    }
    // Only GUIDs resolve here: the material stores references, never paths, so a texture that
    // is in the project but not in the asset database (e.g. never imported and never picked
    // through the inspector) previews as its flat value rather than blocking the material.
    Asset::IAssetDatabase& assetDatabase = m_services->GetAssetDatabase();
    MaterialThumbnailTextures textures;
    const auto loadSlot = [&assetDatabase, &snapshot](const Asset::AssetGuid guid, const bool rawImageSrgb,
                                                      MaterialThumbnailImage& image) {
        if (guid == Asset::kInvalidAssetGuid) {
            return;
        }
        const std::filesystem::path resolved = assetDatabase.Resolve(guid);
        static_cast<void>(LoadMaterialPreviewImage(ResolveThumbnailAssetPath(snapshot.contentRoot, resolved),
                                                      rawImageSrgb, image));
    };
    loadSlot(material.albedoTexture, true, textures.albedo);
    loadSlot(material.normalTexture, false, textures.normal);
    // The runtime renderer only samples the packed metallic-roughness map; the separate
    // metallicTexture/roughnessTexture slots are not consumed by it either, so the preview
    // follows the renderer rather than promising a result the game will not show.
    loadSlot(material.metallicRoughnessTexture, false, textures.metallicRoughness);
    loadSlot(material.aoTexture, false, textures.ao);
    loadSlot(material.emissiveTexture, true, textures.emissive);
    textureId = m_materialThumbnailRenderer.RenderThumbnail(material, textures, kMaterialThumbnailSize,
                                                               kMaterialThumbnailSize);
    // Only cache successful GPU renders so a transient GL state issue never locks an asset onto its SVG fallback.
    if (textureId != 0U) {
        m_materialThumbnailCache.emplace(cacheKey, textureId);
    }
    return textureId;
}

void Editor::ClearMaterialThumbnailCache() noexcept {
    for (auto& [path, textureId] : m_materialThumbnailCache) {
        EditorUiAssets::DeleteDynamicTexture(textureId);
    }
    m_materialThumbnailCache.clear();
    for (std::uintptr_t& textureId : m_retiredMaterialThumbnailTextures) {
        EditorUiAssets::DeleteDynamicTexture(textureId);
    }
    m_retiredMaterialThumbnailTextures.clear();
    ++m_materialThumbnailGeneration;
}

void Editor::InvalidateMaterialThumbnailCache(const std::filesystem::path& absolutePath) {
    const Asset::ProjectFileSnapshot snapshot = m_services->GetProjectFileIndex().GetSnapshot();
    std::error_code error;
    std::filesystem::path cachePath = std::filesystem::relative(absolutePath, snapshot.contentRoot, error);
    if (error || cachePath.empty() || cachePath == "." || *cachePath.begin() == "..") {
        error.clear();
        const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(absolutePath, error);
        const std::filesystem::path canonicalRoot =
            error ? std::filesystem::path{} : std::filesystem::weakly_canonical(snapshot.contentRoot, error);
        cachePath = error ? std::filesystem::path{} : std::filesystem::relative(canonicalPath, canonicalRoot, error);
    }
    if (cachePath.empty() || cachePath == "." || *cachePath.begin() == "..") {
        cachePath = absolutePath.lexically_normal();
    }
    auto cachedIt = m_materialThumbnailCache.find(cachePath.generic_string());
    if (cachedIt == m_materialThumbnailCache.end()) {
        cachedIt = m_materialThumbnailCache.find(absolutePath.lexically_normal().generic_string());
    }
    if (cachedIt == m_materialThumbnailCache.end()) {
        ++m_materialThumbnailGeneration;
        return;
    }
    // Retired, not deleted: see m_retiredMaterialThumbnailTextures.
    m_retiredMaterialThumbnailTextures.push_back(cachedIt->second);
    m_materialThumbnailCache.erase(cachedIt);
    ++m_materialThumbnailGeneration;
}

bool Editor::IsModelSourcePath(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension == ".glb" || extension == ".gltf" || extension == ".obj" || extension == ".fbx";
}

std::filesystem::path Editor::GetImportedModelPath(const std::filesystem::path& relativeSource) const {
    // Beside the import cache rather than inside it: the cache root holds only cache metadata.
    // The source's own extension stays in the name, so "rock.obj" and "rock.glb" never collide.
    const std::filesystem::path cacheRoot = m_services->GetDerivedArtifactCache().GetCacheRoot();
    std::filesystem::path importedRoot = cacheRoot.has_filename() ? cacheRoot.parent_path() : cacheRoot.parent_path().parent_path();
    importedRoot /= "Imported";
    return (importedRoot / relativeSource).concat(".uvmodel").lexically_normal();
}

namespace {

/// Reads only as much of a model source as it takes to tell what it is: the whole file for a
/// .gltf (it is the JSON), the header and JSON chunk for a .glb - never the binary payload - and
/// the scene without its geometry or curves for an .fbx.
[[nodiscard]] EditorModelSourceInfo ReadModelSourceInfo(const std::filesystem::path& path) {
    struct CachedModelInfo {
        std::filesystem::file_time_type writeTime{};
        EditorModelSourceInfo info{};
    };
    static std::mutex s_modelInfoCacheMutex;
    static std::unordered_map<std::string, CachedModelInfo> s_modelInfoCache;
    std::error_code timeError;
    const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(path, timeError);
    const std::string cacheKey = path.generic_string();
    if (!timeError) {
        std::lock_guard<std::mutex> lock(s_modelInfoCacheMutex);
        if (const auto it = s_modelInfoCache.find(cacheKey); it != s_modelInfoCache.end() && it->second.writeTime == writeTime) {
            return it->second.info;
        }
    }

    EditorModelSourceInfo info;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return info;
    }
    std::string json;
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    constexpr std::uint32_t kMaximumJsonBytes = 64U * 1024U * 1024U;
    if (extension == ".glb") {
        std::array<std::uint32_t, 5> header{};
        if (!file.read(reinterpret_cast<char*>(header.data()), sizeof(header)) || header[0] != 0x46546C67U ||
            header[4] != 0x4E4F534AU || header[3] > kMaximumJsonBytes) {
            return info;
        }
        json.resize(header[3]);
        if (!file.read(json.data(), static_cast<std::streamsize>(json.size()))) {
            return info;
        }
    } else if (extension == ".gltf") {
        json.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    } else if (extension == ".fbx") {
        // FBX keeps its skins and takes among the scene's objects rather than in a header, so the
        // file is parsed - without its geometry or curves, which is most of what would take time.
        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(path, error);
        if (error || size > Asset::kMaximumFbxMeshSourceBytes) {
            return info;
        }
        std::vector<char> bytes(static_cast<std::size_t>(size));
        if (size > 0U && !file.read(bytes.data(), static_cast<std::streamsize>(size))) {
            return info;
        }
        const std::optional<Asset::FbxSourceSummary> summary =
            Asset::DescribeFbxSource(std::as_bytes(std::span<const char>(bytes)));
        if (!summary.has_value()) {
            return info;
        }
        // A skin without any mesh geometry is not a Skeletal Mesh asset. Keep this tied to the
        // source summary instead of using the skin flag alone; animation-only FBXs still have bones.
        info.rigged = summary->meshCount > 0U && summary->hasSkin;
        info.hasSkeleton = summary->boneCount > 0U;
        info.animationOnly = summary->IsAnimationOnly();
        info.animationCount = summary->animationCount;
        if (info.animationOnly) {
            char length[32] = {};
            std::snprintf(length, sizeof(length), "%.2f s", summary->longestAnimationSeconds);
            info.summary = std::to_string(summary->boneCount) + (summary->boneCount == 1U ? " bone, " : " bones, ") +
                           std::to_string(summary->animationCount) +
                           (summary->animationCount == 1U ? " animation, " : " animations, ") + length;
        }
        if (!timeError) {
            std::lock_guard<std::mutex> lock(s_modelInfoCacheMutex);
            s_modelInfoCache[cacheKey] = CachedModelInfo{writeTime, info};
        }
        return info;
    } else {
        return info; // OBJ has no skeleton.
    }
    const std::optional<Asset::GltfMetadata> metadata = Asset::ParseGltfMetadata(json);
    info.rigged = metadata.has_value() && metadata->meshCount > 0U && metadata->skinCount > 0U;
    info.hasSkeleton = metadata.has_value() && metadata->skinCount > 0U; // A glTF skeleton is read from its skin.
    if (!timeError) {
        std::lock_guard<std::mutex> lock(s_modelInfoCacheMutex);
        s_modelInfoCache[cacheKey] = CachedModelInfo{writeTime, info};
    }
    return info;
}

} // namespace

const EditorModelSourceInfo* Editor::FindModelSourceInfo(
    const std::filesystem::path& relativeSource) const {
    const auto found = m_modelSources.find(relativeSource.generic_string());
    return found != m_modelSources.end() ? &found->second : nullptr;
}

std::filesystem::path Editor::FindAnimationClipSource(const std::filesystem::path& relativeClip) const {
    const std::string folder = relativeClip.parent_path().generic_string();
    const std::string clipStem = relativeClip.stem().string();
    std::filesystem::path found;
    std::size_t foundStemSize = 0U;
    for (const auto& [key, info] : m_modelSources) {
        if (!info.animationOnly) {
            continue;
        }
        const std::filesystem::path source(key);
        const std::string stem = source.stem().string();
        // The longest stem wins: "Hero_Run.uvanim" is Hero_Run.fbx's own clip before a take of Hero.fbx.
        if (stem.size() > foundStemSize && source.parent_path().generic_string() == folder &&
            (clipStem == stem || clipStem.starts_with(stem + "_"))) {
            found = source;
            foundStemSize = stem.size();
        }
    }
    return found;
}

const std::string* Editor::FindModelImportFailure(const std::filesystem::path& relativeSource) const {
    const auto found = m_modelImportFailures.find(relativeSource.generic_string());
    return found != m_modelImportFailures.end() ? &found->second : nullptr;
}

bool Editor::IsRiggedModelSource(const std::filesystem::path& relativeSource) const {
    const EditorModelSourceInfo* const info = FindModelSourceInfo(relativeSource);
    return info != nullptr && info->rigged;
}

namespace {

[[nodiscard]] bool IsGenericTakeName(const std::string_view name) noexcept {
    std::string clean;
    clean.reserve(name.size());
    for (const char c : name) {
        if (c != ' ' && c != '_' && c != '-' && c != '|' && c != ':') {
            clean.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return clean == "take001" || clean == "take1" || clean == "take" ||
           clean == "defaulttake" || clean == "baseanimation" || clean == "scene" ||
           clean == "mixamocom" || clean == "armature" || clean == "unnamed";
}

} // namespace

namespace {
/// A loose take an earlier import reduced (its original is in the project's animation backup):
/// it is written again from its source, every frame as the source has it.
[[nodiscard]] bool WasReducedEarlier(const std::filesystem::path& contentRoot, const std::filesystem::path& clip) {
    static std::mutex s_reducedMutex;
    static std::unordered_map<std::string, std::unordered_set<std::string>> s_reduced;
    const std::filesystem::path backupRoot = contentRoot.parent_path() / ".uvcache" / "animation-backup";
    const std::string backupKey = backupRoot.generic_string();
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(clip, contentRoot, error);
    if (error) {
        return false;
    }
    const std::string relKey = relative.generic_string();
    {
        std::lock_guard<std::mutex> lock(s_reducedMutex);
        if (const auto found = s_reduced.find(backupKey); found != s_reduced.end()) {
            return found->second.contains(relKey);
        }
    }
    std::unordered_set<std::string> entries;
    std::error_code walkError;
    for (std::filesystem::recursive_directory_iterator walk(backupRoot, walkError), end; !walkError && walk != end;
         walk.increment(walkError)) {
        if (walk->is_regular_file(walkError) && walk->path().extension() == ".uvanim") {
            const std::filesystem::path rel = std::filesystem::relative(walk->path(), backupRoot, walkError);
            if (!walkError && rel.begin() != rel.end()) {
                // <stamp>/<path in Content>: the part after the stamp folder.
                std::filesystem::path inContent;
                for (auto part = std::next(rel.begin()); part != rel.end(); ++part) {
                    inContent /= *part;
                }
                entries.insert(inContent.generic_string());
            }
            walkError.clear();
        }
    }
    std::lock_guard<std::mutex> lock(s_reducedMutex);
    const auto it = s_reduced.try_emplace(backupKey, std::move(entries)).first;
    return it->second.contains(relKey);
}

struct ExtractedModelAnimations final {
    std::filesystem::path absoluteSource;
    std::filesystem::path contentRoot;
    std::filesystem::path importedModel;
    bool keepInModel = false;
    std::vector<std::string> embeddedTakes;
    std::vector<std::filesystem::path> writtenLooseFiles;
};

/// Pure filesystem & FBX extraction step: safe to run on the serialized background import worker
/// (never touches Editor state or IAssetDatabase).
[[nodiscard]] ExtractedModelAnimations ExtractModelAnimationsOffThread(
    const std::filesystem::path& absoluteSource, const std::filesystem::path& contentRoot,
    const std::filesystem::path& importedModel, const bool keepInModel, const bool retargetedCurrent) {
    ExtractedModelAnimations result;
    result.absoluteSource = absoluteSource;
    result.contentRoot = contentRoot;
    result.importedModel = importedModel;
    result.keepInModel = keepInModel;

    std::error_code error;
    const std::filesystem::file_time_type sourceTime = std::filesystem::last_write_time(absoluteSource, error);
    if (error) {
        return result;
    }
    struct CachedAnimationImport {
        std::filesystem::file_time_type sourceTime{};
        std::vector<std::filesystem::path> files;
    };
    static std::mutex s_importedAnimCacheMutex;
    static std::unordered_map<std::string, CachedAnimationImport> s_importedAnimCache;
    const std::string cacheKey = absoluteSource.generic_string();

    // The version of the import is part of the stamp: takes kept by an earlier import (when they
    // were reduced as they came in) are taken again from the source, every frame as it has them.
    constexpr std::uint64_t kTakesImportVersion = 0x7A00000000000000ULL;
    const auto stamp = static_cast<std::uint64_t>(sourceTime.time_since_epoch().count()) ^ kTakesImportVersion;
    if (keepInModel) {
        std::error_code modelError;
        if (!std::filesystem::is_regular_file(importedModel, modelError)) {
            return result; // after its mesh is imported (the next refresh)
        }
        if (Asset::ReadModelAnimationStamp(importedModel) == stamp) {
            return result; // its takes came from this version of the source already
        }
        // Never overwrite embedded takes on a retargeted model whose source has not changed.
        if (retargetedCurrent && !Asset::ListModelAnimations(importedModel).empty()) {
            return result;
        }
    }
    {
        std::optional<std::vector<std::filesystem::path>> cachedFiles;
        {
            std::lock_guard<std::mutex> lock(s_importedAnimCacheMutex);
            if (const auto cacheIt = s_importedAnimCache.find(cacheKey);
                cacheIt != s_importedAnimCache.end() && cacheIt->second.sourceTime == sourceTime) {
                cachedFiles = cacheIt->second.files;
            }
        }
        if (cachedFiles.has_value()) {
            bool allPresent = true;
            for (const std::filesystem::path& expectedFile : *cachedFiles) {
                std::error_code existsErr;
                if (!std::filesystem::is_regular_file(expectedFile, existsErr)) {
                    allPresent = false;
                    break;
                }
            }
            if (allPresent) {
                return result;
            }
        }
    }
    const std::string stem = absoluteSource.stem().string();
    const std::string prefix = stem + "_";
    const std::filesystem::path primaryDestination =
        absoluteSource.parent_path() / (stem + ".uvanim");
    const std::filesystem::file_time_type existingPrimary = std::filesystem::last_write_time(primaryDestination, error);
    if (!keepInModel && !error && existingPrimary >= sourceTime) {
        return result; // fast-path: primary clip already imported since FBX last changed
    }
    error.clear();
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(absoluteSource.parent_path(), error)) {
        if (keepInModel) {
            break;
        }
        if (!entry.is_regular_file() || entry.path().extension() != ".uvanim") {
            continue;
        }
        const std::string entryStem = entry.path().stem().string();
        if (entryStem == stem || entryStem.starts_with(prefix)) {
            std::error_code entryErr;
            const auto entryTime = std::filesystem::last_write_time(entry.path(), entryErr);
            if (!entryErr && entryTime >= sourceTime) {
                return result; // fast-path: multi-take clip already imported since FBX last changed
            }
        }
    }
    error.clear();
    std::ifstream file(absoluteSource, std::ios::binary);
    const std::uintmax_t size = std::filesystem::file_size(absoluteSource, error);
    if (!file || error || size > Asset::kMaximumFbxMeshSourceBytes) {
        return result;
    }
    std::vector<char> bytes(static_cast<std::size_t>(size));
    if (size > 0U && !file.read(bytes.data(), static_cast<std::streamsize>(size))) {
        return result;
    }
    const std::vector<Asset::AnimationClipAsset> rawTakes =
        Asset::ReadFbxAnimations(std::as_bytes(std::span<const char>(bytes)), Asset::kMaximumAnimationAssetBones);

    // Suppress generic exporter takes (e.g. "Take 001") if named takes exist in the FBX.
    // If only generic takes exist ("Take 001" + "mixamo.com"), keep only one, the first that moves,
    // to avoid duplicate files.
    std::vector<Asset::AnimationClipAsset> takes;
    bool hasNonGeneric = false;
    for (const auto& take : rawTakes) {
        const std::size_t bar = take.clipId.find_last_of('|');
        const std::string id = bar == std::string::npos ? take.clipId : take.clipId.substr(bar + 1U);
        if (!IsGenericTakeName(id)) {
            hasNonGeneric = true;
            break;
        }
    }
    for (const auto& take : rawTakes) {
        const std::size_t bar = take.clipId.find_last_of('|');
        const std::string id = bar == std::string::npos ? take.clipId : take.clipId.substr(bar + 1U);
        if (hasNonGeneric && !IsGenericTakeName(id)) {
            takes.push_back(take); // generic "Take 001" beside named takes is redundant
        }
    }
    if (takes.empty() && !rawTakes.empty()) {
        const auto moving = std::ranges::find_if(rawTakes, [](const Asset::AnimationClipAsset& take) {
            return take.durationSeconds > 0.0 &&
                   (!take.samples.empty() ||
                    std::ranges::any_of(take.bones, [](const Asset::AnimationAssetBoneTrack& bone) {
                        return !bone.samples.empty();
                    }));
        });
        takes.push_back(moving != rawTakes.end() ? *moving : rawTakes.front());
    }

    if (keepInModel) {
        std::vector<Asset::AnimationClipAsset> kept;
        std::vector<std::string> names;
        for (Asset::AnimationClipAsset clip : takes) {
            const std::size_t bar = clip.clipId.find_last_of('|');
            std::string take = bar == std::string::npos ? clip.clipId : clip.clipId.substr(bar + 1U);
            if (takes.size() == 1U || IsGenericTakeName(take)) {
                take = stem;
            }
            for (char& character : take) {
                const bool keep = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                                  (character >= '0' && character <= '9') || character == '-' || character == '_' || character == ' ';
                character = keep ? character : '_';
            }
            if (std::ranges::find(names, take) != names.end()) {
                continue;
            }
            clip.clipId = take;
            names.push_back(take);
            kept.push_back(std::move(clip));
        }
        if (!Asset::SaveModelAnimations(importedModel, kept, stamp)) {
            UVE_WARNING("Editor: could not keep the animations of {} in its model", absoluteSource.string());
            return result;
        }
        result.embeddedTakes = std::move(names);
        return result;
    }

    // Clean up any stale duplicate take001 files that might have been created previously
    {
        const std::filesystem::path p1 = absoluteSource.parent_path() / "take001.uvanim";
        const std::filesystem::path p2 = absoluteSource.parent_path() / "Take 001.uvanim";
        const std::filesystem::path p3 = absoluteSource.parent_path() / (absoluteSource.stem().string() + "_Take 001.uvanim");
        const std::filesystem::path p4 = absoluteSource.parent_path() / (absoluteSource.stem().string() + "_take001.uvanim");
        for (const auto& stale : {p1, p2, p3, p4}) {
            std::error_code rmErr;
            if (std::filesystem::exists(stale, rmErr)) {
                std::filesystem::remove(stale, rmErr);
            }
        }
    }

    // The animation is named after its file, everywhere: one take is "<FBX>.uvanim"; several are
    // "<FBX>_<take>.uvanim", the take without its "Armature|"-style prefix. The DCC tool's own take
    // label ("Take 001", an exporter's name) is suppressed or mapped to <FBX>.uvanim.
    const auto clipPath = [&absoluteSource, &takes](const std::string& clipId) {
        if (takes.size() == 1U) {
            return absoluteSource.parent_path() / (absoluteSource.stem().string() + ".uvanim");
        }
        const std::size_t bar = clipId.find_last_of('|');
        const std::string take = bar == std::string::npos ? clipId : clipId.substr(bar + 1U);
        if (IsGenericTakeName(take)) {
            return absoluteSource.parent_path() / (absoluteSource.stem().string() + ".uvanim");
        }
        std::string safe;
        for (const char character : take) {
            const bool keep = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                              (character >= '0' && character <= '9') || character == '-' || character == '_' ||
                              character == ' ';
            safe.push_back(keep ? character : '_');
        }
        return absoluteSource.parent_path() / (absoluteSource.stem().string() + "_" + safe + ".uvanim");
    };
    std::vector<std::filesystem::path> allDestinations;
    for (Asset::AnimationClipAsset clip : takes) {
        const std::filesystem::path destination = clipPath(clip.clipId);
        clip.clipId = destination.stem().string();
        const std::filesystem::file_time_type existing = std::filesystem::last_write_time(destination, error);
        if (!error && existing >= sourceTime && !WasReducedEarlier(contentRoot, destination)) {
            allDestinations.push_back(destination);
            continue; // imported since the FBX last changed
        }
        error.clear();
        if (Asset::SaveAnimationClipAsset(clip, destination)) {
            result.writtenLooseFiles.push_back(destination);
            allDestinations.push_back(destination);
        } else {
            UVE_WARNING("Editor: could not write animation {}", destination.string());
        }
    }
    {
        std::lock_guard<std::mutex> lock(s_importedAnimCacheMutex);
        s_importedAnimCache[cacheKey] = CachedAnimationImport{sourceTime, std::move(allDestinations)};
    }
    return result;
}

[[nodiscard]] std::vector<std::filesystem::path> RegisterExtractedModelAnimations(
    Asset::IAssetDatabase& database, const std::filesystem::path& absoluteSource,
    const std::filesystem::path& contentRoot, const std::filesystem::path& importedModel, const bool keepInModel,
    const std::vector<std::string>& embeddedTakes, const std::vector<std::filesystem::path>& writtenLooseFiles,
    const bool saveDatabase) {
    std::vector<std::filesystem::path> written;
    if (keepInModel) {
        if (embeddedTakes.empty()) {
            return written;
        }
        const std::string stem = absoluteSource.stem().string();
        const std::filesystem::path backup = contentRoot.parent_path() / ".uvcache" / "animation-backup" / "moved-into-models";
        for (const std::string& take : embeddedTakes) {
            const std::filesystem::path embedded = Asset::MakeEmbeddedClipPath(importedModel, take);
            const std::filesystem::path loose =
                absoluteSource.parent_path() / ((take == stem ? stem : stem + "_" + take) + ".uvanim");
            std::error_code looseError;
            if (std::filesystem::is_regular_file(loose, looseError)) {
                const Asset::AssetGuid guid = database.Register(loose);
                static_cast<void>(database.Relocate(guid, embedded));
                std::filesystem::create_directories(backup, looseError);
                std::filesystem::rename(loose, backup / loose.filename(), looseError);
                if (looseError) {
                    looseError.clear();
                    std::filesystem::copy_file(loose, backup / loose.filename(),
                                               std::filesystem::copy_options::overwrite_existing, looseError);
                    std::filesystem::remove(loose, looseError);
                }
            } else {
                static_cast<void>(database.Register(embedded));
            }
            written.push_back(embedded);
        }
        if (saveDatabase) {
            static_cast<void>(database.Save());
        }
        return written;
    }
    for (const std::filesystem::path& loosePath : writtenLooseFiles) {
        static_cast<void>(database.Register(loosePath));
        written.push_back(loosePath);
    }
    if (saveDatabase && !written.empty()) {
        static_cast<void>(database.Save());
    }
    return written;
}

} // namespace

std::vector<std::filesystem::path> Editor::ImportModelAnimations(const std::filesystem::path& absoluteSource) {
    const Asset::ProjectFileSnapshot projectSnapshot = m_services->GetProjectFileIndex().GetSnapshot();
    std::error_code error;
    std::filesystem::path relativeSource = std::filesystem::relative(absoluteSource, projectSnapshot.contentRoot, error);
    if (error) {
        relativeSource.clear();
    }
    const bool keepInModel = !relativeSource.empty() && !ReadModelSourceInfoForSource(relativeSource).animationOnly;
    const std::filesystem::path importedModel = keepInModel ? GetImportedModelPath(relativeSource) : std::filesystem::path{};
    const bool retargetedCurrent = keepInModel && IsRetargetedModelCurrent(relativeSource);
    const ExtractedModelAnimations extracted = ExtractModelAnimationsOffThread(
        absoluteSource, projectSnapshot.contentRoot, importedModel, keepInModel, retargetedCurrent);
    return RegisterExtractedModelAnimations(m_services->GetAssetDatabase(), extracted.absoluteSource,
                                               extracted.contentRoot, extracted.importedModel, extracted.keepInModel,
                                               extracted.embeddedTakes, extracted.writtenLooseFiles, true);
}

EditorModelSourceInfo Editor::ReadModelSourceInfoForSource(const std::filesystem::path& relativeSource) {
    const std::string key = relativeSource.generic_string();
    if (const auto found = m_modelSources.find(key); found != m_modelSources.end()) {
        return found->second;
    }
    const Asset::ProjectFileSnapshot project = m_services->GetProjectFileIndex().GetSnapshot();
    EditorModelSourceInfo info = ReadModelSourceInfo(project.contentRoot / relativeSource);
    m_modelSources.emplace(key, info);
    return info;
}

namespace {

[[nodiscard]] std::filesystem::path RetargetMarkerOf(const std::filesystem::path& importedModel) {
    std::filesystem::path marker = importedModel;
    marker += ".retargeted";
    return marker;
}

[[nodiscard]] std::string SourceStamp(const std::filesystem::path& source) {
    std::error_code error;
    const std::filesystem::file_time_type time = std::filesystem::last_write_time(source, error);
    return error ? std::string{} : std::to_string(static_cast<long long>(time.time_since_epoch().count()));
}

} // namespace

bool Editor::IsRetargetedModelCurrent(const std::filesystem::path& relativeSource) const {
    if (!IsModelSourcePath(relativeSource)) {
        return false;
    }
    const std::filesystem::path imported = GetImportedModelPath(relativeSource);
    std::error_code error;
    if (imported.empty() || !std::filesystem::is_regular_file(imported, error)) {
        return false;
    }
    std::ifstream in(RetargetMarkerOf(imported));
    std::string stamp;
    if (!in || !std::getline(in, stamp)) {
        return false;
    }
    const std::string current = SourceStamp(m_services->GetProjectFileIndex().GetSnapshot().contentRoot / relativeSource);
    return !current.empty() && stamp == current;
}

void Editor::MarkRetargetedModel(const std::filesystem::path& relativeSource, const bool retargeted) {
    if (!IsModelSourcePath(relativeSource)) {
        return;
    }
    const std::filesystem::path marker = RetargetMarkerOf(GetImportedModelPath(relativeSource));
    std::error_code error;
    if (!retargeted) {
        std::filesystem::remove(marker, error);
        return;
    }
    std::ofstream out(marker, std::ios::trunc);
    out << SourceStamp(m_services->GetProjectFileIndex().GetSnapshot().contentRoot / relativeSource) << "\n";
}

namespace {

constexpr std::chrono::milliseconds kBatchItemBreathingYield{15};
constexpr std::chrono::milliseconds kExternalWriteStabilityDelay{300};
constexpr double kExternalWatchIdleIntervalSeconds = 2.0;
constexpr double kExternalWatchStabilizingIntervalSeconds = 0.35;
constexpr std::size_t kExternalCopyRefreshStride = 10U;

void HashCombine64(std::uint64_t& seed, const std::uint64_t value) noexcept {
    seed ^= value + 0x9E3779B97F4A7C15ULL + (seed << 6U) + (seed >> 2U);
}

void HashString64(std::uint64_t& seed, const std::string_view text) noexcept {
    std::uint64_t h = 1469598103934665603ULL;
    for (const unsigned char ch : text) {
        h = (h ^ ch) * 1099511628211ULL;
    }
    HashCombine64(seed, h);
}

[[nodiscard]] bool CopyFileAtomically(const std::filesystem::path& source,
                                         const std::filesystem::path& destination) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(source, ec)) {
        return false;
    }
    if (destination.has_parent_path()) {
        std::filesystem::create_directories(destination.parent_path(), ec);
        if (ec) {
            return false;
        }
    }
    std::filesystem::path tempDestination = destination;
    tempDestination += ".uvcopy-tmp";
    std::filesystem::remove(tempDestination, ec);
    ec.clear();
    std::filesystem::copy_file(source, tempDestination, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::error_code rmErr;
        std::filesystem::remove(tempDestination, rmErr);
        return false;
    }
    std::filesystem::rename(tempDestination, destination, ec);
    if (ec) {
        // Fallback when target cannot be replaced atomically (e.g. Windows replace semantics).
        ec.clear();
        std::filesystem::remove(destination, ec);
        ec.clear();
        std::filesystem::rename(tempDestination, destination, ec);
        if (ec) {
            ec.clear();
            std::filesystem::copy_file(tempDestination, destination, std::filesystem::copy_options::overwrite_existing, ec);
            std::error_code rmErr;
            std::filesystem::remove(tempDestination, rmErr);
            return !ec;
        }
    }
    return true;
}

} // namespace

Asset::IAssetImportQueue& Editor::ActiveModelImportQueue() const noexcept {
    if (m_bgModelImportQueue != nullptr) {
        return *m_bgModelImportQueue;
    }
    return m_services->GetAssetImportQueue();
}

void Editor::EnsureBackgroundModelImportWorker() {
    if (m_bgImportWorker.joinable()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_bgImportMutex);
        m_bgImportStop = false;
        m_bgImportCancel = false;
        m_bgImportHasPending = false;
    }
    m_bgImportWorker = std::thread([this]() {
        for (;;) {
            BgCopyTask copyTask;
            bool hasCopy = false;
            BgModelProbeTask probeTask;
            bool hasProbe = false;
            BgModelConvertTask modelTask;
            bool hasModel = false;
            BgAnimExtractTask animTask;
            bool hasAnim = false;
            bool cleanupCopyDirs = false;
            std::vector<std::filesystem::path> dirsToClean;

            {
                std::unique_lock<std::mutex> lock(m_bgImportMutex);
                m_bgImportCv.wait(lock, [this]() {
                    return m_bgImportStop || m_bgImportCancel || !m_bgCopyQueue.empty() || !m_bgProbeQueue.empty() ||
                           !m_bgModelConvertQueue.empty() || !m_bgAnimQueue.empty();
                });
                if (m_bgImportStop) {
                    return;
                }
                if (m_bgImportCancel) {
                    m_bgCopyQueue.clear();
                    m_bgProbeQueue.clear();
                    m_bgProbeQueuedKeys.clear();
                    m_bgModelConvertQueue.clear();
                    m_bgModelConvertInFlightKeys.clear();
                    m_bgAnimQueue.clear();
                    m_bgAnimQueuedKeys.clear();
                    dirsToClean.swap(m_bgCopyCleanupDirs);
                    cleanupCopyDirs = !dirsToClean.empty();
                    m_bgImportCancel = false;
                    m_bgImportPhase = BackgroundImportPhase::Idle;
                    m_bgImportCurrentItem.clear();
                } else if (!m_bgCopyQueue.empty()) {
                    copyTask = std::move(m_bgCopyQueue.front());
                    m_bgCopyQueue.pop_front();
                    hasCopy = true;
                    m_bgImportPhase = BackgroundImportPhase::Copying;
                    m_bgImportCurrentItem = copyTask.destination.filename().string();
                    if (m_bgCopyQueue.empty() && !m_bgCopyCleanupDirs.empty()) {
                        dirsToClean.swap(m_bgCopyCleanupDirs);
                        cleanupCopyDirs = true;
                    }
                } else if (!m_bgProbeQueue.empty()) {
                    probeTask = std::move(m_bgProbeQueue.front());
                    m_bgProbeQueue.pop_front();
                    hasProbe = true;
                    m_bgImportPhase = BackgroundImportPhase::Probing;
                    m_bgImportCurrentItem = probeTask.source.filename().string();
                } else if (!m_bgModelConvertQueue.empty()) {
                    modelTask = std::move(m_bgModelConvertQueue.front());
                    m_bgModelConvertQueue.pop_front();
                    hasModel = true;
                    m_bgImportPhase = BackgroundImportPhase::ImportingModel;
                    m_bgImportCurrentItem = modelTask.request.sourcePath.filename().string();
                } else if (!m_bgAnimQueue.empty()) {
                    animTask = std::move(m_bgAnimQueue.front());
                    m_bgAnimQueue.pop_front();
                    hasAnim = true;
                    m_bgImportPhase = BackgroundImportPhase::ExtractingAnimations;
                    m_bgImportCurrentItem = animTask.absoluteSource.filename().string();
                }
            }

            if (hasCopy) {
                const bool copied = CopyFileAtomically(copyTask.source, copyTask.destination);
                if (copied) {
                    std::lock_guard<std::mutex> lock(m_bgImportMutex);
                    m_bgCompletedCopies.push_back(copyTask.destination);
                    ++m_bgCopyBatchDone;
                } else {
                    std::lock_guard<std::mutex> lock(m_bgImportMutex);
                    ++m_bgCopyBatchDone;
                }
            } else if (hasProbe) {
                EditorModelSourceInfo info = ReadModelSourceInfo(probeTask.source);
                std::lock_guard<std::mutex> lock(m_bgImportMutex);
                m_bgProbeQueuedKeys.erase(probeTask.key);
                m_bgCompletedProbes.push_back(BgModelProbeResult{
                    std::move(probeTask.key), std::move(probeTask.source), std::move(probeTask.stamp), probeTask.size,
                    std::move(info)});
                ++m_bgProbeBatchDone;
            } else if (hasModel) {
                // One lightweight single-job queue per model keeps peak job history O(1) across 1,000+ imports.
                Asset::AssetImportQueue singleJobQueue(m_services->GetAssetImporter(),
                                                          m_services->GetAssetDatabase(),
                                                          m_services->GetDerivedArtifactCache());
                BgModelConvertResult outcome;
                outcome.key = modelTask.key;
                if (const std::optional<Asset::AssetImportJobId> id =
                        singleJobQueue.Enqueue(std::move(modelTask.request));
                    id.has_value()) {
                    static_cast<void>(singleJobQueue.Tick());
                    const std::vector<Asset::AssetImportJob> jobs = singleJobQueue.GetJobs();
                    const auto found = std::find_if(jobs.begin(), jobs.end(), [id](const Asset::AssetImportJob& job) {
                        return job.id == *id;
                    });
                    if (found != jobs.end() && found->state == Asset::AssetImportJobState::Succeeded) {
                        outcome.succeeded = true;
                        outcome.cacheHit = found->cacheHit;
                    } else {
                        outcome.errorReason = "the importer reported no reason";
                        if (found != jobs.end()) {
                            for (const Asset::AssetImportDiagnostic& diagnostic : found->diagnostics) {
                                if (diagnostic.severity == Asset::AssetImportDiagnosticSeverity::Error &&
                                    !diagnostic.message.empty()) {
                                    outcome.errorReason = diagnostic.message;
                                }
                            }
                        }
                    }
                } else {
                    outcome.errorReason = "unable to enqueue model import";
                }
                std::lock_guard<std::mutex> lock(m_bgImportMutex);
                m_bgCompletedModels.push_back(std::move(outcome));
            } else if (hasAnim) {
                ExtractedModelAnimations extracted = ExtractModelAnimationsOffThread(
                    animTask.absoluteSource, animTask.contentRoot, animTask.importedModel, animTask.keepInModel,
                    animTask.retargetedCurrent);
                BgAnimExtractResult res;
                res.absoluteSource = std::move(extracted.absoluteSource);
                res.contentRoot = std::move(extracted.contentRoot);
                res.importedModel = std::move(extracted.importedModel);
                res.keepInModel = extracted.keepInModel;
                res.embeddedTakes = std::move(extracted.embeddedTakes);
                res.writtenLooseFiles = std::move(extracted.writtenLooseFiles);
                std::lock_guard<std::mutex> lock(m_bgImportMutex);
                m_bgAnimQueuedKeys.erase(animTask.absoluteSource.generic_string());
                m_bgCompletedAnims.push_back(std::move(res));
                ++m_bgAnimBatchDone;
            }

            if (cleanupCopyDirs) {
                for (const std::filesystem::path& dir : dirsToClean) {
                    std::error_code rmErr;
                    std::filesystem::remove_all(dir, rmErr);
                }
            }

            // Gentle per-item pacing between items in a multi-item batch so low-end CPUs and
            // Snapdragon 665 Android devices stay responsive and cool without thermal/RAM spikes.
            std::unique_lock<std::mutex> lock(m_bgImportMutex);
            const bool morePending = !m_bgCopyQueue.empty() || !m_bgProbeQueue.empty() ||
                                     !m_bgModelConvertQueue.empty() || !m_bgAnimQueue.empty();
            if (!morePending) {
                m_bgImportPhase = BackgroundImportPhase::Idle;
                m_bgImportCurrentItem.clear();
            } else if (!m_bgImportStop && !m_bgImportCancel && !m_bgImportHasPending) {
                m_bgImportCv.wait_for(lock, kBatchItemBreathingYield,
                                      [this]() { return m_bgImportStop || m_bgImportCancel; });
            }
        }
    });
}

void Editor::StopBackgroundModelImportWorker() {
    if (m_externalScanCancel != nullptr) {
        m_externalScanCancel->store(true, std::memory_order_relaxed);
    }
    if (m_externalScanFuture.valid()) {
        static_cast<void>(m_externalScanFuture.get());
    }
    if (m_bgImportWorker.joinable()) {
        {
            std::lock_guard<std::mutex> lock(m_bgImportMutex);
            m_bgImportStop = true;
        }
        m_bgImportCv.notify_all();
        m_bgImportWorker.join();
    }
    m_bgModelImportQueue.reset();
}

void Editor::CancelBackgroundImports() {
    bool cancelledAny = false;
    {
        std::lock_guard<std::mutex> lock(m_bgImportMutex);
        const bool hasQueued = !m_bgCopyQueue.empty() || !m_bgProbeQueue.empty() || !m_bgModelConvertQueue.empty() ||
                               !m_bgAnimQueue.empty() || m_bgImportPhase != BackgroundImportPhase::Idle;
        if (hasQueued) {
            cancelledAny = true;
        }
        m_bgImportCancel = true;
        m_bgCopyQueue.clear();
        m_bgProbeQueue.clear();
        m_bgProbeQueuedKeys.clear();
        m_bgModelConvertQueue.clear();
        m_bgModelConvertInFlightKeys.clear();
        m_bgAnimQueue.clear();
        m_bgAnimQueuedKeys.clear();
        m_bgCopyBatchTotal = 0U;
        m_bgCopyBatchDone = 0U;
        m_bgProbeBatchTotal = 0U;
        m_bgProbeBatchDone = 0U;
        m_bgAnimBatchTotal = 0U;
        m_bgAnimBatchDone = 0U;
    }
    m_bgImportCv.notify_all();
    if (!m_pendingModelAnimations.empty() || !m_modelImportJobs.empty()) {
        cancelledAny = true;
    }
    m_pendingModelAnimations.clear();
    m_pendingModelAnimationKeys.clear();
    m_modelAnimationBatchTotal = 0U;
    m_modelAnimationBatchDone = 0U;
    m_modelImportJobs.clear();
    m_importBatchTotal = 0U;
    m_importBatchCompleted = 0U;
    if (cancelledAny) {
        m_contentStatusMessage = "Import cancelled.";
    }
}

void Editor::EnqueueBackgroundExternalCopies(
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> items,
    std::vector<std::filesystem::path> cleanupDirs) {
    if (items.empty()) {
        return;
    }
    EnsureBackgroundModelImportWorker();
    {
        std::lock_guard<std::mutex> lock(m_bgImportMutex);
        m_bgImportCancel = false;
        m_bgCopyBatchTotal += items.size();
        for (auto& [src, dst] : items) {
            m_bgCopyQueue.push_back(BgCopyTask{std::move(src), std::move(dst)});
        }
        for (std::filesystem::path& dir : cleanupDirs) {
            m_bgCopyCleanupDirs.push_back(std::move(dir));
        }
        m_bgImportHasPending = m_startupLoading;
    }
    m_bgImportCv.notify_one();
}

void Editor::SeedObservedContentTree(const Asset::ProjectFileSnapshot& snapshot) {
    m_lastObservedContentTree.clear();
    m_lastObservedContentTree.reserve(snapshot.entries.size());
    std::uint64_t signature = 1469598103934665603ULL;
    for (const Asset::ProjectFileEntry& entry : snapshot.entries) {
        const std::string key = entry.relativePath.generic_string();
        ExternalFileStat stat;
        stat.isDirectory = (entry.kind == Asset::ProjectFileEntryKind::Directory);
        if (!stat.isDirectory) {
            const std::filesystem::path absolute = snapshot.contentRoot / entry.relativePath;
            std::error_code ec;
            const auto wt = std::filesystem::last_write_time(absolute, ec);
            if (!ec) {
                stat.mtimeTicks = static_cast<std::int64_t>(wt.time_since_epoch().count());
                stat.size = std::filesystem::file_size(absolute, ec);
            }
        }
        HashString64(signature, key);
        HashCombine64(signature, stat.isDirectory ? 1ULL : 0ULL);
        HashCombine64(signature, static_cast<std::uint64_t>(stat.size));
        HashCombine64(signature, static_cast<std::uint64_t>(stat.mtimeTicks));
        m_lastObservedContentTree.emplace(key, stat);
    }
    m_lastContentSnapshotSignature = signature;
    m_hasContentSnapshotSignature = true;
}

bool Editor::PollExternalContentChangesNow() {
    if (m_services == nullptr) {
        return false;
    }
    Asset::IProjectChangeWatcher& watcher = m_services->GetProjectChangeWatcher();
    const std::uint64_t seqBefore = watcher.GetSnapshot().latestSequence;
    static_cast<void>(watcher.PollNow(m_services->GetAssetDatabase(), m_services->GetDerivedArtifactCache()));
    const Asset::ProjectChangeSnapshot watcherSnap = watcher.GetSnapshot();

    bool treeChanged = watcherSnap.latestSequence > seqBefore || watcherSnap.rescanRequired || !m_projectFileSnapshotInitialized;
    if (!treeChanged) {
        const std::filesystem::path contentRoot = m_services->GetProjectFileIndex().GetSnapshot().contentRoot;
        std::unordered_map<std::string, ExternalFileStat> currentTree;
        std::error_code ec;
        if (!contentRoot.empty() && std::filesystem::is_directory(contentRoot, ec)) {
            for (std::filesystem::recursive_directory_iterator it(
                     contentRoot, std::filesystem::directory_options::skip_permission_denied, ec),
                 end;
                 !ec && it != end; it.increment(ec)) {
                std::error_code stErr;
                const std::filesystem::file_status st = it->symlink_status(stErr);
                if (stErr || std::filesystem::is_symlink(st)) {
                    it.disable_recursion_pending();
                    continue;
                }
                const std::string fname = it->path().filename().string();
                if (fname.empty() || fname.front() == '.') {
                    if (std::filesystem::is_directory(st)) {
                        it.disable_recursion_pending();
                    }
                    continue;
                }
                if (std::filesystem::is_regular_file(st) && it->path().extension() == ".uvcopy-tmp") {
                    continue;
                }
                const std::filesystem::path rel = std::filesystem::relative(it->path(), contentRoot, stErr);
                if (stErr || rel.empty()) {
                    continue;
                }
                ExternalFileStat stat;
                stat.isDirectory = std::filesystem::is_directory(st);
                if (!stat.isDirectory) {
                    const auto wt = std::filesystem::last_write_time(it->path(), stErr);
                    if (!stErr) {
                        stat.mtimeTicks = static_cast<std::int64_t>(wt.time_since_epoch().count());
                        stat.size = std::filesystem::file_size(it->path(), stErr);
                    }
                }
                currentTree.emplace(rel.generic_string(), stat);
            }
        }
        treeChanged = (currentTree != m_lastObservedContentTree);
    }

    if (treeChanged) {
        m_projectFileLastObservedChangeSequence = watcherSnap.latestSequence;
        RefreshProjectFileIndex();
        return true;
    }
    return false;
}

void Editor::TickExternalContentWatcher() {
    if (!m_prismUiRequested || m_services == nullptr || !m_projectFileSnapshotInitialized ||
        m_playModeState != EditorPlayModeState::Edit) {
        return;
    }

    const bool focused = IsAnyEditorWindowFocused();
    if (!m_wasAnyWindowFocused && focused) {
        m_externalWatchImmediateRequested = true;
    }
    m_wasAnyWindowFocused = focused;

    const auto now = std::chrono::steady_clock::now();

    if (m_externalScanFuture.valid() &&
        m_externalScanFuture.wait_for(std::chrono::seconds::zero()) == std::future_status::ready) {
        std::unordered_map<std::string, ExternalFileStat> scanned = m_externalScanFuture.get();
        bool hasReadyDelta = false;

        // Detect deleted paths.
        for (const auto& entry : m_lastObservedContentTree) {
            if (scanned.find(entry.first) == scanned.end()) {
                hasReadyDelta = true;
                m_stabilizingExternalFiles.erase(entry.first);
            }
        }

        // Detect new or modified paths with 300 ms write-stability debounce for active writes.
        const auto fileNow = std::filesystem::file_time_type::clock::now();
        for (const auto& [key, newStat] : scanned) {
            const auto prevIt = m_lastObservedContentTree.find(key);
            const bool unchanged = (prevIt != m_lastObservedContentTree.end() && prevIt->second == newStat);
            if (unchanged) {
                m_stabilizingExternalFiles.erase(key);
                continue;
            }
            if (newStat.isDirectory) {
                hasReadyDelta = true;
                continue;
            }
            const auto stabIt = m_stabilizingExternalFiles.find(key);
            if (stabIt != m_stabilizingExternalFiles.end()) {
                if (stabIt->second.size != newStat.size || stabIt->second.mtimeTicks != newStat.mtimeTicks) {
                    stabIt->second.size = newStat.size;
                    stabIt->second.mtimeTicks = newStat.mtimeTicks;
                    stabIt->second.lastChangedAt = now;
                } else if (now - stabIt->second.lastChangedAt >= kExternalWriteStabilityDelay) {
                    m_stabilizingExternalFiles.erase(stabIt);
                    hasReadyDelta = true;
                }
            } else {
                const auto fileTime = std::filesystem::file_time_type(std::filesystem::file_time_type::duration(newStat.mtimeTicks));
                const bool recentlyWritten = (fileNow >= fileTime) &&
                                             ((fileNow - fileTime) < kExternalWriteStabilityDelay);
                if (recentlyWritten) {
                    m_stabilizingExternalFiles.emplace(key, StabilizingExternalFile{newStat.size, newStat.mtimeTicks, now});
                } else {
                    hasReadyDelta = true;
                }
            }
        }

        if (hasReadyDelta) {
            // Refresh index now; keep any still-writing files unacknowledged so they trigger once settled.
            RefreshProjectFileIndex();
            for (const auto& pending : m_stabilizingExternalFiles) {
                m_lastObservedContentTree.erase(pending.first);
            }
        }
    }

    if (!m_externalScanFuture.valid()) {
        const double interval = m_stabilizingExternalFiles.empty() ? kExternalWatchIdleIntervalSeconds
                                                                   : kExternalWatchStabilizingIntervalSeconds;
        const bool intervalElapsed = m_lastExternalWatchPoll == std::chrono::steady_clock::time_point{} ||
                                     std::chrono::duration<double>(now - m_lastExternalWatchPoll).count() >= interval;
        if (m_externalWatchImmediateRequested || intervalElapsed) {
            m_externalWatchImmediateRequested = false;
            m_lastExternalWatchPoll = now;
            const std::filesystem::path contentRoot = m_services->GetProjectFileIndex().GetSnapshot().contentRoot;
            if (!contentRoot.empty()) {
                std::shared_ptr<std::atomic<bool>> cancelFlag = m_externalScanCancel;
                m_externalScanFuture = std::async(
                    std::launch::async,
                    [contentRoot, cancelFlag = std::move(cancelFlag)]() -> std::unordered_map<std::string, ExternalFileStat> {
                        std::unordered_map<std::string, ExternalFileStat> result;
                        std::error_code ec;
                        if (!std::filesystem::is_directory(contentRoot, ec)) {
                            return result;
                        }
                        for (std::filesystem::recursive_directory_iterator it(
                                 contentRoot, std::filesystem::directory_options::skip_permission_denied, ec),
                             end;
                             !ec && it != end; it.increment(ec)) {
                            if (cancelFlag != nullptr && cancelFlag->load(std::memory_order_relaxed)) {
                                return {};
                            }
                            std::error_code stErr;
                            const std::filesystem::file_status st = it->symlink_status(stErr);
                            if (stErr || std::filesystem::is_symlink(st)) {
                                it.disable_recursion_pending();
                                continue;
                            }
                            const std::string fname = it->path().filename().string();
                            if (fname.empty() || fname.front() == '.') {
                                if (std::filesystem::is_directory(st)) {
                                    it.disable_recursion_pending();
                                }
                                continue;
                            }
                            if (std::filesystem::is_regular_file(st) && it->path().extension() == ".uvcopy-tmp") {
                                continue;
                            }
                            const std::filesystem::path rel = std::filesystem::relative(it->path(), contentRoot, stErr);
                            if (stErr || rel.empty()) {
                                continue;
                            }
                            ExternalFileStat stat;
                            stat.isDirectory = std::filesystem::is_directory(st);
                            if (!stat.isDirectory) {
                                const auto wt = std::filesystem::last_write_time(it->path(), stErr);
                                if (!stErr) {
                                    stat.mtimeTicks = static_cast<std::int64_t>(wt.time_since_epoch().count());
                                    stat.size = std::filesystem::file_size(it->path(), stErr);
                                }
                            }
                            result.emplace(rel.generic_string(), stat);
                        }
                        return result;
                    });
            }
        }
    }
}

void Editor::EnqueueModelSourceIfReady(const std::string& key, const std::filesystem::path& source,
                                             const EditorModelSourceInfo& info,
                                             const std::uint32_t maxEmbeddedTextureResolution) {
    const auto stampIt = m_modelSourceStampCache.find(key);
    const bool animsAlreadyDone = (stampIt != m_modelSourceStampCache.end() && stampIt->second.animsImported);
    if (info.animationCount > 0U && !animsAlreadyDone) {
        QueueModelAnimationImport(source);
    }
    if (info.animationOnly) {
        return; // Motion with nothing to draw: there is no mesh to import.
    }
    const std::filesystem::path relativePath(key);
    const std::filesystem::path importedModelPath = GetImportedModelPath(relativePath);
    if (stampIt != m_modelSourceStampCache.end() && stampIt->second.modelImported) {
        std::error_code existsErr;
        if (std::filesystem::is_regular_file(importedModelPath, existsErr)) {
            return; // Already converted and registered for this exact source stamp in this session.
        }
    }
    // A character Retarget conformed keeps its conformed model until its source changes:
    // importing it again would put the source's own bones back under clips made for the engine's.
    if (IsRetargetedModelCurrent(relativePath)) {
        static_cast<void>(RegisterImportedModelArtifacts(importedModelPath));
        return;
    }

    Asset::AssetImportRequest request;
    request.sourcePath = source;
    request.destinationPath = importedModelPath;
    auto modelSettings = std::make_shared<Asset::ModelImportSettings>();
    modelSettings->maxEmbeddedTextureResolution = maxEmbeddedTextureResolution;
    request.settings = std::move(modelSettings);

    if (m_prismUiRequested) {
        EnsureBackgroundModelImportWorker();
        bool notifyWorker = false;
        {
            std::lock_guard<std::mutex> lock(m_bgImportMutex);
            if (m_bgModelConvertInFlightKeys.insert(key).second) {
                m_bgImportCancel = false;
                m_bgModelConvertQueue.push_back(BgModelConvertTask{key, std::move(request)});
                ++m_importBatchTotal;
                m_bgImportHasPending = m_startupLoading;
                notifyWorker = true;
            }
        }
        if (notifyWorker) {
            m_bgImportCv.notify_one();
        }
        return;
    }

    if (m_modelImportJobs.find(key) != m_modelImportJobs.end()) {
        return;
    }
    Asset::IAssetImportQueue& queue = ActiveModelImportQueue();
    if (const std::optional<Asset::AssetImportJobId> job = queue.Enqueue(std::move(request));
        job.has_value()) {
        m_modelImportJobs.emplace(key, *job);
    }
}

void Editor::QueueModelAutoImports(const Asset::ProjectFileSnapshot& snapshot) {
    // Keep headless/test sessions on the deterministic main-thread queue; run heavy FBX/model
    // probing, conversion, and animation extraction one-by-one on the serialized background worker
    // in interactive PRISM sessions to prevent UI/ANR stalls on 500+ file drops.
    if (m_prismUiRequested) {
        EnsureBackgroundModelImportWorker();
    }
    const Render::RendererProfileResolution profileRes = GetRendererProfileResolution();
    const std::uint32_t maxEmbeddedTextureResolution =
        (profileRes.effective == Platform::RendererProfile::Compatibility) ? 1024U : 2048U;

    std::unordered_set<std::string> presentKeys;
    bool notifyProbes = false;
    m_modelSources.clear();
    for (const Asset::ProjectFileEntry& entry : snapshot.entries) {
        if (entry.kind == Asset::ProjectFileEntryKind::Directory || !IsModelSourcePath(entry.relativePath)) {
            continue;
        }
        const std::string key = entry.relativePath.generic_string();
        presentKeys.insert(key);
        const std::filesystem::path source = snapshot.contentRoot / entry.relativePath;
        const std::string stamp = SourceStamp(source);
        std::error_code sizeError;
        const std::uintmax_t fileSize = std::filesystem::file_size(source, sizeError);

        const auto cached = m_modelSourceStampCache.find(key);
        if (cached != m_modelSourceStampCache.end() && !stamp.empty() && !sizeError &&
            cached->second.stamp == stamp && cached->second.size == fileSize) {
            const EditorModelSourceInfo& info = m_modelSources[key] = cached->second.info;
            EnqueueModelSourceIfReady(key, source, info, maxEmbeddedTextureResolution);
            continue;
        }

        if (m_prismUiRequested) {
            std::lock_guard<std::mutex> lock(m_bgImportMutex);
            if (m_bgProbeQueuedKeys.insert(key).second) {
                m_bgImportCancel = false;
                m_bgProbeQueue.push_back(BgModelProbeTask{key, source, stamp, sizeError ? 0U : fileSize});
                ++m_bgProbeBatchTotal;
                m_bgImportHasPending = m_startupLoading;
                notifyProbes = true;
            }
            continue;
        }

        EditorModelSourceInfo parsedInfo = ReadModelSourceInfo(source);
        if (!stamp.empty() && !sizeError) {
            m_modelSourceStampCache[key] = CachedModelSourceStamp{stamp, fileSize, parsedInfo};
        }
        const EditorModelSourceInfo& info = m_modelSources[key] = std::move(parsedInfo);
        EnqueueModelSourceIfReady(key, source, info, maxEmbeddedTextureResolution);
    }
    for (auto it = m_modelSourceStampCache.begin(); it != m_modelSourceStampCache.end();) {
        if (presentKeys.find(it->first) == presentKeys.end()) {
            it = m_modelSourceStampCache.erase(it);
        } else {
            ++it;
        }
    }
    if (notifyProbes) {
        m_bgImportCv.notify_one();
    }
}

Asset::AssetGuid Editor::RegisterImportedModelArtifacts(const std::filesystem::path& importedModel) {
    Asset::IAssetDatabase& database = m_services->GetAssetDatabase();
    std::error_code error;
    if (!std::filesystem::is_regular_file(importedModel, error)) {
        return Asset::kInvalidAssetGuid;
    }
    const Asset::AssetGuid meshGuid = database.Register(importedModel);
    const std::filesystem::path directory = importedModel.parent_path();
    std::filesystem::directory_iterator it(directory, error);
    if (error) {
        return meshGuid;
    }
    const std::filesystem::directory_iterator end{};
    while (it != end) {
        std::string extension = it->path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
        // Everything an import publishes beside the mesh it converted. Registering a GUID twice is
        // how the database answers an existing path, so this is idempotent by construction.
        if (extension == ".uvmat" || extension == ".uvmaterial" || extension == ".uvtex" ||
            extension == ".uvskel") {
            static_cast<void>(database.Register(it->path()));
        }
        it.increment(error);
        if (error) {
            return meshGuid;
        }
    }
    return meshGuid;
}

void Editor::QueueModelAnimationImport(const std::filesystem::path& absoluteSource) {
    if (!m_prismUiRequested) {
        static_cast<void>(ImportModelAnimations(absoluteSource));
        return;
    }
    EnsureBackgroundModelImportWorker();
    const Asset::ProjectFileSnapshot projectSnapshot = m_services->GetProjectFileIndex().GetSnapshot();
    std::error_code error;
    std::filesystem::path relativeSource = std::filesystem::relative(absoluteSource, projectSnapshot.contentRoot, error);
    if (error) {
        relativeSource.clear();
    }
    const EditorModelSourceInfo* const knownInfo =
        !relativeSource.empty() ? FindModelSourceInfo(relativeSource) : nullptr;
    const bool keepInModel = !relativeSource.empty() && knownInfo != nullptr && !knownInfo->animationOnly;
    const std::filesystem::path importedModel = keepInModel ? GetImportedModelPath(relativeSource) : std::filesystem::path{};
    const bool retargetedCurrent = keepInModel && IsRetargetedModelCurrent(relativeSource);

    bool notify = false;
    {
        std::lock_guard<std::mutex> lock(m_bgImportMutex);
        if (m_bgAnimQueuedKeys.insert(absoluteSource.generic_string()).second) {
            m_bgImportCancel = false;
            m_bgAnimQueue.push_back(BgAnimExtractTask{
                absoluteSource, projectSnapshot.contentRoot, importedModel, keepInModel, retargetedCurrent});
            ++m_bgAnimBatchTotal;
            m_bgImportHasPending = m_startupLoading;
            notify = true;
        }
    }
    if (notify) {
        m_bgImportCv.notify_one();
    }
}

void Editor::ProcessPendingModelAnimationImports(const double budgetSeconds) {
    const auto began = std::chrono::steady_clock::now();
    while (!m_pendingModelAnimations.empty()) {
        const std::filesystem::path source = std::move(m_pendingModelAnimations.front());
        m_pendingModelAnimations.pop_front();
        m_pendingModelAnimationKeys.erase(source.generic_string());
        static_cast<void>(ImportModelAnimations(source));
        ++m_modelAnimationBatchDone;
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count() >= budgetSeconds) {
            break;
        }
    }
    if (m_pendingModelAnimations.empty()) {
        m_modelAnimationBatchTotal = 0U;
        m_modelAnimationBatchDone = 0U;
    }
}

void Editor::UpdateStartupLoading() {
    const auto now = std::chrono::steady_clock::now();
    if (m_startupBegan == std::chrono::steady_clock::time_point{}) {
        m_startupBegan = now;
        m_startupStepBegan = now;
    }
    const auto advance = [this, now] {
        ++m_startupStep;
        m_startupStepBegan = now;
    };
    const double inStep = std::chrono::duration<double>(now - m_startupStepBegan).count();
    bool bgBusy = false;
    {
        std::lock_guard<std::mutex> lock(m_bgImportMutex);
        bgBusy = !m_bgCopyQueue.empty() || !m_bgProbeQueue.empty() || !m_bgModelConvertQueue.empty() ||
                 !m_bgModelConvertInFlightKeys.empty() || !m_bgAnimQueue.empty() ||
                 !m_bgCompletedCopies.empty() || !m_bgCompletedProbes.empty() ||
                 !m_bgCompletedModels.empty() || !m_bgCompletedAnims.empty() ||
                 m_bgImportPhase != BackgroundImportPhase::Idle;
    }
    switch (m_startupStep) {
    case 0: // The project's files.
        if (m_projectFileSnapshotInitialized) {
            advance();
        }
        break;
    case 1: // Its models and the takes inside them.
        if (!bgBusy && m_modelImportJobs.empty() && m_pendingModelAnimations.empty()) {
            advance();
        }
        break;
    case 2: // The first animation previews (a few frames for the browser to ask for them).
        m_startupIconPeak = std::max(m_startupIconPeak, PendingAnimationIconCount());
        if ((inStep > 0.5 && PendingAnimationIconCount() == 0U) || inStep > 8.0) {
            advance();
        }
        break;
    default:
        m_startupLoading = false;
        break;
    }
    // Never held longer than this: what is left goes on behind the editor.
    if (std::chrono::duration<double>(now - m_startupBegan).count() > 180.0) {
        m_startupLoading = false;
    }
    if (!m_startupLoading) {
        std::lock_guard<std::mutex> lock(m_bgImportMutex);
        m_bgImportHasPending = false; // enable gentle 15 ms pacing for post-startup interactive batches
    }
}

Editor::EditorLoadingState Editor::GetLoadingState() const {
    EditorLoadingState state;
    BackgroundImportPhase bgPhase = BackgroundImportPhase::Idle;
    std::string bgItem;
    std::size_t copyTotal = 0U;
    std::size_t copyDone = 0U;
    std::size_t probeTotal = 0U;
    std::size_t probeDone = 0U;
    std::size_t bgModelInFlight = 0U;
    std::size_t animBgTotal = 0U;
    std::size_t animBgDone = 0U;
    bool bgActive = false;
    {
        std::lock_guard<std::mutex> lock(m_bgImportMutex);
        bgPhase = m_bgImportPhase;
        bgItem = m_bgImportCurrentItem;
        copyTotal = m_bgCopyBatchTotal;
        copyDone = m_bgCopyBatchDone;
        probeTotal = m_bgProbeBatchTotal;
        probeDone = m_bgProbeBatchDone;
        bgModelInFlight = m_bgModelConvertInFlightKeys.size();
        animBgTotal = m_bgAnimBatchTotal;
        animBgDone = m_bgAnimBatchDone;
        bgActive = bgPhase != BackgroundImportPhase::Idle || !m_bgCopyQueue.empty() || !m_bgProbeQueue.empty() ||
                   !m_bgModelConvertQueue.empty() || !m_bgModelConvertInFlightKeys.empty() || !m_bgAnimQueue.empty();
    }

    const std::size_t modelInFlight = m_modelImportJobs.size() + bgModelInFlight;
    const std::size_t modelTotal = std::max(m_importBatchTotal, modelInFlight + m_importBatchCompleted);
    const std::size_t animTotal = m_modelAnimationBatchTotal + animBgTotal;
    const std::size_t animDone = m_modelAnimationBatchDone + animBgDone;
    const std::size_t total = copyTotal + probeTotal + modelTotal + animTotal;
    const std::size_t done = std::min(total, copyDone + probeDone + m_importBatchCompleted + animDone);
    const float work = total > 0U ? std::clamp(static_cast<float>(done) / static_cast<float>(total), 0.0F, 1.0F) : 1.0F;

    std::string phaseLabel;
    if (bgPhase == BackgroundImportPhase::Copying || copyTotal > copyDone) {
        phaseLabel = "Copying assets";
    } else if (bgPhase == BackgroundImportPhase::Probing || probeTotal > probeDone) {
        phaseLabel = "Scanning models";
    } else if (bgPhase == BackgroundImportPhase::ImportingModel || modelInFlight > 0U) {
        phaseLabel = "Importing models";
    } else if (bgPhase == BackgroundImportPhase::ExtractingAnimations || animTotal > animDone ||
               !m_pendingModelAnimations.empty()) {
        phaseLabel = "Importing animations";
        if (bgItem.empty() && !m_pendingModelAnimations.empty()) {
            bgItem = m_pendingModelAnimations.front().filename().string();
        }
    }

    const std::string countText =
        total > 0U ? (std::to_string(std::min(done + (bgActive ? 1U : 0U), total)) + " / " + std::to_string(total))
                   : std::string{};

    if (m_startupLoading && m_state == EditorState::Running) {
        state.startup = true;
        state.done = done;
        state.total = total;
        state.currentItem = bgItem;
        switch (m_startupStep) {
        case 0:
            state.step = "Scanning project files";
            state.progress = 0.08F;
            break;
        case 1:
            state.step = phaseLabel.empty() ? "Importing models" : phaseLabel;
            state.detail = !countText.empty() && !bgItem.empty() ? (countText + " · " + bgItem)
                                                                  : (!countText.empty() ? countText : bgItem);
            state.progress = 0.15F + 0.60F * work;
            break;
        case 2: {
            state.step = "Preparing animation previews";
            const std::size_t left = PendingAnimationIconCount();
            const std::size_t peak = std::max(m_startupIconPeak, left);
            state.detail = left > 0U ? std::to_string(peak - left) + " / " + std::to_string(peak) : std::string{};
            state.progress = 0.75F + 0.23F * (peak > 0U ? static_cast<float>(peak - left) / static_cast<float>(peak) : 1.0F);
            break;
        }
        default:
            state.step = "Opening editor";
            state.progress = 1.0F;
            break;
        }
        return state;
    }

    if (bgActive || !phaseLabel.empty()) {
        state.busy = true;
        state.cancellable = true;
        state.done = done;
        state.total = total;
        state.currentItem = std::move(bgItem);
        state.step = phaseLabel.empty() ? "Importing assets" : std::move(phaseLabel);
        state.detail = countText;
        state.progress = work;
    }
    return state;
}

void Editor::PollModelImportJobs() {
    std::vector<std::filesystem::path> completedCopies;
    std::vector<BgModelProbeResult> completedProbes;
    std::vector<BgModelConvertResult> completedModels;
    std::vector<BgAnimExtractResult> completedAnims;
    bool copyQueueEmpty = true;
    bool bgCompletelyIdle = true;
    {
        std::lock_guard<std::mutex> lock(m_bgImportMutex);
        completedCopies.swap(m_bgCompletedCopies);
        completedProbes.swap(m_bgCompletedProbes);
        completedModels.swap(m_bgCompletedModels);
        completedAnims.swap(m_bgCompletedAnims);
        copyQueueEmpty = m_bgCopyQueue.empty();
        for (const BgModelConvertResult& res : completedModels) {
            m_bgModelConvertInFlightKeys.erase(res.key);
        }
        bgCompletelyIdle = m_bgCopyQueue.empty() && m_bgProbeQueue.empty() && m_bgModelConvertQueue.empty() &&
                           m_bgModelConvertInFlightKeys.empty() && m_bgAnimQueue.empty() &&
                           m_bgImportPhase == BackgroundImportPhase::Idle;
    }

    bool databaseDirty = false;
    bool needsIndexRefresh = false;
    bool importedNewMesh = false;

    if (!completedCopies.empty()) {
        Asset::IAssetDatabase& database = m_services->GetAssetDatabase();
        for (const std::filesystem::path& copiedPath : completedCopies) {
            static_cast<void>(database.Register(copiedPath));
            ++m_bgCopiesSinceRefresh;
        }
        databaseDirty = true;
        if (m_bgCopiesSinceRefresh >= kExternalCopyRefreshStride || copyQueueEmpty) {
            m_bgCopiesSinceRefresh = 0U;
            needsIndexRefresh = true;
        }
    }

    if (!completedProbes.empty()) {
        const Render::RendererProfileResolution profileRes = GetRendererProfileResolution();
        const std::uint32_t maxEmbeddedTextureResolution =
            (profileRes.effective == Platform::RendererProfile::Compatibility) ? 1024U : 2048U;
        for (BgModelProbeResult& probe : completedProbes) {
            if (!probe.stamp.empty()) {
                m_modelSourceStampCache[probe.key] = CachedModelSourceStamp{probe.stamp, probe.size, probe.info};
            }
            const EditorModelSourceInfo& stored = m_modelSources[probe.key] = std::move(probe.info);
            EnqueueModelSourceIfReady(probe.key, probe.source, stored, maxEmbeddedTextureResolution);
        }
    }

    if (!completedModels.empty()) {
        const std::filesystem::path contentRoot = m_services->GetProjectFileIndex().GetSnapshot().contentRoot;
        for (const BgModelConvertResult& outcome : completedModels) {
            if (outcome.succeeded) {
                static_cast<void>(RegisterImportedModelArtifacts(GetImportedModelPath(outcome.key)));
                if (const auto stampIt = m_modelSourceStampCache.find(outcome.key);
                    stampIt != m_modelSourceStampCache.end()) {
                    stampIt->second.modelImported = true;
                }
                if (const auto info = m_modelSources.find(outcome.key);
                    info != m_modelSources.end() && info->second.animationCount > 0U) {
                    if (const auto stampIt = m_modelSourceStampCache.find(outcome.key);
                        stampIt != m_modelSourceStampCache.end()) {
                        stampIt->second.animsImported = false;
                    }
                    QueueModelAnimationImport(contentRoot / outcome.key);
                }
                databaseDirty = true;
                importedNewMesh = importedNewMesh || !outcome.cacheHit;
                m_modelImportFailures.erase(outcome.key);
            } else {
                m_modelImportFailures[outcome.key] = outcome.errorReason;
                UVE_WARNING("Editor: could not import model {}: {}", outcome.key, outcome.errorReason);
            }
            ++m_importBatchCompleted;
        }
    }

    if (!completedAnims.empty()) {
        Asset::IAssetDatabase& database = m_services->GetAssetDatabase();
        for (const BgAnimExtractResult& anim : completedAnims) {
            const std::vector<std::filesystem::path> registered = RegisterExtractedModelAnimations(
                database, anim.absoluteSource, anim.contentRoot, anim.importedModel, anim.keepInModel,
                anim.embeddedTakes, anim.writtenLooseFiles, false);
            std::error_code relErr;
            const std::filesystem::path rel = std::filesystem::relative(anim.absoluteSource, anim.contentRoot, relErr);
            if (!relErr && !rel.empty()) {
                const bool modelReady = !anim.keepInModel || std::filesystem::is_regular_file(anim.importedModel, relErr);
                if (modelReady) {
                    if (const auto stampIt = m_modelSourceStampCache.find(rel.generic_string());
                        stampIt != m_modelSourceStampCache.end()) {
                        stampIt->second.animsImported = true;
                    }
                }
            }
            if (!registered.empty()) {
                databaseDirty = true;
                if (!anim.keepInModel) {
                    needsIndexRefresh = true;
                }
            }
        }
    }

    // Legacy / headless main-thread model import queue path (used when !m_prismUiRequested).
    if (!m_modelImportJobs.empty()) {
        if (m_importBatchTotal < m_modelImportJobs.size() + m_importBatchCompleted) {
            m_importBatchTotal = m_modelImportJobs.size() + m_importBatchCompleted;
        }
        const std::vector<Asset::AssetImportJob> jobs = ActiveModelImportQueue().GetJobs();
        for (auto it = m_modelImportJobs.begin(); it != m_modelImportJobs.end();) {
            const auto job = std::find_if(jobs.begin(), jobs.end(),
                                          [&it](const Asset::AssetImportJob& candidate) { return candidate.id == it->second; });
            if (job == jobs.end() || job->state == Asset::AssetImportJobState::Failed) {
                if (job != jobs.end()) {
                    std::string reason = "the importer reported no reason";
                    for (const Asset::AssetImportDiagnostic& diagnostic : job->diagnostics) {
                        if (diagnostic.severity == Asset::AssetImportDiagnosticSeverity::Error && !diagnostic.message.empty()) {
                            reason = diagnostic.message;
                        }
                    }
                    m_modelImportFailures[it->first] = std::move(reason);
                    UVE_WARNING("Editor: could not import model {}: {}", it->first, m_modelImportFailures[it->first]);
                }
                ++m_importBatchCompleted;
                it = m_modelImportJobs.erase(it);
            } else if (job->state == Asset::AssetImportJobState::Succeeded) {
                static_cast<void>(RegisterImportedModelArtifacts(GetImportedModelPath(it->first)));
                if (const auto info = m_modelSources.find(it->first); info != m_modelSources.end() && info->second.animationCount > 0U) {
                    QueueModelAnimationImport(m_services->GetProjectFileIndex().GetSnapshot().contentRoot / it->first);
                }
                databaseDirty = true;
                importedNewMesh = importedNewMesh || !job->cacheHit;
                m_modelImportFailures.erase(it->first);
                ++m_importBatchCompleted;
                it = m_modelImportJobs.erase(it);
            } else {
                ++it;
            }
        }
    }

    if (databaseDirty) {
        static_cast<void>(m_services->GetAssetDatabase().Save());
    }
    if (importedNewMesh) {
        ClearMeshThumbnailCache(); // A freshly converted mesh has a thumbnail now.
    }
    if (needsIndexRefresh) {
        RefreshProjectFileIndex();
    }

    if (bgCompletelyIdle && m_modelImportJobs.empty()) {
        std::lock_guard<std::mutex> lock(m_bgImportMutex);
        if (m_bgCopyQueue.empty() && m_bgProbeQueue.empty() && m_bgModelConvertQueue.empty() &&
            m_bgModelConvertInFlightKeys.empty() && m_bgAnimQueue.empty() &&
            m_bgImportPhase == BackgroundImportPhase::Idle) {
            m_bgCopyBatchTotal = 0U;
            m_bgCopyBatchDone = 0U;
            m_bgProbeBatchTotal = 0U;
            m_bgProbeBatchDone = 0U;
            m_bgAnimBatchTotal = 0U;
            m_bgAnimBatchDone = 0U;
            m_importBatchTotal = 0U;
            m_importBatchCompleted = 0U;
        }
    }
}

void Editor::RefreshProjectFileIndex() {
    Asset::IProjectFileIndex& projectFileIndex = m_services->GetProjectFileIndex();
    Asset::IProjectChangeWatcher& projectChangeWatcher = m_services->GetProjectChangeWatcher();
    const Asset::ProjectChangeSnapshot changesBeforeRefresh = projectChangeWatcher.GetSnapshot();
    m_projectFileLastRefreshSucceeded = projectFileIndex.Refresh(m_services->GetAssetDatabase());
    m_projectFileSnapshotInitialized = true;
    if (m_projectFileLastRefreshSucceeded) {
        projectChangeWatcher.AcknowledgeThrough(changesBeforeRefresh.latestSequence);
        m_projectFileRefreshAttemptedForRescan = false;
        if (changesBeforeRefresh.rescanRequired) {
            // A successful full index refresh is the explicit boundary that safely clears watcher overflow.
            projectChangeWatcher.AcknowledgeRescan();
        }
        const Asset::ProjectFileSnapshot refreshed = projectFileIndex.GetSnapshot();
        const std::uint64_t previousSignature = m_lastContentSnapshotSignature;
        const bool hadPreviousSignature = m_hasContentSnapshotSignature;
        SeedObservedContentTree(refreshed);
        // Only clear cached thumbnails when on-disk content actually changed.
        if (!hadPreviousSignature || m_lastContentSnapshotSignature != previousSignature) {
            ClearTextureThumbnailCache();
            ClearMeshThumbnailCache();
            ClearMaterialThumbnailCache();
        }
        QueueModelAutoImports(refreshed);
    } else {
        m_projectFileRefreshAttemptedForRescan = changesBeforeRefresh.rescanRequired;
    }
}

} // namespace UVE::Editor
