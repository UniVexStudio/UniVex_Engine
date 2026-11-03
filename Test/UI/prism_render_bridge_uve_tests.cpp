// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_render_bridge_uve.h"
#include "uve/ui/prism/prism_metrics_overlay_uve.h"

#include <gtest/gtest.h>

#include "uve/rhi_null/null_render_device_uve.h"

namespace UVE::UI::Prism::Tests {

TEST(PrismRenderBridgeTest, EmptyDrawListProducesNoCommands) {
    Render::NullRenderDeviceUVE renderDevice;
    PrismRenderBridgeUVE bridge(renderDevice);

    PrismDrawListUVE drawList;
    auto cmdBuffer = renderDevice.CreateCommandBufferUVE();
    ASSERT_NE(cmdBuffer, nullptr);

    bridge.RecordDrawCommandsUVE(drawList, *cmdBuffer, 800U, 600U);
    EXPECT_EQ(bridge.GetLastDiagnostics().drawCallsRecorded, 0U);
}

TEST(PrismRenderBridgeTest, DrawListUploadsAndRecordsDrawCalls) {
    Render::NullRenderDeviceUVE renderDevice;
    PrismRenderBridgeUVE bridge(renderDevice);

    PrismDrawListUVE drawList;
    drawList.AddRectFilled(Math::RectUVE{{10.0F, 10.0F}, {100.0F, 50.0F}}, PrismColorUVE{1.0F, 0.0F, 0.0F, 1.0F});
    drawList.AddRectFilled(Math::RectUVE{{120.0F, 10.0F}, {50.0F, 50.0F}}, PrismColorUVE{0.0F, 1.0F, 0.0F, 1.0F});

    auto cmdBuffer = renderDevice.CreateCommandBufferUVE();
    ASSERT_NE(cmdBuffer, nullptr);

    // The bridge records into a render pass owned by the caller (draws are only legal inside one).
    cmdBuffer->BeginRenderPassUVE({});
    bridge.RecordDrawCommandsUVE(drawList, *cmdBuffer, 1280U, 720U);
    cmdBuffer->EndRenderPassUVE();
    const PrismRenderDiagnosticsUVE diag = bridge.GetLastDiagnostics();

    // 2 rects with same pipeline/clip/texture batch into 1 draw call of 12 indices
    EXPECT_EQ(diag.drawCallsRecorded, 1U);
    EXPECT_EQ(diag.vertexCount, 8U);
    EXPECT_EQ(diag.indexCount, 12U);
}

TEST(PrismMetricsOverlayTest, ToggleMinimizedAltersHeight) {
    PrismMetricsOverlayUVE overlay;
    EXPECT_FALSE(overlay.IsMinimized());
    EXPECT_FLOAT_EQ(overlay.GetStyle().height.value, 110.0F);

    overlay.ToggleMinimized();
    EXPECT_TRUE(overlay.IsMinimized());
    EXPECT_FLOAT_EQ(overlay.GetStyle().height.value, 36.0F);

    overlay.ToggleMinimized();
    EXPECT_FALSE(overlay.IsMinimized());
    EXPECT_FLOAT_EQ(overlay.GetStyle().height.value, 110.0F);
}

} // namespace UVE::UI::Prism::Tests
