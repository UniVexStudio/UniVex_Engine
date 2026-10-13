// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/plugins/native_plugin_host_uve.h"

#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Plugins::Tests {
namespace {

const std::filesystem::path kValidPluginPathUVE{UVE_TEST_PLUGIN_VALID_PATH};
const std::filesystem::path kBadProtocolPluginPathUVE{UVE_TEST_PLUGIN_BAD_PROTOCOL_PATH};
const std::filesystem::path kNoEntryPluginPathUVE{UVE_TEST_PLUGIN_NO_ENTRY_PATH};

NativePluginDescriptorUVE MakeDescriptorUVE() {
    static const char* const capabilities[] = {"test.objects", "test.window"};
    NativePluginDescriptorUVE descriptor;
    descriptor.abiVersion = kNativePluginProtocolVersionUVE;
    descriptor.pluginId = "uve.unit-plugin";
    descriptor.displayName = "Unit Plugin";
    descriptor.versionMajor = 2U;
    descriptor.versionMinor = 3U;
    descriptor.versionPatch = 4U;
    descriptor.requiredEngineProtocol = kNativePluginProtocolVersionUVE;
    descriptor.capabilityIds = capabilities;
    descriptor.capabilityCount = 2U;
    return descriptor;
}

} // namespace

TEST(NativePluginDescriptorValidationUVETest, ValidDescriptor_CopiesManifest) {
    const NativePluginDescriptorUVE descriptor = MakeDescriptorUVE();
    NativePluginManifestUVE manifest;
    const NativePluginDescriptorResultUVE result = ValidateNativePluginDescriptorUVE(&descriptor, manifest);
    EXPECT_TRUE(result.IsValidUVE());
    EXPECT_EQ(manifest.pluginId, "uve.unit-plugin");
    EXPECT_EQ(manifest.displayName, "Unit Plugin");
    EXPECT_EQ(manifest.version, (NativePluginVersionUVE{2U, 3U, 4U}));
    EXPECT_EQ(manifest.requiredEngineProtocol, kNativePluginProtocolVersionUVE);
    EXPECT_EQ(manifest.capabilityIds, (std::vector<std::string>{"test.objects", "test.window"}));
}

TEST(NativePluginDescriptorValidationUVETest, NullDescriptor_ResetsManifest) {
    NativePluginManifestUVE manifest{"junk", "Junk", {9U, 9U, 9U}, 9U, {"junk"}};
    const NativePluginDescriptorResultUVE result = ValidateNativePluginDescriptorUVE(nullptr, manifest);
    EXPECT_EQ(result.code, NativePluginDescriptorCodeUVE::NullDescriptor);
    EXPECT_EQ(manifest, NativePluginManifestUVE{});
}

TEST(NativePluginDescriptorValidationUVETest, BadAbiVersion_ReportsUnsupportedAbi) {
    NativePluginDescriptorUVE descriptor = MakeDescriptorUVE();
    descriptor.abiVersion = 99U;
    NativePluginManifestUVE manifest;
    const NativePluginDescriptorResultUVE result = ValidateNativePluginDescriptorUVE(&descriptor, manifest);
    EXPECT_EQ(result.code, NativePluginDescriptorCodeUVE::UnsupportedAbi);
    EXPECT_EQ(manifest, NativePluginManifestUVE{});
}

TEST(NativePluginDescriptorValidationUVETest, NullIdentifier_ReportsInvalidIdentifier) {
    NativePluginDescriptorUVE descriptor = MakeDescriptorUVE();
    descriptor.pluginId = nullptr;
    NativePluginManifestUVE manifest;
    const NativePluginDescriptorResultUVE result = ValidateNativePluginDescriptorUVE(&descriptor, manifest);
    EXPECT_EQ(result.code, NativePluginDescriptorCodeUVE::InvalidIdentifier);
}

TEST(NativePluginDescriptorValidationUVETest, TooManyCapabilities_ReportsLimit) {
    NativePluginDescriptorUVE descriptor = MakeDescriptorUVE();
    descriptor.capabilityCount =
        static_cast<std::uint32_t>(NativePluginRegistryUVE::kMaximumCapabilitiesPerPluginUVE + 1U);
    NativePluginManifestUVE manifest;
    const NativePluginDescriptorResultUVE result = ValidateNativePluginDescriptorUVE(&descriptor, manifest);
    EXPECT_EQ(result.code, NativePluginDescriptorCodeUVE::TooManyCapabilities);
}

TEST(NativePluginDescriptorValidationUVETest, NullArrayWithCount_ReportsInvalidCapabilityId) {
    NativePluginDescriptorUVE descriptor = MakeDescriptorUVE();
    descriptor.capabilityIds = nullptr;
    NativePluginManifestUVE manifest;
    const NativePluginDescriptorResultUVE result = ValidateNativePluginDescriptorUVE(&descriptor, manifest);
    EXPECT_EQ(result.code, NativePluginDescriptorCodeUVE::InvalidCapabilityId);
}

