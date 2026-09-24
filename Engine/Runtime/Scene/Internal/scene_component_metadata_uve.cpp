// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/scene_component_metadata_uve.h"

#include <string>
#include <utility>
#include <vector>

#include "uve/component/animation_player_component_uve.h"
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
#include "uve/component/node_metadata_component_uve.h"
#include "uve/component/particle_emitter_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/thread_group_component_uve.h"
#include "uve/component/physics_interpolation_component_uve.h"
#include "uve/component/physics_object_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/render_instance_component_uve.h"
#include "uve/component/solid_body_component_uve.h"
#include "uve/component/rigid_body_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/surface_instance_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/logging/assert_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/nodes/3d/decal_3d_uve.h"
#include "uve/nodes/3d/fog_volume_3d_uve.h"
#include "uve/nodes/3d/skeleton_3d_uve.h"
#include "uve/nodes/3d/world_environment_3d_uve.h"
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
        MakeEntryUVE("component.camera", "Camera", kSectionOrderTypeSpecificUVE,
                     {
                         WithRangeUVE(DeclareUVE<&CameraComponentUVE::fieldOfViewDegrees>(
                                          "fieldOfViewDegrees", "Field Of View", kPropertyTypeFloatUVE),
                                      1.0, 179.0, 0.5),
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
                                                            "asset:uvemodel"),
                                        "An imported model. Import a .glb or .gltf (Blender: File > Export > "
                                        "glTF 2.0) from the Content Browser."),
                         WithTooltipUVE(WithCustomDrawerUVE(DeclareUVE<&MeshComponentUVE::materialGuid>(
                                                                "materialGuid", "Material", kPropertyTypeAssetGuidUVE),
                                                            "asset:uvemat"),
                                        "The surface material. Without one the mesh is drawn in neutral grey."),
                         WithCustomDrawerUVE(DeclareUVE<&MeshComponentUVE::visibilityLayers>(
                                                 "visibilityLayers", "Visibility Layers", kPropertyTypeBitMask32UVE),
                                             std::string(kLayerMaskDrawerRenderUVE)),
                     }));

    // One component behind BoxMesh3D, SphereMesh3D and PlaneMesh3D; its section carries the name
    // of the node it is on.
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

    AddUVE<WorldEnvironment3DNodeComponentUVE>(
        entries,
        MakeEntryUVE(
            "component.world_environment", "World Environment", kSectionOrderTypeSpecificUVE,
            {
                DeclareUVE<&WorldEnvironment3DNodeComponentUVE::skyAssetPath>("skyAssetPath", "Sky",
                                                                              kPropertyTypeStringUVE),
                DeclareUVE<&WorldEnvironment3DNodeComponentUVE::ambientColor>(
                    "ambientColor", "Ambient Color", kPropertyTypeColorUVE),
                WithRangeUVE(DeclareUVE<&WorldEnvironment3DNodeComponentUVE::ambientEnergy>(
                                 "ambientEnergy", "Ambient Energy", kPropertyTypeFloatUVE),
                             0.0, 100.0, 0.05),
                WithRangeUVE(DeclareUVE<&WorldEnvironment3DNodeComponentUVE::exposure>(
                                 "exposure", "Exposure", kPropertyTypeFloatUVE),
                             0.0, 100.0, 0.05),
                DeclareUVE<&WorldEnvironment3DNodeComponentUVE::fogEnabled>("fogEnabled", "Fog Enabled",
                                                                            kPropertyTypeBoolUVE),
                [] {
                    TypeMetadataPropertyUVE property = DeclareUVE<&WorldEnvironment3DNodeComponentUVE::fogColor>(
                        "fogColor", "Fog Color", kPropertyTypeColorUVE);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DNodeComponentUVE*>(instance)->fogEnabled;
                    };
                    return property;
                }(),
                [] {
                    TypeMetadataPropertyUVE property =
                        WithRangeUVE(DeclareUVE<&WorldEnvironment3DNodeComponentUVE::fogDensity>(
                                         "fogDensity", "Fog Density", kPropertyTypeFloatUVE),
                                     0.0, 1.0, 0.001);
                    property.isVisible = +[](const void* instance) {
                        return static_cast<const WorldEnvironment3DNodeComponentUVE*>(instance)->fogEnabled;
                    };
                    return property;
                }(),
                DeclareUVE<&WorldEnvironment3DNodeComponentUVE::postProcessingEnabled>(
                    "postProcessingEnabled", "Post Processing", kPropertyTypeBoolUVE),
            }));

    AddUVE<ParticleEmitterComponentUVE>(
        entries, MakeEntryUVE("component.particle_emitter", "ParticleEmitter3D",
                              kSectionOrderTypeSpecificUVE,
                              {WithRangeUVE(DeclareUVE<&ParticleEmitterComponentUVE::maxParticles>(
                                                "maxParticles", "Max Particles", kPropertyTypeUInt32UVE),
                                            0.0, 1000000.0, 1.0)}));
}

void DeclarePhysicsUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    TypeMetadataEntryUVE collider = MakeEntryUVE(
            "component.collider", "Collider", kSectionOrderTypeSpecificUVE,
            {
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
    // A collider is always part of some node rather than a feature of its own: the collision a
    // BoxMesh3D/SphereMesh3D/PlaneMesh3D is created with sits in that node's section, and a body's
    // or area's shape, layer and mask sit in PhysicsObject3D's.
    collider.nestedUnderTypeIds = {"component.primitive_mesh", "component.physics_object"};
    AddUVE<ColliderComponentUVE>(entries, std::move(collider));

    AddUVE<RigidBodyComponentUVE>(
        entries,
        MakeEntryUVE(
            "component.rigid_body", "Rigid Body", kSectionOrderTypeSpecificUVE,
            {
                WithRangeUVE(DeclareUVE<&RigidBodyComponentUVE::mass>("mass", "Mass",
                                                                       kPropertyTypeFloatUVE),
                             0.0, 100000.0, 0.01),
                DeclareUVE<&RigidBodyComponentUVE::isKinematic>("isKinematic", "Kinematic",
                                                                kPropertyTypeBoolUVE),
                WithRangeUVE(DeclareUVE<&RigidBodyComponentUVE::drag>("drag", "Drag",
                                                                       kPropertyTypeFloatUVE),
                             0.0, 100.0, 0.01),
                WithRangeUVE(DeclareUVE<&RigidBodyComponentUVE::gravityScale>(
                                 "gravityScale", "Gravity Scale", kPropertyTypeFloatUVE),
                             -100.0, 100.0, 0.05),
                DeclareUVE<&RigidBodyComponentUVE::velocity>("velocity", "Velocity",
                                                             kPropertyTypeVector3UVE),
                DeclareUVE<&RigidBodyComponentUVE::angularVelocity>("angularVelocity", "Angular Velocity",
                                                                    kPropertyTypeVector3UVE),
                DeclareUVE<&RigidBodyComponentUVE::torque>("torque", "Torque", kPropertyTypeVector3UVE),
                DeclareUVE<&RigidBodyComponentUVE::inverseInertia>("inverseInertia", "Inverse Inertia",
                                                                   kPropertyTypeVector3UVE),
            }));

    // CharacterBody3D's own section. Grouped by what an author is thinking about - how it moves,
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
    AddValidatedUVE<CharacterControllerComponentUVE, &IsCharacterControllerComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.character_controller", "CharacterBody3D", kSectionOrderTypeSpecificUVE,
            {
                WithTooltipUVE(DeclareEnumUVE<&C::motionMode>("motionMode", "Motion Mode",
                                                              {{0, "Grounded"}, {1, "Floating"}}),
                               "Grounded walks on floors under gravity. Floating flies or swims: no gravity, no "
                               "floor, every surface a wall."),
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
                               WithRangeUVE(DeclareUVE<&C::floorSnapLength>("floorSnapLength", "Snap Length",
                                                                          kPropertyTypeFloatUVE),
                                            0.0, 10.0, 0.01),
                               "Stays on the floor walking down steps and ledges up to this far below. 0 lets it "
                               "drop off every edge.")),
                           "Floor"),
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
                InGroupUVE(DeclareRuntimeStateUVE<&C::velocity>("velocity", "Velocity", kPropertyTypeVector3UVE), "State"),
                InGroupUVE(DeclareRuntimeStateUVE<&C::isOnFloor>("isOnFloor", "On Floor", kPropertyTypeBoolUVE), "State"),
                InGroupUVE(DeclareRuntimeStateUVE<&C::isOnCeiling>("isOnCeiling", "On Ceiling", kPropertyTypeBoolUVE),
                           "State"),
                InGroupUVE(DeclareRuntimeStateUVE<&C::floorNormal>("floorNormal", "Floor Normal", kPropertyTypeVector3UVE),
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

    AddUVE<AnimationPlayerComponentUVE>(
        entries,
        MakeEntryUVE("component.animation_player", "Animation Player", kSectionOrderTypeSpecificUVE,
                     {
                         DeclareUVE<&AnimationPlayerComponentUVE::clipAssetPath>("clipAssetPath", "Clip",
                                                                                 kPropertyTypeStringUVE),
                         WithRangeUVE(DeclareUVE<&AnimationPlayerComponentUVE::playbackSpeed>(
                                          "playbackSpeed", "Speed", kPropertyTypeFloatUVE),
                                      -100.0, 100.0, 0.05),
                         DeclareUVE<&AnimationPlayerComponentUVE::looping>("looping", "Looping",
                                                                           kPropertyTypeBoolUVE),
                         DeclareUVE<&AnimationPlayerComponentUVE::playOnAwake>(
                             "playOnAwake", "Play On Awake", kPropertyTypeBoolUVE),
                         DeclareUVE<&AnimationPlayerComponentUVE::enabled>("enabled", "Enabled",
                                                                           kPropertyTypeBoolUVE),
                     }));

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

/// The common Node section: what every node has regardless of what it is. These sort last, below
/// whatever the node itself brings, which is where an author expects them - and in a fixed order
/// among themselves, because an author finds a setting by where it was last time.
/// The abstract 3D bases. Their sections appear on every concrete child, between what the child
/// itself brings and the common Node section.
void DeclareNodeBasesUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    AddUVE<BoneModifierComponentUVE>(
        entries,
        MakeEntryUVE("component.bone_modifier", "BoneModifier3D", kSectionOrderNodeBaseUVE,
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
            "component.physics_object", "PhysicsObject3D", kSectionOrderNodeBaseUVE + 1,
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
        MakeEntryUVE("component.solid_body", "SolidBody3D", kSectionOrderNodeBaseUVE,
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
        MakeEntryUVE("component.render_instance", "RenderInstance3D", kSectionOrderNodeBaseUVE + 10,
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
            "component.surface_instance", "SurfaceInstance3D", kSectionOrderNodeBaseUVE + 3,
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
            "component.light_emitter", "LightEmitter3D", kSectionOrderNodeBaseUVE + 4,
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

/// Skeleton3D: a Node3D child. Its bones are read-only here - they come from the rigged model the
/// Source names and change only by re-exporting it - so they are declared for display and saving,
/// with a drawer that shows the hierarchy instead of a generic editor.
void DeclareSkeletonUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    using K = Skeleton3DNodeComponentUVE;
    TypeMetadataPropertyUVE bones = WithCustomDrawerUVE(DeclareUVE<&K::bones>("bones", "Bones", "SkeletonBones"),
                                                        "skeleton-bones");
    bones.flags = TypeMetadataPropertyFlagsUVE::ReadOnly;
    AddValidatedUVE<Skeleton3DNodeComponentUVE, &IsSkeleton3DNodeComponentValidUVE>(
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

/// Concrete RenderInstance3D children. Each brings exactly its own section; everything above it
/// comes from the bases.
void DeclareRenderInstanceNodesUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    using D = Decal3DNodeComponentUVE;
    AddValidatedUVE<Decal3DNodeComponentUVE, &IsDecal3DNodeComponentValidUVE>(
        entries,
        MakeEntryUVE(
            "component.decal_3d", "Decal3D", kSectionOrderTypeSpecificUVE,
            {
                WithTooltipUVE(DeclareUVE<&D::enabled>("enabled", "Enabled", kPropertyTypeBoolUVE),
                               "Off stops projecting without removing the node."),
                WithTooltipUVE(DeclareUVE<&D::materialAssetPath>("materialAssetPath", "Material",
                                                                 kPropertyTypeStringUVE),
                               "The decal material to project."),
                WithTooltipUVE(WithRangeUVE(DeclareUVE<&D::size>("size", "Size", kPropertyTypeVector3UVE), 0.001,
                                            100000.0, 0.01),
                               "The projection volume, centred on the node; it projects along -Y."),
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
            }));

    using F = FogVolume3DNodeComponentUVE;
    AddValidatedUVE<FogVolume3DNodeComponentUVE, &IsFogVolume3DNodeComponentValidUVE>(
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

void DeclareNodeCommonUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    // Each of these is declared here and nowhere else, and each appears in the Inspector - with
    // its dropdown, its resolved answer and its place in the section - without a line of Inspector
    // code being written for it. That is the whole point of the declaration being the single
    // source of property truth.
    constexpr std::int32_t kProcessOrder = kSectionOrderNodeCommonUVE;
    constexpr std::int32_t kThreadGroupOrder = kSectionOrderNodeCommonUVE + 10;
    constexpr std::int32_t kPhysicsInterpolationOrder = kSectionOrderNodeCommonUVE + 20;
    constexpr std::int32_t kAutoTranslateOrder = kSectionOrderNodeCommonUVE + 30;
    constexpr std::int32_t kEditorDescriptionOrder = kSectionOrderNodeCommonUVE + 40;
    constexpr std::int32_t kScriptOrder = kSectionOrderNodeCommonUVE + 50;
    constexpr std::int32_t kMetadataOrder = kSectionOrderNodeCommonUVE + 60;

    AddUVE<ProcessComponentUVE>(
        entries,
        MakeEntryUVE(
            "component.process", "Process", kProcessOrder,
            {
                WithTooltipUVE(
                    ResolvedByUVE(DeclareEnumUVE<&ProcessComponentUVE::mode>("mode", "Mode",
                                                                             {{0, "Inherit"},
                                                                              {1, "Pausable"},
                                                                              {2, "When Paused"},
                                                                              {3, "Always"},
                                                                              {4, "Disabled"}}),
                                  "resolvedModeInHierarchy"),
                    "Whether this entity's work runs while paused. Drives scripts and particle "
                    "emitters; controllers, projectiles and spring arms skip Disabled and When "
                    "Paused. Inherit takes the parent's answer (Pausable at the top)."),
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
                    {{0, "Inherit"}, {1, "Pausable"}, {2, "When Paused"}, {3, "Always"}, {4, "Disabled"}}),
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
                                           "mode", "Mode", {{0, "Inherit"}, {1, "On"}, {2, "Off"}}),
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
                                      "mode", "Mode", {{0, "Inherit"}, {1, "Always"}, {2, "Disabled"}}),
                                  "resolvedModeInHierarchy"),
                    "Whether this entity's UI Text is looked up in the active locale before it is "
                    "drawn. The authored text is its own key. Disable it for debug labels, "
                    "identifiers and player names; a label with no component follows its parent."),
                DeclareRuntimeStateEnumUVE<&AutoTranslateComponentUVE::resolvedModeInHierarchy>(
                    "resolvedModeInHierarchy", "Resolved Mode",
                    {{0, "Inherit"}, {1, "Always"}, {2, "Disabled"}}),
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
                                          "script-slot")});
    script.presentedInline = true;
    AddUVE<ScriptComponentUVE>(entries, std::move(script));

    // Typed key/value pairs. A list needs add, rename, retype and remove, which a single property
    // row cannot express, so the whole list is one custom-drawn property.
    TypeMetadataEntryUVE metadata = MakeEntryUVE(
        "component.node_metadata", "Metadata", kMetadataOrder,
        {WithCustomDrawerUVE(DeclareUVE<&NodeMetadataComponentUVE::entries>("entries", "Metadata",
                                                                            "NodeMetadataEntryList"),
                             "node-metadata")});
    metadata.presentedInline = true;
    AddUVE<NodeMetadataComponentUVE>(entries, std::move(metadata));
}

[[nodiscard]] TypeMetadataRegistryUVE BuildRegistryUVE() {
    std::vector<TypeMetadataEntryUVE> entries;
    DeclareIdentityAndTransformUVE(entries);
    DeclareRenderingUVE(entries);
    DeclarePhysicsUVE(entries);
    DeclareMediaAndUIUVE(entries);
    DeclareNodeBasesUVE(entries);
    DeclareRenderInstanceNodesUVE(entries);
    DeclareSkeletonUVE(entries);
    DeclareNodeCommonUVE(entries);

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
