// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/core/type_metadata_uve.h"

#include <gtest/gtest.h>

#include "uve/math/vector3_uve.h"
#include "uve/scene/components/transform_component_uve.h"

namespace UVE::Core::Tests {
namespace {

// Genuine gap this closes: TypeMetadataPropertyUVE could previously only *describe* a property
// (name/displayName/typeId/editable) - nothing could actually read or write one generically.
// This round-trips a real registered component's property (Scene::TransformComponentUVE,
// already ported in Engine/Runtime/Component) through the new type-erased accessor, exactly as
// the restructuring plan's Step 9 done-criterion requires.

TEST(PropertyAccessorUVETest, MakePropertyUVE_RoundTripsRegisteredComponentProperty) {
    const TypeMetadataPropertyUVE localPositionProperty =
        MakePropertyUVE<&Scene::TransformComponentUVE::localPosition>("localPosition", "Local Position",
                                                                       "Vector3", true);

    TypeMetadataRegistryUVE registry;
    const TypeMetadataRegistrationResultUVE result = registry.RegisterTypeUVE(TypeMetadataEntryUVE{
        TypeMetadataKindUVE::Component,
        "component.transform",
        "Transform",
        1U,
        {localPositionProperty},
        {},
    });
    ASSERT_TRUE(result.IsRegisteredUVE());

    const TypeMetadataEntryUVE* registeredType = registry.FindTypeUVE("component.transform");
    ASSERT_NE(registeredType, nullptr);
    ASSERT_EQ(registeredType->properties.size(), 1U);
    const TypeMetadataPropertyUVE& property = registeredType->properties.front();

    Scene::TransformComponentUVE transform;
    transform.localPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};

    const Math::Vector3UVE readValue = GetPropertyValueUVE<Math::Vector3UVE>(property, transform);
    EXPECT_FLOAT_EQ(readValue.x, 1.0F);
    EXPECT_FLOAT_EQ(readValue.y, 2.0F);
    EXPECT_FLOAT_EQ(readValue.z, 3.0F);

    SetPropertyValueUVE(property, transform, Math::Vector3UVE{4.0F, 5.0F, 6.0F});
    EXPECT_FLOAT_EQ(transform.localPosition.x, 4.0F);
    EXPECT_FLOAT_EQ(transform.localPosition.y, 5.0F);
    EXPECT_FLOAT_EQ(transform.localPosition.z, 6.0F);
}

TEST(PropertyAccessorUVETest, DescribeOnlyProperty_HasNullAccessorsAndIsANoOp) {
    const TypeMetadataPropertyUVE describeOnly{"legacy", "Legacy", "Number", false};
    EXPECT_EQ(describeOnly.getValue, nullptr);
    EXPECT_EQ(describeOnly.setValue, nullptr);

    Scene::TransformComponentUVE transform;
    transform.localPosition = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
    SetPropertyValueUVE(describeOnly, transform, Math::Vector3UVE{9.0F, 9.0F, 9.0F});
    EXPECT_FLOAT_EQ(transform.localPosition.x, 1.0F);
}

} // namespace
} // namespace UVE::Core::Tests
