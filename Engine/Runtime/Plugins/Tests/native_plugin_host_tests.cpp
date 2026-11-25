// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "native_plugin_host.h"

#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Plugins::Tests {
namespace {

const std::filesystem::path kValidPluginPath{UVE_TEST_PLUGIN_VALID_PATH};
const std::filesystem::path kBadProtocolPluginPath{UVE_TEST_PLUGIN_BAD_PROTOCOL_PATH};
const std::filesystem::path kNoEntryPluginPath{UVE_TEST_PLUGIN_NO_ENTRY_PATH};

NativePluginDescriptor MakeDescriptor() {
    static const char* const capabilities[] = {"test.objects", "test.window"};
    NativePluginDescriptor descriptor;
    descriptor.abiVersion = kNativePluginProtocolVersion;
    descriptor.pluginId = "uve.unit-plugin";
    descriptor.displayName = "Unit Plugin";
    descriptor.versionMajor = 2U;
    descriptor.versionMinor = 3U;
    descriptor.versionPatch = 4U;
    descriptor.requiredEngineProtocol = kNativePluginProtocolVersion;
    descriptor.capabilityIds = capabilities;
    descriptor.capabilityCount = 2U;
    return descriptor;
}

} // namespace

TEST(NativePluginDescriptorValidationTest, ValidDescriptor_CopiesManifest) {
    const NativePluginDescriptor descriptor = MakeDescriptor();
    NativePluginManifest manifest;
    const NativePluginDescriptorResult result = ValidateNativePluginDescriptor(&descriptor, manifest);
    EXPECT_TRUE(result.IsValid());
    EXPECT_EQ(manifest.pluginId, "uve.unit-plugin");
    EXPECT_EQ(manifest.displayName, "Unit Plugin");
    EXPECT_EQ(manifest.version, (NativePluginVersion{2U, 3U, 4U}));
    EXPECT_EQ(manifest.requiredEngineProtocol, kNativePluginProtocolVersion);
    EXPECT_EQ(manifest.capabilityIds, (std::vector<std::string>{"test.objects", "test.window"}));
}

TEST(NativePluginDescriptorValidationTest, NullDescriptor_ResetsManifest) {
    NativePluginManifest manifest{"junk", "Junk", {9U, 9U, 9U}, 9U, {"junk"}};
    const NativePluginDescriptorResult result = ValidateNativePluginDescriptor(nullptr, manifest);
    EXPECT_EQ(result.code, NativePluginDescriptorCode::NullDescriptor);
    EXPECT_EQ(manifest, NativePluginManifest{});
}

TEST(NativePluginDescriptorValidationTest, BadAbiVersion_ReportsUnsupportedAbi) {
    NativePluginDescriptor descriptor = MakeDescriptor();
    descriptor.abiVersion = 99U;
    NativePluginManifest manifest;
    const NativePluginDescriptorResult result = ValidateNativePluginDescriptor(&descriptor, manifest);
    EXPECT_EQ(result.code, NativePluginDescriptorCode::UnsupportedAbi);
    EXPECT_EQ(manifest, NativePluginManifest{});
}

TEST(NativePluginDescriptorValidationTest, NullIdentifier_ReportsInvalidIdentifier) {
    NativePluginDescriptor descriptor = MakeDescriptor();
    descriptor.pluginId = nullptr;
    NativePluginManifest manifest;
    const NativePluginDescriptorResult result = ValidateNativePluginDescriptor(&descriptor, manifest);
    EXPECT_EQ(result.code, NativePluginDescriptorCode::InvalidIdentifier);
}

TEST(NativePluginDescriptorValidationTest, TooManyCapabilities_ReportsLimit) {
    NativePluginDescriptor descriptor = MakeDescriptor();
    descriptor.capabilityCount =
        static_cast<std::uint32_t>(NativePluginRegistry::kMaximumCapabilitiesPerPlugin + 1U);
    NativePluginManifest manifest;
    const NativePluginDescriptorResult result = ValidateNativePluginDescriptor(&descriptor, manifest);
    EXPECT_EQ(result.code, NativePluginDescriptorCode::TooManyCapabilities);
}

TEST(NativePluginDescriptorValidationTest, NullArrayWithCount_ReportsInvalidCapabilityId) {
    NativePluginDescriptor descriptor = MakeDescriptor();
    descriptor.capabilityIds = nullptr;
    NativePluginManifest manifest;
    const NativePluginDescriptorResult result = ValidateNativePluginDescriptor(&descriptor, manifest);
    EXPECT_EQ(result.code, NativePluginDescriptorCode::InvalidCapabilityId);
}

