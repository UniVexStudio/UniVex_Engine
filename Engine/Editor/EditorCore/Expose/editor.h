// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "texture_import_settings.h"
#include <array>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <list>
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
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "skeleton_3d.h"
#include "blueprint_asset.h"
#include "i_asset_database.h"
#include "i_asset_import_queue.h"
#include "i_project_file_index.h"
#include "i_project_change_watcher.h"
#include "engine_services.h"
#include "document_scene.h"
#include "i_simulation_control.h"
#include "settings_observer.h"
#include "settings_registry.h"
#include "animation_clip_editing.h"
#include "editor_color.h"
#include "editor_commands.h"
#include "editor_audio_mixer.h"
#include "editor_settings.h"
#include "editor_viewport_input.h"
#include "animation_library_asset.h"
#include "motion_montage_asset.h"
#include "montage_sequencer_component.h"
#include "shader_asset.h"
#include "gltf_skeleton.h"
#include "editor_asset_table_model.h"
#include "editor_entity_diff.h"
#include "editor_retarget_plan.h"
#include "editor_hierarchy_view.h"
#include "input_action.h"
#include "editor_tool_session.h"
#include "developer_console.h"
#include "editor_ui_assets.h"
#include "inspector_drawer_registry.h"
#include "type_metadata.h"
#include "scene_component_metadata.h"
#include "mesh_thumbnail_renderer.h"
#include "material_thumbnail_renderer.h"
#include "vector2.h"
#include "vector3.h"
#include "animation_sequencer_component.h"
#include "animation_graph_component.h"
#include "audio_source_component.h"
#include "camera_component.h"
#include "canvas_component.h"
#include "character_controller_component.h"
#include "collider_component.h"
#include "auto_translate_component.h"
#include "editor_description_component.h"
#include "object_metadata_component.h"
#include "process_component.h"
#include "thread_group_component.h"
#include "world_transform_component.h"
#include "profiler_diagnostics.h"
#include "network_packet_capture.h"
#include "all_objects_3d.h"
#include "light_component.h"
#include "mesh_component.h"
#include "morph_target_component.h"
#include "skin_component.h"
#include "particle_emitter_component.h"
#include "particle_system_asset.h"
#include "vfx_program.h"
#include "vfx_templates.h"
#include "vfx_world.h"
#include "vfx_draw_list.h"
#include "physics_interpolation_component.h"
#include "primitive_mesh_component.h"
#include "rigid_3d_component.h"
#include "script_component.h"
#include "transform_component.h"
#include "ui_button_component.h"
#include "ui_image_component.h"
#include "ui_text_component.h"
#include "scene_object_registry.h"
#include "entity.h"
#include "i_scene_serializer.h"
#include "blueprint_ast.h"
#include "blueprint_value.h"

namespace UVE::Asset {
struct AnimationClipAsset;
class AssetImportQueue;
} // namespace UVE::Asset

namespace UVE::Editor::Tests {
struct EditorAccess;
}

namespace UVE::Editor {
struct AnimationIconWorker;

struct EditorSettingBinding;

class EditorBridge;

/// EditorState is the lifecycle state of one editor session. The editor owns session data only;
/// EngineCore remains the owner of the ECS, renderer, window, and every engine service.
enum class EditorState {
    Uninitialized,
    Running,
    Shutdown,
};

/// The editor-owned transient simulation state. Edit permits authored document commands; Playing
/// and Paused own an immutable pre-Play document snapshot and reject authoring mutations.
enum class EditorPlayModeState {
    Edit,
    Playing,
    Paused,
};

/// Editor-only 2D canvas presentation state for authored screen-space content such as loading
/// screens. The design surface is not an ECS entity, is never serialized into a scene, and never
/// changes runtime state. Pan is expressed in desktop pixels relative to the fitted canvas center.
struct Editor2DCanvasState final {
    static constexpr float kDesignWidth = 1920.0F;
    static constexpr float kDesignHeight = 1080.0F;

    float zoom = 0.36F;
    Math::Vector2 pan{};
    bool gridVisible = true;
    bool safeAreaVisible = true;
};

/// Canonical named axes used by Editor's Translate, Rotate, and Scale gizmos. The active
/// coordinate space chooses whether their world or selected-entity-local basis is used.
enum class EditorTransformAxis {
    None,
    X,
    Y,
    Z,
};

/// Source-compatible name retained for downstream editor callers; new code should use the
/// transform-wide name because the same axis contract is shared by Translate, Rotate, and Scale.
using EditorTranslateAxis = EditorTransformAxis;

/// Selects whether hierarchy reparenting retains authored local TRS or preserves compatible
/// captured world TRS. This editor-session preference is never serialized or added to history.
enum class EditorReparentTransformMode {
    KeepLocal,
    KeepWorld,
};

/// Session-local transform snapping settings. These values are editor-only and are not serialized
/// into scene documents or runtime state.
struct EditorTransformSnappingSettings final {
    /// Moves and rotations snap to their steps.
    bool enabled = false;
    /// Scales snap to `scaleStep`: switched on its own (its own magnet in the view toolbar).
    bool scaleEnabled = false;
    float translateStep = 1.0F;
    float rotateStepDegrees = 15.0F;
    float scaleStep = 0.1F;
};

/// Where a newly created 3D object appears: at its parent's origin, or at the point the viewport
/// camera orbits - what the person is looking at.
enum class EditorNewObjectPlacement {
    ParentOrigin,
    ViewFocus,
};

/// Where an object moves among its siblings: one place up or down, or to either end.
enum class EditorSiblingMove {
    Up,
    Down,
    ToTop,
    ToBottom,
};

/// One stored editor-viewport pose, expressed in orbit-camera terms (pivot target, yaw/pitch,
/// orbit distance) so the value is independent of any concrete camera implementation - the app
/// host applies it through its OrbitCamera's SetTarget/SetYawPitch/SetDistance. Session-local and
/// never serialized (persisting these across runs needs a settings schema this increment does not
/// define - real, separate follow-up, the same call other editors make for its own Ctrl+0..9 viewport
/// bookmarks when they are stored only in per-user editor settings anyway).
struct EditorViewportBookmark final {
    Math::Vector3 target{};
    float yawRadians = 0.0F;
    float pitchRadians = 0.0F;
    float distance = 10.0F;
};

/// The numeric bookmark slots following the common editor convention (Ctrl+digit stores,
/// plain digit restores).
inline constexpr std::size_t kEditorViewportBookmarkSlotCount = 10U;

/// The orbit distance fly-to-marker bookmarks are composed at: near enough to actually frame the
/// subject the marker stares at, far enough to keep the near-clip out of trouble. A bookmark is
/// trivially dollied afterwards, so this is only ever a starting point.
inline constexpr float kEditorMarkerFocusDistance = 5.0F;

/// A read-only oriented box for the selected collider-backed document entity. All points are in
/// derived world space and are intended for editor feedback only; this value is never serialized.
struct EditorSelectionBounds final {
    std::array<Math::Vector3, 8> worldCorners{};
    Math::Vector3 worldCenter{};
};

/// The supported Library workspace archetypes. Each created entity is a document root with a
/// TransformComponent; specialized kinds add only the named gameplay or built-in primitive component.
enum class EditorSceneComponentKind : std::uint8_t {
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

using EditorSceneComponentValue =
    std::variant<Scene::CameraComponent, Scene::MeshComponent, Scene::LightComponent,
                 Scene::ColliderComponent, Scene::Rigid3DComponent, Scene::AudioSourceComponent,
                 Scene::ParticleEmitterComponent, Scene::ScriptComponent,
                 Scene::AnimationSequencerComponent, Scene::WorldEnvironment3DComponent,
                 Scene::CharacterControllerComponent, Scene::CanvasComponent, Scene::UITextComponent,
                 Scene::UIImageComponent, Scene::UIButtonComponent,
                 Scene::PhysicsInterpolationComponent, Scene::EditorDescriptionComponent, Scene::ProcessComponent,
                 Scene::ThreadGroupComponent, Scene::AutoTranslateComponent,
                 Scene::ObjectMetadataComponent>;

enum class EditorEntityKind {
    Empty,
    Camera,
    DirectionalLight,
    CollisionBox,
    Cube,
    UVSphere,
    Plane,
};

/// The editor's word for each entity kind, as the bridge protocol and starter scenes spell it.
/// "object3d" is the current word for Empty; "empty" and "node3d" are older names still accepted.
[[nodiscard]] std::optional<EditorEntityKind> ParseEditorEntityKind(std::string_view word) noexcept;

/// Editor composes the existing engine services into a first editor foundation: an editor-owned
/// camera, deterministic hierarchy and collider-backed viewport selection, a transform inspector
/// mutation path, world-space Translate, Rotate, and Scale gizmos, scene-document save/load, and the editor's
/// own UI. No UI toolkit type appears in this public interface, so the UI remains an
/// implementation detail of engine/editor.
///
/// The supplied EngineServices reference must remain valid from Init() through Shutdown().
/// Editor is main-thread only, matching the scene, render, and window services it composes.
/// What the editor read from a model source file (.gltf, .glb, .obj, .fbx) on the last project
/// refresh - enough to label it and decide whether it has a mesh to import.
struct EditorModelSourceInfo final {
    /// A mesh is skinned to bones: shown as Model rather than Mesh.
    bool rigged = false;
    /// The file has bones a Skeleton3D can take - skinned or not, so an animation file counts.
    bool hasSkeleton = false;
    /// A skeleton and its animation with no mesh: shown as Animation, and not imported as a mesh.
    bool animationOnly = false;
    /// A one-line description for the Asset Table tooltip ("162 bones, 1 animation, 0.27 s").
    std::string summary;
    /// Takes in the file: each is imported as a `.uvanim` clip beside it.
    std::size_t animationCount = 0U;
};

/// What placing a model source must do, decided BEFORE the first scene object exists.
///
/// The standard FBX pipeline never leaves an asset half-built: an import produces the skeletal mesh, its
/// skeleton and its materials together, or it fails and says why. This enum is that promise written
/// down in one place, because the state it rules out does not look like an error - a file whose
/// converted mesh is not written yet can still be read for its bones, and building those alone
/// produces an Armature that looks exactly like a deliberate animation-only import and never gains
/// a mesh afterwards. "Bones only" stays reachable, but only for a source that has no geometry at
/// all, which is the one case where it is the truth.
enum class ModelSourcePlacement : std::uint8_t {
    /// The converted mesh exists: place the model with its skeleton.
    PlaceModel = 0,
    /// The source holds no geometry and does hold motion: a skeleton and its clips, by design.
    PlaceAnimationOnly,
    /// The source holds geometry and its converted form is not written yet.
    WaitForImport,
    /// The source holds geometry and the import refused it; the editor holds the importer's reason.
    ImportFailed,
};

/// The placement decision for a model source. `convertedMeshExists` is whether the mesh the
/// importer writes (beside the import cache, not beside the source) is on disk; `source` is what the
/// last project refresh read from the file, or null when it has not read it; `importFailed` is
/// whether the editor recorded a failed job for it.
///
/// A converted mesh always wins: whatever the file holds, there is a mesh to draw. Otherwise only a
/// source that says it has no geometry may be placed as bones, and everything else waits or
/// reports - never guesses.
///
/// `inline` because this lives in a header several translation units include: without it every one
/// of them defines the same external symbol and the editor fails to link. It is here rather than in
/// a .cpp because it is a decision, not an implementation - a caller must be able to read the whole
/// table in one place.
[[nodiscard]] inline ModelSourcePlacement DecideModelSourcePlacement(bool convertedMeshExists,
                                                                          const EditorModelSourceInfo* source,
                                                                          bool importFailed) noexcept {
    if (convertedMeshExists) {
        return ModelSourcePlacement::PlaceModel;
    }
    if (source != nullptr && source->animationOnly) {
        return ModelSourcePlacement::PlaceAnimationOnly;
    }
    if (importFailed) {
        return ModelSourcePlacement::ImportFailed;
    }
    return ModelSourcePlacement::WaitForImport;
}

/// The Retarget window's state: the animations picked in Content, the character they are for, what
/// conforming would do, and Generate's progress.
struct RetargetWindowState final {
    /// The animations to conform, absolute paths.
    std::vector<std::filesystem::path> animations;
    /// The character: a content-relative model source (its imported model is conformed) or a `.uvmodel`.
    std::filesystem::path target;
    RetargetPlan plan;
    bool planStale = true;
    /// Counts plans made, so a window showing one knows when it changed.
    std::size_t planSerial = 0U;
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
    std::future<Retarget::RetargetFilesResult> job;
    /// The character file the running job conforms.
    std::filesystem::path jobModel;
    /// True when the running job conformed the character mesh itself (not only its animations).
    bool jobConformedModel = false;
    /// Un-imported animation model sources queued from a folder/multi-select retarget, drained
    /// incrementally within a per-frame time budget so the editor stays responsive.
    std::deque<std::filesystem::path> pendingFolderImports;
    std::size_t pendingImportTotal = 0U;
    std::size_t pendingImportDone = 0U;
    /// Background plan scan for large animation batches so opening a 1,000-clip folder never stalls UI.
    std::future<RetargetPlan> planJob;
    std::shared_ptr<std::atomic<bool>> planCancel = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<Progress> planProgress = std::make_shared<Progress>();
    std::filesystem::path planModelFile;
    /// True when an empty target should auto-pick the first project character; false for explicit
    /// animation-only multi-select or folder batches.
    bool autoSelectDefaultCandidate = true;
    /// Characters conformed or restored while the window was open: the scene put aside by the
    /// preview still holds their old bones until it is back.
    std::vector<std::filesystem::path> changedModels;
    bool focusPending = true;
};

/// The Animation Library window's state: one open `.uvanimlib`, saved on every edit.
struct AnimationLibraryWindowState final {
    /// The library file (absolute path).
    std::filesystem::path assetPath;
    bool focusPending = true;
    /// The library as last loaded or saved. Every mutation writes the file at once.
    Asset::AnimationLibraryAsset library;
    /// The last result, one line.
    std::string status;
    bool statusIsError = false;
    /// A request to rename an entry (its index), and the name the popup edits.
    std::optional<std::size_t> renameEntryRequested;
    std::string entryName;
};

/// Open an existing file, pick a folder, or name a new one.
enum class FilePickerMode {
    Open,
    Save,
    OpenFolder,
};

/// One floating file pick: what it picks and where the choice goes. `extensions` holds the
/// dotted kinds to offer (".uvanimlib"); empty offers every file. `startDirectory` is
/// content-relative (empty is Content itself); `onPick` fires once with the absolute choice, or
/// never when the pick is cancelled.
/// The system's own file picker for Import (Android's file manager), set by the platform host; when
/// it is set, Import opens it directly instead of the editor's file dialog. It calls `onPicked` with
/// the chosen file; false when it could not open.
using SystemImportPicker = std::function<bool(std::function<void(const std::filesystem::path&)> onPicked)>;
void SetSystemImportPicker(SystemImportPicker picker);
/// Multi-file system picker (Android SAF ACTION_OPEN_DOCUMENT + EXTRA_ALLOW_MULTIPLE); calls `onPicked`
/// with one or more staged file paths in app cache.
using SystemImportFilesPicker = std::function<bool(std::function<void(const std::vector<std::filesystem::path>&)> onPicked)>;
void SetSystemImportFilesPicker(SystemImportFilesPicker picker);
/// Folder system picker (Android SAF ACTION_OPEN_DOCUMENT_TREE); calls `onPicked` with staged folder paths.
using SystemImportFolderPicker = std::function<bool(std::function<void(const std::vector<std::filesystem::path>&)> onPicked)>;
void SetSystemImportFolderPicker(SystemImportFolderPicker picker);

struct FilePickerRequest final {
    FilePickerMode mode = FilePickerMode::Open;
    std::string title;
    std::vector<std::string> extensions;
    std::filesystem::path startDirectory;
    std::string saveName;
    /// Quick Import: the picked file is copied into a Content folder first, and the copy is picked.
    bool quickImport = false;
    /// True when the dialog permits selecting multiple files at once and invoking `onPickMany`.
    bool allowMultiSelect = false;
    std::function<void(const std::filesystem::path&)> onPick;
    std::function<void(const std::vector<std::filesystem::path>&)> onPickMany;
};

/// The picker's live state: the request plus its navigation.
struct FilePickerState final {
    FilePickerRequest request;
    /// Content-relative; empty is Content itself.
    std::filesystem::path directory;
    /// The chosen file, content-relative.
    std::optional<std::filesystem::path> selectedFile;
    std::string search;
    std::string saveName;
    /// The last result, one line.
    std::string status;
    bool statusIsError = false;
    bool focusPending = true;
};

/// The Entity Class picker's state: where the body goes, what is picked, and the last line it said.
/// The list itself is the catalogue's.
struct EntityClassPickerState final {
    /// Absolute: where the body's `.uventity` will be written.
    std::filesystem::path directory;
    std::string search;
    /// The catalogue id of the picked body; empty picks nothing.
    std::string pickedItemId;
    std::string status;
    bool statusIsError = false;
    bool focusPending = true;
};

/// The picker's listing for one folder: content-relative subfolders and matching files, sorted.
struct FilePickerListing final {
    std::vector<std::filesystem::path> folders;
    std::vector<std::filesystem::path> files;
};

class EditorShell;
class EditorAssetTable;
class EditorDetails;
class EditorViewportGizmos;

/// The Entity Editor's running Simulate (editor_entity_window.cpp).
struct EntitySimulation;

class Editor final {
    friend struct Tests::EditorAccess;
    friend class EditorBridge;
    friend class EditorShell;
    friend class EditorShortcutsWindow;
    friend class EditorInputMapWindow;
    friend class EditorNetworkCaptureWindow;
    friend class EditorAudioMixerWindow;
    friend class PacketList;
    friend class PacketDetail;
    friend class MixerTable;
    friend class EditorAssetTable;
    friend class EditorDetails;
    friend class EditorViewportGizmos;
    friend class EditorDockPanels;
    friend class EditorViewToolbar;
    friend class EditorEntityEditor;
    friend class EditorVfxEditor;
    friend class EditorSequencerPanel;
    friend class KeyframeView;
    friend class EditorAnimGraphPanel;
    friend class StateMachineView;
    friend class BlendSpaceView;
    friend struct AnimationIconWorker;
    friend class EditorFileDialog;
    friend class AssetThumbnail;
    friend class EditorObjectMenu;
    friend class EditorEntityClassWindow;
    friend class EditorMontageEditor;
    friend class EditorBlueprintPanel;
    friend class EditorRetargetWindow;
    friend class EditorAnimationLibraryWindow;

public:
    explicit Editor(Core::EngineServices& services,
                       std::filesystem::path activeScenePath = "editor_scene.uvscene",
                       std::size_t historyCapacity = 100U,
                       Core::ISimulationControl* simulationControl = nullptr);
    ~Editor();

    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    /// Creates the non-document editor camera and initializes the private UI backend when a real
    /// native window is available. Safe in headless mode: hierarchy, inspector, picking helpers,
    /// transform editing, and persistence logic remain usable while UI rendering is disabled.
    void Init();

    /// Selects the PRISM editor shell instead of the previous UI backend. Must be called before
    /// Init(); `assetDir` holds PRISM's fonts, themes and locale tables.
/// What the loading screen shows: the start of the editor (it covers the window) or work still
    /// running after it (a small panel in a corner).
    struct EditorLoadingState final {
        bool startup = false;
        bool busy = false;
        bool cancellable = false;
        float progress = 0.0F;
        std::size_t done = 0U;
        std::size_t total = 0U;
        std::string step;
        std::string detail;
        std::string currentItem;
    };
    [[nodiscard]] EditorLoadingState GetLoadingState() const;
    void DismissStartupLoading() noexcept { m_startupLoading = false; }
    /// Cancels queued background asset copies, probes, model conversions, and animation extractions
    /// after the single active item finishes atomically.
    void CancelBackgroundImports();
    /// Synchronously polls the project Content directory for externally added, modified, or removed
    /// files and refreshes the project file index when changes are detected.
    bool PollExternalContentChangesNow();
    void SetPrismUiEnabled(bool enabled, std::filesystem::path assetDir = {});
    void SetProjectFilePath(std::filesystem::path projectFile);
    void SetRendererProfile(Platform::RendererProfile profile);
    [[nodiscard]] Render::RendererProfileResolution GetRendererProfileResolution() const noexcept;
    void SetSafeAreaCallback(std::function<std::array<float, 4>()> callback);
    [[nodiscard]] std::array<float, 4> SafeAreaInsets() const;
    [[nodiscard]] bool IsPrismUiActive() const noexcept { return m_levelShell != nullptr; }
    /// Saves the current PRISM editor frame as PNG (false when the PRISM shell is not active).
    bool SavePrismScreenshot(const std::filesystem::path& path);
    /// Saves the open Entity Editor window as PNG (false when no entity is open in its window).
    bool SaveEntityEditorScreenshot(const std::filesystem::path& path);

    /// Validates a possibly deleted selection, cancels an invalid gizmo drag, and performs
    /// non-rendering per-frame maintenance.
    void Tick();

    /// Captures the complete editable document into an in-memory scene envelope and enters the
    /// transient Play sandbox. Returns false without document mutation when capture or Core control fails.
    [[nodiscard]] bool EnterPlayMode();
    [[nodiscard]] bool PausePlayMode();
    [[nodiscard]] bool ResumePlayMode();
    [[nodiscard]] bool StepPlayMode();
    [[nodiscard]] bool StopPlayMode();
    [[nodiscard]] EditorPlayModeState GetPlayModeState() const noexcept;
    [[nodiscard]] bool SetPlayFastForward(bool enabled) noexcept;
    [[nodiscard]] bool IsPlayFastForwardEnabled() const noexcept { return m_playFastForwardEnabled; }
    void SetPlayFastForwardMultiplier(float multiplier) noexcept;
    [[nodiscard]] float GetPlayFastForwardMultiplier() const noexcept { return m_playFastForwardMultiplier; }
    void SetGameMouseCaptured(bool captured);
    [[nodiscard]] bool IsGameMouseCaptured() const noexcept { return m_gameMouseCaptured; }
    /// A viewport is flying (right mouse held: mouse looks, WASD moves): the pointer is hidden and
    /// held, and keys drive the camera instead of the editor's shortcuts.
    void SetViewportFlying(bool flying);
    /// Whether any editor window (the level's, the Entity Editor's, a tool window) has focus: the
    /// editor counts as in use, and keeps its full frame rate, while any of them has.
    [[nodiscard]] bool IsAnyEditorWindowFocused() const;
    [[nodiscard]] bool IsViewportFlying() const noexcept { return m_viewportFlying || m_viewportFlyHeld; }
    /// The right mouse is held on a viewport (flying, or about to): its keys are the camera's.
    void SetViewportFlyHeld(const bool held) noexcept { m_viewportFlyHeld = held; }

    /// Draws the private editor overlay. EngineCore invokes this from its post-render callback
    /// before Present(), after the HDR scene has passed through the standard tone-mapping path.
    void RenderOverlay();

    /// The 4 standard transform-tool modes a Viewport overlay toolbar exposes, named generically
    /// (not tied to any specific renderer's own enum) so EditorCore stays engine/viewport-agnostic
    /// - see ViewportRenderCallback's own doc comment below for why.
    enum class ViewportGizmoMode {
        Move,
        Rotate,
        Scale,
        Universal,
    };

    /// The viewport's named camera views. User is any free orbit; the others look along a world
    /// axis at the orbit target (Top looks down -Y, Front looks down -Z, Right looks down -X).
    enum class ViewportView {
        User,
        Top,
        Bottom,
        Front,
        Back,
        Right,
        Left,
    };

    /// Current state of the Viewport panel's own overlay toolbar (projection mode, active gizmo
    /// tool, snap, grid) - owned and mutated by Editor's own overlay-drawing code, then handed
    /// to each dedicated ViewportRenderCallback each frame so the concrete viewport can apply
    /// it to its own real projection/gizmo-mode/grid state. Kept as plain enums/bools with no
    /// viewport-module type in sight, for the same reason.
    /// One axis colour as plain RGB in 0..1 - see ViewportOverlayState::axisColorX for why this
    /// is a local struct rather than the viewport's own palette type.
    struct ViewportAxisColor final {
        float r = 0.0F;
        float g = 0.0F;
        float b = 0.0F;
    };

    /// One Skeleton3D bone in world space, for the viewport to draw (see BoneShape in the viewport
    /// module for the look). Plain floats for the same reason as the rest of the overlay state.
    struct ViewportBone final {
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
        /// Only the joint (a twist bone on the way down its limb): no blade of its own.
        bool jointOnly = false;
        /// A helper (root, IK target, socket): a marker, tied to its parent by a dashed line.
        bool helper = false;
    };

    /// One world-space segment of a collider wireframe, as the viewport renderer wants it: plain
    /// float triples, because EditorCore does not link the viewport's math types.
    struct ViewportColliderLine final {
        std::array<float, 3> from{};
        std::array<float, 3> to{};
        /// This segment belongs to the selected object's collider. The overlay marks it rather than
        /// colouring it, because what a highlight looks like is the viewport's decision.
        bool highlighted = false;
        /// When true, the viewport renders occluded segments with faint x-ray alpha instead of
        /// hiding them completely (used for ragdoll bone capsules/joints inside a character mesh).
        bool xray = false;
        /// Optional RGB colour override (0..1) for distinguishing active ragdoll bone vs joint cones.
        bool hasColour = false;
        std::array<float, 3> colour{};
    };

