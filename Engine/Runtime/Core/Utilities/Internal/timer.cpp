// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "timer.h"

#include <algorithm>
#include <cmath>

namespace UVE::Utilities {
namespace {

[[nodiscard]] bool IsValidPositiveTimingValue(const double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

} // namespace

Timer::Timer() : m_lastTickTime(std::chrono::steady_clock::now()) {}

void Timer::Tick() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const double rawDelta = std::chrono::duration<double>(now - m_lastTickTime).count();
    m_lastTickTime = now;

    m_totalTime += rawDelta;
    m_deltaTime = std::min(rawDelta, m_maxDeltaTime);
    m_accumulator += m_deltaTime;

    // Prevent accumulator bloat (spiral-of-death guard): never buffer more time than maxStepsPerTick can process.
    const double maxBufferedTime = m_fixedDeltaTime * static_cast<double>(m_maxStepsPerTick);
    if (m_accumulator > maxBufferedTime) {
        m_accumulator = maxBufferedTime;
    }
}

double Timer::GetDeltaTime() const {
    return m_deltaTime;
}

float Timer::GetDeltaTimeFloat() const {
    return static_cast<float>(m_deltaTime);
}

double Timer::GetTotalTime() const {
    return m_totalTime;
}

void Timer::Reset() {
    m_lastTickTime = std::chrono::steady_clock::now();
    m_deltaTime = 0.0;
    m_totalTime = 0.0;
    m_accumulator = 0.0;
}

void Timer::SetMaxDeltaTime(double maxDeltaSeconds) {
    if (IsValidPositiveTimingValue(maxDeltaSeconds)) {
        m_maxDeltaTime = maxDeltaSeconds;
    }
}

void Timer::SetFixedTimestep(double fixedDeltaSeconds) {
    if (IsValidPositiveTimingValue(fixedDeltaSeconds)) {
        m_fixedDeltaTime = fixedDeltaSeconds;
    }
}

void Timer::SetMaxStepsPerTick(const int maxSteps) {
    if (maxSteps >= 1) {
        m_maxStepsPerTick = maxSteps;
    }
}

FixedStepResult Timer::AdvanceFixedStep() {
    FixedStepResult result;

    while (m_accumulator >= m_fixedDeltaTime && result.stepsToRun < m_maxStepsPerTick) {
        m_accumulator -= m_fixedDeltaTime;
        ++result.stepsToRun;
    }

    // Hit the step cap with more than a full step still buffered: drop the
    // remainder down to a single sub-step's worth rather than let the
    // accumulator grow unbounded across frames (see m_maxStepsPerTick).
    if (result.stepsToRun >= m_maxStepsPerTick && m_accumulator >= m_fixedDeltaTime) {
        m_accumulator = std::fmod(m_accumulator, m_fixedDeltaTime);
    }

    result.alpha = (m_fixedDeltaTime > 0.0) ? (m_accumulator / m_fixedDeltaTime) : 0.0;
    return result;
}

void Timer::DiscardFixedStepAccumulator() noexcept {
    m_accumulator = 0.0;
}

} // namespace UVE::Utilities
