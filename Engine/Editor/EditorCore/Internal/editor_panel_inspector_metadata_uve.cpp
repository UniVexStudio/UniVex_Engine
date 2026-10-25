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
#include "uve/math/scalar_uve.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <typeindex>
#include <utility>
#include <vector>

#include <imgui.h>

#include "editor_axis_input_uve.h"
#include "editor_centered_combo_uve.h"
#include "editor_color_field_uve.h"
#include "editor_layer_mask_field_uve.h"

#include "uve/asset/asset_guid_uve.h"
#include "uve/asset/i_asset_database_uve.h"
#include "uve/asset/i_project_file_index_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/editor_description_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/objects/3d/camera_3d_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/asset/material_asset_uve.h"
#include "uve/core/engine_project_settings_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/color_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/bone_attachment_3d_uve.h"
#include "uve/objects/3d/lod_group_3d_uve.h"
#include "uve/objects/3d/sky_3d_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"
#include "uve/objects/3d/two_bone_ik_3d_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/morph_target_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/skin_component_uve.h"
#include "uve/component/surface_instance_component_uve.h"
#include "uve/asset/gltf_skeleton_uve.h"
#include "uve/asset/mesh_asset_uve.h"

namespace UVE::Editor {
namespace {

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

[[nodiscard]] std::vector<std::string> FileDrawerExtensionsUVE(std::string_view drawerId);
[[nodiscard]] std::filesystem::path ClampPickerStartDirectoryUVE(const Asset::ProjectFileSnapshotUVE& project,
                                                                 const std::filesystem::path& wanted);

[[nodiscard]] bool IsCustomDrawnUVE(std::string_view typeId) noexcept {
    return std::find_if(kCustomDrawnComponentTypeIdsUVE.cbegin(), kCustomDrawnComponentTypeIdsUVE.cend(),
                        [&typeId](const char* const candidate) { return typeId == candidate; }) !=
           kCustomDrawnComponentTypeIdsUVE.cend();
}

/// A host section that describes its child in its own shape, so the child's section is not drawn
/// under it as well: the same object is never described twice.
///
///   Mesh3D      - its skin, primitive shape and surface override are the mesh; one block draws
///                 all four, so the three no longer appear as folds of their own.
///   Character3D - Layer and Mask are authored as the body's own rows, from the collider's own
///                 declarations, so a character reads as a body rather than as a body plus a
///                 second, longer list of shape settings.
[[nodiscard]] bool HostDrawsChildUVE(const TypeMetadataEntryUVE& host,
                                     const TypeMetadataEntryUVE& child) noexcept {
    if (host.typeId == "component.mesh") {
        return child.typeId == "component.skin" || child.typeId == "component.primitive_mesh" ||
               child.typeId == "component.surface_instance";
    }
    return host.typeId == "component.character_controller" && child.typeId == "component.collider";
}

/// "component.rigid_body" becomes "rigid-body": the stable drawer id the registry orders and the
/// Inspector's search filter matches on. Derived rather than declared so an id can never drift
/// from the type it belongs to.
[[nodiscard]] std::string DrawerIdForTypeIdUVE(std::string_view typeId) {
    constexpr std::string_view prefix = "component.";
    std::string id{typeId.rfind(prefix, 0U) == 0U ? typeId.substr(prefix.size()) : typeId};
    std::replace(id.begin(), id.end(), '_', '-');
    return id;
}

/// A property is shown when nothing hides it: not flagged Hidden, and either unconditional or
/// accepted by its own predicate against the live component.
[[nodiscard]] bool IsPropertyVisibleUVE(const TypeMetadataPropertyUVE& property, const void* instance) {
    if (property.flags.HasUVE(TypeMetadataPropertyFlagsUVE::Hidden)) {
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
constexpr std::array<std::string_view, 12> kBlockPropertyDrawerIdsUVE{
    "animation-parameters",
    "animation-graph",
    "multiline-text",
    "script-slot",
    "script-exports",
    "object-metadata",
    "skeleton-source",
    "skeleton-bones",
    "entity-reference-list",
    "full-body-effectors",
    "lod-group-thresholds",
    "lod-group-meshes",
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
    drawList.PathArcTo(center, radius, Math::kPiUVE * 0.15F, Math::kPiUVE * 1.75F, 16);
    drawList.PathStroke(color, 0, 1.6F);
    // Arrowhead at the arc's start, pointing along the direction of travel.
    const ImVec2 tip{center.x + (radius * std::cos(Math::kPiUVE * 0.15F)), center.y + (radius * std::sin(Math::kPiUVE * 0.15F))};
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
    // type id for a stable result. That is how the properties every object has in common end up
    // below whatever the object itself brings, without this loop knowing which are which.
    std::vector<const TypeMetadataEntryUVE*> entries;
    const Core::TypeMetadataRegistryUVE& registry = Scene::GetSceneComponentMetadataRegistryUVE();
    for (const TypeMetadataEntryUVE& snapshotEntry : registry.GetSnapshotUVE().entries) {
        // The snapshot is a copy; register against the registry's own stable entry, because the
        // drawer callback and any history entry hold a pointer to it.
        const TypeMetadataEntryUVE* const stable = registry.FindTypeUVE(snapshotEntry.typeId);
        if (stable != nullptr && !IsCustomDrawnUVE(stable->typeId.ToStringUVE())) {
            entries.push_back(stable);
        }
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const TypeMetadataEntryUVE* const left, const TypeMetadataEntryUVE* const right) {
                         if (left->order != right->order) {
                             return left->order < right->order;
                         }
                         return left->typeId.ToStringUVE() < right->typeId.ToStringUVE();
                     });

    // A nested type is drawn by the first of its hosts the entity carries, and on its own when it
    // carries none, so it is never unreachable. A host that is hand-drawn or undeclared cannot draw
    // it, so it is left out of the list.
    const auto findHostsUVE = [&entries](const TypeMetadataEntryUVE& entry) {
        std::vector<const TypeMetadataEntryUVE*> hosts;
        for (const Strings::StringIdUVE& hostId : entry.nestedUnderTypeIds) {
            const auto host = std::find_if(entries.cbegin(), entries.cend(),
                                           [&hostId](const TypeMetadataEntryUVE* candidate) {
                                               return candidate->typeId == hostId;
                                           });
            if (host != entries.cend()) {
                hosts.push_back(*host);
            }
        }
        return hosts;
    };

    // Transform is hand-drawn, but it still takes its declared place in the order.
    bool transformRegistered = false;
    for (const TypeMetadataEntryUVE* const entry : entries) {
        if (!transformRegistered && entry->order > Scene::kSectionOrderTransformUVE) {
            RegisterTransformInspectorDrawerUVE();
            transformRegistered = true;
        }
        // Each nested type this entry may draw, with the hosts it prefers over this one: when the
        // entity carries one of those, that host draws it instead.
        std::vector<NestedMetadataSectionUVE> nested;
        for (const TypeMetadataEntryUVE* const candidate : entries) {
            const std::vector<const TypeMetadataEntryUVE*> hosts = findHostsUVE(*candidate);
            const auto self = std::find(hosts.begin(), hosts.end(), entry);
            if (self != hosts.end()) {
                nested.push_back(NestedMetadataSectionUVE{candidate, {hosts.begin(), self}});
            }
        }
        const std::vector<const TypeMetadataEntryUVE*> hosts = findHostsUVE(*entry);
        static_cast<void>(m_inspectorDrawerRegistry.RegisterDrawerUVE(InspectorDrawerEntryUVE{
            DrawerIdForTypeIdUVE(entry->typeId.ToStringUVE()),
            [this, entry, hosts](const Scene::EntityUVE entity) {
                const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
                return IsDocumentEntityUVE(entity) && entityManager.HasComponentUVE(entity, entry->typeIndex) &&
                       std::none_of(hosts.begin(), hosts.end(), [&](const TypeMetadataEntryUVE* const host) {
                           return entityManager.HasComponentUVE(entity, host->typeIndex);
                       });
            },
            [this, entry, nested = std::move(nested)](const Scene::EntityUVE entity) {
                DrawMetadataComponentDrawerUVE(entity, *entry, nested);
            },
        }));
        // One heading per ancestor, most-derived first: the object's own section, then each
        // abstract base, then Object3D, then Object.
        std::string group;
        if (entry->order >= Scene::kSectionOrderObjectCommonUVE) {
            group = "Object";
        } else if (entry->order >= Scene::kSectionOrderTransformUVE) {
            group = "Object3D";
        } else if (entry->order >= Scene::kSectionOrderTypeSpecificUVE) {
            group = entry->displayName;
        }
        if (!group.empty()) {
            static_cast<void>(
                m_inspectorDrawerRegistry.SetDrawerGroupUVE(DrawerIdForTypeIdUVE(entry->typeId.ToStringUVE()), std::move(group)));
        }
    }
    if (!transformRegistered) {
        RegisterTransformInspectorDrawerUVE();
    }
}

void EditorUVE::ApplyPendingComponentChangeUVE() {
    // A structural edit a row asked for last frame runs here, once, before any section takes a
    // pointer into an entity's component storage: adding or removing a component moves the entity
    // into another archetype, which frees the chunk the row that asked was drawn from. The row
    // therefore records the intent and this turns it into the edit - see the field's own comment.
    const PendingComponentChangeUVE change = m_pendingComponentChange;
    const Scene::EntityUVE target = m_pendingComponentChangeEntity;
    m_pendingComponentChange = PendingComponentChangeUVE::None;
    m_pendingComponentChangeEntity = Scene::kInvalidEntityUVE;
    if (change == PendingComponentChangeUVE::None || target == Scene::kInvalidEntityUVE) {
        return;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!IsDocumentEntityUVE(target) || !entityManager.IsAliveUVE(target)) {
        return;
    }
    const bool hasSky = entityManager.HasComponentUVE<Scene::Sky3DComponentUVE>(target);
    if (change == PendingComponentChangeUVE::AddSky && !hasSky) {
        entityManager.AddComponentUVE<Scene::Sky3DComponentUVE>(target, Scene::Sky3DComponentUVE{});
    } else if (change == PendingComponentChangeUVE::RemoveSky && hasSky) {
        entityManager.RemoveComponentUVE<Scene::Sky3DComponentUVE>(target);
    }
}

void EditorUVE::DrawMetadataComponentDrawerUVE(const Scene::EntityUVE entity, const TypeMetadataEntryUVE& entry,
                                               const std::vector<NestedMetadataSectionUVE>& nested) {
    if (!IsDocumentEntityUVE(entity) || entity != m_selectedEntity) {
        return;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE(entity, entry.typeIndex)) {
        return;
    }
    const void* const instance = entityManager.GetComponentPointerUVE(entity, entry.typeIndex);

    ImGui::PushID(entry.typeId.ToCStringUVE());
    if (entry.presentedInline) {
        ImGui::Spacing();
        DrawMetadataPropertyRowsUVE(entry, instance);
        ImGui::PopID();
        return;
    }
    // Collapsible, and the collapsed state is remembered per section type across selections and
    // sessions (DrawInspectorFoldUVE) - which is what makes a long Inspector usable at all.
    // "###" keeps the header's identity on the type, so a title that follows the value does not
    // reset the section's open state when the value changes.
    const std::string sectionTitle{entry.sectionTitle != nullptr ? entry.sectionTitle(instance)
                                                                 : entry.displayName.c_str()};
    const std::string header = sectionTitle + "###" + std::string(entry.typeId.ToStringUVE());
    const bool sectionOpen = DrawInspectorFoldUVE(header.c_str(), "section:" + std::string(entry.typeId.ToStringUVE()), true, true, 0);
    DrawInspectorSectionMenuUVE(&entry, sectionTitle.c_str());
    if (sectionOpen) {
        if (entry.typeId == "component.mesh") {
            DrawMeshInstance3DInspectorUVE(entity);
        } else {
            if (entry.typeIndex == std::type_index(typeid(Scene::CharacterControllerComponentUVE))) {
                DrawCharacterCollisionRowsUVE(entity);
            }
            DrawMetadataPropertyRowsUVE(entry, instance);
        }
        if (entry.typeIndex == std::type_index(typeid(Scene::CameraComponentUVE))) {
            bool preview = GetPreviewCameraUVE() == entity;
            if (ImGui::Checkbox("Preview", &preview)) {
                if (preview) {
                    SetPreviewCameraUVE(entity);
                } else {
                    ClearPreviewCameraUVE();
                }
            }
        }
        for (const NestedMetadataSectionUVE& section : nested) {
            const TypeMetadataEntryUVE* const child = section.entry;
            if (HostDrawsChildUVE(entry, *child)) {
                continue;
            }
            if (!entityManager.HasComponentUVE(entity, child->typeIndex) ||
                std::any_of(section.preferredHosts.begin(), section.preferredHosts.end(),
                            [&](const TypeMetadataEntryUVE* const host) {
                                return entityManager.HasComponentUVE(entity, host->typeIndex);
                            })) {
                continue;
            }
            ImGui::PushID(child->typeId.ToCStringUVE());
            constexpr ImGuiTreeNodeFlags kNestedFlags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                                        ImGuiTreeNodeFlags_FramePadding;
            if (DrawInspectorFoldUVE(child->displayName.c_str(), "nested:" + std::string(child->typeId.ToStringUVE()), true, false,
                                     kNestedFlags)) {
                DrawMetadataPropertyRowsUVE(*child, entityManager.GetComponentPointerUVE(entity, child->typeIndex));
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    ImGui::PopID();
}

void EditorUVE::DrawCharacterCollisionRowsUVE(const Scene::EntityUVE entity) {
    // Layer and Mask live on the collider, but they answer what the object is on and what it looks
    // for - a property of the body, not of the shape it happens to have - so they are authored in
    // the body's section. The rows are the collider's own declarations, drawn through the row
    // drawer the collider section would use, so the layer picker, its tooltip, its revert control
    // and its undo entry are the same ones, and the values still have exactly one home.
    const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const Core::TypeMetadataRegistryUVE& registry = Scene::GetSceneComponentMetadataRegistryUVE();
    const TypeMetadataEntryUVE* const collider =
        registry.FindTypeByIndexUVE(std::type_index(typeid(Scene::ColliderComponentUVE)));
    if (collider == nullptr || !entityManager.HasComponentUVE(entity, collider->typeIndex)) {
        return;
    }
    const void* const instance = entityManager.GetComponentPointerUVE(entity, collider->typeIndex);
    // Resolved before the table opens, so a declaration that hides one of them (or names it
    // differently) leaves no empty rows behind.
    constexpr std::array<const char*, 2> kBodyRowsUVE{"collisionLayer", "collisionMask"};
    std::array<const TypeMetadataPropertyUVE*, kBodyRowsUVE.size()> rows{};
    std::size_t rowCount = 0U;
    for (const char* const name : kBodyRowsUVE) {
        const auto property = std::find_if(collider->properties.cbegin(), collider->properties.cend(),
                                           [name](const TypeMetadataPropertyUVE& candidate) {
                                               return candidate.name == name;
                                           });
        if (property != collider->properties.cend() && IsPropertyVisibleUVE(*property, instance)) {
            rows[rowCount++] = &*property;
        }
    }
    if (rowCount == 0U || !BeginPropertyRowsUVE("##character-collision")) {
        return;
    }
    for (std::size_t index = 0U; index < rowCount; ++index) {
        ImGui::PushID(rows[index]->name.c_str());
        DrawMetadataPropertyRowUVE(*collider, *rows[index], instance);
        ImGui::PopID();
    }
    ImGui::EndTable();
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
            (property.flags.HasUVE(TypeMetadataPropertyFlagsUVE::RuntimeState) && !simulating)) {
            continue;
        }
        if (group == nullptr ? !property.section.empty() : *group != property.section) {
            closeGroup();
            if (!property.section.empty()) {
                group = &property.section;
                constexpr ImGuiTreeNodeFlags kGroupFlags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                                           ImGuiTreeNodeFlags_FramePadding;
                const std::string groupId = property.section + "##group-" + property.section;
                groupOpen = DrawInspectorFoldUVE(groupId.c_str(), "group:" + std::string(entry.typeId.ToStringUVE()) + "/" + property.section,
                                                 false, false, kGroupFlags);
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
        // "Inherit (Running)" says what Inherit means here without a second row to read.
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
        if (BeginCenteredComboUVE("##value", preview.c_str())) {
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
        const bool changed = ImGui::DragFloat("##value", &value, RangeStepUVE(property, 0.01F),
                                              RangeMinimumUVE(property), RangeMaximumUVE(property));
        edited = ApplyContinuousPropertyEditUVE(entry, property, changed, &value) || edited;
    } else if (property.typeId == Scene::kPropertyTypeVector2UVE) {
        Math::Vector2UVE value{};
        property.getValue(instance, &value);
        const bool changed = DrawAxisVectorInputUVE("##value", &value.x, 2, RangeStepUVE(property, 0.01F),
                                                    RangeMinimumUVE(property), RangeMaximumUVE(property));
        edited = ApplyContinuousPropertyEditUVE(entry, property, changed, &value) || edited;
    } else if (property.typeId == Scene::kPropertyTypeVector3UVE) {
        Math::Vector3UVE value{};
        property.getValue(instance, &value);
        const bool changed = DrawAxisVectorInputUVE("##value", &value.x, 3, RangeStepUVE(property, 0.01F),
                                                    RangeMinimumUVE(property), RangeMaximumUVE(property));
        edited = ApplyContinuousPropertyEditUVE(entry, property, changed, &value) || edited;
    } else if (property.typeId == Scene::kPropertyTypeRectUVE) {
        Math::RectUVE value{};
        property.getValue(instance, &value);
        bool changed =
            DrawAxisVectorInputUVE("##rectPosition", &value.position.x, 2, RangeStepUVE(property, 0.01F),
                                 RangeMinimumUVE(property), RangeMaximumUVE(property));
        changed = DrawAxisVectorInputUVE("##rectSize", &value.size.x, 2, RangeStepUVE(property, 0.01F),
                                         RangeMinimumUVE(property), RangeMaximumUVE(property),
                                         kRectSizeAxisNamesUVE) ||
                  changed;
        edited = ApplyContinuousPropertyEditUVE(entry, property, changed, &value) || edited;
    } else if (property.typeId == Scene::kPropertyTypeColorUVE) {
        // Shown live while the picker is open, recorded as one undo step when it closes.
        Math::Vector3UVE value{};
        property.getValue(instance, &value);
        EditorColorUVE color{value.x, value.y, value.z, 1.0F};
        const ColorFieldEventUVE event =
            DrawColorFieldUVE("##value", property.displayName.c_str(), color, false, m_colorPickerPreferences);
        const Math::Vector3UVE picked{color.r, color.g, color.b};
        if (event == ColorFieldEventUVE::Edited) {
            static_cast<void>(PreviewSelectedComponentPropertyUVE(entry, property, &picked));
        } else if (event == ColorFieldEventUVE::Committed) {
            static_cast<void>(PreviewSelectedComponentPropertyUVE(entry, property, &picked));
            edited = CommitComponentPropertyPreviewUVE();
        } else if (event == ColorFieldEventUVE::Cancelled) {
            static_cast<void>(CancelComponentPropertyPreviewUVE());
        }
    } else if (property.typeId == Scene::kPropertyTypeLinearColorUVE) {
        // Linear-stored twin of the Color branch above: the picker edits in display space, so the
        // stored value is converted out for display and the picked value converted back to linear
        // on the way in. Same live-preview/one-undo-step session semantics.
        Math::ColorUVE value{};
        property.getValue(instance, &value);
        const Math::Vector3UVE shown = Math::DisplayFromColorUVE(value);
        EditorColorUVE color{shown.x, shown.y, shown.z, 1.0F};
        const ColorFieldEventUVE event =
            DrawColorFieldUVE("##value", property.displayName.c_str(), color, false, m_colorPickerPreferences);
        const Math::ColorUVE picked =
            Math::ColorFromDisplayUVE(Math::Vector3UVE{color.r, color.g, color.b});
        if (event == ColorFieldEventUVE::Edited) {
            static_cast<void>(PreviewSelectedComponentPropertyUVE(entry, property, &picked));
        } else if (event == ColorFieldEventUVE::Committed) {
            static_cast<void>(PreviewSelectedComponentPropertyUVE(entry, property, &picked));
            edited = CommitComponentPropertyPreviewUVE();
        } else if (event == ColorFieldEventUVE::Cancelled) {
            static_cast<void>(CancelComponentPropertyPreviewUVE());
        }
    } else if (property.typeId == Scene::kPropertyTypeInt32UVE) {
        std::int32_t value = 0;
        property.getValue(instance, &value);
        int shown = value;
        const bool changed = ImGui::DragInt("##value", &shown, RangeStepUVE(property, 1.0F),
                                            property.range.enabled ? static_cast<int>(property.range.minimum)
                                                                   : std::numeric_limits<int>::lowest(),
                                            property.range.enabled ? static_cast<int>(property.range.maximum)
                                                                   : std::numeric_limits<int>::max());
        value = shown;
        edited = ApplyContinuousPropertyEditUVE(entry, property, changed, &value) || edited;
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
        const bool changed = ImGui::DragInt("##value", &shown, RangeStepUVE(property, 1.0F), minimum, maximum);
        value = static_cast<std::uint32_t>(std::max(0, shown));
        edited = ApplyContinuousPropertyEditUVE(entry, property, changed, &value) || edited;
    } else if (property.typeId == Scene::kPropertyTypeUInt8UVE) {
        // Dragged as a signed int (what ImGui offers) and clamped back into a byte, so an author
        // can never wrap a count past 255 or below zero.
        std::uint8_t value = 0U;
        property.getValue(instance, &value);
        int shown = value;
        const int minimum = property.range.enabled ? std::max(0, static_cast<int>(property.range.minimum)) : 0;
        const int maximum = property.range.enabled ? std::min(255, static_cast<int>(property.range.maximum)) : 255;
        const bool changed = ImGui::DragInt("##value", &shown, RangeStepUVE(property, 1.0F), minimum, maximum);
        value = static_cast<std::uint8_t>(std::clamp(shown, 0, 255));
        edited = ApplyContinuousPropertyEditUVE(entry, property, changed, &value) || edited;
    } else if (property.typeId == Scene::kPropertyTypeBitMask32UVE) {
        std::uint32_t value = 0U;
        property.getValue(instance, &value);
        const bool physics = property.customDrawerId == Scene::kLayerMaskDrawerPhysicsUVE;
        if (physics || property.customDrawerId == Scene::kLayerMaskDrawerRenderUVE) {
            // A layer mask, shown by the names the project gives its layers.
            const Core::LayerSetUVE set = physics ? Core::LayerSetUVE::Physics : Core::LayerSetUVE::Render;
            const Config::SettingsDocumentUVE& project = m_services->GetProjectSettingsUVE();
            LayerNamesUVE names;
            for (std::size_t index = 0U; index < names.size(); ++index) {
                names[index] = Core::GetLayerNameUVE(project, set, index);
            }
            switch (DrawLayerMaskFieldUVE("##value", value, names)) {
            case LayerMaskFieldEventUVE::Changed:
                edited = SetSelectedComponentPropertyUVE(entry, property, &value);
                break;
            case LayerMaskFieldEventUVE::EditNames:
                OpenProjectSettingsUVE();
                m_projectSettingsWindow.search.fill('\0');
                m_projectSettingsWindow.category = physics ? "Layers/Physics" : "Layers/Render";
                break;
            case LayerMaskFieldEventUVE::None:
                break;
            }
        } else if (ImGui::InputScalar("##value", ImGuiDataType_U32, &value, nullptr, nullptr, "%08X",
                                      ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
            // Any other mask: authored as bits, in hexadecimal, so 0xFFFFFFFF reads as "all"
            // instead of as 4294967295.
            edited = SetSelectedComponentPropertyUVE(entry, property, &value);
        }
    } else if (property.typeId == Scene::kPropertyTypeStringUVE) {
        const bool isBoneProperty = (property.name == "boneName" || property.name == "rootBoneName" ||
                                     property.name == "middleBoneName" || property.name == "endBoneName" ||
                                     property.customDrawerId == "bone");
        Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
        Scene::EntityUVE skeletonEntity = Scene::kInvalidEntityUVE;
        if (isBoneProperty) {
            if (entityManager.HasComponentUVE<Scene::BoneAttachment3DComponentUVE>(m_selectedEntity)) {
                skeletonEntity = entityManager.GetComponentUVE<Scene::BoneAttachment3DComponentUVE>(m_selectedEntity).skeleton;
            } else if (entityManager.HasComponentUVE<Scene::TwoBoneIK3DComponentUVE>(m_selectedEntity)) {
                skeletonEntity = entityManager.GetComponentUVE<Scene::TwoBoneIK3DComponentUVE>(m_selectedEntity).skeleton;
            } else if (entityManager.HasComponentUVE<Scene::FullBodyIK3DComponentUVE>(m_selectedEntity)) {
                skeletonEntity = entityManager.GetComponentUVE<Scene::FullBodyIK3DComponentUVE>(m_selectedEntity).skeleton;
            }
            if (skeletonEntity == Scene::kInvalidEntityUVE || !entityManager.HasComponentUVE<Scene::Skeleton3DComponentUVE>(skeletonEntity)) {
                Scene::EntityUVE current = m_selectedEntity;
                Scene::EntityUVE parent = Scene::kInvalidEntityUVE;
                while (TryGetDocumentParentUVE(current, parent) && parent != Scene::kInvalidEntityUVE) {
                    if (entityManager.HasComponentUVE<Scene::Skeleton3DComponentUVE>(parent)) {
                        skeletonEntity = parent;
                        break;
                    }
                    current = parent;
                }
            }
            if (skeletonEntity == Scene::kInvalidEntityUVE || !entityManager.HasComponentUVE<Scene::Skeleton3DComponentUVE>(skeletonEntity)) {
                for (const Scene::EntityUVE root : GetDocumentRootsUVE()) {
                    if (entityManager.HasComponentUVE<Scene::Skeleton3DComponentUVE>(root)) {
                        skeletonEntity = root;
                        break;
                    }
                }
            }
        }

        if (isBoneProperty && skeletonEntity != Scene::kInvalidEntityUVE &&
            entityManager.HasComponentUVE<Scene::Skeleton3DComponentUVE>(skeletonEntity) &&
            !entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(skeletonEntity).bones.empty()) {
            const Scene::Skeleton3DComponentUVE& skel =
                entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(skeletonEntity);
            std::string value;
            property.getValue(instance, &value);
            const std::string preview = value.empty() ? std::string{"Empty"} : value;
            if (BeginCenteredComboUVE("##value", preview.c_str())) {
                if (ImGui::Selectable("Empty", value.empty())) {
                    std::string emptyName{};
                    edited = SetSelectedComponentPropertyUVE(entry, property, &emptyName);
                }
                ImGui::Separator();
                for (std::size_t boneIdx = 0U; boneIdx < skel.bones.size(); ++boneIdx) {
                    const Scene::SkeletonBoneUVE& bone = skel.bones[boneIdx];
                    const bool isSelected = (bone.name == value);
                    const std::string boneLabel = std::to_string(boneIdx) + ": " + bone.name + "##b_" + std::to_string(boneIdx);
                    if (ImGui::Selectable(boneLabel.c_str(), isSelected)) {
                        edited = SetSelectedComponentPropertyUVE(entry, property, &bone.name);
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        } else {
            const auto isMaterialProperty = [](const TypeMetadataPropertyUVE& prop) {
                if (prop.customDrawerId == "file:uvmat" || prop.customDrawerId == "file:uvmaterial" ||
                    prop.customDrawerId == "asset:uvmat" || prop.customDrawerId == "asset:uvmaterial" ||
                    prop.name == "materialOverridePath" || prop.name == "materialOverlayPath" ||
                    prop.name == "materialGuid") {
                    return true;
                }
                const auto containsMaterial = [](const std::string_view s) {
                    if (s.empty()) return false;
                    auto it = std::search(s.begin(), s.end(), "material", "material" + 8,
                        [](char ch1, char ch2) { return std::tolower(static_cast<unsigned char>(ch1)) == std::tolower(static_cast<unsigned char>(ch2)); });
                    return it != s.end();
                };
                return containsMaterial(prop.name) || containsMaterial(prop.displayName);
            };
            const bool isMaterialRow = isMaterialProperty(property);
            if (isMaterialRow) {
                std::string value;
                property.getValue(instance, &value);
                if (DrawStandardMaterialSlotUVE(nullptr, "##value", value)) {
                    edited = SetSelectedComponentPropertyUVE(entry, property, &value);
                }
            } else if (property.customDrawerId == "sky:asset") {
                // The environment's sky, as one slot. It reads Empty while the level has none, and
                // every way of giving it one starts here: New Sky puts a Sky on this environment -
                // whose bands, atmosphere, stars and clouds then draw as the section right below
                // this one - and Sky Texture samples a panorama instead of them. Both live in this
                // row because they answer the same question, and because the Sky's own fields win
                // over the environment's once it exists: a texture field left behind on the
                // environment would be a setting that quietly stopped doing anything.
                const std::uint32_t widget = ImGui::GetID("##value");
                Scene::IEntityManagerUVE& skyManager = m_services->GetEntityManagerUVE();
                const bool hasSky = IsDocumentEntityUVE(m_selectedEntity) &&
                                    skyManager.HasComponentUVE<Scene::Sky3DComponentUVE>(m_selectedEntity);
                Scene::Sky3DComponentUVE* const sky =
                    hasSky ? &skyManager.GetComponentUVE<Scene::Sky3DComponentUVE>(m_selectedEntity) : nullptr;
                // A browse that landed since last frame goes to whichever of the two holds the
                // picture now: the Sky's own field when there is a Sky, the environment's otherwise.
                if (const auto picked = m_fileRowPendingPicks.find(widget); picked != m_fileRowPendingPicks.end()) {
                    if (sky != nullptr) {
                        sky->source = Scene::SkySourceUVE::Panorama;
                        sky->panoramaPath = picked->second;
                    } else {
                        edited = SetSelectedComponentPropertyUVE(entry, property, &picked->second);
                    }
                    m_fileRowPendingPicks.erase(picked);
                }
                std::string value;
                property.getValue(instance, &value);
                const auto sourceName = [](const Scene::SkySourceUVE source) -> const char* {
                    switch (source) {
                        case Scene::SkySourceUVE::Panorama: return "Panorama";
                        case Scene::SkySourceUVE::Atmosphere: return "Atmosphere";
                        case Scene::SkySourceUVE::Gradient: break;
                    }
                    return "Gradient";
                };
                // What the slot says is what the level's sky is, not which field happens to hold it.
                std::string preview;
                if (sky != nullptr) {
                    preview = (sky->source == Scene::SkySourceUVE::Panorama && !sky->panoramaPath.empty())
                                  ? std::filesystem::path{sky->panoramaPath}.stem().string()
                                  : std::string{sourceName(sky->source)};
                } else {
                    preview = value.empty() ? std::string{"Empty"} : std::filesystem::path{value}.stem().string();
                }
                if (BeginCenteredComboUVE("##value", preview.c_str())) {
                    if (sky != nullptr) {
                        if (ImGui::Selectable("Remove Sky")) {
                            m_pendingComponentChange = PendingComponentChangeUVE::RemoveSky;
                            m_pendingComponentChangeEntity = m_selectedEntity;
                        }
                    } else if (ImGui::Selectable("New Sky")) {
                        m_pendingComponentChange = PendingComponentChangeUVE::AddSky;
                        m_pendingComponentChangeEntity = m_selectedEntity;
                    }
                    ImGui::Separator();
                    if (ImGui::Selectable("Sky Texture...")) {
                        FilePickerRequestUVE request;
                        request.mode = FilePickerModeUVE::Open;
                        request.title = "Sky Texture";
                        // Either form: a published .uvtex or the source image, which the texture
                        // loader decodes itself - the panorama does not have to be imported first.
                        request.extensions = {".uvtex", ".png", ".jpg", ".jpeg", ".bmp", ".tga"};
                        const Asset::ProjectFileSnapshotUVE project =
                            m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
                        const std::filesystem::path current =
                            (sky != nullptr && !sky->panoramaPath.empty()) ? std::filesystem::path{sky->panoramaPath}
                                                                          : std::filesystem::path{value};
                        request.startDirectory = ClampPickerStartDirectoryUVE(project, current.parent_path());
                        request.onPick = [this, widget](const std::filesystem::path& absolute) {
                            const Asset::ProjectFileSnapshotUVE currentProject =
                                m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
                            const std::filesystem::path relative =
                                absolute.lexically_relative(currentProject.contentRoot);
                            m_fileRowPendingPicks[widget] = (relative.empty() || *relative.begin() == "..")
                                                                ? absolute.generic_string()
                                                                : relative.generic_string();
                        };
                        OpenFilePickerUVE(std::move(request));
                        ImGui::CloseCurrentPopup();
                    }
                    const bool empty = (sky != nullptr) ? (sky->source != Scene::SkySourceUVE::Panorama || sky->panoramaPath.empty())
                                                        : value.empty();
                    if (ImGui::Selectable("Empty", empty) && !empty) {
                        if (sky != nullptr) {
                            // No image: the bands are what a Panorama with nothing to sample
                            // would have to fall back to, so say so instead of leaving a sky the
                            // renderer would skip.
                            sky->panoramaPath.clear();
                            sky->source = Scene::SkySourceUVE::Gradient;
                        } else {
                            std::string cleared;
                            edited = SetSelectedComponentPropertyUVE(entry, property, &cleared);
                        }
                    }
                    ImGui::EndCombo();
                }
            } else {
                // A "file:ext,..." row also browses: the text stays editable, and Browse opens the
                // floating picker on the file kinds the property declares.
                constexpr std::string_view kFilePrefix = "file:";
                const bool isFileRow = property.customDrawerId.rfind(kFilePrefix, 0U) == 0U;
                if (isFileRow) {
                    // A browse that landed since last frame commits like a typed edit, before the field
                    // is drawn, so the new path shows at once.
                    const std::uint32_t widget = ImGui::GetID("##value");
                    if (const auto found = m_fileRowPendingPicks.find(widget); found != m_fileRowPendingPicks.end()) {
                        edited = SetSelectedComponentPropertyUVE(entry, property, &found->second);
                        m_fileRowPendingPicks.erase(found);
                    }
                }
                std::string value;
                property.getValue(instance, &value);
                // Committed once when the author finishes, so an edit is one history entry and clicking
                // away keeps it. The bound is the one this row always had.
                constexpr std::size_t kMaximumGenericStringBytesUVE = 255U;
                if (isFileRow) {
                    ImGui::SetNextItemWidth(-80.0F);
                }
                if (const std::optional<std::string> committed =
                        DrawCommittedTextInputUVE("##value", value, false, 0.0F, kMaximumGenericStringBytesUVE);
                    committed.has_value()) {
                    edited = SetSelectedComponentPropertyUVE(entry, property, &*committed);
                }
                if (isFileRow) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Browse...")) {
                        FilePickerRequestUVE request;
                        request.mode = FilePickerModeUVE::Open;
                        request.title = property.displayName.empty() ? "Pick File" : property.displayName;
                        request.extensions = FileDrawerExtensionsUVE(property.customDrawerId);
                        const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
                        request.startDirectory =
                            ClampPickerStartDirectoryUVE(project, std::filesystem::path{value}.parent_path());
                        const std::uint32_t widget = ImGui::GetID("##value");
                        request.onPick = [this, widget](const std::filesystem::path& absolute) {
                            // Applies to the selection, re-resolved: whatever is selected when the pick
                            // lands receives it.
                            const Asset::ProjectFileSnapshotUVE current =
                                m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
                            const std::filesystem::path relative = absolute.lexically_relative(current.contentRoot);
                            m_fileRowPendingPicks[widget] = (relative.empty() || *relative.begin() == "..")
                                                                ? absolute.generic_string()
                                                                : relative.generic_string();
                        };
                        OpenFilePickerUVE(std::move(request));
                    }
                }
            }
        }
    } else if (property.typeId == Scene::kPropertyTypeAssetGuidUVE) {
        // A picker over the registered assets of the kind the property declares
        // ("asset:uvmodel"). Picking, never typing: a typed 64-bit number is a way to author a
        // dangling reference. A property that declares no kind is shown read-only.
        Asset::AssetGuidUVE value{};
        property.getValue(instance, &value);
        constexpr std::string_view kAssetPrefix = "asset:";
        const auto isMaterialProperty = [](const TypeMetadataPropertyUVE& prop) {
            if (prop.customDrawerId == "file:uvmat" || prop.customDrawerId == "file:uvmaterial" ||
                prop.customDrawerId == "asset:uvmat" || prop.customDrawerId == "asset:uvmaterial" ||
                prop.name == "materialOverridePath" || prop.name == "materialOverlayPath" ||
                prop.name == "materialGuid") {
                return true;
            }
            const auto containsMaterial = [](const std::string_view s) {
                if (s.empty()) return false;
                auto it = std::search(s.begin(), s.end(), "material", "material" + 8,
                    [](char ch1, char ch2) { return std::tolower(static_cast<unsigned char>(ch1)) == std::tolower(static_cast<unsigned char>(ch2)); });
                return it != s.end();
            };
            return containsMaterial(prop.name) || containsMaterial(prop.displayName);
        };
        const bool isMaterialGuid = isMaterialProperty(property);
        if (isMaterialGuid) {
            if (DrawStandardMaterialSlotUVE(nullptr, "##value", value)) {
                edited = SetSelectedComponentPropertyUVE(entry, property, &value);
            }
        } else if (property.customDrawerId.rfind(kAssetPrefix, 0U) != 0U) {
            ImGui::BeginDisabled();
            ImGui::Text("%016llX", static_cast<unsigned long long>(value.value));
            ImGui::EndDisabled();
        } else {
            const std::string extension = "." + property.customDrawerId.substr(kAssetPrefix.size());
            if (const std::optional<Asset::AssetGuidUVE> chosen =
                    DrawAssetPickerUVE("##value", value, extension, property.displayName)) {
                edited = SetSelectedComponentPropertyUVE(entry, property, &*chosen);
            }
        }
    } else if (property.typeId == Scene::kPropertyTypeEntityUVE) {
        // Same reasoning as an asset guid: an entity reference is picked, not typed. The choices
        // come from GetEntityReferenceCandidatesUVE so this picker and the reference list below
        // offer exactly the same objects.
        Scene::EntityUVE value = Scene::kInvalidEntityUVE;
        property.getValue(instance, &value);
        const bool dangling = value != Scene::kInvalidEntityUVE && !IsDocumentEntityUVE(value);
        const std::string preview = value == Scene::kInvalidEntityUVE ? std::string{"(default)"}
                                    : dangling                        ? std::string{"(missing object)"}
                                                                      : GetEntityDisplayLabelUVE(value);
        if (BeginCenteredComboUVE("##value", preview.c_str())) {
            if (ImGui::Selectable("(default)", value == Scene::kInvalidEntityUVE) &&
                value != Scene::kInvalidEntityUVE) {
                const Scene::EntityUVE none = Scene::kInvalidEntityUVE;
                edited = SetSelectedComponentPropertyUVE(entry, property, &none);
            }
            if (ImGui::IsItemHovered() && !property.tooltip.empty()) {
                ImGui::SetTooltip("%s", property.tooltip.c_str());
            }
            for (const Scene::EntityUVE candidate : GetEntityReferenceCandidatesUVE()) {
                const bool isSelected = candidate == value;
                const std::string label = GetEntityDisplayLabelUVE(candidate) + "##" +
                                          std::to_string(candidate.index) + "_" +
                                          std::to_string(candidate.generation);
                if (ImGui::Selectable(label.c_str(), isSelected) && !isSelected) {
                    edited = SetSelectedComponentPropertyUVE(entry, property, &candidate);
                }
            }
            ImGui::EndCombo();
        }
    } else {
        // A value type nothing here knows how to draw. Saying so is better than drawing something
        // that looks editable and silently is not.
        ImGui::TextDisabled("No editor for type \"%s\".", property.typeId.ToCStringUVE());
    }
    ImGui::EndDisabled();
    static_cast<void>(edited);
}

namespace {

/// The picker's start folder for `clip`: the folder holding the clip file, or Content/Animations
/// when it is unknown (Content itself when even that is missing). Found through the snapshot's
/// registered GUIDs, never by guessing path spellings.
[[nodiscard]] std::filesystem::path PickerStartDirectoryUVE(const Asset::ProjectFileSnapshotUVE& project,
                                                            Asset::AssetGuidUVE clip) {
    if (clip != Asset::AssetGuidUVE{}) {
        for (const Asset::ProjectFileEntryUVE& entry : project.entries) {
            if (entry.registeredAssetGuid.has_value() && *entry.registeredAssetGuid == clip) {
                return entry.relativePath.parent_path();
            }
        }
    }
    const std::filesystem::path fallback{"Animations"};
    for (const Asset::ProjectFileEntryUVE& entry : project.entries) {
        if (entry.kind == Asset::ProjectFileEntryKindUVE::Directory && entry.relativePath == fallback) {
            return fallback;
        }
    }
    return std::filesystem::path{};
}

/// The raw images a texture slot may name when nothing has imported them yet. The texture loader
/// decodes each of these at read time, so a slot that holds one still draws the picture.
[[nodiscard]] bool IsTextureSourceImagePathUVE(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" ||
           extension == ".tga";
}

/// The published form of a source image: `<stem>.uvtex` beside it. When it exists the picker offers
/// that one row instead of both, because both are the same picture and the published file is the
/// reference a material should hold.
[[nodiscard]] bool HasPublishedTextureSiblingUVE(const std::filesystem::path& absoluteSource) {
    std::error_code error;
    return std::filesystem::is_regular_file(
        absoluteSource.parent_path() / (absoluteSource.stem().string() + ".uvtex"), error);
}

/// "file:uvaudio,wav" -> {".uvaudio", ".wav"}.
[[nodiscard]] std::vector<std::string> FileDrawerExtensionsUVE(std::string_view drawerId) {
    constexpr std::string_view kPrefix = "file:";
    if (drawerId.size() <= kPrefix.size()) {
        return {};
    }
    std::vector<std::string> extensions;
    std::string_view rest = drawerId.substr(kPrefix.size());
    while (!rest.empty()) {
        const std::size_t comma = rest.find(',');
        extensions.push_back("." + std::string{rest.substr(0U, comma)});
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1U);
    }
    return extensions;
}

/// `wanted` when it is Content itself or a folder the snapshot knows, else Content itself.
[[nodiscard]] std::filesystem::path ClampPickerStartDirectoryUVE(const Asset::ProjectFileSnapshotUVE& project,
                                                                 const std::filesystem::path& wanted) {
    if (wanted.empty()) {
        return std::filesystem::path{};
    }
    for (const Asset::ProjectFileEntryUVE& entry : project.entries) {
        if (entry.kind == Asset::ProjectFileEntryKindUVE::Directory && entry.relativePath == wanted) {
            return wanted;
        }
    }
    return std::filesystem::path{};
}

} // namespace

std::optional<Asset::AssetGuidUVE> EditorUVE::DrawAssetPickerUVE(const char* const id, const Asset::AssetGuidUVE value,
                                                                const std::string& extension,
                                                                const std::string& title) {
    static_assert(sizeof(std::uint32_t) == sizeof(ImGuiID), "pending picks are keyed by widget id");
    std::optional<Asset::AssetGuidUVE> picked;
    // A Browse pick lands here on the next frame, under this widget's own id.
    const std::uint32_t widget = ImGui::GetID(id);
    if (const auto found = m_assetPickerPendingPicks.find(widget); found != m_assetPickerPendingPicks.end()) {
        picked = found->second;
        m_assetPickerPendingPicks.erase(found);
    }
    const Asset::IAssetDatabaseUVE& database = m_services->GetAssetDatabaseUVE();
    std::string preview = "Empty";
    if (value != Asset::kInvalidAssetGuidUVE) {
        const std::filesystem::path path = database.ResolveUVE(value);
        preview = path.empty() ? "(missing asset)" : path.stem().string();
    }
    const bool isMaterial = (extension == ".uvmaterial" || extension == ".uvmat");
    // A texture reference names either the published .uvtex or the source image it came from, and
    // the runtime reads both (see LoadTextureAssetSourceUVE), so the picker offers both. Offering
    // only the sources is how a slot ended up pointing at a file the renderer could not open.
    const bool isTexture = (extension == ".uvtex" || extension == ".png" || extension == ".jpg" ||
                            extension == ".jpeg" || extension == ".bmp" || extension == ".tga");
    const bool isModel = (extension == ".uvmodel" || extension == ".glb" || extension == ".gltf" || extension == ".fbx" || extension == ".obj");
    const bool isShader = (extension == ".uvshader" || extension == ".glsl");
    const auto matchesExt = [&](const std::string& ext) {
        if (isMaterial) return ext == ".uvmaterial" || ext == ".uvmat";
        if (isTexture) return ext == ".uvtex" || ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
                              ext == ".tga";
        if (isModel) return ext == ".uvmodel" || ext == ".glb" || ext == ".gltf" || ext == ".fbx" || ext == ".obj";
        if (isShader) return ext == ".uvshader" || ext == ".glsl";
        return ext == extension;
    };

    if (BeginCenteredComboUVE(id, preview.c_str())) {
        if (isMaterial) {
            ImGui::TextDisabled("CREATE MATERIAL");
            if (ImGui::Selectable("New Standard Material")) {
                const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
                const std::filesystem::path matDir = project.contentRoot / "Materials";
                std::error_code ec;
                std::filesystem::create_directories(matDir, ec);
                const std::filesystem::path matPath = MakeUniqueContentPathUVE(matDir, "M_Standard", ".uvmaterial");
                Asset::MaterialAssetUVE mat{};
                mat.kind = Asset::MaterialKindUVE::Standard;
                if (Asset::SaveMaterialAssetUVE(mat, matPath)) {
                    picked = m_services->GetAssetDatabaseUVE().RegisterUVE(matPath);
                    RefreshProjectFileIndexUVE();
                }
            }
            if (ImGui::Selectable("New ORM Material")) {
                const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
                const std::filesystem::path matDir = project.contentRoot / "Materials";
                std::error_code ec;
                std::filesystem::create_directories(matDir, ec);
                const std::filesystem::path matPath = MakeUniqueContentPathUVE(matDir, "M_ORM", ".uvmaterial");
                Asset::MaterialAssetUVE mat{};
                mat.kind = Asset::MaterialKindUVE::Orm;
                mat.roughness = 0.5F;
                mat.metallic = 1.0F;
                mat.occlusionStrength = 1.0F;
                if (Asset::SaveMaterialAssetUVE(mat, matPath)) {
                    picked = m_services->GetAssetDatabaseUVE().RegisterUVE(matPath);
                    RefreshProjectFileIndexUVE();
                }
            }
            if (ImGui::Selectable("New Shader Material")) {
                const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
                const std::filesystem::path matDir = project.contentRoot / "Materials";
                std::error_code ec;
                std::filesystem::create_directories(matDir, ec);
                const std::filesystem::path matPath = MakeUniqueContentPathUVE(matDir, "M_CustomShader", ".uvmaterial");
                Asset::MaterialAssetUVE mat{};
                mat.kind = Asset::MaterialKindUVE::Shader;
                mat.useCustomShaderCode = true;
                if (Asset::SaveMaterialAssetUVE(mat, matPath)) {
                    picked = m_services->GetAssetDatabaseUVE().RegisterUVE(matPath);
                    RefreshProjectFileIndexUVE();
                }
            }
            ImGui::Separator();
        }
        if (ImGui::Selectable("Browse...")) {
            FilePickerRequestUVE request;
            request.mode = FilePickerModeUVE::Open;
            request.title = title.empty() ? (isMaterial ? "Browse Material" : ("Browse " + extension)) : title;
            request.extensions = isMaterial ? std::vector<std::string>{".uvmaterial", ".uvmat"}
                               : isTexture ? std::vector<std::string>{".uvtex", ".png", ".jpg", ".jpeg", ".bmp", ".tga"}
                               : isModel ? std::vector<std::string>{".glb", ".gltf", ".fbx", ".obj", ".uvmodel"}
                               : isShader ? std::vector<std::string>{".uvshader", ".glsl"}
                               : std::vector<std::string>{extension};
            const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
            request.startDirectory = PickerStartDirectoryUVE(project, value);
            request.onPick = [this, widget](const std::filesystem::path& absolute) {
                m_assetPickerPendingPicks[widget] = m_services->GetAssetDatabaseUVE().RegisterUVE(absolute);
            };
            OpenFilePickerUVE(std::move(request));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Selectable("Quick Import...")) {
            FilePickerRequestUVE request;
            request.mode = FilePickerModeUVE::Open;
            request.title = "Quick Import";
            request.extensions = isMaterial ? std::vector<std::string>{".uvmaterial", ".uvmat", ".mtl", ".png", ".jpg", ".jpeg", ".tga"}
                                 : isTexture ? std::vector<std::string>{".png", ".jpg", ".jpeg", ".bmp", ".tga"}
                                 : isModel ? std::vector<std::string>{".fbx", ".glb", ".gltf", ".obj"}
                                 : isShader ? std::vector<std::string>{".uvshader", ".glsl"}
                                 : std::vector<std::string>{extension};
            const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
            request.startDirectory = project.contentRoot;
            request.onPick = [this, widget, isMaterial](const std::filesystem::path& absolute) {
                const auto guid = m_services->GetAssetDatabaseUVE().RegisterUVE(absolute);
                RefreshProjectFileIndexUVE();
                if (isMaterial && (absolute.extension() == ".uvmaterial" || absolute.extension() == ".uvmat")) {
                    m_assetPickerPendingPicks[widget] = guid;
                }
            };
            OpenFilePickerUVE(std::move(request));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Selectable("Empty", value == Asset::kInvalidAssetGuidUVE) &&
            value != Asset::kInvalidAssetGuidUVE) {
            picked = Asset::kInvalidAssetGuidUVE;
        }
        ImGui::Separator();
        ImGui::TextDisabled(isMaterial ? "PROJECT MATERIALS" : "PROJECT ASSETS");
        // Registered assets plus every project file of the kind, so a model imported in an
        // earlier session is offered too; picking an unregistered file registers it.
        struct CandidateUVE {
            std::filesystem::path path;
            std::optional<Asset::AssetGuidUVE> guid;
        };
        std::vector<CandidateUVE> candidates;
        for (const Asset::AssetRecordUVE& record : database.GetRegisteredAssetsUVE()) {
            if (matchesExt(record.path.extension().string())) {
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
            if (isModel && IsModelSourcePathUVE(file.relativePath)) {
                path = GetImportedModelPathUVE(file.relativePath);
                std::error_code error;
                if (!std::filesystem::is_regular_file(path, error)) {
                    continue;
                }
            } else if (isTexture && IsTextureSourceImagePathUVE(file.relativePath) &&
                       HasPublishedTextureSiblingUVE(project.contentRoot / file.relativePath)) {
                // Two rows for one picture would read as two textures. The .uvtex is offered on its
                // own row, so the source stands down.
                continue;
            } else if (matchesExt(file.relativePath.extension().string())) {
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
                picked = chosen;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", candidate.path.generic_string().c_str());
            }
        }
        if (!any) {
            ImGui::TextDisabled("No %s assets yet. Import one from the Content Browser.", isMaterial ? "material" : extension.c_str());
        }
        ImGui::EndCombo();
    }
    return picked;
}

void EditorUVE::DrawMaterialPbrEditorInlineUVE(const std::filesystem::path& absolutePath, const char* const idPrefix) {
    std::error_code ec;
    if (absolutePath.empty() || !std::filesystem::is_regular_file(absolutePath, ec)) {
        return;
    }
    Asset::MaterialAssetUVE mat{};
    if (!Asset::LoadMaterialAssetUVE(absolutePath, mat)) {
        return;
    }

    ImGui::PushID(idPrefix);
    const std::string foldLabel = "Material Properties (" + absolutePath.stem().string() + ")";
    const std::string foldKey = "mat_inline:" + absolutePath.generic_string();
    constexpr ImGuiTreeNodeFlags kFlags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
    if (DrawInspectorFoldUVE(foldLabel.c_str(), foldKey.c_str(), false, false, kFlags)) {
        bool changed = false;

        // 0. Kind. A preset, not a separate shading path: picking one applies the same defaults the
        // Content Browser's "New ... Material" entries use and leaves every texture reference
        // alone, so switching back and forth never loses an author's maps.
        ImGui::TextDisabled("KIND");
        int kindIndex = static_cast<int>(mat.kind);
        constexpr const char* kKindLabels[] = {"Standard", "ORM", "Custom Shader"};
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::Combo("##materialKind", &kindIndex, kKindLabels, 3)) {
            mat.kind = static_cast<Asset::MaterialKindUVE>(kindIndex);
            switch (mat.kind) {
                case Asset::MaterialKindUVE::Standard:
                    mat.useCustomShaderCode = false;
                    break;
                case Asset::MaterialKindUVE::Orm:
                    mat.useCustomShaderCode = false;
                    // The packed map is the point of ORM: a fully metallic surface whose
                    // roughness, metallic and occlusion all come from the texture.
                    mat.metallic = 1.0F;
                    mat.occlusionStrength = 1.0F;
                    break;
                case Asset::MaterialKindUVE::Shader:
                    mat.useCustomShaderCode = true;
                    break;
            }
            changed = true;
        }
        const char* kindHint = "Metallic and roughness scalars, scaled by the ORM map when one is set.";
        switch (mat.kind) {
            case Asset::MaterialKindUVE::Standard:
                break;
            case Asset::MaterialKindUVE::Orm:
                kindHint = "The packed ORM map drives occlusion, roughness and metallic; the scalars multiply it.";
                break;
            case Asset::MaterialKindUVE::Shader:
                kindHint = "Shades with the Shader Asset below; the inputs still describe its surface.";
                break;
        }
        ImGui::TextDisabled("%s", kindHint);

        // 1. Albedo
        ImGui::TextDisabled("ALBEDO");
        // Through the editor's one colour field, like every other colour row: the plain ImGui row
        // this replaced opened a square picker, so the same engine showed two different ones.
        EditorColorUVE albedoColor{mat.albedoColor.r, mat.albedoColor.g, mat.albedoColor.b, 1.0F};
        if (DrawColorFieldUVE("##albedoColor", "Albedo Color", albedoColor, false, m_colorPickerPreferences) !=
            ColorFieldEventUVE::None) {
            mat.albedoColor = Math::ColorUVE{albedoColor.r, albedoColor.g, albedoColor.b};
            changed = true;
        }
        if (const auto picked = DrawAssetPickerUVE("##albedoTex", mat.albedoTexture, ".png", "Pick Albedo Texture")) {
            mat.albedoTexture = *picked;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Albedo Texture");

        // 2. Normal Map & Scale
        ImGui::Spacing();
        ImGui::TextDisabled("NORMAL");
        if (const auto picked = DrawAssetPickerUVE("##normalTex", mat.normalTexture, ".png", "Pick Normal Texture")) {
            mat.normalTexture = *picked;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Normal Map");
        if (ImGui::SliderFloat("Normal Scale", &mat.normalScale, 0.0F, 4.0F)) {
            changed = true;
        }

        // 3. Roughness & Metallic & ORM Map
        ImGui::Spacing();
        ImGui::TextDisabled("SURFACE / PBR");
        if (ImGui::SliderFloat("Roughness", &mat.roughness, 0.0F, 1.0F)) {
            changed = true;
        }
        if (const auto picked = DrawAssetPickerUVE("##roughnessTex", mat.roughnessTexture, ".png", "Pick Roughness Texture")) {
            mat.roughnessTexture = *picked;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Roughness Map");

        if (ImGui::SliderFloat("Metallic", &mat.metallic, 0.0F, 1.0F)) {
            changed = true;
        }
        if (const auto picked = DrawAssetPickerUVE("##metallicTex", mat.metallicTexture, ".png", "Pick Metallic Texture")) {
            mat.metallicTexture = *picked;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Metallic Map");

        if (const auto picked = DrawAssetPickerUVE("##ormTex", mat.metallicRoughnessTexture, ".png", "Pick ORM Texture")) {
            mat.metallicRoughnessTexture = *picked;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("ORM Map (AO/Rough/Metal)");
        if (ImGui::SliderFloat("Occlusion Strength", &mat.occlusionStrength, 0.0F, 2.0F)) {
            changed = true;
        }

        // 4. Emission
        ImGui::Spacing();
        ImGui::TextDisabled("EMISSION");
        EditorColorUVE emissiveColor{mat.emissiveColor.r, mat.emissiveColor.g, mat.emissiveColor.b, 1.0F};
        if (DrawColorFieldUVE("##emissiveColor", "Emission Color", emissiveColor, false,
                              m_colorPickerPreferences) != ColorFieldEventUVE::None) {
            mat.emissiveColor = Math::ColorUVE{emissiveColor.r, emissiveColor.g, emissiveColor.b};
            changed = true;
        }
        if (ImGui::DragFloat("Emission Energy", &mat.emissiveEnergy, 0.1F, 0.0F, 100.0F)) {
            changed = true;
        }
        if (const auto picked = DrawAssetPickerUVE("##emissiveTex", mat.emissiveTexture, ".png", "Pick Emission Texture")) {
            mat.emissiveTexture = *picked;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Emission Texture");

        // 4b. Rendering state. These were the material fields with no authoring surface at all -
        // serialized, defaulted, and unreachable - while the renderer derived every one of them
        // from a single isTransparent flag. They are what decides whether a surface composites,
        // whether it is two-sided, and whether it writes depth, so they belong next to the maps
        // rather than behind a preset.
        ImGui::Spacing();
        ImGui::TextDisabled("RENDERING");
        int blendIndex = static_cast<int>(mat.blendMode);
        constexpr const char* kBlendLabels[] = {"Opaque", "Alpha Blend", "Premultiplied Alpha",
                                                "Additive", "Subtractive", "Multiply"};
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::Combo("##materialBlend", &blendIndex, kBlendLabels, 6)) {
            mat.blendMode = static_cast<Asset::MaterialBlendModeUVE>(blendIndex);
            // Kept in step so the two spellings of "translucent" can never disagree: the legacy
            // flag is what old files and the mesh renderer's own queue still read.
            mat.isTransparent = Asset::IsMaterialBlendModeTranslucentUVE(mat.blendMode);
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Blend Mode");

        int cullIndex = static_cast<int>(mat.cullMode);
        constexpr const char* kCullLabels[] = {"Back Faces", "Front Faces", "Two-Sided"};
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::Combo("##materialCull", &cullIndex, kCullLabels, 3)) {
            mat.cullMode = static_cast<Asset::MaterialCullModeUVE>(cullIndex);
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Culling");

        int depthIndex = static_cast<int>(mat.depthDrawMode);
        constexpr const char* kDepthLabels[] = {"Opaque Only", "Always", "Never", "Depth Pre-Pass"};
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::Combo("##materialDepthDraw", &depthIndex, kDepthLabels, 4)) {
            mat.depthDrawMode = static_cast<Asset::MaterialDepthDrawModeUVE>(depthIndex);
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Depth Draw");

        if (ImGui::SliderFloat("Albedo Alpha", &mat.albedoAlpha, 0.0F, 1.0F)) {
            changed = true;
        }
        int alphaSourceIndex = static_cast<int>(mat.alphaSource);
        constexpr const char* kAlphaSourceLabels[] = {"Albedo Alpha", "Separate Channel", "None"};
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::Combo("##materialAlphaSource", &alphaSourceIndex, kAlphaSourceLabels, 3)) {
            mat.alphaSource = static_cast<Asset::MaterialAlphaSourceUVE>(alphaSourceIndex);
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Alpha Source");

        // 5. Custom Shader
        ImGui::Spacing();
        ImGui::TextDisabled("CUSTOM SHADER");
        if (ImGui::Checkbox("Use Custom Shader", &mat.useCustomShaderCode)) {
            // The Kind selector above is the visible summary of this switch; flipping it directly
            // must not leave the summary claiming a preset the material no longer matches.
            if (!mat.useCustomShaderCode && mat.kind == Asset::MaterialKindUVE::Shader) {
                mat.kind = Asset::MaterialKindUVE::Standard;
            } else if (mat.useCustomShaderCode) {
                mat.kind = Asset::MaterialKindUVE::Shader;
            }
            changed = true;
        }
        if (mat.useCustomShaderCode) {
            if (const auto picked = DrawAssetPickerUVE("##customShaderAsset", mat.customShader, ".uvshader", "Pick Shader Asset")) {
                mat.customShader = *picked;
                changed = true;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted("Shader Asset");
        }

        if (changed) {
            static_cast<void>(Asset::SaveMaterialAssetUVE(mat, absolutePath));
            // The Content Browser previews this file's own shading, so its cached image is stale now.
            InvalidateMaterialThumbnailCacheUVE(absolutePath);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

bool EditorUVE::DrawStandardMaterialSlotUVE(const char* const label, const char* const id,
                                           Asset::AssetGuidUVE& inOutGuid) {
    bool edited = false;
    if (label != nullptr && label[0] != '\0') {
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
    }
    if (const std::optional<Asset::AssetGuidUVE> picked = DrawAssetPickerUVE(id, inOutGuid, ".uvmaterial", label ? label : "Material")) {
        inOutGuid = *picked;
        edited = true;
    }
    if (inOutGuid != Asset::kInvalidAssetGuidUVE) {
        const std::filesystem::path absPath = m_services->GetAssetDatabaseUVE().ResolveUVE(inOutGuid);
        DrawMaterialPbrEditorInlineUVE(absPath, id);
    }
    return edited;
}

bool EditorUVE::DrawStandardMaterialSlotUVE(const char* const label, const char* const id,
                                           std::string& inOutPath, const bool editInline) {
    bool edited = false;
    if (label != nullptr && label[0] != '\0') {
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
    }
    const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    Asset::IAssetDatabaseUVE& database = m_services->GetAssetDatabaseUVE();

    Asset::AssetGuidUVE guid = Asset::kInvalidAssetGuidUVE;
    if (!inOutPath.empty()) {
        std::filesystem::path absPath = inOutPath;
        if (absPath.is_relative()) {
            absPath = (project.contentRoot / absPath).lexically_normal();
        }
        guid = database.RegisterUVE(absPath);
    }

    if (const std::optional<Asset::AssetGuidUVE> picked = DrawAssetPickerUVE(id, guid, ".uvmaterial", label ? label : "Material")) {
        if (*picked == Asset::kInvalidAssetGuidUVE) {
            inOutPath.clear();
        } else {
            const std::filesystem::path absPath = database.ResolveUVE(*picked);
            const std::filesystem::path rel = absPath.lexically_relative(project.contentRoot);
            inOutPath = (rel.empty() || *rel.begin() == "..") ? absPath.generic_string() : rel.generic_string();
        }
        edited = true;
    }

    if (editInline && !inOutPath.empty()) {
        std::filesystem::path absPath = inOutPath;
        if (absPath.is_relative()) {
            absPath = (project.contentRoot / absPath).lexically_normal();
        }
        DrawMaterialPbrEditorInlineUVE(absPath, id);
    }
    return edited;
}

bool EditorUVE::AutoPopulateSkinFromModelUVE(const Scene::EntityUVE entity, Scene::SkinComponentUVE& skin) {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();

    // 1. Try to find Skeleton3D on entity or parent
    const Scene::Skeleton3DComponentUVE* skelComp = nullptr;
    if (entityManager.HasComponentUVE<Scene::Skeleton3DComponentUVE>(entity)) {
        skelComp = &entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(entity);
    } else if (entityManager.HasComponentUVE<Scene::HierarchyComponentUVE>(entity)) {
        const Scene::EntityUVE parent = entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(entity).parent;
        if (parent != Scene::kInvalidEntityUVE && entityManager.HasComponentUVE<Scene::Skeleton3DComponentUVE>(parent)) {
            skelComp = &entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(parent);
        }
    }
    if (skelComp != nullptr && !skelComp->bones.empty()) {
        // A mesh and a skeleton in the same place are bound by NAME, through the same lookup the
        // renderer poses with. Binding them by position instead - the skeleton's bone 3 to the
        // mesh's joint 3 - is only right when the two lists came from one export in one order, and
        // silently wrong in every other case: the Inspector would show a configured Skin that
        // poses the model into a knot.
        const std::optional<Asset::MeshAssetUVE> meshAsset = LoadEntityMeshAssetUVE(entity);
        if (meshAsset.has_value() &&
            Scene::TryBuildSkinFromMeshJointsUVE(*meshAsset, *skelComp, skin)) {
            return true;
        }
        // Nothing to match against - a skeleton with no skinned mesh of its own - so the binds are
        // the bones themselves, in order, with an identity inverse bind pose.
        skin.boneBinds.clear();
        skin.boneBinds.reserve(skelComp->bones.size());
        for (std::size_t i = 0; i < skelComp->bones.size(); ++i) {
            Scene::SkinBoneBindUVE bind;
            bind.boneName = skelComp->bones[i].name;
            bind.boneIndex = static_cast<std::int32_t>(i);
            bind.inverseBindPose = Math::Matrix4x4UVE::IdentityUVE();
            skin.boneBinds.push_back(std::move(bind));
        }
        skin.bindCount = static_cast<std::uint32_t>(skin.boneBinds.size());
        return true;
    }

    // 2. Read skeleton from assigned model in MeshComponentUVE
    if (entityManager.HasComponentUVE<Scene::MeshComponentUVE>(entity)) {
        const Scene::MeshComponentUVE& meshComp = entityManager.GetComponentUVE<Scene::MeshComponentUVE>(entity);
        if (meshComp.meshGuid != Asset::kInvalidAssetGuidUVE) {
            const std::filesystem::path resolved = m_services->GetAssetDatabaseUVE().ResolveUVE(meshComp.meshGuid);
            const Asset::ProjectFileSnapshotUVE snap = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
            std::filesystem::path relative = resolved.lexically_relative(snap.contentRoot);
            if (relative.empty() || *relative.begin() == "..") {
                relative = resolved;
            }

            std::error_code ec;
            // 2a. Check if resolved file itself is a .uvmodel with joints
            if (std::filesystem::is_regular_file(resolved, ec) && resolved.extension() == ".uvmodel") {
                Asset::MeshAssetUVE meshAsset;
                if (Asset::LoadMeshAssetUVE(resolved, meshAsset) && !meshAsset.joints.empty()) {
                    skin.boneBinds.clear();
                    skin.boneBinds.reserve(meshAsset.joints.size());
                    for (std::size_t i = 0; i < meshAsset.joints.size(); ++i) {
                        Scene::SkinBoneBindUVE bind;
                        bind.boneName = meshAsset.joints[i].name.empty()
                                            ? ("Bone_" + std::to_string(i))
                                            : meshAsset.joints[i].name;
                        bind.boneIndex = static_cast<std::int32_t>(i);
                        bind.inverseBindPose = meshAsset.joints[i].inverseBindMatrix;
                        skin.boneBinds.push_back(std::move(bind));
                    }
                    skin.bindCount = static_cast<std::uint32_t>(skin.boneBinds.size());
                    return true;
                }
            }

            // 2b. Check if there is an imported .uvmodel corresponding to this model source
            const std::filesystem::path imported = GetImportedModelPathUVE(relative);
            if (std::filesystem::is_regular_file(imported, ec)) {
                Asset::MeshAssetUVE meshAsset;
                if (Asset::LoadMeshAssetUVE(imported, meshAsset) && !meshAsset.joints.empty()) {
                    skin.boneBinds.clear();
                    skin.boneBinds.reserve(meshAsset.joints.size());
                    for (std::size_t i = 0; i < meshAsset.joints.size(); ++i) {
                        Scene::SkinBoneBindUVE bind;
                        bind.boneName = meshAsset.joints[i].name.empty()
                                            ? ("Bone_" + std::to_string(i))
                                            : meshAsset.joints[i].name;
                        bind.boneIndex = static_cast<std::int32_t>(i);
                        bind.inverseBindPose = meshAsset.joints[i].inverseBindMatrix;
                        skin.boneBinds.push_back(std::move(bind));
                    }
                    skin.bindCount = static_cast<std::uint32_t>(skin.boneBinds.size());
                    return true;
                }
            }

            // 2c. Fallback to glTF skeleton parsing
            std::optional<Asset::GltfSkeletonUVE> gltfSkel =
                ReadSkeletonForSourceUVE(relative, Scene::kMaximumSkeletonBonesUVE);
            if (!gltfSkel.has_value() && IsModelSourcePathUVE(resolved)) {
                gltfSkel = Asset::ReadGltfSkeletonUVE(resolved, Scene::kMaximumSkeletonBonesUVE);
            }
            if (gltfSkel.has_value() && !gltfSkel->joints.empty()) {
                skin.boneBinds.clear();
                skin.boneBinds.reserve(gltfSkel->joints.size());
                for (std::size_t i = 0; i < gltfSkel->joints.size(); ++i) {
                    Scene::SkinBoneBindUVE bind;
                    bind.boneName = gltfSkel->joints[i].name.empty()
                                        ? ("Bone_" + std::to_string(i))
                                        : gltfSkel->joints[i].name;
                    bind.boneIndex = static_cast<std::int32_t>(i);
                    bind.inverseBindPose = Math::Matrix4x4UVE::IdentityUVE();
                    skin.boneBinds.push_back(std::move(bind));
                }
                skin.bindCount = static_cast<std::uint32_t>(skin.boneBinds.size());
                return true;
            }
        }
    }
    return false;
}

std::optional<Asset::MeshAssetUVE> EditorUVE::LoadEntityMeshAssetUVE(const Scene::EntityUVE entity) {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::MeshComponentUVE>(entity)) {
        return std::nullopt;
    }
    const Scene::MeshComponentUVE meshComponent = entityManager.GetComponentUVE<Scene::MeshComponentUVE>(entity);
    if (meshComponent.meshGuid == Asset::kInvalidAssetGuidUVE) {
        return std::nullopt;
    }
    std::filesystem::path resolved = m_services->GetAssetDatabaseUVE().ResolveUVE(meshComponent.meshGuid);
    std::error_code ec;
    if (resolved.extension() != ".uvmodel" || !std::filesystem::is_regular_file(resolved, ec)) {
        // A model source (.glb/.fbx/...) draws through the .uvmodel the importer wrote; resolve
        // that the same way the skin and material paths do.
        const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
        const std::filesystem::path relative = resolved.lexically_relative(project.contentRoot);
        if (!relative.empty() && *relative.begin() != "..") {
            const std::filesystem::path imported = GetImportedModelPathUVE(relative);
            if (std::filesystem::is_regular_file(imported, ec)) {
                resolved = imported;
            }
        }
    }
    Asset::MeshAssetUVE meshAsset;
    if (resolved.empty() || !std::filesystem::is_regular_file(resolved, ec) ||
        !Asset::LoadMeshAssetUVE(resolved, meshAsset)) {
        return std::nullopt;
    }
    return meshAsset;
}

void EditorUVE::DrawMeshInstance3DInspectorUVE(const Scene::EntityUVE entity) {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::MeshComponentUVE>(entity)) {
        return;
    }
    Scene::MeshComponentUVE meshComp = entityManager.GetComponentUVE<Scene::MeshComponentUVE>(entity);
    bool meshChanged = false;

    // 1. Mesh Picker
    ImGui::TextUnformatted("Mesh");
    ImGui::SameLine();

    std::string meshPreview = "Empty";
    const bool hasPrimitive = entityManager.HasComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity);
    const Scene::PrimitiveMeshKindUVE primitiveKind =
        hasPrimitive ? entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity).kind
                     : Scene::PrimitiveMeshKindUVE::Cube;
    if (hasPrimitive) {
        switch (primitiveKind) {
            case Scene::PrimitiveMeshKindUVE::Cube: meshPreview = "BoxMesh"; break;
            case Scene::PrimitiveMeshKindUVE::UVSphere: meshPreview = "SphereMesh"; break;
            case Scene::PrimitiveMeshKindUVE::Capsule: meshPreview = "CapsuleMesh"; break;
            case Scene::PrimitiveMeshKindUVE::Cylinder: meshPreview = "CylinderMesh"; break;
            case Scene::PrimitiveMeshKindUVE::Plane: meshPreview = "PlaneMesh"; break;
        }
    } else if (meshComp.meshGuid != Asset::kInvalidAssetGuidUVE) {
        const std::filesystem::path p = m_services->GetAssetDatabaseUVE().ResolveUVE(meshComp.meshGuid);
        meshPreview = p.empty() ? "(missing mesh)" : p.stem().string();
    }

    if (BeginCenteredComboUVE("##meshPicker", meshPreview.c_str())) {
        ImGui::TextDisabled("PROCEDURAL SHAPES");
        const auto setProcedural = [&](Scene::PrimitiveMeshKindUVE kind) {
            Scene::PrimitiveMeshComponentUVE prim{kind};
            entityManager.AddComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity, prim);
            meshComp.meshGuid = Asset::kInvalidAssetGuidUVE;
            meshChanged = true;
        };
        if (ImGui::Selectable("BoxMesh", hasPrimitive && entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity).kind == Scene::PrimitiveMeshKindUVE::Cube)) {
            setProcedural(Scene::PrimitiveMeshKindUVE::Cube);
        }
        if (ImGui::Selectable("SphereMesh", hasPrimitive && entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity).kind == Scene::PrimitiveMeshKindUVE::UVSphere)) {
            setProcedural(Scene::PrimitiveMeshKindUVE::UVSphere);
        }
        if (ImGui::Selectable("CapsuleMesh", hasPrimitive && entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity).kind == Scene::PrimitiveMeshKindUVE::Capsule)) {
            setProcedural(Scene::PrimitiveMeshKindUVE::Capsule);
        }
        if (ImGui::Selectable("CylinderMesh", hasPrimitive && entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity).kind == Scene::PrimitiveMeshKindUVE::Cylinder)) {
            setProcedural(Scene::PrimitiveMeshKindUVE::Cylinder);
        }
        if (ImGui::Selectable("PlaneMesh", hasPrimitive && entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity).kind == Scene::PrimitiveMeshKindUVE::Plane)) {
            setProcedural(Scene::PrimitiveMeshKindUVE::Plane);
        }

        ImGui::Separator();
        if (ImGui::Selectable("Browse...")) {
            FilePickerRequestUVE request;
            request.mode = FilePickerModeUVE::Open;
            request.title = "Browse Model";
            request.extensions = {".glb", ".gltf", ".fbx", ".obj", ".uvmodel"};
            const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
            request.startDirectory = PickerStartDirectoryUVE(project, meshComp.meshGuid);
            request.onPick = [this, entity](const std::filesystem::path& absolute) {
                const Asset::AssetGuidUVE guid = m_services->GetAssetDatabaseUVE().RegisterUVE(absolute);
                Scene::IEntityManagerUVE& em = m_services->GetEntityManagerUVE();
                if (em.HasComponentUVE<Scene::MeshComponentUVE>(entity)) {
                    auto mesh = em.GetComponentUVE<Scene::MeshComponentUVE>(entity);
                    mesh.meshGuid = guid;
                    em.AddComponentUVE<Scene::MeshComponentUVE>(entity, mesh);
                    if (em.HasComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity)) {
                        em.RemoveComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity);
                    }
                    if (em.HasComponentUVE<Scene::SkinComponentUVE>(entity)) {
                        auto skin = em.GetComponentUVE<Scene::SkinComponentUVE>(entity);
                        AutoPopulateSkinFromModelUVE(entity, skin);
                        em.AddComponentUVE<Scene::SkinComponentUVE>(entity, skin);
                    }
                }
            };
            OpenFilePickerUVE(std::move(request));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Selectable("Quick Import...")) {
            FilePickerRequestUVE request;
            request.mode = FilePickerModeUVE::Open;
            request.title = "Quick Import Model";
            request.extensions = {".fbx", ".glb", ".gltf", ".obj"};
            const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
            request.startDirectory = project.contentRoot;
            request.onPick = [this](const std::filesystem::path& absolute) {
                m_services->GetAssetDatabaseUVE().RegisterUVE(absolute);
                RefreshProjectFileIndexUVE();
            };
            OpenFilePickerUVE(std::move(request));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Selectable("Empty")) {
            meshComp.meshGuid = Asset::kInvalidAssetGuidUVE;
            if (entityManager.HasComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity)) {
                entityManager.RemoveComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity);
            }
            meshChanged = true;
        }

        ImGui::Separator();
        ImGui::TextDisabled("PROJECT MODELS");
        struct ModelCandidateUVE {
            std::filesystem::path path;
            std::optional<Asset::AssetGuidUVE> guid;
        };
        std::vector<ModelCandidateUVE> candidates;
        auto& database = m_services->GetAssetDatabaseUVE();
        for (const Asset::AssetRecordUVE& record : database.GetRegisteredAssetsUVE()) {
            const std::string ext = record.path.extension().string();
            if (ext == ".uvmodel" || ext == ".glb" || ext == ".gltf" || ext == ".fbx" || ext == ".obj") {
                candidates.push_back({record.path.lexically_normal(), record.guid});
            }
        }
        const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
        for (const Asset::ProjectFileEntryUVE& file : project.entries) {
            if (file.kind == Asset::ProjectFileEntryKindUVE::Directory) continue;
            std::filesystem::path p;
            const std::string ext = file.relativePath.extension().string();
            if (ext == ".uvmodel" || ext == ".glb" || ext == ".gltf" || ext == ".fbx" || ext == ".obj") {
                p = (project.contentRoot / file.relativePath).lexically_normal();
            } else {
                continue;
            }
            const bool known = std::any_of(candidates.begin(), candidates.end(),
                                           [&p](const ModelCandidateUVE& c) { return c.path == p; });
            if (!known) {
                candidates.push_back({p, std::nullopt});
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
            return a.path.generic_string() < b.path.generic_string();
        });
        for (const auto& c : candidates) {
            const bool isSelected = c.guid.has_value() && *c.guid == meshComp.meshGuid;
            const std::string label = c.path.stem().string() + " (" + c.path.extension().string() + ")##" + c.path.generic_string();
            if (ImGui::Selectable(label.c_str(), isSelected) && !isSelected) {
                const Asset::AssetGuidUVE chosen = c.guid.has_value() ? *c.guid : database.RegisterUVE(c.path);
                meshComp.meshGuid = chosen;
                if (entityManager.HasComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity)) {
                    entityManager.RemoveComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity);
                }
                meshChanged = true;
                if (entityManager.HasComponentUVE<Scene::SkinComponentUVE>(entity)) {
                    auto skin = entityManager.GetComponentUVE<Scene::SkinComponentUVE>(entity);
                    AutoPopulateSkinFromModelUVE(entity, skin);
                    entityManager.AddComponentUVE<Scene::SkinComponentUVE>(entity, skin);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", c.path.generic_string().c_str());
            }
        }
        ImGui::EndCombo();
    }

    if (meshChanged) {
        entityManager.AddComponentUVE<Scene::MeshComponentUVE>(entity, meshComp);
    }

    constexpr ImGuiTreeNodeFlags kSectionFlags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;

    // 2. The mesh's own settings, folded under the mesh's own name. A mesh is a thing with a shape
    // and a look of its own - a primitive is sized here, an imported model lists the materials it
    // was authored with - so this reads as the asset's settings, not the object's. One click on the
    // name opens it; nothing about it needs a double click or another window.
    const bool hasAssignedMesh = hasPrimitive || meshComp.meshGuid != Asset::kInvalidAssetGuidUVE;
    if (hasAssignedMesh) {
        const std::string foldKey = hasPrimitive
                                        ? "mesh_asset:primitive:" +
                                              std::to_string(static_cast<int>(primitiveKind))
                                        : "mesh_asset:" + std::to_string(meshComp.meshGuid.value);
        // The fold is named after the file the author recognises: an imported model keeps the name
        // it was authored as (`Swat.fbx`), not the derived `.uvmodel`'s stem, so the row in the
        // Inspector and the file in the Content Browser read the same.
        std::string meshFoldName = meshPreview;
        if (!hasPrimitive && meshComp.meshGuid != Asset::kInvalidAssetGuidUVE) {
            const std::filesystem::path resolvedMeshPath =
                m_services->GetAssetDatabaseUVE().ResolveUVE(meshComp.meshGuid);
            if (!resolvedMeshPath.empty()) {
                meshFoldName = resolvedMeshPath.extension() == ".uvmodel"
                                   ? resolvedMeshPath.stem().string()
                                   : resolvedMeshPath.filename().string();
            }
        }
        const std::string foldLabel = meshFoldName + "###meshinstance:mesh_asset";
        if (DrawInspectorFoldUVE(foldLabel.c_str(), foldKey, false, false, kSectionFlags)) {
            if (hasPrimitive) {
                // PROCEDURAL SIZE: the primitive's geometry is authored at unit size, so the
                // object's scale IS its size. Editing it here is the same number the Transform
                // section shows, named in the units an author thinks in.
                ImGui::TextDisabled("SIZE");
                if (entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity)) {
                    Scene::TransformComponentUVE transform =
                        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
                    float size[3] = {transform.localScale.x, transform.localScale.y, transform.localScale.z};
                    if (ImGui::DragFloat3("##primitiveSize", size, 0.01F, 0.0001F, 10000.0F, "%.3f m")) {
                        transform.localScale = Math::Vector3UVE{size[0], size[1], size[2]};
                        entityManager.AddComponentUVE<Scene::TransformComponentUVE>(entity, transform);
                    }
                }
                ImGui::Spacing();

                // MATERIAL: the whole of a primitive's surface authoring. It is shaded through the
                // same material path an imported mesh uses, and the material's own albedo is where
                // its colour is authored - which is why there is one surface control here and not
                // two. A second COLOUR row used to sit under this slot while it was empty, and it
                // meant the same object had two places to set a colour, one of which silently
                // stopped existing the moment a material was picked.
                Scene::PrimitiveMeshComponentUVE primitive =
                    entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity);
                ImGui::TextDisabled("MATERIAL");
                if (DrawStandardMaterialSlotUVE(nullptr, "##primitiveMaterial", primitive.materialGuid)) {
                    entityManager.AddComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity, primitive);
                }
            } else {
                // MATERIALS: the model carries its own. Each row is that material's file, edited in
                // place, so what the Inspector changes is what the mesh references.
                ImGui::TextDisabled("MATERIALS");
                const std::optional<Asset::MeshAssetUVE> loadedMesh = LoadEntityMeshAssetUVE(entity);
                if (loadedMesh.has_value()) {
                    const Asset::MeshAssetUVE& meshAsset = *loadedMesh;
                    if (meshAsset.materialGuids.empty()) {
                        ImGui::TextDisabled("This model has no material of its own.");
                    }
                    for (std::size_t materialIndex = 0U; materialIndex < meshAsset.materialGuids.size();
                         ++materialIndex) {
                        const std::filesystem::path materialPath =
                            m_services->GetAssetDatabaseUVE().ResolveUVE(meshAsset.materialGuids[materialIndex]);
                        if (materialPath.empty()) {
                            continue;
                        }
                        ImGui::PushID(static_cast<int>(materialIndex));
                        const std::string idPrefix = "##meshOwnedMaterial" + std::to_string(materialIndex);
                        DrawMaterialPbrEditorInlineUVE(materialPath, idPrefix.c_str());
                        ImGui::PopID();
                    }
                } else {
                    ImGui::TextDisabled("The mesh asset could not be read.");
                }
            }
            ImGui::TreePop();
        }
    }

