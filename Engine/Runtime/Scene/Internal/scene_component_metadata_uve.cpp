// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/scene_component_metadata_uve.h"

#include <string>
#include <utility>
#include <vector>

#include "uve/component/animation_player_component_uve.h"
#include "uve/component/auto_translate_component_uve.h"
#include "uve/component/audio_source_component_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/canvas_component_uve.h"
#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/editor_description_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/node_metadata_component_uve.h"
#include "uve/component/particle_emitter_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/thread_group_component_uve.h"
#include "uve/component/physics_interpolation_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/rigid_body_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/logging/assert_uve.h"
#include "uve/logging/logging_macros_uve.h"
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

    AddUVE<MeshComponentUVE>(
        entries,
        MakeEntryUVE("component.mesh", "Mesh", kSectionOrderTypeSpecificUVE,
                     {
                         DeclareUVE<&MeshComponentUVE::meshGuid>("meshGuid", "Mesh",
                                                                 kPropertyTypeAssetGuidUVE),
                         DeclareUVE<&MeshComponentUVE::materialGuid>("materialGuid", "Material",
                                                                     kPropertyTypeAssetGuidUVE),
                         DeclareUVE<&MeshComponentUVE::visibilityLayers>(
                             "visibilityLayers", "Visibility Layers", kPropertyTypeBitMask32UVE),
                     }));

    AddUVE<PrimitiveMeshComponentUVE>(
        entries,
        MakeEntryUVE("component.primitive_mesh", "Primitive Mesh", kSectionOrderTypeSpecificUVE,
                     {
                         DeclareEnumUVE<&PrimitiveMeshComponentUVE::kind>(
                             "kind", "Shape", {{0, "Cube"}, {1, "UV Sphere"}, {2, "Plane"}}),
                         DeclareUVE<&PrimitiveMeshComponentUVE::baseColor>("baseColor", "Base Color",
                                                                           kPropertyTypeColorUVE),
                     }));

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
        entries, MakeEntryUVE("component.particle_emitter", "Particle Emitter",
                              kSectionOrderTypeSpecificUVE,
                              {WithRangeUVE(DeclareUVE<&ParticleEmitterComponentUVE::maxParticles>(
                                                "maxParticles", "Max Particles", kPropertyTypeUInt32UVE),
                                            0.0, 1000000.0, 1.0)}));
}

void DeclarePhysicsUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    AddUVE<ColliderComponentUVE>(
        entries,
        MakeEntryUVE(
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
                DeclareUVE<&ColliderComponentUVE::collisionLayer>("collisionLayer", "Layer",
                                                                  kPropertyTypeBitMask32UVE),
                DeclareUVE<&ColliderComponentUVE::collisionMask>("collisionMask", "Mask",
                                                                 kPropertyTypeBitMask32UVE),
                WithRangeUVE(DeclareUVE<&ColliderComponentUVE::friction>("friction", "Friction",
                                                                         kPropertyTypeFloatUVE),
                             0.0, 1.0, 0.01),
                WithRangeUVE(DeclareUVE<&ColliderComponentUVE::restitution>("restitution", "Restitution",
                                                                            kPropertyTypeFloatUVE),
                             0.0, 1.0, 0.01),
                WithRangeUVE(DeclareUVE<&ColliderComponentUVE::density>("density", "Density",
                                                                        kPropertyTypeFloatUVE),
                             0.0, 10000.0, 0.01),
            }));

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

    // verticalVelocity and isGrounded are written by the controller every step. They are shown
    // because seeing them is how you debug a controller, and refused to authoring because a value
    // typed into them survives exactly until the next frame.
    AddUVE<CharacterControllerComponentUVE>(
        entries,
        MakeEntryUVE("component.character_controller", "Character Controller",
                     kSectionOrderTypeSpecificUVE,
                     {
                         WithRangeUVE(DeclareUVE<&CharacterControllerComponentUVE::moveSpeed>(
                                          "moveSpeed", "Move Speed", kPropertyTypeFloatUVE),
                                      0.0, 1000.0, 0.1),
                         WithRangeUVE(DeclareUVE<&CharacterControllerComponentUVE::jumpHeight>(
                                          "jumpHeight", "Jump Height", kPropertyTypeFloatUVE),
                                      0.0, 1000.0, 0.1),
                         WithRangeUVE(DeclareUVE<&CharacterControllerComponentUVE::gravityScale>(
                                          "gravityScale", "Gravity Scale", kPropertyTypeFloatUVE),
                                      -100.0, 100.0, 0.05),
                         DeclareRuntimeStateUVE<&CharacterControllerComponentUVE::verticalVelocity>(
                             "verticalVelocity", "Vertical Velocity", kPropertyTypeFloatUVE),
                         DeclareRuntimeStateUVE<&CharacterControllerComponentUVE::isGrounded>(
                             "isGrounded", "Grounded", kPropertyTypeBoolUVE),
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

/// The common Node section: what every node has regardless of what it is. These sort last, below
/// whatever the node itself brings, which is where an author expects them.
void DeclareNodeCommonUVE(std::vector<TypeMetadataEntryUVE>& entries) {
    // Each of these four is declared here and nowhere else, and each appeared in the Inspector,
    // with its dropdown, its ordering fields and its read-only resolved answer, without a line of
    // Inspector code being written for it. That is the whole point of the declaration being the
    // single source of property truth.
    AddUVE<ProcessComponentUVE>(
        entries,
        MakeEntryUVE(
            "component.process", "Process", kSectionOrderNodeCommonUVE,
            {
                WithTooltipUVE(
                    DeclareEnumUVE<&ProcessComponentUVE::mode>("mode", "Mode",
                                                               {{0, "Inherit"},
                                                                {1, "Pausable"},
                                                                {2, "When Paused"},
                                                                {3, "Always"},
                                                                {4, "Disabled"}}),
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

    AddUVE<ThreadGroupComponentUVE>(
        entries,
        MakeEntryUVE(
            "component.thread_group", "Thread Group", kSectionOrderNodeCommonUVE,
            {
                WithTooltipUVE(
                    DeclareEnumUVE<&ThreadGroupComponentUVE::mode>(
                        "mode", "Mode", {{0, "Inherit"}, {1, "Main Thread"}, {2, "Sub Thread"}}),
                    "Which thread this entity's work may run on. Today this moves particle emitter "
                    "simulation onto worker threads; scripts always stay on the main thread. A Main "
                    "Thread ancestor is a constraint a child cannot override."),
                WithTooltipUVE(DeclareUVE<&ThreadGroupComponentUVE::order>("order", "Order",
                                                                          kPropertyTypeInt32UVE),
                               "Not used yet. Particle emitters are simulated independently, so "
                               "their order within a group has no effect."),
                DeclareRuntimeStateEnumUVE<&ThreadGroupComponentUVE::resolvedModeInHierarchy>(
                    "resolvedModeInHierarchy", "Resolved Mode",
                    {{0, "Inherit"}, {1, "Main Thread"}, {2, "Sub Thread"}}),
            }));

    AddUVE<AutoTranslateComponentUVE>(
        entries,
        MakeEntryUVE(
            "component.auto_translate", "Auto Translate", kSectionOrderNodeCommonUVE,
            {
                WithTooltipUVE(
                    DeclareEnumUVE<&AutoTranslateComponentUVE::mode>(
                        "mode", "Mode", {{0, "Inherit"}, {1, "Always"}, {2, "Disabled"}}),
                    "Whether this entity's UI Text is looked up in the active locale before it is "
                    "drawn. The authored text is its own key. Disable it for debug labels, "
                    "identifiers and player names; a label with no component follows its parent."),
                DeclareRuntimeStateEnumUVE<&AutoTranslateComponentUVE::resolvedModeInHierarchy>(
                    "resolvedModeInHierarchy", "Resolved Mode",
                    {{0, "Inherit"}, {1, "Always"}, {2, "Disabled"}}),
            }));

    // The entry list itself has no generic editor yet - a list of key/value pairs needs add and
    // remove affordances a property row cannot express - so it is declared Hidden rather than
    // shown as a control that looks editable and is not. It serializes, it is readable from code,
    // and it gains its editor when the list drawer exists.
    TypeMetadataPropertyUVE metadataEntries = DeclareUVE<&NodeMetadataComponentUVE::entries>(
        "entries", "Entries", "NodeMetadataEntryList");
    metadataEntries.flags = TypeMetadataPropertyFlagsUVE::Hidden;
    AddUVE<NodeMetadataComponentUVE>(entries, MakeEntryUVE("component.node_metadata", "Metadata",
                                                           kSectionOrderNodeCommonUVE,
                                                           {std::move(metadataEntries)}));

    AddUVE<ScriptComponentUVE>(
        entries, MakeEntryUVE("component.script", "Script", kSectionOrderNodeCommonUVE,
                              {DeclareUVE<&ScriptComponentUVE::scriptAssetPath>(
                                  "scriptAssetPath", "Script", kPropertyTypeStringUVE)}));

    // Only `mode` is authored. Everything else on this component is the interpolation system's
    // working state: the resolved answer plus the two poses it blends between. Marking them
    // RuntimeState is what keeps a generic editor from writing a pose and a generic serializer
    // from persisting one - either would corrupt the next frame's interpolation.
    AddUVE<PhysicsInterpolationComponentUVE>(
        entries,
        MakeEntryUVE("component.physics_interpolation", "Physics Interpolation",
                     kSectionOrderNodeCommonUVE,
                     {
                         DeclareEnumUVE<&PhysicsInterpolationComponentUVE::mode>(
                             "mode", "Mode", {{0, "Inherit"}, {1, "On"}, {2, "Off"}}),
                         DeclareRuntimeStateUVE<&PhysicsInterpolationComponentUVE::interpolatedInHierarchy>(
                             "interpolatedInHierarchy", "Interpolated In Hierarchy",
                             kPropertyTypeBoolUVE),
                         DeclareRuntimeStateUVE<&PhysicsInterpolationComponentUVE::hasPreviousPose>(
                             "hasPreviousPose", "Has Previous Pose", kPropertyTypeBoolUVE),
                     }));

    TypeMetadataPropertyUVE description = DeclareUVE<&EditorDescriptionComponentUVE::description>(
        "description", "Description", kPropertyTypeStringUVE);
    description.flags = TypeMetadataPropertyFlagsUVE::EditorOnly;
    AddUVE<EditorDescriptionComponentUVE>(entries,
                                          MakeEntryUVE("component.editor_description", "Editor Description",
                                                       kSectionOrderNodeCommonUVE, {std::move(description)}));
}

[[nodiscard]] TypeMetadataRegistryUVE BuildRegistryUVE() {
    std::vector<TypeMetadataEntryUVE> entries;
    DeclareIdentityAndTransformUVE(entries);
    DeclareRenderingUVE(entries);
    DeclarePhysicsUVE(entries);
    DeclareMediaAndUIUVE(entries);
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
