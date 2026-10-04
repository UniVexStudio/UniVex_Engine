// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/scene_component_metadata_uve.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <typeindex>
#include <vector>

#include "uve/component/collider_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/physics_interpolation_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/objects/3d/bone_attachment_3d_uve.h"
#include "uve/objects/3d/hitbox_3d_uve.h"
#include "uve/objects/3d/hurtbox_3d_uve.h"
#include "uve/objects/3d/decal_3d_uve.h"
#include "uve/objects/3d/lod_group_3d_uve.h"
#include "uve/objects/3d/projectile_3d_uve.h"
#include "uve/objects/3d/ray_cast_3d_uve.h"
#include "uve/objects/3d/spawn_point_3d_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene::Tests {
namespace {

using Core::GetPropertyValueUVE;
using Core::HasPropertyFlagUVE;
using Core::SetPropertyValueUVE;
using Core::TypeMetadataEntryUVE;
using Core::TypeMetadataPropertyFlagsUVE;
using Core::TypeMetadataPropertyUVE;

[[nodiscard]] const TypeMetadataPropertyUVE* FindPropertyUVE(const TypeMetadataEntryUVE& entry,
                                                             const std::string& name) {
    const auto iterator = std::find_if(entry.properties.cbegin(), entry.properties.cend(),
                                       [&name](const auto& property) { return property.name == name; });
    return iterator == entry.properties.cend() ? nullptr : &*iterator;
}

TEST(SceneComponentMetadataUVETest, EveryDeclarationRegistersAndCarriesItsNativeType) {
    // A declaration in scene_component_metadata_uve.cpp is a compile-time literal, so a rejection
    // is a mistake in that file rather than a runtime condition. This is what catches it.
    const Core::TypeMetadataRegistryUVE& registry = GetSceneComponentMetadataRegistryUVE();
    ASSERT_GT(registry.GetTypeCountUVE(), 0U);
    EXPECT_EQ(registry.GetGenerationUVE(), registry.GetTypeCountUVE());

    for (const TypeMetadataEntryUVE& entry : registry.GetSnapshotUVE().entries) {
        EXPECT_EQ(entry.kind, Core::TypeMetadataKindUVE::Component) << entry.typeId;
        EXPECT_NE(entry.typeIndex, std::type_index(typeid(void))) << entry.typeId;
        EXPECT_TRUE(entry.HasFactoryUVE()) << entry.typeId;
        EXPECT_FALSE(entry.properties.empty()) << entry.typeId;
        for (const TypeMetadataPropertyUVE& property : entry.properties) {
            // A declared property must be able to read and write itself, or no generic consumer
            // can do anything with it.
            EXPECT_NE(property.getValue, nullptr) << entry.typeId << "." << property.name;
            EXPECT_NE(property.setValue, nullptr) << entry.typeId << "." << property.name;
            EXPECT_FALSE(property.typeId.empty()) << entry.typeId << "." << property.name;
        }
    }
}

TEST(SceneComponentMetadataUVETest, EveryLayerMaskNamesTheLayersItPicksFrom) {
    // A mask with no layer set would be drawn as bare hexadecimal, without the project's names.
    std::size_t physics = 0U;
    std::size_t render = 0U;
    for (const TypeMetadataEntryUVE& entry : GetSceneComponentMetadataRegistryUVE().GetSnapshotUVE().entries) {
        for (const TypeMetadataPropertyUVE& property : entry.properties) {
            if (property.typeId != kPropertyTypeBitMask32UVE) {
                continue;
            }
            const bool isPhysics = property.customDrawerId == kLayerMaskDrawerPhysicsUVE;
            const bool isRender = property.customDrawerId == kLayerMaskDrawerRenderUVE;
            EXPECT_TRUE(isPhysics || isRender) << entry.typeId << "." << property.name;
            physics += isPhysics ? 1U : 0U;
            render += isRender ? 1U : 0U;
        }
    }
    // The collider's layer and mask, SpringArm3D's collisionMask, RayCast3D's collisionMask,
    // Projectile3D's collisionMask, Hitbox3D/Hurtbox3D's layer and mask, and the navigation
    // region/agent layers - every cast, every swept body, every strike and every path in the engine
    // filters on the same layer contract, so they all belong to the same drawer set rather than a
    // second, hand-drawn one.
    EXPECT_EQ(physics, 11U);
    EXPECT_EQ(render, 4U); // mesh, render instance, light and decal
}

TEST(SceneComponentMetadataUVETest, FindSceneComponentMetadataUVE_ResolvesALiveComponentType) {
    const TypeMetadataEntryUVE* transform =
        FindSceneComponentMetadataUVE(std::type_index(typeid(TransformComponentUVE)));
    ASSERT_NE(transform, nullptr);
    EXPECT_EQ(transform->typeId, "component.transform");
    EXPECT_EQ(transform->order, kSectionOrderTransformUVE);

    // A component type that declares nothing resolves to nothing rather than to a wrong entry.
    EXPECT_EQ(FindSceneComponentMetadataUVE(std::type_index(typeid(int))), nullptr);
}

TEST(SceneComponentMetadataUVETest, DeclaredPropertiesReadAndWriteTheRealComponent) {
    const TypeMetadataEntryUVE* transform =
        FindSceneComponentMetadataUVE(std::type_index(typeid(TransformComponentUVE)));
    ASSERT_NE(transform, nullptr);
    const TypeMetadataPropertyUVE* position = FindPropertyUVE(*transform, "localPosition");
    ASSERT_NE(position, nullptr);

    TransformComponentUVE component;
    SetPropertyValueUVE(*position, component, Math::Vector3UVE{3.0F, 4.0F, 5.0F});
    EXPECT_FLOAT_EQ(component.localPosition.y, 4.0F);
    EXPECT_FLOAT_EQ(GetPropertyValueUVE<Math::Vector3UVE>(*position, component).z, 5.0F);
}

TEST(SceneComponentMetadataUVETest, EnumPropertiesRoundTripThroughInt64) {
    // The point of the int64 accessors: this test never names LightTypeUVE, exactly as a generic
    // consumer never can.
    const TypeMetadataEntryUVE* light = FindSceneComponentMetadataUVE(std::type_index(typeid(LightComponentUVE)));
    ASSERT_NE(light, nullptr);
    const TypeMetadataPropertyUVE* type = FindPropertyUVE(*light, "type");
    ASSERT_NE(type, nullptr);
    ASSERT_EQ(type->enumEntries.size(), 3U);
    EXPECT_EQ(type->enumEntries[2].label, "Spot");

    LightComponentUVE component;
    SetPropertyValueUVE(*type, component, type->enumEntries[2].value);
    EXPECT_EQ(component.type, LightTypeUVE::Spot);
    EXPECT_EQ(GetPropertyValueUVE<std::int64_t>(*type, component), 2);
}

TEST(SceneComponentMetadataUVETest, RuntimeStateIsRefusedForAuthoringAndForPersistence) {
    const TypeMetadataEntryUVE* visibility =
        FindSceneComponentMetadataUVE(std::type_index(typeid(VisibilityComponentUVE)));
    ASSERT_NE(visibility, nullptr);

    const TypeMetadataPropertyUVE* authored = FindPropertyUVE(*visibility, "visible");
    ASSERT_NE(authored, nullptr);
    EXPECT_TRUE(authored->IsAuthoringWritableUVE());

    // visibleInHierarchy is written only by SceneGraphUVE::UpdateUVE. Authoring it would be
    // overwritten on the next update; persisting it would restore a stale answer.
    const TypeMetadataPropertyUVE* derived = FindPropertyUVE(*visibility, "visibleInHierarchy");
    ASSERT_NE(derived, nullptr);
    EXPECT_FALSE(derived->IsAuthoringWritableUVE());
    EXPECT_FALSE(derived->IsSerializedUVE());

    // The same rule, applied to the interpolation system's working state.
    const TypeMetadataEntryUVE* interpolation =
        FindSceneComponentMetadataUVE(std::type_index(typeid(PhysicsInterpolationComponentUVE)));
    ASSERT_NE(interpolation, nullptr);
    const TypeMetadataPropertyUVE* mode = FindPropertyUVE(*interpolation, "mode");
    ASSERT_NE(mode, nullptr);
    EXPECT_TRUE(mode->IsAuthoringWritableUVE());
    const TypeMetadataPropertyUVE* resolved = FindPropertyUVE(*interpolation, "interpolatedInHierarchy");
    ASSERT_NE(resolved, nullptr);
    EXPECT_FALSE(resolved->IsAuthoringWritableUVE());
}

TEST(SceneComponentMetadataUVETest, EntityReferencesAreDeclaredRatherThanRediscovered) {
    // The serializer hand-special-cases exactly these two properties because they hold entity
    // references that need remapping through its file-local id table. Declaring the fact is what
    // lets a generic consumer find them instead of knowing their names.
    const TypeMetadataEntryUVE* visibility =
        FindSceneComponentMetadataUVE(std::type_index(typeid(VisibilityComponentUVE)));
    ASSERT_NE(visibility, nullptr);
    const TypeMetadataPropertyUVE* visibilityParent = FindPropertyUVE(*visibility, "visibilityParent");
    ASSERT_NE(visibilityParent, nullptr);
    EXPECT_TRUE(HasPropertyFlagUVE(visibilityParent->flags, TypeMetadataPropertyFlagsUVE::EntityReference));
    EXPECT_EQ(visibilityParent->customDrawerId, "visibility.parent");

    const TypeMetadataEntryUVE* hierarchy =
        FindSceneComponentMetadataUVE(std::type_index(typeid(HierarchyComponentUVE)));
    ASSERT_NE(hierarchy, nullptr);
    const TypeMetadataPropertyUVE* parent = FindPropertyUVE(*hierarchy, "parent");
    ASSERT_NE(parent, nullptr);
    EXPECT_TRUE(HasPropertyFlagUVE(parent->flags, TypeMetadataPropertyFlagsUVE::EntityReference));
}

TEST(SceneComponentMetadataUVETest, RotationIsRoutedToACustomDrawerRatherThanWrittenRaw) {
    // A generic editor writing localRotation alone would leave localEulerRadians and
    // rotationEditMode describing a different rotation than the quaternion does.
    const TypeMetadataEntryUVE* transform =
        FindSceneComponentMetadataUVE(std::type_index(typeid(TransformComponentUVE)));
    ASSERT_NE(transform, nullptr);
    const TypeMetadataPropertyUVE* rotation = FindPropertyUVE(*transform, "localRotation");
    ASSERT_NE(rotation, nullptr);
    EXPECT_EQ(rotation->customDrawerId, "transform.rotation");
}

TEST(SceneComponentMetadataUVETest, ConditionalVisibilityFollowsTheSiblingFieldItDependsOn) {
    const TypeMetadataEntryUVE* collider =
        FindSceneComponentMetadataUVE(std::type_index(typeid(ColliderComponentUVE)));
    ASSERT_NE(collider, nullptr);
    const TypeMetadataPropertyUVE* halfExtents = FindPropertyUVE(*collider, "halfExtents");
    const TypeMetadataPropertyUVE* height = FindPropertyUVE(*collider, "height");
    ASSERT_NE(halfExtents, nullptr);
    ASSERT_NE(height, nullptr);
    ASSERT_NE(halfExtents->isVisible, nullptr);
    ASSERT_NE(height->isVisible, nullptr);

    ColliderComponentUVE component;
    component.shapeType = ColliderShapeTypeUVE::Box;
    EXPECT_TRUE(halfExtents->isVisible(&component));
    EXPECT_FALSE(height->isVisible(&component));

    component.shapeType = ColliderShapeTypeUVE::Capsule;
    EXPECT_FALSE(halfExtents->isVisible(&component));
    EXPECT_TRUE(height->isVisible(&component));
}

// Sections follow the class chain from most to least derived: the object's own, then Object3D's
// Transform, then the common Object section.
TEST(SceneComponentMetadataUVETest, TheCommonObjectSectionSortsBelowEverythingTypeSpecific) {
    const TypeMetadataEntryUVE* transform =
        FindSceneComponentMetadataUVE(std::type_index(typeid(TransformComponentUVE)));
    const TypeMetadataEntryUVE* light = FindSceneComponentMetadataUVE(std::type_index(typeid(LightComponentUVE)));
    const TypeMetadataEntryUVE* interpolation =
        FindSceneComponentMetadataUVE(std::type_index(typeid(PhysicsInterpolationComponentUVE)));
    ASSERT_NE(transform, nullptr);
    ASSERT_NE(light, nullptr);
    ASSERT_NE(interpolation, nullptr);

    EXPECT_LT(light->order, transform->order);
    EXPECT_LT(transform->order, interpolation->order);
    EXPECT_GE(interpolation->order, kSectionOrderObjectCommonUVE);
}

TEST(SceneComponentMetadataUVETest, TheFactoryAnswersWhatAPropertysDefaultValueIs) {
    // This is reset-to-default, done without the caller naming the component type.
    const TypeMetadataEntryUVE* collider =
        FindSceneComponentMetadataUVE(std::type_index(typeid(ColliderComponentUVE)));
    ASSERT_NE(collider, nullptr);
    const Core::TypeInstanceUVE defaults = Core::TypeInstanceUVE::MakeDefaultUVE(*collider);
    ASSERT_TRUE(defaults.IsValidUVE());

    const TypeMetadataPropertyUVE* density = FindPropertyUVE(*collider, "density");
    ASSERT_NE(density, nullptr);
    float defaultDensity = 0.0F;
    density->getValue(defaults.GetUVE(), &defaultDensity);
    EXPECT_FLOAT_EQ(defaultDensity, ColliderComponentUVE{}.density);
}

TEST(SceneComponentMetadataUVETest, TheSpawnPointSectionCarriesTheWholeAuthoredContract) {
    // A SpawnPoint3D is authored data with no runtime half: everything it owns is what the query
    // reads, and the generic editor must be able to author all of it.
    const TypeMetadataEntryUVE* spawn =
        FindSceneComponentMetadataUVE(std::type_index(typeid(SpawnPoint3DComponentUVE)));
    ASSERT_NE(spawn, nullptr);
    EXPECT_EQ(spawn->typeId, "component.spawn_point");
    EXPECT_EQ(spawn->displayName, "SpawnPoint3D");

    const char* const kAuthored[] = {"spawnTag", "localPosition", "localRotation", "enabled",
                                     "oneShot"};
    for (const char* const name : kAuthored) {
        const TypeMetadataPropertyUVE* property = FindPropertyUVE(*spawn, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_FALSE(HasPropertyFlagUVE(property->flags, TypeMetadataPropertyFlagsUVE::RuntimeState))
            << name;
    }

    // The offsets are the point's own, not a second transform: their declared labels say so, and
    // the values default to a point that spawns exactly where it sits.
    const TypeMetadataPropertyUVE* offset = FindPropertyUVE(*spawn, "localPosition");
    ASSERT_NE(offset, nullptr);
    EXPECT_EQ(offset->displayName, "Offset Position");
    const Core::TypeInstanceUVE defaults = Core::TypeInstanceUVE::MakeDefaultUVE(*spawn);
    ASSERT_TRUE(defaults.IsValidUVE());
    Math::Vector3UVE position{1.0F, 1.0F, 1.0F};
    offset->getValue(defaults.GetUVE(), &position);
    EXPECT_EQ(position, SpawnPoint3DComponentUVE{}.localPosition);

    // The component's own rule travels with the declaration, so a generic writer cannot author an
    // empty tag or a non-finite offset and have it accepted.
    ASSERT_NE(spawn->isInstanceValid, nullptr);
    const SpawnPoint3DComponentUVE valid{};
    EXPECT_TRUE(spawn->isInstanceValid(&valid));
    SpawnPoint3DComponentUVE noTag = valid;
    noTag.spawnTag.clear();
    EXPECT_FALSE(spawn->isInstanceValid(&noTag));
    SpawnPoint3DComponentUVE notFinite = valid;
    notFinite.localPosition.z = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(spawn->isInstanceValid(&notFinite));
}


TEST(SceneComponentMetadataUVETest, TheLodGroupSectionCarriesTheChainTheMeshesAndTheResolvedAnswer) {
    // A LODGroup3D is a list component: the levels are a prefix of two parallel arrays, and the
    // count that decides the prefix is a field of the same component. All three have to reach the
    // Inspector for the chain to be authorable at all - an array no one can fill in is the same as
    // no LOD.
    const TypeMetadataEntryUVE* lod =
        FindSceneComponentMetadataUVE(std::type_index(typeid(LodGroup3DComponentUVE)));
    ASSERT_NE(lod, nullptr);
    EXPECT_EQ(lod->typeId, "component.lod_group_3d");
    EXPECT_EQ(lod->displayName, "LODGroup3D");

    for (const char* const name :
         {"enabled", "levelCount", "hysteresis", "distanceThresholds", "lodMeshGuids"}) {
        const TypeMetadataPropertyUVE* property = FindPropertyUVE(*lod, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_FALSE(HasPropertyFlagUVE(property->flags, TypeMetadataPropertyFlagsUVE::RuntimeState))
            << name;
        EXPECT_TRUE(property->IsAuthoringWritableUVE()) << name;
    }

    const TypeMetadataPropertyUVE* levels = FindPropertyUVE(*lod, "levelCount");
    ASSERT_NE(levels, nullptr);
    EXPECT_EQ(levels->typeId, kPropertyTypeUInt8UVE);
    ASSERT_TRUE(levels->range.enabled);
    EXPECT_DOUBLE_EQ(levels->range.minimum, 1.0);
    EXPECT_DOUBLE_EQ(levels->range.maximum, static_cast<double>(kMaximumLodLevelsUVE));

    const TypeMetadataPropertyUVE* hysteresis = FindPropertyUVE(*lod, "hysteresis");
    ASSERT_NE(hysteresis, nullptr);
    EXPECT_EQ(hysteresis->typeId, kPropertyTypeFloatUVE);
    ASSERT_TRUE(hysteresis->range.enabled);
    EXPECT_DOUBLE_EQ(hysteresis->range.minimum, 0.0);
    EXPECT_DOUBLE_EQ(hysteresis->range.maximum, static_cast<double>(kMaximumLodHysteresisUVE));

    // The two lists: a float chain and one mesh per level, both bounded by the same capacity and
    // both drawn by the section's own drawer rather than a typed row.
    const TypeMetadataPropertyUVE* thresholds = FindPropertyUVE(*lod, "distanceThresholds");
    ASSERT_NE(thresholds, nullptr);
    EXPECT_EQ(thresholds->typeId, kPropertyTypeFloatListUVE);
    EXPECT_EQ(thresholds->customDrawerId, "lod-group-thresholds");
    EXPECT_EQ(thresholds->elementCount, kMaximumLodLevelsUVE);

    const TypeMetadataPropertyUVE* meshes = FindPropertyUVE(*lod, "lodMeshGuids");
    ASSERT_NE(meshes, nullptr);
    EXPECT_EQ(meshes->typeId, kPropertyTypeAssetGuidListUVE);
    EXPECT_EQ(meshes->customDrawerId, "lod-group-meshes");
    EXPECT_EQ(meshes->elementCount, kMaximumLodLevelsUVE);

    // Whole-array accessors, like every other list property: one read, one write, one history
    // entry. A named alias, because the comma in the template argument list would otherwise split
    // the EXPECT_EQ macro's own argument list.
    using MeshListUVE = std::array<Asset::AssetGuidUVE, kMaximumLodLevelsUVE>;
    LodGroup3DComponentUVE component;
    MeshListUVE guids{};
    guids[1] = Asset::AssetGuidUVE{42U};
    SetPropertyValueUVE(*meshes, component, guids);
    EXPECT_EQ(component.lodMeshGuids[1], Asset::AssetGuidUVE{42U});
    EXPECT_EQ(GetPropertyValueUVE<MeshListUVE>(*meshes, component), guids);

    // The resolved answer is runtime state: the renderer owns it, so authoring must not offer to
    // write it and saving must not persist it. It still has to be READABLE, or the Inspector shows
    // nothing where the answer belongs.
    for (const char* const name : {"currentLevel", "culledByDistance"}) {
        const TypeMetadataPropertyUVE* property = FindPropertyUVE(*lod, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_TRUE(HasPropertyFlagUVE(property->flags, TypeMetadataPropertyFlagsUVE::RuntimeState))
            << name;
        EXPECT_FALSE(property->IsAuthoringWritableUVE()) << name;
        EXPECT_FALSE(property->IsSerializedUVE()) << name;
        EXPECT_NE(property->getValue, nullptr) << name;
        EXPECT_EQ(property->section, "Result") << name;
    }

    // The component's own rule travels with the declaration, so a band no level chain can honour is
    // refused at the same validator the engine reads.
    ASSERT_NE(lod->isInstanceValid, nullptr);
    LodGroup3DComponentUVE outOfRange;
    outOfRange.hysteresis = kMaximumLodHysteresisUVE + 0.1F;
    EXPECT_FALSE(lod->isInstanceValid(&outOfRange));
    EXPECT_TRUE(lod->isInstanceValid(&component));
}

TEST(SceneComponentMetadataUVETest, TheBoneAttachmentSectionDeclaresTheReferenceTheBoneAndTheAnswer) {
    // The skeleton is an entity reference, which a text field cannot author and which the serializer
    // has to remap through its file-local id table - the same contract the mixer's target and the
    // visibility parent carry. The two runtime answers the pass writes back each frame are declared
    // as runtime state, so a generic writer cannot author them and the Inspector still shows them.
    const TypeMetadataEntryUVE* entry =
        FindSceneComponentMetadataUVE(std::type_index(typeid(BoneAttachment3DComponentUVE)));
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->typeId, "component.bone_attachment_3d");
    EXPECT_EQ(entry->displayName, "BoneAttachment3D");

    const TypeMetadataPropertyUVE* skeleton = FindPropertyUVE(*entry, "skeleton");
    ASSERT_NE(skeleton, nullptr);
    EXPECT_TRUE(HasPropertyFlagUVE(skeleton->flags, TypeMetadataPropertyFlagsUVE::EntityReference));
    EXPECT_EQ(skeleton->typeId, kPropertyTypeEntityUVE);
    EXPECT_FALSE(HasPropertyFlagUVE(skeleton->flags, TypeMetadataPropertyFlagsUVE::RuntimeState));

    const char* const kAuthored[] = {"enabled",     "boneName",      "boneIndex",
                                     "localPosition", "localRotation", "localScale"};
    for (const char* const name : kAuthored) {
        const TypeMetadataPropertyUVE* property = FindPropertyUVE(*entry, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_FALSE(HasPropertyFlagUVE(property->flags, TypeMetadataPropertyFlagsUVE::RuntimeState)) << name;
    }
    for (const char* const name : {"bound", "resolvedBoneIndex"}) {
        const TypeMetadataPropertyUVE* property = FindPropertyUVE(*entry, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_TRUE(HasPropertyFlagUVE(property->flags, TypeMetadataPropertyFlagsUVE::RuntimeState)) << name;
    }

    // The bone index defaults to "ask the name": that is the sentinel the pass reads, so a component
    // nobody has edited binds by name instead of silently to bone zero.
    const Core::TypeInstanceUVE defaults = Core::TypeInstanceUVE::MakeDefaultUVE(*entry);
    ASSERT_TRUE(defaults.IsValidUVE());
    std::uint32_t boneIndex = 0U;
    const TypeMetadataPropertyUVE* indexProperty = FindPropertyUVE(*entry, "boneIndex");
    ASSERT_NE(indexProperty, nullptr);
    indexProperty->getValue(defaults.GetUVE(), &boneIndex);
    EXPECT_EQ(boneIndex, kInvalidSkeletonBoneIndexUVE);

    // The component's own validity rule travels with the declaration, so a generic writer cannot
    // author a flattened or non-finite offset and have it accepted.
    ASSERT_NE(entry->isInstanceValid, nullptr);
    const BoneAttachment3DComponentUVE valid{};
    EXPECT_TRUE(entry->isInstanceValid(&valid));
    BoneAttachment3DComponentUVE flattened = valid;
    flattened.localScale.y = 0.0F;
    EXPECT_FALSE(entry->isInstanceValid(&flattened));
    BoneAttachment3DComponentUVE notFinite = valid;
    notFinite.localPosition.x = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(entry->isInstanceValid(&notFinite));
}

TEST(SceneComponentMetadataUVETest, TheDecalResultSectionShowsTheCountdownWithoutLettingAuthoringWriteIt) {
    // A decal's authored half is the projection; its runtime half is where the lifetime has got to.
    // The countdown has to be READABLE - an author watching Play wants to see it age - and it must
    // be neither writable nor saved, because the renderer owns the number and a restored decal
    // re-arms it. The same treatment Projectile3D's remaining flight gets, for the same reason.
    const TypeMetadataEntryUVE* decal =
        FindSceneComponentMetadataUVE(std::type_index(typeid(Decal3DComponentUVE)));
    ASSERT_NE(decal, nullptr);
    for (const char* const name : {"remainingLifetime", "expired"}) {
        const TypeMetadataPropertyUVE* property = FindPropertyUVE(*decal, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_TRUE(HasPropertyFlagUVE(property->flags, TypeMetadataPropertyFlagsUVE::RuntimeState))
            << name;
        EXPECT_FALSE(property->IsAuthoringWritableUVE()) << name;
        EXPECT_FALSE(property->IsSerializedUVE()) << name;
        EXPECT_NE(property->getValue, nullptr) << name;
        EXPECT_EQ(property->section, "Result") << name;
    }

    // The authored half stays authoring's: the lifetime is the input the countdown is derived from.
    const TypeMetadataPropertyUVE* lifetime = FindPropertyUVE(*decal, "lifetime");
    ASSERT_NE(lifetime, nullptr);
    EXPECT_FALSE(HasPropertyFlagUVE(lifetime->flags, TypeMetadataPropertyFlagsUVE::RuntimeState));
    EXPECT_TRUE(lifetime->IsAuthoringWritableUVE());
}

TEST(SceneComponentMetadataUVETest, TheRayCastSectionCarriesWhatTheRayIsAndWhatItFound) {
    // A RayCast3D is both authored data (what it is, what it refuses to hit) and runtime state (what
    // it found). Both halves have to reach the Inspector, and the authored half has to include the
    // exclusions list - a reference an author cannot pick is a feature no one can use.
    const TypeMetadataEntryUVE* rayCast =
        FindSceneComponentMetadataUVE(std::type_index(typeid(RayCast3DComponentUVE)));
    ASSERT_NE(rayCast, nullptr);
    EXPECT_EQ(rayCast->typeId, "component.ray_cast_3d");
    EXPECT_EQ(rayCast->displayName, "RayCast3D");

    for (const char* const name : {"enabled", "direction", "length", "collisionMask", "exclusions"}) {
        const TypeMetadataPropertyUVE* property = FindPropertyUVE(*rayCast, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_FALSE(HasPropertyFlagUVE(property->flags, TypeMetadataPropertyFlagsUVE::RuntimeState))
            << name;
        EXPECT_TRUE(property->IsAuthoringWritableUVE()) << name;
    }

    const TypeMetadataPropertyUVE* mask = FindPropertyUVE(*rayCast, "collisionMask");
    ASSERT_NE(mask, nullptr);
    EXPECT_EQ(mask->typeId, kPropertyTypeBitMask32UVE);
    EXPECT_EQ(mask->customDrawerId, kLayerMaskDrawerPhysicsUVE);

    // The exclusions are an entity-reference list: a list value with a stated capacity, drawn by
    // the reference-list drawer, and read/written whole the way every other property is.
    const TypeMetadataPropertyUVE* exclusions = FindPropertyUVE(*rayCast, "exclusions");
    ASSERT_NE(exclusions, nullptr);
    EXPECT_EQ(exclusions->typeId, kPropertyTypeEntityListUVE);
    EXPECT_EQ(exclusions->customDrawerId, "entity-reference-list");
    EXPECT_EQ(exclusions->elementCount, kMaximumRayCastExclusionsUVE);

    // A named alias, because the comma in the template argument list would otherwise split the
    // EXPECT_EQ macro's own argument list.
    using ExclusionListUVE = std::array<EntityUVE, kMaximumRayCastExclusionsUVE>;
    RayCast3DComponentUVE component;
    ExclusionListUVE references = MakeEmptyEntityReferencesUVE<kMaximumRayCastExclusionsUVE>();
    references[0] = EntityUVE{4U, 2U};
    references[1] = EntityUVE{9U, 1U};
    SetPropertyValueUVE(*exclusions, component, references);
    EXPECT_EQ(component.exclusions[0], (EntityUVE{4U, 2U}));
    EXPECT_EQ(component.exclusions[1], (EntityUVE{9U, 1U}));
    EXPECT_EQ(CountRayCast3DExclusionsUVE(component), 2U);
    EXPECT_EQ(GetPropertyValueUVE<ExclusionListUVE>(*exclusions, component), references);

    // The component's own rule travels with the declaration, so the list control cannot author a
    // hole: a live reference behind an empty slot is refused by the same validator the engine reads.
    ASSERT_NE(rayCast->isInstanceValid, nullptr);
    RayCast3DComponentUVE holed = component;
    holed.exclusions[0] = kInvalidEntityUVE;
    EXPECT_FALSE(rayCast->isInstanceValid(&holed));

    // The answer the engine computed is shown, but never authored or persisted.
    ASSERT_TRUE(rayCast->properties.size() >= 9U);
    for (const char* const name : {"hit", "hitEntity", "hitPosition", "hitNormal"}) {
        const TypeMetadataPropertyUVE* property = FindPropertyUVE(*rayCast, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_TRUE(HasPropertyFlagUVE(property->flags, TypeMetadataPropertyFlagsUVE::RuntimeState))
            << name;
        EXPECT_FALSE(property->IsAuthoringWritableUVE()) << name;
        EXPECT_FALSE(property->IsSerializedUVE()) << name;
    }
}
TEST(SceneComponentMetadataUVETest, TheProjectileSectionCarriesItsHitContractAndItsLastContact) {
    // A Projectile3D is the one physics object whose *hit response* is authored: the engine owns
    // the motion, so the policy and the two coefficients have to reach the Inspector, and the
    // contact the engine resolved has to be visible without being authorable or persisted.
    const TypeMetadataEntryUVE* projectile =
        FindSceneComponentMetadataUVE(std::type_index(typeid(Projectile3DComponentUVE)));
    ASSERT_NE(projectile, nullptr);
    EXPECT_EQ(projectile->typeId, "component.projectile_3d");
    EXPECT_EQ(projectile->displayName, "Projectile3D");

    for (const char* const name :
         {"active", "velocity", "acceleration", "radius", "maxLifetime", "collisionMask", "hitPolicy",
          "restitution", "friction"}) {
        const TypeMetadataPropertyUVE* property = FindPropertyUVE(*projectile, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_FALSE(HasPropertyFlagUVE(property->flags, TypeMetadataPropertyFlagsUVE::RuntimeState))
            << name;
        EXPECT_TRUE(property->IsAuthoringWritableUVE()) << name;
    }

    const TypeMetadataPropertyUVE* mask = FindPropertyUVE(*projectile, "collisionMask");
    ASSERT_NE(mask, nullptr);
    EXPECT_EQ(mask->typeId, kPropertyTypeBitMask32UVE);
    EXPECT_EQ(mask->customDrawerId, kLayerMaskDrawerPhysicsUVE);

    // The policy is an enum with exactly the two motions the engine can execute, and a generic
    // consumer authors it the way it authors LightTypeUVE - through int64, never the enum type.
    const TypeMetadataPropertyUVE* policy = FindPropertyUVE(*projectile, "hitPolicy");
    ASSERT_NE(policy, nullptr);
    ASSERT_EQ(policy->enumEntries.size(), 2U);
    EXPECT_EQ(policy->enumEntries[0].label, "Stop");
    EXPECT_EQ(policy->enumEntries[1].label, "Bounce");
    Projectile3DComponentUVE component;
    SetPropertyValueUVE(*policy, component, policy->enumEntries[1].value);
    EXPECT_EQ(component.hitPolicy, Projectile3DHitPolicyUVE::Bounce);
    EXPECT_EQ(GetPropertyValueUVE<std::int64_t>(*policy, component), 1);

    // The coefficients are bounded to 0..1 by the declaration's own range, and they are shown only
    // while the policy actually uses them: a restitution row under "Stop" is a lie.
    for (const char* const name : {"restitution", "friction"}) {
        const TypeMetadataPropertyUVE* property = FindPropertyUVE(*projectile, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_EQ(property->typeId, kPropertyTypeFloatUVE);
        ASSERT_TRUE(property->range.enabled) << name;
        EXPECT_DOUBLE_EQ(property->range.minimum, 0.0) << name;
        EXPECT_DOUBLE_EQ(property->range.maximum, 1.0) << name;
        ASSERT_NE(property->isVisible, nullptr) << name;
        Projectile3DComponentUVE stopped;
        EXPECT_FALSE(property->isVisible(&stopped)) << name;
        Projectile3DComponentUVE bouncing;
        bouncing.hitPolicy = Projectile3DHitPolicyUVE::Bounce;
        EXPECT_TRUE(property->isVisible(&bouncing)) << name;
    }

    // The runtime half: the countdown and the last resolved contact. Runtime-owned, readable, and
    // never persisted - saving where this projectile last landed would restore a stale contact
    // onto a projectile that has not flown yet.
    for (const char* const name :
         {"remainingLifetime", "hit", "hitEntity", "hitPosition", "hitNormal", "impactSpeed", "bounceCount"}) {
        const TypeMetadataPropertyUVE* property = FindPropertyUVE(*projectile, name);
        ASSERT_NE(property, nullptr) << name;
        EXPECT_TRUE(HasPropertyFlagUVE(property->flags, TypeMetadataPropertyFlagsUVE::RuntimeState))
            << name;
        EXPECT_FALSE(property->IsAuthoringWritableUVE()) << name;
        EXPECT_FALSE(property->IsSerializedUVE()) << name;
    }
    EXPECT_EQ(FindPropertyUVE(*projectile, "remainingLifetime")->section, "State");
    EXPECT_EQ(FindPropertyUVE(*projectile, "impactSpeed")->section, "Result");

    // The component's own rule travels with the declaration, so no generic edit can author an
    // unknown policy or a coefficient outside the range the step would refuse.
    ASSERT_NE(projectile->isInstanceValid, nullptr);
    EXPECT_TRUE(projectile->isInstanceValid(&component));
    Projectile3DComponentUVE unknownPolicy;
    unknownPolicy.hitPolicy = static_cast<Projectile3DHitPolicyUVE>(9U);
    EXPECT_FALSE(projectile->isInstanceValid(&unknownPolicy));
    Projectile3DComponentUVE outOfRange;
    outOfRange.restitution = 1.5F;
    EXPECT_FALSE(projectile->isInstanceValid(&outOfRange));
}

TEST(SceneComponentMetadataUVETest, TheCombatSectionsCarryTheStrikeContractAndItsRuntimeResult) {
    // A strike is authored by two components that have to agree, so both declarations have to
    // reach the Inspector together: what the box is (half extents), who may hit whom (layer and
    // mask on each side), and what kind of hit it is (the damage channel). The result of the
    // engine's own scan is visible on the hitbox - the side that does the striking - and is
    // runtime-owned like every other resolved result in this engine.
    const TypeMetadataEntryUVE* hitbox =
        FindSceneComponentMetadataUVE(std::type_index(typeid(Hitbox3DComponentUVE)));
    ASSERT_NE(hitbox, nullptr);
    EXPECT_EQ(hitbox->typeId, "component.hitbox_3d");
    EXPECT_EQ(hitbox->displayName, "Hitbox3D");
    const TypeMetadataEntryUVE* hurtbox =
        FindSceneComponentMetadataUVE(std::type_index(typeid(Hurtbox3DComponentUVE)));
    ASSERT_NE(hurtbox, nullptr);
    EXPECT_EQ(hurtbox->typeId, "component.hurtbox_3d");
    EXPECT_EQ(hurtbox->displayName, "Hurtbox3D");

    for (const char* const name : {"enabled", "halfExtents", "damageChannel"}) {
        for (const TypeMetadataEntryUVE* const entry : {hitbox, hurtbox}) {
            const TypeMetadataPropertyUVE* property = FindPropertyUVE(*entry, name);
            ASSERT_NE(property, nullptr) << entry->typeId << "." << name;
            EXPECT_FALSE(HasPropertyFlagUVE(property->flags, TypeMetadataPropertyFlagsUVE::RuntimeState))
                << name;
            EXPECT_TRUE(property->IsAuthoringWritableUVE()) << name;
        }
    }
    for (const TypeMetadataEntryUVE* const entry : {hitbox, hurtbox}) {
        for (const char* const name : {"collisionLayer", "collisionMask"}) {
            const TypeMetadataPropertyUVE* property = FindPropertyUVE(*entry, name);
            ASSERT_NE(property, nullptr) << entry->typeId << "." << name;
            EXPECT_EQ(property->typeId, kPropertyTypeBitMask32UVE) << name;
            EXPECT_EQ(property->customDrawerId, kLayerMaskDrawerPhysicsUVE) << name;
        }
    }

    // The result of the strike scan lives on the hitbox only: a hurtbox has no strike list of its
    // own, so declaring one there would draw a field nothing ever writes.
    const TypeMetadataPropertyUVE* count = FindPropertyUVE(*hitbox, "strikeCount");
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(count->typeId, kPropertyTypeUInt8UVE);
    EXPECT_EQ(count->section, "Result");
    const TypeMetadataPropertyUVE* truncated = FindPropertyUVE(*hitbox, "strikesTruncated");
    ASSERT_NE(truncated, nullptr);
    EXPECT_EQ(truncated->typeId, kPropertyTypeBoolUVE);
    EXPECT_EQ(truncated->section, "Result");
    for (const TypeMetadataPropertyUVE& property : hitbox->properties) {
        if (property.name == "strikeCount" || property.name == "strikesTruncated") {
            EXPECT_TRUE(HasPropertyFlagUVE(property.flags, TypeMetadataPropertyFlagsUVE::RuntimeState))
                << property.name;
            EXPECT_FALSE(property.IsAuthoringWritableUVE()) << property.name;
            EXPECT_FALSE(property.IsSerializedUVE()) << property.name;
        }
    }
    EXPECT_EQ(FindPropertyUVE(*hurtbox, "strikeCount"), nullptr);
    EXPECT_EQ(FindPropertyUVE(*hurtbox, "strikesTruncated"), nullptr);

    // The one-byte count is declared as one byte wide, and the component's own rule travels with
    // both declarations so no generic edit can author a box that cannot strike or receive.
    static_assert(sizeof(Hitbox3DComponentUVE::strikeCount) == 1U,
                  "strikeCount is declared UInt8, so its storage must be one byte wide");
    Hitbox3DComponentUVE hitboxValue;
    EXPECT_EQ(hitboxValue.strikeCount, 0U);
    EXPECT_FALSE(hitboxValue.strikesTruncated);
    ASSERT_NE(hitbox->isInstanceValid, nullptr);
    ASSERT_NE(hurtbox->isInstanceValid, nullptr);
    EXPECT_TRUE(hitbox->isInstanceValid(&hitboxValue));
    Hurtbox3DComponentUVE hurtboxValue;
    EXPECT_TRUE(hurtbox->isInstanceValid(&hurtboxValue));
    Hitbox3DComponentUVE degenerate;
    degenerate.halfExtents = {0.0F, 1.0F, 1.0F};
    EXPECT_FALSE(hitbox->isInstanceValid(&degenerate));
    Hurtbox3DComponentUVE layerless;
    layerless.collisionLayer = 0U;
    EXPECT_FALSE(hurtbox->isInstanceValid(&layerless));
    Hurtbox3DComponentUVE overlongChannel;
    overlongChannel.damageChannel = std::string(300U, 'x');
    EXPECT_FALSE(hurtbox->isInstanceValid(&overlongChannel));
}

} // namespace
} // namespace UVE::Scene::Tests
