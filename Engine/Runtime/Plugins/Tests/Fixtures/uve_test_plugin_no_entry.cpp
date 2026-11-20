// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Load-bearing fixture for native_plugin_host_uve_tests: a real shared library that exports no
// entry point at all. Loading it must fail with EntryMissing rather than crashing the lookup.

namespace {

[[maybe_unused]] const int kNoEntrySentinelUVE = 0;

} // namespace
