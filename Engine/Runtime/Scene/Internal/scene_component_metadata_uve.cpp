// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/scene_component_metadata_uve.h"

#include <string>
#include <utility>
#include <vector>

#include "uve/component/area_component_uve.h"
#include "uve/component/animation_sequencer_component_uve.h"
#include "uve/component/animation_graph_component_uve.h"
#include "uve/component/auto_translate_component_uve.h"
#include "uve/component/audio_source_component_uve.h"
#include "uve/component/bone_modifier_component_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/canvas_component_uve.h"
#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/editor_description_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/light_emitter_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/object_metadata_component_uve.h"
#include "uve/component/particle_emitter_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/thread_group_component_uve.h"
#include "uve/component/physics_interpolation_component_uve.h"
#include "uve/component/physics_object_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/render_instance_component_uve.h"
#include "uve/component/solid_body_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/surface_instance_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/logging/assert_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/objects/3d/abstract_animation_objects_3d_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"
#include "uve/objects/3d/abstract_physics_objects_3d_uve.h"
#include "uve/objects/3d/bone_attachment_3d_uve.h"
#include "uve/objects/3d/decal_3d_uve.h"
#include "uve/objects/3d/directional_light_3d_uve.h"
#include "uve/objects/3d/fog_volume_3d_uve.h"
#include "uve/objects/3d/kinematic_3d_uve.h"
#include "uve/objects/3d/lod_group_3d_uve.h"
#include "uve/objects/3d/projectile_3d_uve.h"
#include "uve/objects/3d/hitbox_3d_uve.h"
#include "uve/objects/3d/hurtbox_3d_uve.h"
#include "uve/objects/3d/interaction_area_3d_uve.h"
#include "uve/objects/3d/ray_cast_3d_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"
#include "uve/objects/3d/nav_mesh_volume_3d_uve.h"
#include "uve/objects/3d/nav_seeker_3d_uve.h"
#include "uve/objects/3d/spring_arm_3d_uve.h"
#include "uve/objects/3d/spawn_point_3d_uve.h"
#include "uve/objects/3d/health_uve.h"
#include "uve/objects/3d/player_3d_uve.h"
#include "uve/objects/3d/two_bone_ik_3d_uve.h"
#include "uve/objects/3d/world_environment_3d_uve.h"
#include "uve/math/quaternion_uve.h"

namespace UVE::Scene {
namespace {

using Core::MakeEnumPropertyUVE;
using Core::MakePropertyUVE;
using Core::TypeMetadataEnumEntryUVE;
using Core::TypeMetadataEntryUVE;
using Core::TypeMetadataKindUVE;
using Core::TypeMetadataPropertyFlagsUVE;
using Core::TypeMetadataPropertyUVE;
using Core::TypeMetadataRegistryUVE;

/// Declares one editable property. `typeId` is one of the kPropertyType*UVE constants; the caller
/// adjusts flags/range on the returned value for the cases that need it.
template <auto MemberPointer>
[[nodiscard]] TypeMetadataPropertyUVE DeclareUVE(std::string name, std::string displayName,
                                                 const std::string_view typeId) {
    return MakePropertyUVE<MemberPointer>(std::move(name), std::move(displayName), std::string(typeId),
                                          true);
}

/// Declares a property a runtime system owns. It is shown so an author can see what the simulation
/// is doing, never written by authoring, and never persisted - saving it would restore a stale
/// cache over whatever the system computed on load.
template <auto MemberPointer>
[[nodiscard]] TypeMetadataPropertyUVE DeclareRuntimeStateUVE(std::string name, std::string displayName,
                                                             const std::string_view typeId) {
    TypeMetadataPropertyUVE property = DeclareUVE<MemberPointer>(std::move(name), std::move(displayName),
                                                                 typeId);
    property.flags = TypeMetadataPropertyFlagsUVE::RuntimeState;
    return property;
}

template <auto MemberPointer>
[[nodiscard]] TypeMetadataPropertyUVE DeclareEnumUVE(std::string name, std::string displayName,
                                                     std::vector<TypeMetadataEnumEntryUVE> options) {
    return MakeEnumPropertyUVE<MemberPointer>(std::move(name), std::move(displayName),
                                              std::string(kPropertyTypeEnumUVE), true,
                                              std::move(options));
}

/// A runtime-owned enum. Declared through the enum path rather than the plain one so its
/// accessors speak std::int64_t and a generic consumer can actually read it back - a read-only
/// property still has to be readable, or the inspector shows nothing where the resolved answer
/// should be.
template <auto MemberPointer>
[[nodiscard]] TypeMetadataPropertyUVE DeclareRuntimeStateEnumUVE(
    std::string name, std::string displayName, std::vector<TypeMetadataEnumEntryUVE> options) {
    TypeMetadataPropertyUVE property =
        DeclareEnumUVE<MemberPointer>(std::move(name), std::move(displayName), std::move(options));
    property.flags = TypeMetadataPropertyFlagsUVE::RuntimeState;
    return property;
}

/// Adds an inclusive numeric range with the given editing step.
[[nodiscard]] TypeMetadataPropertyUVE WithRangeUVE(TypeMetadataPropertyUVE property, const double minimum,
                                                   const double maximum, const double step) {
    property.range = {true, minimum, maximum, step};
    return property;
}

[[nodiscard]] TypeMetadataPropertyUVE WithTooltipUVE(TypeMetadataPropertyUVE property, std::string tooltip) {
    property.tooltip = std::move(tooltip);
    return property;
}

/// Routes a property through a named custom drawer. Used only where a generic editor would be
/// wrong rather than merely plain - see the rotation and entity-reference cases below.
[[nodiscard]] TypeMetadataPropertyUVE WithCustomDrawerUVE(TypeMetadataPropertyUVE property,
                                                          std::string drawerId) {
    property.customDrawerId = std::move(drawerId);
    return property;
}

/// States how many elements a fixed-capacity list value holds, for the type that has no other way
/// to say it (see kPropertyTypeEntityListUVE).
[[nodiscard]] TypeMetadataPropertyUVE WithElementCountUVE(TypeMetadataPropertyUVE property,
                                                          const std::size_t elementCount) {
    property.elementCount = elementCount;
    return property;
}

/// Places a property in a named sub-group of its section. A group's properties are declared
/// together, after the section's ungrouped ones.
[[nodiscard]] TypeMetadataPropertyUVE InGroupUVE(TypeMetadataPropertyUVE property, std::string group) {
    property.section = std::move(group);
    return property;
}

/// Shows a property only while a bool switch on the same component is on - the field after a
/// "Enabled" toggle means nothing while the toggle is off, so it is not shown then.
template <auto SwitchPointer>
[[nodiscard]] TypeMetadataPropertyUVE WhenOnUVE(TypeMetadataPropertyUVE property) {
    property.isVisible = +[](const void* instance) {
        using OwnerT = typename Core::Detail::MemberPointerTraitsUVE<decltype(SwitchPointer)>::Owner;
        return static_cast<const OwnerT*>(instance)->*SwitchPointer;
    };
    return property;
}

[[nodiscard]] TypeMetadataEntryUVE MakeEntryUVE(std::string typeId, std::string displayName,
                                                const std::int32_t order,
                                                std::vector<TypeMetadataPropertyUVE> properties) {
    TypeMetadataEntryUVE entry{TypeMetadataKindUVE::Component, std::move(typeId), std::move(displayName),
                               1U, std::move(properties), {}};
    entry.order = order;
    return entry;
}

/// Binds an entry to its C++ type and appends it to `entries`. Separated from MakeEntryUVE so the
/// native type appears once per component, right next to the properties that belong to it.
template <typename ComponentT>
void AddUVE(std::vector<TypeMetadataEntryUVE>& entries, TypeMetadataEntryUVE entry) {
    Core::BindTypeUVE<ComponentT>(entry);
    entries.push_back(std::move(entry));
}

/// AddUVE for a component with a whole-value rule, so a generic editor enforces it too.
template <typename ComponentT, bool (*IsValid)(const ComponentT&) noexcept>
void AddValidatedUVE(std::vector<TypeMetadataEntryUVE>& entries, TypeMetadataEntryUVE entry) {
    entry.isInstanceValid = +[](const void* instance) { return IsValid(*static_cast<const ComponentT*>(instance)); };
    AddUVE<ComponentT>(entries, std::move(entry));
}

// ---------------------------------------------------------------------------------------------
// The declarations themselves. Each component states what it exposes exactly once, here, instead
// of being re-described by every consumer that needs to know.
// ---------------------------------------------------------------------------------------------

void DeclareIdentityAndTransformUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    AddUVE<NameComponentUVE>(
        entries, MakeEntryUVE("component.name", "Name", kSectionOrderIdentityUVE,
                              {DeclareUVE<&NameComponentUVE::name>("name", "Name", kPropertyTypeStringUVE)}));

    // Rotation is the one place a generic editor would be actively wrong. The quaternion, the
    // authored Euler angles and the edit mode are three views of one piece of state that must be
    // written together (see TransformComponentUVE::localEulerRadians on why the angles are stored
    // rather than re-extracted). A custom drawer owns all three; the rest of the transform is
    // ordinary data and is declared ordinarily.
    AddUVE<TransformComponentUVE>(
        entries,
        MakeEntryUVE(
            "component.transform", "Transform", kSectionOrderTransformUVE,
            {
                DeclareUVE<&TransformComponentUVE::localPosition>("localPosition", "Position",
                                                                 kPropertyTypeVector3UVE),
                WithCustomDrawerUVE(DeclareUVE<&TransformComponentUVE::localRotation>(
                                        "localRotation", "Rotation", kPropertyTypeQuaternionUVE),
                                    "transform.rotation"),
                DeclareUVE<&TransformComponentUVE::localScale>("localScale", "Scale",
                                                               kPropertyTypeVector3UVE),
                WithTooltipUVE(
                    DeclareUVE<&TransformComponentUVE::topLevel>("topLevel", "Top Level",
                                                                 kPropertyTypeBoolUVE),
                    "Ignore the parent's transform. The entity stays a child for every other "
                    "purpose - outliner, deletion, saving and visibility inheritance."),
            }));

    // visibilityParent holds an entity reference, which a text field cannot author and which the
    // serializer has to remap through its file-local id table. Both facts are declared rather than
    // rediscovered: the flag tells the serializer, the custom drawer tells the inspector.
    TypeMetadataPropertyUVE visibilityParent = WithCustomDrawerUVE(
        DeclareUVE<&VisibilityComponentUVE::visibilityParent>("visibilityParent", "Visibility Parent",
                                                              kPropertyTypeEntityUVE),
        "visibility.parent");
    visibilityParent.flags = TypeMetadataPropertyFlagsUVE::EntityReference;
    visibilityParent.tooltip =
        "Inherit visibility from this entity instead of from the transform parent. Empty means the "
        "transform parent.";

    AddUVE<VisibilityComponentUVE>(
        entries,
        MakeEntryUVE("component.visibility", "Visibility", kSectionOrderVisibilityUVE,
                     {
                         DeclareUVE<&VisibilityComponentUVE::visible>("visible", "Visible",
                                                                      kPropertyTypeBoolUVE),
                         DeclareRuntimeStateUVE<&VisibilityComponentUVE::visibleInHierarchy>(
                             "visibleInHierarchy", "Visible In Hierarchy", kPropertyTypeBoolUVE),
                         std::move(visibilityParent),
                     }));

