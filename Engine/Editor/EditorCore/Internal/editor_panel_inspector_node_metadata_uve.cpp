// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The node's Metadata in the Inspector: one row per typed entry, and "Add Metadata", which opens a
// small floating form - name, type, starting value - rather than adding an unnamed placeholder the
// author then has to fix up.
//
// Every change, whatever it is, rewrites the entry list once through the metadata component's
// declared property. That single path is what makes each edit one undo step, and what keeps the
// rules (identifier keys, bounded values) in one place.

#include "uve/editor/editor_uve.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

#include <imgui.h>

#include "editor_axis_input_uve.h"
#include "editor_color_field_uve.h"

#include "uve/component/node_metadata_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Editor {
namespace {

using Core::VariantTypeUVE;
using Core::VariantUVE;

struct MetadataPropertyUVE final {
    const Core::TypeMetadataEntryUVE* entry = nullptr;
    const Core::TypeMetadataPropertyUVE* property = nullptr;
};

[[nodiscard]] MetadataPropertyUVE FindMetadataPropertyUVE() {
    const Core::TypeMetadataEntryUVE* const entry =
        Scene::FindSceneComponentMetadataUVE(std::type_index(typeid(Scene::NodeMetadataComponentUVE)));
    if (entry == nullptr || entry->properties.empty()) {
        return {};
    }
    return {entry, &entry->properties.front()};
}

[[nodiscard]] std::string TypeLabelUVE(const VariantTypeUVE type) {
    return std::string{Core::GetVariantTypeNameUVE(type)};
}

[[nodiscard]] bool ContainsFoldedUVE(const std::string_view text, const std::string& query) {
    return query.empty() ||
           std::search(text.cbegin(), text.cend(), query.cbegin(), query.cend(), [](const char left, const char right) {
               return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
           }) != text.cend();
}

/// Text editing for a Variant string, committed per edit: metadata values are short, and each
/// commit is one undo entry.
bool EditTextUVE(const char* const id, std::string& text) {
    std::array<char, 512> buffer{};
    text.copy(buffer.data(), std::min(text.size(), buffer.size() - 1U));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText(id, buffer.data(), buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
        text = buffer.data();
        return true;
    }
    return false;
}

/// Draws a list editor: each element through `drawElement`, a remove button per element, and an
/// add button appending `makeElement()`.
template <typename ElementT, typename DrawT, typename MakeT>
bool EditListUVE(std::vector<ElementT>& items, DrawT&& drawElement, MakeT&& makeElement) {
    bool changed = false;
    ImGui::TextDisabled("%zu item(s)", items.size());
    for (std::size_t index = 0; index < items.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::SmallButton("x")) {
            items.erase(items.begin() + static_cast<std::ptrdiff_t>(index));
            changed = true;
            ImGui::PopID();
            break;
        }
        ImGui::SameLine();
        changed = drawElement(items[index]) || changed;
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+ Add item") && items.size() < Core::kMaximumVariantElementsUVE) {
        items.push_back(makeElement());
        changed = true;
    }
    return changed;
}

} // namespace

