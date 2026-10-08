// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/platform/application_window_settings_uve.h"

namespace UVE::Render::Vulkan {

enum class PresentModePolicyUVE : std::uint8_t {
    Immediate,
    Fifo,
    FifoRelaxed,
    Mailbox,
};

enum class PresentModeFallbackUVE : std::uint8_t {
    None,
    OffUsesMailbox,
    OffUsesFifo,
    AdaptiveUsesFifo,
    MailboxUsesFifo,
    InvalidUsesFifo,
};

struct PresentModeAvailabilityUVE final {
    bool immediate = false;
    bool fifoRelaxed = false;
    bool mailbox = false;
};

struct PresentModeResolutionUVE final {
    PresentModePolicyUVE mode = PresentModePolicyUVE::Fifo;
    PresentModeFallbackUVE fallback = PresentModeFallbackUVE::None;

    [[nodiscard]] constexpr bool operator==(const PresentModeResolutionUVE&) const noexcept = default;
};

/// Resolves a portable V-sync request to the closest Vulkan present mode. FIFO is mandatory by
/// the Vulkan surface contract, so it is the deterministic fallback when an optional mode is absent.
[[nodiscard]] constexpr PresentModeResolutionUVE ResolveVulkanPresentModeUVE(
    const Platform::VSyncModeUVE requestedMode, const PresentModeAvailabilityUVE available) noexcept {
    switch (requestedMode) {
    case Platform::VSyncModeUVE::Off:
        if (available.immediate) {
            return {PresentModePolicyUVE::Immediate, PresentModeFallbackUVE::None};
        }
        if (available.mailbox) {
            return {PresentModePolicyUVE::Mailbox, PresentModeFallbackUVE::OffUsesMailbox};
        }
        return {PresentModePolicyUVE::Fifo, PresentModeFallbackUVE::OffUsesFifo};
    case Platform::VSyncModeUVE::On:
        return {PresentModePolicyUVE::Fifo, PresentModeFallbackUVE::None};
    case Platform::VSyncModeUVE::Adaptive:
        return available.fifoRelaxed
                   ? PresentModeResolutionUVE{PresentModePolicyUVE::FifoRelaxed, PresentModeFallbackUVE::None}
                   : PresentModeResolutionUVE{PresentModePolicyUVE::Fifo, PresentModeFallbackUVE::AdaptiveUsesFifo};
    case Platform::VSyncModeUVE::Mailbox:
        return available.mailbox
                   ? PresentModeResolutionUVE{PresentModePolicyUVE::Mailbox, PresentModeFallbackUVE::None}
                   : PresentModeResolutionUVE{PresentModePolicyUVE::Fifo, PresentModeFallbackUVE::MailboxUsesFifo};
    }
    return {PresentModePolicyUVE::Fifo, PresentModeFallbackUVE::InvalidUsesFifo};
}

} // namespace UVE::Render::Vulkan
