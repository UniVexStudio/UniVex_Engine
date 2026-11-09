// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace UVE::Core {

struct DynamicRenderResolutionSettingsUVE final {
    double minimumScale = 0.5;
    double maximumScale = 1.0;
    double targetFrameTimeMilliseconds = 16.6667;
};

/// Slow, hysteretic frame-budget controller for dynamic scene resolution. It consumes wall-clock
/// frame duration supplied by EngineCoreUVE (the sample includes Present/swap waiting; this is not
/// a GPU timestamp query). Downscaling reacts after 8 consecutive over-budget samples; quality is
/// restored more slowly, after 30 consecutive well-under-budget samples.
class DynamicRenderResolutionControllerUVE final {
public:
    [[nodiscard]] bool ConfigureUVE(const DynamicRenderResolutionSettingsUVE& settings) noexcept {
        if (!std::isfinite(settings.minimumScale) || !std::isfinite(settings.maximumScale) ||
            !std::isfinite(settings.targetFrameTimeMilliseconds) || settings.minimumScale <= 0.0 ||
            settings.maximumScale < settings.minimumScale || settings.maximumScale > 1.0 ||
            settings.targetFrameTimeMilliseconds <= 0.0) {
            return false;
        }

        m_settings = settings;
        m_currentScale = settings.maximumScale;
        m_smoothedFrameTimeMilliseconds = 0.0;
        m_hasFrameTimeSample = false;
        m_overBudgetFrameCount = 0U;
        m_underBudgetFrameCount = 0U;
        return true;
    }

    /// Adds one positive wall-clock frame duration. Returns true only when the effective scale
    /// changes; invalid samples are ignored without disturbing the existing smoothing state.
    [[nodiscard]] bool ObserveFrameTimeUVE(const double frameTimeSeconds) noexcept {
        if (!std::isfinite(frameTimeSeconds) || frameTimeSeconds <= 0.0) {
            return false;
        }
        const double frameTimeMilliseconds = frameTimeSeconds * 1000.0;
        if (!std::isfinite(frameTimeMilliseconds) || frameTimeMilliseconds <= 0.0) {
            return false;
        }

        if (!m_hasFrameTimeSample) {
            m_smoothedFrameTimeMilliseconds = frameTimeMilliseconds;
            m_hasFrameTimeSample = true;
        } else {
            // Respond quickly enough after a resolution step that the old budget violation does
            // not trigger several downscales before the lower-cost frame samples are considered.
            constexpr double kSmoothingAlphaUVE = 0.25;
            m_smoothedFrameTimeMilliseconds +=
                (frameTimeMilliseconds - m_smoothedFrameTimeMilliseconds) * kSmoothingAlphaUVE;
        }

        const double slowFrameThreshold = m_settings.targetFrameTimeMilliseconds * 1.08;
        const double fastFrameThreshold = m_settings.targetFrameTimeMilliseconds * 0.85;
        if (m_smoothedFrameTimeMilliseconds > slowFrameThreshold) {
            ++m_overBudgetFrameCount;
            m_underBudgetFrameCount = 0U;
        } else if (m_smoothedFrameTimeMilliseconds < fastFrameThreshold) {
            ++m_underBudgetFrameCount;
            m_overBudgetFrameCount = 0U;
        } else {
            m_overBudgetFrameCount = 0U;
            m_underBudgetFrameCount = 0U;
        }

        if (m_overBudgetFrameCount >= kOverBudgetSamplesBeforeDownscaleUVE) {
            m_overBudgetFrameCount = 0U;
            m_underBudgetFrameCount = 0U;
            const double nextScale = std::max(m_settings.minimumScale, m_currentScale - kDownscaleStepUVE);
            if (nextScale < m_currentScale) {
                m_currentScale = nextScale;
                return true;
            }
        }

        if (m_underBudgetFrameCount >= kUnderBudgetSamplesBeforeUpscaleUVE) {
            m_overBudgetFrameCount = 0U;
            m_underBudgetFrameCount = 0U;
            const double nextScale = std::min(m_settings.maximumScale, m_currentScale + kUpscaleStepUVE);
            if (nextScale > m_currentScale) {
                m_currentScale = nextScale;
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] double GetCurrentScaleUVE() const noexcept { return m_currentScale; }
    [[nodiscard]] double GetSmoothedFrameTimeMillisecondsUVE() const noexcept {
        return m_smoothedFrameTimeMilliseconds;
    }

    inline static constexpr std::uint32_t kOverBudgetSamplesBeforeDownscaleUVE = 8U;
    inline static constexpr std::uint32_t kUnderBudgetSamplesBeforeUpscaleUVE = 30U;
    inline static constexpr double kDownscaleStepUVE = 0.05;
    inline static constexpr double kUpscaleStepUVE = 0.025;

private:
    DynamicRenderResolutionSettingsUVE m_settings{};
    double m_currentScale = 1.0;
    double m_smoothedFrameTimeMilliseconds = 0.0;
    bool m_hasFrameTimeSample = false;
    std::uint32_t m_overBudgetFrameCount = 0U;
    std::uint32_t m_underBudgetFrameCount = 0U;
};

} // namespace UVE::Core
