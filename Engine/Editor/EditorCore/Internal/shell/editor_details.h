// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor.h"
#include "editor_icons.h"
#include "material_asset.h"
#include "primitive_mesh_component.h"
#include "prism/Icons.h"

#include "prism/Widget.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace prism {
class EngineView;
class Button;
class Label;
class PropertyGrid;
class TextInput;
class Widget;
} // namespace prism

namespace UVE::Editor {

/// The Details panel (#17-#22) drawn with PRISM widgets. Everything it shows and edits is the
/// engine's: the selected entity's components and their declared properties come from the scene
/// component metadata registry (the same declarations the previous Inspector drew), in the same
/// section order, and every edit goes through the editor's own write paths:
///
///   * a property: SetSelectedComponentProperty, or Preview... while a drag is held and
///     CommitComponentPropertyPreviewFor when it ends (one undo step per gesture);
///   * the transform: BeginTransformGesture / PreviewTransformGestureValue /
///     CommitTransformGesture, exactly like the transform drawer;
///   * the name: SetSelectedEntityName.
class EditorDetails final {
public:
    explicit EditorDetails(Editor& editor);
    ~EditorDetails();

    [[nodiscard]] prism::Ref<prism::Widget> Build();
    /// Rebuilds the rows when the selection, its components or the search change; otherwise
    /// refreshes the shown values (never the one being edited).
    void Refresh();
    /// The camera Screen is on: the shell shows the selected camera's view in its viewport's corner.
    [[nodiscard]] bool CameraScreenOpen() const noexcept { return m_cameraScreenOpen; }

private:
    using Entry = Core::TypeMetadataEntry;
    using Property = Core::TypeMetadataProperty;

    struct Row final {
        const Entry* entry = nullptr;           // null for the transform rows
        std::function<void(const void* instance)> sync;
        prism::Widget* editor = nullptr;
        bool editing = false;                      // a gesture is held: do not overwrite its value
    };

    /// What a slot's picker offers (editor_details_pickers.cpp).
    struct PickerSpec final {
        std::string title;                    ///< "Mesh", "Material", "Texture" ...
        std::vector<std::string> listed;      ///< project files shown, by extension
        std::vector<std::string> loadable;    ///< what Load... / Import... accept
        bool shapes = false;                  ///< built-in shapes (a MeshInstance3D's mesh)
        bool newMaterial = false;
    };
    /// What picking does: the slot's own setters.
    struct PickerActions final {
        std::function<void()> empty;
        std::function<void(const std::filesystem::path& absolute)> file;
        std::function<void(Scene::PrimitiveMeshKind)> shape;
        std::filesystem::path current;        ///< the file in the slot now (absolute), if any
        std::optional<Scene::PrimitiveMeshKind> currentShape;
        /// More commands at the top of the menu, after Empty (a sky slot's New Sky).
        std::vector<std::pair<std::string, std::function<void()>>> extras;
    };
    [[nodiscard]] static PickerSpec PickerSpecFor(const std::vector<std::string>& extensions);
    void ShowPicker(const PickerSpec& spec, PickerActions actions, prism::Rect anchor);
    /// The asset a picked file becomes for a slot: a model source its .uvmodel, an image its .uvtex.
    [[nodiscard]] Asset::AssetGuid PublishPicked(const std::filesystem::path& absolute);
    /// Packs a material's separate Metallic / Roughness maps into the ORM map the shader reads.
    void PackSeparateMaps(Asset::MaterialAsset& material, const std::filesystem::path& materialPath, bool metallicPicked,
                             bool roughnessPicked);
    [[nodiscard]] std::filesystem::path PathOfGuid(Asset::AssetGuid guid) const;
    /// How the texture at `texturePath` is imported (colour space, usage, mipmaps, size...), applied by
    /// re-importing its source image (editor_details_material.cpp).
    void ShowTextureSettings(const std::filesystem::path& texturePath);

    void Rebuild(Scene::Entity entity);
    // Rows some objects need beyond their properties (editor_details_objects.cpp).
    void AddCameraPreview(Scene::Entity entity);
    void AddRagdollBuilder(Scene::Entity entity);
    void AddSkeletonSource(int category, const Entry& entry, const Property& property, const void* instance,
                              bool writable);
    void AddSkeletonBones(int category, const void* instance);
    /// The object's script `export` fields, one typed row each.
    void AddScriptExports(int category, bool writable);
    /// The object's metadata: key, value and remove per entry, then Add.
    void AddObjectMetadata(int category, const void* instance, bool writable);
    void AddBonePicker(int category, const Entry& entry, const Property& property, const void* instance,
                          bool writable);
    void AddTransform(int category, Scene::Entity entity);
    /// The material file at `absolutePath`, edited in place (editor_details_material.cpp).
    void AddMaterialSection(const std::filesystem::path& absolutePath, const std::string& slotLabel);
    void AddProperty(int category, const Entry& entry, const Property& property, const void* instance,
                        bool writable);
    [[nodiscard]] std::uint64_t StructureSignature(Scene::Entity entity) const;
    [[nodiscard]] bool MatchesFilter(const std::string& text) const;
    [[nodiscard]] bool IsBeingEdited(const Row& row) const;

    Editor& m_editor;
    EditorIcons m_icons;
    std::shared_ptr<prism::TextInput> m_search;
    std::shared_ptr<prism::Label> m_title;
    std::shared_ptr<prism::Label> m_subtitle;
    std::shared_ptr<prism::PropertyGrid> m_grid;
    /// The Scale row's lock (X, Y and Z together).
    std::weak_ptr<prism::Button> m_scaleLockButton;
    /// Scale lock: on, a change to one axis of Scale takes the other two with it (in proportion).
    bool m_scaleLocked = false;
    std::vector<std::shared_ptr<Row>> m_rows;
    Scene::Entity m_entity = Scene::kInvalidEntity;
    std::uint64_t m_signature = 0;
    std::string m_filter;
    // Materials the rows just added use, shown as their own sections after the component's rows.
    std::vector<std::pair<std::filesystem::path, std::string>> m_pendingMaterials;
    // The camera Screen toggle (kept across selections).
    bool m_cameraScreenOpen = true;
    // A row changed what the panel shows (a texture slot gained or lost its map): rebuild on the next refresh.
    bool m_rebuildRequested = false;
    std::size_t m_selectedRagdollBoneIndex = 0U;
};

/// Wraps an asset slot so a file dragged from the Asset Library onto it is used (`use`), when its
/// extension is one of `kinds`. The slot highlights while a usable file is over it.
[[nodiscard]] prism::Ref<prism::Widget> WithAssetDrop(std::function<std::filesystem::path()> contentRoot, prism::Ref<prism::Widget> slot,
                                                         std::vector<std::string> kinds,
                                                         std::function<void(const std::filesystem::path&)> use);

} // namespace UVE::Editor
