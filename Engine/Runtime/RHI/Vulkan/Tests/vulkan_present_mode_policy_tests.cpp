// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "vulkan_present_mode_policy.h"

#include <gtest/gtest.h>

namespace UVE::Render::Vulkan::Tests {
namespace {

using Platform::VSyncMode;

TEST(VulkanPresentModePolicyTest, OffPrefersImmediateThenMailboxThenFifo) {
    const PresentModeAvailability allModes{true, true, true};
    EXPECT_EQ(ResolveVulkanPresentMode(VSyncMode::Off, allModes),
              (PresentModeResolution{PresentModePolicy::Immediate, PresentModeFallback::None}));

    EXPECT_EQ(ResolveVulkanPresentMode(VSyncMode::Off, PresentModeAvailability{false, false, true}),
              (PresentModeResolution{PresentModePolicy::Mailbox,
                                        PresentModeFallback::OffUsesMailbox}));
    EXPECT_EQ(ResolveVulkanPresentMode(VSyncMode::Off, PresentModeAvailability{}),
              (PresentModeResolution{PresentModePolicy::Fifo,
                                        PresentModeFallback::OffUsesFifo}));
}

TEST(VulkanPresentModePolicyTest, OnAlwaysUsesMandatoryFifo) {
    EXPECT_EQ(ResolveVulkanPresentMode(VSyncMode::On, PresentModeAvailability{true, true, true}),
              (PresentModeResolution{PresentModePolicy::Fifo, PresentModeFallback::None}));
}

TEST(VulkanPresentModePolicyTest, AdaptiveAndMailboxUseRequestedModeOrFallbackToFifo) {
    EXPECT_EQ(ResolveVulkanPresentMode(VSyncMode::Adaptive, PresentModeAvailability{false, true, false}),
              (PresentModeResolution{PresentModePolicy::FifoRelaxed, PresentModeFallback::None}));
    EXPECT_EQ(ResolveVulkanPresentMode(VSyncMode::Adaptive, PresentModeAvailability{}),
              (PresentModeResolution{PresentModePolicy::Fifo,
                                        PresentModeFallback::AdaptiveUsesFifo}));

    EXPECT_EQ(ResolveVulkanPresentMode(VSyncMode::Mailbox, PresentModeAvailability{false, false, true}),
              (PresentModeResolution{PresentModePolicy::Mailbox, PresentModeFallback::None}));
    EXPECT_EQ(ResolveVulkanPresentMode(VSyncMode::Mailbox, PresentModeAvailability{}),
              (PresentModeResolution{PresentModePolicy::Fifo,
                                        PresentModeFallback::MailboxUsesFifo}));
}

TEST(VulkanPresentModePolicyTest, InvalidRequestFallsBackToFifo) {
    EXPECT_EQ(ResolveVulkanPresentMode(static_cast<VSyncMode>(255U), PresentModeAvailability{}),
              (PresentModeResolution{PresentModePolicy::Fifo,
                                        PresentModeFallback::InvalidUsesFifo}));
}

} // namespace
} // namespace UVE::Render::Vulkan::Tests