    TypeMetadataPropertyUVE parent = WithCustomDrawerUVE(
        DeclareUVE<&HierarchyComponentUVE::parent>("parent", "Parent", kPropertyTypeEntityUVE),
        "hierarchy.parent");
    parent.flags = TypeMetadataPropertyFlagsUVE::EntityReference;
    AddUVE<HierarchyComponentUVE>(entries, MakeEntryUVE("component.hierarchy", "Hierarchy",
                                                        kSectionOrderIdentityUVE, {std::move(parent)}));
}

void DeclareRenderingUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    AddUVE<CameraComponentUVE>(
        entries,
        MakeEntryUVE(
            "component.camera", "Camera", kSectionOrderTypeSpecificUVE,
            {
                DeclareEnumUVE<&CameraComponentUVE::projection>(
                    "projection", "Mode",
                    {{0, "Perspective"}, {1, "Orthographic"}, {2, "Human Eye"}}),
                DeclareUVE<&CameraComponentUVE::current>("current", "Current", kPropertyTypeBoolUVE),
                [] {
                    TypeMetadataPropertyUVE property = WithRangeUVE(
                        DeclareUVE<&CameraComponentUVE::fieldOfViewDegrees>(
                            "fieldOfViewDegrees", "Field Of View", kPropertyTypeFloatUVE),
                        1.0, 179.0, 0.5);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const CameraComponentUVE*>(instance)->projection ==
                               CameraProjectionModeUVE::Perspective;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataPropertyUVE property = WithRangeUVE(
                        DeclareUVE<&CameraComponentUVE::orthographicSize>("orthographicSize", "Size",
                                                                          kPropertyTypeFloatUVE),
                        0.001, 10000.0, 0.1);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const CameraComponentUVE*>(instance)->projection ==
                               CameraProjectionModeUVE::Orthographic;
                    };
                    return property;
                }(),
                WithRangeUVE(DeclareUVE<&CameraComponentUVE::nearPlane>("nearPlane", "Near Plane",
                                                                        kPropertyTypeFloatUVE),
                             0.001, 10000.0, 0.01),
                WithRangeUVE(DeclareUVE<&CameraComponentUVE::farPlane>("farPlane", "Far Plane",
                                                                       kPropertyTypeFloatUVE),
                             0.002, 100000.0, 1.0),
            }));

    AddUVE<LightComponentUVE>(
        entries,
        MakeEntryUVE(
            "component.light", "Light", kSectionOrderTypeSpecificUVE,
            {
                DeclareEnumUVE<&LightComponentUVE::type>("type", "Type",
                                                         {{0, "Directional"}, {1, "Point"}, {2, "Spot"}}),
                DeclareUVE<&LightComponentUVE::color>("color", "Color", kPropertyTypeColorUVE),
                WithRangeUVE(DeclareUVE<&LightComponentUVE::intensity>("intensity", "Intensity",
                                                                       kPropertyTypeFloatUVE),
                             0.0, 1000.0, 0.05),
                // Range and cone angle apply to some light types and not others. Declaring that
                // as a predicate keeps the inspector from having to know what a spot light is.
                [] {
                    TypeMetadataPropertyUVE property = WithRangeUVE(
                        DeclareUVE<&LightComponentUVE::range>("range", "Range", kPropertyTypeFloatUVE),
                        0.0, 10000.0, 0.1);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const LightComponentUVE*>(instance)->type !=
                               LightTypeUVE::Directional;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataPropertyUVE property =
                        WithRangeUVE(DeclareUVE<&LightComponentUVE::spotAngleDegrees>(
                                         "spotAngleDegrees", "Spot Angle", kPropertyTypeFloatUVE),
                                     0.0, 89.0, 0.5);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const LightComponentUVE*>(instance)->type == LightTypeUVE::Spot;
                    };
                    return property;
                }(),
            }));

    AddValidatedUVE<MeshComponentUVE, &IsMeshComponentValidUVE>(
        entries,
        MakeEntryUVE("component.mesh", "MeshInstance3D", kSectionOrderTypeSpecificUVE,
                     {
                         WithTooltipUVE(WithCustomDrawerUVE(DeclareUVE<&MeshComponentUVE::meshGuid>(
                                                                "meshGuid", "Mesh", kPropertyTypeAssetGuidUVE),
                                                            "asset:uvmodel"),
                                        "An imported model. Import a .glb or .gltf (Blender: File > Export > "
                                        "glTF 2.0) from the Content Browser."),
                         WithTooltipUVE(WithCustomDrawerUVE(DeclareUVE<&MeshComponentUVE::materialGuid>(
                                                                "materialGuid", "Material", kPropertyTypeAssetGuidUVE),
                                                            "asset:uvmat"),
                                        "The surface material. Without one the mesh is drawn in neutral grey."),
                         WithCustomDrawerUVE(DeclareUVE<&MeshComponentUVE::visibilityLayers>(
                                                 "visibilityLayers", "Visibility Layers", kPropertyTypeBitMask32UVE),
                                             std::string(kLayerMaskDrawerRenderUVE)),
                     }));

    // One component behind BoxMesh3D, SphereMesh3D and PlaneMesh3D; its section carries the name
    // of the object it is on.
    TypeMetadataEntryUVE primitive =
        MakeEntryUVE("component.primitive_mesh", "PrimitiveMesh3D", kSectionOrderTypeSpecificUVE,
                     {
                         DeclareEnumUVE<&PrimitiveMeshComponentUVE::kind>(
                             "kind", "Shape", {{0, "Cube"}, {1, "UV Sphere"}, {2, "Plane"}}),
                         DeclareUVE<&PrimitiveMeshComponentUVE::baseColor>("baseColor", "Base Color",
                                                                           kPropertyTypeColorUVE),
                     });
    primitive.sectionTitle = +[](const void* instance) -> const char* {
        switch (static_cast<const PrimitiveMeshComponentUVE*>(instance)->kind) {
            case PrimitiveMeshKindUVE::UVSphere:
                return "SphereMesh3D";
            case PrimitiveMeshKindUVE::Plane:
                return "PlaneMesh3D";
            case PrimitiveMeshKindUVE::Cube:
                break;
        }
        return "BoxMesh3D";
    };
    AddUVE<PrimitiveMeshComponentUVE>(entries, std::move(primitive));

    AddUVE<WorldEnvironment3DComponentUVE>(
        entries,
        MakeEntryUVE(
            "component.world_environment", "WorldEnvironment", kSectionOrderTypeSpecificUVE,
            {
                DeclareUVE<&WorldEnvironment3DComponentUVE::skyColor>("skyColor", "Sky Color",
                                                                          kPropertyTypeColorUVE),
                DeclareUVE<&WorldEnvironment3DComponentUVE::horizonColor>(
                    "horizonColor", "Horizon Color", kPropertyTypeColorUVE),
                DeclareUVE<&WorldEnvironment3DComponentUVE::groundColor>(
                    "groundColor", "Ground Color", kPropertyTypeColorUVE),
                WithRangeUVE(DeclareUVE<&WorldEnvironment3DComponentUVE::skyCurve>(
                                 "skyCurve", "Sky Curve", kPropertyTypeFloatUVE),
                             0.001, 4.0, 0.01),
                WithRangeUVE(DeclareUVE<&WorldEnvironment3DComponentUVE::groundCurve>(
                                 "groundCurve", "Ground Curve", kPropertyTypeFloatUVE),
                             0.001, 4.0, 0.01),
                DeclareUVE<&WorldEnvironment3DComponentUVE::ambientColor>(
                    "ambientColor", "Ambient Tint", kPropertyTypeColorUVE),
                WithRangeUVE(DeclareUVE<&WorldEnvironment3DComponentUVE::ambientEnergy>(
                                 "ambientEnergy", "Ambient Energy", kPropertyTypeFloatUVE),
                             0.0, 100.0, 0.05),
                WithRangeUVE(DeclareUVE<&WorldEnvironment3DComponentUVE::exposure>(
                                 "exposure", "Exposure", kPropertyTypeFloatUVE),
                             0.0, 100.0, 0.05),
                DeclareUVE<&WorldEnvironment3DComponentUVE::fogEnabled>("fogEnabled", "Fog",
                                                                            kPropertyTypeBoolUVE),
                [] {
                    TypeMetadataPropertyUVE property = DeclareUVE<&WorldEnvironment3DComponentUVE::fogColor>(
                        "fogColor", "Fog Color", kPropertyTypeColorUVE);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponentUVE*>(instance)->fogEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataPropertyUVE property =
                        WithRangeUVE(DeclareUVE<&WorldEnvironment3DComponentUVE::fogDensity>(
                                         "fogDensity", "Fog Density", kPropertyTypeFloatUVE),
                                     0.0, 1.0, 0.001);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponentUVE*>(instance)->fogEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataPropertyUVE property =
                        WithRangeUVE(DeclareUVE<&WorldEnvironment3DComponentUVE::fogSkyAffect>(
                                         "fogSkyAffect", "Fog Sky", kPropertyTypeFloatUVE),
                                     0.0, 1.0, 0.01);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponentUVE*>(instance)->fogEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataPropertyUVE property =
                        WithRangeUVE(DeclareUVE<&WorldEnvironment3DComponentUVE::fogHeight>(
                                         "fogHeight", "Fog Height", kPropertyTypeFloatUVE),
                                     -1000.0, 10000.0, 0.5);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponentUVE*>(instance)->fogEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataPropertyUVE property =
                        WithRangeUVE(DeclareUVE<&WorldEnvironment3DComponentUVE::fogHeightFalloff>(
                                         "fogHeightFalloff", "Fog Falloff", kPropertyTypeFloatUVE),
                                     0.1, 10000.0, 1.0);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponentUVE*>(instance)->fogEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataPropertyUVE property =
                        WithRangeUVE(DeclareUVE<&WorldEnvironment3DComponentUVE::fogSunScatter>(
                                         "fogSunScatter", "Fog Sun Scatter", kPropertyTypeFloatUVE),
                                     0.0, 1.0, 0.01);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponentUVE*>(instance)->fogEnabled;
                    };
                    return property;
                }(),
                DeclareUVE<&WorldEnvironment3DComponentUVE::postProcessingEnabled>(
                    "postProcessingEnabled", "Post Processing", kPropertyTypeBoolUVE),
                [] {
                    TypeMetadataPropertyUVE property = DeclareUVE<&WorldEnvironment3DComponentUVE::bloomEnabled>(
                        "bloomEnabled", "Bloom", kPropertyTypeBoolUVE);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponentUVE*>(instance)->postProcessingEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataPropertyUVE property =
                        WithRangeUVE(DeclareUVE<&WorldEnvironment3DComponentUVE::bloomIntensity>(
                                         "bloomIntensity", "Bloom Intensity", kPropertyTypeFloatUVE),
                                     0.0, 8.0, 0.01);
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponentUVE*>(instance);
                        return environment->postProcessingEnabled && environment->bloomEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataPropertyUVE property =
                        WithRangeUVE(DeclareUVE<&WorldEnvironment3DComponentUVE::bloomThreshold>(
                                         "bloomThreshold", "Bloom Threshold", kPropertyTypeFloatUVE),
                                     0.0, 8.0, 0.05);
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponentUVE*>(instance);
                        return environment->postProcessingEnabled && environment->bloomEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataPropertyUVE property = DeclareUVE<&WorldEnvironment3DComponentUVE::ssaoEnabled>(
                        "ssaoEnabled", "SSAO", kPropertyTypeBoolUVE);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DComponentUVE*>(instance)->postProcessingEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataPropertyUVE property =
                        WithRangeUVE(DeclareUVE<&WorldEnvironment3DComponentUVE::ssaoIntensity>(
                                         "ssaoIntensity", "SSAO Intensity", kPropertyTypeFloatUVE),
                                     0.0, 4.0, 0.05);
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponentUVE*>(instance);
                        return environment->postProcessingEnabled && environment->ssaoEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataPropertyUVE property =
                        WithRangeUVE(DeclareUVE<&WorldEnvironment3DComponentUVE::ssaoRadius>(
                                         "ssaoRadius", "SSAO Radius", kPropertyTypeFloatUVE),
                                     0.05, 4.0, 0.05);
                    property.isVisible = +[](const void* instance) {
                        const auto* environment = static_cast<const WorldEnvironment3DComponentUVE*>(instance);
                        return environment->postProcessingEnabled && environment->ssaoEnabled;
                    };
                    return property;
                }(),
                WithRangeUVE(DeclareUVE<&WorldEnvironment3DComponentUVE::brightness>(
                                 "brightness", "Brightness", kPropertyTypeFloatUVE),
                             -1.0, 1.0, 0.01),
                WithRangeUVE(DeclareUVE<&WorldEnvironment3DComponentUVE::contrast>(
                                 "contrast", "Contrast", kPropertyTypeFloatUVE),
                             0.0, 2.0, 0.01),
                WithRangeUVE(DeclareUVE<&WorldEnvironment3DComponentUVE::saturation>(
                                 "saturation", "Saturation", kPropertyTypeFloatUVE),
                             0.0, 2.0, 0.01),
                DeclareUVE<&WorldEnvironment3DComponentUVE::colorFilter>(
                    "colorFilter", "Color Filter", kPropertyTypeColorUVE),
                DeclareUVE<&WorldEnvironment3DComponentUVE::skyAssetPath>("skyAssetPath", "Sky Asset",
                                                                              kPropertyTypeStringUVE),
            }));

    AddUVE<ParticleEmitterComponentUVE>(
        entries, MakeEntryUVE("component.particle_emitter", "ParticleEmitter3D",
                              kSectionOrderTypeSpecificUVE,
                              {WithRangeUVE(DeclareUVE<&ParticleEmitterComponentUVE::maxParticles>(
                                                "maxParticles", "Max Particles", kPropertyTypeUInt32UVE),
                                            0.0, 1000000.0, 1.0)}));

    // LODGroup3D's own section. The chain is declared as a prefix: `levelCount` says how many of
    // the two lists below are in use, and both lists are drawn by a block drawer that shows exactly
    // those - a threshold past the last level is a number that does nothing, and an array of eight
    // rows where three matter reads as eight settings. The resolved level and the cull verdict are
    // runtime state: they describe the frame the renderer last ran, not the scene.
    AddValidatedUVE<LodGroup3DComponentUVE, &IsLodGroup3DObjectComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.lod_group_3d", "LODGroup3D", kSectionOrderTypeSpecificUVE,
            {
                WithTooltipUVE(DeclareUVE<&LodGroup3DComponentUVE::enabled>("enabled", "Enabled",
                                                                            kPropertyTypeBoolUVE),
                               "Off, the object draws at level 0 and is never distance-culled - what "
                               "an author wants while placing it."),
                WithRangeUVE(
                    WithTooltipUVE(DeclareUVE<&LodGroup3DComponentUVE::levelCount>("levelCount", "Levels",
                                                                                   kPropertyTypeUInt8UVE),
                                   "How many levels of the chain are in use. Thresholds and level "
                                   "meshes past this are ignored, so a chain can be shortened "
                                   "without rewriting the numbers."),
                    1.0, 8.0, 1.0),
                WithRangeUVE(
                    WithTooltipUVE(DeclareUVE<&LodGroup3DComponentUVE::hysteresis>("hysteresis", "Hysteresis",
                                                                                  kPropertyTypeFloatUVE),
                                   "How far past a threshold the object must go before it swaps, as a "
                                   "fraction of that threshold; it comes back at the same fraction "
                                   "under. Zero is the plain threshold rule."),
                    0.0, 0.5, 0.01),
                WithTooltipUVE(
                    WithCustomDrawerUVE(
                        WithElementCountUVE(
                            DeclareUVE<&LodGroup3DComponentUVE::distanceThresholds>(
                                "distanceThresholds", "Thresholds", kPropertyTypeFloatListUVE),
                            kMaximumLodLevelsUVE),
                        "lod-group-thresholds"),
                    "The distance each level takes over at, nearest first. Past the last one the "
                    "object is not drawn at all."),
                WithTooltipUVE(
                    WithCustomDrawerUVE(
                        WithElementCountUVE(
                            DeclareUVE<&LodGroup3DComponentUVE::lodMeshGuids>("lodMeshGuids", "Level Meshes",
                                                                              kPropertyTypeAssetGuidListUVE),
                            kMaximumLodLevelsUVE),
                        "lod-group-meshes"),
                    "The mesh drawn at each level. A level with no mesh draws the object's own Mesh "
                    "component mesh."),
                InGroupUVE(DeclareRuntimeStateUVE<&LodGroup3DComponentUVE::currentLevel>(
                               "currentLevel", "Current Level", kPropertyTypeUInt8UVE),
                           "Result"),
                InGroupUVE(DeclareRuntimeStateUVE<&LodGroup3DComponentUVE::culledByDistance>(
                               "culledByDistance", "Distance Culled", kPropertyTypeBoolUVE),
                           "Result"),
            }));
}

void DeclarePhysicsUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    TypeMetadataEntryUVE collider = MakeEntryUVE(
            "component.collider", "Collider", kSectionOrderTypeSpecificUVE,
            {
                WithTooltipUVE(DeclareUVE<&ColliderComponentUVE::disabled>("disabled", "Disabled",
                                                                          kPropertyTypeBoolUVE),
                               "Off this shape is ignored by collision, rays, and areas."),
                DeclareEnumUVE<&ColliderComponentUVE::shapeType>(
                    "shapeType", "Shape", {{0, "Box"}, {1, "Sphere"}, {2, "Capsule"}}),
                [] {
                    TypeMetadataPropertyUVE property = DeclareUVE<&ColliderComponentUVE::halfExtents>(
                        "halfExtents", "Half Extents", kPropertyTypeVector3UVE);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const ColliderComponentUVE*>(instance)->shapeType ==
                               ColliderShapeTypeUVE::Box;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataPropertyUVE property = WithRangeUVE(
                        DeclareUVE<&ColliderComponentUVE::radius>("radius", "Radius",
                                                                   kPropertyTypeFloatUVE),
                        0.0, 10000.0, 0.01);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const ColliderComponentUVE*>(instance)->shapeType !=
                               ColliderShapeTypeUVE::Box;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataPropertyUVE property = WithRangeUVE(
                        DeclareUVE<&ColliderComponentUVE::height>("height", "Height",
                                                                   kPropertyTypeFloatUVE),
                        0.0, 10000.0, 0.01);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const ColliderComponentUVE*>(instance)->shapeType ==
                               ColliderShapeTypeUVE::Capsule;
                    };
                    return property;
                }(),
                WithCustomDrawerUVE(
                    WithTooltipUVE(DeclareUVE<&ColliderComponentUVE::collisionLayer>("collisionLayer", "Layer",
                                                                                     kPropertyTypeBitMask32UVE),
                                   "The layers this object is on - what others can find it on."),
                    std::string(kLayerMaskDrawerPhysicsUVE)),
                WithCustomDrawerUVE(
                    WithTooltipUVE(DeclareUVE<&ColliderComponentUVE::collisionMask>("collisionMask", "Mask",
                                                                                    kPropertyTypeBitMask32UVE),
                                   "The layers this object looks for - what it collides with or detects."),
                    std::string(kLayerMaskDrawerPhysicsUVE)),
                WithRangeUVE(DeclareUVE<&ColliderComponentUVE::friction>("friction", "Friction",
                                                                         kPropertyTypeFloatUVE),
                             0.0, 1.0, 0.01),
                WithRangeUVE(DeclareUVE<&ColliderComponentUVE::restitution>("restitution", "Restitution",
                                                                            kPropertyTypeFloatUVE),
                             0.0, 1.0, 0.01),
                WithRangeUVE(DeclareUVE<&ColliderComponentUVE::density>("density", "Density",
                                                                        kPropertyTypeFloatUVE),
                             0.0, 10000.0, 0.01),
            });
    // A collider is always part of some object rather than a feature of its own: the collision a
    // BoxMesh3D/SphereMesh3D/PlaneMesh3D is created with sits in that object's section, and a body's
    // or area's shape, layer and mask sit in PhysicsObject3D's.
    collider.nestedUnderTypeIds = {"component.primitive_mesh", "component.physics_object"};
    AddUVE<ColliderComponentUVE>(entries, std::move(collider));

    AddUVE<Rigid3DComponentUVE>(
        entries,
        MakeEntryUVE(
            "component.rigid_body", "Rigid Body", kSectionOrderTypeSpecificUVE,
            {
                WithRangeUVE(DeclareUVE<&Rigid3DComponentUVE::mass>("mass", "Mass",
                                                                       kPropertyTypeFloatUVE),
                             0.0, 100000.0, 0.01),
                DeclareUVE<&Rigid3DComponentUVE::isKinematic>("isKinematic", "Kinematic",
                                                                kPropertyTypeBoolUVE),
                WithRangeUVE(DeclareUVE<&Rigid3DComponentUVE::drag>("drag", "Drag",
                                                                       kPropertyTypeFloatUVE),
                             0.0, 100.0, 0.01),
                WithRangeUVE(DeclareUVE<&Rigid3DComponentUVE::gravityScale>(
                                 "gravityScale", "Gravity Scale", kPropertyTypeFloatUVE),
                             -100.0, 100.0, 0.05),
                DeclareUVE<&Rigid3DComponentUVE::velocity>("velocity", "Velocity",
                                                             kPropertyTypeVector3UVE),
                DeclareUVE<&Rigid3DComponentUVE::angularVelocity>("angularVelocity", "Angular Velocity",
                                                                    kPropertyTypeVector3UVE),
                DeclareUVE<&Rigid3DComponentUVE::torque>("torque", "Torque", kPropertyTypeVector3UVE),
                DeclareUVE<&Rigid3DComponentUVE::inverseInertia>("inverseInertia", "Inverse Inertia",
                                                                   kPropertyTypeVector3UVE),
            }));

    using A = AreaComponentUVE;
    const auto whenGravity = [](TypeMetadataPropertyUVE property) {
        property.isVisible = +[](const void* instance) {
            return static_cast<const A*>(instance)->gravityOverride != AreaSpaceOverrideModeUVE::Disabled;
        };
        return property;
    };
    const auto whenGravityPoint = [](TypeMetadataPropertyUVE property) {
        property.isVisible = +[](const void* instance) {
            const A& area = *static_cast<const A*>(instance);
            return area.gravityOverride != AreaSpaceOverrideModeUVE::Disabled && area.gravityPoint;
        };
        return property;
    };
    const auto whenGravityVector = [](TypeMetadataPropertyUVE property) {
        property.isVisible = +[](const void* instance) {
            const A& area = *static_cast<const A*>(instance);
            return area.gravityOverride != AreaSpaceOverrideModeUVE::Disabled && !area.gravityPoint;
        };
        return property;
    };
    const auto whenLinearDamp = [](TypeMetadataPropertyUVE property) {
        property.isVisible = +[](const void* instance) {
            return static_cast<const A*>(instance)->linearDampOverride !=
                   AreaSpaceOverrideModeUVE::Disabled;
        };
        return property;
    };
    const auto whenAngularDamp = [](TypeMetadataPropertyUVE property) {
        property.isVisible = +[](const void* instance) {
            return static_cast<const A*>(instance)->angularDampOverride !=
                   AreaSpaceOverrideModeUVE::Disabled;
        };
        return property;
    };
    const std::vector<TypeMetadataEnumEntryUVE> spaceOverrideOptions{
        {0, "Disabled"},
        {1, "Combine"},
        {2, "Combine Replace"},
        {3, "Replace"},
        {4, "Replace Combine"},
    };
    AddValidatedUVE<AreaComponentUVE, &IsAreaComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.area", "Area3D", kSectionOrderTypeSpecificUVE,
            {
                WithTooltipUVE(
                    DeclareUVE<&A::halfExtents>("halfExtents", "Half Extents", kPropertyTypeVector3UVE),
                    "The axis-aligned box this area occupies, in metres, around the object's origin. "
                    "World scale is deliberately not applied - a scaled art pivot never re-sizes a "
                    "trigger."),
                WithCustomDrawerUVE(
                    WithTooltipUVE(DeclareUVE<&A::collisionLayer>("collisionLayer", "Layer",
                                                                 kPropertyTypeBitMask32UVE),
                                   "The layers this area is on - what others have to be looking for."),
                    std::string(kLayerMaskDrawerPhysicsUVE)),
                WithCustomDrawerUVE(
                    WithTooltipUVE(DeclareUVE<&A::collisionMask>("collisionMask", "Mask",
                                                                kPropertyTypeBitMask32UVE),
                                   "The layers this area looks for. Detection needs BOTH sides to "
                                   "accept the other."),
                    std::string(kLayerMaskDrawerPhysicsUVE)),
                WithTooltipUVE(DeclareUVE<&A::monitoring>("monitoring", "Monitoring",
                                                          kPropertyTypeBoolUVE),
                               "On, this area lists who is inside it and fires enter/exit. Off, the "
                               "lists stay empty. Space override still applies either way."),
                WithTooltipUVE(DeclareUVE<&A::monitorable>("monitorable", "Monitorable",
                                                           kPropertyTypeBoolUVE),
                               "On, other monitoring areas can detect this one."),
                InGroupUVE(WithTooltipUVE(DeclareUVE<&A::priority>("priority", "Priority",
                                                                  kPropertyTypeInt32UVE),
                                          "When several areas overlap one body, higher priority is "
                                          "applied first. Equal priorities resolve by entity id."),
                           "Space"),
                InGroupUVE(WithTooltipUVE(DeclareEnumUVE<&A::gravityOverride>(
                                              "gravityOverride", "Gravity Override", spaceOverrideOptions),
                                          "How this area's gravity mixes with the world and with lower "
                                          "priority areas. Disabled leaves gravity alone."),
                           "Gravity"),
                InGroupUVE(whenGravity(WithTooltipUVE(
                               DeclareUVE<&A::gravityPoint>("gravityPoint", "Point Gravity",
                                                            kPropertyTypeBoolUVE),
                               "On, gravity pulls toward a point instead of along Gravity Direction.")),
                           "Gravity"),
                InGroupUVE(whenGravityVector(WithTooltipUVE(
                               DeclareUVE<&A::gravityDirection>("gravityDirection", "Gravity Direction",
                                                                kPropertyTypeVector3UVE),
                               "The direction of gravity inside this area. It is normalized before "
                               "use; a zero vector is no gravity.")),
                           "Gravity"),
                InGroupUVE(whenGravity(WithRangeUVE(
                               WithTooltipUVE(DeclareUVE<&A::gravityMagnitude>(
                                                  "gravityMagnitude", "Gravity", kPropertyTypeFloatUVE),
                                              "Gravity strength in metres per second squared. Negative "
                                              "repels when Point Gravity is on."),
                               -10000.0, 10000.0, 0.01)),
                           "Gravity"),
                InGroupUVE(whenGravityPoint(WithTooltipUVE(
                               DeclareUVE<&A::gravityPointOffset>("gravityPointOffset", "Point Offset",
                                                                  kPropertyTypeVector3UVE),
                               "Where the point sits relative to this area's origin, in metres.")),
                           "Gravity"),
                InGroupUVE(whenGravityPoint(WithRangeUVE(
                               WithTooltipUVE(DeclareUVE<&A::gravityPointUnitDistance>(
                                                  "gravityPointUnitDistance", "Unit Distance",
                                                  kPropertyTypeFloatUVE),
                                              "Distance at which point gravity equals Gravity. 0 is "
                                              "constant magnitude; greater than 0 is inverse square."),
                               0.0, 100000.0, 0.01)),
                           "Gravity"),
                InGroupUVE(WithTooltipUVE(DeclareEnumUVE<&A::linearDampOverride>(
                                              "linearDampOverride", "Linear Damp Override",
                                              spaceOverrideOptions),
                                          "How this area's linear damping mixes with the body's drag "
                                          "and with lower priority areas."),
                           "Damping"),
                InGroupUVE(whenLinearDamp(WithRangeUVE(
                               WithTooltipUVE(DeclareUVE<&A::linearDamp>("linearDamp", "Linear Damp",
                                                                        kPropertyTypeFloatUVE),
                                              "Linear damping this area contributes. Velocity is scaled "
                                              "by (1 - damp * dt) each physics step."),
                               0.0, 100.0, 0.01)),
                           "Damping"),
                InGroupUVE(WithTooltipUVE(DeclareEnumUVE<&A::angularDampOverride>(
                                              "angularDampOverride", "Angular Damp Override",
                                              spaceOverrideOptions),
                                          "How this area's angular damping mixes with lower priority "
                                          "areas. Bodies have no angular drag of their own."),
                           "Damping"),
                InGroupUVE(whenAngularDamp(WithRangeUVE(
                               WithTooltipUVE(DeclareUVE<&A::angularDamp>("angularDamp", "Angular Damp",
                                                                         kPropertyTypeFloatUVE),
                                              "Angular damping this area contributes, applied after "
                                              "torque integration."),
                               0.0, 100.0, 0.01)),
                           "Damping"),
                InGroupUVE(DeclareRuntimeStateUVE<&A::overlappingBodyCount>(
                               "overlappingBodyCount", "Bodies", kPropertyTypeUInt8UVE),
                           "Occupancy"),
                InGroupUVE(DeclareRuntimeStateUVE<&A::overlappingBodiesTruncated>(
                               "overlappingBodiesTruncated", "Bodies Truncated", kPropertyTypeBoolUVE),
                           "Occupancy"),
                InGroupUVE(DeclareRuntimeStateUVE<&A::overlappingAreaCount>(
                               "overlappingAreaCount", "Areas", kPropertyTypeUInt8UVE),
                           "Occupancy"),
                InGroupUVE(DeclareRuntimeStateUVE<&A::overlappingAreasTruncated>(
                               "overlappingAreasTruncated", "Areas Truncated", kPropertyTypeBoolUVE),
                           "Occupancy"),
            }));

    // Kinematic3D's own section. A platform is authored with two decisions - where it is going, and
    // how quickly it gets there - so the drawer stays short and honest, and says in the property
    // help what the mover does with them.
    // Declared with its own rule: the Inspector refuses an edit that would leave the component past
    // its contract (a non-finite target, an interpolation outside 0..1) and keeps the last accepted
    // value, so the mover never has to guess at a value nobody could have meant.
    AddValidatedUVE<Kinematic3DComponentUVE, &IsKinematic3DObjectComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.kinematic_3d", "Kinematic Body", kSectionOrderTypeSpecificUVE,
            {
                WithTooltipUVE(DeclareUVE<&Kinematic3DComponentUVE::targetVelocity>(
                                   "targetVelocity", "Target Velocity", kPropertyTypeVector3UVE),
                               "Where the body is going, metres per second, in its own local axes. "
                               "Rotated into the world each step. Geometry stops it; bodies it meets "
                               "get pushed."),
                WithRangeUVE(WithTooltipUVE(DeclareUVE<&Kinematic3DComponentUVE::interpolation>(
                                                "interpolation", "Interpolation", kPropertyTypeFloatUVE),
                                            "How quickly the body gets up to speed: 1 is at speed on "
                                            "the first step, lower values ease in over about a second, "
                                            "0 never eases on its own (a script drives the body)."),
                             0.0, 1.0, 0.01),
                WithTooltipUVE(DeclareUVE<&Kinematic3DComponentUVE::active>("active", "Active",
                                                                           kPropertyTypeBoolUVE),
                               "Off leaves the body where it is and stops it dead, including for "
                               "anything standing on it."),
            }));

    // SpringArm3D's own section: the third-person camera boom. The authored half is what the arm
    // reaches and what it collides with, and it is grouped behind the switch the rest of it depends
    // on; the runtime half is the one number the arm derives every step.
    AddValidatedUVE<SpringArm3DComponentUVE, &IsSpringArm3DObjectComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.spring_arm", "SpringArm3D", kSectionOrderTypeSpecificUVE,
            {
                WithTooltipUVE(DeclareUVE<&SpringArm3DComponentUVE::enabled>("enabled", "Enabled",
                                                                             kPropertyTypeBoolUVE),
                               "Off, the arm casts nothing and hands its length back, so whatever it "
                               "carries returns to the pose it was authored with."),
                WhenOnUVE<&SpringArm3DComponentUVE::enabled>(WithRangeUVE(
                    WithTooltipUVE(DeclareUVE<&SpringArm3DComponentUVE::armLength>(
                                       "armLength", "Arm Length", kPropertyTypeFloatUVE),
                                   "How far the arm reaches along its own local +Z, in metres. The "
                                   "arm casts this far every fixed step and shortens to whatever it "
                                   "finds, minus the margin."),
                    0.0, 10000.0, 0.01)),
                WhenOnUVE<&SpringArm3DComponentUVE::enabled>(WithRangeUVE(
                    WithTooltipUVE(DeclareUVE<&SpringArm3DComponentUVE::margin>("margin", "Margin",
                                                                                kPropertyTypeFloatUVE),
                                   "How far the arm keeps the thing it carries off the surface it "
                                   "hit, in metres. This is the camera's skin."),
                    0.0, 10000.0, 0.01)),
                WhenOnUVE<&SpringArm3DComponentUVE::enabled>(WithRangeUVE(
                    WithTooltipUVE(DeclareUVE<&SpringArm3DComponentUVE::smoothing>("smoothing",
                                                                                   "Smoothing",
                                                                                   kPropertyTypeFloatUVE),
                                   "How fast the arm springs back out once the way is clear, per "
                                   "second: 0 pops out exactly like Godot's SpringArm3D, higher "
                                   "values rise sooner. Shortening always snaps - a camera is never "
                                   "allowed to clip through a wall."),
                    0.0, 1000.0, 0.1)),
                WhenOnUVE<&SpringArm3DComponentUVE::enabled>(WithCustomDrawerUVE(
                    WithTooltipUVE(DeclareUVE<&SpringArm3DComponentUVE::collisionMask>(
                                       "collisionMask", "Mask", kPropertyTypeBitMask32UVE),
                                   "What the arm collides with. Leave the layer the rig is on out of "
                                   "it, or the camera will sit on the character it belongs to."),
                    std::string(kLayerMaskDrawerPhysicsUVE))),
                InGroupUVE(DeclareRuntimeStateUVE<&SpringArm3DComponentUVE::currentLength>(
                               "currentLength", "Current Length", kPropertyTypeFloatUVE),
                           "State"),
            }));

    // Character3D's own section. Grouped by what an author is thinking about - how it moves,
    // what it stands on, what it hits - with the state the controller writes each step last, shown
    // only while playing because that is when it describes something real.
    using C = CharacterControllerComponentUVE;
    const auto whenGrounded = [](TypeMetadataPropertyUVE property) {
        property.isVisible = +[](const void* instance) {
            return static_cast<const C*>(instance)->motionMode == CharacterMotionModeUVE::Grounded;
        };
        return property;
    };
    const auto whenBuiltIn = [](TypeMetadataPropertyUVE property) {
        property.isVisible = +[](const void* instance) { return static_cast<const C*>(instance)->builtInMovement; };
        return property;
    };
    const auto whenBuiltInGrounded = [](TypeMetadataPropertyUVE property) {
        property.isVisible = +[](const void* instance) {
            const C& c = *static_cast<const C*>(instance);
            return c.builtInMovement && c.motionMode == CharacterMotionModeUVE::Grounded;
        };
        return property;
    };
    const auto whenPushing = [](TypeMetadataPropertyUVE property) {
        property.isVisible = +[](const void* instance) { return static_cast<const C*>(instance)->pushRigidBodies; };
        return property;
    };
    TypeMetadataPropertyUVE maxSlides = WithTooltipUVE(
        DeclareUVE<&C::maxSlides>("maxSlides", "Max Slides", kPropertyTypeUInt32UVE),
        "How many pieces a move is cut into to follow walls and corners. More is smoother and costs more.");
    maxSlides.range = {true, 1.0, 32.0, 1.0};
    TypeMetadataPropertyUVE maximumContacts = WithTooltipUVE(
        DeclareUVE<&C::maximumContacts>("maximumContacts", "Max Contacts", kPropertyTypeUInt32UVE),
        "The most contacts one move reports, deepest first. Extras are dropped, and the step says so.");
    maximumContacts.range = {true, 1.0, 64.0, 1.0};
    AddValidatedUVE<CharacterControllerComponentUVE, &IsCharacterControllerComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.character_controller", "Character3D", kSectionOrderTypeSpecificUVE,
            {
                WithTooltipUVE(DeclareEnumUVE<&C::motionMode>("motionMode", "Motion Mode",
                                                              {{0, "Grounded"}, {1, "Floating"}}),
                               "Grounded walks on floors under gravity. Floating flies or swims: no gravity, no "
                               "floor, every surface a wall."),
                WithTooltipUVE(DeclareUVE<&C::upDirection>("upDirection", "Up Direction", kPropertyTypeVector3UVE),
                               "The body's own up. Floors, jumps, gravity and ceilings are measured from this. "
                               "World +Y is the default, because the engine's gravity is along -Y."),
                whenGrounded(WithTooltipUVE(WithRangeUVE(DeclareUVE<&C::gravityScale>("gravityScale", "Gravity Scale",
                                                                                      kPropertyTypeFloatUVE),
                                                         0.0, 100.0, 0.05),
                                            "Multiplies the world's gravity. 1 is normal, 0 is none.")),
                InGroupUVE(WithTooltipUVE(DeclareUVE<&C::builtInMovement>("builtInMovement", "Built-in",
                                                                          kPropertyTypeBoolUVE),
                                          "Moves and jumps from the keyboard with no script. Off, it moves by its "
                                          "Velocity alone - how a script or an AI drives it."),
                           "Movement"),
                InGroupUVE(whenBuiltIn(WithTooltipUVE(WithRangeUVE(DeclareUVE<&C::moveSpeed>("moveSpeed", "Speed",
                                                                                             kPropertyTypeFloatUVE),
                                                                   0.0, 1000.0, 0.1),
                                                      "Top speed, in metres per second.")),
                           "Movement"),
                InGroupUVE(whenBuiltInGrounded(WithTooltipUVE(
                               WithRangeUVE(DeclareUVE<&C::jumpHeight>("jumpHeight", "Jump Height", kPropertyTypeFloatUVE),
                                            0.0, 1000.0, 0.05),
                               "How high a jump reaches, in metres, whatever the gravity.")),
                           "Movement"),
                InGroupUVE(whenBuiltInGrounded(WithTooltipUVE(
                               WithRangeUVE(DeclareUVE<&C::airControl>("airControl", "Air Control", kPropertyTypeFloatUVE),
                                            0.0, 1.0, 0.01),
                               "How much steering works in the air. 1 is full control, 0 keeps the jump's direction.")),
                           "Movement"),
                InGroupUVE(whenBuiltInGrounded(WithTooltipUVE(
                               WithRangeUVE(DeclareUVE<&C::coyoteTimeSeconds>("coyoteTimeSeconds", "Coyote Time",
                                                                            kPropertyTypeFloatUVE),
                                            0.0, 1.0, 0.01),
                               "A jump still works this many seconds after walking off a ledge.")),
                           "Movement"),
                InGroupUVE(whenBuiltInGrounded(WithTooltipUVE(
                               WithRangeUVE(DeclareUVE<&C::jumpBufferSeconds>("jumpBufferSeconds", "Jump Buffer",
                                                                            kPropertyTypeFloatUVE),
                                            0.0, 1.0, 0.01),
                               "A jump pressed this many seconds before landing happens on landing.")),
                           "Movement"),
                InGroupUVE(whenGrounded(WithTooltipUVE(
                               WithRangeUVE(DeclareUVE<&C::floorMaxAngleDegrees>("floorMaxAngleDegrees", "Floor Angle",
                                                                                kPropertyTypeFloatUVE),
                                            0.0, 90.0, 1.0),
                               "The steepest slope still walked as a floor. Steeper surfaces are walls it slides "
                               "along.")),
                           "Floor"),
                InGroupUVE(whenGrounded(WithTooltipUVE(
                               WithRangeUVE(DeclareUVE<&C::wallMinSlideAngleDegrees>(
                                                "wallMinSlideAngleDegrees", "Wall Slide Angle", kPropertyTypeFloatUVE),
                                            0.0, 90.0, 1.0),
                               "How far past the floor angle a wall has to lean before the body slides down it "
                               "instead of being stopped by it.")),
                           "Floor"),
                InGroupUVE(whenGrounded(WithTooltipUVE(
                               WithRangeUVE(DeclareUVE<&C::floorSnapLength>("floorSnapLength", "Snap Length",
                                                                          kPropertyTypeFloatUVE),
                                            0.0, 10.0, 0.01),
                               "Stays on the floor walking down steps and ledges up to this far below. 0 lets it "
                               "drop off every edge.")),
                           "Floor"),
                InGroupUVE(whenGrounded(WithTooltipUVE(
                               WithRangeUVE(DeclareUVE<&C::minStepWidth>("minStepWidth", "Min Step Width",
                                                                       kPropertyTypeFloatUVE),
                                            0.0, 1.0, 0.01),
                               "How much room a step needs on top to be worth climbing. Narrower than this is a "
                               "wall with a decoration.")),
                           "Floor"),
                InGroupUVE(whenGrounded(WithTooltipUVE(
                               DeclareUVE<&C::floorStopOnSlope>("floorStopOnSlope", "Stop On Slope",
                                                                kPropertyTypeBoolUVE),
                               "Standing still on a slope stays still instead of creeping downhill.")),
                           "Floor"),
                InGroupUVE(whenGrounded(WithTooltipUVE(
                               DeclareUVE<&C::floorConstantSpeed>("floorConstantSpeed", "Constant Speed",
                                                                  kPropertyTypeBoolUVE),
                               "Walking up or down a slope keeps the horizontal speed the walk started with.")),
                           "Floor"),
                InGroupUVE(whenGrounded(WithTooltipUVE(
                               WithRangeUVE(DeclareUVE<&C::safeMargin>("safeMargin", "Safe Margin",
                                                                      kPropertyTypeFloatUVE),
                                            0.0, 0.25, 0.001),
                               "How far short of a surface the body stops, so it does not jitter against it.")),
                           "Floor"),
                InGroupUVE(whenGrounded(WithTooltipUVE(
                               DeclareUVE<&C::floorBlockOnWall>("floorBlockOnWall", "Block On Wall",
                                                                kPropertyTypeBoolUVE),
                               "On the floor, stop at a wall instead of sliding along it - no corner slipping.")),
                           "Floor"),
                InGroupUVE(WithTooltipUVE(
                               DeclareEnumUVE<&C::platformOnLeave>(
                                   "platformOnLeave", "On Leave Platform",
                                   {{0, "Keep Velocity"}, {1, "Add Velocity"}, {2, "Add Upward Velocity"}}),
                               "What a platform gives a body that walks off it: its whole velocity, only the "
                               "upward part, or nothing."),
                           "Platforms"),
                InGroupUVE(WithTooltipUVE(
                               WithRangeUVE(DeclareUVE<&C::maximumPlatformSpeed>(
                                                "maximumPlatformSpeed", "Max Platform Speed", kPropertyTypeFloatUVE),
                                            0.0, 10000.0, 1.0),
                               "The fastest platform motion that may carry this body. Past it the platform is "
                               "treated as standing still, so a teleported platform cannot fling anyone."),
                           "Platforms"),
                InGroupUVE(whenGrounded(WithTooltipUVE(
                               WithRangeUVE(DeclareUVE<&C::maxStepHeight>("maxStepHeight", "Step Height",
                                                                        kPropertyTypeFloatUVE),
                                            0.0, 10.0, 0.01),
                               "Walks up steps and kerbs up to this high without jumping. 0 turns it off.")),
                           "Floor"),
                InGroupUVE(WithTooltipUVE(DeclareUVE<&C::slideOnCeiling>("slideOnCeiling", "Slide On Ceiling",
                                                                         kPropertyTypeBoolUVE),
                                          "On hitting a ceiling, keep sliding along it. Off, the move stops there."),
                           "Ceiling"),
                InGroupUVE(WithTooltipUVE(DeclareUVE<&C::pushRigidBodies>("pushRigidBodies", "Push Bodies",
                                                                          kPropertyTypeBoolUVE),
                                          "Walking into a rigid body pushes it."),
                           "Pushing"),
                InGroupUVE(whenPushing(WithTooltipUVE(
                               WithRangeUVE(DeclareUVE<&C::pushStrength>("pushStrength", "Strength", kPropertyTypeFloatUVE),
                                            0.0, 100.0, 0.05),
                               "How hard it pushes, relative to its own speed.")),
                           "Pushing"),
                InGroupUVE(whenPushing(WithTooltipUVE(
                               WithRangeUVE(DeclareUVE<&C::maxPushSpeed>("maxPushSpeed", "Max Speed", kPropertyTypeFloatUVE),
                                            0.0, 1000.0, 0.1),
                               "The fastest a push may send a body, in metres per second.")),
                           "Pushing"),
                InGroupUVE(std::move(maxSlides), "Collision"),
                InGroupUVE(std::move(maximumContacts), "Collision"),
                InGroupUVE(DeclareRuntimeStateUVE<&C::velocity>("velocity", "Velocity", kPropertyTypeVector3UVE), "State"),
                InGroupUVE(DeclareRuntimeStateUVE<&C::grounded>("grounded", "Grounded", kPropertyTypeBoolUVE), "State"),
                InGroupUVE(DeclareRuntimeStateUVE<&C::isOnCeiling>("isOnCeiling", "On Ceiling", kPropertyTypeBoolUVE),
                           "State"),
                InGroupUVE(DeclareRuntimeStateUVE<&C::floorNormal>("floorNormal", "Floor Normal", kPropertyTypeVector3UVE),
                           "State"),
            }));
}

