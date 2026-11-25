// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "type_metadata.h"

#include <gtest/gtest.h>

#include <utility>

namespace UVE::Core {

TEST(TypeMetadataRegistryTest, RegisterType_OrdersCopiedSnapshotAndAdvancesGeneration) {
    TypeMetadataRegistry registry;
    ASSERT_TRUE(registry.RegisterType(TypeMetadataEntry{
        TypeMetadataKind::InspectorTarget,
        "object.zeta",
        "Zeta Object",
        1U,
        {{"Value", "Value", "Number", true}},
        {{"Execute", "Execute", 1U}},
    }).IsRegistered());
    ASSERT_TRUE(registry.RegisterType(TypeMetadataEntry{
        TypeMetadataKind::Component,
        "component.transform",
        "Transform",
        2U,
        {{"Position", "Position", "Vector3", true}},
        {},
    }).IsRegistered());

    const TypeMetadataSnapshot snapshot = registry.GetSnapshot();
    ASSERT_EQ(snapshot.entries.size(), 2U);
    EXPECT_EQ(snapshot.generation, 2U);
    EXPECT_EQ(snapshot.entries[0].typeId, "component.transform");
    EXPECT_EQ(snapshot.entries[1].typeId, "object.zeta");
    EXPECT_TRUE(snapshot.entries[1].properties[0].editable);
    EXPECT_EQ(snapshot.entries[1].methods[0].name, "Execute");
}

TEST(TypeMetadataRegistryTest, RegisterType_RejectsDuplicateAndMalformedMembersWithoutMutation) {
    TypeMetadataRegistry registry;
    const TypeMetadataEntry valid{
        TypeMetadataKind::Resource, "resource.mesh", "Mesh", 1U, {}, {}};
    ASSERT_TRUE(registry.RegisterType(valid).IsRegistered());

    const TypeMetadataRegistrationResult duplicate = registry.RegisterType(valid);
    EXPECT_EQ(duplicate.code, TypeMetadataRegistrationCode::DuplicateType);
    EXPECT_EQ(registry.GetTypeCount(), 1U);
    EXPECT_EQ(registry.GetGeneration(), 1U);

    const TypeMetadataRegistrationResult malformed = registry.RegisterType(TypeMetadataEntry{
        TypeMetadataKind::InspectorTarget,
        "target.camera",
        "Camera",
        1U,
        {{"Value", "Value", "Number", true}},
        {{"Value", "Invoke", 0U}},
    });
    EXPECT_EQ(malformed.code, TypeMetadataRegistrationCode::InvalidEntry);
    EXPECT_EQ(registry.GetTypeCount(), 1U);
    EXPECT_EQ(registry.GetGeneration(), 1U);
}

TEST(TypeMetadataRegistryTest, RegisterType_RejectsOversizedMemberCollectionsBeforeMutation) {
    TypeMetadataRegistry registry;
    TypeMetadataEntry oversized{TypeMetadataKind::InspectorTarget,
                                   "object.oversized",
                                   "Oversized Object",
                                   1U,
                                   std::vector<TypeMetadataProperty>(
                                       TypeMetadataRegistry::kMaximumMembersPerType + 1U),
                                   std::vector<TypeMetadataMethod>(
                                       TypeMetadataRegistry::kMaximumMembersPerType + 1U)};

    const TypeMetadataRegistrationResult result = registry.RegisterType(std::move(oversized));

    EXPECT_EQ(result.code, TypeMetadataRegistrationCode::InvalidEntry);
    EXPECT_EQ(registry.GetTypeCount(), 0U);
    EXPECT_EQ(registry.GetGeneration(), 0U);
}

TEST(TypeMetadataRegistryTest, RegisterType_RejectsUnboundedIdentityBeforeMutation) {
    TypeMetadataRegistry registry;
    const std::string oversized(TypeMetadataRegistry::kMaximumIdentifierBytes + 1U, 'x');
    const TypeMetadataRegistrationResult result = registry.RegisterType(TypeMetadataEntry{
        TypeMetadataKind::Other, oversized, "Oversized", 1U, {}, {}});

    EXPECT_EQ(result.code, TypeMetadataRegistrationCode::InvalidEntry);
    EXPECT_EQ(registry.GetTypeCount(), 0U);
    EXPECT_EQ(registry.GetGeneration(), 0U);
}

} // namespace UVE::Core

namespace UVE::Core::Tests {
namespace {

TEST(TypeMetadataRegistryTest, RegisterType_RejectsPresentationLinksThatPointNowhere) {
    TypeMetadataRegistry registry;
    TypeMetadataProperty mode{"mode", "Mode", "Enum", true};
    mode.resolvedByProperty = "resolved";
    // The named companion does not exist: an inspector would have no answer to show.
    EXPECT_FALSE(registry.RegisterType({TypeMetadataKind::Component, "t.dangling", "T", 1U, {mode}, {}})
                     .IsRegistered());
    mode.resolvedByProperty = "mode"; // Its own answer.
    EXPECT_FALSE(registry.RegisterType({TypeMetadataKind::Component, "t.self", "T", 1U, {mode}, {}})
                     .IsRegistered());
    TypeMetadataEntry nestedInItself{TypeMetadataKind::Component, "t.nest", "T", 1U, {}, {}};
    nestedInItself.nestedUnderTypeIds = {"t.other", "t.nest"};
    EXPECT_FALSE(registry.RegisterType(nestedInItself).IsRegistered());
    TypeMetadataEntry hostTwice{TypeMetadataKind::Component, "t.twice", "T", 1U, {}, {}};
    hostTwice.nestedUnderTypeIds = {"t.host", "t.host"};
    EXPECT_FALSE(registry.RegisterType(hostTwice).IsRegistered());
    TypeMetadataEntry twoHosts{TypeMetadataKind::Component, "t.two_hosts", "T", 1U, {}, {}};
    twoHosts.nestedUnderTypeIds = {"t.host_a", "t.host_b"};
    EXPECT_TRUE(registry.RegisterType(twoHosts).IsRegistered());

    mode.resolvedByProperty = "resolved";
    const TypeMetadataProperty resolved{"resolved", "Resolved", "Enum", false};
    EXPECT_TRUE(registry.RegisterType({TypeMetadataKind::Component, "t.ok", "T", 1U, {mode, resolved}, {}})
                    .IsRegistered());
}

} // namespace
} // namespace UVE::Core::Tests
