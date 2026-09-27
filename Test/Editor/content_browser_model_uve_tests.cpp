// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "uve/editor/editor_content_browser_model_uve.h"

namespace UVE::Editor {
namespace {

[[nodiscard]] Asset::ProjectFileEntryUVE FolderUVE(const std::string& path) {
    return Asset::ProjectFileEntryUVE{path, Asset::ProjectFileEntryKindUVE::Directory, std::nullopt};
}

[[nodiscard]] Asset::ProjectFileEntryUVE FileUVE(const std::string& path) {
    return Asset::ProjectFileEntryUVE{path, Asset::ProjectFileEntryKindUVE::File, std::nullopt};
}

/// A small project: Content/{Sea/{Models/{Characters,Environment}, Audio, rock.uvmodel, Wave.uvtex}, Readme.txt}
[[nodiscard]] std::vector<Asset::ProjectFileEntryUVE> ProjectUVE() {
    return {FileUVE("Readme.txt"),
            FolderUVE("Sea"),
            FileUVE("Sea/rock.uvmodel"),
            FileUVE("Sea/Wave.uvtex"),
            FolderUVE("Sea/Models"),
            FolderUVE("Sea/Models/Characters"),
            FileUVE("Sea/Models/Characters/Diver.uventity"),
            FolderUVE("Sea/Models/Environment"),
            FolderUVE("Sea/Audio"),
            FileUVE("Sea/Audio/waves.wav")};
}

[[nodiscard]] std::vector<std::string> NamesUVE(const std::vector<Asset::ProjectFileEntryUVE>& entries,
                                                const std::vector<std::size_t>& indices) {
    std::vector<std::string> names;
    for (const std::size_t index : indices) {
        names.push_back(entries[index].relativePath.generic_string());
    }
    return names;
}

TEST(ContentBrowserModelUVETest, AFolderListsItsChildrenFoldersFirstThenByNameIgnoringCase) {
    const auto entries = ProjectUVE();
    EXPECT_EQ(NamesUVE(entries, ListContentFolderUVE(entries, "Sea", "")),
              (std::vector<std::string>{"Sea/Audio", "Sea/Models", "Sea/rock.uvmodel", "Sea/Wave.uvtex"}));
    EXPECT_EQ(NamesUVE(entries, ListContentFolderUVE(entries, "", "")), (std::vector<std::string>{"Sea", "Readme.txt"}));
    EXPECT_TRUE(ListContentFolderUVE(entries, "Sea/Models/Environment", "").empty());
}

TEST(ContentBrowserModelUVETest, ASearchLooksThroughEveryFolderBelowByNameOnly) {
    const auto entries = ProjectUVE();
    // "wav" finds the texture and the sound, deep or not, but not the folder "Sea" that holds them.
    EXPECT_EQ(NamesUVE(entries, ListContentFolderUVE(entries, "", "wav")),
              (std::vector<std::string>{"Sea/Wave.uvtex", "Sea/Audio/waves.wav"})); // "wave." before "waves"
    // Every word has to match; order does not matter.
    EXPECT_EQ(NamesUVE(entries, ListContentFolderUVE(entries, "", "uvtex WA")), (std::vector<std::string>{"Sea/Wave.uvtex"}));
    // Only below the folder on screen.
    EXPECT_TRUE(ListContentFolderUVE(entries, "Sea/Models", "wav").empty());
    EXPECT_EQ(NamesUVE(entries, ListContentFolderUVE(entries, "Sea/Models", "diver")),
              (std::vector<std::string>{"Sea/Models/Characters/Diver.uventity"}));
    // Spaces alone are no search.
    EXPECT_EQ(ListContentFolderUVE(entries, "Sea", "   "), ListContentFolderUVE(entries, "Sea", ""));
}

TEST(ContentBrowserModelUVETest, InsideMeansStrictlyBelowAndTheRootHoldsEverything) {
    EXPECT_TRUE(IsInsideContentDirectoryUVE("Sea/Audio", "Sea"));
    EXPECT_TRUE(IsInsideContentDirectoryUVE("Sea/Audio/waves.wav", "Sea"));
    EXPECT_FALSE(IsInsideContentDirectoryUVE("Sea", "Sea"));
    EXPECT_FALSE(IsInsideContentDirectoryUVE("Seashell/a", "Sea")); // a name that only starts the same
    EXPECT_TRUE(IsInsideContentDirectoryUVE("Readme.txt", ""));
    EXPECT_FALSE(IsInsideContentDirectoryUVE("", ""));
}

TEST(ContentBrowserModelUVETest, TheFolderTreeSearchKeepsTheWayToEachMatch) {
    const auto entries = ProjectUVE();
    EXPECT_EQ(CollectVisibleContentFoldersUVE(entries, "char"),
              (std::vector<std::string>{"Sea", "Sea/Models", "Sea/Models/Characters"}));
    EXPECT_EQ(CollectVisibleContentFoldersUVE(entries, "").size(), 5U);
    EXPECT_TRUE(CollectVisibleContentFoldersUVE(entries, "rock").empty()); // files are not folders
}

TEST(ContentBrowserModelUVETest, BackAndForwardWalkThePlacesVisited) {
    ContentNavigationHistoryUVE history;
    EXPECT_FALSE(history.CanGoBackUVE());
    history.GoUVE({"Sea", {}});
    history.GoUVE({"Sea", {}}); // the same place again is not a new step
    history.GoUVE({"Sea/Models", {}});
    history.GoUVE({{}, "Props"});
    ASSERT_TRUE(history.BackUVE());
    EXPECT_EQ(history.CurrentUVE().directory, "Sea/Models");
    ASSERT_TRUE(history.BackUVE());
    EXPECT_EQ(history.CurrentUVE().directory, "Sea");
    ASSERT_TRUE(history.ForwardUVE());
    EXPECT_EQ(history.CurrentUVE().directory, "Sea/Models");
    // Going somewhere new drops what was ahead.
    history.GoUVE({"Sea/Audio", {}});
    EXPECT_FALSE(history.CanGoForwardUVE());
    ASSERT_TRUE(history.BackUVE());
    ASSERT_TRUE(history.BackUVE());
    ASSERT_TRUE(history.BackUVE());
    EXPECT_EQ(history.CurrentUVE(), ContentLocationUVE{});
    EXPECT_FALSE(history.BackUVE());
}

TEST(ContentBrowserModelUVETest, HistoryIsBounded) {
    ContentNavigationHistoryUVE history;
    for (int step = 0; step < 200; ++step) {
        history.GoUVE({"Folder" + std::to_string(step), {}});
    }
    int backs = 0;
    while (history.BackUVE()) {
        ++backs;
    }
    EXPECT_EQ(backs, static_cast<int>(ContentNavigationHistoryUVE::kMaxEntriesUVE));
    EXPECT_EQ(history.CurrentUVE().directory, "Folder135");
}

TEST(ContentBrowserModelUVETest, ShelvesHaveUniqueNamesAndHoldEachItemOnce) {
    ContentShelvesUVE shelves;
    EXPECT_EQ(shelves.CreateUVE("Props"), "Props");
    EXPECT_EQ(shelves.CreateUVE("Props"), "Props 2");
    EXPECT_EQ(shelves.CreateUVE(""), "Shelf");
    EXPECT_TRUE(shelves.AddItemUVE("Props", "Sea/rock.uvmodel"));
    EXPECT_FALSE(shelves.AddItemUVE("Props", "Sea/rock.uvmodel"));
    EXPECT_FALSE(shelves.AddItemUVE("Missing", "Sea/rock.uvmodel"));
    EXPECT_TRUE(shelves.ContainsUVE("Props", "Sea/rock.uvmodel"));
    EXPECT_FALSE(shelves.RenameUVE("Props 2", "Props"));
    EXPECT_FALSE(shelves.RenameUVE("Props 2", ""));
    EXPECT_TRUE(shelves.RenameUVE("Props 2", "Characters"));
    EXPECT_NE(shelves.FindUVE("Characters"), nullptr);
    EXPECT_TRUE(shelves.RemoveItemUVE("Props", "Sea/rock.uvmodel"));
    EXPECT_FALSE(shelves.ContainsUVE("Props", "Sea/rock.uvmodel"));
    EXPECT_TRUE(shelves.RemoveUVE("Shelf"));
    EXPECT_FALSE(shelves.RemoveUVE("Shelf"));
    EXPECT_EQ(shelves.GetAllUVE().size(), 2U);
}

TEST(ContentBrowserModelUVETest, AShelfFollowsARenamedFolderAndListsOnlyWhatExists) {
    ContentShelvesUVE shelves;
    const std::string name = shelves.CreateUVE("Divers");
    ASSERT_TRUE(shelves.AddItemUVE(name, "Sea/Models/Characters/Diver.uventity"));
    ASSERT_TRUE(shelves.AddItemUVE(name, "Sea/Models"));
    ASSERT_TRUE(shelves.AddItemUVE(name, "Gone/old.uvmodel"));
    ASSERT_TRUE(shelves.AddItemUVE(name, "Sea/rock.uvmodel"));

    const auto entries = ProjectUVE();
    // Shelf order, missing files left out, and the search applies.
    EXPECT_EQ(NamesUVE(entries, ListContentShelfUVE(entries, *shelves.FindUVE(name), "")),
              (std::vector<std::string>{"Sea/Models/Characters/Diver.uventity", "Sea/Models", "Sea/rock.uvmodel"}));
    EXPECT_EQ(NamesUVE(entries, ListContentShelfUVE(entries, *shelves.FindUVE(name), "rock")),
              (std::vector<std::string>{"Sea/rock.uvmodel"}));

    shelves.MovePathUVE("Sea/Models", "Sea/Meshes");
    const std::vector<std::filesystem::path> expected{"Sea/Meshes/Characters/Diver.uventity", "Sea/Meshes",
                                                      "Gone/old.uvmodel", "Sea/rock.uvmodel"};
    EXPECT_EQ(shelves.FindUVE(name)->items, expected);
}

TEST(ContentBrowserModelUVETest, ShelvesAreBounded) {
    ContentShelvesUVE shelves;
    for (std::size_t i = 0U; i < ContentShelvesUVE::kMaxShelvesUVE; ++i) {
        ASSERT_FALSE(shelves.CreateUVE("S").empty());
    }
    EXPECT_TRUE(shelves.CreateUVE("S").empty());
    for (std::size_t i = 0U; i < ContentShelvesUVE::kMaxItemsPerShelfUVE; ++i) {
        ASSERT_TRUE(shelves.AddItemUVE("S", "f" + std::to_string(i)));
    }
    EXPECT_FALSE(shelves.AddItemUVE("S", "one-too-many"));
}

} // namespace
} // namespace UVE::Editor