/// RayCast3D's own section: what the ray is, what it refuses to hit, and what it found. The
/// exclusions are entity references, so they are drawn by the reference-list drawer and remapped by
/// the scene serializer like every other reference an author can pick; the result rows are runtime
/// state, shown so an author can see the answer the engine computed rather than guessing at it.
void DeclareRayCastUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    AddValidatedUVE<RayCast3DComponentUVE, &IsRayCast3DObjectComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.ray_cast_3d", "RayCast3D", kSectionOrderTypeSpecificUVE,
            {
                WithTooltipUVE(
                    DeclareUVE<&RayCast3DComponentUVE::enabled>("enabled", "Enabled", kPropertyTypeBoolUVE),
                    "Off, the ray casts nothing and clears its result - no hit, no point, no "
                    "normal, no entity."),
                WithTooltipUVE(
                    DeclareUVE<&RayCast3DComponentUVE::direction>("direction", "Direction",
                                                                  kPropertyTypeVector3UVE),
                    "Which way the ray points in this object's own space, rotated by its world "
                    "rotation every step. Any non-zero length is fine; it is a direction, not a "
                    "distance - the Length below is the distance."),
                WithRangeUVE(
                    WithTooltipUVE(DeclareUVE<&RayCast3DComponentUVE::length>("length", "Length",
                                                                             kPropertyTypeFloatUVE),
                                   "How far the ray reaches along its direction, in metres. It "
                                   "reports the closest collider it meets inside that distance and "
                                   "nothing past it."),
                    0.0, 100000.0, 0.1),
                WithCustomDrawerUVE(
                    WithTooltipUVE(
                        DeclareUVE<&RayCast3DComponentUVE::collisionMask>("collisionMask", "Mask",
                                                                          kPropertyTypeBitMask32UVE),
                        "Which collision layers the ray is allowed to hit, on the same layer drawer "
                        "every other physics object uses."),
                    std::string(kLayerMaskDrawerPhysicsUVE)),
                WithTooltipUVE(
                    WithCustomDrawerUVE(
                        WithElementCountUVE(
                            DeclareUVE<&RayCast3DComponentUVE::exclusions>("exclusions", "Exclusions",
                                                                           kPropertyTypeEntityListUVE),
                            kMaximumRayCastExclusionsUVE),
                        "entity-reference-list"),
                    "Objects this ray refuses to hit, on top of its own object - a ray never hits "
                    "the collider it starts inside. An exclusion is not a mask: no layer can bring "
                    "one back. The ray's own object is always excluded, so listing it changes "
                    "nothing."),
                InGroupUVE(DeclareRuntimeStateUVE<&RayCast3DComponentUVE::hit>("hit", "Hit",
                                                                               kPropertyTypeBoolUVE),
                           "Result"),
                InGroupUVE(DeclareRuntimeStateUVE<&RayCast3DComponentUVE::hitEntity>(
                               "hitEntity", "Hit Entity", kPropertyTypeEntityUVE),
                           "Result"),
                InGroupUVE(DeclareRuntimeStateUVE<&RayCast3DComponentUVE::hitPosition>(
                               "hitPosition", "Hit Position", kPropertyTypeVector3UVE),
                           "Result"),
                InGroupUVE(DeclareRuntimeStateUVE<&RayCast3DComponentUVE::hitNormal>(
                               "hitNormal", "Hit Normal", kPropertyTypeVector3UVE),
                           "Result"),
            }));
}

void DeclareProjectileUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    // Projectile3D's own section. The authored half is the whole flight - where it goes, how fat
    // it is, how long it lives, what it may hit and what its motion does when it does. The two
    // bounce coefficients appear only once the policy actually uses them, and the runtime half is
    // last, under the groups its state and its last contact belong to.
    // Declared with its own rule for the same reason Kinematic3D's is: the Inspector refuses an
    // edit that would leave the component past its contract (a policy this engine cannot execute,
    // a coefficient outside 0..1) and keeps the last accepted value, so the step never has to
    // guess at a value nobody could have meant.
    const auto whenBouncing = [](TypeMetadataPropertyUVE property) {
        property.isVisible = +[](const void* instance) {
            return static_cast<const Projectile3DComponentUVE*>(instance)->hitPolicy ==
                   Projectile3DHitPolicyUVE::Bounce;
        };
        return property;
    };
    TypeMetadataPropertyUVE projectileIgnore = WithTooltipUVE(
        DeclareUVE<&Projectile3DComponentUVE::ignoreEntity>("ignoreEntity", "Ignore Entity", kPropertyTypeEntityUVE),
        "Never hit this entity. Set the owner so a shot does not collide with the body that fired it.");
    projectileIgnore.flags = TypeMetadataPropertyFlagsUVE::EntityReference;
    AddValidatedUVE<Projectile3DComponentUVE, &IsProjectile3DObjectComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.projectile_3d", "Projectile3D", kSectionOrderTypeSpecificUVE,
            {
                WithTooltipUVE(
                    DeclareUVE<&Projectile3DComponentUVE::active>("active", "Active", kPropertyTypeBoolUVE),
                    "Off stops everything: no motion, no sweep, no countdown. The engine also "
                    "clears it when the projectile stops on a hit or runs out of lifetime."),
                WithTooltipUVE(
                    DeclareUVE<&Projectile3DComponentUVE::velocity>("velocity", "Velocity",
                                                                    kPropertyTypeVector3UVE),
                    "Where the projectile is going, in metres per second, in its own axes. It "
                    "is the state that accumulates Acceleration, so this is the launch velocity "
                    "and the fixed step keeps it honest."),
                WithTooltipUVE(
                    DeclareUVE<&Projectile3DComponentUVE::acceleration>("acceleration", "Acceleration",
                                                                        kPropertyTypeVector3UVE),
                    "Added to Velocity every step, in metres per second squared: gravity, drag, a "
                    "wind volume. Gravity is not implied - a projectile only falls if something "
                    "here says so."),
                WithRangeUVE(
                    WithTooltipUVE(DeclareUVE<&Projectile3DComponentUVE::radius>("radius", "Radius",
                                                                                 kPropertyTypeFloatUVE),
                                   "The sphere the projectile sweeps, in world metres. It is the "
                                   "size that decides whether it fits through a gap and how far "
                                   "its centre is held off a surface - a ray is infinitely thin "
                                   "and misses the hits a fat projectile must not miss."),
                    0.001, 1000.0, 0.01),
                WithRangeUVE(
                    WithTooltipUVE(DeclareUVE<&Projectile3DComponentUVE::maxLifetime>(
                                       "maxLifetime", "Max Lifetime", kPropertyTypeFloatUVE),
                                   "Seconds of flight before the engine clears Active. A bouncing "
                                   "projectile has no other end, so this is the clock that always "
                                   "ends one."),
                    0.01, 3600.0, 0.1),
                WithCustomDrawerUVE(
                    WithTooltipUVE(DeclareUVE<&Projectile3DComponentUVE::collisionMask>(
                                       "collisionMask", "Mask", kPropertyTypeBitMask32UVE),
                                   "Which collision layers this projectile may hit, on the same "
                                   "layer drawer every other physics object uses."),
                    std::string(kLayerMaskDrawerPhysicsUVE)),
                projectileIgnore,
                WithTooltipUVE(
                    DeclareEnumUVE<&Projectile3DComponentUVE::hitPolicy>("hitPolicy", "On Hit",
                                                                         {{0, "Stop"}, {1, "Bounce"}}),
                    "What this projectile's own motion does when it hits something. The engine "
                    "owns the motion - Stop halts it at the contact, Bounce reflects it - and "
                    "gameplay owns the consequences: damage, effects and despawning read the hit "
                    "event or these result fields."),
                whenBouncing(WithRangeUVE(
                    WithTooltipUVE(DeclareUVE<&Projectile3DComponentUVE::restitution>(
                                       "restitution", "Restitution", kPropertyTypeFloatUVE),
                                   "How much of the speed into a surface comes back out of it: 1 "
                                   "leaves as fast as it arrived, 0 arrives and slides. This is "
                                   "the projectile's own coefficient - the surface's is reported "
                                   "with the contact for gameplay to react to."),
                    0.0, 1.0, 0.01)),
                whenBouncing(WithRangeUVE(
                    WithTooltipUVE(DeclareUVE<&Projectile3DComponentUVE::friction>("friction",
                                                                                   "Bounce Friction",
                                                                                   kPropertyTypeFloatUVE),
                                   "How much of the speed along the surface is lost on each "
                                   "bounce: 0 keeps all of its sideways motion, 1 keeps none. A "
                                   "bounce that leaves no motion at all is a stop."),
                    0.0, 1.0, 0.01)),
                InGroupUVE(DeclareRuntimeStateUVE<&Projectile3DComponentUVE::remainingLifetime>(
                               "remainingLifetime", "Remaining Lifetime", kPropertyTypeFloatUVE),
                           "State"),
                InGroupUVE(DeclareRuntimeStateUVE<&Projectile3DComponentUVE::hit>("hit", "Hit",
                                                                                  kPropertyTypeBoolUVE),
                           "Result"),
                InGroupUVE(DeclareRuntimeStateUVE<&Projectile3DComponentUVE::hitEntity>(
                               "hitEntity", "Hit Entity", kPropertyTypeEntityUVE),
                           "Result"),
                InGroupUVE(DeclareRuntimeStateUVE<&Projectile3DComponentUVE::hitPosition>(
                               "hitPosition", "Hit Position", kPropertyTypeVector3UVE),
                           "Result"),
                InGroupUVE(DeclareRuntimeStateUVE<&Projectile3DComponentUVE::hitNormal>(
                               "hitNormal", "Hit Normal", kPropertyTypeVector3UVE),
                           "Result"),
                InGroupUVE(DeclareRuntimeStateUVE<&Projectile3DComponentUVE::impactSpeed>(
                               "impactSpeed", "Impact Speed", kPropertyTypeFloatUVE),
                           "Result"),
                InGroupUVE(DeclareRuntimeStateUVE<&Projectile3DComponentUVE::bounceCount>(
                               "bounceCount", "Bounces", kPropertyTypeUInt32UVE),
                           "Result"),
            }));
}

void DeclareCombatUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    TypeMetadataPropertyUVE hitboxIgnore = WithTooltipUVE(
        DeclareUVE<&Hitbox3DComponentUVE::ignoreEntity>("ignoreEntity", "Ignore Entity", kPropertyTypeEntityUVE),
        "Never strike this entity. Set the owner so a weapon does not hit the character holding it.");
    hitboxIgnore.flags = TypeMetadataPropertyFlagsUVE::EntityReference;
    AddValidatedUVE<Hitbox3DComponentUVE, &IsHitbox3DObjectComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.hitbox_3d", "Hitbox3D", kSectionOrderTypeSpecificUVE,
            {
                WithTooltipUVE(
                    DeclareUVE<&Hitbox3DComponentUVE::enabled>("enabled", "Enabled", kPropertyTypeBoolUVE),
                    "Off, the hitbox strikes nothing and ends every strike it was in: the engine "
                    "reports the exit, it does not wait for the boxes to separate."),
                WithTooltipUVE(
                    DeclareUVE<&Hitbox3DComponentUVE::halfExtents>("halfExtents", "Half Extents",
                                                                   kPropertyTypeVector3UVE),
                    "The exact oriented box this hitbox strikes with: world position and "
                    "rotation plus these half-extents, with the object's world SCALE deliberately "
                    "not applied - the collider/area convention, so a scaled art pivot never "
                    "re-sizes a hurt volume."),
                WithTooltipUVE(
                    DeclareUVE<&Hitbox3DComponentUVE::damageChannel>("damageChannel", "Channel",
                                                                     kPropertyTypeStringUVE),
                    "What this hitbox is. It only ever strikes a hurtbox authored with the same "
                    "channel, which is how one character carries a sword hitbox and a punch hitbox "
                    "without either firing the other's reactions. Up to 256 bytes."),
                WithCustomDrawerUVE(
                    WithTooltipUVE(DeclareUVE<&Hitbox3DComponentUVE::collisionLayer>(
                                       "collisionLayer", "Layer", kPropertyTypeBitMask32UVE),
                                   "The layers this hitbox is on - what a hurtbox has to be "
                                   "looking for."),
                    std::string(kLayerMaskDrawerPhysicsUVE)),
                WithCustomDrawerUVE(
                    WithTooltipUVE(DeclareUVE<&Hitbox3DComponentUVE::collisionMask>(
                                       "collisionMask", "Mask", kPropertyTypeBitMask32UVE),
                                   "The layers this hitbox strikes. A strike needs BOTH sides to "
                                   "accept the other, so a hurtbox can refuse a whole class of "
                                   "attackers on its own."),
                    std::string(kLayerMaskDrawerPhysicsUVE)),
                std::move(hitboxIgnore),
                InGroupUVE(DeclareRuntimeStateUVE<&Hitbox3DComponentUVE::strikeCount>(
                               "strikeCount", "Strikes", kPropertyTypeUInt8UVE),
                           "Result"),
                InGroupUVE(DeclareRuntimeStateUVE<&Hitbox3DComponentUVE::strikesTruncated>(
                               "strikesTruncated", "Truncated", kPropertyTypeBoolUVE),
                           "Result"),
                InGroupUVE(DeclareRuntimeStateUVE<&Hitbox3DComponentUVE::struckCount>(
                               "struckCount", "Landed", kPropertyTypeUInt8UVE),
                           "Result"),
            }));

    TypeMetadataPropertyUVE hurtboxIgnore = WithTooltipUVE(
        DeclareUVE<&Hurtbox3DComponentUVE::ignoreEntity>("ignoreEntity", "Ignore Entity", kPropertyTypeEntityUVE),
        "Never accept a strike from this entity. Set the owner so a character is not hit by its own weapon.");
    hurtboxIgnore.flags = TypeMetadataPropertyFlagsUVE::EntityReference;
    AddValidatedUVE<Hurtbox3DComponentUVE, &IsHurtbox3DObjectComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.hurtbox_3d", "Hurtbox3D", kSectionOrderTypeSpecificUVE,
            {
                WithTooltipUVE(
                    DeclareUVE<&Hurtbox3DComponentUVE::enabled>("enabled", "Enabled", kPropertyTypeBoolUVE),
                    "Off, nothing can strike this hurtbox: it is not even a candidate, so every "
                    "strike it was in ends on this tick's report."),
                WithTooltipUVE(
                    DeclareUVE<&Hurtbox3DComponentUVE::halfExtents>("halfExtents", "Half Extents",
                                                                    kPropertyTypeVector3UVE),
                    "The exact oriented box this hurtbox is struck on, in world metres: world "
                    "position and rotation plus these half-extents, with the object's world SCALE "
                    "deliberately not applied - the collider/area convention."),
                WithTooltipUVE(
                    DeclareUVE<&Hurtbox3DComponentUVE::damageChannel>("damageChannel", "Channel",
                                                                      kPropertyTypeStringUVE),
                    "What this hurtbox receives. Only a hitbox authored with the same channel can "
                    "strike it. Up to 256 bytes."),
                WithCustomDrawerUVE(
                    WithTooltipUVE(DeclareUVE<&Hurtbox3DComponentUVE::collisionLayer>(
                                       "collisionLayer", "Layer", kPropertyTypeBitMask32UVE),
                                   "The layers this hurtbox is on - what a hitbox has to be looking "
                                   "for."),
                    std::string(kLayerMaskDrawerPhysicsUVE)),
                WithCustomDrawerUVE(
                    WithTooltipUVE(DeclareUVE<&Hurtbox3DComponentUVE::collisionMask>(
                                       "collisionMask", "Mask", kPropertyTypeBitMask32UVE),
                                   "The layers this hurtbox accepts strikes from. A strike needs "
                                   "BOTH sides to accept the other, which is how a hurtbox refuses "
                                   "a whole class of attackers without touching any of them."),
                    std::string(kLayerMaskDrawerPhysicsUVE)),
                std::move(hurtboxIgnore),
                InGroupUVE(DeclareRuntimeStateUVE<&Hurtbox3DComponentUVE::hitCount>(
                               "hitCount", "Hits", kPropertyTypeUInt8UVE),
                           "Result"),
                InGroupUVE(DeclareRuntimeStateUVE<&Hurtbox3DComponentUVE::hitsTruncated>(
                               "hitsTruncated", "Truncated", kPropertyTypeBoolUVE),
                           "Result"),
                InGroupUVE(DeclareRuntimeStateUVE<&Hurtbox3DComponentUVE::receivedCount>(
                               "receivedCount", "Received", kPropertyTypeUInt8UVE),
                           "Result"),
            }));
}

void DeclareAnimationUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    // AnimationSequencer's own section. Its target is an entity reference: flagged so the serializer
    // remaps it, and drawn as an object picker. Empty means the player's parent, which is the common
    // case and needs no picking at all.
    // AnimationDriver: the base AnimationSequencer and AnimationGraph share, shown between their own
    // section and the Object section. Its target is an entity reference: flagged so the serializer
    // remaps it, and drawn as an object picker. Empty means the parent, the common case.
    using M = AnimationDriverComponentUVE;
    TypeMetadataPropertyUVE mixerTarget = WithTooltipUVE(
        DeclareUVE<&M::target>("target", "Target", kPropertyTypeEntityUVE),
        "The object that is moved. Empty means this object's parent.");
    mixerTarget.flags = TypeMetadataPropertyFlagsUVE::EntityReference;
    AddValidatedUVE<AnimationDriverComponentUVE, &IsAnimationDriverComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.animation_mixer", std::string{AnimationDriverObjectDefinitionUVE::typeName}, kSectionOrderObjectBaseUVE + 20,
            {
                WithTooltipUVE(DeclareUVE<&M::active>("active", "Active", kPropertyTypeBoolUVE),
                               "Off, nothing is evaluated and the target is left alone."),
                std::move(mixerTarget),
                WithTooltipUVE(WithRangeUVE(DeclareUVE<&M::speedScale>("speedScale", "Speed Scale",
                                                                       kPropertyTypeFloatUVE),
                                            0.0, 100.0, 0.01),
                               "Multiplies every clock under this object: 0.5 is slow motion, 0 freezes."),
                WithTooltipUVE(DeclareEnumUVE<&M::processCallback>("processCallback", "Update",
                                                                   {{0, "Every Frame"}, {1, "Physics Step"}}),
                               "Every Frame is smoothest on screen. Physics Step keeps the target in step "
                               "with the bodies it pushes."),
                InGroupUVE(WithTooltipUVE(DeclareUVE<&M::animatePosition>("animatePosition", "Position",
                                                                          kPropertyTypeBoolUVE),
                                          "Off, the target's position is left alone."),
                           "Channels"),
                InGroupUVE(WithTooltipUVE(DeclareUVE<&M::animateRotation>("animateRotation", "Rotation",
                                                                          kPropertyTypeBoolUVE),
                                          "Off, the target's rotation is left alone."),
                           "Channels"),
                InGroupUVE(WithTooltipUVE(DeclareUVE<&M::animateScale>("animateScale", "Scale", kPropertyTypeBoolUVE),
                                          "Off, the target's scale is left alone."),
                           "Channels"),
                WithTooltipUVE(DeclareEnumUVE<&M::transition>("transition", "Transition",
                                                              {{0, "Inertialize"}, {1, "Crossfade"}}),
                               "How a clip that starts takes over, over the player's Blend In. Inertialize "
                               "plays the new clip at once and fades out the old pose's difference smoothly: "
                               "cheaper, and no sliding. Crossfade mixes the two."),
                InGroupUVE(WithTooltipUVE(DeclareEnumUVE<&M::rootMotion>("rootMotion", "Mode",
                                                                         {{0, "Off"}, {1, "In Place"},
                                                                          {2, "Apply To Target"}}),
                                          "Skeletal clips only. In Place takes the root bone's ground travel "
                                          "out of the pose. Apply To Target also moves the target by it; a "
                                          "Character3D gets it as velocity, so walls still stop it."),
                           "Root Motion"),
                InGroupUVE(WithTooltipUVE(DeclareUVE<&M::rootMotionBone>("rootMotionBone", "Bone",
                                                                         kPropertyTypeStringUVE),
                                          "The bone whose travel is root motion. Empty picks the first bone "
                                          "whose track moves across the ground (the root or the hips)."),
                           "Root Motion"),
            }));

    // AnimationSequencer's own section.
    using P = AnimationSequencerComponentUVE;
    const auto whenOnce = [](TypeMetadataPropertyUVE property) {
        property.isVisible = +[](const void* instance) {
            return static_cast<const P*>(instance)->loopMode == AnimationLoopModeUVE::Once;
        };
        return property;
    };
    AddValidatedUVE<AnimationSequencerComponentUVE, &IsAnimationSequencerComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.animation_player", "AnimationSequencer", kSectionOrderTypeSpecificUVE,
            {
                WithTooltipUVE(WithCustomDrawerUVE(DeclareUVE<&P::clip>("clip", "Clip", kPropertyTypeAssetGuidUVE),
                                                   "asset:uvanim"),
                               "The .uvanim clip to play."),
                WithTooltipUVE(DeclareUVE<&P::autoplay>("autoplay", "Autoplay", kPropertyTypeBoolUVE),
                               "Starts playing as soon as the scene runs."),
                InGroupUVE(WithTooltipUVE(WithRangeUVE(DeclareUVE<&P::speed>("speed", "Speed", kPropertyTypeFloatUVE),
                                                       -100.0, 100.0, 0.05),
                                          "1 is normal, 2 twice as fast, negative plays backwards, 0 holds."),
                           "Playback"),
                InGroupUVE(WithTooltipUVE(DeclareEnumUVE<&P::loopMode>("loopMode", "Loop Mode",
                                                                       {{0, "Once"}, {1, "Loop"}, {2, "Ping-Pong"}}),
                                          "Once stops at the end. Loop starts again. Ping-Pong turns round and plays "
                                          "back."),
                           "Playback"),
                InGroupUVE(whenOnce(WithTooltipUVE(DeclareEnumUVE<&P::onFinish>(
                                                       "onFinish", "On Finish",
                                                       {{0, "Hold Last Pose"}, {1, "Return To Start"}}),
                                                   "Where the target is left when the clip ends.")),
                           "Playback"),
                InGroupUVE(WithTooltipUVE(WithRangeUVE(DeclareUVE<&P::startOffsetSeconds>(
                                                           "startOffsetSeconds", "Start Offset", kPropertyTypeFloatUVE),
                                                       0.0, 3600.0, 0.01),
                                          "Seconds into the clip where playback starts. Offsetting copies of one "
                                          "clip keeps a crowd from moving in lockstep."),
                           "Playback"),
                InGroupUVE(WithTooltipUVE(WithRangeUVE(DeclareUVE<&P::blendInSeconds>(
                                                           "blendInSeconds", "Blend In", kPropertyTypeFloatUVE),
                                                       0.0, 60.0, 0.01),
                                          "Eases from where the target is into the clip over this many seconds, "
                                          "instead of snapping. 0 snaps."),
                           "Blending"),
                InGroupUVE(WithTooltipUVE(DeclareUVE<&P::relative>("relative", "Relative", kPropertyTypeBoolUVE),
                                          "Plays the clip's motion on top of where the target already is, so one "
                                          "clip works on any object wherever it was placed."),
                           "Blending"),
                InGroupUVE(DeclareRuntimeStateUVE<&P::isPlaying>("isPlaying", "Playing", kPropertyTypeBoolUVE), "State"),
                InGroupUVE(DeclareRuntimeStateUVE<&P::currentTimeSeconds>("currentTimeSeconds", "Time",
                                                                         kPropertyTypeFloatUVE),
                           "State"),
                InGroupUVE(DeclareRuntimeStateUVE<&P::finished>("finished", "Finished", kPropertyTypeBoolUVE), "State"),
            }));

    using T = AnimationGraphComponentUVE;
    // The parameters and the graph are lists with their own add, remove and wiring, which a
    // property row cannot express, so each is one custom-drawn block.
    AddValidatedUVE<AnimationGraphComponentUVE, &IsAnimationGraphComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.animation_tree", "AnimationGraph", kSectionOrderTypeSpecificUVE,
            {
                InGroupUVE(WithCustomDrawerUVE(DeclareUVE<&T::parameters>("parameters", "Parameters",
                                                                         "AnimationParameterList"),
                                               "animation-parameters"),
                           "Parameters"),
                InGroupUVE(WithCustomDrawerUVE(DeclareUVE<&T::nodes>("objects", "Graph", "AnimationGraphObjectList"),
                                               "animation-graph"),
                           "Graph"),
                InGroupUVE(DeclareRuntimeStateUVE<&T::activeStates>("activeStates", "Active States",
                                                                    kPropertyTypeStringUVE),
                           "State"),
            }));
}

void DeclareMediaAndUIUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    AddUVE<AudioSourceComponentUVE>(
        entries,
        MakeEntryUVE(
            "component.audio_source", "Audio Source", kSectionOrderTypeSpecificUVE,
            {
                DeclareUVE<&AudioSourceComponentUVE::audioAssetPath>("audioAssetPath", "Clip",
                                                                     kPropertyTypeStringUVE),
                DeclareUVE<&AudioSourceComponentUVE::mixerGroup>("mixerGroup", "Mixer Group",
                                                                 kPropertyTypeStringUVE),
                WithRangeUVE(DeclareUVE<&AudioSourceComponentUVE::volume>("volume", "Volume",
                                                                           kPropertyTypeFloatUVE),
                             0.0, 1.0, 0.01),
                WithRangeUVE(DeclareUVE<&AudioSourceComponentUVE::pitch>("pitch", "Pitch",
                                                                          kPropertyTypeFloatUVE),
                             0.01, 4.0, 0.01),
                DeclareUVE<&AudioSourceComponentUVE::looping>("looping", "Looping", kPropertyTypeBoolUVE),
                DeclareUVE<&AudioSourceComponentUVE::playOnAwake>("playOnAwake", "Play On Awake",
                                                                  kPropertyTypeBoolUVE),
                DeclareUVE<&AudioSourceComponentUVE::spatial>("spatial", "Spatial",
                                                              kPropertyTypeBoolUVE),
                [] {
                    TypeMetadataPropertyUVE property = WithRangeUVE(
                        DeclareUVE<&AudioSourceComponentUVE::minDistance>("minDistance", "Min Distance",
                                                                          kPropertyTypeFloatUVE),
                        0.0, 10000.0, 0.1);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const AudioSourceComponentUVE*>(instance)->spatial;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataPropertyUVE property = WithRangeUVE(
                        DeclareUVE<&AudioSourceComponentUVE::maxDistance>("maxDistance", "Max Distance",
                                                                          kPropertyTypeFloatUVE),
                        0.0, 10000.0, 0.1);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const AudioSourceComponentUVE*>(instance)->spatial;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataPropertyUVE property =
                        DeclareEnumUVE<&AudioSourceComponentUVE::attenuationCurve>(
                            "attenuationCurve", "Attenuation", {{0, "Linear"}, {1, "Inverse Square"}});
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const AudioSourceComponentUVE*>(instance)->spatial;
                    };
                    return property;
                }(),
            }));

    DeclareAnimationUVE(entries);

    AddUVE<CanvasComponentUVE>(
        entries,
        MakeEntryUVE("component.canvas", "Canvas", kSectionOrderTypeSpecificUVE,
                     {
                         DeclareUVE<&CanvasComponentUVE::visible>("visible", "Visible",
                                                                  kPropertyTypeBoolUVE),
                         DeclareUVE<&CanvasComponentUVE::sortOrder>("sortOrder", "Sort Order",
                                                                    kPropertyTypeInt32UVE),
                     }));

    AddUVE<UITextComponentUVE>(
        entries,
        MakeEntryUVE("component.ui_text", "UI Text", kSectionOrderTypeSpecificUVE,
                     {
                         DeclareUVE<&UITextComponentUVE::text>("text", "Text", kPropertyTypeStringUVE),
                         DeclareUVE<&UITextComponentUVE::positionPixels>("positionPixels", "Position",
                                                                         kPropertyTypeVector2UVE),
                         WithRangeUVE(DeclareUVE<&UITextComponentUVE::fontSize>("fontSize", "Font Size",
                                                                                kPropertyTypeFloatUVE),
                                      1.0, 512.0, 1.0),
                         DeclareUVE<&UITextComponentUVE::color>("color", "Color", kPropertyTypeColorUVE),
                         WithRangeUVE(DeclareUVE<&UITextComponentUVE::alpha>("alpha", "Alpha",
                                                                             kPropertyTypeFloatUVE),
                                      0.0, 1.0, 0.01),
                     }));

    AddUVE<UIImageComponentUVE>(
        entries,
        MakeEntryUVE("component.ui_image", "UI Image", kSectionOrderTypeSpecificUVE,
                     {
                         DeclareUVE<&UIImageComponentUVE::textureAssetGuid>("textureAssetGuid", "Texture",
                                                                            kPropertyTypeAssetGuidUVE),
                         DeclareUVE<&UIImageComponentUVE::positionPixels>("positionPixels", "Position",
                                                                          kPropertyTypeVector2UVE),
                         DeclareUVE<&UIImageComponentUVE::sizePixels>("sizePixels", "Size",
                                                                      kPropertyTypeVector2UVE),
                         DeclareUVE<&UIImageComponentUVE::tintColor>("tintColor", "Tint",
                                                                     kPropertyTypeColorUVE),
                         WithRangeUVE(DeclareUVE<&UIImageComponentUVE::alpha>("alpha", "Alpha",
                                                                              kPropertyTypeFloatUVE),
                                      0.0, 1.0, 0.01),
                     }));

    AddUVE<UIButtonComponentUVE>(
        entries,
        MakeEntryUVE("component.ui_button", "UI Button", kSectionOrderTypeSpecificUVE,
                     {
                         DeclareUVE<&UIButtonComponentUVE::positionPixels>("positionPixels", "Position",
                                                                           kPropertyTypeVector2UVE),
                         DeclareUVE<&UIButtonComponentUVE::sizePixels>("sizePixels", "Size",
                                                                       kPropertyTypeVector2UVE),
                         DeclareUVE<&UIButtonComponentUVE::normalColor>("normalColor", "Normal",
                                                                        kPropertyTypeColorUVE),
                         DeclareUVE<&UIButtonComponentUVE::hoverColor>("hoverColor", "Hover",
                                                                       kPropertyTypeColorUVE),
                         DeclareUVE<&UIButtonComponentUVE::pressedColor>("pressedColor", "Pressed",
                                                                         kPropertyTypeColorUVE),
                         DeclareRuntimeStateUVE<&UIButtonComponentUVE::isHovered>(
                             "isHovered", "Hovered", kPropertyTypeBoolUVE),
                         DeclareRuntimeStateUVE<&UIButtonComponentUVE::wasClickedThisFrame>(
                             "wasClickedThisFrame", "Clicked This Frame", kPropertyTypeBoolUVE),
                     }));
}

/// Links an authored Inherit-style choice to the runtime property holding what it resolved to, so
/// the Inspector can show the answer beside the choice rather than as a row of its own.
[[nodiscard]] TypeMetadataPropertyUVE ResolvedByUVE(TypeMetadataPropertyUVE property, std::string resolvedProperty) {
    property.resolvedByProperty = std::move(resolvedProperty);
    return property;
}

[[nodiscard]] TypeMetadataPropertyUVE HiddenUVE(TypeMetadataPropertyUVE property) {
    property.flags = TypeMetadataPropertyFlagsUVE::Hidden;
    return property;
}

