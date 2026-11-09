// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>
#include <string_view>

namespace UVE::Threading {

/// Names the calling thread for external profilers and debuggers — worker threads anonymous in
/// a profiler are unactionable stacks. The pool names its workers `UVEWorkerN` through this;
/// any long-lived thread (streaming, audio, network) should do the same at startup.
///
/// Only the CURRENT thread can be named: that is the portable subset (macOS offers no
/// cross-thread naming at all; the pool's old Linux-only joiner-side helper was replaced for
/// exactly this reason). Linux truncates to 15 bytes plus the terminator — keep names short.
/// Returns false when the platform refuses or offers no naming; naming is best-effort
/// observability, never load-bearing, so callers do not branch on the result.
[[nodiscard]] bool SetCurrentThreadNameUVE(std::string_view name) noexcept;

/// Reads the calling thread's name back. False when the platform cannot report one (or none
/// was set); `outName` is untouched on failure. Exists for tests and for diagnostics that echo
/// the name — not for logic, since a name is not an identity (see `ThreadIdUVE`).
bool TryGetCurrentThreadNameUVE(std::string& outName);

} // namespace UVE::Threading
