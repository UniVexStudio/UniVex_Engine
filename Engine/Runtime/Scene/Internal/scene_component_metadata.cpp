// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "scene_component_metadata.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "area_component.h"
#include "animation_sequencer_component.h"
#include "montage_sequencer_component.h"
#include "animation_graph_component.h"
#include "auto_translate_component.h"
#include "audio_source_component.h"
#include "bone_modifier_component.h"
#include "camera_component.h"
#include "canvas_component.h"
#include "character_controller_component.h"
#include "collider_component.h"
#include "editor_description_component.h"
#include "hierarchy_component.h"
#include "light_component.h"
#include "light_emitter_component.h"
#include "mesh_component.h"
#include "morph_target_component.h"
#include "skin_component.h"
#include "name_component.h"
#include "object_metadata_component.h"
#include "particle_emitter_component.h"
#include "process_component.h"
#include "thread_group_component.h"
#include "physics_interpolation_component.h"
#include "physics_object_component.h"
#include "primitive_mesh_component.h"
#include "render_instance_component.h"
#include "solid_body_component.h"
#include "rigid_3d_component.h"
#include "script_component.h"
#include "surface_instance_component.h"
#include "transform_component.h"
#include "ui_anchor_component.h"
#include "ui_button_component.h"
#include "ui_checkbox_component.h"
#include "ui_dropdown_component.h"
#include "ui_image_component.h"
#include "ui_layout_container_component.h"
#include "ui_progress_bar_component.h"
#include "ui_view_3d_component.h"
#include "ui_scroll_container_component.h"
#include "ui_scrollbar_component.h"
#include "ui_slider_component.h"
#include "ui_text_component.h"
#include "ui_text_input_component.h"
#include "ui_tooltip_component.h"
#include "ui_tween_component.h"
#include "visibility_component.h"
#include "engine_assert.h"
#include "logging_macros.h"
#include "abstract_animation_objects_3d.h"
#include "abstract_objects_3d.h"
#include "abstract_physics_objects_3d.h"
#include "bone_attachment_3d.h"
#include "decal_3d.h"
#include "directional_light_3d.h"
#include "fog_volume_3d.h"
#include "kinematic_3d.h"
#include "lod_group_3d.h"
#include "projectile_3d.h"
#include "reflection_probe_3d.h"
#include "sky_3d.h"
#include "hitbox_3d.h"
#include "hurtbox_3d.h"
#include "interaction_area_3d.h"
#include "ray_cast_3d.h"
#include "skeleton_3d.h"
#include "nav_mesh_volume_3d.h"
#include "nav_seeker_3d.h"
#include "occluder_3d.h"
#include "spring_arm_3d.h"
#include "spawn_point_3d.h"
#include "health.h"
#include "player_3d.h"
#include "two_bone_ik_3d.h"
#include "full_body_ik_3d.h"
#include "ragdoll_3d.h"
#include "visibility_region_3d.h"
#include "world_environment_3d.h"
#include "world_partition_3d.h"
#include "quaternion.h"

namespace UVE::Scene {
namespace {

using Core::MakeEnumProperty;
using Core::MakeProperty;
using Core::TypeMetadataEnumEntry;
using Core::TypeMetadataEntry;
using Core::TypeMetadataKind;
using Core::TypeMetadataPropertyFlags;
using Core::TypeMetadataProperty;
using Core::TypeMetadataRegistry;

/// Declares one editable property. `typeId` is one of the kPropertyType*UVE constants; the caller
/// adjusts flags/range on the returned value for the cases that need it.
template <auto MemberPointer>
[[nodiscard]] TypeMetadataProperty Declare(std::string name, std::string displayName,
                                                 const Strings::StringId typeId) {
    return MakeProperty<MemberPointer>(std::move(name), std::move(displayName), typeId, true);
}

/// Declares a property that is read and written but never drawn.
///
/// It is the middle ground between a property and a field that does not exist: the value stays in
/// the file, the serializer and every accessor keep working, and the Inspector offers no row. That
/// is what a control that has been replaced by a better one leaves behind - the data for scenes
/// that already have it, and no new place to set it by accident.
[[nodiscard]] TypeMetadataProperty Hidden(TypeMetadataProperty property) {
    property.flags = TypeMetadataPropertyFlags::Hidden;
    return property;
}

/// Declares a property a runtime system owns. It is shown so an author can see what the simulation
/// is doing, never written by authoring, and never persisted - saving it would restore a stale
/// cache over whatever the system computed on load.
template <auto MemberPointer>
[[nodiscard]] TypeMetadataProperty DeclareRuntimeState(std::string name, std::string displayName,
                                                             const Strings::StringId typeId) {
    TypeMetadataProperty property = Declare<MemberPointer>(std::move(name), std::move(displayName),
                                                                 typeId);
    property.flags = TypeMetadataPropertyFlags::RuntimeState;
    return property;
}

template <auto MemberPointer>
[[nodiscard]] TypeMetadataProperty DeclareEnum(std::string name, std::string displayName,
                                                     std::vector<TypeMetadataEnumEntry> options) {
    return MakeEnumProperty<MemberPointer>(std::move(name), std::move(displayName),
                                              kPropertyTypeEnum, true,
                                              std::move(options));
}

/// A runtime-owned enum. Declared through the enum path rather than the plain one so its
/// accessors speak std::int64_t and a generic consumer can actually read it back - a read-only
/// property still has to be readable, or the inspector shows nothing where the resolved answer
/// should be.
template <auto MemberPointer>
[[nodiscard]] TypeMetadataProperty DeclareRuntimeStateEnum(
    std::string name, std::string displayName, std::vector<TypeMetadataEnumEntry> options) {
    TypeMetadataProperty property =
        DeclareEnum<MemberPointer>(std::move(name), std::move(displayName), std::move(options));
    property.flags = TypeMetadataPropertyFlags::RuntimeState;
    return property;
}

/// Adds an inclusive numeric range with the given editing step.
[[nodiscard]] TypeMetadataProperty WithRange(TypeMetadataProperty property, const double minimum,
                                                   const double maximum, const double step) {
    property.range = {true, minimum, maximum, step};
    return property;
}

[[nodiscard]] TypeMetadataProperty WithTooltip(TypeMetadataProperty property, std::string tooltip) {
    property.tooltip = std::move(tooltip);
    return property;
}

/// Routes a property through a named custom drawer. Used only where a generic editor would be
/// wrong rather than merely plain - see the rotation and entity-reference cases below.
[[nodiscard]] TypeMetadataProperty WithCustomDrawer(TypeMetadataProperty property,
                                                          std::string drawerId) {
    property.customDrawerId = std::move(drawerId);
    return property;
}

/// States how many elements a fixed-capacity list value holds, for the type that has no other way
/// to say it (see kPropertyTypeEntityList).
[[nodiscard]] TypeMetadataProperty WithElementCount(TypeMetadataProperty property,
                                                          const std::size_t elementCount) {
    property.elementCount = elementCount;
    return property;
}

/// Places a property in a named sub-group of its section. A group's properties are declared
/// together, after the section's ungrouped ones.
[[nodiscard]] TypeMetadataProperty InGroup(TypeMetadataProperty property, std::string group) {
    property.section = std::move(group);
    return property;
}

/// Shows a property only while a bool switch on the same component is on - the field after a
/// "Enabled" toggle means nothing while the toggle is off, so it is not shown then.
template <auto SwitchPointer>
[[nodiscard]] TypeMetadataProperty WhenOn(TypeMetadataProperty property) {
    property.isVisible = +[](const void* instance) {
        using OwnerT = typename Core::Detail::MemberPointerTraits<decltype(SwitchPointer)>::Owner;
        return static_cast<const OwnerT*>(instance)->*SwitchPointer;
    };
    return property;
}

[[nodiscard]] TypeMetadataEntry MakeEntry(Strings::StringId typeId, std::string cppName,
                                                std::string displayName, const std::int32_t order,
                                                std::vector<TypeMetadataProperty> properties) {
    TypeMetadataEntry entry{TypeMetadataKind::Component, std::move(typeId), std::move(displayName),
                               1U, std::move(properties), {}};
    entry.order = order;
    entry.cppName = std::move(cppName);
    return entry;
}

/// Binds an entry to its C++ type and appends it to `entries`. Separated from MakeEntry so the
/// native type appears once per component, right next to the properties that belong to it.
template <typename ComponentT>
void Add(std::vector<TypeMetadataEntry>& entries, TypeMetadataEntry entry) {
    Core::BindType<ComponentT>(entry);
    entries.push_back(std::move(entry));
}

/// Add for a component with a whole-value rule, so a generic editor enforces it too - and so
/// the generic serializer validates what it loads. `IsValid` is any callable taking
/// `const ComponentT&` (a noexcept function pointer, a plain one, a derived-to-base reuse): the
/// entry's isInstanceValid is a plain function pointer, so every spelling converts.
template <typename ComponentT, auto IsValid>
void AddValidated(std::vector<TypeMetadataEntry>& entries, TypeMetadataEntry entry) {
    entry.isInstanceValid = +[](const void* instance) { return IsValid(*static_cast<const ComponentT*>(instance)); };
    Add<ComponentT>(entries, std::move(entry));
}

[[nodiscard]] bool IsMontageSequencerComponentValid(const MontageSequencerComponent&) noexcept {
    return true;
}

// ---------------------------------------------------------------------------------------------
// The declarations themselves. Each component states what it exposes exactly once, here, instead
// of being re-described by every consumer that needs to know.
// ---------------------------------------------------------------------------------------------

void DeclareIdentityAndTransform(std::vector<TypeMetadataEntry>& entries) {
    AddValidated<NameComponent, &IsNameComponentValid>(
        entries, MakeEntry("component.name", "NameComponent", "Name", kSectionOrderIdentity,
                              {Declare<&NameComponent::name>("name", "Name", kPropertyTypeString)}));

    // Rotation is the one place a generic editor would be actively wrong. The quaternion, the
    // authored Euler angles and the edit mode are three views of one piece of state that must be
    // written together (see TransformComponent::localEulerRadians on why the angles are stored
    // rather than re-extracted). A custom drawer owns all three; the rest of the transform is
    // ordinary data and is declared ordinarily.
    Add<TransformComponent>(
        entries,
        MakeEntry("component.transform", "TransformComponent", "Transform", kSectionOrderTransform,
            {
                Declare<&TransformComponent::localPosition>("localPosition", "Position",
                                                                 kPropertyTypeVector3),
                WithCustomDrawer(Declare<&TransformComponent::localRotation>(
                                        "localRotation", "Rotation", kPropertyTypeQuaternion),
                                    "transform.rotation"),
                Declare<&TransformComponent::localScale>("localScale", "Scale",
                                                               kPropertyTypeVector3),
                WithTooltip(
                    Declare<&TransformComponent::topLevel>("topLevel", "Top Level",
                                                                 kPropertyTypeBool),
                    "Ignore the parent's transform. The entity stays a child for every other "
                    "purpose - outliner, deletion, saving and visibility inheritance."),
            }));

    // visibilityParent holds an entity reference, which a text field cannot author and which the
    // serializer has to remap through its file-local id table. Both facts are declared rather than
    // rediscovered: the flag tells the serializer, the custom drawer tells the inspector.
    TypeMetadataProperty visibilityParent = WithCustomDrawer(
        Declare<&VisibilityComponent::visibilityParent>("visibilityParent", "Visibility Parent",
                                                              kPropertyTypeEntity),
        "visibility.parent");
    visibilityParent.flags = TypeMetadataPropertyFlags::EntityReference;
    visibilityParent.tooltip =
        "Inherit visibility from this entity instead of from the transform parent. Empty means the "
        "transform parent.";

    Add<VisibilityComponent>(
        entries,
        MakeEntry("component.visibility", "VisibilityComponent", "Visibility", kSectionOrderVisibility,
                     {
                         Declare<&VisibilityComponent::visible>("visible", "Visible",
                                                                      kPropertyTypeBool),
                         DeclareRuntimeState<&VisibilityComponent::visibleInHierarchy>(
                             "visibleInHierarchy", "Visible In Hierarchy", kPropertyTypeBool),
                         std::move(visibilityParent),
                     }));

    TypeMetadataProperty parent = WithCustomDrawer(
        Declare<&HierarchyComponent::parent>("parent", "Parent", kPropertyTypeEntity),
        "hierarchy.parent");
    parent.flags = TypeMetadataPropertyFlags::EntityReference;
    Add<HierarchyComponent>(entries, MakeEntry("component.hierarchy", "HierarchyComponent", "Hierarchy",
                                                        kSectionOrderIdentity, {std::move(parent)}));
    // No entry for DisabledComponent on purpose: the registry's contract (pinned by
    // EveryDeclarationRegistersAndCarriesItsNativeType) is entries with at least one property for
    // the generic consumers, and a fieldless tag has nothing for them. Its seams are the
    // Set/IsEntityEnabled helpers and SceneSerializer's hand-written branch, neither of
    // which needs the registry — the same "no metadata entry" corner Folder and Marker3D live in.
}

void DeclareRendering(std::vector<TypeMetadataEntry>& entries) {
    AddValidated<CameraComponent, &IsCameraComponentValid>(
        entries,
        MakeEntry("component.camera", "CameraComponent", "Camera", kSectionOrderTypeSpecific,
            {
                // Which camera the viewport looks through answers a different question from the
                // lens so it sits above the group: one switch and then the numbers it describes.
                Declare<&CameraComponent::current>("current", "Current", kPropertyTypeBool),
                InGroup(DeclareEnum<&CameraComponent::projection>(
                               "projection", "Mode",
                               {{0, "Perspective"}, {1, "Orthographic"}, {2, "Human Eye"}}),
                           "Lens"),
                InGroup([] {
                               TypeMetadataProperty property = WithRange(
                                   Declare<&CameraComponent::fieldOfViewDegrees>(
                                       "fieldOfViewDegrees", "Field Of View", kPropertyTypeFloat),
                                   1.0, 179.0, 0.5);
                               property.isVisible = +[](const void* instance) {
                                   return static_cast<const CameraComponent*>(instance)->projection ==
                                          CameraProjectionMode::Perspective;
                               };
                               return property;
                           }(),
                           "Lens"),
                InGroup([] {
                               TypeMetadataProperty property = WithRange(
                                   Declare<&CameraComponent::orthographicSize>("orthographicSize", "Size",
                                                                                     kPropertyTypeFloat),
                                   0.001, 10000.0, 0.1);
                               property.isVisible = +[](const void* instance) {
                                   return static_cast<const CameraComponent*>(instance)->projection ==
                                          CameraProjectionMode::Orthographic;
                               };
                               return property;
                           }(),
                           "Lens"),
                InGroup(WithRange(Declare<&CameraComponent::nearPlane>("nearPlane", "Near Plane",
                                                                                   kPropertyTypeFloat),
                                        0.001, 10000.0, 0.01),
                           "Lens"),
                InGroup(WithRange(Declare<&CameraComponent::farPlane>("farPlane", "Far Plane",
                                                                                  kPropertyTypeFloat),
                                        0.002, 100000.0, 1.0),
                           "Lens"),
            }));

    AddValidated<LightComponent, &IsLightComponentValid>(
        entries,
        MakeEntry("component.light", "LightComponent", "Light", kSectionOrderTypeSpecific,
            {
                DeclareEnum<&LightComponent::type>("type", "Type",
                                                         {{0, "Directional"}, {1, "Point"}, {2, "Spot"}}),
                Declare<&LightComponent::color>("color", "Color", kPropertyTypeLinearColor),
                WithRange(Declare<&LightComponent::intensity>("intensity", "Intensity",
                                                                       kPropertyTypeFloat),
                             0.0, 1000.0, 0.05),
                // Range and cone angle apply to some light types and not others. Declaring that
                // as a predicate keeps the inspector from having to know what a spot light is.
                [] {
                    TypeMetadataProperty property = WithRange(
                        Declare<&LightComponent::range>("range", "Range", kPropertyTypeFloat),
                        0.0, 10000.0, 0.1);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const LightComponent*>(instance)->type !=
                               LightType::Directional;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&LightComponent::spotAngleDegrees>(
                                         "spotAngleDegrees", "Spot Angle", kPropertyTypeFloat),
                                     0.0, 89.0, 0.5);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const LightComponent*>(instance)->type == LightType::Spot;
                    };
                    return property;
                }(),
            }));

    AddValidated<MeshComponent, &IsMeshComponentValid>(
        entries,
        MakeEntry("component.mesh", "MeshComponent", "MeshInstance3D", kSectionOrderTypeSpecific,
                     {
                         WithTooltip(WithCustomDrawer(Declare<&MeshComponent::meshGuid>(
                                                                "meshGuid", "Mesh", kPropertyTypeAssetGuid),
                                                            "asset:uvmodel"),
                                        "An imported model. Import a .glb or .gltf (Blender: File > Export > "
                                        "glTF 2.0) from the Asset Table."),
                         WithTooltip(WithCustomDrawer(Declare<&MeshComponent::materialGuid>(
                                                                "materialGuid", "Material", kPropertyTypeAssetGuid),
                                                            "asset:uvmat"),
                                        "The surface material. Without one the mesh is drawn in neutral grey."),
                         WithCustomDrawer(Declare<&MeshComponent::visibilityLayers>(
                                                 "visibilityLayers", "Visibility Layers", kPropertyTypeBitMask32),
                                             std::string(kLayerMaskDrawerRender)),
                     }));

    TypeMetadataEntry skinEntry =
        MakeEntry("component.skin", "SkinComponent", "Skin", kSectionOrderTypeSpecific,
                     {
                         Declare<&SkinComponent::bindCount>("bindCount", "Bind Count",
                                                                  kPropertyTypeUInt32),
                     });
    skinEntry.nestedUnderTypeIds = {"component.mesh"};
    AddValidated<SkinComponent, &IsSkinComponentValid>(entries, std::move(skinEntry));

    // Blend shapes. Deliberately NOT nested under component.mesh: nestable types are drawn inside
    // their host's section, and a face rig's weights are the one thing on a mesh an animator edits
    // continuously - burying the list in the Mesh section makes it a scroll away every time. The
    // list itself is a custom drawer because rows carry a shape NAME (chosen from the mesh's own
    // targets, not typed) and a weight, which no generic scalar row can express.
    TypeMetadataEntry morphTargetEntry =
        MakeEntry("component.morph_target", "MorphTargetComponent", "Morph Targets",
                     kSectionOrderTypeSpecific,
                     {
                         WithTooltip(
                             WithCustomDrawer(
                                 Declare<&MorphTargetComponent::weights>("weights", "Weights",
                                                                               Strings::StringId{"MorphTargetWeights"}),
                                 "morph-target-weights"),
                             "One row per blend shape this entity drives. A shape with no row stays at "
                             "the mesh's own default, so a 52-shape face rig stores only the few "
                             "shapes that are off zero."),
                         WithTooltip(
                             Declare<&MorphTargetComponent::enabled>("enabled", "Enabled",
                                                                           kPropertyTypeBool),
                             "Off leaves the weights authored but contributes nothing, so the mesh "
                             "sits at its default shape."),
                     });
    AddValidated<MorphTargetComponent, &IsMorphTargetComponentValid>(entries, std::move(morphTargetEntry));

    // One component behind BoxMesh3D, SphereMesh3D and PlaneMesh3D; its section carries the name
    // of the object it is on.
    TypeMetadataEntry primitive =
        MakeEntry("component.primitive_mesh", "PrimitiveMeshComponent", "PrimitiveMesh3D", kSectionOrderTypeSpecific,
                     {
                         // The object's kind is its shape (a BoxMesh3D is a box): kept in the
                         // file, never offered, so a box cannot be turned into a sphere here.
                         Hidden(DeclareEnum<&PrimitiveMeshComponent::kind>(
                             "kind", "Shape",
                             {{0, "Cube"}, {1, "UV Sphere"}, {2, "Plane"}, {3, "Capsule"}, {4, "Cylinder"}})),
                         // Kept in the file, never offered: scenes saved while a primitive had its
                         // own tint still carry the value, and the renderer still honours it, but
                         // the Inspector no longer sets one. A surface has one colour - the
                         // material's albedo - and a second control beside an empty material slot
                         // meant two answers to the same question.
                         Hidden(Declare<&PrimitiveMeshComponent::baseColor>(
                             "baseColor", "Base Color", kPropertyTypeColor)),
                         // Same asset slot the mesh instance uses, so a primitive and an imported
                         // mesh are shaded through one material path.
                         WithTooltip(
                             WithCustomDrawer(
                                 Declare<&PrimitiveMeshComponent::materialGuid>(
                                     "materialGuid", "Material", kPropertyTypeAssetGuid),
                                 "asset:uvmat"),
                             "The surface material: its albedo is this primitive's colour. Without "
                             "one the primitive is drawn with the engine's default lit material."),
                     });
    primitive.sectionTitle = +[](const void* instance) -> const char* {
        switch (static_cast<const PrimitiveMeshComponent*>(instance)->kind) {
            case PrimitiveMeshKind::UVSphere:
                return "SphereMesh3D";
            case PrimitiveMeshKind::Plane:
                return "PlaneMesh3D";
            case PrimitiveMeshKind::Capsule:
                return "CapsuleMesh3D";
            case PrimitiveMeshKind::Cylinder:
                return "CylinderMesh3D";
            case PrimitiveMeshKind::Cube:
                break;
        }
        return "BoxMesh3D";
    };
    // A primitive is the MESH, not a second feature bolted to it. On a MeshInstance3D the mesh
    // section already picks the shape and authors its material and colour, so drawing this section
    // beside it showed every one of its three rows twice - the duplicate an author had to read
    // past to find out which "Material" was the live one. Nested, the host draws it and drops the
    // section; on an object that has a primitive and no mesh component (BoxMesh3D and friends) it
    // still stands on its own, so nothing becomes unreachable.
    primitive.nestedUnderTypeIds = {"component.mesh"};
    AddValidated<PrimitiveMeshComponent, &IsPrimitiveMeshComponentValid>(entries, std::move(primitive));

