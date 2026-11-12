// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_details.h"

#include "editor_asset_thumbnail.h"
#include "editor_content_browser.h"

#include "color.h"
#include "engine_project_settings.h"
#include "scene_component_metadata.h"
#include "scene_object_type.h"
#include "transform_component.h"
#include "vector2.h"
#include "vector3.h"
#include "asset_guid.h"
#include "i_project_file_index.h"
#include "i_asset_database.h"
#include "mesh_asset.h"
#include "mesh_component.h"
#include "skeleton_3d.h"
#include "camera_component.h"
#include "bone_attachment_3d.h"
#include "core_variant.h"
#include "object_metadata_component.h"
#include "uvscript_value.h"

#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Menu.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace UVE::Editor {
namespace {

using TypeFlagsUVE = Core::TypeMetadataPropertyFlagsUVE;

constexpr float kRadToDegUVE = 57.29577951F;
constexpr float kDegToRadUVE = 0.01745329252F;

/// The component types whose authoring is not a field write (see the Inspector's own list): the
/// name routes through SetSelectedEntityNameUVE, the transform through the gesture API, and the
/// parent through reparenting in the Outliner.
constexpr std::array<std::string_view, 3> kCustomDrawnUVE{"component.name", "component.transform",
                                                          "component.hierarchy"};

/// The object-wide engine settings every object carries (how it is processed, threaded,
/// interpolated, translated, described): kept, but collapsed, as Unreal's Advanced, so what the object
/// is comes first.
[[nodiscard]] bool IsAdvancedSectionUVE(const std::string_view title) {
    static constexpr std::array<std::string_view, 5> kAdvanced{"Process", "Thread Group", "Physics Interpolation", "Auto Translate",
                                                               "Editor Description"};
    return std::find(kAdvanced.begin(), kAdvanced.end(), title) != kAdvanced.end();
}

[[nodiscard]] bool IsCustomDrawnUVE(const std::string_view typeId) {
    return std::find(kCustomDrawnUVE.begin(), kCustomDrawnUVE.end(), typeId) != kCustomDrawnUVE.end();
}

/// The preview a slot of these file kinds shows beside its picker, if any.
/// An asset slot's drop target: a Content Browser item of the slot's kinds dropped on it is used
/// as if it were picked (in the document of the window it is dropped in).
class AssetSlotDropUVE final : public prism::Border {
public:
    AssetSlotDropUVE(std::function<std::filesystem::path()> contentRoot, std::vector<std::string> kinds,
                     std::function<void(const std::filesystem::path&)> use)
        : m_contentRoot(std::move(contentRoot)), m_kinds(std::move(kinds)), m_use(std::move(use)) {}
    bool onDragEnter(const prism::DragDropEvent& e) override { return HotUVE(AcceptsUVE(e).has_value()); }
    bool onDragOver(const prism::DragDropEvent& e) override { return HotUVE(AcceptsUVE(e).has_value()); }
    void onDragLeave() override { static_cast<void>(HotUVE(false)); }
    bool onDrop(const prism::DragDropEvent& e) override {
        static_cast<void>(HotUVE(false));
        const std::optional<std::filesystem::path> path = AcceptsUVE(e);
        if (!path.has_value() || !m_use) {
            return false;
        }
        m_use(*path);
        return true;
    }
    void paint(prism::Canvas& c) override {
        prism::Border::paint(c);
        if (m_hot) {
            c.strokeRoundRect(rect(), 3.0F, prism::theme().accent, 2);
        }
    }

private:
    [[nodiscard]] bool HotUVE(const bool hot) {
        if (hot != m_hot) {
            m_hot = hot;
            invalidate();
        }
        return hot;
    }
    [[nodiscard]] std::optional<std::filesystem::path> AcceptsUVE(const prism::DragDropEvent& e) const {
        if (!e.operation || e.operation->tag() != EditorContentBrowserUVE::kDragTagUVE) {
            return std::nullopt;
        }
        const std::string* relative = e.operation->getPayload<std::string>();
        if (relative == nullptr || relative->empty()) {
            return std::nullopt;
        }
        std::string extension = std::filesystem::path{*relative}.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (std::find(m_kinds.begin(), m_kinds.end(), extension) == m_kinds.end()) {
            return std::nullopt;
        }
        return (m_contentRoot ? m_contentRoot() : std::filesystem::path{}) / std::filesystem::path{*relative};
    }
    std::function<std::filesystem::path()> m_contentRoot;
    std::vector<std::string> m_kinds;
    std::function<void(const std::filesystem::path&)> m_use;
    bool m_hot = false;
};

[[nodiscard]] prism::Ref<prism::Widget> WithAssetDropUVE(std::function<std::filesystem::path()> contentRoot, prism::Ref<prism::Widget> slot,
                                                         std::vector<std::string> kinds,
                                                         std::function<void(const std::filesystem::path&)> use) {
    auto drop = prism::make<AssetSlotDropUVE>(std::move(contentRoot), std::move(kinds), std::move(use));
    drop->setChild(std::move(slot));
    return drop;
}

[[nodiscard]] std::optional<AssetThumbnailUVE::KindUVE> ThumbnailKindForUVE(const std::vector<std::string>& extensions) {
    const auto has = [&](const char* kind) {
        return std::find(extensions.begin(), extensions.end(), std::string(kind)) != extensions.end();
    };
    if (has(".uvmat") || has(".uvmaterial")) {
        return AssetThumbnailUVE::KindUVE::Material;
    }
    if (has(".uvmodel")) {
        return AssetThumbnailUVE::KindUVE::Mesh;
    }
    if (has(".uvtex") || has(".png")) {
        return AssetThumbnailUVE::KindUVE::Texture;
    }
    return std::nullopt;
}

[[nodiscard]] std::string LowerUVE(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

void HashUVE(std::uint64_t& h, const std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
}

void HashUVE(std::uint64_t& h, const std::string_view s) {
    for (const char c : s) {
        HashUVE(h, static_cast<std::uint64_t>(static_cast<unsigned char>(c)));
    }
}

[[nodiscard]] float StepUVE(const Core::TypeMetadataPropertyUVE& property, const float fallback) {
    return property.range.enabled && property.range.step > 0.0 ? static_cast<float>(property.range.step) : fallback;
}

[[nodiscard]] prism::Color ToPrismColorUVE(const float r, const float g, const float b) {
    const auto c = [](const float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F)); };
    return prism::Color(c(r), c(g), c(b), 255);
}

[[nodiscard]] Math::Vector3UVE FromPrismColorUVE(const prism::Color c) {
    return Math::Vector3UVE{static_cast<float>(c.r) / 255.0F, static_cast<float>(c.g) / 255.0F,
                            static_cast<float>(c.b) / 255.0F};
}

/// Two NumberFields side by side for a Vector2.
class Vec2FieldUVE final : public prism::Widget {
public:
    Vec2FieldUVE(const float step, const int decimals) {
        for (auto& field : m_fields) {
            field = prism::make<prism::NumberField>(0.0F, step, decimals);
            addChild(field);
        }
    }
    prism::Vec2 measure(prism::Vec2) override { return m_desired = constrain({120.0F, prism::theme().controlH}); }
    void arrange(prism::Rect r) override {
        prism::Widget::arrange(r);
        const float w = std::floor((r.w - 4.0F) * 0.5F);
        m_fields[0]->arrange(prism::Rect{r.x, r.y, w, r.h});
        m_fields[1]->arrange(prism::Rect{r.x + w + 4.0F, r.y, r.w - w - 4.0F, r.h});
    }
    bool hitSelf() const override { return false; }
    std::array<std::shared_ptr<prism::NumberField>, 2> m_fields;
};

} // namespace

EditorDetailsUVE::EditorDetailsUVE(EditorUVE& editor) : m_editor(editor) {}

EditorDetailsUVE::~EditorDetailsUVE() = default;

