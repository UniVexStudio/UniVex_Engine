// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "type_metadata.h"

#include <gtest/gtest.h>

#include "vector3.h"
#include "transform_component.h"

namespace UVE::Core::Tests {
namespace {

// Genuine gap this closes: TypeMetadataProperty could previously only *describe* a property
// (name/displayName/typeId/editable) - nothing could actually read or write one generically.
// This round-trips a real registered component's property (Scene::TransformComponent,
// already ported in Engine/Runtime/Component) through the new type-erased accessor, exactly as
// the restructuring plan's Step 9 done-criterion requires.

TEST(PropertyAccessorTest, MakeProperty_RoundTripsRegisteredComponentProperty) {
    const TypeMetadataProperty localPositionProperty =
        MakeProperty<&Scene::TransformComponent::localPosition>("localPosition", "Local Position",
                                                                       "Vector3", true);

    TypeMetadataRegistry registry;
    const TypeMetadataRegistrationResult result = registry.RegisterType(TypeMetadataEntry{
        TypeMetadataKind::Component,
        "component.transform",
        "Transform",
        1U,
        {localPositionProperty},
        {},
    });
    ASSERT_TRUE(result.IsRegistered());

    const TypeMetadataEntry* registeredType = registry.FindType("component.transform");
    ASSERT_NE(registeredType, nullptr);
    ASSERT_EQ(registeredType->properties.size(), 1U);
    const TypeMetadataProperty& property = registeredType->properties.front();

    Scene::TransformComponent transform;
    transform.localPosition = Math::Vector3{1.0F, 2.0F, 3.0F};

    const Math::Vector3 readValue = GetPropertyValue<Math::Vector3>(property, transform);
    EXPECT_FLOAT_EQ(readValue.x, 1.0F);
    EXPECT_FLOAT_EQ(readValue.y, 2.0F);
    EXPECT_FLOAT_EQ(readValue.z, 3.0F);

    SetPropertyValue(property, transform, Math::Vector3{4.0F, 5.0F, 6.0F});
    EXPECT_FLOAT_EQ(transform.localPosition.x, 4.0F);
    EXPECT_FLOAT_EQ(transform.localPosition.y, 5.0F);
    EXPECT_FLOAT_EQ(transform.localPosition.z, 6.0F);
}

TEST(PropertyAccessorTest, DescribeOnlyProperty_HasNullAccessorsAndIsANoOp) {
    const TypeMetadataProperty describeOnly{"legacy", "Legacy", "Number", false};
    EXPECT_EQ(describeOnly.getValue, nullptr);
    EXPECT_EQ(describeOnly.setValue, nullptr);

    Scene::TransformComponent transform;
    transform.localPosition = Math::Vector3{1.0F, 1.0F, 1.0F};
    SetPropertyValue(describeOnly, transform, Math::Vector3{9.0F, 9.0F, 9.0F});
    EXPECT_FLOAT_EQ(transform.localPosition.x, 1.0F);
}

} // namespace
} // namespace UVE::Core::Tests