    AddValidated<WorldEnvironment3DComponent, &IsWorldEnvironment3DObjectComponentValid>(
        entries,
        MakeEntry("component.world_environment", "WorldEnvironment3DComponent", "WorldEnvironment", kSectionOrderTypeSpecific,
            {
                WithTooltip(
                    DeclareEnum<&WorldEnvironment3DComponent::ambientSource>(
                        "ambientSource", "Ambient Light Source",
                        {{static_cast<std::int64_t>(WorldEnvironmentAmbientSource::None), "None"},
                         {static_cast<std::int64_t>(WorldEnvironmentAmbientSource::FlatColor), "Flat Color"},
                         {static_cast<std::int64_t>(WorldEnvironmentAmbientSource::Sky), "Sky"},
                         {static_cast<std::int64_t>(WorldEnvironmentAmbientSource::EnvironmentMap),
                          "Environment Map"}}),
                    "None disables only ambient lighting; Flat Color uses Ambient Tint and Energy; Sky keeps "
                    "the existing hemispherical sky/ground fill; Environment Map samples the Sky Asset for "
                    "diffuse and specular ambient light."),
                [] {
                    TypeMetadataProperty property = Declare<&WorldEnvironment3DComponent::skyColor>(
                        "skyColor", "Sky Color", kPropertyTypeColor);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponent*>(instance)->ambientSource ==
                               WorldEnvironmentAmbientSource::Sky;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property = Declare<&WorldEnvironment3DComponent::horizonColor>(
                        "horizonColor", "Horizon Color", kPropertyTypeColor);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponent*>(instance)->ambientSource ==
                               WorldEnvironmentAmbientSource::Sky;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property = Declare<&WorldEnvironment3DComponent::groundColor>(
                        "groundColor", "Ground Color", kPropertyTypeColor);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponent*>(instance)->ambientSource ==
                               WorldEnvironmentAmbientSource::Sky;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::skyCurve>(
                                         "skyCurve", "Sky Curve", kPropertyTypeFloat),
                                     0.001, 4.0, 0.01);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponent*>(instance)->ambientSource ==
                               WorldEnvironmentAmbientSource::Sky;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::groundCurve>(
                                         "groundCurve", "Ground Curve", kPropertyTypeFloat),
                                     0.001, 4.0, 0.01);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponent*>(instance)->ambientSource ==
                               WorldEnvironmentAmbientSource::Sky;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property = Declare<&WorldEnvironment3DComponent::ambientColor>(
                        "ambientColor", "Ambient Tint", kPropertyTypeColor);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponent*>(instance)->ambientSource !=
                               WorldEnvironmentAmbientSource::None;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::ambientEnergy>(
                                         "ambientEnergy", "Ambient Energy", kPropertyTypeFloat),
                                     0.0, 100.0, 0.05);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponent*>(instance)->ambientSource !=
                               WorldEnvironmentAmbientSource::None;
                    };
                    return property;
                }(),

                WithRange(Declare<&WorldEnvironment3DComponent::exposure>(
                                 "exposure", "Exposure", kPropertyTypeFloat),
                             0.0, 100.0, 0.05),
                Declare<&WorldEnvironment3DComponent::fogEnabled>("fogEnabled", "Fog",
                                                                            kPropertyTypeBool),
                [] {
                    TypeMetadataProperty property = DeclareEnum<&WorldEnvironment3DComponent::fogMode>(
                        "fogMode", "Fog Mode",
                        {{static_cast<std::int64_t>(WorldEnvironmentFogMode::Linear), "Linear"},
                         {static_cast<std::int64_t>(WorldEnvironmentFogMode::Exponential), "Exponential"},
                         {static_cast<std::int64_t>(WorldEnvironmentFogMode::Height), "Height"}});
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponent*>(instance)->fogEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property = Declare<&WorldEnvironment3DComponent::fogColor>(
                        "fogColor", "Fog Color", kPropertyTypeColor);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponent*>(instance)->fogEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::fogDensity>(
                                         "fogDensity", "Fog Density", kPropertyTypeFloat),
                                     0.0, 1.0, 0.001);
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponent*>(instance);
                        return environment->fogEnabled && environment->fogMode != WorldEnvironmentFogMode::Linear;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::fogStart>(
                                         "fogStart", "Fog Start", kPropertyTypeFloat),
                                     0.0, 1000000.0, 0.1);
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponent*>(instance);
                        return environment->fogEnabled && environment->fogMode == WorldEnvironmentFogMode::Linear;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::fogEnd>(
                                         "fogEnd", "Fog End", kPropertyTypeFloat),
                                     0.1, 1000000.0, 0.1);
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponent*>(instance);
                        return environment->fogEnabled && environment->fogMode == WorldEnvironmentFogMode::Linear;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::fogSkyAffect>(
                                         "fogSkyAffect", "Fog Sky", kPropertyTypeFloat),
                                     0.0, 1.0, 0.01);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponent*>(instance)->fogEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::fogHeight>(
                                         "fogHeight", "Fog Height", kPropertyTypeFloat),
                                     -1000.0, 10000.0, 0.5);
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponent*>(instance);
                        return environment->fogEnabled && environment->fogMode == WorldEnvironmentFogMode::Height;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::fogHeightFalloff>(
                                         "fogHeightFalloff", "Fog Falloff", kPropertyTypeFloat),
                                     0.1, 10000.0, 1.0);
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponent*>(instance);
                        return environment->fogEnabled && environment->fogMode == WorldEnvironmentFogMode::Height;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::fogSunScatter>(
                                         "fogSunScatter", "Fog Sun Scatter", kPropertyTypeFloat),
                                     0.0, 1.0, 0.01);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponent*>(instance)->fogEnabled;
                    };
                    return property;
                }(),
                Declare<&WorldEnvironment3DComponent::postProcessingEnabled>(
                    "postProcessingEnabled", "Post Processing", kPropertyTypeBool),
                [] {
                    TypeMetadataProperty property = Declare<&WorldEnvironment3DComponent::bloomEnabled>(
                        "bloomEnabled", "Bloom", kPropertyTypeBool);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponent*>(instance)->postProcessingEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::bloomIntensity>(
                                         "bloomIntensity", "Bloom Intensity", kPropertyTypeFloat),
                                     0.0, 8.0, 0.01);
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponent*>(instance);
                        return environment->postProcessingEnabled && environment->bloomEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::bloomThreshold>(
                                         "bloomThreshold", "Bloom Threshold", kPropertyTypeFloat),
                                     0.0, 8.0, 0.05);
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponent*>(instance);
                        return environment->postProcessingEnabled && environment->bloomEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::bloomSoftKnee>(
                                         "bloomSoftKnee", "Bloom Soft Knee", kPropertyTypeFloat),
                                     0.0, 1.0, 0.01);
                    property.tooltip = "Width of the gradual bright-pass transition as a fraction of the threshold. "
                                       "0 preserves a hard threshold; larger values soften the transition.";
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponent*>(instance);
                        return environment->postProcessingEnabled && environment->bloomEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::bloomMipCount>(
                                         "bloomMipCount", "Bloom Mip Count", kPropertyTypeUInt32),
                                     1.0, static_cast<double>(kMaximumWorldEnvironmentBloomMipCount), 1.0);
                    property.tooltip = "Number of progressively smaller half-resolution scales in the bloom blur. "
                                       "1 preserves the legacy single-scale bloom.";
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponent*>(instance);
                        return environment->postProcessingEnabled && environment->bloomEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property = Declare<&WorldEnvironment3DComponent::ssaoEnabled>(
                        "ssaoEnabled", "SSAO", kPropertyTypeBool);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponent*>(instance)->postProcessingEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::ssaoIntensity>(
                                         "ssaoIntensity", "SSAO Intensity", kPropertyTypeFloat),
                                     0.0, 4.0, 0.05);
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponent*>(instance);
                        return environment->postProcessingEnabled && environment->ssaoEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::ssaoRadius>(
                                         "ssaoRadius", "SSAO Radius", kPropertyTypeFloat),
                                     0.05, 4.0, 0.05);
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponent*>(instance);
                        return environment->postProcessingEnabled && environment->ssaoEnabled;
                    };
                    return property;
                }(),
                WithRange(Declare<&WorldEnvironment3DComponent::brightness>(
                                 "brightness", "Brightness", kPropertyTypeFloat),
                             -1.0, 1.0, 0.01),
                WithRange(Declare<&WorldEnvironment3DComponent::contrast>(
                                 "contrast", "Contrast", kPropertyTypeFloat),
                             0.0, 2.0, 0.01),
                WithRange(Declare<&WorldEnvironment3DComponent::saturation>(
                                 "saturation", "Saturation", kPropertyTypeFloat),
                             0.0, 2.0, 0.01),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::vignetteIntensity>(
                                         "vignetteIntensity", "Vignette Intensity", kPropertyTypeFloat),
                                     0.0, 1.0, 0.01);
                    property.tooltip = "Darken the image toward its edges; 0 disables the vignette.";
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponent*>(instance)->postProcessingEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::vignetteRadius>(
                                         "vignetteRadius", "Vignette Radius", kPropertyTypeFloat),
                                     0.0, 1.0, 0.01);
                    property.tooltip = "Normalized screen-space radius where edge darkening begins.";
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponent*>(instance);
                        return environment->postProcessingEnabled && environment->vignetteIntensity > 0.0F;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::chromaticAberrationIntensity>(
                                         "chromaticAberrationIntensity", "Chromatic Aberration",
                                         kPropertyTypeFloat),
                                     0.0, 1.0, 0.01);
                    property.tooltip = "Separate red and blue sampling toward screen edges by up to four target "
                                       "texels; 0 disables the effect.";
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponent*>(instance)->postProcessingEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::filmGrainIntensity>(
                                         "filmGrainIntensity", "Film Grain", kPropertyTypeFloat),
                                     0.0, 1.0, 0.01);
                    property.tooltip = "Animated monochrome noise, up to +/-0.04 at full intensity; 0 disables it.";
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponent*>(instance)->postProcessingEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::lensDistortionIntensity>(
                                         "lensDistortionIntensity", "Lens Distortion", kPropertyTypeFloat),
                                     0.0, 1.0, 0.01);
                    property.tooltip = "Radially expand scene features toward the edges; maximum 12% at full "
                                       "intensity, 0 disables it.";
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponent*>(instance)->postProcessingEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        Declare<&WorldEnvironment3DComponent::depthOfFieldEnabled>(
                            "depthOfFieldEnabled", "Depth of Field", kPropertyTypeBool);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponent*>(instance)->postProcessingEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property = DeclareEnum<&WorldEnvironment3DComponent::depthOfFieldFocusMode>(
                        "depthOfFieldFocusMode", "Focus Mode",
                        {{static_cast<std::int64_t>(WorldEnvironmentDepthOfFieldFocusMode::Manual), "Manual"},
                         {static_cast<std::int64_t>(WorldEnvironmentDepthOfFieldFocusMode::ScreenCenter),
                          "Screen Center Autofocus"}});
                    property.tooltip = "Use the authored focus distance or focus on the closest visible surface at "
                                       "the center of the screen.";
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponent*>(instance);
                        return environment->postProcessingEnabled && environment->depthOfFieldEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property = DeclareEnum<&WorldEnvironment3DComponent::depthOfFieldBokehShape>(
                        "depthOfFieldBokehShape", "Bokeh Shape",
                        {{static_cast<std::int64_t>(WorldEnvironmentDepthOfFieldBokehShape::Circular), "Circular"},
                         {static_cast<std::int64_t>(WorldEnvironmentDepthOfFieldBokehShape::Hexagonal), "Hexagonal"}});
                    property.tooltip = "Shape the radial blur sample kernel as a circle or a six-sided aperture.";
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponent*>(instance);
                        return environment->postProcessingEnabled && environment->depthOfFieldEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::depthOfFieldFocusDistance>(
                                         "depthOfFieldFocusDistance", "Focus Distance", kPropertyTypeFloat),
                                     0.1, 1000.0, 0.1);
                    property.tooltip = "Manual focus distance along the camera ray, in world units.";
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponent*>(instance);
                        return environment->postProcessingEnabled && environment->depthOfFieldEnabled &&
                               environment->depthOfFieldFocusMode ==
                                   WorldEnvironmentDepthOfFieldFocusMode::Manual;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::depthOfFieldAperture>(
                                         "depthOfFieldAperture", "Aperture", kPropertyTypeFloat),
                                     0.0, 1.0, 0.01);
                    property.tooltip = "Normalized blur strength; full aperture permits up to an eight-pixel radius.";
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponent*>(instance);
                        return environment->postProcessingEnabled && environment->depthOfFieldEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::depthOfFieldQuality>(
                                         "depthOfFieldQuality", "Blur Quality", kPropertyTypeUInt32),
                                     0.0, 2.0, 1.0);
                    property.tooltip = "Quality tier: 4, 8, or 12 taps arranged in the selected bokeh shape.";
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponent*>(instance);
                        return environment->postProcessingEnabled && environment->depthOfFieldEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        Declare<&WorldEnvironment3DComponent::motionBlurEnabled>(
                            "motionBlurEnabled", "Motion Blur", kPropertyTypeBool);
                    property.tooltip = "Blur camera-induced motion between consecutive frames; per-object blur is "
                                       "not yet supported.";
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponent*>(instance)->postProcessingEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::motionBlurStrength>(
                                         "motionBlurStrength", "Motion Blur Strength", kPropertyTypeFloat),
                                     0.0, 1.0, 0.01);
                    property.tooltip = "Scale camera motion vectors; 0 is a strict passthrough.";
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponent*>(instance);
                        return environment->postProcessingEnabled && environment->motionBlurEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataProperty property =
                        WithRange(Declare<&WorldEnvironment3DComponent::motionBlurSampleCount>(
                                         "motionBlurSampleCount", "Motion Blur Samples", kPropertyTypeUInt32),
                                     4.0, 12.0, 4.0);
                    property.tooltip = "Camera-motion integration quality: 4, 8, or 12 samples.";
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponent*>(instance);
                        return environment->postProcessingEnabled && environment->motionBlurEnabled;
                    };
                    return property;
                }(),
                Declare<&WorldEnvironment3DComponent::colorFilter>(
                    "colorFilter", "Color Filter", kPropertyTypeColor),
                // A slot, not a bare path field. It reads "Empty" until a sky is added, and the
                // menu behind it is where a sky is created - the same shape as a material slot, so
                // an author looks in one place for "what is on this surface" and for "what is over
                // this level". The texture stays assignable from the same menu, because a
                // panorama is one of the ways a sky can be made rather than a separate concept.
                WithTooltip(WithCustomDrawer(Declare<&WorldEnvironment3DComponent::skyAssetPath>(
                                                       "skyAssetPath", "Sky", kPropertyTypeString),
                                                   "sky:asset"),
                               "The sky over this level. Empty keeps the procedural sky. New Sky adds a Sky "
                               "to this environment, where its bands, atmosphere, stars and clouds are "
                               "authored as a group; a Sky texture is an equirectangular image sampled "
                               "instead of the bands."),
            }));

    AddValidated<ParticleEmitterComponent, &IsParticleEmitterComponentValid>(
        entries, MakeEntry("component.particle_emitter", "ParticleEmitterComponent", "ParticleEmitter3D",
                              kSectionOrderTypeSpecific,
                              {WithRange(Declare<&ParticleEmitterComponent::maxParticles>(
                                                "maxParticles", "Max Particles", kPropertyTypeUInt32),
                                            0.0, 1000000.0, 1.0),
                               WithTooltip(Declare<&ParticleEmitterComponent::emitting>(
                                                  "emitting", "Emitting", kPropertyTypeBool),
                                              "Off, live particles stay but none spawn."),
                               WithRange(Declare<&ParticleEmitterComponent::emissionRate>(
                                                "emissionRate", "Emission Rate", kPropertyTypeFloat),
                                            0.0, 1000000.0, 0.1),
                               WithRange(Declare<&ParticleEmitterComponent::lifetimeSeconds>(
                                                "lifetimeSeconds", "Lifetime", kPropertyTypeFloat),
                                            0.01, 3600.0, 0.01),
                               WithTooltip(Declare<&ParticleEmitterComponent::systemAssetPath>(
                                                  "systemAssetPath", "Particle System", kPropertyTypeString),
                                              "Optional .uvfx particle system asset. Empty keeps the simple built-in emitter."),
                               WithTooltip(Declare<&ParticleEmitterComponent::autoPlay>(
                                                  "autoPlay", "Auto Play", kPropertyTypeBool),
                                              "Start playing the Particle System automatically when the scene starts."),
                               WithTooltip(Declare<&ParticleEmitterComponent::seed>(
                                                  "seed", "Seed", kPropertyTypeUInt32),
                                              "Random seed for deterministic playback (0 derives a per-entity seed).")}));

    // LODGroup3D's own section. The chain is declared as a prefix: `levelCount` says how many of
    // the two lists below are in use, and both lists are drawn by a block drawer that shows exactly
    // those - a threshold past the last level is a number that does nothing, and an array of eight
    // rows where three matter reads as eight settings. The resolved level and the cull verdict are
    // runtime state: they describe the frame the renderer last ran, not the scene.
    AddValidated<LodGroup3DComponent, &IsLodGroup3DObjectComponentValid>(
        entries,
        MakeEntry("component.lod_group_3d", "LodGroup3DComponent", "LODGroup3D", kSectionOrderTypeSpecific,
            {
                WithTooltip(Declare<&LodGroup3DComponent::enabled>("enabled", "Enabled",
                                                                            kPropertyTypeBool),
                               "Off, the object draws at level 0 and is never distance-culled - what "
                               "an author wants while placing it."),
                WithRange(
                    WithTooltip(Declare<&LodGroup3DComponent::levelCount>("levelCount", "Levels",
                                                                                   kPropertyTypeUInt8),
                                   "How many levels of the chain are in use. Thresholds and level "
                                   "meshes past this are ignored, so a chain can be shortened "
                                   "without rewriting the numbers."),
                    1.0, 8.0, 1.0),
                WithRange(
                    WithTooltip(Declare<&LodGroup3DComponent::hysteresis>("hysteresis", "Hysteresis",
                                                                                  kPropertyTypeFloat),
                                   "How far past a threshold the object must go before it swaps, as a "
                                   "fraction of that threshold; it comes back at the same fraction "
                                   "under. Zero is the plain threshold rule."),
                    0.0, 0.5, 0.01),
                WithTooltip(
                    WithCustomDrawer(
                        WithElementCount(
                            Declare<&LodGroup3DComponent::distanceThresholds>(
                                "distanceThresholds", "Thresholds", kPropertyTypeFloatList),
                            kMaximumLodLevels),
                        "lod-group-thresholds"),
                    "The distance each level takes over at, nearest first. Past the last one the "
                    "object is not drawn at all."),
                WithTooltip(
                    WithCustomDrawer(
                        WithElementCount(
                            Declare<&LodGroup3DComponent::lodMeshGuids>("lodMeshGuids", "Level Meshes",
                                                                              kPropertyTypeAssetGuidList),
                            kMaximumLodLevels),
                        "lod-group-meshes"),
                    "The mesh drawn at each level. A level with no mesh draws the object's own Mesh "
                    "component mesh."),
                InGroup(DeclareRuntimeState<&LodGroup3DComponent::currentLevel>(
                               "currentLevel", "Current Level", kPropertyTypeUInt8),
                           "Result"),
                InGroup(DeclareRuntimeState<&LodGroup3DComponent::culledByDistance>(
                               "culledByDistance", "Distance Culled", kPropertyTypeBool),
                           "Result"),
            }));
}

void DeclareSky(std::vector<TypeMetadataEntry>& entries) {
    using S = Sky3DComponent;
    // Three groups, in the order an author settles them: what the sky is made of, the bands that
    // only exist for one of those answers, and the image that only exists for the other. Visibility
    // follows the source rather than every row being always-on, so the Inspector never offers a
    // panorama path to a gradient sky - a value that is stored but cannot affect the picture is
    // worse than a value that is not shown.
    const auto sourceIsPanorama = [](const void* instance) {
        return static_cast<const Sky3DComponent*>(instance)->source == SkySource::Panorama;
    };
    const auto sourceIsNotPanorama = [](const void* instance) {
        return static_cast<const Sky3DComponent*>(instance)->source != SkySource::Panorama;
    };
    const auto sourceIsAtmosphere = [](const void* instance) {
        return static_cast<const Sky3DComponent*>(instance)->source == SkySource::Atmosphere;
    };
    const auto scattered = [sourceIsAtmosphere](TypeMetadataProperty property) {
        property.isVisible = sourceIsAtmosphere;
        return property;
    };
    const auto coloredByCurve = [sourceIsNotPanorama](TypeMetadataProperty property) {
        property.isVisible = sourceIsNotPanorama;
        return property;
    };
    const auto coloredWithPanorama = [sourceIsPanorama](TypeMetadataProperty property) {
        property.isVisible = sourceIsPanorama;
        return property;
    };

    TypeMetadataEntry sky = MakeEntry("component.sky_3d", "Sky3DComponent", "Sky", kSectionOrderTypeSpecific,
            {
                WithTooltip(Declare<&S::enabled>("enabled", "Enabled", kPropertyTypeBool),
                               "Off, the sky is not drawn and the scene falls back to its WorldEnvironment. "
                               "The object stays in the scene so its settings survive a toggle."),
                WithTooltip(
                    DeclareEnum<&S::source>(
                        "source", "Source",
                        {{static_cast<std::int64_t>(SkySource::Gradient), "Gradient"},
                         {static_cast<std::int64_t>(SkySource::Panorama), "Panorama"},
                         {static_cast<std::int64_t>(SkySource::Atmosphere), "Atmosphere"}}),
                    "Gradient blends three authored bands. Panorama samples an equirectangular image. "
                    "Atmosphere scatters sunlight through a model of the air, which is what makes the "
                    "zenith blue and a setting sun red."),

                InGroup(coloredByCurve(WithTooltip(
                               Declare<&S::skyColor>("skyColor", "Sky Color", kPropertyTypeColor),
                               "The colour overhead. Small refinements here change the light more than the "
                               "colour: this is also the ambient that reaches upward faces.")),
                           "Gradient"),
                InGroup(coloredByCurve(WithTooltip(
                               Declare<&S::horizonColor>("horizonColor", "Horizon Color", kPropertyTypeColor),
                               "The colour at eye level. It meets the ground band here, so this is the row "
                               "that sets how far away the horizon reads.")),
                           "Gradient"),
                InGroup(coloredByCurve(WithTooltip(
                               Declare<&S::groundColor>("groundColor", "Ground Color", kPropertyTypeColor),
                               "The colour below the horizon, seen where no geometry covers it. Match it to "
                               "the level's floor or the seam becomes visible at the edges.")),
                           "Gradient"),
                InGroup(coloredByCurve(WithTooltip(
                               WithRange(Declare<&S::skyCurve>("skyCurve", "Sky Curve", kPropertyTypeFloat),
                                            0.001, 4.0, 0.01),
                               "How fast the zenit takes over from the horizon: small keeps a wide band of "
                               "horizon colour, large compresses it into a line.")),
                           "Gradient"),
                InGroup(coloredByCurve(WithTooltip(
                               WithRange(Declare<&S::groundCurve>("groundCurve", "Ground Curve",
                                                                        kPropertyTypeFloat),
                                            0.001, 4.0, 0.01),
                               "How fast the ground band takes over below the horizon. Large makes the "
                               "transition abrupt, which suits a hard sea line.")),
                           "Gradient"),

                InGroup(coloredWithPanorama(WithTooltip(
                               WithCustomDrawer(
                                   Declare<&S::panoramaPath>("panoramaPath", "Panorama",
                                                               kPropertyTypeString),
                                   // A published texture or the source image it would be imported
                                   // from: the runtime loader reads both, so the row offers both.
                                   "file:uvtex,png,jpg,jpeg,bmp,tga"),
                               "An equirectangular image: width twice its height, the horizon across the "
                               "middle. Empty leaves the gradient showing.")),
                           "Panorama"),
                InGroup(coloredWithPanorama(WithTooltip(
                               WithRange(Declare<&S::panoramaRotationDegrees>(
                                                "panoramaRotationDegrees", "Rotation", kPropertyTypeFloat),
                                            -180.0, 180.0, 0.5),
                               "Spins the image about the world's up axis, in degrees, so the part worth "
                               "looking at can be pointed at the scene.")),
                           "Panorama"),
                InGroup(coloredWithPanorama(WithTooltip(
                               WithRange(Declare<&S::panoramaExposure>("panoramaExposure", "Exposure",
                                                                             kPropertyTypeFloat),
                                            -10.0, 10.0, 0.05),
                               "Brightness of the panorama in stops, so an image authored for one exposure "
                               "can be matched to this scene without editing the file.")),
                           "Panorama"),

                // Each of these scales one physical term, so 1.0 is Earth and 0 removes the term.
                // A row with a tooltip that says what the term DOES is worth more here than a
                // range: nobody tunes ozone by feel, they tune it to get the twilight they want.
                InGroup(scattered(WithTooltip(
                               WithRange(Declare<&S::atmosphereRayleigh>("atmosphereRayleigh",
                                                                               "Rayleigh", kPropertyTypeFloat),
                                            0.0, 10.0, 0.01),
                               "Density of the gas itself. This is the blue: it scatters short wavelengths "
                               "far more than long ones, so raising it deepens the zenith and reddens the "
                               "sunset. 0 leaves a colourless sky.")),
                           "Atmosphere"),
                InGroup(scattered(WithTooltip(
                               WithRange(Declare<&S::atmosphereMie>("atmosphereMie", "Mie",
                                                                          kPropertyTypeFloat),
                                            0.0, 10.0, 0.01),
                               "Density of haze and dust. It scatters almost evenly across colours, so "
                               "raising it greys the sky out and thickens the glow around the sun - a "
                               "humid day, or a sandstorm at the far end.")),
                           "Atmosphere"),
                InGroup(scattered(WithTooltip(
                               WithRange(Declare<&S::atmosphereOzone>("atmosphereOzone", "Ozone",
                                                                            kPropertyTypeFloat),
                                            0.0, 10.0, 0.01),
                               "Ozone absorbs mostly green, high in the stratosphere. It is what keeps the "
                               "twilight sky violet instead of brown-grey; removing it is the fastest way "
                               "to see what it was doing.")),
                           "Atmosphere"),
                InGroup(scattered(WithTooltip(
                               WithRange(Declare<&S::atmosphereMieAnisotropy>("atmosphereMieAnisotropy",
                                                                                    "Haze Anisotropy",
                                                                                    kPropertyTypeFloat),
                                            -0.95, 0.95, 0.01),
                               "How tightly the haze throws light forward. Near 0 it scatters evenly; near "
                               "1 the bright halo around the sun becomes a narrow beam and the rest of the "
                               "sky darkens.")),
                           "Atmosphere"),
                InGroup(scattered(WithTooltip(
                               WithRange(Declare<&S::atmosphereMultiScatter>("atmosphereMultiScatter",
                                                                                   "Multiple Scatter",
                                                                                   kPropertyTypeFloat),
                                            0.0, 4.0, 0.01),
                               "Light that bounced before reaching the eye. This is why the horizon is bright "
                               "and shadows are not black; 0 gives a hard single-scatter sky that goes dark "
                               "away from the sun.")),
                           "Atmosphere"),
                InGroup(scattered(WithTooltip(
                               WithRange(Declare<&S::atmosphereIntensity>("atmosphereIntensity",
                                                                                "Sky Intensity",
                                                                                kPropertyTypeFloat),
                                            0.0, 100.0, 0.05),
                               "Brightness of the scattered sky as a whole, for matching a scene whose "
                               "exposure is not the default. It scales the sky and not the sunlight.")),
                           "Atmosphere"),
                InGroup(scattered(WithTooltip(
                               WithRange(Declare<&S::atmosphereSamples>("atmosphereSamples", "Quality",
                                                                              kPropertyTypeUInt32),
                                            4.0, 64.0, 1.0),
                               "Samples along each view ray. More resolves the horizon band and the twilight "
                               "gradient more finely; the cost is linear and this is the most expensive part "
                               "of the sky.")),
                           "Atmosphere"),

                // Night and cloud are overlays on any source, not part of the scattering model, so
                // they are not gated on `source` - a gradient sky gets the same stars and the same
                // weather. Both start off, because a scene that had no stars before this object
                // existed must not quietly gain some.
                InGroup(WithTooltip(WithRange(Declare<&S::starBrightness>("starBrightness",
                                                                                     "Star Brightness",
                                                                                     kPropertyTypeFloat),
                                                       0.0, 100.0, 0.01),
                                          "How bright the stars are. 0 draws none, which is the default; raise "
                                          "it to open the sky at night. They fade out on their own as the sun "
                                          "rises, so this is safe to leave set."),
                           "Night"),
                InGroup(WithTooltip(WithRange(Declare<&S::starDensity>("starDensity", "Star Density",
                                                                                  kPropertyTypeFloat),
                                                       10.0, 1200.0, 1.0),
                                          "How finely the sky is divided into cells that may hold a star. Higher "
                                          "means more and smaller stars; low values give a handful of large "
                                          "ones."),
                           "Night"),
                InGroup(WithTooltip(WithRange(Declare<&S::starSeed>("starSeed", "Star Seed",
                                                                               kPropertyTypeFloat),
                                                       0.0, 1000.0, 1.0),
                                          "Which stars are where. Changing it reshuffles the whole field, so two "
                                          "levels can share a brightness and not the same constellations."),
                           "Night"),
                InGroup(WithTooltip(WithRange(Declare<&S::milkyWayStrength>("milkyWayStrength",
                                                                                        "Milky Way",
                                                                                        kPropertyTypeFloat),
                                                       0.0, 10.0, 0.01),
                                          "Strength of the band of starlight across the sky. 0 hides it. It is "
                                          "dusty rather than smooth on purpose: a clean band reads as a bright "
                                          "smudge."),
                           "Night"),

                InGroup(WithTooltip(Declare<&S::cloudsEnabled>("cloudsEnabled", "Clouds",
                                                                       kPropertyTypeBool),
                                          "Off by default. A flat deck at Cloud Height, sampled from noise and "
                                          "moved by the wind."),
                           "Clouds"),
                InGroup(WhenOn<&S::cloudsEnabled>(WithTooltip(
                               WithRange(Declare<&S::cloudCoverage>("cloudCoverage", "Coverage",
                                                                          kPropertyTypeFloat),
                                            0.0, 1.0, 0.01),
                               "How much of the sky is covered: 0 bare, 1 overcast.")),
                           "Clouds"),
                InGroup(WhenOn<&S::cloudsEnabled>(WithTooltip(
                               WithRange(Declare<&S::cloudDensity>("cloudDensity", "Density",
                                                                         kPropertyTypeFloat),
                                            0.0, 1.0, 0.01),
                               "How opaque the covered part is. Below 1 the sky shows through the cloud, "
                               "which is how a thin high deck looks.")),
                           "Clouds"),
                InGroup(WhenOn<&S::cloudsEnabled>(WithTooltip(
                               WithRange(Declare<&S::cloudAltitude>("cloudAltitude", "Height (m)",
                                                                          kPropertyTypeFloat),
                                            10.0, 20000.0, 10.0),
                               "Height of the deck above the eye, in metres. It sets how fast the cloud slides "
                               "past as the camera moves, which is most of what makes a low deck feel close.")),
                           "Clouds"),
                InGroup(WhenOn<&S::cloudsEnabled>(WithTooltip(
                               WithRange(Declare<&S::cloudWindSpeed>("cloudWindSpeed", "Wind Speed",
                                                                           kPropertyTypeFloat),
                                            -200.0, 200.0, 0.5),
                               "How fast the deck drifts, in metres per second. Negative blows the other way.")),
                           "Clouds"),
                InGroup(WhenOn<&S::cloudsEnabled>(WithTooltip(
                               WithRange(Declare<&S::cloudWindDirectionDegrees>("cloudWindDirectionDegrees",
                                                                                      "Wind Direction",
                                                                                      kPropertyTypeFloat),
                                            -180.0, 180.0, 1.0),
                               "Compass bearing the wind blows toward, in degrees. 0 is +X, 90 is +Z.")),
                           "Clouds"),
                InGroup(WhenOn<&S::cloudsEnabled>(WithTooltip(
                               WithRange(Declare<&S::cloudSoftness>("cloudSoftness", "Softness",
                                                                          kPropertyTypeFloat),
                                            0.01, 1.0, 0.01),
                               "Width of the fade at each cloud's edge. Small reads as hard-edged cumulus, "
                               "large as haze.")),
                           "Clouds"),
                InGroup(WhenOn<&S::cloudsEnabled>(WithTooltip(
                               WithRange(Declare<&S::cloudSilverLining>("cloudSilverLining", "Silver Lining",
                                                                              kPropertyTypeFloat),
                                            0.0, 20.0, 0.05),
                               "How strongly thin cloud brightens toward the sun - the rim that appears when a "
                               "cloud crosses it. 0 keeps the edge flat.")),
                           "Clouds"),

                // The link between the sky above and the light on the ground. Off by default and on
                // purpose: a sky that relights a level the moment it is added would break the rule
                // that adding an object changes nothing until it is authored. Each strength is
                // hidden behind its own switch - a dial that does nothing is worse than no dial.
                InGroup(WithTooltip(Declare<&S::ambientFromSky>("ambientFromSky", "Ambient From Sky",
                                                                       kPropertyTypeBool),
                                          "Take the scene's fill light from this sky instead of the World "
                                          "Environment. Shadows go the colour of the sky rather than black, and "
                                          "the fill rises and sets with the sun."),
                           "Lighting Link"),
                InGroup(WhenOn<&S::ambientFromSky>(WithTooltip(
                               WithRange(Declare<&S::ambientStrength>("ambientStrength", "Ambient Strength",
                                                                          kPropertyTypeFloat),
                                            0.0, 1.0, 0.01),
                               "Scales the fill the sky throws. A brightness dial, not a physical term.")),
                           "Lighting Link"),
                InGroup(WithTooltip(Declare<&S::fogFromSky>("fogFromSky", "Fog From Sky",
                                                                   kPropertyTypeBool),
                                          "Tint the distance with this sky's horizon, so far geometry dissolves "
                                          "into the sky it stands against - what aerial perspective actually is."),
                           "Lighting Link"),
                InGroup(WhenOn<&S::fogFromSky>(WithTooltip(
                               WithRange(Declare<&S::fogStrength>("fogStrength", "Fog Strength",
                                                                      kPropertyTypeFloat),
                                            0.0, 1.0, 0.01),
                               "How far the fog moves toward the sky's own colour. 0 leaves the environment's "
                               "fog exactly as authored, 1 makes it the horizon.")),
                           "Lighting Link"),
                InGroup(WithTooltip(Declare<&S::reflectionsFromSky>("reflectionsFromSky",
                                                                           "Reflections From Sky",
                                                                           kPropertyTypeBool),
                                          "Reflect this sky in metallic surfaces, so a mirror outdoors shows a "
                                          "horizon instead of one flat colour. No capture pass: the sky's three "
                                          "bands are the reflection. A captured environment map or reflection "
                                          "probe still wins where one is set."),
                           "Lighting Link"),

                // Output and behaviour of the sky as a whole, after the Lighting Link because they
                // are about the picture rather than about the scene it lights.
                InGroup(WithTooltip(
                               WithRange(Declare<&S::luminance>("luminance", "Luminance",
                                                                    kPropertyTypeFloat),
                                            0.0, 8.0, 0.01),
                               "How bright this sky is, whichever source is drawing it. 1 is as authored, 2 "
                               "is one stop brighter, 0 is black. It does not change the light the sky throws "
                               "on the scene - that is Ambient Strength, under Lighting Link."),
                           "Output"),
                InGroup(WithTooltip(Declare<&S::animate>("animate", "Animate",
                                                                 kPropertyTypeBool),
                                          "Whether the sky's own clock runs. Off freezes the cloud drift where "
                                          "it is, so two frames can be compared without the sky moving "
                                          "underneath the difference."),
                           "Advanced"),
            });
    // A Sky added from the environment's own Sky slot belongs to that environment, and its
    // settings are that section's business. Nested, the environment draws them as one more
    // expandable group inside itself; standalone - the Sky object from the object catalogue, which
    // has no environment on its entity - it still draws as its own section, so neither way of
    // making a sky is a dead end.
    sky.nestedUnderTypeIds = {"component.world_environment"};
    AddValidated<S, &IsSky3DObjectComponentValid>(entries, std::move(sky));
}

