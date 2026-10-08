// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/vulkan/vulkan_present_mode_policy_uve.h"

#include <gtest/gtest.h>

namespace UVE::Render::Vulkan::Tests {
namespace {

using Platform::VSyncModeUVE;

TEST(VulkanPresentModePolicyUVETest, OffPrefersImmediateThenMailboxThenFifo) {
    const PresentModeAvailabilityUVE allModes{true, true, true};
    EXPECT_EQ(ResolveVulkanPresentModeUVE(VSyncModeUVE::Off, allModes),
              (PresentModeResolutionUVE{PresentModePolicyUVE::Immediate, PresentModeFallbackUVE::None}));

    EXPECT_EQ(ResolveVulkanPresentModeUVE(VSyncModeUVE::Off, PresentModeAvailabilityUVE{false, false, true}),
              (PresentModeResolutionUVE{PresentModePolicyUVE::Mailbox,
                                        PresentModeFallbackUVE::OffUsesMailbox}));
    EXPECT_EQ(ResolveVulkanPresentModeUVE(VSyncModeUVE::Off, PresentModeAvailabilityUVE{}),
              (PresentModeResolutionUVE{PresentModePolicyUVE::Fifo,
                                        PresentModeFallbackUVE::OffUsesFifo}));
}

TEST(VulkanPresentModePolicyUVETest, OnAlwaysUsesMandatoryFifo) {
    EXPECT_EQ(ResolveVulkanPresentModeUVE(VSyncModeUVE::On, PresentModeAvailabilityUVE{true, true, true}),
              (PresentModeResolutionUVE{PresentModePolicyUVE::Fifo, PresentModeFallbackUVE::None}));
}

TEST(VulkanPresentModePolicyUVETest, AdaptiveAndMailboxUseRequestedModeOrFallbackToFifo) {
    EXPECT_EQ(ResolveVulkanPresentModeUVE(VSyncModeUVE::Adaptive, PresentModeAvailabilityUVE{false, true, false}),
              (PresentModeResolutionUVE{PresentModePolicyUVE::FifoRelaxed, PresentModeFallbackUVE::None}));
    EXPECT_EQ(ResolveVulkanPresentModeUVE(VSyncModeUVE::Adaptive, PresentModeAvailabilityUVE{}),
              (PresentModeResolutionUVE{PresentModePolicyUVE::Fifo,
                                        PresentModeFallbackUVE::AdaptiveUsesFifo}));

    EXPECT_EQ(ResolveVulkanPresentModeUVE(VSyncModeUVE::Mailbox, PresentModeAvailabilityUVE{false, false, true}),
              (PresentModeResolutionUVE{PresentModePolicyUVE::Mailbox, PresentModeFallbackUVE::None}));
    EXPECT_EQ(ResolveVulkanPresentModeUVE(VSyncModeUVE::Mailbox, PresentModeAvailabilityUVE{}),
              (PresentModeResolutionUVE{PresentModePolicyUVE::Fifo,
                                        PresentModeFallbackUVE::MailboxUsesFifo}));
}

TEST(VulkanPresentModePolicyUVETest, InvalidRequestFallsBackToFifo) {
    EXPECT_EQ(ResolveVulkanPresentModeUVE(static_cast<VSyncModeUVE>(255U), PresentModeAvailabilityUVE{}),
              (PresentModeResolutionUVE{PresentModePolicyUVE::Fifo,
                                        PresentModeFallbackUVE::InvalidUsesFifo}));
}

} // namespace
} // namespace UVE::Render::Vulkan::Tests
