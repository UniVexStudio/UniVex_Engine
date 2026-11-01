// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_draw_list_uve.h"

#include <gtest/gtest.h>

namespace UVE::UI::Prism::Tests {

TEST(PrismDrawListTest, InitialStateIsEmpty) {
    PrismDrawListUVE drawList;
    EXPECT_EQ(drawList.GetTotalVertexCount(), 0U);
    EXPECT_EQ(drawList.GetTotalIndexCount(), 0U);
    EXPECT_TRUE(drawList.GetCommands().empty());
}

TEST(PrismDrawListTest, FastPathQuadAppendsCorrectTopology) {
    PrismDrawListUVE drawList;
    const Math::RectUVE rect{{10.0F, 20.0F}, {100.0F, 50.0F}};
    const Math::ColorUVE color{1.0F, 0.0F, 0.0F, 1.0F};

    drawList.AddRectFilled(rect, color);

    // Quad must generate exactly 4 vertices and 6 indices (2 triangles)
    EXPECT_EQ(drawList.GetTotalVertexCount(), 4U);
    EXPECT_EQ(drawList.GetTotalIndexCount(), 6U);
    ASSERT_EQ(drawList.GetCommands().size(), 1U);

    const PrismDrawCmdUVE& cmd = drawList.GetCommands()[0];
    EXPECT_EQ(cmd.indexOffset, 0U);
    EXPECT_EQ(cmd.indexCount, 6U);
    EXPECT_EQ(cmd.textureId, 0U);
    EXPECT_EQ(cmd.pipeline, PrismPipelineModeUVE::SolidOrTextured);

    // Positions check
    const auto& v = drawList.GetVertices();
    EXPECT_FLOAT_EQ(v[0].position.x, 10.0F);
    EXPECT_FLOAT_EQ(v[0].position.y, 20.0F);
    EXPECT_FLOAT_EQ(v[1].position.x, 110.0F);
    EXPECT_FLOAT_EQ(v[1].position.y, 20.0F);
    EXPECT_FLOAT_EQ(v[2].position.x, 110.0F);
    EXPECT_FLOAT_EQ(v[2].position.y, 70.0F);
    EXPECT_FLOAT_EQ(v[3].position.x, 10.0F);
    EXPECT_FLOAT_EQ(v[3].position.y, 70.0F);

    // Color check
    EXPECT_EQ(v[0].color, color);
}

TEST(PrismDrawListTest, HierarchicalScissorClippingInvariant) {
    PrismDrawListUVE drawList;

    // Parent scissor: (10, 10) to size (100, 100) -> [10, 10, 110, 110]
    const Math::RectIntUVE parentClip{Math::Vector2iUVE{10, 10}, Math::Vector2iUVE{100, 100}};
    drawList.PushClipRect(parentClip);
    EXPECT_EQ(drawList.GetCurrentClipRect(), parentClip);

    // Draw first item under parent clip
    drawList.AddRectFilled(Math::RectUVE{{0.0F, 0.0F}, {50.0F, 50.0F}}, Math::ColorUVE{1.0F, 1.0F, 1.0F, 1.0F});
    ASSERT_EQ(drawList.GetCommands().size(), 1U);
    EXPECT_EQ(drawList.GetCommands()[0].clipRect, parentClip);

    // Child scissor that exceeds parent on right and bottom: (50, 50) to size (100, 100) -> [50, 50, 150, 150]
    const Math::RectIntUVE childClip{Math::Vector2iUVE{50, 50}, Math::Vector2iUVE{100, 100}};
    drawList.PushClipRect(childClip);

    // Invariant: The resulting clip MUST BE the intersection [50, 50, 110, 110] -> size (60, 60)
    const Math::RectIntUVE expectedIntersection{Math::Vector2iUVE{50, 50}, Math::Vector2iUVE{60, 60}};
    EXPECT_EQ(drawList.GetCurrentClipRect(), expectedIntersection);

    // Draw second item under child clip - should split command batch due to clip boundary
    drawList.AddRectFilled(Math::RectUVE{{60.0F, 60.0F}, {20.0F, 20.0F}}, Math::ColorUVE{0.0F, 1.0F, 0.0F, 1.0F});
    ASSERT_EQ(drawList.GetCommands().size(), 2U);
    EXPECT_EQ(drawList.GetCommands()[1].clipRect, expectedIntersection);

    // Pop back to parent clip
    drawList.PopClipRect();
    EXPECT_EQ(drawList.GetCurrentClipRect(), parentClip);

    // Pop parent clip
    drawList.PopClipRect();
    EXPECT_EQ(drawList.GetCurrentClipRect().position.x, 0);
}

TEST(PrismDrawListTest, TextureBatchingSplitsCommands) {
    PrismDrawListUVE drawList;

    // Draw solid color quad
    drawList.AddRectFilled(Math::RectUVE{{0.0F, 0.0F}, {10.0F, 10.0F}}, Math::ColorUVE{1.0F, 1.0F, 1.0F, 1.0F});
    EXPECT_EQ(drawList.GetCommands().size(), 1U);
    EXPECT_EQ(drawList.GetCommands()[0].textureId, 0U);

    // Draw textured quad with texture ID 42
    drawList.AddTexturedQuad(Math::RectUVE{{10.0F, 10.0F}, {20.0F, 20.0F}},
                             Math::RectUVE{{0.0F, 0.0F}, {1.0F, 1.0F}},
                             Math::ColorUVE{1.0F, 1.0F, 1.0F, 1.0F},
                             42U);
    EXPECT_EQ(drawList.GetCommands().size(), 2U);
    EXPECT_EQ(drawList.GetCommands()[1].textureId, 42U);

    // Draw another textured quad with the SAME texture ID 42 - should BATCH into the same command!
    drawList.AddTexturedQuad(Math::RectUVE{{30.0F, 30.0F}, {20.0F, 20.0F}},
                             Math::RectUVE{{0.0F, 0.0F}, {1.0F, 1.0F}},
                             Math::ColorUVE{1.0F, 1.0F, 1.0F, 1.0F},
                             42U);
    EXPECT_EQ(drawList.GetCommands().size(), 2U);
    EXPECT_EQ(drawList.GetCommands()[1].indexCount, 12U); // 2 quads = 12 indices
}

TEST(PrismDrawListTest, RoundedRectangleTessellationProducesValidFan) {
    PrismDrawListUVE drawList;
    const Math::RectUVE rect{{0.0F, 0.0F}, {100.0F, 100.0F}};
    const PrismCornerRadiiUVE radii{10.0F};

    drawList.AddRectFilled(rect, Math::ColorUVE{1.0F, 1.0F, 1.0F, 1.0F}, radii);

    // Rounded rectangle with tessellation must generate vertices > 4 and valid triangles
    EXPECT_GT(drawList.GetTotalVertexCount(), 4U);
    EXPECT_GT(drawList.GetTotalIndexCount(), 6U);
    EXPECT_EQ(drawList.GetTotalIndexCount() % 3U, 0U);
}

TEST(PrismDrawListTest, ResetClearsAllBuffers) {
    PrismDrawListUVE drawList;
    drawList.AddRectFilled(Math::RectUVE{{0.0F, 0.0F}, {10.0F, 10.0F}}, Math::ColorUVE{1.0F, 1.0F, 1.0F, 1.0F});
    EXPECT_GT(drawList.GetTotalVertexCount(), 0U);

    drawList.Reset();
    EXPECT_EQ(drawList.GetTotalVertexCount(), 0U);
    EXPECT_EQ(drawList.GetTotalIndexCount(), 0U);
    EXPECT_TRUE(drawList.GetCommands().empty());
}

} // namespace UVE::UI::Prism::Tests