void DeclarePhysics(std::vector<TypeMetadataEntry>& entries) {
    TypeMetadataEntry collider = MakeEntry("component.collider", "ColliderComponent", "Collider", kSectionOrderTypeSpecific,
            {
                WithTooltip(Declare<&ColliderComponent::disabled>("disabled", "Disabled",
                                                                          kPropertyTypeBool),
                               "Off this shape is ignored by collision, rays, and areas."),
                InGroup(DeclareEnum<&ColliderComponent::shapeType>(
                               "shapeType", "Shape",
                               {{0, "BoxShape3D"}, {1, "SphereShape3D"}, {2, "CapsuleShape3D"}, {3, "CylinderShape3D"}}),
                           "Shape"),
                InGroup([] {
                               TypeMetadataProperty property = Declare<&ColliderComponent::halfExtents>(
                                   "halfExtents", "Half Extents", kPropertyTypeVector3);
                               property.isVisible = +[](const void* instance) {
                                   return static_cast<const ColliderComponent*>(instance)->shapeType ==
                                          ColliderShapeType::Box;
                               };
                               return property;
                           }(),
                           "Shape"),
                InGroup([] {
                               TypeMetadataProperty property = WithRange(
                                   Declare<&ColliderComponent::radius>("radius", "Radius",
                                                                              kPropertyTypeFloat),
                                   0.0, 10000.0, 0.01);
                               property.isVisible = +[](const void* instance) {
                                   return static_cast<const ColliderComponent*>(instance)->shapeType !=
                                          ColliderShapeType::Box;
                               };
                               return property;
                           }(),
                           "Shape"),
                InGroup([] {
                               TypeMetadataProperty property = WithRange(
                                   Declare<&ColliderComponent::height>("height", "Height",
                                                                              kPropertyTypeFloat),
                                   0.0, 10000.0, 0.01);
                               property.isVisible = +[](const void* instance) {
                                   const auto shape = static_cast<const ColliderComponent*>(instance)->shapeType;
                                   return shape == ColliderShapeType::Capsule || shape == ColliderShapeType::Cylinder;
                               };
                               return property;
                           }(),
                           "Shape"),
                InGroup(WithCustomDrawer(
                               WithTooltip(Declare<&ColliderComponent::collisionLayer>("collisionLayer", "Layer",
                                                                                                kPropertyTypeBitMask32),
                                              "The layers this object is on - what others can find it on."),
                               std::string(kLayerMaskDrawerPhysics)),
                           "Filter"),
                InGroup(WithCustomDrawer(
                               WithTooltip(Declare<&ColliderComponent::collisionMask>("collisionMask", "Mask",
                                                                                               kPropertyTypeBitMask32),
                                              "The layers this object looks for - what it collides with or detects."),
                               std::string(kLayerMaskDrawerPhysics)),
                           "Filter"),
                InGroup(WithRange(Declare<&ColliderComponent::friction>("friction", "Friction",
                                                                                    kPropertyTypeFloat),
                                        0.0, 1.0, 0.01),
                           "Surface"),
                InGroup(WithRange(Declare<&ColliderComponent::restitution>("restitution", "Restitution",
                                                                                       kPropertyTypeFloat),
                                        0.0, 1.0, 0.01),
                           "Surface"),
                InGroup(WithRange(Declare<&ColliderComponent::density>("density", "Density",
                                                                                   kPropertyTypeFloat),
                                        0.0, 10000.0, 0.01),
                           "Surface"),
            });
    // A collider is always part of some object rather than a feature of its own: the collision a
    // BoxMesh3D/SphereMesh3D/PlaneMesh3D is created with sits in that object's section, and a body's
    // or area's shape, layer and mask sit in PhysicsObject3D's. A character's collider is the body
    // itself, so Character3D hosts it first: that section authors the two rows a body owns (Layer
    // and Mask) from the collider's own declarations, and does not repeat the shape under it.
    collider.nestedUnderTypeIds = {"component.character_controller", "component.primitive_mesh",
                                   "component.physics_object"};
    AddValidated<ColliderComponent, &IsColliderComponentValid>(entries, std::move(collider));

    AddValidated<Rigid3DComponent, &IsRigid3DComponentValid>(
        entries,
        MakeEntry("component.rigid_body", "Rigid3DComponent", "Rigid Body", kSectionOrderTypeSpecific,
            {
                WithRange(Declare<&Rigid3DComponent::mass>("mass", "Mass",
                                                                       kPropertyTypeFloat),
                             0.0, 100000.0, 0.01),
                Declare<&Rigid3DComponent::isKinematic>("isKinematic", "Kinematic",
                                                                kPropertyTypeBool),
                WithRange(Declare<&Rigid3DComponent::drag>("drag", "Drag",
                                                                       kPropertyTypeFloat),
                             0.0, 100.0, 0.01),
                WithRange(Declare<&Rigid3DComponent::gravityScale>(
                                 "gravityScale", "Gravity Scale", kPropertyTypeFloat),
                             -100.0, 100.0, 0.05),
                Declare<&Rigid3DComponent::velocity>("velocity", "Velocity",
                                                             kPropertyTypeVector3),
                Declare<&Rigid3DComponent::force>("force", "Force", kPropertyTypeVector3),
                Declare<&Rigid3DComponent::angularVelocity>("angularVelocity", "Angular Velocity",
                                                                    kPropertyTypeVector3),
                Declare<&Rigid3DComponent::torque>("torque", "Torque", kPropertyTypeVector3),
                Declare<&Rigid3DComponent::inverseInertia>("inverseInertia", "Inverse Inertia",
                                                                   kPropertyTypeVector3),
            }));

    using A = AreaComponent;
    const auto whenGravity = [](TypeMetadataProperty property) {
        property.isVisible = +[](const void* instance) {
            return static_cast<const A*>(instance)->gravityOverride != AreaSpaceOverrideMode::Disabled;
        };
        return property;
    };
    const auto whenGravityPoint = [](TypeMetadataProperty property) {
        property.isVisible = +[](const void* instance) {
            const A& area = *static_cast<const A*>(instance);
            return area.gravityOverride != AreaSpaceOverrideMode::Disabled && area.gravityPoint;
        };
        return property;
    };
    const auto whenGravityVector = [](TypeMetadataProperty property) {
        property.isVisible = +[](const void* instance) {
            const A& area = *static_cast<const A*>(instance);
            return area.gravityOverride != AreaSpaceOverrideMode::Disabled && !area.gravityPoint;
        };
        return property;
    };
    const auto whenLinearDamp = [](TypeMetadataProperty property) {
        property.isVisible = +[](const void* instance) {
            return static_cast<const A*>(instance)->linearDampOverride !=
                   AreaSpaceOverrideMode::Disabled;
        };
        return property;
    };
    const auto whenAngularDamp = [](TypeMetadataProperty property) {
        property.isVisible = +[](const void* instance) {
            return static_cast<const A*>(instance)->angularDampOverride !=
                   AreaSpaceOverrideMode::Disabled;
        };
        return property;
    };
    const std::vector<TypeMetadataEnumEntry> spaceOverrideOptions{
        {0, "Disabled"},
        {1, "Combine"},
        {2, "Combine Replace"},
        {3, "Replace"},
        {4, "Replace Combine"},
    };
    AddValidated<AreaComponent, &IsAreaComponentValid>(
        entries,
        MakeEntry("component.area", "AreaComponent", "Area3D", kSectionOrderTypeSpecific,
            {
                WithTooltip(
                    Declare<&A::halfExtents>("halfExtents", "Half Extents", kPropertyTypeVector3),
                    "The axis-aligned box this area occupies, in metres, around the object's origin. "
                    "World scale is deliberately not applied - a scaled art pivot never re-sizes a "
                    "trigger."),
                WithCustomDrawer(
                    WithTooltip(Declare<&A::collisionLayer>("collisionLayer", "Layer",
                                                                 kPropertyTypeBitMask32),
                                   "The layers this area is on - what others have to be looking for."),
                    std::string(kLayerMaskDrawerPhysics)),
                WithCustomDrawer(
                    WithTooltip(Declare<&A::collisionMask>("collisionMask", "Mask",
                                                                kPropertyTypeBitMask32),
                                   "The layers this area looks for. Detection needs BOTH sides to "
                                   "accept the other."),
                    std::string(kLayerMaskDrawerPhysics)),
                WithTooltip(Declare<&A::monitoring>("monitoring", "Monitoring",
                                                          kPropertyTypeBool),
                               "On, this area lists who is inside it and fires enter/exit. Off, the "
                               "lists stay empty. Space override still applies either way."),
                WithTooltip(Declare<&A::monitorable>("monitorable", "Monitorable",
                                                           kPropertyTypeBool),
                               "On, other monitoring areas can detect this one."),
                InGroup(WithTooltip(Declare<&A::priority>("priority", "Priority",
                                                                  kPropertyTypeInt32),
                                          "When several areas overlap one body, higher priority is "
                                          "applied first. Equal priorities resolve by entity id."),
                           "Space"),
                InGroup(WithTooltip(DeclareEnum<&A::gravityOverride>(
                                              "gravityOverride", "Gravity Override", spaceOverrideOptions),
                                          "How this area's gravity mixes with the world and with lower "
                                          "priority areas. Disabled leaves gravity alone."),
                           "Gravity"),
                InGroup(whenGravity(WithTooltip(
                               Declare<&A::gravityPoint>("gravityPoint", "Point Gravity",
                                                            kPropertyTypeBool),
                               "On, gravity pulls toward a point instead of along Gravity Direction.")),
                           "Gravity"),
                InGroup(whenGravityVector(WithTooltip(
                               Declare<&A::gravityDirection>("gravityDirection", "Gravity Direction",
                                                                kPropertyTypeVector3),
                               "The direction of gravity inside this area. It is normalized before "
                               "use; a zero vector is no gravity.")),
                           "Gravity"),
                InGroup(whenGravity(WithRange(
                               WithTooltip(Declare<&A::gravityMagnitude>(
                                                  "gravityMagnitude", "Gravity", kPropertyTypeFloat),
                                              "Gravity strength in metres per second squared. Negative "
                                              "repels when Point Gravity is on."),
                               -10000.0, 10000.0, 0.01)),
                           "Gravity"),
                InGroup(whenGravityPoint(WithTooltip(
                               Declare<&A::gravityPointOffset>("gravityPointOffset", "Point Offset",
                                                                  kPropertyTypeVector3),
                               "Where the point sits relative to this area's origin, in metres.")),
                           "Gravity"),
                InGroup(whenGravityPoint(WithRange(
                               WithTooltip(Declare<&A::gravityPointUnitDistance>(
                                                  "gravityPointUnitDistance", "Unit Distance",
                                                  kPropertyTypeFloat),
                                              "Distance at which point gravity equals Gravity. 0 is "
                                              "constant magnitude; greater than 0 is inverse square."),
                               0.0, 100000.0, 0.01)),
                           "Gravity"),
                InGroup(WithTooltip(DeclareEnum<&A::linearDampOverride>(
                                              "linearDampOverride", "Linear Damp Override",
                                              spaceOverrideOptions),
                                          "How this area's linear damping mixes with the body's drag "
                                          "and with lower priority areas."),
                           "Damping"),
                InGroup(whenLinearDamp(WithRange(
                               WithTooltip(Declare<&A::linearDamp>("linearDamp", "Linear Damp",
                                                                        kPropertyTypeFloat),
                                              "Linear damping this area contributes. Velocity is scaled "
                                              "by (1 - damp * dt) each physics step."),
                               0.0, 100.0, 0.01)),
                           "Damping"),
                InGroup(WithTooltip(DeclareEnum<&A::angularDampOverride>(
                                              "angularDampOverride", "Angular Damp Override",
                                              spaceOverrideOptions),
                                          "How this area's angular damping mixes with lower priority "
                                          "areas. Bodies have no angular drag of their own."),
                           "Damping"),
                InGroup(whenAngularDamp(WithRange(
                               WithTooltip(Declare<&A::angularDamp>("angularDamp", "Angular Damp",
                                                                         kPropertyTypeFloat),
                                              "Angular damping this area contributes, applied after "
                                              "torque integration."),
                               0.0, 100.0, 0.01)),
                           "Damping"),
                InGroup(DeclareRuntimeState<&A::overlappingBodyCount>(
                               "overlappingBodyCount", "Bodies", kPropertyTypeUInt8),
                           "Occupancy"),
                InGroup(DeclareRuntimeState<&A::overlappingBodiesTruncated>(
                               "overlappingBodiesTruncated", "Bodies Truncated", kPropertyTypeBool),
                           "Occupancy"),
                InGroup(DeclareRuntimeState<&A::overlappingAreaCount>(
                               "overlappingAreaCount", "Areas", kPropertyTypeUInt8),
                           "Occupancy"),
                InGroup(DeclareRuntimeState<&A::overlappingAreasTruncated>(
                               "overlappingAreasTruncated", "Areas Truncated", kPropertyTypeBool),
                           "Occupancy"),
            }));

    // Kinematic3D's own section. A platform is authored with two decisions - where it is going, and
    // how quickly it gets there - so the drawer stays short and honest, and says in the property
    // help what the mover does with them.
    // Declared with its own rule: the Inspector refuses an edit that would leave the component past
    // its contract (a non-finite target, an interpolation outside 0..1) and keeps the last accepted
    // value, so the mover never has to guess at a value nobody could have meant.
    AddValidated<Kinematic3DComponent, &IsKinematic3DObjectComponentValid>(
        entries,
        MakeEntry("component.kinematic_3d", "Kinematic3DComponent", "Kinematic Body", kSectionOrderTypeSpecific,
            {
                WithTooltip(Declare<&Kinematic3DComponent::targetVelocity>(
                                   "targetVelocity", "Target Velocity", kPropertyTypeVector3),
                               "Where the body is going, metres per second, in its own local axes. "
                               "Rotated into the world each step. Geometry stops it; bodies it meets "
                               "get pushed."),
                WithRange(WithTooltip(Declare<&Kinematic3DComponent::interpolation>(
                                                "interpolation", "Interpolation", kPropertyTypeFloat),
                                            "How quickly the body gets up to speed: 1 is at speed on "
                                            "the first step, lower values ease in over about a second, "
                                            "0 never eases on its own (a script drives the body)."),
                             0.0, 1.0, 0.01),
                WithTooltip(Declare<&Kinematic3DComponent::active>("active", "Active",
                                                                           kPropertyTypeBool),
                               "Off leaves the body where it is and stops it dead, including for "
                               "anything standing on it."),
            }));

    // SpringArm3D's own section: the third-person camera boom. The authored half is what the arm
    // reaches and what it collides with, and it is grouped behind the switch the rest of it depends
    // on; the runtime half is the one number the arm derives every step.
    AddValidated<SpringArm3DComponent, &IsSpringArm3DObjectComponentValid>(
        entries,
        MakeEntry("component.spring_arm", "SpringArm3DComponent", "SpringArm3D", kSectionOrderTypeSpecific,
            {
                WithTooltip(Declare<&SpringArm3DComponent::enabled>("enabled", "Enabled",
                                                                             kPropertyTypeBool),
                               "Off, the arm casts nothing and hands its length back, so whatever it "
                               "carries returns to the pose it was authored with."),
                WhenOn<&SpringArm3DComponent::enabled>(WithRange(
                    WithTooltip(Declare<&SpringArm3DComponent::armLength>(
                                       "armLength", "Arm Length", kPropertyTypeFloat),
                                   "How far the arm reaches along its own local +Z, in metres. The "
                                   "arm casts this far every fixed step and shortens to whatever it "
                                   "finds, minus the margin."),
                    0.0, 10000.0, 0.01)),
                WhenOn<&SpringArm3DComponent::enabled>(WithRange(
                    WithTooltip(Declare<&SpringArm3DComponent::margin>("margin", "Margin",
                                                                                kPropertyTypeFloat),
                                   "How far the arm keeps the thing it carries off the surface it "
                                   "hit, in metres. This is the camera's skin."),
                    0.0, 10000.0, 0.01)),
                WhenOn<&SpringArm3DComponent::enabled>(WithRange(
                    WithTooltip(Declare<&SpringArm3DComponent::smoothing>("smoothing",
                                                                                   "Smoothing",
                                                                                   kPropertyTypeFloat),
                                   "How fast the arm springs back out once the way is clear, per "
                                   "second: 0 pops out exactly like Godot's SpringArm3D, higher "
                                   "values rise sooner. Shortening always snaps - a camera is never "
                                   "allowed to clip through a wall."),
                    0.0, 1000.0, 0.1)),
                WhenOn<&SpringArm3DComponent::enabled>(WithCustomDrawer(
                    WithTooltip(Declare<&SpringArm3DComponent::collisionMask>(
                                       "collisionMask", "Mask", kPropertyTypeBitMask32),
                                   "What the arm collides with. Leave the layer the rig is on out of "
                                   "it, or the camera will sit on the character it belongs to."),
                    std::string(kLayerMaskDrawerPhysics))),
                InGroup(DeclareRuntimeState<&SpringArm3DComponent::currentLength>(
                               "currentLength", "Current Length", kPropertyTypeFloat),
                           "State"),
            }));

    // Character3D's own section. Grouped by what an author is thinking about - how it moves,
    // what it stands on, what it hits - with the state the controller writes each step last, shown
    // only while playing because that is when it describes something real.
    using C = CharacterControllerComponent;
    const auto whenGrounded = [](TypeMetadataProperty property) {
        property.isVisible = +[](const void* instance) {
            return static_cast<const C*>(instance)->motionMode == CharacterMotionMode::Grounded;
        };
        return property;
    };
    const auto whenBuiltIn = [](TypeMetadataProperty property) {
        property.isVisible = +[](const void* instance) { return static_cast<const C*>(instance)->builtInMovement; };
        return property;
    };
    const auto whenBuiltInGrounded = [](TypeMetadataProperty property) {
        property.isVisible = +[](const void* instance) {
            const C& c = *static_cast<const C*>(instance);
            return c.builtInMovement && c.motionMode == CharacterMotionMode::Grounded;
        };
        return property;
    };
    const auto whenPushing = [](TypeMetadataProperty property) {
        property.isVisible = +[](const void* instance) { return static_cast<const C*>(instance)->pushRigidBodies; };
        return property;
    };
    TypeMetadataProperty maxSlides = WithTooltip(
        Declare<&C::maxSlides>("maxSlides", "Max Slides", kPropertyTypeUInt32),
        "How many pieces a move is cut into to follow walls and corners. More is smoother and costs more.");
    maxSlides.range = {true, 1.0, 32.0, 1.0};
    TypeMetadataProperty maximumContacts = WithTooltip(
        Declare<&C::maximumContacts>("maximumContacts", "Max Contacts", kPropertyTypeUInt32),
        "The most contacts one move reports, deepest first. Extras are dropped, and the step says so.");
    maximumContacts.range = {true, 1.0, 64.0, 1.0};
    AddValidated<CharacterControllerComponent, &IsCharacterControllerComponentValid>(
        entries,
        MakeEntry("component.character_controller", "CharacterControllerComponent", "Character3D", kSectionOrderTypeSpecific,
            {
                WithTooltip(DeclareEnum<&C::motionMode>("motionMode", "Motion Mode",
                                                              {{0, "Grounded"}, {1, "Floating"}}),
                               "Grounded walks on floors under gravity. Floating flies or swims: no gravity, no "
                               "floor, every surface a wall."),
                WithTooltip(Declare<&C::upDirection>("upDirection", "Up Direction", kPropertyTypeVector3),
                               "The body's own up. Floors, jumps, gravity and ceilings are measured from this. "
                               "World +Y is the default, because the engine's gravity is along -Y."),
                whenGrounded(WithTooltip(WithRange(Declare<&C::gravityScale>("gravityScale", "Gravity Scale",
                                                                                      kPropertyTypeFloat),
                                                         0.0, 100.0, 0.05),
                                            "Multiplies the world's gravity. 1 is normal, 0 is none.")),
                InGroup(WithTooltip(Declare<&C::builtInMovement>("builtInMovement", "Built-in",
                                                                          kPropertyTypeBool),
                                          "Moves and jumps from the keyboard with no script. Off, it moves by its "
                                          "Velocity alone - how a script or an AI drives it."),
                           "Movement"),
                InGroup(whenBuiltIn(WithTooltip(WithRange(Declare<&C::moveSpeed>("moveSpeed", "Speed",
                                                                                             kPropertyTypeFloat),
                                                                   0.0, 1000.0, 0.1),
                                                      "Top speed, in metres per second.")),
                           "Movement"),
                InGroup(whenBuiltInGrounded(WithTooltip(
                               WithRange(Declare<&C::jumpHeight>("jumpHeight", "Jump Height", kPropertyTypeFloat),
                                            0.0, 1000.0, 0.05),
                               "How high a jump reaches, in metres, whatever the gravity.")),
                           "Movement"),
                InGroup(whenBuiltInGrounded(WithTooltip(
                               WithRange(Declare<&C::airControl>("airControl", "Air Control", kPropertyTypeFloat),
                                            0.0, 1.0, 0.01),
                               "How much steering works in the air. 1 is full control, 0 keeps the jump's direction.")),
                           "Movement"),
                InGroup(whenBuiltInGrounded(WithTooltip(
                               WithRange(Declare<&C::coyoteTimeSeconds>("coyoteTimeSeconds", "Coyote Time",
                                                                            kPropertyTypeFloat),
                                            0.0, 1.0, 0.01),
                               "A jump still works this many seconds after walking off a ledge.")),
                           "Movement"),
                InGroup(whenBuiltInGrounded(WithTooltip(
                               WithRange(Declare<&C::jumpBufferSeconds>("jumpBufferSeconds", "Jump Buffer",
                                                                            kPropertyTypeFloat),
                                            0.0, 1.0, 0.01),
                               "A jump pressed this many seconds before landing happens on landing.")),
                           "Movement"),
                InGroup(whenGrounded(WithTooltip(
                               WithRange(Declare<&C::floorMaxAngleDegrees>("floorMaxAngleDegrees", "Floor Angle",
                                                                                kPropertyTypeFloat),
                                            0.0, 90.0, 1.0),
                               "The steepest slope still walked as a floor. Steeper surfaces are walls it slides "
                               "along.")),
                           "Floor"),
                InGroup(whenGrounded(WithTooltip(
                               WithRange(Declare<&C::wallMinSlideAngleDegrees>(
                                                "wallMinSlideAngleDegrees", "Wall Slide Angle", kPropertyTypeFloat),
                                            0.0, 90.0, 1.0),
                               "How far past the floor angle a wall has to lean before the body slides down it "
                               "instead of being stopped by it.")),
                           "Floor"),
                InGroup(whenGrounded(WithTooltip(
                               WithRange(Declare<&C::floorSnapLength>("floorSnapLength", "Snap Length",
                                                                          kPropertyTypeFloat),
                                            0.0, 10.0, 0.01),
                               "Stays on the floor walking down steps and ledges up to this far below. 0 lets it "
                               "drop off every edge.")),
                           "Floor"),
                InGroup(whenGrounded(WithTooltip(
                               WithRange(Declare<&C::minStepWidth>("minStepWidth", "Min Step Width",
                                                                       kPropertyTypeFloat),
                                            0.0, 1.0, 0.01),
                               "How much room a step needs on top to be worth climbing. Narrower than this is a "
                               "wall with a decoration.")),
                           "Floor"),
                InGroup(whenGrounded(WithTooltip(
                               Declare<&C::floorStopOnSlope>("floorStopOnSlope", "Stop On Slope",
                                                                kPropertyTypeBool),
                               "Standing still on a slope stays still instead of creeping downhill.")),
                           "Floor"),
                InGroup(whenGrounded(WithTooltip(
                               Declare<&C::floorConstantSpeed>("floorConstantSpeed", "Constant Speed",
                                                                  kPropertyTypeBool),
                               "Walking up or down a slope keeps the horizontal speed the walk started with.")),
                           "Floor"),
                InGroup(whenGrounded(WithTooltip(
                               WithRange(Declare<&C::safeMargin>("safeMargin", "Safe Margin",
                                                                      kPropertyTypeFloat),
                                            0.0, 0.25, 0.001),
                               "How far short of a surface the body stops, so it does not jitter against it.")),
                           "Floor"),
                InGroup(whenGrounded(WithTooltip(
                               Declare<&C::floorBlockOnWall>("floorBlockOnWall", "Block On Wall",
                                                                kPropertyTypeBool),
                               "On the floor, stop at a wall instead of sliding along it - no corner slipping.")),
                           "Floor"),
                InGroup(whenGrounded(WithTooltip(
                               WithRange(Declare<&C::maxStepHeight>("maxStepHeight", "Step Height",
                                                                        kPropertyTypeFloat),
                                            0.0, 10.0, 0.01),
                               "Walks up steps and kerbs up to this high without jumping. 0 turns it off.")),
                           "Floor"),
                InGroup(WithTooltip(
                               DeclareEnum<&C::platformOnLeave>(
                                   "platformOnLeave", "On Leave Platform",
                                   {{0, "Keep Velocity"}, {1, "Add Velocity"}, {2, "Add Upward Velocity"}}),
                               "What a platform gives a body that walks off it: its whole velocity, only the "
                               "upward part, or nothing."),
                           "Platforms"),
                InGroup(WithTooltip(
                               WithRange(Declare<&C::maximumPlatformSpeed>(
                                                "maximumPlatformSpeed", "Max Platform Speed", kPropertyTypeFloat),
                                            0.0, 10000.0, 1.0),
                               "The fastest platform motion that may carry this body. Past it the platform is "
                               "treated as standing still, so a teleported platform cannot fling anyone."),
                           "Platforms"),
                InGroup(WithTooltip(Declare<&C::slideOnCeiling>("slideOnCeiling", "Slide On Ceiling",
                                                                         kPropertyTypeBool),
                                          "On hitting a ceiling, keep sliding along it. Off, the move stops there."),
                           "Ceiling"),
                InGroup(WithTooltip(Declare<&C::pushRigidBodies>("pushRigidBodies", "Push Bodies",
                                                                          kPropertyTypeBool),
                                          "Walking into a rigid body pushes it."),
                           "Pushing"),
                InGroup(whenPushing(WithTooltip(
                               WithRange(Declare<&C::pushStrength>("pushStrength", "Strength", kPropertyTypeFloat),
                                            0.0, 100.0, 0.05),
                               "How hard it pushes, relative to its own speed.")),
                           "Pushing"),
                InGroup(whenPushing(WithTooltip(
                               WithRange(Declare<&C::maxPushSpeed>("maxPushSpeed", "Max Speed", kPropertyTypeFloat),
                                            0.0, 1000.0, 0.1),
                               "The fastest a push may send a body, in metres per second.")),
                           "Pushing"),
                InGroup(std::move(maxSlides), "Collision"),
                InGroup(std::move(maximumContacts), "Collision"),
                InGroup(DeclareRuntimeState<&C::velocity>("velocity", "Velocity", kPropertyTypeVector3), "State"),
                InGroup(DeclareRuntimeState<&C::grounded>("grounded", "Grounded", kPropertyTypeBool), "State"),
                InGroup(DeclareRuntimeState<&C::isOnCeiling>("isOnCeiling", "On Ceiling", kPropertyTypeBool),
                           "State"),
                InGroup(DeclareRuntimeState<&C::floorNormal>("floorNormal", "Floor Normal", kPropertyTypeVector3),
                           "State"),
            }));
}

