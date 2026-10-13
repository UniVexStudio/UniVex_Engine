// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/i_project_file_index_uve.h"
#include "uve/editor/editor_uve.h"

namespace UVE::Editor::Tests {
namespace {

using Asset::ProjectFileEntryKindUVE;
using Asset::ProjectFileEntryUVE;

[[nodiscard]] std::vector<ProjectFileEntryUVE> PickerEntriesUVE() {
    return {
        {std::filesystem::path{"Anims"}, ProjectFileEntryKindUVE::Directory, std::nullopt},
        {std::filesystem::path{"Props"}, ProjectFileEntryKindUVE::Directory, std::nullopt},
        {std::filesystem::path{"walk.uvanimlib"}, ProjectFileEntryKindUVE::File, std::nullopt},
        {std::filesystem::path{"Run.uvanimlib"}, ProjectFileEntryKindUVE::File, std::nullopt},
        {std::filesystem::path{"notes.txt"}, ProjectFileEntryKindUVE::File, std::nullopt},
        {std::filesystem::path{"Anims/idle.uvanimlib"}, ProjectFileEntryKindUVE::File, std::nullopt},
        {std::filesystem::path{"Anims/nested"}, ProjectFileEntryKindUVE::Directory, std::nullopt},
    };
}

TEST(FilePickerUVETest, ListingSplitsFoldersAndMatchingFilesSorted) {
    const FilePickerListingUVE listing =
        EditorUVE::BuildFilePickerListingUVE(PickerEntriesUVE(), {}, {".uvanimlib"}, "");
    EXPECT_EQ(listing.folders, (std::vector<std::filesystem::path>{"Anims", "Props"}));
    EXPECT_EQ(listing.files, (std::vector<std::filesystem::path>{"Run.uvanimlib", "walk.uvanimlib"}));
}

TEST(FilePickerUVETest, ListingDescendsAndSearchIsCaseInsensitive) {
    const std::vector<ProjectFileEntryUVE> entries = PickerEntriesUVE();
    const FilePickerListingUVE sub =
        EditorUVE::BuildFilePickerListingUVE(entries, "Anims", {".uvanimlib"}, "");
    EXPECT_EQ(sub.folders, (std::vector<std::filesystem::path>{"Anims/nested"}));
    EXPECT_EQ(sub.files, (std::vector<std::filesystem::path>{"Anims/idle.uvanimlib"}));
    const FilePickerListingUVE found = EditorUVE::BuildFilePickerListingUVE(entries, {}, {".uvanimlib"}, "run");
    EXPECT_EQ(found.files, (std::vector<std::filesystem::path>{"Run.uvanimlib"}));
    // Folders never hide behind the search: navigation stays put.
    EXPECT_EQ(found.folders.size(), 2U);
    const FilePickerListingUVE none = EditorUVE::BuildFilePickerListingUVE(entries, {}, {".uvscene"}, "");
    EXPECT_TRUE(none.files.empty());
}

TEST(FilePickerUVETest, ListingMatchesAnyOfSeveralKindsOrEveryFile) {
    const std::vector<ProjectFileEntryUVE> entries = PickerEntriesUVE();
    const FilePickerListingUVE multi =
        EditorUVE::BuildFilePickerListingUVE(entries, {}, {".uvanimlib", ".txt"}, "");
    EXPECT_EQ(multi.files, (std::vector<std::filesystem::path>{"Run.uvanimlib", "notes.txt", "walk.uvanimlib"}));
    const FilePickerListingUVE all = EditorUVE::BuildFilePickerListingUVE(entries, {}, {}, "");
    EXPECT_EQ(all.files.size(), 3U);
}

} // namespace
} // namespace UVE::Editor::Tests