    // 3. Expandable Skeleton Section with Skin Slot
    if (DrawInspectorFoldUVE("Skeleton", "meshinstance:skeleton", false, false, kSectionFlags)) {
        const bool hasSkin = entityManager.HasComponentUVE<Scene::SkinComponentUVE>(entity);
        std::string skinPreview = "Empty";
        if (hasSkin) {
            auto skin = entityManager.GetComponentUVE<Scene::SkinComponentUVE>(entity);
            if (skin.bindCount == 0 && skin.boneBinds.empty()) {
                if (AutoPopulateSkinFromModelUVE(entity, skin)) {
                    entityManager.AddComponentUVE<Scene::SkinComponentUVE>(entity, skin);
                }
            }
            skinPreview = "Skin (" + std::to_string(skin.bindCount) + " binds)";
        }
        ImGui::TextUnformatted("Skin");
        ImGui::SameLine();
        if (BeginCenteredComboUVE("##skinSlot", skinPreview.c_str())) {
            if (!hasSkin) {
                if (ImGui::Selectable("Skin") || ImGui::Selectable("+ New Skin")) {
                    Scene::SkinComponentUVE skin{};
                    AutoPopulateSkinFromModelUVE(entity, skin);
                    entityManager.AddComponentUVE<Scene::SkinComponentUVE>(entity, skin);
                }
            } else {
                if (ImGui::Selectable("Re-populate from Model / Skeleton")) {
                    auto skin = entityManager.GetComponentUVE<Scene::SkinComponentUVE>(entity);
                    AutoPopulateSkinFromModelUVE(entity, skin);
                    entityManager.AddComponentUVE<Scene::SkinComponentUVE>(entity, skin);
                }
                if (ImGui::Selectable("Empty")) {
                    entityManager.RemoveComponentUVE<Scene::SkinComponentUVE>(entity);
                }
            }
            ImGui::EndCombo();
        }

        // When assigned, expanding skin settings shows bindCount and boneBinds:
        if (entityManager.HasComponentUVE<Scene::SkinComponentUVE>(entity)) {
            auto skin = entityManager.GetComponentUVE<Scene::SkinComponentUVE>(entity);
            bool skinChanged = false;
            if (DrawInspectorFoldUVE("Skin Settings", "meshinstance:skin_settings", true, false, kSectionFlags)) {
                int bindCountInt = static_cast<int>(skin.bindCount);
                if (ImGui::InputInt("Bind Count", &bindCountInt)) {
                    const std::uint32_t newCount = static_cast<std::uint32_t>(std::max(0, bindCountInt));
                    if (newCount != skin.bindCount) {
                        skin.bindCount = newCount;
                        skin.boneBinds.resize(newCount);
                        for (std::size_t i = 0; i < skin.boneBinds.size(); ++i) {
                            if (skin.boneBinds[i].inverseBindPose == Math::Matrix4x4UVE{}) {
                                skin.boneBinds[i].inverseBindPose = Math::Matrix4x4UVE::IdentityUVE();
                            }
                        }
                        skinChanged = true;
                    }
                }

                if (ImGui::Button("+ Add Bone Bind")) {
                    Scene::SkinBoneBindUVE newBind;
                    newBind.boneIndex = static_cast<std::int32_t>(skin.boneBinds.size());
                    newBind.inverseBindPose = Math::Matrix4x4UVE::IdentityUVE();
                    skin.boneBinds.push_back(std::move(newBind));
                    skin.bindCount = static_cast<std::uint32_t>(skin.boneBinds.size());
                    skinChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Binds") && !skin.boneBinds.empty()) {
                    skin.boneBinds.clear();
                    skin.bindCount = 0;
                    skinChanged = true;
                }

                if (ImGui::TreeNodeEx("Bone Binds", ImGuiTreeNodeFlags_SpanAvailWidth)) {
                    for (std::size_t i = 0; i < skin.boneBinds.size(); ++i) {
                        ImGui::PushID(static_cast<int>(i));
                        auto& bind = skin.boneBinds[i];
                        const std::string label = "Bone " + std::to_string(i) + ": " + (bind.boneName.empty() ? "(unnamed)" : bind.boneName);
                        if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
                            char nameBuf[128];
                            std::strncpy(nameBuf, bind.boneName.c_str(), sizeof(nameBuf) - 1);
                            nameBuf[sizeof(nameBuf) - 1] = '\0';
                            if (ImGui::InputText("Bone Name", nameBuf, sizeof(nameBuf))) {
                                bind.boneName = nameBuf;
                                skinChanged = true;
                            }
                            if (ImGui::InputInt("Bone Index", &bind.boneIndex)) {
                                skinChanged = true;
                            }
                            if (ImGui::Button("Remove")) {
                                skin.boneBinds.erase(skin.boneBinds.begin() + static_cast<std::ptrdiff_t>(i));
                                skin.bindCount = static_cast<std::uint32_t>(skin.boneBinds.size());
                                skinChanged = true;
                                ImGui::TreePop();
                                ImGui::PopID();
                                break;
                            }
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
                ImGui::TreePop();
            }
            if (skinChanged) {
                entityManager.AddComponentUVE<Scene::SkinComponentUVE>(entity, skin);
            }
        }
        ImGui::TreePop();
    }

    // 4. Expandable Surface Material Override Section.
    //
    // Two levels: one material for EVERY surface, then one per surface. The whole-object row is
    // the wider statement, so it silences the per-surface rows while it is set - they are drawn
    // disabled with the reason beside them rather than accepting an edit that could not take
    // effect. The rows deliberately stop at the picker: the same materials are already editable
    // once in the mesh's own MATERIALS fold, and an inline PBR editor under each of a dozen surface
    // rows would bury the rows themselves. Nothing is written to the Mesh component here: the
    // surfaces override used to be copied onto `mesh.materialGuid`, which made two components hold
    // the same decision and left clearing it unable to take effect.
    if (DrawInspectorFoldUVE("Surface Material Override", "meshinstance:surface_override", false, false, kSectionFlags)) {
        if (!entityManager.HasComponentUVE<Scene::SurfaceInstanceComponentUVE>(entity)) {
            entityManager.AddComponentUVE<Scene::SurfaceInstanceComponentUVE>(entity, Scene::SurfaceInstanceComponentUVE{});
        }
        Scene::SurfaceInstanceComponentUVE surfaceComp =
            entityManager.GetComponentUVE<Scene::SurfaceInstanceComponentUVE>(entity);
        bool surfaceChanged = false;

        ImGui::TextDisabled("ALL SURFACES");
        surfaceChanged |= DrawStandardMaterialSlotUVE(nullptr, "##materialOverride", surfaceComp.materialOverridePath);
        if (!surfaceComp.materialOverridePath.empty()) {
            ImGui::TextDisabled("Drawn on every surface in place of the model's own materials.");
        }

        const std::optional<Asset::MeshAssetUVE> meshAsset =
            hasPrimitive ? std::nullopt : LoadEntityMeshAssetUVE(entity);
        const std::size_t surfaceCount = meshAsset.has_value() ? meshAsset->SurfaceCountUVE() : 0U;
        if (!hasPrimitive) {
            ImGui::Spacing();
            ImGui::TextDisabled("PER SURFACE");
            if (surfaceCount == 0U) {
                ImGui::TextDisabled(meshAsset.has_value() ? "This model has no geometry to override."
                                                          : "Select a model to override its surfaces.");
            } else {
                if (surfaceComp.surfaceMaterialOverridePaths.size() < surfaceCount) {
                    surfaceComp.surfaceMaterialOverridePaths.resize(surfaceCount);
                }
                // Ignored at runtime while a whole-object material is set - the override path
                // above, or a material assigned on the mesh's own MATERIAL row, which draws every
                // surface the same way - so they are shown disabled with the reason instead of
                // accepting an edit that would do nothing.
                const bool overriddenEverywhere = !surfaceComp.materialOverridePath.empty();
                const bool meshMaterialSet =
                    !overriddenEverywhere && meshComp.materialGuid != Asset::kInvalidAssetGuidUVE;
                ImGui::BeginDisabled(overriddenEverywhere || meshMaterialSet);
                for (std::size_t surfaceIndex = 0U; surfaceIndex < surfaceCount; ++surfaceIndex) {
                    ImGui::PushID(static_cast<int>(surfaceIndex));
                    const std::string label = "Surface " + std::to_string(surfaceIndex);
                    surfaceChanged |= DrawStandardMaterialSlotUVE(
                        label.c_str(), "##surfaceOverride",
                        surfaceComp.surfaceMaterialOverridePaths[surfaceIndex], false);
                    ImGui::PopID();
                }
                ImGui::EndDisabled();
                if (overriddenEverywhere) {
                    ImGui::TextDisabled("The all-surfaces override above replaces these.");
                } else if (meshMaterialSet) {
                    ImGui::TextDisabled("The mesh's MATERIAL above draws every surface; clear it to use these.");
                }
            }
        }

        if (surfaceChanged) {
            // A trailing empty entry says what an absent entry says, so the list is trimmed back to
            // what the author actually set; an interior empty still marks the surface it belongs to.
            while (!surfaceComp.surfaceMaterialOverridePaths.empty() &&
                   surfaceComp.surfaceMaterialOverridePaths.back().empty()) {
                surfaceComp.surfaceMaterialOverridePaths.pop_back();
            }
            entityManager.AddComponentUVE<Scene::SurfaceInstanceComponentUVE>(entity, surfaceComp);
        }
        ImGui::TreePop();
    }

    // 5. Visibility Layers
    ImGui::Spacing();
    std::uint32_t layers = meshComp.visibilityLayers;
    const Config::SettingsDocumentUVE& projectSettings = m_services->GetProjectSettingsUVE();
    LayerNamesUVE layerNames{};
    for (std::size_t index = 0U; index < layerNames.size(); ++index) {
        layerNames[index] = Core::GetLayerNameUVE(projectSettings, Core::LayerSetUVE::Render, index);
    }
    if (DrawLayerMaskFieldUVE("Visibility Layers", layers, layerNames) == LayerMaskFieldEventUVE::Changed) {
        meshComp.visibilityLayers = layers;
        entityManager.AddComponentUVE<Scene::MeshComponentUVE>(entity, meshComp);
    }
}

bool EditorUVE::DrawCustomPropertyUVE(const TypeMetadataEntryUVE& entry, const TypeMetadataPropertyUVE& property,
                                      const void* const instance) {
    if (property.customDrawerId == "multiline-text") {
        DrawMultilineTextPropertyUVE(entry, property, instance);
        return true;
    }
    if (property.customDrawerId == "animation-parameters") {
        DrawAnimationParametersPropertyUVE(entry, property, instance);
        return true;
    }
    if (property.customDrawerId == "animation-graph") {
        DrawAnimationGraphPropertyUVE(entry, property, instance);
        return true;
    }
    if (property.customDrawerId == "script-slot") {
        DrawScriptSlotPropertyUVE(entry, property, instance);
        return true;
    }
    if (property.customDrawerId == "script-exports") {
        DrawScriptExportsPropertyUVE(entry, property, instance);
        return true;
    }
    if (property.customDrawerId == "object-metadata") {
        DrawObjectMetadataPropertyUVE(entry, property, instance);
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
    if (property.customDrawerId == "entity-reference-list") {
        DrawEntityReferenceListPropertyUVE(entry, property, instance);
        return true;
    }
    if (property.customDrawerId == "full-body-effectors") {
        DrawFullBodyEffectorsPropertyUVE(entry, property, instance);
        return true;
    }
    if (property.customDrawerId == "morph-target-weights") {
        DrawMorphTargetWeightsPropertyUVE(entry, property, instance);
        return true;
    }
    if (property.customDrawerId == "lod-group-thresholds") {
        DrawLodGroupThresholdsPropertyUVE(entry, property, instance);
        return true;
    }
    if (property.customDrawerId == "lod-group-meshes") {
        DrawLodGroupMeshesPropertyUVE(entry, property, instance);
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
    // A live edit still in flight is finished first, before this one changes anything, so the two
    // land in history in the order they happened.
    static_cast<void>(CommitComponentPropertyPreviewUVE());
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

bool EditorUVE::CopySelectedComponentUVE(const TypeMetadataEntryUVE& entry) {
    if (!HasSingleDocumentSelectionUVE() || !entry.HasFactoryUVE()) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE(m_selectedEntity, entry.typeIndex)) {
        return false;
    }
    Core::TypeInstanceUVE copy =
        Core::TypeInstanceUVE::CloneUVE(entry, entityManager.GetComponentPointerUVE(m_selectedEntity, entry.typeIndex));
    if (!copy.IsValidUVE()) {
        return false;
    }
    m_componentClipboard = ComponentClipboardUVE{&entry, std::move(copy)};
    return true;
}

bool EditorUVE::CanPasteSelectedComponentUVE(const TypeMetadataEntryUVE& entry) const noexcept {
    // Types are compared by identity, not by name: two entries can never share a type index.
    return m_componentClipboard.has_value() && m_componentClipboard->entry != nullptr &&
           m_componentClipboard->entry->typeIndex == entry.typeIndex && m_componentClipboard->value.IsValidUVE();
}

bool EditorUVE::PasteSelectedComponentUVE(const TypeMetadataEntryUVE& entry) {
    if (!CanPasteSelectedComponentUVE(entry)) {
        return false;
    }
    return SetSelectedComponentValueUVE(entry, m_componentClipboard->value.GetUVE());
}

bool EditorUVE::ResetSelectedComponentUVE(const TypeMetadataEntryUVE& entry) {
    const Core::TypeInstanceUVE defaults = Core::TypeInstanceUVE::MakeDefaultUVE(entry);
    return defaults.IsValidUVE() && SetSelectedComponentValueUVE(entry, defaults.GetUVE());
}

bool EditorUVE::CopySelectedTransformUVE() {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!HasSingleDocumentSelectionUVE() ||
        !entityManager.HasComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity)) {
        return false;
    }
    m_transformClipboard = entityManager.GetComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity);
    return true;
}

namespace {

// The local pose of `pose` written over `target`, leaving everything that is not pose alone.
Scene::TransformComponentUVE WithLocalPoseUVE(Scene::TransformComponentUVE target,
                                              const Scene::TransformComponentUVE& pose) {
    target.localPosition = pose.localPosition;
    target.localRotation = pose.localRotation;
    target.localScale = pose.localScale;
    target.localEulerRadians = pose.localEulerRadians;
    target.eulerOrder = pose.eulerOrder;
    target.rotationEditMode = pose.rotationEditMode;
    return target;
}

} // namespace

bool EditorUVE::PasteSelectedTransformUVE() {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!m_transformClipboard.has_value() || !HasSingleDocumentSelectionUVE() ||
        !entityManager.HasComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity)) {
        return false;
    }
    return SetSelectedLocalTransformUVE(WithLocalPoseUVE(
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity), *m_transformClipboard));
}

