// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor.h"
#include "editor_icons.h"
#include "material_asset.h"
#include "primitive_mesh_component.h"
#include "prism/Icons.h"

#include "prism/Widget.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace prism {
class EngineView;
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
///   * a property: SetSelectedComponentPropertyUVE, or Preview... while a drag is held and
///     CommitComponentPropertyPreviewForUVE when it ends (one undo step per gesture);
///   * the transform: BeginTransformGestureUVE / PreviewTransformGestureValueUVE /
///     CommitTransformGestureUVE, exactly like the transform drawer;
///   * the name: SetSelectedEntityNameUVE.
class EditorDetailsUVE final {
public:
    explicit EditorDetailsUVE(EditorUVE& editor);
    ~EditorDetailsUVE();

    [[nodiscard]] prism::Ref<prism::Widget> BuildUVE();
    /// Rebuilds the rows when the selection, its components or the search change; otherwise
    /// refreshes the shown values (never the one being edited).
    void RefreshUVE();
    /// The camera Screen is on: the shell shows the selected camera's view in its viewport's corner.
    [[nodiscard]] bool CameraScreenOpenUVE() const noexcept { return m_cameraScreenOpen; }

private:
    using EntryUVE = Core::TypeMetadataEntryUVE;
    using PropertyUVE = Core::TypeMetadataPropertyUVE;

    struct RowUVE final {
        const EntryUVE* entry = nullptr;           // null for the transform rows
        std::function<void(const void* instance)> sync;
        prism::Widget* editor = nullptr;
        bool editing = false;                      // a gesture is held: do not overwrite its value
    };

    /// What a slot's picker offers (editor_details_pickers.cpp).
    struct PickerSpecUVE final {
        std::string title;                    ///< "Mesh", "Material", "Texture" ...
        std::vector<std::string> listed;      ///< project files shown, by extension
        std::vector<std::string> loadable;    ///< what Load... / Import... accept
        bool shapes = false;                  ///< built-in shapes (a MeshInstance3D's mesh)
        bool newMaterial = false;
    };
    /// What picking does: the slot's own setters.
    struct PickerActionsUVE final {
        std::function<void()> empty;
        std::function<void(const std::filesystem::path& absolute)> file;
        std::function<void(Scene::PrimitiveMeshKindUVE)> shape;
        std::filesystem::path current;        ///< the file in the slot now (absolute), if any
        std::optional<Scene::PrimitiveMeshKindUVE> currentShape;
    };
    [[nodiscard]] static PickerSpecUVE PickerSpecForUVE(const std::vector<std::string>& extensions);
    void ShowPickerUVE(const PickerSpecUVE& spec, PickerActionsUVE actions, prism::Rect anchor);
    /// The asset a picked file becomes for a slot: a model source its .uvmodel, an image its .uvtex.
    [[nodiscard]] Asset::AssetGuidUVE PublishPickedUVE(const std::filesystem::path& absolute);
    /// Packs a material's separate Metallic / Roughness maps into the ORM map the shader reads.
    void PackSeparateMapsUVE(Asset::MaterialAssetUVE& material, const std::filesystem::path& materialPath, bool metallicPicked,
                             bool roughnessPicked);
    [[nodiscard]] std::filesystem::path PathOfGuidUVE(Asset::AssetGuidUVE guid) const;

    void RebuildUVE(Scene::EntityUVE entity);
    // Rows some objects need beyond their properties (editor_details_objects.cpp).
    void AddCameraPreviewUVE(Scene::EntityUVE entity);
    void AddSkeletonSourceUVE(int category, const EntryUVE& entry, const PropertyUVE& property, const void* instance,
                              bool writable);
    void AddSkeletonBonesUVE(int category, const void* instance);
    /// The object's script `export` fields, one typed row each.
    void AddScriptExportsUVE(int category, bool writable);
    /// The object's metadata: key, value and remove per entry, then Add.
    void AddObjectMetadataUVE(int category, const void* instance, bool writable);
    void AddBonePickerUVE(int category, const EntryUVE& entry, const PropertyUVE& property, const void* instance,
                          bool writable);
    void AddTransformUVE(int category, Scene::EntityUVE entity);
    /// The material file at `absolutePath`, edited in place (editor_details_material.cpp).
    void AddMaterialSectionUVE(const std::filesystem::path& absolutePath, const std::string& slotLabel);
    void AddPropertyUVE(int category, const EntryUVE& entry, const PropertyUVE& property, const void* instance,
                        bool writable);
    [[nodiscard]] std::uint64_t StructureSignatureUVE(Scene::EntityUVE entity) const;
    [[nodiscard]] bool MatchesFilterUVE(const std::string& text) const;
    [[nodiscard]] bool IsBeingEditedUVE(const RowUVE& row) const;

    EditorUVE& m_editor;
    EditorIconsUVE m_icons;
    std::shared_ptr<prism::TextInput> m_search;
    std::shared_ptr<prism::Label> m_title;
    std::shared_ptr<prism::Label> m_subtitle;
    std::shared_ptr<prism::PropertyGrid> m_grid;
    std::vector<std::shared_ptr<RowUVE>> m_rows;
    Scene::EntityUVE m_entity = Scene::kInvalidEntityUVE;
    std::uint64_t m_signature = 0;
    std::string m_filter;
    // Materials the rows just added use, shown as their own sections after the component's rows.
    std::vector<std::pair<std::filesystem::path, std::string>> m_pendingMaterials;
    // The camera Screen toggle (kept across selections).
    bool m_cameraScreenOpen = true;
};

} // namespace UVE::Editor