/// The common Object section: what every object has regardless of what it is. These sort last, below
/// whatever the object itself brings, which is where an author expects them - and in a fixed order
/// among themselves, because an author finds a setting by where it was last time.
/// The abstract 3D bases. Their sections appear on every concrete child, between what the child
/// itself brings and the common Object section.
// The gameplay-owned authored data: what the scene means to a game rather than to the renderer or
// the solver. SpawnPoint3D is the first citizen here (Unreal's PlayerStart role - Godot ships no
// built-in counterpart), and the section is deliberately declarative: the query that consumes it
// lives in uve_scene, and nothing in this file decides when a point fires.
void DeclareGameplayUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    AddValidatedUVE<PlayerComponentUVE, &IsPlayer3DObjectComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.player", "Player3D", kSectionOrderTypeSpecificUVE,
            {
                WithTooltipUVE(DeclareUVE<&PlayerComponentUVE::possessOnPlay>(
                                   "possessOnPlay", "Possess On Play", kPropertyTypeBoolUVE),
                               "This body receives move, look, jump and interact. Off, it is an NPC."),
                WithTooltipUVE(DeclareUVE<&PlayerComponentUVE::lookEnabled>("lookEnabled", "Look",
                                                                            kPropertyTypeBoolUVE),
                               "Mouse and look-stick rotate this body (yaw) and its camera or spring arm (pitch)."),
                WithRangeUVE(WithTooltipUVE(DeclareUVE<&PlayerComponentUVE::lookSensitivity>(
                                                "lookSensitivity", "Look Sensitivity", kPropertyTypeFloatUVE),
                                            "Degrees per mouse pixel."),
                             0.0, 10.0, 0.01),
                WithRangeUVE(WithTooltipUVE(DeclareUVE<&PlayerComponentUVE::lookStickSpeedDegrees>(
                                                "lookStickSpeedDegrees", "Stick Look Speed",
                                                kPropertyTypeFloatUVE),
                                            "Degrees per second at full stick."),
                             0.0, 720.0, 1.0),
                WithRangeUVE(WithTooltipUVE(DeclareUVE<&PlayerComponentUVE::minPitchDegrees>(
                                                "minPitchDegrees", "Min Pitch", kPropertyTypeFloatUVE),
                                            "Lowest look pitch, in degrees."),
                             -89.0, 89.0, 1.0),
                WithRangeUVE(WithTooltipUVE(DeclareUVE<&PlayerComponentUVE::maxPitchDegrees>(
                                                "maxPitchDegrees", "Max Pitch", kPropertyTypeFloatUVE),
                                            "Highest look pitch, in degrees."),
                             -89.0, 89.0, 1.0),
                InGroupUVE(DeclareRuntimeStateUVE<&PlayerComponentUVE::pitchDegrees>(
                               "pitchDegrees", "Pitch", kPropertyTypeFloatUVE),
                           "State"),
            }));

    AddValidatedUVE<HealthComponentUVE, &IsHealthComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.health", "Health", kSectionOrderTypeSpecificUVE,
            {
                WithRangeUVE(WithTooltipUVE(DeclareUVE<&HealthComponentUVE::maxHealth>(
                                                "maxHealth", "Max Health", kPropertyTypeFloatUVE),
                                            "Hit points at spawn."),
                             1.0, 10000.0, 1.0),
                WithTooltipUVE(DeclareUVE<&HealthComponentUVE::invulnerable>("invulnerable", "Invulnerable",
                                                                            kPropertyTypeBoolUVE),
                               "Strikes do not reduce health."),
                InGroupUVE(DeclareRuntimeStateUVE<&HealthComponentUVE::health>("health", "Health",
                                                                              kPropertyTypeFloatUVE),
                           "State"),
            }));

    AddValidatedUVE<SpawnPoint3DComponentUVE, &IsSpawnPoint3DObjectComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.spawn_point", "SpawnPoint3D", kSectionOrderTypeSpecificUVE,
            {
                WithTooltipUVE(
                    DeclareUVE<&SpawnPoint3DComponentUVE::spawnTag>("spawnTag", "Tag",
                                                                    kPropertyTypeStringUVE),
                    "What this point is for. An empty-tag query takes the first live point whatever "
                    "it is called; a respawn screen asks for \"checkpoint\" and a second level "
                    "entry asks for \"level start\"."),
                WithTooltipUVE(
                    DeclareUVE<&SpawnPoint3DComponentUVE::localPosition>(
                        "localPosition", "Offset Position", kPropertyTypeVector3UVE),
                    "Where the actor appears relative to this point's own pose, in metres: rotated "
                    "by the point's rotation, deliberately NOT scaled by its scale - an offset is a "
                    "distance, not a volume. A point can sit on a floor seam with its actor a step "
                    "above."),
                WithTooltipUVE(
                    DeclareUVE<&SpawnPoint3DComponentUVE::localRotation>(
                        "localRotation", "Offset Rotation", kPropertyTypeQuaternionUVE),
                    "Which way the actor faces when it appears, composed onto this point's own "
                    "rotation."),
                WithTooltipUVE(DeclareUVE<&SpawnPoint3DComponentUVE::enabled>("enabled", "Enabled",
                                                                             kPropertyTypeBoolUVE),
                               "Off, the point is invisible to every spawn query - which is also "
                               "the state a spent one-shot point is left in."),
                WithTooltipUVE(
                    DeclareUVE<&SpawnPoint3DComponentUVE::oneShot>("oneShot", "One Shot",
                                                                   kPropertyTypeBoolUVE),
                    "A checkpoint: the first caller that actually spawns from this point spends "
                    "it, and no later query can use it again this session. The authored value "
                    "comes back when Play is left, like every other sandboxed edit."),
            }));

    TypeMetadataPropertyUVE areaIgnore = WithTooltipUVE(
        DeclareUVE<&InteractionArea3DComponentUVE::ignoreEntity>("ignoreEntity", "Ignore Entity",
                                                                 kPropertyTypeEntityUVE),
        "Never list this interactor. Set the owner so a character does not interact with a volume it carries.");
    areaIgnore.flags = TypeMetadataPropertyFlagsUVE::EntityReference;
    AddValidatedUVE<InteractionArea3DComponentUVE, &IsInteractionArea3DObjectComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.interaction_area_3d", "InteractionArea3D", kSectionOrderTypeSpecificUVE,
            {
                WithTooltipUVE(
                    DeclareUVE<&InteractionArea3DComponentUVE::enabled>("enabled", "Enabled", kPropertyTypeBoolUVE),
                    "Off, the area tracks nobody and drops focus this tick."),
                WithTooltipUVE(
                    DeclareUVE<&InteractionArea3DComponentUVE::halfExtents>("halfExtents", "Half Extents",
                                                                           kPropertyTypeVector3UVE),
                    "The exact oriented box, world position and rotation plus these half-extents. "
                    "World SCALE is not applied."),
                WithTooltipUVE(
                    DeclareUVE<&InteractionArea3DComponentUVE::interactionTag>("interactionTag", "Tag",
                                                                              kPropertyTypeStringUVE),
                    "Carried for gameplay. The scan does not filter on it."),
                WithRangeUVE(
                    WithTooltipUVE(DeclareUVE<&InteractionArea3DComponentUVE::maximumCandidates>(
                                       "maximumCandidates", "Max Interactors", kPropertyTypeUInt32UVE),
                                   "How many overlapping interactors this area stores. Overflow is flagged, "
                                   "not dropped silently. 1 to 4096; storage itself caps at 16."),
                    1.0, 4096.0, 1.0),
                WithCustomDrawerUVE(
                    WithTooltipUVE(DeclareUVE<&InteractionArea3DComponentUVE::collisionLayer>(
                                       "collisionLayer", "Layer", kPropertyTypeBitMask32UVE),
                                   "The layers this area is on."),
                    std::string(kLayerMaskDrawerPhysicsUVE)),
                WithCustomDrawerUVE(
                    WithTooltipUVE(DeclareUVE<&InteractionArea3DComponentUVE::collisionMask>(
                                       "collisionMask", "Mask", kPropertyTypeBitMask32UVE),
                                   "The layers this area accepts interactors from. Both sides must agree."),
                    std::string(kLayerMaskDrawerPhysicsUVE)),
                std::move(areaIgnore),
                InGroupUVE(DeclareRuntimeStateUVE<&InteractionArea3DComponentUVE::interactorCount>(
                               "interactorCount", "Interactors", kPropertyTypeUInt8UVE),
                           "Result"),
                InGroupUVE(DeclareRuntimeStateUVE<&InteractionArea3DComponentUVE::interactorsTruncated>(
                               "interactorsTruncated", "Truncated", kPropertyTypeBoolUVE),
                           "Result"),
                InGroupUVE(DeclareRuntimeStateUVE<&InteractionArea3DComponentUVE::focusedByPrimaryInteractor>(
                               "focusedByPrimaryInteractor", "Focused", kPropertyTypeBoolUVE),
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
void DeclareNavigationUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    using R = NavMeshVolume3DComponentUVE;
    AddValidatedUVE<NavMeshVolume3DComponentUVE, &IsNavMeshVolume3DObjectComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.nav_mesh_volume_3d", "NavMeshVolume3D", kSectionOrderTypeSpecificUVE,
            {
                WithTooltipUVE(DeclareUVE<&R::enabled>("enabled", "Enabled", kPropertyTypeBoolUVE),
                               "Off, the region has no mesh at all: an agent standing on it fails "
                               "rather than walks ground the author just took away."),
                WithTooltipUVE(
                    WithRangeUVE(DeclareUVE<&R::boundsHalfExtents>("boundsHalfExtents", "Size",
                                                                   kPropertyTypeVector3UVE),
                                 0.001, 100000.0, 0.01),
                    "The volume baked into a navmesh, centred on the object and aligned to the world "
                    "axes - the bake's grid is built on the world axes, so where the object is "
                    "rotated, its unrotated volume is what gets rasterized."),
                WithTooltipUVE(
                    WithRangeUVE(DeclareUVE<&R::cellSize>("cellSize", "Cell Size", kPropertyTypeFloatUVE),
                                 0.05, 10.0, 0.05),
                    "The rasterization grid, in metres. Finer follows geometry more closely and costs "
                    "more rays; a grid too large for the bake's cell budget is made coarser and says "
                    "so in the bake's report."),
                WithTooltipUVE(
                    WithRangeUVE(DeclareUVE<&R::agentRadius>("agentRadius", "Agent Radius",
                                                             kPropertyTypeFloatUVE),
                                 0.0, 100.0, 0.01),
                    "Ground closer than this to a wall, a ledge or the region's edge is eroded away: "
                    "an agent of this width cannot stand there with its body on the mesh."),
                WithTooltipUVE(
                    WithRangeUVE(DeclareUVE<&R::agentHeight>("agentHeight", "Agent Height",
                                                             kPropertyTypeFloatUVE),
                                 0.01, 1000.0, 0.01),
                    "How much headroom the ground needs to be walkable. A region shorter than this "
                    "bakes nothing, which is the honest answer for a crawlspace no agent fits in."),
                WithTooltipUVE(
                    WithRangeUVE(DeclareUVE<&R::maximumSlopeDegrees>("maximumSlopeDegrees", "Max Slope",
                                                                     kPropertyTypeFloatUVE),
                                 0.1, 89.9, 0.1),
                    "The steepest surface that still counts as ground, in degrees from up. Anything "
                    "steeper is a wall as far as this navmesh is concerned."),
                WithTooltipUVE(
                    WithRangeUVE(DeclareUVE<&R::maximumStepHeight>("maximumStepHeight", "Max Step",
                                                                   kPropertyTypeFloatUVE),
                                 0.0, 1000.0, 0.01),
                    "The tallest rise between neighbouring cells that may still be walked, in "
                    "metres. Taller than this and they are two floors rather than one step."),
                WithCustomDrawerUVE(
                    WithTooltipUVE(DeclareUVE<&R::navigationLayers>("navigationLayers", "Layers",
                                                                    kPropertyTypeBitMask32UVE),
                                   "The layers written into the baked polygons, matched against an "
                                   "agent's own mask: a path may only use polygons whose layers "
                                   "intersect it."),
                    std::string(kLayerMaskDrawerPhysicsUVE)),
                WithTooltipUVE(
                    DeclareUVE<&R::rebuildRequested>("rebuildRequested", "Rebuild", kPropertyTypeBoolUVE),
                    "Raise it to re-rasterize a region whose volume did not move but whose "
                    "surroundings did. The navigation step bakes and then clears it."),
            }));

    using S = NavSeeker3DComponentUVE;
    AddValidatedUVE<NavSeeker3DComponentUVE, &IsNavSeeker3DObjectComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.nav_seeker_3d", "NavSeeker3D", kSectionOrderTypeSpecificUVE,
            {
                WithTooltipUVE(DeclareUVE<&S::enabled>("enabled", "Enabled", kPropertyTypeBoolUVE),
                               "Off, the agent publishes nothing - no velocity, no route - and "
                               "forgets the route it had."),
                WithTooltipUVE(DeclareUVE<&S::targetPosition>("targetPosition", "Target",
                                                              kPropertyTypeVector3UVE),
                               "Where the agent is walking to, in world space. Changing it drops "
                               "the current route and searches on the next step, whatever the "
                               "update interval says."),
                WithTooltipUVE(
                    DeclareUVE<&S::avoidanceEnabled>("avoidanceEnabled", "Avoidance",
                                                     kPropertyTypeBoolUVE),
                    "On, other agents inside the avoidance radius push this one aside so two "
                    "agents on one route end up beside each other rather than inside each other."),
                WhenOnUVE<&S::avoidanceEnabled>(WithRangeUVE(
                    WithTooltipUVE(DeclareUVE<&S::avoidanceRadius>("avoidanceRadius", "Avoid Radius",
                                                                   kPropertyTypeFloatUVE),
                                   "How close another agent's centre may come before it pushes, in "
                                   "metres. 0 is the same as switching avoidance off."),
                    0.0, 1000.0, 0.01)),
                InGroupUVE(WithRangeUVE(DeclareUVE<&S::radius>("radius", "Radius", kPropertyTypeFloatUVE),
                                        0.01, 1000.0, 0.01),
                           "Agent"),
                InGroupUVE(WithRangeUVE(DeclareUVE<&S::height>("height", "Height", kPropertyTypeFloatUVE),
                                        0.02, 1000.0, 0.01),
                           "Agent"),
                InGroupUVE(WithTooltipUVE(
                               WithRangeUVE(DeclareUVE<&S::maxSpeed>("maxSpeed", "Max Speed",
                                                                     kPropertyTypeFloatUVE),
                                            0.0, 10000.0, 0.1),
                               "Top speed on a straight leg, in metres per second. What actually "
                               "moves the body is the mover or script reading this agent's "
                               "desiredVelocity."),
                           "Agent"),
                InGroupUVE(WithTooltipUVE(
                               WithRangeUVE(DeclareUVE<&S::acceleration>("acceleration", "Acceleration",
                                                                         kPropertyTypeFloatUVE),
                                            0.0, 10000.0, 0.1),
                               "How hard desiredVelocity may change per second. The velocity is what "
                               "a mover is handed, and an unbounded step change is a jolt; 0 "
                               "publishes the wanted velocity immediately."),
                           "Agent"),
                InGroupUVE(WithTooltipUVE(
                               WithRangeUVE(DeclareUVE<&S::targetTolerance>("targetTolerance", "Target "
                                                                                           "Tolerance",
                                                                            kPropertyTypeFloatUVE),
                                            0.0, 1000.0, 0.01),
                               "How close to the target counts as arrived. Larger than the waypoint "
                               "radius on purpose: an agent that had to stop within a metre of a "
                               "moving target would hunt for it."),
                           "Agent"),
                InGroupUVE(WithTooltipUVE(
                               WithRangeUVE(DeclareUVE<&S::waypointRadius>("waypointRadius", "Waypoint "
                                                                                           "Radius",
                                                                           kPropertyTypeFloatUVE),
                                            0.0, 1000.0, 0.01),
                               "How close to a waypoint counts as reached, so the agent turns to the "
                               "next one instead of walking back for it."),
                           "Agent"),
                InGroupUVE(WithTooltipUVE(
                               WithRangeUVE(DeclareUVE<&S::slowDownRadius>("slowDownRadius", "Slow Down "
                                                                                           "Radius",
                                                                           kPropertyTypeFloatUVE),
                                            0.0, 1000.0, 0.01),
                               "Inside this distance from the target the speed is scaled down, so the "
                               "agent arrives rather than overshoots. 0 keeps full speed until the "
                               "tolerance stops it."),
                           "Agent"),
                InGroupUVE(WithCustomDrawerUVE(
                               WithTooltipUVE(DeclareUVE<&S::navigationLayers>("navigationLayers",
                                                                               "Layers",
                                                                               kPropertyTypeBitMask32UVE),
                                              "Which polygons this agent may walk: a path may only "
                                              "cross ground whose layers intersect this mask."),
                               std::string(kLayerMaskDrawerPhysicsUVE)),
                           "Agent"),
                InGroupUVE(WithTooltipUVE(
                               WithRangeUVE(DeclareUVE<&S::pathUpdateInterval>("pathUpdateInterval",
                                                                               "Update Interval",
                                                                               kPropertyTypeFloatUVE),
                                            0.01, 10.0, 0.01),
                               "How often the route is reconsidered, in seconds. Replanning every "
                               "frame would be correct and wasteful; replanning rarely leaves an "
                               "agent walking into a door that closed behind it."),
                           "Route"),
                // The route the last step produced. Runtime state: written by the navigation step,
                // never authored, never saved - a loaded agent finds its own way from its target.
                InGroupUVE(DeclareRuntimeStateUVE<&S::nextPathPosition>("nextPathPosition",
                                                                        "Next Waypoint",
                                                                        kPropertyTypeVector3UVE),
                           "Route"),
                InGroupUVE(DeclareRuntimeStateUVE<&S::desiredVelocity>("desiredVelocity",
                                                                       "Desired Velocity",
                                                                       kPropertyTypeVector3UVE),
                           "Route"),
                InGroupUVE(DeclareRuntimeStateEnumUVE<&S::pathStatus>(
                               "pathStatus", "Status",
                               {{0, "Idle"}, {1, "Following"}, {2, "Finished"}, {3, "Failed"}}),
                           "Route"),
                InGroupUVE(DeclareRuntimeStateUVE<&S::pathChanged>("pathChanged", "Path Changed",
                                                                   kPropertyTypeBoolUVE),
                           "Route"),
                InGroupUVE(DeclareRuntimeStateUVE<&S::targetReached>("targetReached", "Target Reached",
                                                                     kPropertyTypeBoolUVE),
                           "Route"),
            }));
}

void DeclareObjectBasesUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    AddUVE<BoneModifierComponentUVE>(
        entries,
        MakeEntryUVE("component.bone_modifier", std::string{BoneModifier3DObjectDefinitionUVE::typeName}, kSectionOrderObjectBaseUVE,
                     {
                         WithTooltipUVE(DeclareUVE<&BoneModifierComponentUVE::active>("active", "Active",
                                                                                        kPropertyTypeBoolUVE),
                                        "Off skips this modifier, as if it were not there."),
                         WithTooltipUVE(WithRangeUVE(DeclareUVE<&BoneModifierComponentUVE::influence>(
                                                         "influence", "Influence", kPropertyTypeFloatUVE),
                                                     0.0, 1.0, 0.01),
                                        "How much of the modifier's result is blended over the pose."),
                     }));

    TypeMetadataPropertyUVE priority = WithTooltipUVE(
        DeclareUVE<&PhysicsObjectComponentUVE::collisionPriority>("collisionPriority", "Priority",
                                                                  kPropertyTypeFloatUVE),
        "How strongly this object is pushed out of an overlap; higher yields less.");
    priority.range = {true, 0.0, 1000000.0, 0.1};
    AddUVE<PhysicsObjectComponentUVE>(
        entries,
        MakeEntryUVE(
            "component.physics_object", std::string{PhysicsObject3DObjectDefinitionUVE::typeName}, kSectionOrderObjectBaseUVE + 1,
            {
                WithTooltipUVE(DeclareEnumUVE<&PhysicsObjectComponentUVE::disableMode>(
                                   "disableMode", "Disable Mode",
                                   {{0, "Remove"}, {1, "Make Static"}, {2, "Keep Active"}}),
                               "What happens to this object while its Process mode stops it."),
                std::move(priority),
                WithTooltipUVE(DeclareUVE<&PhysicsObjectComponentUVE::inputRayPickable>(
                                   "inputRayPickable", "Ray Pickable", kPropertyTypeBoolUVE),
                               "Whether a mouse or touch pick can hit this object."),
                WithTooltipUVE(DeclareUVE<&PhysicsObjectComponentUVE::inputCaptureOnDrag>(
                                   "inputCaptureOnDrag", "Capture On Drag", kPropertyTypeBoolUVE),
                               "Whether a drag that started here keeps reporting here after leaving."),
            }));

    // Sorts before PhysicsObject3D: a base that derives from another is drawn above it.
    AddUVE<SolidBodyComponentUVE>(
        entries,
        MakeEntryUVE("component.solid_body", std::string{SolidBody3DObjectDefinitionUVE::typeName}, kSectionOrderObjectBaseUVE,
                     {
                         InGroupUVE(WithTooltipUVE(DeclareUVE<&SolidBodyComponentUVE::lockMotionX>(
                                                       "lockMotionX", "X", kPropertyTypeBoolUVE),
                                                   "Never moves along the world X axis."),
                                    "Lock Motion"),
                         InGroupUVE(WithTooltipUVE(DeclareUVE<&SolidBodyComponentUVE::lockMotionY>(
                                                       "lockMotionY", "Y", kPropertyTypeBoolUVE),
                                                   "Never moves along the world Y axis."),
                                    "Lock Motion"),
                         InGroupUVE(WithTooltipUVE(DeclareUVE<&SolidBodyComponentUVE::lockMotionZ>(
                                                       "lockMotionZ", "Z", kPropertyTypeBoolUVE),
                                                   "Never moves along the world Z axis - a side view locks this one."),
                                    "Lock Motion"),
                     }));

    AddUVE<RenderInstanceComponentUVE>(
        entries,
        MakeEntryUVE("component.render_instance", std::string{RenderInstance3DObjectDefinitionUVE::typeName}, kSectionOrderObjectBaseUVE + 10,
                     {
                         WithCustomDrawerUVE(
                             WithTooltipUVE(DeclareUVE<&RenderInstanceComponentUVE::renderLayers>(
                                                "renderLayers", "Layers", kPropertyTypeBitMask32UVE),
                                            "The render layers this is on. A camera draws it only when their layers overlap."),
                             std::string(kLayerMaskDrawerRenderUVE)),
                         WithTooltipUVE(DeclareUVE<&RenderInstanceComponentUVE::sortingOffset>(
                                            "sortingOffset", "Sorting Offset", kPropertyTypeFloatUVE),
                                        "Moves this forward (negative) or back in transparent sorting, without moving it."),
                         WithTooltipUVE(DeclareUVE<&RenderInstanceComponentUVE::sortingUseAabbCenter>(
                                            "sortingUseAabbCenter", "Sort By Bounds Center", kPropertyTypeBoolUVE),
                                        "Sort by the centre of the bounds rather than the origin."),
                     }));

    using S = SurfaceInstanceComponentUVE;
    AddValidatedUVE<SurfaceInstanceComponentUVE, &IsSurfaceInstanceComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.surface_instance", std::string{SurfaceInstance3DObjectDefinitionUVE::typeName}, kSectionOrderObjectBaseUVE + 3,
            {
                WithTooltipUVE(DeclareUVE<&S::materialOverridePath>("materialOverridePath", "Override",
                                                                    kPropertyTypeStringUVE),
                               "Material override: used on every surface in place of the mesh's own. Empty keeps them."),
                WithTooltipUVE(DeclareUVE<&S::materialOverlayPath>("materialOverlayPath", "Overlay",
                                                                   kPropertyTypeStringUVE),
                               "Material overlay: drawn over every surface, on top of whatever it already shows."),
                WithTooltipUVE(WithRangeUVE(DeclareUVE<&S::transparency>("transparency", "Transparency",
                                                                         kPropertyTypeFloatUVE),
                                            0.0, 1.0, 0.01),
                               "Fades the whole instance out: 0 as authored, 1 invisible."),
                InGroupUVE(WithTooltipUVE(DeclareEnumUVE<&S::castShadow>(
                                              "castShadow", "Cast Shadow",
                                              {{0, "Off"}, {1, "On"}, {2, "Double Sided"}, {3, "Shadows Only"}}),
                                          "Whether it casts a shadow, and Shadows Only for an invisible caster."),
                           "Shadow"),
                InGroupUVE(WithTooltipUVE(DeclareEnumUVE<&S::lightingMode>(
                                              "lightingMode", "Lighting",
                                              {{0, "Disabled"}, {1, "Static (Baked)"}, {2, "Dynamic"}}),
                                          "How baked lighting treats it: ignored, baked into, or lit at runtime."),
                           "Global Illumination"),
                InGroupUVE(WithTooltipUVE(WithRangeUVE(DeclareUVE<&S::visibilityRangeBegin>(
                                                           "visibilityRangeBegin", "Begin", kPropertyTypeFloatUVE),
                                                       0.0, 1000000.0, 0.1),
                                          "Hidden closer to the camera than this. 0 never hides it up close."),
                           "Visibility Range"),
                InGroupUVE(WithRangeUVE(DeclareUVE<&S::visibilityRangeBeginMargin>(
                                            "visibilityRangeBeginMargin", "Begin Margin", kPropertyTypeFloatUVE),
                                        0.0, 1000000.0, 0.1),
                           "Visibility Range"),
                InGroupUVE(WithTooltipUVE(WithRangeUVE(DeclareUVE<&S::visibilityRangeEnd>(
                                                           "visibilityRangeEnd", "End", kPropertyTypeFloatUVE),
                                                       0.0, 1000000.0, 0.1),
                                          "Hidden farther from the camera than this. 0 never hides it far away."),
                           "Visibility Range"),
                InGroupUVE(WithRangeUVE(DeclareUVE<&S::visibilityRangeEndMargin>(
                                            "visibilityRangeEndMargin", "End Margin", kPropertyTypeFloatUVE),
                                        0.0, 1000000.0, 0.1),
                           "Visibility Range"),
                InGroupUVE(WithTooltipUVE(DeclareEnumUVE<&S::visibilityRangeFadeMode>(
                                              "visibilityRangeFadeMode", "Fade",
                                              {{0, "Disabled"}, {1, "Self"}, {2, "Dependencies"}}),
                                          "Fade across the margins instead of popping."),
                           "Visibility Range"),
                InGroupUVE(WithTooltipUVE(WithRangeUVE(DeclareUVE<&S::extraCullMargin>(
                                                           "extraCullMargin", "Extra Cull Margin", kPropertyTypeFloatUVE),
                                                       0.0, 100000.0, 0.01),
                                          "Grows the bounds used for culling, for shaders that move vertices outward."),
                           "Culling"),
                InGroupUVE(WithTooltipUVE(WithRangeUVE(DeclareUVE<&S::lodBias>("lodBias", "LOD Bias",
                                                                               kPropertyTypeFloatUVE),
                                                       0.001, 128.0, 0.01),
                                          "Above 1 keeps detailed levels longer; below 1 drops them sooner."),
                           "Culling"),
                InGroupUVE(WithTooltipUVE(DeclareUVE<&S::ignoreOcclusionCulling>(
                                              "ignoreOcclusionCulling", "Ignore Occlusion", kPropertyTypeBoolUVE),
                                          "Never hidden because something else covers it."),
                           "Culling"),
            }));

    using L = LightEmitterComponentUVE;
    AddValidatedUVE<LightEmitterComponentUVE, &IsLightEmitterComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.light_emitter", std::string{LightEmitter3DObjectDefinitionUVE::typeName}, kSectionOrderObjectBaseUVE + 4,
            {
                DeclareUVE<&L::color>("color", "Color", kPropertyTypeColorUVE),
                WithTooltipUVE(WithRangeUVE(DeclareUVE<&L::energy>("energy", "Energy", kPropertyTypeFloatUVE), 0.0,
                                            1000.0, 0.01),
                               "How bright the light is."),
                WithTooltipUVE(WithRangeUVE(DeclareUVE<&L::indirectEnergy>("indirectEnergy", "Indirect Energy",
                                                                           kPropertyTypeFloatUVE),
                                            0.0, 1000.0, 0.01),
                               "Multiplies the light's bounced (indirect) contribution only."),
                WithTooltipUVE(WithRangeUVE(DeclareUVE<&L::volumetricFogEnergy>(
                                                "volumetricFogEnergy", "Fog Energy", kPropertyTypeFloatUVE),
                                            0.0, 1000.0, 0.01),
                               "How strongly it lights volumetric fog and fog volumes."),
                WithTooltipUVE(WithRangeUVE(DeclareUVE<&L::specular>("specular", "Specular", kPropertyTypeFloatUVE),
                                            0.0, 16.0, 0.01),
                               "Strength of the light's highlights on shiny surfaces."),
                WithTooltipUVE(DeclareUVE<&L::negative>("negative", "Negative", kPropertyTypeBoolUVE),
                               "Subtracts light instead of adding it - darkens what it touches."),
                WithTooltipUVE(DeclareEnumUVE<&L::bakeMode>("bakeMode", "Bake Mode",
                                                            {{0, "Disabled"}, {1, "Static"}, {2, "Dynamic"}}),
                               "How baked lighting uses it: not at all, fully baked, or indirect only."),
                WithCustomDrawerUVE(WithTooltipUVE(DeclareUVE<&L::cullMask>("cullMask", "Cull Mask",
                                                                            kPropertyTypeBitMask32UVE),
                                                   "The render layers this light affects."),
                                    std::string(kLayerMaskDrawerRenderUVE)),
                InGroupUVE(DeclareUVE<&L::shadowEnabled>("shadowEnabled", "Enabled", kPropertyTypeBoolUVE), "Shadow"),
                InGroupUVE(WhenOnUVE<&L::shadowEnabled>(WithRangeUVE(
                               DeclareUVE<&L::shadowBias>("shadowBias", "Bias", kPropertyTypeFloatUVE), 0.0, 10.0,
                               0.001)),
                           "Shadow"),
                InGroupUVE(WhenOnUVE<&L::shadowEnabled>(WithRangeUVE(
                               DeclareUVE<&L::shadowNormalBias>("shadowNormalBias", "Normal Bias",
                                                                kPropertyTypeFloatUVE),
                               0.0, 10.0, 0.001)),
                           "Shadow"),
                InGroupUVE(WhenOnUVE<&L::shadowEnabled>(WithRangeUVE(
                               DeclareUVE<&L::shadowOpacity>("shadowOpacity", "Opacity", kPropertyTypeFloatUVE), 0.0,
                               1.0, 0.01)),
                           "Shadow"),
                InGroupUVE(WhenOnUVE<&L::shadowEnabled>(WithRangeUVE(
                               DeclareUVE<&L::shadowBlur>("shadowBlur", "Blur", kPropertyTypeFloatUVE), 0.0, 64.0,
                               0.01)),
                           "Shadow"),
                InGroupUVE(DeclareUVE<&L::distanceFadeEnabled>("distanceFadeEnabled", "Enabled", kPropertyTypeBoolUVE),
                           "Distance Fade"),
                InGroupUVE(WhenOnUVE<&L::distanceFadeEnabled>(WithRangeUVE(
                               DeclareUVE<&L::distanceFadeBegin>("distanceFadeBegin", "Begin", kPropertyTypeFloatUVE),
                               0.0, 1000000.0, 0.1)),
                           "Distance Fade"),
                InGroupUVE(WhenOnUVE<&L::distanceFadeEnabled>(WithRangeUVE(
                               DeclareUVE<&L::distanceFadeShadow>("distanceFadeShadow", "Shadow",
                                                                  kPropertyTypeFloatUVE),
                               0.0, 1000000.0, 0.1)),
                           "Distance Fade"),
                InGroupUVE(WhenOnUVE<&L::distanceFadeEnabled>(WithRangeUVE(
                               DeclareUVE<&L::distanceFadeLength>("distanceFadeLength", "Length",
                                                                  kPropertyTypeFloatUVE),
                               0.0, 1000000.0, 0.1)),
                           "Distance Fade"),
            }));
}

/// Skeleton3D: a Object3D child. Its bones are read-only here - they come from the rigged model the
/// Source names and change only by re-exporting it - so they are declared for display and saving,
/// with a drawer that shows the hierarchy instead of a generic editor.
void DeclareSkeletonUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    using K = Skeleton3DComponentUVE;
    TypeMetadataPropertyUVE bones = WithCustomDrawerUVE(DeclareUVE<&K::bones>("bones", "Bones", "SkeletonBones"),
                                                        "skeleton-bones");
    bones.flags = TypeMetadataPropertyFlagsUVE::ReadOnly;
    AddValidatedUVE<Skeleton3DComponentUVE, &IsSkeleton3DObjectComponentValidUVE>(
        entries,
        MakeEntryUVE("component.skeleton_3d", "Skeleton3D", kSectionOrderTypeSpecificUVE,
                     {
                         WithTooltipUVE(WithCustomDrawerUVE(DeclareUVE<&K::skeletonAssetPath>(
                                                                "skeletonAssetPath", "Source", kPropertyTypeStringUVE),
                                                            "skeleton-source"),
                                        "The rigged model (a glTF exported from Blender, say) whose armature this "
                                        "skeleton uses. Its bones are read from it."),
                         WithTooltipUVE(DeclareUVE<&K::enabled>("enabled", "Enabled", kPropertyTypeBoolUVE),
                                        "Off: attached meshes and bone attachments stop following this skeleton."),
                         std::move(bones),
                     }));
}

/// BoneAttachment3D: a scene object that rides a bone of another object's skeleton instead of the
/// hierarchy. The skeleton is an entity reference - flagged so the serializer remaps it through its
/// file-local id table, exactly like the mixer's target and the visibility parent - and the two
/// runtime answers the engine writes back each frame are declared as runtime state, so an author
/// can see whether the attachment is on its bone without a debugger.
void DeclareBoneAttachmentUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    using A = BoneAttachment3DComponentUVE;
    TypeMetadataPropertyUVE skeleton =
        WithTooltipUVE(DeclareUVE<&A::skeleton>("skeleton", "Skeleton", kPropertyTypeEntityUVE),
                       "The object whose skeleton this attachment rides. Empty leaves the object where it "
                       "was authored.");
    skeleton.flags = TypeMetadataPropertyFlagsUVE::EntityReference;
    AddValidatedUVE<BoneAttachment3DComponentUVE, &IsBoneAttachment3DObjectComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.bone_attachment_3d", "BoneAttachment3D", kSectionOrderTypeSpecificUVE,
            {
                std::move(skeleton),
                WithTooltipUVE(DeclareUVE<&A::enabled>("enabled", "Enabled", kPropertyTypeBoolUVE),
                               "Off stops following the bone without removing the object - it keeps the "
                               "transform it has."),
                WithTooltipUVE(DeclareUVE<&A::boneName>("boneName", "Bone", kPropertyTypeStringUVE),
                               "The bone to ride, by the name the skeleton asset gives it. Exact and "
                               "case-sensitive: renaming a bone in the source loses this attachment "
                               "instead of binding it to whichever bone took the name."),
                WithTooltipUVE(DeclareUVE<&A::boneIndex>("boneIndex", "Bone Index",
                                                         kPropertyTypeUInt32UVE),
                               "The bone's index in the skeleton. 4294967295 means \"bind by the name "
                               "above\"; any other value wins over the name when it names a real bone."),
                WithTooltipUVE(DeclareUVE<&A::localPosition>("localPosition", "Position",
                                                             kPropertyTypeVector3UVE),
                               "Where the object sits on the bone, in the BONE's own space: 0.1 on Y is "
                               "10 cm along the bone's up axis, not the world's."),
                WithTooltipUVE(DeclareUVE<&A::localRotation>("localRotation", "Rotation",
                                                             kPropertyTypeQuaternionUVE),
                               "How the object is turned on the bone, applied after the bone's own "
                               "rotation - so it stays gripped when the arm swings."),
                WithTooltipUVE(DeclareUVE<&A::localScale>("localScale", "Scale", kPropertyTypeVector3UVE),
                               "The object's scale on top of the bone chain's, so a scaled rig scales "
                               "what it carries."),
                InGroupUVE(DeclareRuntimeStateUVE<&A::bound>("bound", "Bound", kPropertyTypeBoolUVE),
                           "State"),
                InGroupUVE(DeclareRuntimeStateUVE<&A::resolvedBoneIndex>("resolvedBoneIndex", "Resolved Bone",
                                                                         kPropertyTypeUInt32UVE),
                           "State"),
            }));
}

/// TwoBoneIK3D: a limb solved back from a target instead of forward from its joints. The three bone
/// references are index-or-name pairs - the index is the reference that cannot go stale, the name
/// the one an author can read in a DCC tool - and the skeleton, target and pole are entity
/// references, flagged so the serializer remaps them through its file-local id table. The answers
/// the solver writes back each pass are declared as runtime state, so an author can see whether a
/// hand actually reached what it was aimed at without a debugger.
void DeclareTwoBoneIKUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    using A = TwoBoneIK3DComponentUVE;
    const auto entityReference = [](TypeMetadataPropertyUVE property) {
        property.flags = TypeMetadataPropertyFlagsUVE::EntityReference;
        return property;
    };
    AddValidatedUVE<TwoBoneIK3DComponentUVE, &IsTwoBoneIK3DObjectComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.two_bone_ik_3d", "TwoBoneIK3D", kSectionOrderTypeSpecificUVE,
            {
                entityReference(WithTooltipUVE(DeclareUVE<&A::skeleton>("skeleton", "Skeleton",
                                                                         kPropertyTypeEntityUVE),
                                               "The object whose skeleton this chain solves. Empty leaves "
                                               "the pose exactly as the animation wrote it.")),
                WithTooltipUVE(DeclareUVE<&A::enabled>("enabled", "Enabled", kPropertyTypeBoolUVE),
                               "Off stops solving without removing the object."),
                InGroupUVE(WithTooltipUVE(DeclareUVE<&A::rootBoneName>("rootBoneName", "Root Bone",
                                                                        kPropertyTypeStringUVE),
                                          "The chain's first bone - the shoulder, the hip. Exact and "
                                          "case-sensitive; renaming a bone in the source loses the chain "
                                          "instead of driving a different limb."),
                           "Chain"),
                InGroupUVE(WithTooltipUVE(DeclareUVE<&A::rootBoneIndex>("rootBoneIndex", "Root Index",
                                                                        kPropertyTypeUInt32UVE),
                                          "The bone's index in the skeleton. 4294967295 means \"ask the name\"; "
                                          "any other value wins over it when it names a real bone."),
                           "Chain"),
                InGroupUVE(WithTooltipUVE(DeclareUVE<&A::middleBoneName>("middleBoneName", "Middle Bone",
                                                                          kPropertyTypeStringUVE),
                                          "The bone the chain bends at - the elbow, the knee. Must be a child "
                                          "of the root bone."),
                           "Chain"),
                InGroupUVE(WithTooltipUVE(DeclareUVE<&A::middleBoneIndex>("middleBoneIndex", "Middle Index",
                                                                          kPropertyTypeUInt32UVE),
                                          "The bend bone by index; the name above is the fallback."),
                           "Chain"),
                InGroupUVE(WithTooltipUVE(DeclareUVE<&A::endBoneName>("endBoneName", "End Bone",
                                                                       kPropertyTypeStringUVE),
                                          "The bone whose origin reaches the target - the wrist, the ankle. "
                                          "Must be a child of the middle bone; its own rotation is left alone."),
                           "Chain"),
                InGroupUVE(WithTooltipUVE(DeclareUVE<&A::endBoneIndex>("endBoneIndex", "End Index",
                                                                       kPropertyTypeUInt32UVE),
                                          "The effector bone by index; the name above is the fallback."),
                           "Chain"),
                InGroupUVE(entityReference(WithTooltipUVE(DeclareUVE<&A::target>("target", "Target",
                                                                                 kPropertyTypeEntityUVE),
                                                          "The object whose world position the limb "
                                                          "reaches for. Empty uses the point below.")),
                           "Target"),
                InGroupUVE(WithTooltipUVE(DeclareUVE<&A::targetPosition>("targetPosition", "Target Point",
                                                                         kPropertyTypeVector3UVE),
                                          "Where the limb should end, in the SKELETON's own space - "
                                          "scaled and rotated with it. Ignored while a target object "
                                          "above is set."),
                           "Target"),
                InGroupUVE(entityReference(WithTooltipUVE(DeclareUVE<&A::poleTarget>("poleTarget", "Pole",
                                                                                     kPropertyTypeEntityUVE),
                                                          "A point the joint bends toward. An elbow has a "
                                                          "circle of positions that all reach the target; "
                                                          "this picks one.")),
                           "Target"),
                InGroupUVE(WithTooltipUVE(DeclareUVE<&A::poleDirection>("poleDirection", "Pole Direction",
                                                                        kPropertyTypeVector3UVE),
                                          "The bend direction when no pole object is set, in the "
                                          "skeleton's own space. Zero keeps the plane the pose is "
                                          "already in."),
                           "Target"),
                InGroupUVE(DeclareRuntimeStateUVE<&A::solved>("solved", "Solved", kPropertyTypeBoolUVE), "State"),
                InGroupUVE(DeclareRuntimeStateUVE<&A::reached>("reached", "Reached", kPropertyTypeBoolUVE),
                           "State"),
                InGroupUVE(DeclareRuntimeStateUVE<&A::endToTargetDistanceMetres>("endToTargetDistanceMetres",
                                                                                 "Distance To Target",
                                                                                 kPropertyTypeFloatUVE),
                           "State"),
                InGroupUVE(DeclareRuntimeStateUVE<&A::resolvedRootBoneIndex>("resolvedRootBoneIndex",
                                                                             "Resolved Root",
                                                                             kPropertyTypeUInt32UVE),
                           "State"),
                InGroupUVE(DeclareRuntimeStateUVE<&A::resolvedMiddleBoneIndex>("resolvedMiddleBoneIndex",
                                                                               "Resolved Middle",
                                                                               kPropertyTypeUInt32UVE),
                           "State"),
                InGroupUVE(DeclareRuntimeStateUVE<&A::resolvedEndBoneIndex>("resolvedEndBoneIndex",
                                                                            "Resolved End",
                                                                            kPropertyTypeUInt32UVE),
                           "State"),
            }));
}