bool EditorUVE::ResetSelectedTransformUVE() {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!HasSingleDocumentSelectionUVE() ||
        !entityManager.HasComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity)) {
        return false;
    }
    return SetSelectedLocalTransformUVE(WithLocalPoseUVE(
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity), Scene::TransformComponentUVE{}));
}

void EditorUVE::DrawInspectorSectionMenuUVE(const TypeMetadataEntryUVE* const entry, const char* const sectionName) {
    if (!ImGui::BeginPopupContextItem("##section-menu")) {
        return;
    }
    ImGui::TextDisabled("%s", sectionName);
    ImGui::Separator();
    const bool writable = IsAuthoringCommandAllowedUVE();
    if (ImGui::MenuItem("Copy Values")) {
        static_cast<void>(entry != nullptr ? CopySelectedComponentUVE(*entry) : CopySelectedTransformUVE());
    }
    const bool canPaste = writable && (entry != nullptr ? CanPasteSelectedComponentUVE(*entry)
                                                        : CanPasteSelectedTransformUVE());
    if (ImGui::MenuItem("Paste Values", nullptr, false, canPaste)) {
        static_cast<void>(entry != nullptr ? PasteSelectedComponentUVE(*entry) : PasteSelectedTransformUVE());
    }
    if (!canPaste && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Copy a %s section first.", sectionName);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Reset to Defaults", nullptr, false, writable)) {
        static_cast<void>(entry != nullptr ? ResetSelectedComponentUVE(*entry) : ResetSelectedTransformUVE());
    }
    ImGui::EndPopup();
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
    // See SetSelectedComponentValueUVE: a live edit in flight is recorded before this one.
    static_cast<void>(CommitComponentPropertyPreviewUVE());
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
    // A collider that has just been switched to Capsule inherits a Box's numbers, and the fields'
    // own defaults (height 1.0, radius 0.5) make the two caps meet exactly: no barrel, so the
    // author picks Capsule and is shown a sphere. The fit runs here rather than on every height
    // drag, so it only repairs a shape that cannot express itself and never overrules a height the
    // author chose on purpose afterwards. It happens before the validity check and before the
    // "after" clone, so the repaired value is what gets recorded and what undo puts back.
    if (entry.typeIndex == std::type_index(typeid(Scene::ColliderComponentUVE))) {
        auto* const collider = static_cast<Scene::ColliderComponentUVE*>(instance);
        *collider = Scene::FitColliderShapeUVE(*collider);
    }
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
    if (entry.typeIndex == std::type_index(typeid(Scene::CameraComponentUVE))) {
        const auto* const camera = static_cast<const Scene::CameraComponentUVE*>(instance);
        if (camera != nullptr && camera->current) {
            Scene::MakeCameraCurrentUVE(entityManager, m_selectedEntity);
        }
    }
    return true;
}

