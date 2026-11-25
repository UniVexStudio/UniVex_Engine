// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "type_metadata.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <typeindex>

#include "camera_component.h"
#include "transform_component.h"
#include "vector3.h"

namespace UVE::Core::Tests {
namespace {

// The registry previously keyed types by string only, while the ECS keys an entity's live
// components by std::type_index. Nothing could join the two, so no generic consumer could go
// from "this entity has these components" to "here is what those components expose". These tests
// cover that bridge, the default-instance factory it carries, and the declared property traits a
// generic consumer needs in order to refuse a write it must not make.

[[nodiscard]] TypeMetadataEntry MakeTransformEntry() {
    TypeMetadataEntry entry{
        TypeMetadataKind::Component, "component.transform", "Transform", 1U, {}, {}};
    BindType<Scene::TransformComponent>(entry);
    entry.properties.push_back(MakeProperty<&Scene::TransformComponent::localPosition>(
        "localPosition", "Position", "Vector3", true));
    return entry;
}

TEST(TypeMetadataBridgeTest, FindTypeByIndex_ResolvesALiveComponentTypeToItsMetadata) {
    TypeMetadataRegistry registry;
    ASSERT_TRUE(registry.RegisterType(MakeTransformEntry()).IsRegistered());

    const TypeMetadataEntry* resolved =
        registry.FindTypeByIndex(std::type_index(typeid(Scene::TransformComponent)));
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved->typeId, "component.transform");
    EXPECT_EQ(resolved, registry.FindType("component.transform"));

