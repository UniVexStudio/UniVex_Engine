// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_uve.h"
#include "uve/editor/editor_render_stats_uve.h"

#include "editor_chrome_layout_uve.h"
#include "editor_entity_label_uve.h"
#include "editor_text_search_uve.h"
#include "editor_fonts_uve.h"
#include "editor_node_icons_uve.h"
#include "uve/editor/editor_theme_uve.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <functional>
#include <limits>
#include <map>
#include <numbers>
#include <string>
#include <string_view>
#include <system_error>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

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
#include "uve/scripting/script_builtin_nodes_uve.h"
#include "uve/scripting/script_bytecode_uve.h"
#include "uve/scripting/script_compiler_ir_uve.h"
#include "uve/component/area_component_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/nodes/3d/all_nodes_3d_uve.h"
#include "uve/nodes/canvas_layer/all_nodes_canvas_layer_uve.h"
#include "uve/scene/nodes/scene_root_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/prefab_instance_component_uve.h"
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
// editor's existing RGBA-texture ImageButton icons (gizmo modes, Snap, node/component add popups).
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
    // Sourced from the node definitions rather than restated here. These half-extents are the same
    // authored defaults BoxMesh3D/SphereMesh3D/PlaneMesh3D attach at creation, and a second copy
    // of them is a second place to edit: changing a primitive's collider in its definition while
    // this switch kept the old number would give an entity different collision depending on
    // whether it was created as that kind or converted to it - a difference nothing would report.
    switch (kind) {
        case Scene::PrimitiveMeshKindUVE::Cube:
            return Scene::BoxMesh3DNodeDefinitionUVE{}.collider.halfExtents;
        case Scene::PrimitiveMeshKindUVE::UVSphere:
            return Scene::SphereMesh3DNodeDefinitionUVE{}.collider.halfExtents;
        case Scene::PrimitiveMeshKindUVE::Plane:
            return Scene::PlaneMesh3DNodeDefinitionUVE{}.collider.halfExtents;
    }
    return Scene::BoxMesh3DNodeDefinitionUVE{}.collider.halfExtents;
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

[[nodiscard]] const char* ScriptValueTypeLabelUVE(const Scripting::ScriptValueTypeUVE type) noexcept {
    switch (type) {
        case Scripting::ScriptValueTypeUVE::Execution: return "Exec";
        case Scripting::ScriptValueTypeUVE::Boolean: return "Bool";
        case Scripting::ScriptValueTypeUVE::Number: return "Number";
        case Scripting::ScriptValueTypeUVE::Vector2: return "Vector2";
        case Scripting::ScriptValueTypeUVE::Vector3: return "Vector3";
        case Scripting::ScriptValueTypeUVE::Entity: return "Entity";
        case Scripting::ScriptValueTypeUVE::Asset: return "Asset";
        case Scripting::ScriptValueTypeUVE::Component: return "Component";
        case Scripting::ScriptValueTypeUVE::Rotation: return "Rotation";
        case Scripting::ScriptValueTypeUVE::Transform: return "Transform";
        case Scripting::ScriptValueTypeUVE::Array: return "Array";
        case Scripting::ScriptValueTypeUVE::Map: return "Map";
        case Scripting::ScriptValueTypeUVE::Set: return "Set";
        case Scripting::ScriptValueTypeUVE::Struct: return "Struct";
    }
    return "Unknown";
}

[[nodiscard]] ImU32 ScriptPinColorUVE(const Scripting::ScriptPinRoleUVE role,
                                      const Scripting::ScriptValueTypeUVE type) noexcept {
    if (role == Scripting::ScriptPinRoleUVE::Execution || type == Scripting::ScriptValueTypeUVE::Execution) {
        return IM_COL32(230, 230, 230, 255);
    }
    switch (type) {
        case Scripting::ScriptValueTypeUVE::Boolean: return IM_COL32(204, 112, 226, 255);
        case Scripting::ScriptValueTypeUVE::Number: return IM_COL32(112, 184, 232, 255);
        case Scripting::ScriptValueTypeUVE::Vector2:
        case Scripting::ScriptValueTypeUVE::Vector3: return IM_COL32(90, 198, 164, 255);
        case Scripting::ScriptValueTypeUVE::Entity:
        case Scripting::ScriptValueTypeUVE::Component: return IM_COL32(232, 166, 82, 255);
        case Scripting::ScriptValueTypeUVE::Asset: return IM_COL32(242, 132, 132, 255);
        default: return IM_COL32(180, 180, 180, 255);
    }
}

[[nodiscard]] ImU32 ScriptPinColorUVE(const Scripting::ScriptGraphCanvasPinSnapshotUVE& pin) noexcept {
    return ScriptPinColorUVE(pin.role, pin.type);
}

// Per-category node header color, matching a design mockup's own per-category header tint
// convention (Blueprint-style visual scripting). Node headers previously ignored `node.category`
// entirely and rendered a uniform gray regardless of node type - the 13 categories here match the
// ones already documented across this engine's built-in node library
// (Engine/Runtime/Scripting/src/script_builtin_nodes_uve.cpp). Unrecognized/"Uncategorized"
// categories fall back to the prior uniform gray so nothing regresses for a node type this table
// doesn't yet name.
[[nodiscard]] ImU32 ScriptNodeCategoryColorUVE(const std::string& category) noexcept {
    if (category == "Flow") return IM_COL32(158, 158, 158, 255);
    if (category == "Conversion") return IM_COL32(120, 140, 160, 255);
    if (category == "Math") return IM_COL32(74, 124, 168, 255);
    if (category == "Logic") return IM_COL32(140, 92, 168, 255);
    if (category == "Engine") return IM_COL32(96, 108, 122, 255);
    if (category == "Variable") return IM_COL32(60, 130, 110, 255);
    if (category == "Entity") return IM_COL32(178, 122, 56, 255);
    if (category == "Input") return IM_COL32(168, 96, 96, 255);
    if (category == "Camera") return IM_COL32(96, 130, 168, 255);
    if (category == "Animation") return IM_COL32(150, 110, 150, 255);
    if (category == "Physics") return IM_COL32(96, 148, 96, 255);
    if (category == "Audio") return IM_COL32(168, 140, 76, 255);
    if (category == "Debug") return IM_COL32(180, 90, 90, 255);
    return IM_COL32(70, 82, 94, 255); // prior uniform header color, unchanged fallback
}

// Draws one small procedural glyph (matching the ImDrawList icon convention already established
// for the Scene Hierarchy/Inspector panels) representing a node's category, before its title text.
// One glyph per category (13 total), not per exact node iconId - the built-in node library has 163
// distinct iconId strings, and a unique glyph per node type is unbounded scope for hand-drawn icons
// (the same reasoning already applied to the Scene Hierarchy's per-category, not per-node, icons).
void DrawScriptNodeCategoryIconUVE(ImDrawList* const drawList, const ImVec2 center, const float radius,
                                    const std::string& category, const ImU32 color) {
    if (category == "Flow") {
        // A small right-pointing triangle (play/flow arrow).
        drawList->AddTriangleFilled(ImVec2{center.x - radius * 0.5F, center.y - radius * 0.7F},
                                    ImVec2{center.x - radius * 0.5F, center.y + radius * 0.7F},
                                    ImVec2{center.x + radius * 0.7F, center.y}, color);
    } else if (category == "Math") {
        drawList->AddLine(ImVec2{center.x - radius, center.y}, ImVec2{center.x + radius, center.y}, color, 1.6F);
        drawList->AddLine(ImVec2{center.x, center.y - radius}, ImVec2{center.x, center.y + radius}, color, 1.6F);
    } else if (category == "Logic") {
        drawList->AddCircle(center, radius * 0.75F, color, 0, 1.6F);
    } else if (category == "Variable") {
        drawList->AddRectFilled(ImVec2{center.x - radius * 0.7F, center.y - radius * 0.5F},
                                ImVec2{center.x + radius * 0.7F, center.y + radius * 0.5F}, color, 2.0F);
    } else if (category == "Entity") {
        drawList->AddRect(ImVec2{center.x - radius * 0.7F, center.y - radius * 0.7F},
                          ImVec2{center.x + radius * 0.7F, center.y + radius * 0.7F}, color, 1.0F, 0, 1.6F);
    } else if (category == "Input") {
        drawList->AddRectFilled(ImVec2{center.x - radius * 0.75F, center.y - radius * 0.4F},
                                ImVec2{center.x + radius * 0.75F, center.y + radius * 0.4F}, color, 2.0F);
    } else if (category == "Camera") {
        drawList->AddRectFilled(ImVec2{center.x - radius * 0.6F, center.y - radius * 0.45F},
                                ImVec2{center.x + radius * 0.3F, center.y + radius * 0.45F}, color, 1.0F);
        drawList->AddTriangleFilled(ImVec2{center.x + radius * 0.3F, center.y - radius * 0.3F},
                                    ImVec2{center.x + radius * 0.3F, center.y + radius * 0.3F},
                                    ImVec2{center.x + radius * 0.85F, center.y}, color);
    } else if (category == "Animation") {
        drawList->AddBezierCubic(ImVec2{center.x - radius, center.y}, ImVec2{center.x - radius * 0.3F, center.y - radius},
                                 ImVec2{center.x + radius * 0.3F, center.y + radius}, ImVec2{center.x + radius, center.y},
                                 color, 1.6F);
    } else if (category == "Physics") {
        drawList->AddCircleFilled(center, radius * 0.75F, color);
    } else if (category == "Audio") {
        drawList->AddTriangleFilled(ImVec2{center.x - radius * 0.2F, center.y - radius * 0.5F},
                                    ImVec2{center.x - radius * 0.2F, center.y + radius * 0.5F},
                                    ImVec2{center.x - radius * 0.8F, center.y}, color);
        drawList->AddCircle(center, radius * 0.9F, color, 0, 1.3F);
    } else if (category == "Debug") {
        drawList->AddText(ImVec2{center.x - radius * 0.35F, center.y - radius * 0.7F}, color, "!");
    } else if (category == "Conversion") {
        drawList->AddLine(ImVec2{center.x - radius, center.y - radius * 0.4F},
                          ImVec2{center.x + radius, center.y + radius * 0.4F}, color, 1.6F);
        drawList->AddLine(ImVec2{center.x - radius, center.y + radius * 0.4F},
                          ImVec2{center.x + radius, center.y - radius * 0.4F}, color, 1.6F);
    } else {
        // Engine and any unrecognized category: a plain dot, matching the Scene Hierarchy's own
        // fallback glyph for base/uncategorized node types.
        drawList->AddCircleFilled(center, radius * 0.55F, color);
    }
}

[[nodiscard]] ImVec2 ScriptCanvasToScreenUVE(const Scripting::ScriptGraphCanvasPointUVE point,
                                              const ImVec2 origin,
                                              const Scripting::ScriptGraphCanvasViewUVE view) noexcept {
    return ImVec2{origin.x + (point.x - view.pan.x) * view.zoom,
                   origin.y + (point.y - view.pan.y) * view.zoom};
}

[[nodiscard]] Scripting::ScriptGraphCanvasPointUVE ScreenToScriptCanvasUVE(
    const ImVec2 point, const ImVec2 origin, const Scripting::ScriptGraphCanvasViewUVE view) noexcept {
    return Scripting::ScriptGraphCanvasPointUVE{
        view.pan.x + (point.x - origin.x) / std::max(view.zoom, 0.0001F),
        view.pan.y + (point.y - origin.y) / std::max(view.zoom, 0.0001F)};
}


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

[[nodiscard]] bool IsFiniteUVE(const float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] Math::QuaternionUVE ConjugateUVE(const Math::QuaternionUVE& value) noexcept {
    return Math::QuaternionUVE{-value.x, -value.y, -value.z, value.w};
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
    if (!Scripting::RegisterBuiltInScriptNodesUVE(m_visualScriptRegistry)) {
        throw std::logic_error("Failed to register built-in Visual Scripting nodes.");
    }
    m_visualScriptBranches.push_back(
        ScriptBranchUVE{"Type 1 Scene", std::make_unique<Scripting::ScriptGraphCanvasUVE>(
                             m_visualScriptRegistry, m_historyCapacity)});
    RegisterBuiltInInspectorDrawersUVE();
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
        // Docking only - no ImGuiConfigFlags_ViewportsEnable: multi-OS-window docking pulls in a
        // second GLFW/OpenGL presentation path this editor's single-window WindowManagerUVE
        // integration was never built for, and nothing in this pass's scope needs it.
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
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
    static_cast<void>(LoadVisualScriptWorkspaceUVE());
    // A document always has exactly one scene root - the structural anchor at the top of the
    // hierarchy. A fresh editor start is an empty document with just the root.
    static_cast<void>(EnsureDocumentSceneRootUVE());
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

    PruneSelectionUVE();
    if (m_hierarchyRenameEntity != Scene::kInvalidEntityUVE &&
        (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE() ||
         !IsDocumentEntityUVE(m_hierarchyRenameEntity) || m_hierarchyRenameEntity != m_selectedEntity)) {
        CancelHierarchyRenameUVE();
    }
}

