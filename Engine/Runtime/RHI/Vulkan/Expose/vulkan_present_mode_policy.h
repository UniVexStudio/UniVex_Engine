// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "application_window_settings.h"

namespace UVE::Render::Vulkan {

enum class PresentModePolicy : std::uint8_t {
    Immediate,
    Fifo,
    FifoRelaxed,
    Mailbox,
};

enum class PresentModeFallback : std::uint8_t {
    None,
    OffUsesMailbox,
    OffUsesFifo,
    AdaptiveUsesFifo,
    MailboxUsesFifo,
    InvalidUsesFifo,
};

struct PresentModeAvailability final {
    bool immediate = false;
    bool fifoRelaxed = false;
    bool mailbox = false;
};

struct PresentModeResolution final {
    PresentModePolicy mode = PresentModePolicy::Fifo;
    PresentModeFallback fallback = PresentModeFallback::None;

    [[nodiscard]] constexpr bool operator==(const PresentModeResolution&) const noexcept = default;
};

/// Resolves a portable V-sync request to the closest Vulkan present mode. FIFO is mandatory by
/// the Vulkan surface contract, so it is the deterministic fallback when an optional mode is absent.
[[nodiscard]] constexpr PresentModeResolution ResolveVulkanPresentMode(
    const Platform::VSyncMode requestedMode, const PresentModeAvailability available) noexcept {
    switch (requestedMode) {
    case Platform::VSyncMode::Off:
        if (available.immediate) {
            return {PresentModePolicy::Immediate, PresentModeFallback::None};
        }
        if (available.mailbox) {
            return {PresentModePolicy::Mailbox, PresentModeFallback::OffUsesMailbox};
        }
        return {PresentModePolicy::Fifo, PresentModeFallback::OffUsesFifo};
    case Platform::VSyncMode::On:
        return {PresentModePolicy::Fifo, PresentModeFallback::None};
    case Platform::VSyncMode::Adaptive:
        return available.fifoRelaxed
                   ? PresentModeResolution{PresentModePolicy::FifoRelaxed, PresentModeFallback::None}
                   : PresentModeResolution{PresentModePolicy::Fifo, PresentModeFallback::AdaptiveUsesFifo};
    case Platform::VSyncMode::Mailbox:
        return available.mailbox
                   ? PresentModeResolution{PresentModePolicy::Mailbox, PresentModeFallback::None}
                   : PresentModeResolution{PresentModePolicy::Fifo, PresentModeFallback::MailboxUsesFifo};
    }
    return {PresentModePolicy::Fifo, PresentModeFallback::InvalidUsesFifo};
}

} // namespace UVE::Render::Vulkan