    // A type that was never registered resolves to nothing rather than to an arbitrary entry.
    EXPECT_EQ(registry.FindTypeByIndex(std::type_index(typeid(Scene::CameraComponent))), nullptr);
}

TEST(TypeMetadataBridgeTest, FindTypeByIndex_NeverMatchesDescribeOnlyEntries) {
    TypeMetadataRegistry registry;
    // Two hand-built entries that own no C++ type both sit at typeid(void). Neither may be
    // returned by an index lookup, and registering the second must not be read as a duplicate
    // native type.
    ASSERT_TRUE(registry.RegisterType(TypeMetadataEntry{
        TypeMetadataKind::Other, "describe.one", "One", 1U, {}, {}}).IsRegistered());
    ASSERT_TRUE(registry.RegisterType(TypeMetadataEntry{
        TypeMetadataKind::Other, "describe.two", "Two", 1U, {}, {}}).IsRegistered());

    EXPECT_EQ(registry.FindTypeByIndex(std::type_index(typeid(void))), nullptr);
    EXPECT_EQ(registry.GetTypeCount(), 2U);
}

TEST(TypeMetadataBridgeTest, RegisterType_RejectsASecondEntryForTheSameNativeType) {
    TypeMetadataRegistry registry;
    ASSERT_TRUE(registry.RegisterType(MakeTransformEntry()).IsRegistered());

    TypeMetadataEntry rival = MakeTransformEntry();
    rival.typeId = "component.transform.rival";
    const TypeMetadataRegistrationResult result = registry.RegisterType(std::move(rival));

    EXPECT_EQ(result.code, TypeMetadataRegistrationCode::DuplicateType);
    EXPECT_EQ(registry.GetTypeCount(), 1U);
    EXPECT_EQ(registry.GetGeneration(), 1U);
}

TEST(TypeMetadataBridgeTest, RegisterType_RejectsAHalfDeclaredFactoryBeforeMutation) {
    TypeMetadataRegistry registry;
    TypeMetadataEntry entry = MakeTransformEntry();
    entry.destroyInstance = nullptr; // A create without a matching destroy would leak on every use.

    EXPECT_EQ(registry.RegisterType(std::move(entry)).code, TypeMetadataRegistrationCode::InvalidEntry);
    EXPECT_EQ(registry.GetTypeCount(), 0U);
}

TEST(TypeMetadataBridgeTest, TypeInstance_ExposesADefaultConstructedInstanceToReadFrom) {
    TypeMetadataRegistry registry;
    ASSERT_TRUE(registry.RegisterType(MakeTransformEntry()).IsRegistered());
    const TypeMetadataEntry* entry = registry.FindType("component.transform");
    ASSERT_NE(entry, nullptr);
    ASSERT_TRUE(entry->HasFactory());

    // This is what reset-to-default needs: the default value of a property, read generically,
    // without the caller ever naming Scene::TransformComponent.
    const TypeInstance defaults = TypeInstance::MakeDefault(*entry);
    ASSERT_TRUE(defaults.IsValid());
    const Math::Vector3 defaultPosition = GetPropertyValue<Math::Vector3>(
        entry->properties.front(), *static_cast<const Scene::TransformComponent*>(defaults.Get()));
    EXPECT_FLOAT_EQ(defaultPosition.x, 0.0F);
    EXPECT_FLOAT_EQ(defaultPosition.y, 0.0F);
    EXPECT_FLOAT_EQ(defaultPosition.z, 0.0F);
}

TEST(TypeMetadataBridgeTest, TypeInstance_IsInvalidForADescribeOnlyEntry) {
    const TypeMetadataEntry describeOnly{TypeMetadataKind::Other, "describe.only", "Only", 1U, {}, {}};
    const TypeInstance defaults = TypeInstance::MakeDefault(describeOnly);
    EXPECT_FALSE(defaults.IsValid());
    EXPECT_EQ(defaults.Get(), nullptr);
}

struct StorageProbe {
    int value = 7;
    std::string label = "probe";
};

TEST(TypeMetadataBridgeTest, BindType_BindsArchetypeSlotConstructionTrio) {
    TypeMetadataEntry entry{TypeMetadataKind::Component, "component.probe", "Probe", 1U, {}, {}};
    BindType<StorageProbe>(entry);
    EXPECT_EQ(entry.instanceSize, sizeof(StorageProbe));
    EXPECT_EQ(entry.instanceAlignment, alignof(StorageProbe));
    ASSERT_NE(entry.constructDefaultInPlace, nullptr);
    ASSERT_NE(entry.moveConstructInPlace, nullptr);
    ASSERT_NE(entry.destroyInPlace, nullptr);

    alignas(StorageProbe) unsigned char buffer[sizeof(StorageProbe)];
    entry.constructDefaultInPlace(buffer);
    EXPECT_EQ(reinterpret_cast<const StorageProbe*>(buffer)->value, 7);
    EXPECT_EQ(reinterpret_cast<const StorageProbe*>(buffer)->label, "probe");

    alignas(StorageProbe) unsigned char moved[sizeof(StorageProbe)];
    entry.moveConstructInPlace(moved, buffer);
    EXPECT_EQ(reinterpret_cast<const StorageProbe*>(moved)->value, 7);
    EXPECT_EQ(reinterpret_cast<const StorageProbe*>(moved)->label, "probe");
    // The move destroys its source (mirroring ComponentTypeInfo's contract), so only the
    // destination is destroyed here; destroying the source again would double-free the string.
    entry.destroyInPlace(moved);
}

TEST(TypeMetadataPropertyTraitsTest, RuntimeStateAndReadOnlyRefuseAuthoringAndPersistence) {
    TypeMetadataProperty derived = MakeProperty<&Scene::TransformComponent::localPosition>(
        "localPosition", "Position", "Vector3", true);
    derived.flags = TypeMetadataPropertyFlags::RuntimeState;

    // Runtime-derived state is neither authorable nor persisted, however it was declared editable:
    // writing it would fight the system that recomputes it, saving it would restore a stale cache.
    EXPECT_FALSE(derived.IsAuthoringWritable());
    EXPECT_FALSE(derived.IsSerialized());

    TypeMetadataProperty readOnly = derived;
    readOnly.flags = TypeMetadataPropertyFlags::ReadOnly;
    EXPECT_FALSE(readOnly.IsAuthoringWritable());
    EXPECT_TRUE(readOnly.IsSerialized()); // Authored elsewhere, but still part of the scene.

    TypeMetadataProperty plain = derived;
    plain.flags = TypeMetadataPropertyFlags::None;
    EXPECT_TRUE(plain.IsAuthoringWritable());
    EXPECT_TRUE(plain.IsSerialized());

    // Editor-only authoring is shown (and writable) in the inspector but never shipped in
    // runtime data, so the metadata-driven serializer skips it like runtime state.
    TypeMetadataProperty editorOnly = derived;
    editorOnly.flags = TypeMetadataPropertyFlags::EditorOnly;
    EXPECT_TRUE(editorOnly.IsAuthoringWritable());
    EXPECT_FALSE(editorOnly.IsSerialized());

    // A describe-only property has no accessor to write through, so it is never authorable.
    const TypeMetadataProperty describeOnly{"legacy", "Legacy", "Number", true};
    EXPECT_FALSE(describeOnly.IsAuthoringWritable());
    EXPECT_FALSE(describeOnly.IsSerialized());
}

TEST(TypeMetadataPropertyTraitsTest, RegisterType_RejectsAnEnumThatCannotRoundTripASelection) {
    TypeMetadataRegistry registry;
    TypeMetadataEntry entry{TypeMetadataKind::Other, "enum.owner", "Enum Owner", 1U, {}, {}};
    TypeMetadataProperty property{"mode", "Mode", "Enum", true};
    property.enumEntries = {{0, "Inherit"}, {0, "On"}}; // Duplicate value: a selection is ambiguous.
    entry.properties.push_back(property);

    EXPECT_EQ(registry.RegisterType(entry).code, TypeMetadataRegistrationCode::InvalidEntry);

    entry.properties.front().enumEntries = {{0, "Inherit"}, {1, ""}}; // Unlabelled option.
    EXPECT_EQ(registry.RegisterType(entry).code, TypeMetadataRegistrationCode::InvalidEntry);

    entry.properties.front().enumEntries = {{0, "Inherit"}, {1, "On"}};
    EXPECT_TRUE(registry.RegisterType(entry).IsRegistered());
}

} // namespace
} // namespace UVE::Core::Tests
