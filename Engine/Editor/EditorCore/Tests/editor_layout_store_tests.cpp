// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include <gtest/gtest.h>

#include "editor_layout_store.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace UVE::Editor::Tests {

TEST(EditorLayoutStoreTest, RoundTripsSplittersPanelsAndWindowBoundsAndResetsCleanly) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("uve_layout_store_test_" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    const std::filesystem::path file = dir / "editor_layout.json";

    auto& store = EditorLayoutStore::Instance();
    store.SetStoragePathForTest(file);

    EXPECT_FALSE(store.HasSplitter("level.columns"));
    EXPECT_TRUE(store.GetBool("level.dockVisible", true));
    EXPECT_EQ(store.GetInt("level.dockTab", 0), 0);
    EXPECT_FALSE(store.GetWindowBounds("Entity Editor").has_value());

    store.SetSplitterSizes("level.columns", {280.0F, 720.0F, 360.0F});
    store.SetBool("level.dockVisible", false);
    store.SetInt("level.dockTab", 2);
    SavedWindowBounds b;
    b.width = 1100;
    b.height = 700;
    b.x = 120;
    b.y = 80;
    b.hasPosition = true;
    b.pageFloating = true;
    b.pageX = 40.0F;
    b.pageY = 30.0F;
    b.pageW = 620.0F;
    b.pageH = 420.0F;
    b.hasPageBounds = true;
    store.SetWindowBounds("Entity Editor", b);

    // Reload from disk to confirm atomic persistence.
    store.SetStoragePathForTest(file);
    ASSERT_TRUE(store.HasSplitter("level.columns"));
    const std::vector<float> sizes = store.GetSplitterSizes("level.columns");
    ASSERT_EQ(sizes.size(), 3U);
    EXPECT_FLOAT_EQ(sizes[0], 280.0F);
    EXPECT_FLOAT_EQ(sizes[2], 360.0F);
    EXPECT_FALSE(store.GetBool("level.dockVisible", true));
    EXPECT_EQ(store.GetInt("level.dockTab", 0), 2);
    const auto loadedBounds = store.GetWindowBounds("Entity Editor");
    ASSERT_TRUE(loadedBounds.has_value());
    EXPECT_EQ(loadedBounds->width, 1100);
    EXPECT_TRUE(loadedBounds->pageFloating);
    EXPECT_FLOAT_EQ(loadedBounds->pageW, 620.0F);

    store.ResetAll();
    store.SetStoragePathForTest(file);
    EXPECT_FALSE(store.HasSplitter("level.columns"));
    EXPECT_TRUE(store.GetBool("level.dockVisible", true));
    EXPECT_FALSE(store.GetWindowBounds("Entity Editor").has_value());

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

} // namespace UVE::Editor::Tests
