// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace UVE::Core {

inline constexpr std::uint32_t kMaximumFrameRateCap = 1000U;

/// Selects the configured foreground/background cap. Headless runtimes are deliberately uncapped.
[[nodiscard]] inline std::uint32_t SelectFrameRateCap(const bool windowedRenderingActive,
                                                        const bool focused,
                                                        const std::uint32_t focusedCap,
                                                        const std::uint32_t unfocusedCap) noexcept {
    if (!windowedRenderingActive) {
        return 0U;
    }
    return focused ? focusedCap : unfocusedCap;
}

/// Pure decision used by EngineCore before sleeping. Keeping the clock sample and state as
/// arguments makes cap changes, deadline boundaries, and missed deadlines deterministic to test.
struct FramePacingDecision final {
    using Clock = std::chrono::steady_clock;

    std::uint32_t frameRateCap = 0U;
    Clock::duration framePeriod{};
    std::optional<Clock::time_point> sleepUntil;
};

[[nodiscard]] inline FramePacingDecision ComputeFramePacingDecision(
    const std::uint32_t frameRateCap, const std::uint32_t previousFrameRateCap,
    const std::optional<FramePacingDecision::Clock::time_point>& nextFrameDeadline,
    const FramePacingDecision::Clock::time_point now) noexcept {
    using Clock = FramePacingDecision::Clock;
    if (frameRateCap == 0U || frameRateCap > kMaximumFrameRateCap) {
        return {};
    }

    const Clock::duration framePeriod = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>{1.0 / static_cast<double>(frameRateCap)});
    const bool sameCapHasFutureDeadline = nextFrameDeadline.has_value() &&
                                          previousFrameRateCap == frameRateCap &&
                                          now < *nextFrameDeadline;
    return FramePacingDecision{frameRateCap, framePeriod,
                                  sameCapHasFutureDeadline ? nextFrameDeadline : std::nullopt};
}

} // namespace UVE::Core