TEST(NativePluginHostUVETest, LoadValidPlugin_RegistersManifestAndUnloads) {
    NativePluginRegistryUVE registry;
    NativePluginHostUVE host(registry);
    const NativePluginLoadResultUVE loaded = host.LoadUVE(kValidPluginPathUVE);
    EXPECT_TRUE(loaded.IsLoadedUVE()) << loaded.message;
    EXPECT_EQ(loaded.pluginId, "uve.test-plugin");
    EXPECT_TRUE(host.IsLoadedUVE("uve.test-plugin"));
    EXPECT_EQ(host.LoadedCountUVE(), 1U);

    const NativePluginManifestUVE* manifest = registry.FindManifestUVE("uve.test-plugin");
    ASSERT_NE(manifest, nullptr);
    EXPECT_EQ(manifest->displayName, "Host Fixture Plugin");
    EXPECT_EQ(manifest->version, (NativePluginVersionUVE{1U, 2U, 3U}));
    EXPECT_EQ(manifest->capabilityIds, (std::vector<std::string>{"test.objects", "test.window"}));

    EXPECT_TRUE(host.UnloadUVE("uve.test-plugin"));
    EXPECT_FALSE(host.IsLoadedUVE("uve.test-plugin"));
    EXPECT_EQ(host.LoadedCountUVE(), 0U);
    EXPECT_EQ(registry.FindManifestUVE("uve.test-plugin"), nullptr);
}

TEST(NativePluginHostUVETest, LoadMissingFile_ReturnsFileNotFound) {
    NativePluginRegistryUVE registry;
    NativePluginHostUVE host(registry);
    const std::filesystem::path missing =
        std::filesystem::temp_directory_path() / "uve-missing-test-plugin.uvplugin";
    const NativePluginLoadResultUVE result = host.LoadUVE(missing);
    EXPECT_EQ(result.code, NativePluginLoadCodeUVE::FileNotFound);
    EXPECT_EQ(host.LoadedCountUVE(), 0U);
    EXPECT_EQ(registry.GetManifestCountUVE(), 0U);
}

TEST(NativePluginHostUVETest, LoadLibraryWithoutEntry_ReturnsEntryMissing) {
    NativePluginRegistryUVE registry;
    NativePluginHostUVE host(registry);
    const NativePluginLoadResultUVE result = host.LoadUVE(kNoEntryPluginPathUVE);
    EXPECT_EQ(result.code, NativePluginLoadCodeUVE::EntryMissing) << result.message;
    EXPECT_EQ(host.LoadedCountUVE(), 0U);
    EXPECT_EQ(registry.GetManifestCountUVE(), 0U);
}

TEST(NativePluginHostUVETest, LoadBadProtocolPlugin_ReturnsUnsupportedProtocol) {
    NativePluginRegistryUVE registry;
    NativePluginHostUVE host(registry);
    const NativePluginLoadResultUVE result = host.LoadUVE(kBadProtocolPluginPathUVE);
    EXPECT_EQ(result.code, NativePluginLoadCodeUVE::UnsupportedProtocol) << result.message;
    EXPECT_EQ(host.LoadedCountUVE(), 0U);
    EXPECT_EQ(registry.GetManifestCountUVE(), 0U);
}

TEST(NativePluginHostUVETest, LoadDuplicatePlugin_RejectsSecondLoad) {
    NativePluginRegistryUVE registry;
    NativePluginHostUVE host(registry);
    EXPECT_TRUE(host.LoadUVE(kValidPluginPathUVE).IsLoadedUVE());
    const NativePluginLoadResultUVE second = host.LoadUVE(kValidPluginPathUVE);
    EXPECT_EQ(second.code, NativePluginLoadCodeUVE::ManifestRejected);
    EXPECT_EQ(host.LoadedCountUVE(), 1U);
    EXPECT_TRUE(host.UnloadUVE("uve.test-plugin"));
    EXPECT_FALSE(host.UnloadUVE("uve.test-plugin"));
}

TEST(NativePluginHostUVETest, UnloadUnknownPlugin_ReturnsFalse) {
    NativePluginRegistryUVE registry;
    NativePluginHostUVE host(registry);
    EXPECT_FALSE(host.UnloadUVE("uve.never-loaded"));
}

TEST(NativePluginHostUVETest, OpenScope_BlocksUnloadUntilClosed) {
    NativePluginRegistryUVE registry;
    NativePluginHostUVE host(registry);
    ASSERT_TRUE(host.LoadUVE(kValidPluginPathUVE).IsLoadedUVE());
    const std::optional<NativePluginRegistrationScopeUVE> scope = registry.OpenScopeUVE("uve.test-plugin");
    ASSERT_TRUE(scope.has_value());
    EXPECT_FALSE(host.UnloadUVE("uve.test-plugin"));
    EXPECT_TRUE(host.IsLoadedUVE("uve.test-plugin"));
    EXPECT_TRUE(registry.CloseScopeUVE(*scope).IsAcceptedUVE());
    EXPECT_TRUE(host.UnloadUVE("uve.test-plugin"));
}

TEST(NativePluginHostUVETest, ReloadAfterUnload_Succeeds) {
    NativePluginRegistryUVE registry;
    NativePluginHostUVE host(registry);
    ASSERT_TRUE(host.LoadUVE(kValidPluginPathUVE).IsLoadedUVE());
    ASSERT_TRUE(host.UnloadUVE("uve.test-plugin"));
    const NativePluginLoadResultUVE reloaded = host.LoadUVE(kValidPluginPathUVE);
    EXPECT_TRUE(reloaded.IsLoadedUVE()) << reloaded.message;
    EXPECT_TRUE(host.UnloadUVE("uve.test-plugin"));
}

} // namespace UVE::Plugins::Tests