/// RayCast3D's own section: what the ray is, what it refuses to hit, and what it found. The
/// exclusions are entity references, so they are drawn by the reference-list drawer and remapped by
/// the scene serializer like every other reference an author can pick; the result rows are runtime
/// state, shown so an author can see the answer the engine computed rather than guessing at it.
void DeclareRayCast(std::vector<TypeMetadataEntry>& entries) {
    AddValidated<RayCast3DComponent, &IsRayCast3DObjectComponentValid>(
        entries,
        MakeEntry("component.ray_cast_3d", "RayCast3DComponent", "RayCast3D", kSectionOrderTypeSpecific,
            {
                WithTooltip(
                    Declare<&RayCast3DComponent::enabled>("enabled", "Enabled", kPropertyTypeBool),
                    "Off, the ray casts nothing and clears its result - no hit, no point, no "
                    "normal, no entity."),
                WithTooltip(
                    Declare<&RayCast3DComponent::direction>("direction", "Direction",
                                                                  kPropertyTypeVector3),
                    "Which way the ray points in this object's own space, rotated by its world "
                    "rotation every step. Any non-zero length is fine; it is a direction, not a "
                    "distance - the Length below is the distance."),
                WithRange(
                    WithTooltip(Declare<&RayCast3DComponent::length>("length", "Length",
                                                                             kPropertyTypeFloat),
                                   "How far the ray reaches along its direction, in metres. It "
                                   "reports the closest collider it meets inside that distance and "
                                   "nothing past it."),
                    0.0, 100000.0, 0.1),
                WithCustomDrawer(
                    WithTooltip(
                        Declare<&RayCast3DComponent::collisionMask>("collisionMask", "Mask",
                                                                          kPropertyTypeBitMask32),
                        "Which collision layers the ray is allowed to hit, on the same layer drawer "
                        "every other physics object uses."),
                    std::string(kLayerMaskDrawerPhysics)),
                WithTooltip(
                    WithCustomDrawer(
                        WithElementCount(
                            Declare<&RayCast3DComponent::exclusions>("exclusions", "Exclusions",
                                                                           kPropertyTypeEntityList),
                            kMaximumRayCastExclusions),
                        "entity-reference-list"),
                    "Objects this ray refuses to hit, on top of its own object - a ray never hits "
                    "the collider it starts inside. An exclusion is not a mask: no layer can bring "
                    "one back. The ray's own object is always excluded, so listing it changes "
                    "nothing."),
                InGroup(DeclareRuntimeState<&RayCast3DComponent::hit>("hit", "Hit",
                                                                               kPropertyTypeBool),
                           "Result"),
                InGroup(DeclareRuntimeState<&RayCast3DComponent::hitEntity>(
                               "hitEntity", "Hit Entity", kPropertyTypeEntity),
                           "Result"),
                InGroup(DeclareRuntimeState<&RayCast3DComponent::hitPosition>(
                               "hitPosition", "Hit Position", kPropertyTypeVector3),
                           "Result"),
                InGroup(DeclareRuntimeState<&RayCast3DComponent::hitNormal>(
                               "hitNormal", "Hit Normal", kPropertyTypeVector3),
                           "Result"),
            }));
}

void DeclareProjectile(std::vector<TypeMetadataEntry>& entries) {
    // Projectile3D's own section. The authored half is the whole flight - where it goes, how fat
    // it is, how long it lives, what it may hit and what its motion does when it does. The two
    // bounce coefficients appear only once the policy actually uses them, and the runtime half is
    // last, under the groups its state and its last contact belong to.
    // Declared with its own rule for the same reason Kinematic3D's is: the Inspector refuses an
    // edit that would leave the component past its contract (a policy this engine cannot execute,
    // a coefficient outside 0..1) and keeps the last accepted value, so the step never has to
    // guess at a value nobody could have meant.
    const auto whenBouncing = [](TypeMetadataProperty property) {
        property.isVisible = +[](const void* instance) {
            return static_cast<const Projectile3DComponent*>(instance)->hitPolicy ==
                   Projectile3DHitPolicy::Bounce;
        };
        return property;
    };
    TypeMetadataProperty projectileIgnore = WithTooltip(
        Declare<&Projectile3DComponent::ignoreEntity>("ignoreEntity", "Ignore Entity", kPropertyTypeEntity),
        "Never hit this entity. Set the owner so a shot does not collide with the body that fired it.");
    projectileIgnore.flags = TypeMetadataPropertyFlags::EntityReference;
    AddValidated<Projectile3DComponent, &IsProjectile3DObjectComponentValid>(
        entries,
        MakeEntry("component.projectile_3d", "Projectile3DComponent", "Projectile3D", kSectionOrderTypeSpecific,
            {
                WithTooltip(
                    Declare<&Projectile3DComponent::active>("active", "Active", kPropertyTypeBool),
                    "Off stops everything: no motion, no sweep, no countdown. The engine also "
                    "clears it when the projectile stops on a hit or runs out of lifetime."),
                WithTooltip(
                    Declare<&Projectile3DComponent::velocity>("velocity", "Velocity",
                                                                    kPropertyTypeVector3),
                    "Where the projectile is going, in metres per second, in its own axes. It "
                    "is the state that accumulates Acceleration, so this is the launch velocity "
                    "and the fixed step keeps it honest."),
                WithTooltip(
                    Declare<&Projectile3DComponent::acceleration>("acceleration", "Acceleration",
                                                                        kPropertyTypeVector3),
                    "Added to Velocity every step, in metres per second squared: gravity, drag, a "
                    "wind volume. Gravity is not implied - a projectile only falls if something "
                    "here says so."),
                WithRange(
                    WithTooltip(Declare<&Projectile3DComponent::radius>("radius", "Radius",
                                                                                 kPropertyTypeFloat),
                                   "The sphere the projectile sweeps, in world metres. It is the "
                                   "size that decides whether it fits through a gap and how far "
                                   "its centre is held off a surface - a ray is infinitely thin "
                                   "and misses the hits a fat projectile must not miss."),
                    0.001, 1000.0, 0.01),
                WithRange(
                    WithTooltip(Declare<&Projectile3DComponent::maxLifetime>(
                                       "maxLifetime", "Max Lifetime", kPropertyTypeFloat),
                                   "Seconds of flight before the engine clears Active. A bouncing "
                                   "projectile has no other end, so this is the clock that always "
                                   "ends one."),
                    0.01, 3600.0, 0.1),
                WithCustomDrawer(
                    WithTooltip(Declare<&Projectile3DComponent::collisionMask>(
                                       "collisionMask", "Mask", kPropertyTypeBitMask32),
                                   "Which collision layers this projectile may hit, on the same "
                                   "layer drawer every other physics object uses."),
                    std::string(kLayerMaskDrawerPhysics)),
                projectileIgnore,
                WithTooltip(
                    DeclareEnum<&Projectile3DComponent::hitPolicy>("hitPolicy", "On Hit",
                                                                         {{0, "Stop"}, {1, "Bounce"}}),
                    "What this projectile's own motion does when it hits something. The engine "
                    "owns the motion - Stop halts it at the contact, Bounce reflects it - and "
                    "gameplay owns the consequences: damage, effects and despawning read the hit "
                    "event or these result fields."),
                whenBouncing(WithRange(
                    WithTooltip(Declare<&Projectile3DComponent::restitution>(
                                       "restitution", "Restitution", kPropertyTypeFloat),
                                   "How much of the speed into a surface comes back out of it: 1 "
                                   "leaves as fast as it arrived, 0 arrives and slides. This is "
                                   "the projectile's own coefficient - the surface's is reported "
                                   "with the contact for gameplay to react to."),
                    0.0, 1.0, 0.01)),
                whenBouncing(WithRange(
                    WithTooltip(Declare<&Projectile3DComponent::friction>("friction",
                                                                                   "Bounce Friction",
                                                                                   kPropertyTypeFloat),
                                   "How much of the speed along the surface is lost on each "
                                   "bounce: 0 keeps all of its sideways motion, 1 keeps none. A "
                                   "bounce that leaves no motion at all is a stop."),
                    0.0, 1.0, 0.01)),
                InGroup(DeclareRuntimeState<&Projectile3DComponent::remainingLifetime>(
                               "remainingLifetime", "Remaining Lifetime", kPropertyTypeFloat),
                           "State"),
                InGroup(DeclareRuntimeState<&Projectile3DComponent::hit>("hit", "Hit",
                                                                                  kPropertyTypeBool),
                           "Result"),
                InGroup(DeclareRuntimeState<&Projectile3DComponent::hitEntity>(
                               "hitEntity", "Hit Entity", kPropertyTypeEntity),
                           "Result"),
                InGroup(DeclareRuntimeState<&Projectile3DComponent::hitPosition>(
                               "hitPosition", "Hit Position", kPropertyTypeVector3),
                           "Result"),
                InGroup(DeclareRuntimeState<&Projectile3DComponent::hitNormal>(
                               "hitNormal", "Hit Normal", kPropertyTypeVector3),
                           "Result"),
                InGroup(DeclareRuntimeState<&Projectile3DComponent::impactSpeed>(
                               "impactSpeed", "Impact Speed", kPropertyTypeFloat),
                           "Result"),
                InGroup(DeclareRuntimeState<&Projectile3DComponent::bounceCount>(
                               "bounceCount", "Bounces", kPropertyTypeUInt32),
                           "Result"),
            }));
}

void DeclareCombat(std::vector<TypeMetadataEntry>& entries) {
    TypeMetadataProperty hitboxIgnore = WithTooltip(
        Declare<&Hitbox3DComponent::ignoreEntity>("ignoreEntity", "Ignore Entity", kPropertyTypeEntity),
        "Never strike this entity. Set the owner so a weapon does not hit the character holding it.");
    hitboxIgnore.flags = TypeMetadataPropertyFlags::EntityReference;
    AddValidated<Hitbox3DComponent, &IsHitbox3DObjectComponentValid>(
        entries,
        MakeEntry("component.hitbox_3d", "Hitbox3DComponent", "Hitbox3D", kSectionOrderTypeSpecific,
            {
                WithTooltip(
                    Declare<&Hitbox3DComponent::enabled>("enabled", "Enabled", kPropertyTypeBool),
                    "Off, the hitbox strikes nothing and ends every strike it was in: the engine "
                    "reports the exit, it does not wait for the boxes to separate."),
                WithTooltip(
                    Declare<&Hitbox3DComponent::halfExtents>("halfExtents", "Half Extents",
                                                                   kPropertyTypeVector3),
                    "The exact oriented box this hitbox strikes with: world position and "
                    "rotation plus these half-extents, with the object's world SCALE deliberately "
                    "not applied - the collider/area convention, so a scaled art pivot never "
                    "re-sizes a hurt volume."),
                WithTooltip(
                    Declare<&Hitbox3DComponent::damageChannel>("damageChannel", "Channel",
                                                                     kPropertyTypeString),
                    "What this hitbox is. It only ever strikes a hurtbox authored with the same "
                    "channel, which is how one character carries a sword hitbox and a punch hitbox "
                    "without either firing the other's reactions. Up to 256 bytes."),
                WithCustomDrawer(
                    WithTooltip(Declare<&Hitbox3DComponent::collisionLayer>(
                                       "collisionLayer", "Layer", kPropertyTypeBitMask32),
                                   "The layers this hitbox is on - what a hurtbox has to be "
                                   "looking for."),
                    std::string(kLayerMaskDrawerPhysics)),
                WithCustomDrawer(
                    WithTooltip(Declare<&Hitbox3DComponent::collisionMask>(
                                       "collisionMask", "Mask", kPropertyTypeBitMask32),
                                   "The layers this hitbox strikes. A strike needs BOTH sides to "
                                   "accept the other, so a hurtbox can refuse a whole class of "
                                   "attackers on its own."),
                    std::string(kLayerMaskDrawerPhysics)),
                std::move(hitboxIgnore),
                InGroup(DeclareRuntimeState<&Hitbox3DComponent::strikeCount>(
                               "strikeCount", "Strikes", kPropertyTypeUInt8),
                           "Result"),
                InGroup(DeclareRuntimeState<&Hitbox3DComponent::strikesTruncated>(
                               "strikesTruncated", "Truncated", kPropertyTypeBool),
                           "Result"),
                InGroup(DeclareRuntimeState<&Hitbox3DComponent::struckCount>(
                               "struckCount", "Landed", kPropertyTypeUInt8),
                           "Result"),
            }));

    TypeMetadataProperty hurtboxIgnore = WithTooltip(
        Declare<&Hurtbox3DComponent::ignoreEntity>("ignoreEntity", "Ignore Entity", kPropertyTypeEntity),
        "Never accept a strike from this entity. Set the owner so a character is not hit by its own weapon.");
    hurtboxIgnore.flags = TypeMetadataPropertyFlags::EntityReference;
    AddValidated<Hurtbox3DComponent, &IsHurtbox3DObjectComponentValid>(
        entries,
        MakeEntry("component.hurtbox_3d", "Hurtbox3DComponent", "Hurtbox3D", kSectionOrderTypeSpecific,
            {
                WithTooltip(
                    Declare<&Hurtbox3DComponent::enabled>("enabled", "Enabled", kPropertyTypeBool),
                    "Off, nothing can strike this hurtbox: it is not even a candidate, so every "
                    "strike it was in ends on this tick's report."),
                WithTooltip(
                    Declare<&Hurtbox3DComponent::halfExtents>("halfExtents", "Half Extents",
                                                                    kPropertyTypeVector3),
                    "The exact oriented box this hurtbox is struck on, in world metres: world "
                    "position and rotation plus these half-extents, with the object's world SCALE "
                    "deliberately not applied - the collider/area convention."),
                WithTooltip(
                    Declare<&Hurtbox3DComponent::damageChannel>("damageChannel", "Channel",
                                                                      kPropertyTypeString),
                    "What this hurtbox receives. Only a hitbox authored with the same channel can "
                    "strike it. Up to 256 bytes."),
                WithCustomDrawer(
                    WithTooltip(Declare<&Hurtbox3DComponent::collisionLayer>(
                                       "collisionLayer", "Layer", kPropertyTypeBitMask32),
                                   "The layers this hurtbox is on - what a hitbox has to be looking "
                                   "for."),
                    std::string(kLayerMaskDrawerPhysics)),
                WithCustomDrawer(
                    WithTooltip(Declare<&Hurtbox3DComponent::collisionMask>(
                                       "collisionMask", "Mask", kPropertyTypeBitMask32),
                                   "The layers this hurtbox accepts strikes from. A strike needs "
                                   "BOTH sides to accept the other, which is how a hurtbox refuses "
                                   "a whole class of attackers without touching any of them."),
                    std::string(kLayerMaskDrawerPhysics)),
                std::move(hurtboxIgnore),
                InGroup(DeclareRuntimeState<&Hurtbox3DComponent::hitCount>(
                               "hitCount", "Hits", kPropertyTypeUInt8),
                           "Result"),
                InGroup(DeclareRuntimeState<&Hurtbox3DComponent::hitsTruncated>(
                               "hitsTruncated", "Truncated", kPropertyTypeBool),
                           "Result"),
                InGroup(DeclareRuntimeState<&Hurtbox3DComponent::receivedCount>(
                               "receivedCount", "Received", kPropertyTypeUInt8),
                           "Result"),
            }));
}

void DeclareAnimation(std::vector<TypeMetadataEntry>& entries) {
    // AnimationSequencer's own section. Its target is an entity reference: flagged so the serializer
    // remaps it, and drawn as an object picker. Empty means the player's parent, which is the common
    // case and needs no picking at all.
    // AnimationDriver: the base AnimationSequencer and AnimationGraph share, shown between their own
    // section and the Object section. Its target is an entity reference: flagged so the serializer
    // remaps it, and drawn as an object picker. Empty means the parent, the common case.
    using M = AnimationDriverComponent;
    TypeMetadataProperty mixerTarget = WithTooltip(
        Declare<&M::target>("target", "Target", kPropertyTypeEntity),
        "The object that is moved. Empty means this object's parent.");
    mixerTarget.flags = TypeMetadataPropertyFlags::EntityReference;
    AddValidated<AnimationDriverComponent, &IsAnimationDriverComponentValid>(
        entries,
        MakeEntry("component.animation_mixer", "AnimationDriverComponent", std::string{AnimationDriverObjectDefinition::typeName}, kSectionOrderObjectBase + 20,
            {
                WithTooltip(Declare<&M::active>("active", "Active", kPropertyTypeBool),
                               "Off, nothing is evaluated and the target is left alone."),
                std::move(mixerTarget),
                WithTooltip(WithRange(Declare<&M::speedScale>("speedScale", "Speed Scale",
                                                                       kPropertyTypeFloat),
                                            0.0, 100.0, 0.01),
                               "Multiplies every clock under this object: 0.5 is slow motion, 0 freezes."),
                WithTooltip(DeclareEnum<&M::processCallback>("processCallback", "Update",
                                                                   {{0, "Every Frame"}, {1, "Physics Step"}}),
                               "Every Frame is smoothest on screen. Physics Step keeps the target in step "
                               "with the bodies it pushes."),
                InGroup(WithTooltip(Declare<&M::animatePosition>("animatePosition", "Position",
                                                                          kPropertyTypeBool),
                                          "Off, the target's position is left alone."),
                           "Channels"),
                InGroup(WithTooltip(Declare<&M::animateRotation>("animateRotation", "Rotation",
                                                                          kPropertyTypeBool),
                                          "Off, the target's rotation is left alone."),
                           "Channels"),
                InGroup(WithTooltip(Declare<&M::animateScale>("animateScale", "Scale", kPropertyTypeBool),
                                          "Off, the target's scale is left alone."),
                           "Channels"),
                WithTooltip(DeclareEnum<&M::transition>("transition", "Transition",
                                                              {{0, "Inertialize"}, {1, "Crossfade"}}),
                               "How a clip that starts takes over, over the player's Blend In. Inertialize "
                               "plays the new clip at once and fades out the old pose's difference smoothly: "
                               "cheaper, and no sliding. Crossfade mixes the two."),
                InGroup(WithTooltip(DeclareEnum<&M::rootMotion>("rootMotion", "Mode",
                                                                         {{0, "Off"}, {1, "In Place"},
                                                                          {2, "Apply To Target"}}),
                                          "Skeletal clips only. In Place takes the root bone's ground travel "
                                          "out of the pose. Apply To Target also moves the target by it; a "
                                          "Character3D gets it as velocity, so walls still stop it."),
                           "Root Motion"),
                InGroup(WithTooltip(Declare<&M::rootMotionBone>("rootMotionBone", "Bone",
                                                                         kPropertyTypeString),
                                          "The bone whose travel is root motion. Empty picks the first bone "
                                          "whose track moves across the ground (the root or the hips)."),
                           "Root Motion"),
            }));

    // AnimationSequencer's own section.
    using P = AnimationSequencerComponent;
    const auto whenOnce = [](TypeMetadataProperty property) {
        property.isVisible = +[](const void* instance) {
            return static_cast<const P*>(instance)->loopMode == AnimationLoopMode::Once;
        };
        return property;
    };
    AddValidated<AnimationSequencerComponent, &IsAnimationSequencerComponentValid>(
        entries,
        MakeEntry("component.animation_player", "AnimationSequencerComponent", "AnimationSequencer", kSectionOrderTypeSpecific,
            {
                WithTooltip(WithCustomDrawer(Declare<&P::clip>("clip", "Clip", kPropertyTypeAssetGuid),
                                                   "asset:uvanim"),
                               "The .uvanim clip to play."),
                WithTooltip(WithCustomDrawer(Declare<&P::libraryRef>("libraryRef", "Library", kPropertyTypeAssetGuid),
                                                   "asset:uvanimlib"),
                               "A .uvanimlib whose clips this player also offers, live: editing the file changes what the Timeline offers."),
                WithTooltip(Declare<&P::autoplay>("autoplay", "Autoplay", kPropertyTypeBool),
                               "Starts playing as soon as the scene runs."),
                InGroup(WithTooltip(WithRange(Declare<&P::speed>("speed", "Speed", kPropertyTypeFloat),
                                                       -100.0, 100.0, 0.05),
                                          "1 is normal, 2 twice as fast, negative plays backwards, 0 holds."),
                           "Playback"),
                InGroup(WithTooltip(DeclareEnum<&P::loopMode>("loopMode", "Loop Mode",
                                                                       {{0, "Once"}, {1, "Loop"}, {2, "Ping-Pong"}}),
                                          "Once stops at the end. Loop starts again. Ping-Pong turns round and plays "
                                          "back."),
                           "Playback"),
                InGroup(whenOnce(WithTooltip(DeclareEnum<&P::onFinish>(
                                                       "onFinish", "On Finish",
                                                       {{0, "Hold Last Pose"}, {1, "Return To Start"}}),
                                                   "Where the target is left when the clip ends.")),
                           "Playback"),
                InGroup(WithTooltip(WithRange(Declare<&P::startOffsetSeconds>(
                                                           "startOffsetSeconds", "Start Offset", kPropertyTypeFloat),
                                                       0.0, 3600.0, 0.01),
                                          "Seconds into the clip where playback starts. Offsetting copies of one "
                                          "clip keeps a crowd from moving in lockstep."),
                           "Playback"),
                InGroup(WithTooltip(WithRange(Declare<&P::blendInSeconds>(
                                                           "blendInSeconds", "Blend In", kPropertyTypeFloat),
                                                       0.0, 60.0, 0.01),
                                          "Eases from where the target is into the clip over this many seconds, "
                                          "instead of snapping. 0 snaps."),
                           "Blending"),
                InGroup(WithTooltip(Declare<&P::relative>("relative", "Relative", kPropertyTypeBool),
                                          "Plays the clip's motion on top of where the target already is, so one "
                                          "clip works on any object wherever it was placed."),
                           "Blending"),
                InGroup(DeclareRuntimeState<&P::isPlaying>("isPlaying", "Playing", kPropertyTypeBool), "State"),
                InGroup(DeclareRuntimeState<&P::currentTimeSeconds>("currentTimeSeconds", "Time",
                                                                         kPropertyTypeFloat),
                           "State"),
                InGroup(DeclareRuntimeState<&P::finished>("finished", "Finished", kPropertyTypeBool), "State"),
            }));

    // MontageSequencer: the montages its script can play, by file name.
    using MP = MontageSequencerComponent;
    const auto montageSlot = [](auto property, const char* tip) {
        return WithTooltip(WithCustomDrawer(std::move(property), "asset:uvmontage"), tip);
    };
    constexpr const char* kMontageTip =
        "An Motion Montage this object can play. Its script plays it by the file's name: "
        "montage.play(\"Attack\") plays Attack.uvmontage.";
    AddValidated<MontageSequencerComponent, &IsMontageSequencerComponentValid>(
        entries,
        MakeEntry("component.montage_sequencer", "MontageSequencerComponent", "MontageSequencer", kSectionOrderTypeSpecific,
            {
                InGroup(montageSlot(Declare<&MP::montage1>("montage1", "Montage 1", kPropertyTypeAssetGuid), kMontageTip), "Montages"),
                InGroup(montageSlot(Declare<&MP::montage2>("montage2", "Montage 2", kPropertyTypeAssetGuid), kMontageTip), "Montages"),
                InGroup(montageSlot(Declare<&MP::montage3>("montage3", "Montage 3", kPropertyTypeAssetGuid), kMontageTip), "Montages"),
                InGroup(montageSlot(Declare<&MP::montage4>("montage4", "Montage 4", kPropertyTypeAssetGuid), kMontageTip), "Montages"),
                InGroup(montageSlot(Declare<&MP::montage5>("montage5", "Montage 5", kPropertyTypeAssetGuid), kMontageTip), "Montages"),
                InGroup(montageSlot(Declare<&MP::montage6>("montage6", "Montage 6", kPropertyTypeAssetGuid), kMontageTip), "Montages"),
                InGroup(montageSlot(Declare<&MP::montage7>("montage7", "Montage 7", kPropertyTypeAssetGuid), kMontageTip), "Montages"),
                InGroup(montageSlot(Declare<&MP::montage8>("montage8", "Montage 8", kPropertyTypeAssetGuid), kMontageTip), "Montages"),
                WithTooltip(WithCustomDrawer(Declare<&MP::libraryRef>("libraryRef", "Library", kPropertyTypeAssetGuid),
                                                   "asset:uvmontagelib"),
                               "A .uvmontagelib whose montages this player also plays by name, live: editing the file changes "
                               "what it offers."),
                InGroup(DeclareRuntimeState<&MP::playingName>("playingName", "Playing", kPropertyTypeString), "State"),
            }));

    using T = AnimationGraphComponent;
    // The parameters and the graph are lists with their own add, remove and wiring, which a
    // property row cannot express, so each is one custom-drawn block.
    AddValidated<AnimationGraphComponent, &IsAnimationGraphComponentValid>(
        entries,
        MakeEntry("component.animation_tree", "AnimationGraphComponent", "AnimationGraph", kSectionOrderTypeSpecific,
            {
                InGroup(WithCustomDrawer(Declare<&T::parameters>("parameters", "Parameters",
                                                                         "AnimationParameterList"),
                                               "animation-parameters"),
                           "Parameters"),
                InGroup(WithCustomDrawer(Declare<&T::nodes>("objects", "Graph", "AnimationGraphObjectList"),
                                               "animation-graph"),
                           "Graph"),
                InGroup(DeclareRuntimeState<&T::activeStates>("activeStates", "Active States",
                                                                    kPropertyTypeString),
                           "State"),
            }));
}