    struct ViewportOverlayState final {
        bool orthographic = false;
        // The named view the camera is in (User once it is orbited freely), and a counter bumped
        // on every request to move to one. The host applies a request when the counter changes,
        // so picking the same view twice still re-snaps, and nothing has to be "consumed".
        ViewportView view = ViewportView::User;
        std::uint32_t viewRequestSerial = 0U;
        // A request to bring an object into view (F over the viewport, or Focus in Viewport on a
        // hierarchy row), applied by the host when the counter changes, like the view request.
        Scene::Entity focusEntity = Scene::kInvalidEntity;
        std::uint32_t focusRequestSerial = 0U;
        ViewportGizmoMode gizmoMode = ViewportGizmoMode::Universal;
        bool snapEnabled = false;
        bool gridVisible = true;
        // How strongly the grid is drawn, 0.1..1. Persisted with gridVisible; see SetViewportGrid.
        float gridOpacity = 1.0F;
        // The smallest grid square, in world units; see SetViewportGridCellSize.
        float gridCellSize = 1.0F;
        // The outline drawn around selected meshes; see SetViewportSelectionOutline.
        bool selectionOutlineVisible = true;
        ViewportAxisColor selectionOutlineColor{1.0F, 0.62F, 0.16F};
        float selectionOutlineThickness = 2.0F;
        // True while the Game workspace tab is active (see EditorWorkspace::Game): the concrete
        // renderer should hide editor-only overlays (grid, transform gizmo) in this mode, matching
        // Unity's own Scene/Game split, since Game is meant to preview what a player would see.
        bool gameWorkspaceActive = false;
        // True while the pointer is over one of the overlay toolbar's own bubble buttons.
        //
        // The bubbles float on top of the rendered image, so the view's own hover is equally true
        // over a button and over the scene - which made clicking "Move" also register as a click on
        // empty space and clear the selection. This carries the answer to the renderer instead.
        bool pointerOverOverlay = false;

        // The entity context toolbar (right-click an entity -> a small "Scripting" bubble anchored
        // at its projected screen position). The world->screen projection needs OrbitCamera, which
        // lives in Engine/Editor/Viewport - a module EditorCore may not depend on - so main.cpp
        // computes the anchor pixel each frame and pushes it in via SetEntityContextToolbarAnchor
        // before RenderOverlay runs that same frame; unlike gizmoMode/orthographic/etc above,
        // this direction has no one-frame lag; entityContextToolbarOpen only goes false again
        // through ClearEntityContextToolbar (a miss) or DrawEntityContextToolbar consuming a
        // click, both driven by this same class.
        bool entityContextToolbarOpen = false;
        Scene::Entity entityContextToolbarEntity = Scene::kInvalidEntity;
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
        // seeds them once at startup through SetViewportAxisColors. Until it does, the flag
        // tells the renderer to keep its own defaults rather than apply three zeroes and paint
        // every axis black.
        ViewportAxisColor axisColorX{};
        ViewportAxisColor axisColorY{};
        ViewportAxisColor axisColorZ{};
        bool axisColorsValid = false;

        // Every enabled, visible Skeleton3D's bones, rebuilt each frame; empty in the Game workspace.
        std::vector<ViewportBone> bones;
        /// The standard "Show Collision": every collider in the document drawn as its actual shape, not
        /// as its bounding box. Off by default and session-only, like the equivalent toggle in every
        /// engine - it is a diagnostic, not a project setting, so it must not survive a relaunch and
        /// silently ship in some else's build.
        bool collisionShapesVisible = false;
        /// This frame's wireframe, rebuilt only while `collisionShapesVisible` is set.
        std::vector<ViewportColliderLine> colliderLines;

        // A studio view for presenting rather than editing (the Retarget window): sky, ground and
        // a faded floor instead of the grid, no toolbar or corner gizmo, no selection, and a
        // camera that faces the front and does not turn (the wheel still zooms). The camera is
        // placed on `studioTarget`, far enough to fit `studioRadius`, each time the serial changes.
        bool studioView = false;

        // The Entity Editor's viewport: one entity, authored in isolation. It is a 3D object view
        // with the level's camera controls, not a level, so level-only controls (such as the
        // preview sun/environment that lights an unlit scene) are hidden there. Set by the panel
        // drawing the viewport, never authored by the user.
        bool entityAuthoringView = false;
        std::array<float, 3> studioTarget{0.0F, 1.0F, 0.0F};
        float studioRadius = 1.5F;
        std::uint32_t studioFramingSerial = 0U;
        int shadingMode = 0;
        bool physicsPaused = false;
        bool previewSun = true;
        bool previewEnvironment = true;
        float previewSunAzimuth = 45.0F;
        float previewSunAltitude = 45.0F;
        float previewSunEnergy = 1.0F;
        std::array<float, 3> previewSunColor{1.0F, 1.0F, 1.0F};
        std::array<float, 3> previewSkyColor{0.31F, 0.54F, 0.91F};
        std::array<float, 3> previewHorizonColor{0.78F, 0.86F, 0.94F};
        std::array<float, 3> previewGroundColor{0.24F, 0.21F, 0.18F};
        /// How much of the preview sky and ground bands light the scene, matching
        /// WorldEnvironment3DComponent::ambientEnergy so "Add Environment to Scene" copies it.
        float previewAmbientEnergy = 1.0F;
        /// This frame's pointer/keyboard input for the view being rendered, filled by the editor
        /// shell right before it calls the viewport renderer (see editor_viewport_input.h).
        EditorViewportInput input;

        /// Viewport render quality preset: 0=Auto, 1=Low, 2=Medium, 3=High, 4=Ultra.
        int qualityPreset = 0;
        /// Output: effective 3D render resolution scale (0.5..1.0; 0.0 until first resolved).
        mutable float resolvedRenderScale = 0.0F;

        /// On-screen Android virtual viewport controller inputs for this frame.
        float touchMoveX = 0.0F;
        float touchMoveY = 0.0F;
        float touchMoveUp = 0.0F;
        float touchLookX = 0.0F;
        float touchLookY = 0.0F;
        float touchZoomDelta = 0.0F;

        /// Output: active 2D marquee box-selection rectangle in viewport-local logical coordinates.
        mutable bool marqueeActive = false;
        mutable float marqueeStartX = 0.0F;
        mutable float marqueeStartY = 0.0F;
        mutable float marqueeEndX = 0.0F;
        mutable float marqueeEndY = 0.0F;

        // ---- Gizmos drawn by the editor shell (PRISM) ------------------------------------------
        /// Set by a shell that draws the viewport's gizmos itself: the renderer then draws none of
        /// its own (corner navigation, transform handles, object gizmos, bones, colliders), nor the
        /// grid or the selection outline, and leaves their pointer handling to the shell. It still
        /// renders the scene, and reports the camera, gizmo, grid, depth and selection data below.
        bool hostDrawsGizmos = false;
        /// Orbit the view by this many pixels (the same units as a mouse orbit drag); the renderer
        /// applies it once and clears it. Used by the shell's navigation gizmo.
        float orbitRequestX = 0.0F;
        float orbitRequestY = 0.0F;

        /// Output: the camera the view was rendered with, in world space. The output fields are
        /// written by the renderer through the const state it is handed (hence mutable).
        struct ViewCamera final {
            bool valid = false;
            std::array<float, 3> eye{};
            std::array<float, 3> right{1.0F, 0.0F, 0.0F};
            std::array<float, 3> up{0.0F, 1.0F, 0.0F};
            std::array<float, 3> forward{0.0F, 0.0F, -1.0F};
            float fovYRadians = 0.87F;
            float nearPlane = 0.1F;
            bool orthographic = false;
            float orthographicHalfHeight = 5.0F;
            float width = 0.0F;
            float height = 0.0F;
        };
        mutable ViewCamera viewCamera;
        /// Output: where the selection's transform gizmo sits (world space), when there is one.
        mutable bool gizmoPivotValid = false;
        mutable std::array<float, 3> gizmoPivot{};
        /// Output: the object gizmos the renderer built for this view (camera frustums, light
        /// directions, fog volumes, probes, partitions, visibility regions, occluders, colliders,
        /// bones), as world-space lines and triangles for the shell to draw.
        struct GizmoLine final {
            std::array<float, 3> a{};
            std::array<float, 3> b{};
            std::array<float, 3> color{};
            float widthPx = 2.0F;
            /// Hidden where the scene is in front of it (a collision shape), instead of showing
            /// through faintly like a gizmo or a bone.
            bool occluded = false;
            /// Opacity of the part behind the scene when it is not `occluded`. Negative means the
            /// shell's default faint gizmo alpha. Ragdoll capsules live inside the character mesh, so
            /// they use a strong value: at the default they were all but invisible.
            float hiddenAlpha = -1.0F;
        };
        struct GizmoTriangle final {
            std::array<float, 3> a{};
            std::array<float, 3> b{};
            std::array<float, 3> c{};
            std::array<float, 3> color{};
            float alpha = 1.0F;
        };
        mutable std::vector<GizmoLine> gizmoLines;
        mutable std::vector<GizmoTriangle> gizmoTriangles;
        /// Output: the scene's depth under the view, as the distance along viewCamera.forward from
        /// viewCamera.eye, one float per texel, row 0 at the top (+1e30 where nothing was drawn).
        /// The buffer may be smaller than the view; it always covers all of it. Empty when the
        /// renderer had none this frame (the shell then draws without occlusion).
        mutable std::vector<float> sceneDepth;
        mutable int sceneDepthWidth = 0;
        mutable int sceneDepthHeight = 0;
        /// Output: the ground grid the shell draws, with the renderer's own settings.
        struct HostGrid final {
            bool visible = false;
            int plane = 0; // 0 XZ, 1 XY, 2 ZY
            float baseSpacing = 1.0F;
            float targetCellPixels = 24.0F;
            float lineWidth = 1.25F;
            float axisWidth = 1.6F;
            std::array<float, 3> thin{};
            std::array<float, 3> mid{};
            std::array<float, 3> thick{};
            float thinIntensity = 0.45F;
            float midIntensity = 0.70F;
            float thickIntensity = 0.95F;
            std::array<std::array<float, 3>, 3> axis{};
            float fadeStart = 12.0F;
            float fadeEnd = 45.0F;
            float opacity = 1.0F;
        };
        mutable HostGrid hostGrid;
        /// Output: the selection silhouette mask (one byte of weight per pixel, row 0 at the top,
        /// view sized), and the outline the shell draws around it.
        mutable std::vector<std::uint8_t> selectionMask;
        mutable int selectionMaskWidth = 0;
        mutable int selectionMaskHeight = 0;
        mutable int selectionOutlineRadius = 0;
        mutable std::array<float, 3> selectionOutlineRgb{};
    };

    /// Render callback for an individual editor 3D viewport: given the view's current available
    /// content-region size and overlay state (see ViewportOverlayState), renders into that
    /// viewport's own framebuffer at (at most) that size and returns a GL texture name for the
    /// editor's view to show, writing the actual rendered size back through outUsedSize. Returning
    /// 0 means "not ready yet" - nothing is drawn that frame. Deliberately free of any GL or
    /// viewport-module types so EditorCore stays engine/viewport-agnostic (this is an upper-layer
    /// module that may compose Engine/Runtime, not something that should link a sibling
    /// Engine/Editor module directly) - see Engine/App/Internal/editor/viewports/ for the concrete
    /// Engine/Editor/Viewport-backed implementations.
    using ViewportRenderCallback =
        std::function<std::uint64_t(const Math::Vector2& availableSize, Math::Vector2& outUsedSize,
                                    const ViewportOverlayState& overlayState)>;

    /// Physically separated per-window 3D viewport bindings. Each editor window owns its own
    /// independent camera, render target, and input state without routing through a shared
    /// dispatcher enum.
    struct EditorViewports final {
        /// The main Level Editor 3D viewport (`level_viewport`).
        ViewportRenderCallback level;
        /// The corner Picture-in-Picture preview for a selected Camera3D (`camera_preview_viewport`).
        ViewportRenderCallback cameraPreview;
        /// The Entity Editor's isolated authoring viewport (`entity_viewport`).
        ViewportRenderCallback entity;
        /// The Widget Editor Designer's 3D View preview (`widget_preview_viewport`).
        ViewportRenderCallback widgetPreview;
        /// The Retarget window's dual-character studio viewport (`retarget_viewport`).
        ViewportRenderCallback retarget;
        /// The Motion Montage Editor's skeletal preview viewport (`montage_viewport`).
        ViewportRenderCallback montage;
        /// The VFX / Particle Graph Editor's live particle preview viewport (`vfx_viewport`).
        ViewportRenderCallback vfx;
    };

    /// Replaces all seven per-window viewport callbacks at once.
    void SetEditorViewports(EditorViewports viewports);
    /// Clears all seven per-window viewport callbacks.
    void ClearEditorViewports();
    /// Returns the currently registered per-window viewport callbacks.
    [[nodiscard]] const EditorViewports& GetEditorViewports() const noexcept { return m_viewports; }

    /// Registers (or clears, with an empty callback) an individual editor window's 3D viewport.
    void SetLevelViewportRenderer(ViewportRenderCallback renderer);
    void SetCameraPreviewViewportRenderer(ViewportRenderCallback renderer);
    void SetEntityViewportRenderer(ViewportRenderCallback renderer);
    void SetWidgetPreviewViewportRenderer(ViewportRenderCallback renderer);
    void SetRetargetViewportRenderer(ViewportRenderCallback renderer);
    void SetMontageViewportRenderer(ViewportRenderCallback renderer);
    void SetVfxViewportRenderer(ViewportRenderCallback renderer);

    /// Makes a scene beside the level for an asset document (the Entity Editor's entity), so
    /// editing it never touches the level. The app passes EngineCore::CreateDocumentScene;
    /// without a factory the Entity Editor cannot open.
    using DocumentSceneFactory = std::function<std::unique_ptr<Core::DocumentScene>()>;
    void SetDocumentSceneFactory(DocumentSceneFactory factory) { m_documentSceneFactory = std::move(factory); }
    /// Where OpenFilePicker opens its window when the editor has a host for it (the PRISM file
    /// window); without one the built-in picker is used.
    using FilePickerHost = std::function<void(FilePickerRequest)>;
    void SetFilePickerHost(FilePickerHost host) { m_filePickerHost = std::move(host); }
    /// Called with a document scene's services just before that scene is destroyed, so whichever
    /// viewport draws it lets go first.
    using DocumentSceneClosing = std::function<void(const Core::EngineServices&)>;
    void SetDocumentSceneClosing(DocumentSceneClosing callback) { m_documentSceneClosing = std::move(callback); }

    /// The services of the document being worked on right now: the level's, or the open entity's
    /// inside an EntityDocumentScope. Active editor viewports render the scene these hold.
    [[nodiscard]] Core::EngineServices& GetActiveServices() noexcept { return *m_services; }
    /// True inside an EntityDocumentScope: the document is the open entity, not the level.
    [[nodiscard]] bool IsEntityDocumentActive() const noexcept { return m_entityDocumentActive; }

    /// While alive, the editor works on the open entity: its scene, selection, undo and dirty
    /// state replace the level's, and everything that reads "the document" reads the entity. The
    /// level's are put back when it ends. Does nothing while no entity is open, or when an outer
    /// scope already made the entity the document.
    class EntityDocumentScope final {
    public:
        explicit EntityDocumentScope(Editor& editor);
        ~EntityDocumentScope();
        EntityDocumentScope(const EntityDocumentScope&) = delete;
        EntityDocumentScope& operator=(const EntityDocumentScope&) = delete;
        [[nodiscard]] bool Active() const noexcept { return m_editor.m_entityDocumentActive; }

    private:
        Editor& m_editor;
        bool m_entered = false;
    };

    /// Saves every document root except the editor camera to the active .uvscene path. Dirty state
    /// is cleared only after the scene serializer reports success.
    [[nodiscard]] bool SaveScene();

    /// Saves everything with unsaved changes: the scene and the open UVScript. Returns how many
    /// were written; a failure is reported in the Content status line.
    std::size_t SaveAll();

    /// Saves the sole selected document subtree as a canonical `.uvprefab` and registers its source
    /// GUID through the existing PrefabSystem. This command never runs during Play or a viewport gesture.
    [[nodiscard]] bool SaveSelectedPrefab(const std::filesystem::path& path);

    /// Makes what the Content catalogue item `itemId` stands for inside `directory`: a folder, a
    /// `.uventity` holding the item's object tree, or a `.uvscene` with a SceneRoot and a Viewport -
    /// 2D or 3D, as the item asks, and recorded in the file. Names never collide ("Character",
    /// "Character 2", ...). The document is not touched and no undo step is recorded. Returns the new
    /// path, or nothing in Play, for an unknown item or a failed write.
    [[nodiscard]] std::optional<std::filesystem::path> CreateContentCatalogueItem(
        std::string_view itemId, const std::filesystem::path& directory);

    /// Makes a Widget Blueprint (`.uvwbp`) in `directory`: a Canvas with a title Text and a Start
    /// Button, and a graph where clicking Start sets the title. The document is not touched.
    /// Returns its path, or nothing when it could not be written.
    [[nodiscard]] std::optional<std::filesystem::path> CreateWidgetBlueprintAsset(const std::filesystem::path& directory,
                                                                                    std::string_view stem = "WBP_NewWidget");

    /// Brings the entity asset (`.uventity` or `.uvprefab`) at `path` into the scene under
    /// `parent` - or, when that is invalid, where a new object would go - selects it and records one
    /// undo step. Returns the new root, or kInvalidEntity.
    [[nodiscard]] Scene::Entity PlaceEntityAsset(const std::filesystem::path& path,
                                                       Scene::Entity parent = Scene::kInvalidEntity);
    /// Changes an AnimationSequencer as one undo step (its animation list, its current clip).
    bool EditAnimationSequencer(Scene::Entity player,
                                const std::function<void(Scene::AnimationSequencerComponent&)>& change);
    /// Adds a project clip to the player's list and makes it the one playing.
    bool AddClipToAnimationSequencer(Scene::Entity player, const std::filesystem::path& absoluteClip);
    /// Imports every clip in the `.uvanimlib` file into the player's list as one undo step,
    /// skipping clips already there. The current clip only changes when the player has none.
    bool ImportAnimationLibraryIntoSequencer(Scene::Entity player, const std::filesystem::path& absoluteLibrary);
    /// Writes the player's list (and its clip, when it is not listed) to `absoluteLibrary`.
    /// Missing clips keep their GUIDs with a "(missing)" name, so importing back restores the list.
    bool ExportAnimationSequencerToLibrary(Scene::Entity player, const std::filesystem::path& absoluteLibrary);
    /// Links the player to the `.uvanimlib` file as one undo step: the Timeline offers the file's
    /// clips alongside the owned list, live. Linking elsewhere switches the link.
    bool LinkAnimationLibraryToSequencer(Scene::Entity player, const std::filesystem::path& absoluteLibrary);
    /// Clears the player's library link as one undo step; the owned list is untouched.
    bool UnlinkAnimationLibraryFromSequencer(Scene::Entity player);
    /// Re-reads the player's linked library into the picker's cache (`linkedLibraryClips` and
    /// friends). The picker calls this when it opens and whenever the link changes.
    void RefreshSequencerLinkedClips(Scene::Entity player);
    /// The clips the Timeline picker offers: the owned list, the clip first when an older save
    /// never listed it, then the linked entries in file order. Deduped.
    static std::vector<Asset::AssetGuid> MergeSequencerClipLists(const std::vector<Asset::AssetGuid>& owned,
                                                                       Asset::AssetGuid clip,
                                                                       const std::vector<Asset::AssetGuid>& linked);
    /// Opens the floating file picker for `request`. One picker at a time: opening another
    /// replaces the pending one, and the replaced `onPick` never fires.
    void OpenFilePicker(FilePickerRequest request);
    [[nodiscard]] bool IsFilePickerOpen() const noexcept { return m_filePicker.has_value(); }
    void CloseFilePicker();

    /// Opens Entity Class over `directory` (absolute): the window that offers the bodies an entity can
    /// start as (Content "+ Add" -> Entity -> "Entity Class..."). Opening it writes nothing.
    void OpenEntityClassPicker(std::filesystem::path directory);
    [[nodiscard]] bool IsEntityClassPickerOpen() const noexcept { return m_entityClassPicker.has_value(); }
    void CloseEntityClassPicker();
    /// Makes the entity class `itemId` in the open picker's directory, selects it in Content and opens
    /// it in the Entity Editor, as one step. Returns false - with the window saying why - for anything
    /// that is not an entity class, and while authoring is not allowed.
    [[nodiscard]] bool CreateEntityClass(std::string_view itemId);
    /// The picker's listing for `directory` (content-relative, empty is Content): its subfolders
    /// plus its files whose extension is one of `extensions` (empty matches every file) and whose
    /// stems contain `search` (case-insensitive). Sorted.
    static FilePickerListing BuildFilePickerListing(const std::vector<Asset::ProjectFileEntry>& entries,
                                                           const std::filesystem::path& directory,
                                                           const std::vector<std::string>& extensions,
                                                           std::string_view search);

    /// Brings a model source (an FBX, glTF or OBJ in Content, by its content-relative path) into the
    /// scene as one undo step and returns its root. A file with bones becomes
    ///   <File> (Object3D)
    ///   +- Armature (Object3D)
    ///   |  +- Skeleton3D          bound to the file's bones
    ///   |     +- <File> Mesh      (MeshInstance3D, when the file has a mesh)
    ///   +- AnimationSequencer        playing the file's first take, looping (when it has takes)
    /// and a file without bones becomes one MeshInstance3D. Refused while the converted mesh a
    /// file needs is not imported yet. `relativeClip`, one of the file's `.uvanim` takes, is the
    /// one the AnimationSequencer plays instead of the first.
    [[nodiscard]] Scene::Entity PlaceModelSource(const std::filesystem::path& relativeSource,
                                                       Scene::Entity parent = Scene::kInvalidEntity,
                                                       const std::filesystem::path& relativeClip = {});

    /// The Entity Editor: an entity asset opened on its own, in its own window. While it is open
    /// the entity *is* the document - the scene is put aside in a snapshot (the way Play does),
    /// so every tool (Scene tree, Inspector, gizmos, undo) works on the entity unchanged, and the
    /// main window stops drawing. Undo starts empty and is cleared again on close: entity handles
    /// are fresh on both sides. Refused in Play or while another entity is open.
    bool OpenEntityEditor(const std::filesystem::path& assetPath);

    // ---- Motion Montage Editor ---------------------------------------------------------------------------
    /// An open Motion Montage (`.uvmontage`): the asset being edited, its undo, the preview's
    /// playhead and playback, and the preview character in a scene of its own.
    struct MontageEditSession final {
        std::filesystem::path path;
        Asset::AssetGuid guid;
        Asset::MotionMontageAsset montage;
        Asset::MotionMontageAsset saved;
        std::vector<Asset::MotionMontageAsset> undo;
        std::vector<Asset::MotionMontageAsset> redo;
        double playhead = 0.0;
        bool playing = false;
        bool loop = true;
        double speed = 1.0;
        Scene::MontagePlayback playback;
        /// The section Play starts from (empty: the first).
        std::string previewSection;
        Scene::Entity previewRoot = Scene::kInvalidEntity;
        Scene::Entity previewSkeleton = Scene::kInvalidEntity;
        ViewportOverlayState overlay;
        std::string status;
        /// The notify / state / curve key the preview last passed, for the status line.
        std::string lastEvent;
        bool focusPending = true;
    };
    /// Opens `path` in the Motion Montage Editor (replacing a montage already open, which must be saved or
    /// discarded first: false while it has unsaved changes).
    bool OpenMontageEditor(const std::filesystem::path& path);
    [[nodiscard]] bool IsMontageEditorOpen() const noexcept { return m_montageEdit.has_value(); }
    [[nodiscard]] bool IsMontageEditorDirty() const noexcept {
        return m_montageEdit.has_value() && !(m_montageEdit->montage == m_montageEdit->saved);
    }
    bool SaveMontageEditor();
    void CloseMontageEditor();
    /// Changes the open montage through `edit` as one undo step (nothing when it changes nothing).
    void EditMontage(const std::function<void(Asset::MotionMontageAsset&)>& edit);
    bool UndoMontageEdit();
    bool RedoMontageEdit();
    /// Puts the preview on the entity asset `guid` (invalid: the skeleton of the first clip).
    void SetMontagePreviewEntity(Asset::AssetGuid guid);
    /// Play from `section` (empty: the playhead's section), pause, or stop back at the start.
    void PlayMontagePreview(std::string_view section = {});
    void PauseMontagePreview();
    void StopMontagePreview();
    void SetMontagePlayhead(double time);
    /// Advances the preview and poses its character. Called once a frame.
    void TickMontageEditor(double deltaSeconds);
    /// Draws the preview into the Motion Montage Editor's dedicated viewport (`montage_viewport`).
    [[nodiscard]] std::uint64_t RenderMontagePreview(const Math::Vector2& size, Math::Vector2& outUsed,
                                                        const EditorViewportInput& input);
    /// What the Widget Editor's Designer shows in a 3D View (UIView3DComponent's look).
    struct WidgetPreviewRequest final {
        /// The entity asset, as the project reads it ("assets/Hero.uventity").
        std::string assetPath;
        float yawDegrees = 0.0F;
        float spinDegreesPerSecond = 0.0F;
        float lookAtHeight = 1.0F;
        float cameraDistance = 3.5F;
        float pitchDegrees = 0.0F;
        /// The widget's Stage as it is in the editor (a prefab snapshot of its "Stage" object), or
        /// empty: the asset then stands alone on the studio floor.
        std::vector<std::byte> stage;
    };
    /// Draws a 3D View's asset, lit and turning, into the Designer's dedicated preview viewport
    /// (`widget_preview_viewport`), in a world of its own; 0 when there is nothing to show.
    [[nodiscard]] std::uint64_t RenderWidgetPreview(const WidgetPreviewRequest& request, const Math::Vector2& size,
                                                      Math::Vector2& outUsed);
    /// Lets the preview's world go (the Widget Editor closed, or no 3D View left).
    void EndWidgetPreview();
    /// The open montage's clip `guid`, loaded once (nullptr when it is not a readable skeletal clip).
    [[nodiscard]] const Asset::AnimationClipAsset* MontageClip(Asset::AssetGuid guid);
    [[nodiscard]] MontageEditSession* MontageEditSessionPtr() noexcept {
        return m_montageEdit.has_value() ? &*m_montageEdit : nullptr;
    }
    [[nodiscard]] bool IsEntityEditorOpen() const noexcept;