prism::Ref<prism::Widget> EditorDetailsUVE::BuildUVE() {
    auto page = prism::vbox(4.0F);
    page->setPadding(prism::Margin(4.0F));
    m_search = prism::make<prism::TextInput>("", "Search details");
    m_search->onChanged = [this](const std::string& text) {
        m_filter = LowerUVE(text);
        m_signature = 0;
    };
    page->add(m_search);
    m_title = prism::make<prism::Label>("Nothing selected", prism::FontRole::Semibold);
    page->add(m_title);
    m_subtitle = prism::make<prism::Label>("Select an object in the Outliner or the Viewport.", prism::FontRole::Small);
    m_subtitle->setColor(prism::ThemeColor(&prism::Theme::textDim));
    page->add(m_subtitle);
    m_grid = prism::make<prism::PropertyGrid>();
    auto scroll = prism::make<prism::ScrollView>();
    scroll->setContent(m_grid);
    page->add(scroll, prism::Slot{1.0F});
    return page;
}

bool EditorDetailsUVE::MatchesFilterUVE(const std::string& text) const {
    return m_filter.empty() || LowerUVE(text).find(m_filter) != std::string::npos;
}

std::uint64_t EditorDetailsUVE::StructureSignatureUVE(const Scene::EntityUVE entity) const {
    std::uint64_t h = 1469598103934665603ULL;
    HashUVE(h, (static_cast<std::uint64_t>(entity.index) << 32U) | entity.generation);
    HashUVE(h, m_filter);
    HashUVE(h, static_cast<std::uint64_t>(m_editor.m_selectedEntities.size()));
    HashUVE(h, static_cast<std::uint64_t>(m_editor.m_playModeState));
    HashUVE(h, m_editor.IsAuthoringCommandAllowedUVE() ? 1U : 0U);
    if (!m_editor.IsDocumentEntityUVE(entity)) {
        return h;
    }
    const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    for (const EntryUVE& snapshot : Scene::GetSceneComponentMetadataRegistryUVE().GetSnapshotUVE().entries) {
        if (!em.HasComponentUVE(entity, snapshot.typeIndex)) {
            continue;
        }
        HashUVE(h, snapshot.typeId.ToStringUVE());
        // Visibility rules read values (a field shown only for one mode): a change re-lays the rows.
        const void* const instance = em.GetComponentPointerUVE(entity, snapshot.typeIndex);
        for (const PropertyUVE& property : snapshot.properties) {
            const bool visible = !property.flags.HasUVE(TypeFlagsUVE::Hidden) &&
                                 (property.isVisible == nullptr || property.isVisible(instance));
            HashUVE(h, visible ? 1U : 0U);
            // An asset slot's value decides which material section follows it.
            if (property.typeId == Scene::kPropertyTypeAssetGuidUVE && instance != nullptr) {
                Asset::AssetGuidUVE value{};
                property.getValue(instance, &value);
                HashUVE(h, value.value);
            }
            // A skeleton's bones are drawn as a tree: a new source re-lays it.
            if (property.customDrawerId == "skeleton-source" && instance != nullptr) {
                const auto& skeleton = *static_cast<const Scene::Skeleton3DComponentUVE*>(instance);
                HashUVE(h, skeleton.skeletonAssetPath);
                HashUVE(h, static_cast<std::uint64_t>(skeleton.bones.size()));
            }
        }
    }
    return h;
}

bool EditorDetailsUVE::IsBeingEditedUVE(const RowUVE& row) const {
    if (row.editing) {
        return true;
    }
    if (row.editor == nullptr || !prism::Application::exists()) {
        return false;
    }
    const prism::Widget* const focus = prism::Application::get().focus();
    return focus != nullptr && focus->isDescendantOf(row.editor);
}

void EditorDetailsUVE::RefreshUVE() {
    if (!m_grid) {
        return;
    }
    const Scene::EntityUVE entity = m_editor.GetSelectedEntityUVE();
    const std::uint64_t signature = StructureSignatureUVE(entity);
    if (signature != m_signature || entity != m_entity) {
        m_signature = signature;
        m_entity = entity;
        RebuildUVE(entity);
        return;
    }
    if (!m_editor.IsDocumentEntityUVE(entity)) {
        return;
    }
    // Same rows: show the values as they are now (a gizmo drag, an undo, a script), except the one
    // the author is editing.
    const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    for (const std::shared_ptr<RowUVE>& row : m_rows) {
        if (!row->sync || IsBeingEditedUVE(*row)) {
            continue;
        }
        const void* instance = nullptr;
        if (row->entry != nullptr) {
            if (!em.HasComponentUVE(entity, row->entry->typeIndex)) {
                continue;
            }
            instance = em.GetComponentPointerUVE(entity, row->entry->typeIndex);
        }
        row->sync(instance);
    }
}