TEST(NativePluginHostTest, LoadValidPlugin_RegistersManifestAndUnloads) {
    NativePluginRegistry registry;
    NativePluginHost host(registry);
    const NativePluginLoadResult loaded = host.Load(kValidPluginPath);
    EXPECT_TRUE(loaded.IsLoaded()) << loaded.message;
    EXPECT_EQ(loaded.pluginId, "uve.test-plugin");
    EXPECT_TRUE(host.IsLoaded("uve.test-plugin"));
    EXPECT_EQ(host.LoadedCount(), 1U);

    const NativePluginManifest* manifest = registry.FindManifest("uve.test-plugin");
    ASSERT_NE(manifest, nullptr);
    EXPECT_EQ(manifest->displayName, "Host Fixture Plugin");
    EXPECT_EQ(manifest->version, (NativePluginVersion{1U, 2U, 3U}));
    EXPECT_EQ(manifest->capabilityIds, (std::vector<std::string>{"test.objects", "test.window"}));

    EXPECT_TRUE(host.Unload("uve.test-plugin"));
    EXPECT_FALSE(host.IsLoaded("uve.test-plugin"));
    EXPECT_EQ(host.LoadedCount(), 0U);
    EXPECT_EQ(registry.FindManifest("uve.test-plugin"), nullptr);
}

TEST(NativePluginHostTest, LoadMissingFile_ReturnsFileNotFound) {
    NativePluginRegistry registry;
    NativePluginHost host(registry);
    const std::filesystem::path missing =
        std::filesystem::temp_directory_path() / "uve-missing-test-plugin.uvplugin";
    const NativePluginLoadResult result = host.Load(missing);
    EXPECT_EQ(result.code, NativePluginLoadCode::FileNotFound);
    EXPECT_EQ(host.LoadedCount(), 0U);
    EXPECT_EQ(registry.GetManifestCount(), 0U);
}

TEST(NativePluginHostTest, LoadLibraryWithoutEntry_ReturnsEntryMissing) {
    NativePluginRegistry registry;
    NativePluginHost host(registry);
    const NativePluginLoadResult result = host.Load(kNoEntryPluginPath);
    EXPECT_EQ(result.code, NativePluginLoadCode::EntryMissing) << result.message;
    EXPECT_EQ(host.LoadedCount(), 0U);
    EXPECT_EQ(registry.GetManifestCount(), 0U);
}

TEST(NativePluginHostTest, LoadBadProtocolPlugin_ReturnsUnsupportedProtocol) {
    NativePluginRegistry registry;
    NativePluginHost host(registry);
    const NativePluginLoadResult result = host.Load(kBadProtocolPluginPath);
    EXPECT_EQ(result.code, NativePluginLoadCode::UnsupportedProtocol) << result.message;
    EXPECT_EQ(host.LoadedCount(), 0U);
    EXPECT_EQ(registry.GetManifestCount(), 0U);
}

TEST(NativePluginHostTest, LoadDuplicatePlugin_RejectsSecondLoad) {
    NativePluginRegistry registry;
    NativePluginHost host(registry);
    EXPECT_TRUE(host.Load(kValidPluginPath).IsLoaded());
    const NativePluginLoadResult second = host.Load(kValidPluginPath);
    EXPECT_EQ(second.code, NativePluginLoadCode::ManifestRejected);
    EXPECT_EQ(host.LoadedCount(), 1U);
    EXPECT_TRUE(host.Unload("uve.test-plugin"));
    EXPECT_FALSE(host.Unload("uve.test-plugin"));
}

TEST(NativePluginHostTest, UnloadUnknownPlugin_ReturnsFalse) {
    NativePluginRegistry registry;
    NativePluginHost host(registry);
    EXPECT_FALSE(host.Unload("uve.never-loaded"));
}

TEST(NativePluginHostTest, OpenScope_BlocksUnloadUntilClosed) {
    NativePluginRegistry registry;
    NativePluginHost host(registry);
    ASSERT_TRUE(host.Load(kValidPluginPath).IsLoaded());
    const std::optional<NativePluginRegistrationScope> scope = registry.OpenScope("uve.test-plugin");
    ASSERT_TRUE(scope.has_value());
    EXPECT_FALSE(host.Unload("uve.test-plugin"));
    EXPECT_TRUE(host.IsLoaded("uve.test-plugin"));
    EXPECT_TRUE(registry.CloseScope(*scope).IsAccepted());
    EXPECT_TRUE(host.Unload("uve.test-plugin"));
}

TEST(NativePluginHostTest, ReloadAfterUnload_Succeeds) {
    NativePluginRegistry registry;
    NativePluginHost host(registry);
    ASSERT_TRUE(host.Load(kValidPluginPath).IsLoaded());
    ASSERT_TRUE(host.Unload("uve.test-plugin"));
    const NativePluginLoadResult reloaded = host.Load(kValidPluginPath);
    EXPECT_TRUE(reloaded.IsLoaded()) << reloaded.message;
    EXPECT_TRUE(host.Unload("uve.test-plugin"));
}

} // namespace UVE::Plugins::Tests
