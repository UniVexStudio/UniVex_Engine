// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The metadata-driven half of the Inspector: one drawer per declared component type, built from
// what the type says about itself rather than from a hand-written form per type.
//
// What this replaced. The Inspector used to dispatch on EditorSceneComponentKindUVE - a closed
// enum with a positionally-parallel std::variant - across eight sites, plus roughly ninety
// per-type branches in the panel file. Adding one component meant editing a dozen places in five
// files, and because that was expensive, nine of the twenty-two drawers had never been finished:
// they showed a title and a Remove button, so a camera's field of view, a light's colour, a
// collider's extents and a rigid body's mass were not editable at all.
//
// Here a component declares its properties once (Engine/Runtime/Scene/Internal/
// scene_component_metadata_uve.cpp) and the Inspector reads the declaration. The number of cases
// this file handles is bounded by the engine's value types - about a dozen - not by its component
// types. A new component becomes fully inspectable by being declared, and nothing here changes.
//
// What is deliberately NOT generic. Three drawers stay hand-written because a generic editor
// would be wrong rather than merely plain, and each says so at its exclusion below.

#include "uve/editor/editor_uve.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

#include <imgui.h>

#include "editor_axis_input_uve.h"

#include "uve/asset/asset_guid_uve.h"
#include "uve/asset/i_asset_database_uve.h"
#include "uve/asset/i_project_file_index_uve.h"
#include "uve/component/editor_description_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Editor {
namespace {

using Core::HasPropertyFlagUVE;
using Core::TypeMetadataEntryUVE;
using Core::TypeMetadataPropertyFlagsUVE;
using Core::TypeMetadataPropertyUVE;

/// Component types whose Inspector section stays hand-written, with the reason each one cannot be
/// served by a generic editor. Everything not listed here is drawn from its declaration.
///
///   component.name      - a name is not a field on a component to the editor; it routes through
///                         SetSelectedEntityNameUVE, which owns uniqueness and its own history
///                         entry, and is shown above the components rather than among them.
///   component.transform - the quaternion, the authored Euler angles and the edit mode are three
///                         views of one piece of state that must be written together. Writing the
///                         quaternion alone would leave the angles describing a different
///                         rotation; see TransformComponentUVE::localEulerRadians.
///   component.hierarchy - the parent is authored by picking from valid reparent targets, which is
///                         a lifecycle command with its own cycle and permission checks, not a
///                         field write.
constexpr std::array<const char*, 3> kCustomDrawnComponentTypeIdsUVE{
    "component.name",
    "component.transform",
    "component.hierarchy",
};

[[nodiscard]] bool IsCustomDrawnUVE(const std::string& typeId) noexcept {
    return std::find_if(kCustomDrawnComponentTypeIdsUVE.cbegin(), kCustomDrawnComponentTypeIdsUVE.cend(),
                        [&typeId](const char* const candidate) { return typeId == candidate; }) !=
           kCustomDrawnComponentTypeIdsUVE.cend();
}

/// "component.rigid_body" becomes "rigid-body": the stable drawer id the registry orders and the
/// Inspector's search filter matches on. Derived rather than declared so an id can never drift
/// from the type it belongs to.
[[nodiscard]] std::string DrawerIdForTypeIdUVE(const std::string& typeId) {
    constexpr std::string_view prefix = "component.";
    std::string id = typeId.rfind(prefix, 0U) == 0U ? typeId.substr(prefix.size()) : typeId;
    std::replace(id.begin(), id.end(), '_', '-');
    return id;
}

/// A property is shown when nothing hides it: not flagged Hidden, and either unconditional or
/// accepted by its own predicate against the live component.
[[nodiscard]] bool IsPropertyVisibleUVE(const TypeMetadataPropertyUVE& property, const void* instance) {
    if (HasPropertyFlagUVE(property.flags, TypeMetadataPropertyFlagsUVE::Hidden)) {
        return false;
    }
    return property.isVisible == nullptr || property.isVisible(instance);
}

void DrawTooltipUVE(const TypeMetadataPropertyUVE& property) {
    if (!property.tooltip.empty() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", property.tooltip.c_str());
    }
}

/// A float range, or a sane unbounded default. DragFloat needs a speed even where the declaration
/// gives no bounds.
[[nodiscard]] float RangeStepUVE(const TypeMetadataPropertyUVE& property, const float fallback) noexcept {
    return (property.range.enabled && property.range.step > 0.0)
               ? static_cast<float>(property.range.step)
               : fallback;
}

[[nodiscard]] float RangeMinimumUVE(const TypeMetadataPropertyUVE& property) noexcept {
    return property.range.enabled ? static_cast<float>(property.range.minimum)
                                  : -std::numeric_limits<float>::max();
}

[[nodiscard]] float RangeMaximumUVE(const TypeMetadataPropertyUVE& property) noexcept {
    return property.range.enabled ? static_cast<float>(property.range.maximum)
                                  : std::numeric_limits<float>::max();
}

/// Which declared components can be detached, and under which authoring kind.
///
/// Custom drawers that lay out their own rows - a multi-line box, a slot with an action strip, a
/// list with an add button - rather than filling a value cell. Any other id falls back to the
/// generic editor for its value type, which is where the rotation and entity-picker ids still go.
constexpr std::array<std::string_view, 5> kBlockPropertyDrawerIdsUVE{
    "multiline-text",
    "script-slot",
    "node-metadata",
    "skeleton-source",
    "skeleton-bones",
};

[[nodiscard]] bool IsBlockPropertyDrawerUVE(const std::string& drawerId) noexcept {
    return std::find(kBlockPropertyDrawerIdsUVE.cbegin(), kBlockPropertyDrawerIdsUVE.cend(), drawerId) !=
           kBlockPropertyDrawerIdsUVE.cend();
}

/// True when another property of `entry` names `property` as its resolved answer. Such a property
/// is shown beside the choice it resolves, never as a row of its own.
[[nodiscard]] bool IsResolvedCompanionUVE(const TypeMetadataEntryUVE& entry, const TypeMetadataPropertyUVE& property) {
    return std::any_of(entry.properties.cbegin(), entry.properties.cend(),
                       [&property](const TypeMetadataPropertyUVE& other) {
                           return other.resolvedByProperty == property.name;
                       });
}

/// The label of what `property` currently resolves to, read through the companion it names: an
/// enum's option label, or On/Off for a flag. Empty when there is no companion or no answer.
[[nodiscard]] std::string ResolvedLabelUVE(const TypeMetadataEntryUVE& entry, const TypeMetadataPropertyUVE& property,
                                           const void* instance) {
    const auto companion = std::find_if(entry.properties.cbegin(), entry.properties.cend(),
                                        [&property](const TypeMetadataPropertyUVE& other) {
                                            return other.name == property.resolvedByProperty;
                                        });
    if (property.resolvedByProperty.empty() || companion == entry.properties.cend() ||
        companion->getValue == nullptr) {
        return {};
    }
    if (!companion->enumEntries.empty()) {
        std::int64_t value = 0;
        companion->getValue(instance, &value);
        for (const Core::TypeMetadataEnumEntryUVE& option : companion->enumEntries) {
            if (option.value == value) {
                return option.label;
            }
        }
        return {};
    }
    if (companion->typeId == Scene::kPropertyTypeBoolUVE) {
        bool value = false;
        companion->getValue(instance, &value);
        return value ? "On" : "Off";
    }
    return {};
}

/// Opens the two-column label/value table every section's rows share. The fixed ratio is what
/// keeps values aligned down the whole panel, although each section is its own table.
[[nodiscard]] bool BeginPropertyRowsUVE(const char* const id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
        return false;
    }
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, 0.42F);
    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 0.58F);
    return true;
}

