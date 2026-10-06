// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <chrono>
#include <limits>
#include <map>
#include <memory>
#include <atomic>
#include <future>
#include <mutex>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <variant>
#include <vector>

#include "uve/asset/i_asset_database_uve.h"
#include "uve/asset/i_asset_import_queue_uve.h"
#include "uve/asset/i_project_file_index_uve.h"
#include "uve/asset/i_project_change_watcher_uve.h"
#include "uve/core/engine_services_uve.h"
#include "uve/core/i_simulation_control_uve.h"
#include "uve/config/settings_registry_uve.h"
#include "uve/editor/animation_clip_editing_uve.h"
#include "uve/editor/editor_color_uve.h"
#include "uve/editor/editor_commands_uve.h"
#include "uve/asset/gltf_skeleton_uve.h"
#include "uve/editor/editor_content_browser_model_uve.h"
#include "uve/editor/editor_retarget_plan_uve.h"
#include "uve/editor/editor_hierarchy_view_uve.h"
#include "uve/input/input_action_uve.h"
#include "uve/editor/editor_tool_session_uve.h"
#include "uve/editor/developer_console_uve.h"
#include "uve/editor/editor_ui_assets_uve.h"
#include "uve/editor/inspector_drawer_registry_uve.h"
#include "uve/object/type_metadata_uve.h"
#include "uve/scene/scene_component_metadata_uve.h"
#include "uve/editor/mesh_thumbnail_renderer_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/component/animation_sequencer_component_uve.h"
#include "uve/component/animation_graph_component_uve.h"
#include "uve/component/audio_source_component_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/canvas_component_uve.h"
#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/auto_translate_component_uve.h"
#include "uve/component/editor_description_component_uve.h"
#include "uve/component/object_metadata_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/thread_group_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/objects/3d/all_objects_3d_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/particle_emitter_component_uve.h"
#include "uve/component/physics_interpolation_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/scene/objects/scene_object_registry_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/scene/i_scene_serializer_uve.h"
#include "uve/uvscript/uvscript_ast_uve.h"
#include "uve/uvscript/uvscript_value_uve.h"

namespace UVE::Asset {
struct AnimationClipAssetUVE;
} // namespace UVE::Asset

namespace UVE::Editor::Tests {
struct EditorUVEAccessUVE;
}

namespace UVE::Editor {

struct EditorSettingBindingUVE;

class EditorBridgeUVE;

/// EditorStateUVE is the lifecycle state of one editor session. The editor owns session data only;
/// EngineCoreUVE remains the owner of the ECS, renderer, window, and every engine service.
enum class EditorStateUVE {
    Uninitialized,
    Running,
    Shutdown,
};

/// The editor-owned transient simulation state. Edit permits authored document commands; Playing
/// and Paused own an immutable pre-Play document snapshot and reject authoring mutations.
enum class EditorPlayModeStateUVE {
    Edit,
    Playing,
    Paused,
};

/// Editor-only 2D canvas presentation state for authored screen-space content such as loading
/// screens. The design surface is not an ECS entity, is never serialized into a scene, and never
/// changes runtime state. Pan is expressed in desktop pixels relative to the fitted canvas center.
struct Editor2DCanvasStateUVE final {
    static constexpr float kDesignWidth = 1920.0F;
    static constexpr float kDesignHeight = 1080.0F;

    float zoom = 0.36F;
    Math::Vector2UVE pan{};
    bool gridVisible = true;
    bool safeAreaVisible = true;
};

/// Canonical named axes used by EditorUVE's Translate, Rotate, and Scale gizmos. The active
/// coordinate space chooses whether their world or selected-entity-local basis is used.
enum class EditorTransformAxisUVE {
    None,
    X,
    Y,
    Z,
};

/// Source-compatible name retained for downstream editor callers; new code should use the
/// transform-wide name because the same axis contract is shared by Translate, Rotate, and Scale.
using EditorTranslateAxisUVE = EditorTransformAxisUVE;

/// Selects whether hierarchy reparenting retains authored local TRS or preserves compatible
/// captured world TRS. This editor-session preference is never serialized or added to history.
enum class EditorReparentTransformModeUVE {
    KeepLocal,
    KeepWorld,
};

/// Session-local transform snapping settings. These values are editor-only and are not serialized
/// into scene documents or runtime state.
struct EditorTransformSnappingSettingsUVE final {
    bool enabled = false;
    float translateStep = 1.0F;
    float rotateStepDegrees = 15.0F;
    float scaleStep = 0.1F;
};

/// Where a newly created 3D object appears: at its parent's origin, or at the point the viewport
/// camera orbits - what the person is looking at.
enum class EditorNewObjectPlacementUVE {
    ParentOrigin,
    ViewFocus,
};

/// Where an object moves among its siblings: one place up or down, or to either end.
enum class EditorSiblingMoveUVE {
    Up,
    Down,
    ToTop,
    ToBottom,
};

/// One stored editor-viewport pose, expressed in orbit-camera terms (pivot target, yaw/pitch,
/// orbit distance) so the value is independent of any concrete camera implementation - the app
/// host applies it through its OrbitCamera's SetTarget/SetYawPitch/SetDistance. Session-local and
/// never serialized (persisting these across runs needs a settings schema this increment does not
/// define - real, separate follow-up, the same call Unreal makes for its own Ctrl+0..9 viewport
/// bookmarks when they are stored only in per-user editor settings anyway).
struct EditorViewportBookmarkUVE final {
    Math::Vector3UVE target{};
    float yawRadians = 0.0F;
    float pitchRadians = 0.0F;
    float distance = 10.0F;
};

/// The numeric bookmark slots following the Unreal editor convention (Ctrl+digit stores,
/// plain digit restores).
inline constexpr std::size_t kEditorViewportBookmarkSlotCountUVE = 10U;

/// The orbit distance fly-to-marker bookmarks are composed at: near enough to actually frame the
/// subject the marker stares at, far enough to keep the near-clip out of trouble. A bookmark is
/// trivially dollied afterwards, so this is only ever a starting point.
inline constexpr float kEditorMarkerFocusDistanceUVE = 5.0F;

/// A read-only oriented box for the selected collider-backed document entity. All points are in
/// derived world space and are intended for editor feedback only; this value is never serialized.
struct EditorSelectionBoundsUVE final {
    std::array<Math::Vector3UVE, 8> worldCorners{};
    Math::Vector3UVE worldCenter{};
};

/// The supported Library workspace archetypes. Each created entity is a document root with a
/// TransformComponentUVE; specialized kinds add only the named gameplay or built-in primitive component.
enum class EditorSceneComponentKindUVE : std::uint8_t {
    Camera,
    Mesh,
    Light,
    Collider,
    Rigid3D,
    AudioSource,
    ParticleEmitter,
    Script,
    AnimationSequencer,
    WorldEnvironment,
    CharacterController,
    Canvas,
    UIText,
    UIImage,
    UIButton,
    PhysicsInterpolation,
    EditorDescription,
    Process,
    ThreadGroup,
    AutoTranslate,
    ObjectMetadata,
};

using EditorSceneComponentValueUVE =
    std::variant<Scene::CameraComponentUVE, Scene::MeshComponentUVE, Scene::LightComponentUVE,
                 Scene::ColliderComponentUVE, Scene::Rigid3DComponentUVE, Scene::AudioSourceComponentUVE,
                 Scene::ParticleEmitterComponentUVE, Scene::ScriptComponentUVE,
                 Scene::AnimationSequencerComponentUVE, Scene::WorldEnvironment3DComponentUVE,
                 Scene::CharacterControllerComponentUVE, Scene::CanvasComponentUVE, Scene::UITextComponentUVE,
                 Scene::UIImageComponentUVE, Scene::UIButtonComponentUVE,
                 Scene::PhysicsInterpolationComponentUVE, Scene::EditorDescriptionComponentUVE, Scene::ProcessComponentUVE,
                 Scene::ThreadGroupComponentUVE, Scene::AutoTranslateComponentUVE,
                 Scene::ObjectMetadataComponentUVE>;

enum class EditorEntityKindUVE {
    Empty,
    Camera,
    DirectionalLight,
    CollisionBox,
    Cube,
    UVSphere,
    Plane,
};

/// EditorUVE composes the existing engine services into a first editor foundation: an editor-owned
/// camera, deterministic hierarchy and collider-backed viewport selection, a transform inspector
/// mutation path, world-space Translate, Rotate, and Scale gizmos, scene-document save/load, and an editor-private
/// Dear ImGui overlay. No Dear ImGui type appears in this public interface, so the UI backend remains
/// an implementation detail of engine/editor.
///
/// The supplied EngineServicesUVE reference must remain valid from InitUVE() through ShutdownUVE().
/// EditorUVE is main-thread only, matching the scene, render, and window services it composes.
/// What the editor read from a model source file (.gltf, .glb, .obj, .fbx) on the last project
/// refresh - enough to label it and decide whether it has a mesh to import.
struct EditorModelSourceInfoUVE final {
    /// A mesh is skinned to bones: shown as Model rather than Mesh.
    bool rigged = false;
    /// The file has bones a Skeleton3D can take - skinned or not, so an animation file counts.
    bool hasSkeleton = false;
    /// A skeleton and its animation with no mesh: shown as Animation, and not imported as a mesh.
    bool animationOnly = false;
    /// A one-line description for the Content Browser tooltip ("162 bones, 1 animation, 0.27 s").
    std::string summary;
    /// Takes in the file: each is imported as a `.uvanim` clip beside it.
    std::size_t animationCount = 0U;
};

/// The Retarget window's state: the animations picked in Content, the character they are for, what
/// conforming would do, and Generate's progress.
struct RetargetWindowStateUVE final {
    /// The animations to conform, absolute paths.
    std::vector<std::filesystem::path> animations;
    /// The character: a content-relative model source (its imported model is conformed) or a `.uvmodel`.
    std::filesystem::path target;
    RetargetPlanUVE plan;
    bool planStale = true;
    std::string boneFilter;
    bool problemsOnly = false;
    /// The last result, one line.
    std::string status;
    bool statusIsError = false;
    /// Where the last run's originals are, for Undo.
    std::filesystem::path lastBackup;
    /// Generate's progress, read from the window while a worker conforms the files.
    struct Progress final {
        std::atomic<std::size_t> done{0U};
        std::atomic<std::size_t> total{0U};
        std::mutex mutex;
        std::string what;
    };
    std::shared_ptr<Progress> progress = std::make_shared<Progress>();
    std::future<Retarget::RetargetFilesResultUVE> job;
    /// The character file the running job conforms.
    std::filesystem::path jobModel;
    /// Characters conformed or restored while the window was open: the scene put aside by the
    /// preview still holds their old bones until it is back.
    std::vector<std::filesystem::path> changedModels;
};

class EditorUVE final {
    friend struct Tests::EditorUVEAccessUVE;
    friend class EditorBridgeUVE;

public:
    explicit EditorUVE(Core::EngineServicesUVE& services,
                       std::filesystem::path activeScenePath = "editor_scene.uvscene",
                       std::size_t historyCapacity = 100U,
                       Core::ISimulationControlUVE* simulationControl = nullptr);
    ~EditorUVE();

    EditorUVE(const EditorUVE&) = delete;
    EditorUVE& operator=(const EditorUVE&) = delete;

    /// Creates the non-document editor camera and initializes the private UI backend when a real
    /// native window is available. Safe in headless mode: hierarchy, inspector, picking helpers,
    /// transform editing, and persistence logic remain usable while UI rendering is disabled.
    void InitUVE();

    /// Validates a possibly deleted selection, cancels an invalid gizmo drag, and performs
    /// non-rendering per-frame maintenance.
    void TickUVE();

    /// Captures the complete editable document into an in-memory scene envelope and enters the
    /// transient Play sandbox. Returns false without document mutation when capture or Core control fails.
    [[nodiscard]] bool EnterPlayModeUVE();
    [[nodiscard]] bool PausePlayModeUVE();
    [[nodiscard]] bool ResumePlayModeUVE();
    [[nodiscard]] bool StepPlayModeUVE();
    [[nodiscard]] bool StopPlayModeUVE();
    [[nodiscard]] EditorPlayModeStateUVE GetPlayModeStateUVE() const noexcept;

    /// Draws the private editor overlay. EngineCoreUVE invokes this from its post-render callback
    /// before PresentUVE(), after the HDR scene has passed through the standard tone-mapping path.
    void RenderOverlayUVE();

    /// The 4 standard transform-tool modes a Viewport overlay toolbar exposes, named generically
    /// (not tied to any specific renderer's own enum) so EditorCore stays engine/viewport-agnostic
    /// - see ViewportPanelRendererUVE's own doc comment below for why.
    enum class ViewportGizmoModeUVE {
        Move,
        Rotate,
        Scale,
        Universal,
    };

    /// The viewport's named camera views. User is any free orbit; the others look along a world
    /// axis at the orbit target (Top looks down -Y, Front looks down -Z, Right looks down -X).
    enum class ViewportViewUVE {
        User,
        Top,
        Bottom,
        Front,
        Back,
        Right,
        Left,
    };

    /// Current state of the Viewport panel's own overlay toolbar (projection mode, active gizmo
    /// tool, snap, grid) - owned and mutated by EditorUVE's own overlay-drawing code
    /// (DrawViewportPanelUVE()), then handed to ViewportPanelRendererUVE each frame so the
    /// concrete renderer can apply it to its own real projection/gizmo-mode/grid state. Kept as
    /// plain enums/bools with no viewport-module type in sight, for the same reason.
    /// One axis colour as plain RGB in 0..1 - see ViewportOverlayStateUVE::axisColorX for why this
    /// is a local struct rather than the viewport's own palette type.
    struct ViewportAxisColorUVE final {
        float r = 0.0F;
        float g = 0.0F;
        float b = 0.0F;
    };

    /// One Skeleton3D bone in world space, for the viewport to draw (see BoneShape in the viewport
    /// module for the look). Plain floats for the same reason as the rest of the overlay state.
    struct ViewportBoneUVE final {
        std::array<float, 3> head{};
        std::array<float, 3> tail{};
        std::array<float, 3> side{1.0F, 0.0F, 0.0F};
        bool hasLink = false;
        std::array<float, 3> linkFrom{};
        bool skeletonSelected = false;
        bool boneSelected = false;
        /// A colour of its own (the Retarget window's joint colours); otherwise the selection look.
        bool hasColour = false;
        std::array<float, 3> colour{};
    };

    struct ViewportOverlayStateUVE final {
        bool orthographic = false;
        // The named view the camera is in (User once it is orbited freely), and a counter bumped
        // on every request to move to one. The host applies a request when the counter changes,
        // so picking the same view twice still re-snaps, and nothing has to be "consumed".
        ViewportViewUVE view = ViewportViewUVE::User;
        std::uint32_t viewRequestSerial = 0U;
        // A request to bring an object into view (F over the viewport, or Focus in Viewport on a
        // hierarchy row), applied by the host when the counter changes, like the view request.
        Scene::EntityUVE focusEntity = Scene::kInvalidEntityUVE;
        std::uint32_t focusRequestSerial = 0U;
        ViewportGizmoModeUVE gizmoMode = ViewportGizmoModeUVE::Universal;
        bool snapEnabled = false;
        bool gridVisible = true;
        // How strongly the grid is drawn, 0.1..1. Persisted with gridVisible; see SetViewportGridUVE.
        float gridOpacity = 1.0F;
        // The smallest grid square, in world units; see SetViewportGridCellSizeUVE.
        float gridCellSize = 1.0F;
        // The outline drawn around selected meshes; see SetViewportSelectionOutlineUVE.
        bool selectionOutlineVisible = true;
        ViewportAxisColorUVE selectionOutlineColor{1.0F, 0.62F, 0.16F};
        float selectionOutlineThickness = 2.0F;
        // True while the Game workspace tab is active (see EditorWorkspaceUVE::Game): the concrete
        // renderer should hide editor-only overlays (grid, transform gizmo) in this mode, matching
        // Unity's own Scene/Game split, since Game is meant to preview what a player would see.
        bool gameWorkspaceActive = false;
        // True while the pointer is over one of the overlay toolbar's own bubble buttons.
        //
        // The bubbles float on top of the rendered image inside the same ImGui window, so the
        // renderer's IsWindowHovered() is equally true over a button and over the scene - which
        // made clicking "Move" also register as a click on empty space and clear the selection.
        // The renderer callback runs BEFORE the bubbles are submitted each frame, so it cannot ask
        // ImGui directly; this carries the answer to it instead. It is therefore one frame old,
        // which is imperceptible for a hover state and exact for every frame of a press.
        bool pointerOverOverlay = false;

        // The entity context toolbar (right-click an entity -> a small "Scripting" bubble anchored
        // at its projected screen position). The world->screen projection needs OrbitCamera, which
        // lives in Engine/Editor/Viewport - a module EditorCore may not depend on - so main.cpp
        // computes the anchor pixel each frame and pushes it in via SetEntityContextToolbarAnchorUVE
        // before RenderOverlayUVE runs that same frame; unlike gizmoMode/orthographic/etc above,
        // this direction has no one-frame lag; entityContextToolbarOpen only goes false again
        // through ClearEntityContextToolbarUVE (a miss) or DrawEntityContextToolbarUVE consuming a
        // click, both driven by this same class.
        bool entityContextToolbarOpen = false;
        Scene::EntityUVE entityContextToolbarEntity = Scene::kInvalidEntityUVE;
        float entityContextToolbarPixelX = 0.0F;
        float entityContextToolbarPixelY = 0.0F;

        // The author's chosen X/Y/Z axis colours, as RGB in 0..1.
        //
        // Deliberately plain float triples and not any Viewport-module palette type, for the same
        // reason as everything else in this struct: EditorCore does not link the viewport, so the
        // colour picker that edits these lives here while the renderer that applies them lives
        // across the boundary. The viewport derives the grid's darker axis lines from these, so
        // one choice moves both the gizmo and the grid.
        //
        // `axisColorsValid` starts false and the values start at zero ON PURPOSE. The default hues
        // belong to the viewport's own AxisPalette, which this module may not include, so the host
        // seeds them once at startup through SetViewportAxisColorsUVE. Until it does, the flag
        // tells the renderer to keep its own defaults rather than apply three zeroes and paint
        // every axis black.
        ViewportAxisColorUVE axisColorX{};
        ViewportAxisColorUVE axisColorY{};
        ViewportAxisColorUVE axisColorZ{};
        bool axisColorsValid = false;