bool EditorUVE::PreviewSelectedComponentPropertyUVE(const TypeMetadataEntryUVE& entry,
                                                    const TypeMetadataPropertyUVE& property,
                                                    const void* const newValue) {
    if (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE() ||
        !property.IsAuthoringWritableUVE() || !entry.HasFactoryUVE() || newValue == nullptr) {
        return false;
    }
    if (m_componentPropertyPreview.has_value() &&
        (m_componentPropertyPreview->entity != m_selectedEntity || m_componentPropertyPreview->entry != &entry ||
         m_componentPropertyPreview->property != &property)) {
        static_cast<void>(CommitComponentPropertyPreviewUVE());
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE(m_selectedEntity, entry.typeIndex)) {
        return false;
    }
    void* const instance = entityManager.GetComponentPointerUVE(m_selectedEntity, entry.typeIndex);
    Core::TypeInstanceUVE previous = Core::TypeInstanceUVE::CloneUVE(entry, instance);
    if (!previous.IsValidUVE()) {
        return false;
    }
    if (!m_componentPropertyPreview.has_value()) {
        Core::TypeInstanceUVE before = Core::TypeInstanceUVE::CloneUVE(entry, instance);
        if (!before.IsValidUVE()) {
            return false;
        }
        m_componentPropertyPreview = ComponentPropertyPreviewUVE{m_selectedEntity, &entry, &property, std::move(before),
                                                                 CaptureSelectionSnapshotUVE(), m_sceneDirty};
    }
    property.setValue(instance, newValue);
    if (entry.isInstanceValid != nullptr && !entry.isInstanceValid(instance)) {
        // Refused by the component's own rule: keep the last accepted value on screen.
        entry.assignInstance(instance, previous.GetUVE());
        return false;
    }
    m_sceneDirty = true;
    return true;
}

