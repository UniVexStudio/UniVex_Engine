// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/diagnostics/profiler_view_uve.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <tuple>

namespace UVE::Core {

std::string_view ProfilerCategoryNameUVE(const DiagnosticCategoryUVE category) noexcept {
    switch (category) {
        case DiagnosticCategoryUVE::Cpu:
            return "cpu";
        case DiagnosticCategoryUVE::Task:
            return "task";
        case DiagnosticCategoryUVE::Memory:
            return "memory";
        case DiagnosticCategoryUVE::Animation:
            return "animation";
        case DiagnosticCategoryUVE::ECS:
            return "ecs";
        case DiagnosticCategoryUVE::VM:
            return "vm";
        case DiagnosticCategoryUVE::Render:
            return "render";
        case DiagnosticCategoryUVE::Audio:
            return "audio";
        case DiagnosticCategoryUVE::Streaming:
            return "streaming";
        case DiagnosticCategoryUVE::Net:
            return "net";
        case DiagnosticCategoryUVE::Count:
            return "unknown";
    }
    return "unknown";
}

std::string FormatProfilerDurationUVE(const std::uint64_t nanoseconds) {
    char text[32];
    if (nanoseconds < 1000U) {
        std::snprintf(text, sizeof(text), "%lluns", static_cast<unsigned long long>(nanoseconds));
        return text;
    }
    if (nanoseconds < 1000000U) {
        std::snprintf(text, sizeof(text), "%.2fus",
                      static_cast<double>(nanoseconds) / 1000.0);
        return text;
    }
    if (nanoseconds < 1000000000U) {
        std::snprintf(text, sizeof(text), "%.2fms",
                      static_cast<double>(nanoseconds) / 1000000.0);
        return text;
    }
    std::snprintf(text, sizeof(text), "%.2fs", static_cast<double>(nanoseconds) / 1000000000.0);
    return text;
}

std::string FormatProfilerCounterUVE(const double value) {
    char text[32];
    if (std::isfinite(value) && value == std::trunc(value) && std::fabs(value) < 1e15) {
        std::snprintf(text, sizeof(text), "%.0f", value);
        return text;
    }
    std::snprintf(text, sizeof(text), "%.3f", value);
    return text;
}

std::vector<ProfilerSpanRowUVE> BuildProfilerSpanRowsUVE(
    const std::vector<DiagnosticSpanUVE>& spans) {
    std::vector<ProfilerSpanRowUVE> rows;
    rows.reserve(spans.size());
    for (const DiagnosticSpanUVE& span : spans) {
        ProfilerSpanRowUVE row;
        row.name = span.name;
        row.category = span.category;
        row.beginNanoseconds = span.beginTimestampNanoseconds;
        row.durationNanoseconds = span.endTimestampNanoseconds >= span.beginTimestampNanoseconds
                                      ? span.endTimestampNanoseconds - span.beginTimestampNanoseconds
                                      : 0U;
        rows.push_back(std::move(row));
    }
    std::sort(rows.begin(), rows.end(),
              [](const ProfilerSpanRowUVE& left, const ProfilerSpanRowUVE& right) {
                  return std::tie(left.beginNanoseconds, left.name) <
                         std::tie(right.beginNanoseconds, right.name);
              });
    return rows;
}

std::vector<ProfilerCounterRowUVE> BuildProfilerCounterRowsUVE(
    const std::vector<DiagnosticCounterUVE>& counters) {
    std::map<std::pair<DiagnosticCategoryUVE, std::string>, ProfilerCounterRowUVE> latest;
    for (const DiagnosticCounterUVE& counter : counters) {
        const std::pair<DiagnosticCategoryUVE, std::string> key{counter.category, counter.name};
        const auto found = latest.find(key);
        if (found == latest.end() ||
            counter.timestampNanoseconds >= found->second.timestampNanoseconds) {
            ProfilerCounterRowUVE row;
            row.name = counter.name;
            row.category = counter.category;
            row.value = counter.value;
            row.timestampNanoseconds = counter.timestampNanoseconds;
            latest[key] = std::move(row);
        }
    }
    std::vector<ProfilerCounterRowUVE> rows;
    rows.reserve(latest.size());
    for (auto& [_, row] : latest) {
        rows.push_back(std::move(row));
    }
    return rows;
}

} // namespace UVE::Core