    /// Opens the Retarget window for `animations` (absolute `.uvanim` paths). `target` is the
    /// character's content-relative model source or `.uvmodel`, or empty to choose in the window.
    void OpenRetargetWindow(std::vector<std::filesystem::path> animations, std::filesystem::path target = {});
    /// Opens the Retarget window for an explicit multi-selection of content-relative files/folders:
    /// strictly only the selected items (expanding any selected folders recursively) are included.
    void OpenRetargetWindowForPaths(const std::vector<std::filesystem::path>& relativePaths);
    /// Opens the Retarget window for all animations inside a content-relative folder (recursively).
    void OpenRetargetWindowForFolder(const std::filesystem::path& relativeFolder);
    [[nodiscard]] bool IsRetargetWindowOpen() const noexcept { return m_retargetWindow.has_value(); }
    void CloseRetargetWindow();
    /// Opens `assetPath` (absolute `.uvanimlib`) in the Animation Library window. Opening another
    /// library switches to it; a file that does not read as a library returns false and leaves
    /// whatever is open alone.
    bool OpenAnimationLibrary(const std::filesystem::path& assetPath);
    [[nodiscard]] bool IsAnimationLibraryWindowOpen() const noexcept { return m_animationLibraryWindow.has_value(); }
    void CloseAnimationLibraryWindow();
    /// Re-reads the open library from `libraryAbsolute` when it is the open file; anything else is
    /// a no-op. Drops onto the file in Content call this so the window never shows stale entries.
    void RefreshAnimationLibraryWindow(const std::filesystem::path& libraryAbsolute);
    /// Appends `clipAbsolutePaths` to the library file, writing once: only `.uvanim` files that
    /// load and whose GUIDs are not already listed join. Returns how many entries were added.
    std::size_t AppendClipsToAnimationLibrary(const std::filesystem::path& libraryAbsolute,
                                                 const std::vector<std::filesystem::path>& clipAbsolutePaths);
    /// Enqueues `clipAbsolutePaths` for paced one-by-one low-RAM ingestion into `libraryAbsolute`
    /// (matching the asset importer's smooth incremental loading).
    void QueueClipsForAnimationLibrary(const std::filesystem::path& libraryAbsolute,
                                       const std::vector<std::filesystem::path>& clipAbsolutePaths);
    /// Processes up to `maxClips` within `maxBudgetSeconds` from the active Animation Library add queue.
    /// Returns true while more clips remain in the queue.
    bool PollAnimationLibraryAddQueue(std::size_t maxClips = 1U, double maxBudgetSeconds = 0.004);
    [[nodiscard]] bool IsAnimationLibraryAddQueueActive() const noexcept;
    /// Collects `.uvanim` animation files directly inside `relativeFolder` (non-recursive: nested
    /// subfolders are excluded).
    [[nodiscard]] std::vector<std::filesystem::path> CollectAnimationClipsInFolder(
        const std::filesystem::path& relativeFolder) const;
    /// Creates a unique `.uvanimlib` inside `relativeFolder` from the direct `.uvanim` files in that
    /// folder, opens the Animation Library window, and queues paced 1-by-1 ingestion. Returns nullopt
    /// when authoring is disallowed or `relativeFolder` has no direct `.uvanim` files.
    [[nodiscard]] std::optional<std::filesystem::path> CreateAnimationLibraryFromFolder(
        const std::filesystem::path& relativeFolder);
    [[nodiscard]] std::size_t CachedAnimationIconCount() const noexcept { return m_animationIcons.size(); }
    /// The open entity's file, or empty.
    [[nodiscard]] std::filesystem::path GetEntityEditorAssetPath() const;
    /// The open entity's root object (the one child of the scene root), or kInvalidEntity.
    [[nodiscard]] Scene::Entity GetEntityEditorRoot();
    /// Writes the entity back to its file. Refused (with a Content status line) when the root is
    /// gone or other objects sit beside it: an entity has exactly one root.
    bool SaveEntityEditor();
    /// Brings every copy of the entity `guid` placed in the level up to its saved file (each keeps
    /// where it stands and its name). Level scope only. Returns how many were updated.
    std::size_t RefreshLevelCopies(Asset::AssetGuid guid, const std::filesystem::path& assetPath);
    /// Throws away the edits and loads the file again.
    bool RevertEntityEditor();
    /// Closes the Entity Editor, saving first when `save`, and brings the scene back. After a save,
    /// clean instances of the entity in the scene are refreshed from the file.
    bool CloseEntityEditor(bool save);

    /// Imports every take of the FBX at `absoluteSource` as a skeletal `.uvanim` beside it, named
    /// "<file>_<take>.uvanim". A clip newer than the FBX is left alone, so this is cheap to call on
    /// every refresh. Returns the paths written.
    std::vector<std::filesystem::path> ImportModelAnimations(const std::filesystem::path& absoluteSource);

    /// The Entity Editor's middle area.
    enum class EntityEditorTab : std::uint8_t { Viewport, Scripting, Events };
    [[nodiscard]] EntityEditorTab GetEntityEditorTab() const noexcept;
    void SetEntityEditorTab(EntityEditorTab tab) noexcept;
    /// One thing Compile found wrong with the open entity. `entity` is the object it belongs to
    /// (kInvalidEntity for a problem with the entity as a whole); `at` is zero when it has no
    /// place in a script.
    struct EntityCompileProblem final {
        Scene::Entity entity = Scene::kInvalidEntity;
        std::string objectName;
        std::string scriptPath;
        Blueprint::SourceLocation at;
        std::string message;
        /// A Blueprint's problem: the node it is on (0 for none).
        std::uint32_t blueprintNode = 0U;
        /// About the entity's make-up (a body with no collider), kept current as it is edited.
        bool structural = false;
    };
    /// The entity's make-up problems: a Character3D, Player3D, Static3D, Rigid3D or Kinematic3D
    /// with no collider on it or under it.
    [[nodiscard]] std::vector<EntityCompileProblem> CollectEntityStructureProblems();
    /// Puts the make-up problems in the open entity's problem list. True when they changed.
    bool RefreshEntityStructureProblems();
    /// The Blueprint panel's unsaved graph for a `.uvbp` path, so Compile checks what is being
    /// edited (like the open text editor for a `.uvs`). Unset or empty: the file on disk.
    std::function<std::optional<Asset::BlueprintAsset>(const std::string& path)> m_openBlueprintProvider;
    /// Writes the Entity Editor's open Blueprint graph when it has unsaved edits (set by its window):
    /// every save of the entity saves its graph too, and Simulate and Revert keep its edits.
    std::function<void()> m_saveOpenBlueprint;
    /// Told when the Asset Table deletes a file (its absolute path): the Entity Editor closes a
    /// graph it had open from that file and empties the Script slots that named it.
    std::function<void(const std::filesystem::path& absolute)> m_onContentDeleted;
    /// Whether the Blueprint open in the Entity Editor has edits not yet saved (closing asks first).
    std::function<bool()> m_openBlueprintDirty;
    /// A Blueprint (project path) the Entity Editor opens once its entity is open (Asset Table open).
    std::string m_pendingBlueprintOpen;
    /// A Blueprint Class to make once the Entity Editor has the entity open (Add > Blueprint Class):
    /// the file, and the object it runs on as names from the root down.
    struct PendingBlueprintCreate {
        std::filesystem::path file;
        std::vector<std::string> objectPath;
    };
    std::optional<PendingBlueprintCreate> m_pendingBlueprintCreate;
    /// Checks the whole open entity: one root, and every object's `.uvs` script (read from the
    /// open text editor when it has unsaved text, else from disk) compiled against that object.
    /// Returns the problem count; the list stays until the next Compile, Revert or close.
    std::size_t CompileEntityEditor();
    /// What changed in the open entity since it was last saved (or opened): objects, components
    /// and properties, from its saved file to what the editor holds now. Empty when nothing did.
    [[nodiscard]] std::vector<EntityDiffRow> DiffEntityEditor();
    /// Simulate: the open entity's UVScripts run on its own scene (ready once, then tick every
    /// frame), the level untouched. Stop puts the entity back exactly as it was before Play.
    enum class EntitySimulationState : std::uint8_t { Stopped, Running, Paused };
    /// Starts (or resumes) the simulation; false when no entity is open or it cannot be captured.
    bool PlayEntitySimulation();
    void PauseEntitySimulation();
    /// Stops it and restores the entity from the snapshot Play took.
    bool StopEntitySimulation();
    [[nodiscard]] EntitySimulationState GetEntitySimulationState() const noexcept;
    /// Advances a running simulation by `deltaSeconds` (the Entity Editor calls it each frame).
    void TickEntitySimulation(double deltaSeconds);
    /// One place the open entity mentions a searched text: an object's name or type, a component,
    /// or a property name or value of one of its components (a script path, a material, ...).
    struct EntityFindRow final {
        Scene::Entity entity = Scene::kInvalidEntity;
        std::string object;
        std::string where;  ///< "Name", "Type", "Collider", "Script.scriptAssetPath", ...
        std::string text;   ///< what matched
    };
    /// Every place in the open entity that contains `query` (case-insensitive), in tree order.
    [[nodiscard]] std::vector<EntityFindRow> FindInEntityEditor(std::string_view query);
    [[nodiscard]] const std::vector<EntityCompileProblem>& GetEntityEditorProblems() const noexcept;
    /// False until Compile has run for this session (so "no problems" means something).
    [[nodiscard]] bool HasEntityEditorCompiled() const noexcept;
    /// One `on <event>` handler in an object's script: what the object answers to.
    struct EntitySignalRow final {
        Scene::Entity entity = Scene::kInvalidEntity;
        std::string objectName;
        std::string scriptPath;
        std::string event;
        /// "(other, impulse)" or empty for a handler without parameters.
        std::string params;
        std::uint32_t line = 0U;
    };
    /// Every handler of every script in the open entity, in tree order then source order.
    [[nodiscard]] std::vector<EntitySignalRow> GetEntityEditorSignals();
    /// Selects `entity`, opens its script in the Scripting tab and puts the caret on `line`
    /// (1-based; 0 leaves it). False when the object has no `.uvs` script.
    bool GoToEntityScript(Scene::Entity entity, std::uint32_t line);
    /// Unsaved edits anywhere in the Entity Editor: the tree or the open script.
    [[nodiscard]] bool HasEntityEditorUnsavedChanges() const noexcept;

    /// Stores `contentRelativePath` (a `.uventity`) as the project's Default Player and saves the
    /// project settings. An empty path clears it.
    [[nodiscard]] bool SetDefaultPlayerEntity(const std::filesystem::path& contentRelativePath);
    [[nodiscard]] std::string GetDefaultPlayerEntity() const;

    /// `directory/stem.extension`, or `directory/stem N.extension` with the smallest N >= 2 that is
    /// free. `extension` is empty for a folder.
    [[nodiscard]] static std::filesystem::path MakeUniqueContentPath(const std::filesystem::path& directory,
                                                                        std::string_view stem,
                                                                        std::string_view extension);

    /// Renames `file` (a file or folder) to `newStem` plus its old extension, in the same folder.
    /// Nothing happens for an empty or path-like stem, or when the name is taken. Returns the new
    /// path. An asset already placed in a scene keeps pointing at the old name.
    /// RenameContentFile, and the renamed file keeps its GUID in the asset database.
    std::optional<std::filesystem::path> RenameContentAsset(const std::filesystem::path& file,
                                                               std::string_view newStem);
    [[nodiscard]] static std::optional<std::filesystem::path> RenameContentFile(const std::filesystem::path& file,
                                                                                   std::string_view newStem);

    /// Copies `file` next to itself under the first free name ("Hero 2.uventity"). Folders are
    /// copied whole. Returns the copy's path.
    [[nodiscard]] static std::optional<std::filesystem::path> DuplicateContentFile(const std::filesystem::path& file);

    /// Moves `id` to the front of `recent`, without duplicates, keeping at most five.
    static void PushContentCreateRecent(std::vector<std::string>& recent, std::string_view id);

    /// Refreshes the sole selected prefab instance from its current source revision. Dirty instances
    /// are rejected with merge-required semantics and are never silently overwritten.
    [[nodiscard]] bool RefreshSelectedPrefab();

    /// Explicitly discards persisted local prefab overrides and refreshes from source. This is a
    /// destructive authoring command and is rejected outside Edit mode or without a sole selection.
    [[nodiscard]] bool DiscardSelectedPrefabOverridesAndRefresh();

    /// Replaces the editable document scene with the active .uvscene file. A backup scene is
    /// created before destructive mutation and restored if deserialization fails; the editor camera
    /// remains outside the document root set.
    [[nodiscard]] bool LoadScene();

    /// Makes a Asset Table `.uvscene` the active scene and loads it. The caller supplies an
    /// existing regular scene file; the current document is recovered by LoadScene() if the
    /// replacement fails.
    [[nodiscard]] bool OpenSceneAsset(const std::filesystem::path& path);

    /// Makes entity the sole ordered hierarchy/inspector selection when it is live; invalid or
    /// deleted handles clear the selection instead of exposing stale ECS state.
    void SelectEntity(Scene::Entity entity) noexcept;
    /// Adds a live document entity to the ordered selection or removes it when already selected.
    /// A newly added entity becomes active. Removing the active entity promotes the last remaining
    /// selected entity; removing the final entity clears the active selection.
    void ToggleEntitySelection(Scene::Entity entity) noexcept;
    void ClearSelection() noexcept;
    /// Returns the ordered, deduplicated live document selection. The final entry is active whenever
    /// ToggleEntitySelection() removed the prior active entity.
    [[nodiscard]] const std::vector<Scene::Entity>& GetSelectedEntities() const noexcept;
    /// Returns true only when the active entity is the sole selected live document entity.
    [[nodiscard]] bool HasSingleDocumentSelection() const noexcept;

    /// Applies one validated local transform through ISceneGraph, ensuring derived world
    /// transforms are marked dirty for EngineCore's next scene-graph update. Returns false for
    /// invalid/deleted/non-transform entities or non-finite transform values.
    [[nodiscard]] bool SetSelectedLocalTransform(const Scene::TransformComponent& transform);
    /// Inspector section clipboard. Copy takes the selected entity's whole component of `entry`'s
    /// type; Paste writes a copied value back onto a component of the same type (any entity) as one
    /// undoable edit, refused when the clipboard holds another type or the value breaks the
    /// component's own rule; Reset writes the type's default the same way.
    [[nodiscard]] bool CopySelectedComponent(const Core::TypeMetadataEntry& entry);
    [[nodiscard]] bool CanPasteSelectedComponent(const Core::TypeMetadataEntry& entry) const noexcept;
    [[nodiscard]] bool PasteSelectedComponent(const Core::TypeMetadataEntry& entry);
    [[nodiscard]] bool ResetSelectedComponent(const Core::TypeMetadataEntry& entry);
    /// The same for the Transform section, which is drawn by hand rather than from metadata. Only
    /// the local pose travels - position, rotation (with its Euler authoring state) and scale; the
    /// object's own top-level flag stays as it is.
    [[nodiscard]] bool CopySelectedTransform();
    [[nodiscard]] bool CanPasteSelectedTransform() const noexcept { return m_transformClipboard.has_value(); }
    [[nodiscard]] bool PasteSelectedTransform();
    [[nodiscard]] bool ResetSelectedTransform();
    [[nodiscard]] bool IsUniformScaleLocked() const noexcept { return m_uniformScaleLocked; }
    void SetUniformScaleLocked(const bool locked) noexcept { m_uniformScaleLocked = locked; }

    /// Adds or updates persistent human-readable metadata for the selected live document entity.
    /// Returns false without mutation for invalid editor/selection state, an empty or whitespace-only
    /// name, a name longer than the supported editor-entry limit, or an unchanged value.
    [[nodiscard]] bool SetSelectedEntityName(std::string name);
    /// Shows or hides `entity` (its Visibility component's authored switch) as one undoable edit.
    /// Unlike the selected-entity setters this targets any document entity, so the hierarchy's
    /// eye toggle works on a row without changing the selection. Returns false without mutation
    /// when editing is not allowed, the entity has no Visibility component, or nothing changes.
    [[nodiscard]] bool SetEntityVisible(Scene::Entity entity, bool visible);
    /// Locks or unlocks `entity` (such as a Folder in the Outliner) from modification or drop/drag.
    [[nodiscard]] bool IsEntityLocked(Scene::Entity entity) const noexcept;
    void SetEntityLocked(Scene::Entity entity, bool locked) noexcept;
    void ToggleEntityLock(Scene::Entity entity) noexcept;
    /// Asks the viewport to bring `entity` into view: a Marker3D flies into its viewpoint, any
    /// other object with a world position becomes the orbit pivot. Returns false and requests
    /// nothing when CanFocusEntityInViewport() says no.
    [[nodiscard]] bool RequestViewportFocus(Scene::Entity entity);
    /// True for a document entity the viewport can focus: one with a world position or a usable
    /// Marker3D viewpoint. The scene root and plain Objects have neither.
    [[nodiscard]] bool CanFocusEntityInViewport(Scene::Entity entity) const;
    [[nodiscard]] std::uint32_t GetViewportFocusRequestSerial() const noexcept {
        return m_viewportOverlayState.focusRequestSerial;
    }
    [[nodiscard]] Scene::Entity GetViewportFocusEntity() const noexcept {
        return m_viewportOverlayState.focusEntity;
    }
    /// Opens or closes `entity`'s row in the hierarchy together with every row below it. Rows
    /// that are drawn the next frame change at once; a row inside a collapsed branch keeps the
    /// request until it is next drawn, so reopening the branch later shows it closed. Returns
    /// false for anything that is not a document entity.
    [[nodiscard]] bool SetHierarchyBranchOpen(Scene::Entity entity, bool open);
    /// The display name of `entity`'s object type ("Static3D"): its stored type, or for an object
    /// saved before types were stored, the best reading of its components
    /// (Scene::ResolveSceneObjectKind). Empty for anything that is not a document entity.
    [[nodiscard]] std::string_view GetObjectTypeName(Scene::Entity entity) const;
    /// True when `entity` is a document object that can make `move` among its siblings: not the
    /// scene root, and not already at the end it would move toward.
    [[nodiscard]] bool CanMoveDocumentEntity(Scene::Entity entity, EditorSiblingMove move);
    /// Moves `entity` among its siblings, keeping its parent and transform, as one undoable edit.
    /// Returns false, changing nothing, when CanMoveDocumentEntity does.
    [[nodiscard]] bool MoveDocumentEntity(Scene::Entity entity, EditorSiblingMove move);
    /// Drag and drop in a tree: `entity` goes inside `target` (its last child), or just before or
    /// after it among `target`'s siblings. Reparents when needed; undoable. where: 0 before, 1 inside, 2 after.
    [[nodiscard]] bool PlaceDocumentEntity(Scene::Entity entity, Scene::Entity target, int where);
    /// Adds a Sky (gradient bands, atmosphere or a panorama) to a World Environment object, or
    /// takes it off. The level is marked changed.
    [[nodiscard]] bool AddSkyToEntity(Scene::Entity entity);
    [[nodiscard]] bool RemoveSkyFromEntity(Scene::Entity entity);
    /// The hierarchy panel preferences in effect (Editor Preferences > Hierarchy).
    [[nodiscard]] const HierarchyViewSettings& GetHierarchyViewSettings() const noexcept { return m_hierarchyView; }
    /// The open state a hierarchy row will be given when it is next drawn, if one is pending.
    [[nodiscard]] std::optional<bool> GetPendingHierarchyRowOpen(Scene::Entity entity) const;
    /// Problems with how `entity` is set up, one readable sentence each, for the hierarchy's
    /// warning badge: a non-finite transform, a script path that is not a valid project path, a
    /// mesh object with no mesh or with a mesh/material the project no longer has, a Skeleton3D
    /// with no source model. Empty when there is nothing to fix.
    [[nodiscard]] std::vector<std::string> GetObjectWarnings(Scene::Entity entity) const;
    /// The script attached to `entity`, when it has one (a non-empty script path).
    [[nodiscard]] std::optional<std::string> GetObjectScriptPath(Scene::Entity entity) const;

    /// Adds or replaces one supported scene component on the selected document entity using the
    /// value variant matching `kind`. Valid changes are one Undo/Redo transaction; invalid, unchanged,
    /// multi-selected, protected-Play, active-gesture, or mismatched kind/value calls fail atomically.
    [[nodiscard]] bool SetSelectedSceneComponent(EditorSceneComponentKind kind,
                                                     const EditorSceneComponentValue& value);

    /// Removes one supported scene component from the selected entity as one Undo/Redo transaction.
    /// Core identity components such as Transform, Name, and Hierarchy are intentionally excluded.
    [[nodiscard]] bool RemoveSelectedSceneComponent(EditorSceneComponentKind kind);

    /// Replaces the selected primitive's complete authored appearance atomically. Primitive kind
    /// and bounded linear-RGB base color are both editable after creation; one changed valid call
    /// becomes one Primitive Appearance Undo/Redo transaction. Invalid, unchanged, multi-selected,
    /// protected-Play, or competing-gesture state returns false without mutation.
    [[nodiscard]] bool SetSelectedPrimitiveMesh(const Scene::PrimitiveMeshComponent& primitive);
    /// What the selected MeshInstance3D draws: a model (`model`), a built-in shape (`shape`, the
    /// model cleared) or nothing (neither). Undoable; a skinned mesh's bones follow a new model.
    [[nodiscard]] bool SetSelectedMeshSource(Asset::AssetGuid model, std::optional<Scene::PrimitiveMeshKind> shape);

    /// Moves the selected document entity by a finite world-space distance along one unit world
    /// axis. Parent world rotation and scale are converted back to a local position delta before
    /// applying the existing scene-graph transform path. Returns false without mutation if the
    /// entity, parent transform, axis, or distance is invalid.
    [[nodiscard]] bool TranslateSelectedAlongAxis(EditorTransformAxis axis, float worldDistance);

    /// Rotates the selected document entity around one finite world axis by radians. A parented
    /// entity receives the equivalent local quaternion delta through the current parent world
    /// rotation. Returns false without mutation for invalid state, axis, angle, transform, parent,
    /// or active editor gesture.
    [[nodiscard]] bool RotateSelectedAroundWorldAxis(EditorTransformAxis axis, float radians);

    /// Changes one positive authored local-scale component of the selected document entity by a
    /// finite additive delta. Returns false without mutation for invalid state, axis, delta, active
    /// gesture, or a proposed zero/negative/non-finite scale result.
    [[nodiscard]] bool ScaleSelectedAlongAxis(EditorTransformAxis axis, float localScaleDelta);

    /// Adds one finite local-scale offset to every authored local-scale component of the selected
    /// entity. The command rejects as a whole if any proposed component is non-finite or below the
    /// positive scale floor; it never clamps individual components or performs proportional scaling.
    [[nodiscard]] bool ScaleSelectedUniformly(float localScaleOffset);

    /// Begins one pointer-driven transform transaction on the selected entity, capturing the
    /// baseline to preview from and to restore on cancel. A drag is not a sequence of commands:
    /// every public transform command records history, so driving one from a drag would push an
    /// undo entry per mouse-move frame. Returns false, leaving any existing session untouched,
    /// for invalid editor state, multi-selection, or an entity with no transform.
    [[nodiscard]] bool BeginTransformGesture(EditorToolSessionMode mode);

    /// Applies the gesture's current value WITHOUT recording history. `totalAmount` is measured
    /// from where the drag began, not from the previous frame - a drag reports its total offset
    /// each frame, and treating it as an increment would compound into a runaway. The mode is the
    /// one captured at Begin, so a tool switch mid-drag cannot reinterpret the gesture.
    /// For Scale, EditorTransformAxis::None means uniform.
    [[nodiscard]] bool PreviewTransformGesture(EditorTransformAxis axis, float totalAmount);

    /// The translate-gesture form that takes a full world-space delta rather than one axis, for a
    /// plane handle - a drag in the XY plane moves along two axes at once, which no single-axis
    /// call can express. Snapping quantises each component by the translate step, so a snapped
    /// plane drag lands on the same lattice an axis drag would. Rejected unless the gesture in
    /// flight is a Translate.
    [[nodiscard]] bool PreviewTranslateGesture(const Math::Vector3& totalWorldDelta);

    /// The Inspector's form of a gesture preview: the whole local transform as its Position,
    /// Rotation and Scale fields now read, applied as-is (no snapping - the author typed or
    /// dragged that exact number). Refused for a non-finite transform or with no gesture in flight.
    [[nodiscard]] bool PreviewTransformGestureValue(const Scene::TransformComponent& transform);

    /// Ends the gesture and records exactly ONE history entry, baseline to final. A gesture that
    /// never moved anything commits cleanly without an entry and without marking the scene dirty.
    [[nodiscard]] bool CommitTransformGesture();

    /// Ends the gesture and restores the baseline. Returns false without restoring when the live
    /// transform no longer matches this gesture's last preview - something else moved the entity,
    /// and writing a stale baseline over it would silently discard that change
    /// (EditorToolSessionOutcome::ExternalTransformConflict).
    [[nodiscard]] bool CancelTransformGesture();

    /// Replaces the snapping settings only when every increment is at least
    /// kMinimumTransformSnapStep and at most its own maximum below, and no transform/navigation
    /// gesture is active. Returns false without mutation otherwise.
    [[nodiscard]] bool SetTransformSnappingSettings(const EditorTransformSnappingSettings& settings);
    [[nodiscard]] const EditorTransformSnappingSettings& GetTransformSnappingSettings() const noexcept;
    static constexpr float kMinimumTransformSnapStep = 0.0001F;
    static constexpr float kMaximumTransformSnapTranslateStep = 1000.0F;
    static constexpr float kMaximumTransformSnapRotateStepDegrees = 360.0F;
    static constexpr float kMaximumTransformSnapScaleStep = 100.0F;

