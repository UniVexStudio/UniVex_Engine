// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Load-bearing fixture for native_plugin_host_uve_tests: a real shared library exporting the
// protocol-1 entry point with a manifest the host accepts.

#include "native_plugin_host.h"

namespace {

int InitializeTestPlugin() {
    return 0;
}

void ShutdownTestPlugin() {}

const char* const kCapabilities[] = {"test.objects", "test.window"};

const UVE::Plugins::NativePluginDescriptor kDescriptor{
    UVE::Plugins::kNativePluginProtocolVersion,
    "uve.test-plugin",
    "Host Fixture Plugin",
    1U,
    2U,
    3U,
    UVE::Plugins::kNativePluginProtocolVersion,
    kCapabilities,
    2U,
    &InitializeTestPlugin,
    &ShutdownTestPlugin,
};

} // namespace

#if defined(_WIN32)
#define UVE_TEST_PLUGIN_EXPORT __declspec(dllexport)
#else
#define UVE_TEST_PLUGIN_EXPORT
#endif

extern "C" UVE_TEST_PLUGIN_EXPORT const UVE::Plugins::NativePluginDescriptor* UveNativePluginEntry() {
    return &kDescriptor;
}
