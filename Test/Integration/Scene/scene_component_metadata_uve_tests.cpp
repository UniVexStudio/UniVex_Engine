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
    // The collider's layer and mask, SpringArm3D's collisionMask, RayCast3D's collisionMask and
    // Projectile3D's collisionMask - every cast and every swept body in the engine filters on the
    // same layer contract, so they all belong to the same drawer set rather than a second,
    // hand-drawn one.
    EXPECT_EQ(physics, 5U);
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

} // namespace
} // namespace UVE::Scene::Tests