/// A small circular-arrow button. Drawn rather than taken from a font so it never depends on which
/// glyphs the editor font happens to carry.
[[nodiscard]] bool DrawRevertIconButtonUVE(const float size) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##revert", ImVec2{size, size});
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    const ImVec2 center{origin.x + (size * 0.5F), origin.y + (size * 0.5F)};
    const float radius = size * 0.28F;
    constexpr float kPi = 3.14159265F;
    drawList.PathArcTo(center, radius, kPi * 0.15F, kPi * 1.75F, 16);
    drawList.PathStroke(color, 0, 1.6F);
    // Arrowhead at the arc's start, pointing along the direction of travel.
    const ImVec2 tip{center.x + (radius * std::cos(kPi * 0.15F)), center.y + (radius * std::sin(kPi * 0.15F))};
    const float head = size * 0.18F;
    drawList.AddTriangleFilled(ImVec2{tip.x - head, tip.y - (head * 0.2F)}, ImVec2{tip.x + head, tip.y - (head * 0.2F)},
                               ImVec2{tip.x, tip.y + head}, color);
    return clicked;
}

/// The std::string plumbing Dear ImGui leaves to the caller: grows the string when the text
/// outgrows it, and refuses input past the bound by trimming at a UTF-8 character boundary.
struct TextInputContextUVE final {
    std::string* text = nullptr;
    std::size_t maximumBytes = 0U;
};

int TextInputCallbackUVE(ImGuiInputTextCallbackData* const data) {
    auto* const context = static_cast<TextInputContextUVE*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        context->text->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = context->text->data();
        return 0;
    }
    if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit &&
        static_cast<std::size_t>(data->BufTextLen) > context->maximumBytes) {
        std::size_t keep = context->maximumBytes;
        // Never cut a multi-byte character in half: back up to the start of the one straddling it.
        while (keep > 0U && (static_cast<unsigned char>(data->Buf[keep]) & 0xC0U) == 0x80U) {
            --keep;
        }
        data->DeleteChars(static_cast<int>(keep), data->BufTextLen - static_cast<int>(keep));
    }
    return 0;
}

} // namespace