    /// Every editor setting kept in the settings file, described (see editor_settings_uve.h).
    [[nodiscard]] const Config::SettingsRegistry& GetSettingsRegistry() const noexcept {
        return m_settingsRegistry;
    }
    /// The value editor setting `id` has right now; nothing for an id that is not an editor setting.
    [[nodiscard]] std::optional<Config::SettingValue> GetEditorSetting(std::string_view id) const;
    /// Applies `value` to editor setting `id` at once. Refused, changing nothing, for an unknown id
    /// or a value its descriptor does not allow. Stored in the settings file with the session.
    /// Notifies observers synchronously after a successful effective change.
    [[nodiscard]] bool SetEditorSetting(std::string_view id, const Config::SettingValue& value);
    /// Calls `callback` after future effective changes to exactly one registered editor setting.
    /// Callbacks run synchronously on the editor's calling thread after the value is applied; a
    /// setting mutation from a callback dispatches its own notification immediately (nested).
    /// Keep subscriptions and callbacks on the editor's owner thread, avoid feedback loops, and do
    /// not throw from callbacks; see SettingsObserverHub for dispatch-time subscription rules.
    [[nodiscard]] Config::SettingsObserverSubscription SubscribeToSetting(
        std::string_view id, Config::SettingsObserverCallback callback);
    /// Calls `callback` after changes to settings in `categoryPrefix` or any descendant category,
    /// matched at slash boundaries. An invalid handle is returned for an empty callback or a prefix
    /// that currently matches no registered editor setting.
    [[nodiscard]] Config::SettingsObserverSubscription SubscribeToCategory(
        std::string_view categoryPrefix, Config::SettingsObserverCallback callback);
    /// Removes a subscription issued by this editor; false for an invalid, foreign, or removed handle.
    [[nodiscard]] bool Unsubscribe(Config::SettingsObserverSubscription subscription);
    /// The point the viewport camera orbits, reported by the host each frame; new objects are placed
    /// there when their placement preference says so. A non-finite point is ignored.
    void SetViewportCameraFocus(const Math::Vector3& focus) noexcept;

    /// Shows the Editor Preferences window, with the search field focused.
    void OpenEditorPreferences() noexcept;
    [[nodiscard]] bool IsEditorPreferencesOpen() const noexcept { return m_preferencesWindow.visible; }
    /// Shows the Project Settings window, with the search field focused.
    void OpenProjectSettings() noexcept;
    [[nodiscard]] bool IsProjectSettingsOpen() const noexcept { return m_projectSettingsWindow.visible; }
    /// Writes the project settings file if it has unsaved changes. Also happens when the Project
    /// Settings window closes and when the editor shuts down.
    [[nodiscard]] bool SaveProjectSettings();
    /// Every command the editor has, in the order the palette and the shortcuts window list them.
    [[nodiscard]] const std::vector<EditorCommand>& GetEditorCommands() const noexcept;
    /// Runs command `id` when it is available now. False for an unknown or unavailable command.
    [[nodiscard]] bool RunEditorCommand(std::string_view id);
    /// Sets shortcut `slot` (0 primary, 1 alternate) of command `id`; an empty shortcut removes it.
    /// Refused for an unknown command, a slot past 1, or a key a shortcut may not use.
    [[nodiscard]] bool SetEditorCommandShortcut(std::string_view id, std::size_t slot,
                                                   const EditorShortcut& shortcut);
    /// Shows the command palette, ready to type into.
    void OpenCommandPalette() noexcept;
    [[nodiscard]] bool IsCommandPaletteOpen() const noexcept { return m_commandPalette.open; }
    /// Shows the Keyboard Shortcuts window.
    void OpenKeyboardShortcuts() noexcept;

    /// Shows the Input Map window: the project's actions and what triggers them.
    void OpenInputMap() noexcept;
    [[nodiscard]] bool IsInputMapOpen() const noexcept { return m_inputMapWindow.visible; }
    /// Writes the input map file if it has unsaved changes. Also happens when the Input Map window
    /// closes and when the editor shuts down.
    [[nodiscard]] bool SaveInputMap();

    /// Returns the derived world-space box for the active live collider-backed document entity.
    /// It never mutates selection, scene state, dirty state, or Undo/Redo history; unsafe or
    /// unsupported state returns std::nullopt.
    [[nodiscard]] std::optional<EditorSelectionBounds> TryGetSelectedBounds() const;

    /// Creates one root-level document entity with a TransformComponent and the specialized
    /// component implied by `kind`, selects it, and marks the document dirty. Returns the invalid
    /// entity handle without mutation when the editor is not running or `kind` is unsupported.
    [[nodiscard]] Scene::Entity CreateDocumentEntity(EditorEntityKind kind);

    /// Creates a user-facing object from the centralized SceneObject registry. Runtime ownership remains
    /// in core/physics/render/audio/scripting; this method only creates the authored scene façade.
    [[nodiscard]] Scene::Entity CreateDocumentSceneObject(
        Scene::Objects::SceneObjectKind kind, Scene::Entity explicitParent = Scene::kInvalidEntity);

    /// Duplicates the selected live document entity and all descendants under the selected root's
    /// current parent, assigns the duplicate root a deterministic available name when it has name
    /// metadata, selects the fresh root, marks the scene dirty, and records one Undo/Redo entry.
    /// Returns the invalid handle without mutation for invalid state, a stale editor camera, an
    /// active viewport gesture, unsupported snapshot component data, or failed restoration.
    [[nodiscard]] Scene::Entity DuplicateSelectedEntity();

    /// Deletes the selected live document entity and every descendant after capturing a reversible
    /// in-memory snapshot. The still-live document parent becomes selected when present; otherwise
    /// selection is cleared. Returns false without mutation for the same invalid/safety states as
    /// DuplicateSelectedEntity().
    [[nodiscard]] bool DeleteSelectedEntity();

    /// Deletes every selected object Delete may remove, each with everything under it, as one
    /// Undo/Redo step. Roots, blocked objects (ObjectActionBlockReason()) and objects under
    /// another deleted one are skipped; a single object deletes exactly as DeleteSelectedEntity().
    /// False without mutation when nothing selected can be deleted.
    [[nodiscard]] bool DeleteSelectedEntities();
    /// DeleteSelectedEntities(), after asking when it would delete more than one object.
    void RequestDeleteSelectedEntities();

    /// Copies the selected object and everything under it (the Outliner's Copy; it works across
    /// documents, so a level object can be pasted into an open entity). False when nothing copyable
    /// is selected: the reason is ObjectActionBlockReason().
    [[nodiscard]] bool CopySelectedObject();
    /// Copy, then Delete (one undo step each).
    [[nodiscard]] bool CutSelectedObject();
    [[nodiscard]] bool CanPasteObject() const noexcept;
    /// Pastes the copied object just below the selection (asChild: inside it, last), or where a new
    /// object would go when nothing is selected; selects it and records one Undo/Redo entry.
    [[nodiscard]] Scene::Entity PasteObject(bool asChild);
    /// Why `entity` cannot be duplicated, copied or deleted, in a sentence for a menu tooltip; empty
    /// when it can.
    [[nodiscard]] std::string ObjectActionBlockReason(Scene::Entity entity);

    /// Reparents the selected document subtree under newParent, or makes it a document root when
    /// newParent is invalid. Keep Local retains authored local Transform; optional Keep World
    /// preserves only a validated shear-safe compatible world TRS. One successful move is recorded
    /// as an Undo/Redo operation. Returns false without mutation for unsafe state or solve input.
    [[nodiscard]] bool ReparentSelectedEntity(Scene::Entity newParent);
    [[nodiscard]] bool SetReparentTransformMode(EditorReparentTransformMode mode);
    [[nodiscard]] EditorReparentTransformMode GetReparentTransformMode() const noexcept;

    /// Replays the most recent supported editor mutation in reverse. It is safe and returns false
    /// when the editor is not running, history is empty, or a target became stale externally.
    [[nodiscard]] bool Undo();
    /// Reapplies the most recently undone supported editor mutation. It follows Undo's lifecycle
    /// and stale-target safety rules and never records another history entry while replaying.
    [[nodiscard]] bool Redo();
    [[nodiscard]] bool CanUndo() const noexcept;
    [[nodiscard]] bool CanRedo() const noexcept;

    [[nodiscard]] std::vector<Scene::Entity> GetDocumentRoots();

    /// The objects an entity reference may name, in Outliner order: every document object with a
    /// transform, minus the selection itself (a reference an object makes to itself resolves to
    /// nothing an author could act on). Shared by the single-reference picker and the
    /// reference-list drawer, so both offer exactly the same choices in the same order.
    [[nodiscard]] std::vector<Scene::Entity> GetEntityReferenceCandidates();

    /// The document's single scene-root entity (the top of the hierarchy), or invalid when the
    /// document somehow has none. Structural only by design: name + identity transform.
    [[nodiscard]] Scene::Entity GetDocumentSceneRoot();
    /// The Outliner's Viewport: the folder at the top of the level that every other folder lives in.
    /// Invalid while an entity or the Retarget preview stands in for the level.
    [[nodiscard]] Scene::Entity GetDocumentViewport();
    /// One open VFX / Particle Graph Editor document: the particle system being authored, its
    /// undo history, the last compile's diagnostics, and the live preview world it plays in.
    struct VfxEditSession final {
        std::filesystem::path path;
        Asset::ParticleSystemAsset system;
        Asset::ParticleSystemAsset savedSystem;
        /// The most recent system that compiled without errors; the preview keeps running it
        /// while the authored system has compile errors.
        Asset::ParticleSystemAsset lastGoodSystem;
        bool hasGoodSystem = false;
        std::vector<Asset::ParticleSystemAsset> undo;
        std::vector<Asset::ParticleSystemAsset> redo;
        std::size_t selectedEmitter = 0U;
        std::optional<std::size_t> soloEmitter;
        std::vector<VFX::ParticleCompileMessage> compileMessages;
        std::string status;
        VFX::VfxWorld previewWorld;
        VFX::VfxHandle previewHandle{};
        Render::VfxDrawList vfxDrawList;
        bool playing = false;
        bool loop = true;
        bool fixedSeed = false;
        bool showGrid = false;
        bool showBounds = false;
        bool focusPending = true;
        bool previewCameraResetPending = true;
        bool confirmClose = false;
        double speed = 1.0;
        double playhead = 0.0;
        std::uint32_t restartCounter = 0U;
        std::vector<float> lastEmitterCpuMs;
        std::size_t lastInstancesDrawn = 0U;
    };

    /// Computes the elevated world-space origin above the y = 0 studio floor for a VFX system's fixed bounds.
    [[nodiscard]] static Math::Vector3 ResolveVfxPreviewOrigin(const Math::Aabb& fixedBounds) noexcept;

    /// Opens the `.uvfx` at `absolutePath` in the VFX Editor (replacing any open document).
    [[nodiscard]] bool OpenVfxEditor(const std::filesystem::path& absolutePath);
    [[nodiscard]] bool IsVfxEditorOpen() const noexcept { return m_vfxEdit != nullptr; }
    [[nodiscard]] bool IsVfxEditorDirty() const noexcept;
    /// Writes the document (only when it is a valid particle system) and marks it clean.
    [[nodiscard]] bool SaveVfxEditor();
    void CloseVfxEditor();
    /// One undoable edit: `edit` mutates a copy of the system, which then replaces it, is
    /// recompiled, and (when it compiles) respawns the live preview. Returns false when no
    /// document is open or the edit changed nothing.
    bool EditVfxSystem(const std::function<void(Asset::ParticleSystemAsset&)>& edit);
    [[nodiscard]] bool UndoVfxEdit();
    [[nodiscard]] bool RedoVfxEdit();
    /// Compiles every enabled emitter (only the solo emitter while one is soloed). Returns true
    /// when there are no errors; only then does the preview switch to the new system.
    [[nodiscard]] bool CompileVfxEditor();
    void PlayVfxPreview();
    void PauseVfxPreview() noexcept;
    /// Stops preview playback and resets the simulation cleanly back to `t = 0`.
    void StopVfxPreview();
    void RestartVfxPreview();
    /// Moves the preview to `seconds`: restarts it and steps it forward deterministically.
    void SetVfxPlayhead(double seconds);
    void TickVfxEditor(double deltaSeconds);
    /// Draws the live preview into the VFX Editor's view; 0 when nothing can be drawn.
    [[nodiscard]] std::uint64_t RenderVfxPreview(const Math::Vector2& size, Math::Vector2& outUsed,
                                                    const EditorViewportInput& input);
    [[nodiscard]] VfxEditSession* VfxEditSessionPtr() noexcept { return m_vfxEdit.get(); }
    /// Creates a new `.uvfx` in `directory` from a built-in template; returns its path (empty on failure).
    [[nodiscard]] std::filesystem::path CreateParticleSystemAsset(const std::filesystem::path& directory,
                                                                     const std::string& name,
                                                                     VFX::ParticleTemplateKind kind);
    /// The Asset Table thumbnail of a `.uvfx`: its particles at 60% of the duration.
    [[nodiscard]] std::uintptr_t GetVfxThumbnail(const std::filesystem::path& relativePath);
    void ClearVfxThumbnailCache() noexcept;
    void InvalidateVfxThumbnailCache(const std::filesystem::path& absolutePath);

    [[nodiscard]] EditorState GetState() const noexcept;
    [[nodiscard]] Scene::Entity GetSelectedEntity() const noexcept;

    [[nodiscard]] Scene::Entity GetPreviewCamera() const noexcept;
    void SetPreviewCamera(Scene::Entity entity);
    void ClearPreviewCamera() noexcept;
    /// Returns editor-only 2D canvas state for screen-space authoring. It is not scene data.
    [[nodiscard]] Editor2DCanvasState Get2DCanvasState() const noexcept;

    /// The editor-viewport bookmark slots (common editor Ctrl+digit/digit convention). All of it
    /// is transient session state on Editor, never document data and never dirtying the scene:
    /// Set rejects an out-of-range slot or a non-finite/badly-formed pose, Get answers no value
    /// for an empty slot, and Clear empties one slot. The app host owns applying a pose to its
    /// real OrbitCamera - this class deliberately has no camera type in its interface.
    [[nodiscard]] bool SetViewportBookmark(std::size_t slot,
                                              const EditorViewportBookmark& bookmark) noexcept;
    [[nodiscard]] std::optional<EditorViewportBookmark> GetViewportBookmark(
        std::size_t slot) const noexcept;
    [[nodiscard]] bool ClearViewportBookmark(std::size_t slot) noexcept;

    /// Composes the fly-to-marker bookmark for an entity: the entity must carry a valid, enabled
    /// Marker3DComponent and a world transform; the marker's authored offset+rotation are
    /// composed under the object's pose (Scene::ComposeMarker3DPose), the camera eye is placed at
    /// the marker's position looking along its composed -Z (the camera convention), and the orbit
    /// bookmark states that same view as target/yaw/pitch/distance so the host camera applies it
    /// verbatim. Any missing piece answers no value - fail-closed, the caller simply does not
    /// move the camera. This is Marker3D's live consumer: a marker is a scene-persistent named
    /// viewpoint the viewport can fly into, not the inert gizmo it stays in Godot.
    [[nodiscard]] std::optional<EditorViewportBookmark> ComposeMarker3DFocusBookmark(
        Scene::Entity entity) const;

    /// The orbit pivot for a plain focus-on-entity (no marker): the entity's world position, or
    /// no value when it has no world transform. Distance/yaw/pitch intentionally stay whatever
    /// the camera already holds - bounds-aware framing needs renderer-side bounds this class does
    /// not own today; real, separate follow-up, not silently faked.
    [[nodiscard]] std::optional<Math::Vector3> ResolveEntityFocusTarget(
        Scene::Entity entity) const;

    /// Inverts the editor OrbitCamera's forward offset formula (eye = target + offset(yaw,pitch) *
    /// distance) for a requested eye/look direction: pitch = asin(-forward.y) clamped to the same
    /// +/-1.5533 range OrbitCameraSettings itself enforces (a straight-up/down look snaps to the
    /// pole with yaw 0 by convention), yaw = atan2(-forward.z, -forward.x), target = eye +
    /// forward * distance. A degenerate (near-zero or non-finite) forward answers no value.
    /// Standing alone so both ComposeMarker3DFocusBookmark above and any future caller pin the
    /// same math; Test/Editor measures the round trip (offset formula then this inverse) back to
    /// the original eye below 1e-4.
    [[nodiscard]] static std::optional<EditorViewportBookmark> ResolveOrbitBookmarkFromLook(
        const Math::Vector3& eye, const Math::Vector3& forward, float distance) noexcept;
    /// Validates and updates the editor-only 2D canvas zoom without changing scene state/history.
    [[nodiscard]] bool Set2DCanvasZoom(float zoom) noexcept;
    /// Restores the editor-only 2D canvas to its centered loading-screen design view.
    void Reset2DCanvasView() noexcept;
    [[nodiscard]] bool IsSceneDirty() const noexcept;
    /// Read-only transform-tool lifecycle diagnostics. These values expose editor-session evidence
    /// only; they neither alter input routing nor claim any ECS mutation succeeded.
    [[nodiscard]] EditorToolSessionPhase GetToolSessionPhase() const noexcept;
    [[nodiscard]] EditorToolSessionOutcome GetLastToolSessionOutcome() const noexcept;
    /// Returns the selected registered asset record when the selected project-file entry is a currently
    /// correlated file. Directories and unregistered files return std::nullopt.
    [[nodiscard]] const std::optional<Asset::AssetRecord>& GetSelectedAsset() const noexcept;
    /// Returns the selected cached project-file entry, including directories and unregistered files.
    [[nodiscard]] const std::optional<Asset::ProjectFileEntry>& GetSelectedProjectFile() const noexcept;
    [[nodiscard]] const std::string& GetAssetFilter() const noexcept;
    [[nodiscard]] const std::filesystem::path& GetActiveScenePath() const noexcept;
    void SetActiveScenePath(std::filesystem::path path);
    /// Opens `entity`'s script in the Scripting workspace (its `.uvs` file in the text editor).
    /// False when `entity` has no Script component or its script is not a `.uvs` file - the
    /// viewport's entity toolbar uses that to decide whether to offer "Scripting" at all.
    [[nodiscard]] bool OpenScriptGraphForEntity(Scene::Entity entity);
    /// "New UVScript" on the Scripting slot. Writes `scripts/<name>.uvs` (a free name) whose header
    /// names the object and its kind, points the Script at it as one undoable edit, and opens it in
    /// the script text editor. Refuses unless authoring is allowed, exactly one document entity is
    /// selected, it carries a Script component and that Script is still empty.
    [[nodiscard]] bool CreateUVScriptForSelectedEntity();

    /// A `.uvs` file open in the Scripting workspace's text editor.
    struct UVScriptDocument final {
        std::string path;
        /// The object the script is checked against: its components decide which properties exist.
        Scene::Entity entity = Scene::kInvalidEntity;
        std::string text;
        std::string savedText;
        /// Every problem the compiler found in `text`, in source order; empty when it compiles.
        std::vector<Blueprint::Diagnostic> diagnostics;

        [[nodiscard]] bool IsDirty() const noexcept { return text != savedText; }
    };
    /// Opens `entity`'s `.uvs` script in the text editor and switches to the Scripting workspace.
    /// A missing file opens empty; saving creates it. False when the entity has no `.uvs` script.
    [[nodiscard]] bool OpenUVScriptForEntity(Scene::Entity entity);
    /// Opens a `.uvs` script file by path in the text editor and switches to the Scripting workspace.
    [[nodiscard]] bool OpenUVScriptPath(const std::filesystem::path& path);
    /// Opens the Godot-style "Create Script" modal dialog for configuring and creating a new UVScript.
    void OpenCreateScriptModal(const std::filesystem::path& targetDirectory, Scene::Entity entity = Scene::kInvalidEntity);
    /// Opens the Godot-style "Create Shader" modal dialog for configuring and creating a new shader.
    void OpenCreateShaderModal(const std::filesystem::path& targetDirectory,
                                  std::function<void(const std::filesystem::path&)> onCreated = nullptr);
    /// Prompts the user to pick external asset files (models, textures, audio, scripts) and imports them.
    bool PromptImportFiles(const std::filesystem::path& targetDirectory = {});
    /// Prompts the user to pick one or more external folders and imports them while preserving subfolders.
    bool PromptImportFolder(const std::filesystem::path& targetDirectory = {});
    /// Imports external files or folders from disk into the project's content directory.
    /// When `deleteStagedSources` is true (e.g. Android SAF staged cache files), deletes temporary staged copies after import.
    bool ImportExternalFiles(const std::vector<std::filesystem::path>& sourceFiles,
                                const std::filesystem::path& targetDirectory = {},
                                bool deleteStagedSources = false);
    [[nodiscard]] const std::optional<UVScriptDocument>& GetOpenUVScript() const noexcept;
    /// Replaces the open script's text and re-checks it. No effect when nothing is open.
    void SetOpenUVScriptText(std::string text);
    /// Writes the open script to its file. The running game picks the change up on its own.
    [[nodiscard]] bool SaveOpenUVScript();
    /// Closes the text editor (unsaved text is dropped) and returns to the scene.
    void CloseOpenUVScript();

    /// One `export` field of the selected object's `.uvs` script, as the Inspector shows it.
    struct ScriptExportRow final {
        std::string name;
        Blueprint::Type type;
        /// The script's own initial value, as UVScript text.
        std::string defaultText;
        /// What this object runs with: its stored value when it has a valid one, else the default.
        std::string valueText;
        bool overridden = false;
    };
    /// The selected object's exported fields, in declaration order. Empty when the object has no
    /// `.uvs` script, the file cannot be read, or it does not compile.
    [[nodiscard]] std::vector<ScriptExportRow> GetSelectedScriptExports();
    /// Sets the selected object's value for export `name` from UVScript text, or - with no text -
    /// goes back to the script's default. One undo step. Refuses a name the script does not
    /// export and text that is not a value of the field's type.
    [[nodiscard]] bool SetSelectedScriptExport(const std::string& name, std::optional<std::string> text);
    /// "Quick Load" and "Load": points the selected entity's Script at an existing script asset, as
    /// one undoable edit. An empty path clears the slot. Any other path must pass
    /// DescribeScriptAssetProblem.
    [[nodiscard]] bool AssignScriptToSelectedEntity(const std::string& path);
    /// Why `path` cannot be assigned as a script, in words fit to show beside the field; empty when
    /// it can. A path must be project-relative and name an existing `.uvs` file.
    [[nodiscard]] std::string DescribeScriptAssetProblem(const std::string& path) const;
    /// The script assets Quick Load offers, sorted: every one the document already uses, plus every
    /// `.uvs` file in the project's scripts folder.
    [[nodiscard]] std::vector<std::string> GetKnownScriptAssetPaths() const;
    /// Metadata on the selected object. Each writes the whole entry list once through the metadata
    /// property path, so every add, edit, rename, retype or removal is exactly one undo entry.
    /// New and renamed keys must pass ValidateObjectMetadataKey.
    [[nodiscard]] bool AddSelectedObjectMetadata(const std::string& key, const Core::Variant& value);
    [[nodiscard]] bool SetSelectedObjectMetadataValue(const std::string& key, const Core::Variant& value);
    /// The same edit shown at once without history, for a value being dragged: the drag becomes
    /// one undo step when it ends (see PreviewSelectedComponentProperty).
    [[nodiscard]] bool PreviewSelectedObjectMetadataValue(const std::string& key, const Core::Variant& value);
    [[nodiscard]] bool RenameSelectedObjectMetadata(const std::string& key, const std::string& newKey);
    /// Converts the value to `type`. Refuses a conversion that would lose data unless `allowLoss`.
    [[nodiscard]] bool ChangeSelectedObjectMetadataType(const std::string& key, Core::VariantType type,
                                                         bool allowLoss);
    [[nodiscard]] bool RemoveSelectedObjectMetadata(const std::string& key);
    /// Arms the entity context toolbar (see ViewportOverlayState) at the given screen pixel for
    /// `entity`. The caller (main.cpp, which owns viewport picking and the camera the pixel was
    /// projected with) must call this before RenderOverlay runs the same frame.
    void SetEntityContextToolbarAnchor(Scene::Entity entity, float pixelX, float pixelY);
    /// Closes the entity context toolbar (a right-click that missed every entity).
    void ClearEntityContextToolbar() noexcept;

    /// Inspector folds (sections, nested components, sub-groups) remembered by key across
    /// selections and sessions. A key never set answers `defaultOpen`. At most
    /// kMaxRememberedInspectorFolds are kept; beyond that new choices are not remembered.
    /// The colour picker's remembered choices - the Advanced section's state, the saved palette and
    /// the recent colours - saved with the session. The setter keeps only finite colours, clamped
    /// to 0..1, and trims each list to its cap (kMaxSavedColors, kMaxRecentColors).
    [[nodiscard]] const ColorPickerPreferences& GetColorPickerPreferences() const noexcept {
        return m_colorPickerPreferences;
    }
    void SetColorPickerPreferences(ColorPickerPreferences preferences);

    [[nodiscard]] const EditorExtendedPreferences& GetExtendedPreferences() const noexcept {
        return m_extendedPreferences;
    }
    void SetExtendedPreferences(EditorExtendedPreferences preferences) noexcept;

    static constexpr std::size_t kMaxRememberedInspectorFolds = 256U;

    /// Viewport projection and named views. Choosing a projection explicitly sticks until changed.
    /// Moving to a named view switches to orthographic *automatically*, and that automatic
    /// orthographic ends - back to perspective - as soon as the camera is orbited out of the view,
    /// which the host reports with NotifyViewportOrbited().
    void SetViewportOrthographic(bool orthographic) noexcept;
    void RequestViewportView(ViewportView view) noexcept;
    void NotifyViewportOrbited() noexcept;
    [[nodiscard]] bool IsViewportOrthographic() const noexcept { return m_viewportOverlayState.orthographic; }
    [[nodiscard]] ViewportView GetViewportView() const noexcept { return m_viewportOverlayState.view; }
    [[nodiscard]] std::uint32_t GetViewportViewRequestSerial() const noexcept {
        return m_viewportOverlayState.viewRequestSerial;
    }
    [[nodiscard]] static const char* GetViewportViewName(ViewportView view) noexcept;
    /// The keypad layout for named views: 7 Top, 1 Front, 3 Right, and with `opposite` (Ctrl) the
    /// view from the other side - Bottom, Back, Left. Any other digit is not a view.
    [[nodiscard]] static std::optional<ViewportView> GetViewportViewForKeypadDigit(int digit,
                                                                                       bool opposite) noexcept;
    /// The shortcut text shown next to a view or the projection switch in the view menu.
    [[nodiscard]] static const char* GetViewportViewShortcut(ViewportView view) noexcept;

