// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/asset/fbx_mesh_converter_uve.h"
#include "uve/asset/gltf_metadata_uve.h"
#include "uve/editor/editor_uve.h"
#include "uve/editor/editor_settings_uve.h"
#include "editor_settings_binding_uve.h"
#include "uve/editor/editor_render_stats_uve.h"
#include "uve/logging/logger_uve.h"

#include "editor_chrome_layout_uve.h"
#include "editor_entity_label_uve.h"
#include "editor_text_search_uve.h"
#include "editor_fonts_uve.h"
#include "editor_object_icons_uve.h"
#include "uve/editor/editor_theme_uve.h"
#include <algorithm>
#include <chrono>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
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
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "uve/asset/bmp_metadata_uve.h"
#include "uve/asset/jpeg_metadata_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/png_metadata_uve.h"
#include "uve/asset/tga_metadata_uve.h"
#include "uve/asset/texture_asset_uve.h"
#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/config/i_config_manager_uve.h"
#include "uve/physics/raycast_query_uve.h"
#include "uve/rhi/render_resource_descs_uve.h"
#include "uve/platform/editor_project_package_uve.h"
#include "uve/component/area_component_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/animation_graph_component_uve.h"
#include "uve/component/animation_sequencer_component_uve.h"
#include "uve/component/audio_source_component_uve.h"
#include "uve/component/auto_translate_component_uve.h"
#include "uve/component/bone_modifier_component_uve.h"
#include "uve/component/canvas_component_uve.h"
#include "uve/component/editor_description_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/object_metadata_component_uve.h"
#include "uve/component/particle_emitter_component_uve.h"
#include "uve/component/physics_object_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/component/solid_body_component_uve.h"
#include "uve/component/thread_group_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/objects/3d/all_objects_3d_uve.h"
#include "uve/objects/canvas/all_objects_canvas_uve.h"
#include "uve/scene/objects/scene_object_type_uve.h"
#include "uve/scene/scene_component_metadata_uve.h"
#include "uve/scene/spawn_point_query_uve.h"
#include "uve/editor/editor_content_catalogue_uve.h"
#include "uve/core/engine_project_settings_uve.h"
#include "uve/input/key_code_uve.h"
#include "uve/object/scene_folder_uve.h"
#include "uve/object/object_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/prefab_instance_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/component/world_transform_component_uve.h"

namespace UVE::Editor {

namespace {

constexpr float kVectorEpsilonUVE = 0.00001F;
constexpr float kMinimumLocalScaleUVE = 0.001F;

// Subsetted Liberation Sans Regular (SIL OFL 1.1 licensed; see
// engine/editor/assets/fonts/THIRD_PARTY_NOTICES.md), the editor's main UI text font - replaces
// ImGui::AddFontDefault()'s built-in low-resolution bitmap font with a real, legible sans-serif.
#include "uve_ui_font_bytes.inc"

// Subsetted Tabler Icons glyphs (MIT licensed; see
// engine/editor/assets/fonts/THIRD_PARTY_NOTICES.md) merged into the default ImGui font. These
// back the menu bar and dock-panel titles below: ImGui::BeginMenu()/ImGui::Begin() only accept a
// plain text label, so an inline ImGui::Image() glyph is not an option there the way it is for the
// editor's existing RGBA-texture ImageButton icons (gizmo modes, Snap, object/component add popups).
#include "uve_icon_font_bytes.inc"

// Subsetted Liberation Mono Regular (SIL OFL 1.1 licensed; see
// engine/editor/assets/fonts/THIRD_PARTY_NOTICES.md) - a separate, non-merged font applied only to
// the Inspector's numeric Transform fields, matching a design mockup's own use of a monospace font
// for numeric values. Not merged into the main UI font's glyph atlas since it's selected per-widget
// via ImGui::PushFont()/PopFont(), not blended into every string the main font already renders.
#include "uve_mono_font_bytes.inc"

constexpr float kUiFontSizePixelsUVE = 16.0F;


constexpr ImWchar kIconFontGlyphRangesUVE[] = {
    0xEA03, 0xEA03, // Inspector (adjustments)
    0xEA45, 0xEA45, // Assets (box)
    0xEA54, 0xEA54, // Viewport (camera)
    0xEA98, 0xEA98, // Edit
    0xEAA4, 0xEAA4, // File
    0xEAAD, 0xEAAD, // Filesystem (folder)
    0xEB2E, 0xEB2E, // Favorites (star)
    0xEBD9, 0xEBD9, // Plugin
    0xEDBA, 0xEDBA, // Window (layout-grid)
    0xF91D, 0xF91D, // Help (help-circle)
    0xFA97, 0xFA97, // GameObject (cube)
    0xFAF7, 0xFAF7, // Contents (folder-open)
    0xFAFA, 0xFAFA, // Scene (list-tree)
    0,
};

// Full menu/panel labels, icon glyph baked in: ImGui::BeginMenu()/Begin() take a single string
// literal-shaped argument, so these can't be built from a separate icon constant concatenated at
// the call site the way adjacent string literals can (kMenuIconFileUVE is a runtime const char*,
// not a literal token, so " File" adjacency wouldn't compile).

[[nodiscard]] Math::Vector3UVE PrimitiveColliderHalfExtentsUVE(const Scene::PrimitiveMeshKindUVE kind) noexcept {
    // Sourced from the object definitions rather than restated here. These half-extents are the same
    // authored defaults BoxMesh3D/SphereMesh3D/PlaneMesh3D attach at creation, and a second copy
    // of them is a second place to edit: changing a primitive's collider in its definition while
    // this switch kept the old number would give an entity different collision depending on
    // whether it was created as that kind or converted to it - a difference nothing would report.
    switch (kind) {
        case Scene::PrimitiveMeshKindUVE::Cube:
            return Scene::BoxMesh3DObjectDefinitionUVE{}.collider.halfExtents;
        case Scene::PrimitiveMeshKindUVE::UVSphere:
            return Scene::SphereMesh3DObjectDefinitionUVE{}.collider.halfExtents;
        case Scene::PrimitiveMeshKindUVE::Plane:
            return Scene::PlaneMesh3DObjectDefinitionUVE{}.collider.halfExtents;
    }
    return Scene::BoxMesh3DObjectDefinitionUVE{}.collider.halfExtents;
}
constexpr float kGizmoAxisLengthUVE = 1.25F;
constexpr float kGizmoHandleRadiusPixelsUVE = 12.0F;
constexpr float kTrackballRadiusPixelsUVE = 42.0F;
constexpr float kTrackballAntipodalDotThresholdUVE = -0.999F;
constexpr float kBottomDockTabHeightUVE = 24.0F;
// Shrunk from 30 now that this row also hosts the Play/Pause/Stop transport buttons (moved out of
// the old menu row) alongside the Scene/Scripting/Game workspace tabs, decluttering both rows
// instead of leaving a tall strip that only ever held two small tab buttons.
constexpr float kEditorViewportToolCanvasHeightUVE = 30.0F;
/// Square resolution rendered for each Content Browser mesh thumbnail (see
/// EditorUVE::GetMeshThumbnailUVE). Matches the content-type badge icons' own baked resolution
/// (uve_content_type_icon_bytes.inc) - plenty of detail at the grid card's much smaller display
/// size without being wasteful to render per mesh.
constexpr int kMeshThumbnailSizeUVE = 64;
constexpr float kScriptCanvasLongPressThresholdSecondsUVE = 0.55F;
constexpr float kScriptCanvasLongPressMaxMovementPixelsUVE = 8.0F;
constexpr float kMinimumViewportDistanceUVE = 0.5F;
constexpr float kMaximumViewportDistanceUVE = 500.0F;
constexpr float kMaximumViewportPitchRadiansUVE = 1.4835299F; // 85 degrees.
constexpr float kViewportOrbitRadiansPerPixelUVE = 0.008F;
constexpr float kViewportZoomExponentPerWheelUnitUVE = 0.16F;
constexpr float kViewportNavigationRadiusPixelsUVE = 32.0F;
constexpr float kViewportNavigationHitRadiusPixelsUVE = 16.0F;
constexpr float kViewportNavigationPlateRadiusPixelsUVE = 47.0F;
constexpr float kMinimum2DCanvasZoomUVE = 0.10F;
constexpr float kMaximum2DCanvasZoomUVE = 4.00F;

// Side-panel widths derived from the editor's visual reference (a 1280px-wide window shows the
// Scene panel at ~216px and the Inspector at ~256px): the proportional term hits those exact
// values at 1280, and the clamps keep both sensible on very small and very large windows. Narrower
// than the previous 0.19/0.22 (243/281 at 1280) so the center viewport - the primary workspace -
// keeps the majority of the width instead of being squeezed by the side panels.



[[nodiscard]] bool IsWhitespaceOnlyUVE(const std::string_view value) noexcept {
    return std::all_of(value.begin(), value.end(), [](const char character) noexcept {
        return std::isspace(static_cast<unsigned char>(character)) != 0;
    });
}

[[nodiscard]] bool AreTransformsEqualUVE(const Scene::TransformComponentUVE& lhs,
                                         const Scene::TransformComponentUVE& rhs) noexcept {
    return lhs.localPosition.x == rhs.localPosition.x && lhs.localPosition.y == rhs.localPosition.y &&
           lhs.localPosition.z == rhs.localPosition.z && lhs.localRotation.x == rhs.localRotation.x &&
           lhs.localRotation.y == rhs.localRotation.y && lhs.localRotation.z == rhs.localRotation.z &&
           lhs.localRotation.w == rhs.localRotation.w && lhs.localScale.x == rhs.localScale.x &&
           lhs.localScale.y == rhs.localScale.y && lhs.localScale.z == rhs.localScale.z;
}

[[nodiscard]] std::filesystem::path MakeRecoveryPathUVE(const std::filesystem::path& scenePath) {
    std::filesystem::path recoveryPath = scenePath;
    recoveryPath += ".editor-recovery";
    return recoveryPath;
}

[[nodiscard]] bool DecodeRawImageThumbnailPixelsUVE(const std::filesystem::path& absolutePath,
                                                     std::uint32_t& outWidth, std::uint32_t& outHeight,
                                                     std::vector<std::byte>& outPixels);

[[nodiscard]] bool IsFiniteUVE(const float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] Math::QuaternionUVE ConjugateUVE(const Math::QuaternionUVE& value) noexcept {
    return Math::QuaternionUVE{-value.x, -value.y, -value.z, value.w};
}

[[nodiscard]] Scene::Objects::SceneObjectKindUVE ToSceneObjectKindUVE(const EditorEntityKindUVE kind) noexcept {
    using Kind = Scene::Objects::SceneObjectKindUVE;
    switch (kind) {
        case EditorEntityKindUVE::Empty:
            return Kind::Object3D;
        case EditorEntityKindUVE::Camera:
            return Kind::Camera3D;
        case EditorEntityKindUVE::DirectionalLight:
            return Kind::Light3D;
        case EditorEntityKindUVE::CollisionBox:
            return Kind::Collider3D;
        case EditorEntityKindUVE::Cube:
            return Kind::BoxMesh3D;
        case EditorEntityKindUVE::UVSphere:
            return Kind::SphereMesh3D;
        case EditorEntityKindUVE::Plane:
            return Kind::PlaneMesh3D;
    }
    return Kind::Object3D;
}

} // namespace

// Set once in InitUVE() after the atlas is built; has static storage duration for the life of the
// process like the byte arrays above, so no lifetime/ownership tracking is needed beyond that.
// A plain file-scope pointer (not a class member) keeps ImFont out of editor_uve.h, matching that
// header's "no Dear ImGui type in this public interface" design.
//
// At namespace scope rather than in the anonymous namespace above, because the inspector panel
// moved to its own translation unit and reads it. Declared in editor_fonts_uve.h - an INTERNAL
// header, so the imgui type stays inside this library rather than reaching public consumers.
ImFont* g_monoFontUVE = nullptr;

EditorUVE::EditorUVE(Core::EngineServicesUVE& services, std::filesystem::path activeScenePath,
                     const std::size_t historyCapacity, Core::ISimulationControlUVE* const simulationControl)
    : m_services(&services),
      m_simulationControl(simulationControl),
      m_activeScenePath(std::move(activeScenePath)),
      m_historyCapacity(std::max<std::size_t>(std::size_t{1U}, historyCapacity)) {
    RegisterBuiltInInspectorDrawersUVE();
    if (!RegisterEditorSettingsUVE(m_settingsRegistry)) {
        throw std::logic_error("Failed to register the editor settings.");
    }
    RegisterEditorCommandsUVE();
}

EditorUVE::~EditorUVE() {
    ShutdownUVE();
}

void EditorUVE::InitUVE() {
    if (m_state != EditorStateUVE::Uninitialized) {
        return;
    }

    Window::IWindowManagerUVE& windowManager = m_services->GetWindowManagerUVE();
    if (windowManager.IsValidUVE() && windowManager.GetNativeWindowHandleUVE() != nullptr) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        // Docking, and viewports: a window may live in its own OS window (the Entity Editor
        // does). The extra windows share the main GL context's objects, so the viewport's
        // rendered texture shows in them too; RenderOverlayUVE() presents them each frame.
        // The editor's own panels stay inside the main window - they are placed there and
        // cannot be dragged out.
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
        ImGui::StyleColorsDark();
        ApplyEditorVisualThemeUVE();

        // Font setup must happen before ImGui_ImplOpenGL3_Init() below: that call builds and
        // uploads the font atlas texture immediately, so any fonts merged in afterward would be
        // silently missing from what actually gets rendered.
        ImGuiIO& io = ImGui::GetIO();
        ImFontConfig uiFontConfig{};
        // Same "static storage duration for the life of the process" reasoning as the icon font
        // below - the .inc byte array must outlive the atlas and must not be freed by it.
        uiFontConfig.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF(const_cast<std::uint8_t*>(uve_ui_font_ttf_bytes.data()),
                                       static_cast<int>(uve_ui_font_ttf_bytes.size()),
                                       kUiFontSizePixelsUVE, &uiFontConfig);
        ImFontConfig iconFontConfig{};
        iconFontConfig.MergeMode = true;
        iconFontConfig.PixelSnapH = true;
        // The .inc byte array has static storage duration for the life of the process; ImGui must
        // not take ownership and free() it via its own allocator.
        iconFontConfig.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF(const_cast<std::uint8_t*>(uve_icon_font_ttf_bytes.data()),
                                       static_cast<int>(uve_icon_font_ttf_bytes.size()), 0.0F,
                                       &iconFontConfig, kIconFontGlyphRangesUVE);

        ImFontConfig monoFontConfig{};
        // Same "static storage duration for the life of the process" reasoning as the other two
        // fonts above - the .inc byte array must outlive the atlas and must not be freed by it.
        monoFontConfig.FontDataOwnedByAtlas = false;
        g_monoFontUVE = io.Fonts->AddFontFromMemoryTTF(const_cast<std::uint8_t*>(uve_mono_font_ttf_bytes.data()),
                                                        static_cast<int>(uve_mono_font_ttf_bytes.size()),
                                                        kUiFontSizePixelsUVE, &monoFontConfig);

        auto* const nativeWindow = static_cast<GLFWwindow*>(windowManager.GetNativeWindowHandleUVE());
        // Install the backend's chained GLFW callbacks so the interactive overlay receives cursor
        // and pointer-button events while WindowManagerUVE's existing close/resize/focus callbacks
        // remain active. Engine input remains a separate service-level abstraction; overlay clicks
        // consume ImGui pointer state and never leak into runtime action mappings.
        const bool glfwInitialized = ImGui_ImplGlfw_InitForOpenGL(nativeWindow, true);
        const bool openglInitialized = glfwInitialized && ImGui_ImplOpenGL3_Init("#version 450 core");
        if (openglInitialized) {
            static_cast<void>(m_uiAssets.InitializeUVE());
            m_meshThumbnailRenderer.InitializeUVE();
            m_uiInitialized = true;
        } else {
            if (glfwInitialized) {
                ImGui_ImplGlfw_Shutdown();
            }
            ImGui::DestroyContext();
        }
    }

    m_state = EditorStateUVE::Running;
    LoadSessionSettingsUVE();
    LoadSharedShelvesUVE();
    // A document always has exactly one Object - the structural anchor at the top of the
    // hierarchy. A fresh editor start is an empty document with just the root.
    static_cast<void>(EnsureDocumentObjectUVE());
    static_cast<void>(EnsureDocumentLayoutUVE());
    ClearHistoryUVE();
    m_sceneDirty = false;
    // Pause on Error counts Error-and-worse records through its own sink. The logger owns the
    // sink from here; the editor only polls its count, never calls back into it. No logger (a
    // use that never initialized the engine) simply leaves the pointer null and the poll a
    // no-op.
    if (Debug::LoggerUVE* const logger = Debug::LoggerUVE::GetActiveInstanceUVE(); logger != nullptr) {
        auto sink = std::make_unique<PlayPauseOnErrorSinkUVE>();
        m_playPauseOnErrorSink = sink.get();
        logger->AddSink(std::move(sink));
    }
}

void EditorUVE::TickUVE() {
    if (m_state != EditorStateUVE::Running) {
        return;
    }

    const Asset::ProjectChangeSnapshotUVE changeSnapshot = m_services->GetProjectChangeWatcherUVE().GetSnapshotUVE();
    const bool firstProjectIndexRefresh = !m_projectFileSnapshotInitialized;
    const bool newProjectChangeBaseline = changeSnapshot.latestSequence > m_projectFileLastObservedChangeSequence;
    const bool pendingRescanRetry = changeSnapshot.rescanRequired && !m_projectFileRefreshAttemptedForRescan;
    if (firstProjectIndexRefresh || newProjectChangeBaseline || pendingRescanRetry) {
        m_projectFileLastObservedChangeSequence = changeSnapshot.latestSequence;
        RefreshProjectFileIndexUVE();
    }
    PollModelImportJobsUVE();
    PollPlayPauseOnErrorUVE();

    std::erase_if(m_lockedHierarchyEntities,
                  [this](const Scene::EntityUVE entity) { return !IsDocumentEntityUVE(entity); });
    PruneSelectionUVE();
    if (m_hierarchyRenameEntity != Scene::kInvalidEntityUVE &&
        (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE() ||
         !IsDocumentEntityUVE(m_hierarchyRenameEntity) || m_hierarchyRenameEntity != m_selectedEntity)) {
        CancelHierarchyRenameUVE();
    }
}

bool EditorUVE::EnterPlayModeUVE() {
    // The document is an entity while the Entity Editor is open; the scene plays, not it.
    if (m_entityEditSession.has_value() || m_retargetPreview.has_value()) {
        return false;
    }
    // A colour still being picked belongs to the document being played; finish it first.
    static_cast<void>(CommitComponentPropertyPreviewUVE());
    if (m_state != EditorStateUVE::Running || m_playModeState != EditorPlayModeStateUVE::Edit ||
        m_simulationControl == nullptr || !IsAuthoringCommandAllowedUVE()) {
        return false;
    }

    // Saved before the snapshot, so the saved file and the state Play restores are the same.
    if (m_playSaveSceneFirst && m_sceneDirty && !m_activeScenePath.empty()) {
        static_cast<void>(SaveSceneUVE());
    }
    const std::vector<Scene::EntityUVE> roots = GetDocumentRootsUVE();
    PlayModeSessionUVE session{};
    session.capturedEmptyDocument = roots.empty();
    if (!session.capturedEmptyDocument) {
        const std::optional<Scene::SceneSnapshotUVE> snapshot =
            m_services->GetSceneSerializerUVE().CaptureUVE(
                m_services->GetEntityManagerUVE(), roots, Asset::AssetKindUVE::Scene);
        if (!snapshot.has_value()) {
            return false;
        }
        session.documentSnapshot = *snapshot;
    }
    session.dirtyBefore = m_sceneDirty;
    session.selectionBefore = CaptureSelectionPathsUVE(roots);

    if (!m_simulationControl->SetTransientSimulationSessionActiveUVE(true)) {
        return false;
    }
    if (!m_simulationControl->SetSimulationExecutionModeUVE(Core::SimulationExecutionModeUVE::Running)) {
        static_cast<void>(m_simulationControl->SetTransientSimulationSessionActiveUVE(false));
        return false;
    }

    m_playModeSession = std::move(session);
    m_playModeState = EditorPlayModeStateUVE::Playing;
    static_cast<void>(m_simulationControl->SetEditorPlaySceneNameUVE(m_activeScenePath.stem().string()));
    // Play-entry spawn resolution, inside the snapshot's protection and never allowed to fail
    // entering Play itself: a scene with no player or no enabled spawn point simply plays from
    // the authored poses.
    static_cast<void>(ApplyPlayEntrySpawnUVE());
    m_workspaceBeforePlayMode = m_activeWorkspace;
    if (m_playSwitchToGame) {
        m_activeWorkspace = EditorWorkspaceUVE::Game;
    }
    if (m_playPauseOnStart) {
        static_cast<void>(PausePlayModeUVE());
    }
    BeginEditorPlayBootSplashUVE();
    // Errors logged up to here belong to entering Play, not to playing; the pause-on-error
    // baseline starts now, so only errors from live play can pause.
    if (m_playPauseOnErrorSink != nullptr) {
        m_playPauseOnErrorSeenErrors = m_playPauseOnErrorSink->GetErrorCountUVE();
    }
    return true;
}

