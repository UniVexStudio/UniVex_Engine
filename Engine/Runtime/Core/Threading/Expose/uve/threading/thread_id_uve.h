// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <thread>

namespace UVE::Threading {

/// `ThreadIdUVE` is the strong thread-identity type: a hashed `std::thread::id` that cannot be
/// confused with a frame number, a timestamp, or any other `uint64` at a call site. Profiler
/// captures, per-thread diagnostics and thread-affinity asserts take this, not a bare integer.
///
/// The value is process-local (a `std::hash<std::thread::id>`): never persist it, never compare
/// it across processes. Zero means unknown/unset, matching the profiler structs' default.
/// `Core::Diagnostics` deliberately keeps raw `uint64` fields (that module has zero `uve/*`
/// dependencies by contract) — `ValueUVE()` is the bridge at its call sites.
struct ThreadIdUVE final {
    std::uint64_t value = 0U;

    /// The calling thread's id.
    [[nodiscard]] static ThreadIdUVE CurrentUVE() noexcept {
        return FromNativeUVE(std::this_thread::get_id());
    }

    /// Wraps a native id. Zero-valued hashes stay zero (unknown) — a one-in-2^64 collision the
    /// profiler accepts, since "unknown" only degrades a thread filter, never correctness.
    [[nodiscard]] static ThreadIdUVE FromNativeUVE(const std::thread::id id) noexcept {
        return ThreadIdUVE{static_cast<std::uint64_t>(std::hash<std::thread::id>{}(id))};
    }

    /// The raw value, for the `uint64` boundaries (`Diagnostics`, logs, debuggers).
    [[nodiscard]] std::uint64_t ValueUVE() const noexcept { return value; }

    [[nodiscard]] friend bool operator==(const ThreadIdUVE& lhs, const ThreadIdUVE& rhs) noexcept {
        return lhs.value == rhs.value;
    }

    [[nodiscard]] friend bool operator!=(const ThreadIdUVE& lhs, const ThreadIdUVE& rhs) noexcept {
        return !(lhs == rhs);
    }
};

} // namespace UVE::Threading

namespace std {

template <>
struct hash<UVE::Threading::ThreadIdUVE> final {
    [[nodiscard]] std::size_t operator()(const UVE::Threading::ThreadIdUVE& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value);
    }
};

} // namespace std
