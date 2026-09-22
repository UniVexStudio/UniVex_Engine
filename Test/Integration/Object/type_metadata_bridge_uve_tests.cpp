// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/object/type_metadata_uve.h"

#include <gtest/gtest.h>

#include <typeindex>

#include "uve/component/camera_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Core::Tests {
namespace {

// The registry previously keyed types by string only, while the ECS keys an entity's live
// components by std::type_index. Nothing could join the two, so no generic consumer could go
// from "this entity has these components" to "here is what those components expose". These tests
// cover that bridge, the default-instance factory it carries, and the declared property traits a
// generic consumer needs in order to refuse a write it must not make.

[[nodiscard]] TypeMetadataEntryUVE MakeTransformEntryUVE() {
    TypeMetadataEntryUVE entry{
        TypeMetadataKindUVE::Component, "component.transform", "Transform", 1U, {}, {}};
    BindTypeUVE<Scene::TransformComponentUVE>(entry);
    entry.properties.push_back(MakePropertyUVE<&Scene::TransformComponentUVE::localPosition>(
        "localPosition", "Position", "Vector3", true));
    return entry;
}

TEST(TypeMetadataBridgeUVETest, FindTypeByIndexUVE_ResolvesALiveComponentTypeToItsMetadata) {
    TypeMetadataRegistryUVE registry;
    ASSERT_TRUE(registry.RegisterTypeUVE(MakeTransformEntryUVE()).IsRegisteredUVE());

    const TypeMetadataEntryUVE* resolved =
        registry.FindTypeByIndexUVE(std::type_index(typeid(Scene::TransformComponentUVE)));
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved->typeId, "component.transform");
    EXPECT_EQ(resolved, registry.FindTypeUVE("component.transform"));

    // A type that was never registered resolves to nothing rather than to an arbitrary entry.
    EXPECT_EQ(registry.FindTypeByIndexUVE(std::type_index(typeid(Scene::CameraComponentUVE))), nullptr);
}

TEST(TypeMetadataBridgeUVETest, FindTypeByIndexUVE_NeverMatchesDescribeOnlyEntries) {
    TypeMetadataRegistryUVE registry;
    // Two hand-built entries that own no C++ type both sit at typeid(void). Neither may be
    // returned by an index lookup, and registering the second must not be read as a duplicate
    // native type.
    ASSERT_TRUE(registry.RegisterTypeUVE(TypeMetadataEntryUVE{
        TypeMetadataKindUVE::Other, "describe.one", "One", 1U, {}, {}}).IsRegisteredUVE());
    ASSERT_TRUE(registry.RegisterTypeUVE(TypeMetadataEntryUVE{
        TypeMetadataKindUVE::Other, "describe.two", "Two", 1U, {}, {}}).IsRegisteredUVE());

    EXPECT_EQ(registry.FindTypeByIndexUVE(std::type_index(typeid(void))), nullptr);
    EXPECT_EQ(registry.GetTypeCountUVE(), 2U);
}

TEST(TypeMetadataBridgeUVETest, RegisterTypeUVE_RejectsASecondEntryForTheSameNativeType) {
    TypeMetadataRegistryUVE registry;
    ASSERT_TRUE(registry.RegisterTypeUVE(MakeTransformEntryUVE()).IsRegisteredUVE());

    TypeMetadataEntryUVE rival = MakeTransformEntryUVE();
    rival.typeId = "component.transform.rival";
    const TypeMetadataRegistrationResultUVE result = registry.RegisterTypeUVE(std::move(rival));

    EXPECT_EQ(result.code, TypeMetadataRegistrationCodeUVE::DuplicateType);
    EXPECT_EQ(registry.GetTypeCountUVE(), 1U);
    EXPECT_EQ(registry.GetGenerationUVE(), 1U);
}

TEST(TypeMetadataBridgeUVETest, RegisterTypeUVE_RejectsAHalfDeclaredFactoryBeforeMutation) {
    TypeMetadataRegistryUVE registry;
    TypeMetadataEntryUVE entry = MakeTransformEntryUVE();
    entry.destroyInstance = nullptr; // A create without a matching destroy would leak on every use.

    EXPECT_EQ(registry.RegisterTypeUVE(std::move(entry)).code, TypeMetadataRegistrationCodeUVE::InvalidEntry);
    EXPECT_EQ(registry.GetTypeCountUVE(), 0U);
}