void DeclareMediaAndUI(std::vector<TypeMetadataEntry>& entries) {
    AddValidated<AudioSourceComponent, &IsAudioSourceComponentValid>(
        entries,
        MakeEntry("component.audio_source", "AudioSourceComponent", "Audio Source", kSectionOrderTypeSpecific,
            {
                InGroup(WithCustomDrawer(Declare<&AudioSourceComponent::audioAssetPath>("audioAssetPath", "Clip",
                                                                                                      kPropertyTypeString),
                                                 "file:uvaudio,wav"),
                           "Clip"),
                InGroup(Declare<&AudioSourceComponent::mixerGroup>("mixerGroup", "Mixer Group",
                                                                            kPropertyTypeString),
                           "Clip"),
                InGroup(WithRange(Declare<&AudioSourceComponent::volume>("volume", "Volume",
                                                                                      kPropertyTypeFloat),
                                        0.0, 1.0, 0.01),
                           "Playback"),
                InGroup(WithRange(Declare<&AudioSourceComponent::pitch>("pitch", "Pitch",
                                                                                     kPropertyTypeFloat),
                                        0.01, 4.0, 0.01),
                           "Playback"),
                InGroup(Declare<&AudioSourceComponent::looping>("looping", "Looping", kPropertyTypeBool),
                           "Playback"),
                InGroup(Declare<&AudioSourceComponent::playOnAwake>("playOnAwake", "Play On Awake",
                                                                             kPropertyTypeBool),
                           "Playback"),
                InGroup(Declare<&AudioSourceComponent::spatial>("spatial", "Spatial",
                                                                         kPropertyTypeBool),
                           "3D"),
                InGroup([] {
                               TypeMetadataProperty property = WithRange(
                                   Declare<&AudioSourceComponent::minDistance>("minDistance", "Min Distance",
                                                                                     kPropertyTypeFloat),
                                   0.0, 10000.0, 0.1);
                               property.isVisible = +[](const void* instance) {
                                   return static_cast<const AudioSourceComponent*>(instance)->spatial;
                               };
                               return property;
                           }(),
                           "3D"),
                InGroup([] {
                               TypeMetadataProperty property = WithRange(
                                   Declare<&AudioSourceComponent::maxDistance>("maxDistance", "Max Distance",
                                                                                     kPropertyTypeFloat),
                                   0.0, 10000.0, 0.1);
                               property.isVisible = +[](const void* instance) {
                                   return static_cast<const AudioSourceComponent*>(instance)->spatial;
                               };
                               return property;
                           }(),
                           "3D"),
                InGroup([] {
                               TypeMetadataProperty property =
                                   DeclareEnum<&AudioSourceComponent::attenuationCurve>(
                                       "attenuationCurve", "Attenuation", {{0, "Linear"}, {1, "Inverse Square"}});
                               property.isVisible = +[](const void* instance) {
                                   return static_cast<const AudioSourceComponent*>(instance)->spatial;
                               };
                               return property;
                           }(),
                           "3D"),
                InGroup([] {
                               TypeMetadataProperty property = Declare<&AudioSourceComponent::occlusionEnabled>(
                                   "occlusionEnabled", "Occlusion", kPropertyTypeBool);
                               property.isVisible = +[](const void* instance) {
                                   return static_cast<const AudioSourceComponent*>(instance)->spatial;
                               };
                               return property;
                           }(),
                           "Occlusion"),
                InGroup([] {
                               TypeMetadataProperty property = WithTooltip(
                                   WithRange(Declare<&AudioSourceComponent::occlusionGain>(
                                                    "occlusionGain", "Occluded Gain", kPropertyTypeFloat),
                                                0.0, 1.0, 0.01),
                                   "Gain the source falls to when fully blocked.");
                               property.isVisible = +[](const void* instance) {
                                   const auto* source = static_cast<const AudioSourceComponent*>(instance);
                                   return source->spatial && source->occlusionEnabled;
                               };
                               return property;
                           }(),
                           "Occlusion"),
                InGroup([] {
                               TypeMetadataProperty property = WithTooltip(
                                   WithRange(Declare<&AudioSourceComponent::occlusionLowPassHz>(
                                                    "occlusionLowPassHz", "Occluded Low-Pass", kPropertyTypeFloat),
                                                20.0, 20000.0, 10.0),
                                   "Low-pass corner the source falls to when fully blocked.");
                               property.isVisible = +[](const void* instance) {
                                   const auto* source = static_cast<const AudioSourceComponent*>(instance);
                                   return source->spatial && source->occlusionEnabled;
                               };
                               return property;
                           }(),
                           "Occlusion"),
                InGroup([] {
                               TypeMetadataProperty property = WithTooltip(
                                   WithCustomDrawer(Declare<&AudioSourceComponent::occlusionLayerMask>(
                                                           "occlusionLayerMask", "Occlusion Layers",
                                                           kPropertyTypeBitMask32),
                                                       std::string(kLayerMaskDrawerPhysics)),
                                   "Which collision layers can block this source.");
                               property.isVisible = +[](const void* instance) {
                                   const auto* source = static_cast<const AudioSourceComponent*>(instance);
                                   return source->spatial && source->occlusionEnabled;
                               };
                               return property;
                           }(),
                           "Occlusion"),
            }));

    DeclareAnimation(entries);

    Add<CanvasComponent>(
        entries,
        MakeEntry("component.canvas", "CanvasComponent", "Canvas", kSectionOrderTypeSpecific,
                     {
                         Declare<&CanvasComponent::visible>("visible", "Visible",
                                                                  kPropertyTypeBool),
                         Declare<&CanvasComponent::sortOrder>("sortOrder", "Sort Order",
                                                                    kPropertyTypeInt32),
                         WithTooltip(Declare<&CanvasComponent::scaleWithScreen>("scaleWithScreen", "Scale With Screen",
                                                                                        kPropertyTypeBool),
                                        "Fit the canvas, laid out for its Reference Resolution, to any screen"),
                         WithTooltip(Declare<&CanvasComponent::referenceResolution>(
                                            "referenceResolution", "Reference Resolution", kPropertyTypeVector2),
                                        "The screen size the widgets were placed for (0 = this device's screen)"),
                     }));

    AddValidated<UITextComponent, &IsUITextComponentValid>(
        entries,
        MakeEntry("component.ui_text", "UITextComponent", "UI Text", kSectionOrderTypeSpecific,
                     {
                         Declare<&UITextComponent::text>("text", "Text", kPropertyTypeString),
                         Declare<&UITextComponent::positionPixels>("positionPixels", "Position",
                                                                         kPropertyTypeVector2),
                         WithRange(Declare<&UITextComponent::fontSize>("fontSize", "Font Size",
                                                                                kPropertyTypeFloat),
                                      1.0, 512.0, 1.0),
                         Declare<&UITextComponent::color>("color", "Color", kPropertyTypeColor),
                         WithRange(Declare<&UITextComponent::alpha>("alpha", "Alpha",
                                                                             kPropertyTypeFloat),
                                      0.0, 1.0, 0.01),
                         InGroup(WithTooltip(Declare<&UITextComponent::fillParent>("fillParent", "Fill Parent",
                                                                                              kPropertyTypeBool),
                                                   "Lay the text out in the parent widget's box (a button's label)"),
                                    "Layout"),
                         InGroup(WithTooltip(Declare<&UITextComponent::boxSize>("boxSize", "Box Size",
                                                                                           kPropertyTypeVector2),
                                                   "The box the text is aligned in, from Position (0 = none)"),
                                    "Layout"),
                         InGroup(DeclareEnum<&UITextComponent::horizontalAlign>(
                                        "horizontalAlign", "Horizontal Align", {{0, "Left"}, {1, "Center"}, {2, "Right"}}),
                                    "Layout"),
                         InGroup(DeclareEnum<&UITextComponent::verticalAlign>(
                                        "verticalAlign", "Vertical Align", {{0, "Top"}, {1, "Middle"}, {2, "Bottom"}}),
                                    "Layout"),
                         InGroup(Declare<&UITextComponent::wrap>("wrap", "Wrap", kPropertyTypeBool), "Layout"),
                         InGroup(WithRange(Declare<&UITextComponent::lineHeight>("lineHeight", "Line Height",
                                                                                            kPropertyTypeFloat),
                                                 0.5, 4.0, 0.05),
                                    "Layout"),
                         InGroup(Declare<&UITextComponent::shadowColor>("shadowColor", "Shadow Color",
                                                                                kPropertyTypeColor),
                                    "Effects"),
                         InGroup(WithRange(Declare<&UITextComponent::shadowAlpha>("shadowAlpha", "Shadow Alpha",
                                                                                             kPropertyTypeFloat),
                                                 0.0, 1.0, 0.01),
                                    "Effects"),
                         InGroup(Declare<&UITextComponent::shadowOffset>("shadowOffset", "Shadow Offset",
                                                                                 kPropertyTypeVector2),
                                    "Effects"),
                         InGroup(Declare<&UITextComponent::outlineColor>("outlineColor", "Outline Color",
                                                                                 kPropertyTypeColor),
                                    "Effects"),
                         InGroup(WithRange(Declare<&UITextComponent::outlineWidth>("outlineWidth", "Outline Width",
                                                                                              kPropertyTypeFloat),
                                                 0.0, 8.0, 0.5),
                                    "Effects"),
                     }));

    AddValidated<UIImageComponent, &IsUIImageComponentValid>(
        entries,
        MakeEntry("component.ui_image", "UIImageComponent", "UI Image", kSectionOrderTypeSpecific,
                     {
                         InGroup(Declare<&UIImageComponent::textureAssetGuid>("textureAssetGuid", "Texture",
                                                                                       kPropertyTypeAssetGuid),
                                    "Image"),
                         InGroup(Declare<&UIImageComponent::rect>("rect", "Rect", kPropertyTypeRect),
                                    "Image"),
                         InGroup(Declare<&UIImageComponent::tintColor>("tintColor", "Tint",
                                                                                kPropertyTypeColor),
                                    "Image"),
                         InGroup(WithRange(Declare<&UIImageComponent::alpha>("alpha", "Alpha",
                                                                                         kPropertyTypeFloat),
                                                 0.0, 1.0, 0.01),
                                    "Image"),
                         InGroup(Declare<&UIImageComponent::nineSliceEnabled>("nineSliceEnabled", "Nine Slice",
                                                                                       kPropertyTypeBool),
                                    "Nine Slice"),
                         InGroup(Declare<&UIImageComponent::sliceFillCenter>("sliceFillCenter", "Slice Fill Center",
                                                                                     kPropertyTypeBool),
                                    "Nine Slice"),
                         InGroup(Declare<&UIImageComponent::sliceMarginMin>("sliceMarginMin", "Slice Margin Min",
                                                                                    kPropertyTypeVector2),
                                    "Nine Slice"),
                         InGroup(Declare<&UIImageComponent::sliceMarginMax>("sliceMarginMax", "Slice Margin Max",
                                                                                    kPropertyTypeVector2),
                                    "Nine Slice"),
                         InGroup(Declare<&UIImageComponent::sliceUVMin>("sliceUVMin", "Slice UV Min",
                                                                                kPropertyTypeVector2),
                                    "Nine Slice"),
                         InGroup(Declare<&UIImageComponent::sliceUVMax>("sliceUVMax", "Slice UV Max",
                                                                                kPropertyTypeVector2),
                                    "Nine Slice"),
                     }));

    Add<UIButtonComponent>(
        entries,
        MakeEntry("component.ui_button", "UIButtonComponent", "UI Button", kSectionOrderTypeSpecific,
                     {
                         Declare<&UIButtonComponent::rect>("rect", "Rect", kPropertyTypeRect),
                         Declare<&UIButtonComponent::normalColor>("normalColor", "Normal",
                                                                        kPropertyTypeColor),
                         Declare<&UIButtonComponent::hoverColor>("hoverColor", "Hover",
                                                                       kPropertyTypeColor),
                         Declare<&UIButtonComponent::pressedColor>("pressedColor", "Pressed",
                                                                         kPropertyTypeColor),
                         DeclareRuntimeState<&UIButtonComponent::isHovered>(
                             "isHovered", "Hovered", kPropertyTypeBool),
                         DeclareRuntimeState<&UIButtonComponent::wasClickedThisFrame>(
                             "wasClickedThisFrame", "Clicked This Frame", kPropertyTypeBool),
                     }));
    AddValidated<UILayoutContainerComponent, &IsUILayoutContainerComponentValid>(
        entries,
        MakeEntry("component.ui_layout_container", "UILayoutContainerComponent", "UI Layout Container",
                     kSectionOrderTypeSpecific,
                     {
                         Declare<&UILayoutContainerComponent::rect>("rect", "Rect",
                                                                         kPropertyTypeRect),
                         InGroup(DeclareEnum<&UILayoutContainerComponent::direction>(
                                        "direction", "Direction", {{0, "Vertical"}, {1, "Horizontal"}}),
                                    "Layout"),
                         InGroup(DeclareEnum<&UILayoutContainerComponent::alignment>(
                                        "alignment", "Alignment", {{0, "Start"}, {1, "Center"}, {2, "End"}}),
                                    "Layout"),
                         InGroup(Declare<&UILayoutContainerComponent::padding>("padding", "Padding",
                                                                                       kPropertyTypeFloat),
                                    "Layout"),
                         InGroup(Declare<&UILayoutContainerComponent::spacing>("spacing", "Spacing",
                                                                                       kPropertyTypeFloat),
                                    "Layout"),
                         InGroup(Declare<&UILayoutContainerComponent::wrapAfter>("wrapAfter", "Wrap After",
                                                                                         kPropertyTypeUInt32),
                                    "Layout"),
                         InGroup(Declare<&UILayoutContainerComponent::autoSizeWidth>("autoSizeWidth", "Auto Size Width",
                                                                                             kPropertyTypeBool),
                                    "Size"),
                         InGroup(Declare<&UILayoutContainerComponent::autoSizeHeight>(
                                        "autoSizeHeight", "Auto Size Height", kPropertyTypeBool),
                                    "Size"),
                     }));
    AddValidated<UIAnchorComponent, &IsUIAnchorComponentValid>(
        entries,
        MakeEntry("component.ui_anchor", "UIAnchorComponent", "UI Anchor", kSectionOrderTypeSpecific,
                     {
                         Declare<&UIAnchorComponent::anchorMin>("anchorMin", "Anchor Min",
                                                                     kPropertyTypeVector2),
                         Declare<&UIAnchorComponent::anchorMax>("anchorMax", "Anchor Max",
                                                                     kPropertyTypeVector2),
                         Declare<&UIAnchorComponent::offsetMin>("offsetMin", "Offset Min",
                                                                     kPropertyTypeVector2),
                         Declare<&UIAnchorComponent::offsetMax>("offsetMax", "Offset Max",
                                                                     kPropertyTypeVector2),
                     }));
    Add<UISliderComponent>(
        entries,
        MakeEntry("component.ui_slider", "UISliderComponent", "UI Slider", kSectionOrderTypeSpecific,
                     {
                         Declare<&UISliderComponent::rect>("rect", "Rect", kPropertyTypeRect),
                         InGroup(Declare<&UISliderComponent::value>("value", "Value", kPropertyTypeFloat),
                                    "Value"),
                         InGroup(Declare<&UISliderComponent::minValue>("minValue", "Min Value",
                                                                               kPropertyTypeFloat),
                                    "Value"),
                         InGroup(Declare<&UISliderComponent::maxValue>("maxValue", "Max Value",
                                                                               kPropertyTypeFloat),
                                    "Value"),
                         InGroup(Declare<&UISliderComponent::step>("step", "Step", kPropertyTypeFloat),
                                    "Value"),
                         InGroup(Declare<&UISliderComponent::trackColor>("trackColor", "Track",
                                                                                 kPropertyTypeColor),
                                    "Appearance"),
                         InGroup(Declare<&UISliderComponent::fillColor>("fillColor", "Fill",
                                                                                kPropertyTypeColor),
                                    "Appearance"),
                         InGroup(Declare<&UISliderComponent::thumbColor>("thumbColor", "Thumb",
                                                                                 kPropertyTypeColor),
                                    "Appearance"),
                         InGroup(Declare<&UISliderComponent::thumbWidth>("thumbWidth", "Thumb Width",
                                                                                 kPropertyTypeFloat),
                                    "Appearance"),
                         InGroup(DeclareRuntimeState<&UISliderComponent::isHovered>(
                                        "isHovered", "Hovered", kPropertyTypeBool),
                                    "State"),
                         InGroup(DeclareRuntimeState<&UISliderComponent::isDragging>(
                                        "isDragging", "Dragging", kPropertyTypeBool),
                                    "State"),
                         InGroup(DeclareRuntimeState<&UISliderComponent::wasChangedThisFrame>(
                                        "wasChangedThisFrame", "Changed This Frame", kPropertyTypeBool),
                                    "State"),
                     }));
    AddValidated<UIProgressBarComponent, &IsUIProgressBarComponentValid>(
        entries,
        MakeEntry("component.ui_progress_bar", "UIProgressBarComponent", "UI Progress Bar",
                     kSectionOrderTypeSpecific,
                     {
                         Declare<&UIProgressBarComponent::rect>("rect", "Rect", kPropertyTypeRect),
                         InGroup(Declare<&UIProgressBarComponent::value>("value", "Value", kPropertyTypeFloat),
                                    "Value"),
                         InGroup(Declare<&UIProgressBarComponent::minValue>("minValue", "Min Value",
                                                                                    kPropertyTypeFloat),
                                    "Value"),
                         InGroup(Declare<&UIProgressBarComponent::maxValue>("maxValue", "Max Value",
                                                                                    kPropertyTypeFloat),
                                    "Value"),
                         InGroup(Declare<&UIProgressBarComponent::backgroundColor>("backgroundColor", "Background",
                                                                                           kPropertyTypeColor),
                                    "Appearance"),
                         InGroup(Declare<&UIProgressBarComponent::fillColor>("fillColor", "Fill",
                                                                                     kPropertyTypeColor),
                                    "Appearance"),
                     }));
    AddValidated<UIView3DComponent, &IsUIView3DComponentValid>(
        entries,
        MakeEntry("component.ui_view_3d", "UIView3DComponent", "UI 3D View", kSectionOrderTypeSpecific,
                     {
                         Declare<&UIView3DComponent::rect>("rect", "Rect", kPropertyTypeRect),
                         WithTooltip(WithCustomDrawer(Declare<&UIView3DComponent::assetPath>(
                                                                "assetPath", "Shows", kPropertyTypeString),
                                                            "file:uventity,uvprefab"),
                                        "The entity asset shown in 3D: a character, a weapon, a car ..."),
                         InGroup(Declare<&UIView3DComponent::cameraDistance>("cameraDistance", "Distance",
                                                                                     kPropertyTypeFloat),
                                    "Camera"),
                         InGroup(Declare<&UIView3DComponent::lookAtHeight>("lookAtHeight", "Look At Height",
                                                                                   kPropertyTypeFloat),
                                    "Camera"),
                         InGroup(Declare<&UIView3DComponent::pitchDegrees>("pitchDegrees", "Pitch",
                                                                                   kPropertyTypeFloat),
                                    "Camera"),
                         InGroup(Declare<&UIView3DComponent::fieldOfViewDegrees>("fieldOfViewDegrees",
                                                                                         "Field of View", kPropertyTypeFloat),
                                    "Camera"),
                         InGroup(Declare<&UIView3DComponent::yawDegrees>("yawDegrees", "Turn", kPropertyTypeFloat),
                                    "Motion"),
                         InGroup(Declare<&UIView3DComponent::spinDegreesPerSecond>("spinDegreesPerSecond",
                                                                                           "Spin (deg/s)", kPropertyTypeFloat),
                                    "Motion"),
                         InGroup(Declare<&UIView3DComponent::backgroundColor>("backgroundColor", "Background",
                                                                                      kPropertyTypeColor),
                                    "Appearance"),
                         InGroup(Declare<&UIView3DComponent::backgroundAlpha>("backgroundAlpha", "Background Alpha",
                                                                                      kPropertyTypeFloat),
                                    "Appearance"),
                         InGroup(WithTooltip(Declare<&UIView3DComponent::useWidgetStage>(
                                                       "useWidgetStage", "Use Widget Stage", kPropertyTypeBool),
                                                   "Stand the asset on the widget's Stage (the Stage tab) and look through its StageCamera"),
                                    "Stage"),
                         InGroup(WithTooltip(Declare<&UIView3DComponent::transparentBackground>(
                                                       "transparentBackground", "Transparent Background", kPropertyTypeBool),
                                                   "No sky behind the stage: the UI and the game show through"),
                                    "Stage"),
                     }));
    Add<UITweenComponent>(
        entries,
        MakeEntry("component.ui_tween", "UITweenComponent", "UI Tween", kSectionOrderTypeSpecific,
                     {
                         InGroup(DeclareEnum<&UITweenComponent::target>("target", "Target",
                                                                                {{0, "Rect"}, {1, "Alpha"}}),
                                    "Target"),
                         InGroup(Declare<&UITweenComponent::fromRect>("fromRect", "From Rect",
                                                                              kPropertyTypeRect),
                                    "Target"),
                         InGroup(Declare<&UITweenComponent::toRect>("toRect", "To Rect", kPropertyTypeRect),
                                    "Target"),
                         InGroup(Declare<&UITweenComponent::fromAlpha>("fromAlpha", "From Alpha",
                                                                               kPropertyTypeFloat),
                                    "Target"),
                         InGroup(Declare<&UITweenComponent::toAlpha>("toAlpha", "To Alpha",
                                                                             kPropertyTypeFloat),
                                    "Target"),
                         InGroup(Declare<&UITweenComponent::duration>("duration", "Duration",
                                                                              kPropertyTypeFloat),
                                    "Timing"),
                         InGroup(Declare<&UITweenComponent::delay>("delay", "Delay", kPropertyTypeFloat),
                                    "Timing"),
                         InGroup(DeclareEnum<&UITweenComponent::ease>("ease", "Ease",
                                                                              {{0, "Linear"},
                                                                               {1, "SineInOut"},
                                                                               {2, "QuadIn"},
                                                                               {3, "QuadOut"},
                                                                               {4, "QuadInOut"},
                                                                               {5, "CubicIn"},
                                                                               {6, "CubicOut"},
                                                                               {7, "CubicInOut"},
                                                                               {8, "OutBack"}}),
                                    "Timing"),
                         InGroup(DeclareEnum<&UITweenComponent::loop>(
                                        "loop", "Loop", {{0, "Once"}, {1, "Loop"}, {2, "PingPong"}}),
                                    "Timing"),
                         InGroup(DeclareRuntimeState<&UITweenComponent::elapsed>(
                                        "elapsed", "Elapsed", kPropertyTypeFloat),
                                    "State"),
                         InGroup(DeclareRuntimeState<&UITweenComponent::playing>(
                                        "playing", "Playing", kPropertyTypeBool),
                                    "State"),
                         InGroup(DeclareRuntimeState<&UITweenComponent::completedThisFrame>(
                                        "completedThisFrame", "Completed This Frame", kPropertyTypeBool),
                                    "State"),
                         InGroup(DeclareRuntimeState<&UITweenComponent::currentAlpha>(
                                        "currentAlpha", "Current Alpha", kPropertyTypeFloat),
                                    "State"),
                     }));
    Add<UICheckboxComponent>(
        entries,
        MakeEntry("component.ui_checkbox", "UICheckboxComponent", "UI Checkbox",
                     kSectionOrderTypeSpecific,
                     {
                         Declare<&UICheckboxComponent::rect>("rect", "Rect", kPropertyTypeRect),
                         Declare<&UICheckboxComponent::boxColor>("boxColor", "Box",
                                                                      kPropertyTypeColor),
                         Declare<&UICheckboxComponent::hoverColor>("hoverColor", "Hover",
                                                                        kPropertyTypeColor),
                         Declare<&UICheckboxComponent::checkColor>("checkColor", "Check",
                                                                        kPropertyTypeColor),
                         Declare<&UICheckboxComponent::checked>("checked", "Checked",
                                                                     kPropertyTypeBool),
                         DeclareRuntimeState<&UICheckboxComponent::isHovered>(
                             "isHovered", "Hovered", kPropertyTypeBool),
                         DeclareRuntimeState<&UICheckboxComponent::wasToggledThisFrame>(
                             "wasToggledThisFrame", "Toggled This Frame", kPropertyTypeBool),
                     }));
    Add<UITooltipComponent>(
        entries,
        MakeEntry("component.ui_tooltip", "UITooltipComponent", "UI Tooltip",
                     kSectionOrderTypeSpecific,
                     {
                         InGroup(Declare<&UITooltipComponent::text>("text", "Text", kPropertyTypeString),
                                    "Content"),
                         InGroup(Declare<&UITooltipComponent::delay>("delay", "Delay",
                                                                             kPropertyTypeFloat),
                                    "Content"),
                         InGroup(Declare<&UITooltipComponent::offset>("offset", "Offset",
                                                                              kPropertyTypeVector2),
                                    "Content"),
                         InGroup(Declare<&UITooltipComponent::padding>("padding", "Padding",
                                                                               kPropertyTypeFloat),
                                    "Layout"),
                         InGroup(Declare<&UITooltipComponent::fontSize>("fontSize", "Font Size",
                                                                                kPropertyTypeFloat),
                                    "Layout"),
                         InGroup(Declare<&UITooltipComponent::backgroundColor>("backgroundColor", "Background",
                                                                                       kPropertyTypeColor),
                                    "Appearance"),
                         InGroup(Declare<&UITooltipComponent::textColor>("textColor", "Text",
                                                                                 kPropertyTypeColor),
                                    "Appearance"),
                         InGroup(DeclareRuntimeState<&UITooltipComponent::hoverTime>(
                                        "hoverTime", "Hover Time", kPropertyTypeFloat),
                                    "State"),
                         InGroup(DeclareRuntimeState<&UITooltipComponent::visibleThisFrame>(
                                        "visibleThisFrame", "Visible This Frame", kPropertyTypeBool),
                                    "State"),
                     }));
    Add<UIDropdownComponent>(
        entries,
        MakeEntry("component.ui_dropdown", "UIDropdownComponent", "UI Dropdown",
                     kSectionOrderTypeSpecific,
                     {
                         Declare<&UIDropdownComponent::rect>("rect", "Rect", kPropertyTypeRect),
                         InGroup(Declare<&UIDropdownComponent::options>("options", "Options",
                                                                                kPropertyTypeString),
                                    "Content"),
                         InGroup(Declare<&UIDropdownComponent::selectedIndex>("selectedIndex", "Selected",
                                                                                      kPropertyTypeInt32),
                                    "Content"),
                         InGroup(Declare<&UIDropdownComponent::placeholder>("placeholder", "Placeholder",
                                                                                    kPropertyTypeString),
                                    "Content"),
                         InGroup(Declare<&UIDropdownComponent::fontSize>("fontSize", "Font Size",
                                                                                 kPropertyTypeFloat),
                                    "Metrics"),
                         InGroup(Declare<&UIDropdownComponent::optionHeight>("optionHeight", "Option Height",
                                                                                     kPropertyTypeFloat),
                                    "Metrics"),
                         InGroup(Declare<&UIDropdownComponent::textPadding>("textPadding", "Text Padding",
                                                                                    kPropertyTypeFloat),
                                    "Metrics"),
                         InGroup(Declare<&UIDropdownComponent::boxColor>("boxColor", "Box",
                                                                                 kPropertyTypeColor),
                                    "Colors"),
                         InGroup(Declare<&UIDropdownComponent::boxHoverColor>("boxHoverColor", "Box Hover",
                                                                                      kPropertyTypeColor),
                                    "Colors"),
                         InGroup(Declare<&UIDropdownComponent::popupColor>("popupColor", "Popup",
                                                                                   kPropertyTypeColor),
                                    "Colors"),
                         InGroup(Declare<&UIDropdownComponent::optionHoverColor>("optionHoverColor",
                                                                                         "Option Hover",
                                                                                         kPropertyTypeColor),
                                    "Colors"),
                         InGroup(Declare<&UIDropdownComponent::selectedColor>("selectedColor", "Selected",
                                                                                      kPropertyTypeColor),
                                    "Colors"),
                         InGroup(Declare<&UIDropdownComponent::textColor>("textColor", "Text",
                                                                                  kPropertyTypeColor),
                                    "Colors"),
                         InGroup(DeclareRuntimeState<&UIDropdownComponent::open>(
                                        "open", "Open", kPropertyTypeBool),
                                    "State"),
                         InGroup(DeclareRuntimeState<&UIDropdownComponent::isHovered>(
                                        "isHovered", "Hovered", kPropertyTypeBool),
                                    "State"),
                         InGroup(DeclareRuntimeState<&UIDropdownComponent::hoveredIndex>(
                                        "hoveredIndex", "Hovered Option", kPropertyTypeInt32),
                                    "State"),
                         InGroup(DeclareRuntimeState<&UIDropdownComponent::wasSelectionChangedThisFrame>(
                                        "wasSelectionChangedThisFrame", "Selection Changed This Frame",
                                        kPropertyTypeBool),
                                    "State"),
                     }));
    Add<UITextInputComponent>(
        entries,
        MakeEntry("component.ui_text_input", "UITextInputComponent", "UI Text Input",
                     kSectionOrderTypeSpecific,
                     {
                         Declare<&UITextInputComponent::rect>("rect", "Rect", kPropertyTypeRect),
                         InGroup(Declare<&UITextInputComponent::text>("text", "Text", kPropertyTypeString),
                                    "Content"),
                         InGroup(Declare<&UITextInputComponent::maxLength>("maxLength", "Max Length",
                                                                                   kPropertyTypeInt32),
                                    "Content"),
                         InGroup(Declare<&UITextInputComponent::placeholder>("placeholder", "Placeholder",
                                                                                     kPropertyTypeString),
                                    "Content"),
                         InGroup(Declare<&UITextInputComponent::fontSize>("fontSize", "Font Size",
                                                                                  kPropertyTypeFloat),
                                    "Metrics"),
                         InGroup(Declare<&UITextInputComponent::textPadding>("textPadding", "Text Padding",
                                                                                     kPropertyTypeFloat),
                                    "Metrics"),
                         InGroup(Declare<&UITextInputComponent::boxColor>("boxColor", "Box",
                                                                                  kPropertyTypeColor),
                                    "Colors"),
                         InGroup(Declare<&UITextInputComponent::focusColor>("focusColor", "Focus",
                                                                                    kPropertyTypeColor),
                                    "Colors"),
                         InGroup(Declare<&UITextInputComponent::textColor>("textColor", "Text",
                                                                                   kPropertyTypeColor),
                                    "Colors"),
                         InGroup(Declare<&UITextInputComponent::caretColor>("caretColor", "Caret",
                                                                                    kPropertyTypeColor),
                                    "Colors"),
                         InGroup(Declare<&UITextInputComponent::placeholderColor>("placeholderColor",
                                                                                          "Placeholder",
                                                                                          kPropertyTypeColor),
                                    "Colors"),
                         InGroup(DeclareRuntimeState<&UITextInputComponent::focused>(
                                        "focused", "Focused", kPropertyTypeBool),
                                    "State"),
                         InGroup(DeclareRuntimeState<&UITextInputComponent::caretIndex>(
                                        "caretIndex", "Caret", kPropertyTypeInt32),
                                    "State"),
                         InGroup(DeclareRuntimeState<&UITextInputComponent::wasSubmittedThisFrame>(
                                        "wasSubmittedThisFrame", "Submitted This Frame", kPropertyTypeBool),
                                    "State"),
                         InGroup(DeclareRuntimeState<&UITextInputComponent::blinkTime>(
                                        "blinkTime", "Blink Time", kPropertyTypeFloat),
                                    "State"),
                         InGroup(DeclareRuntimeState<&UITextInputComponent::scrollOffset>(
                                        "scrollOffset", "Scroll", kPropertyTypeFloat),
                                    "State"),
                     }));
    Add<UIScrollContainerComponent>(
        entries,
        MakeEntry("component.ui_scroll_container", "UIScrollContainerComponent", "UI Scroll Container",
                     kSectionOrderTypeSpecific,
                     {
                         Declare<&UIScrollContainerComponent::rect>("rect", "Rect", kPropertyTypeRect),
                         Declare<&UIScrollContainerComponent::padding>("padding", "Padding",
                                                                            kPropertyTypeFloat),
                         Declare<&UIScrollContainerComponent::gap>("gap", "Gap", kPropertyTypeFloat),
                         Declare<&UIScrollContainerComponent::wheelStep>("wheelStep", "Wheel Step",
                                                                              kPropertyTypeFloat),
                         Declare<&UIScrollContainerComponent::scrollOffset>("scrollOffset", "Scroll",
                                                                                 kPropertyTypeVector2),
                         DeclareRuntimeState<&UIScrollContainerComponent::contentSize>(
                             "contentSize", "Content Size", kPropertyTypeVector2),
                     }));
    Add<UIScrollbarComponent>(
        entries,
        MakeEntry("component.ui_scrollbar", "UIScrollbarComponent", "UI Scrollbar",
                     kSectionOrderTypeSpecific,
                     {
                         Declare<&UIScrollbarComponent::rect>("rect", "Rect", kPropertyTypeRect),
                         Declare<&UIScrollbarComponent::trackColor>("trackColor", "Track",
                                                                          kPropertyTypeColor),
                         Declare<&UIScrollbarComponent::thumbColor>("thumbColor", "Thumb",
                                                                          kPropertyTypeColor),
                         Declare<&UIScrollbarComponent::minThumbHeight>("minThumbHeight", "Min Thumb",
                                                                              kPropertyTypeFloat),
                         DeclareRuntimeState<&UIScrollbarComponent::isHovered>(
                             "isHovered", "Hovered", kPropertyTypeBool),
                         DeclareRuntimeState<&UIScrollbarComponent::isDragging>(
                             "isDragging", "Dragging", kPropertyTypeBool),
                         DeclareRuntimeState<&UIScrollbarComponent::wasChangedThisFrame>(
                             "wasChangedThisFrame", "Changed This Frame", kPropertyTypeBool),
                     }));
}

