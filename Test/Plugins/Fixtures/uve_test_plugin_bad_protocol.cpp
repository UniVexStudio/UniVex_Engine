// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Load-bearing fixture for native_plugin_host_uve_tests: a real shared library whose descriptor
// shape is fine but whose engine protocol the host does not speak. Loading it must fail ABI
// negotiation, not descriptor validation.

#include "uve/plugins/native_plugin_host_uve.h"

namespace {

int InitializeBadProtocolPluginUVE() {
    return 0;
}

void ShutdownBadProtocolPluginUVE() {}

const char* const kCapabilitiesUVE[] = {"test.objects"};

const UVE::Plugins::NativePluginDescriptorUVE kDescriptorUVE{
    UVE::Plugins::kNativePluginProtocolVersionUVE,
    "uve.test-plugin-bad-protocol",
    "Bad Protocol Fixture Plugin",
    1U,
    0U,
    0U,
    99U,
    kCapabilitiesUVE,
    1U,
    &InitializeBadProtocolPluginUVE,
    &ShutdownBadProtocolPluginUVE,
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
