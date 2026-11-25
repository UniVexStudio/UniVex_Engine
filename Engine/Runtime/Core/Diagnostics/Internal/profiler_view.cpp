// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "profiler_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <tuple>

namespace UVE::Core {

std::string_view ProfilerCategoryName(const DiagnosticCategory category) noexcept {
    switch (category) {
        case DiagnosticCategory::Cpu:
            return "cpu";
        case DiagnosticCategory::Task:
            return "task";
        case DiagnosticCategory::Memory:
            return "memory";
        case DiagnosticCategory::Animation:
            return "animation";
        case DiagnosticCategory::ECS:
            return "ecs";
        case DiagnosticCategory::VM:
            return "vm";
        case DiagnosticCategory::Render:
            return "render";
        case DiagnosticCategory::Audio:
            return "audio";
        case DiagnosticCategory::Streaming:
            return "streaming";
        case DiagnosticCategory::Net:
            return "net";
        case DiagnosticCategory::Count:
            return "unknown";
    }
    return "unknown";
}

std::string FormatProfilerDuration(const std::uint64_t nanoseconds) {
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

std::string FormatProfilerCounter(const double value) {
    char text[32];
    if (std::isfinite(value) && value == std::trunc(value) && std::fabs(value) < 1e15) {
        std::snprintf(text, sizeof(text), "%.0f", value);
        return text;
    }
    std::snprintf(text, sizeof(text), "%.3f", value);
    return text;
}

std::vector<ProfilerSpanRow> BuildProfilerSpanRows(
    const std::vector<DiagnosticSpan>& spans) {
    std::vector<ProfilerSpanRow> rows;
    rows.reserve(spans.size());
    for (const DiagnosticSpan& span : spans) {
        ProfilerSpanRow row;
        row.name = span.name;
        row.category = span.category;
        row.beginNanoseconds = span.beginTimestampNanoseconds;
        row.durationNanoseconds = span.endTimestampNanoseconds >= span.beginTimestampNanoseconds
                                      ? span.endTimestampNanoseconds - span.beginTimestampNanoseconds
                                      : 0U;
        rows.push_back(std::move(row));
    }
    std::sort(rows.begin(), rows.end(),
              [](const ProfilerSpanRow& left, const ProfilerSpanRow& right) {
                  return std::tie(left.beginNanoseconds, left.name) <
                         std::tie(right.beginNanoseconds, right.name);
              });
    return rows;
}

std::vector<ProfilerCounterRow> BuildProfilerCounterRows(
    const std::vector<DiagnosticCounter>& counters) {
    std::map<std::pair<DiagnosticCategory, std::string>, ProfilerCounterRow> latest;
    for (const DiagnosticCounter& counter : counters) {
        const std::pair<DiagnosticCategory, std::string> key{counter.category, counter.name};
        const auto found = latest.find(key);
        if (found == latest.end() ||
            counter.timestampNanoseconds >= found->second.timestampNanoseconds) {
            ProfilerCounterRow row;
            row.name = counter.name;
            row.category = counter.category;
            row.value = counter.value;
            row.timestampNanoseconds = counter.timestampNanoseconds;
            latest[key] = std::move(row);
        }
    }
    std::vector<ProfilerCounterRow> rows;
    rows.reserve(latest.size());
    for (auto& [_, row] : latest) {
        rows.push_back(std::move(row));
    }
    return rows;
}

} // namespace UVE::Core
