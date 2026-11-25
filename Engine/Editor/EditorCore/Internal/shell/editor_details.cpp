// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_details.h"
#include "model_animations.h"

#include "editor_asset_thumbnail.h"
#include "editor_asset_table.h"

#include "color.h"
#include "rect.h"
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
#include "ragdoll_3d.h"
#include "skeleton_3d.h"
#include "camera_component.h"
#include "bone_attachment_3d.h"
#include "core_variant.h"
#include "object_metadata_component.h"
#include "blueprint_value.h"

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
#include <system_error>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#include <cstddef>

namespace UVE::Editor {
namespace {

using TypeFlags = Core::TypeMetadataPropertyFlags;

constexpr float kRadToDeg = 57.29577951F;
constexpr float kDegToRad = 0.01745329252F;

/// The Euler angles Details shows: the authored ones while they still make the object's rotation,
/// else read back from the rotation itself (the gizmo, a script or the Sequencer turned it).
[[nodiscard]] Math::Vector3 ShownEuler(const Scene::TransformComponent& t) noexcept {
    Math::Quaternion fromEuler{};
    if (Math::TryMakeEuler(t.localEulerRadians, fromEuler)) {
        const Math::Quaternion& r = t.localRotation;
        const float dot = fromEuler.x * r.x + fromEuler.y * r.y + fromEuler.z * r.z + fromEuler.w * r.w;
        if (std::abs(dot) > 0.99999F) {
            return t.localEulerRadians;
        }
    }
    Math::Vector3 derived{};
    return Math::TryToEuler(t.localRotation, derived) ? derived : t.localEulerRadians;
}

/// The component types whose authoring is not a field write (see the Inspector's own list): the
/// name routes through SetSelectedEntityName, the transform through the gesture API, and the
/// parent through reparenting in the Outliner.
constexpr std::array<std::string_view, 3> kCustomDrawn{"component.name", "component.transform",
                                                          "component.hierarchy"};

/// The object-wide engine settings every object carries (how it is processed, threaded,
/// interpolated, translated, described): kept, but collapsed, as the standard Advanced, so what the object
/// is comes first.
[[nodiscard]] bool IsAdvancedSection(const std::string_view title) {
    static constexpr std::array<std::string_view, 5> kAdvanced{"Process", "Thread Group", "Physics Interpolation", "Auto Translate",
                                                               "Editor Description"};
    return std::find(kAdvanced.begin(), kAdvanced.end(), title) != kAdvanced.end();
}

[[nodiscard]] bool IsCustomDrawn(const std::string_view typeId) {
    return std::find(kCustomDrawn.begin(), kCustomDrawn.end(), typeId) != kCustomDrawn.end();
}

/// The preview a slot of these file kinds shows beside its picker, if any.
/// An asset slot's drop target: a Asset Table item of the slot's kinds dropped on it is used
/// as if it were picked (in the document of the window it is dropped in).
class AssetSlotDrop final : public prism::Border {
public:
    AssetSlotDrop(std::function<std::filesystem::path()> contentRoot, std::vector<std::string> kinds,
                     std::function<void(const std::filesystem::path&)> use)
        : m_contentRoot(std::move(contentRoot)), m_kinds(std::move(kinds)), m_use(std::move(use)) {}
    bool onDragEnter(const prism::DragDropEvent& e) override { return Hot(Accepts(e).has_value()); }
    bool onDragOver(const prism::DragDropEvent& e) override { return Hot(Accepts(e).has_value()); }
    void onDragLeave() override { static_cast<void>(Hot(false)); }
    bool onDrop(const prism::DragDropEvent& e) override {
        static_cast<void>(Hot(false));
        const std::optional<std::filesystem::path> path = Accepts(e);
        if (!path.has_value() || !m_use) {
            return false;
        }
        m_use(*path);
        return true;
    }
    void paint(prism::Canvas& c) override {
        prism::Border::paint(c);
        if (m_hot) {
            c.strokeRoundRect(rect(), 3.0F, prism::theme().highlight, 2);
        }
    }

private:
    [[nodiscard]] bool Hot(const bool hot) {
        if (hot != m_hot) {
            m_hot = hot;
            invalidate();
        }
        return hot;
    }
    [[nodiscard]] std::optional<std::filesystem::path> Accepts(const prism::DragDropEvent& e) const {
        if (!e.operation || e.operation->tag() != EditorAssetTable::kDragTag) {
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

[[nodiscard]] prism::Ref<prism::Widget> MakeAssetDrop(std::function<std::filesystem::path()> contentRoot, prism::Ref<prism::Widget> slot,
                                                         std::vector<std::string> kinds,
                                                         std::function<void(const std::filesystem::path&)> use) {
    auto drop = prism::make<AssetSlotDrop>(std::move(contentRoot), std::move(kinds), std::move(use));
    drop->setChild(std::move(slot));
    return drop;
}

[[nodiscard]] std::optional<AssetThumbnail::Kind> ThumbnailKindFor(const std::vector<std::string>& extensions) {
    const auto has = [&](const char* kind) {
        return std::find(extensions.begin(), extensions.end(), std::string(kind)) != extensions.end();
    };
    if (has(".uvmat") || has(".uvmaterial")) {
        return AssetThumbnail::Kind::Material;
    }
    if (has(".uvmodel")) {
        return AssetThumbnail::Kind::Mesh;
    }
    if (has(".uvtex") || has(".png")) {
        return AssetThumbnail::Kind::Texture;
    }
    return std::nullopt;
}

[[nodiscard]] std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

void Hash(std::uint64_t& h, const std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
}

void Hash(std::uint64_t& h, const std::string_view s) {
    for (const char c : s) {
        Hash(h, static_cast<std::uint64_t>(static_cast<unsigned char>(c)));
    }
}

[[nodiscard]] float Step(const Core::TypeMetadataProperty& property, const float fallback) {
    return property.range.enabled && property.range.step > 0.0 ? static_cast<float>(property.range.step) : fallback;
}

[[nodiscard]] prism::Color ToPrismColor(const float r, const float g, const float b) {
    const auto c = [](const float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F)); };
    return prism::Color(c(r), c(g), c(b), 255);
}

[[nodiscard]] Math::Vector3 FromPrismColor(const prism::Color c) {
    return Math::Vector3{static_cast<float>(c.r) / 255.0F, static_cast<float>(c.g) / 255.0F,
                            static_cast<float>(c.b) / 255.0F};
}

/// Two NumberFields side by side for a Vector2.
class Vec2Field final : public prism::Widget {
public:
    Vec2Field(const float step, const int decimals) {
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

/// A rectangle as two rows: position (X, Y) above size (W, H), in pixels.
class RectField final : public prism::Widget {
public:
    RectField() {
        const std::array<const char*, 4> labels{"X", "Y", "W", "H"};
        const std::array<prism::Color, 4> colours{prism::Color(214, 84, 84), prism::Color(110, 180, 90), prism::Color(150, 150, 160),
                                                  prism::Color(150, 150, 160)};
        for (std::size_t i = 0; i < m_fields.size(); ++i) {
            m_fields[i] = prism::make<prism::NumberField>(0.0F, 1.0F, 0);
            m_fields[i]->setAxisLabel(labels[i], colours[i]);
            m_fields[i]->setDragSpeed(1.0F);
            addChild(m_fields[i]);
        }
        m_fields[2]->setRange(0.0F, 100000.0F);
        m_fields[3]->setRange(0.0F, 100000.0F);
    }
    prism::Vec2 measure(prism::Vec2) override { return m_desired = constrain({120.0F, prism::theme().controlH * 2.0F + 4.0F}); }
    void arrange(prism::Rect r) override {
        prism::Widget::arrange(r);
        const float w = std::floor((r.w - 4.0F) * 0.5F);
        const float h = prism::theme().controlH;
        for (std::size_t i = 0; i < m_fields.size(); ++i) {
            const float x = (i % 2U == 0U) ? r.x : r.x + w + 4.0F;
            const float y = (i < 2U) ? r.y : r.y + h + 4.0F;
            m_fields[i]->arrange(prism::Rect{x, y, i % 2U == 0U ? w : r.w - w - 4.0F, h});
        }
    }
    bool hitSelf() const override { return false; }
    [[nodiscard]] Math::Rect Value() const {
        return Math::Rect{Math::Vector2{m_fields[0]->value(), m_fields[1]->value()},
                             Math::Vector2{m_fields[2]->value(), m_fields[3]->value()}};
    }
    std::array<std::shared_ptr<prism::NumberField>, 4> m_fields;
};

} // namespace

prism::Ref<prism::Widget> WithAssetDrop(std::function<std::filesystem::path()> contentRoot, prism::Ref<prism::Widget> slot,
                                           std::vector<std::string> kinds, std::function<void(const std::filesystem::path&)> use) {
    return MakeAssetDrop(std::move(contentRoot), std::move(slot), std::move(kinds), std::move(use));
}

EditorDetails::EditorDetails(Editor& editor) : m_editor(editor) {}

EditorDetails::~EditorDetails() = default;

prism::Ref<prism::Widget> EditorDetails::Build() {
    auto page = prism::vbox(4.0F);
    page->setPadding(prism::Margin(4.0F));
    m_search = prism::make<prism::TextInput>("", "Search the inspector");
    m_search->onChanged = [this](const std::string& text) {
        m_filter = Lower(text);
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

bool EditorDetails::MatchesFilter(const std::string& text) const {
    return m_filter.empty() || Lower(text).find(m_filter) != std::string::npos;
}

std::uint64_t EditorDetails::StructureSignature(const Scene::Entity entity) const {
    std::uint64_t h = 1469598103934665603ULL;
    Hash(h, (static_cast<std::uint64_t>(entity.index) << 32U) | entity.generation);
    Hash(h, m_filter);
    Hash(h, static_cast<std::uint64_t>(m_editor.m_selectedEntities.size()));
    Hash(h, static_cast<std::uint64_t>(m_editor.m_playModeState));
    Hash(h, m_editor.IsAuthoringCommandAllowed() ? 1U : 0U);
    if (!m_editor.IsDocumentEntity(entity)) {
        if (m_editor.m_selectedProjectFile.has_value()) {
            Hash(h, m_editor.m_selectedProjectFile->relativePath.generic_string());
        }
        return h;
    }
    const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    // The registry is copied (and sorted) only when it changed, not every frame.
    static std::uint64_t s_registryGeneration = ~0ULL;
    static Core::TypeMetadataSnapshot s_registry;
    const auto& registry = Scene::GetSceneComponentMetadataRegistry();
    if (registry.GetGeneration() != s_registryGeneration) {
        s_registry = registry.GetSnapshot();
        s_registryGeneration = registry.GetGeneration();
    }
    for (const Entry& snapshot : s_registry.entries) {
        if (!em.HasComponent(entity, snapshot.typeIndex)) {
            continue;
        }
        Hash(h, static_cast<std::uint64_t>(std::hash<std::type_index>{}(snapshot.typeIndex)));
        // Visibility rules read values (a field shown only for one mode): a change re-lays the rows.
        const void* const instance = em.GetComponentPointer(entity, snapshot.typeIndex);
        for (const Property& property : snapshot.properties) {
            const bool visible = !property.flags.Has(TypeFlags::Hidden) &&
                                 (property.isVisible == nullptr || property.isVisible(instance));
            Hash(h, visible ? 1U : 0U);
            // An asset slot's value decides which material section follows it.
            if (property.typeId == Scene::kPropertyTypeAssetGuid && instance != nullptr) {
                Asset::AssetGuid value{};
                property.getValue(instance, &value);
                Hash(h, value.value);
            } else if (property.typeId == Scene::kPropertyTypeString && instance != nullptr &&
                       (property.customDrawerId == "file:uvmat" || property.customDrawerId == "file:uvmaterial")) {
                std::string value;
                property.getValue(instance, &value);
                Hash(h, value);
            }
            // A skeleton's bones are drawn as a tree: a new source re-lays it.
            if (property.customDrawerId == "skeleton-source" && instance != nullptr) {
                const auto& skeleton = *static_cast<const Scene::Skeleton3DComponent*>(instance);
                Hash(h, skeleton.skeletonAssetPath);
                Hash(h, static_cast<std::uint64_t>(skeleton.bones.size()));
            }
        }
        if (snapshot.typeIndex == std::type_index(typeid(Scene::Ragdoll3DComponent)) && instance != nullptr) {
            const auto& ragdoll = *static_cast<const Scene::Ragdoll3DComponent*>(instance);
            Hash(h, static_cast<std::uint64_t>(ragdoll.bones.size()));
            Hash(h, ragdoll.initialized ? 1U : 0U);
            Hash(h, ragdoll.editorPreviewSimulating ? 1U : 0U);
            Hash(h, static_cast<std::uint64_t>(m_selectedRagdollBoneIndex));
        }
    }
    return h;
}

bool EditorDetails::IsBeingEdited(const Row& row) const {
    if (row.editing) {
        return true;
    }
    if (row.editor == nullptr || !prism::Application::exists()) {
        return false;
    }
    const prism::Widget* const focus = prism::Application::get().focus();
    return focus != nullptr && focus->isDescendantOf(row.editor);
}

void EditorDetails::Refresh() {
    if (!m_grid) {
        return;
    }
    const Scene::Entity entity = m_editor.GetSelectedEntity();
    const std::uint64_t signature = StructureSignature(entity);
    if (signature != m_signature || entity != m_entity || m_rebuildRequested) {
        m_rebuildRequested = false;
        m_signature = signature;
        m_entity = entity;
        Rebuild(entity);
        return;
    }
    const bool hasEntity = m_editor.IsDocumentEntity(entity);
    // Same rows: show the values as they are now (a gizmo drag, an undo, a script, or a live
    // material preview refresh), except the one the author is editing.
    const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    for (const std::shared_ptr<Row>& row : m_rows) {
        if (!row->sync || IsBeingEdited(*row)) {
            continue;
        }
        const void* instance = nullptr;
        if (row->entry != nullptr) {
            if (!hasEntity || !em.HasComponent(entity, row->entry->typeIndex)) {
                continue;
            }
            instance = em.GetComponentPointer(entity, row->entry->typeIndex);
        }
        row->sync(instance);
    }
}

void EditorDetails::Rebuild(const Scene::Entity entity) {
    m_grid->clear();
    m_rows.clear();
    if (m_editor.m_selectedEntities.empty() || !m_editor.IsDocumentEntity(entity)) {
        if (m_editor.m_selectedProjectFile.has_value() && !m_editor.m_selectedProjectFile->relativePath.empty()) {
            const auto& file = *m_editor.m_selectedProjectFile;
            const auto itemType = m_editor.GetAssetTableEntryType(file);
            m_title->setText(file.relativePath.filename().string());
            m_subtitle->setText(Editor::GetAssetTableItemTypeLabel(itemType));
            const int section = m_grid->addCategory("Asset");
            m_grid->addRow(section, "Path", prism::make<prism::Label>(file.relativePath.generic_string()));
            m_grid->addRow(section, "Registered",
                           prism::make<prism::Label>(file.registeredAssetGuid.has_value() ? "Yes" : "No"));
            if (itemType == Editor::AssetTableItemType::Material) {
                const auto snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
                const std::filesystem::path matPath = snapshot.contentRoot / file.relativePath;
                auto preview = prism::make<AssetThumbnail>(m_editor, AssetThumbnail::Kind::Material, 128.0F);
                preview->setAsset(matPath);
                std::weak_ptr<AssetThumbnail> weakPreview = preview;
                auto previewRow = std::make_shared<Row>();
                previewRow->sync = [weakPreview](const void*) {
                    if (auto t = weakPreview.lock()) {
                        t->refresh();
                    }
                };
                previewRow->editor = preview.get();
                m_rows.push_back(std::move(previewRow));
                m_grid->addRow(section, "Preview", std::move(preview), "Live PBR material preview (click to enlarge).");
                AddMaterialSection(matPath, "Material");
            }
            return;
        }
        m_title->setText("Nothing selected");
        m_subtitle->setText("Select an object in the Outliner or the Viewport.");
        return;
    }
    if (!m_editor.HasSingleDocumentSelection()) {
        m_title->setText(std::to_string(m_editor.m_selectedEntities.size()) + " objects selected");
        m_subtitle->setText("Active: " + m_editor.GetEntityDisplayLabel(entity) +
                            " - editing applies to a single object.");
        return;
    }

    m_title->setText(m_editor.GetEntityDisplayLabel(entity));
    m_subtitle->setText(std::string(m_editor.GetObjectTypeName(entity)));
    const bool authoring = m_editor.IsAuthoringCommandAllowed();
    const bool simulating = m_editor.m_playModeState != EditorPlayModeState::Edit;

    // The object's own row: its name (renaming owns uniqueness and its own history entry).
    if (MatchesFilter("Name")) {
        const int object = m_grid->addCategory("Object");
        auto name = prism::make<prism::TextInput>(m_editor.GetEntityDisplayLabel(entity));
        name->setEnabled(authoring);
        name->onCommit = [this](const std::string& text) { static_cast<void>(m_editor.SetSelectedEntityName(text)); };
        auto row = std::make_shared<Row>();
        row->editor = name.get();
        std::weak_ptr<prism::TextInput> weak = name;
        row->sync = [this, weak](const void*) {
            if (auto field = weak.lock()) {
                const std::string label = m_editor.GetEntityDisplayLabel(m_editor.GetSelectedEntity());
                if (field->text() != label) {
                    field->setText(label);
                }
            }
        };
        m_rows.push_back(row);
        m_grid->addRow(object, "Name", name);
    }

    const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    // Sections in the Inspector's order: by declared section order, then type id.
    const Core::TypeMetadataRegistry& registry = Scene::GetSceneComponentMetadataRegistry();
    std::vector<const Entry*> entries;
    for (const Entry& snapshot : registry.GetSnapshot().entries) {
        const Entry* const stable = registry.FindType(snapshot.typeId);
        if (stable != nullptr && em.HasComponent(entity, stable->typeIndex)) {
            entries.push_back(stable);
        }
    }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry* a, const Entry* b) {
        return a->order != b->order ? a->order < b->order : a->typeId.ToString() < b->typeId.ToString();
    });
    // A nested type is drawn under the first of its hosts the entity carries (as the Inspector
    // does), and in its own place when it carries none.
    const auto hostOf = [&](const Entry* child) -> const Entry* {
        for (const auto& hostId : child->nestedUnderTypeIds) {
            for (const Entry* candidate : entries) {
                if (candidate->typeId == hostId && !IsCustomDrawn(candidate->typeId.ToString())) {
                    return candidate;
                }
            }
        }
        return nullptr;
    };
    std::vector<const Entry*> ordered;
    for (const Entry* entry : entries) {
        if (hostOf(entry) != nullptr) {
            continue;
        }
        ordered.push_back(entry);
        for (const Entry* child : entries) {
            if (hostOf(child) == entry) {
                ordered.push_back(child);
            }
        }
    }
    entries = std::move(ordered);

    std::unordered_set<std::string> shownMaterials;
    const auto addUniqueMaterial = [&](const std::filesystem::path& matPath, const std::string& slotLabel) {
        if (matPath.empty()) {
            return;
        }
        std::error_code ec;
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(matPath, ec);
        const std::string key = (ec ? matPath.lexically_normal() : canonical).generic_string();
        if (shownMaterials.insert(key).second) {
            AddMaterialSection(matPath, slotLabel);
        }
    };

    bool transformDone = false;
    // A widget is placed by its rect (and anchors), not by a 3D transform: the transform is not shown.
    const bool widget = std::any_of(entries.begin(), entries.end(), [](const Entry* const entry) {
        return entry->typeId.ToString().starts_with("component.ui_");
    });
    const bool hasTransform = !widget && em.HasComponent<Scene::TransformComponent>(entity);
    for (const Entry* const entry : entries) {
        const std::string_view typeId = entry->typeId.ToString();
        if (!transformDone && hasTransform && entry->order > Scene::kSectionOrderTransform) {
            AddTransform(-1, entity);
            transformDone = true;
        }
        if (IsCustomDrawn(typeId)) {
            continue;
        }
        const void* const instance = em.GetComponentPointer(entity, entry->typeIndex);
        const std::string title = entry->sectionTitle != nullptr ? std::string(entry->sectionTitle(instance)) : entry->displayName;
        const bool sectionMatches = MatchesFilter(title);
        int category = -1;
        std::string group;
        for (const Property& property : entry->properties) {
            const bool visible = !property.flags.Has(TypeFlags::Hidden) &&
                                 (property.isVisible == nullptr || property.isVisible(instance));
            if (!visible || (property.flags.Has(TypeFlags::RuntimeState) && !simulating)) {
                continue;
            }
            const bool companion = std::any_of(entry->properties.begin(), entry->properties.end(),
                                               [&](const Property& other) { return other.resolvedByProperty == property.name; });
            if (companion) {
                continue;
            }
            const std::string label = property.displayName.empty() ? property.name : property.displayName;
            if (!sectionMatches && !MatchesFilter(label) && !MatchesFilter(property.section)) {
                continue;
            }
            if (category < 0 || property.section != group) {
                group = property.section;
                category = m_grid->addCategory(group.empty() ? title : title + " / " + group, !IsAdvancedSection(title));
            }
            AddProperty(category, *entry, property, instance, authoring && property.IsAuthoringWritable());
        }
        for (const auto& [path, slot] : m_pendingMaterials) {
            addUniqueMaterial(path, slot);
        }
        m_pendingMaterials.clear();
        if (entry->typeIndex == std::type_index(typeid(Scene::CameraComponent))) {
            AddCameraPreview(entity);
        }
        if (entry->typeIndex == std::type_index(typeid(Scene::Ragdoll3DComponent))) {
            AddRagdollBuilder(entity);
        }
    }
    // A model's own materials (one per surface), edited in place like a slot's.
    if (em.HasComponent<Scene::MeshComponent>(entity)) {
        if (const std::optional<Asset::MeshAsset> mesh = m_editor.LoadEntityMeshAsset(entity); mesh.has_value()) {
            for (std::size_t i = 0; i < mesh->materialGuids.size(); ++i) {
                const std::filesystem::path path = m_editor.m_services->GetAssetDatabase().Resolve(mesh->materialGuids[i]);
                if (!path.empty()) {
                    addUniqueMaterial(path, "Surface " + std::to_string(i));
                }
            }
        }
    }
    if (!transformDone && hasTransform) {
        AddTransform(-1, entity);
    }
}

void EditorDetails::AddTransform(int category, const Scene::Entity entity) {
    if (!MatchesFilter("Transform") && !MatchesFilter("Location") && !MatchesFilter("Rotation") &&
        !MatchesFilter("Scale")) {
        return;
    }
    if (category < 0) {
        category = m_grid->addCategory("Transform", true);
    }
    const bool authoring = m_editor.IsAuthoringCommandAllowed();
    static_cast<void>(entity);
    struct Axis final {
        const char* label;
        EditorToolSessionMode mode;
        float step;
    };
    static constexpr std::array<Axis, 3> kRows{{
        {"Location", EditorToolSessionMode::Translate, 0.01F},
        {"Rotation", EditorToolSessionMode::Rotate, 0.5F},
        {"Scale", EditorToolSessionMode::Scale, 0.01F},
    }};
    for (std::size_t which = 0; which < kRows.size(); ++which) {
        auto field = prism::make<prism::Vec3Field>(kRows[which].step, which == 1 ? 2 : 3);
        if (which == 1) {
            field->setSuffix("\xC2\xB0");
        }
        field->setEnabled(authoring);
        auto row = std::make_shared<Row>();
        row->editor = field.get();
        std::weak_ptr<Row> weakRow = row;
        std::weak_ptr<prism::Vec3Field> weakField = field;
        const auto current = [this](Scene::TransformComponent& out) {
            const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
            const Scene::Entity selected = m_editor.GetSelectedEntity();
            if (!m_editor.IsDocumentEntity(selected) || !em.HasComponent<Scene::TransformComponent>(selected)) {
                return false;
            }
            out = em.GetComponent<Scene::TransformComponent>(selected);
            return true;
        };
        field->onBegin = [this, weakRow, mode = kRows[which].mode] {
            if (auto r = weakRow.lock()) {
                r->editing = true;
            }
            if (m_editor.GetToolSessionPhase() == EditorToolSessionPhase::Previewing) {
                static_cast<void>(m_editor.CommitTransformGesture());
            }
            static_cast<void>(m_editor.BeginTransformGesture(mode));
        };
        field->onChange = [this, current, which](const int axis, const float value) {
            Scene::TransformComponent t{};
            if (!current(t)) {
                return;
            }
            if (which == 0) {
                (axis == 0 ? t.localPosition.x : axis == 1 ? t.localPosition.y : t.localPosition.z) = value;
            } else if (which == 2) {
                float& edited = axis == 0 ? t.localScale.x : axis == 1 ? t.localScale.y : t.localScale.z;
                if (m_scaleLocked) {
                    // All three together: in proportion, or all equal when the edited one was zero.
                    const float before = edited;
                    if (std::abs(before) > 1e-6F) {
                        const float factor = value / before;
                        t.localScale = Math::Vector3{t.localScale.x * factor, t.localScale.y * factor, t.localScale.z * factor};
                    } else {
                        t.localScale = Math::Vector3{value, value, value};
                    }
                } else {
                    edited = value;
                }
            } else {
                // The authored Euler angles are the edited view of the rotation (see
                // TransformComponent::localEulerRadians); both are written together.
                Math::Vector3 e = ShownEuler(t);
                (axis == 0 ? e.x : axis == 1 ? e.y : e.z) = value * kDegToRad;
                Math::Quaternion q{};
                if (!Math::TryMakeEuler(e, q)) {
                    return;
                }
                t.localEulerRadians = e;
                t.localRotation = q;
            }
            if (m_editor.GetToolSessionPhase() != EditorToolSessionPhase::Previewing ||
                !m_editor.PreviewTransformGestureValue(t)) {
                static_cast<void>(m_editor.SetSelectedLocalTransform(t));
            }
        };
        field->onCommit = [this, weakRow] {
            if (m_editor.GetToolSessionPhase() == EditorToolSessionPhase::Previewing) {
                static_cast<void>(m_editor.CommitTransformGesture());
            }
            if (auto r = weakRow.lock()) {
                r->editing = false;
            }
        };
        // Reset: Location and Rotation to zero, Scale to one (one undo step).
        auto reset = prism::make<prism::Button>("", prism::Icon::Undo, prism::ButtonStyle::Flat);
        reset->setTooltip(which == 2 ? "Reset the scale to 1, 1, 1" : which == 1 ? "Reset the rotation to 0, 0, 0" : "Reset the location to 0, 0, 0");
        reset->setEnabled(authoring);
        reset->onClick = [this, current, which] {
            Scene::TransformComponent t{};
            if (!current(t)) {
                return;
            }
            if (which == 0) {
                t.localPosition = Math::Vector3{};
            } else if (which == 2) {
                t.localScale = Math::Vector3{1.0F, 1.0F, 1.0F};
            } else {
                t.localEulerRadians = Math::Vector3{};
                t.localRotation = Math::Quaternion{};
            }
            static_cast<void>(m_editor.SetSelectedLocalTransform(t));
        };
        // Key: shown only while the Animation Sequencer is open on this object; a filled diamond
        // when the clip already has a key at the playhead.
        auto key = prism::make<prism::Button>("", prism::Icon::Key, prism::ButtonStyle::Flat);
        key->setTooltip("Key this object's transform at the Sequencer's playhead");
        key->onClick = [this] { static_cast<void>(m_editor.KeyAnimationTimelineObject()); };
        key->setVisible(false);
        std::weak_ptr<prism::Button> weakKey = key;
        std::weak_ptr<prism::Button> weakReset = reset;
        auto line = prism::hbox(2.0F);
        line->add(field, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        line->add(key, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        // Beside the label: reset, and on Scale the lock (X, Y, Z together).
        auto labelTools = prism::hbox(2.0F);
        labelTools->add(reset, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        if (which == 2) {
            auto lock = prism::make<prism::Button>("", m_scaleLocked ? prism::Icon::Lock : prism::Icon::Unlock, prism::ButtonStyle::Flat);
            lock->setCheckable(true);
            lock->setChecked(m_scaleLocked);
            lock->setTooltip("Lock: X, Y and Z scale together. Off: one axis at a time");
            std::weak_ptr<prism::Button> weakLock = lock;
            lock->onToggle = [this, weakLock](const bool on) {
                m_scaleLocked = on;
                if (auto l = weakLock.lock()) {
                    l->setIcon(on ? prism::Icon::Lock : prism::Icon::Unlock);
                }
            };
            labelTools->add(lock, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
            m_scaleLockButton = lock;
        }
        row->sync = [this, current, weakField, weakKey, weakReset, which](const void*) {
            if (auto k = weakKey.lock()) {
                const bool keyable = m_editor.CanKeyAnimationTimelineObject(m_editor.GetSelectedEntity());
                if (k->visible() != keyable) {
                    k->setVisible(keyable);
                }
                // Only when it changes: a new icon lays the row out again.
                const bool keyed = keyable && m_editor.HasAnimationTimelineObjectKeyAtPlayhead();
                if (keyable && keyed != k->checked()) {
                    k->setCheckable(true);
                    k->setChecked(keyed);
                }
            }
            Scene::TransformComponent t{};
            auto f = weakField.lock();
            if (!f || !current(t)) {
                return;
            }
            Math::Vector3 v = which == 0 ? t.localPosition : which == 2 ? t.localScale : ShownEuler(t);
            // Reset shows once the value is not its default (0, or 1 for scale), Sequencer or not.
            if (auto r = weakReset.lock()) {
                const float d = which == 2 ? 1.0F : 0.0F;
                const bool changed = std::abs(v.x - d) > 1e-5F || std::abs(v.y - d) > 1e-5F || std::abs(v.z - d) > 1e-5F;
                if (r->visible() != changed) {
                    r->setVisible(changed);
                }
            }
            if (which == 1) {
                v = Math::Vector3{v.x * kRadToDeg, v.y * kRadToDeg, v.z * kRadToDeg};
            }
            if (std::abs(f->get(0) - v.x) > 1e-5F || std::abs(f->get(1) - v.y) > 1e-5F || std::abs(f->get(2) - v.z) > 1e-5F) {
                f->setValue(v.x, v.y, v.z);
            }
        };
        row->sync(nullptr);
        m_rows.push_back(row);
        m_grid->addRow(category, kRows[which].label, line, "", labelTools);
    }
}

void EditorDetails::AddProperty(const int category, const Entry& entry, const Property& property,
                                           const void* const instance, const bool writable) {
    // Rows some objects draw their own way.
    if (property.customDrawerId == "skeleton-source") {
        AddSkeletonSource(category, entry, property, instance, writable);
        return;
    }
    if (property.customDrawerId == "skeleton-bones") {
        AddSkeletonBones(category, instance);
        return;
    }
    if (property.customDrawerId == "script-exports") {
        AddScriptExports(category, writable);
        return;
    }
    if (property.customDrawerId == "object-metadata") {
        AddObjectMetadata(category, instance, writable);
        return;
    }
    if (entry.typeIndex == std::type_index(typeid(Scene::BoneAttachment3DComponent)) && property.name == "boneName") {
        AddBonePicker(category, entry, property, instance, writable);
        return;
    }
    const std::string label = property.displayName.empty() ? property.name : property.displayName;
    auto row = std::make_shared<Row>();
    row->entry = &entry;
    std::weak_ptr<Row> weakRow = row;
    const Entry* const e = &entry;
    const Property* const p = &property;
    prism::Ref<prism::Widget> editor;

    // Continuous edits (drags): previewed live, recorded as one undo step when the gesture ends.
    const auto begin = [weakRow] {
        if (auto r = weakRow.lock()) {
            r->editing = true;
        }
    };
    const auto commit = [this, e, p, weakRow] {
        static_cast<void>(m_editor.CommitComponentPropertyPreviewFor(*e, *p));
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
                static_cast<void>(m_editor.SetSelectedComponentProperty(*e, *p, &chosen));
            }
        };
        editor = combo;
    } else if (property.typeId == Scene::kPropertyTypeBool) {
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
        check->onChange = [this, e, p](const bool v) { static_cast<void>(m_editor.SetSelectedComponentProperty(*e, *p, &v)); };
        editor = check;
    } else if (property.typeId == Scene::kPropertyTypeFloat || property.typeId == Scene::kPropertyTypeInt32 ||
               property.typeId == Scene::kPropertyTypeUInt32 || property.typeId == Scene::kPropertyTypeUInt8) {
        const bool isFloat = property.typeId == Scene::kPropertyTypeFloat;
        auto number = prism::make<prism::NumberField>(0.0F, Step(property, isFloat ? 0.01F : 1.0F), isFloat ? 3 : 0);
        if (property.range.enabled) {
            number->setRange(static_cast<float>(property.range.minimum), static_cast<float>(property.range.maximum));
        } else if (!isFloat && property.typeId != Scene::kPropertyTypeInt32) {
            number->setRange(0.0F, property.typeId == Scene::kPropertyTypeUInt8 ? 255.0F : 4.0e9F);
        }
        std::weak_ptr<prism::NumberField> weak = number;
        const Strings::StringId type = property.typeId;
        const auto read = [p, type](const void* inst) -> float {
            if (type == Scene::kPropertyTypeFloat) { float v = 0; p->getValue(inst, &v); return v; }
            if (type == Scene::kPropertyTypeInt32) { std::int32_t v = 0; p->getValue(inst, &v); return static_cast<float>(v); }
            if (type == Scene::kPropertyTypeUInt32) { std::uint32_t v = 0; p->getValue(inst, &v); return static_cast<float>(v); }
            std::uint8_t v = 0; p->getValue(inst, &v); return static_cast<float>(v);
        };
        const auto write = [this, e, p, type](const float value, const bool preview) {
            const auto apply = [&](const void* v) {
                static_cast<void>(preview ? m_editor.PreviewSelectedComponentProperty(*e, *p, v)
                                          : m_editor.SetSelectedComponentProperty(*e, *p, v));
            };
            if (type == Scene::kPropertyTypeFloat) { const float v = value; apply(&v); }
            else if (type == Scene::kPropertyTypeInt32) { const auto v = static_cast<std::int32_t>(std::lround(value)); apply(&v); }
            else if (type == Scene::kPropertyTypeUInt32) { const auto v = static_cast<std::uint32_t>(std::max(0L, std::lround(value))); apply(&v); }
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
    } else if (property.typeId == Scene::kPropertyTypeVector3 || property.typeId == Scene::kPropertyTypeQuaternion) {
        const bool quaternion = property.typeId == Scene::kPropertyTypeQuaternion;
        auto field = prism::make<prism::Vec3Field>(Step(property, quaternion ? 0.5F : 0.01F), quaternion ? 2 : 3);
        if (quaternion) {
            field->setSuffix("\xC2\xB0");
        }
        std::weak_ptr<prism::Vec3Field> weak = field;
        const auto read = [p, quaternion](const void* inst) {
            if (!quaternion) {
                Math::Vector3 v{};
                p->getValue(inst, &v);
                return v;
            }
            Math::Quaternion q{};
            p->getValue(inst, &q);
            Math::Vector3 r{};
            static_cast<void>(Math::TryToEuler(q, r));
            return Math::Vector3{r.x * kRadToDeg, r.y * kRadToDeg, r.z * kRadToDeg};
        };
        row->sync = [read, weak](const void* inst) {
            auto f = weak.lock();
            if (f && inst != nullptr) {
                const Math::Vector3 v = read(inst);
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
            const Math::Vector3 v{f->get(0), f->get(1), f->get(2)};
            if (!quaternion) {
                static_cast<void>(m_editor.PreviewSelectedComponentProperty(*e, *p, &v));
                return;
            }
            Math::Quaternion q{};
            if (Math::TryMakeEuler(Math::Vector3{v.x * kDegToRad, v.y * kDegToRad, v.z * kDegToRad}, q)) {
                static_cast<void>(m_editor.PreviewSelectedComponentProperty(*e, *p, &q));
            }
        };
        field->onCommit = commit;
        editor = field;
    } else if (property.typeId == Scene::kPropertyTypeVector2) {
        auto field = prism::make<Vec2Field>(Step(property, 0.01F), 3);
        std::weak_ptr<Vec2Field> weak = field;
        row->sync = [p, weak](const void* inst) {
            auto f = weak.lock();
            if (f && inst != nullptr) {
                Math::Vector2 v{};
                p->getValue(inst, &v);
                if (std::abs(f->m_fields[0]->value() - v.x) > 1e-5F) { f->m_fields[0]->setValue(v.x); }
                if (std::abs(f->m_fields[1]->value() - v.y) > 1e-5F) { f->m_fields[1]->setValue(v.y); }
            }
        };
        for (auto& n : field->m_fields) {
            n->onBegin = begin;
            n->onChange = [this, e, p, weak](float) {
                if (auto f = weak.lock()) {
                    const Math::Vector2 v{f->m_fields[0]->value(), f->m_fields[1]->value()};
                    static_cast<void>(m_editor.PreviewSelectedComponentProperty(*e, *p, &v));
                }
            };
            n->onCommit = [commit](float) { commit(); };
        }
        editor = field;
    } else if (property.typeId == Scene::kPropertyTypeRect) {
        auto field = prism::make<RectField>();
        std::weak_ptr<RectField> weak = field;
        row->sync = [p, weak](const void* inst) {
            auto f = weak.lock();
            if (f && inst != nullptr) {
                Math::Rect v{};
                p->getValue(inst, &v);
                const std::array<float, 4> values{v.position.x, v.position.y, v.size.x, v.size.y};
                for (std::size_t i = 0; i < values.size(); ++i) {
                    if (std::abs(f->m_fields[i]->value() - values[i]) > 1e-4F) {
                        f->m_fields[i]->setValue(values[i]);
                    }
                }
            }
        };
        for (auto& n : field->m_fields) {
            n->onBegin = begin;
            n->onChange = [this, e, p, weak](float) {
                if (auto f = weak.lock()) {
                    const Math::Rect v = f->Value();
                    static_cast<void>(m_editor.PreviewSelectedComponentProperty(*e, *p, &v));
                }
            };
            n->onCommit = [commit](float) { commit(); };
        }
        editor = field;
    } else if (property.typeId == Scene::kPropertyTypeColor || property.typeId == Scene::kPropertyTypeLinearColor) {
        const bool linear = property.typeId == Scene::kPropertyTypeLinearColor;
        auto color = prism::make<prism::ColorField>();
        std::weak_ptr<prism::ColorField> weak = color;
        const auto read = [p, linear](const void* inst) {
            if (!linear) {
                Math::Vector3 v{};
                p->getValue(inst, &v);
                return ToPrismColor(v.x, v.y, v.z);
            }
            Math::Color c{};
            p->getValue(inst, &c);
            const Math::Vector3 shown = Math::DisplayFromColor(c);
            return ToPrismColor(shown.x, shown.y, shown.z);
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
            const Math::Vector3 picked = FromPrismColor(c);
            if (linear) {
                const Math::Color value = Math::ColorFromDisplay(picked);
                static_cast<void>(m_editor.PreviewSelectedComponentProperty(*e, *p, &value));
            } else {
                static_cast<void>(m_editor.PreviewSelectedComponentProperty(*e, *p, &picked));
            }
        };
        color->onCommit = [this, weakRow] {
            static_cast<void>(m_editor.CommitComponentPropertyPreview());
            if (auto r = weakRow.lock()) {
                r->editing = false;
            }
        };
        editor = color;
    } else if (property.typeId == Scene::kPropertyTypeString) {
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
        text->onCommit = [this, e, p](const std::string& v) { static_cast<void>(m_editor.SetSelectedComponentProperty(*e, *p, &v)); };
        editor = text;
        // A "file:ext,..." row names a file: the path stays editable, and the picker beside it offers
        // the project's files of those kinds, Load... and Import....
        constexpr std::string_view kFilePrefix = "file:";
        // The environment's sky is a slot too: Empty keeps the procedural sky, a panorama (a published
        // .uvtex or the source image) is sampled instead of it.
        const bool skySlot = property.customDrawerId == "sky:asset";
        // The object's Blueprint, a slot too: a .uvbp of the project, or Empty.
        const bool scriptSlot = property.customDrawerId == "script-slot";
        if (skySlot || scriptSlot || property.customDrawerId.rfind(kFilePrefix, 0U) == 0U) {
            std::vector<std::string> extensions;
            std::string list = skySlot      ? std::string{"uvtex,png,jpg,jpeg,bmp,tga"}
                               : scriptSlot ? std::string{"uvbp"}
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
            pick->setTooltip(property.tooltip.empty() ? std::string("Pick, load or import a file, or drop one from the Asset Table") : property.tooltip);
            std::weak_ptr<prism::Button> weakPick = pick;
            row->sync = [p, weakPick](const void* inst) {
                auto b = weakPick.lock();
                if (b && inst != nullptr) {
                    std::string v;
                    p->getValue(inst, &v);
                    const std::string text = v.empty() ? std::string("Empty") : std::filesystem::path{v}.stem().string();
                    // (A sky slot says what an empty one means: the procedural sky, or the Sky below.)
                    if (b->text() != text) {
                        b->setText(text);
                    }
                }
            };
            // Content-relative, as these rows have always stored it; a file outside Content keeps its full path.
            const std::function<void(const std::filesystem::path&)> useFile = [this, e, p, scriptSlot](const std::filesystem::path& absolute) {
                const std::filesystem::path root = m_editor.m_services->GetProjectFileIndex().GetSnapshot().contentRoot;
                std::error_code error;
                std::filesystem::path relative = std::filesystem::relative(absolute, root, error);
                if (error || relative.empty() || relative == "." || *relative.begin() == "..") {
                    error.clear();
                    const std::filesystem::path canonicalAbs = std::filesystem::weakly_canonical(absolute, error);
                    const std::filesystem::path canonicalRoot =
                        error ? std::filesystem::path{} : std::filesystem::weakly_canonical(root, error);
                    relative = error ? std::filesystem::path{} : std::filesystem::relative(canonicalAbs, canonicalRoot, error);
                }
                const std::string value = (relative.empty() || relative == "." || *relative.begin() == "..")
                                              ? absolute.generic_string()
                                              : relative.generic_string();
                if (scriptSlot) {
                    if (!m_editor.AssignScriptToSelectedEntity(value)) {
                        m_editor.m_contentStatusMessage = m_editor.DescribeScriptAssetProblem(value);
                    }
                    return;
                }
                static_cast<void>(m_editor.SetSelectedComponentProperty(*e, *p, &value));
            };
            pick->onClick = [this, e, p, weakPick, extensions, useFile, skySlot, scriptSlot] {
                PickerSpec spec = PickerSpecFor(extensions);
                if (skySlot) {
                    spec.title = "Sky";
                } else if (scriptSlot) {
                    spec.title = "Blueprint";
                }
                PickerActions actions;
                const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
                const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
                const Scene::Entity selected = m_editor.GetSelectedEntity();
                if (em.HasComponent(selected, e->typeIndex)) {
                    std::string value;
                    p->getValue(em.GetComponentPointer(selected, e->typeIndex), &value);
                    if (!value.empty()) {
                        const std::filesystem::path stored{value};
                        actions.current = stored.is_absolute() ? stored : snapshot.contentRoot / stored;
                    }
                }
                actions.empty = [this, e, p] {
                    const std::string none;
                    static_cast<void>(m_editor.SetSelectedComponentProperty(*e, *p, &none));
                };
                actions.file = useFile;
                if (skySlot) {
                    // The sky is authored on this environment: bands, atmosphere or a panorama.
                    const bool hasSky = em.HasComponent<Scene::Sky3DComponent>(selected);
                    actions.extras.emplace_back(hasSky ? "Edit Sky (below)" : "New Sky", [this, selected] {
                        static_cast<void>(m_editor.AddSkyToEntity(selected));
                    });
                    if (hasSky) {
                        actions.extras.emplace_back("Remove Sky", [this, selected] { static_cast<void>(m_editor.RemoveSkyFromEntity(selected)); });
                    }
                }
                if (auto b = weakPick.lock()) {
                    ShowPicker(spec, std::move(actions), b->rect());
                }
            };
            // The sky slot is the picker alone: its panorama is seen in the viewport, not in a box.
            const std::optional<AssetThumbnail::Kind> kind = skySlot ? std::nullopt : ThumbnailKindFor(extensions);
            if (kind == AssetThumbnail::Kind::Material && instance != nullptr) {
                std::string current;
                p->getValue(instance, &current);
                if (!current.empty()) {
                    const std::filesystem::path stored{current};
                    const std::filesystem::path resolved =
                        stored.is_absolute()
                            ? stored
                            : m_editor.m_services->GetProjectFileIndex().GetSnapshot().contentRoot / stored;
                    m_pendingMaterials.emplace_back(resolved, property.displayName.empty() ? property.name : property.displayName);
                }
            }
            if (kind.has_value()) {
                auto thumb = prism::make<AssetThumbnail>(m_editor, *kind);
                if (instance != nullptr) {
                    std::string current;
                    p->getValue(instance, &current);
                    if (!current.empty()) {
                        const std::filesystem::path stored{current};
                        thumb->setAsset(stored.is_absolute()
                                               ? stored
                                               : m_editor.m_services->GetProjectFileIndex().GetSnapshot().contentRoot / stored);
                    }
                }
                std::weak_ptr<AssetThumbnail> weakThumb = thumb;
                row->sync = [this, p, weakThumb, base = std::move(row->sync)](const void* inst) {
                    if (base) {
                        base(inst);
                    }
                    auto t = weakThumb.lock();
                    if (t && inst != nullptr) {
                        std::string v;
                        p->getValue(inst, &v);
                        const std::filesystem::path stored{v};
                        t->setAsset(v.empty() ? std::filesystem::path{}
                                                 : stored.is_absolute()
                                                       ? stored
                                                       : m_editor.m_services->GetProjectFileIndex().GetSnapshot().contentRoot / stored);
                    }
                };
                auto box = prism::hbox(6.0F);
                box->add(thumb, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Top});
                box->add(pick, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Top});
                editor = box;
            } else {
                editor = pick;
            }
            editor = WithAssetDrop([this] { return m_editor.m_services->GetProjectFileIndex().GetSnapshot().contentRoot; }, editor, PickerSpecFor(extensions).loadable, useFile);
        }
    } else if (property.typeId == Scene::kPropertyTypeBitMask32) {
        auto button = prism::make<prism::Button>("", prism::Icon::None, prism::ButtonStyle::Default);
        std::weak_ptr<prism::Button> weak = button;
        const bool physics = property.customDrawerId == Scene::kLayerMaskDrawerPhysics;
        const bool layers = physics || property.customDrawerId == Scene::kLayerMaskDrawerRender;
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
            const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
            const Scene::Entity selected = m_editor.GetSelectedEntity();
            if (!em.HasComponent(selected, e->typeIndex)) {
                return;
            }
            std::uint32_t mask = 0U;
            p->getValue(em.GetComponentPointer(selected, e->typeIndex), &mask);
            std::vector<prism::MenuItem> items;
            const Config::SettingsDocument& project = m_editor.m_services->GetProjectSettings();
            for (std::size_t bit = 0; bit < 32U; ++bit) {
                std::string name = layers ? Core::GetLayerName(project, physics ? Core::LayerSet::Physics : Core::LayerSet::Render, bit)
                                          : std::string{};
                if (name.empty()) {
                    name = "Layer " + std::to_string(bit + 1U);
                }
                items.push_back(prism::MenuItem::Check(name, ((mask >> bit) & 1U) != 0U, [this, e, p, mask, bit] {
                    const std::uint32_t next = mask ^ (1U << bit);
                    static_cast<void>(m_editor.SetSelectedComponentProperty(*e, *p, &next));
                }));
            }
            if (auto b = weak.lock()) {
                prism::showMenu(std::move(items), b->rect(), prism::PopupPlacement::Below);
            }
        };
        editor = button;
    } else if (property.typeId == Scene::kPropertyTypeAssetGuid) {
        // Picking, never typing (as the Inspector): the registered assets of the kind the property
        // declares - a material slot takes materials, "asset:<ext>" that extension.
        const auto lower = [](std::string v) { return Lower(std::move(v)); };
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
            Asset::AssetGuid value{};
            p->getValue(inst, &value);
            std::string text = "Empty";
            if (value != Asset::AssetGuid{}) {
                text = "Missing asset";
                for (const Asset::ProjectFileEntry& file : m_editor.m_services->GetProjectFileIndex().GetSnapshot().entries) {
                    if (file.registeredAssetGuid.has_value() && *file.registeredAssetGuid == value) {
                        text = file.relativePath.stem().string();
                        break;
                    }
                }
                // Registered since the file index's last scan (a clip just added): the database knows it.
                if (text == "Missing asset") {
                    if (const std::filesystem::path path = m_editor.m_services->GetAssetDatabase().Resolve(value); !path.empty()) {
                        text = Asset::AssetDisplayName(path);
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
            Asset::AssetGuid current{};
            p->getValue(instance, &current);
            if (current != Asset::AssetGuid{}) {
                const std::filesystem::path path = m_editor.m_services->GetAssetDatabase().Resolve(current);
                if (!path.empty()) {
                    m_pendingMaterials.emplace_back(path, property.displayName.empty() ? property.name : property.displayName);
                }
            }
        }
        // A MeshInstance3D's mesh: a model, or one of the built-in shapes, or nothing.
        const bool meshSource = e->typeIndex == std::type_index(typeid(Scene::MeshComponent)) && property.name == "meshGuid";
        if (meshSource) {
            row->sync = [this, p, weak](const void* inst) {
                auto b = weak.lock();
                if (!b || inst == nullptr) {
                    return;
                }
                Asset::AssetGuid value{};
                p->getValue(inst, &value);
                const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
                const Scene::Entity selected = m_editor.GetSelectedEntity();
                std::string text = "Empty";
                if (value != Asset::AssetGuid{}) {
                    const std::filesystem::path path = PathOfGuid(value);
                    text = path.empty() ? std::string("Missing model") : path.stem().string();
                } else if (em.HasComponent<Scene::PrimitiveMeshComponent>(selected)) {
                    static constexpr const char* kNames[] = {"Box", "Sphere", "Plane", "Capsule", "Cylinder"};
                    const auto kind = static_cast<std::size_t>(em.GetComponent<Scene::PrimitiveMeshComponent>(selected).kind);
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
                const Asset::AssetGuid guid = PublishPicked(absolute);
                if (guid == Asset::AssetGuid{}) {
                    m_editor.m_contentStatusMessage = "Could not read " + absolute.filename().string() + " as a model.";
                    return;
                }
                static_cast<void>(m_editor.SetSelectedMeshSource(guid, std::nullopt));
            };
        } else {
            useFile = [this, e, p](const std::filesystem::path& absolute) {
                const Asset::AssetGuid guid = PublishPicked(absolute);
                if (guid == Asset::AssetGuid{}) {
                    m_editor.m_contentStatusMessage = "Could not use " + absolute.filename().string() + ".";
                    return;
                }
                static_cast<void>(m_editor.SetSelectedComponentProperty(*e, *p, &guid));
            };
        }
        button->onClick = [this, e, p, weak, extensions, meshSource, useFile] {
            PickerSpec spec = PickerSpecFor(extensions);
            spec.shapes = meshSource;
            PickerActions actions;
            const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
            const Scene::Entity selected = m_editor.GetSelectedEntity();
            if (em.HasComponent(selected, e->typeIndex)) {
                Asset::AssetGuid current{};
                p->getValue(em.GetComponentPointer(selected, e->typeIndex), &current);
                actions.current = PathOfGuid(current);
            }
            if (meshSource) {
                if (em.HasComponent<Scene::PrimitiveMeshComponent>(selected)) {
                    actions.currentShape = em.GetComponent<Scene::PrimitiveMeshComponent>(selected).kind;
                }
                actions.empty = [this] { static_cast<void>(m_editor.SetSelectedMeshSource(Asset::AssetGuid{}, std::nullopt)); };
                actions.file = useFile;
                actions.shape = [this](const Scene::PrimitiveMeshKind kind) {
                    static_cast<void>(m_editor.SetSelectedMeshSource(Asset::AssetGuid{}, kind));
                };
            } else {
                actions.empty = [this, e, p] {
                    const Asset::AssetGuid none{};
                    static_cast<void>(m_editor.SetSelectedComponentProperty(*e, *p, &none));
                };
                actions.file = useFile;
            }
            if (auto b = weak.lock()) {
                ShowPicker(spec, std::move(actions), b->rect());
            }
        };
        // The standard slot layout: the asset's own preview beside its picker.
        if (const std::optional<AssetThumbnail::Kind> kind = ThumbnailKindFor(extensions); kind.has_value()) {
            auto thumb = prism::make<AssetThumbnail>(m_editor, *kind);
            if (instance != nullptr) {
                Asset::AssetGuid value{};
                p->getValue(instance, &value);
                thumb->setAsset(PathOfGuid(value));
            }
            std::weak_ptr<AssetThumbnail> weakThumb = thumb;
            row->sync = [this, p, weakThumb, base = std::move(row->sync)](const void* inst) {
                if (base) {
                    base(inst);
                }
                auto t = weakThumb.lock();
                if (t && inst != nullptr) {
                    Asset::AssetGuid value{};
                    p->getValue(inst, &value);
                    t->setAsset(PathOfGuid(value));
                }
            };
            auto box = prism::hbox(6.0F);
            box->add(thumb, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Top});
            box->add(button, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Top});
            editor = box;
        } else {
            editor = button;
        }
        editor = WithAssetDrop([this] { return m_editor.m_services->GetProjectFileIndex().GetSnapshot().contentRoot; }, editor, PickerSpecFor(extensions).loadable, useFile);
    } else if (property.typeId == Scene::kPropertyTypeEntity) {
        auto button = prism::make<prism::Button>("Default", prism::Icon::None, prism::ButtonStyle::Default);
        std::weak_ptr<prism::Button> weak = button;
        row->sync = [this, p, weak](const void* inst) {
            auto b = weak.lock();
            if (!b || inst == nullptr) {
                return;
            }
            Scene::Entity value = Scene::kInvalidEntity;
            p->getValue(inst, &value);
            const std::string text = value == Scene::kInvalidEntity        ? std::string{"Default"}
                                     : !m_editor.IsDocumentEntity(value) ? std::string{"Missing object"}
                                                                            : m_editor.GetEntityDisplayLabel(value);
            if (b->text() != text) {
                b->setText(text);
            }
        };
        button->onClick = [this, e, p, weak] {
            std::vector<prism::MenuItem> items;
            items.push_back(prism::MenuItem::Item("Default", [this, e, p] {
                const Scene::Entity none = Scene::kInvalidEntity;
                static_cast<void>(m_editor.SetSelectedComponentProperty(*e, *p, &none));
            }));
            items.push_back(prism::MenuItem::Sep());
            for (const Scene::Entity candidate : m_editor.GetEntityReferenceCandidates()) {
                items.push_back(prism::MenuItem::Item(m_editor.GetEntityDisplayLabel(candidate), [this, e, p, candidate] {
                    static_cast<void>(m_editor.SetSelectedComponentProperty(*e, *p, &candidate));
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
        auto value = prism::make<prism::Label>(std::string(property.typeId.ToString()), prism::FontRole::Small);
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

void EditorDetails::AddScriptExports(const int category, const bool writable) {
    // One row per `export` field of the object's script, typed; a changed value is marked, and its
    // tooltip has the script's default (the reset button puts it back).
    for (const Editor::ScriptExportRow& row : m_editor.GetSelectedScriptExports()) {
        const std::optional<Blueprint::Value> value = Blueprint::ParseValueText(row.valueText, row.type);
        if (!value.has_value()) {
            continue;
        }
        const std::string name = row.name;
        const auto set = [this, name](std::string text) { static_cast<void>(m_editor.SetSelectedScriptExport(name, std::move(text))); };
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
            number->onCommit = [set](const float v) { set(Blueprint::FormatValue(Blueprint::Value{static_cast<double>(v)})); };
            editor = number;
        } else if (const auto* const vector = std::get_if<Blueprint::Vec3Value>(&*value)) {
            auto parts = std::make_shared<std::array<double, 3>>(std::array<double, 3>{vector->x, vector->y, vector->z});
            auto box = prism::hbox(4.0F);
            for (std::size_t axis = 0U; axis < 3U; ++axis) {
                auto number = prism::make<prism::NumberField>(static_cast<float>((*parts)[axis]), 0.05F, 3);
                number->onCommit = [set, parts, axis](const float v) {
                    (*parts)[axis] = static_cast<double>(v);
                    set(Blueprint::FormatValue(Blueprint::Value{Blueprint::Vec3Value{(*parts)[0], (*parts)[1], (*parts)[2]}}));
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
            reset->onClick = [this, name] { static_cast<void>(m_editor.SetSelectedScriptExport(name, std::nullopt)); };
            line->add(reset, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        }
        m_grid->addRow(category, row.name, line, row.type.Name() + ", default " + row.defaultText);
    }
}

// ---------------------------------------------------------------------------------- object metadata

void EditorDetails::AddObjectMetadata(const int category, const void* const instance, const bool writable) {
    if (instance == nullptr) {
        return;
    }
    using Type = Core::VariantType;
    const auto& metadata = *static_cast<const Scene::ObjectMetadataComponent*>(instance);
    for (const Scene::ObjectMetadataEntry& item : metadata.entries) {
        const std::string key = item.key;
        const Core::Variant& v = item.value;
        const auto setValue = [this, key](const Core::Variant& value) {
            static_cast<void>(m_editor.SetSelectedObjectMetadataValue(key, value));
        };
        auto name = prism::make<prism::TextInput>(key, "");
        name->onCommit = [this, key](const std::string& to) {
            if (!to.empty() && to != key) {
                static_cast<void>(m_editor.RenameSelectedObjectMetadata(key, to));
            }
        };
        prism::Ref<prism::Widget> value;
        switch (v.GetType()) {
            case Type::Bool: {
                auto check = prism::make<prism::CheckBox>("", v.TryGet<bool>() != nullptr && *v.TryGet<bool>());
                check->onChange = [setValue](const bool b) { setValue(Core::Variant::MakeBool(b)); };
                value = check;
                break;
            }
            case Type::Int: {
                const std::int64_t* const n = v.TryGet<std::int64_t>();
                auto number = prism::make<prism::NumberField>(n != nullptr ? static_cast<float>(*n) : 0.0F, 1.0F, 0);
                number->onCommit = [setValue](const float f) { setValue(Core::Variant::MakeInt(std::llround(f))); };
                value = number;
                break;
            }
            case Type::Float: {
                const double* const n = v.TryGet<double>();
                auto number = prism::make<prism::NumberField>(n != nullptr ? static_cast<float>(*n) : 0.0F, 0.05F, 3);
                number->onCommit = [setValue](const float f) { setValue(Core::Variant::MakeFloat(static_cast<double>(f))); };
                value = number;
                break;
            }
            case Type::String:
            case Type::InternedString:
            case Type::ObjectPath: {
                const std::string* const s = v.TryGet<std::string>();
                auto input = prism::make<prism::TextInput>(s != nullptr ? *s : std::string{}, "");
                const Type type = v.GetType();
                input->onCommit = [setValue, type](const std::string& t) { setValue(Core::Variant::MakeText(type, t)); };
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
        remove->onClick = [this, key] { static_cast<void>(m_editor.RemoveSelectedObjectMetadata(key)); };
        auto line = prism::hbox(4.0F);
        line->add(name, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        line->add(value, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        line->add(remove, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        static constexpr std::array<const char*, 4> kTypes{"Bool", "Int", "Float", "String"};
        const auto typeIndex = static_cast<std::size_t>(v.GetType());
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
                    if (m_editor.AddSelectedObjectMetadata(key, Core::Variant::MakeDefault(type))) {
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