    /// The viewport grid: shown or hidden, and its opacity (0.1..1 - never fully invisible, which
    /// would be a hidden grid under another name). Both are editor preferences, saved with the
    /// session. An opacity outside the range, or not finite, is refused and nothing changes.
    [[nodiscard]] bool SetViewportGrid(bool visible, float opacity);
    [[nodiscard]] bool IsViewportGridVisible() const noexcept { return m_viewportOverlayState.gridVisible; }
    [[nodiscard]] float GetViewportGridOpacity() const noexcept { return m_viewportOverlayState.gridOpacity; }
    static constexpr float kMinimumViewportGridOpacity = 0.1F;
    /// The smallest square the grid draws, in world units. Zooming out still steps the grid up in
    /// tens from here; zooming in never draws finer than it. A preference, saved with the session.
    /// A size outside kMinimum..kMaximumViewportGridCellSize, or not finite, is refused.
    [[nodiscard]] bool SetViewportGridCellSize(float cellSize);
    [[nodiscard]] float GetViewportGridCellSize() const noexcept { return m_viewportOverlayState.gridCellSize; }
    static constexpr float kMinimumViewportGridCellSize = 0.01F;
    static constexpr float kMaximumViewportGridCellSize = 1000.0F;

    /// The selection outline: shown or hidden, its colour, and its thickness in pixels. Editor
    /// preferences, saved with the session. A colour channel outside 0..1, a thickness outside
    /// kMinimum..kMaximumSelectionOutlineThickness, or anything not finite is refused whole.
    [[nodiscard]] bool SetViewportSelectionOutline(bool visible, ViewportAxisColor color, float thickness);
    [[nodiscard]] bool IsViewportSelectionOutlineVisible() const noexcept {
        return m_viewportOverlayState.selectionOutlineVisible;
    }
    [[nodiscard]] ViewportAxisColor GetViewportSelectionOutlineColor() const noexcept {
        return m_viewportOverlayState.selectionOutlineColor;
    }
    [[nodiscard]] float GetViewportSelectionOutlineThickness() const noexcept {
        return m_viewportOverlayState.selectionOutlineThickness;
    }
    static constexpr float kMinimumSelectionOutlineThickness = 1.0F;
    static constexpr float kMaximumSelectionOutlineThickness = 6.0F;

    /// The viewport's X/Y/Z axis colours, which the menu bar offers a picker for and the host
    /// pushes into the real renderer each frame (see ViewportOverlayState::axisColorX).
    ///
    /// The host calls the setter once at startup to seed the viewport's own default palette -
    /// this module cannot name those defaults itself - and thereafter whenever a persisted
    /// choice is loaded. A channel outside 0..1, or not finite, is refused and nothing changes.
    [[nodiscard]] bool SetViewportAxisColors(ViewportAxisColor x, ViewportAxisColor y,
                                                ViewportAxisColor z);
    [[nodiscard]] bool AreViewportAxisColorsSet() const noexcept;
    [[nodiscard]] ViewportAxisColor GetViewportAxisColor(int axisIndex) const;
    /// Forgets the author's choice, which makes the host re-seed its own default palette on the
    /// next frame. Clearing rather than writing default values keeps those hues in the one module
    /// that owns them instead of copying them into this one.
    void ResetViewportAxisColors() noexcept;

    /// Feeds one tapped session datagram into the Capture dock's ring (decode-on-push; see
    /// PacketCapture). The direction is the tap's: outbound for bytes the watched session
    /// sent, inbound for bytes it received. Timestamps come from the tap's clock and must
    /// arrive in order.
    void CapturePacket(const std::vector<std::uint8_t>& datagram,
                          Network::PacketDirection direction, std::uint64_t timestampMs,
                          Network::InspectedChannel channel =
                              Network::InspectedChannel::Unknown);
    /// Empties the Capture dock's ring: packets, drops, and malformed counts alike.
    void ClearCapturedPackets();
    /// The Capture dock's ring, for hosts that poll capture stats without drawing.
    [[nodiscard]] const Network::PacketCapture& CapturedPackets() const noexcept;
    /// The Profiler dock's capture: engine and tool code records spans/counters into it
    /// (thread-safe), and the dock snapshots it live. Sessions are the dock's own
    /// Begin/End buttons; recording outside an active session fails NotActive.
    Core::ProfilerCapture& Profiler() noexcept;
    /// The last completed capture (empty session id when none ended yet).
    [[nodiscard]] const Core::DiagnosticCaptureBundle& ProfilerBundle() const noexcept;
    /// The Profiler dock's Begin / End buttons: starts a new numbered session, or ends the active
    /// one and keeps its bundle. Either way ProfilerStatus() says what happened.
    void BeginProfilerCapture();
    void EndProfilerCapture();
    [[nodiscard]] const std::string& ProfilerStatus() const noexcept { return m_profilerStatus; }
    /// Releases editor-private UI resources and destroys the editor camera while the services are
    /// still alive. Idempotent after the first successful shutdown.
    void Shutdown();

private:
    enum class EditorLayoutPreset : std::uint8_t {
        Default,
        FocusViewport,
        ContentReview,
    };

    struct EditorSelectionPath final {
        std::size_t rootIndex = 0U;
        std::vector<std::size_t> childIndices;
    };

    struct EditorSelectionSnapshot final {
        std::vector<Scene::Entity> entities;
        Scene::Entity activeEntity = Scene::kInvalidEntity;
    };

    struct EditorSelectionPaths final {
        std::vector<EditorSelectionPath> entityPaths;
        std::optional<EditorSelectionPath> activePath;
    };

    struct EntityEditSession final {
        std::filesystem::path assetPath;
        Asset::AssetGuid guid = Asset::kInvalidAssetGuid;
        Scene::Entity rootEntity = Scene::kInvalidEntity;
        /// Set once the entity was written, so closing refreshes its instances in the level.
        bool savedOnce = false;
        /// The window's X was pressed with unsaved changes: ask before closing.
        bool confirmClose = false;
        /// The running Simulate (scripts and the snapshot to go back to); null while stopped.
        std::shared_ptr<EntitySimulation> simulation;
        EntityEditorTab tab = EntityEditorTab::Viewport;
        std::vector<EntityCompileProblem> problems;
        bool compiled = false;
        /// The window selects `tab` on its next frame (GoTo from a problem or a signal).
        bool forceTab = false;
        /// The tree's width, dragged from its seam. 0 means "not dragged yet": the window then
        /// uses the fraction of its own width below, so the columns open at a sensible ratio on
        /// any window size, and the author's own width takes over from the first drag until the
        /// entity is closed. Same shape as the level's Outliner and Inspector panels, which keep
        /// a 0-or-dragged width the same way.
        float treeWidth = 0.0F;
        /// The Inspector's width, dragged from its seam. 0 means "not dragged yet".
        float inspectorWidth = 0.0F;
        bool focusPending = true;
    };
    /// What the Retarget window's viewport shows: a world of its own (a document scene beside the
    /// level, as the Motion Montage Editor's), so the level stays open and editable the whole time.
    struct RetargetPreview final {
        std::shared_ptr<Core::DocumentScene> scene;
        /// Everything of the preview stands under it.
        Scene::Entity frameRoot = Scene::kInvalidEntity;
        /// The character stands under it; the viewport frames it. (The humanoid is drawn in the
        /// window's source box, not in this world.)
        Scene::Entity figures = Scene::kInvalidEntity;
        Scene::Entity sourceSkeleton = Scene::kInvalidEntity;
        Scene::Entity targetSkeleton = Scene::kInvalidEntity;
        /// The joint colour of each humanoid bone, by reference index.
        std::vector<std::array<float, 3>> sourceColours;
        /// The same colours on the character's bones, by their own names.
        std::unordered_map<std::string, std::array<float, 3>> targetColours;
        /// The character the preview was built for; a change rebuilds it.
        std::filesystem::path builtFor;
        bool frameRequested = false;
        /// The character's bone picked in the window (the source box or the bone list), lit in the preview.
        std::string highlightBone;
        /// How tall the character stands (metres), for framing it.
        float figureHeight = 1.8F;
    };
    /// Makes the preview's own world. False when the editor cannot make one.
    [[nodiscard]] bool BeginRetargetPreview();
    /// Drops the preview's world (the level was never touched).
    [[nodiscard]] bool EndRetargetPreview();
    /// Renders the preview's world for the Retarget window (its own camera state and input).
    [[nodiscard]] std::uint64_t RenderRetargetPreview(const Math::Vector2& size, Math::Vector2& outUsed,
                                                         const EditorViewportInput& input);
    /// Runs `work` with the preview's world as the active document; the level's selection and
    /// dirty flag are left as they were.
    void WithRetargetScene(const std::function<void()>& work);
    /// True while the preview's world is the active document.
    bool m_retargetSceneActive = false;
    /// Builds the world: a sun and the character alone, in the middle (the studio view draws the rest).
    void RebuildRetargetPreview(const RetargetPlan& plan, const std::filesystem::path& modelFile);
    void BuildRetargetPreviewWorld(const RetargetPlan& plan, const std::filesystem::path& modelFile);
    /// The Retarget window's work, called every frame while it is open: a finished job's result,
    /// a fresh plan (and preview) when the character or the files changed.
    void TickRetargetWindow();
    /// The rigged characters the Retarget window offers (model sources and .uvmodel files).
    [[nodiscard]] std::vector<std::filesystem::path> RetargetCandidates() const;
    /// Generate: conforms the character and the animations in the background. False when it cannot now.
    bool StartRetarget();
    /// Undo Last: puts the last run's originals back.
    bool UndoLastRetarget();
    /// The entity's objects in tree order (root first). Empty when no entity is open.
    [[nodiscard]] std::vector<Scene::Entity> CollectEntityEditorObjects();
    /// Instantiates the entity asset as the document's only content (below the scene root).
    [[nodiscard]] Scene::Entity LoadEntityIntoDocument(Asset::AssetGuid guid);
    /// An animation clip's icon (152 x 128, ARGB, the shape of a tile's picture): its skeleton half-way through, from the front, or an
    /// object's path; drawn again when the file changes. Null when the file is not a clip.
public:
    struct AnimationIcon final {
        std::filesystem::file_time_type written{};
        std::vector<std::uint32_t> pixels;
        std::shared_ptr<const std::vector<std::uint32_t>> sharedPixels;
        int width = 0;
        int height = 0;
        double durationSeconds = 0.0;
        std::size_t frames = 0U;
        std::size_t bones = 0U;
        /// Bumped each time it is drawn again, so a view that copied it copies it again.
        std::uint64_t serial = 0U;
    };
    static constexpr std::size_t kMaxCachedAnimationIcons = 192U;
private:
    /// Never blocks: a clip not drawn yet is read and drawn on a worker thread (newest asked first)
    /// and kept on disk under the project's .uvcache, so the next session has it at once; until then
    /// it answers null (or the last picture of a changed file). `m_animationIconGeneration` moves
    /// when new icons have arrived.
    [[nodiscard]] const AnimationIcon* GetAnimationIcon(const std::filesystem::path& absolutePath);
    /// The same, drawn now on this thread when it is not ready (for one clip a window needs at once).
    [[nodiscard]] const AnimationIcon* GetAnimationIconNow(const std::filesystem::path& absolutePath);
    /// The icons asked for next go ahead of those already waiting, in the order asked (a view about
    /// to list its clips calls it first).
    void BeginAnimationIconBatch();
    void TouchAnimationIconLru(const std::string& key);
    std::unordered_map<std::string, AnimationIcon> m_animationIcons;
    std::list<std::string> m_animationIconLru;
    std::unordered_map<std::string, std::list<std::string>::iterator> m_animationIconLruMap;
    std::shared_ptr<struct AnimationIconWorker> m_animationIconWorker;
    std::uint64_t m_animationIconGeneration = 0U;
    std::filesystem::path m_animationIconCacheDir;
    /// Window > Network Capture / Audio Mixer (PRISM windows of their own, shown while set).
    bool m_showCaptureWindow = false;
    bool m_captureFocusPending = false;
    bool m_showAudioMixerWindow = false;
    bool m_audioMixerFocusPending = false;
    /// Reloads every loaded asset whose file is one of `files`, under whichever path spelling it was
    /// registered (a relative and an absolute spelling are two GUIDs with two loaded copies).
    void ReloadRetargetedFiles(const std::vector<std::filesystem::path>& files);
    void FinishRetargetJob(RetargetWindowState& window, Retarget::RetargetFilesResult result);
    /// Gives every Skeleton3D bound to `modelFile`'s source the bones of the conformed model; the
    /// number changed.
    std::size_t RefreshSkeletonsForRetargetedModel(const std::filesystem::path& modelFile);
    /// Whether a model source's imported model is one Retarget conformed, its source unchanged since
    /// (then it is not imported again over the conformed one).
    [[nodiscard]] bool IsRetargetedModelCurrent(const std::filesystem::path& relativeSource) const;
    /// Marks (or unmarks, after Undo) a model source's imported model as conformed by Retarget.
    void MarkRetargetedModel(const std::filesystem::path& relativeSource, bool retargeted);
    /// The character file behind a Retarget target: the `.uvmodel` itself, or a model source's imported model.
    [[nodiscard]] std::filesystem::path ResolveRetargetModelFile(const std::filesystem::path& target) const;

    struct EntityEditorMeshCacheEntry final {
        std::filesystem::path resolvedPath;
        std::filesystem::file_time_type lastWriteTime{};
        Asset::MeshAsset meshAsset;
    };
    mutable std::optional<EntityEditorMeshCacheEntry> m_inspectorMeshAssetCache;

    struct PlayModeSession final {
        Scene::SceneSnapshot documentSnapshot;
        bool capturedEmptyDocument = false;
        bool dirtyBefore = false;
        EditorSelectionPaths selectionBefore;
    };

    /// Play-entry spawn semantics. Called once by EnterPlayMode() after the document snapshot
    /// is captured and the simulation is running: resolves the one spawn point that fires this
    /// session (deterministic content order over enabled+valid SpawnPoint3D objects), moves the
    /// player entity (the one carrying a CharacterControllerComponent) to the composed spawn
    /// pose via the sweep's exact inverse, and disables the point when it was authored
    /// `oneShot = true`. Every mutation sits inside the snapshot, so StopPlayMode() hands
    /// back the authored player pose and every spent one-shot. Returns false when there is
    /// nothing to do - no player, no enabled spawn point, or a degenerate pose/ancestry the
    /// resolvers refuse - and a false return never fails play entry.
    [[nodiscard]] bool ApplyPlayEntrySpawn();
    void BeginEditorPlayBootSplash();

    /// Editor-only workspace labels. They do not alter document data, simulation state, or history.
    enum class EditorWorkspace {
        Library,
        Asset,
        Scripting,
        Debug,
        Plugin,
        Game,
    };

    /// Selects the visible content inside the fixed right-side editor panel.
    enum class EditorRightPanelTab {
        Inspector,
        Import,
        Events,
    };

    /// Selects one docked lower-workspace panel. FileSystem is the safe default and keeps the
    /// former Assets database view visible without introducing an AI tooling implementation.
    enum class EditorBottomDock {
        Debugger,
        Animator,
        AIToolbar,
        FileSystem,
        /// The developer console: output and a command line. Appended so persisted values keep meaning.
        Console,
        /// The packet capture viewer: tapped session datagrams and their decode. Appended
        /// so persisted values keep meaning.
        Capture,
        /// The profiler viewer: capture spans, counters, and breadcrumbs. Appended so
        /// persisted values keep meaning.
        Profiler,
        /// The audio authoring dock: the mixer tree with live meters, and the selected source's
        /// attenuation curve. Appended so persisted values keep meaning.
        Audio,
    };

    /// Session-only Asset Table focus. This filters copied ProjectFileIndex entries and
    /// never requests I/O, loads an asset, or changes the AssetDatabase registry.
    enum class AssetTableTypeFocus {
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
    enum class AssetTableItemType {
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
        /// A named, ordered collection of animation clips (`.uvanimlib`) that sequencers load from.
        AnimationLibrary,
        /// An Motion Montage (`.uvmontage`): opens in the Motion Montage Editor.
        MotionMontage,
        /// A named list of montages (`.uvmontagelib`) MontageSequencers link to.
        MontageLibrary,
        /// A Blueprint Class (`.uvbp`): an object's gameplay as a node graph, edited in the Entity Editor.
        Blueprint,
        /// A Widget Blueprint (`.uvwbp`): UI widgets and their graph, edited in the Widget Editor.
        WidgetBlueprint,
        Script,
        Audio,
        Font,
        /// A Particle System (`.uvfx`): opens in the VFX / Particle Graph Editor.
        ParticleSystem,
        File,
    };

    struct TransformHistoryEntry final {
        Scene::Entity entity = Scene::kInvalidEntity;
        Scene::TransformComponent before{};
        Scene::TransformComponent after{};
        EditorSelectionSnapshot selectionBefore;
        EditorSelectionSnapshot selectionAfter;
        bool dirtyBefore = false;
        bool dirtyAfter = false;
    };

    struct NameHistoryEntry final {
        Scene::Entity entity = Scene::kInvalidEntity;
        std::optional<std::string> beforeName;
        std::optional<std::string> afterName;
        EditorSelectionSnapshot selectionBefore;
        EditorSelectionSnapshot selectionAfter;
        bool dirtyBefore = false;
        bool dirtyAfter = false;
    };

    struct PrimitiveAppearanceHistoryEntry final {
        Scene::Entity entity = Scene::kInvalidEntity;
        Scene::PrimitiveMeshComponent before{};
        Scene::PrimitiveMeshComponent after{};
        EditorSelectionSnapshot selectionBefore;
        EditorSelectionSnapshot selectionAfter;
        bool dirtyBefore = false;
        bool dirtyAfter = false;
    };

    /// A MeshInstance3D's mesh changed between a model, a built-in shape and nothing.
    struct MeshSourceHistoryEntry final {
        Scene::Entity entity = Scene::kInvalidEntity;
        Scene::MeshComponent beforeMesh{};
        std::optional<Scene::PrimitiveMeshComponent> beforeShape;
        Scene::MeshComponent afterMesh{};
        std::optional<Scene::PrimitiveMeshComponent> afterShape;
        std::optional<Scene::SkinComponent> beforeSkin;
        std::optional<Scene::SkinComponent> afterSkin;
        Scene::Entity syncedSkeletonEntity = Scene::kInvalidEntity;
        std::optional<Scene::Skeleton3DComponent> beforeSkeleton;
        std::optional<Scene::Skeleton3DComponent> afterSkeleton;
        Scene::Entity createdSkeletonEntity = Scene::kInvalidEntity;
        std::optional<Scene::SceneSnapshot> createdSkeletonSnapshot;
        EditorSelectionSnapshot selectionBefore;
        EditorSelectionSnapshot selectionAfter;
        bool dirtyBefore = false;
        bool dirtyAfter = false;
    };

    struct SceneComponentHistoryEntry final {
        Scene::Entity entity = Scene::kInvalidEntity;
        EditorSceneComponentKind kind = EditorSceneComponentKind::Camera;
        std::optional<EditorSceneComponentValue> before;
        std::optional<EditorSceneComponentValue> after;
        EditorSelectionSnapshot selectionBefore;
        EditorSelectionSnapshot selectionAfter;
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
    struct ComponentPropertyHistoryEntry final {
        Scene::Entity entity = Scene::kInvalidEntity;
        const Core::TypeMetadataEntry* metadata = nullptr;
        Core::TypeInstance before;
        Core::TypeInstance after;
        EditorSelectionSnapshot selectionBefore;
        EditorSelectionSnapshot selectionAfter;
        bool dirtyBefore = false;
        bool dirtyAfter = false;
    };

    struct CreationHistoryEntry final {
        EditorEntityKind kind = EditorEntityKind::Empty;
        std::string name;
        Scene::Entity activeEntity = Scene::kInvalidEntity;
        EditorSelectionSnapshot selectionBefore;
        EditorSelectionSnapshot selectionAfter;
        bool dirtyBefore = false;
        bool dirtyAfter = false;
    };

    /// A centralized scene object is restored from one complete authored snapshot so compound object
    /// creation (for example Character3D plus Collider and kinematic Rigid3D) is one history unit.
    struct SceneObjectCreationHistoryEntry final {
        Scene::SceneSnapshot snapshot;
        Scene::Objects::SceneObjectKind kind = Scene::Objects::SceneObjectKind::Object3D;
        Scene::Entity activeEntity = Scene::kInvalidEntity;
        EditorSelectionSnapshot selectionBefore;
        EditorSelectionSnapshot selectionAfter;
        bool dirtyBefore = false;
        bool dirtyAfter = false;
        /// The parent the object was created under; Redo restores the subtree back under it
        /// (falling back to the scene root) instead of dropping it to document top level.
        Scene::Entity createdUnderParent = Scene::kInvalidEntity;
    };

    /// A duplicated subtree is restored from a scene-envelope snapshot instead of relying on stale
    /// ECS handles. `activeEntity` is invalid after Undo and becomes a fresh root after Redo.
    struct DuplicationHistoryEntry final {
        Scene::SceneSnapshot snapshot;
        Scene::Entity originalParent = Scene::kInvalidEntity;
        Scene::Entity activeEntity = Scene::kInvalidEntity;
        std::optional<std::string> duplicateRootName;
        EditorSelectionSnapshot selectionBefore;
        EditorSelectionSnapshot selectionAfter;
        bool dirtyBefore = false;
        bool dirtyAfter = false;
        /// Where among its siblings the copy stands: just below the object it copies.
        std::size_t siblingIndex = 0U;
    };

    /// A deleted subtree is restored under its original parent with fresh handles on Undo.
    /// `activeEntity` begins as the deleted root's stale handle and changes to the restored root.
    struct DeletionHistoryEntry final {
        Scene::SceneSnapshot snapshot;
        Scene::Entity originalParent = Scene::kInvalidEntity;
        Scene::Entity activeEntity = Scene::kInvalidEntity;
        EditorSelectionSnapshot selectionBefore;
        EditorSelectionSnapshot selectionAfter;
        bool dirtyBefore = false;
        bool dirtyAfter = false;
        /// Where among its siblings the deleted root stood, so Undo puts it back there.
        std::size_t siblingIndex = 0U;
    };

    /// Several subtrees deleted as one step. Undo restores them in reverse order, so each sibling
    /// index lands in the layout it was captured from.
    struct MultiDeletionHistoryEntry final {
        std::vector<DeletionHistoryEntry> deletions;
        EditorSelectionSnapshot selectionBefore;
        EditorSelectionSnapshot selectionAfter;
        bool dirtyBefore = false;
        bool dirtyAfter = false;
    };

    /// A hierarchy move restores its parent, place among its siblings and authored local
    /// Transform atomically on replay. A move up or down is one with the same parent.
    struct ReparentHistoryEntry final {
        Scene::Entity entity = Scene::kInvalidEntity;
        Scene::Entity parentBefore = Scene::kInvalidEntity;
        Scene::Entity parentAfter = Scene::kInvalidEntity;
        Scene::TransformComponent localTransformBefore{};
        Scene::TransformComponent localTransformAfter{};
        EditorSelectionSnapshot selectionBefore;
        EditorSelectionSnapshot selectionAfter;
        bool dirtyBefore = false;
        bool dirtyAfter = false;
        std::size_t siblingIndexBefore = 0U;
        std::size_t siblingIndexAfter = 0U;
    };

    using HistoryEntry =
        std::variant<TransformHistoryEntry, NameHistoryEntry, PrimitiveAppearanceHistoryEntry,
                     MeshSourceHistoryEntry,
                     SceneComponentHistoryEntry, ComponentPropertyHistoryEntry,
                     CreationHistoryEntry, SceneObjectCreationHistoryEntry,
                     DuplicationHistoryEntry,
                     DeletionHistoryEntry, MultiDeletionHistoryEntry,
                     ReparentHistoryEntry>;

    /// The selected objects DeleteSelectedEntities() deletes, in selection order.
    [[nodiscard]] std::vector<Scene::Entity> CollectDeletableSelection();
    /// Captures `target`'s subtree, parent and sibling place, then destroys it; the selection and
    /// dirty fields are the caller's. Null, with nothing destroyed, when it cannot be captured.
    [[nodiscard]] std::optional<DeletionHistoryEntry> DestroyForDeletion(Scene::Entity target);