/// Concrete RenderInstance3D children. Each brings exactly its own section; everything above it
/// comes from the bases.
void DeclareRenderInstanceObjectsUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    AddValidatedUVE<DirectionalLight3DComponentUVE, &IsDirectionalLight3DComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.directional_light_3d", "DirectionalLight3D", kSectionOrderTypeSpecificUVE,
            {
                InGroupUVE(WithTooltipUVE(WithRangeUVE(DeclareUVE<&DirectionalLight3DComponentUVE::shadowMaxDistance>(
                                                           "shadowMaxDistance", "Max Distance", kPropertyTypeFloatUVE),
                                                       0.0, 10000.0, 1.0),
                                          "How far from the camera shadows are drawn, in metres. 0 follows the "
                                          "camera's far plane. Shorter is sharper."),
                           "Shadow"),
                InGroupUVE(WithTooltipUVE(WithRangeUVE(DeclareUVE<&DirectionalLight3DComponentUVE::shadowSplitBlend>(
                                                           "shadowSplitBlend", "Split Blend", kPropertyTypeFloatUVE),
                                                       0.0, 1.0, 0.01),
                                          "How the shadow cascades share that distance: 0 evenly, 1 packed near the "
                                          "camera (sharper up close)."),
                           "Shadow"),
            }));

    using D = Decal3DComponentUVE;
    AddValidatedUVE<Decal3DComponentUVE, &IsDecal3DObjectComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.decal_3d", "Decal3D", kSectionOrderTypeSpecificUVE,
            {
                WithTooltipUVE(DeclareUVE<&D::enabled>("enabled", "Enabled", kPropertyTypeBoolUVE),
                               "Off stops projecting without removing the object."),
                WithTooltipUVE(DeclareUVE<&D::materialAssetPath>("materialAssetPath", "Material",
                                                                 kPropertyTypeStringUVE),
                               "The decal material to project."),
                WithTooltipUVE(WithRangeUVE(DeclareUVE<&D::size>("size", "Size", kPropertyTypeVector3UVE), 0.001,
                                            100000.0, 0.01),
                               "The projection volume, centred on the object; it projects along -Y."),
                DeclareEnumUVE<&D::projection>("projection", "Projection", {{0, "Box"}, {1, "Cylinder"}}),
                WithTooltipUVE(WithRangeUVE(DeclareUVE<&D::lifetime>("lifetime", "Lifetime", kPropertyTypeFloatUVE),
                                            0.0, 100000.0, 0.1),
                               "Seconds until the decal removes itself. 0 keeps it forever."),
                WithCustomDrawerUVE(WithTooltipUVE(DeclareUVE<&D::cullMask>("cullMask", "Projects On",
                                                                            kPropertyTypeBitMask32UVE),
                                                   "The render layers it projects onto."),
                                    std::string(kLayerMaskDrawerRenderUVE)),
                InGroupUVE(WithTooltipUVE(DeclareUVE<&D::modulate>("modulate", "Modulate", kPropertyTypeColorUVE),
                                          "Tints the projected colour."),
                           "Parameters"),
                InGroupUVE(WithRangeUVE(DeclareUVE<&D::emissionEnergy>("emissionEnergy", "Emission",
                                                                       kPropertyTypeFloatUVE),
                                        0.0, 128.0, 0.01),
                           "Parameters"),
                InGroupUVE(WithTooltipUVE(WithRangeUVE(DeclareUVE<&D::albedoMix>("albedoMix", "Albedo Mix",
                                                                                 kPropertyTypeFloatUVE),
                                                       0.0, 1.0, 0.01),
                                          "How much of the surface colour it replaces. 0 keeps the paint, for dents."),
                           "Parameters"),
                InGroupUVE(WithTooltipUVE(WithRangeUVE(DeclareUVE<&D::normalFade>("normalFade", "Normal Fade",
                                                                                  kPropertyTypeFloatUVE),
                                                       0.0, 1.0, 0.01),
                                          "Fades it on surfaces turned away from the projection."),
                           "Parameters"),
                InGroupUVE(WithRangeUVE(DeclareUVE<&D::upperFade>("upperFade", "Upper", kPropertyTypeFloatUVE), 0.0,
                                        1.0, 0.01),
                           "Vertical Fade"),
                InGroupUVE(WithRangeUVE(DeclareUVE<&D::lowerFade>("lowerFade", "Lower", kPropertyTypeFloatUVE), 0.0,
                                        1.0, 0.01),
                           "Vertical Fade"),
                InGroupUVE(DeclareUVE<&D::distanceFadeEnabled>("distanceFadeEnabled", "Enabled", kPropertyTypeBoolUVE),
                           "Distance Fade"),
                InGroupUVE(WhenOnUVE<&D::distanceFadeEnabled>(WithRangeUVE(
                               DeclareUVE<&D::distanceFadeBegin>("distanceFadeBegin", "Begin", kPropertyTypeFloatUVE),
                               0.0, 1000000.0, 0.1)),
                           "Distance Fade"),
                InGroupUVE(WhenOnUVE<&D::distanceFadeEnabled>(WithRangeUVE(
                               DeclareUVE<&D::distanceFadeLength>("distanceFadeLength", "Length",
                                                                  kPropertyTypeFloatUVE),
                               0.0, 1000000.0, 0.1)),
                           "Distance Fade"),
                // The runtime half: how much of the lifetime is left, and whether it has run out.
                // Shown during Play so an author can watch a decal age, never written by authoring
                // and never saved - the same treatment Projectile3D's countdown gets, because it is
                // the same kind of fact.
                InGroupUVE(DeclareRuntimeStateUVE<&D::remainingLifetime>("remainingLifetime", "Remaining",
                                                                         kPropertyTypeFloatUVE),
                           "Result"),
                InGroupUVE(DeclareRuntimeStateUVE<&D::expired>("expired", "Expired", kPropertyTypeBoolUVE),
                           "Result"),
            }));

    using F = FogVolume3DComponentUVE;
    AddValidatedUVE<FogVolume3DComponentUVE, &IsFogVolume3DObjectComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.fog_volume_3d", "FogVolume3D", kSectionOrderTypeSpecificUVE,
            {
                WithTooltipUVE(DeclareEnumUVE<&F::shape>(
                                   "shape", "Shape",
                                   {{0, "Ellipsoid"}, {1, "Cone"}, {2, "Cylinder"}, {3, "Box"}, {4, "World"}}),
                               "The volume's shape. World fills the whole scene and ignores Size."),
                [] {
                    TypeMetadataPropertyUVE property = WithRangeUVE(
                        DeclareUVE<&F::size>("size", "Size", kPropertyTypeVector3UVE), 0.001, 100000.0, 0.01);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const F*>(instance)->shape != FogVolumeShapeUVE::World;
                    };
                    return property;
                }(),
                WithTooltipUVE(DeclareUVE<&F::materialAssetPath>("materialAssetPath", "Material",
                                                                 kPropertyTypeStringUVE),
                               "An optional fog material. When set it replaces the values below."),
                InGroupUVE(WithTooltipUVE(WithRangeUVE(DeclareUVE<&F::density>("density", "Density",
                                                                               kPropertyTypeFloatUVE),
                                                       -1024.0, 1024.0, 0.01),
                                          "How thick the fog is. Negative clears fog from inside the volume."),
                           "Fog"),
                InGroupUVE(DeclareUVE<&F::albedo>("albedo", "Albedo", kPropertyTypeColorUVE), "Fog"),
                InGroupUVE(WithTooltipUVE(DeclareUVE<&F::emission>("emission", "Emission", kPropertyTypeColorUVE),
                                          "Light the fog gives off by itself, with no light shining on it."),
                           "Fog"),
                InGroupUVE(WithTooltipUVE(WithRangeUVE(DeclareUVE<&F::heightFalloff>(
                                                           "heightFalloff", "Height Falloff", kPropertyTypeFloatUVE),
                                                       0.0, 1024.0, 0.01),
                                          "Thins the fog with height inside the volume. 0 keeps it even."),
                           "Fog"),
                InGroupUVE(WithTooltipUVE(WithRangeUVE(DeclareUVE<&F::edgeFade>("edgeFade", "Edge Fade",
                                                                                kPropertyTypeFloatUVE),
                                                       0.0, 1.0, 0.01),
                                          "Softens the volume's boundary. 0 is a hard edge."),
                           "Fog"),
            }));
}

void DeclareObjectCommonUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    // Each of these is declared here and nowhere else, and each appears in the Inspector - with
    // its dropdown, its resolved answer and its place in the section - without a line of Inspector
    // code being written for it. That is the whole point of the declaration being the single
    // source of property truth.
    constexpr std::int32_t kProcessOrder = kSectionOrderObjectCommonUVE;
    constexpr std::int32_t kThreadGroupOrder = kSectionOrderObjectCommonUVE + 10;
    constexpr std::int32_t kPhysicsInterpolationOrder = kSectionOrderObjectCommonUVE + 20;
    constexpr std::int32_t kAutoTranslateOrder = kSectionOrderObjectCommonUVE + 30;
    constexpr std::int32_t kEditorDescriptionOrder = kSectionOrderObjectCommonUVE + 40;
    constexpr std::int32_t kScriptOrder = kSectionOrderObjectCommonUVE + 50;
    constexpr std::int32_t kMetadataOrder = kSectionOrderObjectCommonUVE + 60;

    AddUVE<ProcessComponentUVE>(
        entries,
        MakeEntryUVE(
            "component.process", "Process", kProcessOrder,
            {
                WithTooltipUVE(
                    ResolvedByUVE(DeclareEnumUVE<&ProcessComponentUVE::mode>("mode", "Mode",
                                                                             {{0, "Inherit"},
                                                                              {1, "Running"},
                                                                              {2, "Paused Only"},
                                                                              {3, "Always"},
                                                                              {4, "Never"}}),
                                  "resolvedModeInHierarchy"),
                    "Whether this entity's work runs while paused. Drives scripts and particle "
                    "emitters; controllers, projectiles and spring arms skip Never and Paused "
                    "Only. Inherit takes the parent's answer (Running at the top)."),
                WithTooltipUVE(DeclareUVE<&ProcessComponentUVE::priority>("priority", "Priority",
                                                                           kPropertyTypeInt32UVE),
                               "Script tick order. Lower runs first; equal priorities keep entity "
                               "order. Not inherited."),
                WithTooltipUVE(DeclareUVE<&ProcessComponentUVE::physicsPriority>(
                                   "physicsPriority", "Physics Priority", kPropertyTypeInt32UVE),
                               "Fixed-step order for character controllers, projectiles and spring "
                               "arms. Lower runs first. Not inherited."),
                DeclareRuntimeStateEnumUVE<&ProcessComponentUVE::resolvedModeInHierarchy>(
                    "resolvedModeInHierarchy", "Resolved Mode",
                    {{0, "Inherit"}, {1, "Running"}, {2, "Paused Only"}, {3, "Always"}, {4, "Never"}}),
            }));

    // A sub-group of Process: which thread the work runs on is a refinement of when it runs.
    TypeMetadataEntryUVE threadGroup = MakeEntryUVE(
        "component.thread_group", "Thread Group", kThreadGroupOrder,
        {
            WithTooltipUVE(
                ResolvedByUVE(DeclareEnumUVE<&ThreadGroupComponentUVE::mode>(
                                  "mode", "Group", {{0, "Inherit"}, {1, "Main Thread"}, {2, "Sub Thread"}}),
                              "resolvedModeInHierarchy"),
                "Which thread this entity's work may run on. Today this moves particle emitter "
                "simulation onto worker threads; scripts always stay on the main thread. A Main "
                "Thread ancestor is a constraint a child cannot override."),
            // Kept declared so code can still read it, but not offered: particle emitters are
            // simulated independently, so an order within a group has nothing to order yet.
            HiddenUVE(DeclareUVE<&ThreadGroupComponentUVE::order>("order", "Order", kPropertyTypeInt32UVE)),
            DeclareRuntimeStateEnumUVE<&ThreadGroupComponentUVE::resolvedModeInHierarchy>(
                "resolvedModeInHierarchy", "Resolved Group",
                {{0, "Inherit"}, {1, "Main Thread"}, {2, "Sub Thread"}}),
        });
    threadGroup.nestedUnderTypeIds = {"component.process"};
    AddUVE<ThreadGroupComponentUVE>(entries, std::move(threadGroup));

    // Only `mode` is authored. Everything else on this component is the interpolation system's
    // working state: the resolved answer plus the two poses it blends between. Marking them
    // RuntimeState is what keeps a generic editor from writing a pose and a generic serializer
    // from persisting one - either would corrupt the next frame's interpolation.
    AddUVE<PhysicsInterpolationComponentUVE>(
        entries,
        MakeEntryUVE("component.physics_interpolation", "Physics Interpolation", kPhysicsInterpolationOrder,
                     {
                         ResolvedByUVE(DeclareEnumUVE<&PhysicsInterpolationComponentUVE::mode>(
                                           "mode", "Mode", {{0, "Inherit"}, {1, "Blended"}, {2, "Exact"}}),
                                       "interpolatedInHierarchy"),
                         DeclareRuntimeStateUVE<&PhysicsInterpolationComponentUVE::interpolatedInHierarchy>(
                             "interpolatedInHierarchy", "Interpolated In Hierarchy",
                             kPropertyTypeBoolUVE),
                         DeclareRuntimeStateUVE<&PhysicsInterpolationComponentUVE::hasPreviousPose>(
                             "hasPreviousPose", "Has Previous Pose", kPropertyTypeBoolUVE),
                     }));

    AddUVE<AutoTranslateComponentUVE>(
        entries,
        MakeEntryUVE(
            "component.auto_translate", "Auto Translate", kAutoTranslateOrder,
            {
                WithTooltipUVE(
                    ResolvedByUVE(DeclareEnumUVE<&AutoTranslateComponentUVE::mode>(
                                      "mode", "Mode", {{0, "Inherit"}, {1, "Localized"}, {2, "Literal"}}),
                                  "resolvedModeInHierarchy"),
                    "Whether this entity's UI Text is looked up in the active locale before it is "
                    "drawn. The authored text is its own key. Disable it for debug labels, "
                    "identifiers and player names; a label with no component follows its parent."),
                DeclareRuntimeStateEnumUVE<&AutoTranslateComponentUVE::resolvedModeInHierarchy>(
                    "resolvedModeInHierarchy", "Resolved Mode",
                    {{0, "Inherit"}, {1, "Localized"}, {2, "Literal"}}),
            }));

    // A note to the next person, so it gets a box that fits a paragraph rather than one line.
    TypeMetadataPropertyUVE description = WithCustomDrawerUVE(
        DeclareUVE<&EditorDescriptionComponentUVE::description>("description", "Description",
                                                                kPropertyTypeStringUVE),
        "multiline-text");
    description.flags = TypeMetadataPropertyFlagsUVE::EditorOnly;
    AddUVE<EditorDescriptionComponentUVE>(entries,
                                          MakeEntryUVE("component.editor_description", "Editor Description",
                                                       kEditorDescriptionOrder, {std::move(description)}));

    // The script slot: empty offers to create, pick or load a script; filled names it. The path
    // is still the stored truth - the drawer only decides how it is chosen.
    TypeMetadataEntryUVE script =
        MakeEntryUVE("component.script", "Script", kScriptOrder,
                     {WithCustomDrawerUVE(DeclareUVE<&ScriptComponentUVE::scriptAssetPath>(
                                              "scriptAssetPath", "Scripting", kPropertyTypeStringUVE),
                                          "script-slot"),
                      // A `.uvs` script's `export` fields, drawn from the script itself; this
                      // object's values are stored here, so each edit is one undo step.
                      WithCustomDrawerUVE(DeclareUVE<&ScriptComponentUVE::exportValues>(
                                              "exportValues", "Exports", "ScriptExportValues"),
                                          "script-exports")});
    script.presentedInline = true;
    AddUVE<ScriptComponentUVE>(entries, std::move(script));

    // Typed key/value pairs. A list needs add, rename, retype and remove, which a single property
    // row cannot express, so the whole list is one custom-drawn property.
    TypeMetadataEntryUVE metadata = MakeEntryUVE(
        "component.object_metadata", "Metadata", kMetadataOrder,
        {WithCustomDrawerUVE(DeclareUVE<&ObjectMetadataComponentUVE::entries>("entries", "Metadata",
                                                                            "ObjectMetadataEntryList"),
                             "object-metadata")});
    metadata.presentedInline = true;
    AddUVE<ObjectMetadataComponentUVE>(entries, std::move(metadata));
}

[[nodiscard]] TypeMetadataRegistryUVE BuildRegistryUVE() {
    std::vector<TypeMetadataEntryUVE> entries;
    DeclareIdentityAndTransformUVE(entries);
    DeclareRenderingUVE(entries);
    DeclarePhysicsUVE(entries);
    DeclareRayCastUVE(entries);
    DeclareProjectileUVE(entries);
    DeclareCombatUVE(entries);
    DeclareMediaAndUIUVE(entries);
    DeclareGameplayUVE(entries);
    DeclareNavigationUVE(entries);
    DeclareObjectBasesUVE(entries);
    DeclareRenderInstanceObjectsUVE(entries);
    DeclareSkeletonUVE(entries);
    DeclareBoneAttachmentUVE(entries);
    DeclareTwoBoneIKUVE(entries);
    DeclareObjectCommonUVE(entries);

    TypeMetadataRegistryUVE registry;
    for (TypeMetadataEntryUVE& entry : entries) {
        const std::string typeId = entry.typeId;
        const Core::TypeMetadataRegistrationResultUVE result = registry.RegisterTypeUVE(std::move(entry));
        if (!result.IsRegisteredUVE()) {
            // A rejected declaration is a mistake in this file, not a runtime condition: the entry
            // is a compile-time literal, so it either always registers or never does. Log loudly
            // and carry on - the affected component simply falls back to having no metadata.
            UVE_ERROR("SceneComponentMetadataUVE: \"{}\" was rejected: {}", typeId, result.message);
            // Loud in debug builds. A rejected declaration otherwise costs only a log line while its
            // whole component silently vanishes from the Inspector - which is exactly how an
            // over-long tooltip once removed the Process section without failing anything but a
            // drawer count.
            UVE_ASSERT(result.IsRegisteredUVE());
        }
    }
    return registry;
}

} // namespace

const Core::TypeMetadataRegistryUVE& GetSceneComponentMetadataRegistryUVE() {
    // Built on first use rather than at static-initialization time, so the order in which
    // translation units initialize can never decide whether the registry is populated.
    static const TypeMetadataRegistryUVE registry = BuildRegistryUVE();
    return registry;
}

const Core::TypeMetadataEntryUVE* FindSceneComponentMetadataUVE(const std::type_index typeIndex) noexcept {
    return GetSceneComponentMetadataRegistryUVE().FindTypeByIndexUVE(typeIndex);
}

} // namespace UVE::Scene