bool EditorUVE::EnterPlayModeUVE() {
    if (m_state != EditorStateUVE::Running || m_playModeState != EditorPlayModeStateUVE::Edit ||
        m_simulationControl == nullptr || !IsAuthoringCommandAllowedUVE()) {
        return false;
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
    m_workspaceBeforePlayMode = m_activeWorkspace;
    m_activeWorkspace = EditorWorkspaceUVE::Game;
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
    if (m_activeWorkspace == EditorWorkspaceUVE::Game) {
        m_activeWorkspace = m_workspaceBeforePlayMode;
    }
    return true;
}

EditorPlayModeStateUVE EditorUVE::GetPlayModeStateUVE() const noexcept {
    return m_playModeState;
}



bool EditorUVE::SaveSceneUVE() {
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

bool EditorUVE::SaveSelectedPrefabUVE(const std::filesystem::path& path) {
    if (!IsLifecycleCommandAllowedUVE() || path.empty() || !IsDocumentEntityUVE(m_selectedEntity)) {
        return false;
    }
    const Asset::AssetGuidUVE guid = m_services->GetPrefabSystemUVE().SavePrefabUVE(
        m_services->GetEntityManagerUVE(), m_services->GetAssetDatabaseUVE(), m_selectedEntity, path);
    return guid != Asset::kInvalidAssetGuidUVE;
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
        static_cast<void>(EnsureDocumentSceneRootUVE());
        return false;
    }

    std::filesystem::remove(recoveryPath, error);
    // Auto-migrate: a loaded document must end with exactly one scene root. Files saved before
    // the root existed are legitimately multi-root (every Add-Node used to create a new root),
    // so wrap their top-level entities under a fresh SceneRoot; files that already carry the
    // root pass through untouched. Either way the in-memory document afterwards holds the
    // one-root invariant every other document seam relies on.
    bool migrated = false;
    const Scene::EntityUVE sceneRoot = EnsureDocumentSceneRootUVE();
    if (sceneRoot != Scene::kInvalidEntityUVE) {
        Scene::ISceneGraphUVE& sceneGraph = m_services->GetSceneGraphUVE();

        // Strip any marker other than the one root this document keeps. A .uvescene is plain JSON
        // on disk, so a file can arrive carrying two of them - a badly resolved merge, a
        // hand-edit, a future tool - and the loop below would then reparent the second one UNDER
        // the first, leaving a root inside a root. That is not cosmetic: the editor refuses to
        // delete or reparent a scene root, so the surplus would be an entity the user has no way
        // to remove.
        //
        // The marker is stripped rather than the entity destroyed. The surplus root may well have
        // children, and discarding authored content to repair a structural mistake is the wrong
        // trade - demoted to an ordinary node it keeps its name, its transform and its subtree,
        // and the migration below folds it under the real root like any other top-level entity.
        std::vector<Scene::EntityUVE> surplusRoots;
        entityManager.ForEachUVE<Scene::SceneRootComponentUVE>(
            [&surplusRoots, sceneRoot](const Scene::EntityUVE entity, const Scene::SceneRootComponentUVE&) {
                if (entity != sceneRoot) {
                    surplusRoots.push_back(entity);
                }
            });
        for (const Scene::EntityUVE surplus : surplusRoots) {
            entityManager.RemoveComponentUVE<Scene::SceneRootComponentUVE>(surplus);
            migrated = true; // the document no longer matches the bytes it was loaded from
        }

        for (const Scene::EntityUVE topLevel : GetDocumentRootsUVE()) {
            if (topLevel != sceneRoot) {
                sceneGraph.SetParentUVE(entityManager, topLevel, sceneRoot);
                migrated = true;
            }
        }
    }
    ClearSelectionUVE();
    ClearHistoryUVE();
    m_sceneDirty = migrated; // a wrapped legacy file no longer matches its bytes on disk
    InvalidateHierarchyFilterCacheUVE();
    return true;
}

void EditorUVE::SelectEntityUVE(const Scene::EntityUVE entity) noexcept {
    if (!IsAuthoringCommandAllowedUVE()) {
        return;
    }
    if (!IsDocumentEntityUVE(entity)) {
        ClearSelectionUVE();
        return;
    }
    RestoreSelectionUVE(EditorSelectionSnapshotUVE{{entity}, entity});
}

void EditorUVE::ToggleEntitySelectionUVE(const Scene::EntityUVE entity) noexcept {
    if (!IsAuthoringCommandAllowedUVE() || !IsDocumentEntityUVE(entity)) {
        return;
    }

    const auto selectedIt = std::find(m_selectedEntities.begin(), m_selectedEntities.end(), entity);
    if (selectedIt == m_selectedEntities.end()) {
        m_selectedEntities.push_back(entity);
        m_selectedEntity = entity;
        CancelHierarchyRenameUVE();
        return;
    }

    const bool removedActive = entity == m_selectedEntity;
    m_selectedEntities.erase(selectedIt);
    if (m_selectedEntities.empty()) {
        m_selectedEntity = Scene::kInvalidEntityUVE;
    } else if (removedActive) {
        m_selectedEntity = m_selectedEntities.back();
    }
    CancelHierarchyRenameUVE();
}

void EditorUVE::ClearSelectionUVE() noexcept {
    m_selectedEntities.clear();
    m_selectedEntity = Scene::kInvalidEntityUVE;
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
            } else if constexpr (std::is_same_v<ValueType, Scene::RigidBodyComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::RigidBody && Scene::IsRigidBodyComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::AudioSourceComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::AudioSource && Scene::IsAudioSourceComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::ParticleEmitterComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::ParticleEmitter &&
                       Scene::IsParticleEmitterComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::ScriptComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::Script && Scene::IsScriptComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::AnimationPlayerComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::AnimationPlayer &&
                       Scene::IsAnimationPlayerComponentValidUVE(typedValue);
            } else if constexpr (std::is_same_v<ValueType, Scene::WorldEnvironment3DNodeComponentUVE>) {
                return kind == EditorSceneComponentKindUVE::WorldEnvironment &&
                       Scene::IsWorldEnvironment3DNodeComponentValidUVE(typedValue);
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
                       left.farPlane == right.farPlane;
            } else if constexpr (std::is_same_v<LeftType, Scene::MeshComponentUVE>) {
                return left.meshGuid == right.meshGuid && left.materialGuid == right.materialGuid;
            } else if constexpr (std::is_same_v<LeftType, Scene::LightComponentUVE>) {
                return left.color == right.color && left.intensity == right.intensity && left.type == right.type &&
                       left.range == right.range && left.spotAngleDegrees == right.spotAngleDegrees;
            } else if constexpr (std::is_same_v<LeftType, Scene::ColliderComponentUVE>) {
                return left.halfExtents == right.halfExtents && left.collisionLayer == right.collisionLayer &&
                       left.collisionMask == right.collisionMask && left.friction == right.friction &&
                       left.restitution == right.restitution && left.density == right.density &&
                       left.shapeType == right.shapeType && left.radius == right.radius && left.height == right.height;
            } else if constexpr (std::is_same_v<LeftType, Scene::RigidBodyComponentUVE>) {
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
                return left.scriptAssetPath == right.scriptAssetPath;
            } else if constexpr (std::is_same_v<LeftType, Scene::AnimationPlayerComponentUVE>) {
                return left.clipAssetPath == right.clipAssetPath && left.playbackSpeed == right.playbackSpeed &&
                       left.looping == right.looping && left.playOnAwake == right.playOnAwake &&
                       left.enabled == right.enabled;
            } else if constexpr (std::is_same_v<LeftType, Scene::WorldEnvironment3DNodeComponentUVE>) {
                return left.skyAssetPath == right.skyAssetPath && left.ambientColor == right.ambientColor &&
                       left.fogColor == right.fogColor && left.ambientEnergy == right.ambientEnergy &&
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
        case EditorSceneComponentKindUVE::Camera:
            return apply.template operator()<Scene::CameraComponentUVE>();
        case EditorSceneComponentKindUVE::Mesh:
            return apply.template operator()<Scene::MeshComponentUVE>();
        case EditorSceneComponentKindUVE::Light:
            return apply.template operator()<Scene::LightComponentUVE>();
        case EditorSceneComponentKindUVE::Collider:
            return apply.template operator()<Scene::ColliderComponentUVE>();
        case EditorSceneComponentKindUVE::RigidBody:
            return apply.template operator()<Scene::RigidBodyComponentUVE>();
        case EditorSceneComponentKindUVE::AudioSource:
            return apply.template operator()<Scene::AudioSourceComponentUVE>();
        case EditorSceneComponentKindUVE::ParticleEmitter:
            return apply.template operator()<Scene::ParticleEmitterComponentUVE>();
        case EditorSceneComponentKindUVE::Script:
            return apply.template operator()<Scene::ScriptComponentUVE>();
        case EditorSceneComponentKindUVE::AnimationPlayer:
            return apply.template operator()<Scene::AnimationPlayerComponentUVE>();
        case EditorSceneComponentKindUVE::WorldEnvironment:
            return apply.template operator()<Scene::WorldEnvironment3DNodeComponentUVE>();
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
        case EditorSceneComponentKindUVE::RigidBody:
            if (entityManager.HasComponentUVE<Scene::RigidBodyComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::RigidBodyComponentUVE>(m_selectedEntity);
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
        case EditorSceneComponentKindUVE::AnimationPlayer:
            if (entityManager.HasComponentUVE<Scene::AnimationPlayerComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::AnimationPlayerComponentUVE>(m_selectedEntity);
            }
            break;
        case EditorSceneComponentKindUVE::WorldEnvironment:
            if (entityManager.HasComponentUVE<Scene::WorldEnvironment3DNodeComponentUVE>(m_selectedEntity)) {
                before = entityManager.GetComponentUVE<Scene::WorldEnvironment3DNodeComponentUVE>(m_selectedEntity);
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
    }
    if (before.has_value() && AreSceneComponentValuesEqualUVE(*before, value)) {
        return false;
    }

    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    if (!ApplySceneComponentStateUVE(m_selectedEntity, kind, value)) {
        return false;
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
        case EditorSceneComponentKindUVE::RigidBody:
            if (entityManager.HasComponentUVE<Scene::RigidBodyComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::RigidBodyComponentUVE>(m_selectedEntity);
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
        case EditorSceneComponentKindUVE::AnimationPlayer:
            if (entityManager.HasComponentUVE<Scene::AnimationPlayerComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::AnimationPlayerComponentUVE>(m_selectedEntity);
            break;
        case EditorSceneComponentKindUVE::WorldEnvironment:
            if (entityManager.HasComponentUVE<Scene::WorldEnvironment3DNodeComponentUVE>(m_selectedEntity)) before = entityManager.GetComponentUVE<Scene::WorldEnvironment3DNodeComponentUVE>(m_selectedEntity);
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
    }
    if (!before.has_value()) {
        return false;
    }

    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    if (!ApplySceneComponentStateUVE(m_selectedEntity, kind, std::nullopt)) {
        return false;
    }
    m_sceneDirty = true;
    RecordHistoryUVE(SceneComponentHistoryEntryUVE{
        m_selectedEntity, kind, before, std::nullopt, selectionBefore, CaptureSelectionSnapshotUVE(), dirtyBefore, true});
    return true;
}

bool EditorUVE::SetSelectedEntityNameUVE(std::string name) {
    if (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE() ||
        !IsEntityNameValidUVE(name)) {
        return false;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    std::optional<std::string> beforeName;
    if (entityManager.HasComponentUVE<Scene::NameComponentUVE>(m_selectedEntity)) {
        beforeName = entityManager.GetComponentUVE<Scene::NameComponentUVE>(m_selectedEntity).name;
        if (*beforeName == name) {
            return false;
        }
    }

    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    const std::optional<std::string> afterName{std::move(name)};
    if (!ApplyEntityNameStateUVE(m_selectedEntity, afterName)) {
        return false;
    }

    m_sceneDirty = true;
    RecordHistoryUVE(NameHistoryEntryUVE{
        m_selectedEntity, beforeName, afterName, selectionBefore, CaptureSelectionSnapshotUVE(), dirtyBefore, true});
    return true;
}

bool EditorUVE::TranslateSelectedAlongAxisUVE(const EditorTransformAxisUVE axis, const float worldDistance) {
    if (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE() ||
        !IsFiniteUVE(worldDistance) || axis == EditorTransformAxisUVE::None) {
        return false;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity)) {
        return false;
    }

    const float snappedDistance = m_transformSnappingSettings.enabled
                                      ? SnapScalarUVE(worldDistance, m_transformSnappingSettings.translateStep)
                                      : worldDistance;
    Math::Vector3UVE localDelta{};
    const Math::Vector3UVE worldDelta = GetAxisVectorUVE(axis) * snappedDistance;
    if (!ComputeLocalDeltaForWorldDeltaUVE(m_selectedEntity, worldDelta, localDelta)) {
        return false;
    }

    Scene::TransformComponentUVE updated =
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity);
    updated.localPosition += localDelta;
    return SetSelectedLocalTransformUVE(updated);
}

bool EditorUVE::RotateSelectedAroundWorldAxisUVE(const EditorTransformAxisUVE axis, const float radians) {
    if (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE() ||
        !IsFiniteUVE(radians) || axis == EditorTransformAxisUVE::None) {
        return false;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity)) {
        return false;
    }

    const float rotateStepRadians =
        (m_transformSnappingSettings.rotateStepDegrees * std::numbers::pi_v<float>) / 180.0F;
    const float snappedRadians = m_transformSnappingSettings.enabled
                                     ? SnapScalarUVE(radians, rotateStepRadians)
                                     : radians;
    Scene::TransformComponentUVE updated =
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity);
    Math::QuaternionUVE localRotation{};
    if (!ComputeLocalRotationForWorldAxisUVE(m_selectedEntity, updated.localRotation, GetAxisVectorUVE(axis),
                                             snappedRadians, localRotation)) {
        return false;
    }
    updated.localRotation = localRotation;
    return SetSelectedLocalTransformUVE(updated);
}

bool EditorUVE::ScaleSelectedAlongAxisUVE(const EditorTransformAxisUVE axis,
                                           const float localScaleDelta) {
    if (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE() ||
        !IsFiniteUVE(localScaleDelta) || axis == EditorTransformAxisUVE::None) {
        return false;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity)) {
        return false;
    }

    Scene::TransformComponentUVE updated =
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity);
    float* component = nullptr;
    switch (axis) {
        case EditorTransformAxisUVE::X:
            component = &updated.localScale.x;
            break;
        case EditorTransformAxisUVE::Y:
            component = &updated.localScale.y;
            break;
        case EditorTransformAxisUVE::Z:
            component = &updated.localScale.z;
            break;
        case EditorTransformAxisUVE::None:
            return false;
    }
    const float snappedScaleDelta = m_transformSnappingSettings.enabled
                                        ? SnapScalarUVE(localScaleDelta, m_transformSnappingSettings.scaleStep)
                                        : localScaleDelta;
    *component += snappedScaleDelta;
    if (!IsFiniteUVE(*component) || *component < kMinimumLocalScaleUVE) {
        return false;
    }
    return SetSelectedLocalTransformUVE(updated);
}

