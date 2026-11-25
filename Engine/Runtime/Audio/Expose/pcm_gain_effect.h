// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <array>
#include <cstddef>
#include <vector>
namespace UVE::Audio {
inline constexpr std::size_t kMaximumPcmGainSamples = 1U << 20U;
inline constexpr std::size_t kMaximumPcmGainChainEffects = 8U;

struct PcmGainEffectWindow final {
    std::size_t startSample = 0U;
    std::size_t sampleCount = 0U;
    float gain = 1.0F;
    [[nodiscard]] bool operator==(const PcmGainEffectWindow&) const noexcept = default;
};

inline constexpr std::size_t kMaximumPcmGainScheduledWindows = 8U;

/// Owns a fixed-capacity FIFO of caller-owned gain windows. It owns no samples, mixer, device, stream,
/// voice, or backend; queued windows are applied once and removed only after successful output publication.
class PcmGainEffectSchedule final {
public:
    [[nodiscard]] bool ScheduleWindow(const PcmGainEffectWindow& window) noexcept;
    [[nodiscard]] bool ApplyPending(const std::vector<float>& inputSamples,
                                       std::vector<float>& outputSamples) noexcept;
    void Reset() noexcept;
    [[nodiscard]] std::size_t GetPendingWindowCount() const noexcept { return m_count; }

private:
    std::array<PcmGainEffectWindow, kMaximumPcmGainScheduledWindows> m_windows{};
    std::size_t m_count = 0U;
};

/// Applies ordered bounded gain windows to copied samples. Overlapping windows are applied in
/// caller order; the helper owns no mixer, device, stream, voice, or scheduler lifetime.
[[nodiscard]] bool ApplyScheduledPcmGainEffects(
    const std::vector<float>& inputSamples, const std::vector<PcmGainEffectWindow>& windows,
    std::vector<float>& outputSamples) noexcept;
/// Applies an ordered bounded chain of finite nonnegative PCM gain stages to copied samples.
/// Allocation and validation failures return false without publishing partial output. The chain owns
/// no mixer, device, stream, voice, or sample-buffer lifetime.
[[nodiscard]] bool ApplyPcmGainEffectChain(const std::vector<float>& inputSamples,
                                               const std::vector<float>& gains,
                                               std::vector<float>& outputSamples) noexcept;

/// Applies a bounded finite gain to copied normalized PCM samples with hard [-1, 1] clamping.
/// The helper owns no device, voice, mixer, stream, or sample-buffer lifetime.
[[nodiscard]] bool ApplyPcmGainEffect(const std::vector<float>& inputSamples, float gain,
                                         std::vector<float>& outputSamples) noexcept;
} // namespace UVE::Audio
