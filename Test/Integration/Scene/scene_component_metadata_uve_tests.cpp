// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/scene_component_metadata_uve.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <typeindex>
#include <vector>

#include "uve/component/collider_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/physics_interpolation_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/visibility_component_uve.h"
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

// Sections follow the class chain from most to least derived: the node's own, then Node3D's
// Transform, then the common Node section.
TEST(SceneComponentMetadataUVETest, TheCommonNodeSectionSortsBelowEverythingTypeSpecific) {
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
    EXPECT_GE(interpolation->order, kSectionOrderNodeCommonUVE);
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

} // namespace
} // namespace UVE::Scene::Tests