bool EditorUVE::DrawVariantValueEditorUVE(const char* const id, VariantUVE& value, const int depth) {
    ImGui::PushID(id);
    bool changed = false;
    // Metadata is committed as a whole entry list, so a colour changes it only once the picker
    // closes with the colour kept; while it is open the field shows its own working copy.
    const auto EditVariantColorUVE = [this](const char* fieldId, Core::VariantColorUVE& stored) {
        EditorColorUVE color{stored.r, stored.g, stored.b, stored.a};
        if (DrawColorFieldUVE(fieldId, "Color", color, true, m_colorPickerPreferences) !=
            ColorFieldEventUVE::Committed) {
            return false;
        }
        stored = Core::VariantColorUVE{color.r, color.g, color.b, color.a};
        return true;
    };
    const auto dragFloats = [](float* values, const int count) {
        return DrawAxisVectorInputUVE("##v", values, count, 0.01F);
    };
    value.VisitMutableUVE([&](auto& stored) {
        using T = std::decay_t<decltype(stored)>;
        if constexpr (std::is_same_v<T, bool>) {
            changed = ImGui::Checkbox("##v", &stored);
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed = ImGui::DragScalar("##v", ImGuiDataType_S64, &stored, 1.0F);
        } else if constexpr (std::is_same_v<T, double>) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed = ImGui::DragScalar("##v", ImGuiDataType_Double, &stored, 0.01F);
        } else if constexpr (std::is_same_v<T, std::string>) {
            changed = EditTextUVE("##v", stored);
        } else if constexpr (std::is_same_v<T, Math::Vector2UVE> || std::is_same_v<T, Math::Vector3UVE> ||
                             std::is_same_v<T, Core::VariantVector4UVE> || std::is_same_v<T, Math::QuaternionUVE>) {
            changed = dragFloats(&stored.x, static_cast<int>(sizeof(T) / sizeof(float)));
        } else if constexpr (std::is_same_v<T, Core::VariantColorUVE>) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed = EditVariantColorUVE("##v", stored);
        } else if constexpr (std::is_same_v<T, std::uint64_t>) {
            // A resource reference is picked, not typed; shown until a picker exists.
            ImGui::TextDisabled(stored == 0U ? "(none)" : "%016llX", static_cast<unsigned long long>(stored));
        } else if constexpr (std::is_same_v<T, std::vector<VariantUVE>>) {
            if (depth >= 4) {
                ImGui::TextDisabled("%zu item(s)", stored.size());
            } else {
                changed = EditListUVE(
                    stored, [&](VariantUVE& element) { return DrawVariantValueEditorUVE("##e", element, depth + 1); },
                    [] { return VariantUVE::MakeIntUVE(0); });
            }
        } else if constexpr (std::is_same_v<T, std::vector<Core::VariantDictionaryEntryUVE>>) {
            if (depth >= 4) {
                ImGui::TextDisabled("%zu pair(s)", stored.size());
            } else {
                changed = EditListUVE(
                    stored,
                    [&](Core::VariantDictionaryEntryUVE& pair) {
                        const bool keyChanged = EditTextUVE("##k", pair.key);
                        return DrawVariantValueEditorUVE("##e", pair.value, depth + 1) || keyChanged;
                    },
                    [&stored] {
                        return Core::VariantDictionaryEntryUVE{"key" + std::to_string(stored.size()),
                                                               VariantUVE::MakeIntUVE(0)};
                    });
            }
        } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
            changed = EditListUVE(stored, [](std::string& element) { return EditTextUVE("##e", element); },
                                  [] { return std::string{}; });
        } else if constexpr (std::is_same_v<T, std::vector<Math::Vector2UVE>> ||
                             std::is_same_v<T, std::vector<Math::Vector3UVE>>) {
            using E = typename T::value_type;
            changed = EditListUVE(
                stored, [&](E& element) { return dragFloats(&element.x, static_cast<int>(sizeof(E) / sizeof(float))); },
                [] { return E{}; });
        } else if constexpr (std::is_same_v<T, std::vector<Core::VariantColorUVE>>) {
            changed = EditListUVE(
                stored,
                [&](Core::VariantColorUVE& element) {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    return EditVariantColorUVE("##e", element);
                },
                [] { return Core::VariantColorUVE{}; });
        } else {
            // The numeric packed arrays: one scalar editor per element, in the array's own width.
            using E = typename T::value_type;
            constexpr ImGuiDataType kType = std::is_same_v<E, std::uint8_t>   ? ImGuiDataType_U8
                                            : std::is_same_v<E, std::int32_t> ? ImGuiDataType_S32
                                            : std::is_same_v<E, std::int64_t> ? ImGuiDataType_S64
                                            : std::is_same_v<E, float>        ? ImGuiDataType_Float
                                                                              : ImGuiDataType_Double;
            changed = EditListUVE(
                stored,
                [](E& element) {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    return ImGui::DragScalar("##e", kType, &element, 1.0F);
                },
                [] { return E{}; });
        }
    });
    ImGui::PopID();
    return changed;
}