/// Links an authored Inherit-style choice to the runtime property holding what it resolved to, so
/// the Inspector can show the answer beside the choice rather than as a row of its own.
[[nodiscard]] TypeMetadataProperty ResolvedBy(TypeMetadataProperty property, std::string resolvedProperty) {
    property.resolvedByProperty = std::move(resolvedProperty);
    return property;
}

/// The common Object section: what every object has regardless of what it is. These sort last, below
/// whatever the object itself brings, which is where an author expects them - and in a fixed order
/// among themselves, because an author finds a setting by where it was last time.
/// The abstract 3D bases. Their sections appear on every concrete child, between what the child
/// itself brings and the common Object section.
// The gameplay-owned authored data: what the scene means to a game rather than to the renderer or
// the solver. SpawnPoint3D is the first citizen here (the standard PlayerStart role - Godot ships no
// built-in counterpart), and the section is deliberately declarative: the query that consumes it
// lives in uve_scene, and nothing in this file decides when a point fires.
void DeclareGameplay(std::vector<TypeMetadataEntry>& entries) {
    AddValidated<PlayerComponent, &IsPlayer3DObjectComponentValid>(
        entries,
        MakeEntry("component.player", "PlayerComponent", "Player3D", kSectionOrderTypeSpecific,
            {
                WithTooltip(Declare<&PlayerComponent::possessOnPlay>(
                                   "possessOnPlay", "Possess On Play", kPropertyTypeBool),
                               "This body receives move, look, jump and interact. Off, it is an NPC."),
                WithTooltip(Declare<&PlayerComponent::lookEnabled>("lookEnabled", "Look",
                                                                            kPropertyTypeBool),
                               "Mouse and look-stick rotate this body (yaw) and its camera or spring arm (pitch)."),
                WithRange(WithTooltip(Declare<&PlayerComponent::lookSensitivity>(
                                                "lookSensitivity", "Look Sensitivity", kPropertyTypeFloat),
                                            "Degrees per mouse pixel."),
                             0.0, 10.0, 0.01),
                WithRange(WithTooltip(Declare<&PlayerComponent::lookStickSpeedDegrees>(
                                                "lookStickSpeedDegrees", "Stick Look Speed",
                                                kPropertyTypeFloat),
                                            "Degrees per second at full stick."),
                             0.0, 720.0, 1.0),
                WithRange(WithTooltip(Declare<&PlayerComponent::minPitchDegrees>(
                                                "minPitchDegrees", "Min Pitch", kPropertyTypeFloat),
                                            "Lowest look pitch, in degrees."),
                             -89.0, 89.0, 1.0),
                WithRange(WithTooltip(Declare<&PlayerComponent::maxPitchDegrees>(
                                                "maxPitchDegrees", "Max Pitch", kPropertyTypeFloat),
                                            "Highest look pitch, in degrees."),
                             -89.0, 89.0, 1.0),
                InGroup(DeclareRuntimeState<&PlayerComponent::pitchDegrees>(
                               "pitchDegrees", "Pitch", kPropertyTypeFloat),
                           "State"),
            }));

    AddValidated<HealthComponent, &IsHealthComponentValid>(
        entries,
        MakeEntry("component.health", "HealthComponent", "Health", kSectionOrderTypeSpecific,
            {
                WithRange(WithTooltip(Declare<&HealthComponent::maxHealth>(
                                                "maxHealth", "Max Health", kPropertyTypeFloat),
                                            "Hit points at spawn."),
                             1.0, 10000.0, 1.0),
                WithTooltip(Declare<&HealthComponent::invulnerable>("invulnerable", "Invulnerable",
                                                                            kPropertyTypeBool),
                               "Strikes do not reduce health."),
                InGroup(DeclareRuntimeState<&HealthComponent::health>("health", "Health",
                                                                              kPropertyTypeFloat),
                           "State"),
            }));

    AddValidated<SpawnPoint3DComponent, &IsSpawnPoint3DObjectComponentValid>(
        entries,
        MakeEntry("component.spawn_point", "SpawnPoint3DComponent", "SpawnPoint3D", kSectionOrderTypeSpecific,
            {
                WithTooltip(
                    Declare<&SpawnPoint3DComponent::spawnTag>("spawnTag", "Tag",
                                                                    kPropertyTypeString),
                    "What this point is for. An empty-tag query takes the first live point whatever "
                    "it is called; a respawn screen asks for \"checkpoint\" and a second level "
                    "entry asks for \"level start\"."),
                WithTooltip(
                    Declare<&SpawnPoint3DComponent::localPosition>(
                        "localPosition", "Offset Position", kPropertyTypeVector3),
                    "Where the actor appears relative to this point's own pose, in metres: rotated "
                    "by the point's rotation, deliberately NOT scaled by its scale - an offset is a "
                    "distance, not a volume. A point can sit on a floor seam with its actor a step "
                    "above."),
                WithTooltip(
                    Declare<&SpawnPoint3DComponent::localRotation>(
                        "localRotation", "Offset Rotation", kPropertyTypeQuaternion),
                    "Which way the actor faces when it appears, composed onto this point's own "
                    "rotation."),
                WithTooltip(Declare<&SpawnPoint3DComponent::enabled>("enabled", "Enabled",
                                                                             kPropertyTypeBool),
                               "Off, the point is invisible to every spawn query - which is also "
                               "the state a spent one-shot point is left in."),
                WithTooltip(
                    Declare<&SpawnPoint3DComponent::oneShot>("oneShot", "One Shot",
                                                                   kPropertyTypeBool),
                    "A checkpoint: the first caller that actually spawns from this point spends "
                    "it, and no later query can use it again this session. The authored value "
                    "comes back when Play is left, like every other sandboxed edit."),
            }));

    TypeMetadataProperty areaIgnore = WithTooltip(
        Declare<&InteractionArea3DComponent::ignoreEntity>("ignoreEntity", "Ignore Entity",
                                                                 kPropertyTypeEntity),
        "Never list this interactor. Set the owner so a character does not interact with a volume it carries.");
    areaIgnore.flags = TypeMetadataPropertyFlags::EntityReference;
    AddValidated<InteractionArea3DComponent, &IsInteractionArea3DObjectComponentValid>(
        entries,
        MakeEntry("component.interaction_area_3d", "InteractionArea3DComponent", "InteractionArea3D", kSectionOrderTypeSpecific,
            {
                WithTooltip(
                    Declare<&InteractionArea3DComponent::enabled>("enabled", "Enabled", kPropertyTypeBool),
                    "Off, the area tracks nobody and drops focus this tick."),
                WithTooltip(
                    Declare<&InteractionArea3DComponent::halfExtents>("halfExtents", "Half Extents",
                                                                           kPropertyTypeVector3),
                    "The exact oriented box, world position and rotation plus these half-extents. "
                    "World SCALE is not applied."),
                WithTooltip(
                    Declare<&InteractionArea3DComponent::interactionTag>("interactionTag", "Tag",
                                                                              kPropertyTypeString),
                    "Carried for gameplay. The scan does not filter on it."),
                WithRange(
                    WithTooltip(Declare<&InteractionArea3DComponent::maximumCandidates>(
                                       "maximumCandidates", "Max Interactors", kPropertyTypeUInt32),
                                   "How many overlapping interactors this area stores. Overflow is flagged, "
                                   "not dropped silently. 1 to 4096; storage itself caps at 16."),
                    1.0, 4096.0, 1.0),
                WithCustomDrawer(
                    WithTooltip(Declare<&InteractionArea3DComponent::collisionLayer>(
                                       "collisionLayer", "Layer", kPropertyTypeBitMask32),
                                   "The layers this area is on."),
                    std::string(kLayerMaskDrawerPhysics)),
                WithCustomDrawer(
                    WithTooltip(Declare<&InteractionArea3DComponent::collisionMask>(
                                       "collisionMask", "Mask", kPropertyTypeBitMask32),
                                   "The layers this area accepts interactors from. Both sides must agree."),
                    std::string(kLayerMaskDrawerPhysics)),
                std::move(areaIgnore),
                InGroup(DeclareRuntimeState<&InteractionArea3DComponent::interactorCount>(
                               "interactorCount", "Interactors", kPropertyTypeUInt8),
                           "Result"),
                InGroup(DeclareRuntimeState<&InteractionArea3DComponent::interactorsTruncated>(
                               "interactorsTruncated", "Truncated", kPropertyTypeBool),
                           "Result"),
                InGroup(DeclareRuntimeState<&InteractionArea3DComponent::focusedByPrimaryInteractor>(
                               "focusedByPrimaryInteractor", "Focused", kPropertyTypeBool),
                           "Result"),
            }));
}

/// The two AI navigation objects: the region that bakes into a navmesh, and the agent that walks it.
///
/// A region's properties are the bake's own inputs - the volume, the agent it is baked FOR, and the
/// layers its polygons carry - because a mesh eroded for one width is not the mesh a wider agent
/// needs. An agent's are its own measurements plus the route it last published, and the route is
/// declared as runtime state: written by the navigation step, never authored, never saved, and shown
/// while the game runs because that is the only time it describes something real.
void DeclareNavigation(std::vector<TypeMetadataEntry>& entries) {
    using R = NavMeshVolume3DComponent;
    AddValidated<NavMeshVolume3DComponent, &IsNavMeshVolume3DObjectComponentValid>(
        entries,
        MakeEntry("component.nav_mesh_volume_3d", "NavMeshVolume3DComponent", "NavMeshVolume3D", kSectionOrderTypeSpecific,
            {
                WithTooltip(Declare<&R::enabled>("enabled", "Enabled", kPropertyTypeBool),
                               "Off, the region has no mesh at all: an agent standing on it fails "
                               "rather than walks ground the author just took away."),
                InGroup(WithTooltip(
                               WithRange(Declare<&R::boundsHalfExtents>("boundsHalfExtents", "Size",
                                                                              kPropertyTypeVector3),
                                            0.001, 100000.0, 0.01),
                               "The volume baked into a navmesh, centred on the object and aligned to the world "
                               "axes - the bake's grid is built on the world axes, so where the object is "
                               "rotated, its unrotated volume is what gets rasterized."),
                           "Bounds"),
                InGroup(WithTooltip(
                               WithRange(Declare<&R::cellSize>("cellSize", "Cell Size", kPropertyTypeFloat),
                                            0.05, 10.0, 0.05),
                               "The rasterization grid, in metres. Finer follows geometry more closely and costs "
                               "more rays; a grid too large for the bake's cell budget is made coarser and says "
                               "so in the bake's report."),
                           "Bounds"),
                InGroup(WithTooltip(
                               WithRange(Declare<&R::agentRadius>("agentRadius", "Agent Radius",
                                                                        kPropertyTypeFloat),
                                            0.0, 100.0, 0.01),
                               "Ground closer than this to a wall, a ledge or the region's edge is eroded away: "
                               "an agent of this width cannot stand there with its body on the mesh."),
                           "Agent"),
                InGroup(WithTooltip(
                               WithRange(Declare<&R::agentHeight>("agentHeight", "Agent Height",
                                                                        kPropertyTypeFloat),
                                            0.01, 1000.0, 0.01),
                               "How much headroom the ground needs to be walkable. A region shorter than this "
                               "bakes nothing, which is the honest answer for a crawlspace no agent fits in."),
                           "Agent"),
                InGroup(WithTooltip(
                               WithRange(Declare<&R::maximumSlopeDegrees>("maximumSlopeDegrees", "Max Slope",
                                                                                kPropertyTypeFloat),
                                            0.1, 89.9, 0.1),
                               "The steepest surface that still counts as ground, in degrees from up. Anything "
                               "steeper is a wall as far as this navmesh is concerned."),
                           "Agent"),
                InGroup(WithTooltip(
                               WithRange(Declare<&R::maximumStepHeight>("maximumStepHeight", "Max Step",
                                                                              kPropertyTypeFloat),
                                            0.0, 1000.0, 0.01),
                               "The tallest rise between neighbouring cells that may still be walked, in "
                               "metres. Taller than this and they are two floors rather than one step."),
                           "Agent"),
                InGroup(WithCustomDrawer(
                               WithTooltip(Declare<&R::navigationLayers>("navigationLayers", "Layers",
                                                                               kPropertyTypeBitMask32),
                                              "The layers written into the baked polygons, matched against an "
                                              "agent's own mask: a path may only use polygons whose layers "
                                              "intersect it."),
                               std::string(kLayerMaskDrawerPhysics)),
                           "Build"),
                InGroup(WithTooltip(
                               Declare<&R::rebuildRequested>("rebuildRequested", "Rebuild", kPropertyTypeBool),
                               "Raise it to re-rasterize a region whose volume did not move but whose "
                               "surroundings did. The navigation step bakes and then clears it."),
                           "Build"),
            }));

    using S = NavSeeker3DComponent;
    AddValidated<NavSeeker3DComponent, &IsNavSeeker3DObjectComponentValid>(
        entries,
        MakeEntry("component.nav_seeker_3d", "NavSeeker3DComponent", "NavSeeker3D", kSectionOrderTypeSpecific,
            {
                WithTooltip(Declare<&S::enabled>("enabled", "Enabled", kPropertyTypeBool),
                               "Off, the agent publishes nothing - no velocity, no route - and "
                               "forgets the route it had."),
                WithTooltip(Declare<&S::targetPosition>("targetPosition", "Target",
                                                              kPropertyTypeVector3),
                               "Where the agent is walking to, in world space. Changing it drops "
                               "the current route and searches on the next step, whatever the "
                               "update interval says."),
                WithTooltip(
                    Declare<&S::avoidanceEnabled>("avoidanceEnabled", "Avoidance",
                                                     kPropertyTypeBool),
                    "On, other agents inside the avoidance radius push this one aside so two "
                    "agents on one route end up beside each other rather than inside each other."),
                WhenOn<&S::avoidanceEnabled>(WithRange(
                    WithTooltip(Declare<&S::avoidanceRadius>("avoidanceRadius", "Avoid Radius",
                                                                   kPropertyTypeFloat),
                                   "How close another agent's centre may come before it pushes, in "
                                   "metres. 0 is the same as switching avoidance off."),
                    0.0, 1000.0, 0.01)),
                InGroup(WithRange(Declare<&S::radius>("radius", "Radius", kPropertyTypeFloat),
                                        0.01, 1000.0, 0.01),
                           "Agent"),
                InGroup(WithRange(Declare<&S::height>("height", "Height", kPropertyTypeFloat),
                                        0.02, 1000.0, 0.01),
                           "Agent"),
                InGroup(WithTooltip(
                               WithRange(Declare<&S::maxSpeed>("maxSpeed", "Max Speed",
                                                                     kPropertyTypeFloat),
                                            0.0, 10000.0, 0.1),
                               "Top speed on a straight leg, in metres per second. What actually "
                               "moves the body is the mover or script reading this agent's "
                               "desiredVelocity."),
                           "Agent"),
                InGroup(WithTooltip(
                               WithRange(Declare<&S::acceleration>("acceleration", "Acceleration",
                                                                         kPropertyTypeFloat),
                                            0.0, 10000.0, 0.1),
                               "How hard desiredVelocity may change per second. The velocity is what "
                               "a mover is handed, and an unbounded step change is a jolt; 0 "
                               "publishes the wanted velocity immediately."),
                           "Agent"),
                InGroup(WithTooltip(
                               WithRange(Declare<&S::targetTolerance>("targetTolerance", "Target "
                                                                                           "Tolerance",
                                                                            kPropertyTypeFloat),
                                            0.0, 1000.0, 0.01),
                               "How close to the target counts as arrived. Larger than the waypoint "
                               "radius on purpose: an agent that had to stop within a metre of a "
                               "moving target would hunt for it."),
                           "Agent"),
                InGroup(WithTooltip(
                               WithRange(Declare<&S::waypointRadius>("waypointRadius", "Waypoint "
                                                                                           "Radius",
                                                                           kPropertyTypeFloat),
                                            0.0, 1000.0, 0.01),
                               "How close to a waypoint counts as reached, so the agent turns to the "
                               "next one instead of walking back for it."),
                           "Agent"),
                InGroup(WithTooltip(
                               WithRange(Declare<&S::slowDownRadius>("slowDownRadius", "Slow Down "
                                                                                           "Radius",
                                                                           kPropertyTypeFloat),
                                            0.0, 1000.0, 0.01),
                               "Inside this distance from the target the speed is scaled down, so the "
                               "agent arrives rather than overshoots. 0 keeps full speed until the "
                               "tolerance stops it."),
                           "Agent"),
                InGroup(WithCustomDrawer(
                               WithTooltip(Declare<&S::navigationLayers>("navigationLayers",
                                                                               "Layers",
                                                                               kPropertyTypeBitMask32),
                                              "Which polygons this agent may walk: a path may only "
                                              "cross ground whose layers intersect this mask."),
                               std::string(kLayerMaskDrawerPhysics)),
                           "Agent"),
                InGroup(WithTooltip(
                               WithRange(Declare<&S::pathUpdateInterval>("pathUpdateInterval",
                                                                               "Update Interval",
                                                                               kPropertyTypeFloat),
                                            0.01, 10.0, 0.01),
                               "How often the route is reconsidered, in seconds. Replanning every "
                               "frame would be correct and wasteful; replanning rarely leaves an "
                               "agent walking into a door that closed behind it."),
                           "Route"),
                // The route the last step produced. Runtime state: written by the navigation step,
                // never authored, never saved - a loaded agent finds its own way from its target.
                InGroup(DeclareRuntimeState<&S::nextPathPosition>("nextPathPosition",
                                                                        "Next Waypoint",
                                                                        kPropertyTypeVector3),
                           "Route"),
                InGroup(DeclareRuntimeState<&S::desiredVelocity>("desiredVelocity",
                                                                       "Desired Velocity",
                                                                       kPropertyTypeVector3),
                           "Route"),
                InGroup(DeclareRuntimeStateEnum<&S::pathStatus>(
                               "pathStatus", "Status",
                               {{0, "Idle"}, {1, "Following"}, {2, "Finished"}, {3, "Failed"}}),
                           "Route"),
                InGroup(DeclareRuntimeState<&S::pathChanged>("pathChanged", "Path Changed",
                                                                   kPropertyTypeBool),
                           "Route"),
                InGroup(DeclareRuntimeState<&S::targetReached>("targetReached", "Target Reached",
                                                                     kPropertyTypeBool),
                           "Route"),
            }));
}

void DeclareObjectBases(std::vector<TypeMetadataEntry>& entries) {
    AddValidated<BoneModifierComponent, &IsBoneModifierComponentValid>(
        entries,
        MakeEntry("component.bone_modifier", "BoneModifierComponent", std::string{BoneModifier3DObjectDefinition::typeName}, kSectionOrderObjectBase,
                     {
                         WithTooltip(Declare<&BoneModifierComponent::active>("active", "Active",
                                                                                        kPropertyTypeBool),
                                        "Off skips this modifier, as if it were not there."),
                         WithTooltip(WithRange(Declare<&BoneModifierComponent::influence>(
                                                         "influence", "Influence", kPropertyTypeFloat),
                                                     0.0, 1.0, 0.01),
                                        "How much of the modifier's result is blended over the pose."),
                     }));

    TypeMetadataProperty priority = WithTooltip(
        Declare<&PhysicsObjectComponent::collisionPriority>("collisionPriority", "Priority",
                                                                  kPropertyTypeFloat),
        "How strongly this object is pushed out of an overlap; higher yields less.");
    priority.range = {true, 0.0, 1000000.0, 0.1};
    AddValidated<PhysicsObjectComponent, &IsPhysicsObjectComponentValid>(
        entries,
        MakeEntry("component.physics_object", "PhysicsObjectComponent", std::string{PhysicsObject3DObjectDefinition::typeName}, kSectionOrderObjectBase + 1,
            {
                WithTooltip(DeclareEnum<&PhysicsObjectComponent::disableMode>(
                                   "disableMode", "Disable Mode",
                                   {{0, "Remove"}, {1, "Make Static"}, {2, "Keep Active"}}),
                               "What happens to this object while its Process mode stops it."),
                std::move(priority),
                WithTooltip(Declare<&PhysicsObjectComponent::inputRayPickable>(
                                   "inputRayPickable", "Ray Pickable", kPropertyTypeBool),
                               "Whether a mouse or touch pick can hit this object."),
                WithTooltip(Declare<&PhysicsObjectComponent::inputCaptureOnDrag>(
                                   "inputCaptureOnDrag", "Capture On Drag", kPropertyTypeBool),
                               "Whether a drag that started here keeps reporting here after leaving."),
            }));

    // Sorts before PhysicsObject3D: a base that derives from another is drawn above it.
    Add<SolidBodyComponent>(
        entries,
        MakeEntry("component.solid_body", "SolidBodyComponent", std::string{SolidBody3DObjectDefinition::typeName}, kSectionOrderObjectBase,
                     {
                         InGroup(WithTooltip(Declare<&SolidBodyComponent::lockMotionX>(
                                                       "lockMotionX", "X", kPropertyTypeBool),
                                                   "Never moves along the world X axis."),
                                    "Lock Motion"),
                         InGroup(WithTooltip(Declare<&SolidBodyComponent::lockMotionY>(
                                                       "lockMotionY", "Y", kPropertyTypeBool),
                                                   "Never moves along the world Y axis."),
                                    "Lock Motion"),
                         InGroup(WithTooltip(Declare<&SolidBodyComponent::lockMotionZ>(
                                                       "lockMotionZ", "Z", kPropertyTypeBool),
                                                   "Never moves along the world Z axis - a side view locks this one."),
                                    "Lock Motion"),
                     }));

    AddValidated<RenderInstanceComponent, &IsRenderInstanceComponentValid>(
        entries,
        MakeEntry("component.render_instance", "RenderInstanceComponent", std::string{RenderInstance3DObjectDefinition::typeName}, kSectionOrderObjectBase + 10,
                     {
                         WithCustomDrawer(
                             WithTooltip(Declare<&RenderInstanceComponent::renderLayers>(
                                                "renderLayers", "Layers", kPropertyTypeBitMask32),
                                            "The render layers this is on. A camera draws it only when their layers overlap."),
                             std::string(kLayerMaskDrawerRender)),
                         WithTooltip(Declare<&RenderInstanceComponent::sortingOffset>(
                                            "sortingOffset", "Sorting Offset", kPropertyTypeFloat),
                                        "Moves this forward (negative) or back in transparent sorting, without moving it."),
                         WithTooltip(Declare<&RenderInstanceComponent::sortingUseAabbCenter>(
                                            "sortingUseAabbCenter", "Sort By Bounds Center", kPropertyTypeBool),
                                        "Sort by the centre of the bounds rather than the origin."),
                     }));

    using S = SurfaceInstanceComponent;
    TypeMetadataEntry surfaceEntry =
        MakeEntry("component.surface_instance", "SurfaceInstanceComponent", std::string{SurfaceInstance3DObjectDefinition::typeName}, kSectionOrderObjectBase + 3,
            {
                WithTooltip(WithCustomDrawer(Declare<&S::materialOverridePath>("materialOverridePath", "Override",
                                                                                          kPropertyTypeString),
                                                     "file:uvmat"),
                               "Material override: used on every surface in place of the mesh's own. Empty keeps them."),
                WithTooltip(WithCustomDrawer(Declare<&S::materialOverlayPath>("materialOverlayPath", "Overlay",
                                                                                         kPropertyTypeString),
                                                     "file:uvmat"),
                               "Material overlay: drawn over every surface, on top of whatever it already shows."),
                // Per-surface overrides, index for index with the mesh's surfaces. Declared so the
                // doc persists them, hidden because the rows belong beside the surfaces they name:
                // the MeshInstance3D inspector draws one picker per surface, and a generic text row
                // per entry would be a list of paths with nothing saying which surface each is.
                [] {
                    TypeMetadataProperty property = Declare<&S::surfaceMaterialOverridePaths>(
                        "surfaceMaterialOverridePaths", "Surface Overrides", kPropertyTypeStringList);
                    property.flags = TypeMetadataPropertyFlags::Hidden;
                    return property;
                }(),
                WithTooltip(WithRange(Declare<&S::transparency>("transparency", "Transparency",
                                                                         kPropertyTypeFloat),
                                            0.0, 1.0, 0.01),
                               "Fades the whole instance out: 0 as authored, 1 invisible."),
                InGroup(WithTooltip(DeclareEnum<&S::castShadow>(
                                              "castShadow", "Cast Shadow",
                                              {{0, "Off"}, {1, "On"}, {2, "Double Sided"}, {3, "Shadows Only"}}),
                                          "Whether it casts a shadow, and Shadows Only for an invisible caster."),
                           "Shadow"),
                InGroup(WithTooltip(DeclareEnum<&S::lightingMode>(
                                              "lightingMode", "Lighting",
                                              {{0, "Disabled"}, {1, "Static (Baked)"}, {2, "Dynamic"}}),
                                          "How baked lighting treats it: ignored, baked into, or lit at runtime."),
                           "Global Illumination"),
                InGroup(WithTooltip(WithRange(Declare<&S::visibilityRangeBegin>(
                                                           "visibilityRangeBegin", "Begin", kPropertyTypeFloat),
                                                       0.0, 1000000.0, 0.1),
                                          "Hidden closer to the camera than this. 0 never hides it up close."),
                           "Visibility Range"),
                InGroup(WithRange(Declare<&S::visibilityRangeBeginMargin>(
                                            "visibilityRangeBeginMargin", "Begin Margin", kPropertyTypeFloat),
                                        0.0, 1000000.0, 0.1),
                           "Visibility Range"),
                InGroup(WithTooltip(WithRange(Declare<&S::visibilityRangeEnd>(
                                                           "visibilityRangeEnd", "End", kPropertyTypeFloat),
                                                       0.0, 1000000.0, 0.1),
                                          "Hidden farther from the camera than this. 0 never hides it far away."),
                           "Visibility Range"),
                InGroup(WithRange(Declare<&S::visibilityRangeEndMargin>(
                                            "visibilityRangeEndMargin", "End Margin", kPropertyTypeFloat),
                                        0.0, 1000000.0, 0.1),
                           "Visibility Range"),
                InGroup(WithTooltip(DeclareEnum<&S::visibilityRangeFadeMode>(
                                              "visibilityRangeFadeMode", "Fade",
                                              {{0, "Disabled"}, {1, "Self"}, {2, "Dependencies"}}),
                                          "Fade across the margins instead of popping."),
                           "Visibility Range"),
                InGroup(WithTooltip(WithRange(Declare<&S::extraCullMargin>(
                                                           "extraCullMargin", "Extra Cull Margin", kPropertyTypeFloat),
                                                       0.0, 100000.0, 0.01),
                                          "Grows the bounds used for culling, for shaders that move vertices outward."),
                           "Culling"),
                InGroup(WithTooltip(WithRange(Declare<&S::lodBias>("lodBias", "LOD Bias",
                                                                               kPropertyTypeFloat),
                                                       0.001, 128.0, 0.01),
                                          "Above 1 keeps detailed levels longer; below 1 drops them sooner."),
                           "Culling"),
                InGroup(WithTooltip(Declare<&S::ignoreOcclusionCulling>(
                                              "ignoreOcclusionCulling", "Ignore Occlusion", kPropertyTypeBool),
                                          "Never hidden because something else covers it."),
                           "Culling"),
            });
    AddValidated<SurfaceInstanceComponent, &IsSurfaceInstanceComponentValid>(
        entries, std::move(surfaceEntry));

    using L = LightEmitterComponent;
    AddValidated<LightEmitterComponent, &IsLightEmitterComponentValid>(
        entries,
        MakeEntry("component.light_emitter", "LightEmitterComponent", std::string{LightEmitter3DObjectDefinition::typeName}, kSectionOrderObjectBase + 4,
            {
                Declare<&L::color>("color", "Color", kPropertyTypeLinearColor),
                WithTooltip(WithRange(Declare<&L::energy>("energy", "Energy", kPropertyTypeFloat), 0.0,
                                            1000.0, 0.01),
                               "How bright the light is."),
                WithTooltip(WithRange(Declare<&L::indirectEnergy>("indirectEnergy", "Indirect Energy",
                                                                           kPropertyTypeFloat),
                                            0.0, 1000.0, 0.01),
                               "Multiplies the light's bounced (indirect) contribution only."),
                WithTooltip(WithRange(Declare<&L::volumetricFogEnergy>(
                                                "volumetricFogEnergy", "Fog Energy", kPropertyTypeFloat),
                                            0.0, 1000.0, 0.01),
                               "How strongly it lights volumetric fog and fog volumes."),
                WithTooltip(WithRange(Declare<&L::specular>("specular", "Specular", kPropertyTypeFloat),
                                            0.0, 16.0, 0.01),
                               "Strength of the light's highlights on shiny surfaces."),
                WithTooltip(DeclareEnum<&L::bakeMode>("bakeMode", "Bake Mode",
                                                            {{0, "Disabled"}, {1, "Static"}, {2, "Dynamic"}}),
                               "How baked lighting uses it: not at all, fully baked, or indirect only."),
                WithCustomDrawer(WithTooltip(Declare<&L::cullMask>("cullMask", "Cull Mask",
                                                                            kPropertyTypeBitMask32),
                                                   "The render layers this light affects."),
                                    std::string(kLayerMaskDrawerRender)),
                InGroup(Declare<&L::shadowEnabled>("shadowEnabled", "Enabled", kPropertyTypeBool), "Shadow"),
                InGroup(WhenOn<&L::shadowEnabled>(WithTooltip(
                               WithRange(Declare<&L::shadowBias>("shadowBias", "Bias", kPropertyTypeFloat),
                                           -1.0, 10.0, 0.001),
                               "Negative inherits the project's default shadow depth bias; non-negative values "
                               "override it for this light.")),
                           "Shadow"),
                InGroup(WhenOn<&L::shadowEnabled>(WithTooltip(
                               WithRange(Declare<&L::shadowNormalBias>("shadowNormalBias", "Normal Bias",
                                                                             kPropertyTypeFloat),
                                           -1.0, 10.0, 0.001),
                               "Negative inherits the project's default normal-bias multiplier; non-negative "
                               "values override it for this light.")),
                           "Shadow"),
                InGroup(WhenOn<&L::shadowEnabled>(WithRange(
                               Declare<&L::shadowOpacity>("shadowOpacity", "Opacity", kPropertyTypeFloat), 0.0,
                               1.0, 0.01)),
                           "Shadow"),
                InGroup(WhenOn<&L::shadowEnabled>(WithRange(
                               Declare<&L::shadowBlur>("shadowBlur", "Blur", kPropertyTypeFloat), 0.0, 64.0,
                               0.01)),
                           "Shadow"),
                InGroup(Declare<&L::distanceFadeEnabled>("distanceFadeEnabled", "Enabled", kPropertyTypeBool),
                           "Distance Fade"),
                InGroup(WhenOn<&L::distanceFadeEnabled>(WithRange(
                               Declare<&L::distanceFadeBegin>("distanceFadeBegin", "Begin", kPropertyTypeFloat),
                               0.0, 1000000.0, 0.1)),
                           "Distance Fade"),
                InGroup(WhenOn<&L::distanceFadeEnabled>(WithRange(
                               Declare<&L::distanceFadeShadow>("distanceFadeShadow", "Shadow",
                                                                  kPropertyTypeFloat),
                               0.0, 1000000.0, 0.1)),
                           "Distance Fade"),
                InGroup(WhenOn<&L::distanceFadeEnabled>(WithRange(
                               Declare<&L::distanceFadeLength>("distanceFadeLength", "Length",
                                                                  kPropertyTypeFloat),
                               0.0, 1000000.0, 0.1)),
                           "Distance Fade"),
            }));
}