void EditorUVE::RegisterMetadataInspectorDrawersUVE() {
    // Registration order is the Inspector's section order: by the declared section key, then by
    // type id for a stable result. That is how the properties every node has in common end up
    // below whatever the node itself brings, without this loop knowing which are which.
    std::vector<const TypeMetadataEntryUVE*> entries;
    const Core::TypeMetadataRegistryUVE& registry = Scene::GetSceneComponentMetadataRegistryUVE();
    for (const TypeMetadataEntryUVE& snapshotEntry : registry.GetSnapshotUVE().entries) {
        // The snapshot is a copy; register against the registry's own stable entry, because the
        // drawer callback and any history entry hold a pointer to it.
        const TypeMetadataEntryUVE* const stable = registry.FindTypeUVE(snapshotEntry.typeId);
        if (stable != nullptr && !IsCustomDrawnUVE(stable->typeId)) {
            entries.push_back(stable);
        }
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const TypeMetadataEntryUVE* const left, const TypeMetadataEntryUVE* const right) {
                         if (left->order != right->order) {
                             return left->order < right->order;
                         }
                         return left->typeId < right->typeId;
                     });

    // A nested type is drawn by its host when the entity carries both, and on its own otherwise, so
    // it is never unreachable. A host that is hand-drawn or undeclared cannot draw it, so a type
    // naming one stands alone.
    const auto findHostUVE = [&entries](const TypeMetadataEntryUVE& entry) -> const TypeMetadataEntryUVE* {
        if (entry.nestedUnderTypeId.empty()) {
            return nullptr;
        }
        const auto host = std::find_if(entries.cbegin(), entries.cend(), [&entry](const TypeMetadataEntryUVE* candidate) {
            return candidate->typeId == entry.nestedUnderTypeId;
        });
        return host == entries.cend() ? nullptr : *host;
    };

    // Transform is hand-drawn, but it still takes its declared place in the order.
    bool transformRegistered = false;
    for (const TypeMetadataEntryUVE* const entry : entries) {
        if (!transformRegistered && entry->order > Scene::kSectionOrderTransformUVE) {
            RegisterTransformInspectorDrawerUVE();
            transformRegistered = true;
        }
        std::vector<const TypeMetadataEntryUVE*> nested;
        for (const TypeMetadataEntryUVE* const candidate : entries) {
            if (findHostUVE(*candidate) == entry) {
                nested.push_back(candidate);
            }
        }
        const TypeMetadataEntryUVE* const host = findHostUVE(*entry);
        static_cast<void>(m_inspectorDrawerRegistry.RegisterDrawerUVE(InspectorDrawerEntryUVE{
            DrawerIdForTypeIdUVE(entry->typeId),
            [this, entry, host](const Scene::EntityUVE entity) {
                const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
                return IsDocumentEntityUVE(entity) && entityManager.HasComponentUVE(entity, entry->typeIndex) &&
                       (host == nullptr || !entityManager.HasComponentUVE(entity, host->typeIndex));
            },
            [this, entry, nested = std::move(nested)](const Scene::EntityUVE entity) {
                DrawMetadataComponentDrawerUVE(entity, *entry, nested);
            },
        }));
    }
    if (!transformRegistered) {
        RegisterTransformInspectorDrawerUVE();
    }
}

