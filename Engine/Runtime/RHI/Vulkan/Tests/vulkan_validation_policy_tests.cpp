// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "vulkan_validation_policy.h"

#include <gtest/gtest.h>

namespace UVE::Render::Vulkan::Tests {

TEST(VulkanValidationPolicyTest, DoesNotRequestOptionalDebugToolingWhenDisabled) {
    EXPECT_EQ(ResolveValidationLayerPlan(false, true, true), (ValidationLayerPlan{}));
}

TEST(VulkanValidationPolicyTest, EnablesLayerAndDebugMessengerWhenBothAreAvailable) {
    EXPECT_EQ(ResolveValidationLayerPlan(true, true, true),
              (ValidationLayerPlan{true, true, false, false}));
}

TEST(VulkanValidationPolicyTest, MissingLayerDoesNotBlockVulkanBackendCreation) {
    EXPECT_EQ(ResolveValidationLayerPlan(true, false, true),
              (ValidationLayerPlan{false, false, true, false}));
}

TEST(VulkanValidationPolicyTest, KeepsValidationLayerWhenDebugUtilsExtensionIsUnavailable) {
    EXPECT_EQ(ResolveValidationLayerPlan(true, true, false),
              (ValidationLayerPlan{true, false, false, true}));
}

} // namespace UVE::Render::Vulkan::Tests