        // Every enabled, visible Skeleton3D's bones, rebuilt each frame; empty in the Game workspace.
        std::vector<ViewportBoneUVE> bones;

        // A studio view for presenting rather than editing (the Retarget window): sky, ground and
        // a faded floor instead of the grid, no toolbar or corner gizmo, no selection, and a
        // camera that faces the front and does not turn (the wheel still zooms). The camera is
        // placed on `studioTarget`, far enough to fit `studioRadius`, each time the serial changes.
        bool studioView = false;
        std::array<float, 3> studioTarget{0.0F, 1.0F, 0.0F};
        float studioRadius = 1.5F;
        std::uint32_t studioFramingSerial = 0U;
    };

    /// Render callback for the dockable "Viewport" panel: given the panel's current available
    /// content-region size and the overlay toolbar's current state (see ViewportOverlayStateUVE),
    /// renders into the caller's own framebuffer at (at most) that size and returns an ImGui
    /// texture ID (ImTextureID is ImU64 in the vendored ImGui version) to display via
    /// ImGui::Image(), writing the actual rendered size back through outUsedSize. Returning 0
    /// means "not ready yet" - nothing is drawn that frame. Deliberately free of any ImGui/GL/
    /// viewport-module types so EditorCore stays engine/viewport-agnostic (this is an upper-layer
    /// module that may compose Engine/Runtime, not something that should link a sibling
    /// Engine/Editor module directly) - see Engine/App/src/editor/main.cpp for the concrete
    /// Engine/Editor/Viewport-backed implementation.
    /// Identifies one of the editor's physically independent render views.  This is deliberately
    /// explicit at the renderer boundary: Main, Entity Editor, and Retarget must never reuse an
    /// orbit camera, gesture, framebuffer, or render texture merely because only one happened to
    /// be visible in an earlier frame.
    enum class ViewportContextUVE : std::uint8_t {
        Main = 0,
        EntityEditor,
        Retarget,
        Count
    };

    using ViewportPanelRendererUVE =
        std::function<std::uint64_t(ViewportContextUVE context, const Math::Vector2UVE& availableSize,
                                    Math::Vector2UVE& outUsedSize,
                                    const ViewportOverlayStateUVE& overlayState)>;

    /// Registers (or clears, with an empty std::function) the Viewport panel's render callback.
    /// Called once per frame from RenderOverlayUVE() while the panel is visible.
    void SetViewportPanelRendererUVE(ViewportPanelRendererUVE renderer);

    /// Saves every document root except the editor camera to the active .uvscene path. Dirty state
    /// is cleared only after the scene serializer reports success.
    [[nodiscard]] bool SaveSceneUVE();

    /// Saves everything with unsaved changes: the scene and the open UVScript. Returns how many
    /// were written; a failure is reported in the Content status line.
    std::size_t SaveAllUVE();

    /// Saves the sole selected document subtree as a canonical `.uvprefab` and registers its source
    /// GUID through the existing PrefabSystemUVE. This command never runs during Play or a viewport gesture.
    [[nodiscard]] bool SaveSelectedPrefabUVE(const std::filesystem::path& path);

    /// Makes what the Content catalogue item `itemId` stands for inside `directory`: a folder, a
    /// `.uventity` holding the item's object tree, or a `.uvscene` with a SceneRoot, Viewport and
    /// World folder. Names never collide ("Character", "Character 2", ...). The document is not
    /// touched and no undo step is recorded. Returns the new path, or nothing in Play, for an
    /// unknown item or a failed write.
    [[nodiscard]] std::optional<std::filesystem::path> CreateContentCatalogueItemUVE(
        std::string_view itemId, const std::filesystem::path& directory);

    /// Brings the entity asset (`.uventity` or `.uvprefab`) at `path` into the scene under
    /// `parent` - or, when that is invalid, where a new object would go - selects it and records one
    /// undo step. Returns the new root, or kInvalidEntityUVE.
    [[nodiscard]] Scene::EntityUVE PlaceEntityAssetUVE(const std::filesystem::path& path,
                                                       Scene::EntityUVE parent = Scene::kInvalidEntityUVE);
    /// Changes an AnimationSequencer as one undo step (its animation list, its current clip).
    bool EditAnimationSequencerUVE(Scene::EntityUVE player,
                                const std::function<void(Scene::AnimationSequencerComponentUVE&)>& change);
    /// Adds a project clip to the player's list and makes it the one playing.
    bool AddClipToAnimationSequencerUVE(Scene::EntityUVE player, const std::filesystem::path& absoluteClip);

    /// Brings a model source (an FBX, glTF or OBJ in Content, by its content-relative path) into the
    /// scene as one undo step and returns its root. A file with bones becomes
    ///   <File> (Object3D)
    ///   +- Armature (Object3D)
    ///   |  +- Skeleton3D          bound to the file's bones
    ///   |     +- <File> Mesh      (MeshInstance3D, when the file has a mesh)
    ///   +- AnimationSequencer        playing the file's first take, looping (when it has takes)
    /// and a file without bones becomes one MeshInstance3D. Refused while the converted mesh a
    /// file needs is not imported yet.
    [[nodiscard]] Scene::EntityUVE PlaceModelSourceUVE(const std::filesystem::path& relativeSource,
                                                       Scene::EntityUVE parent = Scene::kInvalidEntityUVE);

    /// The Entity Editor: an entity asset opened on its own, in its own window. While it is open
    /// the entity *is* the document - the scene is put aside in a snapshot (the way Play does),
    /// so every tool (Scene tree, Inspector, gizmos, undo) works on the entity unchanged, and the
    /// main window stops drawing. Undo starts empty and is cleared again on close: entity handles
    /// are fresh on both sides. Refused in Play or while another entity is open.
    bool OpenEntityEditorUVE(const std::filesystem::path& assetPath);
    [[nodiscard]] bool IsEntityEditorOpenUVE() const noexcept;

    /// Opens the Retarget window for `animations` (absolute `.uvanim` paths). `target` is the
    /// character's content-relative model source or `.uvmodel`, or empty to choose in the window.
    void OpenRetargetWindowUVE(std::vector<std::filesystem::path> animations, std::filesystem::path target = {});
    [[nodiscard]] bool IsRetargetWindowOpenUVE() const noexcept { return m_retargetWindow.has_value(); }
    void CloseRetargetWindowUVE();
    /// The open entity's file, or empty.
    [[nodiscard]] std::filesystem::path GetEntityEditorAssetPathUVE() const;
    /// The open entity's root object (the one child of the scene root), or kInvalidEntityUVE.
    [[nodiscard]] Scene::EntityUVE GetEntityEditorRootUVE();
    /// Writes the entity back to its file. Refused (with a Content status line) when the root is
    /// gone or other objects sit beside it: an entity has exactly one root.
    bool SaveEntityEditorUVE();
    /// Throws away the edits and loads the file again.
    bool RevertEntityEditorUVE();
    /// Closes the Entity Editor, saving first when `save`, and brings the scene back. After a save,
    /// clean instances of the entity in the scene are refreshed from the file.
    bool CloseEntityEditorUVE(bool save);

    /// Imports every take of the FBX at `absoluteSource` as a skeletal `.uvanim` beside it, named
    /// "<file>_<take>.uvanim". A clip newer than the FBX is left alone, so this is cheap to call on
    /// every refresh. Returns the paths written.
    std::vector<std::filesystem::path> ImportModelAnimationsUVE(const std::filesystem::path& absoluteSource);

    /// The Entity Editor's middle area.
    enum class EntityEditorTabUVE : std::uint8_t { Viewport, Scripting, Events };
    /// The Entity Editor's bottom dock.
    enum class EntityEditorDockTabUVE : std::uint8_t { Content, Timeline, AnimGraph };
    [[nodiscard]] EntityEditorTabUVE GetEntityEditorTabUVE() const noexcept;
    void SetEntityEditorTabUVE(EntityEditorTabUVE tab) noexcept;
    /// One thing Compile found wrong with the open entity. `entity` is the object it belongs to
    /// (kInvalidEntityUVE for a problem with the entity as a whole); `at` is zero when it has no
    /// place in a script.
    struct EntityCompileProblemUVE final {
        Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
        std::string objectName;
        std::string scriptPath;
        UVScript::SourceLocationUVE at;
        std::string message;
    };
    /// Checks the whole open entity: one root, and every object's `.uvs` script (read from the
    /// open text editor when it has unsaved text, else from disk) compiled against that object.
    /// Returns the problem count; the list stays until the next Compile, Revert or close.
    std::size_t CompileEntityEditorUVE();
    [[nodiscard]] const std::vector<EntityCompileProblemUVE>& GetEntityEditorProblemsUVE() const noexcept;
    /// False until Compile has run for this session (so "no problems" means something).
    [[nodiscard]] bool HasEntityEditorCompiledUVE() const noexcept;
    /// One `on <event>` handler in an object's script: what the object answers to.
    struct EntitySignalRowUVE final {
        Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
        std::string objectName;
        std::string scriptPath;
        std::string event;
        /// "(other, impulse)" or empty for a handler without parameters.
        std::string params;
        std::uint32_t line = 0U;
    };
    /// Every handler of every script in the open entity, in tree order then source order.
    [[nodiscard]] std::vector<EntitySignalRowUVE> GetEntityEditorSignalsUVE();
    /// Selects `entity`, opens its script in the Scripting tab and puts the caret on `line`
    /// (1-based; 0 leaves it). False when the object has no `.uvs` script.
    bool GoToEntityScriptUVE(Scene::EntityUVE entity, std::uint32_t line);
    /// Unsaved edits anywhere in the Entity Editor: the tree or the open script.
    [[nodiscard]] bool HasEntityEditorUnsavedChangesUVE() const noexcept;

    /// Stores `contentRelativePath` (a `.uventity`) as the project's Default Player and saves the
    /// project settings. An empty path clears it.
    [[nodiscard]] bool SetDefaultPlayerEntityUVE(const std::filesystem::path& contentRelativePath);
    [[nodiscard]] std::string GetDefaultPlayerEntityUVE() const;

    /// `directory/stem.extension`, or `directory/stem N.extension` with the smallest N >= 2 that is
    /// free. `extension` is empty for a folder.
    [[nodiscard]] static std::filesystem::path MakeUniqueContentPathUVE(const std::filesystem::path& directory,
                                                                        std::string_view stem,
                                                                        std::string_view extension);

    /// Renames `file` (a file or folder) to `newStem` plus its old extension, in the same folder.
    /// Nothing happens for an empty or path-like stem, or when the name is taken. Returns the new
    /// path. An asset already placed in a scene keeps pointing at the old name.
    /// RenameContentFileUVE, and the renamed file keeps its GUID in the asset database.
    std::optional<std::filesystem::path> RenameContentAssetUVE(const std::filesystem::path& file,
                                                               std::string_view newStem);
    [[nodiscard]] static std::optional<std::filesystem::path> RenameContentFileUVE(const std::filesystem::path& file,
                                                                                   std::string_view newStem);

    /// Copies `file` next to itself under the first free name ("Hero 2.uventity"). Folders are
    /// copied whole. Returns the copy's path.
    [[nodiscard]] static std::optional<std::filesystem::path> DuplicateContentFileUVE(const std::filesystem::path& file);

    /// Moves `id` to the front of `recent`, without duplicates, keeping at most five.
    static void PushContentCreateRecentUVE(std::vector<std::string>& recent, std::string_view id);

    /// Refreshes the sole selected prefab instance from its current source revision. Dirty instances
    /// are rejected with merge-required semantics and are never silently overwritten.
    [[nodiscard]] bool RefreshSelectedPrefabUVE();

    /// Explicitly discards persisted local prefab overrides and refreshes from source. This is a
    /// destructive authoring command and is rejected outside Edit mode or without a sole selection.
    [[nodiscard]] bool DiscardSelectedPrefabOverridesAndRefreshUVE();

    /// Replaces the editable document scene with the active .uvscene file. A backup scene is
    /// created before destructive mutation and restored if deserialization fails; the editor camera
    /// remains outside the document root set.
    [[nodiscard]] bool LoadSceneUVE();

    /// Makes a Content Browser `.uvscene` the active scene and loads it. The caller supplies an
    /// existing regular scene file; the current document is recovered by LoadSceneUVE() if the
    /// replacement fails.
    [[nodiscard]] bool OpenSceneAssetUVE(const std::filesystem::path& path);

    /// Makes entity the sole ordered hierarchy/inspector selection when it is live; invalid or
    /// deleted handles clear the selection instead of exposing stale ECS state.
    void SelectEntityUVE(Scene::EntityUVE entity) noexcept;
    /// Adds a live document entity to the ordered selection or removes it when already selected.
    /// A newly added entity becomes active. Removing the active entity promotes the last remaining
    /// selected entity; removing the final entity clears the active selection.
    void ToggleEntitySelectionUVE(Scene::EntityUVE entity) noexcept;
    void ClearSelectionUVE() noexcept;
    /// Returns the ordered, deduplicated live document selection. The final entry is active whenever
    /// ToggleEntitySelectionUVE() removed the prior active entity.
    [[nodiscard]] const std::vector<Scene::EntityUVE>& GetSelectedEntitiesUVE() const noexcept;
    /// Returns true only when the active entity is the sole selected live document entity.
    [[nodiscard]] bool HasSingleDocumentSelectionUVE() const noexcept;

    /// Applies one validated local transform through ISceneGraphUVE, ensuring derived world
    /// transforms are marked dirty for EngineCoreUVE's next scene-graph update. Returns false for
    /// invalid/deleted/non-transform entities or non-finite transform values.
    [[nodiscard]] bool SetSelectedLocalTransformUVE(const Scene::TransformComponentUVE& transform);
    /// Inspector section clipboard. Copy takes the selected entity's whole component of `entry`'s
    /// type; Paste writes a copied value back onto a component of the same type (any entity) as one
    /// undoable edit, refused when the clipboard holds another type or the value breaks the
    /// component's own rule; Reset writes the type's default the same way.
    [[nodiscard]] bool CopySelectedComponentUVE(const Core::TypeMetadataEntryUVE& entry);
    [[nodiscard]] bool CanPasteSelectedComponentUVE(const Core::TypeMetadataEntryUVE& entry) const noexcept;
    [[nodiscard]] bool PasteSelectedComponentUVE(const Core::TypeMetadataEntryUVE& entry);
    [[nodiscard]] bool ResetSelectedComponentUVE(const Core::TypeMetadataEntryUVE& entry);
    /// The same for the Transform section, which is drawn by hand rather than from metadata. Only
    /// the local pose travels - position, rotation (with its Euler authoring state) and scale; the
    /// object's own top-level flag stays as it is.
    [[nodiscard]] bool CopySelectedTransformUVE();
    [[nodiscard]] bool CanPasteSelectedTransformUVE() const noexcept { return m_transformClipboard.has_value(); }
    [[nodiscard]] bool PasteSelectedTransformUVE();
    [[nodiscard]] bool ResetSelectedTransformUVE();

    /// Adds or updates persistent human-readable metadata for the selected live document entity.
    /// Returns false without mutation for invalid editor/selection state, an empty or whitespace-only
    /// name, a name longer than the supported editor-entry limit, or an unchanged value.
    [[nodiscard]] bool SetSelectedEntityNameUVE(std::string name);
    /// Shows or hides `entity` (its Visibility component's authored switch) as one undoable edit.
    /// Unlike the selected-entity setters this targets any document entity, so the hierarchy's
    /// eye toggle works on a row without changing the selection. Returns false without mutation
    /// when editing is not allowed, the entity has no Visibility component, or nothing changes.
    [[nodiscard]] bool SetEntityVisibleUVE(Scene::EntityUVE entity, bool visible);
    /// Asks the viewport to bring `entity` into view: a Marker3D flies into its viewpoint, any
    /// other object with a world position becomes the orbit pivot. Returns false and requests
    /// nothing when CanFocusEntityInViewportUVE() says no.
    [[nodiscard]] bool RequestViewportFocusUVE(Scene::EntityUVE entity);
    /// True for a document entity the viewport can focus: one with a world position or a usable
    /// Marker3D viewpoint. The scene root and plain Objects have neither.
    [[nodiscard]] bool CanFocusEntityInViewportUVE(Scene::EntityUVE entity) const;
    [[nodiscard]] std::uint32_t GetViewportFocusRequestSerialUVE() const noexcept {
        return m_viewportOverlayState.focusRequestSerial;
    }
    [[nodiscard]] Scene::EntityUVE GetViewportFocusEntityUVE() const noexcept {
        return m_viewportOverlayState.focusEntity;
    }
    /// Opens or closes `entity`'s row in the hierarchy together with every row below it. Rows
    /// that are drawn the next frame change at once; a row inside a collapsed branch keeps the
    /// request until it is next drawn, so reopening the branch later shows it closed. Returns
    /// false for anything that is not a document entity.
    [[nodiscard]] bool SetHierarchyBranchOpenUVE(Scene::EntityUVE entity, bool open);
    /// The display name of `entity`'s object type ("Static3D"): its stored type, or for an object
    /// saved before types were stored, the best reading of its components
    /// (Scene::ResolveSceneObjectKindUVE). Empty for anything that is not a document entity.
    [[nodiscard]] std::string_view GetObjectTypeNameUVE(Scene::EntityUVE entity) const;
    /// True when `entity` is a document object that can make `move` among its siblings: not the
    /// scene root, and not already at the end it would move toward.
    [[nodiscard]] bool CanMoveDocumentEntityUVE(Scene::EntityUVE entity, EditorSiblingMoveUVE move);
    /// Moves `entity` among its siblings, keeping its parent and transform, as one undoable edit.
    /// Returns false, changing nothing, when CanMoveDocumentEntityUVE does.
    [[nodiscard]] bool MoveDocumentEntityUVE(Scene::EntityUVE entity, EditorSiblingMoveUVE move);
    /// The hierarchy panel preferences in effect (Editor Preferences > Hierarchy).
    [[nodiscard]] const HierarchyViewSettingsUVE& GetHierarchyViewSettingsUVE() const noexcept { return m_hierarchyView; }
    /// The open state a hierarchy row will be given when it is next drawn, if one is pending.
    [[nodiscard]] std::optional<bool> GetPendingHierarchyRowOpenUVE(Scene::EntityUVE entity) const;
    /// Problems with how `entity` is set up, one readable sentence each, for the hierarchy's
    /// warning badge: a non-finite transform, a script path that is not a valid project path, a
    /// mesh object with no mesh or with a mesh/material the project no longer has, a Skeleton3D
    /// with no source model. Empty when there is nothing to fix.
    [[nodiscard]] std::vector<std::string> GetObjectWarningsUVE(Scene::EntityUVE entity) const;
    /// The script attached to `entity`, when it has one (a non-empty script path).
    [[nodiscard]] std::optional<std::string> GetObjectScriptPathUVE(Scene::EntityUVE entity) const;

