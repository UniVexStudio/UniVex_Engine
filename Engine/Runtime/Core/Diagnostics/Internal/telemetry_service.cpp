// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "telemetry_service.h"

#include <chrono>
#include <fstream>
#include <utility>

namespace UVE::Core {
namespace {

[[nodiscard]] std::string EscapeJsonStringUVE(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4U);
    for (const char c : s) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

} // namespace

JsonFileTelemetrySinkUVE::JsonFileTelemetrySinkUVE(std::filesystem::path outputFile)
    : m_outputFile(std::move(outputFile)) {}

void JsonFileTelemetrySinkUVE::WriteBatchUVE(const std::vector<TelemetryEventUVE>& events) {
    if (events.empty() || m_outputFile.empty()) {
        return;
    }
    std::ofstream out(m_outputFile, std::ios::app);
    if (!out) {
        return;
    }
    for (const TelemetryEventUVE& ev : events) {
        out << "{\"event\":\"" << EscapeJsonStringUVE(ev.eventName) << "\",\"session\":\""
            << EscapeJsonStringUVE(ev.sessionId) << "\",\"ts\":" << ev.timestampUnixMs
            << ",\"props\":{";
        bool first = true;
        for (const auto& [k, v] : ev.properties) {
            if (!first) out << ',';
            first = false;
            out << '"' << EscapeJsonStringUVE(k) << "\":\"" << EscapeJsonStringUVE(v) << '"';
        }
        out << "}}\n";
    }
}

TelemetryServiceUVE::TelemetryServiceUVE(std::string sessionId)
    : m_sessionId(std::move(sessionId)) {}

void TelemetryServiceUVE::AddSinkUVE(std::shared_ptr<ITelemetrySinkUVE> sink) {
    if (sink) {
        m_sinks.push_back(std::move(sink));
    }
}

void TelemetryServiceUVE::RecordEventUVE(std::string eventName,
                                         std::map<std::string, std::string> properties) {
    if (!m_consentEnabled || eventName.empty()) {
        return;
    }
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    m_pending.push_back(TelemetryEventUVE{
        std::move(eventName),
        m_sessionId,
        static_cast<std::uint64_t>(now),
        std::move(properties),
    });
    ++m_totalRecorded;
}

void TelemetryServiceUVE::FlushUVE() {
    if (m_pending.empty()) {
        return;
    }
    for (const auto& sink : m_sinks) {
        if (sink) {
            sink->WriteBatchUVE(m_pending);
        }
    }
    m_pending.clear();
}

} // namespace UVE::Core