bool EditorUVE::CommitSelectedNodeMetadataUVE(std::vector<Scene::NodeMetadataEntryUVE> entries, const bool preview) {
    Scene::NodeMetadataComponentUVE candidate{std::move(entries)};
    if (!Scene::IsNodeMetadataComponentValidUVE(candidate)) {
        return false;
    }
    const MetadataPropertyUVE target = FindMetadataPropertyUVE();
    if (target.entry == nullptr) {
        return false;
    }
    return preview ? PreviewSelectedComponentPropertyUVE(*target.entry, *target.property, &candidate.entries)
                   : SetSelectedComponentPropertyUVE(*target.entry, *target.property, &candidate.entries);
}

namespace {

[[nodiscard]] const Scene::NodeMetadataComponentUVE* SelectedMetadataUVE(Scene::IEntityManagerUVE& entityManager,
                                                                         const Scene::EntityUVE entity) {
    return entityManager.HasComponentUVE<Scene::NodeMetadataComponentUVE>(entity)
               ? &entityManager.GetComponentUVE<Scene::NodeMetadataComponentUVE>(entity)
               : nullptr;
}

} // namespace

bool EditorUVE::AddSelectedNodeMetadataUVE(const std::string& key, const VariantUVE& value) {
    if (!HasSingleDocumentSelectionUVE()) {
        return false;
    }
    const Scene::NodeMetadataComponentUVE* const metadata =
        SelectedMetadataUVE(m_services->GetEntityManagerUVE(), m_selectedEntity);
    if (metadata == nullptr || Scene::ValidateNodeMetadataKeyUVE(key, *metadata) != Scene::NodeMetadataKeyIssueUVE::None) {
        return false;
    }
    std::vector<Scene::NodeMetadataEntryUVE> entries = metadata->entries;
    entries.push_back({key, value});
    return CommitSelectedNodeMetadataUVE(std::move(entries));
}

bool EditorUVE::SetSelectedNodeMetadataValueUVE(const std::string& key, const VariantUVE& value) {
    return WriteSelectedNodeMetadataValueUVE(key, value, false);
}

bool EditorUVE::PreviewSelectedNodeMetadataValueUVE(const std::string& key, const VariantUVE& value) {
    return WriteSelectedNodeMetadataValueUVE(key, value, true);
}

bool EditorUVE::WriteSelectedNodeMetadataValueUVE(const std::string& key, const VariantUVE& value,
                                                  const bool preview) {
    if (!HasSingleDocumentSelectionUVE()) {
        return false;
    }
    const Scene::NodeMetadataComponentUVE* const metadata =
        SelectedMetadataUVE(m_services->GetEntityManagerUVE(), m_selectedEntity);
    if (metadata == nullptr) {
        return false;
    }
    std::vector<Scene::NodeMetadataEntryUVE> entries = metadata->entries;
    const auto found = std::find_if(entries.begin(), entries.end(), [&key](const auto& entry) { return entry.key == key; });
    if (found == entries.end()) {
        return false;
    }
    found->value = value;
    return CommitSelectedNodeMetadataUVE(std::move(entries), preview);
}

bool EditorUVE::RenameSelectedNodeMetadataUVE(const std::string& key, const std::string& newKey) {
    if (!HasSingleDocumentSelectionUVE()) {
        return false;
    }
    const Scene::NodeMetadataComponentUVE* const metadata =
        SelectedMetadataUVE(m_services->GetEntityManagerUVE(), m_selectedEntity);
    if (metadata == nullptr ||
        Scene::ValidateNodeMetadataKeyUVE(newKey, *metadata, key) != Scene::NodeMetadataKeyIssueUVE::None) {
        return false;
    }
    std::vector<Scene::NodeMetadataEntryUVE> entries = metadata->entries;
    const auto found = std::find_if(entries.begin(), entries.end(), [&key](const auto& entry) { return entry.key == key; });
    if (found == entries.end()) {
        return false;
    }
    found->key = newKey;
    return CommitSelectedNodeMetadataUVE(std::move(entries));
}

