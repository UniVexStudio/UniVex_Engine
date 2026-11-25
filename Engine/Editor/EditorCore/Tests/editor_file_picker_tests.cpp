// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "i_project_file_index.h"
#include "editor.h"

namespace UVE::Editor::Tests {
namespace {

using Asset::ProjectFileEntryKind;
using Asset::ProjectFileEntry;

[[nodiscard]] std::vector<ProjectFileEntry> PickerEntries() {
    return {
        {std::filesystem::path{"Anims"}, ProjectFileEntryKind::Directory, std::nullopt},
        {std::filesystem::path{"Props"}, ProjectFileEntryKind::Directory, std::nullopt},
        {std::filesystem::path{"walk.uvanimlib"}, ProjectFileEntryKind::File, std::nullopt},
        {std::filesystem::path{"Run.uvanimlib"}, ProjectFileEntryKind::File, std::nullopt},
        {std::filesystem::path{"notes.txt"}, ProjectFileEntryKind::File, std::nullopt},
        {std::filesystem::path{"Anims/idle.uvanimlib"}, ProjectFileEntryKind::File, std::nullopt},
        {std::filesystem::path{"Anims/nested"}, ProjectFileEntryKind::Directory, std::nullopt},
    };
}

TEST(FilePickerTest, ListingSplitsFoldersAndMatchingFilesSorted) {
    const FilePickerListing listing =
        Editor::BuildFilePickerListing(PickerEntries(), {}, {".uvanimlib"}, "");
    EXPECT_EQ(listing.folders, (std::vector<std::filesystem::path>{"Anims", "Props"}));
    EXPECT_EQ(listing.files, (std::vector<std::filesystem::path>{"Run.uvanimlib", "walk.uvanimlib"}));
}

TEST(FilePickerTest, ListingDescendsAndSearchIsCaseInsensitive) {
    const std::vector<ProjectFileEntry> entries = PickerEntries();
    const FilePickerListing sub =
        Editor::BuildFilePickerListing(entries, "Anims", {".uvanimlib"}, "");
    EXPECT_EQ(sub.folders, (std::vector<std::filesystem::path>{"Anims/nested"}));
    EXPECT_EQ(sub.files, (std::vector<std::filesystem::path>{"Anims/idle.uvanimlib"}));
    const FilePickerListing found = Editor::BuildFilePickerListing(entries, {}, {".uvanimlib"}, "run");
    EXPECT_EQ(found.files, (std::vector<std::filesystem::path>{"Run.uvanimlib"}));
    // Folders never hide behind the search: navigation stays put.
    EXPECT_EQ(found.folders.size(), 2U);
    const FilePickerListing none = Editor::BuildFilePickerListing(entries, {}, {".uvscene"}, "");
    EXPECT_TRUE(none.files.empty());
}

TEST(FilePickerTest, ListingMatchesAnyOfSeveralKindsOrEveryFile) {
    const std::vector<ProjectFileEntry> entries = PickerEntries();
    const FilePickerListing multi =
        Editor::BuildFilePickerListing(entries, {}, {".uvanimlib", ".txt"}, "");
    EXPECT_EQ(multi.files, (std::vector<std::filesystem::path>{"Run.uvanimlib", "notes.txt", "walk.uvanimlib"}));
    const FilePickerListing all = Editor::BuildFilePickerListing(entries, {}, {}, "");
    EXPECT_EQ(all.files.size(), 3U);
}

} // namespace
} // namespace UVE::Editor::Tests
