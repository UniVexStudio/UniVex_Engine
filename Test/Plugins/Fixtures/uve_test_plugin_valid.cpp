// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Load-bearing fixture for native_plugin_host_uve_tests: a real shared library exporting the
// protocol-1 entry point with a manifest the host accepts.

#include "uve/plugins/native_plugin_host_uve.h"

namespace {

int InitializeTestPluginUVE() {
    return 0;
}

void ShutdownTestPluginUVE() {}

const char* const kCapabilitiesUVE[] = {"test.objects", "test.window"};

const UVE::Plugins::NativePluginDescriptorUVE kDescriptorUVE{
    UVE::Plugins::kNativePluginProtocolVersionUVE,
    "uve.test-plugin",
    "Host Fixture Plugin",
    1U,
    2U,
    3U,
    UVE::Plugins::kNativePluginProtocolVersionUVE,
    kCapabilitiesUVE,
    2U,
    &InitializeTestPluginUVE,
    &ShutdownTestPluginUVE,
};

} // namespace

#if defined(_WIN32)
#define UVE_TEST_PLUGIN_EXPORT __declspec(dllexport)
#else
#define UVE_TEST_PLUGIN_EXPORT
#endif

extern "C" UVE_TEST_PLUGIN_EXPORT const UVE::Plugins::NativePluginDescriptorUVE* UveNativePluginEntryUVE() {
    return &kDescriptorUVE;
}