bool EditorUVE::CommitComponentPropertyPreviewUVE() {
    if (!m_componentPropertyPreview.has_value()) {
        return false;
    }
    ComponentPropertyPreviewUVE preview = std::move(*m_componentPropertyPreview);
    m_componentPropertyPreview.reset();
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!IsDocumentEntityUVE(preview.entity) || !entityManager.HasComponentUVE(preview.entity, preview.entry->typeIndex)) {
        return false;
    }
    void* const instance = entityManager.GetComponentPointerUVE(preview.entity, preview.entry->typeIndex);
    if (preview.property->areEqual != nullptr && preview.property->areEqual(preview.before.GetUVE(), instance)) {
        // Opened and closed without a net change: nothing to undo, and nothing left unsaved.
        m_sceneDirty = preview.dirtyBefore;
        return true;
    }
    Core::TypeInstanceUVE after = Core::TypeInstanceUVE::CloneUVE(*preview.entry, instance);
    if (!after.IsValidUVE()) {
        preview.entry->assignInstance(instance, preview.before.GetUVE());
        m_sceneDirty = preview.dirtyBefore;
        return false;
    }
    m_sceneDirty = true;
    RecordHistoryUVE(ComponentPropertyHistoryEntryUVE{preview.entity, preview.entry, std::move(preview.before),
                                                      std::move(after), preview.selectionBefore,
                                                      CaptureSelectionSnapshotUVE(), preview.dirtyBefore, true});
    return true;
}

