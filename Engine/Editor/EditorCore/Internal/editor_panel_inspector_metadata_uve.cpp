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
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

#include <imgui.h>

#include "uve/asset/asset_guid_uve.h"
#include "uve/component/entity_uve.h"
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
/// Removing a component is still a per-kind command (EditorSceneComponentKindUVE), because a
/// generic detach needs the ECS's erased add/remove path, which is not public. That is one small
/// table in one place instead of the eight dispatch sites the Inspector used to carry, and a
/// component absent from it simply offers no Remove button - which is already correct for
/// Transform, Visibility and Name, none of which were ever removable.
[[nodiscard]] const std::vector<std::pair<std::string, EditorSceneComponentKindUVE>>&
GetRemovableComponentKindsUVE() {
    static const std::vector<std::pair<std::string, EditorSceneComponentKindUVE>> kinds{
        {"component.camera", EditorSceneComponentKindUVE::Camera},
        {"component.mesh", EditorSceneComponentKindUVE::Mesh},
        {"component.light", EditorSceneComponentKindUVE::Light},
        {"component.collider", EditorSceneComponentKindUVE::Collider},
        {"component.rigid_body", EditorSceneComponentKindUVE::RigidBody},
        {"component.audio_source", EditorSceneComponentKindUVE::AudioSource},
        {"component.particle_emitter", EditorSceneComponentKindUVE::ParticleEmitter},
        {"component.script", EditorSceneComponentKindUVE::Script},
        {"component.animation_player", EditorSceneComponentKindUVE::AnimationPlayer},
        {"component.world_environment", EditorSceneComponentKindUVE::WorldEnvironment},
        {"component.character_controller", EditorSceneComponentKindUVE::CharacterController},
        {"component.canvas", EditorSceneComponentKindUVE::Canvas},
        {"component.ui_text", EditorSceneComponentKindUVE::UIText},
        {"component.ui_image", EditorSceneComponentKindUVE::UIImage},
        {"component.ui_button", EditorSceneComponentKindUVE::UIButton},
        {"component.physics_interpolation", EditorSceneComponentKindUVE::PhysicsInterpolation},
        {"component.editor_description", EditorSceneComponentKindUVE::EditorDescription},
    };
    return kinds;
}

[[nodiscard]] std::optional<EditorSceneComponentKindUVE> FindRemovableKindUVE(const std::string& typeId) {
    for (const auto& [candidate, kind] : GetRemovableComponentKindsUVE()) {
        if (candidate == typeId) {
            return kind;
        }
    }
    return std::nullopt;
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

    for (const TypeMetadataEntryUVE* const entry : entries) {
        static_cast<void>(m_inspectorDrawerRegistry.RegisterDrawerUVE(InspectorDrawerEntryUVE{
            DrawerIdForTypeIdUVE(entry->typeId),
            [this, entry](const Scene::EntityUVE entity) {
                return IsDocumentEntityUVE(entity) &&
                       m_services->GetEntityManagerUVE().HasComponentUVE(entity, entry->typeIndex);
            },
            [this, entry](const Scene::EntityUVE entity) {
                DrawMetadataComponentDrawerUVE(entity, *entry);
            },
        }));
    }
}

