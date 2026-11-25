// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "plugin_manifest_validation.h"
#include "plugin_registry.h"

#include <gtest/gtest.h>

namespace UVE::Plugins::Tests {

TEST(NativePluginManifestValidationTest, ValidateNativePluginManifest_ReportsBoundedStructuredDiagnostics) {
    NativePluginManifest valid{"uve.valid", "Valid", {1U, 0U, 0U},
                                 kNativePluginProtocolVersion, {"object.types", "editor.window"}};
    EXPECT_TRUE(ValidateNativePluginManifest(valid).IsValid());

    NativePluginManifest duplicate = valid;
    duplicate.capabilityIds = {"object.types", "object.types"};
    const NativePluginManifestValidationResult duplicateResult = ValidateNativePluginManifest(duplicate);
    ASSERT_FALSE(duplicateResult.IsValid());
    ASSERT_EQ(duplicateResult.diagnostics.size(), 1U);
    EXPECT_EQ(duplicateResult.diagnostics[0].code,
              NativePluginManifestValidationCode::DuplicateCapabilityId);
    EXPECT_EQ(duplicateResult.diagnostics[0].capabilityIndex, 1U);

    NativePluginManifest invalidProtocol = valid;
    invalidProtocol.requiredEngineProtocol = 99U;
    const NativePluginManifestValidationResult protocolResult =
        ValidateNativePluginManifest(invalidProtocol);
    ASSERT_FALSE(protocolResult.IsValid());
    EXPECT_EQ(protocolResult.diagnostics[0].code,
              NativePluginManifestValidationCode::UnsupportedProtocol);

    NativePluginManifest tooManyCapabilities = valid;
    tooManyCapabilities.capabilityIds.assign(
        NativePluginRegistry::kMaximumCapabilitiesPerPlugin + 1U, "capability");
    const NativePluginManifestValidationResult limitResult =
        ValidateNativePluginManifest(tooManyCapabilities);
    ASSERT_FALSE(limitResult.IsValid());
    EXPECT_EQ(limitResult.diagnostics[0].code,
              NativePluginManifestValidationCode::TooManyCapabilities);
}

TEST(NativePluginManifestValidationTest, NegotiateNativePluginAbi_ReportsCompatibilityAndProtocolFailures) {
    const NativePluginManifest compatible{"uve.compatible", "Compatible", {1U, 0U, 0U}, 7U, {}};
    const NativePluginAbiNegotiationResult accepted = NegotiateNativePluginAbi(compatible, 7U);
    EXPECT_TRUE(accepted.IsCompatible());
    EXPECT_EQ(accepted.engineProtocol, 7U);
    EXPECT_EQ(accepted.pluginProtocol, 7U);

    const NativePluginAbiNegotiationResult invalidEngine = NegotiateNativePluginAbi(compatible, 0U);
    EXPECT_EQ(invalidEngine.code, NativePluginAbiNegotiationCode::InvalidEngineProtocol);
    EXPECT_FALSE(invalidEngine.IsCompatible());

    const NativePluginManifest incompatible{"uve.incompatible", "Incompatible", {1U, 0U, 0U}, 8U, {}};
    const NativePluginAbiNegotiationResult rejected = NegotiateNativePluginAbi(incompatible, 7U);
    EXPECT_EQ(rejected.code, NativePluginAbiNegotiationCode::UnsupportedPluginProtocol);
    EXPECT_FALSE(rejected.IsCompatible());
}

TEST(NativePluginManifestValidationTest, ValidateNativePluginManifest_AppliesExplicitCapabilityPolicy) {
    const NativePluginManifest manifest{"uve.policy", "Policy", {1U, 0U, 0U},
                                           kNativePluginProtocolVersion, {"object.types", "editor.window"}};
    const NativePluginCapabilityPolicy allowPolicy{false, {"object.types", "editor.window"}};
    EXPECT_TRUE(ValidateNativePluginManifest(manifest, allowPolicy).IsValid());

    const NativePluginCapabilityPolicy denyPolicy{false, {"object.types"}};
    const NativePluginManifestValidationResult denied =
        ValidateNativePluginManifest(manifest, denyPolicy);
    ASSERT_FALSE(denied.IsValid());
    ASSERT_EQ(denied.diagnostics.size(), 1U);
    EXPECT_EQ(denied.diagnostics[0].code, NativePluginManifestValidationCode::CapabilityNotAllowed);
    EXPECT_EQ(denied.diagnostics[0].capabilityIndex, 1U);

    const NativePluginCapabilityPolicy duplicatePolicy{false, {"object.types", "object.types"}};
    const NativePluginManifestValidationResult duplicate =
        ValidateNativePluginManifest(manifest, duplicatePolicy);
    ASSERT_FALSE(duplicate.IsValid());
    EXPECT_EQ(duplicate.diagnostics[0].code,
              NativePluginManifestValidationCode::DuplicatePolicyCapabilityId);
    EXPECT_EQ(duplicate.diagnostics[0].capabilityIndex, 1U);

    const NativePluginCapabilityPolicy invalidPolicy{false, {"bad id"}};
    const NativePluginManifestValidationResult invalid =
        ValidateNativePluginManifest(manifest, invalidPolicy);
    ASSERT_FALSE(invalid.IsValid());
    EXPECT_EQ(invalid.diagnostics[0].code, NativePluginManifestValidationCode::InvalidCapabilityId);
}

TEST(NativePluginRegistryTest, RegisterManifest_ExplicitPolicyRejectsBeforeMutation) {
    NativePluginRegistry registry;
    const NativePluginManifest manifest{"uve.restricted", "Restricted", {1U, 0U, 0U},
                                           kNativePluginProtocolVersion, {"object.types", "filesystem.read"}};
    const NativePluginCapabilityPolicy policy{false, {"object.types"}};
    const NativePluginRegistryResult rejected = registry.RegisterManifest(manifest, policy);
    EXPECT_FALSE(rejected.IsAccepted());
    EXPECT_EQ(registry.GetManifestCount(), 0U);
    EXPECT_EQ(registry.GetOpenScopeCount(), 0U);
    EXPECT_EQ(registry.FindManifest("uve.restricted"), nullptr);

    const NativePluginCapabilityPolicy allowPolicy{false, {"object.types", "filesystem.read"}};
    const NativePluginRegistryResult accepted = registry.RegisterManifest(manifest, allowPolicy);
    ASSERT_TRUE(accepted.IsAccepted()) << accepted.message;
    EXPECT_EQ(registry.GetManifestCount(), 1U);
    ASSERT_TRUE(registry.OpenScope("uve.restricted").has_value());
}

TEST(NativePluginRegistryTest, RegisterManifest_ValidatesIdentityProtocolAndCapabilities) {
    NativePluginRegistry registry;
    NativePluginManifest manifest{"uve.terrain", "Terrain", {1U, 2U, 0U},
                                     kNativePluginProtocolVersion, {"object.types", "editor.window"}};
    const NativePluginRegistryResult registered = registry.RegisterManifest(manifest);
    ASSERT_TRUE(registered.IsAccepted()) << registered.message;
    EXPECT_EQ(registry.GetManifestCount(), 1U);
    ASSERT_NE(registry.FindManifest("uve.terrain"), nullptr);
    EXPECT_EQ(registry.FindManifest("uve.terrain")->displayName, "Terrain");
    EXPECT_FALSE(registry.RegisterManifest(manifest).IsAccepted());
    EXPECT_FALSE(registry.RegisterManifest({"bad id", "Bad", {}, 1U, {}}).IsAccepted());
    EXPECT_FALSE(registry.RegisterManifest({"uve.future", "Future", {}, 99U, {}}).IsAccepted());
}

TEST(NativePluginRegistryTest, ScopesAreGenerationCheckedAndExplicitlyClosed) {
    NativePluginRegistry registry;
    const NativePluginRegistryResult registered = registry.RegisterManifest({"uve.test", "Test", {}, 1U, {}});
    ASSERT_TRUE(registered.IsAccepted()) << registered.message;
    const auto scope = registry.OpenScope("uve.test");
    ASSERT_TRUE(scope.has_value());
    EXPECT_TRUE(scope->IsValid());
    EXPECT_TRUE(registry.IsScopeOpen("uve.test"));
    EXPECT_FALSE(registry.OpenScope("uve.test").has_value());
    ASSERT_TRUE(registry.CloseScope(*scope).IsAccepted());
    EXPECT_FALSE(registry.IsScopeOpen("uve.test"));
    EXPECT_EQ(registry.CloseScope(*scope).code, NativePluginRegistryCode::InvalidScope);
    EXPECT_EQ(registry.GetOpenScopeCount(), 0U);
}

TEST(NativePluginRegistryTest, UnregisterManifest_RejectsOpenScopeAndRemovesAfterClose) {
    NativePluginRegistry registry;
    ASSERT_TRUE(registry.RegisterManifest({"uve.unload", "Unload", {}, 1U, {}}).IsAccepted());
    const auto scope = registry.OpenScope("uve.unload");
    ASSERT_TRUE(scope.has_value());

    const NativePluginRegistryResult busy = registry.UnregisterManifest("uve.unload");
    EXPECT_EQ(busy.code, NativePluginRegistryCode::Busy);
    EXPECT_NE(registry.FindManifest("uve.unload"), nullptr);
    EXPECT_EQ(registry.GetManifestCount(), 1U);

    ASSERT_TRUE(registry.CloseScope(*scope).IsAccepted());
    const NativePluginRegistryResult removed = registry.UnregisterManifest("uve.unload");
    EXPECT_TRUE(removed.IsAccepted());
    EXPECT_EQ(registry.FindManifest("uve.unload"), nullptr);
    EXPECT_EQ(registry.GetManifestCount(), 0U);
    EXPECT_EQ(registry.UnregisterManifest("uve.unload").code, NativePluginRegistryCode::NotFound);
}

TEST(NativePluginRegistryTest, InvalidAndUnknownScopesAreRejectedWithoutMutation) {
    NativePluginRegistry registry;
    EXPECT_FALSE(registry.OpenScope("missing").has_value());
    EXPECT_EQ(registry.CloseScope({}).code, NativePluginRegistryCode::InvalidScope);
    EXPECT_EQ(registry.UnregisterManifest("").code, NativePluginRegistryCode::NotFound);
    EXPECT_EQ(registry.GetManifestCount(), 0U);
}

} // namespace UVE::Plugins::Tests
