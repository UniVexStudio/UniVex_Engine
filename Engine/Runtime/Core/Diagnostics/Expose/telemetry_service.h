// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace UVE::Core {

struct TelemetryEvent final {
    std::string eventName;
    std::string sessionId;
    std::uint64_t timestampUnixMs = 0U;
    std::map<std::string, std::string> properties;
};

/// Pluggable analytics / crash-breadcrumb telemetry sink (file, HTTP, or test double).
class ITelemetrySink {
public:
    virtual ~ITelemetrySink() = default;
    virtual void WriteBatch(const std::vector<TelemetryEvent>& events) = 0;
};

/// Writes telemetry events as newline-delimited JSON (JSONL) for offline diagnostics or upload.
class JsonFileTelemetrySink final : public ITelemetrySink {
public:
    explicit JsonFileTelemetrySink(std::filesystem::path outputFile);
    void WriteBatch(const std::vector<TelemetryEvent>& events) override;
    [[nodiscard]] const std::filesystem::path& OutputFile() const noexcept { return m_outputFile; }

private:
    std::filesystem::path m_outputFile;
};

/// Consent-gated structured telemetry & session analytics recorder.
class TelemetryService final {
public:
    explicit TelemetryService(std::string sessionId = "local-session");

    void SetConsentEnabled(bool enabled) noexcept { m_consentEnabled = enabled; }
    [[nodiscard]] bool IsConsentEnabled() const noexcept { return m_consentEnabled; }

    void AddSink(std::shared_ptr<ITelemetrySink> sink);
    void RecordEvent(std::string eventName, std::map<std::string, std::string> properties = {});
    void Flush();

    [[nodiscard]] std::size_t PendingEventCount() const noexcept { return m_pending.size(); }
    [[nodiscard]] std::uint64_t TotalRecordedEvents() const noexcept { return m_totalRecorded; }

private:
    std::string m_sessionId;
    bool m_consentEnabled = false;
    std::uint64_t m_totalRecorded = 0U;
    std::vector<TelemetryEvent> m_pending;
    std::vector<std::shared_ptr<ITelemetrySink>> m_sinks;
};

} // namespace UVE::Core