TEST(TypeMetadataBridgeUVETest, TypeInstanceUVE_ExposesADefaultConstructedInstanceToReadFrom) {
    TypeMetadataRegistryUVE registry;
    ASSERT_TRUE(registry.RegisterTypeUVE(MakeTransformEntryUVE()).IsRegisteredUVE());
    const TypeMetadataEntryUVE* entry = registry.FindTypeUVE("component.transform");
    ASSERT_NE(entry, nullptr);
    ASSERT_TRUE(entry->HasFactoryUVE());

    // This is what reset-to-default needs: the default value of a property, read generically,
    // without the caller ever naming Scene::TransformComponentUVE.
    const TypeInstanceUVE defaults = TypeInstanceUVE::MakeDefaultUVE(*entry);
    ASSERT_TRUE(defaults.IsValidUVE());
    const Math::Vector3UVE defaultPosition = GetPropertyValueUVE<Math::Vector3UVE>(
        entry->properties.front(), *static_cast<const Scene::TransformComponentUVE*>(defaults.GetUVE()));
    EXPECT_FLOAT_EQ(defaultPosition.x, 0.0F);
    EXPECT_FLOAT_EQ(defaultPosition.y, 0.0F);
    EXPECT_FLOAT_EQ(defaultPosition.z, 0.0F);
}

TEST(TypeMetadataBridgeUVETest, TypeInstanceUVE_IsInvalidForADescribeOnlyEntry) {
    const TypeMetadataEntryUVE describeOnly{TypeMetadataKindUVE::Other, "describe.only", "Only", 1U, {}, {}};
    const TypeInstanceUVE defaults = TypeInstanceUVE::MakeDefaultUVE(describeOnly);
    EXPECT_FALSE(defaults.IsValidUVE());
    EXPECT_EQ(defaults.GetUVE(), nullptr);
}

TEST(TypeMetadataPropertyTraitsUVETest, RuntimeStateAndReadOnlyRefuseAuthoringAndPersistence) {
    TypeMetadataPropertyUVE derived = MakePropertyUVE<&Scene::TransformComponentUVE::localPosition>(
        "localPosition", "Position", "Vector3", true);
    derived.flags = TypeMetadataPropertyFlagsUVE::RuntimeState;

    // Runtime-derived state is neither authorable nor persisted, however it was declared editable:
    // writing it would fight the system that recomputes it, saving it would restore a stale cache.
    EXPECT_FALSE(derived.IsAuthoringWritableUVE());
    EXPECT_FALSE(derived.IsSerializedUVE());

    TypeMetadataPropertyUVE readOnly = derived;
    readOnly.flags = TypeMetadataPropertyFlagsUVE::ReadOnly;
    EXPECT_FALSE(readOnly.IsAuthoringWritableUVE());
    EXPECT_TRUE(readOnly.IsSerializedUVE()); // Authored elsewhere, but still part of the scene.

    TypeMetadataPropertyUVE plain = derived;
    plain.flags = TypeMetadataPropertyFlagsUVE::None;
    EXPECT_TRUE(plain.IsAuthoringWritableUVE());
    EXPECT_TRUE(plain.IsSerializedUVE());

    // A describe-only property has no accessor to write through, so it is never authorable.
    const TypeMetadataPropertyUVE describeOnly{"legacy", "Legacy", "Number", true};
    EXPECT_FALSE(describeOnly.IsAuthoringWritableUVE());
    EXPECT_FALSE(describeOnly.IsSerializedUVE());
}

TEST(TypeMetadataPropertyTraitsUVETest, RegisterTypeUVE_RejectsAnEnumThatCannotRoundTripASelection) {
    TypeMetadataRegistryUVE registry;
    TypeMetadataEntryUVE entry{TypeMetadataKindUVE::Other, "enum.owner", "Enum Owner", 1U, {}, {}};
    TypeMetadataPropertyUVE property{"mode", "Mode", "Enum", true};
    property.enumEntries = {{0, "Inherit"}, {0, "On"}}; // Duplicate value: a selection is ambiguous.
    entry.properties.push_back(property);

    EXPECT_EQ(registry.RegisterTypeUVE(entry).code, TypeMetadataRegistrationCodeUVE::InvalidEntry);

    entry.properties.front().enumEntries = {{0, "Inherit"}, {1, ""}}; // Unlabelled option.
    EXPECT_EQ(registry.RegisterTypeUVE(entry).code, TypeMetadataRegistrationCodeUVE::InvalidEntry);

    entry.properties.front().enumEntries = {{0, "Inherit"}, {1, "On"}};
    EXPECT_TRUE(registry.RegisterTypeUVE(entry).IsRegisteredUVE());
}

} // namespace
} // namespace UVE::Core::Tests