    /// Writes `text` to a project file: through the VFS when a mount covers `path`, else at `path`
    /// itself relative to the working directory.
    /// Written to a temporary and renamed, so a failed write never leaves a half-written file.
    [[nodiscard]] bool WriteProjectTextFile(const std::filesystem::path& path, std::string_view text);
    /// Reads a project file by the same rule as WriteProjectTextFile.
    [[nodiscard]] std::optional<std::string> ReadProjectTextFile(const std::filesystem::path& path) const;
    /// What an object's Script slot runs in the Entity Editor: a .uvs's text as edited (saved or not), or a
    /// Blueprint graph (.uvbp, the open graph or the file) with the name and kind it is compiled for.
    struct EntityScriptSource final {
        std::optional<std::string> text;
        std::optional<Asset::BlueprintAsset> graph;
        std::string objectName;
        std::string objectKind;
    };
    [[nodiscard]] std::optional<EntityScriptSource> ReadEntityScriptSource(const std::string& path, Scene::Entity entity) const;
    [[nodiscard]] bool IsDocumentEntity(Scene::Entity entity) const noexcept;
    [[nodiscard]] bool HasSceneGraphObject(Scene::Entity entity) const noexcept;
    /// True for any live document entity in the hierarchy, spatial or not. This, not
    /// HasSceneGraphObject, is what a parent needs: a pure Object such as the scene root holds
    /// children without having a transform of its own.
    [[nodiscard]] bool IsReparentableObject(Scene::Entity entity) const noexcept;
    [[nodiscard]] bool IsHierarchyObject(Scene::Entity entity) const noexcept;
    /// The world pose a child of `parent` composes its local transform from, by the rule
    /// SceneGraph::Update applies: identity for no parent and for a parent with no transform
    /// (a pure Object starts its children's transform chains), otherwise the parent's world transform.
    /// Null when `parent` is not a live document entity.
    [[nodiscard]] std::optional<Scene::WorldTransformComponent> TryGetComposingParentWorld(
        Scene::Entity parent) const;
    [[nodiscard]] bool IsTransformFinite(const Scene::TransformComponent& transform) const noexcept;
    [[nodiscard]] bool IsEntityNameValid(std::string_view name) const noexcept;
    [[nodiscard]] std::string GetEntityDisplayLabel(Scene::Entity entity) const;
    [[nodiscard]] std::string GetDefaultEntityName(EditorEntityKind kind) const;
    [[nodiscard]] std::string MakeUniqueDocumentEntityName(std::string_view baseName) const;
    [[nodiscard]] bool IsFiniteVector(const Math::Vector3& vector) const noexcept;
    [[nodiscard]] bool IsQuaternionFinite(const Math::Quaternion& quaternion) const noexcept;
    [[nodiscard]] bool AreTransformSnappingSettingsValid(
        const EditorTransformSnappingSettings& settings) const noexcept;
    [[nodiscard]] float SnapScalar(float value, float increment) const noexcept;
    [[nodiscard]] Math::Vector3 GetAxisVector(EditorTransformAxis axis) const noexcept;
    [[nodiscard]] bool ComputeLocalDeltaForWorldDelta(Scene::Entity entity,
                                                           const Math::Vector3& worldDelta,
                                                           Math::Vector3& outLocalDelta) const;
    [[nodiscard]] bool ComputeLocalRotationForWorldAxis(Scene::Entity entity,
                                                            const Math::Quaternion& initialLocalRotation,
                                                            const Math::Vector3& worldAxis, float radians,
                                                            Math::Quaternion& outLocalRotation) const;
    /// The transform `source` becomes after one axis operation, including snapping and the
    /// world-to-local conversion. Shared by the four public axis commands - which pass the LIVE
    /// transform, making them incremental - and by the gesture preview path, which passes the
    /// gesture BASELINE, making it absolute. One copy of the maths, so the two can never drift.
    /// For Scale, EditorTransformAxis::None means uniform; Translate and Rotate reject it.
    [[nodiscard]] bool ComputeGestureTransform(EditorToolSessionMode mode,
                                                   EditorTransformAxis axis, float amount,
                                                   const Scene::TransformComponent& source,
                                                   Scene::TransformComponent& outTransform) const;
    /// The translate half of ComputeGestureTransform, taking the world delta directly. The
    /// axis form is this with a delta of `axisVector * amount`.
    [[nodiscard]] bool ComputeTranslatedTransform(Scene::Entity entity,
                                                      const Math::Vector3& worldDelta,
                                                      const Scene::TransformComponent& source,
                                                      Scene::TransformComponent& outTransform) const;
    /// ComputeGestureTransform against the selected entity's live transform, behind the guards
    /// the four public commands share.
    [[nodiscard]] bool TryComputeSelectedGestureTransform(
        EditorToolSessionMode mode, EditorTransformAxis axis, float amount,
        Scene::TransformComponent& outTransform) const;
    [[nodiscard]] bool ApplyLocalTransform(Scene::Entity entity,
                                               const Scene::TransformComponent& transform);
    [[nodiscard]] bool ApplyEntityNameState(Scene::Entity entity,
                                                const std::optional<std::string>& name);
    [[nodiscard]] bool ApplyMeshSourceState(Scene::Entity entity, const Scene::MeshComponent& mesh,
                                               const std::optional<Scene::PrimitiveMeshComponent>& shape);
    [[nodiscard]] bool ApplyPrimitiveMeshState(Scene::Entity entity,
                                                    const Scene::PrimitiveMeshComponent& primitive);
    [[nodiscard]] bool IsSceneComponentValueValid(EditorSceneComponentKind kind,
                                                      const EditorSceneComponentValue& value) const noexcept;
    [[nodiscard]] bool AreSceneComponentValuesEqual(const EditorSceneComponentValue& lhs,
                                                        const EditorSceneComponentValue& rhs) const noexcept;
    [[nodiscard]] bool ApplySceneComponentState(
        Scene::Entity entity, EditorSceneComponentKind kind,
        const std::optional<EditorSceneComponentValue>& value);
    [[nodiscard]] bool IsDocumentSubtree(Scene::Entity root) const;
    [[nodiscard]] bool DoesSubtreeContainEntity(Scene::Entity root,
                                                    Scene::Entity candidate) const;
    [[nodiscard]] std::optional<Scene::SceneSnapshot> CaptureSubtree(Scene::Entity root);
    [[nodiscard]] Scene::Entity RestoreSubtreeUnderParent(const Scene::SceneSnapshot& snapshot,
                                                                 Scene::Entity parent);
    [[nodiscard]] bool TryGetDocumentParent(Scene::Entity entity, Scene::Entity& outParent) const;
    /// Returns one compact editor-only tag using the fixed primitive-first priority documented for
    /// the Scene Outliner. Empty means the entity is a plain document entity.
    [[nodiscard]] std::string GetOutlinerTypeTag(Scene::Entity entity) const;
    [[nodiscard]] std::vector<Scene::Entity> GetDocumentAncestry(Scene::Entity entity) const;
    [[nodiscard]] std::vector<Scene::Entity> GetEligibleReparentParents(Scene::Entity entity);
    [[nodiscard]] std::string GetHierarchyCandidateLabel(Scene::Entity entity) const;
    [[nodiscard]] bool IsLifecycleCommandAllowed() const noexcept;
    [[nodiscard]] bool IsAuthoringCommandAllowed() const noexcept;
    [[nodiscard]] EditorSelectionSnapshot CaptureSelectionSnapshot() const;
    void RestoreSelection(EditorSelectionSnapshot selection) noexcept;
    void PruneSelection() noexcept;
    [[nodiscard]] bool IsEntitySelected(Scene::Entity entity) const noexcept;
    [[nodiscard]] std::optional<EditorSelectionBounds> TryGetEntityBounds(Scene::Entity entity) const;
    [[nodiscard]] EditorSelectionPaths CaptureSelectionPaths(
        const std::vector<Scene::Entity>& roots) const;
    [[nodiscard]] EditorSelectionSnapshot ResolveSelectionPaths(
        const EditorSelectionPaths& paths, const std::vector<Scene::Entity>& roots) const;
    [[nodiscard]] Scene::Entity ResolveSelectionPath(
        const EditorSelectionPath& path, const std::vector<Scene::Entity>& roots) const;
    [[nodiscard]] bool FindSelectionPath(Scene::Entity current, Scene::Entity target,
                                             std::vector<std::size_t>& inOutChildIndices) const;
    [[nodiscard]] bool ReparentDocumentEntity(Scene::Entity entity, Scene::Entity newParent);
    [[nodiscard]] bool ComputeKeepWorldLocalTransform(Scene::Entity entity, Scene::Entity newParent,
                                                          Scene::TransformComponent& outTransform) const;
    [[nodiscard]] bool IsReparentModeChangeAllowed() const noexcept;
    [[nodiscard]] bool IsHierarchyFilterActive() const noexcept;
    [[nodiscard]] bool IsHierarchyEntityVisible(Scene::Entity entity) const;
    /// True when an Outliner row stands for a whole entity asset that has been placed in the level,
    /// rather than for an object the level itself owns.
    ///
    /// Such a row is a leaf here. The objects inside it are the entity's own scene - they belong to
    /// the Entity Editor, and showing them in the level's tree made a placed entity look like a
    /// folder whose contents were loose objects the level could select, move and delete. They are
    /// still in the document, still drawn and still simulated; the level simply addresses the
    /// entity as one thing, the same way it addresses a mesh as one thing rather than as its
    /// triangles. While the Entity Editor has the entity open it IS the document, so nothing is
    /// sealed and its own tree is drawn in full.
    [[nodiscard]] bool IsSealedHierarchyRow(Scene::Entity entity) const noexcept;
    /// The row that actually represents `entity` in the Outliner: the entity itself, or - when it
    /// lives inside a placed entity - the instance that owns it. Selecting, focusing and inspecting
    /// go through this, so a click that lands inside a placed entity cannot select an object the
    /// tree does not show.
    [[nodiscard]] Scene::Entity ResolveHierarchyRowOwner(Scene::Entity entity) const noexcept;
    void RebuildHierarchyFilterCache();
    void InvalidateHierarchyFilterCache() noexcept;
    void CancelHierarchyRename() noexcept;
    [[nodiscard]] Scene::Entity CreateDocumentEntityInternal(
        EditorEntityKind kind, const std::optional<std::string>& explicitName);
    /// Creates the document-entity shell every scene object starts from: a live entity with a
    /// default TransformComponent and the given (already finalized) NameComponent.
    /// Object definitions (Engine/Runtime/Objects/3D) attach their kind-specific components on top.
    [[nodiscard]] Scene::Entity CreateDocumentEntityShellInternal(const std::string_view name);

    /// Returns whether `entity` carries the scene-root marker. The root is never deletable,
    /// re-parentable, or duplicable - every one of those commands checks this first.
    [[nodiscard]] bool IsSceneRootEntity(Scene::Entity entity) const;
    /// An object the tree is built on and that cannot be deleted, duplicated or moved: the scene
    /// root, and while the Entity Editor is open, the entity's own root.
    [[nodiscard]] bool IsStructuralRoot(Scene::Entity entity);

    /// Returns the document's scene root when one exists, else creates it (name + transform +
    /// marker via the SceneRoot ObjectDefinition). Idempotent: the one-root invariant every
    /// document seam relies on is established or confirmed on every call.
    [[nodiscard]] Scene::Entity EnsureDocumentSceneRoot();
    /// Creates a document entity for one object kind from that kind's ObjectDefinition: a
    /// uniquely-named entity shell plus the definition's component recipe. Defined in
    /// editor_uve.cpp next to its only call sites.
    /// A new, typed, unparented object of `kind` with no undo step - the recipe both the Add menus
    /// and the Content catalogue build from.
    [[nodiscard]] Scene::Entity CreateSceneObjectEntityInternal(Scene::Objects::SceneObjectKind kind);
    template <typename Definition, typename ApplyFunc>
    [[nodiscard]] Scene::Entity CreateObjectDefinitionEntityInternal(const Definition& definition,
                                                                         ApplyFunc applyDefinition);
    void RecordHistory(HistoryEntry entry);
    void ClearHistory() noexcept;
    [[nodiscard]] bool UndoHistoryEntry(HistoryEntry& entry);
    [[nodiscard]] bool RedoHistoryEntry(HistoryEntry& entry);
    void DestroyDocumentSubtree(Scene::Entity root);
    void ClearDocumentScene();
    void LoadSessionSettings();
    [[nodiscard]] bool SaveSessionSettings();
    void ApplyLayoutPreset(EditorLayoutPreset preset) noexcept;
    /// Turns the viewport's preview sun / preview sky into the scene's own DirectionalLight3D /
    /// WorldEnvironment3D (creating it, or updating the one there), selects it and returns it.
    /// Invalid when authoring is not allowed.
    Scene::Entity AddPreviewSunToScene();
    Scene::Entity AddPreviewEnvironmentToScene();
    // One settings window's view state (Editor Preferences, Project Settings). Session-only.
    struct SettingsWindowState final {
        bool visible = false;
        bool focusPending = false;
        bool focusSearch = false;
        bool modifiedOnly = false;
        bool showAdvanced = false;
        bool restartRequiredBaselineCaptured = false;
        std::array<char, 128> search{};
        std::string category;
        std::string activeKeyBindingId;
        std::unordered_map<std::string, Config::SettingValue> restartRequiredBaseline;
    };
    // The Input Map window (editor_panel_input_map_uve.cpp). Session-only view state.
    struct InputMapWindowState final {
        bool visible = false;
        bool focusPending = false;
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
    // Commands (editor_commands_uve.cpp).
    struct CommandPaletteState final {
        bool open = false;
        bool focus = false;
        std::array<char, 128> query{};
        int highlighted = 0;
    };
    struct ShortcutsWindowState final {
        bool visible = false;
        bool focusPending = false;
        std::array<char, 64> filter{};
        bool listening = false;
        std::size_t listenCommand = 0U;
        std::size_t listenSlot = 0U;
    };
    void RegisterEditorCommands();
    [[nodiscard]] EditorCommand* FindEditorCommand(std::string_view id) noexcept;
    /// Shortcut `slot` of command `id` set (and kept with the preferences); the defaults back.
    bool AssignCommandShortcut(std::string_view id, std::size_t slot, const EditorShortcut& shortcut);
    void ResetCommandShortcuts(std::string_view id);
    /// The commands (as "Category: Label") that also use `shortcut`, other than `exceptId`.
    [[nodiscard]] std::vector<std::string> CommandsUsingShortcut(const EditorShortcut& shortcut, std::string_view exceptId) const;
    [[nodiscard]] static std::string CommandTitleText(const EditorCommand& command);
    [[nodiscard]] static bool IsCommandEnabled(const EditorCommand& command);
    // Shortcuts as the hidden settings "editor.shortcuts.<command>.primary|alternate".
    [[nodiscard]] std::optional<Config::SettingValue> GetShortcutSetting(std::string_view id) const;
    [[nodiscard]] bool SetShortcutSetting(std::string_view id, const Config::SettingValue& value);
    // Sets the project's input map and registers it with the input system at once.
    void CommitInputMap(std::vector<Input::InputAction> actions);
    /// A double-click on a row that stands for a file opens it: an entity asset behind an instance
    /// (Entity Editor), or the .uvscene behind the level's Viewport. True when the row had something
    /// to open, so the Double-Click preference must not also act on it.
    [[nodiscard]] bool OpenHierarchyRowTarget(Scene::Entity entity);
    /// A type a section may draw inside itself, and the hosts that type prefers over this one.
    struct NestedMetadataSection final {
        const Core::TypeMetadataEntry* entry = nullptr;
        std::vector<const Core::TypeMetadataEntry*> preferredHosts;
    };
    /// Applies the component add or remove a row asked for on the previous frame, before this
    /// frame's sections take any pointer into component storage. See m_pendingComponentChange.
    void ApplyPendingComponentChange();
    /// A combo over the project's assets with `extension` (".uvanim"), with a Browse row that
    /// opens the floating picker. Returns the pick, if any; kInvalidAssetGuid means "(none)"
    /// was picked. `title` names the picker window, defaulting to "Browse <extension>".
    [[nodiscard]] std::optional<Asset::AssetGuid> DrawAssetPicker(const char* id, Asset::AssetGuid value,
                                                                      const std::string& extension,
                                                                      const std::string& title = {});
    /// The texture asset a slot should hold for the file the author picked: the published `.uvtex`
    /// beside a source image when one is already there, otherwise the source imported to one now,
    /// otherwise the source itself registered - the last case is a slot that still draws (the
    /// runtime loader decodes a source image) rather than one silently left empty.
    ///
    /// A material stores a GUID, and a GUID that names a raw source is an authoring convenience the
    /// cook step does not accept: publishing on pick is what makes what the picker writes and what
    /// the project can ship the same thing.
    [[nodiscard]] Asset::AssetGuid PublishTextureSource(const std::filesystem::path& absolutePath);
    /// The source image a texture is made from: itself when it is one, else the image of the same name
    /// beside a `.uvtex` (where publishing puts it). Empty when there is none.
    [[nodiscard]] static std::filesystem::path FindTextureSource(const std::filesystem::path& texturePath);
    /// How the source image `source` is imported: its `.uvimport` sidecar when it has one, else the defaults.
    [[nodiscard]] static Asset::TextureImportSettings LoadTextureImportSettings(const std::filesystem::path& source);
    /// Saves `settings` as `source`'s sidecar and imports it again to the `.uvtex` beside it, so every
    /// material and slot using that texture shows the new import. False when the import fails.
    [[nodiscard]] bool ReimportTexture(const std::filesystem::path& source, const Asset::TextureImportSettings& settings);
    /// The mesh of a model file: a `.uvmodel` is registered as it stands, and a model source
    /// (.fbx/.glb/.gltf/.obj) is copied into Content when it is outside it and then converted, so
    /// the GUID a mesh slot receives is always one the mesh loader can actually read.
    ///
    /// This is the other half of the reason a mesh slot must never be handed a source file directly:
    /// LoadMeshAsset reads the `.uvmodel` envelope and nothing else, so registering an FBX put a
    /// name in the slot that resolved to a path and then failed to load every frame - a mesh the
    /// author could see assigned and never see drawn. Textures have the same shape of problem and
    /// solve it the same way (see PublishTextureSource), except there the runtime can decode the
    /// source as a fallback and here it cannot, so a conversion that fails yields no GUID at all
    /// rather than a reference that is guaranteed to stay invisible.
    [[nodiscard]] Asset::AssetGuid PublishModelSource(const std::filesystem::path& absolutePath);
    /// The normal map a colour picture implies, published beside it as <stem>_normal.uvtex: slope
    /// derived from the picture's own light and shade, so a material that only has an albedo still
    /// catches a light the way a surface does. An already derived map beside the albedo is reused
    /// rather than derived again. Empty when the texture cannot be read or the map cannot be saved.
    [[nodiscard]] std::optional<Asset::AssetGuid> DeriveNormalMapForAlbedo(Asset::AssetGuid albedo);
    struct PendingMaterialEditor final {
        std::filesystem::path absolutePath;
        std::string id;
    };
    std::vector<PendingMaterialEditor> m_pendingMaterialEditors;
    /// Auto-populates a SkinComponent from an entity's assigned model or attached Skeleton3D.
    bool AutoPopulateSkinFromModel(Scene::Entity entity, Scene::SkinComponent& skin);
    /// The mesh asset an entity draws with, read through the `.uvmodel` the importer wrote beside a
    /// model source. Empty when nothing is assigned, the file will not read, or the entity draws a
    /// procedural primitive (which owns no asset).
    [[nodiscard]] std::optional<Asset::MeshAsset> LoadEntityMeshAsset(Scene::Entity entity);
    /// Points the selected Skeleton3D at the model source `relativeSource` (content-relative) and
    /// loads its bones; an empty path clears both. One undo step. False, with the reason in
    /// m_skeletonSourceStatus, when the file has no readable skeleton.
    bool BindSelectedSkeletonSource(const std::filesystem::path& relativeSource);
    /// The world-space bones of every enabled Skeleton3D in the document, for the viewport.
    void BuildSkeletonOverlay(std::vector<ViewportBone>& outBones) const;
    /// The world-space wireframe of every collider in the document, for the viewport's "Show
    /// Collision" overlay. Empty while `collisionShapesVisible` is off. One entity pass, whole
    /// shapes only, bounded by `ColliderDebugSceneSettings`.
    void BuildColliderOverlay(std::vector<ViewportColliderLine>& outLines) const;
    /// Turns the collider wireframe overlay on or off. Returns whether the value changed; the lines
    /// themselves are rebuilt on the next frame that draws the viewport.
    bool SetViewportCollisionShapes(bool visible);
    /// True when `instance`'s value for `property` equals what a newly added component holds. False
    /// when that cannot be known (no equality for the type), so a revert is offered rather than hidden.
    [[nodiscard]] bool IsPropertyAtDefault(const Core::TypeMetadataEntry& entry,
                                              const Core::TypeMetadataProperty& property, const void* instance);
    /// Replaces the selected entity's whole component of `entry`'s type with `newInstance`, as one
    /// undo step, when the type's own rule accepts it. For edits that change several fields at once.
    bool SetSelectedComponentValue(const Core::TypeMetadataEntry& entry, const void* newInstance);
    /// Writes one property of one component on the selected entity and records one undo entry.
    /// Refuses when authoring is unavailable, the selection is not a single document entity, the
    /// entity does not hold the component, the property is not authoring-writable, or the value is
    /// unchanged - matching what the per-type commands already refuse.
    [[nodiscard]] bool SetSelectedComponentProperty(const Core::TypeMetadataEntry& entry,
                                                       const Core::TypeMetadataProperty& property,
                                                       const void* newValue);
    /// A continuous edit of one property - a colour picker session - shown live but recorded as
    /// ONE undo entry when it ends, the way a transform drag is. Preview writes the value without
    /// history (refused, leaving the previous value, when the component's rule rejects it); the
    /// first preview captures the component to restore. A preview of another entity, component
    /// or property first commits the one in flight.
    [[nodiscard]] bool PreviewSelectedComponentProperty(const Core::TypeMetadataEntry& entry,
                                                           const Core::TypeMetadataProperty& property,
                                                           const void* newValue);
    /// Ends the edit in flight with one history entry from where it started to where it is. An
    /// edit that ended where it began records nothing and leaves the scene's dirty flag as it was.
    [[nodiscard]] bool CommitComponentPropertyPreview();
    /// Ends the edit in flight by putting the component back as it was, with no history.
    bool CancelComponentPropertyPreview();
    /// Commits the edit in flight only if it is of this property; any other is left alone.
    [[nodiscard]] bool CommitComponentPropertyPreviewFor(const Core::TypeMetadataEntry& entry,
                                                            const Core::TypeMetadataProperty& property);
    /// Restores one property to the value a default-constructed component would have.
    [[nodiscard]] bool ResetSelectedComponentProperty(const Core::TypeMetadataEntry& entry,
                                                         const Core::TypeMetadataProperty& property);
    /// Overwrites a live component with a recorded snapshot of it, for undo and redo. Fails
    /// without mutation when the entity is gone or no longer holds the component, which is what
    /// makes a stale history entry clear the history rather than corrupt the scene.
    [[nodiscard]] bool ApplyComponentPropertySnapshot(Scene::Entity entity,
                                                         const Core::TypeMetadataEntry* metadata,
                                                         const void* snapshot);
    /// Extension-only fallback classification used before model-source metadata is available.
    [[nodiscard]] static AssetTableItemType ClassifyAssetTableEntry(
        const Asset::ProjectFileEntry& entry);
    /// Classifies a browser entry using the source summary from the latest project refresh, so
    /// animation-only FBX sources are not presented as static meshes and rigged sources as meshes.
    [[nodiscard]] AssetTableItemType GetAssetTableEntryType(
        const Asset::ProjectFileEntry& entry) const;
    [[nodiscard]] static const char* GetAssetTableItemTypeLabel(AssetTableItemType type) noexcept;
    [[nodiscard]] static const char* GetAssetTableFocusLabel(AssetTableTypeFocus focus) noexcept;
    [[nodiscard]] bool DoesAssetTableEntryMatchFocus(const Asset::ProjectFileEntry& entry) const;
    [[nodiscard]] bool IsAssetTableDirectoryInSnapshot(const Asset::ProjectFileSnapshot& snapshot,
                                                               const std::filesystem::path& directory) const;
    void ReconcileAssetTableDirectory(const Asset::ProjectFileSnapshot& snapshot) noexcept;
    [[nodiscard]] bool IsProjectPathFavorited(const std::filesystem::path& relativePath) const;
    void ToggleProjectPathFavorite(const std::filesystem::path& relativePath);
    static constexpr std::size_t kMaxPersistedFavoriteProjects = 128U;
    /// The team's shelves live beside project.uvsettings, in project.uvshelves.
    [[nodiscard]] std::filesystem::path GetSharedShelvesPath() const;
    /// Replaces the shared shelves with the file's; no file means none. Personal shelves stay.
    void LoadSharedShelves();
    /// Writes the shared shelves; true when written or when there was nothing to write.
    bool SaveSharedShelves();
    /// Loads the file again when it changed on disk since it was last read or written.
    void ReloadSharedShelvesIfChanged();
    /// Size and last change of a content file, from m_contentFileFacts (read on first ask).
    [[nodiscard]] ContentFileFacts GetContentFileFacts(const std::filesystem::path& contentRoot,
                                                             const Asset::ProjectFileEntry& entry,
                                                             std::uint64_t refreshGeneration);
    /// Returns a GL texture id showing relativePath's own decoded image content, loading and
    /// uploading it on first request and caching the result thereafter. Returns 0 if the file
    /// cannot be loaded as a texture asset (not a texture, corrupt, or an unsupported pixel
    /// format) - callers should fall back to the generic per-type icon in that case.
    [[nodiscard]] std::uintptr_t GetTextureThumbnail(const std::filesystem::path& relativePath);
    void ClearTextureThumbnailCache() noexcept;
    /// Returns a GL texture id previewing relativePath's own mesh geometry (fixed camera angle,
    /// no material), rendering and caching it on first request. Returns 0 if the file cannot be
    /// loaded as a mesh asset or has no vertices/indices - callers should fall back to the
    /// generic per-type icon in that case.
    [[nodiscard]] std::uintptr_t GetMeshThumbnail(const std::filesystem::path& relativePath);
    void ClearMeshThumbnailCache() noexcept;
    /// Returns a GL texture id previewing relativePath's material: its albedo, normal,
    /// metallic-roughness, occlusion and emissive inputs shaded with a physically based model on a
    /// fixed preview sphere, rendered and cached on first request. Texture references resolve
    /// through the asset database; an unset or unresolvable slot falls back to the material's flat
    /// value. Returns 0 if the file cannot be loaded as a material asset - callers should fall
    /// back to the generic per-type icon in that case.
    [[nodiscard]] std::uintptr_t GetMaterialThumbnail(const std::filesystem::path& relativePath);
    void ClearMaterialThumbnailCache() noexcept;
    /// Drops one material's cached preview after its asset was saved, so the Asset Table shows
    /// the edit. `absolutePath` is the material file the Inspector wrote; it is matched against the
    /// project content root and ignored when outside it.
    void InvalidateMaterialThumbnailCache(const std::filesystem::path& absolutePath);
    /// Refreshes the read-only project index after the engine-owned watcher observes a new
    /// filesystem baseline. It never schedules imports or mutates project files.
    void RefreshProjectFileIndex();
    void BeginContentRename(const std::filesystem::path& relativePath);
    /// Model sources (.glb/.gltf/.obj) are imported automatically: the source stays the thing an
    /// author sees and picks, and its converted mesh lives in the derived-data folder beside the
    /// import cache, never in the content folder.
    [[nodiscard]] static bool IsModelSourcePath(const std::filesystem::path& path);
    /// Where the converted mesh of the model source at `relativeSource` (content-relative) lives.
    [[nodiscard]] std::filesystem::path GetImportedModelPath(const std::filesystem::path& relativeSource) const;
    /// The skeleton a model source gives a Skeleton3D: the imported model's when it has been
    /// conformed to the humanoid (Retarget), else the source file's own bones.
    [[nodiscard]] std::optional<Asset::GltfSkeleton> ReadSkeletonForModelFile(const std::filesystem::path& importedModelPath,
                                                                                    std::size_t maximumBones) const;
    [[nodiscard]] std::optional<Asset::GltfSkeleton> ReadSkeletonForSource(const std::filesystem::path& relativeSource,
                                                                                 std::size_t maximumBones) const;
    /// Queues an import for every model source in `snapshot` that is not already in flight. An
    /// unchanged source is a cache hit in the import queue, so this costs a hash, not a re-import.
    void QueueModelAutoImports(const Asset::ProjectFileSnapshot& snapshot);
    /// A model's takes go into it a few at a time each frame (all at once without the PRISM UI,
    /// where nothing draws between frames anyway), so a project of hundreds never stalls a frame.
    void QueueModelAnimationImport(const std::filesystem::path& absoluteSource);
    void ProcessPendingModelAnimationImports(double budgetSeconds);
    void UpdateStartupLoading();
    [[nodiscard]] std::size_t PendingAnimationIconCount() const;
    std::deque<std::filesystem::path> m_pendingModelAnimations;
    std::unordered_set<std::string> m_pendingModelAnimationKeys;
    std::size_t m_modelAnimationBatchTotal = 0U;
    std::size_t m_modelAnimationBatchDone = 0U;
    /// The loading screen at start: until the project is scanned, its models imported and the
    /// first animation previews drawn.
    bool m_startupLoading = true;
    int m_startupStep = 0;
    std::chrono::steady_clock::time_point m_startupBegan{};
    std::chrono::steady_clock::time_point m_startupStepBegan{};
    std::size_t m_startupIconPeak = 0U;
    /// Collects finished auto-imports; a successful one refreshes the mesh thumbnails.
    void PollModelImportJobs();
    [[nodiscard]] Asset::IAssetImportQueue& ActiveModelImportQueue() const noexcept;
    void EnsureBackgroundModelImportWorker();
    void StopBackgroundModelImportWorker();
    void EnqueueBackgroundExternalCopies(std::vector<std::pair<std::filesystem::path, std::filesystem::path>> items,
                                            std::vector<std::filesystem::path> cleanupDirs);
    void EnqueueModelSourceIfReady(const std::string& key, const std::filesystem::path& source,
                                      const EditorModelSourceInfo& info,
                                      std::uint32_t maxEmbeddedTextureResolution);
    void TickExternalContentWatcher();
    void SeedObservedContentTree(const Asset::ProjectFileSnapshot& snapshot);
    /// Registers the converted mesh at `importedModel` and every published asset beside it -
    /// its `.uvmat` materials, its `.uvtex` textures, its `.uvskel` rig.
    ///
    /// The importer registers those as it writes them, but a cached import is a cache hit for the
    /// CONVERSION, not for the asset database: the importer never runs, so nothing it would have
    /// registered is registered. A mesh that names those GUIDs then draws nothing at all - the
    /// materialed path drops it because the material will not resolve, and the built-in path
    /// stands down because the mesh appears to have a material. Registering the folder closes
    /// that gap whether or not this session ran the conversion.
    /// Returns the converted mesh's GUID, or `kInvalidAssetGuid` when it is not there.
    [[nodiscard]] Asset::AssetGuid RegisterImportedModelArtifacts(const std::filesystem::path& importedModel);
    /// What the last project refresh read from a model source's file, or null if it is not one.
    [[nodiscard]] const EditorModelSourceInfo* FindModelSourceInfo(const std::filesystem::path& relativeSource) const;
    /// The animation-only model source a `.uvanim` clip was imported from (`<source>.uvanim` or
    /// `<source>_<take>.uvanim` beside it), by content-relative path; empty when it has none.
    [[nodiscard]] std::filesystem::path FindAnimationClipSource(const std::filesystem::path& relativeClip) const;
    /// What a model source's file holds, read now if the last refresh did not cover it.
    ///
    /// The refresh cache is an optimisation for the panels that draw many entries; it must never be
    /// the reason an action fails. A placement that ran before the Asset Table had refreshed
    /// used to see "no information" and treat a bones-only animation as a model whose mesh had not
    /// been converted yet, so it placed nothing and said it was still importing. Reading the file
    /// here is one bounded summary - no geometry, no curves - and it is remembered for next time.
    [[nodiscard]] EditorModelSourceInfo ReadModelSourceInfoForSource(const std::filesystem::path& relativeSource);
    /// Why the last import of `relativeSource` failed - the importer's own diagnostic, the same text
    /// the log file carries - or null when it has not failed. Kept after the job leaves the queue so
    /// the editor can say what went wrong instead of only the log.
    [[nodiscard]] const std::string* FindModelImportFailure(const std::filesystem::path& relativeSource) const;
    /// True for a model source whose file declares a skeleton (read once per project refresh).
    [[nodiscard]] bool IsRiggedModelSource(const std::filesystem::path& relativeSource) const;