void EditorDetailsUVE::RebuildUVE(const Scene::EntityUVE entity) {
    m_grid->clear();
    m_rows.clear();
    if (m_editor.m_selectedEntities.empty() || !m_editor.IsDocumentEntityUVE(entity)) {
        if (m_editor.m_selectedProjectFile.has_value() && !m_editor.m_selectedProjectFile->relativePath.empty()) {
            const auto& file = *m_editor.m_selectedProjectFile;
            m_title->setText(file.relativePath.filename().string());
            m_subtitle->setText(EditorUVE::GetContentBrowserItemTypeLabelUVE(EditorUVE::ClassifyContentBrowserEntryUVE(file)));
            const int section = m_grid->addCategory("Asset");
            m_grid->addRow(section, "Path", prism::make<prism::Label>(file.relativePath.generic_string()));
            m_grid->addRow(section, "Registered",
                           prism::make<prism::Label>(file.registeredAssetGuid.has_value() ? "Yes" : "No"));
            return;
        }
        m_title->setText("Nothing selected");
        m_subtitle->setText("Select an object in the Outliner or the Viewport.");
        return;
    }
    if (!m_editor.HasSingleDocumentSelectionUVE()) {
        m_title->setText(std::to_string(m_editor.m_selectedEntities.size()) + " objects selected");
        m_subtitle->setText("Active: " + m_editor.GetEntityDisplayLabelUVE(entity) +
                            " - editing applies to a single object.");
        return;
    }

    m_title->setText(m_editor.GetEntityDisplayLabelUVE(entity));
    m_subtitle->setText(std::string(m_editor.GetObjectTypeNameUVE(entity)));
    const bool authoring = m_editor.IsAuthoringCommandAllowedUVE();
    const bool simulating = m_editor.m_playModeState != EditorPlayModeStateUVE::Edit;

    // The object's own row: its name (renaming owns uniqueness and its own history entry).
    if (MatchesFilterUVE("Name")) {
        const int object = m_grid->addCategory("Object");
        auto name = prism::make<prism::TextInput>(m_editor.GetEntityDisplayLabelUVE(entity));
        name->setEnabled(authoring);
        name->onCommit = [this](const std::string& text) { static_cast<void>(m_editor.SetSelectedEntityNameUVE(text)); };
        auto row = std::make_shared<RowUVE>();
        row->editor = name.get();
        std::weak_ptr<prism::TextInput> weak = name;
        row->sync = [this, weak](const void*) {
            if (auto field = weak.lock()) {
                const std::string label = m_editor.GetEntityDisplayLabelUVE(m_editor.GetSelectedEntityUVE());
                if (field->text() != label) {
                    field->setText(label);
                }
            }
        };
        m_rows.push_back(row);
        m_grid->addRow(object, "Name", name);
    }

    const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    // Sections in the Inspector's order: by declared section order, then type id.
    const Core::TypeMetadataRegistryUVE& registry = Scene::GetSceneComponentMetadataRegistryUVE();
    std::vector<const EntryUVE*> entries;
    for (const EntryUVE& snapshot : registry.GetSnapshotUVE().entries) {
        const EntryUVE* const stable = registry.FindTypeUVE(snapshot.typeId);
        if (stable != nullptr && em.HasComponentUVE(entity, stable->typeIndex)) {
            entries.push_back(stable);
        }
    }
    std::stable_sort(entries.begin(), entries.end(), [](const EntryUVE* a, const EntryUVE* b) {
        return a->order != b->order ? a->order < b->order : a->typeId.ToStringUVE() < b->typeId.ToStringUVE();
    });
    // A nested type is drawn under the first of its hosts the entity carries (as the Inspector
    // does), and in its own place when it carries none.
    const auto hostOf = [&](const EntryUVE* child) -> const EntryUVE* {
        for (const auto& hostId : child->nestedUnderTypeIds) {
            for (const EntryUVE* candidate : entries) {
                if (candidate->typeId == hostId && !IsCustomDrawnUVE(candidate->typeId.ToStringUVE())) {
                    return candidate;
                }
            }
        }
        return nullptr;
    };
    std::vector<const EntryUVE*> ordered;
    for (const EntryUVE* entry : entries) {
        if (hostOf(entry) != nullptr) {
            continue;
        }
        ordered.push_back(entry);
        for (const EntryUVE* child : entries) {
            if (hostOf(child) == entry) {
                ordered.push_back(child);
            }
        }
    }
    entries = std::move(ordered);

    bool transformDone = false;
    const bool hasTransform = em.HasComponentUVE<Scene::TransformComponentUVE>(entity);
    for (const EntryUVE* const entry : entries) {
        const std::string_view typeId = entry->typeId.ToStringUVE();
        if (!transformDone && hasTransform && entry->order > Scene::kSectionOrderTransformUVE) {
            AddTransformUVE(-1, entity);
            transformDone = true;
        }
        if (IsCustomDrawnUVE(typeId)) {
            continue;
        }
        const void* const instance = em.GetComponentPointerUVE(entity, entry->typeIndex);
        const std::string title = entry->sectionTitle != nullptr ? std::string(entry->sectionTitle(instance)) : entry->displayName;
        const bool sectionMatches = MatchesFilterUVE(title);
        int category = -1;
        std::string group;
        for (const PropertyUVE& property : entry->properties) {
            const bool visible = !property.flags.HasUVE(TypeFlagsUVE::Hidden) &&
                                 (property.isVisible == nullptr || property.isVisible(instance));
            if (!visible || (property.flags.HasUVE(TypeFlagsUVE::RuntimeState) && !simulating)) {
                continue;
            }
            const bool companion = std::any_of(entry->properties.begin(), entry->properties.end(),
                                               [&](const PropertyUVE& other) { return other.resolvedByProperty == property.name; });
            if (companion) {
                continue;
            }
            const std::string label = property.displayName.empty() ? property.name : property.displayName;
            if (!sectionMatches && !MatchesFilterUVE(label) && !MatchesFilterUVE(property.section)) {
                continue;
            }
            if (category < 0 || property.section != group) {
                group = property.section;
                category = m_grid->addCategory(group.empty() ? title : title + " / " + group, !IsAdvancedSectionUVE(title));
            }
            AddPropertyUVE(category, *entry, property, instance, authoring && property.IsAuthoringWritableUVE());
        }
        for (const auto& [path, slot] : m_pendingMaterials) {
            AddMaterialSectionUVE(path, slot);
        }
        m_pendingMaterials.clear();
        if (entry->typeIndex == std::type_index(typeid(Scene::CameraComponentUVE))) {
            AddCameraPreviewUVE(entity);
        }
    }
    // A model's own materials (one per surface), edited in place like a slot's.
    if (em.HasComponentUVE<Scene::MeshComponentUVE>(entity)) {
        if (const std::optional<Asset::MeshAssetUVE> mesh = m_editor.LoadEntityMeshAssetUVE(entity); mesh.has_value()) {
            for (std::size_t i = 0; i < mesh->materialGuids.size(); ++i) {
                const std::filesystem::path path = m_editor.m_services->GetAssetDatabaseUVE().ResolveUVE(mesh->materialGuids[i]);
                if (!path.empty()) {
                    AddMaterialSectionUVE(path, "Surface " + std::to_string(i));
                }
            }
        }
    }
    if (!transformDone && hasTransform) {
        AddTransformUVE(-1, entity);
    }
}

void EditorDetailsUVE::AddTransformUVE(int category, const Scene::EntityUVE entity) {
    if (!MatchesFilterUVE("Transform") && !MatchesFilterUVE("Location") && !MatchesFilterUVE("Rotation") &&
        !MatchesFilterUVE("Scale")) {
        return;
    }
    if (category < 0) {
        category = m_grid->addCategory("Transform", true);
    }
    const bool authoring = m_editor.IsAuthoringCommandAllowedUVE();
    static_cast<void>(entity);
    struct AxisUVE final {
        const char* label;
        EditorToolSessionModeUVE mode;
        float step;
    };
    static constexpr std::array<AxisUVE, 3> kRowsUVE{{
        {"Location", EditorToolSessionModeUVE::Translate, 0.01F},
        {"Rotation", EditorToolSessionModeUVE::Rotate, 0.5F},
        {"Scale", EditorToolSessionModeUVE::Scale, 0.01F},
    }};
    for (std::size_t which = 0; which < kRowsUVE.size(); ++which) {
        auto field = prism::make<prism::Vec3Field>(kRowsUVE[which].step, which == 1 ? 2 : 3);
        if (which == 1) {
            field->setSuffix("\xC2\xB0");
        }
        field->setEnabled(authoring);
        auto row = std::make_shared<RowUVE>();
        row->editor = field.get();
        std::weak_ptr<RowUVE> weakRow = row;
        std::weak_ptr<prism::Vec3Field> weakField = field;
        const auto current = [this](Scene::TransformComponentUVE& out) {
            const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
            const Scene::EntityUVE selected = m_editor.GetSelectedEntityUVE();
            if (!m_editor.IsDocumentEntityUVE(selected) || !em.HasComponentUVE<Scene::TransformComponentUVE>(selected)) {
                return false;
            }
            out = em.GetComponentUVE<Scene::TransformComponentUVE>(selected);
            return true;
        };
        field->onBegin = [this, weakRow, mode = kRowsUVE[which].mode] {
            if (auto r = weakRow.lock()) {
                r->editing = true;
            }
            if (m_editor.GetToolSessionPhaseUVE() == EditorToolSessionPhaseUVE::Previewing) {
                static_cast<void>(m_editor.CommitTransformGestureUVE());
            }
            static_cast<void>(m_editor.BeginTransformGestureUVE(mode));
        };
        field->onChange = [this, current, which](const int axis, const float value) {
            Scene::TransformComponentUVE t{};
            if (!current(t)) {
                return;
            }
            if (which == 0) {
                (axis == 0 ? t.localPosition.x : axis == 1 ? t.localPosition.y : t.localPosition.z) = value;
            } else if (which == 2) {
                (axis == 0 ? t.localScale.x : axis == 1 ? t.localScale.y : t.localScale.z) = value;
            } else {
                // The authored Euler angles are the edited view of the rotation (see
                // TransformComponentUVE::localEulerRadians); both are written together.
                Math::Vector3UVE e = t.localEulerRadians;
                (axis == 0 ? e.x : axis == 1 ? e.y : e.z) = value * kDegToRadUVE;
                Math::QuaternionUVE q{};
                if (!Math::TryMakeEulerUVE(e, q)) {
                    return;
                }
                t.localEulerRadians = e;
                t.localRotation = q;
            }
            if (m_editor.GetToolSessionPhaseUVE() != EditorToolSessionPhaseUVE::Previewing ||
                !m_editor.PreviewTransformGestureValueUVE(t)) {
                static_cast<void>(m_editor.SetSelectedLocalTransformUVE(t));
            }
        };
        field->onCommit = [this, weakRow] {
            if (m_editor.GetToolSessionPhaseUVE() == EditorToolSessionPhaseUVE::Previewing) {
                static_cast<void>(m_editor.CommitTransformGestureUVE());
            }
            if (auto r = weakRow.lock()) {
                r->editing = false;
            }
        };
        row->sync = [current, weakField, which](const void*) {
            Scene::TransformComponentUVE t{};
            auto f = weakField.lock();
            if (!f || !current(t)) {
                return;
            }
            Math::Vector3UVE v = which == 0 ? t.localPosition : which == 2 ? t.localScale : t.localEulerRadians;
            if (which == 1) {
                v = Math::Vector3UVE{v.x * kRadToDegUVE, v.y * kRadToDegUVE, v.z * kRadToDegUVE};
            }
            if (std::abs(f->get(0) - v.x) > 1e-5F || std::abs(f->get(1) - v.y) > 1e-5F || std::abs(f->get(2) - v.z) > 1e-5F) {
                f->setValue(v.x, v.y, v.z);
            }
        };
        row->sync(nullptr);
        m_rows.push_back(row);
        m_grid->addRow(category, kRowsUVE[which].label, field);
    }
}

