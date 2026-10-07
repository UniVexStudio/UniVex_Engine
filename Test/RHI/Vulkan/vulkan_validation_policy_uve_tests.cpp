// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/vulkan/vulkan_validation_policy_uve.h"

#include <gtest/gtest.h>

namespace UVE::Render::Vulkan::Tests {

TEST(VulkanValidationPolicyUVETest, DoesNotRequestOptionalDebugToolingWhenDisabled) {
    EXPECT_EQ(ResolveValidationLayerPlanUVE(false, true, true), (ValidationLayerPlanUVE{}));
}

TEST(VulkanValidationPolicyUVETest, EnablesLayerAndDebugMessengerWhenBothAreAvailable) {
    EXPECT_EQ(ResolveValidationLayerPlanUVE(true, true, true),
              (ValidationLayerPlanUVE{true, true, false, false}));
}

TEST(VulkanValidationPolicyUVETest, MissingLayerDoesNotBlockVulkanBackendCreation) {
    EXPECT_EQ(ResolveValidationLayerPlanUVE(true, false, true),
              (ValidationLayerPlanUVE{false, false, true, false}));
}

TEST(VulkanValidationPolicyUVETest, KeepsValidationLayerWhenDebugUtilsExtensionIsUnavailable) {
    EXPECT_EQ(ResolveValidationLayerPlanUVE(true, true, false),
              (ValidationLayerPlanUVE{true, false, false, true}));
}

} // namespace UVE::Render::Vulkan::Tests
