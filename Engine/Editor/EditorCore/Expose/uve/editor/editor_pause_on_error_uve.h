// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <atomic>
#include <cstdint>

#include "uve/logging/log_sink_uve.h"

namespace UVE::Editor {

/// Counts Error-and-worse log records for Pause on Error. The editor registers one on the
/// active logger at Init and polls the count each frame while playing: a sink never calls back
/// into the editor, so logging from any thread stays safe and pausing stays a main-thread
/// decision. Owned by the logger once registered; the editor keeps a non-owning pointer.
class PlayPauseOnErrorSinkUVE final : public Debug::ILogSinkUVE {
public:
    void Write(const Debug::LogMessageUVE& message) override {
        if (message.level >= Debug::LogLevelUVE::Error) {
            m_errorCount.fetch_add(1U, std::memory_order_relaxed);
        }
    }

    void Flush() override {}

    /// How many Error-or-worse records have arrived since registration.
    [[nodiscard]] std::uint64_t GetErrorCountUVE() const noexcept {
        return m_errorCount.load(std::memory_order_relaxed);
    }

private:
    std::atomic<std::uint64_t> m_errorCount{0U};
};

} // namespace UVE::Editor