void EditorDetailsUVE::AddPropertyUVE(const int category, const EntryUVE& entry, const PropertyUVE& property,
                                           const void* const instance, const bool writable) {
    // Rows some objects draw their own way.
    if (property.customDrawerId == "skeleton-source") {
        AddSkeletonSourceUVE(category, entry, property, instance, writable);
        return;
    }
    if (property.customDrawerId == "skeleton-bones") {
        AddSkeletonBonesUVE(category, instance);
        return;
    }
    if (property.customDrawerId == "script-exports") {
        AddScriptExportsUVE(category, writable);
        return;
    }
    if (property.customDrawerId == "object-metadata") {
        AddObjectMetadataUVE(category, instance, writable);
        return;
    }
    if (entry.typeIndex == std::type_index(typeid(Scene::BoneAttachment3DComponentUVE)) && property.name == "boneName") {
        AddBonePickerUVE(category, entry, property, instance, writable);
        return;
    }
    const std::string label = property.displayName.empty() ? property.name : property.displayName;
    auto row = std::make_shared<RowUVE>();
    row->entry = &entry;
    std::weak_ptr<RowUVE> weakRow = row;
    const EntryUVE* const e = &entry;
    const PropertyUVE* const p = &property;
    prism::Ref<prism::Widget> editor;

    // Continuous edits (drags): previewed live, recorded as one undo step when the gesture ends.
    const auto begin = [weakRow] {
        if (auto r = weakRow.lock()) {
            r->editing = true;
        }
    };
    const auto commit = [this, e, p, weakRow] {
        static_cast<void>(m_editor.CommitComponentPropertyPreviewForUVE(*e, *p));
        if (auto r = weakRow.lock()) {
            r->editing = false;
        }
    };

    if (!property.enumEntries.empty()) {
        std::vector<std::string> labels;
        for (const auto& option : property.enumEntries) {
            labels.push_back(option.label);
        }
        auto combo = prism::make<prism::ComboBox>(labels, 0);
        std::weak_ptr<prism::ComboBox> weak = combo;
        row->sync = [p, weak](const void* inst) {
            auto c = weak.lock();
            if (!c || inst == nullptr) {
                return;
            }
            std::int64_t current = 0;
            p->getValue(inst, &current);
            for (std::size_t i = 0; i < p->enumEntries.size(); ++i) {
                if (p->enumEntries[i].value == current && c->selected() != static_cast<int>(i)) {
                    c->setSelected(static_cast<int>(i));
                }
            }
        };
        combo->onChange = [this, e, p](const int index) {
            if (index >= 0 && static_cast<std::size_t>(index) < p->enumEntries.size()) {
                const std::int64_t chosen = p->enumEntries[static_cast<std::size_t>(index)].value;
                static_cast<void>(m_editor.SetSelectedComponentPropertyUVE(*e, *p, &chosen));
            }
        };
        editor = combo;
    } else if (property.typeId == Scene::kPropertyTypeBoolUVE) {
        auto check = prism::make<prism::CheckBox>("", false);
        std::weak_ptr<prism::CheckBox> weak = check;
        row->sync = [p, weak](const void* inst) {
            auto c = weak.lock();
            bool v = false;
            if (c && inst != nullptr) {
                p->getValue(inst, &v);
                if (c->checked() != v) {
                    c->setChecked(v);
                }
            }
        };
        check->onChange = [this, e, p](const bool v) { static_cast<void>(m_editor.SetSelectedComponentPropertyUVE(*e, *p, &v)); };
        editor = check;
    } else if (property.typeId == Scene::kPropertyTypeFloatUVE || property.typeId == Scene::kPropertyTypeInt32UVE ||
               property.typeId == Scene::kPropertyTypeUInt32UVE || property.typeId == Scene::kPropertyTypeUInt8UVE) {
        const bool isFloat = property.typeId == Scene::kPropertyTypeFloatUVE;
        auto number = prism::make<prism::NumberField>(0.0F, StepUVE(property, isFloat ? 0.01F : 1.0F), isFloat ? 3 : 0);
        if (property.range.enabled) {
            number->setRange(static_cast<float>(property.range.minimum), static_cast<float>(property.range.maximum));
        } else if (!isFloat && property.typeId != Scene::kPropertyTypeInt32UVE) {
            number->setRange(0.0F, property.typeId == Scene::kPropertyTypeUInt8UVE ? 255.0F : 4.0e9F);
        }
        std::weak_ptr<prism::NumberField> weak = number;
        const Strings::StringIdUVE type = property.typeId;
        const auto read = [p, type](const void* inst) -> float {
            if (type == Scene::kPropertyTypeFloatUVE) { float v = 0; p->getValue(inst, &v); return v; }
            if (type == Scene::kPropertyTypeInt32UVE) { std::int32_t v = 0; p->getValue(inst, &v); return static_cast<float>(v); }
            if (type == Scene::kPropertyTypeUInt32UVE) { std::uint32_t v = 0; p->getValue(inst, &v); return static_cast<float>(v); }
            std::uint8_t v = 0; p->getValue(inst, &v); return static_cast<float>(v);
        };
        const auto write = [this, e, p, type](const float value, const bool preview) {
            const auto apply = [&](const void* v) {
                static_cast<void>(preview ? m_editor.PreviewSelectedComponentPropertyUVE(*e, *p, v)
                                          : m_editor.SetSelectedComponentPropertyUVE(*e, *p, v));
            };
            if (type == Scene::kPropertyTypeFloatUVE) { const float v = value; apply(&v); }
            else if (type == Scene::kPropertyTypeInt32UVE) { const auto v = static_cast<std::int32_t>(std::lround(value)); apply(&v); }
            else if (type == Scene::kPropertyTypeUInt32UVE) { const auto v = static_cast<std::uint32_t>(std::max(0L, std::lround(value))); apply(&v); }
            else { const auto v = static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L)); apply(&v); }
        };
        row->sync = [read, weak](const void* inst) {
            auto n = weak.lock();
            if (n && inst != nullptr) {
                const float v = read(inst);
                if (std::abs(n->value() - v) > 1e-6F) {
                    n->setValue(v);
                }
            }
        };
        number->onBegin = begin;
        number->onChange = [write](const float v) { write(v, true); };
        number->onCommit = [write, commit](const float v) {
            write(v, true);
            commit();
        };
        editor = number;
    } else if (property.typeId == Scene::kPropertyTypeVector3UVE || property.typeId == Scene::kPropertyTypeQuaternionUVE) {
        const bool quaternion = property.typeId == Scene::kPropertyTypeQuaternionUVE;
        auto field = prism::make<prism::Vec3Field>(StepUVE(property, quaternion ? 0.5F : 0.01F), quaternion ? 2 : 3);
        if (quaternion) {
            field->setSuffix("\xC2\xB0");
        }
        std::weak_ptr<prism::Vec3Field> weak = field;
        const auto read = [p, quaternion](const void* inst) {
            if (!quaternion) {
                Math::Vector3UVE v{};
                p->getValue(inst, &v);
                return v;
            }
            Math::QuaternionUVE q{};
            p->getValue(inst, &q);
            Math::Vector3UVE r{};
            static_cast<void>(Math::TryToEulerUVE(q, r));
            return Math::Vector3UVE{r.x * kRadToDegUVE, r.y * kRadToDegUVE, r.z * kRadToDegUVE};
        };
        row->sync = [read, weak](const void* inst) {
            auto f = weak.lock();
            if (f && inst != nullptr) {
                const Math::Vector3UVE v = read(inst);
                if (std::abs(f->get(0) - v.x) > 1e-5F || std::abs(f->get(1) - v.y) > 1e-5F || std::abs(f->get(2) - v.z) > 1e-5F) {
                    f->setValue(v.x, v.y, v.z);
                }
            }
        };
        field->onBegin = begin;
        field->onChange = [this, e, p, weak, quaternion](int, float) {
            auto f = weak.lock();
            if (!f) {
                return;
            }
            const Math::Vector3UVE v{f->get(0), f->get(1), f->get(2)};
            if (!quaternion) {
                static_cast<void>(m_editor.PreviewSelectedComponentPropertyUVE(*e, *p, &v));
                return;
            }
            Math::QuaternionUVE q{};
            if (Math::TryMakeEulerUVE(Math::Vector3UVE{v.x * kDegToRadUVE, v.y * kDegToRadUVE, v.z * kDegToRadUVE}, q)) {
                static_cast<void>(m_editor.PreviewSelectedComponentPropertyUVE(*e, *p, &q));
            }
        };
        field->onCommit = commit;
        editor = field;
    } else if (property.typeId == Scene::kPropertyTypeVector2UVE) {
        auto field = prism::make<Vec2FieldUVE>(StepUVE(property, 0.01F), 3);
        std::weak_ptr<Vec2FieldUVE> weak = field;
        row->sync = [p, weak](const void* inst) {
            auto f = weak.lock();
            if (f && inst != nullptr) {
                Math::Vector2UVE v{};
                p->getValue(inst, &v);
                if (std::abs(f->m_fields[0]->value() - v.x) > 1e-5F) { f->m_fields[0]->setValue(v.x); }
                if (std::abs(f->m_fields[1]->value() - v.y) > 1e-5F) { f->m_fields[1]->setValue(v.y); }
            }
        };
        for (auto& n : field->m_fields) {
            n->onBegin = begin;
            n->onChange = [this, e, p, weak](float) {
                if (auto f = weak.lock()) {
                    const Math::Vector2UVE v{f->m_fields[0]->value(), f->m_fields[1]->value()};
                    static_cast<void>(m_editor.PreviewSelectedComponentPropertyUVE(*e, *p, &v));
                }
            };
            n->onCommit = [commit](float) { commit(); };
        }
        editor = field;
    } else if (property.typeId == Scene::kPropertyTypeColorUVE || property.typeId == Scene::kPropertyTypeLinearColorUVE) {
        const bool linear = property.typeId == Scene::kPropertyTypeLinearColorUVE;
        auto color = prism::make<prism::ColorField>();
        std::weak_ptr<prism::ColorField> weak = color;
        const auto read = [p, linear](const void* inst) {
            if (!linear) {
                Math::Vector3UVE v{};
                p->getValue(inst, &v);
                return ToPrismColorUVE(v.x, v.y, v.z);
            }
            Math::ColorUVE c{};
            p->getValue(inst, &c);
            const Math::Vector3UVE shown = Math::DisplayFromColorUVE(c);
            return ToPrismColorUVE(shown.x, shown.y, shown.z);
        };
        row->sync = [read, weak](const void* inst) {
            auto f = weak.lock();
            if (f && inst != nullptr) {
                const prism::Color c = read(inst);
                const prism::Color now = f->color();
                if (c.r != now.r || c.g != now.g || c.b != now.b) {
                    f->setColor(c);
                }
            }
        };
        color->onChange = [this, e, p, linear, weakRow](const prism::Color c) {
            if (auto r = weakRow.lock()) {
                r->editing = true;
            }
            const Math::Vector3UVE picked = FromPrismColorUVE(c);
            if (linear) {
                const Math::ColorUVE value = Math::ColorFromDisplayUVE(picked);
                static_cast<void>(m_editor.PreviewSelectedComponentPropertyUVE(*e, *p, &value));
            } else {
                static_cast<void>(m_editor.PreviewSelectedComponentPropertyUVE(*e, *p, &picked));
            }
        };
        color->onCommit = [this, weakRow] {
            static_cast<void>(m_editor.CommitComponentPropertyPreviewUVE());
            if (auto r = weakRow.lock()) {
                r->editing = false;
            }
        };
        editor = color;
    } else if (property.typeId == Scene::kPropertyTypeStringUVE) {
        auto text = prism::make<prism::TextInput>("");
        std::weak_ptr<prism::TextInput> weak = text;
        row->sync = [p, weak](const void* inst) {
            auto t = weak.lock();
            if (t && inst != nullptr) {
                std::string v;
                p->getValue(inst, &v);
                if (t->text() != v) {
                    t->setText(v);
                }
            }
        };
        text->onCommit = [this, e, p](const std::string& v) { static_cast<void>(m_editor.SetSelectedComponentPropertyUVE(*e, *p, &v)); };
        editor = text;
        // A "file:ext,..." row names a file: the path stays editable, and the picker beside it offers
        // the project's files of those kinds, Load... and Import....
        constexpr std::string_view kFilePrefix = "file:";
        // The environment's sky is a slot too: Empty keeps the procedural sky, a panorama (a published
        // .uvtex or the source image) is sampled instead of it.
        const bool skySlot = property.customDrawerId == "sky:asset";
        // The object's script, a slot too: a .uvs file of the project, or Empty.
        const bool scriptSlot = property.customDrawerId == "script-slot";
        if (skySlot || scriptSlot || property.customDrawerId.rfind(kFilePrefix, 0U) == 0U) {
            std::vector<std::string> extensions;
            std::string list = skySlot      ? std::string{"uvtex,png,jpg,jpeg,bmp,tga"}
                               : scriptSlot ? std::string{"uvbp,uvs"}
                                            : property.customDrawerId.substr(kFilePrefix.size());
            for (std::size_t start = 0U; start <= list.size();) {
                const std::size_t comma = list.find(',', start);
                const std::string kind = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                if (!kind.empty()) {
                    extensions.push_back("." + kind);
                }
                if (comma == std::string::npos) {
                    break;
                }
                start = comma + 1U;
            }
            // One button, like an asset slot: the file's name, or (Empty); the menu has Empty too.
            auto pick = prism::make<prism::Button>("Empty", prism::Icon::None,
                                                   prism::ButtonStyle::Default);
            pick->setTooltip(property.tooltip.empty() ? std::string("Pick, load or import a file, or drop one from the Content Browser") : property.tooltip);
            std::weak_ptr<prism::Button> weakPick = pick;
            row->sync = [p, weakPick](const void* inst) {
                auto b = weakPick.lock();
                if (b && inst != nullptr) {
                    std::string v;
                    p->getValue(inst, &v);
                    const std::string text = v.empty() ? std::string("Empty") : std::filesystem::path{v}.stem().string();
                    if (b->text() != text) {
                        b->setText(text);
                    }
                }
            };
            // Content-relative, as these rows have always stored it; a file outside Content keeps its full path.
            const std::function<void(const std::filesystem::path&)> useFile = [this, e, p, scriptSlot](const std::filesystem::path& absolute) {
                const std::filesystem::path root = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot;
                const std::filesystem::path relative = absolute.lexically_relative(root);
                const std::string value = (relative.empty() || *relative.begin() == "..") ? absolute.generic_string()
                                                                                            : relative.generic_string();
                if (scriptSlot) {
                    if (!m_editor.AssignScriptToSelectedEntityUVE(value)) {
                        m_editor.m_contentStatusMessage = m_editor.DescribeScriptAssetProblemUVE(value);
                    }
                    return;
                }
                static_cast<void>(m_editor.SetSelectedComponentPropertyUVE(*e, *p, &value));
            };
            pick->onClick = [this, e, p, weakPick, extensions, useFile, skySlot, scriptSlot] {
                PickerSpecUVE spec = PickerSpecForUVE(extensions);
                if (skySlot) {
                    spec.title = "Sky";
                } else if (scriptSlot) {
                    spec.title = "Script";
                }
                PickerActionsUVE actions;
                const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
                const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
                const Scene::EntityUVE selected = m_editor.GetSelectedEntityUVE();
                if (em.HasComponentUVE(selected, e->typeIndex)) {
                    std::string value;
                    p->getValue(em.GetComponentPointerUVE(selected, e->typeIndex), &value);
                    if (!value.empty()) {
                        const std::filesystem::path stored{value};
                        actions.current = stored.is_absolute() ? stored : snapshot.contentRoot / stored;
                    }
                }
                actions.empty = [this, e, p] {
                    const std::string none;
                    static_cast<void>(m_editor.SetSelectedComponentPropertyUVE(*e, *p, &none));
                };
                actions.file = useFile;
                if (auto b = weakPick.lock()) {
                    ShowPickerUVE(spec, std::move(actions), b->rect());
                }
            };
            if (const std::optional<AssetThumbnailUVE::KindUVE> kind = ThumbnailKindForUVE(extensions); kind.has_value()) {
                auto thumb = prism::make<AssetThumbnailUVE>(m_editor, *kind);
                std::weak_ptr<AssetThumbnailUVE> weakThumb = thumb;
                row->sync = [this, p, weakThumb, base = std::move(row->sync)](const void* inst) {
                    if (base) {
                        base(inst);
                    }
                    auto t = weakThumb.lock();
                    if (t && inst != nullptr) {
                        std::string v;
                        p->getValue(inst, &v);
                        const std::filesystem::path stored{v};
                        t->setAssetUVE(v.empty() ? std::filesystem::path{}
                                                 : stored.is_absolute()
                                                       ? stored
                                                       : m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot / stored);
                    }
                };
                auto box = prism::hbox(6.0F);
                box->add(thumb, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Top});
                box->add(pick, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Top});
                editor = box;
            } else {
                editor = pick;
            }
            editor = WithAssetDropUVE([this] { return m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot; }, editor, PickerSpecForUVE(extensions).loadable, useFile);
        }
    } else if (property.typeId == Scene::kPropertyTypeBitMask32UVE) {
        auto button = prism::make<prism::Button>("", prism::Icon::None, prism::ButtonStyle::Default);
        std::weak_ptr<prism::Button> weak = button;
        const bool physics = property.customDrawerId == Scene::kLayerMaskDrawerPhysicsUVE;
        const bool layers = physics || property.customDrawerId == Scene::kLayerMaskDrawerRenderUVE;
        row->sync = [p, weak](const void* inst) {
            auto b = weak.lock();
            if (b && inst != nullptr) {
                std::uint32_t v = 0U;
                p->getValue(inst, &v);
                int count = 0;
                for (int bit = 0; bit < 32; ++bit) {
                    count += (v >> bit) & 1U;
                }
                const std::string text = v == 0U ? "Nothing" : v == 0xFFFFFFFFU ? "Everything" : std::to_string(count) + " layers";
                if (b->text() != text) {
                    b->setText(text);
                }
            }
        };
        button->onClick = [this, e, p, weak, physics, layers] {
            const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
            const Scene::EntityUVE selected = m_editor.GetSelectedEntityUVE();
            if (!em.HasComponentUVE(selected, e->typeIndex)) {
                return;
            }
            std::uint32_t mask = 0U;
            p->getValue(em.GetComponentPointerUVE(selected, e->typeIndex), &mask);
            std::vector<prism::MenuItem> items;
            const Config::SettingsDocumentUVE& project = m_editor.m_services->GetProjectSettingsUVE();
            for (std::size_t bit = 0; bit < 32U; ++bit) {
                std::string name = layers ? Core::GetLayerNameUVE(project, physics ? Core::LayerSetUVE::Physics : Core::LayerSetUVE::Render, bit)
                                          : std::string{};
                if (name.empty()) {
                    name = "Layer " + std::to_string(bit + 1U);
                }
                items.push_back(prism::MenuItem::Check(name, ((mask >> bit) & 1U) != 0U, [this, e, p, mask, bit] {
                    const std::uint32_t next = mask ^ (1U << bit);
                    static_cast<void>(m_editor.SetSelectedComponentPropertyUVE(*e, *p, &next));
                }));
            }
            if (auto b = weak.lock()) {
                prism::showMenu(std::move(items), b->rect(), prism::PopupPlacement::Below);
            }
        };
        editor = button;
    } else if (property.typeId == Scene::kPropertyTypeAssetGuidUVE) {
        // Picking, never typing (as the Inspector): the registered assets of the kind the property
        // declares - a material slot takes materials, "asset:<ext>" that extension.
        const auto lower = [](std::string v) { return LowerUVE(std::move(v)); };
        const bool material = property.customDrawerId == "file:uvmat" || property.customDrawerId == "file:uvmaterial" ||
                              property.customDrawerId == "asset:uvmat" || property.customDrawerId == "asset:uvmaterial" ||
                              lower(property.name).find("material") != std::string::npos ||
                              lower(property.displayName).find("material") != std::string::npos;
        std::vector<std::string> extensions;
        if (material) {
            extensions = {".uvmat", ".uvmaterial"};
        } else if (property.customDrawerId.rfind("asset:", 0U) == 0U) {
            extensions = {"." + property.customDrawerId.substr(6U)};
        }
        auto button = prism::make<prism::Button>("Empty", prism::Icon::None,
                                                 prism::ButtonStyle::Default);
        std::weak_ptr<prism::Button> weak = button;
        row->sync = [this, p, weak](const void* inst) {
            auto b = weak.lock();
            if (!b || inst == nullptr) {
                return;
            }
            Asset::AssetGuidUVE value{};
            p->getValue(inst, &value);
            std::string text = "Empty";
            if (value != Asset::AssetGuidUVE{}) {
                text = "Missing asset";
                for (const Asset::ProjectFileEntryUVE& file : m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE().entries) {
                    if (file.registeredAssetGuid.has_value() && *file.registeredAssetGuid == value) {
                        text = file.relativePath.stem().string();
                        break;
                    }
                }
                // Registered since the file index's last scan (a clip just added): the database knows it.
                if (text == "Missing asset") {
                    if (const std::filesystem::path path = m_editor.m_services->GetAssetDatabaseUVE().ResolveUVE(value); !path.empty()) {
                        text = path.stem().string();
                    }
                }
            }
            if (b->text() != text) {
                b->setText(text);
            }
        };
        if (extensions.empty()) {
            button->setEnabled(false); // a property that declares no kind is read-only
        }
        if (material && instance != nullptr) {
            Asset::AssetGuidUVE current{};
            p->getValue(instance, &current);
            if (current != Asset::AssetGuidUVE{}) {
                const std::filesystem::path path = m_editor.m_services->GetAssetDatabaseUVE().ResolveUVE(current);
                if (!path.empty()) {
                    m_pendingMaterials.emplace_back(path, property.displayName.empty() ? property.name : property.displayName);
                }
            }
        }
        // A MeshInstance3D's mesh: a model, or one of the built-in shapes, or nothing.
        const bool meshSource = e->typeIndex == std::type_index(typeid(Scene::MeshComponentUVE)) && property.name == "meshGuid";
        if (meshSource) {
            row->sync = [this, p, weak](const void* inst) {
                auto b = weak.lock();
                if (!b || inst == nullptr) {
                    return;
                }
                Asset::AssetGuidUVE value{};
                p->getValue(inst, &value);
                const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
                const Scene::EntityUVE selected = m_editor.GetSelectedEntityUVE();
                std::string text = "Empty";
                if (value != Asset::AssetGuidUVE{}) {
                    const std::filesystem::path path = PathOfGuidUVE(value);
                    text = path.empty() ? std::string("Missing model") : path.stem().string();
                } else if (em.HasComponentUVE<Scene::PrimitiveMeshComponentUVE>(selected)) {
                    static constexpr const char* kNames[] = {"Box", "Sphere", "Plane", "Capsule", "Cylinder"};
                    const auto kind = static_cast<std::size_t>(em.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(selected).kind);
                    text = std::string(kind < std::size(kNames) ? kNames[kind] : "Shape") + " (shape)";
                }
                if (b->text() != text) {
                    b->setText(text);
                }
            };
        }
        // What a picked, loaded or dropped file does to this slot.
        std::function<void(const std::filesystem::path&)> useFile;
        if (meshSource) {
            useFile = [this](const std::filesystem::path& absolute) {
                const Asset::AssetGuidUVE guid = PublishPickedUVE(absolute);
                if (guid == Asset::AssetGuidUVE{}) {
                    m_editor.m_contentStatusMessage = "Could not read " + absolute.filename().string() + " as a model.";
                    return;
                }
                static_cast<void>(m_editor.SetSelectedMeshSourceUVE(guid, std::nullopt));
            };
        } else {
            useFile = [this, e, p](const std::filesystem::path& absolute) {
                const Asset::AssetGuidUVE guid = PublishPickedUVE(absolute);
                if (guid == Asset::AssetGuidUVE{}) {
                    m_editor.m_contentStatusMessage = "Could not use " + absolute.filename().string() + ".";
                    return;
                }
                static_cast<void>(m_editor.SetSelectedComponentPropertyUVE(*e, *p, &guid));
            };
        }
        button->onClick = [this, e, p, weak, extensions, meshSource, useFile] {
            PickerSpecUVE spec = PickerSpecForUVE(extensions);
            spec.shapes = meshSource;
            PickerActionsUVE actions;
            const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
            const Scene::EntityUVE selected = m_editor.GetSelectedEntityUVE();
            if (em.HasComponentUVE(selected, e->typeIndex)) {
                Asset::AssetGuidUVE current{};
                p->getValue(em.GetComponentPointerUVE(selected, e->typeIndex), &current);
                actions.current = PathOfGuidUVE(current);
            }
            if (meshSource) {
                if (em.HasComponentUVE<Scene::PrimitiveMeshComponentUVE>(selected)) {
                    actions.currentShape = em.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(selected).kind;
                }
                actions.empty = [this] { static_cast<void>(m_editor.SetSelectedMeshSourceUVE(Asset::AssetGuidUVE{}, std::nullopt)); };
                actions.file = useFile;
                actions.shape = [this](const Scene::PrimitiveMeshKindUVE kind) {
                    static_cast<void>(m_editor.SetSelectedMeshSourceUVE(Asset::AssetGuidUVE{}, kind));
                };
            } else {
                actions.empty = [this, e, p] {
                    const Asset::AssetGuidUVE none{};
                    static_cast<void>(m_editor.SetSelectedComponentPropertyUVE(*e, *p, &none));
                };
                actions.file = useFile;
            }
            if (auto b = weak.lock()) {
                ShowPickerUVE(spec, std::move(actions), b->rect());
            }
        };
        // Unreal's slot layout: the asset's own preview beside its picker.
        if (const std::optional<AssetThumbnailUVE::KindUVE> kind = ThumbnailKindForUVE(extensions); kind.has_value()) {
            auto thumb = prism::make<AssetThumbnailUVE>(m_editor, *kind);
            std::weak_ptr<AssetThumbnailUVE> weakThumb = thumb;
            row->sync = [this, p, weakThumb, base = std::move(row->sync)](const void* inst) {
                if (base) {
                    base(inst);
                }
                auto t = weakThumb.lock();
                if (t && inst != nullptr) {
                    Asset::AssetGuidUVE value{};
                    p->getValue(inst, &value);
                    t->setAssetUVE(PathOfGuidUVE(value));
                }
            };
            auto box = prism::hbox(6.0F);
            box->add(thumb, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Top});
            box->add(button, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Top});
            editor = box;
        } else {
            editor = button;
        }
        editor = WithAssetDropUVE([this] { return m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot; }, editor, PickerSpecForUVE(extensions).loadable, useFile);
    } else if (property.typeId == Scene::kPropertyTypeEntityUVE) {
        auto button = prism::make<prism::Button>("Default", prism::Icon::None, prism::ButtonStyle::Default);
        std::weak_ptr<prism::Button> weak = button;
        row->sync = [this, p, weak](const void* inst) {
            auto b = weak.lock();
            if (!b || inst == nullptr) {
                return;
            }
            Scene::EntityUVE value = Scene::kInvalidEntityUVE;
            p->getValue(inst, &value);
            const std::string text = value == Scene::kInvalidEntityUVE        ? std::string{"Default"}
                                     : !m_editor.IsDocumentEntityUVE(value) ? std::string{"Missing object"}
                                                                            : m_editor.GetEntityDisplayLabelUVE(value);
            if (b->text() != text) {
                b->setText(text);
            }
        };
        button->onClick = [this, e, p, weak] {
            std::vector<prism::MenuItem> items;
            items.push_back(prism::MenuItem::Item("Default", [this, e, p] {
                const Scene::EntityUVE none = Scene::kInvalidEntityUVE;
                static_cast<void>(m_editor.SetSelectedComponentPropertyUVE(*e, *p, &none));
            }));
            items.push_back(prism::MenuItem::Sep());
            for (const Scene::EntityUVE candidate : m_editor.GetEntityReferenceCandidatesUVE()) {
                items.push_back(prism::MenuItem::Item(m_editor.GetEntityDisplayLabelUVE(candidate), [this, e, p, candidate] {
                    static_cast<void>(m_editor.SetSelectedComponentPropertyUVE(*e, *p, &candidate));
                }));
            }
            if (auto b = weak.lock()) {
                prism::showMenu(std::move(items), b->rect(), prism::PopupPlacement::Below);
            }
        };
        editor = button;
    } else {
        // Lists and block drawers (scripts, animation graphs, LOD tables...) get their PRISM editors
        // next; until then the value type is shown, never silently dropped.
        auto value = prism::make<prism::Label>(std::string(property.typeId.ToStringUVE()), prism::FontRole::Small);
        value->setColor(prism::ThemeColor(&prism::Theme::textDim));
        editor = value;
    }

    editor->setEnabled(writable);
    editor->setTooltip(property.tooltip);
    row->editor = editor.get();
    if (row->sync) {
        row->sync(instance);
    }
    m_rows.push_back(row);
    m_grid->addRow(category, label, editor, property.tooltip);
}