    /// Adds or replaces one supported scene component on the selected document entity using the
    /// value variant matching `kind`. Valid changes are one Undo/Redo transaction; invalid, unchanged,
    /// multi-selected, protected-Play, active-gesture, or mismatched kind/value calls fail atomically.
    [[nodiscard]] bool SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE kind,
                                                     const EditorSceneComponentValueUVE& value);

    /// Removes one supported scene component from the selected entity as one Undo/Redo transaction.
    /// Core identity components such as Transform, Name, and Hierarchy are intentionally excluded.
    [[nodiscard]] bool RemoveSelectedSceneComponentUVE(EditorSceneComponentKindUVE kind);

    /// Replaces the selected primitive's complete authored appearance atomically. Primitive kind
    /// and bounded linear-RGB base color are both editable after creation; one changed valid call
    /// becomes one Primitive Appearance Undo/Redo transaction. Invalid, unchanged, multi-selected,
    /// protected-Play, or competing-gesture state returns false without mutation.
    [[nodiscard]] bool SetSelectedPrimitiveMeshUVE(const Scene::PrimitiveMeshComponentUVE& primitive);

    /// Moves the selected document entity by a finite world-space distance along one unit world
    /// axis. Parent world rotation and scale are converted back to a local position delta before
    /// applying the existing scene-graph transform path. Returns false without mutation if the
    /// entity, parent transform, axis, or distance is invalid.
    [[nodiscard]] bool TranslateSelectedAlongAxisUVE(EditorTransformAxisUVE axis, float worldDistance);

    /// Rotates the selected document entity around one finite world axis by radians. A parented
    /// entity receives the equivalent local quaternion delta through the current parent world
    /// rotation. Returns false without mutation for invalid state, axis, angle, transform, parent,
    /// or active editor gesture.
    [[nodiscard]] bool RotateSelectedAroundWorldAxisUVE(EditorTransformAxisUVE axis, float radians);

    /// Changes one positive authored local-scale component of the selected document entity by a
    /// finite additive delta. Returns false without mutation for invalid state, axis, delta, active
    /// gesture, or a proposed zero/negative/non-finite scale result.
    [[nodiscard]] bool ScaleSelectedAlongAxisUVE(EditorTransformAxisUVE axis, float localScaleDelta);

    /// Adds one finite local-scale offset to every authored local-scale component of the selected
    /// entity. The command rejects as a whole if any proposed component is non-finite or below the
    /// positive scale floor; it never clamps individual components or performs proportional scaling.
    [[nodiscard]] bool ScaleSelectedUniformlyUVE(float localScaleOffset);

    /// Begins one pointer-driven transform transaction on the selected entity, capturing the
    /// baseline to preview from and to restore on cancel. A drag is not a sequence of commands:
    /// every public transform command records history, so driving one from a drag would push an
    /// undo entry per mouse-move frame. Returns false, leaving any existing session untouched,
    /// for invalid editor state, multi-selection, or an entity with no transform.
    [[nodiscard]] bool BeginTransformGestureUVE(EditorToolSessionModeUVE mode);

    /// Applies the gesture's current value WITHOUT recording history. `totalAmount` is measured
    /// from where the drag began, not from the previous frame - a drag reports its total offset
    /// each frame, and treating it as an increment would compound into a runaway. The mode is the
    /// one captured at Begin, so a tool switch mid-drag cannot reinterpret the gesture.
    /// For Scale, EditorTransformAxisUVE::None means uniform.
    [[nodiscard]] bool PreviewTransformGestureUVE(EditorTransformAxisUVE axis, float totalAmount);

    /// The translate-gesture form that takes a full world-space delta rather than one axis, for a
    /// plane handle - a drag in the XY plane moves along two axes at once, which no single-axis
    /// call can express. Snapping quantises each component by the translate step, so a snapped
    /// plane drag lands on the same lattice an axis drag would. Rejected unless the gesture in
    /// flight is a Translate.
    [[nodiscard]] bool PreviewTranslateGestureUVE(const Math::Vector3UVE& totalWorldDelta);

    /// The Inspector's form of a gesture preview: the whole local transform as its Position,
    /// Rotation and Scale fields now read, applied as-is (no snapping - the author typed or
    /// dragged that exact number). Refused for a non-finite transform or with no gesture in flight.
    [[nodiscard]] bool PreviewTransformGestureValueUVE(const Scene::TransformComponentUVE& transform);

    /// Ends the gesture and records exactly ONE history entry, baseline to final. A gesture that
    /// never moved anything commits cleanly without an entry and without marking the scene dirty.
    [[nodiscard]] bool CommitTransformGestureUVE();

    /// Ends the gesture and restores the baseline. Returns false without restoring when the live
    /// transform no longer matches this gesture's last preview - something else moved the entity,
    /// and writing a stale baseline over it would silently discard that change
    /// (EditorToolSessionOutcomeUVE::ExternalTransformConflict).
    [[nodiscard]] bool CancelTransformGestureUVE();

    /// Replaces the snapping settings only when every increment is at least
    /// kMinimumTransformSnapStepUVE and at most its own maximum below, and no transform/navigation
    /// gesture is active. Returns false without mutation otherwise.
    [[nodiscard]] bool SetTransformSnappingSettingsUVE(const EditorTransformSnappingSettingsUVE& settings);
    [[nodiscard]] const EditorTransformSnappingSettingsUVE& GetTransformSnappingSettingsUVE() const noexcept;
    static constexpr float kMinimumTransformSnapStepUVE = 0.0001F;
    static constexpr float kMaximumTransformSnapTranslateStepUVE = 1000.0F;
    static constexpr float kMaximumTransformSnapRotateStepDegreesUVE = 360.0F;
    static constexpr float kMaximumTransformSnapScaleStepUVE = 100.0F;

    /// Every editor setting kept in the settings file, described (see editor_settings_uve.h).
    [[nodiscard]] const Config::SettingsRegistryUVE& GetSettingsRegistryUVE() const noexcept {
        return m_settingsRegistry;
    }
    /// The value editor setting `id` has right now; nothing for an id that is not an editor setting.
    [[nodiscard]] std::optional<Config::SettingValueUVE> GetEditorSettingUVE(std::string_view id) const;
    /// Applies `value` to editor setting `id` at once. Refused, changing nothing, for an unknown id
    /// or a value its descriptor does not allow. Stored in the settings file with the session.
    [[nodiscard]] bool SetEditorSettingUVE(std::string_view id, const Config::SettingValueUVE& value);
    /// The point the viewport camera orbits, reported by the host each frame; new objects are placed
    /// there when their placement preference says so. A non-finite point is ignored.
    void SetViewportCameraFocusUVE(const Math::Vector3UVE& focus) noexcept;

    /// Shows the Editor Preferences window, with the search field focused.
    void OpenEditorPreferencesUVE() noexcept;
    [[nodiscard]] bool IsEditorPreferencesOpenUVE() const noexcept { return m_preferencesWindow.visible; }
    /// Shows the Project Settings window, with the search field focused.
    void OpenProjectSettingsUVE() noexcept;
    [[nodiscard]] bool IsProjectSettingsOpenUVE() const noexcept { return m_projectSettingsWindow.visible; }
    /// Writes the project settings file if it has unsaved changes. Also happens when the Project
    /// Settings window closes and when the editor shuts down.
    [[nodiscard]] bool SaveProjectSettingsUVE();
    /// Every command the editor has, in the order the palette and the shortcuts window list them.
    [[nodiscard]] const std::vector<EditorCommandUVE>& GetEditorCommandsUVE() const noexcept;
    /// Runs command `id` when it is available now. False for an unknown or unavailable command.
    [[nodiscard]] bool RunEditorCommandUVE(std::string_view id);
    /// Sets shortcut `slot` (0 primary, 1 alternate) of command `id`; an empty shortcut removes it.
    /// Refused for an unknown command, a slot past 1, or a key a shortcut may not use.
    [[nodiscard]] bool SetEditorCommandShortcutUVE(std::string_view id, std::size_t slot,
                                                   const EditorShortcutUVE& shortcut);
    /// Shows the command palette, ready to type into.
    void OpenCommandPaletteUVE() noexcept;
    [[nodiscard]] bool IsCommandPaletteOpenUVE() const noexcept { return m_commandPalette.open; }
    /// Shows the Keyboard Shortcuts window.
    void OpenKeyboardShortcutsUVE() noexcept;

    /// Shows the Input Map window: the project's actions and what triggers them.
    void OpenInputMapUVE() noexcept;
    [[nodiscard]] bool IsInputMapOpenUVE() const noexcept { return m_inputMapWindow.visible; }
    /// Writes the input map file if it has unsaved changes. Also happens when the Input Map window
    /// closes and when the editor shuts down.
    [[nodiscard]] bool SaveInputMapUVE();

    /// Returns the derived world-space box for the active live collider-backed document entity.
    /// It never mutates selection, scene state, dirty state, or Undo/Redo history; unsafe or
    /// unsupported state returns std::nullopt.
    [[nodiscard]] std::optional<EditorSelectionBoundsUVE> TryGetSelectedBoundsUVE() const;

    /// Creates one root-level document entity with a TransformComponentUVE and the specialized
    /// component implied by `kind`, selects it, and marks the document dirty. Returns the invalid
    /// entity handle without mutation when the editor is not running or `kind` is unsupported.
    [[nodiscard]] Scene::EntityUVE CreateDocumentEntityUVE(EditorEntityKindUVE kind);

