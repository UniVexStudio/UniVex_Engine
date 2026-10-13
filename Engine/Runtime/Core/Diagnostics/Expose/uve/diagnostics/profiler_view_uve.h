// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once
//
// Scope: the profiler panel's view model - the formatting and judgement the dock cannot
// test from behind imgui. The dock prints rows; these builders decide them: durations
// render human, spans sort by begin, counters collapse to latest-per-name. Zero uve/*
// dependencies beyond the capture types themselves.
//

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "uve/diagnostics/profiler_diagnostics_uve.h"

namespace UVE::Core {

/// Short display name for a category ("cpu", "net", ...). "unknown" for Count and
/// anything outside the enum - the panel never prints a bare number.
[[nodiscard]] std::string_view ProfilerCategoryNameUVE(
    DiagnosticCategoryUVE category) noexcept;

/// Human duration: "850ns", "1.50us", "3.40ms", "2.00s" by magnitude.
[[nodiscard]] std::string FormatProfilerDurationUVE(std::uint64_t nanoseconds);

/// Human counter: integral doubles print whole ("2048"), the rest with 3 decimals
/// ("1.500").
[[nodiscard]] std::string FormatProfilerCounterUVE(double value);

struct ProfilerSpanRowUVE final {
    std::string name;
    DiagnosticCategoryUVE category = DiagnosticCategoryUVE::Cpu;
    std::uint64_t beginNanoseconds = 0U;
    std::uint64_t durationNanoseconds = 0U;

    [[nodiscard]] bool operator==(const ProfilerSpanRowUVE&) const noexcept = default;
};

/// One row per span, sorted by (begin, name). Durations saturate: an end before its
/// begin (impossible through the capture, possible in a hand-built vector) shows 0.
[[nodiscard]] std::vector<ProfilerSpanRowUVE> BuildProfilerSpanRowsUVE(
    const std::vector<DiagnosticSpanUVE>& spans);

struct ProfilerCounterRowUVE final {
    std::string name;
    DiagnosticCategoryUVE category = DiagnosticCategoryUVE::Cpu;
    double value = 0.0;
    std::uint64_t timestampNanoseconds = 0U;

    [[nodiscard]] bool operator==(const ProfilerCounterRowUVE&) const noexcept = default;
};

/// Latest value per (category, name), sorted by (category, name) for a stable panel.
/// Timestamp ties keep the last occurrence - the vector order breaks them.
[[nodiscard]] std::vector<ProfilerCounterRowUVE> BuildProfilerCounterRowsUVE(
    const std::vector<DiagnosticCounterUVE>& counters);

} // namespace UVE::Core
