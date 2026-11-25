// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "scene_component_metadata.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <typeindex>
#include <vector>

#include "area_component.h"
#include "character_controller_component.h"
#include "collider_component.h"
#include "hierarchy_component.h"
#include "light_component.h"
#include "mesh_component.h"
#include "primitive_mesh_component.h"
#include "physics_interpolation_component.h"
#include "surface_instance_component.h"
#include "transform_component.h"
#include "visibility_component.h"
#include "bone_attachment_3d.h"
#include "hitbox_3d.h"
#include "hurtbox_3d.h"
#include "interaction_area_3d.h"
#include "decal_3d.h"
#include "lod_group_3d.h"
#include "occluder_3d.h"
#include "visibility_region_3d.h"
#include "world_environment_3d.h"
#include "world_partition_3d.h"
#include "projectile_3d.h"
#include "ray_cast_3d.h"
#include "reflection_probe_3d.h"
#include "sky_3d.h"
#include "spawn_point_3d.h"
#include "health.h"
#include "player_3d.h"
#include "two_bone_ik_3d.h"
#include "full_body_ik_3d.h"
#include "vector3.h"

namespace UVE::Scene::Tests {
namespace {

using Core::GetPropertyValue;
using Core::SetPropertyValue;
using Core::TypeMetadataEntry;
using Core::TypeMetadataPropertyFlags;
using Core::TypeMetadataProperty;

[[nodiscard]] const TypeMetadataProperty* FindProperty(const TypeMetadataEntry& entry,
                                                             const std::string& name) {
    const auto iterator = std::find_if(entry.properties.cbegin(), entry.properties.cend(),
                                       [&name](const auto& property) { return property.name == name; });
    return iterator == entry.properties.cend() ? nullptr : &*iterator;
}

TEST(SceneComponentMetadataTest, EveryDeclarationRegistersAndCarriesItsNativeType) {
    // A declaration in scene_component_metadata_uve.cpp is a compile-time literal, so a rejection
    // is a mistake in that file rather than a runtime condition. This is what catches it.
    const Core::TypeMetadataRegistry& registry = GetSceneComponentMetadataRegistry();
    ASSERT_GT(registry.GetTypeCount(), 0U);
    EXPECT_EQ(registry.GetGeneration(), registry.GetTypeCount());

    for (const TypeMetadataEntry& entry : registry.GetSnapshot().entries) {
        EXPECT_EQ(entry.kind, Core::TypeMetadataKind::Component) << entry.typeId;
        EXPECT_NE(entry.typeIndex, std::type_index(typeid(void))) << entry.typeId;
        EXPECT_TRUE(entry.HasFactory()) << entry.typeId;
        EXPECT_FALSE(entry.properties.empty()) << entry.typeId;
        for (const TypeMetadataProperty& property : entry.properties) {
            // A declared property must be able to read and write itself, or no generic consumer
            // can do anything with it.
            EXPECT_NE(property.getValue, nullptr) << entry.typeId << "." << property.name;
            EXPECT_NE(property.setValue, nullptr) << entry.typeId << "." << property.name;
            EXPECT_FALSE(property.typeId.IsEmpty()) << entry.typeId << "." << property.name;
        }
    }
}

TEST(SceneComponentMetadataTest, EveryLayerMaskNamesTheLayersItPicksFrom) {
    // A mask with no layer set would be drawn as bare hexadecimal, without the project's names.
    std::size_t physics = 0U;
    std::size_t render = 0U;
    for (const TypeMetadataEntry& entry : GetSceneComponentMetadataRegistry().GetSnapshot().entries) {
        for (const TypeMetadataProperty& property : entry.properties) {
            if (property.typeId != kPropertyTypeBitMask32) {
                continue;
            }
            const bool isPhysics = property.customDrawerId == kLayerMaskDrawerPhysics;
            const bool isRender = property.customDrawerId == kLayerMaskDrawerRender;
            EXPECT_TRUE(isPhysics || isRender) << entry.typeId << "." << property.name;
            physics += isPhysics ? 1U : 0U;
            render += isRender ? 1U : 0U;
        }
    }
    // The collider's layer and mask, SpringArm3D's collisionMask, RayCast3D's collisionMask,
    // Projectile3D's collisionMask, Hitbox3D/Hurtbox3D's layer and mask, InteractionArea3D's
    // layer and mask, the navigation region/agent layers, and the audio source's occlusion mask -
    // every cast, every swept body, every strike, every interact volume, every path and every
    // sound-blocking query in the engine filters on the same layer contract, so they all belong to
    // the same drawer set rather than a second, hand-drawn one.
    EXPECT_EQ(physics, 16U);
    EXPECT_EQ(render, 5U); // mesh, render instance, light, decal and reflection probe
}

// A group is a fold, and a fold is one unbroken run of rows: the Inspector opens a new fold every
// time the group name changes, so a row from a group that reappears further down the section opens
// a second fold with the same name - and an ungrouped row in the middle cuts the first one in two.
// Both read as a section that lost its order, and neither is visible from the declaration that
// caused it. This walks every component so a later edit that moves one row across a boundary fails
// here rather than in the panel.
TEST(SceneComponentMetadataTest, EveryGroupIsOneUnbrokenRunOfRows) {
    std::size_t groups = 0U;
    for (const TypeMetadataEntry& entry : GetSceneComponentMetadataRegistry().GetSnapshot().entries) {
        std::vector<std::string> closed;
        std::string run;
        for (const TypeMetadataProperty& property : entry.properties) {
            if (property.section == run) {
                continue;
            }
            if (!run.empty()) {
                closed.push_back(run);
            }
            run = property.section;
            if (run.empty()) {
                continue;
            }
            // A group that has already ended cannot start again. A group that is merely ungrouped
            // between two members is this same failure seen from the other side: the run ended.
            EXPECT_EQ(std::find(closed.begin(), closed.end(), run), closed.end())
                << entry.typeId << "." << property.name << " reopens the group " << run;
            ++groups;
        }
    }
    // Guards against the walk passing because nothing is grouped at all.
    EXPECT_GT(groups, 0U);
}

TEST(SceneComponentMetadataTest, FindSceneComponentMetadata_ResolvesALiveComponentType) {
    const TypeMetadataEntry* transform =
        FindSceneComponentMetadata(std::type_index(typeid(TransformComponent)));
    ASSERT_NE(transform, nullptr);
    EXPECT_EQ(transform->typeId, "component.transform");
    EXPECT_EQ(transform->order, kSectionOrderTransform);

    // A component type that declares nothing resolves to nothing rather than to a wrong entry.
    EXPECT_EQ(FindSceneComponentMetadata(std::type_index(typeid(int))), nullptr);
}

TEST(SceneComponentMetadataTest, DeclaredPropertiesReadAndWriteTheRealComponent) {
    const TypeMetadataEntry* transform =
        FindSceneComponentMetadata(std::type_index(typeid(TransformComponent)));
    ASSERT_NE(transform, nullptr);
    const TypeMetadataProperty* position = FindProperty(*transform, "localPosition");
    ASSERT_NE(position, nullptr);

    TransformComponent component;
    SetPropertyValue(*position, component, Math::Vector3{3.0F, 4.0F, 5.0F});
    EXPECT_FLOAT_EQ(component.localPosition.y, 4.0F);
    EXPECT_FLOAT_EQ(GetPropertyValue<Math::Vector3>(*position, component).z, 5.0F);
}

TEST(SceneComponentMetadataTest, SurfaceOverridesArePersistedButDrawnByTheirOwnRows) {
    // The per-surface override list is document data - the serializer writes it, and a reload has
    // to bring it back - but it is not a generic row: the MeshInstance3D inspector draws one
    // picker per surface beside the material that surface already uses. Declaring it Hidden is the
    // difference between "persisted" and "appears as a wall of path strings".
    const TypeMetadataEntry* entry =
        FindSceneComponentMetadata(std::type_index(typeid(SurfaceInstanceComponent)));
    ASSERT_NE(entry, nullptr);
    const TypeMetadataProperty* overrides = FindProperty(*entry, "surfaceMaterialOverridePaths");
    ASSERT_NE(overrides, nullptr);
    EXPECT_EQ(overrides->typeId, kPropertyTypeStringList);
    EXPECT_TRUE(overrides->flags.Has(TypeMetadataPropertyFlags::Hidden));
    EXPECT_FALSE(overrides->flags.Has(TypeMetadataPropertyFlags::RuntimeState));

    SurfaceInstanceComponent component;
    const std::vector<std::string> paths{"", "materials/one.uvmaterial"};
    SetPropertyValue(*overrides, component, paths);
    EXPECT_EQ(component.surfaceMaterialOverridePaths, paths);
    EXPECT_EQ(GetPropertyValue<std::vector<std::string>>(*overrides, component), paths);
}

TEST(SceneComponentMetadataTest, EnumPropertiesRoundTripThroughInt64) {
    // The point of the int64 accessors: this test never names LightType, exactly as a generic
    // consumer never can.
    const TypeMetadataEntry* light = FindSceneComponentMetadata(std::type_index(typeid(LightComponent)));
    ASSERT_NE(light, nullptr);
    const TypeMetadataProperty* type = FindProperty(*light, "type");
    ASSERT_NE(type, nullptr);
    ASSERT_EQ(type->enumEntries.size(), 3U);
    EXPECT_EQ(type->enumEntries[2].label, "Spot");

    LightComponent component;
    SetPropertyValue(*type, component, type->enumEntries[2].value);
    EXPECT_EQ(component.type, LightType::Spot);
    EXPECT_EQ(GetPropertyValue<std::int64_t>(*type, component), 2);
}

TEST(SceneComponentMetadataTest, RuntimeStateIsRefusedForAuthoringAndForPersistence) {
    const TypeMetadataEntry* visibility =
        FindSceneComponentMetadata(std::type_index(typeid(VisibilityComponent)));
    ASSERT_NE(visibility, nullptr);

    const TypeMetadataProperty* authored = FindProperty(*visibility, "visible");
    ASSERT_NE(authored, nullptr);
    EXPECT_TRUE(authored->IsAuthoringWritable());

    // visibleInHierarchy is written only by SceneGraph::Update. Authoring it would be
    // overwritten on the next update; persisting it would restore a stale answer.
    const TypeMetadataProperty* derived = FindProperty(*visibility, "visibleInHierarchy");
    ASSERT_NE(derived, nullptr);
    EXPECT_FALSE(derived->IsAuthoringWritable());
    EXPECT_FALSE(derived->IsSerialized());

    // The same rule, applied to the interpolation system's working state.
    const TypeMetadataEntry* interpolation =
        FindSceneComponentMetadata(std::type_index(typeid(PhysicsInterpolationComponent)));
    ASSERT_NE(interpolation, nullptr);
    const TypeMetadataProperty* mode = FindProperty(*interpolation, "mode");
    ASSERT_NE(mode, nullptr);
    EXPECT_TRUE(mode->IsAuthoringWritable());
    const TypeMetadataProperty* resolved = FindProperty(*interpolation, "interpolatedInHierarchy");
    ASSERT_NE(resolved, nullptr);
    EXPECT_FALSE(resolved->IsAuthoringWritable());
}

TEST(SceneComponentMetadataTest, EntityReferencesAreDeclaredRatherThanRediscovered) {
    // The serializer hand-special-cases exactly these two properties because they hold entity
    // references that need remapping through its file-local id table. Declaring the fact is what
    // lets a generic consumer find them instead of knowing their names.
    const TypeMetadataEntry* visibility =
        FindSceneComponentMetadata(std::type_index(typeid(VisibilityComponent)));
    ASSERT_NE(visibility, nullptr);
    const TypeMetadataProperty* visibilityParent = FindProperty(*visibility, "visibilityParent");
    ASSERT_NE(visibilityParent, nullptr);
    EXPECT_TRUE(visibilityParent->flags.Has(TypeMetadataPropertyFlags::EntityReference));
    EXPECT_EQ(visibilityParent->customDrawerId, "visibility.parent");

    const TypeMetadataEntry* hierarchy =
        FindSceneComponentMetadata(std::type_index(typeid(HierarchyComponent)));
    ASSERT_NE(hierarchy, nullptr);
    const TypeMetadataProperty* parent = FindProperty(*hierarchy, "parent");
    ASSERT_NE(parent, nullptr);
    EXPECT_TRUE(parent->flags.Has(TypeMetadataPropertyFlags::EntityReference));
}

TEST(SceneComponentMetadataTest, RotationIsRoutedToACustomDrawerRatherThanWrittenRaw) {
    // A generic editor writing localRotation alone would leave localEulerRadians and
    // rotationEditMode describing a different rotation than the quaternion does.
    const TypeMetadataEntry* transform =
        FindSceneComponentMetadata(std::type_index(typeid(TransformComponent)));
    ASSERT_NE(transform, nullptr);
    const TypeMetadataProperty* rotation = FindProperty(*transform, "localRotation");
    ASSERT_NE(rotation, nullptr);
    EXPECT_EQ(rotation->customDrawerId, "transform.rotation");
}

TEST(SceneComponentMetadataTest, WorldEnvironmentFogModeSelectsTheRelevantAuthoringControls) {
    const TypeMetadataEntry* environmentEntry =
        FindSceneComponentMetadata(std::type_index(typeid(WorldEnvironment3DComponent)));
    ASSERT_NE(environmentEntry, nullptr);
    const TypeMetadataProperty* mode = FindProperty(*environmentEntry, "fogMode");
    const TypeMetadataProperty* density = FindProperty(*environmentEntry, "fogDensity");
    const TypeMetadataProperty* start = FindProperty(*environmentEntry, "fogStart");
    const TypeMetadataProperty* end = FindProperty(*environmentEntry, "fogEnd");
    const TypeMetadataProperty* height = FindProperty(*environmentEntry, "fogHeight");
    ASSERT_NE(mode, nullptr);
    ASSERT_NE(density, nullptr);
    ASSERT_NE(start, nullptr);
    ASSERT_NE(end, nullptr);
    ASSERT_NE(height, nullptr);
    ASSERT_EQ(mode->enumEntries.size(), 3U);
    EXPECT_EQ(mode->enumEntries[0].label, "Linear");
    EXPECT_EQ(mode->enumEntries[1].label, "Exponential");
    EXPECT_EQ(mode->enumEntries[2].label, "Height");
    ASSERT_NE(mode->isVisible, nullptr);
    ASSERT_NE(density->isVisible, nullptr);
    ASSERT_NE(start->isVisible, nullptr);
    ASSERT_NE(end->isVisible, nullptr);
    ASSERT_NE(height->isVisible, nullptr);

    WorldEnvironment3DComponent component;
    EXPECT_FALSE(mode->isVisible(&component));
    component.fogEnabled = true;
    EXPECT_TRUE(mode->isVisible(&component));

    SetPropertyValue(*mode, component, mode->enumEntries[0].value);
    EXPECT_EQ(component.fogMode, WorldEnvironmentFogMode::Linear);
    EXPECT_FALSE(density->isVisible(&component));
    EXPECT_TRUE(start->isVisible(&component));
    EXPECT_TRUE(end->isVisible(&component));
    EXPECT_FALSE(height->isVisible(&component));

    SetPropertyValue(*mode, component, mode->enumEntries[1].value);
    EXPECT_EQ(component.fogMode, WorldEnvironmentFogMode::Exponential);
    EXPECT_TRUE(density->isVisible(&component));
    EXPECT_FALSE(start->isVisible(&component));
    EXPECT_FALSE(height->isVisible(&component));

    SetPropertyValue(*mode, component, mode->enumEntries[2].value);
    EXPECT_EQ(component.fogMode, WorldEnvironmentFogMode::Height);
    EXPECT_TRUE(density->isVisible(&component));
    EXPECT_TRUE(height->isVisible(&component));
}

TEST(SceneComponentMetadataTest, WorldEnvironmentAmbientSourceExposesRealLightingModes) {
    const TypeMetadataEntry* environmentEntry =
        FindSceneComponentMetadata(std::type_index(typeid(WorldEnvironment3DComponent)));
    ASSERT_NE(environmentEntry, nullptr);
    const TypeMetadataProperty* source = FindProperty(*environmentEntry, "ambientSource");
    const TypeMetadataProperty* tint = FindProperty(*environmentEntry, "ambientColor");
    const TypeMetadataProperty* energy = FindProperty(*environmentEntry, "ambientEnergy");
    ASSERT_NE(source, nullptr);
    ASSERT_NE(tint, nullptr);
    ASSERT_NE(energy, nullptr);
    ASSERT_EQ(source->enumEntries.size(), 4U);
    EXPECT_EQ(source->enumEntries[0].label, "None");
    EXPECT_EQ(source->enumEntries[1].label, "Flat Color");
    EXPECT_EQ(source->enumEntries[2].label, "Sky");
    EXPECT_EQ(source->enumEntries[3].label, "Environment Map");
    ASSERT_NE(tint->isVisible, nullptr);
    ASSERT_NE(energy->isVisible, nullptr);

    WorldEnvironment3DComponent component;
    EXPECT_EQ(component.ambientSource, WorldEnvironmentAmbientSource::Sky);
    EXPECT_TRUE(tint->isVisible(&component));
    EXPECT_TRUE(energy->isVisible(&component));
    SetPropertyValue(*source, component, source->enumEntries[0].value);
    EXPECT_EQ(component.ambientSource, WorldEnvironmentAmbientSource::None);
    EXPECT_FALSE(tint->isVisible(&component));
    EXPECT_FALSE(energy->isVisible(&component));
    SetPropertyValue(*source, component, source->enumEntries[1].value);
    EXPECT_EQ(component.ambientSource, WorldEnvironmentAmbientSource::FlatColor);
    EXPECT_TRUE(tint->isVisible(&component));
    SetPropertyValue(*source, component, source->enumEntries[3].value);
    EXPECT_EQ(component.ambientSource, WorldEnvironmentAmbientSource::EnvironmentMap);
    EXPECT_TRUE(energy->isVisible(&component));
}

TEST(SceneComponentMetadataTest, BloomSoftKneeIsAuthorableAndOnlyShownForActiveBloom) {
    const TypeMetadataEntry* environmentEntry =
        FindSceneComponentMetadata(std::type_index(typeid(WorldEnvironment3DComponent)));
    ASSERT_NE(environmentEntry, nullptr);
    const TypeMetadataProperty* softKnee = FindProperty(*environmentEntry, "bloomSoftKnee");
    ASSERT_NE(softKnee, nullptr);
    EXPECT_TRUE(softKnee->IsAuthoringWritable());
    EXPECT_TRUE(softKnee->IsSerialized());
    EXPECT_FALSE(softKnee->tooltip.empty());
    ASSERT_TRUE(softKnee->range.enabled);
    EXPECT_DOUBLE_EQ(softKnee->range.minimum, 0.0);
    EXPECT_DOUBLE_EQ(softKnee->range.maximum, 1.0);
    ASSERT_NE(softKnee->isVisible, nullptr);

    WorldEnvironment3DComponent environment{};
    // These rows belong to the effects stack, which is off until an author turns it on.
    environment.postProcessingEnabled = true;
    EXPECT_TRUE(softKnee->isVisible(&environment));
    environment.bloomEnabled = false;
    EXPECT_FALSE(softKnee->isVisible(&environment));
    environment.bloomEnabled = true;
    environment.postProcessingEnabled = false;
    EXPECT_FALSE(softKnee->isVisible(&environment));
    environment.postProcessingEnabled = true;
    SetPropertyValue(*softKnee, environment, 0.65F);
    EXPECT_FLOAT_EQ(environment.bloomSoftKnee, 0.65F);
}

TEST(SceneComponentMetadataTest, BloomMipCountIsAuthorableAndVisibleOnlyForActiveBloom) {
    const TypeMetadataEntry* environmentEntry =
        FindSceneComponentMetadata(std::type_index(typeid(WorldEnvironment3DComponent)));
    ASSERT_NE(environmentEntry, nullptr);
    const TypeMetadataProperty* mipCount = FindProperty(*environmentEntry, "bloomMipCount");
    ASSERT_NE(mipCount, nullptr);
    EXPECT_EQ(mipCount->typeId, kPropertyTypeUInt32);
    EXPECT_TRUE(mipCount->IsAuthoringWritable());
    EXPECT_TRUE(mipCount->IsSerialized());
    EXPECT_FALSE(mipCount->tooltip.empty());
    ASSERT_TRUE(mipCount->range.enabled);
    EXPECT_DOUBLE_EQ(mipCount->range.minimum, 1.0);
    EXPECT_DOUBLE_EQ(mipCount->range.maximum,
                     static_cast<double>(kMaximumWorldEnvironmentBloomMipCount));
    EXPECT_DOUBLE_EQ(mipCount->range.step, 1.0);
    ASSERT_NE(mipCount->isVisible, nullptr);

    WorldEnvironment3DComponent environment{};
    // These rows belong to the effects stack, which is off until an author turns it on.
    environment.postProcessingEnabled = true;
    EXPECT_TRUE(mipCount->isVisible(&environment));
    environment.bloomEnabled = false;
    EXPECT_FALSE(mipCount->isVisible(&environment));
    environment.bloomEnabled = true;
    environment.postProcessingEnabled = false;
    EXPECT_FALSE(mipCount->isVisible(&environment));
    environment.postProcessingEnabled = true;
    SetPropertyValue(*mipCount, environment, 3U);
    EXPECT_EQ(environment.bloomMipCount, 3U);
}

TEST(SceneComponentMetadataTest, VignetteControlsAreAuthorableAndShownWhenPostProcessingIsEnabled) {
    const TypeMetadataEntry* environmentEntry =
        FindSceneComponentMetadata(std::type_index(typeid(WorldEnvironment3DComponent)));
    ASSERT_NE(environmentEntry, nullptr);
    const TypeMetadataProperty* intensity = FindProperty(*environmentEntry, "vignetteIntensity");
    const TypeMetadataProperty* radius = FindProperty(*environmentEntry, "vignetteRadius");
    ASSERT_NE(intensity, nullptr);
    ASSERT_NE(radius, nullptr);
    EXPECT_TRUE(intensity->IsAuthoringWritable());
    EXPECT_TRUE(intensity->IsSerialized());
    EXPECT_TRUE(radius->IsAuthoringWritable());
    EXPECT_TRUE(radius->IsSerialized());
    EXPECT_FALSE(intensity->tooltip.empty());
    EXPECT_FALSE(radius->tooltip.empty());
    ASSERT_TRUE(intensity->range.enabled);
    ASSERT_TRUE(radius->range.enabled);
    EXPECT_DOUBLE_EQ(intensity->range.minimum, 0.0);
    EXPECT_DOUBLE_EQ(intensity->range.maximum, 1.0);
    EXPECT_DOUBLE_EQ(radius->range.minimum, 0.0);
    EXPECT_DOUBLE_EQ(radius->range.maximum, 1.0);
    ASSERT_NE(intensity->isVisible, nullptr);
    ASSERT_NE(radius->isVisible, nullptr);

    WorldEnvironment3DComponent environment{};
    // These rows belong to the effects stack, which is off until an author turns it on.
    environment.postProcessingEnabled = true;
    EXPECT_TRUE(intensity->isVisible(&environment));
    EXPECT_FALSE(radius->isVisible(&environment));
    SetPropertyValue(*intensity, environment, 0.7F);
    EXPECT_TRUE(radius->isVisible(&environment));
    SetPropertyValue(*radius, environment, 0.55F);
    EXPECT_FLOAT_EQ(environment.vignetteIntensity, 0.7F);
    EXPECT_FLOAT_EQ(environment.vignetteRadius, 0.55F);
    environment.postProcessingEnabled = false;
    EXPECT_FALSE(intensity->isVisible(&environment));
    EXPECT_FALSE(radius->isVisible(&environment));
}

TEST(SceneComponentMetadataTest, ChromaticAberrationIsAnAuthorablePostProcessingControl) {
    const TypeMetadataEntry* environmentEntry =
        FindSceneComponentMetadata(std::type_index(typeid(WorldEnvironment3DComponent)));
    ASSERT_NE(environmentEntry, nullptr);
    const TypeMetadataProperty* aberration =
        FindProperty(*environmentEntry, "chromaticAberrationIntensity");
    ASSERT_NE(aberration, nullptr);
    EXPECT_TRUE(aberration->IsAuthoringWritable());
    EXPECT_TRUE(aberration->IsSerialized());
    EXPECT_FALSE(aberration->tooltip.empty());
    ASSERT_TRUE(aberration->range.enabled);
    EXPECT_DOUBLE_EQ(aberration->range.minimum, 0.0);
    EXPECT_DOUBLE_EQ(aberration->range.maximum, 1.0);
    ASSERT_NE(aberration->isVisible, nullptr);

    WorldEnvironment3DComponent environment{};
    // These rows belong to the effects stack, which is off until an author turns it on.
    environment.postProcessingEnabled = true;
    EXPECT_TRUE(aberration->isVisible(&environment));
    SetPropertyValue(*aberration, environment, 0.45F);
    EXPECT_FLOAT_EQ(environment.chromaticAberrationIntensity, 0.45F);
    environment.postProcessingEnabled = false;
    EXPECT_FALSE(aberration->isVisible(&environment));
}

TEST(SceneComponentMetadataTest, FilmGrainIsAnAuthorablePostProcessingControl) {
    const TypeMetadataEntry* environmentEntry =
        FindSceneComponentMetadata(std::type_index(typeid(WorldEnvironment3DComponent)));
    ASSERT_NE(environmentEntry, nullptr);
    const TypeMetadataProperty* grain = FindProperty(*environmentEntry, "filmGrainIntensity");
    ASSERT_NE(grain, nullptr);
    EXPECT_TRUE(grain->IsAuthoringWritable());
    EXPECT_TRUE(grain->IsSerialized());
    EXPECT_FALSE(grain->tooltip.empty());
    ASSERT_TRUE(grain->range.enabled);
    EXPECT_DOUBLE_EQ(grain->range.minimum, 0.0);
    EXPECT_DOUBLE_EQ(grain->range.maximum, 1.0);
    ASSERT_NE(grain->isVisible, nullptr);

    WorldEnvironment3DComponent environment{};
    // These rows belong to the effects stack, which is off until an author turns it on.
    environment.postProcessingEnabled = true;
    EXPECT_TRUE(grain->isVisible(&environment));
    SetPropertyValue(*grain, environment, 0.35F);
    EXPECT_FLOAT_EQ(environment.filmGrainIntensity, 0.35F);
    environment.postProcessingEnabled = false;
    EXPECT_FALSE(grain->isVisible(&environment));
}

TEST(SceneComponentMetadataTest, LensDistortionIsAnAuthorablePostProcessingControl) {
    const TypeMetadataEntry* environmentEntry =
        FindSceneComponentMetadata(std::type_index(typeid(WorldEnvironment3DComponent)));
    ASSERT_NE(environmentEntry, nullptr);
    const TypeMetadataProperty* distortion =
        FindProperty(*environmentEntry, "lensDistortionIntensity");
    ASSERT_NE(distortion, nullptr);
    EXPECT_TRUE(distortion->IsAuthoringWritable());
    EXPECT_TRUE(distortion->IsSerialized());
    EXPECT_FALSE(distortion->tooltip.empty());
    ASSERT_TRUE(distortion->range.enabled);
    EXPECT_DOUBLE_EQ(distortion->range.minimum, 0.0);
    EXPECT_DOUBLE_EQ(distortion->range.maximum, 1.0);
    ASSERT_NE(distortion->isVisible, nullptr);

    WorldEnvironment3DComponent environment{};
    // These rows belong to the effects stack, which is off until an author turns it on.
    environment.postProcessingEnabled = true;
    EXPECT_TRUE(distortion->isVisible(&environment));
    SetPropertyValue(*distortion, environment, 0.5F);
    EXPECT_FLOAT_EQ(environment.lensDistortionIntensity, 0.5F);
    environment.postProcessingEnabled = false;
    EXPECT_FALSE(distortion->isVisible(&environment));
}

TEST(SceneComponentMetadataTest, DepthOfFieldControlsAreAuthorableAndConditionallyVisible) {
    const TypeMetadataEntry* environmentEntry =
        FindSceneComponentMetadata(std::type_index(typeid(WorldEnvironment3DComponent)));
    ASSERT_NE(environmentEntry, nullptr);
    const TypeMetadataProperty* enabled = FindProperty(*environmentEntry, "depthOfFieldEnabled");
    const TypeMetadataProperty* focusMode = FindProperty(*environmentEntry, "depthOfFieldFocusMode");
    const TypeMetadataProperty* bokehShape = FindProperty(*environmentEntry, "depthOfFieldBokehShape");
    const TypeMetadataProperty* focus = FindProperty(*environmentEntry, "depthOfFieldFocusDistance");
    const TypeMetadataProperty* aperture = FindProperty(*environmentEntry, "depthOfFieldAperture");
    const TypeMetadataProperty* quality = FindProperty(*environmentEntry, "depthOfFieldQuality");
    ASSERT_NE(enabled, nullptr);
    ASSERT_NE(focusMode, nullptr);
    ASSERT_NE(bokehShape, nullptr);
    ASSERT_NE(focus, nullptr);
    ASSERT_NE(aperture, nullptr);
    ASSERT_NE(quality, nullptr);
    EXPECT_TRUE(enabled->IsAuthoringWritable());
    EXPECT_TRUE(enabled->IsSerialized());
    EXPECT_TRUE(focusMode->IsAuthoringWritable());
    EXPECT_TRUE(focusMode->IsSerialized());
    ASSERT_EQ(focusMode->enumEntries.size(), 2U);
    EXPECT_EQ(focusMode->enumEntries[0].label, "Manual");
    EXPECT_EQ(focusMode->enumEntries[1].label, "Screen Center Autofocus");
    EXPECT_TRUE(bokehShape->IsAuthoringWritable());
    EXPECT_TRUE(bokehShape->IsSerialized());
    ASSERT_EQ(bokehShape->enumEntries.size(), 2U);
    EXPECT_EQ(bokehShape->enumEntries[0].label, "Circular");
    EXPECT_EQ(bokehShape->enumEntries[1].label, "Hexagonal");
    EXPECT_FALSE(bokehShape->tooltip.empty());
    ASSERT_NE(bokehShape->isVisible, nullptr);
    EXPECT_TRUE(focus->IsAuthoringWritable());
    EXPECT_TRUE(focus->IsSerialized());
    EXPECT_TRUE(aperture->IsAuthoringWritable());
    EXPECT_TRUE(aperture->IsSerialized());
    EXPECT_TRUE(quality->IsAuthoringWritable());
    EXPECT_TRUE(quality->IsSerialized());
    ASSERT_TRUE(focus->range.enabled);
    ASSERT_TRUE(aperture->range.enabled);
    ASSERT_TRUE(quality->range.enabled);
    EXPECT_DOUBLE_EQ(focus->range.minimum, 0.1);
    EXPECT_DOUBLE_EQ(aperture->range.minimum, 0.0);
    EXPECT_DOUBLE_EQ(aperture->range.maximum, 1.0);
    EXPECT_DOUBLE_EQ(quality->range.minimum, 0.0);
    EXPECT_DOUBLE_EQ(quality->range.maximum, 2.0);

    WorldEnvironment3DComponent environment{};
    // These rows belong to the effects stack, which is off until an author turns it on.
    environment.postProcessingEnabled = true;
    ASSERT_NE(enabled->isVisible, nullptr);
    ASSERT_NE(focusMode->isVisible, nullptr);
    ASSERT_NE(bokehShape->isVisible, nullptr);
    ASSERT_NE(focus->isVisible, nullptr);
    ASSERT_NE(aperture->isVisible, nullptr);
    ASSERT_NE(quality->isVisible, nullptr);
    EXPECT_EQ(environment.depthOfFieldFocusMode, WorldEnvironmentDepthOfFieldFocusMode::Manual);
    EXPECT_EQ(environment.depthOfFieldBokehShape, WorldEnvironmentDepthOfFieldBokehShape::Circular);
    EXPECT_TRUE(enabled->isVisible(&environment));
    EXPECT_FALSE(focusMode->isVisible(&environment));
    EXPECT_FALSE(bokehShape->isVisible(&environment));
    EXPECT_FALSE(focus->isVisible(&environment));
    SetPropertyValue(*enabled, environment, true);
    EXPECT_TRUE(focusMode->isVisible(&environment));
    EXPECT_TRUE(bokehShape->isVisible(&environment));
    EXPECT_TRUE(focus->isVisible(&environment));
    EXPECT_TRUE(aperture->isVisible(&environment));
    EXPECT_TRUE(quality->isVisible(&environment));
    SetPropertyValue(*focus, environment, 24.0F);
    SetPropertyValue(*bokehShape, environment, bokehShape->enumEntries[1].value);
    SetPropertyValue(*aperture, environment, 0.8F);
    SetPropertyValue(*quality, environment, 2U);
    EXPECT_FLOAT_EQ(environment.depthOfFieldFocusDistance, 24.0F);
    EXPECT_EQ(environment.depthOfFieldBokehShape, WorldEnvironmentDepthOfFieldBokehShape::Hexagonal);
    EXPECT_FLOAT_EQ(environment.depthOfFieldAperture, 0.8F);
    EXPECT_EQ(environment.depthOfFieldQuality, 2U);
    SetPropertyValue(*focusMode, environment, focusMode->enumEntries[1].value);
    EXPECT_EQ(environment.depthOfFieldFocusMode, WorldEnvironmentDepthOfFieldFocusMode::ScreenCenter);
    EXPECT_FALSE(focus->isVisible(&environment));
    environment.postProcessingEnabled = false;
    EXPECT_FALSE(enabled->isVisible(&environment));
    EXPECT_FALSE(focusMode->isVisible(&environment));
    EXPECT_FALSE(bokehShape->isVisible(&environment));
    EXPECT_FALSE(focus->isVisible(&environment));
    EXPECT_FALSE(aperture->isVisible(&environment));
    EXPECT_FALSE(quality->isVisible(&environment));
}

TEST(SceneComponentMetadataTest, MotionBlurControlsAreAuthorableAndConditionallyVisible) {
    const TypeMetadataEntry* environmentEntry =
        FindSceneComponentMetadata(std::type_index(typeid(WorldEnvironment3DComponent)));
    ASSERT_NE(environmentEntry, nullptr);
    const TypeMetadataProperty* enabled = FindProperty(*environmentEntry, "motionBlurEnabled");
    const TypeMetadataProperty* strength = FindProperty(*environmentEntry, "motionBlurStrength");
    const TypeMetadataProperty* samples = FindProperty(*environmentEntry, "motionBlurSampleCount");
    ASSERT_NE(enabled, nullptr);
    ASSERT_NE(strength, nullptr);
    ASSERT_NE(samples, nullptr);
    EXPECT_TRUE(enabled->IsAuthoringWritable());
    EXPECT_TRUE(enabled->IsSerialized());
    EXPECT_TRUE(strength->IsAuthoringWritable());
    EXPECT_TRUE(strength->IsSerialized());
    EXPECT_TRUE(samples->IsAuthoringWritable());
    EXPECT_TRUE(samples->IsSerialized());
    ASSERT_TRUE(strength->range.enabled);
    EXPECT_DOUBLE_EQ(strength->range.minimum, 0.0);
    EXPECT_DOUBLE_EQ(strength->range.maximum, 1.0);
    ASSERT_TRUE(samples->range.enabled);
    EXPECT_DOUBLE_EQ(samples->range.minimum, 4.0);
    EXPECT_DOUBLE_EQ(samples->range.maximum, 12.0);
    EXPECT_DOUBLE_EQ(samples->range.step, 4.0);

    WorldEnvironment3DComponent environment{};
    environment.postProcessingEnabled = false;
    ASSERT_NE(enabled->isVisible, nullptr);
    ASSERT_NE(strength->isVisible, nullptr);
    ASSERT_NE(samples->isVisible, nullptr);
    EXPECT_FALSE(environment.motionBlurEnabled);
    EXPECT_FALSE(enabled->isVisible(&environment));
    EXPECT_FALSE(strength->isVisible(&environment));
    EXPECT_FALSE(samples->isVisible(&environment));
    environment.postProcessingEnabled = true;
    EXPECT_TRUE(enabled->isVisible(&environment));
    EXPECT_FALSE(strength->isVisible(&environment));
    SetPropertyValue(*enabled, environment, true);
    EXPECT_TRUE(environment.motionBlurEnabled);
    EXPECT_TRUE(strength->isVisible(&environment));
    EXPECT_TRUE(samples->isVisible(&environment));
    SetPropertyValue(*strength, environment, 0.25F);
    SetPropertyValue(*samples, environment, 12U);
    EXPECT_FLOAT_EQ(environment.motionBlurStrength, 0.25F);
    EXPECT_EQ(environment.motionBlurSampleCount, 12U);
    environment.postProcessingEnabled = false;
    EXPECT_FALSE(enabled->isVisible(&environment));
    EXPECT_FALSE(strength->isVisible(&environment));
    EXPECT_FALSE(samples->isVisible(&environment));
}

TEST(SceneComponentMetadataTest, ReflectionProbeResolutionIsAnAuthorableValidatedPerProbeSetting) {
    const TypeMetadataEntry* probeEntry =
        FindSceneComponentMetadata(std::type_index(typeid(ReflectionProbe3DComponent)));
    ASSERT_NE(probeEntry, nullptr);
    EXPECT_EQ(probeEntry->typeId, "component.reflection_probe_3d");
    const TypeMetadataProperty* resolution = FindProperty(*probeEntry, "resolution");
    ASSERT_NE(resolution, nullptr);
    EXPECT_TRUE(resolution->IsAuthoringWritable());
    EXPECT_TRUE(resolution->IsSerialized());
    ASSERT_EQ(resolution->enumEntries.size(), 4U);
    EXPECT_EQ(resolution->enumEntries[0].label, "64 (Low)");
    EXPECT_EQ(resolution->enumEntries[1].label, "128 (Medium)");
    EXPECT_EQ(resolution->enumEntries[2].label, "256 (High)");
    EXPECT_EQ(resolution->enumEntries[3].label, "512 (Ultra)");
    ASSERT_NE(probeEntry->isInstanceValid, nullptr);

    ReflectionProbe3DComponent probe{};
    EXPECT_EQ(probe.resolution, ReflectionProbeResolution::Medium);
    EXPECT_TRUE(probeEntry->isInstanceValid(&probe));
    SetPropertyValue(*resolution, probe, resolution->enumEntries[2].value);
    EXPECT_EQ(probe.resolution, ReflectionProbeResolution::High);
    EXPECT_TRUE(probeEntry->isInstanceValid(&probe));
    probe.resolution = static_cast<ReflectionProbeResolution>(255U);
    EXPECT_FALSE(probeEntry->isInstanceValid(&probe));
}

TEST(SceneComponentMetadataTest, ConditionalVisibilityFollowsTheSiblingFieldItDependsOn) {
    const TypeMetadataEntry* collider =
        FindSceneComponentMetadata(std::type_index(typeid(ColliderComponent)));
    ASSERT_NE(collider, nullptr);
    const TypeMetadataProperty* halfExtents = FindProperty(*collider, "halfExtents");
    const TypeMetadataProperty* height = FindProperty(*collider, "height");
    ASSERT_NE(halfExtents, nullptr);
    ASSERT_NE(height, nullptr);
    ASSERT_NE(halfExtents->isVisible, nullptr);
    ASSERT_NE(height->isVisible, nullptr);

    ColliderComponent component;
    component.shapeType = ColliderShapeType::Box;
    EXPECT_TRUE(halfExtents->isVisible(&component));
    EXPECT_FALSE(height->isVisible(&component));

    component.shapeType = ColliderShapeType::Capsule;
    EXPECT_FALSE(halfExtents->isVisible(&component));
    EXPECT_TRUE(height->isVisible(&component));
}

// Character3D is the collider's first host and draws the two rows a body owns itself, so a
// character's collider section never appears a second time under the body. That makes two things a
// contract: the host link, and the two property names the body block resolves. A rename that broke
// either would leave a character with no layer and mask anywhere.
TEST(SceneComponentMetadataTest, CharacterSectionHostsTheColliderAndKeepsItsTwoBodyRows) {
    const TypeMetadataEntry* character =
        FindSceneComponentMetadata(std::type_index(typeid(CharacterControllerComponent)));
    const TypeMetadataEntry* collider =
        FindSceneComponentMetadata(std::type_index(typeid(ColliderComponent)));
    ASSERT_NE(character, nullptr);
    ASSERT_NE(collider, nullptr);

    // First, because a host listed after another is only a fallback: a body also carries a
    // PhysicsObject3D, and whichever host comes first is the section that represents its collider.
    ASSERT_FALSE(collider->nestedUnderTypeIds.empty());
    EXPECT_EQ(collider->nestedUnderTypeIds.front(), character->typeId);

    for (const char* const name : {"collisionLayer", "collisionMask"}) {
        const TypeMetadataProperty* const property = FindProperty(*collider, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_EQ(property->customDrawerId, std::string(kLayerMaskDrawerPhysics)) << name;
    }
}

// A primitive mesh is the mesh, not a feature on top of it. On a MeshInstance3D the mesh section
// already picks the shape and authors its material, so the primitive's own section drew those rows
// a second time. Nesting it under the mesh component removes the duplicate; an object that carries
// a primitive and no mesh component still gets the section, so nothing becomes unreachable.
TEST(SceneComponentMetadataTest, PrimitiveMeshIsDrawnByTheMeshSectionRatherThanTwice) {
    const TypeMetadataEntry* mesh = FindSceneComponentMetadata(std::type_index(typeid(MeshComponent)));
    const TypeMetadataEntry* primitive =
        FindSceneComponentMetadata(std::type_index(typeid(PrimitiveMeshComponent)));
    ASSERT_NE(mesh, nullptr);
    ASSERT_NE(primitive, nullptr);
    ASSERT_FALSE(primitive->nestedUnderTypeIds.empty());
    EXPECT_EQ(primitive->nestedUnderTypeIds.front(), mesh->typeId);

    // The rows the mesh section draws in its own shape. Resolved by name here so a rename that
    // silently stopped one of them being drawn anywhere would fail rather than pass.
    const TypeMetadataProperty* const material = FindProperty(*primitive, "materialGuid");
    ASSERT_NE(material, nullptr);
    EXPECT_FALSE(material->flags.Has(TypeMetadataPropertyFlags::Hidden));

    // The shape is the object's kind (a BoxMesh3D is a box): stored, never offered as a choice.
    const TypeMetadataProperty* const kind = FindProperty(*primitive, "kind");
    ASSERT_NE(kind, nullptr);
    EXPECT_TRUE(kind->flags.Has(TypeMetadataPropertyFlags::Hidden));

    // A primitive has no colour of its own: its surface is its material, and the colour row that
    // used to sit under an empty material slot meant the same object had two answers. The property
    // stays so scenes that already carry a tint still load, and it is hidden so no section can
    // offer it again - asserting only that it disappeared would let a later change re-expose it.
    const TypeMetadataProperty* const baseColor = FindProperty(*primitive, "baseColor");
    ASSERT_NE(baseColor, nullptr);
    EXPECT_TRUE(baseColor->flags.Has(TypeMetadataPropertyFlags::Hidden));
}

// The environment's sky row was a bare path field with a Browse button: a level could have a sky
// texture but had nowhere to say what the sky *is*. It is a slot now - it reads Empty until
// something is put in it, and the menu behind it is where a Sky is added, so the Sky's bands,
// atmosphere, stars and clouds are reachable from the environment they light. Nested under the
// environment, those rows expand inside that section rather than becoming a second object in the
// Outliner; the Sky still declares its own entry, so a standalone Sky object keeps its section.
TEST(SceneComponentMetadataTest, TheEnvironmentSkySlotIsWhereASkyIsAdded) {
    const TypeMetadataEntry* environment =
        FindSceneComponentMetadata(std::type_index(typeid(WorldEnvironment3DComponent)));
    const TypeMetadataEntry* sky = FindSceneComponentMetadata(std::type_index(typeid(Sky3DComponent)));
    ASSERT_NE(environment, nullptr);
    ASSERT_NE(sky, nullptr);

    // The slot: one row, named for the thing it holds rather than the field behind it, and drawn
    // by the sky drawer - a row that fell back to a plain text field would have no Empty and no
    // way to add anything.
    const TypeMetadataProperty* const slot = FindProperty(*environment, "skyAssetPath");
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->customDrawerId, std::string("sky:asset"));
    EXPECT_EQ(slot->displayName, std::string("Sky"));

    // Nested, so the environment section draws the Sky's rows itself instead of the Sky appearing
    // as a section of its own on an object whose recipe never mentioned one.
    ASSERT_FALSE(sky->nestedUnderTypeIds.empty());
    EXPECT_EQ(sky->nestedUnderTypeIds.front(), environment->typeId);

    // What the slot's New Sky puts there has to be readable where it lands: one row from each band
    // of the section, so a fold that opened to nothing would fail here rather than in the editor.
    for (const char* const name : {"source", "skyColor", "starDensity", "cloudsEnabled"}) {
        const TypeMetadataProperty* const property = FindProperty(*sky, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_FALSE(property->flags.Has(TypeMetadataPropertyFlags::Hidden)) << name;
    }
}

// Sections follow the class chain from most to least derived: the object's own, then Object3D's
// Transform, then the common Object section.
TEST(SceneComponentMetadataTest, TheCommonObjectSectionSortsBelowEverythingTypeSpecific) {
    const TypeMetadataEntry* transform =
        FindSceneComponentMetadata(std::type_index(typeid(TransformComponent)));
    const TypeMetadataEntry* light = FindSceneComponentMetadata(std::type_index(typeid(LightComponent)));
    const TypeMetadataEntry* interpolation =
        FindSceneComponentMetadata(std::type_index(typeid(PhysicsInterpolationComponent)));
    ASSERT_NE(transform, nullptr);
    ASSERT_NE(light, nullptr);
    ASSERT_NE(interpolation, nullptr);

    EXPECT_LT(light->order, transform->order);
    EXPECT_LT(transform->order, interpolation->order);
    EXPECT_GE(interpolation->order, kSectionOrderObjectCommon);
}

TEST(SceneComponentMetadataTest, TheFactoryAnswersWhatAPropertysDefaultValueIs) {
    // This is reset-to-default, done without the caller naming the component type.
    const TypeMetadataEntry* collider =
        FindSceneComponentMetadata(std::type_index(typeid(ColliderComponent)));
    ASSERT_NE(collider, nullptr);
    const Core::TypeInstance defaults = Core::TypeInstance::MakeDefault(*collider);
    ASSERT_TRUE(defaults.IsValid());

    const TypeMetadataProperty* density = FindProperty(*collider, "density");
    ASSERT_NE(density, nullptr);
    float defaultDensity = 0.0F;
    density->getValue(defaults.Get(), &defaultDensity);
    EXPECT_FLOAT_EQ(defaultDensity, ColliderComponent{}.density);
}

TEST(SceneComponentMetadataTest, ThePlayerSectionCarriesPossessAndLook) {
    const TypeMetadataEntry* player =
        FindSceneComponentMetadata(std::type_index(typeid(PlayerComponent)));
    ASSERT_NE(player, nullptr);
    EXPECT_EQ(player->typeId, "component.player");
    EXPECT_EQ(player->displayName, "Player3D");
    ASSERT_NE(FindProperty(*player, "possessOnPlay"), nullptr);
    ASSERT_NE(FindProperty(*player, "lookEnabled"), nullptr);
    const TypeMetadataProperty* pitch = FindProperty(*player, "pitchDegrees");
    ASSERT_NE(pitch, nullptr);
    EXPECT_TRUE(pitch->flags.Has(TypeMetadataPropertyFlags::RuntimeState));
}

TEST(SceneComponentMetadataTest, TheHealthSectionCarriesMaxInvulnerableAndRuntimeHealth) {
    const TypeMetadataEntry* health =
        FindSceneComponentMetadata(std::type_index(typeid(HealthComponent)));
    ASSERT_NE(health, nullptr);
    EXPECT_EQ(health->typeId, "component.health");
    EXPECT_EQ(health->displayName, "Health");
    ASSERT_NE(FindProperty(*health, "maxHealth"), nullptr);
    ASSERT_NE(FindProperty(*health, "invulnerable"), nullptr);
    const TypeMetadataProperty* current = FindProperty(*health, "health");
    ASSERT_NE(current, nullptr);
    EXPECT_TRUE(current->flags.Has(TypeMetadataPropertyFlags::RuntimeState));
}

TEST(SceneComponentMetadataTest, TheSpawnPointSectionCarriesTheWholeAuthoredContract) {
    // A SpawnPoint3D is authored data with no runtime half: everything it owns is what the query
    // reads, and the generic editor must be able to author all of it.
    const TypeMetadataEntry* spawn =
        FindSceneComponentMetadata(std::type_index(typeid(SpawnPoint3DComponent)));
    ASSERT_NE(spawn, nullptr);
    EXPECT_EQ(spawn->typeId, "component.spawn_point");
    EXPECT_EQ(spawn->displayName, "SpawnPoint3D");

    const char* const kAuthored[] = {"spawnTag", "localPosition", "localRotation", "enabled",
                                     "oneShot"};
    for (const char* const name : kAuthored) {
        const TypeMetadataProperty* property = FindProperty(*spawn, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_FALSE(property->flags.Has(TypeMetadataPropertyFlags::RuntimeState))
            << name;
    }

    // The offsets are the point's own, not a second transform: their declared labels say so, and
    // the values default to a point that spawns exactly where it sits.
    const TypeMetadataProperty* offset = FindProperty(*spawn, "localPosition");
    ASSERT_NE(offset, nullptr);
    EXPECT_EQ(offset->displayName, "Offset Position");
    const Core::TypeInstance defaults = Core::TypeInstance::MakeDefault(*spawn);
    ASSERT_TRUE(defaults.IsValid());
    Math::Vector3 position{1.0F, 1.0F, 1.0F};
    offset->getValue(defaults.Get(), &position);
    EXPECT_EQ(position, SpawnPoint3DComponent{}.localPosition);

    // The component's own rule travels with the declaration, so a generic writer cannot author an
    // empty tag or a non-finite offset and have it accepted.
    ASSERT_NE(spawn->isInstanceValid, nullptr);
    const SpawnPoint3DComponent valid{};
    EXPECT_TRUE(spawn->isInstanceValid(&valid));
    SpawnPoint3DComponent noTag = valid;
    noTag.spawnTag.clear();
    EXPECT_FALSE(spawn->isInstanceValid(&noTag));
    SpawnPoint3DComponent notFinite = valid;
    notFinite.localPosition.z = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(spawn->isInstanceValid(&notFinite));
}


TEST(SceneComponentMetadataTest, TheLodGroupSectionCarriesTheChainTheMeshesAndTheResolvedAnswer) {
    // A LODGroup3D is a list component: the levels are a prefix of two parallel arrays, and the
    // count that decides the prefix is a field of the same component. All three have to reach the
    // Inspector for the chain to be authorable at all - an array no one can fill in is the same as
    // no LOD.
    const TypeMetadataEntry* lod =
        FindSceneComponentMetadata(std::type_index(typeid(LodGroup3DComponent)));
    ASSERT_NE(lod, nullptr);
    EXPECT_EQ(lod->typeId, "component.lod_group_3d");
    EXPECT_EQ(lod->displayName, "LODGroup3D");

    for (const char* const name :
         {"enabled", "levelCount", "hysteresis", "distanceThresholds", "lodMeshGuids"}) {
        const TypeMetadataProperty* property = FindProperty(*lod, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_FALSE(property->flags.Has(TypeMetadataPropertyFlags::RuntimeState))
            << name;
        EXPECT_TRUE(property->IsAuthoringWritable()) << name;
    }

    const TypeMetadataProperty* levels = FindProperty(*lod, "levelCount");
    ASSERT_NE(levels, nullptr);
    EXPECT_EQ(levels->typeId, kPropertyTypeUInt8);
    ASSERT_TRUE(levels->range.enabled);
    EXPECT_DOUBLE_EQ(levels->range.minimum, 1.0);
    EXPECT_DOUBLE_EQ(levels->range.maximum, static_cast<double>(kMaximumLodLevels));

    const TypeMetadataProperty* hysteresis = FindProperty(*lod, "hysteresis");
    ASSERT_NE(hysteresis, nullptr);
    EXPECT_EQ(hysteresis->typeId, kPropertyTypeFloat);
    ASSERT_TRUE(hysteresis->range.enabled);
    EXPECT_DOUBLE_EQ(hysteresis->range.minimum, 0.0);
    EXPECT_DOUBLE_EQ(hysteresis->range.maximum, static_cast<double>(kMaximumLodHysteresis));

    // The two lists: a float chain and one mesh per level, both bounded by the same capacity and
    // both drawn by the section's own drawer rather than a typed row.
    const TypeMetadataProperty* thresholds = FindProperty(*lod, "distanceThresholds");
    ASSERT_NE(thresholds, nullptr);
    EXPECT_EQ(thresholds->typeId, kPropertyTypeFloatList);
    EXPECT_EQ(thresholds->customDrawerId, "lod-group-thresholds");
    EXPECT_EQ(thresholds->elementCount, kMaximumLodLevels);

    const TypeMetadataProperty* meshes = FindProperty(*lod, "lodMeshGuids");
    ASSERT_NE(meshes, nullptr);
    EXPECT_EQ(meshes->typeId, kPropertyTypeAssetGuidList);
    EXPECT_EQ(meshes->customDrawerId, "lod-group-meshes");
    EXPECT_EQ(meshes->elementCount, kMaximumLodLevels);

    // Whole-array accessors, like every other list property: one read, one write, one history
    // entry. A named alias, because the comma in the template argument list would otherwise split
    // the EXPECT_EQ macro's own argument list.
    using MeshList = std::array<Asset::AssetGuid, kMaximumLodLevels>;
    LodGroup3DComponent component;
    MeshList guids{};
    guids[1] = Asset::AssetGuid{42U};
    SetPropertyValue(*meshes, component, guids);
    EXPECT_EQ(component.lodMeshGuids[1], Asset::AssetGuid{42U});
    EXPECT_EQ(GetPropertyValue<MeshList>(*meshes, component), guids);

    // The resolved answer is runtime state: the renderer owns it, so authoring must not offer to
    // write it and saving must not persist it. It still has to be READABLE, or the Inspector shows
    // nothing where the answer belongs.
    for (const char* const name : {"currentLevel", "culledByDistance"}) {
        const TypeMetadataProperty* property = FindProperty(*lod, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_TRUE(property->flags.Has(TypeMetadataPropertyFlags::RuntimeState))
            << name;
        EXPECT_FALSE(property->IsAuthoringWritable()) << name;
        EXPECT_FALSE(property->IsSerialized()) << name;
        EXPECT_NE(property->getValue, nullptr) << name;
        EXPECT_EQ(property->section, "Result") << name;
    }

    // The component's own rule travels with the declaration, so a band no level chain can honour is
    // refused at the same validator the engine reads.
    ASSERT_NE(lod->isInstanceValid, nullptr);
    LodGroup3DComponent outOfRange;
    outOfRange.hysteresis = kMaximumLodHysteresis + 0.1F;
    EXPECT_FALSE(lod->isInstanceValid(&outOfRange));
    EXPECT_TRUE(lod->isInstanceValid(&component));
}

TEST(SceneComponentMetadataTest, TheWorldPartitionSectionCarriesTheVisBudgetAndTheLiveCount) {
    const TypeMetadataEntry* partition =
        FindSceneComponentMetadata(std::type_index(typeid(WorldPartition3DComponent)));
    ASSERT_NE(partition, nullptr);
    EXPECT_EQ(partition->typeId, "component.world_partition_3d");
    EXPECT_EQ(partition->displayName, "WorldPartition3D");

    for (const char* const name : {"enabled", "cellSize", "maximumLoadedCells"}) {
        const TypeMetadataProperty* property = FindProperty(*partition, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_FALSE(property->flags.Has(TypeMetadataPropertyFlags::RuntimeState))
            << name;
        EXPECT_TRUE(property->IsAuthoringWritable()) << name;
    }

    const TypeMetadataProperty* live = FindProperty(*partition, "loadedCellCount");
    ASSERT_NE(live, nullptr);
    EXPECT_TRUE(live->flags.Has(TypeMetadataPropertyFlags::RuntimeState));
    EXPECT_FALSE(live->IsAuthoringWritable());
    EXPECT_FALSE(live->IsSerialized());
    EXPECT_EQ(live->section, "Result");

    ASSERT_NE(partition->isInstanceValid, nullptr);
    const WorldPartition3DComponent valid{};
    EXPECT_TRUE(partition->isInstanceValid(&valid));
    WorldPartition3DComponent broken = valid;
    broken.cellSize = 0.0F;
    EXPECT_FALSE(partition->isInstanceValid(&broken));
}

TEST(SceneComponentMetadataTest, TheVisibilityRegionSectionCarriesTheRoomBoxAndTheActiveFlag) {
    const TypeMetadataEntry* region =
        FindSceneComponentMetadata(std::type_index(typeid(VisibilityRegion3DComponent)));
    ASSERT_NE(region, nullptr);
    EXPECT_EQ(region->typeId, "component.visibility_region_3d");
    EXPECT_EQ(region->displayName, "VisibilityRegion3D");

    for (const char* const name : {"enabled", "halfExtents", "visibilityLayers"}) {
        const TypeMetadataProperty* property = FindProperty(*region, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_FALSE(property->flags.Has(TypeMetadataPropertyFlags::RuntimeState))
            << name;
        EXPECT_TRUE(property->IsAuthoringWritable()) << name;
    }

    const TypeMetadataProperty* active = FindProperty(*region, "active");
    ASSERT_NE(active, nullptr);
    EXPECT_TRUE(active->flags.Has(TypeMetadataPropertyFlags::RuntimeState));
    EXPECT_FALSE(active->IsAuthoringWritable());
    EXPECT_FALSE(active->IsSerialized());
    EXPECT_EQ(active->section, "Result");

    ASSERT_NE(region->isInstanceValid, nullptr);
    const VisibilityRegion3DComponent valid{};
    EXPECT_TRUE(region->isInstanceValid(&valid));
    VisibilityRegion3DComponent broken = valid;
    broken.halfExtents.x = 0.0F;
    EXPECT_FALSE(region->isInstanceValid(&broken));
}

TEST(SceneComponentMetadataTest, TheOccluderSectionCarriesTheCoverBox) {
    const TypeMetadataEntry* occluder =
        FindSceneComponentMetadata(std::type_index(typeid(Occluder3DComponent)));
    ASSERT_NE(occluder, nullptr);
    EXPECT_EQ(occluder->typeId, "component.occluder_3d");
    EXPECT_EQ(occluder->displayName, "Occluder3D");

    for (const char* const name : {"enabled", "halfExtents", "mode"}) {
        const TypeMetadataProperty* property = FindProperty(*occluder, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_TRUE(property->IsAuthoringWritable()) << name;
    }

    ASSERT_NE(occluder->isInstanceValid, nullptr);
    const Occluder3DComponent valid{};
    EXPECT_TRUE(occluder->isInstanceValid(&valid));
    Occluder3DComponent broken = valid;
    broken.halfExtents.x = 0.0F;
    EXPECT_FALSE(occluder->isInstanceValid(&broken));
}

TEST(SceneComponentMetadataTest, TheBoneAttachmentSectionDeclaresTheReferenceTheBoneAndTheAnswer) {
    // The skeleton is an entity reference, which a text field cannot author and which the serializer
    // has to remap through its file-local id table - the same contract the mixer's target and the
    // visibility parent carry. The two runtime answers the pass writes back each frame are declared
    // as runtime state, so a generic writer cannot author them and the Inspector still shows them.
    const TypeMetadataEntry* entry =
        FindSceneComponentMetadata(std::type_index(typeid(BoneAttachment3DComponent)));
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->typeId, "component.bone_attachment_3d");
    EXPECT_EQ(entry->displayName, "BoneAttachment3D");

    const TypeMetadataProperty* skeleton = FindProperty(*entry, "skeleton");
    ASSERT_NE(skeleton, nullptr);
    EXPECT_TRUE(skeleton->flags.Has(TypeMetadataPropertyFlags::EntityReference));
    EXPECT_EQ(skeleton->typeId, kPropertyTypeEntity);
    EXPECT_FALSE(skeleton->flags.Has(TypeMetadataPropertyFlags::RuntimeState));

    const char* const kAuthored[] = {"enabled",     "boneName",      "boneIndex",
                                     "localPosition", "localRotation", "localScale"};
    for (const char* const name : kAuthored) {
        const TypeMetadataProperty* property = FindProperty(*entry, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_FALSE(property->flags.Has(TypeMetadataPropertyFlags::RuntimeState)) << name;
    }
    for (const char* const name : {"bound", "resolvedBoneIndex"}) {
        const TypeMetadataProperty* property = FindProperty(*entry, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_TRUE(property->flags.Has(TypeMetadataPropertyFlags::RuntimeState)) << name;
    }

    // The bone index defaults to "ask the name": that is the sentinel the pass reads, so a component
    // nobody has edited binds by name instead of silently to bone zero.
    const Core::TypeInstance defaults = Core::TypeInstance::MakeDefault(*entry);
    ASSERT_TRUE(defaults.IsValid());
    std::uint32_t boneIndex = 0U;
    const TypeMetadataProperty* indexProperty = FindProperty(*entry, "boneIndex");
    ASSERT_NE(indexProperty, nullptr);
    indexProperty->getValue(defaults.Get(), &boneIndex);
    EXPECT_EQ(boneIndex, kInvalidSkeletonBoneIndex);

    // The component's own validity rule travels with the declaration, so a generic writer cannot
    // author a flattened or non-finite offset and have it accepted.
    ASSERT_NE(entry->isInstanceValid, nullptr);
    const BoneAttachment3DComponent valid{};
    EXPECT_TRUE(entry->isInstanceValid(&valid));
    BoneAttachment3DComponent flattened = valid;
    flattened.localScale.y = 0.0F;
    EXPECT_FALSE(entry->isInstanceValid(&flattened));
    BoneAttachment3DComponent notFinite = valid;
    notFinite.localPosition.x = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(entry->isInstanceValid(&notFinite));
}

TEST(SceneComponentMetadataTest, TheTwoBoneIKSectionDeclaresThreeReferencesTheChainAndTheAnswer) {
    // Three entity references - the skeleton, the object it reaches for, and the pole that picks which
    // way the joint bends - each carrying the flag the serializer reads to remap it through its
    // file-local id table, exactly like the attachment's skeleton. The answers one solve writes back
    // are runtime state, so authoring cannot write them and the Inspector still shows them.
    const TypeMetadataEntry* entry =
        FindSceneComponentMetadata(std::type_index(typeid(TwoBoneIK3DComponent)));
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->typeId, "component.two_bone_ik_3d");
    EXPECT_EQ(entry->displayName, "TwoBoneIK3D");

    for (const char* const name : {"skeleton", "target", "poleTarget"}) {
        const TypeMetadataProperty* reference = FindProperty(*entry, name);
        ASSERT_NE(reference, nullptr) << name;
        EXPECT_TRUE(reference->flags.Has(TypeMetadataPropertyFlags::EntityReference)) << name;
        EXPECT_EQ(reference->typeId, kPropertyTypeEntity) << name;
        EXPECT_FALSE(reference->flags.Has(TypeMetadataPropertyFlags::RuntimeState)) << name;
    }

    const char* const kAuthored[] = {"enabled",         "rootBoneName",    "rootBoneIndex",
                                     "middleBoneName",  "middleBoneIndex", "endBoneName",
                                     "endBoneIndex",    "targetPosition",  "poleDirection"};
    for (const char* const name : kAuthored) {
        const TypeMetadataProperty* property = FindProperty(*entry, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_FALSE(property->flags.Has(TypeMetadataPropertyFlags::RuntimeState)) << name;
    }
    for (const char* const name : {"solved", "reached", "endToTargetDistanceMetres", "resolvedRootBoneIndex",
                                   "resolvedMiddleBoneIndex", "resolvedEndBoneIndex"}) {
        const TypeMetadataProperty* property = FindProperty(*entry, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_TRUE(property->flags.Has(TypeMetadataPropertyFlags::RuntimeState)) << name;
    }

    // Every bone index defaults to "ask the name": the sentinel the pass reads, so a chain nobody has
    // edited binds by name instead of silently to bone zero.
    const Core::TypeInstance defaults = Core::TypeInstance::MakeDefault(*entry);
    ASSERT_TRUE(defaults.IsValid());
    for (const char* const name : {"rootBoneIndex", "middleBoneIndex", "endBoneIndex"}) {
        const TypeMetadataProperty* index = FindProperty(*entry, name);
        ASSERT_NE(index, nullptr) << name;
        std::uint32_t value = 0U;
        index->getValue(defaults.Get(), &value);
        EXPECT_EQ(value, kInvalidSkeletonBoneIndex) << name;
    }

    // The component's own validity rule travels with the declaration, so a generic writer cannot author
    // a name with a byte in it or a non-finite target point and have it accepted.
    ASSERT_NE(entry->isInstanceValid, nullptr);
    const TwoBoneIK3DComponent valid{};
    EXPECT_TRUE(entry->isInstanceValid(&valid));
    TwoBoneIK3DComponent forgedName = valid;
    forgedName.middleBoneName = std::string("elbow\0", 6U);
    EXPECT_FALSE(entry->isInstanceValid(&forgedName));
    TwoBoneIK3DComponent notFinite = valid;
    notFinite.targetPosition.z = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(entry->isInstanceValid(&notFinite));
}

TEST(SceneComponentMetadataTest, TheFullBodyIKSectionDeclaresThePullsTheBudgetAndTheAnswer) {
    // The skeleton is an entity reference carrying the flag the serializer reads to remap it through
    // its file-local id table; the effectors are a list drawn by their own drawer; the solver's
    // budget is three bounded numbers. The answers one solve writes back - did it run, did it
    // converge, what the budget cost - are runtime state, so authoring cannot write them and the
    // Inspector still shows them.
    const TypeMetadataEntry* entry =
        FindSceneComponentMetadata(std::type_index(typeid(FullBodyIK3DComponent)));
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->typeId, "component.full_body_ik_3d");
    EXPECT_EQ(entry->displayName, "FullBodyIK3D");

    const TypeMetadataProperty* reference = FindProperty(*entry, "skeleton");
    ASSERT_NE(reference, nullptr);
    EXPECT_TRUE(reference->flags.Has(TypeMetadataPropertyFlags::EntityReference));
    EXPECT_EQ(reference->typeId, kPropertyTypeEntity);
    EXPECT_FALSE(reference->flags.Has(TypeMetadataPropertyFlags::RuntimeState));

    for (const char* const name : {"enabled", "effectors", "rootBoneName", "rootBoneIndex", "maxIterations",
                                   "toleranceMetres", "rootTranslationWeight", "maxJointStepRadians"}) {
        const TypeMetadataProperty* property = FindProperty(*entry, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_FALSE(property->flags.Has(TypeMetadataPropertyFlags::RuntimeState)) << name;
    }
    for (const char* const name : {"solved", "converged", "iterationsUsed"}) {
        const TypeMetadataProperty* property = FindProperty(*entry, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_TRUE(property->flags.Has(TypeMetadataPropertyFlags::RuntimeState)) << name;
    }

    // The root bone index defaults to "ask the name": the sentinel the pass reads, so a solve nobody
    // has edited binds by name instead of silently to bone zero.
    const Core::TypeInstance defaults = Core::TypeInstance::MakeDefault(*entry);
    ASSERT_TRUE(defaults.IsValid());
    const TypeMetadataProperty* index = FindProperty(*entry, "rootBoneIndex");
    ASSERT_NE(index, nullptr);
    std::uint32_t value = 0U;
    index->getValue(defaults.Get(), &value);
    EXPECT_EQ(value, kInvalidSkeletonBoneIndex);

    // The component's own validity rule travels with the declaration, so a generic writer cannot
    // author a budget outside its bounds and have it accepted.
    ASSERT_NE(entry->isInstanceValid, nullptr);
    const FullBodyIK3DComponent valid{};
    EXPECT_TRUE(entry->isInstanceValid(&valid));
    FullBodyIK3DComponent wrongBudget = valid;
    wrongBudget.maxIterations = kMaximumFullBodyIKIterations + 1U;
    EXPECT_FALSE(entry->isInstanceValid(&wrongBudget));
    FullBodyIK3DComponent notFinite = valid;
    notFinite.toleranceMetres = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(entry->isInstanceValid(&notFinite));
}

TEST(SceneComponentMetadataTest, TheDecalResultSectionShowsTheCountdownWithoutLettingAuthoringWriteIt) {
    // A decal's authored half is the projection; its runtime half is where the lifetime has got to.
    // The countdown has to be READABLE - an author watching Play wants to see it age - and it must
    // be neither writable nor saved, because the renderer owns the number and a restored decal
    // re-arms it. The same treatment Projectile3D's remaining flight gets, for the same reason.
    const TypeMetadataEntry* decal =
        FindSceneComponentMetadata(std::type_index(typeid(Decal3DComponent)));
    ASSERT_NE(decal, nullptr);
    for (const char* const name : {"remainingLifetime", "expired"}) {
        const TypeMetadataProperty* property = FindProperty(*decal, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_TRUE(property->flags.Has(TypeMetadataPropertyFlags::RuntimeState))
            << name;
        EXPECT_FALSE(property->IsAuthoringWritable()) << name;
        EXPECT_FALSE(property->IsSerialized()) << name;
        EXPECT_NE(property->getValue, nullptr) << name;
        EXPECT_EQ(property->section, "Result") << name;
    }

    // The authored half stays authoring's: the lifetime is the input the countdown is derived from.
    const TypeMetadataProperty* lifetime = FindProperty(*decal, "lifetime");
    ASSERT_NE(lifetime, nullptr);
    EXPECT_FALSE(lifetime->flags.Has(TypeMetadataPropertyFlags::RuntimeState));
    EXPECT_TRUE(lifetime->IsAuthoringWritable());
}

TEST(SceneComponentMetadataTest, TheRayCastSectionCarriesWhatTheRayIsAndWhatItFound) {
    // A RayCast3D is both authored data (what it is, what it refuses to hit) and runtime state (what
    // it found). Both halves have to reach the Inspector, and the authored half has to include the
    // exclusions list - a reference an author cannot pick is a feature no one can use.
    const TypeMetadataEntry* rayCast =
        FindSceneComponentMetadata(std::type_index(typeid(RayCast3DComponent)));
    ASSERT_NE(rayCast, nullptr);
    EXPECT_EQ(rayCast->typeId, "component.ray_cast_3d");
    EXPECT_EQ(rayCast->displayName, "RayCast3D");

    for (const char* const name : {"enabled", "direction", "length", "collisionMask", "exclusions"}) {
        const TypeMetadataProperty* property = FindProperty(*rayCast, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_FALSE(property->flags.Has(TypeMetadataPropertyFlags::RuntimeState))
            << name;
        EXPECT_TRUE(property->IsAuthoringWritable()) << name;
    }

    const TypeMetadataProperty* mask = FindProperty(*rayCast, "collisionMask");
    ASSERT_NE(mask, nullptr);
    EXPECT_EQ(mask->typeId, kPropertyTypeBitMask32);
    EXPECT_EQ(mask->customDrawerId, kLayerMaskDrawerPhysics);

    // The exclusions are an entity-reference list: a list value with a stated capacity, drawn by
    // the reference-list drawer, and read/written whole the way every other property is.
    const TypeMetadataProperty* exclusions = FindProperty(*rayCast, "exclusions");
    ASSERT_NE(exclusions, nullptr);
    EXPECT_EQ(exclusions->typeId, kPropertyTypeEntityList);
    EXPECT_EQ(exclusions->customDrawerId, "entity-reference-list");
    EXPECT_EQ(exclusions->elementCount, kMaximumRayCastExclusions);

    // A named alias, because the comma in the template argument list would otherwise split the
    // EXPECT_EQ macro's own argument list.
    using ExclusionList = std::array<Entity, kMaximumRayCastExclusions>;
    RayCast3DComponent component;
    ExclusionList references = MakeEmptyEntityReferences<kMaximumRayCastExclusions>();
    references[0] = Entity{4U, 2U};
    references[1] = Entity{9U, 1U};
    SetPropertyValue(*exclusions, component, references);
    EXPECT_EQ(component.exclusions[0], (Entity{4U, 2U}));
    EXPECT_EQ(component.exclusions[1], (Entity{9U, 1U}));
    EXPECT_EQ(CountRayCast3DExclusions(component), 2U);
    EXPECT_EQ(GetPropertyValue<ExclusionList>(*exclusions, component), references);

    // The component's own rule travels with the declaration, so the list control cannot author a
    // hole: a live reference behind an empty slot is refused by the same validator the engine reads.
    ASSERT_NE(rayCast->isInstanceValid, nullptr);
    RayCast3DComponent holed = component;
    holed.exclusions[0] = kInvalidEntity;
    EXPECT_FALSE(rayCast->isInstanceValid(&holed));

    // The answer the engine computed is shown, but never authored or persisted.
    ASSERT_TRUE(rayCast->properties.size() >= 9U);
    for (const char* const name : {"hit", "hitEntity", "hitPosition", "hitNormal"}) {
        const TypeMetadataProperty* property = FindProperty(*rayCast, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_TRUE(property->flags.Has(TypeMetadataPropertyFlags::RuntimeState))
            << name;
        EXPECT_FALSE(property->IsAuthoringWritable()) << name;
        EXPECT_FALSE(property->IsSerialized()) << name;
    }
}
TEST(SceneComponentMetadataTest, TheProjectileSectionCarriesItsHitContractAndItsLastContact) {
    // A Projectile3D is the one physics object whose *hit response* is authored: the engine owns
    // the motion, so the policy and the two coefficients have to reach the Inspector, and the
    // contact the engine resolved has to be visible without being authorable or persisted.
    const TypeMetadataEntry* projectile =
        FindSceneComponentMetadata(std::type_index(typeid(Projectile3DComponent)));
    ASSERT_NE(projectile, nullptr);
    EXPECT_EQ(projectile->typeId, "component.projectile_3d");
    EXPECT_EQ(projectile->displayName, "Projectile3D");

    for (const char* const name :
         {"active", "velocity", "acceleration", "radius", "maxLifetime", "collisionMask", "ignoreEntity",
          "hitPolicy", "restitution", "friction"}) {
        const TypeMetadataProperty* property = FindProperty(*projectile, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_FALSE(property->flags.Has(TypeMetadataPropertyFlags::RuntimeState))
            << name;
        EXPECT_TRUE(property->IsAuthoringWritable()) << name;
    }

    const TypeMetadataProperty* mask = FindProperty(*projectile, "collisionMask");
    ASSERT_NE(mask, nullptr);
    EXPECT_EQ(mask->typeId, kPropertyTypeBitMask32);
    EXPECT_EQ(mask->customDrawerId, kLayerMaskDrawerPhysics);

    const TypeMetadataProperty* ignoreEntity = FindProperty(*projectile, "ignoreEntity");
    ASSERT_NE(ignoreEntity, nullptr);
    EXPECT_EQ(ignoreEntity->typeId, kPropertyTypeEntity);
    EXPECT_TRUE(ignoreEntity->flags.Has(TypeMetadataPropertyFlags::EntityReference));
    EXPECT_TRUE(ignoreEntity->IsAuthoringWritable());

    // The policy is an enum with exactly the two motions the engine can execute, and a generic
    // consumer authors it the way it authors LightType - through int64, never the enum type.
    const TypeMetadataProperty* policy = FindProperty(*projectile, "hitPolicy");
    ASSERT_NE(policy, nullptr);
    ASSERT_EQ(policy->enumEntries.size(), 2U);
    EXPECT_EQ(policy->enumEntries[0].label, "Stop");
    EXPECT_EQ(policy->enumEntries[1].label, "Bounce");
    Projectile3DComponent component;
    SetPropertyValue(*policy, component, policy->enumEntries[1].value);
    EXPECT_EQ(component.hitPolicy, Projectile3DHitPolicy::Bounce);
    EXPECT_EQ(GetPropertyValue<std::int64_t>(*policy, component), 1);

    // The coefficients are bounded to 0..1 by the declaration's own range, and they are shown only
    // while the policy actually uses them: a restitution row under "Stop" is a lie.
    for (const char* const name : {"restitution", "friction"}) {
        const TypeMetadataProperty* property = FindProperty(*projectile, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_EQ(property->typeId, kPropertyTypeFloat);
        ASSERT_TRUE(property->range.enabled) << name;
        EXPECT_DOUBLE_EQ(property->range.minimum, 0.0) << name;
        EXPECT_DOUBLE_EQ(property->range.maximum, 1.0) << name;
        ASSERT_NE(property->isVisible, nullptr) << name;
        Projectile3DComponent stopped;
        EXPECT_FALSE(property->isVisible(&stopped)) << name;
        Projectile3DComponent bouncing;
        bouncing.hitPolicy = Projectile3DHitPolicy::Bounce;
        EXPECT_TRUE(property->isVisible(&bouncing)) << name;
    }

    // The runtime half: the countdown and the last resolved contact. Runtime-owned, readable, and
    // never persisted - saving where this projectile last landed would restore a stale contact
    // onto a projectile that has not flown yet.
    for (const char* const name :
         {"remainingLifetime", "hit", "hitEntity", "hitPosition", "hitNormal", "impactSpeed", "bounceCount"}) {
        const TypeMetadataProperty* property = FindProperty(*projectile, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_TRUE(property->flags.Has(TypeMetadataPropertyFlags::RuntimeState))
            << name;
        EXPECT_FALSE(property->IsAuthoringWritable()) << name;
        EXPECT_FALSE(property->IsSerialized()) << name;
    }
    EXPECT_EQ(FindProperty(*projectile, "remainingLifetime")->section, "State");
    EXPECT_EQ(FindProperty(*projectile, "impactSpeed")->section, "Result");

    // The component's own rule travels with the declaration, so no generic edit can author an
    // unknown policy or a coefficient outside the range the step would refuse.
    ASSERT_NE(projectile->isInstanceValid, nullptr);
    EXPECT_TRUE(projectile->isInstanceValid(&component));
    Projectile3DComponent unknownPolicy;
    unknownPolicy.hitPolicy = static_cast<Projectile3DHitPolicy>(9U);
    EXPECT_FALSE(projectile->isInstanceValid(&unknownPolicy));
    Projectile3DComponent outOfRange;
    outOfRange.restitution = 1.5F;
    EXPECT_FALSE(projectile->isInstanceValid(&outOfRange));
}

TEST(SceneComponentMetadataTest, TheCombatSectionsCarryTheStrikeContractAndItsRuntimeResult) {
    // A strike is authored by two components that have to agree, so both declarations have to
    // reach the Inspector together: what the box is (half extents), who may hit whom (layer and
    // mask on each side), and what kind of hit it is (the damage channel). The result of the
    // engine's own scan is visible on the hitbox - the side that does the striking - and is
    // runtime-owned like every other resolved result in this engine.
    const TypeMetadataEntry* hitbox =
        FindSceneComponentMetadata(std::type_index(typeid(Hitbox3DComponent)));
    ASSERT_NE(hitbox, nullptr);
    EXPECT_EQ(hitbox->typeId, "component.hitbox_3d");
    EXPECT_EQ(hitbox->displayName, "Hitbox3D");
    const TypeMetadataEntry* hurtbox =
        FindSceneComponentMetadata(std::type_index(typeid(Hurtbox3DComponent)));
    ASSERT_NE(hurtbox, nullptr);
    EXPECT_EQ(hurtbox->typeId, "component.hurtbox_3d");
    EXPECT_EQ(hurtbox->displayName, "Hurtbox3D");

    for (const char* const name : {"enabled", "halfExtents", "damageChannel"}) {
        for (const TypeMetadataEntry* const entry : {hitbox, hurtbox}) {
            const TypeMetadataProperty* property = FindProperty(*entry, name);
            ASSERT_NE(property, nullptr) << entry->typeId << "." << name;
            EXPECT_FALSE(property->flags.Has(TypeMetadataPropertyFlags::RuntimeState))
                << name;
            EXPECT_TRUE(property->IsAuthoringWritable()) << name;
        }
    }
    for (const TypeMetadataEntry* const entry : {hitbox, hurtbox}) {
        for (const char* const name : {"collisionLayer", "collisionMask"}) {
            const TypeMetadataProperty* property = FindProperty(*entry, name);
            ASSERT_NE(property, nullptr) << entry->typeId << "." << name;
            EXPECT_EQ(property->typeId, kPropertyTypeBitMask32) << name;
            EXPECT_EQ(property->customDrawerId, kLayerMaskDrawerPhysics) << name;
        }
    }

    const TypeMetadataProperty* count = FindProperty(*hitbox, "strikeCount");
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(count->typeId, kPropertyTypeUInt8);
    EXPECT_EQ(count->section, "Result");
    const TypeMetadataProperty* truncated = FindProperty(*hitbox, "strikesTruncated");
    ASSERT_NE(truncated, nullptr);
    EXPECT_EQ(truncated->typeId, kPropertyTypeBool);
    EXPECT_EQ(truncated->section, "Result");
    for (const TypeMetadataEntry* const entry : {hitbox, hurtbox}) {
        const TypeMetadataProperty* ignoreEntity = FindProperty(*entry, "ignoreEntity");
        ASSERT_NE(ignoreEntity, nullptr) << entry->typeId;
        EXPECT_EQ(ignoreEntity->typeId, kPropertyTypeEntity);
        EXPECT_TRUE(ignoreEntity->flags.Has(TypeMetadataPropertyFlags::EntityReference));
        EXPECT_TRUE(ignoreEntity->IsAuthoringWritable());
    }

    const TypeMetadataProperty* landed = FindProperty(*hitbox, "struckCount");
    ASSERT_NE(landed, nullptr);
    EXPECT_EQ(landed->typeId, kPropertyTypeUInt8);
    EXPECT_EQ(landed->section, "Result");
    for (const TypeMetadataProperty& property : hitbox->properties) {
        if (property.name == "strikeCount" || property.name == "strikesTruncated" ||
            property.name == "struckCount") {
            EXPECT_TRUE(property.flags.Has(TypeMetadataPropertyFlags::RuntimeState))
                << property.name;
            EXPECT_FALSE(property.IsAuthoringWritable()) << property.name;
            EXPECT_FALSE(property.IsSerialized()) << property.name;
        }
    }
    const TypeMetadataProperty* hits = FindProperty(*hurtbox, "hitCount");
    ASSERT_NE(hits, nullptr);
    EXPECT_EQ(hits->typeId, kPropertyTypeUInt8);
    EXPECT_EQ(hits->section, "Result");
    for (const TypeMetadataProperty& property : hurtbox->properties) {
        if (property.name == "hitCount" || property.name == "hitsTruncated" ||
            property.name == "receivedCount") {
            EXPECT_TRUE(property.flags.Has(TypeMetadataPropertyFlags::RuntimeState))
                << property.name;
            EXPECT_FALSE(property.IsAuthoringWritable()) << property.name;
            EXPECT_FALSE(property.IsSerialized()) << property.name;
        }
    }
    EXPECT_EQ(FindProperty(*hurtbox, "strikeCount"), nullptr);
    EXPECT_EQ(FindProperty(*hitbox, "hitCount"), nullptr);

    // The one-byte count is declared as one byte wide, and the component's own rule travels with
    // both declarations so no generic edit can author a box that cannot strike or receive.
    static_assert(sizeof(Hitbox3DComponent::strikeCount) == 1U,
                  "strikeCount is declared UInt8, so its storage must be one byte wide");
    Hitbox3DComponent hitboxValue;
    EXPECT_EQ(hitboxValue.strikeCount, 0U);
    EXPECT_FALSE(hitboxValue.strikesTruncated);
    ASSERT_NE(hitbox->isInstanceValid, nullptr);
    ASSERT_NE(hurtbox->isInstanceValid, nullptr);
    EXPECT_TRUE(hitbox->isInstanceValid(&hitboxValue));
    Hurtbox3DComponent hurtboxValue;
    EXPECT_TRUE(hurtbox->isInstanceValid(&hurtboxValue));
    Hitbox3DComponent degenerate;
    degenerate.halfExtents = {0.0F, 1.0F, 1.0F};
    EXPECT_FALSE(hitbox->isInstanceValid(&degenerate));
    Hurtbox3DComponent layerless;
    layerless.collisionLayer = 0U;
    EXPECT_FALSE(hurtbox->isInstanceValid(&layerless));
    Hurtbox3DComponent overlongChannel;
    overlongChannel.damageChannel = std::string(300U, 'x');
    EXPECT_FALSE(hurtbox->isInstanceValid(&overlongChannel));
}

TEST(SceneComponentMetadataTest, TheInteractionAreaSectionCarriesIgnoreAndRuntimeOccupancy) {
    const TypeMetadataEntry* area =
        FindSceneComponentMetadata(std::type_index(typeid(InteractionArea3DComponent)));
    ASSERT_NE(area, nullptr);
    EXPECT_EQ(area->typeId, "component.interaction_area_3d");
    EXPECT_EQ(area->displayName, "InteractionArea3D");

    for (const char* const name :
         {"enabled", "halfExtents", "interactionTag", "maximumCandidates", "collisionLayer", "collisionMask",
          "ignoreEntity"}) {
        const TypeMetadataProperty* property = FindProperty(*area, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_TRUE(property->IsAuthoringWritable()) << name;
    }
    const TypeMetadataProperty* ignoreEntity = FindProperty(*area, "ignoreEntity");
    ASSERT_NE(ignoreEntity, nullptr);
    EXPECT_TRUE(ignoreEntity->flags.Has(TypeMetadataPropertyFlags::EntityReference));
    for (const char* const name : {"interactorCount", "interactorsTruncated", "focusedByPrimaryInteractor"}) {
        const TypeMetadataProperty* property = FindProperty(*area, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_TRUE(property->flags.Has(TypeMetadataPropertyFlags::RuntimeState)) << name;
        EXPECT_FALSE(property->IsAuthoringWritable()) << name;
        EXPECT_EQ(property->section, "Result");
    }
    ASSERT_NE(area->isInstanceValid, nullptr);
    InteractionArea3DComponent value;
    EXPECT_TRUE(area->isInstanceValid(&value));
    value.maximumCandidates = 0U;
    EXPECT_FALSE(area->isInstanceValid(&value));
}

TEST(SceneComponentMetadataTest, TheArea3DSectionCarriesSpaceOverrideAndOccupancy) {
    const TypeMetadataEntry* area = FindSceneComponentMetadata(std::type_index(typeid(AreaComponent)));
    ASSERT_NE(area, nullptr);
    EXPECT_EQ(area->typeId, "component.area");
    EXPECT_EQ(area->displayName, "Area3D");

    for (const char* const name :
         {"halfExtents", "collisionLayer", "collisionMask", "monitoring", "monitorable", "priority",
          "gravityOverride", "gravityPoint", "gravityDirection", "gravityMagnitude",
          "gravityPointOffset", "gravityPointUnitDistance", "linearDampOverride", "linearDamp",
          "angularDampOverride", "angularDamp"}) {
        const TypeMetadataProperty* property = FindProperty(*area, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_FALSE(property->flags.Has(TypeMetadataPropertyFlags::RuntimeState))
            << name;
        EXPECT_TRUE(property->IsAuthoringWritable()) << name;
    }
    for (const char* const name : {"collisionLayer", "collisionMask"}) {
        const TypeMetadataProperty* property = FindProperty(*area, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_EQ(property->typeId, kPropertyTypeBitMask32) << name;
        EXPECT_EQ(property->customDrawerId, kLayerMaskDrawerPhysics) << name;
    }
    for (const char* const name :
         {"overlappingBodyCount", "overlappingBodiesTruncated", "overlappingAreaCount",
          "overlappingAreasTruncated"}) {
        const TypeMetadataProperty* property = FindProperty(*area, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_TRUE(property->flags.Has(TypeMetadataPropertyFlags::RuntimeState))
            << name;
        EXPECT_FALSE(property->IsAuthoringWritable()) << name;
    }

    ASSERT_NE(area->isInstanceValid, nullptr);
    AreaComponent valid{};
    EXPECT_TRUE(area->isInstanceValid(&valid));
    AreaComponent unknownMode{};
    unknownMode.gravityOverride = static_cast<AreaSpaceOverrideMode>(9U);
    EXPECT_FALSE(area->isInstanceValid(&unknownMode));
}

TEST(SceneComponentMetadataTest, EveryComponentEntryNamesItsCppType) {
    // Auto-discovery joins the registry to the serializer's component table on cppName: an
    // entry without one is invisible to generic serialization, and a duplicate would fight
    // over one table slot. This is the drift guard for both.
    const Core::TypeMetadataRegistry& registry = GetSceneComponentMetadataRegistry();
    std::vector<std::string> names;
    for (const TypeMetadataEntry& entry : registry.GetSnapshot().entries) {
        EXPECT_FALSE(entry.cppName.empty()) << entry.typeId;
        names.push_back(entry.cppName);
    }
    ASSERT_FALSE(names.empty());
    std::sort(names.begin(), names.end());
    EXPECT_EQ(std::adjacent_find(names.begin(), names.end()), names.end());
}

} // namespace
} // namespace UVE::Scene::Tests