void EditorUVE::BeginEditorPlayBootSplashUVE() {
    m_playBootSplashActive = false;
    m_playBootSplashSkipRequested = false;
    m_playBootSplashFadeSeconds = 0.25;
    m_playBootSplashMinimumSeconds = 1.0;
    m_playBootSplashSkippable = true;
    m_playBootSplashBackground = {0.05F, 0.05F, 0.05F, 1.0F};
    m_playBootSplashImagePath.clear();
    m_playBootSplashImageDimensions = Math::Vector2UVE{};
    if (m_services == nullptr) {
        return;
    }

    try {
        const Config::SettingsDocumentUVE& settings = m_services->GetProjectSettingsUVE();
        const auto readValue = [&settings](const std::string_view id) {
            return settings.GetValueUVE(id);
        };
        const std::optional<Config::SettingValueUVE> skipValue =
            readValue(Core::EngineProjectSettingIdUVE::kSkipBootSplashInEditorPlayModeUVE);
        if (!skipValue.has_value() || std::get<bool>(*skipValue)) {
            return;
        }

        // These reads have already been type/range checked by the project-settings registry. Keep
        // the conversions direct; path containment below remains a consumer-specific safety check.
        if (const std::optional<Config::SettingValueUVE> fadeValue =
                readValue(Core::EngineProjectSettingIdUVE::kBootSplashFadeSecondsUVE);
            fadeValue.has_value()) {
            m_playBootSplashFadeSeconds = std::get<double>(*fadeValue);
        }
        if (const std::optional<Config::SettingValueUVE> minimumValue =
                readValue(Core::EngineProjectSettingIdUVE::kBootSplashMinimumDisplaySecondsUVE);
            minimumValue.has_value()) {
            m_playBootSplashMinimumSeconds = std::get<double>(*minimumValue);
        }
        if (const std::optional<Config::SettingValueUVE> skippableValue =
                readValue(Core::EngineProjectSettingIdUVE::kBootSplashSkippableUVE);
            skippableValue.has_value()) {
            m_playBootSplashSkippable = std::get<bool>(*skippableValue);
        }
        if (const std::optional<Config::SettingValueUVE> backgroundValue =
                readValue(Core::EngineProjectSettingIdUVE::kBootBackgroundColorUVE);
            backgroundValue.has_value()) {
            const Config::SettingColorUVE& color = std::get<Config::SettingColorUVE>(*backgroundValue);
            m_playBootSplashBackground = {color.r, color.g, color.b, color.a};
        }
        if (const std::optional<Config::SettingValueUVE> imageValue =
                readValue(Core::EngineProjectSettingIdUVE::kBootSplashImageUVE);
            imageValue.has_value()) {
            const std::string& imagePath = std::get<std::string>(*imageValue);
            if (!imagePath.empty()) {
                const std::filesystem::path relativeImage{imagePath};
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
                m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot;
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
                Asset::TextureAssetUVE importedTexture;
                std::uint32_t width = 0U;
                std::uint32_t height = 0U;
                std::vector<std::byte> pixels;
                std::string extension = absoluteImage.extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                               [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
                bool imageRead = false;
                if (extension == ".uvtex") {
                    imageRead = Asset::LoadTextureAssetUVE(canonicalImage, importedTexture);
                    if (imageRead) {
                        width = importedTexture.width;
                        height = importedTexture.height;
                    }
                } else {
                    imageRead = DecodeRawImageThumbnailPixelsUVE(canonicalImage, width, height, pixels);
                }
                if (imageRead && width > 0U && height > 0U) {
                    m_playBootSplashImageDimensions = Math::Vector2UVE{static_cast<float>(width),
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

void EditorUVE::DrawEditorPlayBootSplashOverlayUVE(const Math::Vector2UVE imageOrigin,
                                                    const Math::Vector2UVE imageSize) {
    if (!m_playBootSplashActive || m_playModeState == EditorPlayModeStateUVE::Edit || m_services == nullptr ||
        imageSize.x <= 0.0F || imageSize.y <= 0.0F) {
        return;
    }

    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - m_playBootSplashStart).count();
    const double minimum = m_playBootSplashMinimumSeconds;
    if (m_playBootSplashSkippable &&
        m_services->GetInputSystemUVE().WasKeyPressedThisFrameUVE(Input::KeyCodeUVE::Escape)) {
        m_playBootSplashSkipRequested = true;
    }
    if (m_playBootSplashSkippable && m_playBootSplashSkipRequested && elapsed >= minimum) {
        m_playBootSplashActive = false;
        return;
    }

    const double holdEnd = std::max(m_playBootSplashFadeSeconds, minimum);
    if (elapsed >= holdEnd + m_playBootSplashFadeSeconds) {
        m_playBootSplashActive = false;
        return;
    }
    double opacity = 1.0;
    if (m_playBootSplashFadeSeconds > 0.0 && elapsed < m_playBootSplashFadeSeconds) {
        opacity = elapsed / m_playBootSplashFadeSeconds;
    } else if (m_playBootSplashFadeSeconds > 0.0 && elapsed >= holdEnd) {
        opacity = 1.0 - (elapsed - holdEnd) / m_playBootSplashFadeSeconds;
    }
    const float clampedOpacity = static_cast<float>(std::clamp(opacity, 0.0, 1.0));
    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    const ImVec2 min{imageOrigin.x, imageOrigin.y};
    const ImVec2 max{imageOrigin.x + imageSize.x, imageOrigin.y + imageSize.y};
    const ImVec4 backgroundColor{m_playBootSplashBackground[0], m_playBootSplashBackground[1],
                                 m_playBootSplashBackground[2],
                                 m_playBootSplashBackground[3] * clampedOpacity};
    drawList->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(backgroundColor));

    const char* const title = "UniVex Runtime";
    const ImVec2 titleSize = ImGui::CalcTextSize(title);
    float titleY = imageOrigin.y + (imageSize.y - titleSize.y) * 0.5F;
    if (!m_playBootSplashImagePath.empty()) {
        const std::uintptr_t textureId = GetTextureThumbnailUVE(m_playBootSplashImagePath);
        if (textureId != 0U) {
            const float aspectRatio = m_playBootSplashImageDimensions.x > 0.0F &&
                                              m_playBootSplashImageDimensions.y > 0.0F
                                          ? m_playBootSplashImageDimensions.x / m_playBootSplashImageDimensions.y
                                          : 1.6F;
            const float maxWidth = imageSize.x * 0.62F;
            const float maxHeight = imageSize.y * 0.52F;
            float logoWidth = maxWidth;
            float logoHeight = maxWidth / aspectRatio;
            if (logoHeight > maxHeight) {
                logoHeight = maxHeight;
                logoWidth = maxHeight * aspectRatio;
            }
            const float logoLeft = imageOrigin.x + (imageSize.x - logoWidth) * 0.5F;
            const float logoTop = imageOrigin.y + imageSize.y * 0.34F - logoHeight * 0.5F;
            const ImVec4 imageTint{1.0F, 1.0F, 1.0F, clampedOpacity};
            drawList->AddImage(static_cast<ImTextureID>(textureId), ImVec2{logoLeft, logoTop},
                               ImVec2{logoLeft + logoWidth, logoTop + logoHeight}, ImVec2{0.0F, 0.0F},
                               ImVec2{1.0F, 1.0F}, ImGui::ColorConvertFloat4ToU32(imageTint));
            titleY = logoTop + logoHeight + 22.0F;
        }
    }
    const ImVec2 titlePosition{imageOrigin.x + (imageSize.x - titleSize.x) * 0.5F, titleY};
    const ImVec4 titleColor{1.0F, 1.0F, 1.0F, clampedOpacity};
    drawList->AddText(titlePosition, ImGui::ColorConvertFloat4ToU32(titleColor), title);
}

bool EditorUVE::ApplyPlayEntrySpawnUVE() {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = m_services->GetSceneGraphUVE();

    Scene::EntityUVE player = Scene::ResolvePossessedPlayerUVE(entityManager);
    if (player == Scene::kInvalidEntityUVE) {
        const std::string relative = GetDefaultPlayerEntityUVE();
        if (!relative.empty()) {
            const std::filesystem::path absolute =
                m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot /
                std::filesystem::path{relative};
            std::error_code error;
            if (std::filesystem::is_regular_file(absolute, error)) {
                const Asset::AssetGuidUVE guid = m_services->GetAssetDatabaseUVE().RegisterUVE(absolute);
                if (guid != Asset::kInvalidAssetGuidUVE) {
                    static_cast<void>(m_services->GetPrefabSystemUVE().InstantiateUVE(
                        entityManager, sceneGraph, m_services->GetAssetDatabaseUVE(), guid,
                        GetDocumentObjectUVE()));
                    sceneGraph.UpdateUVE(entityManager);
                    player = Scene::ResolvePossessedPlayerUVE(entityManager);
                }
            }
        }
    }
    if (player == Scene::kInvalidEntityUVE) {
        player = Scene::ResolvePlayCharacterUVE(entityManager);
    }
    if (player == Scene::kInvalidEntityUVE) {
        return false;
    }
    Scene::MakePlayerCameraCurrentUVE(entityManager, player);

    // The candidates come from the shared spawn query - enabled, valid, world-posed, in stable
    // content order - rather than from a second copy of those rules living here. The tag is left
    // empty (a scene's play entry takes the first live spawn point whatever it is called), and
    // consumption is deliberately NOT asked for: a one-shot point is only spent once the player
    // has actually been moved, below, so a refused teleport leaves its checkpoint intact.
    const Scene::SpawnPoint3DQueryResultsUVE spawns =
        Scene::QuerySpawnPointsUVE(entityManager, Scene::SpawnPoint3DQueryUVE{});
    if (!spawns.HasAnyUVE()) {
        return false;
    }
    const Scene::SpawnPoint3DQueryResultUVE& spawnPoint = spawns.results[0];
    const std::optional<Scene::SpawnPoint3DPoseUVE> spawnPose{spawnPoint.pose};

    // World-space spawn pose -> the player's LOCAL pose under its own parent, via the sweep's
    // exact inverse. A root-level player passes identity TRS and the pose falls straight
    // through.
    const auto& playerHierarchy = entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(player);
    Math::Vector3UVE parentPosition{};
    Math::QuaternionUVE parentRotation{};
    Math::Vector3UVE parentScale{1.0F, 1.0F, 1.0F};
    const bool hasParent = playerHierarchy.parent != Scene::kInvalidEntityUVE;
    if (hasParent &&
        entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(playerHierarchy.parent)) {
        const Scene::WorldTransformComponentUVE& parentWorld =
            entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(playerHierarchy.parent);
        parentPosition = parentWorld.worldPosition;
        parentRotation = parentWorld.worldRotation;
        parentScale = parentWorld.worldScale;
    }
    const std::optional<Scene::SpawnPoint3DPoseUVE> playerLocalPose =
        Scene::ResolveSpawnPointPlayerLocalUVE(*spawnPose, parentPosition, parentRotation, parentScale);
    if (!playerLocalPose.has_value()) {
        return false;
    }

    Scene::TransformComponentUVE playerTransform =
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(player);
    playerTransform.localPosition = playerLocalPose->position;
    playerTransform.localRotation = playerLocalPose->rotation;
    // The documented rule for a simulation write that sets rotation directly: the quaternion is
    // now the truth, so the authored (and now stale) Euler cache must stop replaying.
    playerTransform.rotationEditMode = Scene::RotationEditModeUVE::Quaternion;
    m_services->GetSceneGraphUVE().SetLocalTransformUVE(entityManager, player, playerTransform);

    // The teleport succeeded, so a one-shot point is spent now - never before.
    static_cast<void>(Scene::ConsumeSpawnPointUVE(entityManager, spawnPoint.entity));
    return true;
}

bool EditorUVE::PausePlayModeUVE() {
    if (m_state != EditorStateUVE::Running || m_playModeState != EditorPlayModeStateUVE::Playing ||
        m_simulationControl == nullptr ||
        !m_simulationControl->SetSimulationExecutionModeUVE(Core::SimulationExecutionModeUVE::Paused)) {
        return false;
    }
    m_playModeState = EditorPlayModeStateUVE::Paused;
    return true;
}

bool EditorUVE::ResumePlayModeUVE() {
    if (m_state != EditorStateUVE::Running || m_playModeState != EditorPlayModeStateUVE::Paused ||
        m_simulationControl == nullptr ||
        !m_simulationControl->SetSimulationExecutionModeUVE(Core::SimulationExecutionModeUVE::Running)) {
        return false;
    }
    m_playModeState = EditorPlayModeStateUVE::Playing;
    return true;
}

void EditorUVE::PollPlayPauseOnErrorUVE() {
    if (m_playModeState != EditorPlayModeStateUVE::Playing || m_playPauseOnErrorSink == nullptr) {
        return;
    }
    const std::uint64_t errors = m_playPauseOnErrorSink->GetErrorCountUVE();
    const bool freshErrors = errors != m_playPauseOnErrorSeenErrors;
    m_playPauseOnErrorSeenErrors = errors;
    if (m_playPauseOnError && freshErrors) {
        static_cast<void>(PausePlayModeUVE());
    }
}

bool EditorUVE::StepPlayModeUVE() {
    return m_state == EditorStateUVE::Running && m_playModeState == EditorPlayModeStateUVE::Paused &&
           m_simulationControl != nullptr && m_simulationControl->RequestSingleSimulationStepUVE();
}

bool EditorUVE::StopPlayModeUVE() {
    if (m_state != EditorStateUVE::Running || m_playModeState == EditorPlayModeStateUVE::Edit ||
        !m_playModeSession.has_value() || m_simulationControl == nullptr ||
        !m_simulationControl->SetSimulationExecutionModeUVE(Core::SimulationExecutionModeUVE::Paused)) {
        return false;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const std::vector<Scene::EntityUVE> transientRoots = GetDocumentRootsUVE();
    std::optional<Scene::SceneSnapshotUVE> transientSnapshot;
    if (!transientRoots.empty()) {
        transientSnapshot = m_services->GetSceneSerializerUVE().CaptureUVE(
            entityManager, transientRoots, Asset::AssetKindUVE::Scene);
        if (!transientSnapshot.has_value()) {
            return false;
        }
    }

    const PlayModeSessionUVE& session = *m_playModeSession;
    ClearDocumentSceneUVE();
    std::vector<Scene::EntityUVE> restoredRoots;
    if (!session.capturedEmptyDocument) {
        restoredRoots = m_services->GetSceneSerializerUVE().RestoreUVE(entityManager, session.documentSnapshot);
        if (restoredRoots.empty()) {
            ClearDocumentSceneUVE();
            if (transientSnapshot.has_value()) {
                static_cast<void>(m_services->GetSceneSerializerUVE().RestoreUVE(entityManager, *transientSnapshot));
            }
            return false;
        }
    }

    RestoreSelectionUVE(ResolveSelectionPathsUVE(session.selectionBefore, restoredRoots));
    m_sceneDirty = session.dirtyBefore;
    if (!m_simulationControl->SetSimulationExecutionModeUVE(Core::SimulationExecutionModeUVE::Running) ||
        !m_simulationControl->SetTransientSimulationSessionActiveUVE(false)) {
        return false;
    }

    m_playModeSession.reset();
    m_playModeState = EditorPlayModeStateUVE::Edit;
    static_cast<void>(m_simulationControl->SetEditorPlaySceneNameUVE({}));
    m_playBootSplashActive = false;
    if (m_activeWorkspace == EditorWorkspaceUVE::Game) {
        m_activeWorkspace = m_workspaceBeforePlayMode;
    }
    return true;
}

EditorPlayModeStateUVE EditorUVE::GetPlayModeStateUVE() const noexcept {
    return m_playModeState;
}



bool EditorUVE::SaveSceneUVE() {
    // While the Entity Editor is open the document is the entity: saving writes the entity.
    if (m_entityEditSession.has_value()) {
        return SaveEntityEditorUVE();
    }
    if (!IsAuthoringCommandAllowedUVE() || m_activeScenePath.empty()) {
        return false;
    }

    const std::vector<Scene::EntityUVE> roots = GetDocumentRootsUVE();
    const bool saved = m_services->GetSceneSerializerUVE().SaveUVE(
        m_services->GetEntityManagerUVE(), roots, m_activeScenePath, Asset::AssetKindUVE::Scene);
    if (saved) {
        m_sceneDirty = false;
    }
    return saved;
}

std::size_t EditorUVE::SaveAllUVE() {
    std::size_t saved = 0U;
    std::vector<std::string> failed;
    if (m_sceneDirty && (!m_activeScenePath.empty() || m_entityEditSession.has_value())) {
        if (SaveSceneUVE()) {
            ++saved;
        } else {
            failed.emplace_back(m_activeScenePath.filename().string());
        }
    }
    if (m_openUVScript.has_value() && m_openUVScript->IsDirtyUVE()) {
        if (SaveOpenUVScriptUVE()) {
            ++saved;
        } else {
            failed.emplace_back(std::filesystem::path{m_openUVScript->path}.filename().string());
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

bool EditorUVE::SaveSelectedPrefabUVE(const std::filesystem::path& path) {
    if (!IsLifecycleCommandAllowedUVE() || path.empty() || !IsDocumentEntityUVE(m_selectedEntity)) {
        return false;
    }
    const Asset::AssetGuidUVE guid = m_services->GetPrefabSystemUVE().SavePrefabUVE(
        m_services->GetEntityManagerUVE(), m_services->GetAssetDatabaseUVE(), m_selectedEntity, path);
    return guid != Asset::kInvalidAssetGuidUVE;
}

std::filesystem::path EditorUVE::MakeUniqueContentPathUVE(const std::filesystem::path& directory,
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

std::optional<std::filesystem::path> EditorUVE::CreateContentCatalogueItemUVE(
    const std::string_view itemId, const std::filesystem::path& directory) {
    const ContentCatalogueItemUVE* const item = FindContentCatalogueItemUVE(itemId);
    std::error_code error;
    if (item == nullptr || !IsAuthoringCommandAllowedUVE() || directory.empty() ||
        !std::filesystem::is_directory(directory, error)) {
        return std::nullopt;
    }

    if (item->action == ContentCatalogueActionUVE::Folder) {
        const std::filesystem::path folder = MakeUniqueContentPathUVE(directory, "New Folder", "");
        if (!std::filesystem::create_directory(folder, error) || error) {
            return std::nullopt;
        }
        return folder;
    }
    if (item->action == ContentCatalogueActionUVE::SceneAsset) {
        const std::filesystem::path path = MakeUniqueContentPathUVE(directory, item->label, ".uvscene");
        Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
        Scene::ISceneGraphUVE& sceneGraph = m_services->GetSceneGraphUVE();
        const Scene::EntityUVE root = CreateObjectDefinitionEntityInternalUVE(
            Scene::ObjectDefinitionUVE{}, Scene::ApplyObjectDefinitionUVE);
        const Scene::EntityUVE viewport = CreateObjectDefinitionEntityInternalUVE(
            Scene::ViewportObjectDefinitionUVE{}, Scene::ApplyViewportObjectDefinitionUVE);
        const Scene::EntityUVE world = CreateObjectDefinitionEntityInternalUVE(
            Scene::FolderObjectDefinitionUVE{}, Scene::ApplyFolderObjectDefinitionUVE);
        if (root == Scene::kInvalidEntityUVE || viewport == Scene::kInvalidEntityUVE || world == Scene::kInvalidEntityUVE) {
            if (root != Scene::kInvalidEntityUVE) DestroyDocumentSubtreeUVE(root);
            if (viewport != Scene::kInvalidEntityUVE) DestroyDocumentSubtreeUVE(viewport);
            if (world != Scene::kInvalidEntityUVE) DestroyDocumentSubtreeUVE(world);
            return std::nullopt;
        }
        sceneGraph.SetParentUVE(entityManager, viewport, root);
        sceneGraph.SetParentUVE(entityManager, world, viewport);
        const bool saved = m_services->GetSceneSerializerUVE().SaveUVE(entityManager, {root}, path, Scene::SceneAssetTypeUVE::Scene);
        DestroyDocumentSubtreeUVE(root);
        InvalidateHierarchyFilterCacheUVE();
        return saved ? std::optional<std::filesystem::path>{path} : std::nullopt;
    }

    if (item->objects.empty()) {
        return std::nullopt;
    }

    // The tree is built in the live entity manager, saved, and destroyed again in this call: it is
    // never a document object, never selected and never in the undo history.
    const std::filesystem::path path = MakeUniqueContentPathUVE(directory, item->label, ".uventity");
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = m_services->GetSceneGraphUVE();
    std::vector<Scene::EntityUVE> built;
    built.reserve(item->objects.size());
    bool complete = true;
    for (const ContentCatalogueObjectUVE& object : item->objects) {
        const Scene::EntityUVE entity = CreateSceneObjectEntityInternalUVE(object.kind);
        if (entity == Scene::kInvalidEntityUVE || object.parent >= static_cast<std::int32_t>(built.size()) ||
            (built.empty() != (object.parent < 0))) {
            if (entity != Scene::kInvalidEntityUVE) {
                built.push_back(entity);
            }
            complete = false;
            break;
        }
        const std::string name = built.empty() ? path.stem().string() : std::string{object.name};
        if (!name.empty()) {
            entityManager.GetComponentUVE<Scene::NameComponentUVE>(entity).name = name;
        }
        if (object.parent >= 0) {
            sceneGraph.SetParentUVE(entityManager, entity, built[static_cast<std::size_t>(object.parent)]);
        }
        built.push_back(entity);
    }

    Asset::AssetGuidUVE guid = Asset::kInvalidAssetGuidUVE;
    if (complete) {
        guid = m_services->GetPrefabSystemUVE().SavePrefabUVE(entityManager, m_services->GetAssetDatabaseUVE(),
                                                             built.front(), path);
    }
    if (!built.empty()) {
        DestroyDocumentSubtreeUVE(built.front());
    }
    InvalidateHierarchyFilterCacheUVE();
    if (guid == Asset::kInvalidAssetGuidUVE) {
        return std::nullopt;
    }
    return path;
}

Scene::EntityUVE EditorUVE::PlaceEntityAssetUVE(const std::filesystem::path& path, Scene::EntityUVE parent) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    std::error_code error;
    if (!IsAuthoringCommandAllowedUVE() || (extension != ".uventity" && extension != ".uvprefab") ||
        !std::filesystem::is_regular_file(path, error)) {
        return Scene::kInvalidEntityUVE;
    }
    if (parent == Scene::kInvalidEntityUVE || !IsDocumentEntityUVE(parent)) {
        parent = ResolveNewObjectParentUVE();
    }

    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const std::string rootName = MakeUniqueDocumentEntityNameUVE(path.stem().string());
    const Asset::AssetGuidUVE guid = m_services->GetAssetDatabaseUVE().RegisterUVE(path);
    const Scene::EntityUVE root = m_services->GetPrefabSystemUVE().InstantiateUVE(
        entityManager, m_services->GetSceneGraphUVE(), m_services->GetAssetDatabaseUVE(), guid, parent);
    if (root == Scene::kInvalidEntityUVE) {
        return Scene::kInvalidEntityUVE;
    }
    if (entityManager.HasComponentUVE<Scene::NameComponentUVE>(root)) {
        entityManager.GetComponentUVE<Scene::NameComponentUVE>(root).name = rootName;
    }
    InvalidateHierarchyFilterCacheUVE();
    PlaceNewDocumentObjectUVE(root);

    const std::optional<Scene::SceneSnapshotUVE> snapshot = CaptureSubtreeUVE(root);
    if (!snapshot.has_value()) {
        DestroyDocumentSubtreeUVE(root);
        RestoreSelectionUVE(selectionBefore);
        m_sceneDirty = dirtyBefore;
        return Scene::kInvalidEntityUVE;
    }
    SelectEntityUVE(root);
    m_sceneDirty = true;
    RecordHistoryUVE(SceneObjectCreationHistoryEntryUVE{*snapshot, Scene::ResolveSceneObjectKindUVE(entityManager, root),
                                                      root, selectionBefore, CaptureSelectionSnapshotUVE(),
                                                      dirtyBefore, true, parent});
    return root;
}

bool EditorUVE::SetDefaultPlayerEntityUVE(const std::filesystem::path& contentRelativePath) {
    Config::SettingsDocumentUVE& project = m_services->GetProjectSettingsUVE();
    const std::string value = contentRelativePath.generic_string();
    if (!project.SetValueUVE(Core::EngineProjectSettingIdUVE::kDefaultPlayerEntityUVE, Config::SettingValueUVE{value})) {
        return false;
    }
    return SaveProjectSettingsUVE();
}

std::string EditorUVE::GetDefaultPlayerEntityUVE() const {
    const std::optional<Config::SettingValueUVE> value =
        m_services->GetProjectSettingsUVE().GetValueUVE(Core::EngineProjectSettingIdUVE::kDefaultPlayerEntityUVE);
    return value.has_value() ? std::get<std::string>(*value) : std::string{};
}

bool EditorUVE::RefreshSelectedPrefabUVE() {
    if (!IsLifecycleCommandAllowedUVE() || !IsDocumentEntityUVE(m_selectedEntity)) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::PrefabInstanceComponentUVE>(m_selectedEntity)) {
        return false;
    }
    const Scene::PrefabRefreshResultUVE result = m_services->GetPrefabSystemUVE().RefreshInstanceUVE(
        entityManager, m_services->GetSceneGraphUVE(), m_services->GetAssetDatabaseUVE(), m_selectedEntity);
    if (!result.IsSuccessUVE()) {
        return false;
    }
    if (result.code == Scene::PrefabRefreshCodeUVE::Refreshed) {
        SelectEntityUVE(result.rootEntity);
        m_sceneDirty = true;
        InvalidateHierarchyFilterCacheUVE();
    }
    return true;
}

bool EditorUVE::DiscardSelectedPrefabOverridesAndRefreshUVE() {
    if (!IsLifecycleCommandAllowedUVE() || !IsDocumentEntityUVE(m_selectedEntity)) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::PrefabInstanceComponentUVE>(m_selectedEntity)) {
        return false;
    }
    Scene::PrefabInstanceComponentUVE cleared =
        entityManager.GetComponentUVE<Scene::PrefabInstanceComponentUVE>(m_selectedEntity);
    if (cleared.overrides.empty()) {
        return RefreshSelectedPrefabUVE();
    }
    cleared.overrides.clear();
    entityManager.AddComponentUVE<Scene::PrefabInstanceComponentUVE>(m_selectedEntity, cleared);
    const Scene::PrefabRefreshResultUVE result = m_services->GetPrefabSystemUVE().RefreshInstanceUVE(
        entityManager, m_services->GetSceneGraphUVE(), m_services->GetAssetDatabaseUVE(), m_selectedEntity, true);
    if (!result.IsSuccessUVE()) {
        return false;
    }
    if (result.code == Scene::PrefabRefreshCodeUVE::Refreshed) {
        SelectEntityUVE(result.rootEntity);
        InvalidateHierarchyFilterCacheUVE();
    }
    m_sceneDirty = true;
    return true;
}

bool EditorUVE::LoadSceneUVE() {
    if (m_entityEditSession.has_value() || m_retargetPreview.has_value()) {
        return false;
    }
    static_cast<void>(CommitComponentPropertyPreviewUVE());
    if (!IsAuthoringCommandAllowedUVE() || m_activeScenePath.empty() ||
        !std::filesystem::exists(m_activeScenePath)) {
        return false;
    }

    const std::filesystem::path recoveryPath = MakeRecoveryPathUVE(m_activeScenePath);
    std::error_code error;
    std::filesystem::remove(recoveryPath, error);

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const std::vector<Scene::EntityUVE> documentRoots = GetDocumentRootsUVE();
    if (!m_services->GetSceneSerializerUVE().SaveUVE(
            entityManager, documentRoots, recoveryPath, Asset::AssetKindUVE::Scene)) {
        return false;
    }

    ClearHistoryUVE();
    ClearDocumentSceneUVE();
    const std::vector<Scene::EntityUVE> loadedRoots =
        m_services->GetSceneSerializerUVE().LoadUVE(entityManager, m_activeScenePath);
    if (loadedRoots.empty()) {
        ClearDocumentSceneUVE();
        static_cast<void>(m_services->GetSceneSerializerUVE().LoadUVE(entityManager, recoveryPath));
        std::filesystem::remove(recoveryPath, error);
        // Even a total load failure must not leave a rootless document behind.
        static_cast<void>(EnsureDocumentObjectUVE());
        return false;
    }

    // Locks belong to the editor view of a document, not to serialized scene objects.
    m_lockedHierarchyEntities.clear();
    std::filesystem::remove(recoveryPath, error);
    // Auto-migrate: a loaded document must end with exactly one Object. Files saved before
    // the root existed are legitimately multi-root (every Add-Object used to create a new root),
    // so wrap their top-level entities under a fresh Object; files that already carry the
    // root pass through untouched. Either way the in-memory document afterwards holds the
    // one-root invariant every other document seam relies on.
    bool migrated = false;
    const Scene::EntityUVE rootObject = EnsureDocumentObjectUVE();
    if (rootObject != Scene::kInvalidEntityUVE) {
        Scene::ISceneGraphUVE& sceneGraph = m_services->GetSceneGraphUVE();

        // Strip any marker other than the one root this document keeps. A .uvscene is plain JSON
        // on disk, so a file can arrive carrying two of them - a badly resolved merge, a
        // hand-edit, a future tool - and the loop below would then reparent the second one UNDER
        // the first, leaving a root inside a root. That is not cosmetic: the editor refuses to
        // delete or reparent an Object, so the surplus would be an entity the user has no way
        // to remove.
        //
        // The marker is stripped rather than the entity destroyed. The surplus root may well have
        // children, and discarding authored content to repair a structural mistake is the wrong
        // trade - demoted to an ordinary object it keeps its name, its transform and its subtree,
        // and the migration below folds it under the real root like any other top-level entity.
        std::vector<Scene::EntityUVE> surplusRoots;
        entityManager.ForEachUVE<Scene::ObjectComponentUVE>(
            [&surplusRoots, rootObject](const Scene::EntityUVE entity, const Scene::ObjectComponentUVE&) {
                if (entity != rootObject) {
                    surplusRoots.push_back(entity);
                }
            });
        for (const Scene::EntityUVE surplus : surplusRoots) {
            entityManager.RemoveComponentUVE<Scene::ObjectComponentUVE>(surplus);
            migrated = true; // the document no longer matches the bytes it was loaded from
        }

        for (const Scene::EntityUVE topLevel : GetDocumentRootsUVE()) {
            if (topLevel != rootObject) {
                sceneGraph.SetParentUVE(entityManager, topLevel, rootObject);
                migrated = true;
            }
        }
    }
    // Levels saved before the Outliner layout get its Viewport, and their objects a folder.
    migrated = EnsureDocumentLayoutUVE() || migrated;
    ClearSelectionUVE();
    ClearHistoryUVE();
    m_sceneDirty = migrated; // a wrapped legacy file no longer matches its bytes on disk
    InvalidateHierarchyFilterCacheUVE();
    return true;
}

bool EditorUVE::OpenSceneAssetUVE(const std::filesystem::path& path) {
    std::error_code error;
    if (!IsAuthoringCommandAllowedUVE() || path.empty() || path.extension() != ".uvscene" ||
        !std::filesystem::is_regular_file(path, error)) {
        return false;
    }
    const std::filesystem::path previous = m_activeScenePath;
    m_activeScenePath = path;
    if (LoadSceneUVE()) {
        return true;
    }
    m_activeScenePath = previous;
    return false;
}

std::vector<Scene::EntityUVE> EditorUVE::ExpandHierarchySelectionUVE(
    const std::vector<Scene::EntityUVE>& roots) const {
    std::vector<Scene::EntityUVE> expanded;
    expanded.reserve(roots.size());
    if (roots.empty()) {
        return expanded;
    }
    std::unordered_set<Scene::EntityUVE> visited;
    visited.reserve(roots.size());

    if (!m_hierarchyView.selectChildren) {
        for (const Scene::EntityUVE root : roots) {
            if (IsDocumentEntityUVE(root) && visited.insert(root).second) {
                expanded.push_back(root);
            }
        }
        return expanded;
    }

    // Build the parent-to-children view in one pass. Calling GetChildrenUVE once per descendant
    // would rescan every hierarchy component each time and make selecting a large subtree quadratic.
    using OrderedChildUVE = std::pair<std::int64_t, Scene::EntityUVE>;
    std::unordered_map<Scene::EntityUVE, std::vector<OrderedChildUVE>> orderedChildren;
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    entityManager.ForEachUVE<Scene::HierarchyComponentUVE>(
        [&orderedChildren](const Scene::EntityUVE child, Scene::HierarchyComponentUVE& hierarchy) {
            if (hierarchy.parent != Scene::kInvalidEntityUVE) {
                orderedChildren[hierarchy.parent].emplace_back(hierarchy.siblingOrder, child);
            }
        });
    for (auto& [parent, children] : orderedChildren) {
        static_cast<void>(parent);
        std::sort(children.begin(), children.end(), [](const OrderedChildUVE& left, const OrderedChildUVE& right) {
            if (left.first != right.first) {
                return left.first < right.first;
            }
            return left.second.index != right.second.index ? left.second.index < right.second.index
                                                           : left.second.generation < right.second.generation;
        });
    }

    std::vector<Scene::EntityUVE> pending;
    for (const Scene::EntityUVE root : roots) {
        if (IsDocumentEntityUVE(root)) {
            pending.push_back(root);
        }
        while (!pending.empty()) {
            const Scene::EntityUVE current = pending.back();
            pending.pop_back();
            if (!IsDocumentEntityUVE(current) || !visited.insert(current).second) {
                continue;
            }
            expanded.push_back(current);
            if (const auto children = orderedChildren.find(current); children != orderedChildren.end()) {
                // Push in reverse so popping preserves the scene graph's stable sibling order.
                for (auto child = children->second.rbegin(); child != children->second.rend(); ++child) {
                    pending.push_back(child->second);
                }
            }
        }
    }
    return expanded;
}

void EditorUVE::SelectEntityUVE(const Scene::EntityUVE entity) noexcept {
    if (!IsAuthoringCommandAllowedUVE()) {
        return;
    }
    if (!IsDocumentEntityUVE(entity)) {
        ClearSelectionUVE();
        return;
    }
    if (IsEntityLockedUVE(entity)) {
        return;
    }
    std::vector<Scene::EntityUVE> selected = ExpandHierarchySelectionUVE({entity});
    std::erase_if(selected, [this](const Scene::EntityUVE current) { return IsEntityLockedUVE(current); });
    RestoreSelectionUVE(EditorSelectionSnapshotUVE{std::move(selected), entity});
}

void EditorUVE::ToggleEntitySelectionUVE(const Scene::EntityUVE entity) noexcept {
    if (!IsAuthoringCommandAllowedUVE() || !IsDocumentEntityUVE(entity) || IsEntityLockedUVE(entity)) {
        return;
    }

    m_hierarchySelectionAnchor = entity;
    const auto selectedIt = std::find(m_selectedEntities.begin(), m_selectedEntities.end(), entity);
    const std::vector<Scene::EntityUVE> selectionGroup = ExpandHierarchySelectionUVE({entity});
    const std::unordered_set<Scene::EntityUVE> selectionGroupSet(selectionGroup.begin(), selectionGroup.end());
    if (selectedIt == m_selectedEntities.end()) {
        std::unordered_set<Scene::EntityUVE> selectedSet(m_selectedEntities.begin(), m_selectedEntities.end());
        selectedSet.reserve(m_selectedEntities.size() + selectionGroup.size());
        for (const Scene::EntityUVE current : selectionGroup) {
            if (IsDocumentEntityUVE(current) && !IsEntityLockedUVE(current) && selectedSet.insert(current).second) {
                m_selectedEntities.push_back(current);
            }
        }
        m_selectedEntity = entity;
        CancelHierarchyRenameUVE();
        return;
    }

    const bool removedActive = selectionGroupSet.contains(m_selectedEntity);
    std::erase_if(m_selectedEntities, [&selectionGroupSet](const Scene::EntityUVE current) {
        return selectionGroupSet.contains(current);
    });
    if (m_selectedEntities.empty()) {
        m_selectedEntity = Scene::kInvalidEntityUVE;
    } else if (removedActive) {
        m_selectedEntity = m_selectedEntities.back();
    }
    CancelHierarchyRenameUVE();
}

void EditorUVE::SelectHierarchyRangeUVE(const Scene::EntityUVE entity,
                                       const std::vector<Scene::EntityUVE>& visibleOrder,
                                       const bool addToSelection) noexcept {
    if (!IsAuthoringCommandAllowedUVE() || !IsDocumentEntityUVE(entity) || IsEntityLockedUVE(entity)) {
        return;
    }

    Scene::EntityUVE anchor = m_hierarchySelectionAnchor;
    if (!IsDocumentEntityUVE(anchor)) {
        anchor = m_selectedEntity;
    }
    const auto clickedIt = std::find(visibleOrder.begin(), visibleOrder.end(), entity);
    const auto anchorIt = std::find(visibleOrder.begin(), visibleOrder.end(), anchor);
    const bool hasRange = clickedIt != visibleOrder.end() && anchorIt != visibleOrder.end();

    const auto rangeFirst = hasRange ? std::min(clickedIt, anchorIt) : visibleOrder.end();
    const auto rangeLast = hasRange ? std::max(clickedIt, anchorIt) : visibleOrder.end();
    const std::size_t rangeCount =
        hasRange ? static_cast<std::size_t>(std::distance(rangeFirst, rangeLast)) + 1U : 1U;
    const std::size_t reserveCount = (addToSelection ? m_selectedEntities.size() : 0U) + rangeCount;
    std::vector<Scene::EntityUVE> selected;
    selected.reserve(reserveCount);
    std::unordered_set<Scene::EntityUVE> selectedSet;
    selectedSet.reserve(reserveCount);

    if (addToSelection) {
        for (const Scene::EntityUVE current : m_selectedEntities) {
            if (IsDocumentEntityUVE(current) && !IsEntityLockedUVE(current) && selectedSet.insert(current).second) {
                selected.push_back(current);
            }
        }
    }

    std::vector<Scene::EntityUVE> rangeRoots;
    rangeRoots.reserve(rangeCount);
    if (hasRange) {
        for (auto current = rangeFirst; current <= rangeLast; ++current) {
            const Scene::EntityUVE candidate = *current;
            if (IsDocumentEntityUVE(candidate) && !IsEntityLockedUVE(candidate)) {
                rangeRoots.push_back(candidate);
            }
        }
    } else {
        rangeRoots.push_back(entity);
        // If the old anchor is hidden by collapse or filtering, this click becomes the new anchor.
        m_hierarchySelectionAnchor = entity;
    }
    for (const Scene::EntityUVE candidate : ExpandHierarchySelectionUVE(rangeRoots)) {
        if (IsDocumentEntityUVE(candidate) && !IsEntityLockedUVE(candidate) && selectedSet.insert(candidate).second) {
            selected.push_back(candidate);
        }
    }

    const bool changed = selected != m_selectedEntities || m_selectedEntity != entity;
    m_selectedEntities = std::move(selected);
    m_selectedEntity = entity;
    if (changed) {
        CancelHierarchyRenameUVE();
    }
}

void EditorUVE::ApplyHierarchyBoxSelectionUVE(
    const std::vector<HierarchySelectionRowBoundsUVE>& rowBounds,
    const float startX, const float startY, const float endX, const float endY, const bool additive,
    const std::vector<Scene::EntityUVE>& selectionBefore, const Scene::EntityUVE activeBefore) noexcept {
    if (!IsAuthoringCommandAllowedUVE() || !std::isfinite(startX) || !std::isfinite(startY) ||
        !std::isfinite(endX) || !std::isfinite(endY)) {
        return;
    }

    const float left = std::min(startX, endX);
    const float top = std::min(startY, endY);
    const float right = std::max(startX, endX);
    const float bottom = std::max(startY, endY);
    std::vector<Scene::EntityUVE> selected;
    const std::size_t reserveCount = (additive ? selectionBefore.size() : 0U) + rowBounds.size();
    selected.reserve(reserveCount);
    std::unordered_set<Scene::EntityUVE> selectedSet;
    selectedSet.reserve(reserveCount);

    if (additive) {
        for (const Scene::EntityUVE entity : selectionBefore) {
            if (IsDocumentEntityUVE(entity) && !IsEntityLockedUVE(entity) && selectedSet.insert(entity).second) {
                selected.push_back(entity);
            }
        }
    }

    bool selectedFromBox = false;
    Scene::EntityUVE active = Scene::kInvalidEntityUVE;
    std::vector<Scene::EntityUVE> boxRoots;
    boxRoots.reserve(rowBounds.size());
    for (const HierarchySelectionRowBoundsUVE& row : rowBounds) {
        const bool overlaps = row.left < right && row.right > left && row.top < bottom && row.bottom > top;
        if (!overlaps || !IsDocumentEntityUVE(row.entity) || IsEntityLockedUVE(row.entity)) {
            continue;
        }
        selectedFromBox = true;
        active = row.entity;
        boxRoots.push_back(row.entity);
    }
    for (const Scene::EntityUVE candidate : ExpandHierarchySelectionUVE(boxRoots)) {
        if (IsDocumentEntityUVE(candidate) && !IsEntityLockedUVE(candidate) && selectedSet.insert(candidate).second) {
            selected.push_back(candidate);
        }
    }

    if (active == Scene::kInvalidEntityUVE && additive) {
        if (IsDocumentEntityUVE(activeBefore) && selectedSet.contains(activeBefore)) {
            active = activeBefore;
        } else if (!selected.empty()) {
            active = selected.back();
        }
    }
    if (active == Scene::kInvalidEntityUVE && !selected.empty()) {
        active = selected.back();
    }

    const bool changed = selected != m_selectedEntities || active != m_selectedEntity;
    m_selectedEntities = std::move(selected);
    m_selectedEntity = active;
    if (selectedFromBox) {
        m_hierarchySelectionAnchor = active;
    } else if (!additive) {
        m_hierarchySelectionAnchor = Scene::kInvalidEntityUVE;
    }
    if (changed) {
        CancelHierarchyRenameUVE();
    }
}

void EditorUVE::ClearSelectionUVE() noexcept {
    m_selectedEntities.clear();
    m_selectedEntity = Scene::kInvalidEntityUVE;
    m_hierarchySelectionAnchor = Scene::kInvalidEntityUVE;
    m_hierarchyBoxSelecting = false;
    m_hierarchyBoxSelectionBefore.clear();
    m_hierarchyBoxActiveBefore = Scene::kInvalidEntityUVE;
    CancelHierarchyRenameUVE();
}

const std::vector<Scene::EntityUVE>& EditorUVE::GetSelectedEntitiesUVE() const noexcept {
    return m_selectedEntities;
}

bool EditorUVE::HasSingleDocumentSelectionUVE() const noexcept {
    return m_selectedEntities.size() == 1U && m_selectedEntities.front() == m_selectedEntity &&
           IsDocumentEntityUVE(m_selectedEntity);
}

bool EditorUVE::SetSelectedLocalTransformUVE(const Scene::TransformComponentUVE& transform) {
    if (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE() ||
        !IsTransformFiniteUVE(transform)) {
        return false;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity)) {
        return false;
    }

    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const Scene::TransformComponentUVE before =
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity);
    if (AreTransformsEqualUVE(before, transform)) {
        return false;
    }

    const bool dirtyBefore = m_sceneDirty;
    if (!ApplyLocalTransformUVE(m_selectedEntity, transform)) {
        return false;
    }

    m_sceneDirty = true;
    RecordHistoryUVE(TransformHistoryEntryUVE{
        m_selectedEntity, before, transform, selectionBefore, CaptureSelectionSnapshotUVE(), dirtyBefore, true});
    return true;
}

bool EditorUVE::SetSelectedPrimitiveMeshUVE(const Scene::PrimitiveMeshComponentUVE& primitive) {
    if (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE() ||
        !Scene::IsPrimitiveMeshComponentValidUVE(primitive)) {
        return false;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::PrimitiveMeshComponentUVE>(m_selectedEntity)) {
        return false;
    }

    const Scene::PrimitiveMeshComponentUVE before =
        entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(m_selectedEntity);
    if (before.kind == primitive.kind && before.baseColor == primitive.baseColor) {
        return false;
    }

    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    if (!ApplyPrimitiveMeshStateUVE(m_selectedEntity, primitive)) {
        return false;
    }

    m_sceneDirty = true;
    RecordHistoryUVE(PrimitiveAppearanceHistoryEntryUVE{
        m_selectedEntity, before, primitive, selectionBefore, CaptureSelectionSnapshotUVE(), dirtyBefore, true});
    return true;
}

bool EditorUVE::IsSceneComponentValueValidUVE(
    const EditorSceneComponentKindUVE kind, const EditorSceneComponentValueUVE& value) const noexcept {
    return std::visit(
        [kind](const auto& typedValue) noexcept {
            using ValueType = std::decay_t<decltype(typedValue)>;
            if constexpr (std::is_same_v<ValueType, Scene::CameraComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::Camera && Scene::IsCameraComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::MeshComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::Mesh && Scene::IsMeshComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::LightComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::Light && Scene::IsLightComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::ColliderComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::Collider && Scene::IsColliderComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::Rigid3DComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::Rigid3D && Scene::IsRigid3DComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::AudioSourceComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::AudioSource && Scene::IsAudioSourceComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::ParticleEmitterComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::ParticleEmitter &&
                       Scene::IsParticleEmitterComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::ScriptComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::Script && Scene::IsScriptComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::AnimationSequencerComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::AnimationSequencer &&
                       Scene::IsAnimationSequencerComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::WorldEnvironment3DComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::WorldEnvironment &&
                       Scene::IsWorldEnvironment3DObjectComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::CharacterControllerComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::CharacterController &&
                       Scene::IsCharacterControllerComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::CanvasComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::Canvas && Scene::IsCanvasComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::UITextComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::UIText && Scene::IsUITextComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::UIImageComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::UIImage && Scene::IsUIImageComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::UIButtonComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::UIButton && Scene::IsUIButtonComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::ProcessComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::Process &&
                       Scene::IsProcessComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::ThreadGroupComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::ThreadGroup &&
                       Scene::IsThreadGroupComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::AutoTranslateComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::AutoTranslate &&
                       Scene::IsAutoTranslateComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::ObjectMetadataComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::ObjectMetadata &&
                       Scene::IsObjectMetadataComponentValidUVE(typedValue);            } else if constexpr (std::is_same_v<ValueType, Scene::PhysicsInterpolationComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::PhysicsInterpolation &&
                       Scene::IsPhysicsInterpolationComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::EditorDescriptionComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::EditorDescription &&
                       Scene::IsEditorDescriptionComponentValidUVE(typedValue);
            } else {
                return false;
            }
        },
        value);
}

bool EditorUVE::AreSceneComponentValuesEqualUVE(const EditorSceneComponentValueUVE& lhs,
                                                 const EditorSceneComponentValueUVE& rhs) const noexcept {
    return std::visit(
        [](const auto& left, const auto& right) noexcept {
            using LeftType = std::decay_t<decltype(left)>;
            using RightType = std::decay_t<decltype(right)>;
            if constexpr (!std::is_same_v<LeftType, RightType>) {
                return false;
            } else if constexpr (std::is_same_v<LeftType, Scene::CameraComponentUVE>) {
                return left.fieldOfViewDegrees == right.fieldOfViewDegrees && left.nearPlane == right.nearPlane &&
                       left.farPlane == right.farPlane && left.projection == right.projection &&
                       left.orthographicSize == right.orthographicSize && left.current == right.current;
            } else if constexpr (std::is_same_v<LeftType, Scene::MeshComponentUVE>) {
                return left.meshGuid == right.meshGuid && left.materialGuid == right.materialGuid;
            } else if constexpr (std::is_same_v<LeftType, Scene::LightComponentUVE>) {
                return left.color == right.color && left.intensity == right.intensity && left.type == right.type &&
                       left.range == right.range && left.spotAngleDegrees == right.spotAngleDegrees;
            } else if constexpr (std::is_same_v<LeftType, Scene::ColliderComponentUVE>) {
                return left.halfExtents == right.halfExtents && left.collisionLayer == right.collisionLayer &&
                       left.collisionMask == right.collisionMask && left.friction == right.friction &&
                       left.restitution == right.restitution && left.density == right.density &&
                       left.shapeType == right.shapeType && left.radius == right.radius &&
                       left.height == right.height && left.disabled == right.disabled;
            } else if constexpr (std::is_same_v<LeftType, Scene::Rigid3DComponentUVE>) {
                return left.mass == right.mass && left.isKinematic == right.isKinematic &&
                       left.velocity == right.velocity && left.angularVelocity == right.angularVelocity &&
                       left.torque == right.torque && left.inverseInertia == right.inverseInertia &&
                       left.drag == right.drag && left.gravityScale == right.gravityScale;
            } else if constexpr (std::is_same_v<LeftType, Scene::AudioSourceComponentUVE>) {
                return left.audioAssetPath == right.audioAssetPath && left.mixerGroup == right.mixerGroup &&
                       left.volume == right.volume && left.looping == right.looping && left.pitch == right.pitch &&
                       left.spatial == right.spatial && left.minDistance == right.minDistance &&
                       left.maxDistance == right.maxDistance && left.attenuationCurve == right.attenuationCurve &&
                       left.playOnAwake == right.playOnAwake;
            } else if constexpr (std::is_same_v<LeftType, Scene::ParticleEmitterComponentUVE>) {
                return left.maxParticles == right.maxParticles;
            } else if constexpr (std::is_same_v<LeftType, Scene::ScriptComponentUVE>) {
                return left.scriptAssetPath == right.scriptAssetPath && left.exportValues == right.exportValues;
            } else if constexpr (std::is_same_v<LeftType, Scene::AnimationSequencerComponentUVE>) {
                return left.HasSameSettingsUVE(right);
            } else if constexpr (std::is_same_v<LeftType, Scene::WorldEnvironment3DComponentUVE>) {
                return left.skyAssetPath == right.skyAssetPath && left.ambientSource == right.ambientSource &&
                       left.ambientColor == right.ambientColor && left.fogColor == right.fogColor &&
                       left.ambientEnergy == right.ambientEnergy &&
                       left.exposure == right.exposure && left.fogDensity == right.fogDensity &&
                       left.fogEnabled == right.fogEnabled &&
                       left.postProcessingEnabled == right.postProcessingEnabled;
            } else if constexpr (std::is_same_v<LeftType, Scene::CharacterControllerComponentUVE>) {
                return left.moveSpeed == right.moveSpeed && left.jumpHeight == right.jumpHeight &&
                       left.gravityScale == right.gravityScale;
            } else if constexpr (std::is_same_v<LeftType, Scene::CanvasComponentUVE>) {
                return left.visible == right.visible && left.sortOrder == right.sortOrder;
            } else if constexpr (std::is_same_v<LeftType, Scene::UITextComponentUVE>) {
                return left.text == right.text && left.positionPixels == right.positionPixels &&
                       left.fontSize == right.fontSize && left.color == right.color && left.alpha == right.alpha;
            } else if constexpr (std::is_same_v<LeftType, Scene::UIImageComponentUVE>) {
                return left.textureAssetGuid == right.textureAssetGuid &&
                       left.positionPixels == right.positionPixels && left.sizePixels == right.sizePixels &&
                       left.tintColor == right.tintColor && left.alpha == right.alpha;
            } else if constexpr (std::is_same_v<LeftType, Scene::UIButtonComponentUVE>) {
                return left.positionPixels == right.positionPixels && left.sizePixels == right.sizePixels &&
                       left.normalColor == right.normalColor && left.hoverColor == right.hoverColor &&
                       left.pressedColor == right.pressedColor;
            } else if constexpr (std::is_same_v<LeftType, Scene::PhysicsInterpolationComponentUVE>) {
                // Only `mode` is the authored, comparable field - the pose members are
                // runtime-computed and never part of an authoring diff (see the component's own
                // doc comment).
                return left.mode == right.mode;
            } else if constexpr (std::is_same_v<LeftType, Scene::EditorDescriptionComponentUVE>) {
                return left.description == right.description;
            } else {
                return false;
            }
        },
        lhs,
        rhs);
}

bool EditorUVE::ApplySceneComponentStateUVE(
    const Scene::EntityUVE entity, const EditorSceneComponentKindUVE kind,
    const std::optional<EditorSceneComponentValueUVE>& value) {
    if (!IsDocumentEntityUVE(entity)) {
        return false;
    }

    const auto apply = [&]<typename T>() {
        Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
        if (!value.has_value()) {
            if (!entityManager.HasComponentUVE<T>(entity)) {
                return false;
            }
            entityManager.RemoveComponentUVE<T>(entity);
            return true;
        }
        const T* const typedValue = std::get_if<T>(&*value);
        if (typedValue == nullptr) {
            return false;
        }
        if (entityManager.HasComponentUVE<T>(entity)) {
            entityManager.GetComponentUVE<T>(entity) = *typedValue;
        } else {
            entityManager.AddComponentUVE<T>(entity, *typedValue);
        }
        return true;
    };

    switch (kind) {
        case EditorSceneComponentKindUVE::Camera: {
            const bool applied = apply.template operator()<Scene::CameraComponentUVE>();
            if (applied && value.has_value()) {
                Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
                if (entityManager.HasComponentUVE<Scene::CameraComponentUVE>(entity) &&
                    entityManager.GetComponentUVE<Scene::CameraComponentUVE>(entity).current) {
                    Scene::MakeCameraCurrentUVE(entityManager, entity);
                }
            }
            return applied;
        }
        case EditorSceneComponentKindUVE::Mesh:
            return apply.template operator()<Scene::MeshComponentUVE>();
        case EditorSceneComponentKindUVE::Light:
            return apply.template operator()<Scene::LightComponentUVE>();
        case EditorSceneComponentKindUVE::Collider:
            return apply.template operator()<Scene::ColliderComponentUVE>();
        case EditorSceneComponentKindUVE::Rigid3D:
            return apply.template operator()<Scene::Rigid3DComponentUVE>();
        case EditorSceneComponentKindUVE::AudioSource:
            return apply.template operator()<Scene::AudioSourceComponentUVE>();
        case EditorSceneComponentKindUVE::ParticleEmitter:
            return apply.template operator()<Scene::ParticleEmitterComponentUVE>();
        case EditorSceneComponentKindUVE::Script:
            return apply.template operator()<Scene::ScriptComponentUVE>();
        case EditorSceneComponentKindUVE::AnimationSequencer:
            return apply.template operator()<Scene::AnimationSequencerComponentUVE>();
        case EditorSceneComponentKindUVE::WorldEnvironment:
            return apply.template operator()<Scene::WorldEnvironment3DComponentUVE>();
        case EditorSceneComponentKindUVE::CharacterController:
            return apply.template operator()<Scene::CharacterControllerComponentUVE>();
        case EditorSceneComponentKindUVE::Canvas:
            return apply.template operator()<Scene::CanvasComponentUVE>();
        case EditorSceneComponentKindUVE::UIText:
            return apply.template operator()<Scene::UITextComponentUVE>();
        case EditorSceneComponentKindUVE::UIImage:
            return apply.template operator()<Scene::UIImageComponentUVE>();
        case EditorSceneComponentKindUVE::UIButton:
            return apply.template operator()<Scene::UIButtonComponentUVE>();
        case EditorSceneComponentKindUVE::PhysicsInterpolation:
            return apply.template operator()<Scene::PhysicsInterpolationComponentUVE>();
        case EditorSceneComponentKindUVE::EditorDescription:
            return apply.template operator()<Scene::EditorDescriptionComponentUVE>();
        case EditorSceneComponentKindUVE::Process:
            return apply.template operator()<Scene::ProcessComponentUVE>();
        case EditorSceneComponentKindUVE::ThreadGroup:
            return apply.template operator()<Scene::ThreadGroupComponentUVE>();
        case EditorSceneComponentKindUVE::AutoTranslate:
            return apply.template operator()<Scene::AutoTranslateComponentUVE>();
        case EditorSceneComponentKindUVE::ObjectMetadata:
            return apply.template operator()<Scene::ObjectMetadataComponentUVE>();
    }
    return false;
}

bool EditorUVE::SetSelectedSceneComponentUVE(const EditorSceneComponentKindUVE kind,
                                              const EditorSceneComponentValueUVE& value) {
    if (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE() ||
        !IsSceneComponentValueValidUVE(kind, value)) {
        return false;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    std::optional<EditorSceneComponentValueUVE> before;
    switch (kind) {
        case EditorSceneComponentKindUVE::Camera:
            if (entityManager.HasComponentUVE<Scene::CameraComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::CameraComponentUVE>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKindUVE::Mesh:
            if (entityManager.HasComponentUVE<Scene::MeshComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::MeshComponentUVE>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKindUVE::Light:
            if (entityManager.HasComponentUVE<Scene::LightComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::LightComponentUVE>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKindUVE::Collider:
            if (entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKindUVE::Rigid3D:
            if (entityManager.HasComponentUVE<Scene::Rigid3DComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKindUVE::AudioSource:
            if (entityManager.HasComponentUVE<Scene::AudioSourceComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::AudioSourceComponentUVE>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKindUVE::ParticleEmitter:
            if (entityManager.HasComponentUVE<Scene::ParticleEmitterComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::ParticleEmitterComponentUVE>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKindUVE::Script:
            if (entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKindUVE::AnimationSequencer:
            if (entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKindUVE::WorldEnvironment:
            if (entityManager.HasComponentUVE<Scene::WorldEnvironment3DComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::WorldEnvironment3DComponentUVE>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKindUVE::CharacterController:
            if (entityManager.HasComponentUVE<Scene::CharacterControllerComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKindUVE::Canvas:
            if (entityManager.HasComponentUVE<Scene::CanvasComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::CanvasComponentUVE>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKindUVE::UIText:
            if (entityManager.HasComponentUVE<Scene::UITextComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::UITextComponentUVE>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKindUVE::UIImage:
            if (entityManager.HasComponentUVE<Scene::UIImageComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::UIImageComponentUVE>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKindUVE::UIButton:
            if (entityManager.HasComponentUVE<Scene::UIButtonComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKindUVE::PhysicsInterpolation:
            if (entityManager.HasComponentUVE<Scene::PhysicsInterpolationComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::PhysicsInterpolationComponentUVE>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKindUVE::EditorDescription:
            if (entityManager.HasComponentUVE<Scene::EditorDescriptionComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::EditorDescriptionComponentUVE>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKindUVE::Process:
            if (entityManager.HasComponentUVE<Scene::ProcessComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::ProcessComponentUVE>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKindUVE::ThreadGroup:
            if (entityManager.HasComponentUVE<Scene::ThreadGroupComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::ThreadGroupComponentUVE>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKindUVE::AutoTranslate:
            if (entityManager.HasComponentUVE<Scene::AutoTranslateComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::AutoTranslateComponentUVE>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKindUVE::ObjectMetadata:
            if (entityManager.HasComponentUVE<Scene::ObjectMetadataComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::ObjectMetadataComponentUVE>(m_selectedEntity);
            }
            break;
    }
    if (before.has_value() && AreSceneComponentValuesEqualUVE(*before, value)) {
        return false;
    }

    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    if (!ApplySceneComponentStateUVE(m_selectedEntity, kind, value)) {
        return false;
    }
    // Applying always leaves the component attached, so the presence this edit ends with is
    // "present": the filter cache only has work to do when nothing was there before. (The undo and
    // redo paths compare their optionals because either side can be empty there.)
    if (!before.has_value()) {
        InvalidateHierarchyFilterCacheUVE();
    }
    m_sceneDirty = true;
    RecordHistoryUVE(SceneComponentHistoryEntryUVE{
        m_selectedEntity, kind, before, value, selectionBefore, CaptureSelectionSnapshotUVE(), dirtyBefore, true});
    return true;
}

bool EditorUVE::RemoveSelectedSceneComponentUVE(const EditorSceneComponentKindUVE kind) {
    if (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE()) {
        return false;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    std::optional<EditorSceneComponentValueUVE> before;
    switch (kind) {
        case EditorSceneComponentKindUVE::Camera:
            if (entityManager.HasComponentUVE<Scene::CameraComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::CameraComponentUVE>(m_selectedEntity);
            break;
        case EditorSceneComponentKindUVE::Mesh:
            if (entityManager.HasComponentUVE<Scene::MeshComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::MeshComponentUVE>(m_selectedEntity);
            break;
        case EditorSceneComponentKindUVE::Light:
            if (entityManager.HasComponentUVE<Scene::LightComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::LightComponentUVE>(m_selectedEntity);
            break;
        case EditorSceneComponentKindUVE::Collider:
            if (entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(m_selectedEntity);
            break;
        case EditorSceneComponentKindUVE::Rigid3D:
            if (entityManager.HasComponentUVE<Scene::Rigid3DComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(m_selectedEntity);
            break;
        case EditorSceneComponentKindUVE::AudioSource:
            if (entityManager.HasComponentUVE<Scene::AudioSourceComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::AudioSourceComponentUVE>(m_selectedEntity);
            break;
        case EditorSceneComponentKindUVE::ParticleEmitter:
            if (entityManager.HasComponentUVE<Scene::ParticleEmitterComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::ParticleEmitterComponentUVE>(m_selectedEntity);
            break;
        case EditorSceneComponentKindUVE::Script:
            if (entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(m_selectedEntity);
            break;
        case EditorSceneComponentKindUVE::AnimationSequencer:
            if (entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(m_selectedEntity);
            break;
        case EditorSceneComponentKindUVE::WorldEnvironment:
            if (entityManager.HasComponentUVE<Scene::WorldEnvironment3DComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::WorldEnvironment3DComponentUVE>(m_selectedEntity);
            break;
        case EditorSceneComponentKindUVE::CharacterController:
            if (entityManager.HasComponentUVE<Scene::CharacterControllerComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(m_selectedEntity);
            break;
        case EditorSceneComponentKindUVE::Canvas:
            if (entityManager.HasComponentUVE<Scene::CanvasComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::CanvasComponentUVE>(m_selectedEntity);
            break;
        case EditorSceneComponentKindUVE::UIText:
            if (entityManager.HasComponentUVE<Scene::UITextComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::UITextComponentUVE>(m_selectedEntity);
            break;
        case EditorSceneComponentKindUVE::UIImage:
            if (entityManager.HasComponentUVE<Scene::UIImageComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::UIImageComponentUVE>(m_selectedEntity);
            break;
        case EditorSceneComponentKindUVE::UIButton:
            if (entityManager.HasComponentUVE<Scene::UIButtonComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(m_selectedEntity);
            break;
        case EditorSceneComponentKindUVE::PhysicsInterpolation:
            if (entityManager.HasComponentUVE<Scene::PhysicsInterpolationComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::PhysicsInterpolationComponentUVE>(m_selectedEntity);
            break;
        case EditorSceneComponentKindUVE::EditorDescription:
            if (entityManager.HasComponentUVE<Scene::EditorDescriptionComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::EditorDescriptionComponentUVE>(m_selectedEntity);
            break;
        case EditorSceneComponentKindUVE::Process:
            if (entityManager.HasComponentUVE<Scene::ProcessComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::ProcessComponentUVE>(m_selectedEntity);
            break;
        case EditorSceneComponentKindUVE::ThreadGroup:
            if (entityManager.HasComponentUVE<Scene::ThreadGroupComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::ThreadGroupComponentUVE>(m_selectedEntity);
            break;
        case EditorSceneComponentKindUVE::AutoTranslate:
            if (entityManager.HasComponentUVE<Scene::AutoTranslateComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::AutoTranslateComponentUVE>(m_selectedEntity);
            break;
        case EditorSceneComponentKindUVE::ObjectMetadata:
            if (entityManager.HasComponentUVE<Scene::ObjectMetadataComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::ObjectMetadataComponentUVE>(m_selectedEntity);
            break;
    }
    if (!before.has_value()) {
        return false;
    }

    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    if (!ApplySceneComponentStateUVE(m_selectedEntity, kind, std::nullopt)) {
        return false;
    }
    InvalidateHierarchyFilterCacheUVE();
    m_sceneDirty = true;
    RecordHistoryUVE(SceneComponentHistoryEntryUVE{
        m_selectedEntity, kind, before, std::nullopt, selectionBefore, CaptureSelectionSnapshotUVE(), dirtyBefore, true});
    return true;
}

bool EditorUVE::SetSelectedEntityNameUVE(std::string name) {
    if (!IsAuthoringCommandAllowedUVE() || !IsDocumentEntityUVE(m_selectedEntity) ||
        !IsEntitySelectedUVE(m_selectedEntity) || IsEntityLockedUVE(m_selectedEntity)) {
        return false;
    }
    return SetHierarchyEntityNameUVE(m_selectedEntity, std::move(name));
}

bool EditorUVE::SetHierarchyEntityNameUVE(const Scene::EntityUVE entity, std::string name) {
    if (!IsAuthoringCommandAllowedUVE() || !IsDocumentEntityUVE(entity) || IsEntityLockedUVE(entity) ||
        !IsEntityNameValidUVE(name)) {
        return false;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    std::optional<std::string> beforeName;
    if (entityManager.HasComponentUVE<Scene::NameComponentUVE>(entity)) {
        beforeName = entityManager.GetComponentUVE<Scene::NameComponentUVE>(entity).name;
        if (*beforeName == name) {
            return false;
        }
    }

    // Manual names use the same document-wide numeric-suffix policy as newly created objects.
    // Ignore the renamed entity so an available suffix it already owns does not become "Name 3".
    std::string uniqueName = MakeUniqueDocumentEntityNameUVE(name, entity);
    if (!IsEntityNameValidUVE(uniqueName) || (beforeName.has_value() && *beforeName == uniqueName)) {
        return false;
    }

    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    const std::optional<std::string> afterName{std::move(uniqueName)};
    if (!ApplyEntityNameStateUVE(entity, afterName)) {
        return false;
    }

    m_sceneDirty = true;
    RecordHistoryUVE(NameHistoryEntryUVE{
        entity, beforeName, afterName, selectionBefore, CaptureSelectionSnapshotUVE(), dirtyBefore, true});
    return true;
}

bool EditorUVE::ComputeTranslatedTransformUVE(const Scene::EntityUVE entity,
                                              const Math::Vector3UVE& worldDelta,
                                              const Scene::TransformComponentUVE& source,
                                              Scene::TransformComponentUVE& outTransform) const {
    if (!IsFiniteVectorUVE(worldDelta)) {
        return false;
    }
    Math::Vector3UVE localDelta{};
    if (!ComputeLocalDeltaForWorldDeltaUVE(entity, worldDelta, localDelta)) {
        return false;
    }
    outTransform = source;
    outTransform.localPosition += localDelta;
    return true;
}

bool EditorUVE::ComputeGestureTransformUVE(const EditorToolSessionModeUVE mode,
                                           const EditorTransformAxisUVE axis, const float amount,
                                           const Scene::TransformComponentUVE& source,
                                           Scene::TransformComponentUVE& outTransform) const {
    if (!IsFiniteUVE(amount)) {
        return false;
    }

    outTransform = source;
    switch (mode) {
        case EditorToolSessionModeUVE::Translate: {
            if (axis == EditorTransformAxisUVE::None) {
                return false;
            }
            // Snap the DISTANCE along the axis, then build the delta from it - snapping the
            // resulting vector per component would quantise a diagonal axis differently.
            const float snappedDistance =
                m_transformSnappingSettings.enabled
                    ? SnapScalarUVE(amount, m_transformSnappingSettings.translateStep)
                    : amount;
            return ComputeTranslatedTransformUVE(m_selectedEntity,
                                                 GetAxisVectorUVE(axis) * snappedDistance, source,
                                                 outTransform);
        }
        case EditorToolSessionModeUVE::Rotate: {
            if (axis == EditorTransformAxisUVE::None) {
                return false;
            }
            const float rotateStepRadians =
                (m_transformSnappingSettings.rotateStepDegrees * std::numbers::pi_v<float>) / 180.0F;
            const float snappedRadians = m_transformSnappingSettings.enabled
                                             ? SnapScalarUVE(amount, rotateStepRadians)
                                             : amount;
            Math::QuaternionUVE localRotation{};
            if (!ComputeLocalRotationForWorldAxisUVE(m_selectedEntity, source.localRotation,
                                                     GetAxisVectorUVE(axis), snappedRadians,
                                                     localRotation)) {
                return false;
            }
            outTransform.localRotation = localRotation;
            return true;
        }
        case EditorToolSessionModeUVE::Scale: {
            const float snappedDelta = m_transformSnappingSettings.enabled
                                           ? SnapScalarUVE(amount, m_transformSnappingSettings.scaleStep)
                                           : amount;
            if (axis == EditorTransformAxisUVE::None) {
                // Uniform: every component moves by the same additive offset. The command rejects
                // as a whole if any result is invalid; it never clamps one component or silently
                // turns the request into a proportional scale.
                if (!IsFiniteUVE(snappedDelta)) {
                    return false;
                }
                outTransform.localScale.x += snappedDelta;
                outTransform.localScale.y += snappedDelta;
                outTransform.localScale.z += snappedDelta;
                return IsFiniteUVE(outTransform.localScale.x) && IsFiniteUVE(outTransform.localScale.y) &&
                       IsFiniteUVE(outTransform.localScale.z) &&
                       outTransform.localScale.x >= kMinimumLocalScaleUVE &&
                       outTransform.localScale.y >= kMinimumLocalScaleUVE &&
                       outTransform.localScale.z >= kMinimumLocalScaleUVE;
            }
            float* component = nullptr;
            switch (axis) {
                case EditorTransformAxisUVE::X: component = &outTransform.localScale.x; break;
                case EditorTransformAxisUVE::Y: component = &outTransform.localScale.y; break;
                case EditorTransformAxisUVE::Z: component = &outTransform.localScale.z; break;
                case EditorTransformAxisUVE::None: return false;
            }
            *component += snappedDelta;
            return IsFiniteUVE(*component) && *component >= kMinimumLocalScaleUVE;
        }
    }
    return false;
}

/// The shared guard the four public axis commands used to each repeat: valid editor state, a
/// single document entity selected, and that entity actually carrying a transform to read. It
/// reads the LIVE transform as the source, which is what an incremental command means.
bool EditorUVE::TryComputeSelectedGestureTransformUVE(const EditorToolSessionModeUVE mode,
                                                      const EditorTransformAxisUVE axis,
                                                      const float amount,
                                                      Scene::TransformComponentUVE& outTransform) const {
    if (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE()) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity)) {
        return false;
    }
    const Scene::TransformComponentUVE live =
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity);
    return ComputeGestureTransformUVE(mode, axis, amount, live, outTransform);
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
//   * Previews go through ApplyLocalTransformUVE, the same scene-graph write the commands use,
//     but deliberately NOT through SetSelectedLocalTransformUVE - that is the one that records
//     history, which is precisely what a preview must not do.
// ---------------------------------------------------------------------------------------------

bool EditorUVE::BeginTransformGestureUVE(const EditorToolSessionModeUVE mode) {
    if (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE()) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity)) {
        return false;
    }
    const Scene::TransformComponentUVE baseline =
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity);
    // m_sceneDirty travels with the baseline so a cancelled gesture restores the document's
    // unsaved state as well as its transform - a drag that is abandoned must not leave the scene
    // looking modified.
    return m_toolSession.BeginUVE(m_selectedEntity, mode, baseline, m_sceneDirty);
}

bool EditorUVE::PreviewTransformGestureUVE(const EditorTransformAxisUVE axis, const float totalAmount) {
    if (m_toolSession.GetPhaseUVE() != EditorToolSessionPhaseUVE::Previewing) {
        return false;
    }
    const std::optional<EditorToolSessionSnapshotUVE>& snapshot = m_toolSession.GetSnapshotUVE();
    if (!snapshot.has_value() || !IsAuthoringCommandAllowedUVE()) {
        return false;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const Scene::EntityUVE entity = snapshot->entity;
    if (!entityManager.IsAliveUVE(entity) ||
        !entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity)) {
        // The gesture's target went away underneath it. Discard rather than cancel: there is no
        // live transform left to compare against, so no restore can be claimed.
        m_toolSession.DiscardUVE();
        return false;
    }

    Scene::TransformComponentUVE updated{};
    if (!ComputeGestureTransformUVE(snapshot->mode, axis, totalAmount, snapshot->baselineTransform,
                                    updated)) {
        return false;
    }
    if (!IsTransformFiniteUVE(updated) || !ApplyLocalTransformUVE(entity, updated)) {
        return false;
    }
    m_sceneDirty = true;
    return m_toolSession.RecordPreviewAppliedUVE(updated);
}

bool EditorUVE::PreviewTranslateGestureUVE(const Math::Vector3UVE& totalWorldDelta) {
    if (m_toolSession.GetPhaseUVE() != EditorToolSessionPhaseUVE::Previewing) {
        return false;
    }
    const std::optional<EditorToolSessionSnapshotUVE>& snapshot = m_toolSession.GetSnapshotUVE();
    if (!snapshot.has_value() || snapshot->mode != EditorToolSessionModeUVE::Translate ||
        !IsAuthoringCommandAllowedUVE() || !IsFiniteVectorUVE(totalWorldDelta)) {
        return false;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const Scene::EntityUVE entity = snapshot->entity;
    if (!entityManager.IsAliveUVE(entity) ||
        !entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity)) {
        m_toolSession.DiscardUVE();
        return false;
    }

    // A plane drag has no single axis, so each component is quantised on its own - which lands on
    // the same lattice a pair of axis drags would have reached.
    Math::Vector3UVE worldDelta = totalWorldDelta;
    if (m_transformSnappingSettings.enabled) {
        const float step = m_transformSnappingSettings.translateStep;
        worldDelta = Math::Vector3UVE{SnapScalarUVE(worldDelta.x, step), SnapScalarUVE(worldDelta.y, step),
                                      SnapScalarUVE(worldDelta.z, step)};
    }

    Scene::TransformComponentUVE updated{};
    if (!ComputeTranslatedTransformUVE(entity, worldDelta, snapshot->baselineTransform, updated)) {
        return false;
    }
    if (!IsTransformFiniteUVE(updated) || !ApplyLocalTransformUVE(entity, updated)) {
        return false;
    }
    m_sceneDirty = true;
    return m_toolSession.RecordPreviewAppliedUVE(updated);
}

bool EditorUVE::PreviewTransformGestureValueUVE(const Scene::TransformComponentUVE& transform) {
    if (m_toolSession.GetPhaseUVE() != EditorToolSessionPhaseUVE::Previewing) {
        return false;
    }
    const std::optional<EditorToolSessionSnapshotUVE>& snapshot = m_toolSession.GetSnapshotUVE();
    if (!snapshot.has_value() || !IsAuthoringCommandAllowedUVE() || !IsTransformFiniteUVE(transform)) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const Scene::EntityUVE entity = snapshot->entity;
    if (!entityManager.IsAliveUVE(entity) ||
        !entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity)) {
        m_toolSession.DiscardUVE();
        return false;
    }
    if (!ApplyLocalTransformUVE(entity, transform)) {
        return false;
    }
    m_sceneDirty = true;
    return m_toolSession.RecordPreviewAppliedUVE(transform);
}

bool EditorUVE::CommitTransformGestureUVE() {
    if (m_toolSession.GetPhaseUVE() != EditorToolSessionPhaseUVE::Previewing) {
        return false;
    }
    const std::optional<EditorToolSessionSnapshotUVE>& live = m_toolSession.GetSnapshotUVE();
    if (!live.has_value()) {
        return false;
    }
    const bool changed = !AreTransformsEqualUVE(live->baselineTransform, live->lastAppliedTransform);

    const std::optional<EditorToolSessionSnapshotUVE> snapshot = m_toolSession.CommitUVE(changed);
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
    const EditorSelectionSnapshotUVE selection = CaptureSelectionSnapshotUVE();
    m_sceneDirty = true;
    RecordHistoryUVE(TransformHistoryEntryUVE{snapshot->entity, snapshot->baselineTransform,
                                              snapshot->lastAppliedTransform, selection, selection,
                                              snapshot->baselineDirty, true});
    return true;
}

bool EditorUVE::CancelTransformGestureUVE() {
    if (m_toolSession.GetPhaseUVE() != EditorToolSessionPhaseUVE::Previewing) {
        return false;
    }
    const std::optional<EditorToolSessionSnapshotUVE>& live = m_toolSession.GetSnapshotUVE();
    if (!live.has_value()) {
        return false;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const Scene::EntityUVE entity = live->entity;
    if (!entityManager.IsAliveUVE(entity) ||
        !entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity)) {
        m_toolSession.DiscardUVE();
        return false;
    }

    const Scene::TransformComponentUVE current =
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
    const std::optional<EditorToolSessionSnapshotUVE> snapshot = m_toolSession.CancelUVE(current);
    if (!snapshot.has_value()) {
        // ExternalTransformConflict: something else moved this entity since the last preview, so
        // the baseline is stale and writing it back would silently discard that change. The
        // session has already cleared; the external value is left exactly as it stands.
        return false;
    }

    if (!ApplyLocalTransformUVE(entity, snapshot->baselineTransform)) {
        m_toolSession.MarkRestoreFailedUVE();
        return false;
    }
    m_sceneDirty = snapshot->baselineDirty;
    return true;
}

bool EditorUVE::TranslateSelectedAlongAxisUVE(const EditorTransformAxisUVE axis, const float worldDistance) {
    Scene::TransformComponentUVE updated{};
    if (!TryComputeSelectedGestureTransformUVE(EditorToolSessionModeUVE::Translate, axis, worldDistance,
                                               updated)) {
        return false;
    }
    return SetSelectedLocalTransformUVE(updated);
}

bool EditorUVE::RotateSelectedAroundWorldAxisUVE(const EditorTransformAxisUVE axis, const float radians) {
    Scene::TransformComponentUVE updated{};
    if (!TryComputeSelectedGestureTransformUVE(EditorToolSessionModeUVE::Rotate, axis, radians, updated)) {
        return false;
    }
    return SetSelectedLocalTransformUVE(updated);
}

bool EditorUVE::ScaleSelectedAlongAxisUVE(const EditorTransformAxisUVE axis,
                                          const float localScaleDelta) {
    if (axis == EditorTransformAxisUVE::None) {
        return false; // None means "uniform" to the shared helper; this command is per-axis only
    }
    Scene::TransformComponentUVE updated{};
    if (!TryComputeSelectedGestureTransformUVE(EditorToolSessionModeUVE::Scale, axis, localScaleDelta,
                                               updated)) {
        return false;
    }
    return SetSelectedLocalTransformUVE(updated);
}

bool EditorUVE::ScaleSelectedUniformlyUVE(const float localScaleOffset) {
    Scene::TransformComponentUVE updated{};
    if (!TryComputeSelectedGestureTransformUVE(EditorToolSessionModeUVE::Scale,
                                               EditorTransformAxisUVE::None, localScaleOffset, updated)) {
        return false;
    }
    return SetSelectedLocalTransformUVE(updated);
}

bool EditorUVE::IsReparentModeChangeAllowedUVE() const noexcept {
    return IsAuthoringCommandAllowedUVE();
}

bool EditorUVE::SetReparentTransformModeUVE(const EditorReparentTransformModeUVE mode) {
    if (!IsReparentModeChangeAllowedUVE()) {
        return false;
    }
    m_reparentTransformMode = mode;
    return true;
}

EditorReparentTransformModeUVE EditorUVE::GetReparentTransformModeUVE() const noexcept {
    return m_reparentTransformMode;
}

bool EditorUVE::SetTransformSnappingSettingsUVE(const EditorTransformSnappingSettingsUVE& settings) {
    if (!IsAuthoringCommandAllowedUVE() || !AreTransformSnappingSettingsValidUVE(settings)) {
        return false;
    }
    const EditorTransformSnappingSettingsUVE previous = m_transformSnappingSettings;
    m_transformSnappingSettings = settings;
    m_viewportOverlayState.snapEnabled = settings.enabled;
    namespace Id = EditorSettingIdUVE;
    NotifyEditorSettingChangedUVE(Id::kSnapEnabledUVE, previous.enabled, settings.enabled);
    NotifyEditorSettingChangedUVE(Id::kSnapTranslateStepUVE, static_cast<double>(previous.translateStep),
                                  static_cast<double>(settings.translateStep));
    NotifyEditorSettingChangedUVE(Id::kSnapRotateStepDegreesUVE, static_cast<double>(previous.rotateStepDegrees),
                                  static_cast<double>(settings.rotateStepDegrees));
    NotifyEditorSettingChangedUVE(Id::kSnapScaleStepUVE, static_cast<double>(previous.scaleStep),
                                  static_cast<double>(settings.scaleStep));
    return true;
}

const EditorTransformSnappingSettingsUVE& EditorUVE::GetTransformSnappingSettingsUVE() const noexcept {
    return m_transformSnappingSettings;
}

std::optional<EditorSelectionBoundsUVE> EditorUVE::TryGetSelectedBoundsUVE() const {
    return TryGetEntityBoundsUVE(m_selectedEntity);
}

std::optional<EditorSelectionBoundsUVE> EditorUVE::TryGetEntityBoundsUVE(const Scene::EntityUVE entity) const {
    if (m_state != EditorStateUVE::Running || !IsDocumentEntityUVE(entity)) {
        return std::nullopt;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity) ||
        !entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity) ||
        !entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(entity)) {
        return std::nullopt;
    }

    const Scene::WorldTransformComponentUVE& worldTransform =
        entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
    const Scene::ColliderComponentUVE& collider =
        entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(entity);
    if (worldTransform.dirty || !IsFiniteVectorUVE(worldTransform.worldPosition) ||
        !IsFiniteVectorUVE(worldTransform.worldScale) || !IsFiniteVectorUVE(collider.halfExtents) ||
        collider.halfExtents.x <= kVectorEpsilonUVE || collider.halfExtents.y <= kVectorEpsilonUVE ||
        collider.halfExtents.z <= kVectorEpsilonUVE ||
        std::abs(worldTransform.worldScale.x) <= kVectorEpsilonUVE ||
        std::abs(worldTransform.worldScale.y) <= kVectorEpsilonUVE ||
        std::abs(worldTransform.worldScale.z) <= kVectorEpsilonUVE) {
        return std::nullopt;
    }

    Math::QuaternionUVE normalizedRotation{};
    if (!Math::TryNormalizeUVE(worldTransform.worldRotation, normalizedRotation)) {
        return std::nullopt;
    }

    constexpr std::array<Math::Vector3UVE, 8> kCornerSignsUVE{
        Math::Vector3UVE{-1.0F, -1.0F, -1.0F},
        Math::Vector3UVE{1.0F, -1.0F, -1.0F},
        Math::Vector3UVE{1.0F, 1.0F, -1.0F},
        Math::Vector3UVE{-1.0F, 1.0F, -1.0F},
        Math::Vector3UVE{-1.0F, -1.0F, 1.0F},
        Math::Vector3UVE{1.0F, -1.0F, 1.0F},
        Math::Vector3UVE{1.0F, 1.0F, 1.0F},
        Math::Vector3UVE{-1.0F, 1.0F, 1.0F},
    };

    EditorSelectionBoundsUVE bounds{};
    bounds.worldCenter = worldTransform.worldPosition;
    for (std::size_t index = 0U; index < kCornerSignsUVE.size(); ++index) {
        const Math::Vector3UVE localCorner = kCornerSignsUVE[index] * collider.halfExtents;
        const Math::Vector3UVE scaledCorner = localCorner * worldTransform.worldScale;
        bounds.worldCorners[index] = worldTransform.worldPosition +
                                     Math::RotateVectorUVE(normalizedRotation, scaledCorner);
        if (!IsFiniteVectorUVE(bounds.worldCorners[index])) {
            return std::nullopt;
        }
    }
    return bounds;
}

Scene::EntityUVE EditorUVE::CreateDocumentEntityUVE(const EditorEntityKindUVE kind) {
    if (!IsAuthoringCommandAllowedUVE()) {
        return Scene::kInvalidEntityUVE;
    }

    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    const Scene::EntityUVE entity = CreateDocumentEntityInternalUVE(kind, std::nullopt);
    if (entity == Scene::kInvalidEntityUVE) {
        return Scene::kInvalidEntityUVE;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const std::string createdName = entityManager.GetComponentUVE<Scene::NameComponentUVE>(entity).name;
    SelectEntityUVE(entity);
    m_sceneDirty = true;
    RecordHistoryUVE(CreationHistoryEntryUVE{
        kind, createdName, entity, selectionBefore, CaptureSelectionSnapshotUVE(), dirtyBefore, true});
    return entity;
}

namespace {

// The recipe extras every Recipe-based kind carries: Visibility plus the common Object section.
// One helper so the owned-components switch below cannot list them differently per kind.
template <typename Visitor>
void VisitTypeChangeRecipeCommonUVE(Visitor& visitor) {
    visitor.template OnUVE<Scene::VisibilityComponentUVE>();
    visitor.template OnUVE<Scene::ProcessComponentUVE>();
    visitor.template OnUVE<Scene::ThreadGroupComponentUVE>();
    visitor.template OnUVE<Scene::PhysicsInterpolationComponentUVE>();
    visitor.template OnUVE<Scene::AutoTranslateComponentUVE>();
    visitor.template OnUVE<Scene::EditorDescriptionComponentUVE>();
    visitor.template OnUVE<Scene::ScriptComponentUVE>();
    visitor.template OnUVE<Scene::ObjectMetadataComponentUVE>();
}

// What the pure-Object kinds (no Transform, nothing to hide) carry instead: the same common
// Object section without Visibility.
template <typename Visitor>
void VisitTypeChangeObjectCommonUVE(Visitor& visitor) {
    visitor.template OnUVE<Scene::ProcessComponentUVE>();
    visitor.template OnUVE<Scene::ThreadGroupComponentUVE>();
    visitor.template OnUVE<Scene::PhysicsInterpolationComponentUVE>();
    visitor.template OnUVE<Scene::AutoTranslateComponentUVE>();
    visitor.template OnUVE<Scene::EditorDescriptionComponentUVE>();
    visitor.template OnUVE<Scene::ScriptComponentUVE>();
    visitor.template OnUVE<Scene::ObjectMetadataComponentUVE>();
}

// Every component `kind` attaches beyond the creation shell (Transform, WorldTransform, Hierarchy,
// Name) and the kind tag - read off the kind's Apply path, base helpers included. The one table
// the type change walks three ways: collecting type indices for the legality check, capturing
// values while removing, and capturing the fresh set afterwards. Replay walks the recorded values
// instead, so this switch and SceneObjectTypeChangeValueUVE cover the same types by construction:
// a type stored here that the variant lacks fails to compile at the capture. False only for the
// structural kinds, which never convert in either direction.
template <typename Visitor>
[[nodiscard]] bool VisitSceneObjectTypeChangeOwnedUVE(const Scene::Objects::SceneObjectKindUVE kind,
                                                      Visitor& visitor) {
    switch (kind) {
        case Scene::Objects::SceneObjectKindUVE::Object3D:
            VisitTypeChangeRecipeCommonUVE(visitor);
            return true;
        case Scene::Objects::SceneObjectKindUVE::Area3D:
            visitor.template OnUVE<Scene::AreaComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::RayCast3D:
            visitor.template OnUVE<Scene::RayCast3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::Static3D:
            visitor.template OnUVE<Scene::ColliderComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::Kinematic3D:
            VisitTypeChangeRecipeCommonUVE(visitor);
            visitor.template OnUVE<Scene::PhysicsObjectComponentUVE>();
            visitor.template OnUVE<Scene::ColliderComponentUVE>();
            visitor.template OnUVE<Scene::Rigid3DComponentUVE>();
            visitor.template OnUVE<Scene::Kinematic3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::NavMeshVolume3D:
            visitor.template OnUVE<Scene::NavMeshVolume3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::NavSeeker3D:
            visitor.template OnUVE<Scene::NavSeeker3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::Skeleton3D:
            VisitTypeChangeRecipeCommonUVE(visitor);
            visitor.template OnUVE<Scene::Skeleton3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::BoneAttachment3D:
            visitor.template OnUVE<Scene::BoneAttachment3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::SpringArm3D:
            visitor.template OnUVE<Scene::SpringArm3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::Marker3D:
            visitor.template OnUVE<Scene::Marker3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::Hitbox3D:
            visitor.template OnUVE<Scene::Hitbox3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::Hurtbox3D:
            visitor.template OnUVE<Scene::Hurtbox3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::Projectile3D:
            visitor.template OnUVE<Scene::Projectile3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::InteractionArea3D:
            visitor.template OnUVE<Scene::InteractionArea3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::WorldEnvironment3D:
            VisitTypeChangeObjectCommonUVE(visitor);
            visitor.template OnUVE<Scene::WorldEnvironment3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::ReflectionProbe3D:
            VisitTypeChangeRecipeCommonUVE(visitor);
            visitor.template OnUVE<Scene::ReflectionProbe3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::Decal3D:
            VisitTypeChangeRecipeCommonUVE(visitor);
            visitor.template OnUVE<Scene::RenderInstanceComponentUVE>();
            visitor.template OnUVE<Scene::Decal3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::LODGroup3D:
            VisitTypeChangeRecipeCommonUVE(visitor);
            visitor.template OnUVE<Scene::LodGroup3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::Occluder3D:
            VisitTypeChangeRecipeCommonUVE(visitor);
            visitor.template OnUVE<Scene::Occluder3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::VisibilityRegion3D:
            VisitTypeChangeRecipeCommonUVE(visitor);
            visitor.template OnUVE<Scene::VisibilityRegion3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::SpawnPoint3D:
            visitor.template OnUVE<Scene::SpawnPoint3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::LevelStreamer3D:
            visitor.template OnUVE<Scene::LevelStreamer3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::WorldPartition3D:
            VisitTypeChangeRecipeCommonUVE(visitor);
            visitor.template OnUVE<Scene::WorldPartition3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::AnimationGraph:
            VisitTypeChangeObjectCommonUVE(visitor);
            visitor.template OnUVE<Scene::AnimationDriverComponentUVE>();
            visitor.template OnUVE<Scene::AnimationGraphComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::AnimationSequencer:
            VisitTypeChangeObjectCommonUVE(visitor);
            visitor.template OnUVE<Scene::AnimationDriverComponentUVE>();
            visitor.template OnUVE<Scene::AnimationSequencerComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::Character3D:
            VisitTypeChangeRecipeCommonUVE(visitor);
            visitor.template OnUVE<Scene::PhysicsObjectComponentUVE>();
            visitor.template OnUVE<Scene::SolidBodyComponentUVE>();
            visitor.template OnUVE<Scene::ColliderComponentUVE>();
            visitor.template OnUVE<Scene::CharacterControllerComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::Camera3D:
            visitor.template OnUVE<Scene::CameraComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::MeshInstance3D:
            VisitTypeChangeRecipeCommonUVE(visitor);
            visitor.template OnUVE<Scene::RenderInstanceComponentUVE>();
            visitor.template OnUVE<Scene::SurfaceInstanceComponentUVE>();
            visitor.template OnUVE<Scene::MeshComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::BoxMesh3D:
        case Scene::Objects::SceneObjectKindUVE::SphereMesh3D:
        case Scene::Objects::SceneObjectKindUVE::PlaneMesh3D:
            VisitTypeChangeRecipeCommonUVE(visitor);
            visitor.template OnUVE<Scene::RenderInstanceComponentUVE>();
            visitor.template OnUVE<Scene::SurfaceInstanceComponentUVE>();
            visitor.template OnUVE<Scene::PrimitiveMeshComponentUVE>();
            visitor.template OnUVE<Scene::ColliderComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::Light3D:
            visitor.template OnUVE<Scene::LightComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::Collider3D:
            visitor.template OnUVE<Scene::ColliderComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::Rigid3D:
            VisitTypeChangeRecipeCommonUVE(visitor);
            visitor.template OnUVE<Scene::PhysicsObjectComponentUVE>();
            visitor.template OnUVE<Scene::ColliderComponentUVE>();
            visitor.template OnUVE<Scene::Rigid3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::AudioSource3D:
            visitor.template OnUVE<Scene::AudioSourceComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::ParticleEmitter3D:
            VisitTypeChangeRecipeCommonUVE(visitor);
            visitor.template OnUVE<Scene::RenderInstanceComponentUVE>();
            visitor.template OnUVE<Scene::SurfaceInstanceComponentUVE>();
            visitor.template OnUVE<Scene::ParticleEmitterComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::Script:
            visitor.template OnUVE<Scene::ScriptComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::Canvas:
            visitor.template OnUVE<Scene::CanvasComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::UIText:
            visitor.template OnUVE<Scene::UITextComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::UIImage:
            visitor.template OnUVE<Scene::UIImageComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::UIButton:
            visitor.template OnUVE<Scene::UIButtonComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::FogVolume3D:
            VisitTypeChangeRecipeCommonUVE(visitor);
            visitor.template OnUVE<Scene::RenderInstanceComponentUVE>();
            visitor.template OnUVE<Scene::FogVolume3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::DirectionalLight3D:
            VisitTypeChangeRecipeCommonUVE(visitor);
            visitor.template OnUVE<Scene::RenderInstanceComponentUVE>();
            visitor.template OnUVE<Scene::LightEmitterComponentUVE>();
            visitor.template OnUVE<Scene::DirectionalLight3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::TwoBoneIK3D:
            VisitTypeChangeRecipeCommonUVE(visitor);
            visitor.template OnUVE<Scene::BoneModifierComponentUVE>();
            visitor.template OnUVE<Scene::TwoBoneIK3DComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::Player3D:
            VisitTypeChangeRecipeCommonUVE(visitor);
            visitor.template OnUVE<Scene::PhysicsObjectComponentUVE>();
            visitor.template OnUVE<Scene::SolidBodyComponentUVE>();
            visitor.template OnUVE<Scene::ColliderComponentUVE>();
            visitor.template OnUVE<Scene::CharacterControllerComponentUVE>();
            visitor.template OnUVE<Scene::PlayerComponentUVE>();
            visitor.template OnUVE<Scene::HealthComponentUVE>();
            return true;
        case Scene::Objects::SceneObjectKindUVE::Object:
        case Scene::Objects::SceneObjectKindUVE::Viewport:
        case Scene::Objects::SceneObjectKindUVE::Folder:
            return false;
    }
    // Unreachable for a valid kind; keeps the function total for any raw enumerator value.
    return false;
}

struct TypeChangeTypeCollectorUVE final {
    std::vector<std::type_index>& out;
    template <typename T>
    void OnUVE() {
        out.push_back(std::type_index(typeid(T)));
    }
};

[[nodiscard]] bool CollectTypeChangeOwnedTypeIndicesUVE(
    const Scene::Objects::SceneObjectKindUVE kind, std::vector<std::type_index>& out) {
    TypeChangeTypeCollectorUVE collector{out};
    return VisitSceneObjectTypeChangeOwnedUVE(kind, collector);
}

struct TypeChangeCaptureVisitorUVE final {
    Scene::IEntityManagerUVE& entityManager;
    Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
    std::vector<SceneObjectTypeChangeValueUVE>& out;
    bool remove = false;
    template <typename T>
    void OnUVE() {
        // Absent is fine, not an error: the author may have removed one of the kind's components
        // by hand, and only what is actually there needs carrying.
        if (!entityManager.HasComponentUVE<T>(entity)) {
            return;
        }
        out.emplace_back(entityManager.GetComponentUVE<T>(entity));
        if (remove) {
            entityManager.RemoveComponentUVE<T>(entity);
        }
    }
};

// ---- Per-kind recipe-default extras -----------------------------------------------------------
// Creation attaches the extras named by editor.objects.defaultExtras.<kindTypeId> with engine
// defaults. The component ids below must stay in lockstep with the palette in
// editor_settings_uve.cpp (kDefaultExtraComponentIdsUVE); the kitchen-sink creation test proves
// every palette member attaches here.
template <typename Component>
void AttachDefaultExtraIfMissingUVE(Scene::IEntityManagerUVE& entityManager, const Scene::EntityUVE entity) {
    if (!entityManager.HasComponentUVE<Component>(entity)) {
        entityManager.AddComponentUVE<Component>(entity, Component{});
    }
}

struct DefaultExtraAttachmentUVE final {
    std::string_view componentId;
    void (*attach)(Scene::IEntityManagerUVE&, const Scene::EntityUVE);
};

constexpr DefaultExtraAttachmentUVE kDefaultExtraAttachmentsUVE[] = {
    {"component.script", &AttachDefaultExtraIfMissingUVE<Scene::ScriptComponentUVE>},
    {"component.object_metadata", &AttachDefaultExtraIfMissingUVE<Scene::ObjectMetadataComponentUVE>},
    {"component.editor_description", &AttachDefaultExtraIfMissingUVE<Scene::EditorDescriptionComponentUVE>},
    {"component.mesh", &AttachDefaultExtraIfMissingUVE<Scene::MeshComponentUVE>},
    {"component.primitive_mesh", &AttachDefaultExtraIfMissingUVE<Scene::PrimitiveMeshComponentUVE>},
    {"component.collider", &AttachDefaultExtraIfMissingUVE<Scene::ColliderComponentUVE>},
    {"component.rigid_body", &AttachDefaultExtraIfMissingUVE<Scene::Rigid3DComponentUVE>},
    {"component.solid_body", &AttachDefaultExtraIfMissingUVE<Scene::SolidBodyComponentUVE>},
    {"component.physics_object", &AttachDefaultExtraIfMissingUVE<Scene::PhysicsObjectComponentUVE>},
    {"component.kinematic_3d", &AttachDefaultExtraIfMissingUVE<Scene::Kinematic3DComponentUVE>},
    {"component.character_controller", &AttachDefaultExtraIfMissingUVE<Scene::CharacterControllerComponentUVE>},
    {"component.area", &AttachDefaultExtraIfMissingUVE<Scene::AreaComponentUVE>},
    {"component.camera", &AttachDefaultExtraIfMissingUVE<Scene::CameraComponentUVE>},
    {"component.audio_source", &AttachDefaultExtraIfMissingUVE<Scene::AudioSourceComponentUVE>},
    {"component.particle_emitter", &AttachDefaultExtraIfMissingUVE<Scene::ParticleEmitterComponentUVE>},
    {"component.ui_button", &AttachDefaultExtraIfMissingUVE<Scene::UIButtonComponentUVE>},
    {"component.ui_text", &AttachDefaultExtraIfMissingUVE<Scene::UITextComponentUVE>},
    {"component.ui_image", &AttachDefaultExtraIfMissingUVE<Scene::UIImageComponentUVE>},
    {"component.canvas", &AttachDefaultExtraIfMissingUVE<Scene::CanvasComponentUVE>},
    {"component.auto_translate", &AttachDefaultExtraIfMissingUVE<Scene::AutoTranslateComponentUVE>},
    {"component.thread_group", &AttachDefaultExtraIfMissingUVE<Scene::ThreadGroupComponentUVE>},
    {"component.process", &AttachDefaultExtraIfMissingUVE<Scene::ProcessComponentUVE>},
    {"component.bone_modifier", &AttachDefaultExtraIfMissingUVE<Scene::BoneModifierComponentUVE>},
    {"component.animation_player", &AttachDefaultExtraIfMissingUVE<Scene::AnimationSequencerComponentUVE>},
    {"component.animation_tree", &AttachDefaultExtraIfMissingUVE<Scene::AnimationGraphComponentUVE>},
};

// Unknown ids are ignored, not an error: settings written by a newer editor still load, and only
// their known extras apply.
void AttachDefaultExtraComponentsUVE(Scene::IEntityManagerUVE& entityManager, const Scene::EntityUVE entity,
                                     const Config::SettingStringListUVE& componentIds) {
    for (const std::string& id : componentIds) {
        for (const DefaultExtraAttachmentUVE& attachment : kDefaultExtraAttachmentsUVE) {
            if (attachment.componentId == id) {
                attachment.attach(entityManager, entity);
                break;
            }
        }
    }
}

void CaptureTypeChangeOwnedUVE(Scene::IEntityManagerUVE& entityManager, const Scene::EntityUVE entity,
                               const Scene::Objects::SceneObjectKindUVE kind,
                               std::vector<SceneObjectTypeChangeValueUVE>& out, const bool remove) {
    TypeChangeCaptureVisitorUVE visitor{entityManager, entity, out, remove};
    static_cast<void>(VisitSceneObjectTypeChangeOwnedUVE(kind, visitor));
}

// Writes recorded values back: overwriting what is there, attaching what is not unless
// `onlyPresent` (the carried-values pass must not resurrect components the target kind dropped).
void RestoreTypeChangeValuesUVE(Scene::IEntityManagerUVE& entityManager, const Scene::EntityUVE entity,
                                const std::vector<SceneObjectTypeChangeValueUVE>& values,
                                const bool onlyPresent) {
    for (const SceneObjectTypeChangeValueUVE& value : values) {
        std::visit(
            [&entityManager, entity, onlyPresent](const auto& typedValue) {
                using Component = std::decay_t<decltype(typedValue)>;
                if (entityManager.HasComponentUVE<Component>(entity)) {
                    entityManager.GetComponentUVE<Component>(entity) = typedValue;
                } else if (!onlyPresent) {
                    entityManager.AddComponentUVE<Component>(entity, typedValue);
                }
            },
            value);
    }
}

// Puts `kind`'s owned components on: the same Apply path creation uses, with fresh authored
// defaults. No shell, no naming, no retag - the caller owns those. Carried values go over the top
// afterwards, so this stays a plain creation-shaped pass.
[[nodiscard]] bool ApplyTypeChangeKindUVE(Scene::IEntityManagerUVE& entityManager,
                                         const Scene::EntityUVE entity,
                                         const Scene::Objects::SceneObjectKindUVE kind) {
    switch (kind) {
        case Scene::Objects::SceneObjectKindUVE::Object3D:
            Scene::ApplyObject3DObjectDefinitionUVE(entityManager, entity, Scene::Object3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::Area3D:
            Scene::ApplyArea3DObjectDefinitionUVE(entityManager, entity, Scene::Area3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::RayCast3D:
            entityManager.AddComponentUVE<Scene::RayCast3DComponentUVE>(entity, Scene::RayCast3DComponentUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::Static3D:
            Scene::ApplyStatic3DObjectDefinitionUVE(entityManager, entity, Scene::Static3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::Kinematic3D:
            Scene::ApplyKinematic3DObjectDefinitionUVE(entityManager, entity, Scene::Kinematic3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::NavMeshVolume3D:
            entityManager.AddComponentUVE<Scene::NavMeshVolume3DComponentUVE>(
                entity, Scene::NavMeshVolume3DComponentUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::NavSeeker3D:
            entityManager.AddComponentUVE<Scene::NavSeeker3DComponentUVE>(entity, Scene::NavSeeker3DComponentUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::Skeleton3D:
            Scene::ApplySkeleton3DObjectDefinitionUVE(entityManager, entity, Scene::Skeleton3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::BoneAttachment3D:
            entityManager.AddComponentUVE<Scene::BoneAttachment3DComponentUVE>(
                entity, Scene::BoneAttachment3DComponentUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::SpringArm3D:
            Scene::ApplySpringArm3DObjectDefinitionUVE(entityManager, entity, Scene::SpringArm3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::Marker3D:
            entityManager.AddComponentUVE<Scene::Marker3DComponentUVE>(entity, Scene::Marker3DComponentUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::Hitbox3D:
            entityManager.AddComponentUVE<Scene::Hitbox3DComponentUVE>(entity, Scene::Hitbox3DComponentUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::Hurtbox3D:
            entityManager.AddComponentUVE<Scene::Hurtbox3DComponentUVE>(entity, Scene::Hurtbox3DComponentUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::Projectile3D:
            entityManager.AddComponentUVE<Scene::Projectile3DComponentUVE>(entity, Scene::Projectile3DComponentUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::InteractionArea3D:
            entityManager.AddComponentUVE<Scene::InteractionArea3DComponentUVE>(
                entity, Scene::InteractionArea3DComponentUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::WorldEnvironment3D:
            Scene::ApplyWorldEnvironmentObjectDefinitionUVE(
                entityManager, entity, Scene::WorldEnvironmentObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::ReflectionProbe3D:
            Scene::ApplyReflectionProbe3DObjectDefinitionUVE(
                entityManager, entity, Scene::ReflectionProbe3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::Decal3D:
            Scene::ApplyDecal3DObjectDefinitionUVE(entityManager, entity, Scene::Decal3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::LODGroup3D:
            Scene::ApplyLodGroup3DObjectDefinitionUVE(entityManager, entity, Scene::LodGroup3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::Occluder3D:
            Scene::ApplyOccluder3DObjectDefinitionUVE(entityManager, entity, Scene::Occluder3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::VisibilityRegion3D:
            Scene::ApplyVisibilityRegion3DObjectDefinitionUVE(
                entityManager, entity, Scene::VisibilityRegion3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::SpawnPoint3D:
            entityManager.AddComponentUVE<Scene::SpawnPoint3DComponentUVE>(entity, Scene::SpawnPoint3DComponentUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::LevelStreamer3D:
            entityManager.AddComponentUVE<Scene::LevelStreamer3DComponentUVE>(
                entity, Scene::LevelStreamer3DComponentUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::WorldPartition3D:
            Scene::ApplyWorldPartition3DObjectDefinitionUVE(
                entityManager, entity, Scene::WorldPartition3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::AnimationGraph:
            Scene::ApplyAnimationGraphObjectDefinitionUVE(
                entityManager, entity, Scene::AnimationGraphObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::AnimationSequencer:
            Scene::ApplyAnimationSequencerObjectDefinitionUVE(
                entityManager, entity, Scene::AnimationSequencerObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::Character3D:
            Scene::ApplyCharacter3DObjectDefinitionUVE(entityManager, entity, Scene::Character3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::Camera3D:
            Scene::ApplyCamera3DObjectDefinitionUVE(entityManager, entity, Scene::Camera3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::MeshInstance3D:
            Scene::ApplyMeshInstance3DObjectDefinitionUVE(
                entityManager, entity, Scene::MeshInstance3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::BoxMesh3D:
            Scene::ApplyBoxMesh3DObjectDefinitionUVE(entityManager, entity, Scene::BoxMesh3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::SphereMesh3D:
            Scene::ApplySphereMesh3DObjectDefinitionUVE(
                entityManager, entity, Scene::SphereMesh3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::PlaneMesh3D:
            Scene::ApplyPlaneMesh3DObjectDefinitionUVE(entityManager, entity, Scene::PlaneMesh3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::Light3D:
            Scene::ApplyLight3DObjectDefinitionUVE(entityManager, entity, Scene::Light3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::Collider3D:
            Scene::ApplyCollider3DObjectDefinitionUVE(entityManager, entity, Scene::Collider3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::Rigid3D:
            Scene::ApplyRigid3DObjectDefinitionUVE(entityManager, entity, Scene::Rigid3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::AudioSource3D:
            Scene::ApplyAudioSource3DObjectDefinitionUVE(
                entityManager, entity, Scene::AudioSource3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::ParticleEmitter3D:
            Scene::ApplyParticleEmitter3DObjectDefinitionUVE(
                entityManager, entity, Scene::ParticleEmitter3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::Script:
            Scene::ApplyScriptObjectDefinitionUVE(entityManager, entity, Scene::ScriptObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::Canvas:
            Scene::ApplyCanvasObjectDefinitionUVE(entityManager, entity, Scene::CanvasObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::UIText:
            Scene::ApplyUITextObjectDefinitionUVE(entityManager, entity, Scene::UITextObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::UIImage:
            Scene::ApplyUIImageObjectDefinitionUVE(entityManager, entity, Scene::UIImageObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::UIButton:
            Scene::ApplyUIButtonObjectDefinitionUVE(entityManager, entity, Scene::UIButtonObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::FogVolume3D:
            Scene::ApplyFogVolume3DObjectDefinitionUVE(entityManager, entity, Scene::FogVolume3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::DirectionalLight3D:
            Scene::ApplyDirectionalLight3DObjectDefinitionUVE(
                entityManager, entity, Scene::DirectionalLight3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::TwoBoneIK3D:
            Scene::ApplyTwoBoneIK3DObjectDefinitionUVE(entityManager, entity, Scene::TwoBoneIK3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::Player3D:
            Scene::ApplyPlayer3DObjectDefinitionUVE(entityManager, entity, Scene::Player3DObjectDefinitionUVE{});
            return true;
        case Scene::Objects::SceneObjectKindUVE::Object:
        case Scene::Objects::SceneObjectKindUVE::Viewport:
        case Scene::Objects::SceneObjectKindUVE::Folder:
            return false;
    }
    // Unreachable for a valid kind; keeps the function total for any raw enumerator value.
    return false;
}

// The document-structure boundary: the Object root, the level Viewport, and folders organise the
// scene rather than take part in it, so no object converts into or out of them.
[[nodiscard]] bool IsStructuralTypeChangeKindUVE(const Scene::Objects::SceneObjectKindUVE kind) noexcept {
    return kind == Scene::Objects::SceneObjectKindUVE::Object ||
           kind == Scene::Objects::SceneObjectKindUVE::Viewport ||
           kind == Scene::Objects::SceneObjectKindUVE::Folder;
}

// The kinds whose Apply runs the pure-Object baseline, which folds the Transform into the children
// and removes it: crossing to or from one is the only time a change touches the shell.
[[nodiscard]] bool IsTransformlessTypeChangeKindUVE(const Scene::Objects::SceneObjectKindUVE kind) noexcept {
    return kind == Scene::Objects::SceneObjectKindUVE::AnimationSequencer ||
           kind == Scene::Objects::SceneObjectKindUVE::AnimationGraph ||
           kind == Scene::Objects::SceneObjectKindUVE::WorldEnvironment3D;
}

[[nodiscard]] std::optional<Scene::PrimitiveMeshKindUVE> TypeChangePrimitiveTargetUVE(
    const Scene::Objects::SceneObjectKindUVE kind) noexcept {
    switch (kind) {
        case Scene::Objects::SceneObjectKindUVE::BoxMesh3D:
            return Scene::PrimitiveMeshKindUVE::Cube;
        case Scene::Objects::SceneObjectKindUVE::SphereMesh3D:
            return Scene::PrimitiveMeshKindUVE::UVSphere;
        case Scene::Objects::SceneObjectKindUVE::PlaneMesh3D:
            return Scene::PrimitiveMeshKindUVE::Plane;
        default:
            return std::nullopt;
    }
}

} // namespace

Scene::EntityUVE EditorUVE::CreateSceneObjectEntityInternalUVE(const Scene::Objects::SceneObjectKindUVE kind) {
    Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const auto createObjectWithComponent = [this, &entityManager, kind](auto component) {
        // These kinds carry a component but no ObjectDefinition of their own, so they have no
        // `defaultName` to route through. Going via EditorEntityKindUVE::Empty alone resolved every
        // one of them to Object3DObjectDefinitionUVE::defaultName - a freshly added RayCast3D, a
        // Marker3D and a LevelStreamer3D all appeared in the Outliner as "Object3D", "Object3D 2",
        // "Object3D 3", with nothing but their component list telling them apart. The registry's
        // displayName is already the kind's own name and unique per kind, so it is the name to use.
        // It still goes through MakeUniqueDocumentEntityNameUVE, matching the 29 kinds that DO have
        // a definition (CreateObjectDefinitionEntityInternalUVE does the same), so adding two of a
        // kind yields "RayCast3D" then "RayCast3D 2" rather than a duplicate name.
        const Scene::Objects::SceneObjectDescriptorUVE* const descriptor =
            Scene::Objects::FindSceneObjectDescriptorUVE(kind);
        std::optional<std::string> explicitName;
        if (descriptor != nullptr) {
            explicitName = MakeUniqueDocumentEntityNameUVE(descriptor->displayName);
        }
        Scene::EntityUVE created = CreateDocumentEntityInternalUVE(EditorEntityKindUVE::Empty, explicitName);
        if (created != Scene::kInvalidEntityUVE) {
            using Component = std::decay_t<decltype(component)>;
            entityManager.AddComponentUVE<Component>(created, std::move(component));
        }
        return created;
    };

    switch (kind) {
        // Every case creates from its own ObjectDefinition (Engine/Runtime/Objects/3D or
        // Objects/UI — one .h + .cpp per kind holds the recipe: components to attach,
        // authored defaults, default entity name). No object-kind-specific recipe is authored in
        // this switch anymore.
        case Scene::Objects::SceneObjectKindUVE::Object3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::Object3DObjectDefinitionUVE{},
                                                            Scene::ApplyObject3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::Camera3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::Camera3DObjectDefinitionUVE{},
                                                            Scene::ApplyCamera3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::MeshInstance3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::MeshInstance3DObjectDefinitionUVE{},
                                                            Scene::ApplyMeshInstance3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::BoxMesh3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::BoxMesh3DObjectDefinitionUVE{},
                                                            Scene::ApplyBoxMesh3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::SphereMesh3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::SphereMesh3DObjectDefinitionUVE{},
                                                            Scene::ApplySphereMesh3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::PlaneMesh3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::PlaneMesh3DObjectDefinitionUVE{},
                                                            Scene::ApplyPlaneMesh3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::Light3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::Light3DObjectDefinitionUVE{},
                                                            Scene::ApplyLight3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::Collider3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::Collider3DObjectDefinitionUVE{},
                                                            Scene::ApplyCollider3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::Character3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::Character3DObjectDefinitionUVE{},
                                                            Scene::ApplyCharacter3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::Player3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::Player3DObjectDefinitionUVE{},
                                                            Scene::ApplyPlayer3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::Rigid3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::Rigid3DObjectDefinitionUVE{},
                                                            Scene::ApplyRigid3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::AnimationSequencer:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::AnimationSequencerObjectDefinitionUVE{},
                                                            Scene::ApplyAnimationSequencerObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::AudioSource3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::AudioSource3DObjectDefinitionUVE{},
                                                            Scene::ApplyAudioSource3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::ParticleEmitter3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::ParticleEmitter3DObjectDefinitionUVE{},
                                                            Scene::ApplyParticleEmitter3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::Script:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::ScriptObjectDefinitionUVE{},
                                                            Scene::ApplyScriptObjectDefinitionUVE);
            break;
        // Canvas family — the four UI kinds promoted out of the Inspector-only world, so
        // UI authoring uses the same Add-Object entry point (definitions live in
        // Engine/Runtime/Objects/UI).
        case Scene::Objects::SceneObjectKindUVE::Canvas:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::CanvasObjectDefinitionUVE{},
                                                            Scene::ApplyCanvasObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::UIText:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::UITextObjectDefinitionUVE{},
                                                            Scene::ApplyUITextObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::UIImage:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::UIImageObjectDefinitionUVE{},
                                                            Scene::ApplyUIImageObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::UIButton:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::UIButtonObjectDefinitionUVE{},
                                                            Scene::ApplyUIButtonObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::Area3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::Area3DObjectDefinitionUVE{},
                                                            Scene::ApplyArea3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::RayCast3D:
            entity = createObjectWithComponent(Scene::RayCast3DComponentUVE{});
            break;
        case Scene::Objects::SceneObjectKindUVE::Static3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::Static3DObjectDefinitionUVE{},
                                                            Scene::ApplyStatic3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::Kinematic3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::Kinematic3DObjectDefinitionUVE{},
                                                            Scene::ApplyKinematic3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::NavMeshVolume3D:
            entity = createObjectWithComponent(Scene::NavMeshVolume3DComponentUVE{});
            break;
        case Scene::Objects::SceneObjectKindUVE::NavSeeker3D:
            entity = createObjectWithComponent(Scene::NavSeeker3DComponentUVE{});
            break;
        case Scene::Objects::SceneObjectKindUVE::Skeleton3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::Skeleton3DObjectDefinitionUVE{},
                                                            Scene::ApplySkeleton3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::BoneAttachment3D:
            entity = createObjectWithComponent(Scene::BoneAttachment3DComponentUVE{});
            break;
        case Scene::Objects::SceneObjectKindUVE::TwoBoneIK3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::TwoBoneIK3DObjectDefinitionUVE{},
                                                            Scene::ApplyTwoBoneIK3DObjectDefinitionUVE);
            break;
        // SpringArm3D carries its own component like its neighbours, but its recipe
        // (Object3D baseline plus seeding currentLength to the authored armLength the same way the
        // deserializer seeds it) is a definition's worth of behaviour, so it reads like the rest.
        case Scene::Objects::SceneObjectKindUVE::SpringArm3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::SpringArm3DObjectDefinitionUVE{},
                                                            Scene::ApplySpringArm3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::Marker3D:
            entity = createObjectWithComponent(Scene::Marker3DComponentUVE{});
            break;
        case Scene::Objects::SceneObjectKindUVE::Hitbox3D:
            entity = createObjectWithComponent(Scene::Hitbox3DComponentUVE{});
            break;
        case Scene::Objects::SceneObjectKindUVE::Hurtbox3D:
            entity = createObjectWithComponent(Scene::Hurtbox3DComponentUVE{});
            break;
        case Scene::Objects::SceneObjectKindUVE::Projectile3D:
            entity = createObjectWithComponent(Scene::Projectile3DComponentUVE{});
            break;
        case Scene::Objects::SceneObjectKindUVE::InteractionArea3D:
            entity = createObjectWithComponent(Scene::InteractionArea3DComponentUVE{});
            break;
        case Scene::Objects::SceneObjectKindUVE::WorldEnvironment3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::WorldEnvironmentObjectDefinitionUVE{},
                                                            Scene::ApplyWorldEnvironmentObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::DirectionalLight3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::DirectionalLight3DObjectDefinitionUVE{},
                                                            Scene::ApplyDirectionalLight3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::ReflectionProbe3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::ReflectionProbe3DObjectDefinitionUVE{},
                                                            Scene::ApplyReflectionProbe3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::Decal3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::Decal3DObjectDefinitionUVE{},
                                                            Scene::ApplyDecal3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::FogVolume3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::FogVolume3DObjectDefinitionUVE{},
                                                            Scene::ApplyFogVolume3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::LODGroup3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::LodGroup3DObjectDefinitionUVE{},
                                                            Scene::ApplyLodGroup3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::Occluder3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::Occluder3DObjectDefinitionUVE{},
                                                            Scene::ApplyOccluder3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::VisibilityRegion3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::VisibilityRegion3DObjectDefinitionUVE{},
                                                            Scene::ApplyVisibilityRegion3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::SpawnPoint3D:
            entity = createObjectWithComponent(Scene::SpawnPoint3DComponentUVE{});
            break;
        case Scene::Objects::SceneObjectKindUVE::LevelStreamer3D:
            entity = createObjectWithComponent(Scene::LevelStreamer3DComponentUVE{});
            break;
        case Scene::Objects::SceneObjectKindUVE::WorldPartition3D:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::WorldPartition3DObjectDefinitionUVE{},
                                                            Scene::ApplyWorldPartition3DObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::AnimationGraph:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::AnimationGraphObjectDefinitionUVE{},
                                                            Scene::ApplyAnimationGraphObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::Folder:
            entity = CreateObjectDefinitionEntityInternalUVE(Scene::FolderObjectDefinitionUVE{},
                                                            Scene::ApplyFolderObjectDefinitionUVE);
            break;
        case Scene::Objects::SceneObjectKindUVE::Viewport:
            // The level's Viewport is created by the document layout (EnsureDocumentLayoutUVE).
            return Scene::kInvalidEntityUVE;
        case Scene::Objects::SceneObjectKindUVE::Object:
            // The Object is created by the document lifecycle
            // (EnsureDocumentObjectUVE), never through the library path.
            return Scene::kInvalidEntityUVE;
    }

    if (entity != Scene::kInvalidEntityUVE) {
        // Typed at once, so snapshots taken of it (undo, prefab save) bring the type back with it.
        Scene::SetSceneObjectKindUVE(entityManager, entity, kind);
    }
    return entity;
}

Scene::EntityUVE EditorUVE::CreateDocumentSceneObjectUVE(
    const Scene::Objects::SceneObjectKindUVE kind) {
    if (!IsAuthoringCommandAllowedUVE() || m_selectedEntities.size() > 1U) {
        return Scene::kInvalidEntityUVE;
    }
    const Scene::Objects::SceneObjectDescriptorUVE* descriptor =
        Scene::Objects::FindSceneObjectDescriptorUVE(kind);
    if (descriptor == nullptr || !descriptor->libraryCreatable) {
        return Scene::kInvalidEntityUVE;
    }

    // The level has one DirectionalLight3D and one WorldEnvironment at its top.
    if (IsOutlinerLayoutActiveUVE() && IsTopLevelSingletonKindUVE(kind) &&
        FindTopLevelObjectUVE(kind) != Scene::kInvalidEntityUVE) {
        return Scene::kInvalidEntityUVE;
    }

    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const Scene::EntityUVE entity = CreateSceneObjectEntityInternalUVE(kind);
    if (entity == Scene::kInvalidEntityUVE) {
        return Scene::kInvalidEntityUVE;
    }

    // New objects join the hierarchy instead of becoming document roots, and are placed before the
    // snapshot below is taken, so undo and redo restore the same place.
    const Scene::EntityUVE parentObject = ResolveNewObjectParentForUVE(kind);
    if (parentObject != Scene::kInvalidEntityUVE) {
        m_services->GetSceneGraphUVE().SetParentUVE(entityManager, entity, parentObject);
        InvalidateHierarchyFilterCacheUVE();
    }
    PlaceNewDocumentObjectUVE(entity);

    // The author's per-kind extras ride along with the recipe, before the snapshot below is taken,
    // so undo removes them with the object and redo restores them. Only the document path passes
    // here: content-catalogue transient builds go through CreateSceneObjectEntityInternalUVE alone.
    if (const auto extras = m_objectDefaultExtras.find(std::string(Scene::Objects::GetSceneObjectTypeIdUVE(kind)));
        extras != m_objectDefaultExtras.end()) {
        AttachDefaultExtraComponentsUVE(entityManager, entity, extras->second);
    }

    const std::optional<Scene::SceneSnapshotUVE> snapshot = CaptureSubtreeUVE(entity);
    if (!snapshot.has_value()) {
        DestroyDocumentSubtreeUVE(entity);
        RestoreSelectionUVE(selectionBefore);
        m_sceneDirty = dirtyBefore;
        return Scene::kInvalidEntityUVE;
    }

    SelectEntityUVE(entity);
    m_sceneDirty = true;
    RecordHistoryUVE(SceneObjectCreationHistoryEntryUVE{
        *snapshot, kind, entity, selectionBefore, CaptureSelectionSnapshotUVE(), dirtyBefore, true,
        parentObject});
    return entity;
}

bool EditorUVE::CanChangeDocumentSceneObjectKindUVE(const Scene::EntityUVE entity,
                                                    const Scene::Objects::SceneObjectKindUVE kind) {
    if (!IsAuthoringCommandAllowedUVE() || !IsDocumentEntityUVE(entity) || IsEntityLockedUVE(entity) ||
        IsStructuralRootUVE(entity)) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const Scene::Objects::SceneObjectKindUVE source =
        Scene::ResolveSceneObjectKindUVE(entityManager, entity);
    if (source == kind || IsStructuralTypeChangeKindUVE(source) || IsStructuralTypeChangeKindUVE(kind)) {
        return false;
    }
    const Scene::Objects::SceneObjectDescriptorUVE* const descriptor =
        Scene::Objects::FindSceneObjectDescriptorUVE(kind);
    if (descriptor == nullptr || !descriptor->libraryCreatable) {
        return false;
    }
    // Same one-sun-one-sky rule as creation: becoming the top-level singleton is only possible
    // while the level does not have one yet.
    if (IsOutlinerLayoutActiveUVE() && IsTopLevelSingletonKindUVE(kind) &&
        FindTopLevelObjectUVE(kind) != Scene::kInvalidEntityUVE) {
        return false;
    }
    // A component the target owns that the source does not must be absent: if the author attached
    // one by hand, the change would have to steal it or overwrite it, so the target is refused and
    // simply not offered. Components both kinds own round-trip through the entry with their values.
    std::vector<std::type_index> sourceOwned;
    std::vector<std::type_index> targetOwned;
    if (!CollectTypeChangeOwnedTypeIndicesUVE(source, sourceOwned) ||
        !CollectTypeChangeOwnedTypeIndicesUVE(kind, targetOwned)) {
        return false;
    }
    const std::vector<std::type_index> present = entityManager.GetComponentTypesUVE(entity);
    // Default extras the source kind bestowed are carried, not refused: the change captures them
    // with their values like source-owned components (see the conversion shadow in
    // ChangeDocumentSceneObjectKindUVE). Provenance is read from the source kind's current
    // defaults, so an extra removed from the setting since creation refuses like a hand-attached
    // component - conservatively, since the change can no longer tell them apart.
    const Config::SettingStringListUVE sourceExtras =
        GetObjectDefaultExtrasUVE(Scene::Objects::GetSceneObjectTypeIdUVE(source));
    for (const std::type_index targetType : targetOwned) {
        const bool shared =
            std::find(sourceOwned.begin(), sourceOwned.end(), targetType) != sourceOwned.end();
        if (shared) {
            continue;
        }
        if (std::find(present.begin(), present.end(), targetType) == present.end()) {
            continue;
        }
        const Core::TypeMetadataEntryUVE* const metadata = Scene::FindSceneComponentMetadataUVE(targetType);
        if (metadata != nullptr &&
            std::find(sourceExtras.begin(), sourceExtras.end(), metadata->typeId) != sourceExtras.end()) {
            continue;
        }
        return false;
    }
    return true;
}

std::vector<Scene::Objects::SceneObjectKindUVE> EditorUVE::GetSceneObjectKindChangeTargetsUVE(
    const Scene::EntityUVE entity) {
    std::vector<Scene::Objects::SceneObjectKindUVE> targets;
    if (!IsAuthoringCommandAllowedUVE() || !IsDocumentEntityUVE(entity) || IsEntityLockedUVE(entity) ||
        IsStructuralRootUVE(entity)) {
        return targets;
    }
    for (const Scene::Objects::SceneObjectDescriptorUVE& descriptor :
         Scene::Objects::GetSceneObjectDescriptorsUVE()) {
        if (CanChangeDocumentSceneObjectKindUVE(entity, descriptor.kind)) {
            targets.push_back(descriptor.kind);
        }
    }
    return targets;
}

std::optional<EditorUVE::ObjectDefaultExtrasPreviewUVE> EditorUVE::ComputeObjectDefaultExtrasForEntityUVE(
    const Scene::EntityUVE entity) {
    if (!IsDocumentEntityUVE(entity)) {
        return std::nullopt;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const Scene::Objects::SceneObjectKindUVE kind =
        Scene::ResolveSceneObjectKindUVE(entityManager, entity);
    const Scene::Objects::SceneObjectDescriptorUVE* const descriptor =
        Scene::Objects::FindSceneObjectDescriptorUVE(kind);
    if (descriptor == nullptr || !descriptor->libraryCreatable) {
        return std::nullopt;
    }
    // The recipe, exactly as conversion knows it: the kind's owned components plus the shell every
    // document object is born with (CreateDocumentEntityShellInternalUVE: Transform and Name, with
    // WorldTransform and Hierarchy arriving through the scene graph) and the structural tags. What
    // remains is the author's own layer - extras candidates and palette outsiders alike.
    std::vector<std::type_index> owned;
    if (!CollectTypeChangeOwnedTypeIndicesUVE(kind, owned)) {
        return std::nullopt;
    }
    const std::vector<std::type_index> shell = {
        std::type_index(typeid(Scene::NameComponentUVE)),
        std::type_index(typeid(Scene::TransformComponentUVE)),
        std::type_index(typeid(Scene::WorldTransformComponentUVE)),
        std::type_index(typeid(Scene::HierarchyComponentUVE)),
        std::type_index(typeid(Scene::SceneObjectTypeComponentUVE)),
        std::type_index(typeid(Scene::FolderComponentUVE)),
    };
    ObjectDefaultExtrasPreviewUVE preview;
    preview.kind = kind;
    for (const std::type_index present : entityManager.GetComponentTypesUVE(entity)) {
        if (std::find(owned.begin(), owned.end(), present) != owned.end() ||
            std::find(shell.begin(), shell.end(), present) != shell.end()) {
            continue;
        }
        const Core::TypeMetadataEntryUVE* const metadata = Scene::FindSceneComponentMetadataUVE(present);
        if (metadata == nullptr) {
            ++preview.skippedUnnamed;
            continue;
        }
        if (EditorSettingIdUVE::IsObjectDefaultExtraComponentUVE(metadata->typeId)) {
            preview.extras.push_back(metadata->typeId);
        } else {
            preview.skipped.push_back(metadata->typeId);
        }
    }
    // Palette order, not entity order: saved lists read the same no matter how the node grew.
    const auto paletteIndex = [](const std::string& id) {
        const auto& palette = EditorSettingIdUVE::kObjectDefaultExtrasPaletteUVE;
        return std::distance(palette.begin(), std::find(palette.begin(), palette.end(), id));
    };
    std::sort(preview.extras.begin(), preview.extras.end(),
              [&paletteIndex](const std::string& a, const std::string& b) {
                  return paletteIndex(a) < paletteIndex(b);
              });
    std::sort(preview.skipped.begin(), preview.skipped.end());
    return preview;
}

bool EditorUVE::ChangeDocumentSceneObjectKindUVE(const Scene::EntityUVE entity,
                                                 const Scene::Objects::SceneObjectKindUVE kind) {
    if (!CanChangeDocumentSceneObjectKindUVE(entity, kind)) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const Scene::Objects::SceneObjectKindUVE source =
        Scene::ResolveSceneObjectKindUVE(entityManager, entity);
    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;

    SceneObjectTypeChangeHistoryEntryUVE entry;
    entry.entity = entity;
    entry.kindBefore = source;
    entry.kindAfter = kind;
    entry.hadTypeComponentBefore =
        entityManager.HasComponentUVE<Scene::SceneObjectTypeComponentUVE>(entity);
    entry.selectionBefore = selectionBefore;
    entry.dirtyBefore = dirtyBefore;
    // The only shell component a change can move: the pure-Object kinds carry no Transform, so a
    // change across that boundary backs the author's up for the way back. WorldTransform is
    // derived and recomputes from it.
    if (entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity)) {
        entry.transformBefore = entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
    }
    CaptureTypeChangeOwnedUVE(entityManager, entity, source, entry.removed, true);
    // The conversion shadow: extras the TARGET kind owns are still on the entity (nothing removed
    // them), and the apply below would re-add them over the live instances. Capturing them into
    // `removed` first backs their authored values up for the carried-values pass and for undo,
    // exactly like source-owned components. Absent is a no-op, so kinds without extras behave as
    // before.
    CaptureTypeChangeOwnedUVE(entityManager, entity, kind, entry.removed, true);
    if (!ApplyTypeChangeKindUVE(entityManager, entity, kind)) {
        RestoreTypeChangeValuesUVE(entityManager, entity, entry.removed, false);
        Scene::SetSceneObjectKindUVE(entityManager, entity, source);
        return false;
    }
    // Shared components keep the author's values over the fresh defaults; components the target
    // dropped stay dropped.
    RestoreTypeChangeValuesUVE(entityManager, entity, entry.removed, true);
    // A primitive that becomes another primitive keeps its size and color but takes the new shape:
    // Box, Sphere and Plane share one component with different kinds.
    if (const std::optional<Scene::PrimitiveMeshKindUVE> primitive = TypeChangePrimitiveTargetUVE(kind);
        primitive.has_value() &&
        entityManager.HasComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity)) {
        entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity).kind = *primitive;
    }
    CaptureTypeChangeOwnedUVE(entityManager, entity, kind, entry.added, false);
    Scene::SetSceneObjectKindUVE(entityManager, entity, kind);

    InvalidateHierarchyFilterCacheUVE();
    m_sceneDirty = true;
    entry.selectionAfter = CaptureSelectionSnapshotUVE();
    entry.dirtyAfter = true;
    RecordHistoryUVE(std::move(entry));
    return true;
}

bool EditorUVE::ApplySceneObjectTypeChangeUVE(SceneObjectTypeChangeHistoryEntryUVE& entry,
                                              const bool forward) {
    if (!IsDocumentEntityUVE(entry.entity)) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const Scene::Objects::SceneObjectKindUVE toKind = forward ? entry.kindAfter : entry.kindBefore;
    const std::vector<SceneObjectTypeChangeValueUVE>& off = forward ? entry.removed : entry.added;
    const std::vector<SceneObjectTypeChangeValueUVE>& on = forward ? entry.added : entry.removed;
    // Exact replay of recorded values, never fresh defaults: undo and redo restore what the change
    // saw, whatever the definitions say today.
    for (const SceneObjectTypeChangeValueUVE& value : off) {
        std::visit(
            [&entityManager, entity = entry.entity](const auto& typedValue) {
                using Component = std::decay_t<decltype(typedValue)>;
                if (entityManager.HasComponentUVE<Component>(entity)) {
                    entityManager.RemoveComponentUVE<Component>(entity);
                }
            },
            value);
    }
    RestoreTypeChangeValuesUVE(entityManager, entry.entity, on, false);
    // The transform boundary, replayed: the pure-Object kinds hold no Transform, every other kind
    // does. The fallbacks below only name a missing Name component, which the shell never drops,
    // so the author's name always survives this.
    std::string_view nameFallback = "Object";
    if (entityManager.HasComponentUVE<Scene::NameComponentUVE>(entry.entity)) {
        nameFallback = entityManager.GetComponentUVE<Scene::NameComponentUVE>(entry.entity).name;
    }
    if (IsTransformlessTypeChangeKindUVE(toKind)) {
        Scene::EnsureObjectBaselineUVE(entityManager, entry.entity, nameFallback);
    } else {
        Scene::EnsureObject3DBaselineUVE(entityManager, entry.entity, nameFallback);
    }
    // The author's Transform back over the fresh one the baseline ensured - but only when there is
    // both a backup and a Transform to write it to, so a replay landing on a pure-Object kind
    // keeps the backup for the way back instead.
    if (entry.transformBefore.has_value() &&
        entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entry.entity)) {
        if (const Scene::TransformComponentUVE* const backup =
                std::get_if<Scene::TransformComponentUVE>(&*entry.transformBefore);
            backup != nullptr) {
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entry.entity) = *backup;
        }
    }
    if (!forward && !entry.hadTypeComponentBefore) {
        if (entityManager.HasComponentUVE<Scene::SceneObjectTypeComponentUVE>(entry.entity)) {
            entityManager.RemoveComponentUVE<Scene::SceneObjectTypeComponentUVE>(entry.entity);
        }
    } else {
        Scene::SetSceneObjectKindUVE(entityManager, entry.entity, toKind);
    }
    return true;
}

Scene::EntityUVE EditorUVE::DuplicateSelectedEntityUVE() {
    if (!IsLifecycleCommandAllowedUVE() || !IsDocumentEntityUVE(m_selectedEntity) ||
        IsStructuralRootUVE(m_selectedEntity)) {
        return Scene::kInvalidEntityUVE;
    }

    const Scene::EntityUVE source = m_selectedEntity;
    const std::optional<Scene::SceneSnapshotUVE> snapshot = CaptureSubtreeUVE(source);
    if (!snapshot.has_value()) {
        return Scene::kInvalidEntityUVE;
    }

    Scene::EntityUVE originalParent = Scene::kInvalidEntityUVE;
    if (!TryGetDocumentParentUVE(source, originalParent)) {
        return Scene::kInvalidEntityUVE;
    }

    const Scene::EntityUVE duplicate = RestoreSubtreeUnderParentUVE(*snapshot, originalParent);
    if (duplicate == Scene::kInvalidEntityUVE) {
        return Scene::kInvalidEntityUVE;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    // The copy goes right below the object it copies, not to the end of the list.
    Scene::ISceneGraphUVE& sceneGraph = m_services->GetSceneGraphUVE();
    const std::size_t duplicateIndex = sceneGraph.GetSiblingIndexUVE(entityManager, source).value_or(0U) + 1U;
    static_cast<void>(sceneGraph.SetSiblingIndexUVE(entityManager, duplicate, duplicateIndex));
    std::optional<std::string> duplicateRootName;
    if (entityManager.HasComponentUVE<Scene::NameComponentUVE>(source)) {
        const std::string& sourceName = entityManager.GetComponentUVE<Scene::NameComponentUVE>(source).name;
        if (IsEntityNameValidUVE(sourceName)) {
            duplicateRootName = MakeUniqueDocumentEntityNameUVE(sourceName);
            if (!ApplyEntityNameStateUVE(duplicate, duplicateRootName)) {
                DestroyDocumentSubtreeUVE(duplicate);
                return Scene::kInvalidEntityUVE;
            }
        }
    }

    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    SelectEntityUVE(duplicate);
    m_sceneDirty = true;
    InvalidateHierarchyFilterCacheUVE();
    RecordHistoryUVE(DuplicationHistoryEntryUVE{
        std::move(*snapshot), originalParent, duplicate, std::move(duplicateRootName), selectionBefore,
        CaptureSelectionSnapshotUVE(), dirtyBefore, true, duplicateIndex});
    return duplicate;
}

bool EditorUVE::DeleteSelectedEntityUVE() {
    if (!IsLifecycleCommandAllowedUVE() || !IsDocumentEntityUVE(m_selectedEntity) ||
        IsStructuralRootUVE(m_selectedEntity)) {
        return false;
    }

    const Scene::EntityUVE target = m_selectedEntity;
    const std::optional<Scene::SceneSnapshotUVE> snapshot = CaptureSubtreeUVE(target);
    if (!snapshot.has_value()) {
        return false;
    }

    Scene::EntityUVE originalParent = Scene::kInvalidEntityUVE;
    if (!TryGetDocumentParentUVE(target, originalParent)) {
        return false;
    }

    const std::size_t siblingIndex =
        m_services->GetSceneGraphUVE().GetSiblingIndexUVE(m_services->GetEntityManagerUVE(), target).value_or(0U);
    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    DestroyDocumentSubtreeUVE(target);
    const Scene::EntityUVE selectionAfter = IsDocumentEntityUVE(originalParent)
                                                ? originalParent
                                                : Scene::kInvalidEntityUVE;
    RestoreSelectionUVE(EditorSelectionSnapshotUVE{
        selectionAfter == Scene::kInvalidEntityUVE ? std::vector<Scene::EntityUVE>{}
                                                    : std::vector<Scene::EntityUVE>{selectionAfter},
        selectionAfter});
    m_sceneDirty = true;
    InvalidateHierarchyFilterCacheUVE();
    RecordHistoryUVE(DeletionHistoryEntryUVE{std::move(*snapshot), originalParent, target, selectionBefore,
                                             CaptureSelectionSnapshotUVE(), dirtyBefore, true, siblingIndex});
    return true;
}

std::optional<EditorUVE::EntityClipboardUVE> EditorUVE::CaptureEntityClipboardUVE() {
    // The lifecycle gate is the authoring state plus a single live document selection; a structural
    // anchor is not an object anyone authored, so it is not something to copy.
    if (!IsLifecycleCommandAllowedUVE() || IsStructuralRootUVE(m_selectedEntity)) {
        return std::nullopt;
    }

    const Scene::EntityUVE source = m_selectedEntity;
    std::optional<Scene::SceneSnapshotUVE> snapshot = CaptureSubtreeUVE(source);
    if (!snapshot.has_value()) {
        return std::nullopt;
    }

    EntityClipboardUVE clipboard;
    clipboard.snapshot = std::move(*snapshot);
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (entityManager.HasComponentUVE<Scene::NameComponentUVE>(source)) {
        clipboard.rootName = entityManager.GetComponentUVE<Scene::NameComponentUVE>(source).name;
    }
    clipboard.entityCount = CountDocumentSubtreeEntitiesUpToConfirmationThresholdUVE(source);
    return clipboard;
}

bool EditorUVE::CopySelectedEntityUVE() {
    std::optional<EntityClipboardUVE> captured = CaptureEntityClipboardUVE();
    if (!captured.has_value()) {
        return false;
    }
    m_entityClipboard = std::move(*captured);
    return true;
}

bool EditorUVE::CutSelectedEntityUVE() {
    // Capture first, adopt only after the delete landed: a cut that failed to remove the object
    // must not leave a copy of it on the clipboard, where the next Paste would duplicate it.
    std::optional<EntityClipboardUVE> captured = CaptureEntityClipboardUVE();
    if (!captured.has_value()) {
        return false;
    }
    if (!DeleteSelectedEntityUVE()) {
        return false;
    }
    m_entityClipboard = std::move(*captured);
    return true;
}

Scene::EntityUVE EditorUVE::PasteEntityUVE() {
    if (!IsAuthoringCommandAllowedUVE() || !m_entityClipboard.has_value()) {
        return Scene::kInvalidEntityUVE;
    }

    // Where a new object goes: into the selected folder, under the selection when it lives inside
    // one, otherwise the folder new objects go to - so a Paste never lands somewhere a new object
    // could not.
    const Scene::EntityUVE parent = ResolveNewObjectParentUVE();
    const Scene::EntityUVE pasted = RestoreSubtreeUnderParentUVE(m_entityClipboard->snapshot, parent);
    if (pasted == Scene::kInvalidEntityUVE) {
        return Scene::kInvalidEntityUVE;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    std::optional<std::string> pastedRootName;
    if (entityManager.HasComponentUVE<Scene::NameComponentUVE>(pasted) &&
        IsEntityNameValidUVE(m_entityClipboard->rootName)) {
        // The restored root still carries the name it was captured with; it must not count itself
        // as a clash, or a Paste after a Cut (which freed the name) would come back as "Lamp 2".
        pastedRootName = MakeUniqueDocumentEntityNameUVE(m_entityClipboard->rootName, pasted);
        if (!ApplyEntityNameStateUVE(pasted, *pastedRootName)) {
            DestroyDocumentSubtreeUVE(pasted);
            return Scene::kInvalidEntityUVE;
        }
    }

    const std::size_t siblingIndex =
        m_services->GetSceneGraphUVE().GetSiblingIndexUVE(entityManager, pasted).value_or(0U);
    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    SelectEntityUVE(pasted);
    m_sceneDirty = true;
    InvalidateHierarchyFilterCacheUVE();
    // A paste is an insertion of a captured subtree, exactly what a duplicate is; Undo removes the
    // inserted root again, Redo puts it back under the same parent with the same name.
    RecordHistoryUVE(DuplicationHistoryEntryUVE{m_entityClipboard->snapshot, parent, pasted,
                                                std::move(pastedRootName), selectionBefore,
                                                CaptureSelectionSnapshotUVE(), dirtyBefore, true, siblingIndex});
    return pasted;
}

bool EditorUVE::HasEntityClipboardUVE() const noexcept {
    return m_entityClipboard.has_value();
}

std::string EditorUVE::GetEntityClipboardLabelUVE() const {
    if (!m_entityClipboard.has_value()) {
        return {};
    }
    const std::size_t count = m_entityClipboard->entityCount;
    if (count == 0U) {
        return "The copied objects";
    }
    if (count >= kHierarchyLargeSubtreeReparentThresholdUVE) {
        return std::to_string(kHierarchyLargeSubtreeReparentThresholdUVE) + "+ objects";
    }
    if (count == 1U) {
        return "1 object";
    }
    return std::to_string(count) + " objects";
}

std::string EditorUVE::GetEntityNodePathUVE(const Scene::EntityUVE entity) const {
    const std::vector<Scene::EntityUVE> ancestry = GetDocumentAncestryUVE(entity);
    std::string path;
    for (const Scene::EntityUVE ancestor : ancestry) {
        if (!path.empty()) {
            path.push_back('/');
        }
        path.append(GetEntityDisplayLabelUVE(ancestor));
    }
    return path;
}

bool EditorUVE::CopyEntityNodePathUVE(const Scene::EntityUVE entity) {
    const std::string path = GetEntityNodePathUVE(entity);
    if (path.empty()) {
        return false;
    }
    SetClipboardTextUVE(path);
    return true;
}

bool EditorUVE::CopyEntityIdentifierUVE(const Scene::EntityUVE entity) {
    if (!IsDocumentEntityUVE(entity)) {
        return false;
    }
    SetClipboardTextUVE(std::to_string(entity.index) + ":" + std::to_string(entity.generation));
    return true;
}

const std::string& EditorUVE::GetClipboardTextUVE() const noexcept {
    return m_clipboardText;
}

void EditorUVE::SetClipboardTextUVE(std::string text) {
    m_clipboardText = std::move(text);
    // The host owns the real clipboard through ImGui; a headless editor and a test keep only the
    // string above. ImGui's own clipboard needs a context, so this is guarded rather than assumed.
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui::SetClipboardText(m_clipboardText.c_str());
    }
}

bool EditorUVE::ReparentSelectedEntityUVE(const Scene::EntityUVE newParent) {
    return ReparentDocumentEntityUVE(m_selectedEntity, newParent);
}

bool EditorUVE::ComputeKeepWorldLocalTransformUVE(const Scene::EntityUVE entity,
                                                    const Scene::EntityUVE newParent,
                                                    Scene::TransformComponentUVE& outTransform) const {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!IsDocumentEntityUVE(entity) || !entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
        return false;
    }
    const Scene::WorldTransformComponentUVE& sourceWorld =
        entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
    Math::QuaternionUVE sourceRotation{};
    if (sourceWorld.dirty || !IsFiniteVectorUVE(sourceWorld.worldPosition) ||
        !IsFiniteVectorUVE(sourceWorld.worldScale) || !Math::TryNormalizeUVE(sourceWorld.worldRotation, sourceRotation)) {
        return false;
    }

    // No parent and a pure-Object parent both compose as identity, which falls straight through.
    const std::optional<Scene::WorldTransformComponentUVE> parentWorld = TryGetComposingParentWorldUVE(newParent);
    Math::QuaternionUVE parentRotation{};
    if (!parentWorld.has_value() || parentWorld->dirty || !IsFiniteVectorUVE(parentWorld->worldPosition) ||
        !IsFiniteVectorUVE(parentWorld->worldScale) ||
        !Math::TryNormalizeUVE(parentWorld->worldRotation, parentRotation) ||
        parentWorld->worldScale.x < kMinimumLocalScaleUVE || parentWorld->worldScale.y < kMinimumLocalScaleUVE ||
        parentWorld->worldScale.z < kMinimumLocalScaleUVE) {
        return false;
    }
    const bool nonUniform = std::abs(parentWorld->worldScale.x - parentWorld->worldScale.y) > kVectorEpsilonUVE ||
                            std::abs(parentWorld->worldScale.x - parentWorld->worldScale.z) > kVectorEpsilonUVE ||
                            std::abs(parentWorld->worldScale.y - parentWorld->worldScale.z) > kVectorEpsilonUVE;
    const bool rotated = std::abs(parentRotation.x) > kVectorEpsilonUVE ||
                         std::abs(parentRotation.y) > kVectorEpsilonUVE ||
                         std::abs(parentRotation.z) > kVectorEpsilonUVE ||
                         std::abs(std::abs(parentRotation.w) - 1.0F) > kVectorEpsilonUVE;
    if (nonUniform && rotated) {
        return false;
    }
    const Math::Vector3UVE parentPosition = parentWorld->worldPosition;
    const Math::Vector3UVE parentScale = parentWorld->worldScale;

    Math::QuaternionUVE parentInverse{};
    if (!Math::TryInverseUVE(parentRotation, parentInverse)) {
        return false;
    }
    const Math::Vector3UVE unrotated = Math::RotateVectorUVE(
        parentInverse, sourceWorld.worldPosition - parentPosition);
    outTransform.localPosition = Math::Vector3UVE{
        unrotated.x / parentScale.x, unrotated.y / parentScale.y, unrotated.z / parentScale.z};
    if (!Math::TryNormalizeUVE(Math::MultiplyUVE(parentInverse, sourceRotation), outTransform.localRotation)) {
        return false;
    }
    outTransform.localScale = Math::Vector3UVE{
        sourceWorld.worldScale.x / parentScale.x, sourceWorld.worldScale.y / parentScale.y,
        sourceWorld.worldScale.z / parentScale.z};
    return IsTransformFiniteUVE(outTransform) && outTransform.localScale.x >= kMinimumLocalScaleUVE &&
           outTransform.localScale.y >= kMinimumLocalScaleUVE && outTransform.localScale.z >= kMinimumLocalScaleUVE;
}

std::size_t EditorUVE::CountDocumentSubtreeEntitiesUpToConfirmationThresholdUVE(
    const Scene::EntityUVE root) const {
    if (!IsDocumentEntityUVE(root)) {
        return 0U;
    }

    const std::size_t threshold = kHierarchyLargeSubtreeReparentThresholdUVE;
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = m_services->GetSceneGraphUVE();
    std::vector<Scene::EntityUVE> pending{root};
    std::unordered_set<Scene::EntityUVE> visited;
    visited.reserve(threshold);
    std::size_t count = 0U;
    while (!pending.empty()) {
        const Scene::EntityUVE current = pending.back();
        pending.pop_back();
        if (!IsDocumentEntityUVE(current) || !visited.insert(current).second) {
            return 0U;
        }
        ++count;
        if (count >= threshold) {
            return threshold;
        }
        const std::vector<Scene::EntityUVE> children = sceneGraph.GetChildrenUVE(entityManager, current);
        for (const Scene::EntityUVE child : children) {
            if (count + pending.size() >= threshold) {
                break;
            }
            pending.push_back(child);
        }
    }
    return count;
}

bool EditorUVE::RequestHierarchyReparentUVE(const Scene::EntityUVE entity, const Scene::EntityUVE newParent) {
    if (m_pendingHierarchyReparent.has_value()) {
        CancelHierarchyReparentUVE();
    }
    if (!IsLifecycleCommandAllowedUVE() || !m_hierarchyView.dragToReparent ||
        IsStructuralRootUVE(entity) || !IsReparentableObjectUVE(entity)) {
        return false;
    }
    if (!m_hierarchyView.confirmLargeSubtreeReparent) {
        return ReparentDocumentEntityUVE(entity, newParent);
    }

    const std::size_t subtreeEntityCount = CountDocumentSubtreeEntitiesUpToConfirmationThresholdUVE(entity);
    if (subtreeEntityCount == 0U) {
        return false;
    }

    // Reject destinations that are already known to be invalid before opening a modal. In
    // particular, do not resolve/create the default folder for an empty-space drop yet: cancelling
    // the confirmation must not make any document edits as a side effect of merely asking.
    Scene::EntityUVE effectiveParent = newParent;
    if (effectiveParent == Scene::kInvalidEntityUVE) {
        if (m_entityEditSession.has_value()) {
            effectiveParent = GetEntityEditorRootUVE();
        } else {
            Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
            const Scene::EntityUVE viewport = GetDocumentViewportUVE();
            const Scene::Objects::SceneObjectKindUVE kind = Scene::ResolveSceneObjectKindUVE(entityManager, entity);
            if (viewport == Scene::kInvalidEntityUVE || IsTopLevelSingletonKindUVE(kind)) {
                effectiveParent = GetDocumentObjectUVE();
            } else if (entityManager.HasComponentUVE<Scene::FolderComponentUVE>(entity)) {
                effectiveParent = viewport;
            } else {
                const auto isInViewport = [this, viewport](const Scene::EntityUVE folder) {
                    const std::vector<Scene::EntityUVE> ancestry = GetDocumentAncestryUVE(folder);
                    return std::find(ancestry.begin(), ancestry.end(), viewport) != ancestry.end();
                };
                if (m_lastUsedFolder != Scene::kInvalidEntityUVE &&
                    entityManager.IsAliveUVE(m_lastUsedFolder) &&
                    entityManager.HasComponentUVE<Scene::FolderComponentUVE>(m_lastUsedFolder) &&
                    isInViewport(m_lastUsedFolder)) {
                    effectiveParent = m_lastUsedFolder;
                } else {
                    for (const Scene::EntityUVE child :
                         m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, viewport)) {
                        if (entityManager.HasComponentUVE<Scene::FolderComponentUVE>(child)) {
                            effectiveParent = child;
                            break;
                        }
                    }
                }
            }
        }
    }

    if (effectiveParent != Scene::kInvalidEntityUVE) {
        if (!IsHierarchyObjectUVE(effectiveParent) || entity == effectiveParent ||
            !IsAllowedOutlinerParentUVE(entity, effectiveParent)) {
            return false;
        }
        const std::vector<Scene::EntityUVE> ancestry = GetDocumentAncestryUVE(effectiveParent);
        if (ancestry.empty() || std::find(ancestry.begin(), ancestry.end(), entity) != ancestry.end()) {
            return false;
        }
        Scene::EntityUVE currentParent = Scene::kInvalidEntityUVE;
        if (!TryGetDocumentParentUVE(entity, currentParent) || currentParent == effectiveParent) {
            return false;
        }
        Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
        if (entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity) &&
            m_reparentTransformMode == EditorReparentTransformModeUVE::KeepWorld) {
            Scene::TransformComponentUVE localAfter{};
            if (!ComputeKeepWorldLocalTransformUVE(entity, effectiveParent, localAfter)) {
                return false;
            }
        }
    }

    if (subtreeEntityCount >= kHierarchyLargeSubtreeReparentThresholdUVE) {
        m_pendingHierarchyReparent = PendingHierarchyReparentUVE{entity, newParent, subtreeEntityCount};
        m_hierarchyReparentPopupRequested = true;
        return true;
    }
    return ReparentDocumentEntityUVE(entity, newParent);
}

bool EditorUVE::ConfirmHierarchyReparentUVE() {
    if (!m_pendingHierarchyReparent.has_value()) {
        return false;
    }
    const PendingHierarchyReparentUVE pending = *m_pendingHierarchyReparent;
    CancelHierarchyReparentUVE();
    if (!m_hierarchyView.dragToReparent || !m_hierarchyView.confirmLargeSubtreeReparent) {
        return false;
    }
    return ReparentDocumentEntityUVE(pending.entity, pending.newParent);
}

void EditorUVE::CancelHierarchyReparentUVE() noexcept {
    m_pendingHierarchyReparent.reset();
    m_hierarchyReparentPopupRequested = false;
}

bool EditorUVE::RequestHierarchyDeleteUVE() {
    if (m_pendingHierarchyDelete.has_value()) {
        CancelHierarchyDeleteUVE();
    }
    if (!IsLifecycleCommandAllowedUVE() || !IsDocumentEntityUVE(m_selectedEntity) ||
        IsStructuralRootUVE(m_selectedEntity)) {
        return false;
    }
    if (!m_hierarchyView.confirmDeleteSubtree) {
        return DeleteSelectedEntityUVE();
    }
    const std::size_t subtreeEntityCount =
        CountDocumentSubtreeEntitiesUpToConfirmationThresholdUVE(m_selectedEntity);
    if (subtreeEntityCount == 0U) {
        return false;
    }
    // A lone object deletes immediately, still undoable; only a branch - descendants going down
    // silently with it - stops to ask.
    if (subtreeEntityCount >= kHierarchyDeleteSubtreeConfirmThresholdUVE) {
        m_pendingHierarchyDelete = PendingHierarchyDeleteUVE{m_selectedEntity, subtreeEntityCount};
        m_hierarchyDeletePopupRequested = true;
        return true;
    }
    return DeleteSelectedEntityUVE();
}

bool EditorUVE::ConfirmHierarchyDeleteUVE() {
    if (!m_pendingHierarchyDelete.has_value()) {
        return false;
    }
    const PendingHierarchyDeleteUVE pending = *m_pendingHierarchyDelete;
    CancelHierarchyDeleteUVE();
    if (!m_hierarchyView.confirmDeleteSubtree) {
        return false;
    }
    // The delete runs on the selection, so it must still be the object the request named; a
    // selection that moved behind the modal refuses rather than deletes a stranger.
    if (m_selectedEntity != pending.entity) {
        return false;
    }
    return DeleteSelectedEntityUVE();
}

void EditorUVE::CancelHierarchyDeleteUVE() noexcept {
    m_pendingHierarchyDelete.reset();
    m_hierarchyDeletePopupRequested = false;
}

bool EditorUVE::ReparentDocumentEntityUVE(const Scene::EntityUVE entity, const Scene::EntityUVE newParent) {
    if (!IsLifecycleCommandAllowedUVE() || IsStructuralRootUVE(entity) || !IsReparentableObjectUVE(entity) ||
        !IsDocumentSubtreeUVE(entity) ||
        (newParent != Scene::kInvalidEntityUVE && !IsHierarchyObjectUVE(newParent)) ||
        entity == newParent || DoesSubtreeContainEntityUVE(entity, newParent)) {
        return false;
    }
    // One-root documents: "move to document root" means becoming a direct child of the
    // Object - nothing but the root itself may sit at top level.
    // In the Entity Editor the entity's root plays that part.
    // In the level, "the top" means the object's folder, or the Viewport for a folder.
    const auto defaultParent = [this, entity] {
        Scene::IEntityManagerUVE& manager = m_services->GetEntityManagerUVE();
        if (GetDocumentViewportUVE() == Scene::kInvalidEntityUVE) {
            return EnsureDocumentObjectUVE();
        }
        if (IsTopLevelSingletonKindUVE(Scene::ResolveSceneObjectKindUVE(manager, entity))) {
            return EnsureDocumentObjectUVE();
        }
        return manager.HasComponentUVE<Scene::FolderComponentUVE>(entity) ? GetDocumentViewportUVE()
                                                                          : ResolveObjectFolderUVE();
    };
    const Scene::EntityUVE effectiveParent =
        newParent != Scene::kInvalidEntityUVE ? newParent
        : m_entityEditSession.has_value()     ? GetEntityEditorRootUVE()
                                              : defaultParent();
    if (effectiveParent == Scene::kInvalidEntityUVE || entity == effectiveParent ||
        DoesSubtreeContainEntityUVE(entity, effectiveParent) || !IsAllowedOutlinerParentUVE(entity, effectiveParent)) {
        return false;
    }
    Scene::EntityUVE parentBefore = Scene::kInvalidEntityUVE;
    if (!TryGetDocumentParentUVE(entity, parentBefore) || parentBefore == effectiveParent) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    // A pure Object has no transform: it moves in the hierarchy and nothing else changes.
    const bool spatial = entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity);
    const Scene::TransformComponentUVE localBefore =
        spatial ? entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity) : Scene::TransformComponentUVE{};
    Scene::TransformComponentUVE localAfter = localBefore;
    if (spatial && m_reparentTransformMode == EditorReparentTransformModeUVE::KeepWorld &&
        !ComputeKeepWorldLocalTransformUVE(entity, effectiveParent, localAfter)) {
        return false;
    }
    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    const std::size_t siblingIndexBefore =
        m_services->GetSceneGraphUVE().GetSiblingIndexUVE(entityManager, entity).value_or(0U);
    m_services->GetSceneGraphUVE().SetParentUVE(entityManager, entity, effectiveParent);
    if (spatial && !ApplyLocalTransformUVE(entity, localAfter)) {
        m_services->GetSceneGraphUVE().SetParentUVE(entityManager, entity, parentBefore);
        static_cast<void>(ApplyLocalTransformUVE(entity, localBefore));
        return false;
    }
    RestoreSelectionUVE(EditorSelectionSnapshotUVE{{entity}, entity});
    m_sceneDirty = true;
    InvalidateHierarchyFilterCacheUVE();
    // The parent the entity actually got. Recording `newParent` would make redo of "move to
    // document root" set no parent at all, leaving a stray beside the Object instead of under it.
    RecordHistoryUVE(ReparentHistoryEntryUVE{
        entity, parentBefore, effectiveParent, localBefore, localAfter, selectionBefore,
        CaptureSelectionSnapshotUVE(), dirtyBefore, true, siblingIndexBefore,
        m_services->GetSceneGraphUVE().GetSiblingIndexUVE(entityManager, entity).value_or(0U)});
    return true;
}

bool EditorUVE::CanMoveDocumentEntityUVE(const Scene::EntityUVE entity, const EditorSiblingMoveUVE move) {
    if (!IsLifecycleCommandAllowedUVE() || !IsDocumentEntityUVE(entity) || IsStructuralRootUVE(entity)) {
        return false;
    }
    Scene::EntityUVE parent = Scene::kInvalidEntityUVE;
    if (!TryGetDocumentParentUVE(entity, parent)) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const std::optional<std::size_t> index = m_services->GetSceneGraphUVE().GetSiblingIndexUVE(entityManager, entity);
    if (!index.has_value()) {
        return false;
    }
    const std::size_t last = m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, parent).size() - 1U;
    return move == EditorSiblingMoveUVE::Up || move == EditorSiblingMoveUVE::ToTop ? *index > 0U : *index < last;
}

bool EditorUVE::MoveDocumentEntityUVE(const Scene::EntityUVE entity, const EditorSiblingMoveUVE move) {
    if (!CanMoveDocumentEntityUVE(entity, move)) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = m_services->GetSceneGraphUVE();
    Scene::EntityUVE parent = Scene::kInvalidEntityUVE;
    static_cast<void>(TryGetDocumentParentUVE(entity, parent));
    const std::size_t before = sceneGraph.GetSiblingIndexUVE(entityManager, entity).value_or(0U);
    const std::size_t last = sceneGraph.GetChildrenUVE(entityManager, parent).size() - 1U;
    std::size_t after = before;
    switch (move) {
        case EditorSiblingMoveUVE::Up:
            after = before - 1U;
            break;
        case EditorSiblingMoveUVE::Down:
            after = before + 1U;
            break;
        case EditorSiblingMoveUVE::ToTop:
            after = 0U;
            break;
        case EditorSiblingMoveUVE::ToBottom:
            after = last;
            break;
    }
    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    if (!sceneGraph.SetSiblingIndexUVE(entityManager, entity, after)) {
        return false;
    }
    RestoreSelectionUVE(EditorSelectionSnapshotUVE{{entity}, entity});
    m_sceneDirty = true;
    InvalidateHierarchyFilterCacheUVE();
    // Recorded as a move to the same parent, so undo and redo share the reparent path.
    const Scene::TransformComponentUVE local =
        entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity)
            ? entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity)
            : Scene::TransformComponentUVE{};
    RecordHistoryUVE(ReparentHistoryEntryUVE{entity, parent, parent, local, local, selectionBefore,
                                             CaptureSelectionSnapshotUVE(), dirtyBefore, true, before, after});
    return true;
}

bool EditorUVE::UndoUVE() {
    // An edit still in flight is finished first, so undo takes back that edit rather than the
    // one before it underneath a live value.
    static_cast<void>(CommitComponentPropertyPreviewUVE());
    if (!IsAuthoringCommandAllowedUVE() || m_undoHistory.empty()) {
        return false;
    }

    HistoryEntryUVE entry = std::move(m_undoHistory.back());
    m_undoHistory.pop_back();
    if (!UndoHistoryEntryUVE(entry)) {
        ClearHistoryUVE();
        return false;
    }

    m_redoHistory.push_back(std::move(entry));
    return true;
}

bool EditorUVE::RedoUVE() {
    static_cast<void>(CommitComponentPropertyPreviewUVE());
    if (!IsAuthoringCommandAllowedUVE() || m_redoHistory.empty()) {
        return false;
    }

    HistoryEntryUVE entry = std::move(m_redoHistory.back());
    m_redoHistory.pop_back();
    if (!RedoHistoryEntryUVE(entry)) {
        ClearHistoryUVE();
        return false;
    }

    m_undoHistory.push_back(std::move(entry));
    return true;
}

bool EditorUVE::CanUndoUVE() const noexcept {
    return IsAuthoringCommandAllowedUVE() && !m_undoHistory.empty();
}

bool EditorUVE::CanRedoUVE() const noexcept {
    return IsAuthoringCommandAllowedUVE() && !m_redoHistory.empty();
}

bool EditorUVE::ApplyLocalTransformUVE(const Scene::EntityUVE entity,
                                       const Scene::TransformComponentUVE& transform) {
    if (!IsDocumentEntityUVE(entity) || !IsTransformFiniteUVE(transform)) {
        return false;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity)) {
        return false;
    }

    m_services->GetSceneGraphUVE().SetLocalTransformUVE(entityManager, entity, transform);
    return true;
}

bool EditorUVE::ApplyPrimitiveMeshStateUVE(const Scene::EntityUVE entity,
                                            const Scene::PrimitiveMeshComponentUVE& primitive) {
    if (!IsDocumentEntityUVE(entity) || !Scene::IsPrimitiveMeshComponentValidUVE(primitive)) {
        return false;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity)) {
        return false;
    }
    entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity) = primitive;
    if (entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(entity)) {
        entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(entity).halfExtents =
            PrimitiveColliderHalfExtentsUVE(primitive.kind);
    }
    return true;
}

bool EditorUVE::ApplyEntityNameStateUVE(const Scene::EntityUVE entity,
                                          const std::optional<std::string>& name) {

    if (!IsDocumentEntityUVE(entity) || (name.has_value() && !IsEntityNameValidUVE(*name))) {
        return false;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const bool hasName = entityManager.HasComponentUVE<Scene::NameComponentUVE>(entity);
    if (!name.has_value()) {
        if (!hasName) {
            return false;
        }
        entityManager.RemoveComponentUVE<Scene::NameComponentUVE>(entity);
        InvalidateHierarchyFilterCacheUVE();
        return true;
    }

    if (hasName) {
        entityManager.GetComponentUVE<Scene::NameComponentUVE>(entity).name = *name;
    } else {
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(entity, Scene::NameComponentUVE{*name});
    }
    InvalidateHierarchyFilterCacheUVE();
    return true;
}

bool EditorUVE::IsDocumentSubtreeUVE(const Scene::EntityUVE root) const {
    if (!IsDocumentEntityUVE(root)) {
        return false;
    }

    // A document subtree must never absorb the editor-owned viewport camera, even if a caller
    // externally attempts an invalid reparent. Detect malformed cycles before any traversal caller
    // can act on the subtree.
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    std::vector<Scene::EntityUVE> pending{root};
    std::vector<Scene::EntityUVE> visited;
    while (!pending.empty()) {
        const Scene::EntityUVE current = pending.back();
        pending.pop_back();
        if (!IsDocumentEntityUVE(current) ||
            std::find(visited.begin(), visited.end(), current) != visited.end()) {
            return false;
        }
        visited.push_back(current);
        const std::vector<Scene::EntityUVE> children =
            m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, current);
        pending.insert(pending.end(), children.begin(), children.end());
    }
    return true;
}

bool EditorUVE::DoesSubtreeContainEntityUVE(const Scene::EntityUVE root,
                                            const Scene::EntityUVE candidate) const {
    if (candidate == Scene::kInvalidEntityUVE || !IsDocumentSubtreeUVE(root)) {
        return false;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    std::vector<Scene::EntityUVE> pending{root};
    while (!pending.empty()) {
        const Scene::EntityUVE current = pending.back();
        pending.pop_back();
        if (current == candidate) {
            return true;
        }
        const std::vector<Scene::EntityUVE> children =
            m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, current);
        pending.insert(pending.end(), children.begin(), children.end());
    }
    return false;
}

std::optional<Scene::SceneSnapshotUVE> EditorUVE::CaptureSubtreeUVE(const Scene::EntityUVE root) {
    if (!IsDocumentSubtreeUVE(root)) {
        return std::nullopt;
    }

    return m_services->GetSceneSerializerUVE().CaptureUVE(
        m_services->GetEntityManagerUVE(), {root}, Asset::AssetKindUVE::Scene);
}

Scene::EntityUVE EditorUVE::RestoreSubtreeUnderParentUVE(const Scene::SceneSnapshotUVE& snapshot,
                                                         const Scene::EntityUVE parent) {
    if (parent != Scene::kInvalidEntityUVE && !IsDocumentEntityUVE(parent)) {
        return Scene::kInvalidEntityUVE;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    std::vector<Scene::EntityUVE> restoredRoots = m_services->GetSceneSerializerUVE().RestoreUVE(entityManager, snapshot);
    if (restoredRoots.size() != 1U || !IsDocumentEntityUVE(restoredRoots.front())) {
        for (const Scene::EntityUVE restoredRoot : restoredRoots) {
            if (IsDocumentEntityUVE(restoredRoot)) {
                DestroyDocumentSubtreeUVE(restoredRoot);
            }
        }
        return Scene::kInvalidEntityUVE;
    }

    const Scene::EntityUVE restoredRoot = restoredRoots.front();
    if (parent != Scene::kInvalidEntityUVE) {
        if (!entityManager.HasComponentUVE<Scene::HierarchyComponentUVE>(restoredRoot)) {
            DestroyDocumentSubtreeUVE(restoredRoot);
            return Scene::kInvalidEntityUVE;
        }
        m_services->GetSceneGraphUVE().SetParentUVE(entityManager, restoredRoot, parent);
    }
    return restoredRoot;
}

bool EditorUVE::TryGetDocumentParentUVE(const Scene::EntityUVE entity, Scene::EntityUVE& outParent) const {
    outParent = Scene::kInvalidEntityUVE;
    if (!IsDocumentEntityUVE(entity)) {
        return false;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::HierarchyComponentUVE>(entity)) {
        return true;
    }
    const Scene::EntityUVE parent = entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(entity).parent;
    if (parent == Scene::kInvalidEntityUVE) {
        return true;
    }
    if (!IsDocumentEntityUVE(parent)) {
        return false;
    }
    outParent = parent;
    return true;
}

std::string_view EditorUVE::GetObjectTypeNameUVE(const Scene::EntityUVE entity) const {
    if (!IsDocumentEntityUVE(entity)) {
        return {};
    }
    const Scene::Objects::SceneObjectDescriptorUVE* const descriptor = Scene::Objects::FindSceneObjectDescriptorUVE(
        Scene::ResolveSceneObjectKindUVE(m_services->GetEntityManagerUVE(), entity));
    return descriptor != nullptr ? descriptor->displayName : std::string_view{};
}

std::string EditorUVE::GetOutlinerTypeTagUVE(const Scene::EntityUVE entity) const {
    if (!IsDocumentEntityUVE(entity)) {
        return {};
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (entityManager.HasComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity)) {
        switch (entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity).kind) {
            case Scene::PrimitiveMeshKindUVE::Plane:
                return std::string{Scene::PlaneMesh3DObjectDefinitionUVE::defaultName};
            case Scene::PrimitiveMeshKindUVE::UVSphere:
                return std::string{Scene::SphereMesh3DObjectDefinitionUVE::defaultName};
            case Scene::PrimitiveMeshKindUVE::Cube:
                return std::string{Scene::BoxMesh3DObjectDefinitionUVE::defaultName};
        }
    }
    if (entityManager.HasComponentUVE<Scene::CameraComponentUVE>(entity)) {
        return std::string{Scene::Camera3DObjectDefinitionUVE::defaultName};
    }
    if (entityManager.HasComponentUVE<Scene::LightComponentUVE>(entity) &&
        entityManager.GetComponentUVE<Scene::LightComponentUVE>(entity).type == Scene::LightTypeUVE::Directional) {
        return std::string{Scene::Light3DObjectDefinitionUVE::defaultName};
    }
    if (entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(entity)) {
        return std::string{Scene::Collider3DObjectDefinitionUVE::defaultName};
    }
    return {};
}

std::vector<Scene::EntityUVE> EditorUVE::GetDocumentAncestryUVE(const Scene::EntityUVE entity) const {
    if (!IsDocumentEntityUVE(entity)) {
        return {};
    }

    std::vector<Scene::EntityUVE> ancestry;
    Scene::EntityUVE current = entity;
    while (current != Scene::kInvalidEntityUVE) {
        if (!IsDocumentEntityUVE(current) ||
            std::find(ancestry.begin(), ancestry.end(), current) != ancestry.end()) {
            return {};
        }
        ancestry.push_back(current);

        Scene::EntityUVE parent = Scene::kInvalidEntityUVE;
        if (!TryGetDocumentParentUVE(current, parent)) {
            return {};
        }
        current = parent;
    }

    std::reverse(ancestry.begin(), ancestry.end());
    return ancestry;
}

std::vector<Scene::EntityUVE> EditorUVE::GetEligibleReparentParentsUVE(const Scene::EntityUVE entity) {
    if (!IsDocumentSubtreeUVE(entity)) {
        return {};
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    std::vector<Scene::EntityUVE> excludedSubtree;
    std::vector<Scene::EntityUVE> pending{entity};
    while (!pending.empty()) {
        const Scene::EntityUVE current = pending.back();
        pending.pop_back();
        if (!IsDocumentEntityUVE(current) ||
            std::find(excludedSubtree.begin(), excludedSubtree.end(), current) != excludedSubtree.end()) {
            return {};
        }
        excludedSubtree.push_back(current);
        const std::vector<Scene::EntityUVE> children =
            m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, current);
        pending.insert(pending.end(), children.begin(), children.end());
    }

    std::vector<Scene::EntityUVE> candidates;
    std::vector<Scene::EntityUVE> visited;
    const auto visit = [this, &entityManager, &excludedSubtree, &candidates, &visited](
                           const auto& self, const Scene::EntityUVE current) -> void {
        if (!IsDocumentEntityUVE(current) || std::find(visited.begin(), visited.end(), current) != visited.end()) {
            return;
        }
        visited.push_back(current);
        if (std::find(excludedSubtree.begin(), excludedSubtree.end(), current) == excludedSubtree.end()) {
            candidates.push_back(current);
        }
        for (const Scene::EntityUVE child : m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, current)) {
            self(self, child);
        }
    };

    for (const Scene::EntityUVE root : GetDocumentRootsUVE()) {
        visit(visit, root);
    }
    std::erase_if(candidates, [this, entity](const Scene::EntityUVE parent) {
        return !IsAllowedOutlinerParentUVE(entity, parent);
    });
    return candidates;
}

std::string EditorUVE::GetHierarchyCandidateLabelUVE(const Scene::EntityUVE entity) const {
    return GetEntityDisplayLabelUVE(entity);
}

bool EditorUVE::IsLifecycleCommandAllowedUVE() const noexcept {
    return IsAuthoringCommandAllowedUVE() && HasSingleDocumentSelectionUVE();
}

bool EditorUVE::IsAuthoringCommandAllowedUVE() const noexcept {
    // The Retarget preview stands in for the scene while its window is open: nothing is authored
    // (or saved) into it.
    return m_state == EditorStateUVE::Running && m_playModeState == EditorPlayModeStateUVE::Edit &&
           !m_retargetPreview.has_value();
}

EditorUVE::EditorSelectionSnapshotUVE EditorUVE::CaptureSelectionSnapshotUVE() const {
    EditorSelectionSnapshotUVE selection{};
    for (const Scene::EntityUVE entity : m_selectedEntities) {
        if (IsDocumentEntityUVE(entity) &&
            std::find(selection.entities.begin(), selection.entities.end(), entity) == selection.entities.end()) {
            selection.entities.push_back(entity);
        }
    }
    if (IsDocumentEntityUVE(m_selectedEntity) &&
        std::find(selection.entities.begin(), selection.entities.end(), m_selectedEntity) != selection.entities.end()) {
        selection.activeEntity = m_selectedEntity;
    } else if (!selection.entities.empty()) {
        selection.activeEntity = selection.entities.back();
    }
    return selection;
}

bool EditorUVE::IsSelectionSnapshotCurrentUVE(const EditorSelectionSnapshotUVE& snapshot) const {
    const EditorSelectionSnapshotUVE live = CaptureSelectionSnapshotUVE();
    return live.activeEntity == snapshot.activeEntity && live.entities == snapshot.entities;
}

void EditorUVE::RestoreSelectionUVE(EditorSelectionSnapshotUVE selection) noexcept {
    std::vector<Scene::EntityUVE> restored;
    restored.reserve(selection.entities.size());
    for (const Scene::EntityUVE entity : selection.entities) {
        if (IsDocumentEntityUVE(entity) && !IsEntityLockedUVE(entity) &&
            std::find(restored.begin(), restored.end(), entity) == restored.end()) {
            restored.push_back(entity);
        }
    }

    const bool activeValid = IsDocumentEntityUVE(selection.activeEntity) &&
                             std::find(restored.begin(), restored.end(), selection.activeEntity) != restored.end();
    const Scene::EntityUVE restoredActive = activeValid
                                                ? selection.activeEntity
                                                : (restored.empty() ? Scene::kInvalidEntityUVE : restored.back());
    const bool changed = restored != m_selectedEntities || restoredActive != m_selectedEntity;
    m_selectedEntities = std::move(restored);
    m_selectedEntity = restoredActive;
    m_hierarchySelectionAnchor = restoredActive;
    if (changed) {
        CancelHierarchyRenameUVE();
    }
}

void EditorUVE::PruneSelectionUVE() noexcept {
    const Scene::EntityUVE selectionAnchor = m_hierarchySelectionAnchor;
    RestoreSelectionUVE(CaptureSelectionSnapshotUVE());
    if (IsDocumentEntityUVE(selectionAnchor) && !IsEntityLockedUVE(selectionAnchor)) {
        m_hierarchySelectionAnchor = selectionAnchor;
    }
}

bool EditorUVE::IsEntitySelectedUVE(const Scene::EntityUVE entity) const noexcept {
    return std::find(m_selectedEntities.begin(), m_selectedEntities.end(), entity) != m_selectedEntities.end();
}

EditorUVE::EditorSelectionPathsUVE EditorUVE::CaptureSelectionPathsUVE(
    const std::vector<Scene::EntityUVE>& roots) const {
    EditorSelectionPathsUVE paths{};
    const EditorSelectionSnapshotUVE selection = CaptureSelectionSnapshotUVE();
    const auto capturePath = [this, &roots](const Scene::EntityUVE entity) -> std::optional<EditorSelectionPathUVE> {
        for (std::size_t rootIndex = 0U; rootIndex < roots.size(); ++rootIndex) {
            EditorSelectionPathUVE path{};
            path.rootIndex = rootIndex;
            if (FindSelectionPathUVE(roots[rootIndex], entity, path.childIndices)) {
                return path;
            }
        }
        return std::nullopt;
    };

    for (const Scene::EntityUVE entity : selection.entities) {
        if (const std::optional<EditorSelectionPathUVE> path = capturePath(entity); path.has_value()) {
            paths.entityPaths.push_back(*path);
        }
    }
    paths.activePath = capturePath(selection.activeEntity);
    return paths;
}

EditorUVE::EditorSelectionSnapshotUVE EditorUVE::ResolveSelectionPathsUVE(
    const EditorSelectionPathsUVE& paths, const std::vector<Scene::EntityUVE>& roots) const {
    EditorSelectionSnapshotUVE selection{};
    for (const EditorSelectionPathUVE& path : paths.entityPaths) {
        const Scene::EntityUVE entity = ResolveSelectionPathUVE(path, roots);
        if (IsDocumentEntityUVE(entity) &&
            std::find(selection.entities.begin(), selection.entities.end(), entity) == selection.entities.end()) {
            selection.entities.push_back(entity);
        }
    }
    if (paths.activePath.has_value()) {
        const Scene::EntityUVE active = ResolveSelectionPathUVE(*paths.activePath, roots);
        if (std::find(selection.entities.begin(), selection.entities.end(), active) != selection.entities.end()) {
            selection.activeEntity = active;
        }
    }
    if (selection.activeEntity == Scene::kInvalidEntityUVE && !selection.entities.empty()) {
        selection.activeEntity = selection.entities.back();
    }
    return selection;
}

Scene::EntityUVE EditorUVE::ResolveSelectionPathUVE(const EditorSelectionPathUVE& path,
                                                     const std::vector<Scene::EntityUVE>& roots) const {
    if (path.rootIndex >= roots.size() || !IsDocumentEntityUVE(roots[path.rootIndex])) {
        return Scene::kInvalidEntityUVE;
    }

    Scene::EntityUVE resolved = roots[path.rootIndex];
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    for (const std::size_t childIndex : path.childIndices) {
        const std::vector<Scene::EntityUVE> children =
            m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, resolved);
        if (childIndex >= children.size() || !IsDocumentEntityUVE(children[childIndex])) {
            return Scene::kInvalidEntityUVE;
        }
        resolved = children[childIndex];
    }
    return resolved;
}

bool EditorUVE::FindSelectionPathUVE(const Scene::EntityUVE current, const Scene::EntityUVE target,
                                     std::vector<std::size_t>& inOutChildIndices) const {
    if (current == target) {
        return true;
    }
    if (!IsDocumentEntityUVE(current)) {
        return false;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const std::vector<Scene::EntityUVE> children =
        m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, current);
    for (std::size_t childIndex = 0U; childIndex < children.size(); ++childIndex) {
        inOutChildIndices.push_back(childIndex);
        if (FindSelectionPathUVE(children[childIndex], target, inOutChildIndices)) {
            return true;
        }
        inOutChildIndices.pop_back();
    }
    return false;
}

Scene::EntityUVE EditorUVE::CreateDocumentEntityShellInternalUVE(const std::string_view name) {
    if (name.empty() || !IsEntityNameValidUVE(name)) {
        return Scene::kInvalidEntityUVE;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = m_services->GetSceneGraphUVE();
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, entity, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::NameComponentUVE>(entity, Scene::NameComponentUVE{std::string{name}});
    InvalidateHierarchyFilterCacheUVE();
    return entity;
}

template <typename Definition, typename ApplyFunc>
Scene::EntityUVE EditorUVE::CreateObjectDefinitionEntityInternalUVE(const Definition& definition,
                                                                  ApplyFunc applyDefinition) {
    Scene::EntityUVE entity = CreateDocumentEntityShellInternalUVE(
        MakeUniqueDocumentEntityNameUVE(definition.defaultName));
    if (entity != Scene::kInvalidEntityUVE) {
        applyDefinition(m_services->GetEntityManagerUVE(), entity, definition);
    }
    return entity;
}

Scene::EntityUVE EditorUVE::CreateDocumentEntityInternalUVE(
    const EditorEntityKindUVE kind, const std::optional<std::string>& explicitName) {
    switch (kind) {
        case EditorEntityKindUVE::Empty:
        case EditorEntityKindUVE::Camera:
        case EditorEntityKindUVE::DirectionalLight:
        case EditorEntityKindUVE::CollisionBox:
        case EditorEntityKindUVE::Cube:
        case EditorEntityKindUVE::UVSphere:
        case EditorEntityKindUVE::Plane:
            break;
        default:
            return Scene::kInvalidEntityUVE;
    }

    const std::string defaultName = GetDefaultEntityNameUVE(kind);
    const std::string name = explicitName.has_value() ? *explicitName : MakeUniqueDocumentEntityNameUVE(defaultName);
    if (defaultName.empty() || !IsEntityNameValidUVE(name)) {
        return Scene::kInvalidEntityUVE;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const Scene::EntityUVE entity = CreateDocumentEntityShellInternalUVE(name);
    if (entity == Scene::kInvalidEntityUVE) {
        return Scene::kInvalidEntityUVE;
    }

    // The per-kind recipes below deliberately live with their object definitions in
    // Engine/Runtime/Objects/3D (one .h + .cpp per kind), not inline here: these legacy editor
    // entity kinds are alternate doors into the exact same recipes the scene-object Add-Object list
    // uses, so a kind's defaults have one home, not two.
    switch (kind) {
        case EditorEntityKindUVE::Empty:
            break;
        case EditorEntityKindUVE::Camera:
            Scene::ApplyCamera3DObjectDefinitionUVE(entityManager, entity, Scene::Camera3DObjectDefinitionUVE{});
            break;
        case EditorEntityKindUVE::DirectionalLight:
            Scene::ApplyLight3DObjectDefinitionUVE(entityManager, entity, Scene::Light3DObjectDefinitionUVE{});
            break;
        case EditorEntityKindUVE::CollisionBox:
            Scene::ApplyCollider3DObjectDefinitionUVE(entityManager, entity, Scene::Collider3DObjectDefinitionUVE{});
            break;
        case EditorEntityKindUVE::Cube:
            Scene::ApplyBoxMesh3DObjectDefinitionUVE(entityManager, entity, Scene::BoxMesh3DObjectDefinitionUVE{});
            break;
        case EditorEntityKindUVE::UVSphere:
            Scene::ApplySphereMesh3DObjectDefinitionUVE(entityManager, entity, Scene::SphereMesh3DObjectDefinitionUVE{});
            break;
        case EditorEntityKindUVE::Plane:
            Scene::ApplyPlaneMesh3DObjectDefinitionUVE(entityManager, entity, Scene::PlaneMesh3DObjectDefinitionUVE{});
            break;
        default:
            return Scene::kInvalidEntityUVE;
    }

    // These legacy kinds are the same objects the Add Object list makes, and are typed as those.
    Scene::SetSceneObjectKindUVE(entityManager, entity, ToSceneObjectKindUVE(kind));

    // Same hierarchy-joining rule as CreateDocumentSceneObjectUVE - never a new document root.
    if (entity != Scene::kInvalidEntityUVE) {
        const Scene::EntityUVE parentObject = ResolveNewObjectParentUVE();
        if (parentObject != Scene::kInvalidEntityUVE) {
            m_services->GetSceneGraphUVE().SetParentUVE(entityManager, entity, parentObject);
            InvalidateHierarchyFilterCacheUVE();
        }
        PlaceNewDocumentObjectUVE(entity);
    }
    return entity;
}

Scene::EntityUVE EditorUVE::ResolveNewObjectParentUVE() {
    // In the level, an object always lives in a folder: under the selection when it is in one, else in
    // the folder new objects go to.
    if (IsOutlinerLayoutActiveUVE() && GetDocumentViewportUVE() != Scene::kInvalidEntityUVE) {
        Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
        if (m_newObjectsUnderSelection && m_selectedEntity != Scene::kInvalidEntityUVE &&
            IsDocumentSubtreeUVE(m_selectedEntity) && m_selectedEntity != GetDocumentViewportUVE()) {
            for (Scene::EntityUVE cursor = m_selectedEntity, parent = Scene::kInvalidEntityUVE;
                 cursor != Scene::kInvalidEntityUVE && TryGetDocumentParentUVE(cursor, parent); cursor = parent) {
                if (entityManager.HasComponentUVE<Scene::FolderComponentUVE>(cursor) &&
                    !entityManager.HasComponentUVE<Scene::OutlinerViewportComponentUVE>(cursor)) {
                    if (cursor == m_selectedEntity) {
                        m_lastUsedFolder = cursor;
                    }
                    return m_selectedEntity;
                }
            }
        }
        return ResolveObjectFolderUVE();
    }
    if (m_newObjectsUnderSelection && m_selectedEntity != Scene::kInvalidEntityUVE &&
        IsDocumentSubtreeUVE(m_selectedEntity)) {
        return m_selectedEntity;
    }
    // In the Entity Editor, new objects belong to the entity.
    if (m_entityEditSession.has_value()) {
        const Scene::EntityUVE entityRoot = GetEntityEditorRootUVE();
        if (entityRoot != Scene::kInvalidEntityUVE) {
            return entityRoot;
        }
    }
    return EnsureDocumentObjectUVE();
}

Scene::EntityUVE EditorUVE::ResolveNewObjectParentForUVE(const Scene::Objects::SceneObjectKindUVE kind) {
    if (!IsOutlinerLayoutActiveUVE() || GetDocumentViewportUVE() == Scene::kInvalidEntityUVE) {
        return ResolveNewObjectParentUVE();
    }
    if (IsTopLevelSingletonKindUVE(kind)) {
        return EnsureDocumentObjectUVE();
    }
    if (kind == Scene::Objects::SceneObjectKindUVE::Folder) {
        // Into the selected folder (or the Viewport itself); otherwise the Viewport.
        Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
        if (m_selectedEntity != Scene::kInvalidEntityUVE && IsDocumentSubtreeUVE(m_selectedEntity) &&
            entityManager.HasComponentUVE<Scene::FolderComponentUVE>(m_selectedEntity)) {
            return m_selectedEntity;
        }
        return GetDocumentViewportUVE();
    }
    return ResolveNewObjectParentUVE();
}

bool EditorUVE::IsOutlinerLayoutActiveUVE() const noexcept {
    return !m_entityEditSession.has_value() && !m_retargetPreview.has_value();
}

Scene::EntityUVE EditorUVE::GetDocumentViewportUVE() {
    if (!IsOutlinerLayoutActiveUVE()) {
        return Scene::kInvalidEntityUVE;
    }
    Scene::EntityUVE found = Scene::kInvalidEntityUVE;
    m_services->GetEntityManagerUVE().ForEachUVE<Scene::OutlinerViewportComponentUVE>(
        [&found](const Scene::EntityUVE entity, const Scene::OutlinerViewportComponentUVE&) {
            if (found == Scene::kInvalidEntityUVE) {
                found = entity;
            }
        });
    return found;
}

bool EditorUVE::IsTopLevelSingletonKindUVE(const Scene::Objects::SceneObjectKindUVE kind) const noexcept {
    return kind == Scene::Objects::SceneObjectKindUVE::DirectionalLight3D ||
           kind == Scene::Objects::SceneObjectKindUVE::WorldEnvironment3D;
}

Scene::EntityUVE EditorUVE::FindTopLevelObjectUVE(const Scene::Objects::SceneObjectKindUVE kind) {
    const Scene::EntityUVE rootObject = GetDocumentObjectUVE();
    if (rootObject == Scene::kInvalidEntityUVE) {
        return Scene::kInvalidEntityUVE;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    for (const Scene::EntityUVE child : m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, rootObject)) {
        if (Scene::ResolveSceneObjectKindUVE(entityManager, child) == kind) {
            return child;
        }
    }
    return Scene::kInvalidEntityUVE;
}

bool EditorUVE::EnsureDocumentLayoutUVE() {
    if (!IsOutlinerLayoutActiveUVE()) {
        return false;
    }
    const Scene::EntityUVE rootObject = EnsureDocumentObjectUVE();
    if (rootObject == Scene::kInvalidEntityUVE) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = m_services->GetSceneGraphUVE();
    bool changed = false;
    Scene::EntityUVE viewport = GetDocumentViewportUVE();
    if (viewport == Scene::kInvalidEntityUVE) {
        viewport = CreateDocumentEntityShellInternalUVE("Viewport");
        if (viewport == Scene::kInvalidEntityUVE) {
            return false;
        }
        Scene::ApplyViewportObjectDefinitionUVE(entityManager, viewport, Scene::ViewportObjectDefinitionUVE{});
        Scene::SetSceneObjectKindUVE(entityManager, viewport, Scene::Objects::SceneObjectKindUVE::Viewport);
        sceneGraph.SetParentUVE(entityManager, viewport, rootObject);
        changed = true;
    }
    if (Scene::EntityUVE parent = Scene::kInvalidEntityUVE;
        !TryGetDocumentParentUVE(viewport, parent) || parent != rootObject) {
        sceneGraph.SetParentUVE(entityManager, viewport, rootObject);
        changed = true;
    }
    static_cast<void>(sceneGraph.SetSiblingIndexUVE(entityManager, viewport, 0U));
    // The Viewport is the open level: it carries the name of its asset in the Content Browser.
    if (const std::string stem = m_activeScenePath.stem().string(); !stem.empty()) {
        if (!entityManager.HasComponentUVE<Scene::NameComponentUVE>(viewport)) {
            entityManager.AddComponentUVE<Scene::NameComponentUVE>(viewport, Scene::NameComponentUVE{stem});
        } else if (entityManager.GetComponentUVE<Scene::NameComponentUVE>(viewport).name != stem) {
            entityManager.GetComponentUVE<Scene::NameComponentUVE>(viewport).name = stem;
            changed = true;
        }
    }

    // Anything at the top that is not one of the three moves into a folder; a folder moves into the
    // Viewport. A second DirectionalLight3D or WorldEnvironment is an ordinary object there.
    bool seenLight = false;
    bool seenEnvironment = false;
    std::vector<Scene::EntityUVE> strays;
    for (const Scene::EntityUVE child : sceneGraph.GetChildrenUVE(entityManager, rootObject)) {
        if (child == viewport) {
            continue;
        }
        const Scene::Objects::SceneObjectKindUVE kind = Scene::ResolveSceneObjectKindUVE(entityManager, child);
        if (kind == Scene::Objects::SceneObjectKindUVE::DirectionalLight3D && !seenLight) {
            seenLight = true;
        } else if (kind == Scene::Objects::SceneObjectKindUVE::WorldEnvironment3D && !seenEnvironment) {
            seenEnvironment = true;
        } else {
            strays.push_back(child);
        }
    }
    const bool viewportEmpty = sceneGraph.GetChildrenUVE(entityManager, viewport).empty();
    Scene::EntityUVE world = Scene::kInvalidEntityUVE;
    for (const Scene::EntityUVE stray : strays) {
        if (entityManager.HasComponentUVE<Scene::FolderComponentUVE>(stray)) {
            sceneGraph.SetParentUVE(entityManager, stray, viewport);
        } else {
            if (world == Scene::kInvalidEntityUVE) {
                world = ResolveObjectFolderUVE();
            }
            sceneGraph.SetParentUVE(entityManager, stray, world);
        }
        changed = true;
    }
    // A level always starts with a folder to put things in.
    if (viewportEmpty && sceneGraph.GetChildrenUVE(entityManager, viewport).empty()) {
        static_cast<void>(ResolveObjectFolderUVE());
        changed = true;
    }
    InvalidateHierarchyFilterCacheUVE();
    return changed;
}

Scene::EntityUVE EditorUVE::ResolveObjectFolderUVE() {
    const Scene::EntityUVE viewport = GetDocumentViewportUVE();
    if (viewport == Scene::kInvalidEntityUVE) {
        return EnsureDocumentObjectUVE();
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = m_services->GetSceneGraphUVE();
    const auto inViewport = [&](const Scene::EntityUVE folder) {
        for (Scene::EntityUVE cursor = folder, parent = Scene::kInvalidEntityUVE;
             cursor != Scene::kInvalidEntityUVE && TryGetDocumentParentUVE(cursor, parent); cursor = parent) {
            if (parent == viewport) {
                return true;
            }
        }
        return false;
    };
    if (m_lastUsedFolder != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(m_lastUsedFolder) &&
        entityManager.HasComponentUVE<Scene::FolderComponentUVE>(m_lastUsedFolder) && inViewport(m_lastUsedFolder)) {
        return m_lastUsedFolder;
    }
    for (const Scene::EntityUVE child : sceneGraph.GetChildrenUVE(entityManager, viewport)) {
        if (entityManager.HasComponentUVE<Scene::FolderComponentUVE>(child)) {
            return m_lastUsedFolder = child;
        }
    }
    const Scene::EntityUVE world = CreateDocumentEntityShellInternalUVE(MakeUniqueDocumentEntityNameUVE("World"));
    if (world == Scene::kInvalidEntityUVE) {
        return viewport;
    }
    Scene::ApplyFolderObjectDefinitionUVE(entityManager, world, Scene::FolderObjectDefinitionUVE{});
    Scene::SetSceneObjectKindUVE(entityManager, world, Scene::Objects::SceneObjectKindUVE::Folder);
    sceneGraph.SetParentUVE(entityManager, world, viewport);
    InvalidateHierarchyFilterCacheUVE();
    return m_lastUsedFolder = world;
}

bool EditorUVE::IsAllowedOutlinerParentUVE(const Scene::EntityUVE entity, const Scene::EntityUVE parent) {
    if (!IsOutlinerLayoutActiveUVE() || GetDocumentViewportUVE() == Scene::kInvalidEntityUVE) {
        return true;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const Scene::EntityUVE rootObject = GetDocumentObjectUVE();
    const Scene::EntityUVE viewport = GetDocumentViewportUVE();
    if (entity == viewport) {
        return parent == rootObject;
    }
    const Scene::Objects::SceneObjectKindUVE kind = Scene::ResolveSceneObjectKindUVE(entityManager, entity);
    if (parent == rootObject) {
        // Only the level's own sun and environment sit beside the Viewport, one of each.
        const Scene::EntityUVE existing = IsTopLevelSingletonKindUVE(kind) ? FindTopLevelObjectUVE(kind) : entity;
        return IsTopLevelSingletonKindUVE(kind) && (existing == Scene::kInvalidEntityUVE || existing == entity);
    }
    if (parent == viewport) {
        return entityManager.HasComponentUVE<Scene::FolderComponentUVE>(entity);
    }
    // Anywhere else in the Viewport's tree is inside a folder.
    for (Scene::EntityUVE cursor = parent, up = Scene::kInvalidEntityUVE;
         cursor != Scene::kInvalidEntityUVE && TryGetDocumentParentUVE(cursor, up); cursor = up) {
        if (cursor == viewport) {
            return true;
        }
        if (up == viewport) {
            return true;
        }
    }
    return false;
}

void EditorUVE::PlaceNewDocumentObjectUVE(const Scene::EntityUVE entity) {
    switch (m_newObjectPlacement) {
        case EditorNewObjectPlacementUVE::ViewFocus:
            if (m_viewportCameraFocus.has_value()) {
                PlaceNewDocumentObjectAtUVE(entity, *m_viewportCameraFocus);
            }
            return;
        case EditorNewObjectPlacementUVE::GroundPlane:
            // No aim yet (the cursor never entered the viewport) falls back to the focus, so the
            // first object still lands where the person is looking instead of at the origin.
            if (m_viewportCursorGroundPoint.has_value()) {
                PlaceNewDocumentObjectAtUVE(entity, *m_viewportCursorGroundPoint);
            } else if (m_viewportCameraFocus.has_value()) {
                PlaceNewDocumentObjectAtUVE(entity, *m_viewportCameraFocus);
            }
            return;
        case EditorNewObjectPlacementUVE::ParentOrigin:
            return;
    }
}

void EditorUVE::PlaceNewDocumentObjectAtUVE(const Scene::EntityUVE entity,
                                            const Math::Vector3UVE& worldPoint) {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity) ||
        !entityManager.HasComponentUVE<Scene::HierarchyComponentUVE>(entity)) {
        return; // A pure Object has no place in space.
    }
    // The object sits at its parent's origin; move it by the offset from there to the point, taken
    // into the parent's space. A parent whose world transform is not resolved yet leaves it be.
    const std::optional<Scene::WorldTransformComponentUVE> parentWorld =
        TryGetComposingParentWorldUVE(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(entity).parent);
    Math::Vector3UVE localPosition{};
    if (!parentWorld.has_value() || parentWorld->dirty ||
        !ComputeLocalDeltaForWorldDeltaUVE(entity, worldPoint - parentWorld->worldPosition,
                                           localPosition)) {
        return;
    }
    Scene::TransformComponentUVE transform = entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
    transform.localPosition = localPosition;
    m_services->GetSceneGraphUVE().SetLocalTransformUVE(entityManager, entity, transform);
}

void EditorUVE::SetViewportCameraFocusUVE(const Math::Vector3UVE& focus) noexcept {
    if (IsFiniteVectorUVE(focus)) {
        m_viewportCameraFocus = focus;
    }
}

void EditorUVE::SetViewportCursorGroundPointUVE(const Math::Vector3UVE& groundPoint) noexcept {
    if (IsFiniteVectorUVE(groundPoint)) {
        m_viewportCursorGroundPoint = groundPoint;
    }
}

void EditorUVE::RecordHistoryUVE(HistoryEntryUVE entry) {
    m_redoHistory.clear();
    if (m_undoHistory.size() >= m_historyCapacity) {
        m_undoHistory.pop_front();
    }
    m_undoHistory.push_back(std::move(entry));
}

void EditorUVE::ClearHistoryUVE() noexcept {
    m_undoHistory.clear();
    m_redoHistory.clear();
}

bool EditorUVE::UndoHistoryEntryUVE(HistoryEntryUVE& entry) {
    return std::visit(
        [this](auto& typedEntry) -> bool {
            using EntryType = std::decay_t<decltype(typedEntry)>;
            if constexpr (std::is_same_v<EntryType, TransformHistoryEntryUVE>) {
                if (!ApplyLocalTransformUVE(typedEntry.entity, typedEntry.before)) {
                    return false;
                }
                RestoreSelectionUVE(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                return true;
            } else if constexpr (std::is_same_v<EntryType, NameHistoryEntryUVE>) {
                if (!ApplyEntityNameStateUVE(typedEntry.entity, typedEntry.beforeName)) {
                    return false;
                }
                RestoreSelectionUVE(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                return true;
            } else if constexpr (std::is_same_v<EntryType, PrimitiveAppearanceHistoryEntryUVE>) {
                if (!ApplyPrimitiveMeshStateUVE(typedEntry.entity, typedEntry.before)) {
                    return false;
                }
                RestoreSelectionUVE(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                return true;
            } else if constexpr (std::is_same_v<EntryType, SceneComponentHistoryEntryUVE>) {
                if (!ApplySceneComponentStateUVE(typedEntry.entity, typedEntry.kind, typedEntry.before)) {
                    return false;
                }
                if (typedEntry.before.has_value() != typedEntry.after.has_value()) {
                    InvalidateHierarchyFilterCacheUVE();
                }
                RestoreSelectionUVE(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                return true;
            } else if constexpr (std::is_same_v<EntryType, ComponentPropertyHistoryEntryUVE>) {
                if (!ApplyComponentPropertySnapshotUVE(typedEntry.entity, typedEntry.metadata,
                                                       typedEntry.before.GetUVE())) {
                    return false;
                }
                RestoreSelectionUVE(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                return true;
            } else if constexpr (std::is_same_v<EntryType, MultiComponentPropertyHistoryEntryUVE>) {
                // Every holder checked before any is touched, so a stale holder refuses the whole
                // step instead of leaving half the selection rewound.
                for (const MultiComponentPropertyEditUVE& edit : typedEntry.edits) {
                    if (!IsDocumentEntityUVE(edit.entity)) {
                        return false;
                    }
                }
                for (const MultiComponentPropertyEditUVE& edit : typedEntry.edits) {
                    if (!ApplyComponentPropertySnapshotUVE(edit.entity, typedEntry.metadata,
                                                           edit.before.GetUVE())) {
                        return false;
                    }
                }
                RestoreSelectionUVE(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                return true;
            } else if constexpr (std::is_same_v<EntryType, CreationHistoryEntryUVE>) {
                if (!IsDocumentEntityUVE(typedEntry.activeEntity)) {
                    return false;
                }
                DestroyDocumentSubtreeUVE(typedEntry.activeEntity);
                typedEntry.activeEntity = Scene::kInvalidEntityUVE;
                RestoreSelectionUVE(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                return true;
            } else if constexpr (std::is_same_v<EntryType, SceneObjectCreationHistoryEntryUVE>) {
                if (!IsDocumentEntityUVE(typedEntry.activeEntity)) {
                    return false;
                }
                DestroyDocumentSubtreeUVE(typedEntry.activeEntity);
                typedEntry.activeEntity = Scene::kInvalidEntityUVE;
                RestoreSelectionUVE(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                return true;
            } else if constexpr (std::is_same_v<EntryType, DuplicationHistoryEntryUVE>) {
                if (!IsDocumentEntityUVE(typedEntry.activeEntity)) {
                    return false;
                }
                DestroyDocumentSubtreeUVE(typedEntry.activeEntity);
                typedEntry.activeEntity = Scene::kInvalidEntityUVE;
                RestoreSelectionUVE(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                return true;
            } else if constexpr (std::is_same_v<EntryType, DeletionHistoryEntryUVE>) {
                if (typedEntry.activeEntity != Scene::kInvalidEntityUVE &&
                    m_services->GetEntityManagerUVE().IsAliveUVE(typedEntry.activeEntity)) {
                    return false;
                }
                const Scene::EntityUVE restored = RestoreSubtreeUnderParentUVE(typedEntry.snapshot, typedEntry.originalParent);
                if (restored == Scene::kInvalidEntityUVE) {
                    return false;
                }
                static_cast<void>(m_services->GetSceneGraphUVE().SetSiblingIndexUVE(
                    m_services->GetEntityManagerUVE(), restored, typedEntry.siblingIndex));
                typedEntry.activeEntity = restored;
                typedEntry.selectionBefore = EditorSelectionSnapshotUVE{{restored}, restored};
                RestoreSelectionUVE(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                return true;
            } else if constexpr (std::is_same_v<EntryType, SceneObjectTypeChangeHistoryEntryUVE>) {
                if (!ApplySceneObjectTypeChangeUVE(typedEntry, false)) {
                    return false;
                }
                InvalidateHierarchyFilterCacheUVE();
                RestoreSelectionUVE(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                return true;
            } else {
                // A move among siblings keeps its parent, so it needs neither a reparent nor a
                // transform - and a pure Object, which has no transform, can make one.
                const bool reparented = typedEntry.parentBefore != typedEntry.parentAfter;
                if (reparented ? (!IsReparentableObjectUVE(typedEntry.entity) ||
                                  (typedEntry.parentBefore != Scene::kInvalidEntityUVE &&
                                   !IsHierarchyObjectUVE(typedEntry.parentBefore)) ||
                                  DoesSubtreeContainEntityUVE(typedEntry.entity, typedEntry.parentBefore))
                               : !IsHierarchyObjectUVE(typedEntry.entity)) {
                    return false;
                }
                if (reparented) {
                    m_services->GetSceneGraphUVE().SetParentUVE(
                        m_services->GetEntityManagerUVE(), typedEntry.entity, typedEntry.parentBefore);
                    if (HasSceneGraphObjectUVE(typedEntry.entity) &&
                        !ApplyLocalTransformUVE(typedEntry.entity, typedEntry.localTransformBefore)) {
                        return false;
                    }
                }
                static_cast<void>(m_services->GetSceneGraphUVE().SetSiblingIndexUVE(
                    m_services->GetEntityManagerUVE(), typedEntry.entity, typedEntry.siblingIndexBefore));
                RestoreSelectionUVE(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                InvalidateHierarchyFilterCacheUVE();
                return true;
            }
        },
        entry);
}

bool EditorUVE::RedoHistoryEntryUVE(HistoryEntryUVE& entry) {
    return std::visit(
        [this](auto& typedEntry) -> bool {
            using EntryType = std::decay_t<decltype(typedEntry)>;
            if constexpr (std::is_same_v<EntryType, TransformHistoryEntryUVE>) {
                if (!ApplyLocalTransformUVE(typedEntry.entity, typedEntry.after)) {
                    return false;
                }
                RestoreSelectionUVE(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                return true;
            } else if constexpr (std::is_same_v<EntryType, NameHistoryEntryUVE>) {
                if (!ApplyEntityNameStateUVE(typedEntry.entity, typedEntry.afterName)) {
                    return false;
                }
                RestoreSelectionUVE(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                return true;
            } else if constexpr (std::is_same_v<EntryType, PrimitiveAppearanceHistoryEntryUVE>) {
                if (!ApplyPrimitiveMeshStateUVE(typedEntry.entity, typedEntry.after)) {
                    return false;
                }
                RestoreSelectionUVE(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                return true;
            } else if constexpr (std::is_same_v<EntryType, SceneComponentHistoryEntryUVE>) {
                if (!ApplySceneComponentStateUVE(typedEntry.entity, typedEntry.kind, typedEntry.after)) {
                    return false;
                }
                if (typedEntry.before.has_value() != typedEntry.after.has_value()) {
                    InvalidateHierarchyFilterCacheUVE();
                }
                RestoreSelectionUVE(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                return true;
            } else if constexpr (std::is_same_v<EntryType, ComponentPropertyHistoryEntryUVE>) {
                if (!ApplyComponentPropertySnapshotUVE(typedEntry.entity, typedEntry.metadata,
                                                       typedEntry.after.GetUVE())) {
                    return false;
                }
                RestoreSelectionUVE(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                return true;
            } else if constexpr (std::is_same_v<EntryType, MultiComponentPropertyHistoryEntryUVE>) {
                for (const MultiComponentPropertyEditUVE& edit : typedEntry.edits) {
                    if (!IsDocumentEntityUVE(edit.entity)) {
                        return false;
                    }
                }
                for (const MultiComponentPropertyEditUVE& edit : typedEntry.edits) {
                    if (!ApplyComponentPropertySnapshotUVE(edit.entity, typedEntry.metadata,
                                                           edit.after.GetUVE())) {
                        return false;
                    }
                }
                RestoreSelectionUVE(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                return true;
            } else if constexpr (std::is_same_v<EntryType, CreationHistoryEntryUVE>) {
                if (typedEntry.activeEntity != Scene::kInvalidEntityUVE &&
                    m_services->GetEntityManagerUVE().IsAliveUVE(typedEntry.activeEntity)) {
                    return false;
                }
                const Scene::EntityUVE recreated =
                    CreateDocumentEntityInternalUVE(typedEntry.kind, std::optional<std::string>{typedEntry.name});
                if (recreated == Scene::kInvalidEntityUVE) {
                    return false;
                }
                typedEntry.activeEntity = recreated;
                typedEntry.selectionAfter = EditorSelectionSnapshotUVE{{recreated}, recreated};
                RestoreSelectionUVE(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                return true;
            } else if constexpr (std::is_same_v<EntryType, SceneObjectCreationHistoryEntryUVE>) {
                if (typedEntry.activeEntity != Scene::kInvalidEntityUVE &&
                    m_services->GetEntityManagerUVE().IsAliveUVE(typedEntry.activeEntity)) {
                    return false;
                }
                Scene::EntityUVE redoParent = typedEntry.createdUnderParent;
                if (redoParent == Scene::kInvalidEntityUVE || !IsDocumentEntityUVE(redoParent)) {
                    // The original parent is gone (deleted by later history, say) - re-home
                    // under the Object rather than dropping the object to document top level.
                    redoParent = EnsureDocumentObjectUVE();
                }
                const Scene::EntityUVE restored =
                    RestoreSubtreeUnderParentUVE(typedEntry.snapshot, redoParent);
                if (restored == Scene::kInvalidEntityUVE) {
                    return false;
                }
                typedEntry.activeEntity = restored;
                typedEntry.selectionAfter = EditorSelectionSnapshotUVE{{restored}, restored};
                RestoreSelectionUVE(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                InvalidateHierarchyFilterCacheUVE();
                return true;
            } else if constexpr (std::is_same_v<EntryType, DuplicationHistoryEntryUVE>) {
                if (typedEntry.activeEntity != Scene::kInvalidEntityUVE &&
                    m_services->GetEntityManagerUVE().IsAliveUVE(typedEntry.activeEntity)) {
                    return false;
                }
                const Scene::EntityUVE restored = RestoreSubtreeUnderParentUVE(typedEntry.snapshot, typedEntry.originalParent);
                if (restored == Scene::kInvalidEntityUVE) {
                    return false;
                }
                if (typedEntry.duplicateRootName.has_value() &&
                    !ApplyEntityNameStateUVE(restored, typedEntry.duplicateRootName)) {
                    DestroyDocumentSubtreeUVE(restored);
                    return false;
                }
                static_cast<void>(m_services->GetSceneGraphUVE().SetSiblingIndexUVE(
                    m_services->GetEntityManagerUVE(), restored, typedEntry.siblingIndex));
                typedEntry.activeEntity = restored;
                typedEntry.selectionAfter = EditorSelectionSnapshotUVE{{restored}, restored};
                RestoreSelectionUVE(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                return true;
            } else if constexpr (std::is_same_v<EntryType, DeletionHistoryEntryUVE>) {
                if (!IsDocumentEntityUVE(typedEntry.activeEntity)) {
                    return false;
                }
                DestroyDocumentSubtreeUVE(typedEntry.activeEntity);
                typedEntry.activeEntity = Scene::kInvalidEntityUVE;
                RestoreSelectionUVE(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                return true;
            } else if constexpr (std::is_same_v<EntryType, SceneObjectTypeChangeHistoryEntryUVE>) {
                if (!ApplySceneObjectTypeChangeUVE(typedEntry, true)) {
                    return false;
                }
                InvalidateHierarchyFilterCacheUVE();
                RestoreSelectionUVE(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                return true;
            } else {
                const bool reparented = typedEntry.parentBefore != typedEntry.parentAfter;
                if (reparented ? (!IsReparentableObjectUVE(typedEntry.entity) ||
                                  (typedEntry.parentAfter != Scene::kInvalidEntityUVE &&
                                   !IsHierarchyObjectUVE(typedEntry.parentAfter)) ||
                                  DoesSubtreeContainEntityUVE(typedEntry.entity, typedEntry.parentAfter))
                               : !IsHierarchyObjectUVE(typedEntry.entity)) {
                    return false;
                }
                if (reparented) {
                    m_services->GetSceneGraphUVE().SetParentUVE(
                        m_services->GetEntityManagerUVE(), typedEntry.entity, typedEntry.parentAfter);
                    if (HasSceneGraphObjectUVE(typedEntry.entity) &&
                        !ApplyLocalTransformUVE(typedEntry.entity, typedEntry.localTransformAfter)) {
                        return false;
                    }
                }
                static_cast<void>(m_services->GetSceneGraphUVE().SetSiblingIndexUVE(
                    m_services->GetEntityManagerUVE(), typedEntry.entity, typedEntry.siblingIndexAfter));
                RestoreSelectionUVE(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                InvalidateHierarchyFilterCacheUVE();
                return true;
            }
        },
        entry);
}

bool EditorUVE::IsStructuralRootUVE(const Scene::EntityUVE entity) {
    return IsObjectRootEntityUVE(entity) ||
           (entity != Scene::kInvalidEntityUVE && entity == GetDocumentViewportUVE()) ||
           (m_entityEditSession.has_value() && entity != Scene::kInvalidEntityUVE && entity == GetEntityEditorRootUVE());
}

bool EditorUVE::IsObjectRootEntityUVE(const Scene::EntityUVE entity) const {
    return entity != Scene::kInvalidEntityUVE && m_services->GetEntityManagerUVE().IsAliveUVE(entity) &&
           m_services->GetEntityManagerUVE().HasComponentUVE<Scene::ObjectComponentUVE>(entity);
}

Scene::EntityUVE EditorUVE::GetDocumentObjectUVE() {
    Scene::EntityUVE found = Scene::kInvalidEntityUVE;
    m_services->GetEntityManagerUVE().ForEachUVE<Scene::ObjectComponentUVE>(
        [&found](const Scene::EntityUVE entity, const Scene::ObjectComponentUVE&) {
            if (found == Scene::kInvalidEntityUVE) {
                found = entity; // markers are guarded to exactly one; first hit is THE root
            }
        });
    return found;
}

Scene::EntityUVE EditorUVE::EnsureDocumentObjectUVE() {
    const Scene::EntityUVE existing = GetDocumentObjectUVE();
    if (existing != Scene::kInvalidEntityUVE) {
        return existing;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const Scene::EntityUVE root =
        CreateDocumentEntityShellInternalUVE(Scene::ObjectDefinitionUVE::defaultName);
    if (root == Scene::kInvalidEntityUVE) {
        return Scene::kInvalidEntityUVE;
    }
    Scene::ApplyObjectDefinitionUVE(entityManager, root, Scene::ObjectDefinitionUVE{});
    return root;
}

std::vector<Scene::EntityUVE> EditorUVE::GetDocumentRootsUVE() {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    std::vector<Scene::EntityUVE> roots =
        m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, Scene::kInvalidEntityUVE);
    // Internal engine/editor infrastructure entities (e.g. the Viewport's hidden free-look-camera
    // proxy) are also Objects (AttachTransformUVE always creates one), but must never be
    // treated as document content - this is the one place that decision needs to be made, since
    // every other document-root consumer (Play-mode snapshot capture/restore, the Scene Hierarchy
    // panel, ClearDocumentSceneUVE, etc.) already reaches roots exclusively through this function.
    roots.erase(std::remove_if(roots.begin(), roots.end(),
                               [&entityManager](const Scene::EntityUVE entity) {
                                   return entityManager.HasComponentUVE<Scene::EditorInternalEntityComponentUVE>(
                                       entity);
                               }),
               roots.end());
    // The Object leads. The entity manager iterates in archetype order, which it documents as
    // unspecified and which moves whenever an object's component set changes; without this the root
    // could sit anywhere among stray top-level entities, in the outliner, in the reparent list, and
    // in the order a Play-mode snapshot is captured and restored.
    std::stable_partition(roots.begin(), roots.end(), [&entityManager](const Scene::EntityUVE entity) {
        return entityManager.HasComponentUVE<Scene::ObjectComponentUVE>(entity);
    });
    return roots;
}

std::vector<Scene::EntityUVE> EditorUVE::SortHierarchyRowsUVE(
    std::vector<Scene::EntityUVE> entities) const {
    const HierarchySortModeUVE sortMode = m_hierarchyView.sortMode;
    if (sortMode == HierarchySortModeUVE::SceneOrder || entities.size() < 2U) {
        return entities;
    }

    // The Object is a structural anchor rather than an authored sibling; keep it first even when
    // loose document roots are sorted. All other groups are presentation-only stable sorts.
    std::size_t firstSortable =
        !entities.empty() && IsObjectRootEntityUVE(entities.front()) ? 1U : 0U;
    struct SortKeyUVE final {
        Scene::EntityUVE entity;
        std::string name;
        std::string type;
    };
    std::vector<SortKeyUVE> keys;
    keys.reserve(entities.size() - firstSortable);
    for (std::size_t index = firstSortable; index < entities.size(); ++index) {
        const Scene::EntityUVE entity = entities[index];
        keys.push_back(SortKeyUVE{entity, GetEntityDisplayLabelUVE(entity),
                                  std::string{GetObjectTypeNameUVE(entity)}});
    }
    std::stable_sort(keys.begin(), keys.end(), [sortMode](const SortKeyUVE& left, const SortKeyUVE& right) {
        return CompareHierarchySortKeysUVE(sortMode, left.name, left.type, right.name, right.type) < 0;
    });
    for (std::size_t index = 0U; index < keys.size(); ++index) {
        entities[firstSortable + index] = keys[index].entity;
    }
    return entities;
}

std::vector<Scene::EntityUVE> EditorUVE::GetHierarchyChildrenInViewOrderUVE(
    const Scene::EntityUVE parent) const {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    return SortHierarchyRowsUVE(m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, parent));
}

EditorStateUVE EditorUVE::GetStateUVE() const noexcept {
    return m_state;
}

Scene::EntityUVE EditorUVE::GetSelectedEntityUVE() const noexcept {
    return m_selectedEntity;
}

Scene::EntityUVE EditorUVE::GetPreviewCameraUVE() const noexcept {
    if (m_previewCamera == Scene::kInvalidEntityUVE || m_services == nullptr) {
        return Scene::kInvalidEntityUVE;
    }
    if (!Scene::IsDocumentCameraEntityUVE(m_services->GetEntityManagerUVE(), m_previewCamera)) {
        return Scene::kInvalidEntityUVE;
    }
    return m_previewCamera;
}

void EditorUVE::SetPreviewCameraUVE(const Scene::EntityUVE entity) {
    if (m_services == nullptr || !Scene::IsDocumentCameraEntityUVE(m_services->GetEntityManagerUVE(), entity)) {
        return;
    }
    m_previewCamera = entity;
}

void EditorUVE::ClearPreviewCameraUVE() noexcept {
    m_previewCamera = Scene::kInvalidEntityUVE;
}

namespace {

[[nodiscard]] bool IsFiniteVector3UVE(const Math::Vector3UVE& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool IsViewportBookmarkWellFormedUVE(const EditorViewportBookmarkUVE& bookmark) noexcept {
    return IsFiniteVector3UVE(bookmark.target) && std::isfinite(bookmark.yawRadians) &&
           std::isfinite(bookmark.pitchRadians) && std::isfinite(bookmark.distance) &&
           bookmark.distance > 0.0F;
}

} // namespace

bool EditorUVE::SetViewportBookmarkUVE(const std::size_t slot,
                                       const EditorViewportBookmarkUVE& bookmark) noexcept {
    // The clamping stays with the camera on apply (see ResolveOrbitBookmarkFromLookUVE's doc):
    // rejecting a straight-pole pose here would throw away a perfectly valid look direction for
    // a UI-level preference, and callers composing from real look vectors should not have to
    // special-case the poles.
    if (slot >= kEditorViewportBookmarkSlotCountUVE || !IsViewportBookmarkWellFormedUVE(bookmark)) {
        return false;
    }
    m_viewportBookmarks[slot] = bookmark;
    return true;
}

std::optional<EditorViewportBookmarkUVE> EditorUVE::GetViewportBookmarkUVE(
    const std::size_t slot) const noexcept {
    if (slot >= kEditorViewportBookmarkSlotCountUVE) {
        return std::nullopt;
    }
    return m_viewportBookmarks[slot];
}

bool EditorUVE::ClearViewportBookmarkUVE(const std::size_t slot) noexcept {
    if (slot >= kEditorViewportBookmarkSlotCountUVE) {
        return false;
    }
    const bool wasOccupied = m_viewportBookmarks[slot].has_value();
    m_viewportBookmarks[slot].reset();
    return wasOccupied;
}

std::optional<EditorViewportBookmarkUVE> EditorUVE::ComposeMarker3DFocusBookmarkUVE(
    const Scene::EntityUVE entity) const {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::Marker3DComponentUVE>(entity) ||
        !entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
        return std::nullopt;
    }
    const Scene::Marker3DComponentUVE& marker =
        entityManager.GetComponentUVE<Scene::Marker3DComponentUVE>(entity);
    if (!marker.enabled || !Scene::IsMarker3DObjectComponentValidUVE(marker)) {
        return std::nullopt;
    }
    const Scene::WorldTransformComponentUVE& worldTransform =
        entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
    Math::QuaternionUVE worldRotation{};
    if (!Math::TryNormalizeUVE(worldTransform.worldRotation, worldRotation)) {
        return std::nullopt; // a degenerate object rotation gives no meaningful viewpoint
    }
    const std::optional<Scene::Marker3DPoseUVE> pose = Scene::ComposeMarker3DPoseUVE(
        worldTransform.worldPosition, worldRotation, marker.localPosition, marker.localRotation);
    if (!pose.has_value()) {
        return std::nullopt;
    }
    // The engine camera convention looks down -Z (see the SpringArm3D module doc), so the
    // marker's facing is its composed rotation applied to -Z.
    const Math::Vector3UVE forward =
        Math::RotateVectorUVE(pose->rotation, Math::Vector3UVE{0.0F, 0.0F, -1.0F});
    return ResolveOrbitBookmarkFromLookUVE(pose->position, forward, kEditorMarkerFocusDistanceUVE);
}

std::optional<Math::Vector3UVE> EditorUVE::ResolveEntityFocusTargetUVE(
    const Scene::EntityUVE entity) const {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
        return std::nullopt;
    }
    const Math::Vector3UVE& worldPosition =
        entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity).worldPosition;
    if (!IsFiniteVector3UVE(worldPosition)) {
        return std::nullopt;
    }
    return worldPosition;
}

std::optional<EditorViewportBookmarkUVE> EditorUVE::ResolveOrbitBookmarkFromLookUVE(
    const Math::Vector3UVE& eye, const Math::Vector3UVE& forward, const float distance) noexcept {
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
    if (!IsFiniteVector3UVE(eye) || !IsFiniteVector3UVE(forward) || !std::isfinite(distance) ||
        distance <= 0.0F) {
        return std::nullopt;
    }
    const float forwardLengthSquared = Math::LengthSquaredUVE(forward);
    if (forwardLengthSquared < kDegenerateForwardEpsilon * kDegenerateForwardEpsilon) {
        return std::nullopt;
    }
    const float forwardLength = std::sqrt(forwardLengthSquared);
    const Math::Vector3UVE direction = forward * (1.0F / forwardLength);

    // OrbitCamera: eye = target + offset(yaw,pitch) * distance with offset =
    // (cos(yaw)cos(pitch), sin(pitch), sin(yaw)cos(pitch)); the look direction (target - eye)
    // is therefore -offset, i.e. sin(pitch) = -dir.y and (cos,sin)(yaw)*cos(pitch) = (-x,-z).
    EditorViewportBookmarkUVE bookmark{};
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

Editor2DCanvasStateUVE EditorUVE::Get2DCanvasStateUVE() const noexcept {
    return m_2dCanvasState;
}
bool EditorUVE::Set2DCanvasZoomUVE(const float zoom) noexcept {
    if (!std::isfinite(zoom) || zoom < kMinimum2DCanvasZoomUVE || zoom > kMaximum2DCanvasZoomUVE) {
        return false;
    }
    m_2dCanvasState.zoom = zoom;
    return true;
}
void EditorUVE::Reset2DCanvasViewUVE() noexcept {
    m_2dCanvasState = Editor2DCanvasStateUVE{};
    m_2dCanvasPanning = false;
}
bool EditorUVE::IsSceneDirtyUVE() const noexcept {

    return m_sceneDirty;
}

EditorToolSessionPhaseUVE EditorUVE::GetToolSessionPhaseUVE() const noexcept {
    return m_toolSession.GetPhaseUVE();
}

EditorToolSessionOutcomeUVE EditorUVE::GetLastToolSessionOutcomeUVE() const noexcept {
    return m_toolSession.GetLastOutcomeUVE();
}

const std::optional<Asset::AssetRecordUVE>& EditorUVE::GetSelectedAssetUVE() const noexcept {
    return m_selectedAsset;
}

const std::optional<Asset::ProjectFileEntryUVE>& EditorUVE::GetSelectedProjectFileUVE() const noexcept {
    return m_selectedProjectFile;
}

const std::string& EditorUVE::GetAssetFilterUVE() const noexcept {
    return m_assetFilter;
}

const std::filesystem::path& EditorUVE::GetActiveScenePathUVE() const noexcept {
    return m_activeScenePath;
}

void EditorUVE::SetActiveScenePathUVE(std::filesystem::path path) {
    if (!path.empty()) {
        m_activeScenePath = std::move(path);
    }
}

bool EditorUVE::OpenScriptGraphForEntityUVE(const Scene::EntityUVE entity) {
    return OpenUVScriptForEntityUVE(entity);
}

bool EditorUVE::WriteProjectTextFileUVE(const std::filesystem::path& path, const std::string_view text) {
    const auto writeAtomicallyUVE = [text](const std::filesystem::path& target) {
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
    Asset::IFileSystemUVE& fileSystem = m_services->GetFileSystemUVE();
    const std::filesystem::path resolvedPath = fileSystem.ResolveRealPathUVE(virtualPath);
    if (!resolvedPath.empty()) {
        return writeAtomicallyUVE(resolvedPath);
    }
    std::vector<std::byte> bytes(text.size());
    if (!text.empty()) {
        std::memcpy(bytes.data(), text.data(), text.size());
    }
    if (fileSystem.WriteFileUVE(virtualPath, bytes)) {
        return true;
    }
    // Some editor test/legacy configurations have no mounted project directory. Preserve the
    // established raw-path behavior as a compatibility fallback after the native VFS attempt.
    return writeAtomicallyUVE(path);
}

std::optional<std::string> EditorUVE::ReadProjectTextFileUVE(const std::filesystem::path& path) const {
    // Asked first, because a VFS read that misses logs an error - and "is this name free?" is an
    // ordinary question here, not a failure.
    const Asset::IFileSystemUVE& fileSystem = m_services->GetFileSystemUVE();
    if (const std::optional<std::vector<std::byte>> bytes =
            fileSystem.HasFileUVE(path.generic_string()) ? fileSystem.ReadFileUVE(path.generic_string()) : std::nullopt;
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


void EditorUVE::SetColorPickerPreferencesUVE(ColorPickerPreferencesUVE preferences) {
    const bool previousAdvancedOpen = m_colorPickerPreferences.advancedOpen;
    const ColorPickerRgbDisplayUVE previousRgbDisplay = m_colorPickerPreferences.rgbDisplay;
    const auto sanitize = [](std::vector<EditorColorUVE>& colors, const std::size_t cap) {
        std::erase_if(colors, [](const EditorColorUVE& color) {
            return !std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b) ||
                   !std::isfinite(color.a);
        });
        for (EditorColorUVE& color : colors) {
            color = EditorColorUVE{std::clamp(color.r, 0.0F, 1.0F), std::clamp(color.g, 0.0F, 1.0F),
                                   std::clamp(color.b, 0.0F, 1.0F), std::clamp(color.a, 0.0F, 1.0F)};
        }
        if (colors.size() > cap) {
            colors.resize(cap);
        }
    };
    sanitize(preferences.saved, kMaxSavedColorsUVE);
    sanitize(preferences.recents, kMaxRecentColorsUVE);
    if (preferences.rgbDisplay != ColorPickerRgbDisplayUVE::ZeroToOne &&
        preferences.rgbDisplay != ColorPickerRgbDisplayUVE::ZeroTo255) {
        preferences.rgbDisplay = ColorPickerRgbDisplayUVE::ZeroToOne;
    }
    m_colorPickerPreferences = std::move(preferences);
    NotifyEditorSettingChangedUVE(EditorSettingIdUVE::kColorPickerAdvancedOpenUVE, previousAdvancedOpen,
                                  m_colorPickerPreferences.advancedOpen);
    // Unlike the Advanced flag above, only a real change notifies: the saved/recent bindings
    // rebuild the whole struct on every load, and that must not look like a display change.
    if (m_colorPickerPreferences.rgbDisplay != previousRgbDisplay) {
        NotifyEditorSettingChangedUVE(EditorSettingIdUVE::kColorPickerRgbDisplayUVE,
                                      static_cast<std::int64_t>(previousRgbDisplay),
                                      static_cast<std::int64_t>(m_colorPickerPreferences.rgbDisplay));
    }
}

void EditorUVE::ShutdownUVE() {
    if (m_state == EditorStateUVE::Shutdown || m_state == EditorStateUVE::Uninitialized) {
        return;
    }
    // The pause-on-error sink stays on the logger (there is no unregister); the editor only
    // stops reading it.
    m_playPauseOnErrorSink = nullptr;
    CancelHierarchyReparentUVE();
    m_hierarchyReparentPopupWasOpened = false;
    static_cast<void>(CommitComponentPropertyPreviewUVE());
    // An open entity is closed without saving (nothing here can ask), so the scene is back in
    // place for whatever runs after the editor.
    if (m_entityEditSession.has_value()) {
        static_cast<void>(CloseEntityEditorUVE(false));
    }
    CloseRetargetWindowUVE();

    // An interactive session keeps its editor preferences (panels, snapping, grid, Inspector folds,
    // favourites) without the author having to remember "Save Editor Preferences" first. Headless
    // runs - tests, tools - have no UI and leave the stored preferences alone.
    if (m_uiInitialized) {
        static_cast<void>(SaveSessionSettingsUVE());
    }
    static_cast<void>(SaveSharedShelvesUVE());
    // Project settings and the input map changed in this session belong to the project, whatever
    // ran the editor.
    static_cast<void>(SaveProjectSettingsUVE());
    static_cast<void>(SaveInputMapUVE());

    if (m_playModeState != EditorPlayModeStateUVE::Edit) {
        if (!StopPlayModeUVE() && m_simulationControl != nullptr) {
            static_cast<void>(m_simulationControl->SetSimulationExecutionModeUVE(
                Core::SimulationExecutionModeUVE::Running));
            static_cast<void>(m_simulationControl->SetTransientSimulationSessionActiveUVE(false));
            m_playModeSession.reset();
            m_playModeState = EditorPlayModeStateUVE::Edit;
        }
    }
    if (m_uiInitialized) {
        ClearTextureThumbnailCacheUVE();
        ClearMeshThumbnailCacheUVE();
        m_meshThumbnailRenderer.ShutdownUVE();
        m_uiAssets.ShutdownUVE();
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        m_uiInitialized = false;
    }

    ClearSelectionUVE();
    ClearHistoryUVE();
    m_lockedHierarchyEntities.clear();
    m_entityClipboard.reset();
    m_clipboardText.clear();
    m_state = EditorStateUVE::Shutdown;
}

bool EditorUVE::IsDocumentEntityUVE(const Scene::EntityUVE entity) const noexcept {
    return entity != Scene::kInvalidEntityUVE && m_services->GetEntityManagerUVE().IsAliveUVE(entity);
}

bool EditorUVE::HasSceneGraphObjectUVE(const Scene::EntityUVE entity) const noexcept {
    if (!IsDocumentEntityUVE(entity)) {
        return false;
    }

    const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    return entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity) &&
           entityManager.HasComponentUVE<Scene::HierarchyComponentUVE>(entity) &&
           entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity);
}

bool EditorUVE::IsReparentableObjectUVE(const Scene::EntityUVE entity) const noexcept {
    // A spatial object carries the full transform set; a pure Object (AnimationSequencer, the Object)
    // carries none of it. Anything in between is a half-built entity and is refused.
    if (HasSceneGraphObjectUVE(entity)) {
        return true;
    }
    const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    return IsHierarchyObjectUVE(entity) && !entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity) &&
           !entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity);
}

bool EditorUVE::IsHierarchyObjectUVE(const Scene::EntityUVE entity) const noexcept {
    return IsDocumentEntityUVE(entity) &&
           m_services->GetEntityManagerUVE().HasComponentUVE<Scene::HierarchyComponentUVE>(entity);
}

std::optional<Scene::WorldTransformComponentUVE> EditorUVE::TryGetComposingParentWorldUVE(
    const Scene::EntityUVE parent) const {
    // Identity, already resolved: what a child with no parent, or under a pure Object, composes from.
    const Scene::WorldTransformComponentUVE identity{
        Math::Vector3UVE{}, Math::QuaternionUVE{}, Math::Vector3UVE{1.0F, 1.0F, 1.0F}, false};
    if (parent == Scene::kInvalidEntityUVE) {
        return identity;
    }
    if (!IsDocumentEntityUVE(parent)) {
        return std::nullopt;
    }
    const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    // Spatial means both halves, exactly as the scene graph's sweep decides it.
    if (!entityManager.HasComponentUVE<Scene::TransformComponentUVE>(parent) ||
        !entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(parent)) {
        return identity;
    }
    return entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(parent);
}

bool EditorUVE::IsEntityNameValidUVE(const std::string_view name) const noexcept {
    return !name.empty() && name.size() <= kMaximumEntityNameBytesUVE && !IsWhitespaceOnlyUVE(name);
}

std::string EditorUVE::GetEntityDisplayLabelUVE(const Scene::EntityUVE entity) const {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (entityManager.IsAliveUVE(entity) && entityManager.HasComponentUVE<Scene::NameComponentUVE>(entity)) {
        const std::string& name = entityManager.GetComponentUVE<Scene::NameComponentUVE>(entity).name;
        if (!name.empty()) {
            return name;
        }
    }
    return EntityLabelUVE(entity);
}

std::string EditorUVE::GetDefaultEntityNameUVE(const EditorEntityKindUVE kind) const {
    // The names themselves live with each object kind's definition in Engine/Runtime/Objects/3D —
    // this legacy-kind mapper only picks which definition to ask, never authors a name itself.
    switch (kind) {
        case EditorEntityKindUVE::Empty:
            return std::string{Scene::Object3DObjectDefinitionUVE::defaultName};
        case EditorEntityKindUVE::Camera:
            return std::string{Scene::Camera3DObjectDefinitionUVE::defaultName};
        case EditorEntityKindUVE::DirectionalLight:
            return std::string{Scene::Light3DObjectDefinitionUVE::defaultName};
        case EditorEntityKindUVE::CollisionBox:
            return std::string{Scene::Collider3DObjectDefinitionUVE::defaultName};
        case EditorEntityKindUVE::Cube:
            return std::string{Scene::BoxMesh3DObjectDefinitionUVE::defaultName};
        case EditorEntityKindUVE::UVSphere:
            return std::string{Scene::SphereMesh3DObjectDefinitionUVE::defaultName};
        case EditorEntityKindUVE::Plane:
            return std::string{Scene::PlaneMesh3DObjectDefinitionUVE::defaultName};
    }
    return {};
}

std::string EditorUVE::MakeUniqueDocumentEntityNameUVE(const std::string_view baseName,
                                                        const Scene::EntityUVE ignoredEntity) const {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    std::vector<std::string> names;
    entityManager.ForEachUVE<Scene::NameComponentUVE>(
        [this, &names, ignoredEntity](const Scene::EntityUVE entity, Scene::NameComponentUVE& component) {
            if (entity != ignoredEntity && IsDocumentEntityUVE(entity)) {
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
        const std::string candidate =
            FormatDuplicateNameUVE(baseName, suffix, m_hierarchyView.duplicateNameSuffix);
        if (!isUsed(candidate)) {
            return candidate;
        }
    }
}

bool EditorUVE::IsTransformFiniteUVE(const Scene::TransformComponentUVE& transform) const noexcept {
    return IsFiniteVectorUVE(transform.localPosition) && IsFiniteUVE(transform.localRotation.x) &&
           IsFiniteUVE(transform.localRotation.y) && IsFiniteUVE(transform.localRotation.z) &&
           IsFiniteUVE(transform.localRotation.w) && IsFiniteVectorUVE(transform.localScale);
}

bool EditorUVE::IsQuaternionFiniteUVE(const Math::QuaternionUVE& quaternion) const noexcept {
    return Math::IsFiniteUVE(quaternion);
}

bool EditorUVE::AreTransformSnappingSettingsValidUVE(
    const EditorTransformSnappingSettingsUVE& settings) const noexcept {
    // Written as "inside the range" so NaN, which compares false, is refused too.
    const auto inRange = [](const float step, const float maximum) {
        return step >= kMinimumTransformSnapStepUVE && step <= maximum;
    };
    return inRange(settings.translateStep, kMaximumTransformSnapTranslateStepUVE) &&
           inRange(settings.rotateStepDegrees, kMaximumTransformSnapRotateStepDegreesUVE) &&
           inRange(settings.scaleStep, kMaximumTransformSnapScaleStepUVE);
}

float EditorUVE::SnapScalarUVE(const float value, const float increment) const noexcept {
    if (!IsFiniteUVE(value) || !IsFiniteUVE(increment) || increment <= kVectorEpsilonUVE) {
        return value;
    }
    const float snapped = std::round(value / increment) * increment;
    return IsFiniteUVE(snapped) ? snapped : value;
}

bool EditorUVE::IsFiniteVectorUVE(const Math::Vector3UVE& vector) const noexcept {
    return IsFiniteUVE(vector.x) && IsFiniteUVE(vector.y) && IsFiniteUVE(vector.z);
}

Math::Vector3UVE EditorUVE::GetAxisVectorUVE(const EditorTransformAxisUVE axis) const noexcept {
    switch (axis) {
        case EditorTransformAxisUVE::X:
            return Math::Vector3UVE{1.0F, 0.0F, 0.0F};
        case EditorTransformAxisUVE::Y:
            return Math::Vector3UVE{0.0F, 1.0F, 0.0F};
        case EditorTransformAxisUVE::Z:
            return Math::Vector3UVE{0.0F, 0.0F, 1.0F};
        case EditorTransformAxisUVE::None:
            return Math::Vector3UVE{};
    }
    return Math::Vector3UVE{};
}

bool EditorUVE::ComputeLocalRotationForWorldAxisUVE(const Scene::EntityUVE entity,
                                                        const Math::QuaternionUVE& initialLocalRotation,
                                                        const Math::Vector3UVE& worldAxis, const float radians,
                                                        Math::QuaternionUVE& outLocalRotation) const {
    if (!IsDocumentEntityUVE(entity) || !IsQuaternionFiniteUVE(initialLocalRotation) ||
        !IsFiniteUVE(radians) || !IsFiniteVectorUVE(worldAxis) ||
        Math::LengthSquaredUVE(worldAxis) <= kVectorEpsilonUVE) {
        return false;
    }

    Math::QuaternionUVE initialNormalized{};
    Math::QuaternionUVE worldDelta{};
    if (!Math::TryNormalizeUVE(initialLocalRotation, initialNormalized) ||
        !Math::TryMakeAxisAngleUVE(worldAxis, radians, worldDelta)) {
        return false;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::HierarchyComponentUVE>(entity)) {
        return false;
    }

    const Scene::HierarchyComponentUVE& hierarchy =
        entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(entity);
    Math::QuaternionUVE localDelta = worldDelta;
    if (hierarchy.parent != Scene::kInvalidEntityUVE) {
        const std::optional<Scene::WorldTransformComponentUVE> composingParent =
            TryGetComposingParentWorldUVE(hierarchy.parent);
        if (!composingParent.has_value()) {
            return false;
        }

        const Scene::WorldTransformComponentUVE& parentWorld = *composingParent;
        Math::QuaternionUVE parentNormalized{};
        Math::QuaternionUVE parentInverse{};
        if (parentWorld.dirty || !Math::TryNormalizeUVE(parentWorld.worldRotation, parentNormalized) ||
            !Math::TryInverseUVE(parentNormalized, parentInverse)) {
            return false;
        }
        localDelta = Math::MultiplyUVE(
            Math::MultiplyUVE(parentInverse, worldDelta), parentNormalized);
    }

    return Math::TryNormalizeUVE(Math::MultiplyUVE(localDelta, initialNormalized), outLocalRotation);
}

bool EditorUVE::ComputeLocalRotationForWorldRotationUVE(const Scene::EntityUVE entity,
                                                        const Math::QuaternionUVE& desiredWorldRotation,
                                                        Math::QuaternionUVE& outLocalRotation) const {
    if (!IsDocumentEntityUVE(entity) || !IsQuaternionFiniteUVE(desiredWorldRotation)) {
        return false;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::HierarchyComponentUVE>(entity)) {
        return false;
    }

    const Scene::HierarchyComponentUVE& hierarchy =
        entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(entity);
    if (hierarchy.parent == Scene::kInvalidEntityUVE) {
        // A root's parent space is the world, so the wanted rotation is already the local one.
        return Math::TryNormalizeUVE(desiredWorldRotation, outLocalRotation);
    }

    const std::optional<Scene::WorldTransformComponentUVE> composingParent =
        TryGetComposingParentWorldUVE(hierarchy.parent);
    if (!composingParent.has_value()) {
        return false;
    }
    const Scene::WorldTransformComponentUVE& parentWorld = *composingParent;
    Math::QuaternionUVE parentNormalized{};
    Math::QuaternionUVE parentInverse{};
    if (parentWorld.dirty || !Math::TryNormalizeUVE(parentWorld.worldRotation, parentNormalized) ||
        !Math::TryInverseUVE(parentNormalized, parentInverse)) {
        return false;
    }
    return Math::TryNormalizeUVE(Math::MultiplyUVE(parentInverse, desiredWorldRotation), outLocalRotation);
}

bool EditorUVE::ComputeLocalDeltaForWorldDeltaUVE(const Scene::EntityUVE entity,
                                                   const Math::Vector3UVE& worldDelta,
                                                   Math::Vector3UVE& outLocalDelta) const {
    if (!IsDocumentEntityUVE(entity) || !IsFiniteVectorUVE(worldDelta)) {
        return false;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::HierarchyComponentUVE>(entity)) {
        return false;
    }

    const Scene::HierarchyComponentUVE& hierarchy =
        entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(entity);
    if (hierarchy.parent == Scene::kInvalidEntityUVE) {
        outLocalDelta = worldDelta;
        return true;
    }

    const std::optional<Scene::WorldTransformComponentUVE> composingParent =
        TryGetComposingParentWorldUVE(hierarchy.parent);
    if (!composingParent.has_value()) {
        return false;
    }

    const Scene::WorldTransformComponentUVE& parentWorld = *composingParent;
    if (parentWorld.dirty || !IsFiniteVectorUVE(parentWorld.worldScale) ||
        std::abs(parentWorld.worldScale.x) <= kVectorEpsilonUVE ||
        std::abs(parentWorld.worldScale.y) <= kVectorEpsilonUVE ||
        std::abs(parentWorld.worldScale.z) <= kVectorEpsilonUVE) {
        return false;
    }

    const Math::Vector3UVE unrotated =
        Math::RotateVectorUVE(ConjugateUVE(parentWorld.worldRotation), worldDelta);
    outLocalDelta = Math::Vector3UVE{
        unrotated.x / parentWorld.worldScale.x,
        unrotated.y / parentWorld.worldScale.y,
        unrotated.z / parentWorld.worldScale.z,
    };
    return IsFiniteVectorUVE(outLocalDelta);
}

void EditorUVE::DestroyDocumentSubtreeUVE(const Scene::EntityUVE root) {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const std::vector<Scene::EntityUVE> children =
        m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, root);
    for (const Scene::EntityUVE child : children) {
        DestroyDocumentSubtreeUVE(child);
    }
    if (entityManager.IsAliveUVE(root)) {
        entityManager.DestroyEntityUVE(root);
    }
}

void EditorUVE::ClearDocumentSceneUVE() {
    const std::vector<Scene::EntityUVE> roots = GetDocumentRootsUVE();
    for (const Scene::EntityUVE root : roots) {
        DestroyDocumentSubtreeUVE(root);
    }
    ClearSelectionUVE();
    // The clipboard describes the document that just went away; a Paste into the next one would
    // insert objects no author copied from it.
    m_entityClipboard.reset();
}

void EditorUVE::ApplyLayoutPresetUVE(const EditorLayoutPresetUVE preset) noexcept {
    switch (preset) {
        case EditorLayoutPresetUVE::Default:
            m_scenePanelVisible = true;
            m_viewportPanelVisible = true;
            m_inspectorPanelVisible = true;
            m_bottomDockVisible = true;
            m_activeRightPanelTab = EditorRightPanelTabUVE::Inspector;
            m_activeBottomDock = EditorBottomDockUVE::FileSystem;
            break;
        case EditorLayoutPresetUVE::FocusViewport:
            m_scenePanelVisible = false;
            m_viewportPanelVisible = true;
            m_inspectorPanelVisible = false;
            m_bottomDockVisible = false;
            break;
        case EditorLayoutPresetUVE::ContentReview:
            m_scenePanelVisible = true;
            m_viewportPanelVisible = true;
            m_inspectorPanelVisible = true;
            m_bottomDockVisible = true;
            m_activeRightPanelTab = EditorRightPanelTabUVE::Import;
            m_activeBottomDock = EditorBottomDockUVE::FileSystem;
            break;
    }
}

void EditorUVE::LoadSessionSettingsUVE() {
    Config::IConfigManagerUVE& config = m_services->GetConfigManagerUVE();
    constexpr std::int64_t kSessionVersion = 1;
    const std::int64_t version = config.GetIntUVE("editor.sessionSettingsVersion", 0);
    if (version > kSessionVersion) {
        return;
    }
    // Aliases marked Deprecated move their legacy values to the replacement ids before the editor
    // reads the current descriptors; legal values already at the new ids win.
    static_cast<void>(m_settingsRegistry.MigrateDeprecatedValuesUVE(config));

    // Inspector folds used to be stored as objects (`<id>.<index>.key/.open`). Convert that
    // legacy shape once to the StringList descriptor's ordered `key`/state records before the
    // registry reads it; current list entries are leaf strings at `<id>.<index>`.
    const std::string foldId{EditorSettingIdUVE::kInspectorFoldsUVE};
    const std::int64_t legacyFoldCount = std::clamp(
        config.GetIntUVE(foldId + ".count", 0), std::int64_t{0},
        static_cast<std::int64_t>(kMaxRememberedInspectorFoldsUVE));
    bool hasLegacyFoldShape = legacyFoldCount > 0 && !config.HasKeyUVE(foldId + ".0");
    for (std::size_t index = 0U; index < kMaxRememberedInspectorFoldsUVE && !hasLegacyFoldShape; ++index) {
        const std::string prefix = foldId + "." + std::to_string(index);
        hasLegacyFoldShape = config.HasKeyUVE(prefix + ".key") || config.HasKeyUVE(prefix + ".open");
    }
    if (hasLegacyFoldShape) {
        Config::SettingStringListUVE encodedFolds;
        encodedFolds.reserve(static_cast<std::size_t>(legacyFoldCount));
        for (std::int64_t index = 0; index < legacyFoldCount; ++index) {
            const std::string prefix = foldId + "." + std::to_string(index) + ".";
            const std::string key = config.GetStringUVE(prefix + "key", "");
            if (!key.empty()) {
                encodedFolds.push_back(std::string{config.GetBoolUVE(prefix + "open", true) ? "1:" : "0:"} + key);
            }
        }
        for (std::size_t index = 0U; index < kMaxRememberedInspectorFoldsUVE; ++index) {
            const std::string prefix = foldId + "." + std::to_string(index);
            static_cast<void>(config.RemoveKeyUVE(prefix + ".key"));
            static_cast<void>(config.RemoveKeyUVE(prefix + ".open"));
        }
        static_cast<void>(m_settingsRegistry.SetValueUVE(config, foldId, encodedFolds));
    }

    // Every value read through the registry is legal for its setting - anything missing, mistyped
    // or out of range comes back as that one setting's default - so each binding applies it directly,
    // without repeating boundary validation in the editor setter.
    for (const Config::SettingDescriptorUVE* descriptor : m_settingsRegistry.GetAllUVE()) {
        // A renamed setting's old name is an alias for the migration above only: its value was
        // just moved to the new name, and applying it again would overwrite what the new name
        // already holds.
        if (descriptor->HasFlagUVE(Config::kSettingFlagDeprecatedUVE)) {
            continue;
        }
        if (const std::optional<Config::SettingValueUVE> value = m_settingsRegistry.GetValueUVE(config, descriptor->id)) {
            static_cast<void>(ApplyValidatedEditorSettingUVE(descriptor->id, *value));
        }
    }
    // The host owns the viewport's axis defaults. Adopt a complete stored palette only; otherwise
    // leave the current state untouched. A new editor starts unset so the host can seed it, while
    // an already-seeded host palette must not be replaced by missing or invalid stored data.
    constexpr std::string_view kLegacyAxisPaletteSetKeyUVE = "editor.viewport.axisColors.set";
    const bool hasLegacyAxisMarker = config.HasKeyUVE(kLegacyAxisPaletteSetKeyUVE);
    const bool shouldRestoreAxisPalette = hasLegacyAxisMarker
                                              ? config.GetBoolUVE(kLegacyAxisPaletteSetKeyUVE, false)
                                              : true;
    if (shouldRestoreAxisPalette) {
        const std::optional<Config::SettingValueUVE> x =
            m_settingsRegistry.GetStoredValueUVE(config, EditorSettingIdUVE::kViewportAxisColorXUVE);
        const std::optional<Config::SettingValueUVE> y =
            m_settingsRegistry.GetStoredValueUVE(config, EditorSettingIdUVE::kViewportAxisColorYUVE);
        const std::optional<Config::SettingValueUVE> z =
            m_settingsRegistry.GetStoredValueUVE(config, EditorSettingIdUVE::kViewportAxisColorZUVE);
        if (x && y && z) {
            const auto toAxisColor = [](const Config::SettingValueUVE& value) {
                const Config::SettingColorUVE& color = std::get<Config::SettingColorUVE>(value);
                return ViewportAxisColorUVE{color.r, color.g, color.b};
            };
            m_viewportOverlayState.axisColorX = toAxisColor(*x);
            m_viewportOverlayState.axisColorY = toAxisColor(*y);
            m_viewportOverlayState.axisColorZ = toAxisColor(*z);
            m_viewportOverlayState.axisColorsValid = true;
        }
    } else {
        static_cast<void>(m_settingsRegistry.ClearValueUVE(config, EditorSettingIdUVE::kViewportAxisColorXUVE));
        static_cast<void>(m_settingsRegistry.ClearValueUVE(config, EditorSettingIdUVE::kViewportAxisColorYUVE));
        static_cast<void>(m_settingsRegistry.ClearValueUVE(config, EditorSettingIdUVE::kViewportAxisColorZUVE));
    }
    if (hasLegacyAxisMarker) {
        static_cast<void>(config.RemoveKeyUVE(kLegacyAxisPaletteSetKeyUVE));
    }

    // Personal shelves: read the established count/name/items keys through their bounded descriptors.
    // The team's shelves come from the project (LoadSharedShelvesUVE) and are left alone here; a
    // personal shelf whose name a team shelf has is kept under a new name rather than dropped.
    m_contentShelves.RemoveAllUVE(false);
    const std::int64_t shelfCount =
        m_settingsRegistry.GetIntUVE(config, EditorSettingIdUVE::kPersonalShelvesCountUVE);
    for (std::int64_t index = 0; index < shelfCount; ++index) {
        const std::size_t shelfIndex = static_cast<std::size_t>(index);
        const std::string storedName =
            m_settingsRegistry.GetStringUVE(config, EditorSettingIdUVE::GetPersonalShelfNameSettingIdUVE(shelfIndex));
        const std::string name = storedName.empty() ? std::string{} : m_contentShelves.CreateUVE(storedName);
        if (name.empty()) {
            continue;
        }
        const Config::SettingStringListUVE storedItems =
            m_settingsRegistry.GetStringListUVE(config, EditorSettingIdUVE::GetPersonalShelfItemsSettingIdUVE(shelfIndex));
        for (const std::string& stored : storedItems) {
            if (!stored.empty()) {
                static_cast<void>(m_contentShelves.AddItemUVE(name, stored));
            }
        }
    }
    // Restoring stored preferences is not an author edit, so nothing above counts as a change to
    // save back: without this the editor would rewrite the settings file on every launch, and a
    // host default seeded after a load would be pinned to disk as if the author had chosen it.
    m_preferencesAutoSavePending = false;
}

bool EditorUVE::SaveSessionSettingsUVE() {
    if (m_state != EditorStateUVE::Running ||
        !AreTransformSnappingSettingsValidUVE(m_transformSnappingSettings)) {
        return false;
    }
    Config::IConfigManagerUVE& config = m_services->GetConfigManagerUVE();
    config.SetIntUVE("editor.sessionSettingsVersion", 1);
    // Each value was accepted by a setter whose range is the one its setting declares, so the
    // registry takes it. The exception is the Game workspace, which a session is never restored
    // into: that write is refused and the last restorable workspace stays stored.
    for (const Config::SettingDescriptorUVE* descriptor : m_settingsRegistry.GetAllUVE()) {
        if (const std::optional<Config::SettingValueUVE> value = GetEditorSettingUVE(descriptor->id)) {
            static_cast<void>(m_settingsRegistry.SetValueUVE(config, descriptor->id, *value));
        }
    }
    // The axis palette has no editor-owned default. Store all three descriptor values when the
    // host has seeded or restored a complete palette; absence means "ask the host for its defaults".
    if (m_viewportOverlayState.axisColorsValid) {
        const auto toSettingColor = [](const ViewportAxisColorUVE color) {
            return Config::SettingColorUVE{color.r, color.g, color.b};
        };
        static_cast<void>(m_settingsRegistry.SetValueUVE(
            config, EditorSettingIdUVE::kViewportAxisColorXUVE, toSettingColor(m_viewportOverlayState.axisColorX)));
        static_cast<void>(m_settingsRegistry.SetValueUVE(
            config, EditorSettingIdUVE::kViewportAxisColorYUVE, toSettingColor(m_viewportOverlayState.axisColorY)));
        static_cast<void>(m_settingsRegistry.SetValueUVE(
            config, EditorSettingIdUVE::kViewportAxisColorZUVE, toSettingColor(m_viewportOverlayState.axisColorZ)));
    } else {
        static_cast<void>(m_settingsRegistry.ClearValueUVE(config, EditorSettingIdUVE::kViewportAxisColorXUVE));
        static_cast<void>(m_settingsRegistry.ClearValueUVE(config, EditorSettingIdUVE::kViewportAxisColorYUVE));
        static_cast<void>(m_settingsRegistry.ClearValueUVE(config, EditorSettingIdUVE::kViewportAxisColorZUVE));
    }
    static_cast<void>(config.RemoveKeyUVE("editor.viewport.axisColors.set"));
    std::vector<ContentShelfUVE> shelves;
    std::ranges::copy_if(m_contentShelves.GetAllUVE(), std::back_inserter(shelves),
                         [](const ContentShelfUVE& shelf) { return !shelf.shared; });
    bool allShelvesStored = m_settingsRegistry.SetValueUVE(
        config, EditorSettingIdUVE::kPersonalShelvesCountUVE,
        Config::SettingValueUVE{static_cast<std::int64_t>(shelves.size())});
    for (std::size_t index = 0U; index < shelves.size(); ++index) {
        const std::string nameId = EditorSettingIdUVE::GetPersonalShelfNameSettingIdUVE(index);
        const std::string itemsId = EditorSettingIdUVE::GetPersonalShelfItemsSettingIdUVE(index);
        const bool nameStored = m_settingsRegistry.SetValueUVE(config, nameId, Config::SettingValueUVE{shelves[index].name});
        if (!nameStored) {
            static_cast<void>(m_settingsRegistry.ClearValueUVE(config, nameId));
        }
        Config::SettingStringListUVE items;
        items.reserve(shelves[index].items.size());
        for (const std::filesystem::path& item : shelves[index].items) {
            items.push_back(item.generic_string());
        }
        const bool itemsStored =
            m_settingsRegistry.SetValueUVE(config, itemsId, Config::SettingValueUVE{std::move(items)});
        if (!itemsStored) {
            static_cast<void>(m_settingsRegistry.ClearValueUVE(config, itemsId));
        }
        allShelvesStored = nameStored && itemsStored && allShelvesStored;
    }
    return config.SaveUVE() && allShelvesStored;
}



bool EditorUVE::IsHierarchyFilterActiveUVE() const noexcept {
    return !m_hierarchyFilter.empty();
}

bool EditorUVE::IsHierarchyEntityVisibleUVE(const Scene::EntityUVE entity) const {
    return !IsHierarchyFilterActiveUVE() ||
           std::find(m_cachedHierarchyVisibleEntities.begin(), m_cachedHierarchyVisibleEntities.end(), entity) !=
               m_cachedHierarchyVisibleEntities.end();
}

void EditorUVE::InvalidateHierarchyFilterCacheUVE() noexcept {
    m_hierarchyFilterCacheDirty = true;
}

void EditorUVE::BeginHierarchyRenameUVE(const Scene::EntityUVE entity) {
    if (!IsAuthoringCommandAllowedUVE() || !IsDocumentEntityUVE(entity) || IsEntityLockedUVE(entity)) {
        return;
    }
    m_hierarchyRenameEntity = entity;
    m_hierarchyRenameBuffer = GetEntityDisplayLabelUVE(entity);
    m_hierarchyRenameFocusRequested = true;
    m_hierarchyRenameUsesDialogUVE = m_hierarchyView.renameMode == HierarchyRenameModeUVE::Dialog;
    m_hierarchyRenameDialogOpenRequested = m_hierarchyRenameUsesDialogUVE;
}

void EditorUVE::CancelHierarchyRenameUVE() noexcept {
    m_hierarchyRenameEntity = Scene::kInvalidEntityUVE;
    m_hierarchyRenameBuffer.clear();
    m_hierarchyRenameFocusRequested = false;
    m_hierarchyRenameUsesDialogUVE = false;
    m_hierarchyRenameDialogOpenRequested = false;
}

void EditorUVE::RebuildHierarchyFilterCacheUVE() {
    if (!m_hierarchyFilterCacheDirty && m_cachedHierarchyFilter == m_hierarchyFilter) {
        return;
    }
    m_cachedHierarchyVisibleEntities.clear();
    m_cachedHierarchyFilter = m_hierarchyFilter;
    m_hierarchyFilterCacheDirty = false;
    if (!IsHierarchyFilterActiveUVE()) {
        return;
    }
    const bool hasTypeQuery = m_hierarchyFilter.rfind("type:", 0U) == 0U;
    const bool hasComponentQuery = m_hierarchyFilter.rfind("component:", 0U) == 0U;
    const std::string_view filterText{m_hierarchyFilter};
    const std::string_view query = hasTypeQuery ? filterText.substr(5U)
                                : hasComponentQuery ? filterText.substr(10U)
                                                    : filterText;
    const bool includeTypesAndComponents =
        m_hierarchyView.filterMode == HierarchyFilterModeUVE::NameTypeAndComponents;
    const bool keepAncestors = m_hierarchyView.filterKeepAncestors;
    const auto contains = [this](const std::string_view text, const std::string_view needle) {
        return m_hierarchyView.filterCaseSensitive ? text.find(needle) != std::string_view::npos
                                                   : ContainsCaseInsensitiveUVE(text, needle);
    };
    const auto equals = [&contains](const std::string_view left, const std::string_view right) {
        return left.size() == right.size() && contains(left, right);
    };
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const auto visit = [this, &contains, &equals, &entityManager, query, hasTypeQuery, hasComponentQuery,
                        includeTypesAndComponents, keepAncestors](const auto& self,
                                                                 const Scene::EntityUVE entity) -> bool {
        if (!IsDocumentEntityUVE(entity)) {
            return false;
        }
        const std::string displayLabel = GetEntityDisplayLabelUVE(entity);
        const std::string typeTag = GetOutlinerTypeTagUVE(entity);
        const std::string_view objectType = GetObjectTypeNameUVE(entity);
        // Both the actual object type ("Static3D") and its specialised outliner tag ("Collision Box") count.
        const bool shouldSearchType = hasTypeQuery || (!hasComponentQuery && includeTypesAndComponents);
        const bool typeMatches = shouldSearchType && (contains(typeTag, query) || contains(objectType, query));
        const bool shouldSearchComponents =
            hasComponentQuery || (!hasTypeQuery && !hasComponentQuery && includeTypesAndComponents);
        bool componentMatches = false;
        if (shouldSearchComponents) {
            for (const std::type_index componentType : entityManager.GetComponentTypesUVE(entity)) {
                const Core::TypeMetadataEntryUVE* const metadata = Scene::FindSceneComponentMetadataUVE(componentType);
                if (metadata != nullptr && contains(metadata->displayName, query)) {
                    componentMatches = true;
                    break;
                }
            }
        }
        Scene::EntityUVE parent = Scene::kInvalidEntityUVE;
        const bool isRoot = !TryGetDocumentParentUVE(entity, parent) || parent == Scene::kInvalidEntityUVE;
        const bool rootQuery = !hasTypeQuery && !hasComponentQuery && equals(query, "root") && isRoot;
        const bool nameMatches = !hasTypeQuery && !hasComponentQuery &&
                                 (contains(displayLabel, query) || rootQuery);
        const bool matches = hasTypeQuery       ? typeMatches
                             : hasComponentQuery ? componentMatches
                             : nameMatches || (includeTypesAndComponents && (typeMatches || componentMatches));
        // With context disabled, preserve preorder in this list so flat results follow the selected
        // hierarchy-view order. With context enabled, parents are added after their descendants below.
        if (!keepAncestors && matches) {
            m_cachedHierarchyVisibleEntities.push_back(entity);
        }
        bool subtreeMatches = matches;
        for (const Scene::EntityUVE child : GetHierarchyChildrenInViewOrderUVE(entity)) {
            subtreeMatches = self(self, child) || subtreeMatches;
        }
        if (keepAncestors && subtreeMatches) {
            m_cachedHierarchyVisibleEntities.push_back(entity);
        }
        return subtreeMatches;
    };
    for (const Scene::EntityUVE root : SortHierarchyRowsUVE(GetDocumentRootsUVE())) {
        static_cast<void>(visit(visit, root));
    }
}

void EditorUVE::AcceptHierarchyDropTargetUVE(const Scene::EntityUVE targetParent) {
    if (!IsLifecycleCommandAllowedUVE() ||
        (targetParent != Scene::kInvalidEntityUVE && !IsDocumentEntityUVE(targetParent)) ||
        !ImGui::BeginDragDropTarget()) {
        return;
    }

    const ImGuiPayload* const payload = ImGui::AcceptDragDropPayload(kHierarchyEntityPayloadUVE);
    if (payload != nullptr && payload->DataSize == static_cast<int>(sizeof(Scene::EntityUVE))) {
        Scene::EntityUVE source = Scene::kInvalidEntityUVE;
        std::memcpy(&source, payload->Data, sizeof(source));
        static_cast<void>(RequestHierarchyReparentUVE(source, targetParent));
    }
    // An entity asset dragged out of Content lands under the row, or in the scene for empty space.
    const ImGuiPayload* const asset = ImGui::AcceptDragDropPayload(kContentEntityPayloadUVE);
    if (asset != nullptr && asset->DataSize > 1) {
        const std::string path(static_cast<const char*>(asset->Data), static_cast<std::size_t>(asset->DataSize - 1));
        static_cast<void>(PlaceEntityAssetUVE(
            path, targetParent != Scene::kInvalidEntityUVE ? targetParent : ResolveNewObjectParentUVE()));
    }
    ImGui::EndDragDropTarget();
}

EditorUVE::ContentBrowserItemTypeUVE EditorUVE::ClassifyContentBrowserEntryUVE(
    const Asset::ProjectFileEntryUVE& entry) {
    if (entry.kind == Asset::ProjectFileEntryKindUVE::Directory) {
        return ContentBrowserItemTypeUVE::Folder;
    }

    std::string extension = entry.relativePath.extension().generic_string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (extension == ".uvscene") {
        return ContentBrowserItemTypeUVE::Scene;
    }
    if (extension == ".uvprefab") {
        return ContentBrowserItemTypeUVE::Prefab;
    }
    if (extension == ".uventity") {
        return ContentBrowserItemTypeUVE::Entity;
    }
    if (extension == ".uvbundle") {
        return ContentBrowserItemTypeUVE::Bundle;
    }
    // Model sources are shown as what they are - a mesh - and imported automatically behind the
    // scenes (see QueueModelAutoImportsUVE); a rigged one is relabelled Model by the caller, which
    // knows the file's contents.
    if (extension == ".uvmodel" || IsModelSourcePathUVE(entry.relativePath)) {
        return ContentBrowserItemTypeUVE::Mesh;
    }
    if (extension == ".uvtex") {
        return ContentBrowserItemTypeUVE::Texture;
    }
    // Raw, not-yet-imported source images. Godot-style engines preview these directly rather than
    // requiring an import step first; this repo already has standalone decoders for all four
    // (uve/asset/{png,jpeg,bmp,tga}_metadata_uve.h) that GetTextureThumbnailUVE() falls back to
    // when the file isn't a `.uvtex` envelope. Reusing Texture rather than adding a new enum value
    // since both content-browser call sites already dispatch thumbnails on this exact type.
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" ||
        extension == ".tga") {
        return ContentBrowserItemTypeUVE::Texture;
    }
    if (extension == ".uvshader") {
        return ContentBrowserItemTypeUVE::Shader;
    }
    if (extension == ".uvmat") {
        return ContentBrowserItemTypeUVE::Material;
    }
    if (extension == ".uvsave") {
        return ContentBrowserItemTypeUVE::Save;
    }
    if (extension == ".uvs") {
        return ContentBrowserItemTypeUVE::Script;
    }
    // A clip (a take imported from an FBX, or authored): the same Animation an animation-only
    // model source shows as.
    if (extension == ".uvanim") {
        return ContentBrowserItemTypeUVE::Animation;
    }
    // Imported clips and the WAV sources the importer reads them from.
    if (extension == ".uvaudio" || extension == ".wav") {
        return ContentBrowserItemTypeUVE::Audio;
    }
    if (extension == ".ttf" || extension == ".otf") {
        return ContentBrowserItemTypeUVE::Font;
    }
    return ContentBrowserItemTypeUVE::File;
}

const char* EditorUVE::GetContentBrowserItemTypeLabelUVE(const ContentBrowserItemTypeUVE type) noexcept {
    switch (type) {
        case ContentBrowserItemTypeUVE::Folder:
            return "Folder";
        case ContentBrowserItemTypeUVE::Scene:
            return "Viewport"; // a level: it opens as the Viewport in the Outliner
        case ContentBrowserItemTypeUVE::Prefab:
            return "Prefab";
        case ContentBrowserItemTypeUVE::Entity:
            return "Entity";
        case ContentBrowserItemTypeUVE::Bundle:
            return "Bundle";
        case ContentBrowserItemTypeUVE::Mesh:
            return "Mesh";
        case ContentBrowserItemTypeUVE::Model:
            return "Model";
        case ContentBrowserItemTypeUVE::Texture:
            return "Texture";
        case ContentBrowserItemTypeUVE::Shader:
            return "Shader";
        case ContentBrowserItemTypeUVE::Material:
            return "Material";
        case ContentBrowserItemTypeUVE::Save:
            return "Save";
        case ContentBrowserItemTypeUVE::Animation:
            return "Animation";
        case ContentBrowserItemTypeUVE::Script:
            return "Script";
        case ContentBrowserItemTypeUVE::Audio:
            return "Audio";
        case ContentBrowserItemTypeUVE::Font:
            return "Font";
        case ContentBrowserItemTypeUVE::File:
            return "File";
    }
    return "File";
}

const char* EditorUVE::GetContentBrowserFocusLabelUVE(const ContentBrowserTypeFocusUVE focus) noexcept {
    switch (focus) {
        case ContentBrowserTypeFocusUVE::All:
            return "All";
        case ContentBrowserTypeFocusUVE::Folders:
            return "Folders";
        case ContentBrowserTypeFocusUVE::Scene:
            return "Scene";
        case ContentBrowserTypeFocusUVE::Prefab:
            return "Prefab";
        case ContentBrowserTypeFocusUVE::Bundle:
            return "Bundle";
        case ContentBrowserTypeFocusUVE::Mesh:
            return "Mesh";
        case ContentBrowserTypeFocusUVE::Texture:
            return "Texture";
        case ContentBrowserTypeFocusUVE::Shader:
            return "Shader";
        case ContentBrowserTypeFocusUVE::Material:
            return "Material";
        case ContentBrowserTypeFocusUVE::Save:
            return "Save";
        case ContentBrowserTypeFocusUVE::Registered:
            return "Registered";
        case ContentBrowserTypeFocusUVE::OtherFiles:
            return "Other Files";
    }
    return "All";
}

bool EditorUVE::DoesContentBrowserEntryMatchFocusUVE(const Asset::ProjectFileEntryUVE& entry) const {
    const ContentBrowserItemTypeUVE type = ClassifyContentBrowserEntryUVE(entry);
    switch (m_contentBrowserTypeFocus) {
        case ContentBrowserTypeFocusUVE::All:
            return true;
        case ContentBrowserTypeFocusUVE::Folders:
            return type == ContentBrowserItemTypeUVE::Folder;
        case ContentBrowserTypeFocusUVE::Scene:
            return type == ContentBrowserItemTypeUVE::Scene;
        case ContentBrowserTypeFocusUVE::Prefab:
            return type == ContentBrowserItemTypeUVE::Prefab || type == ContentBrowserItemTypeUVE::Entity;
        case ContentBrowserTypeFocusUVE::Bundle:
            return type == ContentBrowserItemTypeUVE::Bundle;
        case ContentBrowserTypeFocusUVE::Mesh:
            return type == ContentBrowserItemTypeUVE::Mesh || type == ContentBrowserItemTypeUVE::Model;
        case ContentBrowserTypeFocusUVE::Texture:
            return type == ContentBrowserItemTypeUVE::Texture;
        case ContentBrowserTypeFocusUVE::Shader:
            return type == ContentBrowserItemTypeUVE::Shader;
        case ContentBrowserTypeFocusUVE::Material:
            return type == ContentBrowserItemTypeUVE::Material;
        case ContentBrowserTypeFocusUVE::Save:
            return type == ContentBrowserItemTypeUVE::Save;
        case ContentBrowserTypeFocusUVE::Registered:
            return entry.kind == Asset::ProjectFileEntryKindUVE::File && entry.registeredAssetGuid.has_value();
        case ContentBrowserTypeFocusUVE::OtherFiles:
            return type == ContentBrowserItemTypeUVE::File;
    }
    return false;
}

bool EditorUVE::IsContentBrowserDirectoryInSnapshotUVE(const Asset::ProjectFileSnapshotUVE& snapshot,
                                                        const std::filesystem::path& directory) const {
    if (directory.empty()) {
        return true;
    }
    return std::any_of(snapshot.entries.begin(), snapshot.entries.end(), [&directory](const Asset::ProjectFileEntryUVE& entry) {
        return entry.kind == Asset::ProjectFileEntryKindUVE::Directory && entry.relativePath == directory;
    });
}

void EditorUVE::ReconcileContentBrowserDirectoryUVE(const Asset::ProjectFileSnapshotUVE& snapshot) noexcept {
    if (!IsContentBrowserDirectoryInSnapshotUVE(snapshot, m_contentBrowserDirectory)) {
        m_contentBrowserDirectory.clear();
    }
}

bool EditorUVE::IsProjectPathFavoritedUVE(const std::filesystem::path& relativePath) const {
    return std::find(m_favoriteProjectPaths.begin(), m_favoriteProjectPaths.end(), relativePath) !=
           m_favoriteProjectPaths.end();
}

std::filesystem::path EditorUVE::GetSharedShelvesPathUVE() const {
    return m_services->GetProjectSettingsUVE().GetPathUVE().parent_path() / "project.uvshelves";
}

void EditorUVE::LoadSharedShelvesUVE() {
    const std::filesystem::path path = GetSharedShelvesPathUVE();
    std::error_code error;
    const auto writeTime = std::filesystem::last_write_time(path, error);
    if (error) {
        m_contentShelves.RemoveAllUVE(true); // no file: the team has no shelves
        m_sharedShelvesWriteTime.reset();
        return;
    }
    m_sharedShelvesWriteTime = writeTime;
    std::ifstream file(path, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    std::string problem;
    if (!ReadSharedShelvesTextUVE(text, m_contentShelves, problem)) {
        m_contentStatusMessage = "Could not read the team's shelves (" + path.filename().string() + "): " + problem + ".";
    }
}

bool EditorUVE::SaveSharedShelvesUVE() {
    const std::filesystem::path path = GetSharedShelvesPathUVE();
    std::error_code error;
    const bool anyShared = std::ranges::any_of(m_contentShelves.GetAllUVE(), &ContentShelfUVE::shared);
    // A project that never had a team shelf does not get an empty file.
    if (!anyShared && !std::filesystem::exists(path, error)) {
        return true;
    }
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << WriteSharedShelvesTextUVE(m_contentShelves);
        if (!file) {
            m_contentStatusMessage = "Could not save the team's shelves to " + path.filename().string() + ".";
            return false;
        }
    }
    const auto writeTime = std::filesystem::last_write_time(path, error);
    m_sharedShelvesWriteTime = error ? std::nullopt : std::optional{writeTime};
    return true;
}

void EditorUVE::ReloadSharedShelvesIfChangedUVE() {
    std::error_code error;
    const auto writeTime = std::filesystem::last_write_time(GetSharedShelvesPathUVE(), error);
    const std::optional<std::filesystem::file_time_type> now = error ? std::nullopt : std::optional{writeTime};
    if (now != m_sharedShelvesWriteTime) {
        LoadSharedShelvesUVE();
    }
}

ContentFileFactsUVE EditorUVE::GetContentFileFactsUVE(const std::filesystem::path& contentRoot,
                                                                 const Asset::ProjectFileEntryUVE& entry,
                                                                 const std::uint64_t refreshGeneration) {
    if (refreshGeneration != m_contentFileFactsGeneration) {
        m_contentFileFacts.clear();
        m_contentFileFactsGeneration = refreshGeneration;
    }
    const std::string key = entry.relativePath.generic_string();
    if (const auto it = m_contentFileFacts.find(key); it != m_contentFileFacts.end()) {
        return it->second;
    }
    ContentFileFactsUVE facts;
    std::error_code error;
    const std::filesystem::path absolute = contentRoot / entry.relativePath;
    if (entry.kind == Asset::ProjectFileEntryKindUVE::File) {
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

void EditorUVE::ToggleProjectPathFavoriteUVE(const std::filesystem::path& relativePath) {
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
// rather than through the full AssetImporterUVE registry/metadata-sidecar machinery, since a
// thumbnail only ever needs pixels). Returns false for an unrecognized extension or malformed file -
// each decoder already bounds/validates its own input, so no extra size/sanity checks are needed here.
bool DecodeRawImageThumbnailPixelsUVE(const std::filesystem::path& absolutePath, std::uint32_t& outWidth,
                                       std::uint32_t& outHeight, std::vector<std::byte>& outPixels) {
    std::ifstream file(absolutePath, std::ios::binary);
    if (!file) {
        return false;
    }
    const std::vector<char> rawBytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(rawBytes.size());
    std::transform(rawBytes.begin(), rawBytes.end(), bytes.begin(),
                   [](const char byte) { return static_cast<std::byte>(byte); });

    std::string extension = absolutePath.extension().generic_string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });

    if (extension == ".png") {
        Asset::PngRgba8ImageUVE image;
        if (!Asset::DecodePngRgba8ImageUVE(bytes, image)) return false;
        outWidth = image.width; outHeight = image.height; outPixels = std::move(image.pixels);
        return true;
    }
    if (extension == ".jpg" || extension == ".jpeg") {
        Asset::JpegRgba8ImageUVE image;
        if (!Asset::DecodeJpegRgba8ImageUVE(bytes, image)) return false;
        outWidth = image.width; outHeight = image.height; outPixels = std::move(image.pixels);
        return true;
    }
    if (extension == ".bmp") {
        Asset::BmpRgba8ImageUVE image;
        if (!Asset::DecodeBmpRgba8ImageUVE(bytes, image)) return false;
        outWidth = image.width; outHeight = image.height; outPixels = std::move(image.pixels);
        return true;
    }
    if (extension == ".tga") {
        Asset::TgaRgba8ImageUVE image;
        if (!Asset::DecodeTgaRgba8ImageUVE(bytes, image)) return false;
        outWidth = image.width; outHeight = image.height; outPixels = std::move(image.pixels);
        return true;
    }
    return false;
}

} // namespace

std::uintptr_t EditorUVE::GetTextureThumbnailUVE(const std::filesystem::path& relativePath) {
    const std::string cacheKey = relativePath.generic_string();
    const auto cachedIt = m_textureThumbnailCache.find(cacheKey);
    if (cachedIt != m_textureThumbnailCache.end()) {
        return cachedIt->second;
    }
    const Asset::ProjectFileSnapshotUVE snapshot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    const std::filesystem::path absolutePath = snapshot.contentRoot / relativePath;
    std::uintptr_t textureId = 0U;
    Asset::TextureAssetUVE texture;
    if (Asset::LoadTextureAssetUVE(absolutePath, texture) && texture.width > 0U && texture.height > 0U &&
        texture.format == Asset::TextureAssetFormatUVE::RGBA8Unorm) {
        textureId = EditorUiAssetsUVE::UploadDynamicTextureUVE(reinterpret_cast<const std::uint8_t*>(texture.pixels.data()),
                                                                static_cast<int>(texture.width),
                                                                static_cast<int>(texture.height));
    } else {
        // Not a `.uvtex` envelope - it may still be a raw, un-imported source image.
        std::uint32_t rawWidth = 0U;
        std::uint32_t rawHeight = 0U;
        std::vector<std::byte> rawPixels;
        if (DecodeRawImageThumbnailPixelsUVE(absolutePath, rawWidth, rawHeight, rawPixels) && rawWidth > 0U &&
            rawHeight > 0U) {
            textureId = EditorUiAssetsUVE::UploadDynamicTextureUVE(reinterpret_cast<const std::uint8_t*>(rawPixels.data()),
                                                                    static_cast<int>(rawWidth),
                                                                    static_cast<int>(rawHeight));
        }
    }
    m_textureThumbnailCache.emplace(cacheKey, textureId);
    return textureId;
}

void EditorUVE::ClearTextureThumbnailCacheUVE() noexcept {
    for (auto& [path, textureId] : m_textureThumbnailCache) {
        EditorUiAssetsUVE::DeleteDynamicTextureUVE(textureId);
    }
    m_textureThumbnailCache.clear();
}

std::uintptr_t EditorUVE::GetMeshThumbnailUVE(const std::filesystem::path& relativePath) {
    const std::string cacheKey = relativePath.generic_string();
    const auto cachedIt = m_meshThumbnailCache.find(cacheKey);
    if (cachedIt != m_meshThumbnailCache.end()) {
        return cachedIt->second;
    }
    const Asset::ProjectFileSnapshotUVE snapshot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    // A model source previews its imported mesh; until that exists there is nothing to render yet.
    const std::filesystem::path absolutePath =
        IsModelSourcePathUVE(relativePath) ? GetImportedModelPathUVE(relativePath) : snapshot.contentRoot / relativePath;
    Asset::MeshAssetUVE mesh;
    std::uintptr_t textureId = 0U;
    if (Asset::LoadMeshAssetUVE(absolutePath, mesh)) {
        textureId = m_meshThumbnailRenderer.RenderThumbnailUVE(mesh, kMeshThumbnailSizeUVE, kMeshThumbnailSizeUVE);
    }
    m_meshThumbnailCache.emplace(cacheKey, textureId);
    return textureId;
}

void EditorUVE::ClearMeshThumbnailCacheUVE() noexcept {
    for (auto& [path, textureId] : m_meshThumbnailCache) {
        EditorUiAssetsUVE::DeleteDynamicTextureUVE(textureId);
    }
    m_meshThumbnailCache.clear();
}

bool EditorUVE::IsModelSourcePathUVE(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension == ".glb" || extension == ".gltf" || extension == ".obj" || extension == ".fbx";
}

std::filesystem::path EditorUVE::GetImportedModelPathUVE(const std::filesystem::path& relativeSource) const {
    // Beside the import cache rather than inside it: the cache root holds only cache metadata.
    // The source's own extension stays in the name, so "rock.obj" and "rock.glb" never collide.
    const std::filesystem::path cacheRoot = m_services->GetDerivedArtifactCacheUVE().GetCacheRootUVE();
    std::filesystem::path importedRoot = cacheRoot.has_filename() ? cacheRoot.parent_path() : cacheRoot.parent_path().parent_path();
    importedRoot /= "Imported";
    return (importedRoot / relativeSource).concat(".uvmodel").lexically_normal();
}

namespace {

/// Reads only as much of a model source as it takes to tell what it is: the whole file for a
/// .gltf (it is the JSON), the header and JSON chunk for a .glb - never the binary payload - and
/// the scene without its geometry or curves for an .fbx.
[[nodiscard]] EditorModelSourceInfoUVE ReadModelSourceInfoUVE(const std::filesystem::path& path) {
    EditorModelSourceInfoUVE info;
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
        if (error || size > Asset::kMaximumFbxMeshSourceBytesUVE) {
            return info;
        }
        const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        const std::optional<Asset::FbxSourceSummaryUVE> summary =
            Asset::DescribeFbxSourceUVE(std::as_bytes(std::span<const char>(bytes)));
        if (!summary.has_value()) {
            return info;
        }
        info.rigged = summary->hasSkin;
        info.hasSkeleton = summary->boneCount > 0U;
        info.animationOnly = summary->IsAnimationOnlyUVE();
        info.animationCount = summary->animationCount;
        if (info.animationOnly) {
            char length[32] = {};
            std::snprintf(length, sizeof(length), "%.2f s", summary->longestAnimationSeconds);
            info.summary = std::to_string(summary->boneCount) + (summary->boneCount == 1U ? " bone, " : " bones, ") +
                           std::to_string(summary->animationCount) +
                           (summary->animationCount == 1U ? " animation, " : " animations, ") + length;
        }
        return info;
    } else {
        return info; // OBJ has no skeleton.
    }
    const std::optional<Asset::GltfMetadataUVE> metadata = Asset::ParseGltfMetadataUVE(json);
    info.rigged = metadata.has_value() && metadata->skinCount > 0U;
    info.hasSkeleton = info.rigged; // A glTF skeleton is read from its skin.
    return info;
}

} // namespace

const EditorModelSourceInfoUVE* EditorUVE::FindModelSourceInfoUVE(
    const std::filesystem::path& relativeSource) const {
    const auto found = m_modelSources.find(relativeSource.generic_string());
    return found != m_modelSources.end() ? &found->second : nullptr;
}

bool EditorUVE::IsRiggedModelSourceUVE(const std::filesystem::path& relativeSource) const {
    const EditorModelSourceInfoUVE* const info = FindModelSourceInfoUVE(relativeSource);
    return info != nullptr && info->rigged;
}

std::vector<std::filesystem::path> EditorUVE::ImportModelAnimationsUVE(const std::filesystem::path& absoluteSource) {
    std::vector<std::filesystem::path> written;
    std::error_code error;
    const std::filesystem::file_time_type sourceTime = std::filesystem::last_write_time(absoluteSource, error);
    if (error) {
        return written;
    }
    std::ifstream file(absoluteSource, std::ios::binary);
    const std::uintmax_t size = std::filesystem::file_size(absoluteSource, error);
    if (!file || error || size > Asset::kMaximumFbxMeshSourceBytesUVE) {
        return written;
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const std::vector<Asset::AnimationClipAssetUVE> takes =
        Asset::ReadFbxAnimationsUVE(std::as_bytes(std::span<const char>(bytes)), Asset::kMaximumAnimationAssetBonesUVE);
    // The animation is named after its file, everywhere: one take is "<FBX>.uvanim"; several are
    // "<FBX>_<take>.uvanim", the take without its "Armature|"-style prefix. The DCC tool's own take
    // label ("Take 001", an exporter's name) is not a name anyone chose.
    const auto clipPath = [&absoluteSource, &takes](const std::string& clipId) {
        if (takes.size() == 1U) {
            return absoluteSource.parent_path() / (absoluteSource.stem().string() + ".uvanim");
        }
        const std::size_t bar = clipId.find_last_of('|');
        const std::string take = bar == std::string::npos ? clipId : clipId.substr(bar + 1U);
        std::string safe;
        for (const char character : take) {
            const bool keep = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                              (character >= '0' && character <= '9') || character == '-' || character == '_' ||
                              character == ' ';
            safe.push_back(keep ? character : '_');
        }
        return absoluteSource.parent_path() / (absoluteSource.stem().string() + "_" + safe + ".uvanim");
    };
    for (Asset::AnimationClipAssetUVE clip : takes) {
        const std::filesystem::path destination = clipPath(clip.clipId);
        clip.clipId = destination.stem().string();
        const std::filesystem::file_time_type existing = std::filesystem::last_write_time(destination, error);
        if (!error && existing >= sourceTime) {
            continue; // imported since the FBX last changed
        }
        error.clear();
        if (Asset::SaveAnimationClipAssetUVE(clip, destination)) {
            written.push_back(destination);
        } else {
            UVE_WARNING("EditorUVE: could not write animation {}", destination.string());
        }
    }
    return written;
}

void EditorUVE::QueueModelAutoImportsUVE(const Asset::ProjectFileSnapshotUVE& snapshot) {
    Asset::IAssetImportQueueUVE& queue = m_services->GetAssetImportQueueUVE();
    m_modelSources.clear();
    for (const Asset::ProjectFileEntryUVE& entry : snapshot.entries) {
        if (entry.kind == Asset::ProjectFileEntryKindUVE::Directory || !IsModelSourcePathUVE(entry.relativePath)) {
            continue;
        }
        const std::string key = entry.relativePath.generic_string();
        const std::filesystem::path source = snapshot.contentRoot / entry.relativePath;
        const EditorModelSourceInfoUVE& info = m_modelSources[key] = ReadModelSourceInfoUVE(source);
        // An FBX's takes (the extension is compared as the model check compares it: any case).
        if (info.animationCount > 0U) {
            static_cast<void>(ImportModelAnimationsUVE(source));
        }
        if (info.animationOnly) {
            continue; // Motion with nothing to draw: there is no mesh to import.
        }
        if (m_modelImportJobs.find(key) != m_modelImportJobs.end()) {
            continue; // Already queued or running; its completion is collected by PollModelImportJobsUVE.
        }
        Asset::AssetImportRequestUVE request;
        request.sourcePath = source;
        request.destinationPath = GetImportedModelPathUVE(entry.relativePath);
        request.settings = std::make_shared<const Asset::AssetImportSettingsUVE>();
        if (const std::optional<Asset::AssetImportJobIdUVE> job = queue.EnqueueUVE(std::move(request));
            job.has_value()) {
            m_modelImportJobs.emplace(key, *job);
        }
    }
}

void EditorUVE::PollModelImportJobsUVE() {
    if (m_modelImportJobs.empty()) {
        return;
    }
    const std::vector<Asset::AssetImportJobUVE> jobs = m_services->GetAssetImportQueueUVE().GetJobsUVE();
    bool imported = false;
    for (auto it = m_modelImportJobs.begin(); it != m_modelImportJobs.end();) {
        const auto job = std::find_if(jobs.begin(), jobs.end(),
                                      [&it](const Asset::AssetImportJobUVE& candidate) { return candidate.id == it->second; });
        if (job == jobs.end() || job->state == Asset::AssetImportJobStateUVE::Failed) {
            if (job != jobs.end()) {
                UVE_WARNING("EditorUVE: could not import model {}", it->first);
            }
            it = m_modelImportJobs.erase(it);
        } else if (job->state == Asset::AssetImportJobStateUVE::Succeeded) {
            imported = imported || !job->cacheHit;
            it = m_modelImportJobs.erase(it);
        } else {
            ++it;
        }
    }
    if (imported) {
        ClearMeshThumbnailCacheUVE(); // A freshly converted mesh has a thumbnail now.
    }
}

void EditorUVE::RefreshProjectFileIndexUVE() {
    Asset::IProjectFileIndexUVE& projectFileIndex = m_services->GetProjectFileIndexUVE();
    Asset::IProjectChangeWatcherUVE& projectChangeWatcher = m_services->GetProjectChangeWatcherUVE();
    const Asset::ProjectChangeSnapshotUVE changesBeforeRefresh = projectChangeWatcher.GetSnapshotUVE();
    m_projectFileLastRefreshSucceeded = projectFileIndex.RefreshUVE(m_services->GetAssetDatabaseUVE());
    m_projectFileSnapshotInitialized = true;
    if (m_projectFileLastRefreshSucceeded) {
        projectChangeWatcher.AcknowledgeThroughUVE(changesBeforeRefresh.latestSequence);
        m_projectFileRefreshAttemptedForRescan = false;
        if (changesBeforeRefresh.rescanRequired) {
            // A successful full index refresh is the explicit boundary that safely clears watcher overflow.
            projectChangeWatcher.AcknowledgeRescanUVE();
        }
        // On-disk content may have changed since these were cached; re-decode lazily on next display.
        ClearTextureThumbnailCacheUVE();
        ClearMeshThumbnailCacheUVE();
        QueueModelAutoImportsUVE(projectFileIndex.GetSnapshotUVE());
    } else {
        m_projectFileRefreshAttemptedForRescan = changesBeforeRefresh.rescanRequired;
    }
}

void EditorUVE::DrawScriptingWorkspaceUVE() {
    if (m_openUVScript.has_value()) {
        DrawUVScriptEditorUVE();
        return;
    }
    // Nothing open: say how to get a script here rather than show an empty workspace.
    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2{mainViewport->WorkPos.x, mainViewport->WorkPos.y + kEditorTopChromeHeightUVE},
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2{mainViewport->WorkSize.x, std::max(120.0F, mainViewport->WorkSize.y - kEditorTopChromeHeightUVE)},
        ImGuiCond_Always);
    constexpr ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar;
    if (ImGui::Begin("Scripting Workspace##uve", nullptr, windowFlags)) {
        ImGui::TextDisabled("No script is open.");
        ImGui::TextDisabled("Select an object, then use its Script slot in the Inspector: New UVScript, or Open.");
        if (ImGui::SmallButton("Back to the scene")) {
            m_activeWorkspace = EditorWorkspaceUVE::Library;
        }
    }
    ImGui::End();
}

} // namespace UVE::Editor