/// Skeleton3D: a Object3D child. Its bones are read-only here - they come from the rigged model the
/// Source names and change only by re-exporting it - so they are declared for display and saving,
/// with a drawer that shows the hierarchy instead of a generic editor.
void DeclareSkeleton(std::vector<TypeMetadataEntry>& entries) {
    using K = Skeleton3DComponent;
    TypeMetadataProperty bones = WithCustomDrawer(Declare<&K::bones>("bones", "Bones", "SkeletonBones"),
                                                        "skeleton-bones");
    bones.flags = TypeMetadataPropertyFlags::ReadOnly;
    AddValidated<Skeleton3DComponent, &IsSkeleton3DObjectComponentValid>(
        entries,
        MakeEntry("component.skeleton_3d", "Skeleton3DComponent", "Skeleton3D", kSectionOrderTypeSpecific,
                     {
                         WithTooltip(WithCustomDrawer(Declare<&K::skeletonAssetPath>(
                                                                "skeletonAssetPath", "Source", kPropertyTypeString),
                                                            "skeleton-source"),
                                        "The rigged model (a glTF exported from Blender, say) whose armature this "
                                        "skeleton uses. Its bones are read from it."),
                         WithTooltip(Declare<&K::enabled>("enabled", "Enabled", kPropertyTypeBool),
                                        "Off: attached meshes and bone attachments stop following this skeleton."),
                         std::move(bones),
                     }));
}

/// BoneAttachment3D: a scene object that rides a bone of another object's skeleton instead of the
/// hierarchy. The skeleton is an entity reference - flagged so the serializer remaps it through its
/// file-local id table, exactly like the mixer's target and the visibility parent - and the two
/// runtime answers the engine writes back each frame are declared as runtime state, so an author
/// can see whether the attachment is on its bone without a debugger.
void DeclareBoneAttachment(std::vector<TypeMetadataEntry>& entries) {
    using A = BoneAttachment3DComponent;
    TypeMetadataProperty skeleton =
        WithTooltip(Declare<&A::skeleton>("skeleton", "Skeleton", kPropertyTypeEntity),
                       "The object whose skeleton this attachment rides. Empty leaves the object where it "
                       "was authored.");
    skeleton.flags = TypeMetadataPropertyFlags::EntityReference;
    AddValidated<BoneAttachment3DComponent, &IsBoneAttachment3DObjectComponentValid>(
        entries,
        MakeEntry("component.bone_attachment_3d", "BoneAttachment3DComponent", "BoneAttachment3D", kSectionOrderTypeSpecific,
            {
                std::move(skeleton),
                WithTooltip(Declare<&A::enabled>("enabled", "Enabled", kPropertyTypeBool),
                               "Off stops following the bone without removing the object - it keeps the "
                               "transform it has."),
                WithTooltip(Declare<&A::boneName>("boneName", "Bone", kPropertyTypeString),
                               "The bone to ride, by the name the skeleton asset gives it. Exact and "
                               "case-sensitive: renaming a bone in the source loses this attachment "
                               "instead of binding it to whichever bone took the name."),
                WithTooltip(Declare<&A::boneIndex>("boneIndex", "Bone Index",
                                                         kPropertyTypeUInt32),
                               "The bone's index in the skeleton. 4294967295 means \"bind by the name "
                               "above\"; any other value wins over the name when it names a real bone."),
                WithTooltip(Declare<&A::localPosition>("localPosition", "Position",
                                                             kPropertyTypeVector3),
                               "Where the object sits on the bone, in the BONE's own space: 0.1 on Y is "
                               "10 cm along the bone's up axis, not the world's."),
                WithTooltip(Declare<&A::localRotation>("localRotation", "Rotation",
                                                             kPropertyTypeQuaternion),
                               "How the object is turned on the bone, applied after the bone's own "
                               "rotation - so it stays gripped when the arm swings."),
                WithTooltip(Declare<&A::localScale>("localScale", "Scale", kPropertyTypeVector3),
                               "The object's scale on top of the bone chain's, so a scaled rig scales "
                               "what it carries."),
                InGroup(DeclareRuntimeState<&A::bound>("bound", "Bound", kPropertyTypeBool),
                           "State"),
                InGroup(DeclareRuntimeState<&A::resolvedBoneIndex>("resolvedBoneIndex", "Resolved Bone",
                                                                         kPropertyTypeUInt32),
                           "State"),
            }));
}

/// TwoBoneIK3D: a limb solved back from a target instead of forward from its joints. The three bone
/// references are index-or-name pairs - the index is the reference that cannot go stale, the name
/// the one an author can read in a DCC tool - and the skeleton, target and pole are entity
/// references, flagged so the serializer remaps them through its file-local id table. The answers
/// the solver writes back each pass are declared as runtime state, so an author can see whether a
/// hand actually reached what it was aimed at without a debugger.
void DeclareTwoBoneIK(std::vector<TypeMetadataEntry>& entries) {
    using A = TwoBoneIK3DComponent;
    const auto entityReference = [](TypeMetadataProperty property) {
        property.flags = TypeMetadataPropertyFlags::EntityReference;
        return property;
    };
    AddValidated<TwoBoneIK3DComponent, &IsTwoBoneIK3DObjectComponentValid>(
        entries,
        MakeEntry("component.two_bone_ik_3d", "TwoBoneIK3DComponent", "TwoBoneIK3D", kSectionOrderTypeSpecific,
            {
                entityReference(WithTooltip(Declare<&A::skeleton>("skeleton", "Skeleton",
                                                                         kPropertyTypeEntity),
                                               "The object whose skeleton this chain solves. Empty leaves "
                                               "the pose exactly as the animation wrote it.")),
                WithTooltip(Declare<&A::enabled>("enabled", "Enabled", kPropertyTypeBool),
                               "Off stops solving without removing the object."),
                InGroup(WithTooltip(Declare<&A::rootBoneName>("rootBoneName", "Root Bone",
                                                                        kPropertyTypeString),
                                          "The chain's first bone - the shoulder, the hip. Exact and "
                                          "case-sensitive; renaming a bone in the source loses the chain "
                                          "instead of driving a different limb."),
                           "Chain"),
                InGroup(WithTooltip(Declare<&A::rootBoneIndex>("rootBoneIndex", "Root Index",
                                                                        kPropertyTypeUInt32),
                                          "The bone's index in the skeleton. 4294967295 means \"ask the name\"; "
                                          "any other value wins over it when it names a real bone."),
                           "Chain"),
                InGroup(WithTooltip(Declare<&A::middleBoneName>("middleBoneName", "Middle Bone",
                                                                          kPropertyTypeString),
                                          "The bone the chain bends at - the elbow, the knee. Must be a child "
                                          "of the root bone."),
                           "Chain"),
                InGroup(WithTooltip(Declare<&A::middleBoneIndex>("middleBoneIndex", "Middle Index",
                                                                          kPropertyTypeUInt32),
                                          "The bend bone by index; the name above is the fallback."),
                           "Chain"),
                InGroup(WithTooltip(Declare<&A::endBoneName>("endBoneName", "End Bone",
                                                                       kPropertyTypeString),
                                          "The bone whose origin reaches the target - the wrist, the ankle. "
                                          "Must be a child of the middle bone; its own rotation is left alone."),
                           "Chain"),
                InGroup(WithTooltip(Declare<&A::endBoneIndex>("endBoneIndex", "End Index",
                                                                       kPropertyTypeUInt32),
                                          "The effector bone by index; the name above is the fallback."),
                           "Chain"),
                InGroup(entityReference(WithTooltip(Declare<&A::target>("target", "Target",
                                                                                 kPropertyTypeEntity),
                                                          "The object whose world position the limb "
                                                          "reaches for. Empty uses the point below.")),
                           "Target"),
                InGroup(WithTooltip(Declare<&A::targetPosition>("targetPosition", "Target Point",
                                                                         kPropertyTypeVector3),
                                          "Where the limb should end, in the SKELETON's own space - "
                                          "scaled and rotated with it. Ignored while a target object "
                                          "above is set."),
                           "Target"),
                InGroup(entityReference(WithTooltip(Declare<&A::poleTarget>("poleTarget", "Pole",
                                                                                     kPropertyTypeEntity),
                                                          "A point the joint bends toward. An elbow has a "
                                                          "circle of positions that all reach the target; "
                                                          "this picks one.")),
                           "Target"),
                InGroup(WithTooltip(Declare<&A::poleDirection>("poleDirection", "Pole Direction",
                                                                        kPropertyTypeVector3),
                                          "The bend direction when no pole object is set, in the "
                                          "skeleton's own space. Zero keeps the plane the pose is "
                                          "already in."),
                           "Target"),
                InGroup(DeclareRuntimeState<&A::solved>("solved", "Solved", kPropertyTypeBool), "State"),
                InGroup(DeclareRuntimeState<&A::reached>("reached", "Reached", kPropertyTypeBool),
                           "State"),
                InGroup(DeclareRuntimeState<&A::endToTargetDistanceMetres>("endToTargetDistanceMetres",
                                                                                 "Distance To Target",
                                                                                 kPropertyTypeFloat),
                           "State"),
                InGroup(DeclareRuntimeState<&A::resolvedRootBoneIndex>("resolvedRootBoneIndex",
                                                                             "Resolved Root",
                                                                             kPropertyTypeUInt32),
                           "State"),
                InGroup(DeclareRuntimeState<&A::resolvedMiddleBoneIndex>("resolvedMiddleBoneIndex",
                                                                               "Resolved Middle",
                                                                               kPropertyTypeUInt32),
                           "State"),
                InGroup(DeclareRuntimeState<&A::resolvedEndBoneIndex>("resolvedEndBoneIndex",
                                                                            "Resolved End",
                                                                            kPropertyTypeUInt32),
                           "State"),
            }));
}

/// FullBodyIK3D: the whole skeleton solved at once so several limbs share one answer. The root
/// bone pair is the same index-or-name reference the chain carries, the effectors are a list of
/// bone/target/weight pulls drawn by their own drawer, and the solver's budget is authored as
/// three numbers rather than discovered by a profiler. The answers the solve writes back each
/// pass - did it run, did it converge, what the budget cost - are runtime state, so an author can
/// see what a solve did without a debugger.
void DeclareFullBodyIK(std::vector<TypeMetadataEntry>& entries) {
    using A = FullBodyIK3DComponent;
    const auto entityReference = [](TypeMetadataProperty property) {
        property.flags = TypeMetadataPropertyFlags::EntityReference;
        return property;
    };
    AddValidated<FullBodyIK3DComponent, &IsFullBodyIK3DObjectComponentValid>(
        entries,
        MakeEntry("component.full_body_ik_3d", "FullBodyIK3DComponent", "FullBodyIK3D", kSectionOrderTypeSpecific,
            {
                entityReference(WithTooltip(Declare<&A::skeleton>("skeleton", "Skeleton",
                                                                       kPropertyTypeEntity),
                                               "The object whose skeleton this solve poses. Empty leaves "
                                               "the pose exactly as the animation wrote it.")),
                WithTooltip(Declare<&A::enabled>("enabled", "Enabled", kPropertyTypeBool),
                               "Off stops solving without removing the object."),
                WithCustomDrawer(
                    WithTooltip(Declare<&A::effectors>("effectors", "Effectors", "FullBodyIKEffectors"),
                                   "Where each limb should end up, and how hard it pulls. Sweeps run in "
                                   "list order, so an earlier effector is answered first."),
                    "full-body-effectors"),
                InGroup(WithTooltip(Declare<&A::rootBoneName>("rootBoneName", "Root Bone",
                                                                      kPropertyTypeString),
                                          "The joint the solve pivots around and may translate - the "
                                          "hips. Every effector must sit in this bone's subtree."),
                           "Root"),
                InGroup(WithTooltip(Declare<&A::rootBoneIndex>("rootBoneIndex", "Root Index",
                                                                        kPropertyTypeUInt32),
                                          "The root bone's index in the skeleton. 4294967295 means \"ask "
                                          "the name\"; any other value wins over it when it names a real bone."),
                           "Root"),
                InGroup(WithTooltip(WithRange(Declare<&A::maxIterations>("maxIterations",
                                                                                     "Max Iterations",
                                                                                     kPropertyTypeUInt32),
                                                       1.0, 64.0, 1.0),
                                          "How many full sweeps over every effector the solve may run. "
                                          "The cap is what makes the frame's cost bounded."),
                           "Solver"),
                InGroup(WithTooltip(WithRange(Declare<&A::toleranceMetres>("toleranceMetres",
                                                                                       "Tolerance (m)",
                                                                                       kPropertyTypeFloat),
                                                       0.000001, 1.0, 0.0001),
                                          "How close an effector has to land before it counts as placed."),
                           "Solver"),
                InGroup(WithTooltip(WithRange(Declare<&A::rootTranslationWeight>(
                                                           "rootTranslationWeight", "Root Translation",
                                                           kPropertyTypeFloat),
                                                       0.0, 1.0, 0.05),
                                          "How much of the average pull each iteration may spend moving "
                                          "the root instead of bending joints. 0 pins the character in "
                                          "place; 1 lets it step toward its targets."),
                           "Solver"),
                InGroup(WithTooltip(WithRange(Declare<&A::maxJointStepRadians>(
                                                           "maxJointStepRadians", "Max Joint Step (rad)",
                                                           kPropertyTypeFloat),
                                                       0.017453292519943295, 3.14159265358979323846, 0.017453292519943295),
                                          "The most any one joint may rotate in a single sweep. A hard "
                                          "pull arrives over a few frames instead of snapping through "
                                          "a fold in one."),
                           "Solver"),
                InGroup(DeclareRuntimeState<&A::solved>("solved", "Solved", kPropertyTypeBool), "State"),
                InGroup(DeclareRuntimeState<&A::converged>("converged", "Converged", kPropertyTypeBool),
                           "State"),
                InGroup(DeclareRuntimeState<&A::iterationsUsed>("iterationsUsed", "Iterations Used",
                                                                      kPropertyTypeUInt32),
                           "State"),
            }));
}

/// Ragdoll3D: articulated passive, active-motor, and partial-subtree ragdoll driving a Skeleton3D.
void DeclareRagdoll3D(std::vector<TypeMetadataEntry>& entries) {
    using R = Ragdoll3DComponent;
    const auto entityReference = [](TypeMetadataProperty property) {
        property.flags = TypeMetadataPropertyFlags::EntityReference;
        return property;
    };
    AddValidated<Ragdoll3DComponent, &IsRagdoll3DComponentAuthoredValid>(
        entries,
        MakeEntry("component.ragdoll_3d", "Ragdoll3DComponent", "Ragdoll3D", kSectionOrderTypeSpecific,
            {
                entityReference(WithTooltip(Declare<&R::skeleton>("skeleton", "Skeleton",
                                                                        kPropertyTypeEntity),
                                               "Skeleton3D posed by this ragdoll.")),
                WithTooltip(Declare<&R::enabled>("enabled", "Enabled", kPropertyTypeBool),
                               "Enable or pause ragdoll simulation."),
                InGroup(WithTooltip(DeclareEnum<&R::mode>(
                                              "mode", "Mode",
                                              {
                                                  {static_cast<std::int64_t>(RagdollMode::Kinematic), "Kinematic"},
                                                  {static_cast<std::int64_t>(RagdollMode::Active), "Active"},
                                                  {static_cast<std::int64_t>(RagdollMode::Simulating), "Simulating"},
                                                  {static_cast<std::int64_t>(RagdollMode::BlendingOut), "BlendingOut"},
                                              }),
                                          "Kinematic tracks animation; Active uses PD motors; Simulating is full ragdoll."),
                           "Simulation"),
                InGroup(WithTooltip(DeclareEnum<&R::qualityPreset>(
                                              "qualityPreset", "Quality Preset",
                                              {
                                                  {static_cast<std::int64_t>(RagdollQualityPreset::High), "High"},
                                                  {static_cast<std::int64_t>(RagdollQualityPreset::Balanced), "Balanced"},
                                                  {static_cast<std::int64_t>(RagdollQualityPreset::Fast), "Fast"},
                                                  {static_cast<std::int64_t>(RagdollQualityPreset::Custom), "Custom"},
                                              }),
                                          "Balances solver substeps and self-collision for full quality vs low-end hardware."),
                           "Simulation"),
                InGroup(WithTooltip(WithRange(Declare<&R::physicsBlendWeight>(
                                                           "physicsBlendWeight", "Physics Blend",
                                                           kPropertyTypeFloat),
                                                       0.0, 1.0, 0.05),
                                          "Blend between animated pose (0) and physics ragdoll (1)."),
                           "Simulation"),
                InGroup(WithTooltip(Declare<&R::gravity>("gravity", "Gravity", kPropertyTypeVector3),
                                          "World gravity acceleration applied to dynamic limbs."),
                           "Simulation"),
                InGroup(WithTooltip(Declare<&R::initialVelocity>("initialVelocity", "Initial Velocity",
                                                                          kPropertyTypeVector3),
                                          "Extra linear velocity added when simulation starts."),
                           "Simulation"),
                InGroup(WithTooltip(Declare<&R::partialRootBoneName>("partialRootBoneName", "Partial Root Bone",
                                                                              kPropertyTypeString),
                                          "Empty simulates full body; naming a bone simulates only its subtree."),
                           "Simulation"),
                InGroup(WithTooltip(Declare<&R::enableSelfCollision>("enableSelfCollision", "Self-Collision",
                                                                              kPropertyTypeBool),
                                          "Prevent non-adjacent limbs from clipping through the torso."),
                           "Simulation"),
                InGroup(WithTooltip(Declare<&R::enableDynamicPushback>("enableDynamicPushback", "Dynamic Pushback",
                                                                                kPropertyTypeBool),
                                          "Transfer ragdoll contact impulses back to dynamic Rigid3D props."),
                           "Simulation"),
                InGroup(WithTooltip(Declare<&R::driveEntityTransform>("driveEntityTransform", "Drive Entity Transform",
                                                                               kPropertyTypeBool),
                                          "Move the skeleton entity transform to follow the fallen pelvis."),
                           "Simulation"),
                InGroup(WithTooltip(Declare<&R::inheritAnimatedVelocity>("inheritAnimatedVelocity", "Inherit Velocity",
                                                                                  kPropertyTypeBool),
                                          "Carry animated limb velocities into ragdoll upon activation."),
                           "Simulation"),
                InGroup(WithTooltip(WithRange(Declare<&R::motorStiffness>("motorStiffness", "Motor Stiffness",
                                                                                      kPropertyTypeFloat),
                                                       0.0, 2000.0, 10.0),
                                          "Joint PD spring stiffness in Active Ragdoll mode."),
                           "Active Motors"),
                InGroup(WithTooltip(WithRange(Declare<&R::motorDamping>("motorDamping", "Motor Damping",
                                                                                    kPropertyTypeFloat),
                                                       0.0, 200.0, 2.0),
                                          "Joint PD angular damping in Active Ragdoll mode."),
                           "Active Motors"),
                InGroup(WithTooltip(WithRange(Declare<&R::maxMotorTorque>("maxMotorTorque", "Max Motor Torque",
                                                                                      kPropertyTypeFloat),
                                                       0.0, 2000.0, 10.0),
                                          "Torque cap (N.m) allowing heavy impacts to overpower motors."),
                           "Active Motors"),
                InGroup(WithTooltip(WithRange(Declare<&R::rootDriveLinearStiffness>(
                                                           "rootDriveLinearStiffness", "Root Linear Stiffness",
                                                           kPropertyTypeFloat),
                                                       0.0, 2000.0, 10.0),
                                          "Optional world-space linear spring holding pelvis in Active mode."),
                           "Root Drive"),
                InGroup(WithTooltip(WithRange(Declare<&R::rootDriveLinearDamping>(
                                                           "rootDriveLinearDamping", "Root Linear Damping",
                                                           kPropertyTypeFloat),
                                                       0.0, 200.0, 2.0),
                                          "Damping for Active mode world-space root linear drive."),
                           "Root Drive"),
                InGroup(WithTooltip(WithRange(Declare<&R::rootDriveMaxLinearForce>(
                                                           "rootDriveMaxLinearForce", "Root Max Linear Force",
                                                           kPropertyTypeFloat),
                                                       0.0, 5000.0, 25.0),
                                          "Maximum force (N) for Active mode root linear drive."),
                           "Root Drive"),
                InGroup(WithTooltip(WithRange(Declare<&R::rootDriveAngularStiffness>(
                                                           "rootDriveAngularStiffness", "Root Angular Stiffness",
                                                           kPropertyTypeFloat),
                                                       0.0, 2000.0, 10.0),
                                          "World-space upright balance stiffness for pelvis in Active mode."),
                           "Root Drive"),
                InGroup(WithTooltip(WithRange(Declare<&R::rootDriveAngularDamping>(
                                                           "rootDriveAngularDamping", "Root Angular Damping",
                                                           kPropertyTypeFloat),
                                                       0.0, 200.0, 2.0),
                                          "Angular damping for Active mode root upright drive."),
                           "Root Drive"),
                InGroup(WithTooltip(WithRange(Declare<&R::rootDriveMaxAngularTorque>(
                                                           "rootDriveMaxAngularTorque", "Root Max Angular Torque",
                                                           kPropertyTypeFloat),
                                                       0.0, 2000.0, 10.0),
                                          "Maximum torque (N.m) for Active mode root upright drive."),
                           "Root Drive"),
                InGroup(WithTooltip(WithRange(Declare<&R::maxSubstepsPerFrame>(
                                                           "maxSubstepsPerFrame", "Max Substeps",
                                                           kPropertyTypeUInt32),
                                                       1.0, static_cast<double>(Physics::kMaximumRagdollSubsteps), 1.0),
                                          "Maximum fixed physics substeps per frame."),
                           "Solver"),
                InGroup(WithTooltip(Declare<&R::autoRecover>("autoRecover", "Auto Recover", kPropertyTypeBool),
                                          "When the body comes to rest it stands back up on its own."),
                           "Recovery"),
                InGroup(WithTooltip(WithRange(Declare<&R::recoverDelaySeconds>("recoverDelaySeconds", "Recover Delay",
                                                                                      kPropertyTypeFloat),
                                                       0.0, 30.0, 0.1),
                                          "Seconds the body must rest before it stands back up."),
                           "Recovery"),
                InGroup(WithTooltip(WithRange(Declare<&R::recoverDurationSeconds>("recoverDurationSeconds",
                                                                                         "Recover Duration",
                                                                                         kPropertyTypeFloat),
                                                       0.1, 10.0, 0.1),
                                          "Seconds the stand-up blend takes."),
                           "Recovery"),
                InGroup(WithTooltip(Declare<&R::automaticReactions>("automaticReactions", "Automatic Reactions",
                                                                           kPropertyTypeBool),
                                          "A light hit staggers the body, a strong hit or a hard landing knocks it down."),
                           "Automatic Reactions"),
                InGroup(WithTooltip(WithRange(Declare<&R::staggerImpulseThreshold>(
                                                           "staggerImpulseThreshold", "Stagger Impulse",
                                                           kPropertyTypeFloat),
                                                       0.0, 5000.0, 5.0),
                                          "Hit impulse (N.s) from which the body staggers."),
                           "Automatic Reactions"),
                InGroup(WithTooltip(WithRange(Declare<&R::fallImpulseThreshold>(
                                                           "fallImpulseThreshold", "Fall Impulse",
                                                           kPropertyTypeFloat),
                                                       0.0, 10000.0, 10.0),
                                          "Hit impulse (N.s) from which the body is knocked down."),
                           "Automatic Reactions"),
                InGroup(WithTooltip(WithRange(Declare<&R::fallLandingSpeed>(
                                                           "fallLandingSpeed", "Fall Landing Speed",
                                                           kPropertyTypeFloat),
                                                       0.0, 100.0, 0.5),
                                          "Downward speed (m/s) at landing from which the body falls."),
                           "Automatic Reactions"),
                InGroup(WithTooltip(WithRange(Declare<&R::staggerBlendPeak>(
                                                           "staggerBlendPeak", "Stagger Peak", kPropertyTypeFloat),
                                                       0.0, 1.0, 0.05),
                                          "Most the physics takes over during a stagger."),
                           "Automatic Reactions"),
                InGroup(WithTooltip(WithRange(Declare<&R::staggerRiseSeconds>(
                                                           "staggerRiseSeconds", "Stagger Rise", kPropertyTypeFloat),
                                                       0.0, 5.0, 0.01),
                                          "Seconds the stagger takes to reach its peak."),
                           "Automatic Reactions"),
                InGroup(WithTooltip(WithRange(Declare<&R::staggerDecaySeconds>(
                                                           "staggerDecaySeconds", "Stagger Decay", kPropertyTypeFloat),
                                                       0.05, 10.0, 0.05),
                                          "Seconds the stagger takes to fade back to the animation."),
                           "Automatic Reactions"),
                InGroup(WithTooltip(WithRange(Declare<&R::staggerFallTiltDegrees>(
                                                           "staggerFallTiltDegrees", "Stagger Fall Tilt",
                                                           kPropertyTypeFloat),
                                                       1.0, 180.0, 1.0),
                                          "A staggering body pushed further than this from its pose falls."),
                           "Automatic Reactions"),
                InGroup(WithTooltip(Declare<&R::getUpFaceUpTrigger>("getUpFaceUpTrigger", "Get Up Face Up Trigger",
                                                                           kPropertyTypeString),
                                          "Animation Graph trigger fired when the body stands up from lying on its back."),
                           "Automatic Reactions"),
                InGroup(WithTooltip(Declare<&R::getUpFaceDownTrigger>("getUpFaceDownTrigger",
                                                                             "Get Up Face Down Trigger",
                                                                             kPropertyTypeString),
                                          "Animation Graph trigger fired when the body stands up from lying on its front."),
                           "Automatic Reactions"),
                InGroup(DeclareRuntimeState<&R::initialized>("initialized", "Initialized", kPropertyTypeBool),
                           "State"),
                InGroup(DeclareRuntimeState<&R::editorPreviewSimulating>("editorPreviewSimulating", "Preview Active",
                                                                               kPropertyTypeBool),
                           "State"),
            }));
}