    /// Creates a user-facing object from the centralized SceneObject registry. Runtime ownership remains
    /// in core/physics/render/audio/scripting; this method only creates the authored scene façade.
    [[nodiscard]] Scene::EntityUVE CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE kind);

    /// Duplicates the selected live document entity and all descendants under the selected root's
    /// current parent, assigns the duplicate root a deterministic available name when it has name
    /// metadata, selects the fresh root, marks the scene dirty, and records one Undo/Redo entry.
    /// Returns the invalid handle without mutation for invalid state, a stale editor camera, an
    /// active viewport gesture, unsupported snapshot component data, or failed restoration.
    [[nodiscard]] Scene::EntityUVE DuplicateSelectedEntityUVE();

    /// Deletes the selected live document entity and every descendant after capturing a reversible
    /// in-memory snapshot. The still-live document parent becomes selected when present; otherwise
    /// selection is cleared. Returns false without mutation for the same invalid/safety states as
    /// DuplicateSelectedEntityUVE().
    [[nodiscard]] bool DeleteSelectedEntityUVE();

    /// Reparents the selected document subtree under newParent, or makes it a document root when
    /// newParent is invalid. Keep Local retains authored local Transform; optional Keep World
    /// preserves only a validated shear-safe compatible world TRS. One successful move is recorded
    /// as an Undo/Redo operation. Returns false without mutation for unsafe state or solve input.
    [[nodiscard]] bool ReparentSelectedEntityUVE(Scene::EntityUVE newParent);
    [[nodiscard]] bool SetReparentTransformModeUVE(EditorReparentTransformModeUVE mode);
    [[nodiscard]] EditorReparentTransformModeUVE GetReparentTransformModeUVE() const noexcept;

    /// Replays the most recent supported editor mutation in reverse. It is safe and returns false
    /// when the editor is not running, history is empty, or a target became stale externally.
    [[nodiscard]] bool UndoUVE();
    /// Reapplies the most recently undone supported editor mutation. It follows UndoUVE's lifecycle
    /// and stale-target safety rules and never records another history entry while replaying.
    [[nodiscard]] bool RedoUVE();
    [[nodiscard]] bool CanUndoUVE() const noexcept;
    [[nodiscard]] bool CanRedoUVE() const noexcept;

    [[nodiscard]] std::vector<Scene::EntityUVE> GetDocumentRootsUVE();

    /// The objects an entity reference may name, in Outliner order: every document object with a
    /// transform, minus the selection itself (a reference an object makes to itself resolves to
    /// nothing an author could act on). Shared by the single-reference picker and the
    /// reference-list drawer, so both offer exactly the same choices in the same order.
    [[nodiscard]] std::vector<Scene::EntityUVE> GetEntityReferenceCandidatesUVE();

    /// The document's single scene-root entity (the top of the hierarchy), or invalid when the
    /// document somehow has none. Structural only by design: name + identity transform.
    [[nodiscard]] Scene::EntityUVE GetDocumentSceneRootUVE();
    /// The Outliner's Viewport: the folder at the top of the level that every other folder lives in.
    /// Invalid while an entity or the Retarget preview stands in for the level.
    [[nodiscard]] Scene::EntityUVE GetDocumentViewportUVE();
    [[nodiscard]] EditorStateUVE GetStateUVE() const noexcept;
    [[nodiscard]] Scene::EntityUVE GetSelectedEntityUVE() const noexcept;

    [[nodiscard]] Scene::EntityUVE GetPreviewCameraUVE() const noexcept;
    void SetPreviewCameraUVE(Scene::EntityUVE entity);
    void ClearPreviewCameraUVE() noexcept;
    /// Returns editor-only 2D canvas state for screen-space authoring. It is not scene data.
    [[nodiscard]] Editor2DCanvasStateUVE Get2DCanvasStateUVE() const noexcept;

    /// The editor-viewport bookmark slots (Unreal-editor Ctrl+digit/digit convention). All of it
    /// is transient session state on EditorUVE, never document data and never dirtying the scene:
    /// Set rejects an out-of-range slot or a non-finite/badly-formed pose, Get answers no value
    /// for an empty slot, and Clear empties one slot. The app host owns applying a pose to its
    /// real OrbitCamera - this class deliberately has no camera type in its interface.
    [[nodiscard]] bool SetViewportBookmarkUVE(std::size_t slot,
                                              const EditorViewportBookmarkUVE& bookmark) noexcept;
    [[nodiscard]] std::optional<EditorViewportBookmarkUVE> GetViewportBookmarkUVE(
        std::size_t slot) const noexcept;
    [[nodiscard]] bool ClearViewportBookmarkUVE(std::size_t slot) noexcept;

    /// Composes the fly-to-marker bookmark for an entity: the entity must carry a valid, enabled
    /// Marker3DComponentUVE and a world transform; the marker's authored offset+rotation are
    /// composed under the object's pose (Scene::ComposeMarker3DPoseUVE), the camera eye is placed at
    /// the marker's position looking along its composed -Z (the camera convention), and the orbit
    /// bookmark states that same view as target/yaw/pitch/distance so the host camera applies it
    /// verbatim. Any missing piece answers no value - fail-closed, the caller simply does not
    /// move the camera. This is Marker3D's live consumer: a marker is a scene-persistent named
    /// viewpoint the viewport can fly into, not the inert gizmo it stays in Godot.
    [[nodiscard]] std::optional<EditorViewportBookmarkUVE> ComposeMarker3DFocusBookmarkUVE(
        Scene::EntityUVE entity) const;

    /// The orbit pivot for a plain focus-on-entity (no marker): the entity's world position, or
    /// no value when it has no world transform. Distance/yaw/pitch intentionally stay whatever
    /// the camera already holds - bounds-aware framing needs renderer-side bounds this class does
    /// not own today; real, separate follow-up, not silently faked.
    [[nodiscard]] std::optional<Math::Vector3UVE> ResolveEntityFocusTargetUVE(
        Scene::EntityUVE entity) const;

    /// Inverts the editor OrbitCamera's forward offset formula (eye = target + offset(yaw,pitch) *
    /// distance) for a requested eye/look direction: pitch = asin(-forward.y) clamped to the same
    /// +/-1.5533 range OrbitCameraSettings itself enforces (a straight-up/down look snaps to the
    /// pole with yaw 0 by convention), yaw = atan2(-forward.z, -forward.x), target = eye +
    /// forward * distance. A degenerate (near-zero or non-finite) forward answers no value.
    /// Standing alone so both ComposeMarker3DFocusBookmarkUVE above and any future caller pin the
    /// same math; Test/Editor measures the round trip (offset formula then this inverse) back to
    /// the original eye below 1e-4.
    [[nodiscard]] static std::optional<EditorViewportBookmarkUVE> ResolveOrbitBookmarkFromLookUVE(
        const Math::Vector3UVE& eye, const Math::Vector3UVE& forward, float distance) noexcept;
    /// Validates and updates the editor-only 2D canvas zoom without changing scene state/history.
    [[nodiscard]] bool Set2DCanvasZoomUVE(float zoom) noexcept;
    /// Restores the editor-only 2D canvas to its centered loading-screen design view.
    void Reset2DCanvasViewUVE() noexcept;
    [[nodiscard]] bool IsSceneDirtyUVE() const noexcept;
    /// Read-only transform-tool lifecycle diagnostics. These values expose editor-session evidence
    /// only; they neither alter input routing nor claim any ECS mutation succeeded.
    [[nodiscard]] EditorToolSessionPhaseUVE GetToolSessionPhaseUVE() const noexcept;
    [[nodiscard]] EditorToolSessionOutcomeUVE GetLastToolSessionOutcomeUVE() const noexcept;
    /// Returns the selected registered asset record when the selected project-file entry is a currently
    /// correlated file. Directories and unregistered files return std::nullopt.
    [[nodiscard]] const std::optional<Asset::AssetRecordUVE>& GetSelectedAssetUVE() const noexcept;
    /// Returns the selected cached project-file entry, including directories and unregistered files.
    [[nodiscard]] const std::optional<Asset::ProjectFileEntryUVE>& GetSelectedProjectFileUVE() const noexcept;
    [[nodiscard]] const std::string& GetAssetFilterUVE() const noexcept;
    [[nodiscard]] const std::filesystem::path& GetActiveScenePathUVE() const noexcept;
    void SetActiveScenePathUVE(std::filesystem::path path);
    /// Opens `entity`'s script in the Scripting workspace (its `.uvs` file in the text editor).
    /// False when `entity` has no Script component or its script is not a `.uvs` file - the
    /// viewport's entity toolbar uses that to decide whether to offer "Scripting" at all.
    [[nodiscard]] bool OpenScriptGraphForEntityUVE(Scene::EntityUVE entity);
    /// "New UVScript" on the Scripting slot. Writes `scripts/<name>.uvs` (a free name) whose header
    /// names the object and its kind, points the Script at it as one undoable edit, and opens it in
    /// the script text editor. Refuses unless authoring is allowed, exactly one document entity is
    /// selected, it carries a Script component and that Script is still empty.
    [[nodiscard]] bool CreateUVScriptForSelectedEntityUVE();

    /// A `.uvs` file open in the Scripting workspace's text editor.
    struct UVScriptDocumentUVE final {
        std::string path;
        /// The object the script is checked against: its components decide which properties exist.
        Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
        std::string text;
        std::string savedText;
        /// Every problem the compiler found in `text`, in source order; empty when it compiles.
        std::vector<UVScript::DiagnosticUVE> diagnostics;

        [[nodiscard]] bool IsDirtyUVE() const noexcept { return text != savedText; }
    };
    /// Opens `entity`'s `.uvs` script in the text editor and switches to the Scripting workspace.
    /// A missing file opens empty; saving creates it. False when the entity has no `.uvs` script.
    [[nodiscard]] bool OpenUVScriptForEntityUVE(Scene::EntityUVE entity);
    [[nodiscard]] const std::optional<UVScriptDocumentUVE>& GetOpenUVScriptUVE() const noexcept;
    /// Replaces the open script's text and re-checks it. No effect when nothing is open.
    void SetOpenUVScriptTextUVE(std::string text);
    /// Writes the open script to its file. The running game picks the change up on its own.
    [[nodiscard]] bool SaveOpenUVScriptUVE();
    /// Closes the text editor (unsaved text is dropped) and returns to the scene.
    void CloseOpenUVScriptUVE();

    /// One `export` field of the selected object's `.uvs` script, as the Inspector shows it.
    struct ScriptExportRowUVE final {
        std::string name;
        UVScript::TypeUVE type;
        /// The script's own initial value, as UVScript text.
        std::string defaultText;
        /// What this object runs with: its stored value when it has a valid one, else the default.
        std::string valueText;
        bool overridden = false;
    };
    /// The selected object's exported fields, in declaration order. Empty when the object has no
    /// `.uvs` script, the file cannot be read, or it does not compile.
    [[nodiscard]] std::vector<ScriptExportRowUVE> GetSelectedScriptExportsUVE();
    /// Sets the selected object's value for export `name` from UVScript text, or - with no text -
    /// goes back to the script's default. One undo step. Refuses a name the script does not
    /// export and text that is not a value of the field's type.
    [[nodiscard]] bool SetSelectedScriptExportUVE(const std::string& name, std::optional<std::string> text);
    /// "Quick Load" and "Load": points the selected entity's Script at an existing script asset, as
    /// one undoable edit. An empty path clears the slot. Any other path must pass
    /// DescribeScriptAssetProblemUVE.
    [[nodiscard]] bool AssignScriptToSelectedEntityUVE(const std::string& path);
    /// Why `path` cannot be assigned as a script, in words fit to show beside the field; empty when
    /// it can. A path must be project-relative and name an existing `.uvs` file.
    [[nodiscard]] std::string DescribeScriptAssetProblemUVE(const std::string& path) const;
    /// The script assets Quick Load offers, sorted: every one the document already uses, plus every
    /// `.uvs` file in the project's scripts folder.
    [[nodiscard]] std::vector<std::string> GetKnownScriptAssetPathsUVE() const;
    /// Metadata on the selected object. Each writes the whole entry list once through the metadata
    /// property path, so every add, edit, rename, retype or removal is exactly one undo entry.
    /// New and renamed keys must pass ValidateObjectMetadataKeyUVE.
    [[nodiscard]] bool AddSelectedObjectMetadataUVE(const std::string& key, const Core::VariantUVE& value);
    [[nodiscard]] bool SetSelectedObjectMetadataValueUVE(const std::string& key, const Core::VariantUVE& value);
    /// The same edit shown at once without history, for a value being dragged: the drag becomes
    /// one undo step when it ends (see PreviewSelectedComponentPropertyUVE).
    [[nodiscard]] bool PreviewSelectedObjectMetadataValueUVE(const std::string& key, const Core::VariantUVE& value);
    [[nodiscard]] bool RenameSelectedObjectMetadataUVE(const std::string& key, const std::string& newKey);
    /// Converts the value to `type`. Refuses a conversion that would lose data unless `allowLoss`.
    [[nodiscard]] bool ChangeSelectedObjectMetadataTypeUVE(const std::string& key, Core::VariantTypeUVE type,
                                                         bool allowLoss);
    [[nodiscard]] bool RemoveSelectedObjectMetadataUVE(const std::string& key);
    /// Arms the entity context toolbar (see ViewportOverlayStateUVE) at the given screen pixel for
    /// `entity`. The caller (main.cpp, which owns viewport picking and the camera the pixel was
    /// projected with) must call this before RenderOverlayUVE runs the same frame.
    void SetEntityContextToolbarAnchorUVE(Scene::EntityUVE entity, float pixelX, float pixelY);
    /// Closes the entity context toolbar (a right-click that missed every entity).
    void ClearEntityContextToolbarUVE() noexcept;

    /// Inspector folds (sections, nested components, sub-groups) remembered by key across
    /// selections and sessions. A key never set answers `defaultOpen`. At most
    /// kMaxRememberedInspectorFoldsUVE are kept; beyond that new choices are not remembered.
    /// The colour picker's remembered choices - the Advanced section's state, the saved palette and
    /// the recent colours - saved with the session. The setter keeps only finite colours, clamped
    /// to 0..1, and trims each list to its cap (kMaxSavedColorsUVE, kMaxRecentColorsUVE).
    [[nodiscard]] const ColorPickerPreferencesUVE& GetColorPickerPreferencesUVE() const noexcept {
        return m_colorPickerPreferences;
    }
    void SetColorPickerPreferencesUVE(ColorPickerPreferencesUVE preferences);

    void SetInspectorFoldOpenUVE(const std::string& key, bool open);
    [[nodiscard]] bool IsInspectorFoldOpenUVE(const std::string& key, bool defaultOpen) const;
    static constexpr std::size_t kMaxRememberedInspectorFoldsUVE = 256U;

    /// Viewport projection and named views. Choosing a projection explicitly sticks until changed.
    /// Moving to a named view switches to orthographic *automatically*, and that automatic
    /// orthographic ends - back to perspective - as soon as the camera is orbited out of the view,
    /// which the host reports with NotifyViewportOrbitedUVE().
    void SetViewportOrthographicUVE(bool orthographic) noexcept;
    void RequestViewportViewUVE(ViewportViewUVE view) noexcept;
    void NotifyViewportOrbitedUVE() noexcept;
    [[nodiscard]] bool IsViewportOrthographicUVE() const noexcept { return m_viewportOverlayState.orthographic; }
    [[nodiscard]] ViewportViewUVE GetViewportViewUVE() const noexcept { return m_viewportOverlayState.view; }
    [[nodiscard]] std::uint32_t GetViewportViewRequestSerialUVE() const noexcept {
        return m_viewportOverlayState.viewRequestSerial;
    }
    [[nodiscard]] static const char* GetViewportViewNameUVE(ViewportViewUVE view) noexcept;
    /// The keypad layout for named views: 7 Top, 1 Front, 3 Right, and with `opposite` (Ctrl) the
    /// view from the other side - Bottom, Back, Left. Any other digit is not a view.
    [[nodiscard]] static std::optional<ViewportViewUVE> GetViewportViewForKeypadDigitUVE(int digit,
                                                                                       bool opposite) noexcept;
    /// The shortcut text shown next to a view or the projection switch in the view menu.
    [[nodiscard]] static const char* GetViewportViewShortcutUVE(ViewportViewUVE view) noexcept;

    /// The viewport grid: shown or hidden, and its opacity (0.1..1 - never fully invisible, which
    /// would be a hidden grid under another name). Both are editor preferences, saved with the
    /// session. An opacity outside the range, or not finite, is refused and nothing changes.
    [[nodiscard]] bool SetViewportGridUVE(bool visible, float opacity);
    [[nodiscard]] bool IsViewportGridVisibleUVE() const noexcept { return m_viewportOverlayState.gridVisible; }
    [[nodiscard]] float GetViewportGridOpacityUVE() const noexcept { return m_viewportOverlayState.gridOpacity; }
    static constexpr float kMinimumViewportGridOpacityUVE = 0.1F;
    /// The smallest square the grid draws, in world units. Zooming out still steps the grid up in
    /// tens from here; zooming in never draws finer than it. A preference, saved with the session.
    /// A size outside kMinimum..kMaximumViewportGridCellSizeUVE, or not finite, is refused.
    [[nodiscard]] bool SetViewportGridCellSizeUVE(float cellSize);
    [[nodiscard]] float GetViewportGridCellSizeUVE() const noexcept { return m_viewportOverlayState.gridCellSize; }
    static constexpr float kMinimumViewportGridCellSizeUVE = 0.01F;
    static constexpr float kMaximumViewportGridCellSizeUVE = 1000.0F;

    /// The selection outline: shown or hidden, its colour, and its thickness in pixels. Editor
    /// preferences, saved with the session. A colour channel outside 0..1, a thickness outside
    /// kMinimum..kMaximumSelectionOutlineThicknessUVE, or anything not finite is refused whole.
    [[nodiscard]] bool SetViewportSelectionOutlineUVE(bool visible, ViewportAxisColorUVE color, float thickness);
    [[nodiscard]] bool IsViewportSelectionOutlineVisibleUVE() const noexcept {
        return m_viewportOverlayState.selectionOutlineVisible;
    }
    [[nodiscard]] ViewportAxisColorUVE GetViewportSelectionOutlineColorUVE() const noexcept {
        return m_viewportOverlayState.selectionOutlineColor;
    }
    [[nodiscard]] float GetViewportSelectionOutlineThicknessUVE() const noexcept {
        return m_viewportOverlayState.selectionOutlineThickness;
    }
    static constexpr float kMinimumSelectionOutlineThicknessUVE = 1.0F;
    static constexpr float kMaximumSelectionOutlineThicknessUVE = 6.0F;

    /// The viewport's X/Y/Z axis colours, which the menu bar offers a picker for and the host
    /// pushes into the real renderer each frame (see ViewportOverlayStateUVE::axisColorX).
    ///
    /// The host calls the setter once at startup to seed the viewport's own default palette -
    /// this module cannot name those defaults itself - and thereafter whenever a persisted
    /// choice is loaded. A channel outside 0..1, or not finite, is refused and nothing changes.
    [[nodiscard]] bool SetViewportAxisColorsUVE(ViewportAxisColorUVE x, ViewportAxisColorUVE y,
                                                ViewportAxisColorUVE z);
    [[nodiscard]] bool AreViewportAxisColorsSetUVE() const noexcept;
    [[nodiscard]] ViewportAxisColorUVE GetViewportAxisColorUVE(int axisIndex) const;
    /// Forgets the author's choice, which makes the host re-seed its own default palette on the
    /// next frame. Clearing rather than writing default values keeps those hues in the one module
    /// that owns them instead of copying them into this one.
    void ResetViewportAxisColorsUVE() noexcept;

    /// Releases editor-private UI resources and destroys the editor camera while the services are
    /// still alive. Idempotent after the first successful shutdown.
    void ShutdownUVE();