    Core::EngineServices* m_services = nullptr;
    /// Level-document services (never swapped by EntityDocumentScope); safe for the background import worker.
    Core::EngineServices* const m_levelServices = nullptr;
    /// The editor's own settings, declared by RegisterEditorSettings; read and written in the
    /// services' settings store.
    Config::SettingsRegistry m_settingsRegistry;
    Config::SettingsObserverHub m_settingsObservers{m_settingsRegistry};
    // Object creation preferences (editor_settings_uve.cpp), and the host's latest camera focus.
    bool m_newObjectsUnderSelection = true;
    EditorNewObjectPlacement m_newObjectPlacement = EditorNewObjectPlacement::ParentOrigin;
    std::optional<Math::Vector3> m_viewportCameraFocus;
    /// One-shot world position for the next placed object (content dropped into the viewport lands
    /// where it was dropped). Consumed by PlaceNewDocumentObject whatever the placement setting.
    std::optional<Math::Vector3> m_nextPlacementPoint;
    // Play mode preferences (editor_settings_uve.cpp).
    bool m_playPauseOnStart = false;
    bool m_playSaveSceneFirst = false;
    bool m_playSwitchToGame = true;
    static constexpr ViewportAxisColor kDefaultPlayTintColor{0.30F, 0.48F, 0.80F};
    static constexpr float kDefaultPlayTintStrength = 0.2F;
    bool m_playTintEnabled = true;
    ViewportAxisColor m_playTintColor = kDefaultPlayTintColor;
    float m_playTintStrength = kDefaultPlayTintStrength;
    // Hierarchy panel preferences (editor_settings_uve.cpp).
    HierarchyViewSettings m_hierarchyView;
    std::unordered_set<Scene::Entity> m_lockedFolders;
    // Each editor setting's descriptor and its reads and writes of the state above, in one table
    // (editor_settings_uve.cpp) that loading, saving and the preferences window all use.
    [[nodiscard]] static const std::vector<EditorSettingBinding>& GetSettingBindings();
    [[nodiscard]] static const EditorSettingBinding* FindSettingBinding(std::string_view id);
    void NotifyEditorSettingChanged(std::string_view id, const Config::SettingValue& previousValue,
                                       const Config::SettingValue& newValue);
    // Where a new object goes: under the single selection when the preference allows and there is
    // one, otherwise under the scene root. Then, for a spatial object, where in space.
    [[nodiscard]] Scene::Entity ResolveNewObjectParent();
    /// Where a new object of `kind` goes: DirectionalLight3D and WorldEnvironment at the top, a folder
    /// in the selected folder or the Viewport, anything else in a folder (see ResolveObjectFolder).
    [[nodiscard]] Scene::Entity ResolveNewObjectParentFor(Scene::Objects::SceneObjectKind kind);
    /// The parent a new object may actually be given. A placed entity is one row in the level and
    /// its contents are the Entity Editor's, so asking to put something under it - or under anything
    /// already inside it - would create an object no window shows: it would exist, collide, and be
    /// impossible to select. Such a request lands beside the entity instead, under the level parent
    /// the row really has.
    [[nodiscard]] Scene::Entity ResolvePlaceableParent(Scene::Entity candidate);
    // ---- The level's Outliner layout ---------------------------------------------------------------
    //   (scene root, not shown)
    //   +- Viewport            folders only
    //   |  +- Folder ...       any object
    //   +- DirectionalLight3D  one at most
    //   +- WorldEnvironment    one at most
    /// Whether the layout applies: the level is the document (no entity, no Retarget preview open).
    [[nodiscard]] bool IsOutlinerLayoutActive() const noexcept;
    /// Builds the layout if missing: the Viewport, and anything a level saved before the layout had
    /// at the top moved into a folder - one made only if there is a stray to put in it, so an empty
    /// level opens with no folder holding nothing. True when it changed anything.
    bool EnsureDocumentLayout();
    /// The folder a new object goes into when none is selected: the last one used, else the Viewport's
    /// first, else a new "World".
    [[nodiscard]] Scene::Entity ResolveObjectFolder();
    /// Whether `entity` may sit under `parent` in the layout.
    [[nodiscard]] bool IsAllowedOutlinerParent(Scene::Entity entity, Scene::Entity parent);
    /// DirectionalLight3D and WorldEnvironment: the level's two top-level singletons.
    [[nodiscard]] bool IsTopLevelSingletonKind(Scene::Objects::SceneObjectKind kind) const noexcept;
    /// The top-level DirectionalLight3D or WorldEnvironment, when the level has it.
    [[nodiscard]] Scene::Entity FindTopLevelObject(Scene::Objects::SceneObjectKind kind);
    Scene::Entity m_lastUsedFolder = Scene::kInvalidEntity;
    void PlaceNewDocumentObject(Scene::Entity entity);
    friend bool RegisterEditorSettings(Config::SettingsRegistry& registry);
    Core::ISimulationControl* m_simulationControl = nullptr;
    EditorState m_state = EditorState::Uninitialized;
    EditorPlayModeState m_playModeState = EditorPlayModeState::Edit;
    std::optional<PlayModeSession> m_playModeSession;
    bool m_playBootSplashActive = false;
    bool m_playBootSplashSkippable = true;
    bool m_playBootSplashSkipRequested = false;
    double m_playBootSplashFadeSeconds = 0.25;
    double m_playBootSplashMinimumSeconds = 1.0;
    std::chrono::steady_clock::time_point m_playBootSplashStart{};
    std::array<float, 4U> m_playBootSplashBackground{0.0F, 0.0F, 0.0F, 1.0F};
    std::filesystem::path m_playBootSplashImagePath;
    Math::Vector2 m_playBootSplashImageDimensions{};
    std::optional<EntityEditSession> m_entityEditSession;
    std::optional<RetargetWindowState> m_retargetWindow;
    struct AnimationLibraryAddQueueState final {
        std::filesystem::path libraryPath;
        Asset::AnimationLibraryAsset library;
        std::deque<std::filesystem::path> pendingClips;
        std::size_t totalCount = 0U;
        std::size_t processedCount = 0U;
        std::size_t addedCount = 0U;
        std::size_t dirtyUnsavedCount = 0U;
        bool databaseTouched = false;
    };
    std::optional<AnimationLibraryWindowState> m_animationLibraryWindow;
    std::optional<AnimationLibraryAddQueueState> m_animLibraryAddQueue;
    std::optional<FilePickerState> m_filePicker;
    std::optional<EntityClassPickerState> m_entityClassPicker;
    /// Browse picks waiting for their widget: widget id (kept as uint32_t like
    /// m_inspectorTextEdits) to the pick. Asset combos take GUIDs, file rows take paths; each is
    /// consumed by its own drawer on the next frame.
    std::unordered_map<std::uint32_t, Asset::AssetGuid> m_assetPickerPendingPicks;
    std::unordered_map<std::uint32_t, std::string> m_fileRowPendingPicks;
    /// A structural edit the Inspector asked for while drawing a row.
    ///
    /// Adding or removing a component moves the entity into a different archetype, which frees the
    /// chunk the row's payload pointer points into. Doing it inline would leave every row after the
    /// button reading freed memory for the rest of the frame, so the request is recorded here and
    /// applied at the top of the next component draw, before any payload pointer is taken.
    enum class PendingComponentChange : std::uint8_t { None, AddSky, RemoveSky };
    PendingComponentChange m_pendingComponentChange = PendingComponentChange::None;
    Scene::Entity m_pendingComponentChangeEntity = Scene::kInvalidEntity;
    std::optional<RetargetPreview> m_retargetPreview;
    // Which workspace tab was active before EnterPlayMode() switched to Game, so StopPlayMode()
    // can restore it - mirrors Unity's own Scene<->Game auto-switch on Play/Stop.
    EditorWorkspace m_workspaceBeforePlayMode = EditorWorkspace::Library;
    bool m_gameMouseCaptured = false;
    bool m_viewportFlying = false;
    bool m_viewportFlyHeld = false;
    void ReleaseGameMouseOnFocusLoss();
    bool m_playFastForwardEnabled = false;
    float m_playFastForwardMultiplier = 2.0F;
    // 0 = plain Play triangle, 1 = Pause bars; eased toward the target each frame in
    // DrawMenuBar() so the icon animates instead of instantly swapping shape.
    float m_playButtonMorphProgress = 0.0F;
    std::vector<Scene::Entity> m_selectedEntities;
    Scene::Entity m_selectedEntity = Scene::kInvalidEntity;
    std::filesystem::path m_activeScenePath;
    std::size_t m_historyCapacity = 100U;
    EditorReparentTransformMode m_reparentTransformMode = EditorReparentTransformMode::KeepLocal;
    EditorTransformSnappingSettings m_transformSnappingSettings{};
    EditorToolSession m_toolSession;
    Editor2DCanvasState m_2dCanvasState{};
    // Transient editor-viewport bookmark slots (Set/Get/ClearViewportBookmark) - session
    // state only, intentionally NOT part of any document or settings file.
    std::array<std::optional<EditorViewportBookmark>, kEditorViewportBookmarkSlotCount>
        m_viewportBookmarks{};
    bool m_2dCanvasPanning = false;
    std::deque<HistoryEntry> m_undoHistory;
    std::deque<HistoryEntry> m_redoHistory;