// ---------------------------------------------------------------------------------- script exports

void EditorDetailsUVE::AddScriptExportsUVE(const int category, const bool writable) {
    // One row per `export` field of the object's script, typed; a changed value is marked, and its
    // tooltip has the script's default (the reset button puts it back).
    for (const EditorUVE::ScriptExportRowUVE& row : m_editor.GetSelectedScriptExportsUVE()) {
        const std::optional<UVScript::ValueUVE> value = UVScript::ParseValueTextUVE(row.valueText, row.type);
        if (!value.has_value()) {
            continue;
        }
        const std::string name = row.name;
        const auto set = [this, name](std::string text) { static_cast<void>(m_editor.SetSelectedScriptExportUVE(name, std::move(text))); };
        prism::Ref<prism::Widget> editor;
        if (const bool* const flag = std::get_if<bool>(&*value)) {
            auto check = prism::make<prism::CheckBox>("", *flag);
            check->onChange = [set](const bool v) { set(v ? "true" : "false"); };
            editor = check;
        } else if (const std::int64_t* const integer = std::get_if<std::int64_t>(&*value)) {
            auto number = prism::make<prism::NumberField>(static_cast<float>(*integer), 1.0F, 0);
            number->onCommit = [set](const float v) { set(std::to_string(std::llround(v))); };
            editor = number;
        } else if (const double* const real = std::get_if<double>(&*value)) {
            auto number = prism::make<prism::NumberField>(static_cast<float>(*real), 0.05F, 3);
            number->onCommit = [set](const float v) { set(UVScript::FormatValueUVE(UVScript::ValueUVE{static_cast<double>(v)})); };
            editor = number;
        } else if (const auto* const vector = std::get_if<UVScript::Vec3ValueUVE>(&*value)) {
            auto parts = std::make_shared<std::array<double, 3>>(std::array<double, 3>{vector->x, vector->y, vector->z});
            auto box = prism::hbox(4.0F);
            for (std::size_t axis = 0U; axis < 3U; ++axis) {
                auto number = prism::make<prism::NumberField>(static_cast<float>((*parts)[axis]), 0.05F, 3);
                number->onCommit = [set, parts, axis](const float v) {
                    (*parts)[axis] = static_cast<double>(v);
                    set(UVScript::FormatValueUVE(UVScript::ValueUVE{UVScript::Vec3ValueUVE{(*parts)[0], (*parts)[1], (*parts)[2]}}));
                };
                box->add(number, prism::Slot{1.0F});
            }
            editor = box;
        } else if (const std::string* const text = std::get_if<std::string>(&*value)) {
            auto input = prism::make<prism::TextInput>(*text, "");
            input->onCommit = [set](const std::string& v) { set(v); };
            editor = input;
        } else {
            continue;
        }
        editor->setEnabled(writable);
        auto line = prism::hbox(4.0F);
        line->add(editor, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        if (row.overridden) {
            auto reset = prism::make<prism::Button>("", prism::Icon::Undo, prism::ButtonStyle::Flat);
            reset->setTooltip("Back to the script's default (" + row.defaultText + ")");
            reset->setEnabled(writable);
            reset->onClick = [this, name] { static_cast<void>(m_editor.SetSelectedScriptExportUVE(name, std::nullopt)); };
            line->add(reset, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        }
        m_grid->addRow(category, row.name, line, row.type.NameUVE() + ", default " + row.defaultText);
    }
}

// ---------------------------------------------------------------------------------- object metadata

void EditorDetailsUVE::AddObjectMetadataUVE(const int category, const void* const instance, const bool writable) {
    if (instance == nullptr) {
        return;
    }
    using Type = Core::VariantTypeUVE;
    const auto& metadata = *static_cast<const Scene::ObjectMetadataComponentUVE*>(instance);
    for (const Scene::ObjectMetadataEntryUVE& item : metadata.entries) {
        const std::string key = item.key;
        const Core::VariantUVE& v = item.value;
        const auto setValue = [this, key](const Core::VariantUVE& value) {
            static_cast<void>(m_editor.SetSelectedObjectMetadataValueUVE(key, value));
        };
        auto name = prism::make<prism::TextInput>(key, "");
        name->onCommit = [this, key](const std::string& to) {
            if (!to.empty() && to != key) {
                static_cast<void>(m_editor.RenameSelectedObjectMetadataUVE(key, to));
            }
        };
        prism::Ref<prism::Widget> value;
        switch (v.GetTypeUVE()) {
            case Type::Bool: {
                auto check = prism::make<prism::CheckBox>("", v.TryGetUVE<bool>() != nullptr && *v.TryGetUVE<bool>());
                check->onChange = [setValue](const bool b) { setValue(Core::VariantUVE::MakeBoolUVE(b)); };
                value = check;
                break;
            }
            case Type::Int: {
                const std::int64_t* const n = v.TryGetUVE<std::int64_t>();
                auto number = prism::make<prism::NumberField>(n != nullptr ? static_cast<float>(*n) : 0.0F, 1.0F, 0);
                number->onCommit = [setValue](const float f) { setValue(Core::VariantUVE::MakeIntUVE(std::llround(f))); };
                value = number;
                break;
            }
            case Type::Float: {
                const double* const n = v.TryGetUVE<double>();
                auto number = prism::make<prism::NumberField>(n != nullptr ? static_cast<float>(*n) : 0.0F, 0.05F, 3);
                number->onCommit = [setValue](const float f) { setValue(Core::VariantUVE::MakeFloatUVE(static_cast<double>(f))); };
                value = number;
                break;
            }
            case Type::String:
            case Type::InternedString:
            case Type::ObjectPath: {
                const std::string* const s = v.TryGetUVE<std::string>();
                auto input = prism::make<prism::TextInput>(s != nullptr ? *s : std::string{}, "");
                const Type type = v.GetTypeUVE();
                input->onCommit = [setValue, type](const std::string& t) { setValue(Core::VariantUVE::MakeTextUVE(type, t)); };
                value = input;
                break;
            }
            default: {
                auto label = prism::make<prism::Label>("", prism::FontRole::Small);
                label->setColor(prism::ThemeColor(&prism::Theme::textDim));
                value = label;
                break;
            }
        }
        value->setEnabled(writable);
        auto remove = prism::make<prism::Button>("", prism::Icon::Trash, prism::ButtonStyle::Flat);
        remove->setTooltip("Remove");
        remove->setEnabled(writable);
        remove->onClick = [this, key] { static_cast<void>(m_editor.RemoveSelectedObjectMetadataUVE(key)); };
        auto line = prism::hbox(4.0F);
        line->add(name, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        line->add(value, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        line->add(remove, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        static constexpr std::array<const char*, 4> kTypes{"Bool", "Int", "Float", "String"};
        const auto typeIndex = static_cast<std::size_t>(v.GetTypeUVE());
        m_grid->addRow(category, typeIndex < kTypes.size() ? kTypes[typeIndex] : "Value", line);
    }
    // Add: a new key of the type picked, named uniquely.
    auto add = prism::make<prism::Button>("Add", prism::Icon::Plus, prism::ButtonStyle::Default);
    add->setEnabled(writable);
    std::weak_ptr<prism::Button> weakAdd = add;
    add->onClick = [this, weakAdd] {
        const auto addOf = [this](const Type type) {
            return [this, type] {
                for (int i = 1; i < 1000; ++i) {
                    const std::string key = i == 1 ? std::string("Key") : "Key" + std::to_string(i);
                    if (m_editor.AddSelectedObjectMetadataUVE(key, Core::VariantUVE::MakeDefaultUVE(type))) {
                        return;
                    }
                }
            };
        };
        std::vector<prism::MenuItem> items{prism::MenuItem::Item("Bool", addOf(Type::Bool)), prism::MenuItem::Item("Int", addOf(Type::Int)),
                                           prism::MenuItem::Item("Float", addOf(Type::Float)),
                                           prism::MenuItem::Item("String", addOf(Type::String))};
        if (auto b = weakAdd.lock()) {
            prism::showMenu(std::move(items), b->rect(), prism::PopupPlacement::Below);
        }
    };
    m_grid->addRow(category, "", add);
}

} // namespace UVE::Editor