void EditorUVE::DrawMetadataComponentDrawerUVE(const Scene::EntityUVE entity, const TypeMetadataEntryUVE& entry,
                                               const std::vector<const TypeMetadataEntryUVE*>& nested) {
    if (!IsDocumentEntityUVE(entity) || entity != m_selectedEntity) {
        return;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE(entity, entry.typeIndex)) {
        return;
    }
    const void* const instance = entityManager.GetComponentPointerUVE(entity, entry.typeIndex);

    ImGui::PushID(entry.typeId.c_str());
    if (entry.presentedInline) {
        ImGui::Spacing();
        DrawMetadataPropertyRowsUVE(entry, instance);
        ImGui::PopID();
        return;
    }
    // Collapsible, and collapsed state is remembered per section by Dear ImGui's own storage for
    // the window - which is what makes a long Inspector usable at all.
    // "###" keeps the header's identity on the type, so a title that follows the value does not
    // reset the section's open state when the value changes.
    const std::string header = std::string{entry.sectionTitle != nullptr ? entry.sectionTitle(instance)
                                                                          : entry.displayName.c_str()} +
                               "###" + entry.typeId;
    if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawMetadataPropertyRowsUVE(entry, instance);
        for (const TypeMetadataEntryUVE* const child : nested) {
            if (!entityManager.HasComponentUVE(entity, child->typeIndex)) {
                continue;
            }
            ImGui::PushID(child->typeId.c_str());
            constexpr ImGuiTreeNodeFlags kNestedFlags = ImGuiTreeNodeFlags_DefaultOpen |
                                                        ImGuiTreeNodeFlags_SpanAvailWidth |
                                                        ImGuiTreeNodeFlags_FramePadding;
            if (ImGui::TreeNodeEx(child->displayName.c_str(), kNestedFlags)) {
                DrawMetadataPropertyRowsUVE(*child, entityManager.GetComponentPointerUVE(entity, child->typeIndex));
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    ImGui::PopID();
}

void EditorUVE::DrawMetadataPropertyRowsUVE(const TypeMetadataEntryUVE& entry, const void* const instance) {
    // Runtime-owned state describes a simulation, and there is one only in Play. Shown while
    // editing, it is a frozen number that looks like a setting.
    const bool simulating = m_playModeState != EditorPlayModeStateUVE::Edit;
    enum class TableStateUVE : std::uint8_t { Closed, Open, Clipped };
    TableStateUVE table = TableStateUVE::Closed;
    int tableIndex = 0;
    const auto closeTable = [&table] {
        if (table == TableStateUVE::Open) {
            ImGui::EndTable();
        }
        table = TableStateUVE::Closed;
    };
    // Sub-groups: consecutive properties naming the same `section` sit under one collapsible
    // header inside the type's section, so a long section reads as a few labelled blocks. A
    // collapsed group submits nothing for its rows.
    const std::string* group = nullptr;
    bool groupOpen = false;
    const auto closeGroup = [&] {
        closeTable();
        if (groupOpen) {
            ImGui::TreePop();
        }
        group = nullptr;
        groupOpen = false;
    };

    for (const TypeMetadataPropertyUVE& property : entry.properties) {
        if (!IsPropertyVisibleUVE(property, instance) || IsResolvedCompanionUVE(entry, property) ||
            (HasPropertyFlagUVE(property.flags, TypeMetadataPropertyFlagsUVE::RuntimeState) && !simulating)) {
            continue;
        }
        if (group == nullptr ? !property.section.empty() : *group != property.section) {
            closeGroup();
            if (!property.section.empty()) {
                group = &property.section;
                constexpr ImGuiTreeNodeFlags kGroupFlags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                                           ImGuiTreeNodeFlags_FramePadding;
                const std::string groupId = property.section + "##group-" + property.section;
                groupOpen = ImGui::TreeNodeEx(groupId.c_str(), kGroupFlags);
            }
        }
        if (group != nullptr && !groupOpen) {
            continue;
        }
        if (IsBlockPropertyDrawerUVE(property.customDrawerId)) {
            // A block drawer lays out its own rows, so the shared table closes around it and a
            // fresh one opens for whatever follows.
            closeTable();
            ImGui::PushID(property.name.c_str());
            static_cast<void>(DrawCustomPropertyUVE(entry, property, instance));
            ImGui::PopID();
            continue;
        }
        if (table == TableStateUVE::Closed) {
            const std::string tableId = "##rows" + std::to_string(tableIndex++);
            table = BeginPropertyRowsUVE(tableId.c_str()) ? TableStateUVE::Open : TableStateUVE::Clipped;
        }
        if (table == TableStateUVE::Clipped) {
            continue; // Scrolled out of view; Dear ImGui asks for nothing to be submitted.
        }
        ImGui::PushID(property.name.c_str());
        DrawMetadataPropertyRowUVE(entry, property, instance);
        ImGui::PopID();
    }
    closeGroup();
}

bool EditorUVE::DrawMetadataPropertyLabelUVE(const TypeMetadataEntryUVE& entry, const TypeMetadataPropertyUVE& property,
                                             const void* const instance, const bool writable) {
    const float cellStart = ImGui::GetCursorPosX();
    const float cellWidth = ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(property.displayName.c_str());
    DrawTooltipUVE(property);
    // Offered only when there is something to revert to. A revert control on every row, most of
    // them already at their default, is noise that hides the few rows someone actually changed.
    if (!writable || IsPropertyAtDefaultUVE(entry, property, instance)) {
        return false;
    }
    const float size = ImGui::GetFrameHeight();
    ImGui::SameLine(cellStart + std::max(0.0F, cellWidth - size));
    // The type's own factory says what a fresh component holds, so a default can never drift
    // from the one the constructor actually applies.
    const bool reverted = DrawRevertIconButtonUVE(size) && ResetSelectedComponentPropertyUVE(entry, property);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Revert to the value a newly added %s has.", entry.displayName.c_str());
    }
    return reverted;
}

void EditorUVE::DrawMetadataPropertyRowUVE(const TypeMetadataEntryUVE& entry,
                                           const TypeMetadataPropertyUVE& property, const void* instance) {
    // Runtime-owned and read-only state is drawn disabled so the widget cannot report an edit that
    // would be overwritten on the next update.
    const bool writable = property.IsAuthoringWritableUVE() && IsAuthoringCommandAllowedUVE();
    bool edited = false;

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    edited = DrawMetadataPropertyLabelUVE(entry, property, instance, writable);
    ImGui::TableSetColumnIndex(1);

    ImGui::BeginDisabled(!writable);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (!property.enumEntries.empty()) {
        // Collapsed dropdown - an enum never occupies the section with one row per option. When
        // the choice is resolved against the hierarchy, the answer rides along in the preview:
        // "Inherit (Pausable)" says what Inherit means here without a second row to read.
        std::int64_t current = 0;
        property.getValue(instance, &current);
        std::size_t selected = 0U;
        for (std::size_t index = 0; index < property.enumEntries.size(); ++index) {
            if (property.enumEntries[index].value == current) {
                selected = index;
            }
        }
        std::string preview = property.enumEntries[selected].label;
        if (const std::string resolved = ResolvedLabelUVE(entry, property, instance);
            !resolved.empty() && resolved != preview) {
            preview += " (" + resolved + ")";
        }
        if (ImGui::BeginCombo("##value", preview.c_str())) {
            for (std::size_t index = 0; index < property.enumEntries.size(); ++index) {
                const bool isSelected = index == selected;
                if (ImGui::Selectable(property.enumEntries[index].label.c_str(), isSelected) && !isSelected) {
                    const std::int64_t chosen = property.enumEntries[index].value;
                    edited = SetSelectedComponentPropertyUVE(entry, property, &chosen);
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    } else if (property.typeId == Scene::kPropertyTypeBoolUVE) {
        bool value = false;
        property.getValue(instance, &value);
        if (ImGui::Checkbox("##value", &value)) {
            edited = SetSelectedComponentPropertyUVE(entry, property, &value);
        }
    } else if (property.typeId == Scene::kPropertyTypeFloatUVE) {
        float value = 0.0F;
        property.getValue(instance, &value);
        if (ImGui::DragFloat("##value", &value, RangeStepUVE(property, 0.01F), RangeMinimumUVE(property),
                             RangeMaximumUVE(property))) {
            edited = SetSelectedComponentPropertyUVE(entry, property, &value);
        }
    } else if (property.typeId == Scene::kPropertyTypeVector2UVE) {
        Math::Vector2UVE value{};
        property.getValue(instance, &value);
        if (DrawAxisVectorInputUVE("##value", &value.x, 2, RangeStepUVE(property, 0.01F), RangeMinimumUVE(property),
                                   RangeMaximumUVE(property))) {
            edited = SetSelectedComponentPropertyUVE(entry, property, &value);
        }
    } else if (property.typeId == Scene::kPropertyTypeVector3UVE) {
        Math::Vector3UVE value{};
        property.getValue(instance, &value);
        if (DrawAxisVectorInputUVE("##value", &value.x, 3, RangeStepUVE(property, 0.01F), RangeMinimumUVE(property),
                                   RangeMaximumUVE(property))) {
            edited = SetSelectedComponentPropertyUVE(entry, property, &value);
        }
    } else if (property.typeId == Scene::kPropertyTypeColorUVE) {
        Math::Vector3UVE value{};
        property.getValue(instance, &value);
        if (ImGui::ColorEdit3("##value", &value.x)) {
            edited = SetSelectedComponentPropertyUVE(entry, property, &value);
        }
    } else if (property.typeId == Scene::kPropertyTypeInt32UVE) {
        std::int32_t value = 0;
        property.getValue(instance, &value);
        int shown = value;
        if (ImGui::DragInt("##value", &shown, RangeStepUVE(property, 1.0F),
                           property.range.enabled ? static_cast<int>(property.range.minimum)
                                                  : std::numeric_limits<int>::lowest(),
                           property.range.enabled ? static_cast<int>(property.range.maximum)
                                                  : std::numeric_limits<int>::max())) {
            value = shown;
            edited = SetSelectedComponentPropertyUVE(entry, property, &value);
        }
    } else if (property.typeId == Scene::kPropertyTypeUInt32UVE) {
        std::uint32_t value = 0U;
        property.getValue(instance, &value);
        // Edited as a signed int because that is what ImGui offers, then clamped at zero rather
        // than wrapping to four billion when an author drags below the minimum.
        int shown = value <= static_cast<std::uint32_t>(std::numeric_limits<int>::max())
                        ? static_cast<int>(value)
                        : std::numeric_limits<int>::max();
        const int minimum = property.range.enabled ? std::max(0, static_cast<int>(property.range.minimum)) : 0;
        const int maximum = property.range.enabled ? static_cast<int>(property.range.maximum)
                                                   : std::numeric_limits<int>::max();
        if (ImGui::DragInt("##value", &shown, RangeStepUVE(property, 1.0F), minimum, maximum)) {
            value = static_cast<std::uint32_t>(std::max(0, shown));
            edited = SetSelectedComponentPropertyUVE(entry, property, &value);
        }
    } else if (property.typeId == Scene::kPropertyTypeBitMask32UVE) {
        // Authored as bits, because that is what a layer or a mask is. Hexadecimal rather than a
        // decimal count, so 0xFFFFFFFF reads as "all layers" instead of as 4294967295.
        std::uint32_t value = 0U;
        property.getValue(instance, &value);
        if (ImGui::InputScalar("##value", ImGuiDataType_U32, &value, nullptr, nullptr, "%08X",
                               ImGuiInputTextFlags_CharsHexadecimal |
                                   ImGuiInputTextFlags_EnterReturnsTrue)) {
            edited = SetSelectedComponentPropertyUVE(entry, property, &value);
        }
    } else if (property.typeId == Scene::kPropertyTypeStringUVE) {
        std::string value;
        property.getValue(instance, &value);
        // Committed once when the author finishes, so an edit is one history entry and clicking
        // away keeps it. The bound is the one this row always had.
        constexpr std::size_t kMaximumGenericStringBytesUVE = 255U;
        if (const std::optional<std::string> committed =
                DrawCommittedTextInputUVE("##value", value, false, 0.0F, kMaximumGenericStringBytesUVE);
            committed.has_value()) {
            edited = SetSelectedComponentPropertyUVE(entry, property, &*committed);
        }
    } else if (property.typeId == Scene::kPropertyTypeAssetGuidUVE) {
        // A picker over the registered assets of the kind the property declares
        // ("asset:uvemodel"). Picking, never typing: a typed 64-bit number is a way to author a
        // dangling reference. A property that declares no kind is shown read-only.
        Asset::AssetGuidUVE value{};
        property.getValue(instance, &value);
        constexpr std::string_view kAssetPrefix = "asset:";
        if (property.customDrawerId.rfind(kAssetPrefix, 0U) != 0U) {
            ImGui::BeginDisabled();
            ImGui::Text("%016llX", static_cast<unsigned long long>(value.value));
            ImGui::EndDisabled();
        } else {
            const std::string extension = "." + property.customDrawerId.substr(kAssetPrefix.size());
            const Asset::IAssetDatabaseUVE& database = m_services->GetAssetDatabaseUVE();
            std::string preview = "(none)";
            if (value != Asset::kInvalidAssetGuidUVE) {
                const std::filesystem::path path = database.ResolveUVE(value);
                preview = path.empty() ? "(missing asset)" : path.stem().string();
            }
            if (ImGui::BeginCombo("##value", preview.c_str())) {
                if (ImGui::Selectable("(none)", value == Asset::kInvalidAssetGuidUVE) &&
                    value != Asset::kInvalidAssetGuidUVE) {
                    const Asset::AssetGuidUVE none = Asset::kInvalidAssetGuidUVE;
                    edited = SetSelectedComponentPropertyUVE(entry, property, &none);
                }
                // Registered assets plus every project file of the kind, so a model imported in an
                // earlier session is offered too; picking an unregistered file registers it.
                struct CandidateUVE {
                    std::filesystem::path path;
                    std::optional<Asset::AssetGuidUVE> guid;
                };
                std::vector<CandidateUVE> candidates;
                for (const Asset::AssetRecordUVE& record : database.GetRegisteredAssetsUVE()) {
                    if (record.path.extension().string() == extension) {
                        candidates.push_back({record.path.lexically_normal(), record.guid});
                    }
                }
                const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
                for (const Asset::ProjectFileEntryUVE& file : project.entries) {
                    if (file.kind == Asset::ProjectFileEntryKindUVE::Directory) {
                        continue;
                    }
                    // A model source (.glb/.gltf/.obj) stands for the mesh it was imported to, once
                    // that import has produced it; the author picks the file they know.
                    std::filesystem::path path;
                    if (extension == ".uvemodel" && IsModelSourcePathUVE(file.relativePath)) {
                        path = GetImportedModelPathUVE(file.relativePath);
                        std::error_code error;
                        if (!std::filesystem::is_regular_file(path, error)) {
                            continue;
                        }
                    } else if (file.relativePath.extension().string() == extension) {
                        path = (project.contentRoot / file.relativePath).lexically_normal();
                    } else {
                        continue;
                    }
                    const bool known = std::any_of(candidates.begin(), candidates.end(),
                                                   [&path](const CandidateUVE& candidate) { return candidate.path == path; });
                    if (!known) {
                        candidates.push_back({path, std::nullopt});
                    }
                }
                std::sort(candidates.begin(), candidates.end(), [](const CandidateUVE& left, const CandidateUVE& right) {
                    return left.path.generic_string() < right.path.generic_string();
                });
                bool any = false;
                for (const CandidateUVE& candidate : candidates) {
                    any = true;
                    const bool isSelected = candidate.guid.has_value() && *candidate.guid == value;
                    const std::string label = candidate.path.stem().string() + "##" + candidate.path.generic_string();
                    if (ImGui::Selectable(label.c_str(), isSelected) && !isSelected) {
                        const Asset::AssetGuidUVE chosen = candidate.guid.has_value()
                                                               ? *candidate.guid
                                                               : m_services->GetAssetDatabaseUVE().RegisterUVE(candidate.path);
                        edited = SetSelectedComponentPropertyUVE(entry, property, &chosen);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", candidate.path.generic_string().c_str());
                    }
                }
                if (!any) {
                    ImGui::TextDisabled("No %s assets yet. Import one from the Content Browser.", extension.c_str());
                }
                ImGui::EndCombo();
            }
        }
    } else if (property.typeId == Scene::kPropertyTypeEntityUVE) {
        // Same reasoning as an asset guid: an entity reference is picked, not typed. The two
        // properties that hold one both declare a custom drawer for when that picker lands.
        Scene::EntityUVE value = Scene::kInvalidEntityUVE;
        property.getValue(instance, &value);
        ImGui::BeginDisabled();
        if (value == Scene::kInvalidEntityUVE) {
            ImGui::TextUnformatted("(none)");
        } else {
            ImGui::TextUnformatted(GetEntityDisplayLabelUVE(value).c_str());
        }
        ImGui::EndDisabled();
    } else {
        // A value type nothing here knows how to draw. Saying so is better than drawing something
        // that looks editable and silently is not.
        ImGui::TextDisabled("No editor for type \"%s\".", property.typeId.c_str());
    }
    ImGui::EndDisabled();
    static_cast<void>(edited);
}

bool EditorUVE::DrawCustomPropertyUVE(const TypeMetadataEntryUVE& entry, const TypeMetadataPropertyUVE& property,
                                      const void* const instance) {
    if (property.customDrawerId == "multiline-text") {
        DrawMultilineTextPropertyUVE(entry, property, instance);
        return true;
    }
    if (property.customDrawerId == "script-slot") {
        DrawScriptSlotPropertyUVE(entry, property, instance);
        return true;
    }
    if (property.customDrawerId == "node-metadata") {
        DrawNodeMetadataPropertyUVE(entry, property, instance);
        return true;
    }
    if (property.customDrawerId == "skeleton-source") {
        DrawSkeletonSourcePropertyUVE(entry, property, instance);
        return true;
    }
    if (property.customDrawerId == "skeleton-bones") {
        DrawSkeletonBonesPropertyUVE(entry, property, instance);
        return true;
    }
    return false;
}

void EditorUVE::DrawMultilineTextPropertyUVE(const TypeMetadataEntryUVE& entry, const TypeMetadataPropertyUVE& property,
                                             const void* const instance) {
    if (property.typeId != Scene::kPropertyTypeStringUVE || property.getValue == nullptr) {
        return;
    }
    const bool writable = property.IsAuthoringWritableUVE() && IsAuthoringCommandAllowedUVE();
    if (!BeginPropertyRowsUVE("##multiline")) {
        return;
    }
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    static_cast<void>(DrawMetadataPropertyLabelUVE(entry, property, instance, writable));
    ImGui::TableSetColumnIndex(1);
    std::string value;
    property.getValue(instance, &value);
    ImGui::BeginDisabled(!writable);
    ImGui::SetNextItemWidth(-FLT_MIN);
    // Four lines: enough to read a note without scrolling, small enough not to dominate the panel.
    const float height = (ImGui::GetTextLineHeight() * 4.0F) + (ImGui::GetStyle().FramePadding.y * 2.0F);
    if (const std::optional<std::string> committed =
            DrawCommittedTextInputUVE("##value", value, true, height, Scene::kMaximumEditorDescriptionBytesUVE);
        committed.has_value()) {
        static_cast<void>(SetSelectedComponentPropertyUVE(entry, property, &*committed));
    }
    ImGui::EndDisabled();
    ImGui::EndTable();
}

bool EditorUVE::IsPropertyAtDefaultUVE(const TypeMetadataEntryUVE& entry, const TypeMetadataPropertyUVE& property,
                                       const void* const instance) {
    if (property.areEqual == nullptr || !entry.HasFactoryUVE()) {
        return false;
    }
    auto defaults = m_inspectorDefaultInstances.find(&entry);
    if (defaults == m_inspectorDefaultInstances.end()) {
        defaults = m_inspectorDefaultInstances.emplace(&entry, Core::TypeInstanceUVE::MakeDefaultUVE(entry)).first;
    }
    return defaults->second.IsValidUVE() && property.areEqual(defaults->second.GetUVE(), instance);
}

std::optional<std::string> EditorUVE::DrawCommittedTextInputUVE(const char* const id, const std::string& current,
                                                                const bool multiline, const float height,
                                                                const std::size_t maximumBytes) {
    const ImGuiID widgetId = ImGui::GetID(id);
    const auto findEdit = [this, widgetId] {
        return std::find_if(m_inspectorTextEdits.begin(), m_inspectorTextEdits.end(),
                            [widgetId](const auto& edit) { return edit.first == widgetId; });
    };
    // While the field is active Dear ImGui holds the text itself; the buffer handed to it only
    // receives the edits. Before that, it is a fresh copy of the stored value.
    auto edit = findEdit();
    std::string scratch = edit == m_inspectorTextEdits.end() ? current : std::string{};
    std::string& text = edit == m_inspectorTextEdits.end() ? scratch : edit->second;
    TextInputContextUVE context{&text, maximumBytes};
    constexpr ImGuiInputTextFlags kFlags = ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CallbackEdit;
    if (multiline) {
        ImGui::InputTextMultiline(id, text.data(), text.capacity() + 1U, ImVec2{-FLT_MIN, height}, kFlags,
                                  TextInputCallbackUVE, &context);
    } else {
        ImGui::InputText(id, text.data(), text.capacity() + 1U, kFlags, TextInputCallbackUVE, &context);
    }
    if (ImGui::IsItemActivated() && edit == m_inspectorTextEdits.end()) {
        // At most the field being let go and the one being taken are live at once; anything older
        // belonged to a field that vanished while active (the selection changed), and is dropped.
        if (m_inspectorTextEdits.size() >= 2U) {
            m_inspectorTextEdits.erase(m_inspectorTextEdits.begin());
        }
        m_inspectorTextEdits.emplace_back(widgetId, text);
        return std::nullopt;
    }
    if (!ImGui::IsItemDeactivated() || edit == m_inspectorTextEdits.end()) {
        return std::nullopt;
    }
    std::string committed = std::move(edit->second);
    m_inspectorTextEdits.erase(edit);
    if (!ImGui::IsItemDeactivatedAfterEdit()) {
        return std::nullopt;
    }
    return committed;
}

bool EditorUVE::SetSelectedComponentValueUVE(const TypeMetadataEntryUVE& entry, const void* const newInstance) {
    if (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE() || !entry.HasFactoryUVE() ||
        newInstance == nullptr || (entry.isInstanceValid != nullptr && !entry.isInstanceValid(newInstance))) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE(m_selectedEntity, entry.typeIndex)) {
        return false;
    }
    void* const instance = entityManager.GetComponentPointerUVE(m_selectedEntity, entry.typeIndex);
    Core::TypeInstanceUVE before = Core::TypeInstanceUVE::CloneUVE(entry, instance);
    Core::TypeInstanceUVE after = Core::TypeInstanceUVE::CloneUVE(entry, newInstance);
    if (!before.IsValidUVE() || !after.IsValidUVE()) {
        return false;
    }
    entry.assignInstance(instance, newInstance);
    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    m_sceneDirty = true;
    RecordHistoryUVE(ComponentPropertyHistoryEntryUVE{m_selectedEntity, &entry, std::move(before), std::move(after),
                                                      selectionBefore, CaptureSelectionSnapshotUVE(), dirtyBefore,
                                                      true});
    return true;
}

bool EditorUVE::SetEntityVisibleUVE(const Scene::EntityUVE entity, const bool visible) {
    if (!IsAuthoringCommandAllowedUVE() || !IsDocumentEntityUVE(entity)) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(entity)) {
        return false;
    }
    const Core::TypeMetadataEntryUVE* const entry = Scene::GetSceneComponentMetadataRegistryUVE().FindTypeByIndexUVE(
        std::type_index(typeid(Scene::VisibilityComponentUVE)));
    if (entry == nullptr || !entry->HasFactoryUVE()) {
        return false;
    }
    auto& visibility = entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(entity);
    if (visibility.visible == visible) {
        return false;
    }
    // Same history entry as an Inspector edit, so undo restores the whole component on this
    // entity and leaves the selection exactly as it was.
    Core::TypeInstanceUVE before = Core::TypeInstanceUVE::CloneUVE(*entry, &visibility);
    if (!before.IsValidUVE()) {
        return false;
    }
    visibility.visible = visible;
    Core::TypeInstanceUVE after = Core::TypeInstanceUVE::CloneUVE(*entry, &visibility);
    if (!after.IsValidUVE()) {
        visibility.visible = !visible;
        return false;
    }
    const EditorSelectionSnapshotUVE selection = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    m_sceneDirty = true;
    RecordHistoryUVE(ComponentPropertyHistoryEntryUVE{entity, entry, std::move(before), std::move(after), selection,
                                                      selection, dirtyBefore, true});
    return true;
}

bool EditorUVE::SetSelectedComponentPropertyUVE(const TypeMetadataEntryUVE& entry,
                                                const TypeMetadataPropertyUVE& property,
                                                const void* const newValue) {
    if (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE() ||
        !property.IsAuthoringWritableUVE() || !entry.HasFactoryUVE() || newValue == nullptr) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE(m_selectedEntity, entry.typeIndex)) {
        return false;
    }
    void* const instance = entityManager.GetComponentPointerUVE(m_selectedEntity, entry.typeIndex);

    // The prior value is cloned before the write, so an undo restores the whole component rather
    // than replaying a reverse edit - the same guarantee the per-type commands give.
    Core::TypeInstanceUVE before = Core::TypeInstanceUVE::CloneUVE(entry, instance);
    if (!before.IsValidUVE()) {
        return false;
    }
    property.setValue(instance, newValue);
    if (entry.isInstanceValid != nullptr && !entry.isInstanceValid(instance)) {
        // The component's own rule refuses this combination; put the prior value back.
        entry.assignInstance(instance, before.GetUVE());
        return false;
    }
    // An edit that changed nothing records no history, matching what the per-type commands do.
    // A property with no equality operator is treated as changed, which over-records rather than
    // silently dropping a real edit.
    if (property.areEqual != nullptr && property.areEqual(before.GetUVE(), instance)) {
        return false;
    }

    Core::TypeInstanceUVE after = Core::TypeInstanceUVE::CloneUVE(entry, instance);
    if (!after.IsValidUVE()) {
        // The write already happened; putting the prior value back leaves the scene consistent
        // rather than mutated with no way to undo it.
        entry.assignInstance(instance, before.GetUVE());
        return false;
    }

    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    m_sceneDirty = true;
    RecordHistoryUVE(ComponentPropertyHistoryEntryUVE{m_selectedEntity, &entry, std::move(before),
                                                      std::move(after), selectionBefore,
                                                      CaptureSelectionSnapshotUVE(), dirtyBefore, true});
    return true;
}

bool EditorUVE::ApplyComponentPropertySnapshotUVE(const Scene::EntityUVE entity,
                                                  const TypeMetadataEntryUVE* const metadata,
                                                  const void* const snapshot) {
    if (metadata == nullptr || snapshot == nullptr || !metadata->HasFactoryUVE() ||
        !IsDocumentEntityUVE(entity)) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE(entity, metadata->typeIndex)) {
        return false;
    }
    void* const instance = entityManager.GetComponentPointerUVE(entity, metadata->typeIndex);
    metadata->assignInstance(instance, snapshot);
    return true;
}