bool EditorUVE::ChangeSelectedNodeMetadataTypeUVE(const std::string& key, const VariantTypeUVE type,
                                                  const bool allowLoss) {
    if (!HasSingleDocumentSelectionUVE()) {
        return false;
    }
    const Scene::NodeMetadataComponentUVE* const metadata =
        SelectedMetadataUVE(m_services->GetEntityManagerUVE(), m_selectedEntity);
    if (metadata == nullptr) {
        return false;
    }
    std::vector<Scene::NodeMetadataEntryUVE> entries = metadata->entries;
    const auto found = std::find_if(entries.begin(), entries.end(), [&key](const auto& entry) { return entry.key == key; });
    if (found == entries.end()) {
        return false;
    }
    const std::optional<Core::VariantConversionUVE> converted = Core::TryConvertVariantUVE(found->value, type);
    if (!converted.has_value() || (!converted->lossless && !allowLoss)) {
        return false;
    }
    found->value = converted->value;
    return CommitSelectedNodeMetadataUVE(std::move(entries));
}

bool EditorUVE::RemoveSelectedNodeMetadataUVE(const std::string& key) {
    if (!HasSingleDocumentSelectionUVE()) {
        return false;
    }
    const Scene::NodeMetadataComponentUVE* const metadata =
        SelectedMetadataUVE(m_services->GetEntityManagerUVE(), m_selectedEntity);
    if (metadata == nullptr) {
        return false;
    }
    std::vector<Scene::NodeMetadataEntryUVE> entries = metadata->entries;
    const auto removed = std::remove_if(entries.begin(), entries.end(), [&key](const auto& entry) { return entry.key == key; });
    if (removed == entries.end()) {
        return false;
    }
    entries.erase(removed, entries.end());
    return CommitSelectedNodeMetadataUVE(std::move(entries));
}

