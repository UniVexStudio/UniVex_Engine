// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <chrono>

#include "i_timer.h"

namespace UVE::Utilities {

/// Timer is the concrete, engine-standard implementation of ITimer,
/// wrapping std::chrono::steady_clock. Delta time and total time are stored
/// internally as double: a float total would lose meaningful precision
/// over a long-running session, while a float delta is fine for per-frame
/// use (see GetDeltaTimeFloat).
/// Thread-safety: not thread-safe. Owned and ticked exclusively by the
/// frame-pipeline thread (EngineCore's main loop); no internal
/// synchronization is provided.
class Timer final : public ITimer {
public:
    Timer();

    void Tick() override;
    [[nodiscard]] double GetDeltaTime() const override;
    [[nodiscard]] float GetDeltaTimeFloat() const override;
    [[nodiscard]] double GetTotalTime() const override;
    void Reset() override;
    void SetMaxDeltaTime(double maxDeltaSeconds) override;
    void SetFixedTimestep(double fixedDeltaSeconds) override;
    void SetMaxStepsPerTick(int maxSteps) override;
    FixedStepResult AdvanceFixedStep() override;
    void DiscardFixedStepAccumulator() noexcept override;

private:
    std::chrono::steady_clock::time_point m_lastTickTime;
    double m_deltaTime = 0.0;
    double m_totalTime = 0.0;
    double m_maxDeltaTime = 0.10;

    double m_fixedDeltaTime = 1.0 / 60.0;
    double m_accumulator = 0.0;

    /// Cap on stepsToRun returned by a single AdvanceFixedStep() call
    /// (SetMaxStepsPerTick). A second, independent spiral-of-death guard
    /// beyond the per-Tick() delta clamp: even with a clamped delta, a
    /// sustained run of low frame rates could otherwise let the accumulator
    /// grow forever if steps consistently take longer to run than they
    /// simulate.
    static constexpr int kDefaultMaxStepsPerTick = 4;
    int m_maxStepsPerTick = kDefaultMaxStepsPerTick;
};

} // namespace UVE::Utilities
