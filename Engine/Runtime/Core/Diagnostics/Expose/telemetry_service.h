// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace UVE::Core {

struct TelemetryEventUVE final {
    std::string eventName;
    std::string sessionId;
    std::uint64_t timestampUnixMs = 0U;
    std::map<std::string, std::string> properties;
};

/// Pluggable analytics / crash-breadcrumb telemetry sink (file, HTTP, or test double).
class ITelemetrySinkUVE {
public:
    virtual ~ITelemetrySinkUVE() = default;
    virtual void WriteBatchUVE(const std::vector<TelemetryEventUVE>& events) = 0;
};

/// Writes telemetry events as newline-delimited JSON (JSONL) for offline diagnostics or upload.
class JsonFileTelemetrySinkUVE final : public ITelemetrySinkUVE {
public:
    explicit JsonFileTelemetrySinkUVE(std::filesystem::path outputFile);
    void WriteBatchUVE(const std::vector<TelemetryEventUVE>& events) override;
    [[nodiscard]] const std::filesystem::path& OutputFileUVE() const noexcept { return m_outputFile; }

private:
    std::filesystem::path m_outputFile;
};

/// Consent-gated structured telemetry & session analytics recorder.
class TelemetryServiceUVE final {
public:
    explicit TelemetryServiceUVE(std::string sessionId = "local-session");

    void SetConsentEnabledUVE(bool enabled) noexcept { m_consentEnabled = enabled; }
    [[nodiscard]] bool IsConsentEnabledUVE() const noexcept { return m_consentEnabled; }

    void AddSinkUVE(std::shared_ptr<ITelemetrySinkUVE> sink);
    void RecordEventUVE(std::string eventName, std::map<std::string, std::string> properties = {});
    void FlushUVE();

    [[nodiscard]] std::size_t PendingEventCountUVE() const noexcept { return m_pending.size(); }
    [[nodiscard]] std::uint64_t TotalRecordedEventsUVE() const noexcept { return m_totalRecorded; }

private:
    std::string m_sessionId;
    bool m_consentEnabled = false;
    std::uint64_t m_totalRecorded = 0U;
    std::vector<TelemetryEventUVE> m_pending;
    std::vector<std::shared_ptr<ITelemetrySinkUVE>> m_sinks;
};

} // namespace UVE::Core