bool EditorUVE::ScaleSelectedUniformlyUVE(const float localScaleOffset) {
    if (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE() ||
        !IsFiniteUVE(localScaleOffset)) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity)) {
        return false;
    }
    const float snappedOffset = m_transformSnappingSettings.enabled
                                    ? SnapScalarUVE(localScaleOffset, m_transformSnappingSettings.scaleStep)
                                    : localScaleOffset;
    Scene::TransformComponentUVE updated =
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity);
    updated.localScale.x += snappedOffset;
    updated.localScale.y += snappedOffset;
    updated.localScale.z += snappedOffset;
    if (!IsFiniteUVE(snappedOffset) || !IsFiniteUVE(updated.localScale.x) ||
        !IsFiniteUVE(updated.localScale.y) || !IsFiniteUVE(updated.localScale.z) ||
        updated.localScale.x < kMinimumLocalScaleUVE || updated.localScale.y < kMinimumLocalScaleUVE ||
        updated.localScale.z < kMinimumLocalScaleUVE) {
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
    m_transformSnappingSettings = settings;
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

Scene::EntityUVE EditorUVE::CreateDocumentSceneNodeUVE(
    const Scene::Nodes::SceneNodeKindUVE kind) {
    if (!IsAuthoringCommandAllowedUVE() || m_selectedEntities.size() > 1U) {
        return Scene::kInvalidEntityUVE;
    }
    const Scene::Nodes::SceneNodeDescriptorUVE* descriptor =
        Scene::Nodes::FindSceneNodeDescriptorUVE(kind);
    if (descriptor == nullptr || !descriptor->libraryCreatable) {
        return Scene::kInvalidEntityUVE;
    }

    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const auto createNodeWithComponent = [this, &entityManager](auto component) {
        Scene::EntityUVE created = CreateDocumentEntityInternalUVE(EditorEntityKindUVE::Empty, std::nullopt);
        if (created != Scene::kInvalidEntityUVE) {
            using Component = std::decay_t<decltype(component)>;
            entityManager.AddComponentUVE<Component>(created, std::move(component));
        }
        return created;
    };

    switch (kind) {
        // The 17 kinds whose authored data already lives in a shared component each create from
        // their NodeDefinition in Engine/Runtime/Nodes/3D — one .h + .cpp per kind holds the
        // recipe (components to attach, authored defaults, default entity name). No
        // node-kind-specific recipe is authored in this switch anymore.
        case Scene::Nodes::SceneNodeKindUVE::Node3D:
            entity = CreateNodeDefinitionEntityInternalUVE(Scene::Node3DNodeDefinitionUVE{},
                                                            Scene::ApplyNode3DNodeDefinitionUVE);
            break;
        case Scene::Nodes::SceneNodeKindUVE::Camera3D:
            entity = CreateNodeDefinitionEntityInternalUVE(Scene::Camera3DNodeDefinitionUVE{},
                                                            Scene::ApplyCamera3DNodeDefinitionUVE);
            break;
        case Scene::Nodes::SceneNodeKindUVE::MeshInstance3D:
            entity = CreateNodeDefinitionEntityInternalUVE(Scene::MeshInstance3DNodeDefinitionUVE{},
                                                            Scene::ApplyMeshInstance3DNodeDefinitionUVE);
            break;
        case Scene::Nodes::SceneNodeKindUVE::BoxMesh3D:
            entity = CreateNodeDefinitionEntityInternalUVE(Scene::BoxMesh3DNodeDefinitionUVE{},
                                                            Scene::ApplyBoxMesh3DNodeDefinitionUVE);
            break;
        case Scene::Nodes::SceneNodeKindUVE::SphereMesh3D:
            entity = CreateNodeDefinitionEntityInternalUVE(Scene::SphereMesh3DNodeDefinitionUVE{},
                                                            Scene::ApplySphereMesh3DNodeDefinitionUVE);
            break;
        case Scene::Nodes::SceneNodeKindUVE::PlaneMesh3D:
            entity = CreateNodeDefinitionEntityInternalUVE(Scene::PlaneMesh3DNodeDefinitionUVE{},
                                                            Scene::ApplyPlaneMesh3DNodeDefinitionUVE);
            break;
        case Scene::Nodes::SceneNodeKindUVE::Light3D:
            entity = CreateNodeDefinitionEntityInternalUVE(Scene::Light3DNodeDefinitionUVE{},
                                                            Scene::ApplyLight3DNodeDefinitionUVE);
            break;
        case Scene::Nodes::SceneNodeKindUVE::Collider3D:
            entity = CreateNodeDefinitionEntityInternalUVE(Scene::Collider3DNodeDefinitionUVE{},
                                                            Scene::ApplyCollider3DNodeDefinitionUVE);
            break;
        case Scene::Nodes::SceneNodeKindUVE::CharacterBody3D:
            entity = CreateNodeDefinitionEntityInternalUVE(Scene::CharacterBody3DNodeDefinitionUVE{},
                                                            Scene::ApplyCharacterBody3DNodeDefinitionUVE);
            break;
        case Scene::Nodes::SceneNodeKindUVE::RigidBody3D:
            entity = CreateNodeDefinitionEntityInternalUVE(Scene::RigidBody3DNodeDefinitionUVE{},
                                                            Scene::ApplyRigidBody3DNodeDefinitionUVE);
            break;
        case Scene::Nodes::SceneNodeKindUVE::AnimationPlayer:
            entity = CreateNodeDefinitionEntityInternalUVE(Scene::AnimationPlayerNodeDefinitionUVE{},
                                                            Scene::ApplyAnimationPlayerNodeDefinitionUVE);
            break;
        case Scene::Nodes::SceneNodeKindUVE::AudioSource3D:
            entity = CreateNodeDefinitionEntityInternalUVE(Scene::AudioSource3DNodeDefinitionUVE{},
                                                            Scene::ApplyAudioSource3DNodeDefinitionUVE);
            break;
        case Scene::Nodes::SceneNodeKindUVE::ParticleEmitter3D:
            entity = CreateNodeDefinitionEntityInternalUVE(Scene::ParticleEmitter3DNodeDefinitionUVE{},
                                                            Scene::ApplyParticleEmitter3DNodeDefinitionUVE);
            break;
        case Scene::Nodes::SceneNodeKindUVE::Script:
            entity = CreateNodeDefinitionEntityInternalUVE(Scene::ScriptNodeDefinitionUVE{},
                                                            Scene::ApplyScriptNodeDefinitionUVE);
            break;
        // CanvasLayer family — the four UI kinds promoted out of the Inspector-only world, so
        // UI authoring uses the same Add-Node entry point (definitions live in
        // Engine/Runtime/Nodes/CanvasLayer).
        case Scene::Nodes::SceneNodeKindUVE::Canvas:
            entity = CreateNodeDefinitionEntityInternalUVE(Scene::CanvasNodeDefinitionUVE{},
                                                            Scene::ApplyCanvasNodeDefinitionUVE);
            break;
        case Scene::Nodes::SceneNodeKindUVE::UIText:
            entity = CreateNodeDefinitionEntityInternalUVE(Scene::UITextNodeDefinitionUVE{},
                                                            Scene::ApplyUITextNodeDefinitionUVE);
            break;
        case Scene::Nodes::SceneNodeKindUVE::UIImage:
            entity = CreateNodeDefinitionEntityInternalUVE(Scene::UIImageNodeDefinitionUVE{},
                                                            Scene::ApplyUIImageNodeDefinitionUVE);
            break;
        case Scene::Nodes::SceneNodeKindUVE::UIButton:
            entity = CreateNodeDefinitionEntityInternalUVE(Scene::UIButtonNodeDefinitionUVE{},
                                                            Scene::ApplyUIButtonNodeDefinitionUVE);
            break;
        case Scene::Nodes::SceneNodeKindUVE::Area3D:
            entity = CreateNodeDefinitionEntityInternalUVE(Scene::Area3DNodeDefinitionUVE{},
                                                            Scene::ApplyArea3DNodeDefinitionUVE);
            break;
        case Scene::Nodes::SceneNodeKindUVE::RayCast3D:
            entity = createNodeWithComponent(Scene::RayCast3DNodeComponentUVE{});
            break;
        case Scene::Nodes::SceneNodeKindUVE::StaticBody3D:
            entity = CreateNodeDefinitionEntityInternalUVE(Scene::StaticBody3DNodeDefinitionUVE{},
                                                            Scene::ApplyStaticBody3DNodeDefinitionUVE);
            break;
        case Scene::Nodes::SceneNodeKindUVE::AnimatableBody3D:
            entity = CreateNodeDefinitionEntityInternalUVE(Scene::AnimatableBody3DNodeDefinitionUVE{},
                                                            Scene::ApplyAnimatableBody3DNodeDefinitionUVE);
            break;
        case Scene::Nodes::SceneNodeKindUVE::NavigationRegion3D:
            entity = createNodeWithComponent(Scene::NavigationRegion3DNodeComponentUVE{});
            break;
        case Scene::Nodes::SceneNodeKindUVE::NavigationAgent3D:
            entity = createNodeWithComponent(Scene::NavigationAgent3DNodeComponentUVE{});
            break;
        case Scene::Nodes::SceneNodeKindUVE::Skeleton3D:
            entity = createNodeWithComponent(Scene::Skeleton3DNodeComponentUVE{});
            break;
        case Scene::Nodes::SceneNodeKindUVE::BoneAttachment3D:
            entity = createNodeWithComponent(Scene::BoneAttachment3DNodeComponentUVE{});
            break;
        case Scene::Nodes::SceneNodeKindUVE::SpringArm3D:
            entity = createNodeWithComponent(Scene::SpringArm3DNodeComponentUVE{});
            break;
        case Scene::Nodes::SceneNodeKindUVE::Marker3D:
            entity = createNodeWithComponent(Scene::Marker3DNodeComponentUVE{});
            break;
        case Scene::Nodes::SceneNodeKindUVE::Hitbox3D:
            entity = createNodeWithComponent(Scene::Hitbox3DNodeComponentUVE{});
            break;
        case Scene::Nodes::SceneNodeKindUVE::Hurtbox3D:
            entity = createNodeWithComponent(Scene::Hurtbox3DNodeComponentUVE{});
            break;
        case Scene::Nodes::SceneNodeKindUVE::Projectile3D:
            entity = createNodeWithComponent(Scene::Projectile3DNodeComponentUVE{});
            break;
        case Scene::Nodes::SceneNodeKindUVE::InteractionArea3D:
            entity = createNodeWithComponent(Scene::InteractionArea3DNodeComponentUVE{});
            break;
        case Scene::Nodes::SceneNodeKindUVE::WorldEnvironment3D:
            entity = createNodeWithComponent(Scene::WorldEnvironment3DNodeComponentUVE{});
            break;
        case Scene::Nodes::SceneNodeKindUVE::ReflectionProbe3D:
            entity = createNodeWithComponent(Scene::ReflectionProbe3DNodeComponentUVE{});
            break;
        case Scene::Nodes::SceneNodeKindUVE::Decal3D:
            entity = createNodeWithComponent(Scene::Decal3DNodeComponentUVE{});
            break;
        case Scene::Nodes::SceneNodeKindUVE::LODGroup3D:
            entity = createNodeWithComponent(Scene::LodGroup3DNodeComponentUVE{});
            break;
        case Scene::Nodes::SceneNodeKindUVE::Occluder3D:
            entity = createNodeWithComponent(Scene::Occluder3DNodeComponentUVE{});
            break;
        case Scene::Nodes::SceneNodeKindUVE::VisibilityRegion3D:
            entity = createNodeWithComponent(Scene::VisibilityRegion3DNodeComponentUVE{});
            break;
        case Scene::Nodes::SceneNodeKindUVE::SpawnPoint3D:
            entity = createNodeWithComponent(Scene::SpawnPoint3DNodeComponentUVE{});
            break;
        case Scene::Nodes::SceneNodeKindUVE::LevelStreamer3D:
            entity = createNodeWithComponent(Scene::LevelStreamer3DNodeComponentUVE{});
            break;
        case Scene::Nodes::SceneNodeKindUVE::WorldPartition3D:
            entity = createNodeWithComponent(Scene::WorldPartition3DNodeComponentUVE{});
            break;
        case Scene::Nodes::SceneNodeKindUVE::AnimationTree:
            // Not library-creatable until the real animation pipeline exists; the kind's
            // per-file home (definition only, deliberately no Apply) is
            // uve/nodes/3d/animation_tree_uve.h.
            return Scene::kInvalidEntityUVE;
        case Scene::Nodes::SceneNodeKindUVE::SceneRoot:
            // The scene root is created by the document lifecycle
            // (EnsureDocumentSceneRootUVE), never through the library path.
            return Scene::kInvalidEntityUVE;
    }

    if (entity == Scene::kInvalidEntityUVE) {
        return Scene::kInvalidEntityUVE;
    }

    // New nodes join the hierarchy instead of becoming document roots: under the single
    // selection when there is one, otherwise directly under the scene root.
    Scene::EntityUVE parentNode = Scene::kInvalidEntityUVE;
    if (m_selectedEntity != Scene::kInvalidEntityUVE && IsDocumentSubtreeUVE(m_selectedEntity)) {
        parentNode = m_selectedEntity;
    }
    if (parentNode == Scene::kInvalidEntityUVE) {
        parentNode = EnsureDocumentSceneRootUVE();
    }
    if (parentNode != Scene::kInvalidEntityUVE) {
        m_services->GetSceneGraphUVE().SetParentUVE(entityManager, entity, parentNode);
        InvalidateHierarchyFilterCacheUVE();
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
    RecordHistoryUVE(SceneNodeCreationHistoryEntryUVE{
        *snapshot, kind, entity, selectionBefore, CaptureSelectionSnapshotUVE(), dirtyBefore, true,
        parentNode});
    return entity;
}

Scene::EntityUVE EditorUVE::DuplicateSelectedEntityUVE() {
    if (!IsLifecycleCommandAllowedUVE() || !IsDocumentEntityUVE(m_selectedEntity) ||
        IsSceneRootEntityUVE(m_selectedEntity)) {
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
        CaptureSelectionSnapshotUVE(), dirtyBefore, true});
    return duplicate;
}

bool EditorUVE::DeleteSelectedEntityUVE() {
    if (!IsLifecycleCommandAllowedUVE() || !IsDocumentEntityUVE(m_selectedEntity) ||
        IsSceneRootEntityUVE(m_selectedEntity)) {
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
    RecordHistoryUVE(DeletionHistoryEntryUVE{
        std::move(*snapshot), originalParent, target, selectionBefore, CaptureSelectionSnapshotUVE(), dirtyBefore, true});
    return true;
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

    Math::Vector3UVE parentPosition{};
    Math::Vector3UVE parentScale{1.0F, 1.0F, 1.0F};
    Math::QuaternionUVE parentRotation{0.0F, 0.0F, 0.0F, 1.0F};
    if (newParent != Scene::kInvalidEntityUVE) {
        if (!IsDocumentEntityUVE(newParent) ||
            !entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(newParent)) {
            return false;
        }
        const Scene::WorldTransformComponentUVE& parentWorld =
            entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(newParent);
        if (parentWorld.dirty || !IsFiniteVectorUVE(parentWorld.worldPosition) ||
            !IsFiniteVectorUVE(parentWorld.worldScale) ||
            !Math::TryNormalizeUVE(parentWorld.worldRotation, parentRotation) ||
            parentWorld.worldScale.x < kMinimumLocalScaleUVE ||
            parentWorld.worldScale.y < kMinimumLocalScaleUVE ||
            parentWorld.worldScale.z < kMinimumLocalScaleUVE) {
            return false;
        }
        const bool nonUniform = std::abs(parentWorld.worldScale.x - parentWorld.worldScale.y) > kVectorEpsilonUVE ||
                                std::abs(parentWorld.worldScale.x - parentWorld.worldScale.z) > kVectorEpsilonUVE ||
                                std::abs(parentWorld.worldScale.y - parentWorld.worldScale.z) > kVectorEpsilonUVE;
        const bool rotated = std::abs(parentRotation.x) > kVectorEpsilonUVE ||
                             std::abs(parentRotation.y) > kVectorEpsilonUVE ||
                             std::abs(parentRotation.z) > kVectorEpsilonUVE ||
                             std::abs(std::abs(parentRotation.w) - 1.0F) > kVectorEpsilonUVE;
        if (nonUniform && rotated) {
            return false;
        }
        parentPosition = parentWorld.worldPosition;
        parentScale = parentWorld.worldScale;
    }

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

bool EditorUVE::ReparentDocumentEntityUVE(const Scene::EntityUVE entity, const Scene::EntityUVE newParent) {
    if (!IsLifecycleCommandAllowedUVE() || IsSceneRootEntityUVE(entity) || !HasSceneGraphNodeUVE(entity) ||
        !IsDocumentSubtreeUVE(entity) ||
        (newParent != Scene::kInvalidEntityUVE && !HasSceneGraphNodeUVE(newParent)) ||
        entity == newParent || DoesSubtreeContainEntityUVE(entity, newParent)) {
        return false;
    }
    // One-root documents: "move to document root" means becoming a direct child of the
    // scene root - nothing but the root itself may sit at top level.
    const Scene::EntityUVE effectiveParent =
        newParent == Scene::kInvalidEntityUVE ? EnsureDocumentSceneRootUVE() : newParent;
    if (effectiveParent == Scene::kInvalidEntityUVE || entity == effectiveParent ||
        DoesSubtreeContainEntityUVE(entity, effectiveParent)) {
        return false;
    }
    Scene::EntityUVE parentBefore = Scene::kInvalidEntityUVE;
    if (!TryGetDocumentParentUVE(entity, parentBefore) || parentBefore == effectiveParent) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity)) {
        return false;
    }
    const Scene::TransformComponentUVE localBefore =
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
    Scene::TransformComponentUVE localAfter = localBefore;
    if (m_reparentTransformMode == EditorReparentTransformModeUVE::KeepWorld &&
        !ComputeKeepWorldLocalTransformUVE(entity, effectiveParent, localAfter)) {
        return false;
    }
    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    m_services->GetSceneGraphUVE().SetParentUVE(entityManager, entity, effectiveParent);
    if (!ApplyLocalTransformUVE(entity, localAfter)) {
        m_services->GetSceneGraphUVE().SetParentUVE(entityManager, entity, parentBefore);
        static_cast<void>(ApplyLocalTransformUVE(entity, localBefore));
        return false;
    }
    RestoreSelectionUVE(EditorSelectionSnapshotUVE{{entity}, entity});
    m_sceneDirty = true;
    InvalidateHierarchyFilterCacheUVE();
    RecordHistoryUVE(ReparentHistoryEntryUVE{
        entity, parentBefore, newParent, localBefore, localAfter, selectionBefore, CaptureSelectionSnapshotUVE(),
        dirtyBefore, true});
    return true;
}

bool EditorUVE::UndoUVE() {
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

std::string EditorUVE::GetOutlinerTypeTagUVE(const Scene::EntityUVE entity) const {
    if (!IsDocumentEntityUVE(entity)) {
        return {};
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (entityManager.HasComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity)) {
        switch (entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity).kind) {
            case Scene::PrimitiveMeshKindUVE::Plane:
                return std::string{Scene::PlaneMesh3DNodeDefinitionUVE::defaultName};
            case Scene::PrimitiveMeshKindUVE::UVSphere:
                return std::string{Scene::SphereMesh3DNodeDefinitionUVE::defaultName};
            case Scene::PrimitiveMeshKindUVE::Cube:
                return std::string{Scene::BoxMesh3DNodeDefinitionUVE::defaultName};
        }
    }
    if (entityManager.HasComponentUVE<Scene::CameraComponentUVE>(entity)) {
        return std::string{Scene::Camera3DNodeDefinitionUVE::defaultName};
    }
    if (entityManager.HasComponentUVE<Scene::LightComponentUVE>(entity) &&
        entityManager.GetComponentUVE<Scene::LightComponentUVE>(entity).type == Scene::LightTypeUVE::Directional) {
        return std::string{Scene::Light3DNodeDefinitionUVE::defaultName};
    }
    if (entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(entity)) {
        return std::string{Scene::Collider3DNodeDefinitionUVE::defaultName};
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
    return candidates;
}

std::string EditorUVE::GetHierarchyCandidateLabelUVE(const Scene::EntityUVE entity) const {
    return GetEntityDisplayLabelUVE(entity);
}

bool EditorUVE::IsLifecycleCommandAllowedUVE() const noexcept {
    return IsAuthoringCommandAllowedUVE() && HasSingleDocumentSelectionUVE();
}

bool EditorUVE::IsAuthoringCommandAllowedUVE() const noexcept {
    return m_state == EditorStateUVE::Running && m_playModeState == EditorPlayModeStateUVE::Edit;
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

void EditorUVE::RestoreSelectionUVE(EditorSelectionSnapshotUVE selection) noexcept {
    std::vector<Scene::EntityUVE> restored;
    restored.reserve(selection.entities.size());
    for (const Scene::EntityUVE entity : selection.entities) {
        if (IsDocumentEntityUVE(entity) && std::find(restored.begin(), restored.end(), entity) == restored.end()) {
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
    if (changed) {
        CancelHierarchyRenameUVE();
    }
}

void EditorUVE::PruneSelectionUVE() noexcept {
    RestoreSelectionUVE(CaptureSelectionSnapshotUVE());
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
Scene::EntityUVE EditorUVE::CreateNodeDefinitionEntityInternalUVE(const Definition& definition,
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

    // The per-kind recipes below deliberately live with their node definitions in
    // Engine/Runtime/Nodes/3D (one .h + .cpp per kind), not inline here: these legacy editor
    // entity kinds are alternate doors into the exact same recipes the scene-node Add-Node list
    // uses, so a kind's defaults have one home, not two.
    switch (kind) {
        case EditorEntityKindUVE::Empty:
            break;
        case EditorEntityKindUVE::Camera:
            Scene::ApplyCamera3DNodeDefinitionUVE(entityManager, entity, Scene::Camera3DNodeDefinitionUVE{});
            break;
        case EditorEntityKindUVE::DirectionalLight:
            Scene::ApplyLight3DNodeDefinitionUVE(entityManager, entity, Scene::Light3DNodeDefinitionUVE{});
            break;
        case EditorEntityKindUVE::CollisionBox:
            Scene::ApplyCollider3DNodeDefinitionUVE(entityManager, entity, Scene::Collider3DNodeDefinitionUVE{});
            break;
        case EditorEntityKindUVE::Cube:
            Scene::ApplyBoxMesh3DNodeDefinitionUVE(entityManager, entity, Scene::BoxMesh3DNodeDefinitionUVE{});
            break;
        case EditorEntityKindUVE::UVSphere:
            Scene::ApplySphereMesh3DNodeDefinitionUVE(entityManager, entity, Scene::SphereMesh3DNodeDefinitionUVE{});
            break;
        case EditorEntityKindUVE::Plane:
            Scene::ApplyPlaneMesh3DNodeDefinitionUVE(entityManager, entity, Scene::PlaneMesh3DNodeDefinitionUVE{});
            break;
        default:
            return Scene::kInvalidEntityUVE;
    }

    // Same hierarchy-joining rule as CreateDocumentSceneNodeUVE: under the single selection
    // when there is one, otherwise directly under the scene root - never a new document root.
    if (entity != Scene::kInvalidEntityUVE) {
        Scene::EntityUVE parentNode = Scene::kInvalidEntityUVE;
        if (m_selectedEntity != Scene::kInvalidEntityUVE && IsDocumentSubtreeUVE(m_selectedEntity)) {
            parentNode = m_selectedEntity;
        }
        if (parentNode == Scene::kInvalidEntityUVE) {
            parentNode = EnsureDocumentSceneRootUVE();
        }
        if (parentNode != Scene::kInvalidEntityUVE) {
            m_services->GetSceneGraphUVE().SetParentUVE(entityManager, entity, parentNode);
            InvalidateHierarchyFilterCacheUVE();
        }
    }
    return entity;
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
            } else if constexpr (std::is_same_v<EntryType, SceneNodeCreationHistoryEntryUVE>) {
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
                typedEntry.activeEntity = restored;
                typedEntry.selectionBefore = EditorSelectionSnapshotUVE{{restored}, restored};
                RestoreSelectionUVE(typedEntry.selectionBefore);
                m_sceneDirty = typedEntry.dirtyBefore;
                return true;
            } else {
                if (!HasSceneGraphNodeUVE(typedEntry.entity) ||
                    (typedEntry.parentBefore != Scene::kInvalidEntityUVE &&
                     !HasSceneGraphNodeUVE(typedEntry.parentBefore)) ||
                    DoesSubtreeContainEntityUVE(typedEntry.entity, typedEntry.parentBefore)) {
                    return false;
                }
                m_services->GetSceneGraphUVE().SetParentUVE(
                    m_services->GetEntityManagerUVE(), typedEntry.entity, typedEntry.parentBefore);
                if (!ApplyLocalTransformUVE(typedEntry.entity, typedEntry.localTransformBefore)) {
                    return false;
                }
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
            } else if constexpr (std::is_same_v<EntryType, SceneNodeCreationHistoryEntryUVE>) {
                if (typedEntry.activeEntity != Scene::kInvalidEntityUVE &&
                    m_services->GetEntityManagerUVE().IsAliveUVE(typedEntry.activeEntity)) {
                    return false;
                }
                Scene::EntityUVE redoParent = typedEntry.createdUnderParent;
                if (redoParent == Scene::kInvalidEntityUVE || !IsDocumentEntityUVE(redoParent)) {
                    // The original parent is gone (deleted by later history, say) - re-home
                    // under the scene root rather than dropping the node to document top level.
                    redoParent = EnsureDocumentSceneRootUVE();
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
            } else {
                if (!HasSceneGraphNodeUVE(typedEntry.entity) ||
                    (typedEntry.parentAfter != Scene::kInvalidEntityUVE &&
                     !HasSceneGraphNodeUVE(typedEntry.parentAfter)) ||
                    DoesSubtreeContainEntityUVE(typedEntry.entity, typedEntry.parentAfter)) {
                    return false;
                }
                m_services->GetSceneGraphUVE().SetParentUVE(
                    m_services->GetEntityManagerUVE(), typedEntry.entity, typedEntry.parentAfter);
                if (!ApplyLocalTransformUVE(typedEntry.entity, typedEntry.localTransformAfter)) {
                    return false;
                }
                RestoreSelectionUVE(typedEntry.selectionAfter);
                m_sceneDirty = typedEntry.dirtyAfter;
                InvalidateHierarchyFilterCacheUVE();
                return true;
            }
        },
        entry);
}

bool EditorUVE::IsSceneRootEntityUVE(const Scene::EntityUVE entity) const {
    return entity != Scene::kInvalidEntityUVE && m_services->GetEntityManagerUVE().IsAliveUVE(entity) &&
           m_services->GetEntityManagerUVE().HasComponentUVE<Scene::SceneRootComponentUVE>(entity);
}

Scene::EntityUVE EditorUVE::GetDocumentSceneRootUVE() {
    Scene::EntityUVE found = Scene::kInvalidEntityUVE;
    m_services->GetEntityManagerUVE().ForEachUVE<Scene::SceneRootComponentUVE>(
        [&found](const Scene::EntityUVE entity, const Scene::SceneRootComponentUVE&) {
            if (found == Scene::kInvalidEntityUVE) {
                found = entity; // markers are guarded to exactly one; first hit is THE root
            }
        });
    return found;
}

Scene::EntityUVE EditorUVE::EnsureDocumentSceneRootUVE() {
    const Scene::EntityUVE existing = GetDocumentSceneRootUVE();
    if (existing != Scene::kInvalidEntityUVE) {
        return existing;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const Scene::EntityUVE root =
        CreateDocumentEntityShellInternalUVE(Scene::SceneRootNodeDefinitionUVE::defaultName);
    if (root == Scene::kInvalidEntityUVE) {
        return Scene::kInvalidEntityUVE;
    }
    Scene::ApplySceneRootNodeDefinitionUVE(entityManager, root, Scene::SceneRootNodeDefinitionUVE{});
    return root;
}

std::vector<Scene::EntityUVE> EditorUVE::GetDocumentRootsUVE() {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    std::vector<Scene::EntityUVE> roots =
        m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, Scene::kInvalidEntityUVE);
    // Internal engine/editor infrastructure entities (e.g. the Viewport's hidden free-look-camera
    // proxy) are also scene roots (AttachTransformUVE always creates one), but must never be
    // treated as document content - this is the one place that decision needs to be made, since
    // every other document-root consumer (Play-mode snapshot capture/restore, the Scene Hierarchy
    // panel, ClearDocumentSceneUVE, etc.) already reaches roots exclusively through this function.
    roots.erase(std::remove_if(roots.begin(), roots.end(),
                               [&entityManager](const Scene::EntityUVE entity) {
                                   return entityManager.HasComponentUVE<Scene::EditorInternalEntityComponentUVE>(
                                       entity);
                               }),
               roots.end());
    return roots;
}

EditorStateUVE EditorUVE::GetStateUVE() const noexcept {
    return m_state;
}

Scene::EntityUVE EditorUVE::GetSelectedEntityUVE() const noexcept {
    return m_selectedEntity;
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

Scripting::ScriptGraphCanvasUVE& EditorUVE::GetVisualScriptCanvasUVE() noexcept {
    return ActiveVisualScriptCanvasUVE();
}

Scripting::ScriptGraphCanvasUVE& EditorUVE::ActiveVisualScriptCanvasUVE() noexcept {
    return *m_visualScriptBranches[m_activeVisualScriptBranch].canvas;
}

const Scripting::ScriptGraphCanvasUVE& EditorUVE::ActiveVisualScriptCanvasUVE() const noexcept {
    return *m_visualScriptBranches[m_activeVisualScriptBranch].canvas;
}

std::vector<std::string> EditorUVE::GetVisualScriptBranchNamesUVE() const {
    std::vector<std::string> names;
    names.reserve(m_visualScriptBranches.size());
    for (const ScriptBranchUVE& branch : m_visualScriptBranches) {
        names.push_back(branch.name);
    }
    return names;
}

const std::string& EditorUVE::GetActiveVisualScriptBranchNameUVE() const noexcept {
    return m_visualScriptBranches[m_activeVisualScriptBranch].name;
}

bool EditorUVE::CreateVisualScriptBranchUVE(std::string name) {
    const auto invalidName = [&name] {
        return name.empty() || name.size() > 96U ||
               std::any_of(name.begin(), name.end(), [](const char value) {
                   return std::iscntrl(static_cast<unsigned char>(value)) != 0 || value == '/' || value == 0x5c;
               });
    };
    if (m_state != EditorStateUVE::Running || invalidName() ||
        std::any_of(m_visualScriptBranches.begin(), m_visualScriptBranches.end(),
                    [&name](const ScriptBranchUVE& branch) { return branch.name == name; })) {
        return false;
    }
    m_visualScriptBranches.push_back(
        ScriptBranchUVE{std::move(name), std::make_unique<Scripting::ScriptGraphCanvasUVE>(
                            m_visualScriptRegistry, m_historyCapacity)});
    m_activeVisualScriptBranch = m_visualScriptBranches.size() - 1U;
    return true;
}

bool EditorUVE::SelectVisualScriptBranchUVE(std::string name) {
    if (m_state != EditorStateUVE::Running) {
        return false;
    }
    const auto iterator = std::find_if(m_visualScriptBranches.begin(), m_visualScriptBranches.end(),
                                       [&name](const ScriptBranchUVE& branch) { return branch.name == name; });
    if (iterator == m_visualScriptBranches.end()) {
        return false;
    }
    m_activeVisualScriptBranch = static_cast<std::size_t>(std::distance(m_visualScriptBranches.begin(), iterator));
    return true;
}

bool EditorUVE::RenameActiveVisualScriptBranchUVE(std::string name) {
    const auto invalidName = [&name] {
        return name.empty() || name.size() > 96U ||
               std::any_of(name.begin(), name.end(), [](const char value) {
                   return std::iscntrl(static_cast<unsigned char>(value)) != 0 || value == '/' || value == 0x5c;
               });
    };
    if (m_state != EditorStateUVE::Running || invalidName() ||
        std::any_of(m_visualScriptBranches.begin(), m_visualScriptBranches.end(),
                    [this, &name](const ScriptBranchUVE& branch) {
                        return &branch != &m_visualScriptBranches[m_activeVisualScriptBranch] && branch.name == name;
                    })) {
        return false;
    }
    m_visualScriptBranches[m_activeVisualScriptBranch].name = std::move(name);
    return true;
}

std::filesystem::path ScriptWorkspacePathUVE(const std::filesystem::path& scenePath) {
    std::filesystem::path path = scenePath;
    path.replace_extension(".scripting");
    return path.empty() ? std::filesystem::path{"main.scripting"} : path;
}

bool EditorUVE::SaveVisualScriptWorkspaceUVE() {
    if (m_state != EditorStateUVE::Running || m_visualScriptBranches.empty()) {
        return false;
    }
    Scripting::ScriptGraphWorkspaceSchemaUVE workspace{};
    workspace.branches.reserve(m_visualScriptBranches.size());
    for (const ScriptBranchUVE& branch : m_visualScriptBranches) {
        const Scripting::ScriptGraphCanvasLayoutSnapshotUVE layout = branch.canvas->GetLayoutSnapshotUVE();
        Scripting::ScriptGraphSchemaUVE schema{};
        schema.graph = branch.canvas->GetGraphUVE();
        schema.layout.reserve(layout.entries.size());
        for (const auto& entry : layout.entries) {
            schema.layout.push_back(Scripting::ScriptGraphLayoutEntryUVE{
                entry.nodeId, entry.position.x, entry.position.y});
        }
        workspace.branches.push_back(Scripting::ScriptGraphWorkspaceBranchUVE{
            branch.name, std::move(schema), {layout.view.pan.x, layout.view.pan.y, layout.view.zoom}});
    }
    std::vector<Scripting::ScriptPersistenceDiagnosticUVE> diagnostics;
    const std::string encoded = Scripting::EncodeScriptGraphWorkspaceUVE(workspace, diagnostics);
    if (encoded.empty() || !diagnostics.empty()) {
        return false;
    }
    const std::filesystem::path path = ScriptWorkspacePathUVE(m_activeScenePath);
    const std::string virtualPath = path.generic_string();
    Asset::IFileSystemUVE& fileSystem = m_services->GetFileSystemUVE();
    const std::filesystem::path resolvedPath = fileSystem.ResolveRealPathUVE(virtualPath);
    if (!resolvedPath.empty()) {
        const std::filesystem::path temporaryPath = resolvedPath.string() + ".tmp";
        std::error_code error;
        if (!resolvedPath.parent_path().empty()) {
            std::filesystem::create_directories(resolvedPath.parent_path(), error);
        }
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return false;
        }
        output.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
        output.flush();
        const bool outputGood = output.good();
        output.close();
        if (!outputGood) {
            std::filesystem::remove(temporaryPath, error);
            return false;
        }
        std::filesystem::rename(temporaryPath, resolvedPath, error);
        if (error) {
            std::filesystem::remove(resolvedPath, error);
            error.clear();
            std::filesystem::rename(temporaryPath, resolvedPath, error);
        }
        if (error) {
            std::filesystem::remove(temporaryPath, error);
            return false;
        }
        return true;
    }
    std::vector<std::byte> bytes(encoded.size());
    if (!encoded.empty()) {
        std::memcpy(bytes.data(), encoded.data(), encoded.size());
    }
    if (fileSystem.WriteFileUVE(virtualPath, bytes)) {
        return true;
    }
    // Some editor test/legacy configurations have no mounted project directory. Preserve the
    // established raw-path behavior as a compatibility fallback after the native VFS attempt.
    const std::filesystem::path temporaryPath = path.string() + ".tmp";
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }
    output.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    output.flush();
    const bool outputGood = output.good();
    output.close();
    if (!outputGood) {
        std::filesystem::remove(temporaryPath, error);
        return false;
    }
    std::filesystem::rename(temporaryPath, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporaryPath, path, error);
    }
    if (error) {
        std::filesystem::remove(temporaryPath, error);
        return false;
    }
    return true;
}

bool EditorUVE::LoadVisualScriptWorkspaceUVE() {
    if (m_state != EditorStateUVE::Running) {
        return false;
    }
    const std::filesystem::path path = ScriptWorkspacePathUVE(m_activeScenePath);
    const std::string virtualPath = path.generic_string();
    std::string text;
    if (const std::optional<std::vector<std::byte>> bytes = m_services->GetFileSystemUVE().ReadFileUVE(virtualPath);
        bytes.has_value()) {
        text.assign(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    } else {
        if (!std::filesystem::exists(path)) {
            return false;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            return false;
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        text = buffer.str();
    }
    const Scripting::ScriptGraphWorkspaceDecodeResultUVE decoded =
        Scripting::DecodeScriptGraphWorkspaceUVE(text);
    if (!decoded.IsSuccessUVE()) {
        return false;
    }
    std::vector<ScriptBranchUVE> loaded;
    loaded.reserve(decoded.workspace->branches.size());
    for (const auto& branch : decoded.workspace->branches) {
        auto canvas = std::make_unique<Scripting::ScriptGraphCanvasUVE>(m_visualScriptRegistry, m_historyCapacity);
        Scripting::ScriptGraphCanvasLayoutSnapshotUVE layout{};
        layout.view = {branch.view.panX, branch.view.panY, branch.view.zoom};
        layout.entries.reserve(branch.schema.layout.size());
        for (const auto& entry : branch.schema.layout) {
            layout.entries.push_back(Scripting::ScriptGraphCanvasLayoutEntryUVE{
                entry.nodeId, {entry.x, entry.y}});
        }
        if (!canvas->RestorePersistenceUVE(branch.schema, std::move(layout)).IsAppliedUVE()) {
            return false;
        }
        loaded.push_back(ScriptBranchUVE{branch.name, std::move(canvas)});
    }
    if (loaded.empty()) {
        return false;
    }
    m_visualScriptBranches = std::move(loaded);
    m_activeVisualScriptBranch = 0U;
    return true;
}

const Scripting::ScriptNodeRegistryUVE& EditorUVE::GetVisualScriptRegistryUVE() const noexcept {
    return m_visualScriptRegistry;
}

void EditorUVE::ShutdownUVE() {
    if (m_state == EditorStateUVE::Shutdown || m_state == EditorStateUVE::Uninitialized) {
        return;
    }

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
    m_state = EditorStateUVE::Shutdown;
}

bool EditorUVE::IsDocumentEntityUVE(const Scene::EntityUVE entity) const noexcept {
    return entity != Scene::kInvalidEntityUVE && m_services->GetEntityManagerUVE().IsAliveUVE(entity);
}

bool EditorUVE::HasSceneGraphNodeUVE(const Scene::EntityUVE entity) const noexcept {
    if (!IsDocumentEntityUVE(entity)) {
        return false;
    }

    const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    return entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity) &&
           entityManager.HasComponentUVE<Scene::HierarchyComponentUVE>(entity) &&
           entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity);
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
    // The names themselves live with each node kind's definition in Engine/Runtime/Nodes/3D —
    // this legacy-kind mapper only picks which definition to ask, never authors a name itself.
    switch (kind) {
        case EditorEntityKindUVE::Empty:
            return std::string{Scene::Node3DNodeDefinitionUVE::defaultName};
        case EditorEntityKindUVE::Camera:
            return std::string{Scene::Camera3DNodeDefinitionUVE::defaultName};
        case EditorEntityKindUVE::DirectionalLight:
            return std::string{Scene::Light3DNodeDefinitionUVE::defaultName};
        case EditorEntityKindUVE::CollisionBox:
            return std::string{Scene::Collider3DNodeDefinitionUVE::defaultName};
        case EditorEntityKindUVE::Cube:
            return std::string{Scene::BoxMesh3DNodeDefinitionUVE::defaultName};
        case EditorEntityKindUVE::UVSphere:
            return std::string{Scene::SphereMesh3DNodeDefinitionUVE::defaultName};
        case EditorEntityKindUVE::Plane:
            return std::string{Scene::PlaneMesh3DNodeDefinitionUVE::defaultName};
    }
    return {};
}

std::string EditorUVE::MakeUniqueDocumentEntityNameUVE(const std::string_view baseName) const {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    std::vector<std::string> names;
    entityManager.ForEachUVE<Scene::NameComponentUVE>(
        [this, &names](const Scene::EntityUVE entity, Scene::NameComponentUVE& component) {
            if (IsDocumentEntityUVE(entity)) {
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
    return IsFiniteUVE(settings.translateStep) && settings.translateStep > kVectorEpsilonUVE &&
           IsFiniteUVE(settings.rotateStepDegrees) && settings.rotateStepDegrees > kVectorEpsilonUVE &&
           IsFiniteUVE(settings.scaleStep) && settings.scaleStep > kVectorEpsilonUVE;
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
        if (!entityManager.IsAliveUVE(hierarchy.parent) ||
            !entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(hierarchy.parent)) {
            return false;
        }

        const Scene::WorldTransformComponentUVE& parentWorld =
            entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(hierarchy.parent);
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

    if (!entityManager.IsAliveUVE(hierarchy.parent) ||
        !entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(hierarchy.parent)) {
        return false;
    }

    const Scene::WorldTransformComponentUVE& parentWorld =
        entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(hierarchy.parent);
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
    const auto getEnum = [&config](const std::string_view key, const std::int64_t fallback, const std::int64_t maximum) {
        const std::int64_t value = config.GetIntUVE(key, fallback);
        return value >= 0 && value <= maximum ? value : fallback;
    };
    m_scenePanelVisible = config.GetBoolUVE("editor.panels.sceneVisible", true);
    m_viewportPanelVisible = config.GetBoolUVE("editor.panels.viewportVisible", true);
    m_inspectorPanelVisible = config.GetBoolUVE("editor.panels.inspectorVisible", true);
    m_bottomDockVisible = config.GetBoolUVE("editor.panels.bottomDockVisible", true);
    m_activeWorkspace = static_cast<EditorWorkspaceUVE>(getEnum("editor.workspace.active", 0, 4));
    m_activeRightPanelTab = static_cast<EditorRightPanelTabUVE>(getEnum("editor.rightPanel.activeTab", 0, 2));
    m_activeBottomDock = static_cast<EditorBottomDockUVE>(getEnum("editor.bottomDock.active", 3, 3));
    EditorTransformSnappingSettingsUVE snapping{};
    const auto getPositiveSnapValue = [&config](const std::string_view key, const float fallback) {
        const float candidate = static_cast<float>(config.GetDoubleUVE(key, fallback));
        return IsFiniteUVE(candidate) && candidate > 0.0F ? candidate : fallback;
    };
    snapping.enabled = config.GetBoolUVE("editor.viewport.snap.enabled", false);
    snapping.translateStep = getPositiveSnapValue("editor.viewport.snap.translateStep", snapping.translateStep);
    snapping.rotateStepDegrees =
        getPositiveSnapValue("editor.viewport.snap.rotateStepDegrees", snapping.rotateStepDegrees);
    snapping.scaleStep = getPositiveSnapValue("editor.viewport.snap.scaleStep", snapping.scaleStep);
    m_transformSnappingSettings = snapping;
    constexpr std::int64_t kMaxPersistedFavoritesUVE = 128;
    const std::int64_t favoritesCount =
        std::clamp(config.GetIntUVE("editor.favorites.count", 0), std::int64_t{0}, kMaxPersistedFavoritesUVE);
    m_favoriteProjectPaths.clear();
    m_favoriteProjectPaths.reserve(static_cast<std::size_t>(favoritesCount));
    for (std::int64_t index = 0; index < favoritesCount; ++index) {
        const std::string stored = config.GetStringUVE("editor.favorites." + std::to_string(index), "");
        if (!stored.empty()) {
            m_favoriteProjectPaths.emplace_back(stored);
        }
    }
}

bool EditorUVE::SaveSessionSettingsUVE() {
    if (m_state != EditorStateUVE::Running ||
        !AreTransformSnappingSettingsValidUVE(m_transformSnappingSettings)) {
        return false;
    }
    Config::IConfigManagerUVE& config = m_services->GetConfigManagerUVE();
    config.SetIntUVE("editor.sessionSettingsVersion", 1);
    config.SetIntUVE("editor.workspace.active", static_cast<std::int64_t>(m_activeWorkspace));
    config.SetIntUVE("editor.rightPanel.activeTab", static_cast<std::int64_t>(m_activeRightPanelTab));
    config.SetIntUVE("editor.bottomDock.active", static_cast<std::int64_t>(m_activeBottomDock));
    config.SetBoolUVE("editor.panels.sceneVisible", m_scenePanelVisible);
    config.SetBoolUVE("editor.panels.viewportVisible", m_viewportPanelVisible);
    config.SetBoolUVE("editor.panels.inspectorVisible", m_inspectorPanelVisible);
    config.SetBoolUVE("editor.panels.bottomDockVisible", m_bottomDockVisible);
    config.SetBoolUVE("editor.viewport.snap.enabled", m_transformSnappingSettings.enabled);
    config.SetDoubleUVE("editor.viewport.snap.translateStep", m_transformSnappingSettings.translateStep);
    config.SetDoubleUVE("editor.viewport.snap.rotateStepDegrees", m_transformSnappingSettings.rotateStepDegrees);
    config.SetDoubleUVE("editor.viewport.snap.scaleStep", m_transformSnappingSettings.scaleStep);
    constexpr std::size_t kMaxPersistedFavoritesUVE = 128U;
    const std::size_t favoritesToPersist = std::min(m_favoriteProjectPaths.size(), kMaxPersistedFavoritesUVE);
    config.SetIntUVE("editor.favorites.count", static_cast<std::int64_t>(favoritesToPersist));
    for (std::size_t index = 0U; index < favoritesToPersist; ++index) {
        config.SetStringUVE("editor.favorites." + std::to_string(index),
                             m_favoriteProjectPaths[index].generic_string());
    }
    return config.SaveUVE();
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

void EditorUVE::CancelHierarchyRenameUVE() noexcept {
    m_hierarchyRenameEntity = Scene::kInvalidEntityUVE;
    m_hierarchyRenameBuffer.clear();
    m_hierarchyRenameFocusRequested = false;
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
    const auto visit = [this](const auto& self, const Scene::EntityUVE entity) -> bool {
        if (!IsDocumentEntityUVE(entity)) {
            return false;
        }
        const std::string displayLabel = GetEntityDisplayLabelUVE(entity);
        const std::string typeTag = GetOutlinerTypeTagUVE(entity);
        const bool hasTypeQuery = m_hierarchyFilter.rfind("type:", 0U) == 0U;
        const std::string_view typeQuery = hasTypeQuery ? std::string_view{m_hierarchyFilter}.substr(5U) : std::string_view{};
        const bool typeMatches = !hasTypeQuery || ContainsCaseInsensitiveUVE(typeTag, typeQuery);
        Scene::EntityUVE parent = Scene::kInvalidEntityUVE;
        const bool isRoot = !TryGetDocumentParentUVE(entity, parent) || parent == Scene::kInvalidEntityUVE;
        const bool nameMatches = hasTypeQuery || ContainsCaseInsensitiveUVE(displayLabel, m_hierarchyFilter) ||
                                 (m_hierarchyFilter == "root" && isRoot);
        bool visible = typeMatches && nameMatches;
        Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
        for (const Scene::EntityUVE child : m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, entity)) {
            visible = self(self, child) || visible;
        }
        if (visible) {
            m_cachedHierarchyVisibleEntities.push_back(entity);
        }
        return visible;
    };
    for (const Scene::EntityUVE root : GetDocumentRootsUVE()) {
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
        static_cast<void>(ReparentDocumentEntityUVE(source, targetParent));
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
    if (extension == ".uvescene") {
        return ContentBrowserItemTypeUVE::Scene;
    }
    if (extension == ".uveprefab") {
        return ContentBrowserItemTypeUVE::Prefab;
    }
    if (extension == ".uvebundle") {
        return ContentBrowserItemTypeUVE::Bundle;
    }
    if (extension == ".uvemodel") {
        return ContentBrowserItemTypeUVE::Mesh;
    }
    if (extension == ".uvetex") {
        return ContentBrowserItemTypeUVE::Texture;
    }
    // Raw, not-yet-imported source images. Godot-style engines preview these directly rather than
    // requiring an import step first; this repo already has standalone decoders for all four
    // (uve/asset/{png,jpeg,bmp,tga}_metadata_uve.h) that GetTextureThumbnailUVE() falls back to
    // when the file isn't a `.uvetex` envelope. Reusing Texture rather than adding a new enum value
    // since both content-browser call sites already dispatch thumbnails on this exact type.
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" ||
        extension == ".tga") {
        return ContentBrowserItemTypeUVE::Texture;
    }
    if (extension == ".uveshader") {
        return ContentBrowserItemTypeUVE::Shader;
    }
    if (extension == ".uvemat") {
        return ContentBrowserItemTypeUVE::Material;
    }
    if (extension == ".uvesave") {
        return ContentBrowserItemTypeUVE::Save;
    }
    return ContentBrowserItemTypeUVE::File;
}

const char* EditorUVE::GetContentBrowserItemTypeLabelUVE(const ContentBrowserItemTypeUVE type) noexcept {
    switch (type) {
        case ContentBrowserItemTypeUVE::Folder:
            return "Folder";
        case ContentBrowserItemTypeUVE::Scene:
            return "Scene";
        case ContentBrowserItemTypeUVE::Prefab:
            return "Prefab";
        case ContentBrowserItemTypeUVE::Bundle:
            return "Bundle";
        case ContentBrowserItemTypeUVE::Mesh:
            return "Mesh";
        case ContentBrowserItemTypeUVE::Texture:
            return "Texture";
        case ContentBrowserItemTypeUVE::Shader:
            return "Shader";
        case ContentBrowserItemTypeUVE::Material:
            return "Material";
        case ContentBrowserItemTypeUVE::Save:
            return "Save";
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
            return type == ContentBrowserItemTypeUVE::Prefab;
        case ContentBrowserTypeFocusUVE::Bundle:
            return type == ContentBrowserItemTypeUVE::Bundle;
        case ContentBrowserTypeFocusUVE::Mesh:
            return type == ContentBrowserItemTypeUVE::Mesh;
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
        texture.format == Asset::TextureFormatUVE::RGBA8Unorm) {
        textureId = EditorUiAssetsUVE::UploadDynamicTextureUVE(reinterpret_cast<const std::uint8_t*>(texture.pixels.data()),
                                                                static_cast<int>(texture.width),
                                                                static_cast<int>(texture.height));
    } else {
        // Not a `.uvetex` envelope - it may still be a raw, un-imported source image.
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
    const std::filesystem::path absolutePath = snapshot.contentRoot / relativePath;
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
    } else {
        m_projectFileRefreshAttemptedForRescan = changesBeforeRefresh.rescanRequired;
    }
}

void EditorUVE::CompileVisualScriptUVE() {
    const Scripting::ScriptGraphCanvasSnapshotUVE snapshot = ActiveVisualScriptCanvasUVE().GetSnapshotUVE();
    m_scriptCompileAttempted = true;
    m_scriptCompileSucceeded = false;
    m_scriptLastCompiledGraphRevision = snapshot.graphRevision;
    m_scriptCompileInstructionCount = 0U;
    m_scriptCompileMessage.clear();

    const Scripting::ScriptIrCompileResultUVE compiled =
        Scripting::CompileScriptGraphToIrUVE(ActiveVisualScriptCanvasUVE().GetGraphUVE(), m_visualScriptRegistry);
    if (!compiled.IsSuccessUVE()) {
        if (compiled.diagnostics.empty()) {
            m_scriptCompileMessage = "Graph compilation was rejected without a diagnostic.";
        } else {
            const auto& diagnostic = compiled.diagnostics.front();
            m_scriptCompileMessage = "Node " + std::to_string(diagnostic.nodeId) + ": " + diagnostic.message;
            if (!diagnostic.pinName.empty()) {
                m_scriptCompileMessage += " (" + diagnostic.pinName + ")";
            }
        }
        return;
    }

    std::vector<Scripting::ScriptBytecodeDiagnosticUVE> loweringDiagnostics;
    const std::optional<Scripting::ScriptBytecodeProgramUVE> bytecode =
        Scripting::LowerIrToBytecodeUVE(*compiled.program, loweringDiagnostics);
    if (!bytecode.has_value() || !loweringDiagnostics.empty()) {
        if (loweringDiagnostics.empty()) {
            m_scriptCompileMessage = "Bytecode lowering was rejected without a diagnostic.";
        } else {
            m_scriptCompileMessage = "Bytecode lowering: " + loweringDiagnostics.front().message;
        }
        return;
    }

    m_scriptCompileSucceeded = true;
    m_scriptCompileInstructionCount = bytecode->instructions.size();
    m_scriptCompileMessage = "Compiled " + std::to_string(m_scriptCompileInstructionCount) + " instructions.";
}

void EditorUVE::DrawScriptingWorkspaceUVE() {
    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    const ImVec2 position{mainViewport->WorkPos.x,
                          mainViewport->WorkPos.y + kEditorTopChromeHeightUVE};
    const ImVec2 size{mainViewport->WorkSize.x,
                      std::max(120.0F, mainViewport->WorkSize.y - kEditorTopChromeHeightUVE)};
    ImGui::SetNextWindowPos(position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("Scripting Workspace##uve", nullptr, windowFlags)) {
        ImGui::End();
        return;
    }

    const Scripting::ScriptGraphCanvasSnapshotUVE snapshot = ActiveVisualScriptCanvasUVE().GetSnapshotUVE();
    const auto selectedNode = [&snapshot]() -> const Scripting::ScriptGraphCanvasNodeSnapshotUVE* {
        if (snapshot.selectedNodeIds.size() != 1U) {
            return nullptr;
        }
        const auto iterator = std::find_if(
            snapshot.nodes.cbegin(), snapshot.nodes.cend(),
            [&snapshot](const Scripting::ScriptGraphCanvasNodeSnapshotUVE& node) {
                return node.id == snapshot.selectedNodeIds.front();
            });
        return iterator == snapshot.nodes.cend() ? nullptr : &*iterator;
    };
    const auto findNode = [&snapshot](const std::uint32_t nodeId)
        -> const Scripting::ScriptGraphCanvasNodeSnapshotUVE* {
        const auto iterator = std::find_if(
            snapshot.nodes.cbegin(), snapshot.nodes.cend(),
            [nodeId](const Scripting::ScriptGraphCanvasNodeSnapshotUVE& node) { return node.id == nodeId; });
        return iterator == snapshot.nodes.cend() ? nullptr : &*iterator;
    };
    const auto findPin = [](const Scripting::ScriptGraphCanvasNodeSnapshotUVE& node,
                            const std::string& name) -> const Scripting::ScriptGraphCanvasPinSnapshotUVE* {
        const auto iterator = std::find_if(
            node.pins.cbegin(), node.pins.cend(),
            [&name](const Scripting::ScriptGraphCanvasPinSnapshotUVE& pin) { return pin.name == name; });
        return iterator == node.pins.cend() ? nullptr : &*iterator;
    };

    if (ImGui::BeginChild("##scripting-toolbar", ImVec2{0.0F, 28.0F}, false)) {
        ImGui::TextColored(ImVec4{0.70F, 0.72F, 0.76F, 1.0F}, "GRAPH");
        ImGui::SameLine();
        ImGui::TextDisabled("native canvas | branch %s | revision %llu",
                            GetActiveVisualScriptBranchNameUVE().c_str(),
                            static_cast<unsigned long long>(snapshot.revision));
        ImGui::SameLine();
        // Without an explicit width, BeginCombo defaults to consuming most of the row's remaining
        // space, starving every sibling button after it (Rename/Save/Load/Undo/Compiler/Redo and the
        // hint text) - they were being pushed off the right edge of the toolbar with no scrollbar to
        // reach them, silently hiding real, working buttons rather than an actual missing feature.
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::BeginCombo("##script-branch-combo", GetActiveVisualScriptBranchNameUVE().c_str(),
                              ImGuiComboFlags_HeightSmall)) {
            for (const std::string& branchName : GetVisualScriptBranchNamesUVE()) {
                const bool selected = branchName == GetActiveVisualScriptBranchNameUVE();
                if (ImGui::Selectable(branchName.c_str(), selected)) {
                    static_cast<void>(SelectVisualScriptBranchUVE(branchName));
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+ Branch")) {
            m_scriptBranchDialogBuffer = "Type 2 Scene";
            m_scriptBranchDialogRenaming = false;
            ImGui::OpenPopup("script-branch-name-popup");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Rename")) {
            m_scriptBranchDialogBuffer = GetActiveVisualScriptBranchNameUVE();
            m_scriptBranchDialogRenaming = true;
            ImGui::OpenPopup("script-branch-name-popup");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Save .scripting")) {
            static_cast<void>(SaveVisualScriptWorkspaceUVE());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Load .scripting")) {
            static_cast<void>(LoadVisualScriptWorkspaceUVE());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Undo")) {
            static_cast<void>(ActiveVisualScriptCanvasUVE().UndoUVE());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Compiler")) {
            CompileVisualScriptUVE();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Redo")) {
            static_cast<void>(ActiveVisualScriptCanvasUVE().RedoUVE());
        }
        ImGui::SameLine();
        ImGui::TextDisabled("LMB select/drag/link | RMB/MMB pan | long-press search | wheel zoom");
        if (ImGui::BeginPopup("script-branch-name-popup")) {
            ImGui::TextUnformatted(m_scriptBranchDialogRenaming ? "Rename script branch" : "Create script branch");
            ImGui::Separator();
            std::array<char, 97> nameBuffer{};
            std::strncpy(nameBuffer.data(), m_scriptBranchDialogBuffer.c_str(), nameBuffer.size() - 1U);
            const bool submitted = ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size(),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
            m_scriptBranchDialogBuffer = nameBuffer.data();
            if (submitted || ImGui::Button(m_scriptBranchDialogRenaming ? "Rename" : "Create")) {
                const bool applied = m_scriptBranchDialogRenaming
                    ? RenameActiveVisualScriptBranchUVE(m_scriptBranchDialogBuffer)
                    : CreateVisualScriptBranchUVE(m_scriptBranchDialogBuffer);
                if (applied) {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();

    const ImVec2 workspaceSize = ImGui::GetContentRegionAvail();
    if (ImGui::BeginChild("##scripting-layout", workspaceSize, false)) {
        // The persistent category-grouped node palette that used to occupy this column was removed
        // as a deliberate redundancy cleanup: right-click/long-press/wire-drop already open the same
        // searchable node list at the point of use (see script-node-search-popup below), so a second,
        // always-visible copy of the same list added nothing but a duplicate surface to keep in sync.
        // What replaces this freed column is still undecided - left for a follow-up, not guessed at
        // here - so the canvas below simply grows into the reclaimed width for now.
        const float detailsWidth = 276.0F;
        const float canvasWidth = std::max(180.0F, ImGui::GetContentRegionAvail().x - detailsWidth - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::BeginChild("##script-canvas-frame", ImVec2{canvasWidth, 0.0F}, true)) {
            const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
            const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
            const Scripting::ScriptGraphCanvasViewUVE view = snapshot.view;
            // AllowOverlap: this button spans the whole canvas and is submitted before the zoom
            // pill drawn later in this same scope - without it, this button greedily claims
            // ActiveId on every click anywhere in the canvas (including over the pill), which
            // silently blocks the pill's own InvisibleButtons from ever registering a press even
            // though plain hover still highlights them correctly.
            ImGui::InvisibleButton("##script-canvas-input", canvasSize,
                                   ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                       ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_AllowOverlap);
            const bool canvasButtonHovered = ImGui::IsItemHovered();
            const ImVec2 mouse = ImGui::GetMousePos();
            const ImVec2 mouseLocal{mouse.x - canvasOrigin.x, mouse.y - canvasOrigin.y};
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            // Zoom pill geometry (drawn near the end of this scope), computed early so the
            // canvas's own click/pan/deselect handling below can treat it as outside the canvas -
            // it visually sits inside the canvas's hit-test rect, so without this exclusion a
            // click on it would also register as an "empty canvas" click and deselect the current
            // node/start a pan, on top of whatever the pill button itself does.
            constexpr float kZoomPillPaddingUVE = 3.0F;
            constexpr float kZoomButtonDiameterUVE = 20.0F;
            const std::string zoomLabel = std::to_string(static_cast<int>(view.zoom * 100.0F + 0.5F)) + "%";
            const float zoomLabelWidth = std::max(34.0F, ImGui::CalcTextSize(zoomLabel.c_str()).x);
            const float zoomFitWidth = ImGui::CalcTextSize("Fit").x + 16.0F;
            const float zoomPillHeight = kZoomButtonDiameterUVE + kZoomPillPaddingUVE * 2.0F;
            const float zoomPillWidth = kZoomPillPaddingUVE + kZoomButtonDiameterUVE + zoomLabelWidth +
                                        kZoomButtonDiameterUVE + zoomFitWidth + kZoomPillPaddingUVE;
            const ImVec2 zoomPillMin{canvasOrigin.x + 10.0F, canvasOrigin.y + canvasSize.y - zoomPillHeight - 10.0F};
            const ImVec2 zoomPillMax{zoomPillMin.x + zoomPillWidth, zoomPillMin.y + zoomPillHeight};
            const bool mouseOverZoomPill = mouse.x >= zoomPillMin.x && mouse.x <= zoomPillMax.x &&
                                           mouse.y >= zoomPillMin.y && mouse.y <= zoomPillMax.y;
            const bool canvasHovered = canvasButtonHovered && !mouseOverZoomPill;
            drawList->AddRectFilled(canvasOrigin,
                                    ImVec2{canvasOrigin.x + canvasSize.x, canvasOrigin.y + canvasSize.y},
                                    IM_COL32(20, 22, 25, 255));
            drawList->AddText(ImVec2{canvasOrigin.x + 16.0F, canvasOrigin.y + 12.0F},
                              IM_COL32(166, 172, 180, 235), "GRAPH CANVAS");
            // A dot at each grid intersection, matching a design mockup's own
            // `radial-gradient(rgba(255,255,255,.055) 1px, transparent 1px)` canvas background,
            // rather than the previous crossed-line grid - approximated with small low-alpha
            // filled circles since ImDrawList has no radial-gradient/repeating-pattern primitive.
            constexpr float gridSpacing = 24.0F;
            const float gridOffsetX = std::fmod(-view.pan.x * view.zoom, gridSpacing);
            const float gridOffsetY = std::fmod(-view.pan.y * view.zoom, gridSpacing);
            for (float y = canvasOrigin.y + gridOffsetY; y < canvasOrigin.y + canvasSize.y; y += gridSpacing) {
                for (float x = canvasOrigin.x + gridOffsetX; x < canvasOrigin.x + canvasSize.x; x += gridSpacing) {
                    drawList->AddCircleFilled(ImVec2{x, y}, 1.3F, IM_COL32(255, 255, 255, 22));
                }
            }

            const auto nodePosition = [this](const Scripting::ScriptGraphCanvasNodeSnapshotUVE& node) {
                return m_scriptCanvasDragging && node.id == m_scriptCanvasDragNodeId
                    ? m_scriptCanvasDragPreviewPosition : node.position;
            };
            constexpr float nodeWidth = 228.0F;
            constexpr float headerHeight = 26.0F;
            constexpr float pinRowHeight = 19.0F;
            const auto nodeHeight = [](const Scripting::ScriptGraphCanvasNodeSnapshotUVE& node) {
                return 38.0F + pinRowHeight * static_cast<float>(std::max<std::size_t>(1U, node.pins.size()));
            };
            const auto pinScreenPosition = [&](const Scripting::ScriptGraphCanvasNodeSnapshotUVE& node,
                                               const Scripting::ScriptGraphCanvasPinSnapshotUVE& pin) {
                const auto iterator = std::find_if(node.pins.cbegin(), node.pins.cend(),
                                                   [&pin](const auto& candidate) { return candidate.name == pin.name; });
                const std::size_t pinIndex = iterator == node.pins.cend()
                    ? 0U : static_cast<std::size_t>(std::distance(node.pins.cbegin(), iterator));
                const ImVec2 nodeMin = ScriptCanvasToScreenUVE(nodePosition(node), canvasOrigin, view);
                const float y = nodeMin.y + headerHeight + 13.0F + pinRowHeight * static_cast<float>(pinIndex);
                return pin.direction == Scripting::ScriptPinDirectionUVE::Input
                    ? ImVec2{nodeMin.x + 8.0F, y} : ImVec2{nodeMin.x + nodeWidth - 8.0F, y};
            };

            for (const Scripting::ScriptGraphCanvasLinkSnapshotUVE& link : snapshot.links) {
                const auto* const outputNode = findNode(link.link.output.nodeId);
                const auto* const inputNode = findNode(link.link.input.nodeId);
                if (outputNode == nullptr || inputNode == nullptr) {
                    continue;
                }
                const auto* const outputPin = findPin(*outputNode, link.link.output.pinName);
                const auto* const inputPin = findPin(*inputNode, link.link.input.pinName);
                if (outputPin == nullptr || inputPin == nullptr) {
                    continue;
                }
                const ImVec2 start = pinScreenPosition(*outputNode, *outputPin);
                const ImVec2 end = pinScreenPosition(*inputNode, *inputPin);
                const float tangent = std::max(36.0F, std::abs(end.x - start.x) * 0.45F);
                drawList->AddBezierCubic(start, ImVec2{start.x + tangent, start.y},
                                         ImVec2{end.x - tangent, end.y}, end,
                                         IM_COL32(148, 174, 196, 235), 2.0F);
            }

            for (const Scripting::ScriptGraphCanvasNodeSnapshotUVE& node : snapshot.nodes) {
                const ImVec2 nodeMin = ScriptCanvasToScreenUVE(nodePosition(node), canvasOrigin, view);
                const float nodeHeightPixels = nodeHeight(node);
                const ImVec2 nodeMax{nodeMin.x + nodeWidth, nodeMin.y + nodeHeightPixels};
                const bool selected = std::find(snapshot.selectedNodeIds.cbegin(), snapshot.selectedNodeIds.cend(), node.id) !=
                                      snapshot.selectedNodeIds.cend();
                // Header fill is now per-category (matching a design mockup's own header-tint
                // convention) instead of a uniform gray - a lighter tint of the category color
                // when selected, the plain category color otherwise. Body/border rounding bumped
                // 4px->6px to match the same mockup's node corner radius.
                const ImU32 categoryColor = ScriptNodeCategoryColorUVE(node.category);
                const ImU32 bodyColor = selected ? IM_COL32(58, 65, 72, 255) : IM_COL32(46, 50, 56, 255);
                const ImU32 headerColor = selected ? ImGui::ColorConvertFloat4ToU32(ImVec4{
                    std::min(1.0F, static_cast<float>(categoryColor & 0xFFU) / 255.0F + 0.18F),
                    std::min(1.0F, static_cast<float>((categoryColor >> 8U) & 0xFFU) / 255.0F + 0.18F),
                    std::min(1.0F, static_cast<float>((categoryColor >> 16U) & 0xFFU) / 255.0F + 0.18F), 1.0F})
                    : categoryColor;
                drawList->AddRectFilled(nodeMin, nodeMax, bodyColor, 6.0F);
                drawList->AddRectFilled(nodeMin, ImVec2{nodeMax.x, nodeMin.y + headerHeight}, headerColor, 6.0F,
                                        ImDrawFlags_RoundCornersTop);
                drawList->AddRect(nodeMin, nodeMax, selected ? IM_COL32(205, 180, 108, 255) : IM_COL32(105, 112, 120, 255),
                                  6.0F, 0, selected ? 2.0F : 1.0F);
                const float categoryIconRadius = 6.0F;
                const ImVec2 categoryIconCenter{nodeMin.x + 14.0F, nodeMin.y + headerHeight * 0.5F};
                DrawScriptNodeCategoryIconUVE(drawList, categoryIconCenter, categoryIconRadius, node.category,
                                              IM_COL32(255, 255, 255, 235));
                const std::string title = node.displayName.empty() ? node.typeId : node.displayName;
                drawList->AddText(ImVec2{nodeMin.x + 24.0F, nodeMin.y + 6.0F}, IM_COL32(226, 241, 252, 255), title.c_str());
                for (std::size_t pinIndex = 0U; pinIndex < node.pins.size(); ++pinIndex) {
                    const auto& pin = node.pins[pinIndex];
                    const ImVec2 pinPosition = pinScreenPosition(node, pin);
                    const float pinRadius = 5.0F * std::clamp(view.zoom, 0.75F, 1.25F);
                    const bool isExecutionPin = pin.role == Scripting::ScriptPinRoleUVE::Execution ||
                                                pin.type == Scripting::ScriptValueTypeUVE::Execution;
                    if (isExecutionPin) {
                        // A small right-pointing diamond/arrow silhouette distinguishes flow pins
                        // from data pins, matching a design mockup's own exec-pin shape convention
                        // (data pins stay plain filled circles, unchanged below).
                        const ImVec2 points[5] = {
                            ImVec2{pinPosition.x - pinRadius, pinPosition.y - pinRadius},
                            ImVec2{pinPosition.x + pinRadius * 0.1F, pinPosition.y - pinRadius},
                            ImVec2{pinPosition.x + pinRadius * 1.1F, pinPosition.y},
                            ImVec2{pinPosition.x + pinRadius * 0.1F, pinPosition.y + pinRadius},
                            ImVec2{pinPosition.x - pinRadius, pinPosition.y + pinRadius},
                        };
                        drawList->AddConvexPolyFilled(points, 5, ScriptPinColorUVE(pin));
                    } else {
                        drawList->AddCircleFilled(pinPosition, pinRadius, ScriptPinColorUVE(pin));
                    }
                    const float textX = pin.direction == Scripting::ScriptPinDirectionUVE::Input
                        ? nodeMin.x + 17.0F : nodeMin.x + 14.0F;
                    const ImVec2 textPosition{pin.direction == Scripting::ScriptPinDirectionUVE::Input
                                                  ? textX : nodeMin.x + nodeWidth - 14.0F - ImGui::CalcTextSize(pin.name.c_str()).x,
                                              pinPosition.y - 7.0F};
                    drawList->AddText(textPosition, IM_COL32(214, 220, 227, 255), pin.name.c_str());
                }
            }

            const auto findNodeAt = [&](const ImVec2 point) -> const Scripting::ScriptGraphCanvasNodeSnapshotUVE* {
                for (auto iterator = snapshot.nodes.crbegin(); iterator != snapshot.nodes.crend(); ++iterator) {
                    const ImVec2 nodeMin = ScriptCanvasToScreenUVE(nodePosition(*iterator), canvasOrigin, view);
                    const ImVec2 nodeMax{nodeMin.x + nodeWidth, nodeMin.y + nodeHeight(*iterator)};
                    if (point.x >= nodeMin.x && point.x <= nodeMax.x && point.y >= nodeMin.y && point.y <= nodeMax.y) {
                        return &*iterator;
                    }
                }
                return nullptr;
            };
            const auto findPinAt = [&](const ImVec2 point, const Scripting::ScriptGraphCanvasNodeSnapshotUVE& node)
                -> const Scripting::ScriptGraphCanvasPinSnapshotUVE* {
                for (const auto& pin : node.pins) {
                    const ImVec2 pinPosition = pinScreenPosition(node, pin);
                    const float dx = point.x - pinPosition.x;
                    const float dy = point.y - pinPosition.y;
                    if ((dx * dx) + (dy * dy) <= 64.0F) {
                        return &pin;
                    }
                }
                return nullptr;
            };

            if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && findNodeAt(mouse) == nullptr) {
                m_scriptCanvasLongPressPending = true;
                m_scriptCanvasLongPressSeconds = 0.0F;
                m_scriptCanvasLongPressStartPointer =
                    Scripting::ScriptGraphCanvasPointUVE{mouseLocal.x, mouseLocal.y};
            }
            bool openedLongPressPopup = false;
            if (m_scriptCanvasLongPressPending) {
                const float dx = mouseLocal.x - m_scriptCanvasLongPressStartPointer.x;
                const float dy = mouseLocal.y - m_scriptCanvasLongPressStartPointer.y;
                const bool movedTooFar = (dx * dx) + (dy * dy) >
                                         kScriptCanvasLongPressMaxMovementPixelsUVE *
                                             kScriptCanvasLongPressMaxMovementPixelsUVE;
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || movedTooFar ||
                    ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                    m_scriptCanvasPanning || m_scriptCanvasLinkSourceNodeId != 0U) {
                    m_scriptCanvasLongPressPending = false;
                    m_scriptCanvasLongPressSeconds = 0.0F;
                } else {
                    m_scriptCanvasLongPressSeconds += ImGui::GetIO().DeltaTime;
                    if (m_scriptCanvasLongPressSeconds >= kScriptCanvasLongPressThresholdSecondsUVE) {
                        const ImVec2 pressScreen{canvasOrigin.x + m_scriptCanvasLongPressStartPointer.x,
                                                canvasOrigin.y + m_scriptCanvasLongPressStartPointer.y};
                        m_scriptCanvasContextMenuPosition = ScreenToScriptCanvasUVE(pressScreen, canvasOrigin, view);
                        m_scriptCanvasContextFilter.clear();
                        ImGui::OpenPopup("script-node-search-popup");
                        m_scriptCanvasLongPressPending = false;
                        m_scriptCanvasLongPressSeconds = 0.0F;
                        openedLongPressPopup = true;
                    }
                }
            }

            if (m_scriptCanvasDragging) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    const Scripting::ScriptGraphCanvasPointUVE currentGraphPosition =
                        ScreenToScriptCanvasUVE(mouse, canvasOrigin, view);
                    m_scriptCanvasDragPreviewPosition = Scripting::ScriptGraphCanvasPointUVE{
                        m_scriptCanvasDragStartPosition.x + currentGraphPosition.x - m_scriptCanvasDragStartPointer.x,
                        m_scriptCanvasDragStartPosition.y + currentGraphPosition.y - m_scriptCanvasDragStartPointer.y};
                } else {
                    static_cast<void>(ActiveVisualScriptCanvasUVE().MoveNodeUVE(
                        m_scriptCanvasDragNodeId, m_scriptCanvasDragPreviewPosition, m_scriptCanvasDragRevision));
                    m_scriptCanvasDragging = false;
                    m_scriptCanvasDragNodeId = 0U;
                }
            } else if (m_scriptCanvasPanning) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
                    ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                    Scripting::ScriptGraphCanvasViewUVE nextView = m_scriptCanvasPanViewStart;
                    nextView.pan.x = m_scriptCanvasPanViewStart.pan.x -
                                     (mouseLocal.x - m_scriptCanvasPanStart.x) / std::max(view.zoom, 0.1F);
                    nextView.pan.y = m_scriptCanvasPanViewStart.pan.y -
                                     (mouseLocal.y - m_scriptCanvasPanStart.y) / std::max(view.zoom, 0.1F);
                    static_cast<void>(ActiveVisualScriptCanvasUVE().SetViewUVE(nextView));
                } else {
                    m_scriptCanvasPanning = false;
                }
            } else if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                const auto* const node = findNodeAt(mouse);
                if (node != nullptr) {
                    const auto* const pin = findPinAt(mouse, *node);
                    static_cast<void>(ActiveVisualScriptCanvasUVE().SetSelectionUVE({node->id}));
                    // Wiring is a real press-hold-drag-release gesture (matching Unreal), not two
                    // separate clicks: pressing an Output pin here just arms the source: the actual
                    // connect-or-search decision happens on release, in the standalone
                    // IsMouseReleased block below - see its own comment for why a second click can
                    // never legally observe an already-armed source under this model.
                    if (pin != nullptr && pin->direction == Scripting::ScriptPinDirectionUVE::Output) {
                        m_scriptCanvasLinkSourceNodeId = node->id;
                        m_scriptCanvasLinkSourcePin = pin->name;
                    } else if (pin == nullptr) {
                        const Scripting::ScriptGraphCanvasPointUVE graphPosition =
                            ScreenToScriptCanvasUVE(mouse, canvasOrigin, view);
                        m_scriptCanvasDragging = true;
                        m_scriptCanvasDragNodeId = node->id;
                        m_scriptCanvasDragStartPosition = node->position;
                        m_scriptCanvasDragStartPointer = graphPosition;
                        m_scriptCanvasDragPreviewPosition = node->position;
                        m_scriptCanvasDragRevision = snapshot.revision;
                    }
                } else {
                    static_cast<void>(ActiveVisualScriptCanvasUVE().SetSelectionUVE({}));
                }
            } else if (!openedLongPressPopup && canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                if (findNodeAt(mouse) == nullptr) {
                    m_scriptCanvasContextMenuPosition = ScreenToScriptCanvasUVE(mouse, canvasOrigin, view);
                    m_scriptCanvasContextFilter.clear();
                    ImGui::OpenPopup("script-node-search-popup");
                } else {
                    m_scriptCanvasPanning = true;
                    m_scriptCanvasPanStart = Scripting::ScriptGraphCanvasPointUVE{mouseLocal.x, mouseLocal.y};
                    m_scriptCanvasPanViewStart = view;
                }
            } else if (canvasHovered &&
                       (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) {
                m_scriptCanvasPanning = true;
                m_scriptCanvasPanStart = Scripting::ScriptGraphCanvasPointUVE{mouseLocal.x, mouseLocal.y};
                m_scriptCanvasPanViewStart = view;
            }
            if (canvasHovered && ImGui::GetIO().MouseWheel != 0.0F) {
                const Scripting::ScriptGraphCanvasPointUVE graphUnderPointer =
                    ScreenToScriptCanvasUVE(mouse, canvasOrigin, view);
                Scripting::ScriptGraphCanvasViewUVE nextView = view;
                nextView.zoom = std::clamp(view.zoom * std::pow(1.12F, ImGui::GetIO().MouseWheel),
                                           Scripting::kMinimumScriptGraphCanvasZoomUVE,
                                           Scripting::kMaximumScriptGraphCanvasZoomUVE);
                nextView.pan.x = graphUnderPointer.x - mouseLocal.x / nextView.zoom;
                nextView.pan.y = graphUnderPointer.y - mouseLocal.y / nextView.zoom;
                static_cast<void>(ActiveVisualScriptCanvasUVE().SetViewUVE(nextView));
            }
            // Wire drag release: completes the link if dropped on a compatible pin on a different
            // node, otherwise opens the same node-search popup used for right-click/long-press at
            // the drop point - picking a node there attempts to auto-wire it back to the source pin
            // (see the popup body below), matching Unreal's "drop a wire into empty space to
            // search+connect" convention. Only IsMouseReleased (not a second click) can ever observe
            // an armed source here: once a press arms it, the button is down, and ImGui can't emit
            // another IsMouseClicked for the same button without an intervening release firing this
            // block first.
            if (m_scriptCanvasLinkSourceNodeId != 0U && !m_scriptCanvasLinkAwaitingPick &&
                ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                const auto* const sourceNode = findNode(m_scriptCanvasLinkSourceNodeId);
                bool connected = false;
                if (sourceNode != nullptr) {
                    const auto* const targetNode = findNodeAt(mouse);
                    if (targetNode != nullptr && targetNode->id != sourceNode->id) {
                        const auto* const targetPin = findPinAt(mouse, *targetNode);
                        if (targetPin != nullptr) {
                            const auto result = ActiveVisualScriptCanvasUVE().AddLinkUVE(
                                Scripting::ScriptLinkUVE{{m_scriptCanvasLinkSourceNodeId, m_scriptCanvasLinkSourcePin},
                                                         {targetNode->id, targetPin->name}});
                            connected = result.IsAppliedUVE();
                        }
                    }
                }
                if (connected) {
                    m_scriptCanvasLinkSourceNodeId = 0U;
                    m_scriptCanvasLinkSourcePin.clear();
                } else {
                    m_scriptCanvasContextMenuPosition = ScreenToScriptCanvasUVE(mouse, canvasOrigin, view);
                    m_scriptCanvasContextFilter.clear();
                    m_scriptCanvasLinkAwaitingPick = true;
                    ImGui::OpenPopup("script-node-search-popup");
                }
            }
            if (m_scriptCanvasLinkSourceNodeId != 0U && !m_scriptCanvasLinkSourcePin.empty()) {
                const auto* const sourceNode = findNode(m_scriptCanvasLinkSourceNodeId);
                if (sourceNode != nullptr) {
                    const auto* const sourcePin = findPin(*sourceNode, m_scriptCanvasLinkSourcePin);
                    if (sourcePin != nullptr) {
                        const ImVec2 start = pinScreenPosition(*sourceNode, *sourcePin);
                        drawList->AddLine(start, mouse, IM_COL32(240, 208, 116, 230), 2.0F);
                    }
                }
            }
            if (snapshot.nodes.empty()) {
                drawList->AddText(ImVec2{canvasOrigin.x + 20.0F, canvasOrigin.y + 20.0F},
                                  IM_COL32(184, 184, 188, 255),
                                  "Right-click or long-press to search and add a registered node.");
            }
            // Deliberate exception to the editor's own square-corners-everywhere rule: a floating,
            // transient search/pick surface (not a docked panel) reads as a genuinely different kind
            // of UI element - matches the same rounded, dark-gray-transparent treatment already
            // established for the zoom pill immediately below, and the reference shape of Unreal's
            // own right-click/drop-a-wire node search.
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 10.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0F, 10.0F});
            ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4{18.0F / 255.0F, 21.0F / 255.0F, 28.0F / 255.0F, 0.86F});
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{1.0F, 1.0F, 1.0F, 24.0F / 255.0F});
            if (ImGui::BeginPopup("script-node-search-popup")) {
                std::array<char, 257> contextFilterBuffer{};
                std::strncpy(contextFilterBuffer.data(), m_scriptCanvasContextFilter.c_str(),
                             contextFilterBuffer.size() - 1U);
                ImGui::SetNextItemWidth(280.0F);
                if (ImGui::InputTextWithHint("##script-context-search", "Search registered nodes", contextFilterBuffer.data(),
                                             contextFilterBuffer.size())) {
                    m_scriptCanvasContextFilter = contextFilterBuffer.data();
                }
                ImGui::BeginChild("##script-context-results", ImVec2{280.0F, 220.0F}, false);
                std::size_t visibleContextNodes = 0U;
                for (const Scripting::ScriptGraphCanvasPaletteEntryUVE& entry : snapshot.paletteDescriptors) {
                    if (!ContainsCaseInsensitiveUVE(entry.displayName, m_scriptCanvasContextFilter) &&
                        !ContainsCaseInsensitiveUVE(entry.category, m_scriptCanvasContextFilter) &&
                        !ContainsCaseInsensitiveUVE(entry.typeId, m_scriptCanvasContextFilter)) {
                        continue;
                    }
                    ++visibleContextNodes;
                    // Name-only: no icon, no category label - a clean flat list, distinct from the
                    // persistent palette sidebar's own icon+category-grouped presentation.
                    const std::string label = (entry.displayName.empty() ? entry.typeId : entry.displayName) +
                                              "##context-node-" + entry.typeId;
                    if (ImGui::Selectable(label.c_str())) {
                        const auto addResult = ActiveVisualScriptCanvasUVE().AddNodeTypeUVE(
                            entry.typeId, m_scriptCanvasContextMenuPosition, snapshot.revision);
                        // A wire was dropped into empty space to reach this popup (not a plain
                        // right-click/long-press add) - try to auto-wire the source pin to the first
                        // compatible pin on the freshly added node, matching Unreal's own
                        // drop-to-search-and-connect behavior. The new node's id isn't returned by
                        // AddNodeTypeUVE, so it's found as the one id present in a fresh snapshot but
                        // absent from the snapshot this frame started with. A silent no-op (node
                        // stays, just unwired) if no pin on it accepts the link - AddLinkUVE already
                        // owns every real type/direction/duplicate rule, not duplicated here.
                        if (addResult.IsAppliedUVE() && m_scriptCanvasLinkAwaitingPick &&
                            m_scriptCanvasLinkSourceNodeId != 0U) {
                            const Scripting::ScriptGraphCanvasSnapshotUVE freshSnapshot =
                                ActiveVisualScriptCanvasUVE().GetSnapshotUVE();
                            const Scripting::ScriptGraphCanvasNodeSnapshotUVE* newNode = nullptr;
                            for (const auto& candidate : freshSnapshot.nodes) {
                                const bool existedBefore = std::any_of(
                                    snapshot.nodes.cbegin(), snapshot.nodes.cend(),
                                    [&candidate](const auto& old) { return old.id == candidate.id; });
                                if (!existedBefore) {
                                    newNode = &candidate;
                                    break;
                                }
                            }
                            if (newNode != nullptr) {
                                for (const auto& candidatePin : newNode->pins) {
                                    if (candidatePin.direction != Scripting::ScriptPinDirectionUVE::Input) {
                                        continue;
                                    }
                                    const auto linkResult = ActiveVisualScriptCanvasUVE().AddLinkUVE(
                                        Scripting::ScriptLinkUVE{{m_scriptCanvasLinkSourceNodeId, m_scriptCanvasLinkSourcePin},
                                                                 {newNode->id, candidatePin.name}});
                                    if (linkResult.IsAppliedUVE()) {
                                        break;
                                    }
                                }
                            }
                        }
                        m_scriptCanvasLinkSourceNodeId = 0U;
                        m_scriptCanvasLinkSourcePin.clear();
                        m_scriptCanvasLinkAwaitingPick = false;
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (visibleContextNodes == 0U) {
                    ImGui::TextDisabled(m_scriptCanvasContextFilter.empty()
                        ? "No registered nodes are available."
                        : "No results. Try a node name or category.");
                }
                ImGui::EndChild();
                ImGui::EndPopup();
            } else if (m_scriptCanvasLinkAwaitingPick) {
                // Popup dismissed (Escape, click-away) without picking a node - drop the pending
                // link source instead of leaving it silently armed for whatever gesture comes next.
                m_scriptCanvasLinkSourceNodeId = 0U;
                m_scriptCanvasLinkSourcePin.clear();
                m_scriptCanvasLinkAwaitingPick = false;
            }
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);

            // Zoom-percentage + fit-to-view pill, bottom-left of the canvas - matches a design
            // mockup's own `.graph-zoom-ov` control, reusing the exact rounded-pill "bubble" style
            // already established for the 3D Viewport panel's own overlay toolbar
            // (DrawViewportOverlayBubblesUVE) so the two read as the same visual language.
            {
                drawList->AddRectFilled(zoomPillMin, zoomPillMax, IM_COL32(18, 21, 28, 200), zoomPillHeight * 0.5F);
                drawList->AddRect(zoomPillMin, zoomPillMax, IM_COL32(255, 255, 255, 24), zoomPillHeight * 0.5F);

                float cursorX = zoomPillMin.x + kZoomPillPaddingUVE;
                const float buttonY = zoomPillMin.y + kZoomPillPaddingUVE;
                const auto zoomPillButton = [&](const char* const id, const char* const label) {
                    ImGui::SetCursorScreenPos(ImVec2{cursorX, buttonY});
                    const bool pressed = ImGui::InvisibleButton(id, ImVec2{kZoomButtonDiameterUVE, kZoomButtonDiameterUVE});
                    const bool hovered = ImGui::IsItemHovered();
                    const ImVec2 center{cursorX + kZoomButtonDiameterUVE * 0.5F, buttonY + kZoomButtonDiameterUVE * 0.5F};
                    if (hovered) {
                        drawList->AddCircleFilled(center, kZoomButtonDiameterUVE * 0.5F, IM_COL32(255, 255, 255, 20));
                    }
                    const ImVec2 labelSize = ImGui::CalcTextSize(label);
                    drawList->AddText(ImVec2{center.x - labelSize.x * 0.5F, center.y - labelSize.y * 0.5F},
                                      IM_COL32(214, 220, 227, 255), label);
                    cursorX += kZoomButtonDiameterUVE;
                    return pressed;
                };
                const auto applyZoom = [&](const float newZoom) {
                    Scripting::ScriptGraphCanvasViewUVE nextView = view;
                    nextView.zoom = std::clamp(newZoom, Scripting::kMinimumScriptGraphCanvasZoomUVE,
                                               Scripting::kMaximumScriptGraphCanvasZoomUVE);
                    static_cast<void>(ActiveVisualScriptCanvasUVE().SetViewUVE(nextView));
                };
                if (zoomPillButton("##script-zoom-out", "-")) {
                    applyZoom(view.zoom / 1.2F);
                }
                ImGui::SetCursorScreenPos(ImVec2{cursorX, buttonY});
                ImGui::Dummy(ImVec2{zoomLabelWidth, kZoomButtonDiameterUVE});
                drawList->AddText(ImVec2{cursorX + (zoomLabelWidth - ImGui::CalcTextSize(zoomLabel.c_str()).x) * 0.5F,
                                          buttonY + (kZoomButtonDiameterUVE - ImGui::GetTextLineHeight()) * 0.5F},
                                  IM_COL32(166, 172, 180, 235), zoomLabel.c_str());
                cursorX += zoomLabelWidth;
                if (zoomPillButton("##script-zoom-in", "+")) {
                    applyZoom(view.zoom * 1.2F);
                }
                ImGui::SetCursorScreenPos(ImVec2{cursorX, buttonY});
                const bool fitPressed = ImGui::InvisibleButton("##script-zoom-fit", ImVec2{zoomFitWidth, kZoomButtonDiameterUVE});
                const ImVec2 fitLabelSize = ImGui::CalcTextSize("Fit");
                drawList->AddText(ImVec2{cursorX + (zoomFitWidth - fitLabelSize.x) * 0.5F,
                                          buttonY + (kZoomButtonDiameterUVE - fitLabelSize.y) * 0.5F},
                                  IM_COL32(214, 220, 227, 255), "Fit");
                if (fitPressed) {
                    Scripting::ScriptGraphCanvasViewUVE nextView = view;
                    nextView.zoom = 1.0F;
                    nextView.pan = Scripting::ScriptGraphCanvasPointUVE{0.0F, 0.0F};
                    static_cast<void>(ActiveVisualScriptCanvasUVE().SetViewUVE(nextView));
                }
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();

        if (ImGui::BeginChild("##script-details", ImVec2{0.0F, 0.0F}, true)) {
            ImGui::TextColored(ImVec4{0.70F, 0.72F, 0.76F, 1.0F}, "DETAILS");
            ImGui::SameLine();
            ImGui::TextDisabled("node properties");
            ImGui::Separator();
            const auto* const node = selectedNode();
            if (node == nullptr) {
                ImGui::TextDisabled("Select one node to inspect its pins.");
            } else {
                ImGui::TextWrapped("%s", node->displayName.empty() ? node->typeId.c_str() : node->displayName.c_str());
                ImGui::TextDisabled("Type: %s | Node ID: %u", node->typeId.c_str(), node->id);
                ImGui::Separator();
                for (const auto& pin : node->pins) {
                    const ImU32 color = ScriptPinColorUVE(pin);
                    const ImVec4 colorFloat{
                        static_cast<float>((color >> IM_COL32_R_SHIFT) & 0xffU) / 255.0F,
                        static_cast<float>((color >> IM_COL32_G_SHIFT) & 0xffU) / 255.0F,
                        static_cast<float>((color >> IM_COL32_B_SHIFT) & 0xffU) / 255.0F, 1.0F};
                    ImGui::TextColored(colorFloat, "%s %s | %s", pin.direction == Scripting::ScriptPinDirectionUVE::Input ? "IN" : "OUT",
                                       pin.name.c_str(), ScriptValueTypeLabelUVE(pin.type));
                    if (pin.direction == Scripting::ScriptPinDirectionUVE::Input && pin.role == Scripting::ScriptPinRoleUVE::Data &&
                        (pin.type == Scripting::ScriptValueTypeUVE::Number || pin.type == Scripting::ScriptValueTypeUVE::Boolean)) {
                        if (m_scriptCanvasDefaultEditNodeId != node->id || m_scriptCanvasDefaultEditPin != pin.name) {
                            m_scriptCanvasDefaultEditNodeId = node->id;
                            m_scriptCanvasDefaultEditPin = pin.name;
                            m_scriptCanvasDefaultEditBuffer = pin.defaultValue.value_or("");
                        }
                        std::array<char, 257> defaultBuffer{};
                        std::strncpy(defaultBuffer.data(), m_scriptCanvasDefaultEditBuffer.c_str(), defaultBuffer.size() - 1U);
                        const std::string inputId = "Default##" + std::to_string(node->id) + "-" + pin.name;
                        if (ImGui::InputText(inputId.c_str(), defaultBuffer.data(), defaultBuffer.size(),
                                             ImGuiInputTextFlags_EnterReturnsTrue)) {
                            m_scriptCanvasDefaultEditBuffer = defaultBuffer.data();
                            static_cast<void>(ActiveVisualScriptCanvasUVE().SetPinDefaultValueUVE(
                                node->id, pin.name, m_scriptCanvasDefaultEditBuffer));
                        } else {
                            m_scriptCanvasDefaultEditBuffer = defaultBuffer.data();
                        }
                    }
                }
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Validation");
            if (snapshot.diagnostics.empty()) {
                ImGui::TextColored(ImVec4{0.45F, 0.86F, 0.63F, 1.0F}, "No graph diagnostics.");
            } else {
                for (const auto& diagnostic : snapshot.diagnostics) {
                    ImGui::TextWrapped("Node %u: %s", diagnostic.nodeId, diagnostic.message.c_str());
                }
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Compiler");
            if (!m_scriptCompileAttempted) {
                ImGui::TextDisabled("Not compiled yet.");
            } else if (m_scriptLastCompiledGraphRevision != snapshot.graphRevision) {
                ImGui::TextColored(ImVec4{0.93F, 0.72F, 0.35F, 1.0F},
                                   "Graph changed since the last compile.");
            } else {
                const ImVec4 statusColor = m_scriptCompileSucceeded
                    ? ImVec4{0.45F, 0.86F, 0.63F, 1.0F}
                    : ImVec4{0.96F, 0.43F, 0.43F, 1.0F};
                ImGui::TextColored(statusColor, "%s", m_scriptCompileMessage.c_str());
            }
            if (!snapshot.selectedNodeIds.empty() && ImGui::SmallButton("Delete selected node")) {
                for (const std::uint32_t nodeId : snapshot.selectedNodeIds) {
                    static_cast<void>(ActiveVisualScriptCanvasUVE().RemoveNodeUVE(nodeId));
                }
            }
            ImGui::TextDisabled("Graph edits use native validation, revision checks, and canvas history.");
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::End();
}


} // namespace UVE::Editor