void EditorUVE::DrawNodeMetadataPropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                            const Core::TypeMetadataPropertyUVE& property, const void* const instance) {
    static_cast<void>(entry);
    static_cast<void>(property);
    const auto& metadata = *static_cast<const Scene::NodeMetadataComponentUVE*>(instance);
    const bool writable = IsAuthoringCommandAllowedUVE();

    // One row per entry: the key as the label, the typed value beside it. Right-click a key to
    // rename it, change its type or remove it.
    if (!metadata.entries.empty() &&
        ImGui::BeginTable("##metadata", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, 0.42F);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 0.58F);
        for (const Scene::NodeMetadataEntryUVE& item : metadata.entries) {
            ImGui::PushID(item.key.c_str());
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(item.key.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s. Right-click to rename, change type or remove.",
                                  TypeLabelUVE(item.value.GetTypeUVE()).c_str());
            }
            if (writable && ImGui::BeginPopupContextItem("##row-menu")) {
                if (ImGui::MenuItem("Rename...")) {
                    m_metadataRenameKey = item.key;
                    m_metadataRenameDraft = item.key;
                }
                if (ImGui::BeginMenu("Change Type")) {
                    for (const VariantTypeUVE type : Core::GetAllVariantTypesUVE()) {
                        const std::optional<Core::VariantConversionUVE> preview =
                            Core::TryConvertVariantUVE(item.value, type);
                        std::string label = TypeLabelUVE(type);
                        if (preview.has_value() && !preview->lossless) {
                            label += "  (loses data)";
                        }
                        if (ImGui::MenuItem(label.c_str(), nullptr, type == item.value.GetTypeUVE(),
                                            preview.has_value() && type != item.value.GetTypeUVE())) {
                            if (!ChangeSelectedNodeMetadataTypeUVE(item.key, type, false)) {
                                m_metadataPendingRetype = std::make_pair(item.key, type); // Needs a yes.
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Remove")) {
                    static_cast<void>(RemoveSelectedNodeMetadataUVE(item.key));
                }
                ImGui::EndPopup();
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginDisabled(!writable);
            VariantUVE edited = item.value;
            // One group, so a drag anywhere in the value - a number, one axis of a vector, an
            // element of a list - is one edit, recorded as one undo step when it is let go.
            ImGui::BeginGroup();
            const bool changed = DrawVariantValueEditorUVE("##value", edited, 0);
            ImGui::EndGroup();
            if (changed) {
                static_cast<void>(ImGui::IsItemActive() ? PreviewSelectedNodeMetadataValueUVE(item.key, edited)
                                                        : SetSelectedNodeMetadataValueUVE(item.key, edited));
            }
            if (ImGui::IsItemDeactivated()) {
                const MetadataPropertyUVE target = FindMetadataPropertyUVE();
                if (target.entry != nullptr) {
                    static_cast<void>(CommitComponentPropertyPreviewForUVE(*target.entry, *target.property));
                }
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Rename: inline, validated as it is typed.
    if (!m_metadataRenameKey.empty()) {
        ImGui::OpenPopup("Rename Metadata##metadata-rename");
    }
    if (ImGui::BeginPopupModal("Rename Metadata##metadata-rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        std::array<char, Scene::kMaximumNodeMetadataKeyBytesUVE + 1U> buffer{};
        m_metadataRenameDraft.copy(buffer.data(), buffer.size() - 1U);
        ImGui::SetNextItemWidth(280.0F);
        const bool submitted =
            ImGui::InputText("##rename", buffer.data(), buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        m_metadataRenameDraft = buffer.data();
        const Scene::NodeMetadataKeyIssueUVE issue =
            Scene::ValidateNodeMetadataKeyUVE(m_metadataRenameDraft, metadata, m_metadataRenameKey);
        if (issue != Scene::NodeMetadataKeyIssueUVE::None) {
            ImGui::TextColored(ImVec4{0.95F, 0.62F, 0.35F, 1.0F}, "%s",
                               std::string{Scene::DescribeNodeMetadataKeyIssueUVE(issue)}.c_str());
        }
        const bool cancel = ImGui::Button("Cancel", ImVec2{120.0F, 0.0F}) || ImGui::IsKeyPressed(ImGuiKey_Escape);
        ImGui::SameLine();
        ImGui::BeginDisabled(issue != Scene::NodeMetadataKeyIssueUVE::None);
        const bool rename = ImGui::Button("Rename", ImVec2{120.0F, 0.0F}) ||
                            (submitted && issue == Scene::NodeMetadataKeyIssueUVE::None);
        ImGui::EndDisabled();
        if ((rename && (m_metadataRenameDraft == m_metadataRenameKey ||
                        RenameSelectedNodeMetadataUVE(m_metadataRenameKey, m_metadataRenameDraft))) ||
            cancel) {
            m_metadataRenameKey.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // A retype that would lose data is never silent.
    if (m_metadataPendingRetype.has_value()) {
        ImGui::OpenPopup("Change Type?##metadata-retype");
    }
    if (ImGui::BeginPopupModal("Change Type?##metadata-retype", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (m_metadataPendingRetype.has_value()) {
            ImGui::Text("Converting \"%s\" to %s will lose data.", m_metadataPendingRetype->first.c_str(),
                        TypeLabelUVE(m_metadataPendingRetype->second).c_str());
            ImGui::TextDisabled("Undo restores the original value.");
        }
        const bool cancel = ImGui::Button("Cancel", ImVec2{120.0F, 0.0F}) || ImGui::IsKeyPressed(ImGuiKey_Escape);
        ImGui::SameLine();
        if (ImGui::Button("Convert", ImVec2{120.0F, 0.0F}) && m_metadataPendingRetype.has_value()) {
            static_cast<void>(ChangeSelectedNodeMetadataTypeUVE(m_metadataPendingRetype->first,
                                                                m_metadataPendingRetype->second, true));
            m_metadataPendingRetype.reset();
            ImGui::CloseCurrentPopup();
        } else if (cancel) {
            m_metadataPendingRetype.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::BeginDisabled(!writable || metadata.entries.size() >= Scene::kMaximumNodeMetadataEntriesUVE);
    if (ImGui::Button("Add Metadata", ImVec2{-FLT_MIN, 0.0F})) {
        m_metadataAddName.clear();
        m_metadataTypeFilter.clear();
        m_metadataAddValue = VariantUVE::MakeDefaultUVE(m_metadataAddType);
        ImGui::OpenPopup("##metadata-add");
    }
    ImGui::EndDisabled();

    // The floating form: titled with the node it adds to, so there is never a doubt which one.
    ImGui::SetNextWindowSize(ImVec2{380.0F, 0.0F}, ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##metadata-add")) {
        const std::string title = "Add Metadata Property \"" + GetEntityDisplayLabelUVE(m_selectedEntity) + "\"";
        ImGui::TextUnformatted(title.c_str());
        ImGui::Separator();
        if (!m_metadataAddValue.has_value()) {
            m_metadataAddValue = VariantUVE::MakeDefaultUVE(m_metadataAddType);
        }

        if (!ImGui::BeginTable("##add-form", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::EndPopup();
            return;
        }
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, 0.28F);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 0.72F);
        // Name, checked as it is typed so the reason a name is refused is visible before Add.
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Name");
        ImGui::TableSetColumnIndex(1);
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        std::array<char, Scene::kMaximumNodeMetadataKeyBytesUVE + 1U> name{};
        m_metadataAddName.copy(name.data(), name.size() - 1U);
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool submitted = ImGui::InputTextWithHint("##name", "e.g. door_key", name.data(), name.size(),
                                                        ImGuiInputTextFlags_EnterReturnsTrue);
        m_metadataAddName = name.data();

        // Type: searchable, grouped by kind, remembering the last choice.
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Type");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##type", TypeLabelUVE(m_metadataAddType).c_str(), ImGuiComboFlags_HeightLarge)) {
            std::array<char, 64> filter{};
            m_metadataTypeFilter.copy(filter.data(), filter.size() - 1U);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }
            if (ImGui::InputTextWithHint("##type-filter", "Search types", filter.data(), filter.size())) {
                m_metadataTypeFilter = filter.data();
            }
            std::string_view lastCategory;
            for (const VariantTypeUVE type : Core::GetAllVariantTypesUVE()) {
                const std::string label = TypeLabelUVE(type);
                const std::string_view category = Core::GetVariantTypeCategoryUVE(type);
                if (!ContainsFoldedUVE(label, m_metadataTypeFilter) && !ContainsFoldedUVE(category, m_metadataTypeFilter)) {
                    continue;
                }
                if (category != lastCategory) {
                    ImGui::SeparatorText(std::string{category}.c_str());
                    lastCategory = category;
                }
                if (ImGui::Selectable(label.c_str(), type == m_metadataAddType)) {
                    m_metadataAddType = type;
                    m_metadataAddValue = VariantUVE::MakeDefaultUVE(type);
                }
            }
            ImGui::EndCombo();
        }

        // Starting value, so the entry is right the moment it exists.
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Value");
        ImGui::TableSetColumnIndex(1);
        static_cast<void>(DrawVariantValueEditorUVE("##initial", *m_metadataAddValue, 0));
        ImGui::EndTable();

        const Scene::NodeMetadataKeyIssueUVE issue = Scene::ValidateNodeMetadataKeyUVE(m_metadataAddName, metadata);
        if (issue != Scene::NodeMetadataKeyIssueUVE::None && !m_metadataAddName.empty()) {
            ImGui::TextColored(ImVec4{0.95F, 0.62F, 0.35F, 1.0F}, "%s",
                               std::string{Scene::DescribeNodeMetadataKeyIssueUVE(issue)}.c_str());
        }
        ImGui::Separator();
        const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5F;
        const bool cancel = ImGui::Button("Cancel", ImVec2{buttonWidth, 0.0F}) || ImGui::IsKeyPressed(ImGuiKey_Escape);
        ImGui::SameLine();
        ImGui::BeginDisabled(issue != Scene::NodeMetadataKeyIssueUVE::None);
        const bool add = ImGui::Button("Add", ImVec2{buttonWidth, 0.0F}) ||
                         (submitted && issue == Scene::NodeMetadataKeyIssueUVE::None);
        ImGui::EndDisabled();
        if ((add && AddSelectedNodeMetadataUVE(m_metadataAddName, *m_metadataAddValue)) || cancel) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace UVE::Editor