/// Concrete RenderInstance3D children. Each brings exactly its own section; everything above it
/// comes from the bases.
void DeclareRenderInstanceObjects(std::vector<TypeMetadataEntry>& entries) {
    using R = ReflectionProbe3DComponent;
    AddValidated<R, &IsReflectionProbe3DObjectComponentValid>(
        entries,
        MakeEntry("component.reflection_probe_3d", "ReflectionProbe3DComponent", "ReflectionProbe3D", kSectionOrderTypeSpecific,
            {
                WithTooltip(Declare<&R::enabled>("enabled", "Enabled", kPropertyTypeBool),
                               "Off, the probe contributes no local reflection and captures are skipped."),
                WithTooltip(WithRange(Declare<&R::size>("size", "Influence Size", kPropertyTypeVector3),
                                            0.001, 100000.0, 0.01),
                               "Full box extents in metres. Reflection influence fades to zero at each face."),
                WithTooltip(
                    DeclareEnum<&R::resolution>(
                        "resolution", "Capture Resolution",
                        {{0, "64 (Low)"}, {1, "128 (Medium)"}, {2, "256 (High)"}, {3, "512 (Ultra)"}}),
                    "Texels per cubemap face. Higher resolution sharpens the captured reflection at the cost of "
                    "six larger textures; changing it reallocates the probe and triggers a fresh capture."),
                DeclareEnum<&R::updateMode>("updateMode", "Update Mode",
                                               {{0, "Once"}, {1, "Every Frame"}, {2, "On Demand"}}),
            }));

    AddValidated<DirectionalLight3DComponent, &IsDirectionalLight3DComponentValid>(
        entries,
        MakeEntry("component.directional_light_3d", "DirectionalLight3DComponent", "DirectionalLight3D", kSectionOrderTypeSpecific,
            {
                InGroup(WithTooltip(WithRange(Declare<&DirectionalLight3DComponent::shadowMaxDistance>(
                                                           "shadowMaxDistance", "Max Distance", kPropertyTypeFloat),
                                                       0.0, 10000.0, 1.0),
                                          "How far from the camera shadows are drawn, in metres. 0 follows the "
                                          "camera's far plane. Shorter is sharper."),
                           "Shadow"),
                InGroup(WithTooltip(WithRange(Declare<&DirectionalLight3DComponent::shadowSplitBlend>(
                                                           "shadowSplitBlend", "Split Blend", kPropertyTypeFloat),
                                                       -1.0, 1.0, 0.01),
                                          "How the shadow cascades share that distance: 0 evenly, 1 packed near the "
                                          "camera (sharper up close); a negative value inherits the project's default."),
                           "Shadow"),
                InGroup(WithTooltip(WithRange(Declare<&DirectionalLight3DComponent::shadowDistanceFadeRange>(
                                                           "shadowDistanceFadeRange", "Distance Fade Range",
                                                           kPropertyTypeFloat),
                                                       0.0, 10000.0, 1.0),
                                          "Fade shadows smoothly to fully lit over this many metres at the end of "
                                          "the final cascade. 0 disables the fade."),
                           "Shadow"),

                // The disc is this light's visible body in the sky, so its rows belong here rather
                // than on the Sky object: two directional lights are two bodies, each of which
                // sits where its own light points and is as bright as its own emitter says.
                InGroup(WithTooltip(WithRange(Declare<&DirectionalLight3DComponent::discAngularDiameterDegrees>(
                                                           "discAngularDiameterDegrees", "Angular Diameter",
                                                           kPropertyTypeFloat),
                                                       0.0, 90.0, 0.05),
                                          "How wide the disc appears, in degrees. 0 hides it. 3.8 is what the "
                                          "viewport's sun has always looked like; the real sun is 0.53, which "
                                          "is accurate and small."),
                           "Sun Disc"),
                InGroup(WithTooltip(WithRange(Declare<&DirectionalLight3DComponent::discIntensity>(
                                                           "discIntensity", "Disc Intensity", kPropertyTypeFloat),
                                                       0.0, 64.0, 0.05),
                                          "Peak brightness of the disc, on top of the colour and energy the light "
                                          "already gives the scene. Bright enough and it blooms."),
                           "Sun Disc"),
                InGroup(WithTooltip(WithRange(Declare<&DirectionalLight3DComponent::discLimbDarkening>(
                                                           "discLimbDarkening", "Limb Darkening",
                                                           kPropertyTypeFloat),
                                                       0.0, 1.0, 0.01),
                                          "How much dimmer the disc's rim is than its centre. 0 is a flat disc; the "
                                          "real sun is about 0.6, a moon about 0.85."),
                           "Sun Disc"),
            }));

    using D = Decal3DComponent;
    AddValidated<Decal3DComponent, &IsDecal3DObjectComponentValid>(
        entries,
        MakeEntry("component.decal_3d", "Decal3DComponent", "Decal3D", kSectionOrderTypeSpecific,
            {
                WithTooltip(Declare<&D::enabled>("enabled", "Enabled", kPropertyTypeBool),
                               "Off stops projecting without removing the object."),
                WithTooltip(WithCustomDrawer(Declare<&D::materialAssetPath>("materialAssetPath", "Material",
                                                                                       kPropertyTypeString),
                                                     "file:uvmat"),
                               "The decal material to project."),
                WithTooltip(WithRange(Declare<&D::size>("size", "Size", kPropertyTypeVector3), 0.001,
                                            100000.0, 0.01),
                               "The projection volume, centred on the object; it projects along -Y."),
                DeclareEnum<&D::projection>("projection", "Projection", {{0, "Box"}, {1, "Cylinder"}}),
                WithTooltip(WithRange(Declare<&D::lifetime>("lifetime", "Lifetime", kPropertyTypeFloat),
                                            0.0, 100000.0, 0.1),
                               "Seconds until the decal removes itself. 0 keeps it forever."),
                WithCustomDrawer(WithTooltip(Declare<&D::cullMask>("cullMask", "Projects On",
                                                                            kPropertyTypeBitMask32),
                                                   "The render layers it projects onto."),
                                    std::string(kLayerMaskDrawerRender)),
                InGroup(WithTooltip(Declare<&D::modulate>("modulate", "Modulate", kPropertyTypeColor),
                                          "Tints the projected colour."),
                           "Parameters"),
                InGroup(WithRange(Declare<&D::emissionEnergy>("emissionEnergy", "Emission",
                                                                       kPropertyTypeFloat),
                                        0.0, 128.0, 0.01),
                           "Parameters"),
                InGroup(WithTooltip(WithRange(Declare<&D::albedoMix>("albedoMix", "Albedo Mix",
                                                                                 kPropertyTypeFloat),
                                                       0.0, 1.0, 0.01),
                                          "How much of the surface colour it replaces. 0 keeps the paint, for dents."),
                           "Parameters"),
                InGroup(WithTooltip(WithRange(Declare<&D::normalFade>("normalFade", "Normal Fade",
                                                                                  kPropertyTypeFloat),
                                                       0.0, 1.0, 0.01),
                                          "Fades it on surfaces turned away from the projection."),
                           "Parameters"),
                InGroup(WithRange(Declare<&D::upperFade>("upperFade", "Upper", kPropertyTypeFloat), 0.0,
                                        1.0, 0.01),
                           "Vertical Fade"),
                InGroup(WithRange(Declare<&D::lowerFade>("lowerFade", "Lower", kPropertyTypeFloat), 0.0,
                                        1.0, 0.01),
                           "Vertical Fade"),
                InGroup(Declare<&D::distanceFadeEnabled>("distanceFadeEnabled", "Enabled", kPropertyTypeBool),
                           "Distance Fade"),
                InGroup(WhenOn<&D::distanceFadeEnabled>(WithRange(
                               Declare<&D::distanceFadeBegin>("distanceFadeBegin", "Begin", kPropertyTypeFloat),
                               0.0, 1000000.0, 0.1)),
                           "Distance Fade"),
                InGroup(WhenOn<&D::distanceFadeEnabled>(WithRange(
                               Declare<&D::distanceFadeLength>("distanceFadeLength", "Length",
                                                                  kPropertyTypeFloat),
                               0.0, 1000000.0, 0.1)),
                           "Distance Fade"),
                // The runtime half: how much of the lifetime is left, and whether it has run out.
                // Shown during Play so an author can watch a decal age, never written by authoring
                // and never saved - the same treatment Projectile3D's countdown gets, because it is
                // the same kind of fact.
                InGroup(DeclareRuntimeState<&D::remainingLifetime>("remainingLifetime", "Remaining",
                                                                         kPropertyTypeFloat),
                           "Result"),
                InGroup(DeclareRuntimeState<&D::expired>("expired", "Expired", kPropertyTypeBool),
                           "Result"),
            }));

    using F = FogVolume3DComponent;
    AddValidated<FogVolume3DComponent, &IsFogVolume3DObjectComponentValid>(
        entries,
        MakeEntry("component.fog_volume_3d", "FogVolume3DComponent", "FogVolume3D", kSectionOrderTypeSpecific,
            {
                WithTooltip(DeclareEnum<&F::shape>(
                                   "shape", "Shape",
                                   {{0, "Ellipsoid"}, {1, "Cone"}, {2, "Cylinder"}, {3, "Box"}, {4, "World"}}),
                               "The volume's shape. World fills the whole scene and ignores Size."),
                [] {
                    TypeMetadataProperty property = WithRange(
                        Declare<&F::size>("size", "Size", kPropertyTypeVector3), 0.001, 100000.0, 0.01);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const F*>(instance)->shape != FogVolumeShape::World;
                    };
                    return property;
                }(),
                WithTooltip(WithCustomDrawer(Declare<&F::materialAssetPath>("materialAssetPath", "Material",
                                                                                       kPropertyTypeString),
                                                     "file:uvmat"),
                               "Reserved for a custom fog material. Until one is assigned, Density, Albedo and Emission drive the volume."),
                InGroup(WithTooltip(WithRange(Declare<&F::density>("density", "Density",
                                                                               kPropertyTypeFloat),
                                                       -1024.0, 1024.0, 0.01),
                                          "How thick the fog is. Negative clears fog from inside the volume."),
                           "Fog"),
                InGroup(Declare<&F::albedo>("albedo", "Albedo", kPropertyTypeColor), "Fog"),
                InGroup(WithTooltip(Declare<&F::emission>("emission", "Emission", kPropertyTypeColor),
                                          "Light the fog gives off by itself, with no light shining on it."),
                           "Fog"),
                InGroup(WithTooltip(WithRange(Declare<&F::heightFalloff>(
                                                           "heightFalloff", "Height Falloff", kPropertyTypeFloat),
                                                       0.0, 1024.0, 0.01),
                                          "Thins the fog with height inside the volume. 0 keeps it even."),
                           "Fog"),
                InGroup(WithTooltip(WithRange(Declare<&F::edgeFade>("edgeFade", "Edge Fade",
                                                                                kPropertyTypeFloat),
                                                       0.0, 1.0, 0.01),
                                          "Softens the volume's boundary. 0 is a hard edge."),
                           "Fog"),
            }));

    using P = WorldPartition3DComponent;
    AddValidated<WorldPartition3DComponent, &IsWorldPartition3DObjectComponentValid>(
        entries,
        MakeEntry("component.world_partition_3d", "WorldPartition3DComponent", "WorldPartition3D", kSectionOrderTypeSpecific,
            {
                WithTooltip(Declare<&P::enabled>("enabled", "Enabled", kPropertyTypeBool),
                               "Off, every member draws. On, only the nearest occupied cells up to the "
                               "budget draw. This does not load files."),
                WithTooltip(
                    WithRange(Declare<&P::cellSize>("cellSize", "Cell Size", kPropertyTypeFloat),
                                 0.001, 100000.0, 0.01),
                    "Metres along one cell edge. The volume starts at this object's position and "
                    "covers Cell Size times the saved cell counts along X, Y and Z."),
                WithTooltip(
                    WithRange(Declare<&P::maximumLoadedCells>("maximumLoadedCells", "Loaded Cells",
                                                                    kPropertyTypeUInt32),
                                 1.0, static_cast<double>(kMaximumStreamedCells), 1.0),
                    "How many occupied cells stay drawn. Farther cells skip their draws."),
                InGroup(DeclareRuntimeState<&P::loadedCellCount>("loadedCellCount", "Live Cells",
                                                                       kPropertyTypeUInt32),
                           "Result"),
            }));

    using V = VisibilityRegion3DComponent;
    AddValidated<VisibilityRegion3DComponent, &IsVisibilityRegion3DObjectComponentValid>(
        entries,
        MakeEntry("component.visibility_region_3d", "VisibilityRegion3DComponent", "VisibilityRegion3D", kSectionOrderTypeSpecific,
            {
                WithTooltip(Declare<&V::enabled>("enabled", "Enabled", kPropertyTypeBool),
                               "Off, every member draws. On, interior drawables skip while no viewer "
                               "stands inside the box."),
                WithTooltip(
                    WithRange(Declare<&V::halfExtents>("halfExtents", "Half Extents",
                                                             kPropertyTypeVector3),
                                 0.001, 100000.0, 0.01),
                    "The room box, centred on this object and aligned to the world axes."),
                WithCustomDrawer(
                    WithTooltip(Declare<&V::visibilityLayers>("visibilityLayers", "Visibility Layers",
                                                                    kPropertyTypeBitMask32),
                                   "Which mesh layers this room manages. A zero mask manages nothing. "
                                   "Primitives, decals, particles and fog use layer 0."),
                    std::string(kLayerMaskDrawerRender)),
                InGroup(DeclareRuntimeState<&V::active>("active", "Active", kPropertyTypeBool),
                           "Result"),
            }));

    using O = Occluder3DComponent;
    AddValidated<Occluder3DComponent, &IsOccluder3DObjectComponentValid>(
        entries,
        MakeEntry("component.occluder_3d", "Occluder3DComponent", "Occluder3D", kSectionOrderTypeSpecific,
            {
                WithTooltip(Declare<&O::enabled>("enabled", "Enabled", kPropertyTypeBool),
                               "Off, the box covers nothing. On, drawables whose whole bounds sit "
                               "behind it skip."),
                WithTooltip(
                    WithRange(Declare<&O::halfExtents>("halfExtents", "Half Extents",
                                                             kPropertyTypeVector3),
                                 0.001, 100000.0, 0.01),
                    "The cover box, centred on this object and aligned to the world axes."),
                WithTooltip(DeclareEnum<&O::mode>("mode", "Mode", {{0, "Conservative Box"}}),
                               "Hides a drawable only when every corner of its bounds is behind the box."),
            }));
}

void DeclareObjectCommon(std::vector<TypeMetadataEntry>& entries) {
    // Each of these is declared here and nowhere else, and each appears in the Inspector - with
    // its dropdown, its resolved answer and its place in the section - without a line of Inspector
    // code being written for it. That is the whole point of the declaration being the single
    // source of property truth.
    constexpr std::int32_t kProcessOrder = kSectionOrderObjectCommon;
    constexpr std::int32_t kThreadGroupOrder = kSectionOrderObjectCommon + 10;
    constexpr std::int32_t kPhysicsInterpolationOrder = kSectionOrderObjectCommon + 20;
    constexpr std::int32_t kAutoTranslateOrder = kSectionOrderObjectCommon + 30;
    constexpr std::int32_t kEditorDescriptionOrder = kSectionOrderObjectCommon + 40;
    constexpr std::int32_t kScriptOrder = kSectionOrderObjectCommon + 50;
    constexpr std::int32_t kMetadataOrder = kSectionOrderObjectCommon + 60;

    AddValidated<ProcessComponent, &IsProcessComponentValid>(
        entries,
        MakeEntry("component.process", "ProcessComponent", "Process", kProcessOrder,
            {
                WithTooltip(
                    ResolvedBy(DeclareEnum<&ProcessComponent::mode>("mode", "Mode",
                                                                             {{0, "Inherit"},
                                                                              {1, "Running"},
                                                                              {2, "Paused Only"},
                                                                              {3, "Always"},
                                                                              {4, "Never"}}),
                                  "resolvedModeInHierarchy"),
                    "Whether this entity's work runs while paused. Drives scripts and particle "
                    "emitters; controllers, projectiles and spring arms skip Never and Paused "
                    "Only. Inherit takes the parent's answer (Running at the top)."),
                WithTooltip(Declare<&ProcessComponent::priority>("priority", "Priority",
                                                                           kPropertyTypeInt32),
                               "Script tick order. Lower runs first; equal priorities keep entity "
                               "order. Not inherited."),
                WithTooltip(Declare<&ProcessComponent::physicsPriority>(
                                   "physicsPriority", "Physics Priority", kPropertyTypeInt32),
                               "Fixed-step order for character controllers, projectiles and spring "
                               "arms. Lower runs first. Not inherited."),
                DeclareRuntimeStateEnum<&ProcessComponent::resolvedModeInHierarchy>(
                    "resolvedModeInHierarchy", "Resolved Mode",
                    {{0, "Inherit"}, {1, "Running"}, {2, "Paused Only"}, {3, "Always"}, {4, "Never"}}),
            }));

    // A sub-group of Process: which thread the work runs on is a refinement of when it runs.
    TypeMetadataEntry threadGroup = MakeEntry("component.thread_group", "ThreadGroupComponent", "Thread Group", kThreadGroupOrder,
        {
            WithTooltip(
                ResolvedBy(DeclareEnum<&ThreadGroupComponent::mode>(
                                  "mode", "Group", {{0, "Inherit"}, {1, "Main Thread"}, {2, "Sub Thread"}}),
                              "resolvedModeInHierarchy"),
                "Which thread this entity's work may run on. Today this moves particle emitter "
                "simulation onto worker threads; scripts always stay on the main thread. A Main "
                "Thread ancestor is a constraint a child cannot override."),
            // Kept declared so code can still read it, but not offered: particle emitters are
            // simulated independently, so an order within a group has nothing to order yet.
            Hidden(Declare<&ThreadGroupComponent::order>("order", "Order", kPropertyTypeInt32)),
            DeclareRuntimeStateEnum<&ThreadGroupComponent::resolvedModeInHierarchy>(
                "resolvedModeInHierarchy", "Resolved Group",
                {{0, "Inherit"}, {1, "Main Thread"}, {2, "Sub Thread"}}),
        });
    threadGroup.nestedUnderTypeIds = {"component.process"};
    AddValidated<ThreadGroupComponent, &IsThreadGroupComponentValid>(entries, std::move(threadGroup));

    // Only `mode` is authored. Everything else on this component is the interpolation system's
    // working state: the resolved answer plus the two poses it blends between. Marking them
    // RuntimeState is what keeps a generic editor from writing a pose and a generic serializer
    // from persisting one - either would corrupt the next frame's interpolation.
    AddValidated<PhysicsInterpolationComponent, &IsPhysicsInterpolationComponentValid>(
        entries,
        MakeEntry("component.physics_interpolation", "PhysicsInterpolationComponent", "Physics Interpolation", kPhysicsInterpolationOrder,
                     {
                         ResolvedBy(DeclareEnum<&PhysicsInterpolationComponent::mode>(
                                           "mode", "Mode", {{0, "Inherit"}, {1, "Blended"}, {2, "Exact"}}),
                                       "interpolatedInHierarchy"),
                         DeclareRuntimeState<&PhysicsInterpolationComponent::interpolatedInHierarchy>(
                             "interpolatedInHierarchy", "Interpolated In Hierarchy",
                             kPropertyTypeBool),
                         DeclareRuntimeState<&PhysicsInterpolationComponent::hasPreviousPose>(
                             "hasPreviousPose", "Has Previous Pose", kPropertyTypeBool),
                     }));

    AddValidated<AutoTranslateComponent, &IsAutoTranslateComponentValid>(
        entries,
        MakeEntry("component.auto_translate", "AutoTranslateComponent", "Auto Translate", kAutoTranslateOrder,
            {
                WithTooltip(
                    ResolvedBy(DeclareEnum<&AutoTranslateComponent::mode>(
                                      "mode", "Mode", {{0, "Inherit"}, {1, "Localized"}, {2, "Literal"}}),
                                  "resolvedModeInHierarchy"),
                    "Whether this entity's UI Text is looked up in the active locale before it is "
                    "drawn. The authored text is its own key. Disable it for debug labels, "
                    "identifiers and player names; a label with no component follows its parent."),
                DeclareRuntimeStateEnum<&AutoTranslateComponent::resolvedModeInHierarchy>(
                    "resolvedModeInHierarchy", "Resolved Mode",
                    {{0, "Inherit"}, {1, "Localized"}, {2, "Literal"}}),
            }));

    // A note to the next person, so it gets a box that fits a paragraph rather than one line.
    TypeMetadataProperty description = WithCustomDrawer(
        Declare<&EditorDescriptionComponent::description>("description", "Description",
                                                                kPropertyTypeString),
        "multiline-text");
    description.flags = TypeMetadataPropertyFlags::EditorOnly;
    Add<EditorDescriptionComponent>(entries,
                                          MakeEntry("component.editor_description", "EditorDescriptionComponent", "Editor Description",
                                                       kEditorDescriptionOrder, {std::move(description)}));

    // The script slot: empty offers to create, pick or load a script; filled names it. The path
    // is still the stored truth - the drawer only decides how it is chosen.
    TypeMetadataEntry script =
        MakeEntry("component.script", "ScriptComponent", "Script", kScriptOrder,
                     {WithCustomDrawer(Declare<&ScriptComponent::scriptAssetPath>(
                                              "scriptAssetPath", "Scripting", kPropertyTypeString),
                                          "script-slot"),
                      // A `.uvs` script's `export` fields, drawn from the script itself; this
                      // object's values are stored here, so each edit is one undo step.
                      WithCustomDrawer(Declare<&ScriptComponent::exportValues>(
                                              "exportValues", "Exports", "ScriptExportValues"),
                                          "script-exports")});
    script.presentedInline = true;
    Add<ScriptComponent>(entries, std::move(script));

    // Typed key/value pairs. A list needs add, rename, retype and remove, which a single property
    // row cannot express, so the whole list is one custom-drawn property.
    TypeMetadataEntry metadata = MakeEntry("component.object_metadata", "ObjectMetadataComponent", "Metadata", kMetadataOrder,
        {WithCustomDrawer(Declare<&ObjectMetadataComponent::entries>("entries", "Metadata",
                                                                            "ObjectMetadataEntryList"),
                             "object-metadata")});
    metadata.presentedInline = true;
    Add<ObjectMetadataComponent>(entries, std::move(metadata));
}

[[nodiscard]] TypeMetadataRegistry BuildRegistry() {
    std::vector<TypeMetadataEntry> entries;
    DeclareIdentityAndTransform(entries);
    DeclareRendering(entries);
    DeclarePhysics(entries);
    DeclareRayCast(entries);
    DeclareProjectile(entries);
    DeclareCombat(entries);
    DeclareMediaAndUI(entries);
    DeclareGameplay(entries);
    DeclareNavigation(entries);
    DeclareObjectBases(entries);
    DeclareSky(entries);
    DeclareRenderInstanceObjects(entries);
    DeclareSkeleton(entries);
    DeclareBoneAttachment(entries);
    DeclareTwoBoneIK(entries);
    DeclareFullBodyIK(entries);
    DeclareRagdoll3D(entries);
    DeclareObjectCommon(entries);

    TypeMetadataRegistry registry;
    for (TypeMetadataEntry& entry : entries) {
        const Strings::StringId typeId = entry.typeId;
        const Core::TypeMetadataRegistrationResult result = registry.RegisterType(std::move(entry));
        if (!result.IsRegistered()) {
            // A rejected declaration is a mistake in this file, not a runtime condition: the entry
            // is a compile-time literal, so it either always registers or never does. Log loudly
            // and carry on - the affected component simply falls back to having no metadata.
            UVE_ERROR("SceneComponentMetadata: \"{}\" was rejected: {}", typeId.ToString(), result.message);
            // Loud in debug builds. A rejected declaration otherwise costs only a log line while its
            // whole component silently vanishes from the Inspector - which is exactly how an
            // over-long tooltip once removed the Process section without failing anything but a
            // drawer count.
            UVE_ASSERT(result.IsRegistered());
        }
    }
    return registry;
}

} // namespace

const Core::TypeMetadataRegistry& GetSceneComponentMetadataRegistry() {
    // Built on first use rather than at static-initialization time, so the order in which
    // translation units initialize can never decide whether the registry is populated.
    static const TypeMetadataRegistry registry = BuildRegistry();
    return registry;
}

const Core::TypeMetadataEntry* FindSceneComponentMetadata(const std::type_index typeIndex) noexcept {
    return GetSceneComponentMetadataRegistry().FindTypeByIndex(typeIndex);
}

} // namespace UVE::Scene
