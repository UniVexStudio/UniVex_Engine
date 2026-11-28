// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Load-bearing fixture for native_plugin_host_uve_tests: a real shared library whose descriptor
// shape is fine but whose engine protocol the host does not speak. Loading it must fail ABI
// negotiation, not descriptor validation.

#include "native_plugin_host.h"

namespace {

int InitializeBadProtocolPlugin() {
    return 0;
}

void ShutdownBadProtocolPlugin() {}

const char* const kCapabilities[] = {"test.objects"};

const UVE::Plugins::NativePluginDescriptor kDescriptor{
    UVE::Plugins::kNativePluginProtocolVersion,
    "uve.test-plugin-bad-protocol",
    "Bad Protocol Fixture Plugin",
    1U,
    0U,
    0U,
    99U,
    kCapabilities,
    1U,
    &InitializeBadProtocolPlugin,
    &ShutdownBadProtocolPlugin,
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