    EditorWorkspace m_activeWorkspace = EditorWorkspace::Library;
    /// Transient Plugin window/tool gates. These are editor-session state only and never become ECS
    /// components, serialized scene data, or runtime/plugin activation side effects.
    bool m_pluginWindowVisible = false;
    // The Editor Preferences and Project Settings windows (editor_panel_preferences_uve.cpp).
    SettingsWindowState m_preferencesWindow;
    SettingsWindowState m_projectSettingsWindow;
    InputMapWindowState m_inputMapWindow;
    std::vector<EditorCommand> m_commands;
    std::deque<std::string> m_recentCommandIds;
    CommandPaletteState m_commandPalette;
    ShortcutsWindowState m_shortcutsWindow;
    EditorRightPanelTab m_activeRightPanelTab = EditorRightPanelTab::Inspector;
    /// The tab the strip showed last frame, to tell a click in it from a change made elsewhere.
    EditorRightPanelTab m_drawnRightPanelTab = EditorRightPanelTab::Inspector;
    InspectorDrawerRegistry m_inspectorDrawerRegistry;
    DeveloperConsole m_developerConsole;
    EditorBottomDock m_activeBottomDock = EditorBottomDock::FileSystem;
    /// Empty is the ProjectFileIndex content root. This value is session-only and must name a
    /// directory in the latest successful copied snapshot before it is used as a browser location.
    std::filesystem::path m_assetTableDirectory;
    /// The Entity Editor's Asset Table is docked as a tab (else it is a drawer that slides up).
    bool m_entityContentDocked = false;
    /// User-curated shortcuts into the project tree, as project-relative generic paths; both
    /// directories and files may be favorited. Persisted across sessions (see
    /// Save/LoadSessionSettings). An entry no longer present in the latest snapshot is simply
    /// not shown, never pruned from storage here, so a not-yet-scanned favorite is not lost.
    std::vector<std::filesystem::path> m_favoriteProjectPaths;
    // Inspector fold states by key; saved with the session.
    std::map<std::string, bool> m_inspectorFoldOpen;
    // Inspector section clipboard (see CopySelectedComponent / CopySelectedTransform).
    struct ComponentClipboard final {
        const Core::TypeMetadataEntry* entry = nullptr;
        Core::TypeInstance value;
    };
    std::optional<ComponentClipboard> m_componentClipboard;
    /// The Outliner's object clipboard (CopySelectedObject / PasteObject).
    std::optional<Scene::SceneSnapshot> m_objectClipboard;
    std::optional<Scene::TransformComponent> m_transformClipboard;
    /// Whether scale axes are linked/proportional (Uniform Scaling / Magnet mode). Defaults to true.
    bool m_uniformScaleLocked = true;
    /// The shelf the Content item area shows instead of m_assetTableDirectory, or empty.
    std::string m_assetTableShelf;
    /// The user's shelves; saved with the session like the pinned paths.
    ContentShelves m_contentShelves;
    /// project.uvshelves' write time as last read or written, to notice a change made outside
    /// (a pull, a teammate's editor), and when the panel last looked.
    std::optional<std::filesystem::file_time_type> m_sharedShelvesWriteTime;
    double m_sharedShelvesCheckedAt = 0.0;
    /// Back/forward; follows m_assetTableDirectory/m_assetTableShelf each frame, so a
    /// change made anywhere (tree, breadcrumb, bridge) is a step Back can undo.
    ContentNavigationHistory m_contentHistory;
    /// The folder tree's own search, shown while its magnifier is on.
    std::string m_contentTreeFilter;
    bool m_contentTreeSearchOpen = false;
    /// The shelf being renamed inline in the sidebar, and its text.
    std::string m_contentShelfRenaming;
    std::string m_contentShelfRenameText;
    /// Fraction of the merged Asset Table panel's width given to its left file/folder list
    /// (the remainder goes to the right thumbnail grid); adjusted by dragging the splitter between
    /// them. Matches the ~35% left / ~65% right proportions of the design this panel was built to.
    float m_assetTableSplitRatio = 0.35F;
    /// Whether the Asset Table shows its sidebar (pinned, folders, shelves) beside the items,
    /// or the items alone at full width. Toggled from the panel's Settings menu.
    bool m_assetTableSplitMode = true;
    /// How the Asset Table shows files, picked from its "..." menu.
    enum class AssetTableViewMode : std::uint8_t {
        SmallTiles = 0,
        LargeTiles,
        List,
    };
    AssetTableViewMode m_assetTableViewMode = AssetTableViewMode::SmallTiles;
    /// Which of the five modes the item area is in (Settings > Mode, or the mode strip).
    AssetTableMode m_assetTableMode = AssetTableMode::Tiles;
    /// Details' sort, for this session.
    ContentSortKey m_contentSortKey = ContentSortKey::Name;
    bool m_contentSortAscending = true;
    /// Size and last change of files the Details, Recent and Columns modes have asked about, by
    /// content-relative path; read from disk once each and forgotten when the project is rescanned.
    std::unordered_map<std::string, ContentFileFacts> m_contentFileFacts;
    std::uint64_t m_contentFileFactsGeneration = 0U;
    /// Content-derived thumbnail textures for Asset Table entries (currently texture assets
    /// only), keyed by project-relative generic path. A cached 0 means a prior load attempt
    /// failed (not a texture, corrupt, or unsupported format) and callers should fall back to the
    /// generic per-type icon rather than retrying every frame. Cleared whenever the project file
    /// index is successfully refreshed, since on-disk content may have changed.
    std::map<std::string, std::uintptr_t> m_textureThumbnailCache;
    /// Content-derived thumbnail textures for Asset Table mesh entries, rendered on demand by
    /// m_meshThumbnailRenderer. Kept up to date on its own: the files it was drawn from (the mesh
    /// and its materials) are looked at again now and then, and a change draws it again; a
    /// picture that could not be drawn yet (an import still running) is tried again.
    struct MeshThumbnailEntry final {
        std::uintptr_t texture = 0U;
        /// The files the picture was drawn from, and when each was last written then.
        std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> sources;
        std::chrono::steady_clock::time_point checkedAt{};
    };
    std::map<std::string, MeshThumbnailEntry> m_meshThumbnailCache;
    /// The last animation pose shown on a model in the Entity Editor, by bone name, keyed by the
    /// model's file name. Entity and character thumbnails stand in it instead of the rest pose.
    std::map<std::string, std::unordered_map<std::string, Scene::SkeletonBonePose>> m_thumbnailPoses;
    /// Bumped whenever a mesh picture is drawn again or the pictures are dropped: views that copy
    /// pictures by texture id copy again (an id can be reused for a new picture).
    std::uint64_t m_meshThumbnailGeneration = 0U;
    /// Content-derived thumbnail textures for Asset Table material entries, rendered on demand
    /// by m_materialThumbnailRenderer. Same caching contract as m_textureThumbnailCache, plus
    /// targeted invalidation when the Inspector saves a material (InvalidateMaterialThumbnailCache).
    std::map<std::string, std::uintptr_t> m_materialThumbnailCache;
    /// Bumped whenever a material thumbnail is invalidated or cleared so tiles re-read updated pixels.
    std::uint64_t m_materialThumbnailGeneration = 0U;
    /// Material thumbnails invalidated during this frame. The Inspector saves a material on every
    /// edit - including every frame of a slider drag - so its preview is dropped from
    /// m_materialThumbnailCache immediately but the GL texture is only freed at the start of the
    /// next Asset Table pass, so a card still drawn this frame keeps its texture.
    std::vector<std::uintptr_t> m_retiredMaterialThumbnailTextures;
    /// Why the last Skeleton3D source bind failed; shown under the Source row until the next bind.
    std::string m_skeletonSourceStatus;
    /// The bone whose rest pose the Skeleton3D Inspector shows, by name.
    std::string m_selectedSkeletonBone;
    /// The bone currently selected in the Ragdoll3D Inspector for capsule/limit editing.
    std::string m_selectedRagdollBoneName;
    /// The Entity Editor's Timeline: the AnimationSequencer it shows, that player's clip, and the
    /// preview playhead. Previewing writes the skeleton's runtime pose only, never a saved value.
    struct AnimationTimelineState final {
        Scene::Entity player = Scene::kInvalidEntity;
        Asset::AssetGuid clipGuid{};
        std::shared_ptr<const Asset::AnimationClipAsset> clip;
        /// The clip `clip` replaced this frame, kept alive until the next one: the frame's drawing
        /// still reads it through a plain pointer.
        std::shared_ptr<const Asset::AnimationClipAsset> retired;
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
        std::vector<ClipKey> selectedKeys;
        ClipKeyClipboard clipboard;
        std::vector<std::shared_ptr<const Asset::AnimationClipAsset>> undo;
        std::vector<std::shared_ptr<const Asset::AnimationClipAsset>> redo;
        /// Edited since it was loaded or saved.
        bool dirty = false;
        std::uint64_t editRevision = 0U;
        /// Record: moving the animated object (gizmo or Details) keys its transform at the playhead.
        bool recording = false;
        /// Sequencer settings: the preview's speed, the view following the playhead while it plays,
        /// scrubbing and key drags snapped to the clip's frames, and the clock in seconds or frames.
        double playbackRate = 1.0;
        bool followPlayhead = true;
        bool snapToFrames = true;
        bool clockInSeconds = false;
        /// The Sequencer panel is on screen (keys and key buttons show only then).
        bool panelShown = false;
        /// The object the clip's Transform track moves, previewed at the playhead: its own pose from
        /// before the preview (put back when the preview ends), the pose last written and when.
        Scene::Entity objectTarget = Scene::kInvalidEntity;
        Scene::TransformComponent objectRest{};
        bool objectApplied = false;
        Scene::TransformComponent objectLast{};
        double objectLastTime = -1.0;
        const void* objectLastClip = nullptr;
        /// Objects under it the clip has their own tracks for (clip.objects), previewed the same way.
        struct ChildPose {
            Scene::Entity entity = Scene::kInvalidEntity;
            Scene::TransformComponent rest{};
            Scene::TransformComponent last{};
        };
        std::vector<ChildPose> children;
        double childrenLastTime = -1.0;
        const void* childrenLastClip = nullptr;
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
        struct ClipCard final {
            std::string name;
            std::filesystem::path path;
            std::filesystem::file_time_type written{};
            bool hasWritten = false;
            double durationSeconds = 0.0;
            bool skeletal = false;
            bool readable = false;
        };
        std::unordered_map<std::uint64_t, ClipCard> clipCards;
        std::string pickerSearch;
        bool renameClipRequested = false;
        std::string clipName;
        /// The linked `.uvanimlib`'s cache: the link this was read for, the file's stem, its clip
        /// GUIDs in file order, and whether a set link is unreadable. Refreshed when the picker
        /// opens and whenever the link changes, so the section always shows the file as it is.
        Asset::AssetGuid linkedLibraryRef{};
        std::string linkedLibraryName;
        std::vector<Asset::AssetGuid> linkedLibraryClips;
        bool linkedLibraryBroken = false;
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
        Scene::Entity previewSkeleton = Scene::kInvalidEntity;
        /// The objects the blend-shape preview drove, with the weights each held before it did:
        /// put back when the preview stops, so scrubbing a clip never rewrites an entity's
        /// authored shapes. Preview state only - nothing here reaches the document or its file.
        struct MorphPreviewTarget final {
            Scene::Entity entity = Scene::kInvalidEntity;
            std::vector<Scene::MorphTargetWeight> previousWeights;
        };
        std::vector<MorphPreviewTarget> morphPreview;
        /// Blend-shape names of each previewed object's mesh, in the mesh's own order, read once per
        /// object while its mesh stays the same: parsing a .uvmodel on every frame would be the
        /// most expensive thing in the tab.
        struct MorphTargetNames final {
            Scene::Entity entity = Scene::kInvalidEntity;
            Asset::AssetGuid meshGuid{};
            std::vector<std::string> names;
        };
        std::vector<MorphTargetNames> morphTargetNames;
        /// Blend-shape tracks the last preview could not play - no object with that name, an object
        /// with no shapes, or a key count that does not match its mesh - shown in the bottom bar.
        std::size_t morphTracksNotPlaying = 0U;
    };
    AnimationTimelineState m_timeline;
    /// The Timeline had keyboard focus last frame: its keys (Delete, Ctrl+Z, arrows...) are its own,
    /// so the editor's shortcuts stand aside, as they do while typing.
    bool m_timelineOwnsKeys = false;
    /// What the Animation Sequencer shows this frame: the entity's players, the one it shows, the
    /// skeleton and root its preview drives, and that player's clip (null when it has none or the
    /// file cannot be read). Resolved by SyncAnimationTimeline.
    struct AnimationTimelineView final {
        std::vector<Scene::Entity> players;
        Scene::Entity player = Scene::kInvalidEntity;
        Scene::Entity skeleton = Scene::kInvalidEntity;
        Scene::Entity previewRoot = Scene::kInvalidEntity;
        std::shared_ptr<const Asset::AnimationClipAsset> clipOwner;
        const Asset::AnimationClipAsset* clip = nullptr;
        double durationSeconds = 1.0;
        double frameRate = 30.0;
    };
    /// Picks the player (the selected one, else the one shown last, else the first with a clip),
    /// reloads its clip when it changed (saving unsaved key edits to the old one first), and finds
    /// the preview's root and skeleton. Stops the preview when the entity has no player.
    AnimationTimelineView SyncAnimationTimeline();
    /// Advances a playing preview by `deltaSeconds` and poses the skeleton and blend shapes at the
    /// playhead. Writes runtime state only.
    void StepAnimationTimeline(double deltaSeconds);
    /// Transport: the playhead to `seconds` (clamped to the clip, playback paused), Play / Pause,
    /// Stop (to the start), one frame back or forward, and the Loop switch (the player's loop mode).
    void SetAnimationTimelineTime(double seconds);
    void ToggleAnimationTimelinePlay();
    void StopAnimationTimelinePlayback();
    void StepAnimationTimelineFrame(int frames);
    void SetAnimationTimelineLoop(bool loop);
    /// The object the open clip's Transform track moves (the player's driver target, else its
    /// parent), or none.
    [[nodiscard]] Scene::Entity AnimationTimelineObjectTarget() const;
    /// The clip track `entity` is keyed on: "" for the animated object itself, the object prefix
    /// and its name for a named object under it (a mesh, a part), none for anything else.
    [[nodiscard]] std::optional<std::string> AnimationTimelineTrackOf(Scene::Entity entity) const;
    /// Whether `entity`'s transform can be keyed now: the Sequencer is shown with a clip open and
    /// `entity` is the object it animates or an object under it.
    [[nodiscard]] bool CanKeyAnimationTimelineObject(Scene::Entity entity) const;
    /// Keys the selected object's transform (the animated object, or one under it), as it stands, at
    /// the playhead, replacing a key there. With nothing keyable selected, the animated object.
    bool KeyAnimationTimelineObject();
    bool KeyAnimationTimelineObject(Scene::Entity entity);
    /// Keys the selected bone as it is posed now, starting its track when the clip has none.
    bool KeyAnimationTimelineBone(const std::string& bone);
    /// Whether the selected object's track has a key at the playhead.
    [[nodiscard]] bool HasAnimationTimelineObjectKeyAtPlayhead() const;
    /// Record on or off (see AnimationTimelineState::recording).
    void SetAnimationTimelineRecording(bool recording);
    /// Puts the animated object back to its own pose (the preview is over, or the entity is saved).
    void RestoreAnimationTimelineObject();
    void PreviewAnimationTimelineObject(const Asset::AnimationClipAsset& clip);
    void PreviewAnimationTimelineChildren(const Asset::AnimationClipAsset& clip);
    /// Every change to the open clip: one step of the clip's own undo history, shown at once.
    bool EditAnimationTimelineClip(const std::function<void(Asset::AnimationClipAsset&)>& change);
    void UndoAnimationTimeline();
    void RedoAnimationTimeline();
    /// Writes the edited clip to its .uvanim.
    void SaveAnimationTimelineClip();
    /// K: a key on the selected bone (else the clip's own track) at the playhead.
    void InsertAnimationTimelineKey();
    /// The picker's card for a clip: its name, path, length and kind; read once, cached.
    const AnimationTimelineState::ClipCard& AnimationTimelineClipCard(Asset::AssetGuid clip);
    /// The player's own list (its clip first when an older save never listed it) and the linked
    /// library's clips it does not list.
    [[nodiscard]] std::vector<Asset::AssetGuid> AnimationTimelineOwnClips(Scene::Entity player);
    [[nodiscard]] std::vector<Asset::AssetGuid> AnimationTimelineLinkedClips(Scene::Entity player);
    /// Picker actions on `player`: play another clip, take one off the list, copy one to a new
    /// file, start a new one-second clip (keyed with the skeleton's rest pose), and rename the open
    /// clip's file (keeping its GUID).
    void ChooseAnimationTimelineClip(Scene::Entity player, Asset::AssetGuid clip);
    void RemoveAnimationTimelineClip(Scene::Entity player, Asset::AssetGuid clip);
    void DuplicateAnimationTimelineClip(Scene::Entity player, Asset::AssetGuid clip);
    bool CreateAnimationTimelineClip(Scene::Entity player, Scene::Entity skeleton);
    bool RenameAnimationTimelineClip(const std::string& name);
    /// The file dialogs: Add a clip, Import a library, Link a library, Export to a library.
    enum class AnimationTimelineBrowse : std::uint8_t { AddClip, ImportLibrary, LinkLibrary, ExportLibrary };
    void BrowseAnimationTimeline(Scene::Entity player, AnimationTimelineBrowse what);
    /// Puts the previewed skeleton back at rest and stops the preview.
    void StopAnimationTimelinePreview();
    /// Drives the open clip's blend-shape tracks onto the objects their node names refer to, under
    /// `root`, at the current playhead. The weights are runtime preview state: the first frame that
    /// drives an object captures what it held, and StopAnimationTimelinePreview puts it back.
    void PreviewAnimationTimelineMorphs(const Asset::AnimationClipAsset& clip, Scene::Entity root);
    /// Puts back the weights the blend-shape preview replaced, and forgets what it drove.
    void StopAnimationTimelineMorphPreview();
    /// The blend-shape names of `entity`'s mesh, in the mesh's own order; cached per mesh, and empty
    /// when the object has no mesh, no readable model or no shapes.
    [[nodiscard]] std::span<const std::string> TimelineMorphTargetNames(Scene::Entity entity);
    /// The Entity Editor's Anim Graph: the AnimationGraph it shows as boxes and wires, the view
    /// onto the canvas, and what the mouse is doing to it.
    struct AnimationGraphViewState final {
        Scene::Entity tree = Scene::kInvalidEntity;
        /// Canvas point at the panel's top-left, and pixels per canvas unit.
        float panX = -40.0F;
        float panY = -120.0F;
        float zoom = 1.0F;
        bool framed = false;
        std::vector<std::uint32_t> selected;
        /// A drag of the selected objects: the graph before it, restored and re-applied as one
        /// undo step on release.
        bool draggingObjects = false;
        std::vector<Scene::AnimationGraphNode> dragBefore;
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
        struct PreviewLogLine final {
            double at = 0.0;
            std::string text;
            bool stateChange = false;
        };
        std::vector<PreviewLogLine> previewLog;
        /// Each state machine's active state at the last step, by object id, to see it change.
        std::unordered_map<std::uint32_t, std::uint32_t> previewActive;
        /// Recent values of each Float parameter, oldest first, for its sparkline.
        std::unordered_map<std::string, std::vector<float>> parameterHistory;
        Scene::Entity previewSkeleton = Scene::kInvalidEntity;
        /// A drag in the Blend Space 2D plot: -1 the position, 0.. a point, -2 none; and the tree
        /// before it, restored and re-applied as one undo step on release.
        int plotDrag = -2;
        Scene::AnimationGraphComponent plotBefore;
        std::string boneSearch;
        /// The object opened in its own editor (a Blend Space or a State Machine), 0 for the graph.
        std::uint32_t focus = 0U;
        /// The graph shown when nothing is focused: the top tree (machine 0), or the graph of a
        /// State Machine's state.
        std::uint32_t scopeMachine = 0U;
        std::uint32_t scopeState = 0U;
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
        Scene::AnimationGraphComponent stateBefore;
        /// The Blend Space editor's tool (0 select and move, 1 add a point, 2 remove a point), its
        /// snapping, and the point whose animation is being picked (-1: none).
        int spaceTool = 0;
        bool snap = true;
        float snapStep = 0.1F;
        int pickClipForSlot = -1;
        /// An inline value on an object being dragged: the tree before it, for one undo step.
        bool inlineEditing = false;
        Scene::AnimationGraphComponent inlineBefore;
        /// Clip file names by guid, for labels.
        std::unordered_map<std::uint64_t, std::string> clipNames;
        /// Clips the preview has read, by guid; null for one that could not be read.
        std::unordered_map<std::uint64_t, std::shared_ptr<const Asset::AnimationClipAsset>> clips;
        /// Assigned or active AnimationSequencer in Outliner whose library feeds this graph.
        Scene::Entity sequencer = Scene::kInvalidEntity;
        /// Modal picker open trigger
        bool openSequencerPickerModal = false;
        std::string sequencerSearch;
    };
    AnimationGraphViewState m_animGraph;
    /// Gets all unique clips available to an AnimationSequencer (current clip, library list, and linked library).
    [[nodiscard]] std::vector<Asset::AssetGuid> GetSequencerClips(Scene::Entity sequencerEntity) const;
    /// Resolves the active AnimationSequencer for an AnimationGraph: explicit reference, tree entity, target, parent, or timeline player.
    [[nodiscard]] Scene::Entity ResolveActiveSequencerForGraph(Scene::Entity treeEntity) const;
    /// What the Animation Graph page shows: the entity's graphs, the one it shows, and the skeleton
    /// its preview runs on. Resolved by SyncAnimationGraphView, which also resets the view state
    /// when the graph shown changes.
    struct AnimationGraphPanelView final {
        std::vector<Scene::Entity> graphs;
        Scene::Entity tree = Scene::kInvalidEntity;
        Scene::Entity skeleton = Scene::kInvalidEntity;
    };
    AnimationGraphPanelView SyncAnimationGraphView();
    /// Steps the shown graph on its skeleton by `deltaSeconds` (held while paused, one frame when
    /// asked), and logs what it did. Runtime pose only; stops when the preview is off.
    void StepAnimationGraphPreview(double deltaSeconds);
    /// A clip the preview reads, loaded once; null when it cannot be read.
    const Asset::AnimationClipAsset* AnimationGraphPreviewClip(Asset::AssetGuid clip);
    /// Notes what the preview's last step did: clip events, state changes, parameter values.
    void RecordAnimationGraphPreview(const Scene::AnimationGraphComponent& tree);
    /// Puts the previewed skeleton back at rest and the tree back at its start.
    void StopAnimationGraphPreview();
    /// A clip's file name for labels, "" for none.
    [[nodiscard]] const std::string& AnimationClipName(Asset::AssetGuid clip);
    /// Changes the AnimationGraph's objects or parameters as one undo step, like an Inspector edit.
    /// False when nothing changed or the result is not a valid graph (the change is dropped).
    bool EditAnimationGraph(Scene::Entity tree,
                              const std::function<void(Scene::AnimationGraphComponent&)>& change);
    /// In-flight automatic model imports, by content-relative source path.
    std::map<std::string, Asset::AssetImportJobId> m_modelImportJobs;
    std::size_t m_importBatchTotal = 0U;
    std::size_t m_importBatchCompleted = 0U;
    /// The last failure per model source, by content-relative source path. Survives a project
    /// refresh on purpose: a file that fails to import stays failed until its bytes change, so the
    /// reason has to outlive the queue job that produced it.
    std::map<std::string, std::string> m_modelImportFailures;
    /// Every content-relative model source, as the last project refresh read it.
    std::map<std::string, EditorModelSourceInfo> m_modelSources;
    struct CachedModelSourceStamp final {
        std::string stamp;
        std::uintmax_t size = 0U;
        EditorModelSourceInfo info;
        bool modelImported = false;
        bool animsImported = false;
    };
    std::unordered_map<std::string, CachedModelSourceStamp> m_modelSourceStampCache;
    enum class BackgroundImportPhase : std::uint8_t {
        Idle,
        Copying,
        Probing,
        ImportingModel,
        ExtractingAnimations,
    };
    struct BgCopyTask final {
        std::filesystem::path source;
        std::filesystem::path destination;
    };
    struct BgModelProbeTask final {
        std::string key;
        std::filesystem::path source;
        std::string stamp;
        std::uintmax_t size = 0U;
    };
    struct BgModelProbeResult final {
        std::string key;
        std::filesystem::path source;
        std::string stamp;
        std::uintmax_t size = 0U;
        EditorModelSourceInfo info;
    };
    struct BgModelConvertTask final {
        std::string key;
        Asset::AssetImportRequest request;
    };
    struct BgModelConvertResult final {
        std::string key;
        bool succeeded = false;
        bool cacheHit = false;
        std::string errorReason;
    };
    struct BgAnimExtractTask final {
        std::filesystem::path absoluteSource;
        std::filesystem::path contentRoot;
        std::filesystem::path importedModel;
        bool keepInModel = false;
        bool retargetedCurrent = false;
    };
    struct BgAnimExtractResult final {
        std::filesystem::path absoluteSource;
        std::filesystem::path contentRoot;
        std::filesystem::path importedModel;
        bool keepInModel = false;
        std::vector<std::string> embeddedTakes;
        std::vector<std::filesystem::path> writtenLooseFiles;
    };
    struct ExternalFileStat final {
        bool isDirectory = false;
        std::uintmax_t size = 0U;
        std::int64_t mtimeTicks = 0;
        bool operator==(const ExternalFileStat&) const noexcept = default;
    };
    struct StabilizingExternalFile final {
        std::uintmax_t size = 0U;
        std::int64_t mtimeTicks = 0;
        std::chrono::steady_clock::time_point lastChangedAt{};
    };
    std::unique_ptr<Asset::AssetImportQueue> m_bgModelImportQueue;
    mutable std::mutex m_bgImportMutex;
    std::condition_variable m_bgImportCv;
    bool m_bgImportStop = false;
    bool m_bgImportCancel = false;
    bool m_bgImportHasPending = false;
    BackgroundImportPhase m_bgImportPhase = BackgroundImportPhase::Idle;
    std::string m_bgImportCurrentItem;
    std::deque<BgCopyTask> m_bgCopyQueue;
    std::vector<std::filesystem::path> m_bgCopyCleanupDirs;
    std::vector<std::filesystem::path> m_bgCompletedCopies;
    std::size_t m_bgCopyBatchTotal = 0U;
    std::size_t m_bgCopyBatchDone = 0U;
    std::size_t m_bgCopiesSinceRefresh = 0U;
    std::deque<BgModelProbeTask> m_bgProbeQueue;
    std::unordered_set<std::string> m_bgProbeQueuedKeys;
    std::vector<BgModelProbeResult> m_bgCompletedProbes;
    std::size_t m_bgProbeBatchTotal = 0U;
    std::size_t m_bgProbeBatchDone = 0U;
    std::deque<BgModelConvertTask> m_bgModelConvertQueue;
    std::unordered_set<std::string> m_bgModelConvertInFlightKeys;
    std::vector<BgModelConvertResult> m_bgCompletedModels;
    std::deque<BgAnimExtractTask> m_bgAnimQueue;
    std::unordered_set<std::string> m_bgAnimQueuedKeys;
    std::vector<BgAnimExtractResult> m_bgCompletedAnims;
    std::size_t m_bgAnimBatchTotal = 0U;
    std::size_t m_bgAnimBatchDone = 0U;
    std::thread m_bgImportWorker;
    // External filesystem watcher (OS / Android file manager additions into Content/).
    std::shared_ptr<std::atomic<bool>> m_externalScanCancel = std::make_shared<std::atomic<bool>>(false);
    std::future<std::unordered_map<std::string, ExternalFileStat>> m_externalScanFuture;
    std::unordered_map<std::string, ExternalFileStat> m_lastObservedContentTree;
    std::unordered_map<std::string, StabilizingExternalFile> m_stabilizingExternalFiles;
    std::chrono::steady_clock::time_point m_lastExternalWatchPoll{};
    bool m_wasAnyWindowFocused = false;
    bool m_externalWatchImmediateRequested = false;
    std::uint64_t m_lastContentSnapshotSignature = 0U;
    bool m_hasContentSnapshotSignature = false;
    MeshThumbnailRenderer m_meshThumbnailRenderer;
    MaterialThumbnailRenderer m_materialThumbnailRenderer;
    AssetTableTypeFocus m_assetTableTypeFocus = AssetTableTypeFocus::All;
    std::string m_assetFilter;
    /// One default-constructed instance per inspected component type, made on first use, so the
    /// Inspector can tell a changed value from a default one without constructing a component per
    /// row per frame.
    std::unordered_map<const Core::TypeMetadataEntry*, Core::TypeInstance> m_inspectorDefaultInstances;
    /// Working copies of the text fields being edited (DrawCommittedTextInput), keyed by widget
    /// id. The text field owns the text while it is active; this is where it lands so it can be
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
    std::optional<UVScriptDocument> m_openUVScript;
    /// A `.uvshader` asset currently open for in-inspector editing.
    struct ShaderAssetEditState final {
        std::filesystem::path path;
        Asset::ShaderAsset shader;
        bool isDirty = false;
    };
    std::optional<ShaderAssetEditState> m_openShaderAsset;
    /// State of the Godot-style "Create Script" dialog.
    struct CreateScriptModalState final {
        bool isOpen = false;
        bool openRequested = false;
        std::filesystem::path targetDirectory;
        Scene::Entity targetEntity = Scene::kInvalidEntity;
        std::string scriptPathText = "NewScript.uvs";
        std::string inheritsClass = "Object";
        int templateIndex = 0;
    };
    CreateScriptModalState m_createScriptModal;
    /// State of the Godot-style "Create Shader" dialog.
    struct CreateShaderModalState final {
        bool isOpen = false;
        bool openRequested = false;
        std::filesystem::path targetDirectory;
        std::string shaderPathText = "NewShader.uvshader";
        int shaderTypeIndex = 0;
        std::function<void(const std::filesystem::path&)> onShaderCreated = nullptr;
    };
    CreateShaderModalState m_createShaderModal;
    /// A caret move the text editor applies on its next frame (1-based line), set by GoTo.
    std::uint32_t m_uvscriptJumpLine = 0U;
    /// The export rows the Inspector last drew, reused until the object, its script or its values
    /// change, or half a second passes (the file may have been edited).
    struct ScriptExportsCache final {
        Scene::Entity entity = Scene::kInvalidEntity;
        std::string path;
        std::map<std::string, std::string> values;
        std::chrono::steady_clock::time_point builtAt{};
        std::vector<ScriptExportRow> rows;
    };
    ScriptExportsCache m_scriptExportsCache;
    /// The Add Metadata popup's draft. The type is remembered between uses: the next property an
    /// author adds is most often the same kind as the last.
    std::string m_metadataAddName;
    std::string m_metadataTypeFilter;
    Core::VariantType m_metadataAddType = Core::VariantType::Bool;
    std::optional<Core::Variant> m_metadataAddValue;
    /// Row being renamed, and its draft name.
    std::string m_metadataRenameKey;
    std::string m_metadataRenameDraft;
    /// A lossy retype awaiting confirmation: key and target type.
    std::optional<std::pair<std::string, Core::VariantType>> m_metadataPendingRetype;
    /// Commits a new entry list for the selected object's metadata: the single write path.
    /// With `preview`, the list is shown without history, as part of a drag.
    [[nodiscard]] bool CommitSelectedObjectMetadata(std::vector<Scene::ObjectMetadataEntry> entries,
                                                     bool preview = false);
    [[nodiscard]] bool WriteSelectedObjectMetadataValue(const std::string& key, const Core::Variant& value,
                                                         bool preview);
    std::string m_consoleFilter;
    std::string m_consoleCommand;
    static constexpr std::size_t kNoCaptureSelection = static_cast<std::size_t>(-1);
    Network::PacketCapture m_packetCapture;
    Network::PacketCaptureFilter m_captureFilter;
    std::array<char, 256U> m_captureText{};
    int m_captureDirection = 0;
    int m_captureChannel = 0;
    bool m_captureMalformedOnly = false;
    std::size_t m_captureSelected = kNoCaptureSelection;
    Core::ProfilerCapture m_profilerCapture;
    Core::DiagnosticCaptureBundle m_profilerBundle;
    bool m_profilerHasBundle = false;
    std::uint64_t m_profilerSession = 0U;
    std::string m_profilerStatus = "No capture.";
    std::string m_hierarchyFilter;
    std::string m_cachedHierarchyFilter;
    std::vector<Scene::Entity> m_cachedHierarchyVisibleEntities;
    Scene::Entity m_hierarchyRenameEntity = Scene::kInvalidEntity;
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
    Scene::Entity m_hierarchyRevealedEntity = Scene::kInvalidEntity;
    std::vector<Scene::Entity> m_hierarchyRevealAncestors;
    bool m_hierarchyRevealPending = false;
    // Expand Branch / Collapse Branch: the open state each row in the branch should take the next
    // time it is drawn. A row is erased once applied (see SetHierarchyBranchOpen).
    std::unordered_map<Scene::Entity, bool> m_hierarchyPendingRowOpen;
    ColorPickerPreferences m_colorPickerPreferences;
    EditorExtendedPreferences m_extendedPreferences{};
    std::chrono::steady_clock::time_point m_lastAutosaveTime{std::chrono::steady_clock::now()};
    // The property edit in flight; see PreviewSelectedComponentProperty.
    struct ComponentPropertyPreview final {
        Scene::Entity entity = Scene::kInvalidEntity;
        const Core::TypeMetadataEntry* entry = nullptr;
        const Core::TypeMetadataProperty* property = nullptr;
        Core::TypeInstance before;
        EditorSelectionSnapshot selectionBefore;
        bool dirtyBefore = false;
    };
    std::optional<ComponentPropertyPreview> m_componentPropertyPreview;

    /// What belongs to one document rather than to the editor: its scene (through its services),
    /// selection, undo, dirty state and the scene tree's per-document state. The editor's members
    /// hold the active document's; this holds the other one's until the next swap.
    struct DocumentState final {
        Core::EngineServices* services = nullptr;
        std::vector<Scene::Entity> selectedEntities;
        Scene::Entity selectedEntity = Scene::kInvalidEntity;
        std::deque<HistoryEntry> undoHistory;
        std::deque<HistoryEntry> redoHistory;
        bool sceneDirty = false;
        std::unordered_set<Scene::Entity> lockedFolders;
        Scene::Entity lastUsedFolder = Scene::kInvalidEntity;
        Scene::Entity pendingComponentChangeEntity = Scene::kInvalidEntity;
        std::unordered_map<Scene::Entity, bool> hierarchyPendingRowOpen;
        Scene::Entity previewCamera = Scene::kInvalidEntity;
        std::optional<ComponentPropertyPreview> componentPropertyPreview;
    };
    /// Exchanges the active document's state with m_otherDocument's.
    void SwapDocumentState();
    /// Ends the Entity Editor session (the scene is destroyed); the level is the document again.
    void FinishEntityEditorClose();
    DocumentSceneFactory m_documentSceneFactory;
    DocumentSceneClosing m_documentSceneClosing;
    FilePickerHost m_filePickerHost;
    std::unique_ptr<Core::DocumentScene> m_entityScene;
    std::optional<MontageEditSession> m_montageEdit;
    /// The Motion Montage Editor's preview scene (its character), beside the level and the entity.
    std::unique_ptr<Core::DocumentScene> m_montageScene;
    std::map<std::uint64_t, std::optional<Asset::AnimationClipAsset>> m_montageClips;
    /// The Widget Editor's 3D View preview: its world, the asset standing in it, how it is framed.
    struct WidgetPreview final {
        std::unique_ptr<Core::DocumentScene> scene;
        std::string assetPath;
        Scene::Entity root = Scene::kInvalidEntity;
        ViewportOverlayState overlay;
        float spinDegrees = 0.0F;
        std::chrono::steady_clock::time_point lastFrame{};
        std::array<float, 3> framedTarget{0.0F, 0.0F, 0.0F};
        float framedRadius = 0.0F;
        std::vector<std::byte> stageBytes;
        Scene::Entity stage = Scene::kInvalidEntity;
        Scene::Entity stageCamera = Scene::kInvalidEntity;
    };
    std::optional<WidgetPreview> m_widgetPreview;
    void RebuildMontagePreview();
    DocumentState m_otherDocument;
    bool m_entityDocumentActive = false;
    /// Close was asked from inside the entity's own scope: finished when that scope ends.
    std::optional<bool> m_entityClosePending;
    /// The entity was saved inside its own scope: its copies in the level are refreshed once the
    /// level is the active document again.
    bool m_entityLevelRefreshPending = false;
    std::optional<std::pair<std::string, std::string>> m_pendingAnimationParameterRename;
    std::string m_objectPickerFilter;
    std::string m_objectPickerScrolledFilter;
    std::optional<Asset::AssetRecord> m_selectedAsset;
    /// Everything picked in Content (Ctrl and Shift click); m_selectedProjectFile is the last one.
    ContentSelection m_contentSelection;
    /// The items Content drew this frame and the frame before, in order: what Shift+click ranges over.
    std::vector<std::string> m_contentShownOrder;
    std::vector<std::string> m_contentShownOrderPrevious;
    std::optional<Asset::ProjectFileEntry> m_selectedProjectFile;
    std::optional<Asset::ProjectFileEntry> m_filesystemContextEntry;
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
    /// The dock body's height, dragged from its top edge; the layout clamps it (kAssetsPanelHeight default).
    float m_bottomDockHeight = 192.0F;
    float m_scenePanelWidth = 240.0F;
    float m_inspectorPanelWidth = 320.0F;
    std::array<char, 512> m_consoleInput{};
    bool m_consoleScrollToBottom = false;
    EditorViewports m_viewports;
    /// The VFX Editor's open document, its preview world's scene, and the thumbnail cache.
    std::unique_ptr<VfxEditSession> m_vfxEdit;
    std::unique_ptr<Core::DocumentScene> m_vfxScene;
    ViewportOverlayState m_vfxViewportOverlayState;
    std::map<std::string, std::uintptr_t> m_vfxThumbnailCache;
    bool m_prismUiRequested = false;
    std::filesystem::path m_prismAssetDir;
    std::filesystem::path m_projectFilePath;
    std::function<std::array<float, 4>()> m_safeAreaCallback;
    std::unique_ptr<EditorShell> m_levelShell;
    // UI/render state is isolated per window, matching the dedicated per-window viewports.
    // Level remains the canonical settings-backed state; tool-window state is session-local.
    ViewportOverlayState m_viewportOverlayState;
    ViewportOverlayState m_entityViewportOverlayState;
    ViewportOverlayState m_retargetViewportOverlayState;
    Scene::Entity m_previewCamera = Scene::kInvalidEntity;
    bool m_sceneDirty = false;
    bool m_uiInitialized = false;
    EditorUiAssets m_uiAssets;
};

} // namespace UVE::Editor