bool EditorUVE::CancelComponentPropertyPreviewUVE() {
    if (!m_componentPropertyPreview.has_value()) {
        return false;
    }
    ComponentPropertyPreviewUVE preview = std::move(*m_componentPropertyPreview);
    m_componentPropertyPreview.reset();
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!IsDocumentEntityUVE(preview.entity) || !entityManager.HasComponentUVE(preview.entity, preview.entry->typeIndex)) {
        return false;
    }
    void* const instance = entityManager.GetComponentPointerUVE(preview.entity, preview.entry->typeIndex);
    preview.entry->assignInstance(instance, preview.before.GetUVE());
    m_sceneDirty = preview.dirtyBefore;
    return true;
}

bool EditorUVE::CommitComponentPropertyPreviewForUVE(const TypeMetadataEntryUVE& entry,
                                                    const TypeMetadataPropertyUVE& property) {
    if (!m_componentPropertyPreview.has_value() || m_componentPropertyPreview->entry != &entry ||
        m_componentPropertyPreview->property != &property) {
        return false;
    }
    return CommitComponentPropertyPreviewUVE();
}

bool EditorUVE::ApplyContinuousPropertyEditUVE(const TypeMetadataEntryUVE& entry,
                                               const TypeMetadataPropertyUVE& property, const bool changed,
                                               const void* const newValue) {
    bool written = false;
    if (changed) {
        // Held: a drag (or typing into the field) in progress, shown live and not yet history.
        written = ImGui::IsItemActive() ? PreviewSelectedComponentPropertyUVE(entry, property, newValue)
                                        : SetSelectedComponentPropertyUVE(entry, property, newValue);
    }
    if (ImGui::IsItemDeactivated()) {
        written = CommitComponentPropertyPreviewForUVE(entry, property) || written;
    }
    return written;
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

std::vector<Scene::EntityUVE> EditorUVE::GetEntityReferenceCandidatesUVE() {
    // Outliner order, by the same walk the Outliner itself uses: roots reversed onto a stack, then
    // each object's children pushed in reverse so the next one popped is the next one drawn.
    std::vector<Scene::EntityUVE> candidates;
    const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    std::vector<Scene::EntityUVE> pending = GetDocumentRootsUVE();
    std::reverse(pending.begin(), pending.end());
    while (!pending.empty()) {
        const Scene::EntityUVE candidate = pending.back();
        pending.pop_back();
        std::vector<Scene::EntityUVE> children =
            m_services->GetSceneGraphUVE().GetChildrenUVE(m_services->GetEntityManagerUVE(), candidate);
        pending.insert(pending.end(), children.rbegin(), children.rend());
        if (candidate == m_selectedEntity ||
            !entityManager.HasComponentUVE<Scene::TransformComponentUVE>(candidate)) {
            continue;
        }
        candidates.push_back(candidate);
    }
    return candidates;
}

void EditorUVE::DrawEntityReferenceListPropertyUVE(const TypeMetadataEntryUVE& entry,
                                                    const TypeMetadataPropertyUVE& property,
                                                    const void* const instance) {
    if (property.typeId != Scene::kPropertyTypeEntityListUVE || property.getValue == nullptr ||
        property.elementCount == 0U) {
        return;
    }

    // The value is a fixed-capacity list of references, copied whole by the property's own accessor
    // - sized from the declaration's element count, and written back as one property write, which
    // means one undo step and the component's own rule as the last word. An empty slot is
    // kInvalidEntityUVE and the list is a prefix, so anything that removes a slot compacts what
    // followed it rather than leaving a hole the component would refuse.
    std::vector<Scene::EntityUVE> references(property.elementCount, Scene::kInvalidEntityUVE);
    property.getValue(instance, references.data());
    std::size_t referenceCount = 0U;
    for (std::size_t index = 0U; index < references.size(); ++index) {
        if (references[index] != Scene::kInvalidEntityUVE) {
            references[referenceCount] = references[index];
            ++referenceCount;
        }
    }
    for (std::size_t index = referenceCount; index < references.size(); ++index) {
        references[index] = Scene::kInvalidEntityUVE;
    }

    const bool writable = property.IsAuthoringWritableUVE() && IsAuthoringCommandAllowedUVE();
    const std::vector<Scene::EntityUVE> candidates = GetEntityReferenceCandidatesUVE();
    std::optional<std::size_t> removeIndex;
    std::optional<std::size_t> replaceIndex;
    std::optional<Scene::EntityUVE> replaceWith;
    bool clearAll = false;

    ImGui::BeginDisabled(!writable);
    {
        const std::string header =
            "Exclusions (" + std::to_string(referenceCount) + ")##entity-references";
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(header.c_str());
        if (ImGui::IsItemHovered()) {
            DrawTooltipUVE(property);
        }
        if (referenceCount != 0U) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) {
                clearAll = true;
            }
        }
    }
    if (BeginPropertyRowsUVE("##entity-references")) {
        const auto alreadyListed = [&references, referenceCount](const Scene::EntityUVE candidate) {
            return std::find(references.cbegin(), references.cbegin() + static_cast<std::ptrdiff_t>(referenceCount),
                             candidate) != references.cbegin() + static_cast<std::ptrdiff_t>(referenceCount);
        };
        for (std::size_t index = 0U; index < referenceCount; ++index) {
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(index == 0U ? "Excludes" : "And");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            const Scene::EntityUVE current = references[index];
            const std::string preview = IsDocumentEntityUVE(current) ? GetEntityDisplayLabelUVE(current)
                                                                     : std::string{"(missing object)"};
            if (BeginCenteredComboUVE("##reference", preview.c_str())) {
                if (ImGui::Selectable("(remove)")) {
                    removeIndex = index;
                }
                for (const Scene::EntityUVE candidate : candidates) {
                    const bool isSelected = candidate == current;
                    // An object already on the list is offered once; the component refuses
                    // duplicates, so a second one would be a write that silently did nothing.
                    if (alreadyListed(candidate) && !isSelected) {
                        continue;
                    }
                    const std::string label = GetEntityDisplayLabelUVE(candidate) + "##" +
                                              std::to_string(candidate.index) + "_" +
                                              std::to_string(candidate.generation);
                    if (ImGui::Selectable(label.c_str(), isSelected) && !isSelected) {
                        replaceIndex = index;
                        replaceWith = candidate;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }
        if (referenceCount < references.size()) {
            ImGui::PushID("add");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Add");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (BeginCenteredComboUVE("##reference", "(pick an object)")) {
                for (const Scene::EntityUVE candidate : candidates) {
                    if (alreadyListed(candidate)) {
                        continue;
                    }
                    const std::string label = GetEntityDisplayLabelUVE(candidate) + "##" +
                                              std::to_string(candidate.index) + "_" +
                                              std::to_string(candidate.generation);
                    if (ImGui::Selectable(label.c_str())) {
                        replaceIndex = referenceCount;
                        replaceWith = candidate;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        } else {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("Full");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("All %zu slots are in use.", references.size());
        }
        ImGui::EndTable();
    }
    ImGui::EndDisabled();

    if (!clearAll && !removeIndex.has_value() && !replaceWith.has_value()) {
        return;
    }
    if (replaceIndex.has_value() && replaceWith.has_value()) {
        references[*replaceIndex] = *replaceWith;
    }
    if (removeIndex.has_value()) {
        references[*removeIndex] = Scene::kInvalidEntityUVE;
    }
    std::vector<Scene::EntityUVE> written(property.elementCount, Scene::kInvalidEntityUVE);
    std::size_t writtenCount = 0U;
    if (!clearAll) {
        for (const Scene::EntityUVE reference : references) {
            if (reference != Scene::kInvalidEntityUVE) {
                written[writtenCount] = reference;
                ++writtenCount;
            }
        }
    }
    static_cast<void>(SetSelectedComponentPropertyUVE(entry, property, written.data()));
}

void EditorUVE::DrawFullBodyEffectorsPropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                                 const Core::TypeMetadataPropertyUVE& property,
                                                 const void* const instance) {
    if (property.typeId != Strings::StringIdUVE{"FullBodyIKEffectors"} || property.getValue == nullptr ||
        property.setValue == nullptr || instance == nullptr) {
        return;
    }

    // The whole effector list, copied out and written back through the property's own typed
    // accessor - one undo step per edit and the component's validator as the last word. Rows carry
    // a bone reference, a target reference or authored point, and a pull weight; the runtime
    // answers the solve writes back live in the State group, not here.
    std::vector<Scene::FullBodyIKEffectorUVE> effectors;
    property.getValue(instance, &effectors);

    const bool writable = property.IsAuthoringWritableUVE() && IsAuthoringCommandAllowedUVE();
    std::optional<std::size_t> removeIndex;
    bool added = false;
    bool edited = false;

    ImGui::BeginDisabled(!writable);
    {
        const std::string header = "Effectors (" + std::to_string(effectors.size()) + ")##full-body-effectors";
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(header.c_str());
        if (ImGui::IsItemHovered()) {
            DrawTooltipUVE(property);
        }
        if (effectors.size() < Scene::kMaximumFullBodyIKEffectorsUVE) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Add")) {
                effectors.push_back(Scene::FullBodyIKEffectorUVE{});
                added = true;
                edited = true;
            }
        }
    }
    if (BeginPropertyRowsUVE("##full-body-effectors")) {
        const std::vector<Scene::EntityUVE> candidates = GetEntityReferenceCandidatesUVE();
        for (std::size_t index = 0U; index < effectors.size(); ++index) {
            Scene::FullBodyIKEffectorUVE& effector = effectors[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Bone");
            ImGui::TableSetColumnIndex(1);
            {
                std::array<char, 128> boneName{};
                const std::size_t copyLength = std::min(effector.boneName.size(), boneName.size() - 1U);
                std::memcpy(boneName.data(), effector.boneName.data(), copyLength);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputText("##bone-name", boneName.data(), boneName.size())) {
                    effector.boneName = boneName.data();
                    edited = true;
                }
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Bone Index");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            {
                int boneIndex = static_cast<int>(effector.boneIndex);
                if (ImGui::InputInt("##bone-index", &boneIndex, 1)) {
                    effector.boneIndex = boneIndex < 0
                                             ? Scene::kInvalidSkeletonBoneIndexUVE
                                             : static_cast<std::uint32_t>(boneIndex);
                    edited = true;
                }
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Target");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            {
                const std::string preview = effector.target != Scene::kInvalidEntityUVE && IsDocumentEntityUVE(effector.target)
                                                ? GetEntityDisplayLabelUVE(effector.target)
                                                : std::string{"(authored point)"};
                if (BeginCenteredComboUVE("##target", preview.c_str())) {
                    if (ImGui::Selectable("(authored point)")) {
                        effector.target = Scene::kInvalidEntityUVE;
                        edited = true;
                    }
                    for (const Scene::EntityUVE candidate : candidates) {
                        const bool isSelected = candidate == effector.target;
                        const std::string label = GetEntityDisplayLabelUVE(candidate) + "##" +
                                                  std::to_string(candidate.index) + "_" +
                                                  std::to_string(candidate.generation);
                        if (ImGui::Selectable(label.c_str(), isSelected) && !isSelected) {
                            effector.target = candidate;
                            edited = true;
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Target Point");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            {
                float point[3] = {effector.targetPosition.x, effector.targetPosition.y,
                                  effector.targetPosition.z};
                if (ImGui::DragFloat3("##target-point", point, 0.01F)) {
                    effector.targetPosition = Math::Vector3UVE{point[0], point[1], point[2]};
                    edited = true;
                }
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Weight");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            {
                float weight = effector.positionWeight;
                if (ImGui::SliderFloat("##weight", &weight, 0.0F, 1.0F, "%.3f")) {
                    effector.positionWeight = weight;
                    edited = true;
                }
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("");
            ImGui::TableSetColumnIndex(1);
            if (ImGui::SmallButton("Remove")) {
                removeIndex = index;
                edited = true;
            }
            ImGui::PopID();
        }
        if (effectors.empty()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("None");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("Add a limb to pull on: a bone, a target, and how hard it pulls.");
        } else {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("Sweeps");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("Effectors are answered in this order each pass.");
        }
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
    static_cast<void>(added);

    if (!edited) {
        return;
    }
    if (removeIndex.has_value()) {
        effectors.erase(effectors.begin() + static_cast<std::ptrdiff_t>(*removeIndex));
    }
    static_cast<void>(SetSelectedComponentPropertyUVE(entry, property, &effectors));
}

void EditorUVE::DrawMorphTargetWeightsPropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                                  const Core::TypeMetadataPropertyUVE& property,
                                                  const void* const instance) {
    if (property.typeId != Strings::StringIdUVE{"MorphTargetWeights"} || property.getValue == nullptr ||
        property.setValue == nullptr || instance == nullptr) {
        return;
    }

    // The list is sparse by design: a row exists only for a shape this entity drives, and every
    // other shape sits at the mesh's own default. Editing therefore always writes the whole array
    // back through the property's typed accessor - one undo step per edit, and the component's
    // validator decides whether it lands.
    std::vector<Scene::MorphTargetWeightUVE> weights;
    property.getValue(instance, &weights);

    const bool writable = property.IsAuthoringWritableUVE() && IsAuthoringCommandAllowedUVE();
    std::optional<std::size_t> removeIndex;
    bool edited = false;

    ImGui::BeginDisabled(!writable);
    {
        const std::string header = "Blend Shapes (" + std::to_string(weights.size()) + ")##morph-weights";
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(header.c_str());
        if (ImGui::IsItemHovered()) {
            DrawTooltipUVE(property);
        }
        if (weights.size() < Scene::kMaximumMorphTargetWeightsUVE) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Add")) {
                weights.push_back(Scene::MorphTargetWeightUVE{});
                edited = true;
            }
        }
    }
    if (BeginPropertyRowsUVE("##morph-weights")) {
        for (std::size_t index = 0U; index < weights.size(); ++index) {
            Scene::MorphTargetWeightUVE& row = weights[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Shape");
            ImGui::TableSetColumnIndex(1);
            {
                std::array<char, 128> targetName{};
                const std::size_t copyLength = std::min(row.target.size(), targetName.size() - 1U);
                std::memcpy(targetName.data(), row.target.data(), copyLength);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputText("##morph-target-name", targetName.data(), targetName.size())) {
                    row.target = targetName.data();
                    edited = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("The blend shape's name in the mesh asset, exactly as imported "
                                      "(for example jawOpen). Typed rather than listed so the "
                                      "Inspector never blocks on loading the mesh.");
                }
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Weight");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            {
                // The full authored range, not 0..1: glTF permits corrective shapes to pull in
                // either direction, and a slider that silently clamped them would make a shape
                // impossible to author as exported.
                float weight = row.weight;
                if (ImGui::DragFloat("##morph-weight", &weight, 0.01F, -4.0F, 4.0F, "%.3f")) {
                    row.weight = weight;
                    edited = true;
                }
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("");
            ImGui::TableSetColumnIndex(1);
            if (ImGui::SmallButton("Remove")) {
                removeIndex = index;
                edited = true;
            }
            ImGui::PopID();
        }
        if (weights.empty()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("None");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("The mesh sits at its default shape. Add one to drive a blend shape.");
        }
        ImGui::EndTable();
    }
    ImGui::EndDisabled();

    if (!edited) {
        return;
    }
    if (removeIndex.has_value()) {
        weights.erase(weights.begin() + static_cast<std::ptrdiff_t>(*removeIndex));
    }
    static_cast<void>(SetSelectedComponentPropertyUVE(entry, property, &weights));
}

void EditorUVE::DrawLodGroupThresholdsPropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                                  const Core::TypeMetadataPropertyUVE& property,
                                                  const void* const instance) {
    if (property.typeId != Scene::kPropertyTypeFloatListUVE || property.getValue == nullptr ||
        instance == nullptr || property.elementCount == 0U) {
        return;
    }

    // Only the levels in use are shown. `levelCount` is the authority and it is read from the
    // component itself, so the list can never disagree with the switch that decides it - and the
    // rows past it, which would do nothing when edited, are simply not offered.
    const auto* const group = static_cast<const Scene::LodGroup3DComponentUVE*>(instance);
    std::size_t levelCount = group->levelCount;
    if (levelCount == 0U || levelCount > property.elementCount) {
        levelCount = 1U; // A hand-edited component; the validator refuses the write that made it.
    }
    std::array<float, Scene::kMaximumLodLevelsUVE> thresholds{};
    property.getValue(instance, thresholds.data());

    const bool writable = property.IsAuthoringWritableUVE() && IsAuthoringCommandAllowedUVE();
    bool edited = false;
    ImGui::BeginDisabled(!writable);
    if (BeginPropertyRowsUVE("##lod-thresholds")) {
        const float step = RangeStepUVE(property, 0.5F);
        const float minimum = RangeMinimumUVE(property);
        const float maximum = RangeMaximumUVE(property);
        for (std::size_t level = 0U; level < levelCount; ++level) {
            ImGui::PushID(static_cast<int>(level));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            const std::string label = "Level " + std::to_string(level + 1U);
            ImGui::TextUnformatted(label.c_str());
            if (ImGui::IsItemHovered()) {
                DrawTooltipUVE(property);
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            edited |= ImGui::DragFloat("##threshold", &thresholds[level], step, minimum, maximum, "%.2f m");
            ImGui::PopID();
        }

        // The rule the thresholds add up to, stated where they are authored: past the last one the
        // object is not drawn at all, and the hysteresis band moves that point out by the same
        // fraction the levels use.
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("Culled past");
        ImGui::TableSetColumnIndex(1);
        if (!group->enabled) {
            ImGui::TextDisabled("Never - the group is disabled");
        } else {
            const float hysteresis = group->hysteresis > 0.0F ? group->hysteresis : 0.0F;
            ImGui::Text("%.2f m", thresholds[levelCount - 1U] * (1.0F + hysteresis));
        }
        ImGui::EndTable();
    }
    ImGui::EndDisabled();

    if (edited) {
        static_cast<void>(SetSelectedComponentPropertyUVE(entry, property, thresholds.data()));
    }
}

void EditorUVE::DrawLodGroupMeshesPropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                              const Core::TypeMetadataPropertyUVE& property,
                                              const void* const instance) {
    if (property.typeId != Scene::kPropertyTypeAssetGuidListUVE || property.getValue == nullptr ||
        instance == nullptr || property.elementCount == 0U) {
        return;
    }

    const auto* const group = static_cast<const Scene::LodGroup3DComponentUVE*>(instance);
    std::size_t levelCount = group->levelCount;
    if (levelCount == 0U || levelCount > property.elementCount) {
        levelCount = 1U;
    }
    std::array<Asset::AssetGuidUVE, Scene::kMaximumLodLevelsUVE> meshes{};
    property.getValue(instance, meshes.data());

    const bool writable = property.IsAuthoringWritableUVE() && IsAuthoringCommandAllowedUVE();
    std::optional<Asset::AssetGuidUVE> picked;
    std::size_t pickedLevel = 0U;
    bool cleared = false;
    ImGui::BeginDisabled(!writable);
    if (BeginPropertyRowsUVE("##lod-meshes")) {
        for (std::size_t level = 0U; level < levelCount; ++level) {
            ImGui::PushID(static_cast<int>(level));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            const std::string label = "Level " + std::to_string(level + 1U);
            ImGui::TextUnformatted(label.c_str());
            if (ImGui::IsItemHovered()) {
                DrawTooltipUVE(property);
            }
            ImGui::TableSetColumnIndex(1);
            if (const std::optional<Asset::AssetGuidUVE> chosen =
                    DrawAssetPickerUVE("##level", meshes[level], ".uvmodel")) {
                if (*chosen == Asset::kInvalidAssetGuidUVE) {
                    cleared = true;
                    pickedLevel = level;
                } else {
                    picked = chosen;
                    pickedLevel = level;
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndDisabled();

    if (!cleared && !picked.has_value()) {
        return;
    }
    // The whole array is written back either way, so a cleared level and an assigned one are the
    // same kind of edit: one property write, one undo entry, and the component's own validity rule
    // as the last word.
    meshes[pickedLevel] = cleared ? Asset::kInvalidAssetGuidUVE : *picked;
    static_cast<void>(SetSelectedComponentPropertyUVE(entry, property, meshes.data()));
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
                         property.typeId == Scene::kPropertyTypeUInt8UVE ||
                         property.typeId == Scene::kPropertyTypeBitMask32UVE ||
                         property.typeId == Scene::kPropertyTypeVector2UVE ||
                         property.typeId == Scene::kPropertyTypeVector3UVE ||
                         property.typeId == Scene::kPropertyTypeRectUVE ||
                         property.typeId == Scene::kPropertyTypeColorUVE ||
                         property.typeId == Scene::kPropertyTypeLinearColorUVE ||
                         property.typeId == Scene::kPropertyTypeQuaternionUVE;
    if (!trivial) {
        return false;
    }
    property.getValue(defaults.GetUVE(), buffer.data());
    return SetSelectedComponentPropertyUVE(entry, property, buffer.data());
}

} // namespace UVE::Editor