private:
    enum class EditorLayoutPresetUVE : std::uint8_t {
        Default,
        FocusViewport,
        ContentReview,
    };

    struct EditorSelectionPathUVE final {
        std::size_t rootIndex = 0U;
        std::vector<std::size_t> childIndices;
    };

    struct EditorSelectionSnapshotUVE final {
        std::vector<Scene::EntityUVE> entities;
        Scene::EntityUVE activeEntity = Scene::kInvalidEntityUVE;
    };

    struct EditorSelectionPathsUVE final {
        std::vector<EditorSelectionPathUVE> entityPaths;
        std::optional<EditorSelectionPathUVE> activePath;
    };


    struct EntityEditSessionUVE final {
        std::filesystem::path assetPath;
        Asset::AssetGuidUVE guid = Asset::kInvalidAssetGuidUVE;
        Scene::SceneSnapshotUVE sceneSnapshot;
        bool sceneWasEmpty = false;
        bool sceneDirtyBefore = false;
        EditorSelectionPathsUVE selectionBefore;
        /// Set once the entity was written, so closing refreshes its instances in the scene.
        bool savedOnce = false;
        /// The window's X was pressed with unsaved changes: ask before closing.
        bool confirmClose = false;
        /// The simulation is held while an entity is open (a character would otherwise fall and
        /// be saved where it landed); this is what to go back to.
        std::optional<Core::SimulationExecutionModeUVE> simulationBefore;
        EntityEditorTabUVE tab = EntityEditorTabUVE::Viewport;
        std::vector<EntityCompileProblemUVE> problems;
        bool compiled = false;
        /// The window selects `tab` on its next frame (GoTo from a problem or a signal).
        bool forceTab = false;
        EntityEditorDockTabUVE dockTab = EntityEditorDockTabUVE::Content;
        /// The object the dock last followed: selecting an AnimationSequencer or AnimationGraph opens its
        /// tab once, and the tab stays the user's choice until the selection moves again.
        Scene::EntityUVE dockFollowed = Scene::kInvalidEntityUVE;
        float dockHeight = 220.0F;
    };
    /// What the Retarget window's viewport shows while it is open: the scene is put aside (as the
    /// Entity Editor does) and a preview world stands in its place. Closing brings the scene back.
    struct RetargetPreviewUVE final {
        Scene::SceneSnapshotUVE sceneSnapshot;
        bool sceneWasEmpty = false;
        bool sceneDirtyBefore = false;
        EditorSelectionPathsUVE selectionBefore;
        std::optional<Core::SimulationExecutionModeUVE> simulationBefore;
        /// Everything of the preview stands under it.
        Scene::EntityUVE frameRoot = Scene::kInvalidEntityUVE;
        /// The humanoid and the character stand under it; the viewport frames it.
        Scene::EntityUVE figures = Scene::kInvalidEntityUVE;
        Scene::EntityUVE sourceSkeleton = Scene::kInvalidEntityUVE;
        Scene::EntityUVE targetSkeleton = Scene::kInvalidEntityUVE;
        /// The joint colour of each humanoid bone, by reference index.
        std::vector<std::array<float, 3>> sourceColours;
        /// The same colours on the character's bones, by their own names.
        std::unordered_map<std::string, std::array<float, 3>> targetColours;
        /// The character the preview was built for; a change rebuilds it.
        std::filesystem::path builtFor;
        bool frameRequested = false;
    };
    /// Puts the scene aside for the preview. False (nothing changed) when the editor cannot right now.
    [[nodiscard]] bool BeginRetargetPreviewUVE();
    /// Brings the scene back. False, staying in the preview, when it cannot be restored.
    [[nodiscard]] bool EndRetargetPreviewUVE();
    /// Builds the world: a sun, the humanoid and the character side by side (the studio view draws the rest).
    void RebuildRetargetPreviewUVE(const RetargetPlanUVE& plan, const std::filesystem::path& modelFile);
    void DrawRetargetPreviewUVE();
    void DrawRetargetPlaceholderUVE();
    /// The entity's objects in tree order (root first). Empty when no entity is open.
    [[nodiscard]] std::vector<Scene::EntityUVE> CollectEntityEditorObjectsUVE();
    /// Instantiates the entity asset as the document's only content (below the scene root).
    [[nodiscard]] Scene::EntityUVE LoadEntityIntoDocumentUVE(Asset::AssetGuidUVE guid);
    /// The Entity Editor's own window, and what the main window shows meanwhile.
    void DrawEntityEditorWindowUVE();
    void DrawRetargetWindowUVE();
    void FinishRetargetJobUVE(RetargetWindowStateUVE& window, Retarget::RetargetFilesResultUVE result);
    /// Gives every Skeleton3D bound to `modelFile`'s source the bones of the conformed model; the
    /// number changed.
    std::size_t RefreshSkeletonsForRetargetedModelUVE(const std::filesystem::path& modelFile);
    /// The character file behind a Retarget target: the `.uvmodel` itself, or a model source's imported model.
    [[nodiscard]] std::filesystem::path ResolveRetargetModelFileUVE(const std::filesystem::path& target) const;
    void DrawEntityEditorPlaceholderUVE();
    /// The Entity Editor's middle: the Viewport / Scripting / Events tabs and Compile's problems.
    void DrawEntityEditorMiddleUVE(EntityEditSessionUVE& session);
    void DrawEntityEditorScriptingTabUVE();
    void DrawEntityEditorDockUVE(EntityEditSessionUVE& session);
    void DrawEntityEditorSignalsTabUVE();
    /// "Not compiled", "Compiled" or "3 problems", in the toolbar.
    static void DrawEntityCompileBadgeUVE(const EntityEditSessionUVE& session);

    struct PlayModeSessionUVE final {
        Scene::SceneSnapshotUVE documentSnapshot;
        bool capturedEmptyDocument = false;
        bool dirtyBefore = false;
        EditorSelectionPathsUVE selectionBefore;
    };

    /// Play-entry spawn semantics. Called once by EnterPlayModeUVE() after the document snapshot
    /// is captured and the simulation is running: resolves the one spawn point that fires this
    /// session (deterministic content order over enabled+valid SpawnPoint3D objects), moves the
    /// player entity (the one carrying a CharacterControllerComponentUVE) to the composed spawn
    /// pose via the sweep's exact inverse, and disables the point when it was authored
    /// `oneShot = true`. Every mutation sits inside the snapshot, so StopPlayModeUVE() hands
    /// back the authored player pose and every spent one-shot. Returns false when there is
    /// nothing to do - no player, no enabled spawn point, or a degenerate pose/ancestry the
    /// resolvers refuse - and a false return never fails play entry.
    [[nodiscard]] bool ApplyPlayEntrySpawnUVE();

    /// Editor-only workspace labels. They do not alter document data, simulation state, or history.
    enum class EditorWorkspaceUVE {
        Library,
        Asset,
        Scripting,
        Debug,
        Plugin,
        Game,
    };

    /// Selects the visible content inside the fixed right-side editor panel.
    enum class EditorRightPanelTabUVE {
        Inspector,
        Import,
        Events,
    };

    /// Selects one docked lower-workspace panel. FileSystem is the safe default and keeps the
    /// former Assets database view visible without introducing an AI tooling implementation.
    enum class EditorBottomDockUVE {
        Debugger,
        Animator,
        AIToolbar,
        FileSystem,
        /// The developer console: output and a command line. Appended so persisted values keep meaning.
        Console,
    };

    /// Session-only Content Browser focus. This filters copied ProjectFileIndexUVE entries and
    /// never requests I/O, loads an asset, or changes the AssetDatabaseUVE registry.
    enum class ContentBrowserTypeFocusUVE {
        All,
        Folders,
        Scene,
        Prefab,
        Bundle,
        Mesh,
        Texture,
        Shader,
        Material,
        Save,
        Registered,
        OtherFiles,
    };

    /// A file's primary presentation type. Registry correlation is deliberately a separate badge:
    /// one registered `.uvmodel` row therefore remains Mesh + Registered, never an ambiguous tag.
    enum class ContentBrowserItemTypeUVE {
        Folder,
        Scene,
        Prefab,
        /// A `.uventity`: a prefab envelope made from the Content "+ Add" catalogue. It opens as
        /// a tree (Open Tree) rather than as a plain prefab.
        Entity,
        Bundle,
        Mesh,
        /// A model source with a skeleton (mesh plus bones), as opposed to a static Mesh.
        Model,
        Texture,
        Shader,
        Material,
        Save,
        /// Motion without a mesh: a model source that holds only a skeleton and its animation.
        Animation,
        Script,
        Audio,
        Font,
        File,
    };

    struct TransformHistoryEntryUVE final {
        Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
        Scene::TransformComponentUVE before{};
        Scene::TransformComponentUVE after{};
        EditorSelectionSnapshotUVE selectionBefore;
        EditorSelectionSnapshotUVE selectionAfter;
        bool dirtyBefore = false;
        bool dirtyAfter = false;
    };

    struct NameHistoryEntryUVE final {
        Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
        std::optional<std::string> beforeName;
        std::optional<std::string> afterName;
        EditorSelectionSnapshotUVE selectionBefore;
        EditorSelectionSnapshotUVE selectionAfter;
        bool dirtyBefore = false;
        bool dirtyAfter = false;
    };

    struct PrimitiveAppearanceHistoryEntryUVE final {
        Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
        Scene::PrimitiveMeshComponentUVE before{};
        Scene::PrimitiveMeshComponentUVE after{};
        EditorSelectionSnapshotUVE selectionBefore;
        EditorSelectionSnapshotUVE selectionAfter;
        bool dirtyBefore = false;
        bool dirtyAfter = false;
    };

    struct SceneComponentHistoryEntryUVE final {
        Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
        EditorSceneComponentKindUVE kind = EditorSceneComponentKindUVE::Camera;
        std::optional<EditorSceneComponentValueUVE> before;
        std::optional<EditorSceneComponentValueUVE> after;
        EditorSelectionSnapshotUVE selectionBefore;
        EditorSelectionSnapshotUVE selectionAfter;
        bool dirtyBefore = false;
        bool dirtyAfter = false;
    };

    /// One authored write to one property of one component, recorded without naming that
    /// component's type. `metadata` points into the process-wide component metadata registry,
    /// which is built once and never mutated, so the pointer stays valid for the entry's life.
    ///
    /// The before/after values are type-erased clones rather than a variant of every component
    /// type, which is what makes this entry independent of how many component types exist.
    /// It is move-only for that reason, matching how history entries are already handled
    /// everywhere: they are moved onto the stacks and moved back off, never copied.
    struct ComponentPropertyHistoryEntryUVE final {
        Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
        const Core::TypeMetadataEntryUVE* metadata = nullptr;
        Core::TypeInstanceUVE before;
        Core::TypeInstanceUVE after;
        EditorSelectionSnapshotUVE selectionBefore;
        EditorSelectionSnapshotUVE selectionAfter;
        bool dirtyBefore = false;
        bool dirtyAfter = false;
    };

    struct CreationHistoryEntryUVE final {
        EditorEntityKindUVE kind = EditorEntityKindUVE::Empty;
        std::string name;
        Scene::EntityUVE activeEntity = Scene::kInvalidEntityUVE;
        EditorSelectionSnapshotUVE selectionBefore;
        EditorSelectionSnapshotUVE selectionAfter;
        bool dirtyBefore = false;
        bool dirtyAfter = false;
    };

    /// A centralized scene object is restored from one complete authored snapshot so compound object
    /// creation (for example Character3D plus Collider and kinematic Rigid3D) is one history unit.
    struct SceneObjectCreationHistoryEntryUVE final {
        Scene::SceneSnapshotUVE snapshot;
        Scene::Objects::SceneObjectKindUVE kind = Scene::Objects::SceneObjectKindUVE::Object3D;
        Scene::EntityUVE activeEntity = Scene::kInvalidEntityUVE;
        EditorSelectionSnapshotUVE selectionBefore;
        EditorSelectionSnapshotUVE selectionAfter;
        bool dirtyBefore = false;
        bool dirtyAfter = false;
        /// The parent the object was created under; Redo restores the subtree back under it
        /// (falling back to the scene root) instead of dropping it to document top level.
        Scene::EntityUVE createdUnderParent = Scene::kInvalidEntityUVE;
    };

    /// A duplicated subtree is restored from a scene-envelope snapshot instead of relying on stale
    /// ECS handles. `activeEntity` is invalid after Undo and becomes a fresh root after Redo.
    struct DuplicationHistoryEntryUVE final {
        Scene::SceneSnapshotUVE snapshot;
        Scene::EntityUVE originalParent = Scene::kInvalidEntityUVE;
        Scene::EntityUVE activeEntity = Scene::kInvalidEntityUVE;
        std::optional<std::string> duplicateRootName;
        EditorSelectionSnapshotUVE selectionBefore;
        EditorSelectionSnapshotUVE selectionAfter;
        bool dirtyBefore = false;
        bool dirtyAfter = false;
        /// Where among its siblings the copy stands: just below the object it copies.
        std::size_t siblingIndex = 0U;
    };

    /// A deleted subtree is restored under its original parent with fresh handles on Undo.
    /// `activeEntity` begins as the deleted root's stale handle and changes to the restored root.
    struct DeletionHistoryEntryUVE final {
        Scene::SceneSnapshotUVE snapshot;
        Scene::EntityUVE originalParent = Scene::kInvalidEntityUVE;
        Scene::EntityUVE activeEntity = Scene::kInvalidEntityUVE;
        EditorSelectionSnapshotUVE selectionBefore;
        EditorSelectionSnapshotUVE selectionAfter;
        bool dirtyBefore = false;
        bool dirtyAfter = false;
        /// Where among its siblings the deleted root stood, so Undo puts it back there.
        std::size_t siblingIndex = 0U;
    };

    /// A hierarchy move restores its parent, place among its siblings and authored local
    /// Transform atomically on replay. A move up or down is one with the same parent.
    struct ReparentHistoryEntryUVE final {
        Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
        Scene::EntityUVE parentBefore = Scene::kInvalidEntityUVE;
        Scene::EntityUVE parentAfter = Scene::kInvalidEntityUVE;
        Scene::TransformComponentUVE localTransformBefore{};
        Scene::TransformComponentUVE localTransformAfter{};
        EditorSelectionSnapshotUVE selectionBefore;
        EditorSelectionSnapshotUVE selectionAfter;
        bool dirtyBefore = false;
        bool dirtyAfter = false;
        std::size_t siblingIndexBefore = 0U;
        std::size_t siblingIndexAfter = 0U;
    };

    using HistoryEntryUVE =
        std::variant<TransformHistoryEntryUVE, NameHistoryEntryUVE, PrimitiveAppearanceHistoryEntryUVE,
                     SceneComponentHistoryEntryUVE, ComponentPropertyHistoryEntryUVE,
                     CreationHistoryEntryUVE, SceneObjectCreationHistoryEntryUVE,
                     DuplicationHistoryEntryUVE,
                     DeletionHistoryEntryUVE,
                     ReparentHistoryEntryUVE>;

    /// Writes `text` to a project file: through the VFS when a mount covers `path`, else at `path`
    /// itself relative to the working directory.
    /// Written to a temporary and renamed, so a failed write never leaves a half-written file.
    [[nodiscard]] bool WriteProjectTextFileUVE(const std::filesystem::path& path, std::string_view text);
    /// Reads a project file by the same rule as WriteProjectTextFileUVE.
    [[nodiscard]] std::optional<std::string> ReadProjectTextFileUVE(const std::filesystem::path& path) const;
    [[nodiscard]] bool IsDocumentEntityUVE(Scene::EntityUVE entity) const noexcept;
    [[nodiscard]] bool HasSceneGraphObjectUVE(Scene::EntityUVE entity) const noexcept;
    /// True for any live document entity in the hierarchy, spatial or not. This, not
    /// HasSceneGraphObjectUVE, is what a parent needs: a pure Object such as the scene root holds
    /// children without having a transform of its own.
    [[nodiscard]] bool IsReparentableObjectUVE(Scene::EntityUVE entity) const noexcept;
    [[nodiscard]] bool IsHierarchyObjectUVE(Scene::EntityUVE entity) const noexcept;
    /// The world pose a child of `parent` composes its local transform from, by the rule
    /// SceneGraphUVE::UpdateUVE applies: identity for no parent and for a parent with no transform
    /// (a pure Object starts its children's transform chains), otherwise the parent's world transform.
    /// Null when `parent` is not a live document entity.
    [[nodiscard]] std::optional<Scene::WorldTransformComponentUVE> TryGetComposingParentWorldUVE(
        Scene::EntityUVE parent) const;
    [[nodiscard]] bool IsTransformFiniteUVE(const Scene::TransformComponentUVE& transform) const noexcept;
    [[nodiscard]] bool IsEntityNameValidUVE(std::string_view name) const noexcept;
    [[nodiscard]] std::string GetEntityDisplayLabelUVE(Scene::EntityUVE entity) const;
    [[nodiscard]] std::string GetDefaultEntityNameUVE(EditorEntityKindUVE kind) const;
    [[nodiscard]] std::string MakeUniqueDocumentEntityNameUVE(std::string_view baseName) const;
    [[nodiscard]] bool IsFiniteVectorUVE(const Math::Vector3UVE& vector) const noexcept;
    [[nodiscard]] bool IsQuaternionFiniteUVE(const Math::QuaternionUVE& quaternion) const noexcept;
    [[nodiscard]] bool AreTransformSnappingSettingsValidUVE(
        const EditorTransformSnappingSettingsUVE& settings) const noexcept;
    [[nodiscard]] float SnapScalarUVE(float value, float increment) const noexcept;
    [[nodiscard]] Math::Vector3UVE GetAxisVectorUVE(EditorTransformAxisUVE axis) const noexcept;
    [[nodiscard]] bool ComputeLocalDeltaForWorldDeltaUVE(Scene::EntityUVE entity,
                                                           const Math::Vector3UVE& worldDelta,
                                                           Math::Vector3UVE& outLocalDelta) const;
    [[nodiscard]] bool ComputeLocalRotationForWorldAxisUVE(Scene::EntityUVE entity,
                                                            const Math::QuaternionUVE& initialLocalRotation,
                                                            const Math::Vector3UVE& worldAxis, float radians,
                                                            Math::QuaternionUVE& outLocalRotation) const;
    /// The transform `source` becomes after one axis operation, including snapping and the
    /// world-to-local conversion. Shared by the four public axis commands - which pass the LIVE
    /// transform, making them incremental - and by the gesture preview path, which passes the
    /// gesture BASELINE, making it absolute. One copy of the maths, so the two can never drift.
    /// For Scale, EditorTransformAxisUVE::None means uniform; Translate and Rotate reject it.
    [[nodiscard]] bool ComputeGestureTransformUVE(EditorToolSessionModeUVE mode,
                                                   EditorTransformAxisUVE axis, float amount,
                                                   const Scene::TransformComponentUVE& source,
                                                   Scene::TransformComponentUVE& outTransform) const;
    /// The translate half of ComputeGestureTransformUVE, taking the world delta directly. The
    /// axis form is this with a delta of `axisVector * amount`.
    [[nodiscard]] bool ComputeTranslatedTransformUVE(Scene::EntityUVE entity,
                                                      const Math::Vector3UVE& worldDelta,
                                                      const Scene::TransformComponentUVE& source,
                                                      Scene::TransformComponentUVE& outTransform) const;
    /// ComputeGestureTransformUVE against the selected entity's live transform, behind the guards
    /// the four public commands share.
    [[nodiscard]] bool TryComputeSelectedGestureTransformUVE(
        EditorToolSessionModeUVE mode, EditorTransformAxisUVE axis, float amount,
        Scene::TransformComponentUVE& outTransform) const;
    [[nodiscard]] bool ApplyLocalTransformUVE(Scene::EntityUVE entity,
                                               const Scene::TransformComponentUVE& transform);
    [[nodiscard]] bool ApplyEntityNameStateUVE(Scene::EntityUVE entity,
                                                const std::optional<std::string>& name);
    [[nodiscard]] bool ApplyPrimitiveMeshStateUVE(Scene::EntityUVE entity,
                                                    const Scene::PrimitiveMeshComponentUVE& primitive);
    [[nodiscard]] bool IsSceneComponentValueValidUVE(EditorSceneComponentKindUVE kind,
                                                      const EditorSceneComponentValueUVE& value) const noexcept;
    [[nodiscard]] bool AreSceneComponentValuesEqualUVE(const EditorSceneComponentValueUVE& lhs,
                                                        const EditorSceneComponentValueUVE& rhs) const noexcept;
    [[nodiscard]] bool ApplySceneComponentStateUVE(
        Scene::EntityUVE entity, EditorSceneComponentKindUVE kind,
        const std::optional<EditorSceneComponentValueUVE>& value);
    [[nodiscard]] bool IsDocumentSubtreeUVE(Scene::EntityUVE root) const;
    [[nodiscard]] bool DoesSubtreeContainEntityUVE(Scene::EntityUVE root,
                                                    Scene::EntityUVE candidate) const;
    [[nodiscard]] std::optional<Scene::SceneSnapshotUVE> CaptureSubtreeUVE(Scene::EntityUVE root);
    [[nodiscard]] Scene::EntityUVE RestoreSubtreeUnderParentUVE(const Scene::SceneSnapshotUVE& snapshot,
                                                                 Scene::EntityUVE parent);
    [[nodiscard]] bool TryGetDocumentParentUVE(Scene::EntityUVE entity, Scene::EntityUVE& outParent) const;
    /// Returns one compact editor-only tag using the fixed primitive-first priority documented for
    /// the Scene Outliner. Empty means the entity is a plain document entity.
    [[nodiscard]] std::string GetOutlinerTypeTagUVE(Scene::EntityUVE entity) const;
    [[nodiscard]] std::vector<Scene::EntityUVE> GetDocumentAncestryUVE(Scene::EntityUVE entity) const;
    [[nodiscard]] std::vector<Scene::EntityUVE> GetEligibleReparentParentsUVE(Scene::EntityUVE entity);
    [[nodiscard]] std::string GetHierarchyCandidateLabelUVE(Scene::EntityUVE entity) const;
    [[nodiscard]] bool IsLifecycleCommandAllowedUVE() const noexcept;
    [[nodiscard]] bool IsAuthoringCommandAllowedUVE() const noexcept;
    [[nodiscard]] EditorSelectionSnapshotUVE CaptureSelectionSnapshotUVE() const;
    void RestoreSelectionUVE(EditorSelectionSnapshotUVE selection) noexcept;
    void PruneSelectionUVE() noexcept;
    [[nodiscard]] bool IsEntitySelectedUVE(Scene::EntityUVE entity) const noexcept;
    [[nodiscard]] std::optional<EditorSelectionBoundsUVE> TryGetEntityBoundsUVE(Scene::EntityUVE entity) const;
    [[nodiscard]] EditorSelectionPathsUVE CaptureSelectionPathsUVE(
        const std::vector<Scene::EntityUVE>& roots) const;
    [[nodiscard]] EditorSelectionSnapshotUVE ResolveSelectionPathsUVE(
        const EditorSelectionPathsUVE& paths, const std::vector<Scene::EntityUVE>& roots) const;
    [[nodiscard]] Scene::EntityUVE ResolveSelectionPathUVE(
        const EditorSelectionPathUVE& path, const std::vector<Scene::EntityUVE>& roots) const;
    [[nodiscard]] bool FindSelectionPathUVE(Scene::EntityUVE current, Scene::EntityUVE target,
                                             std::vector<std::size_t>& inOutChildIndices) const;
    [[nodiscard]] bool ReparentDocumentEntityUVE(Scene::EntityUVE entity, Scene::EntityUVE newParent);
    [[nodiscard]] bool ComputeKeepWorldLocalTransformUVE(Scene::EntityUVE entity, Scene::EntityUVE newParent,
                                                          Scene::TransformComponentUVE& outTransform) const;
    [[nodiscard]] bool IsReparentModeChangeAllowedUVE() const noexcept;
    [[nodiscard]] bool IsHierarchyFilterActiveUVE() const noexcept;
    [[nodiscard]] bool IsHierarchyEntityVisibleUVE(Scene::EntityUVE entity) const;
    void RebuildHierarchyFilterCacheUVE();
    void InvalidateHierarchyFilterCacheUVE() noexcept;
    void CancelHierarchyRenameUVE() noexcept;
    [[nodiscard]] Scene::EntityUVE CreateDocumentEntityInternalUVE(
        EditorEntityKindUVE kind, const std::optional<std::string>& explicitName);
    /// Creates the document-entity shell every scene object starts from: a live entity with a
    /// default TransformComponentUVE and the given (already finalized) NameComponentUVE.
    /// Object definitions (Engine/Runtime/Objects/3D) attach their kind-specific components on top.
    [[nodiscard]] Scene::EntityUVE CreateDocumentEntityShellInternalUVE(const std::string_view name);

    /// Returns whether `entity` carries the scene-root marker. The root is never deletable,
    /// re-parentable, or duplicable - every one of those commands checks this first.
    [[nodiscard]] bool IsSceneRootEntityUVE(Scene::EntityUVE entity) const;
    /// An object the tree is built on and that cannot be deleted, duplicated or moved: the scene
    /// root, and while the Entity Editor is open, the entity's own root.
    [[nodiscard]] bool IsStructuralRootUVE(Scene::EntityUVE entity);
    /// Gives an object the recipe parts it was saved without - Visibility for a spatial object and the
    /// common Object section - so its Inspector always shows the full recipe.
    void RepairInspectorRecipeUVE(Scene::EntityUVE entity);
    /// Registers the hand-drawn Transform section; called at Transform's place in section order.
    void RegisterTransformInspectorDrawerUVE();

    /// Returns the document's scene root when one exists, else creates it (name + transform +
    /// marker via the SceneRoot ObjectDefinition). Idempotent: the one-root invariant every
    /// document seam relies on is established or confirmed on every call.
    [[nodiscard]] Scene::EntityUVE EnsureDocumentSceneRootUVE();
    /// Creates a document entity for one object kind from that kind's ObjectDefinition: a
    /// uniquely-named entity shell plus the definition's component recipe. Defined in
    /// editor_uve.cpp next to its only call sites.
    /// A new, typed, unparented object of `kind` with no undo step - the recipe both the Add menus
    /// and the Content catalogue build from.
    [[nodiscard]] Scene::EntityUVE CreateSceneObjectEntityInternalUVE(Scene::Objects::SceneObjectKindUVE kind);
    template <typename Definition, typename ApplyFunc>
    [[nodiscard]] Scene::EntityUVE CreateObjectDefinitionEntityInternalUVE(const Definition& definition,
                                                                         ApplyFunc applyDefinition);
    void RecordHistoryUVE(HistoryEntryUVE entry);
    void ClearHistoryUVE() noexcept;
    [[nodiscard]] bool UndoHistoryEntryUVE(HistoryEntryUVE& entry);
    [[nodiscard]] bool RedoHistoryEntryUVE(HistoryEntryUVE& entry);
    void DestroyDocumentSubtreeUVE(Scene::EntityUVE root);
    void ClearDocumentSceneUVE();
    void LoadSessionSettingsUVE();
    /// Moves the value of every renamed setting id from its old key to its new one, once, when the
    /// file still carries the old name and the new one holds nothing (see kRenamedSettingIdsUVE).
    void MigrateRenamedSettingIdsUVE(Config::IConfigManagerUVE& config);
    [[nodiscard]] bool SaveSessionSettingsUVE();
    void ApplyLayoutPresetUVE(EditorLayoutPresetUVE preset) noexcept;
    void DrawMenuBarUVE();
    void DrawViewportPanelUVE();
    /// The rendered scene and its overlay, filling the rest of the current window (the main
    /// Viewport panel and the Entity Editor both use it).
    void DrawViewportImageUVE(ViewportContextUVE context);
    void DrawViewportOverlayBubblesUVE(Math::Vector2UVE imageOrigin, Math::Vector2UVE imageSize);
    void DrawEntityContextToolbarUVE(Math::Vector2UVE imageOrigin, Math::Vector2UVE imageSize);
    void DrawViewportAxisColorPickerUVE();
    void DrawViewportSelectionOutlineMenuUVE();
    void DrawPluginWindowUVE();
    // One settings window's view state (Editor Preferences, Project Settings). Session-only.
    struct SettingsWindowStateUVE final {
        bool visible = false;
        bool focusSearch = false;
        bool modifiedOnly = false;
        bool showAdvanced = false;
        std::array<char, 128> search{};
        std::string category;
    };
    // Where a settings window reads and writes values: the live editor, or a settings document.
    struct SettingsWindowSourceUVE final {
        const Config::SettingsRegistryUVE* registry = nullptr;
        std::function<std::optional<Config::SettingValueUVE>(std::string_view id)> get;
        std::function<bool(std::string_view id, const Config::SettingValueUVE& value)> set;
        // Where the changes go, shown in the footer, and any footer buttons beside Reset.
        std::string footerNote;
        std::function<void()> drawFooterActions;
    };
    void DrawEditorPreferencesWindowUVE();
    void DrawProjectSettingsWindowUVE();
    // The Input Map window (editor_panel_input_map_uve.cpp). Session-only view state.
    struct InputMapWindowStateUVE final {
        bool visible = false;
        std::size_t selected = 0U;
        std::array<char, 64> filter{};
        // The name field's buffer, and which action and name it was filled from.
        std::array<char, 129> rename{};
        std::size_t renameFor = std::numeric_limits<std::size_t>::max();
        std::string renameSource;
        // Listening for an input to bind: to which action and side, replacing which binding.
        bool listening = false;
        std::size_t listenAction = 0U;
        bool listenNegative = false;
        std::optional<std::size_t> listenReplace;
    };
    void DrawInputMapWindowUVE();
    // Commands (editor_commands_uve.cpp).
    struct CommandPaletteStateUVE final {
        bool open = false;
        bool focus = false;
        std::array<char, 128> query{};
        int highlighted = 0;
    };
    struct ShortcutsWindowStateUVE final {
        bool visible = false;
        std::array<char, 64> filter{};
        bool listening = false;
        std::size_t listenCommand = 0U;
        std::size_t listenSlot = 0U;
    };
    void RegisterEditorCommandsUVE();
    [[nodiscard]] EditorCommandUVE* FindEditorCommandUVE(std::string_view id) noexcept;
    void DispatchEditorShortcutsUVE();
    void DrawCommandMenuItemUVE(std::string_view id);
    void DrawCommandPaletteUVE();
    void DrawKeyboardShortcutsWindowUVE();
    // Shortcuts as the hidden settings "editor.shortcuts.<command>.primary|alternate".
    [[nodiscard]] std::optional<Config::SettingValueUVE> GetShortcutSettingUVE(std::string_view id) const;
    [[nodiscard]] bool SetShortcutSettingUVE(std::string_view id, const Config::SettingValueUVE& value);
    void DrawInputBindingListUVE(std::vector<Input::InputActionUVE>& actions, std::size_t actionIndex, bool negative,
                                 bool& changed);
    // Sets the project's input map and registers it with the input system at once.
    void CommitInputMapUVE(std::vector<Input::InputActionUVE> actions);
    void DrawSettingsWindowBodyUVE(SettingsWindowStateUVE& state, const SettingsWindowSourceUVE& source);
    void DrawSettingRowUVE(const Config::SettingDescriptorUVE& descriptor, bool modified,
                           const SettingsWindowSourceUVE& source);
    void DrawBottomDockUVE();
    void DrawBottomDockContentUVE();
    /// The strip of dock tabs along the bottom edge - Content, Output, Console - and the dock toggle.
    void DrawBottomDockTabBarUVE();
    /// The Console dock: the developer console's output and its command line.
    void DrawConsoleDockUVE();
    void DrawHierarchyPanelUVE();
    /// The Scene tree with its "+" and search, filling the rest of the current window.
    void DrawHierarchyBodyUVE();
    void DrawHierarchyObjectContextMenuUVE(Scene::EntityUVE entity);
    void DrawObjectPickerUVE();
    // A collapsing header (`asHeader`) or tree object whose open state lives in m_inspectorFoldOpen.
    bool DrawInspectorFoldUVE(const char* label, const std::string& key, bool defaultOpen, bool asHeader,
                              int flags);
    void DrawHierarchyVisibilityToggleUVE(Scene::EntityUVE entity, bool rowHovered);
    // Right-click menu on an Inspector section header: Copy / Paste / Reset. `entry` null means
    // the Transform section.
    void DrawInspectorSectionMenuUVE(const Core::TypeMetadataEntryUVE* entry, const char* sectionName);
    void DrawHierarchyRowBadgesUVE(const std::vector<std::string>& warnings, const std::optional<std::string>& script,
                                   float eyeColumns);
    void DrawHierarchyObjectUVE(Scene::EntityUVE entity);
    void AcceptHierarchyDropTargetUVE(Scene::EntityUVE targetParent);
    void DrawInspectorPanelUVE();
    void DrawInspectorContentUVE();
    void RegisterBuiltInInspectorDrawersUVE();
    /// Registers one drawer per declared component type, from the metadata registry. This replaced
    /// a hand-written registration and a per-type switch for each of them: a component that
    /// declares its properties is inspectable without the inspector being told it exists.
    void RegisterMetadataInspectorDrawersUVE();
    /// A type a section may draw inside itself, and the hosts that type prefers over this one.
    struct NestedMetadataSectionUVE final {
        const Core::TypeMetadataEntryUVE* entry = nullptr;
        std::vector<const Core::TypeMetadataEntryUVE*> preferredHosts;
    };
    /// Draws one component's section: a collapsible header (or, for a type presented inline, just
    /// its rows), its properties, and the section of each type in `nested` the entity also carries
    /// and no preferred host of it draws. Writes go through SetSelectedComponentPropertyUVE below.
    void DrawMetadataComponentDrawerUVE(Scene::EntityUVE entity, const Core::TypeMetadataEntryUVE& entry,
                                        const std::vector<NestedMetadataSectionUVE>& nested);
    /// Draws the visible properties of one component as label/value rows. Runtime-owned rows appear
    /// only during Play, where they describe something real; a row another property declares as its
    /// resolved answer is shown beside that property instead of on its own.
    void DrawMetadataPropertyRowsUVE(const Core::TypeMetadataEntryUVE& entry, const void* instance);
    /// Draws one property row: label, tooltip, a revert control when the value differs from a new
    /// component's, and the widget its declared value type calls for. `instance` points at the live
    /// component on the selected entity.
    void DrawMetadataPropertyRowUVE(const Core::TypeMetadataEntryUVE& entry,
                                    const Core::TypeMetadataPropertyUVE& property, const void* instance);
    /// The label cell shared by generic and custom rows. Returns true when the author reverted the
    /// property to its default this frame.
    bool DrawMetadataPropertyLabelUVE(const Core::TypeMetadataEntryUVE& entry,
                                      const Core::TypeMetadataPropertyUVE& property, const void* instance,
                                      bool writable);
    /// Dispatches a property that names a custom drawer. Returns false for an id with no drawer,
    /// which the caller treats as "draw it generically".
    bool DrawCustomPropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                               const Core::TypeMetadataPropertyUVE& property, const void* instance);
    void DrawMultilineTextPropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                      const Core::TypeMetadataPropertyUVE& property, const void* instance);
    void DrawScriptSlotPropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                   const Core::TypeMetadataPropertyUVE& property, const void* instance);
    /// The rows under the script slot: one control per `export` field of a `.uvs` script.
    void DrawScriptExportsPropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                      const Core::TypeMetadataPropertyUVE& property, const void* instance);
    /// A combo over the project's assets with `extension` (".uvanim"). Returns the pick, if any;
    /// kInvalidAssetGuidUVE means "(none)" was picked.
    [[nodiscard]] std::optional<Asset::AssetGuidUVE> DrawAssetPickerUVE(const char* id, Asset::AssetGuidUVE value,
                                                                      const std::string& extension);
    /// AnimationGraph's parameter table and its graph (objects, wiring, transitions). Every edit
    /// writes the whole list back through SetSelectedComponentPropertyUVE, so it is one undo step
    /// and the graph is re-validated before it lands.
    void DrawAnimationParametersPropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                            const Core::TypeMetadataPropertyUVE& property, const void* instance);
    void DrawAnimationGraphPropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                       const Core::TypeMetadataPropertyUVE& property, const void* instance);
    /// Queues a parameter rename for the graph block drawn next, so the objects and transitions that
    /// read the old name follow it.
    void RenameAnimationParameterReferencesUVE(const std::string& from, const std::string& to);
    void DrawObjectMetadataPropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                     const Core::TypeMetadataPropertyUVE& property, const void* instance);
    /// Skeleton3D's Source row: which rigged model its bones come from, with Reload and Clear.
    void DrawSkeletonSourcePropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                       const Core::TypeMetadataPropertyUVE& property, const void* instance);
    /// Skeleton3D's bone hierarchy, read-only: bones are authored in the DCC tool, not here.
    void DrawSkeletonBonesPropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                      const Core::TypeMetadataPropertyUVE& property, const void* instance);
    /// A fixed-capacity list of entity references (RayCast3D's exclusions): one object picker per
    /// slot, plus the add/remove rows. Every edit writes the whole list back through
    /// SetSelectedComponentPropertyUVE, so it is one undo step and the component's own rule
    /// (dense, no duplicates) is enforced before the write lands.
    void DrawEntityReferenceListPropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                            const Core::TypeMetadataPropertyUVE& property, const void* instance);
    /// LODGroup3D's distance chain: one threshold row per level in use, plus the distance the
    /// object is culled past, which is the rule those thresholds add up to. One write for the whole
    /// array, so an edit is one undo step.
    void DrawLodGroupThresholdsPropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                           const Core::TypeMetadataPropertyUVE& property, const void* instance);
    /// LODGroup3D's per-level meshes: one mesh picker per level in use. An unassigned level draws
    /// the object's own Mesh component mesh, which is what the row shows until it is overridden.
    void DrawLodGroupMeshesPropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                       const Core::TypeMetadataPropertyUVE& property, const void* instance);
    /// Points the selected Skeleton3D at the model source `relativeSource` (content-relative) and
    /// loads its bones; an empty path clears both. One undo step. False, with the reason in
    /// m_skeletonSourceStatus, when the file has no readable skeleton.
    bool BindSelectedSkeletonSourceUVE(const std::filesystem::path& relativeSource);
    /// The world-space bones of every enabled Skeleton3D in the document, for the viewport.
    void BuildSkeletonOverlayUVE(std::vector<ViewportBoneUVE>& outBones) const;
    /// True when `instance`'s value for `property` equals what a newly added component holds. False
    /// when that cannot be known (no equality for the type), so a revert is offered rather than hidden.
    [[nodiscard]] bool IsPropertyAtDefaultUVE(const Core::TypeMetadataEntryUVE& entry,
                                              const Core::TypeMetadataPropertyUVE& property, const void* instance);
    /// A text field that commits once, when the author finishes - Enter, or focus leaving after an
    /// edit - rather than on every keystroke: one undo entry per edit, and no edit lost to a click
    /// elsewhere. Input past `maximumBytes` is refused as it is typed. Returns the committed text.
    [[nodiscard]] std::optional<std::string> DrawCommittedTextInputUVE(const char* id, const std::string& current,
                                                                       bool multiline, float height,
                                                                       std::size_t maximumBytes);
    /// Replaces the selected entity's whole component of `entry`'s type with `newInstance`, as one
    /// undo step, when the type's own rule accepts it. For edits that change several fields at once.
    bool SetSelectedComponentValueUVE(const Core::TypeMetadataEntryUVE& entry, const void* newInstance);
    /// Writes one property of one component on the selected entity and records one undo entry.
    /// Refuses when authoring is unavailable, the selection is not a single document entity, the
    /// entity does not hold the component, the property is not authoring-writable, or the value is
    /// unchanged - matching what the per-type commands already refuse.
    [[nodiscard]] bool SetSelectedComponentPropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                                       const Core::TypeMetadataPropertyUVE& property,
                                                       const void* newValue);
    /// A continuous edit of one property - a colour picker session - shown live but recorded as
    /// ONE undo entry when it ends, the way a transform drag is. Preview writes the value without
    /// history (refused, leaving the previous value, when the component's rule rejects it); the
    /// first preview captures the component to restore. A preview of another entity, component
    /// or property first commits the one in flight.
    [[nodiscard]] bool PreviewSelectedComponentPropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                                           const Core::TypeMetadataPropertyUVE& property,
                                                           const void* newValue);
    /// Ends the edit in flight with one history entry from where it started to where it is. An
    /// edit that ended where it began records nothing and leaves the scene's dirty flag as it was.
    [[nodiscard]] bool CommitComponentPropertyPreviewUVE();
    /// Ends the edit in flight by putting the component back as it was, with no history.
    bool CancelComponentPropertyPreviewUVE();
    /// Commits the edit in flight only if it is of this property; any other is left alone.
    [[nodiscard]] bool CommitComponentPropertyPreviewForUVE(const Core::TypeMetadataEntryUVE& entry,
                                                            const Core::TypeMetadataPropertyUVE& property);
    /// For a drag-style field just drawn: while it is held its value is previewed, and when it is
    /// let go the whole drag becomes one undo step. A change made without holding it is recorded
    /// at once. Returns whether anything was written.
    bool ApplyContinuousPropertyEditUVE(const Core::TypeMetadataEntryUVE& entry,
                                        const Core::TypeMetadataPropertyUVE& property, bool changed,
                                        const void* newValue);
    /// Restores one property to the value a default-constructed component would have.
    [[nodiscard]] bool ResetSelectedComponentPropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                                         const Core::TypeMetadataPropertyUVE& property);
    /// Overwrites a live component with a recorded snapshot of it, for undo and redo. Fails
    /// without mutation when the entity is gone or no longer holds the component, which is what
    /// makes a stale history entry clear the history rather than corrupt the scene.
    [[nodiscard]] bool ApplyComponentPropertySnapshotUVE(Scene::EntityUVE entity,
                                                         const Core::TypeMetadataEntryUVE* metadata,
                                                         const void* snapshot);
    void DrawTransformInspectorDrawerUVE(Scene::EntityUVE entity);
    void DrawPrefabInspectorDrawerUVE(Scene::EntityUVE entity);
    void DrawImportQueueMonitorUVE();
    void DrawScriptingWorkspaceUVE();
    /// The Scripting workspace while a `.uvs` file is open: toolbar, text, and the compiler's list.
    void DrawUVScriptEditorUVE();
    /// The editor's text, toolbar and problem list inside the current window; shared by the
    /// Scripting workspace and the Entity Editor's Scripting tab. Returns true on Close.
    bool DrawUVScriptEditorBodyUVE(bool offerClose);
    /// The script's text box: line numbers in a gutter, and the text coloured by kind.
    void DrawUVScriptTextBoxUVE(UVScriptDocumentUVE& document, float height);
    [[nodiscard]] static ContentBrowserItemTypeUVE ClassifyContentBrowserEntryUVE(
        const Asset::ProjectFileEntryUVE& entry);
    [[nodiscard]] static const char* GetContentBrowserItemTypeLabelUVE(ContentBrowserItemTypeUVE type) noexcept;
    [[nodiscard]] static const char* GetContentBrowserFocusLabelUVE(ContentBrowserTypeFocusUVE focus) noexcept;
    [[nodiscard]] bool DoesContentBrowserEntryMatchFocusUVE(const Asset::ProjectFileEntryUVE& entry) const;
    [[nodiscard]] bool IsContentBrowserDirectoryInSnapshotUVE(const Asset::ProjectFileSnapshotUVE& snapshot,
                                                               const std::filesystem::path& directory) const;
    void ReconcileContentBrowserDirectoryUVE(const Asset::ProjectFileSnapshotUVE& snapshot) noexcept;
    [[nodiscard]] bool IsProjectPathFavoritedUVE(const std::filesystem::path& relativePath) const;
    void ToggleProjectPathFavoriteUVE(const std::filesystem::path& relativePath);
    /// The team's shelves live beside project.uvsettings, in project.uvshelves.
    [[nodiscard]] std::filesystem::path GetSharedShelvesPathUVE() const;
    /// Replaces the shared shelves with the file's; no file means none. Personal shelves stay.
    void LoadSharedShelvesUVE();
    /// Writes the shared shelves; true when written or when there was nothing to write.
    bool SaveSharedShelvesUVE();
    /// Loads the file again when it changed on disk since it was last read or written.
    void ReloadSharedShelvesIfChangedUVE();
    /// Size and last change of a content file, from m_contentFileFacts (read on first ask).
    [[nodiscard]] ContentFileFactsUVE GetContentFileFactsUVE(const std::filesystem::path& contentRoot,
                                                             const Asset::ProjectFileEntryUVE& entry,
                                                             std::uint64_t refreshGeneration);
    /// Returns a GL texture id showing relativePath's own decoded image content, loading and
    /// uploading it on first request and caching the result thereafter. Returns 0 if the file
    /// cannot be loaded as a texture asset (not a texture, corrupt, or an unsupported pixel
    /// format) - callers should fall back to the generic per-type icon in that case.
    [[nodiscard]] std::uintptr_t GetTextureThumbnailUVE(const std::filesystem::path& relativePath);
    void ClearTextureThumbnailCacheUVE() noexcept;
    /// Returns a GL texture id previewing relativePath's own mesh geometry (fixed camera angle,
    /// no material), rendering and caching it on first request. Returns 0 if the file cannot be
    /// loaded as a mesh asset or has no vertices/indices - callers should fall back to the
    /// generic per-type icon in that case.
    [[nodiscard]] std::uintptr_t GetMeshThumbnailUVE(const std::filesystem::path& relativePath);
    void ClearMeshThumbnailCacheUVE() noexcept;
    /// Draws the merged Content Browser panel (folder/file list on the left, thumbnail grid on the
    /// right, separated by a draggable splitter) - replaces the former separate Filesystem and
    /// Contents panels, which showed the same underlying directory from two windows.
    void DrawContentBrowserPanelUVE();
    /// The Content Browser inside the current window: the main dock and the Entity Editor's dock.
    void DrawContentBrowserBodyUVE();
    /// Refreshes the read-only project index after the engine-owned watcher observes a new
    /// filesystem baseline. It never schedules imports or mutates project files.
    void RefreshProjectFileIndexUVE();
    void DrawFilesystemContextPopupUVE();
    /// Places a draggable Content popup before BeginPopup: above the anchor while it is new, at
    /// (x, y) after. `justOpened` restarts it at the pointer.
    static void PlaceContentMenuUVE(bool justOpened, float& anchorX, float& anchorY, int frames, float x, float y);
    /// After BeginPopup: counts the popup's frames and takes its position once it has settled.
    static void SettleContentMenuUVE(int& frames, float& x, float& y);
    /// The Content "+ Add" menu's body - also what right-clicking empty Content space opens.
    void DrawContentCreateMenuUVE(const std::filesystem::path& contentRoot, const std::filesystem::path& directory);
    /// Draws the inline name field over a card while it is being renamed; true while it is.
    bool DrawContentRenameFieldUVE(const std::filesystem::path& contentRoot,
                                   const Asset::ProjectFileEntryUVE& entry, float x, float y, float width);
    void BeginContentRenameUVE(const std::filesystem::path& relativePath);
    /// A drop target for an entity asset dragged out of Content; places it under `parent`.
    void AcceptContentEntityDropUVE(Scene::EntityUVE parent);
    /// Model sources (.glb/.gltf/.obj) are imported automatically: the source stays the thing an
    /// author sees and picks, and its converted mesh lives in the derived-data folder beside the
    /// import cache, never in the content folder.
    [[nodiscard]] static bool IsModelSourcePathUVE(const std::filesystem::path& path);
    /// Where the converted mesh of the model source at `relativeSource` (content-relative) lives.
    [[nodiscard]] std::filesystem::path GetImportedModelPathUVE(const std::filesystem::path& relativeSource) const;
    /// The skeleton a model source gives a Skeleton3D: the imported model's when it has been
    /// conformed to the humanoid (Retarget), else the source file's own bones.
    [[nodiscard]] std::optional<Asset::GltfSkeletonUVE> ReadSkeletonForSourceUVE(const std::filesystem::path& relativeSource,
                                                                                 std::size_t maximumBones) const;
    /// Queues an import for every model source in `snapshot` that is not already in flight. An
    /// unchanged source is a cache hit in the import queue, so this costs a hash, not a re-import.
    void QueueModelAutoImportsUVE(const Asset::ProjectFileSnapshotUVE& snapshot);
    /// Collects finished auto-imports; a successful one refreshes the mesh thumbnails.
    void PollModelImportJobsUVE();
    /// What the last project refresh read from a model source's file, or null if it is not one.
    [[nodiscard]] const EditorModelSourceInfoUVE* FindModelSourceInfoUVE(const std::filesystem::path& relativeSource) const;
    /// True for a model source whose file declares a skeleton (read once per project refresh).
    [[nodiscard]] bool IsRiggedModelSourceUVE(const std::filesystem::path& relativeSource) const;

    Core::EngineServicesUVE* m_services = nullptr;
    /// The editor's own settings, declared by RegisterEditorSettingsUVE; read and written in the
    /// services' settings store.
    Config::SettingsRegistryUVE m_settingsRegistry;
    // Object creation preferences (editor_settings_uve.cpp), and the host's latest camera focus.
    bool m_newObjectsUnderSelection = true;
    EditorNewObjectPlacementUVE m_newObjectPlacement = EditorNewObjectPlacementUVE::ParentOrigin;
    std::optional<Math::Vector3UVE> m_viewportCameraFocus;
    // Play mode preferences (editor_settings_uve.cpp).
    bool m_playPauseOnStart = false;
    bool m_playSaveSceneFirst = false;
    bool m_playSwitchToGame = true;
    static constexpr ViewportAxisColorUVE kDefaultPlayTintColorUVE{0.30F, 0.48F, 0.80F};
    static constexpr float kDefaultPlayTintStrengthUVE = 0.2F;
    bool m_playTintEnabled = true;
    ViewportAxisColorUVE m_playTintColor = kDefaultPlayTintColorUVE;
    float m_playTintStrength = kDefaultPlayTintStrengthUVE;
    // Hierarchy panel preferences (editor_settings_uve.cpp).
    HierarchyViewSettingsUVE m_hierarchyView;
    // Each editor setting's descriptor and its reads and writes of the state above, in one table
    // (editor_settings_uve.cpp) that loading, saving and the preferences window all use.
    [[nodiscard]] static const std::vector<EditorSettingBindingUVE>& GetSettingBindingsUVE();
    [[nodiscard]] static const EditorSettingBindingUVE* FindSettingBindingUVE(std::string_view id);
    // Where a new object goes: under the single selection when the preference allows and there is
    // one, otherwise under the scene root. Then, for a spatial object, where in space.
    [[nodiscard]] Scene::EntityUVE ResolveNewObjectParentUVE();
    /// Where a new object of `kind` goes: DirectionalLight3D and WorldEnvironment at the top, a folder
    /// in the selected folder or the Viewport, anything else in a folder (see ResolveObjectFolderUVE).
    [[nodiscard]] Scene::EntityUVE ResolveNewObjectParentForUVE(Scene::Objects::SceneObjectKindUVE kind);
    // ---- The level's Outliner layout ---------------------------------------------------------------
    //   (scene root, not shown)
    //   +- Viewport            folders only
    //   |  +- Folder ...       any object
    //   +- DirectionalLight3D  one at most
    //   +- WorldEnvironment    one at most
    /// Whether the layout applies: the level is the document (no entity, no Retarget preview open).
    [[nodiscard]] bool IsOutlinerLayoutActiveUVE() const noexcept;
    /// Builds the layout if missing: the Viewport, a World folder in a fresh level, and anything a
    /// level saved before the layout had at the top moved into World. True when it changed anything.
    bool EnsureDocumentLayoutUVE();
    /// The folder a new object goes into when none is selected: the last one used, else the Viewport's
    /// first, else a new "World".
    [[nodiscard]] Scene::EntityUVE ResolveObjectFolderUVE();
    /// Whether `entity` may sit under `parent` in the layout.
    [[nodiscard]] bool IsAllowedOutlinerParentUVE(Scene::EntityUVE entity, Scene::EntityUVE parent);
    /// DirectionalLight3D and WorldEnvironment: the level's two top-level singletons.
    [[nodiscard]] bool IsTopLevelSingletonKindUVE(Scene::Objects::SceneObjectKindUVE kind) const noexcept;
    /// The top-level DirectionalLight3D or WorldEnvironment, when the level has it.
    [[nodiscard]] Scene::EntityUVE FindTopLevelObjectUVE(Scene::Objects::SceneObjectKindUVE kind);
    Scene::EntityUVE m_lastUsedFolder = Scene::kInvalidEntityUVE;
    void PlaceNewDocumentObjectUVE(Scene::EntityUVE entity);
    friend bool RegisterEditorSettingsUVE(Config::SettingsRegistryUVE& registry);
    Core::ISimulationControlUVE* m_simulationControl = nullptr;
    EditorStateUVE m_state = EditorStateUVE::Uninitialized;
    EditorPlayModeStateUVE m_playModeState = EditorPlayModeStateUVE::Edit;
    std::optional<PlayModeSessionUVE> m_playModeSession;
    std::optional<EntityEditSessionUVE> m_entityEditSession;
    std::optional<RetargetWindowStateUVE> m_retargetWindow;
    std::optional<RetargetPreviewUVE> m_retargetPreview;
    // Which workspace tab was active before EnterPlayModeUVE() switched to Game, so StopPlayModeUVE()
    // can restore it - mirrors Unity's own Scene<->Game auto-switch on Play/Stop.
    EditorWorkspaceUVE m_workspaceBeforePlayMode = EditorWorkspaceUVE::Library;
    // 0 = plain Play triangle, 1 = Pause bars; eased toward the target each frame in
    // DrawMenuBarUVE() so the icon animates instead of instantly swapping shape.
    float m_playButtonMorphProgress = 0.0F;
    std::vector<Scene::EntityUVE> m_selectedEntities;
    Scene::EntityUVE m_selectedEntity = Scene::kInvalidEntityUVE;
    std::filesystem::path m_activeScenePath;
    std::size_t m_historyCapacity = 100U;
    EditorReparentTransformModeUVE m_reparentTransformMode = EditorReparentTransformModeUVE::KeepLocal;
    EditorTransformSnappingSettingsUVE m_transformSnappingSettings{};
    EditorToolSessionUVE m_toolSession;
    Editor2DCanvasStateUVE m_2dCanvasState{};
    // Transient editor-viewport bookmark slots (Set/Get/ClearViewportBookmarkUVE) - session
    // state only, intentionally NOT part of any document or settings file.
    std::array<std::optional<EditorViewportBookmarkUVE>, kEditorViewportBookmarkSlotCountUVE>
        m_viewportBookmarks{};
    bool m_2dCanvasPanning = false;
    std::deque<HistoryEntryUVE> m_undoHistory;
    std::deque<HistoryEntryUVE> m_redoHistory;
    EditorWorkspaceUVE m_activeWorkspace = EditorWorkspaceUVE::Library;
    /// Transient Plugin window/tool gates. These are editor-session state only and never become ECS
    /// components, serialized scene data, or runtime/plugin activation side effects.
    bool m_pluginWindowVisible = false;
    // The Editor Preferences and Project Settings windows (editor_panel_preferences_uve.cpp).
    SettingsWindowStateUVE m_preferencesWindow;
    SettingsWindowStateUVE m_projectSettingsWindow;
    InputMapWindowStateUVE m_inputMapWindow;
    std::vector<EditorCommandUVE> m_commands;
    std::deque<std::string> m_recentCommandIds;
    CommandPaletteStateUVE m_commandPalette;
    ShortcutsWindowStateUVE m_shortcutsWindow;
    EditorRightPanelTabUVE m_activeRightPanelTab = EditorRightPanelTabUVE::Inspector;
    /// The tab the strip showed last frame, to tell a click in it from a change made elsewhere.
    EditorRightPanelTabUVE m_drawnRightPanelTab = EditorRightPanelTabUVE::Inspector;
    InspectorDrawerRegistryUVE m_inspectorDrawerRegistry;
    DeveloperConsoleUVE m_developerConsole;
    EditorBottomDockUVE m_activeBottomDock = EditorBottomDockUVE::FileSystem;
    /// Empty is the ProjectFileIndexUVE content root. This value is session-only and must name a
    /// directory in the latest successful copied snapshot before it is used as a browser location.
    std::filesystem::path m_contentBrowserDirectory;
    /// User-curated shortcuts into the project tree, as project-relative generic paths; both
    /// directories and files may be favorited. Persisted across sessions (see
    /// Save/LoadSessionSettingsUVE). An entry no longer present in the latest snapshot is simply
    /// not shown, never pruned from storage here, so a not-yet-scanned favorite is not lost.
    std::vector<std::filesystem::path> m_favoriteProjectPaths;
    // Inspector fold states by key (see SetInspectorFoldOpenUVE); saved with the session.
    std::map<std::string, bool> m_inspectorFoldOpen;
    // Inspector section clipboard (see CopySelectedComponentUVE / CopySelectedTransformUVE).
    struct ComponentClipboardUVE final {
        const Core::TypeMetadataEntryUVE* entry = nullptr;
        Core::TypeInstanceUVE value;
    };
    std::optional<ComponentClipboardUVE> m_componentClipboard;
    std::optional<Scene::TransformComponentUVE> m_transformClipboard;
    /// The shelf the Content item area shows instead of m_contentBrowserDirectory, or empty.
    std::string m_contentBrowserShelf;
    /// The user's shelves; saved with the session like the pinned paths.
    ContentShelvesUVE m_contentShelves;
    /// project.uvshelves' write time as last read or written, to notice a change made outside
    /// (a pull, a teammate's editor), and when the panel last looked.
    std::optional<std::filesystem::file_time_type> m_sharedShelvesWriteTime;
    double m_sharedShelvesCheckedAt = 0.0;
    /// Back/forward; follows m_contentBrowserDirectory/m_contentBrowserShelf each frame, so a
    /// change made anywhere (tree, breadcrumb, bridge) is a step Back can undo.
    ContentNavigationHistoryUVE m_contentHistory;
    /// The folder tree's own search, shown while its magnifier is on.
    std::string m_contentTreeFilter;
    bool m_contentTreeSearchOpen = false;
    /// The shelf being renamed inline in the sidebar, and its text.
    std::string m_contentShelfRenaming;
    std::string m_contentShelfRenameText;
    /// Fraction of the merged Content Browser panel's width given to its left file/folder list
    /// (the remainder goes to the right thumbnail grid); adjusted by dragging the splitter between
    /// them. Matches the ~35% left / ~65% right proportions of the design this panel was built to.
    float m_contentBrowserSplitRatio = 0.35F;
    /// Whether the Content Browser shows its sidebar (pinned, folders, shelves) beside the items,
    /// or the items alone at full width. Toggled from the panel's Settings menu.
    bool m_contentBrowserSplitModeUVE = true;
    /// How the Content Browser shows files, picked from its "..." menu.
    enum class ContentBrowserViewModeUVE : std::uint8_t {
        SmallTiles = 0,
        LargeTiles,
        List,
    };
    ContentBrowserViewModeUVE m_contentBrowserViewMode = ContentBrowserViewModeUVE::SmallTiles;
    /// Which of the five modes the item area is in (Settings > Mode, or the mode strip).
    ContentBrowserModeUVE m_contentBrowserMode = ContentBrowserModeUVE::Tiles;
    /// Details' sort, for this session.
    ContentSortKeyUVE m_contentSortKey = ContentSortKeyUVE::Name;
    bool m_contentSortAscending = true;
    /// Size and last change of files the Details, Recent and Columns modes have asked about, by
    /// content-relative path; read from disk once each and forgotten when the project is rescanned.
    std::unordered_map<std::string, ContentFileFactsUVE> m_contentFileFacts;
    std::uint64_t m_contentFileFactsGeneration = 0U;
    /// Content-derived thumbnail textures for Content Browser entries (currently texture assets
    /// only), keyed by project-relative generic path. A cached 0 means a prior load attempt
    /// failed (not a texture, corrupt, or unsupported format) and callers should fall back to the
    /// generic per-type icon rather than retrying every frame. Cleared whenever the project file
    /// index is successfully refreshed, since on-disk content may have changed.
    std::map<std::string, std::uintptr_t> m_textureThumbnailCache;
    /// Content-derived thumbnail textures for Content Browser mesh entries, rendered on demand by
    /// m_meshThumbnailRenderer. Same caching/invalidation contract as m_textureThumbnailCache.
    std::map<std::string, std::uintptr_t> m_meshThumbnailCache;
    /// Why the last Skeleton3D source bind failed; shown under the Source row until the next bind.
    std::string m_skeletonSourceStatus;
    /// The bone whose rest pose the Skeleton3D Inspector shows, by name.
    std::string m_selectedSkeletonBone;
    /// The Entity Editor's Timeline: the AnimationSequencer it shows, that player's clip, and the
    /// preview playhead. Previewing writes the skeleton's runtime pose only, never a saved value.
    struct AnimationTimelineStateUVE final {
        Scene::EntityUVE player = Scene::kInvalidEntityUVE;
        Asset::AssetGuidUVE clipGuid{};
        std::shared_ptr<const Asset::AnimationClipAssetUVE> clip;
        /// The clip `clip` replaced this frame, kept alive until the next one: the frame's drawing
        /// still reads it through a plain pointer.
        std::shared_ptr<const Asset::AnimationClipAssetUVE> retired;
        std::string loadError;
        double timeSeconds = 0.0;
        bool playing = false;
        bool loop = true;
        /// Horizontal zoom; 0 fits the whole clip.
        float pixelsPerSecond = 0.0F;
        /// The time at the left edge of the track area while zoomed in.
        double scrollSeconds = 0.0;
        std::string filter;
        /// Tracks opened into their Position / Rotation / Scale rows, by name.
        std::vector<std::string> expandedTracks;
        /// Editing: the selected keys, what Ctrl+C copied, and the clip's own undo history.
        std::vector<ClipKeyUVE> selectedKeys;
        ClipKeyClipboardUVE clipboard;
        std::vector<std::shared_ptr<const Asset::AnimationClipAssetUVE>> undo;
        std::vector<std::shared_ptr<const Asset::AnimationClipAssetUVE>> redo;
        /// Edited since it was loaded or saved.
        bool dirty = false;
        /// A drag of the selected keys, from this mouse x.
        bool draggingKeys = false;
        float dragFromX = 0.0F;
        /// A selection box, from this screen point.
        bool boxSelecting = false;
        float boxFromX = 0.0F;
        float boxFromY = 0.0F;
        std::string status;
        /// The selected event (-1: none), a drag of it from this mouse x, and the rename field.
        int selectedEvent = -1;
        bool draggingEvent = false;
        float eventDragFromX = 0.0F;
        bool renameEventRequested = false;
        std::string eventName;
        /// The animation picker: what each clip is called and how long it runs, read when the
        /// picker opens; its search text; and a request to name the current clip.
        struct ClipCardUVE final {
            std::string name;
            std::filesystem::path path;
            double durationSeconds = 0.0;
            bool skeletal = false;
            bool readable = false;
        };
        std::unordered_map<std::uint64_t, ClipCardUVE> clipCards;
        std::string pickerSearch;
        bool renameClipRequested = false;
        std::string clipName;
        /// Curves view instead of the dope sheet, and which channel it draws (-1: all that move).
        bool curves = false;
        int curveChannel = -1;
        /// A drag of one curve point: channel * 3 + axis, the key's time, the value range frozen
        /// at the start (so the graph does not rescale under the mouse), and the unwrap offset of
        /// a rotation angle.
        bool curveDragging = false;
        int curveComponent = 0;
        double curveKeySeconds = 0.0;
        float curveRangeMin = 0.0F;
        float curveRangeMax = 1.0F;
        float curveUnwrap = 0.0F;
        /// The skeleton whose pose the Timeline wrote, to put back at rest when it stops.
        Scene::EntityUVE previewSkeleton = Scene::kInvalidEntityUVE;
    };
    AnimationTimelineStateUVE m_timeline;
    /// The Timeline had keyboard focus last frame: its keys (Delete, Ctrl+Z, arrows...) are its own,
    /// so the editor's shortcuts stand aside, as they do while typing.
    bool m_timelineOwnsKeys = false;
    /// The Timeline tab's body: transport, ruler, bone tracks and the preview on the skeleton.
    void DrawAnimationTimelineUVE();
    /// Puts the previewed skeleton back at rest and stops the preview.
    void StopAnimationTimelinePreviewUVE();
    /// The Timeline's animation picker: the player's animations, switching, New, Add from Project,
    /// Rename, Duplicate and Remove.
    void DrawAnimationPickerUVE(Scene::EntityUVE player, Scene::EntityUVE skeleton);
    /// A .uvanim dragged from Content onto the Timeline joins the player's list.
    void AcceptTimelineClipDropUVE(Scene::EntityUVE player);
    /// The Entity Editor's Anim Graph: the AnimationGraph it shows as boxes and wires, the view
    /// onto the canvas, and what the mouse is doing to it.
    struct AnimationGraphViewStateUVE final {
        Scene::EntityUVE tree = Scene::kInvalidEntityUVE;
        /// Canvas point at the panel's top-left, and pixels per canvas unit.
        float panX = -40.0F;
        float panY = -120.0F;
        float zoom = 1.0F;
        bool framed = false;
        std::vector<std::uint32_t> selected;
        /// A drag of the selected objects: the graph before it, restored and re-applied as one
        /// undo step on release.
        bool draggingObjects = false;
        std::vector<Scene::AnimationGraphNodeUVE> dragBefore;
        /// A wire being drawn from this object's output (0: none).
        std::uint32_t wireFrom = 0U;
        bool boxSelecting = false;
        float boxFromX = 0.0F;
        float boxFromY = 0.0F;
        /// Where the Add Object menu will place the object, in canvas units.
        float addAtX = 0.0F;
        float addAtY = 0.0F;
        std::string addSearch;
        std::string status;
        /// The tree runs on the entity's skeleton while the tab is open: pose only, never saved.
        bool previewing = true;
        /// Preview transport: held, one frame asked for, and how fast it runs (1 is real time); the
        /// preview's own clock, for the log.
        bool previewPaused = false;
        bool previewStepOnce = false;
        float previewRate = 1.0F;
        double previewClock = 0.0;
        /// What the preview did, newest last: clip events and state changes, with when.
        struct PreviewLogLineUVE final {
            double at = 0.0;
            std::string text;
            bool stateChange = false;
        };
        std::vector<PreviewLogLineUVE> previewLog;
        /// Each state machine's active state at the last step, by object id, to see it change.
        std::unordered_map<std::uint32_t, std::uint32_t> previewActive;
        /// Recent values of each Float parameter, oldest first, for its sparkline.
        std::unordered_map<std::string, std::vector<float>> parameterHistory;
        Scene::EntityUVE previewSkeleton = Scene::kInvalidEntityUVE;
        /// A drag in the Blend Space 2D plot: -1 the position, 0.. a point, -2 none; and the tree
        /// before it, restored and re-applied as one undo step on release.
        int plotDrag = -2;
        Scene::AnimationGraphComponentUVE plotBefore;
        std::string boneSearch;
        /// The object opened in its own editor (a Blend Space or a State Machine), 0 for the graph.
        std::uint32_t focus = 0U;
        /// The State Machine view: its own pan and zoom (framed on first look), what is picked
        /// (a state slot or a transition index, -1 for none), a box being dragged (a state slot,
        /// kEntry/kAny, or -3 none) with the tree before it, a transition being drawn from a state
        /// (or Any, or Entry to set the entry state), and a transition setting being dragged.
        float statePanX = 0.0F;
        float statePanY = 0.0F;
        float stateZoom = 1.0F;
        bool stateFramed = false;
        std::uint32_t stateMachine = 0U;
        int pickedState = -1;
        int pickedTransition = -1;
        int stateDrag = -3;
        int linkFrom = -3;
        bool transitionDragging = false;
        Scene::AnimationGraphComponentUVE stateBefore;
        /// The Blend Space editor's tool (0 select and move, 1 add a point, 2 remove a point), its
        /// snapping, and the point whose animation is being picked (-1: none).
        int spaceTool = 0;
        bool snap = true;
        float snapStep = 0.1F;
        int pickClipForSlot = -1;
        /// An inline value on an object being dragged: the tree before it, for one undo step.
        bool inlineEditing = false;
        Scene::AnimationGraphComponentUVE inlineBefore;
        /// Clip file names by guid, for labels.
        std::unordered_map<std::uint64_t, std::string> clipNames;
        /// Clips the preview has read, by guid; null for one that could not be read.
        std::unordered_map<std::uint64_t, std::shared_ptr<const Asset::AnimationClipAssetUVE>> clips;
    };
    AnimationGraphViewStateUVE m_animGraph;
    /// Notes what the preview's last step did: clip events, state changes, parameter values.
    void RecordAnimationGraphPreviewUVE(const Scene::AnimationGraphComponentUVE& tree);
    /// Puts the previewed skeleton back at rest and the tree back at its start.
    void StopAnimationGraphPreviewUVE();
    /// A Blend Space opened in the Anim Graph's own editor: tools, snapping, the axes' areas, and
    /// points to add (picking their animation at once), move and remove.
    void DrawBlendSpaceEditorUVE(Scene::EntityUVE tree, std::size_t objectIndex);
    /// A State Machine opened in the Anim Graph: its states as boxes with Entry and Any, the
    /// transitions as arrows, drawn and edited in place, and what is running shown live.
    void DrawStateMachineViewUVE(Scene::EntityUVE tree, std::size_t objectIndex);
    /// The side strip while a State Machine is open: the picked state or transition's settings.
    void DrawStateMachineSelectionUVE(Scene::EntityUVE tree, std::size_t objectIndex,
                                      std::optional<std::function<void(Scene::AnimationGraphComponentUVE&)>>& edit);
    /// A clip's file name for labels, "" for none.
    [[nodiscard]] const std::string& AnimationClipNameUVE(Asset::AssetGuidUVE clip);
    /// The Anim Graph tab's body.
    void DrawAnimationGraphCanvasUVE();
    /// Changes the AnimationGraph's objects or parameters as one undo step, like an Inspector edit.
    /// False when nothing changed or the result is not a valid graph (the change is dropped).
    bool EditAnimationGraphUVE(Scene::EntityUVE tree,
                              const std::function<void(Scene::AnimationGraphComponentUVE&)>& change);
    /// In-flight automatic model imports, by content-relative source path.
    std::map<std::string, Asset::AssetImportJobIdUVE> m_modelImportJobs;
    /// Every content-relative model source, as the last project refresh read it.
    std::map<std::string, EditorModelSourceInfoUVE> m_modelSources;
    MeshThumbnailRendererUVE m_meshThumbnailRenderer;
    ContentBrowserTypeFocusUVE m_contentBrowserTypeFocus = ContentBrowserTypeFocusUVE::All;
    std::string m_assetFilter;
    /// One default-constructed instance per inspected component type, made on first use, so the
    /// Inspector can tell a changed value from a default one without constructing a component per
    /// row per frame.
    std::unordered_map<const Core::TypeMetadataEntryUVE*, Core::TypeInstanceUVE> m_inspectorDefaultInstances;
    /// Working copies of the text fields being edited (DrawCommittedTextInputUVE), keyed by widget
    /// id. Dear ImGui owns the text while a field is active; this is where it lands so it can be
    /// committed on the frame the field is let go. Per widget, because clicking from one field into
    /// another activates the second in the same frame the first reports it was let go.
    std::vector<std::pair<std::uint32_t, std::string>> m_inspectorTextEdits;
    std::string m_scriptQuickLoadFilter;
    /// Gathered when Quick Load opens rather than every frame it is open: it walks a folder.
    std::vector<std::string> m_scriptQuickLoadCandidates;
    std::string m_scriptLoadPath;
    /// The Load dialog's verdict on the path last checked, re-derived only when the path changes:
    /// checking reads and decodes the file.
    std::optional<std::string> m_scriptLoadCheckedPath;
    std::string m_scriptLoadProblem;
    std::optional<UVScriptDocumentUVE> m_openUVScript;
    /// A caret move the text editor applies on its next frame (1-based line), set by GoTo.
    std::uint32_t m_uvscriptJumpLine = 0U;
    /// The export rows the Inspector last drew, reused until the object, its script or its values
    /// change, or half a second passes (the file may have been edited).
    struct ScriptExportsCacheUVE final {
        Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
        std::string path;
        std::map<std::string, std::string> values;
        std::chrono::steady_clock::time_point builtAt{};
        std::vector<ScriptExportRowUVE> rows;
    };
    ScriptExportsCacheUVE m_scriptExportsCache;
    /// The Add Metadata popup's draft. The type is remembered between uses: the next property an
    /// author adds is most often the same kind as the last.
    std::string m_metadataAddName;
    std::string m_metadataTypeFilter;
    Core::VariantTypeUVE m_metadataAddType = Core::VariantTypeUVE::Bool;
    std::optional<Core::VariantUVE> m_metadataAddValue;
    /// Row being renamed, and its draft name.
    std::string m_metadataRenameKey;
    std::string m_metadataRenameDraft;
    /// A lossy retype awaiting confirmation: key and target type.
    std::optional<std::pair<std::string, Core::VariantTypeUVE>> m_metadataPendingRetype;
    /// Commits a new entry list for the selected object's metadata: the single write path.
    /// With `preview`, the list is shown without history, as part of a drag.
    [[nodiscard]] bool CommitSelectedObjectMetadataUVE(std::vector<Scene::ObjectMetadataEntryUVE> entries,
                                                     bool preview = false);
    [[nodiscard]] bool WriteSelectedObjectMetadataValueUVE(const std::string& key, const Core::VariantUVE& value,
                                                         bool preview);
    /// Draws an editor for one Variant; returns true when the value changed and should be committed.
    bool DrawVariantValueEditorUVE(const char* id, Core::VariantUVE& value, int depth);
    std::string m_consoleFilter;
    std::string m_consoleCommand;
    std::string m_hierarchyFilter;
    std::string m_cachedHierarchyFilter;
    std::vector<Scene::EntityUVE> m_cachedHierarchyVisibleEntities;
    Scene::EntityUVE m_hierarchyRenameEntity = Scene::kInvalidEntityUVE;
    std::string m_hierarchyRenameBuffer;
    bool m_hierarchyFilterCacheDirty = true;
    bool m_hierarchyRenameFocusRequested = false;
    // The Add Object picker: a small floating box with a search field, opened from the Scene panel's
    // + button and from a row's "Add Child Object". The request is a flag so either caller can ask
    // for it from inside its own popup and the picker still opens in the panel's ID scope.
    bool m_objectPickerOpenRequested = false;
    // True while the orthographic projection came from a named view rather than an explicit choice.
    bool m_viewportOrthographicIsAutomatic = false;
    // Reveal-on-select: when the active selection changes, the hierarchy opens the rows above it
    // and scrolls it into view once, so an object picked in the viewport or just added is never
    // hidden in a collapsed branch. Once shown, the user is free to collapse it again.
    Scene::EntityUVE m_hierarchyRevealedEntity = Scene::kInvalidEntityUVE;
    std::vector<Scene::EntityUVE> m_hierarchyRevealAncestors;
    bool m_hierarchyRevealPending = false;
    // Expand Branch / Collapse Branch: the open state each row in the branch should take the next
    // time it is drawn. A row is erased once applied (see SetHierarchyBranchOpenUVE).
    std::unordered_map<Scene::EntityUVE, bool> m_hierarchyPendingRowOpen;
    ColorPickerPreferencesUVE m_colorPickerPreferences;
    // The property edit in flight; see PreviewSelectedComponentPropertyUVE.
    struct ComponentPropertyPreviewUVE final {
        Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
        const Core::TypeMetadataEntryUVE* entry = nullptr;
        const Core::TypeMetadataPropertyUVE* property = nullptr;
        Core::TypeInstanceUVE before;
        EditorSelectionSnapshotUVE selectionBefore;
        bool dirtyBefore = false;
    };
    std::optional<ComponentPropertyPreviewUVE> m_componentPropertyPreview;
    std::optional<std::pair<std::string, std::string>> m_pendingAnimationParameterRename;
    std::string m_objectPickerFilter;
    std::string m_objectPickerScrolledFilter;
    std::optional<Asset::AssetRecordUVE> m_selectedAsset;
    /// Everything picked in Content (Ctrl and Shift click); m_selectedProjectFile is the last one.
    ContentSelectionUVE m_contentSelection;
    /// The items Content drew this frame and the frame before, in order: what Shift+click ranges over.
    std::vector<std::string> m_contentShownOrder;
    std::vector<std::string> m_contentShownOrderPrevious;
    std::optional<Asset::ProjectFileEntryUVE> m_selectedProjectFile;
    std::optional<Asset::ProjectFileEntryUVE> m_filesystemContextEntry;
    bool m_filesystemContextVisible = false;
    /// Content "+ Add": last five item ids used (persisted), the search text, and a request from
    /// a right-click on empty space to open the menu this frame.
    std::vector<std::string> m_contentCreateRecent;
    std::string m_contentCreateFilter;
    bool m_contentCreateMenuRequested = false;
    /// Where the Content menus sit once open; their top strip drags them. A menu opens above the
    /// pointer (anchorX/Y) and is held there for its first frames, until it has its real size.
    float m_contentCreateMenuX = 0.0F;
    float m_contentCreateMenuY = 0.0F;
    float m_contentCreateMenuAnchorX = 0.0F;
    float m_contentCreateMenuAnchorY = 0.0F;
    int m_contentCreateMenuFrames = 0;
    float m_contentItemMenuX = 0.0F;
    float m_contentItemMenuY = 0.0F;
    float m_contentItemMenuAnchorX = 0.0F;
    float m_contentItemMenuAnchorY = 0.0F;
    int m_contentItemMenuFrames = 0;
    /// The card being renamed inline (Content-relative), its text, and whether to focus it.
    std::filesystem::path m_contentRenamePath;
    std::string m_contentRenameText;
    bool m_contentRenameFocus = false;
    /// One line under the Content toolbar about the last action ("Entity Editor comes next").
    std::string m_contentStatusMessage;
    std::filesystem::path m_filesystemLongPressPath;
    float m_filesystemLongPressSeconds = 0.0F;
    bool m_projectFileSnapshotInitialized = false;
    bool m_projectFileLastRefreshSucceeded = true;
    std::uint64_t m_projectFileLastObservedChangeSequence = 0U;
    bool m_projectFileRefreshAttemptedForRescan = false;
    bool m_scenePanelVisible = true;
    bool m_inspectorPanelVisible = true;
    bool m_bottomDockVisible = true;
    /// The dock body's height, dragged from its top edge; the layout clamps it (kAssetsPanelHeightUVE default).
    float m_bottomDockHeight = 192.0F;
    std::array<char, 512> m_consoleInput{};
    bool m_consoleScrollToBottom = false;
    bool m_viewportPanelVisible = true;
    ViewportPanelRendererUVE m_viewportPanelRenderer;
    // UI/render state is isolated just like the three backend cameras and render targets. Main
    // remains the canonical settings-backed state; tool-window state is session-local.
    ViewportOverlayStateUVE m_viewportOverlayState;
    ViewportOverlayStateUVE m_entityViewportOverlayState;
    ViewportOverlayStateUVE m_retargetViewportOverlayState;
    Scene::EntityUVE m_previewCamera = Scene::kInvalidEntityUVE;
    bool m_sceneDirty = false;
    bool m_uiInitialized = false;
    EditorUiAssetsUVE m_uiAssets;
};

} // namespace UVE::Editor