bool EditorUVE::ResetSelectedComponentPropertyUVE(const TypeMetadataEntryUVE& entry,
                                                  const TypeMetadataPropertyUVE& property) {
    if (!entry.HasFactoryUVE() || property.getValue == nullptr) {
        return false;
    }
    const Core::TypeInstanceUVE defaults = Core::TypeInstanceUVE::MakeDefaultUVE(entry);
    if (!defaults.IsValidUVE()) {
        return false;
    }
    // Read the default through the same accessor the live value uses, so the buffer is exactly the
    // right size and type for the write that follows - without this function naming either.
    std::array<std::byte, 64> buffer{};
    if (property.typeId == Scene::kPropertyTypeStringUVE) {
        std::string value;
        property.getValue(defaults.GetUVE(), &value);
        return SetSelectedComponentPropertyUVE(entry, property, &value);
    }
    if (property.typeId == Scene::kPropertyTypeAssetGuidUVE) {
        Asset::AssetGuidUVE value{};
        property.getValue(defaults.GetUVE(), &value);
        return SetSelectedComponentPropertyUVE(entry, property, &value);
    }
    if (property.typeId == Scene::kPropertyTypeEntityUVE) {
        Scene::EntityUVE value = Scene::kInvalidEntityUVE;
        property.getValue(defaults.GetUVE(), &value);
        return SetSelectedComponentPropertyUVE(entry, property, &value);
    }
    // The byte buffer is only sound for trivially copyable values, so the types allowed through it
    // are named rather than assumed. A list or other owning value (the metadata entries) is never
    // reset through here; its custom drawer owns that.
    const bool trivial = !property.enumEntries.empty() || property.typeId == Scene::kPropertyTypeBoolUVE ||
                         property.typeId == Scene::kPropertyTypeFloatUVE ||
                         property.typeId == Scene::kPropertyTypeInt32UVE ||
                         property.typeId == Scene::kPropertyTypeUInt32UVE ||
                         property.typeId == Scene::kPropertyTypeBitMask32UVE ||
                         property.typeId == Scene::kPropertyTypeVector2UVE ||
                         property.typeId == Scene::kPropertyTypeVector3UVE ||
                         property.typeId == Scene::kPropertyTypeColorUVE ||
                         property.typeId == Scene::kPropertyTypeQuaternionUVE;
    if (!trivial) {
        return false;
    }
    property.getValue(defaults.GetUVE(), buffer.data());
    return SetSelectedComponentPropertyUVE(entry, property, buffer.data());
}

} // namespace UVE::Editor