void EditorUVE::DrawMetadataComponentDrawerUVE(const Scene::EntityUVE entity,
                                               const TypeMetadataEntryUVE& entry) {
    if (!IsDocumentEntityUVE(entity) || entity != m_selectedEntity) {
        return;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE(entity, entry.typeIndex)) {
        return;
    }
    const void* const instance = entityManager.GetComponentPointerUVE(entity, entry.typeIndex);

    ImGui::Separator();
    // Collapsible, and collapsed state is remembered per section by Dear ImGui's own storage for
    // the window - which is what makes a long Inspector usable at all.
    ImGui::PushID(entry.typeId.c_str());
    if (!ImGui::CollapsingHeader(entry.displayName.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PopID();
        return;
    }

    for (const TypeMetadataPropertyUVE& property : entry.properties) {
        if (!IsPropertyVisibleUVE(property, instance)) {
            continue;
        }
        ImGui::PushID(property.name.c_str());
        DrawMetadataPropertyRowUVE(entry, property, instance);
        ImGui::PopID();
    }

    if (const std::optional<EditorSceneComponentKindUVE> kind = FindRemovableKindUVE(entry.typeId);
        kind.has_value()) {
        const std::string label = "Remove " + entry.displayName;
        if (ImGui::Button(label.c_str())) {
            static_cast<void>(RemoveSelectedSceneComponentUVE(*kind));
        }
    }
    ImGui::PopID();
}

void EditorUVE::DrawMetadataPropertyRowUVE(const TypeMetadataEntryUVE& entry,
                                           const TypeMetadataPropertyUVE& property, const void* instance) {
    // Runtime-owned and read-only state is shown, not hidden: seeing what the simulation computed
    // is how you debug it. It is drawn disabled so the widget cannot report an edit that would be
    // overwritten on the next update.
    const bool writable = property.IsAuthoringWritableUVE() && IsAuthoringCommandAllowedUVE();
    ImGui::BeginDisabled(!writable);
    ImGui::TextUnformatted(property.displayName.c_str());
    DrawTooltipUVE(property);
    ImGui::SetNextItemWidth(-1.0F);

    bool edited = false;
    if (!property.enumEntries.empty()) {
        // Collapsed dropdown, which is what Combo is by default - an enum never occupies the
        // section with one row per option.
        std::int64_t current = 0;
        property.getValue(instance, &current);
        std::vector<const char*> labels;
        labels.reserve(property.enumEntries.size());
        int selected = 0;
        for (std::size_t index = 0; index < property.enumEntries.size(); ++index) {
            labels.push_back(property.enumEntries[index].label.c_str());
            if (property.enumEntries[index].value == current) {
                selected = static_cast<int>(index);
            }
        }
        if (ImGui::Combo("##value", &selected, labels.data(), static_cast<int>(labels.size()))) {
            const std::int64_t chosen = property.enumEntries[static_cast<std::size_t>(selected)].value;
            edited = SetSelectedComponentPropertyUVE(entry, property, &chosen);
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
        if (ImGui::DragFloat2("##value", &value.x, RangeStepUVE(property, 0.01F), RangeMinimumUVE(property),
                              RangeMaximumUVE(property))) {
            edited = SetSelectedComponentPropertyUVE(entry, property, &value);
        }
    } else if (property.typeId == Scene::kPropertyTypeVector3UVE) {
        Math::Vector3UVE value{};
        property.getValue(instance, &value);
        if (ImGui::DragFloat3("##value", &value.x, RangeStepUVE(property, 0.01F), RangeMinimumUVE(property),
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
        std::array<char, 256> buffer{};
        value.copy(buffer.data(), std::min(value.size(), buffer.size() - 1U));
        // EnterReturnsTrue so a string edit is one history entry on commit rather than one per
        // keystroke, which is what a per-frame InputText would record.
        if (ImGui::InputText("##value", buffer.data(), buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
            std::string committed{buffer.data()};
            edited = SetSelectedComponentPropertyUVE(entry, property, &committed);
        }
    } else if (property.typeId == Scene::kPropertyTypeAssetGuidUVE) {
        // Shown, not edited: there is no asset picker yet, and a text field that accepts an
        // arbitrary 64-bit number is a way to author a dangling reference, not a way to pick an
        // asset. It becomes editable when the picker exists.
        Asset::AssetGuidUVE value{};
        property.getValue(instance, &value);
        ImGui::BeginDisabled();
        ImGui::Text("%016llX", static_cast<unsigned long long>(value.value));
        ImGui::EndDisabled();
        ImGui::TextDisabled("Assign from the Content Browser; there is no inline picker yet.");
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

    if (writable) {
        ImGui::SameLine();
        // Reset asks the type's own factory what a fresh component would hold, so a default can
        // never drift from the one the constructor actually applies.
        if (ImGui::SmallButton("Reset")) {
            edited = ResetSelectedComponentPropertyUVE(entry, property) || edited;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Restore the value a newly added %s would have.", entry.displayName.c_str());
        }
    }
    static_cast<void>(edited);
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
    // Every remaining declared value type is trivially copyable and fits the buffer: bool, the
    // integer types, float, Vector2/3, Quaternion, and an enum's std::int64_t.
    property.getValue(defaults.GetUVE(), buffer.data());
    return SetSelectedComponentPropertyUVE(entry, property, buffer.data());
}

} // namespace UVE::Editor
