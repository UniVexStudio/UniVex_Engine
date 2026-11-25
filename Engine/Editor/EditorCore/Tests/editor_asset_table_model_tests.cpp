// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "editor_asset_table_model.h"

namespace UVE::Editor {
namespace {

[[nodiscard]] Asset::ProjectFileEntry Folder(const std::string& path) {
    return Asset::ProjectFileEntry{path, Asset::ProjectFileEntryKind::Directory, std::nullopt};
}

[[nodiscard]] Asset::ProjectFileEntry File(const std::string& path) {
    return Asset::ProjectFileEntry{path, Asset::ProjectFileEntryKind::File, std::nullopt};
}

/// A small project: Content/{Sea/{Models/{Characters,Environment}, Audio, rock.uvmodel, Wave.uvtex}, Readme.txt}
[[nodiscard]] std::vector<Asset::ProjectFileEntry> Project() {
    return {File("Readme.txt"),
            Folder("Sea"),
            File("Sea/rock.uvmodel"),
            File("Sea/Wave.uvtex"),
            Folder("Sea/Models"),
            Folder("Sea/Models/Characters"),
            File("Sea/Models/Characters/Diver.uventity"),
            Folder("Sea/Models/Environment"),
            Folder("Sea/Audio"),
            File("Sea/Audio/waves.wav")};
}

[[nodiscard]] std::vector<std::string> Names(const std::vector<Asset::ProjectFileEntry>& entries,
                                                const std::vector<std::size_t>& indices) {
    std::vector<std::string> names;
    for (const std::size_t index : indices) {
        names.push_back(entries[index].relativePath.generic_string());
    }
    return names;
}

TEST(AssetTableModelTest, AFolderListsItsChildrenFoldersFirstThenByNameIgnoringCase) {
    const auto entries = Project();
    EXPECT_EQ(Names(entries, ListContentFolder(entries, "Sea", "")),
              (std::vector<std::string>{"Sea/Audio", "Sea/Models", "Sea/rock.uvmodel", "Sea/Wave.uvtex"}));
    EXPECT_EQ(Names(entries, ListContentFolder(entries, "", "")), (std::vector<std::string>{"Sea", "Readme.txt"}));
    EXPECT_TRUE(ListContentFolder(entries, "Sea/Models/Environment", "").empty());
}

TEST(AssetTableModelTest, ASearchLooksThroughEveryFolderBelowByNameOnly) {
    const auto entries = Project();
    // "wav" finds the texture and the sound, deep or not, but not the folder "Sea" that holds them.
    EXPECT_EQ(Names(entries, ListContentFolder(entries, "", "wav")),
              (std::vector<std::string>{"Sea/Wave.uvtex", "Sea/Audio/waves.wav"})); // "wave." before "waves"
    // Every word has to match; order does not matter.
    EXPECT_EQ(Names(entries, ListContentFolder(entries, "", "uvtex WA")), (std::vector<std::string>{"Sea/Wave.uvtex"}));
    // Only below the folder on screen.
    EXPECT_TRUE(ListContentFolder(entries, "Sea/Models", "wav").empty());
    EXPECT_EQ(Names(entries, ListContentFolder(entries, "Sea/Models", "diver")),
              (std::vector<std::string>{"Sea/Models/Characters/Diver.uventity"}));
    // Spaces alone are no search.
    EXPECT_EQ(ListContentFolder(entries, "Sea", "   "), ListContentFolder(entries, "Sea", ""));
}

TEST(AssetTableModelTest, InsideMeansStrictlyBelowAndTheRootHoldsEverything) {
    EXPECT_TRUE(IsInsideContentDirectory("Sea/Audio", "Sea"));
    EXPECT_TRUE(IsInsideContentDirectory("Sea/Audio/waves.wav", "Sea"));
    EXPECT_FALSE(IsInsideContentDirectory("Sea", "Sea"));
    EXPECT_FALSE(IsInsideContentDirectory("Seashell/a", "Sea")); // a name that only starts the same
    EXPECT_TRUE(IsInsideContentDirectory("Readme.txt", ""));
    EXPECT_FALSE(IsInsideContentDirectory("", ""));
}

TEST(AssetTableModelTest, TheFolderTreeSearchKeepsTheWayToEachMatch) {
    const auto entries = Project();
    EXPECT_EQ(CollectVisibleContentFolders(entries, "char"),
              (std::vector<std::string>{"Sea", "Sea/Models", "Sea/Models/Characters"}));
    EXPECT_EQ(CollectVisibleContentFolders(entries, "").size(), 5U);
    EXPECT_TRUE(CollectVisibleContentFolders(entries, "rock").empty()); // files are not folders
}

TEST(AssetTableModelTest, BackAndForwardWalkThePlacesVisited) {
    ContentNavigationHistory history;
    EXPECT_FALSE(history.CanGoBack());
    history.Go({"Sea", {}});
    history.Go({"Sea", {}}); // the same place again is not a new step
    history.Go({"Sea/Models", {}});
    history.Go({{}, "Props"});
    ASSERT_TRUE(history.Back());
    EXPECT_EQ(history.Current().directory, "Sea/Models");
    ASSERT_TRUE(history.Back());
    EXPECT_EQ(history.Current().directory, "Sea");
    ASSERT_TRUE(history.Forward());
    EXPECT_EQ(history.Current().directory, "Sea/Models");
    // Going somewhere new drops what was ahead.
    history.Go({"Sea/Audio", {}});
    EXPECT_FALSE(history.CanGoForward());
    ASSERT_TRUE(history.Back());
    ASSERT_TRUE(history.Back());
    ASSERT_TRUE(history.Back());
    EXPECT_EQ(history.Current(), ContentLocation{});
    EXPECT_FALSE(history.Back());
}

TEST(AssetTableModelTest, HistoryIsBounded) {
    ContentNavigationHistory history;
    for (int step = 0; step < 200; ++step) {
        history.Go({"Folder" + std::to_string(step), {}});
    }
    int backs = 0;
    while (history.Back()) {
        ++backs;
    }
    EXPECT_EQ(backs, static_cast<int>(ContentNavigationHistory::kMaxEntries));
    EXPECT_EQ(history.Current().directory, "Folder135");
}

TEST(AssetTableModelTest, ShelvesHaveUniqueNamesAndHoldEachItemOnce) {
    ContentShelves shelves;
    EXPECT_EQ(shelves.Create("Props"), "Props");
    EXPECT_EQ(shelves.Create("Props"), "Props 2");
    EXPECT_EQ(shelves.Create(""), "Shelf");
    EXPECT_TRUE(shelves.AddItem("Props", "Sea/rock.uvmodel"));
    EXPECT_FALSE(shelves.AddItem("Props", "Sea/rock.uvmodel"));
    EXPECT_FALSE(shelves.AddItem("Missing", "Sea/rock.uvmodel"));
    EXPECT_TRUE(shelves.Contains("Props", "Sea/rock.uvmodel"));
    EXPECT_FALSE(shelves.Rename("Props 2", "Props"));
    EXPECT_FALSE(shelves.Rename("Props 2", ""));
    EXPECT_TRUE(shelves.Rename("Props 2", "Characters"));
    EXPECT_NE(shelves.Find("Characters"), nullptr);
    EXPECT_TRUE(shelves.RemoveItem("Props", "Sea/rock.uvmodel"));
    EXPECT_FALSE(shelves.Contains("Props", "Sea/rock.uvmodel"));
    EXPECT_TRUE(shelves.Remove("Shelf"));
    EXPECT_FALSE(shelves.Remove("Shelf"));
    EXPECT_EQ(shelves.GetAll().size(), 2U);
}

TEST(AssetTableModelTest, AShelfFollowsARenamedFolderAndListsOnlyWhatExists) {
    ContentShelves shelves;
    const std::string name = shelves.Create("Divers");
    ASSERT_TRUE(shelves.AddItem(name, "Sea/Models/Characters/Diver.uventity"));
    ASSERT_TRUE(shelves.AddItem(name, "Sea/Models"));
    ASSERT_TRUE(shelves.AddItem(name, "Gone/old.uvmodel"));
    ASSERT_TRUE(shelves.AddItem(name, "Sea/rock.uvmodel"));

    const auto entries = Project();
    // Shelf order, missing files left out, and the search applies.
    EXPECT_EQ(Names(entries, ListContentShelf(entries, *shelves.Find(name), "")),
              (std::vector<std::string>{"Sea/Models/Characters/Diver.uventity", "Sea/Models", "Sea/rock.uvmodel"}));
    EXPECT_EQ(Names(entries, ListContentShelf(entries, *shelves.Find(name), "rock")),
              (std::vector<std::string>{"Sea/rock.uvmodel"}));

    shelves.MovePath("Sea/Models", "Sea/Meshes");
    const std::vector<std::filesystem::path> expected{"Sea/Meshes/Characters/Diver.uventity", "Sea/Meshes",
                                                      "Gone/old.uvmodel", "Sea/rock.uvmodel"};
    EXPECT_EQ(shelves.Find(name)->items, expected);
}

TEST(AssetTableModelTest, ShelvesAreBounded) {
    ContentShelves shelves;
    for (std::size_t i = 0U; i < ContentShelves::kMaxShelves; ++i) {
        ASSERT_FALSE(shelves.Create("S").empty());
    }
    EXPECT_TRUE(shelves.Create("S").empty());
    for (std::size_t i = 0U; i < ContentShelves::kMaxItemsPerShelf; ++i) {
        ASSERT_TRUE(shelves.AddItem("S", "f" + std::to_string(i)));
    }
    EXPECT_FALSE(shelves.AddItem("S", "one-too-many"));
}

TEST(AssetTableModelTest, TeamAndPersonalShelvesAreKeptApart) {
    ContentShelves shelves;
    ASSERT_EQ(shelves.Create("Props", true), "Props");
    ASSERT_EQ(shelves.Create("Mine"), "Mine");
    EXPECT_TRUE(shelves.Find("Props")->shared);
    EXPECT_FALSE(shelves.Find("Mine")->shared);
    EXPECT_TRUE(shelves.SetShared("Mine", true));
    EXPECT_FALSE(shelves.SetShared("Missing", true));
    ASSERT_TRUE(shelves.SetShared("Mine", false));
    shelves.RemoveAll(true);
    ASSERT_EQ(shelves.GetAll().size(), 1U);
    EXPECT_EQ(shelves.GetAll()[0].name, "Mine");
}

TEST(AssetTableModelTest, TheTeamFileHoldsOnlyTeamShelvesAndReadsBack) {
    ContentShelves written;
    ASSERT_EQ(written.Create("Props", true), "Props");
    ASSERT_TRUE(written.AddItem("Props", "Sea/rock.uvmodel"));
    ASSERT_TRUE(written.AddItem("Props", "Sea/Models"));
    ASSERT_EQ(written.Create("Mine"), "Mine");
    const std::string text = WriteSharedShelvesText(written);
    EXPECT_EQ(text.find("Mine"), std::string::npos);

    ContentShelves read;
    ASSERT_EQ(read.Create("Scratch"), "Scratch"); // a personal shelf already there stays
    std::string error;
    ASSERT_TRUE(ReadSharedShelvesText(text, read, error)) << error;
    ASSERT_NE(read.Find("Props"), nullptr);
    EXPECT_TRUE(read.Find("Props")->shared);
    EXPECT_EQ(read.Find("Props")->items, (std::vector<std::filesystem::path>{"Sea/rock.uvmodel", "Sea/Models"}));
    ASSERT_NE(read.Find("Scratch"), nullptr);
    EXPECT_FALSE(read.Find("Scratch")->shared);
    EXPECT_EQ(read.GetAll().size(), 2U);
}

TEST(AssetTableModelTest, ATeamShelfWinsANameAndThePersonalOneIsRenamed) {
    ContentShelves shelves;
    ASSERT_EQ(shelves.Create("Props"), "Props");
    ASSERT_TRUE(shelves.AddItem("Props", "mine.uvmodel"));
    std::string error;
    ASSERT_TRUE(ReadSharedShelvesText(R"({"version":1,"shelves":[{"name":"Props","items":["team.uvmodel"]}]})",
                                         shelves, error));
    ASSERT_NE(shelves.Find("Props"), nullptr);
    EXPECT_TRUE(shelves.Find("Props")->shared);
    EXPECT_TRUE(shelves.Contains("Props", "team.uvmodel"));
    ASSERT_NE(shelves.Find("Props 2"), nullptr);
    EXPECT_FALSE(shelves.Find("Props 2")->shared);
    EXPECT_TRUE(shelves.Contains("Props 2", "mine.uvmodel"));
}

TEST(AssetTableModelTest, TheTeamFileIsReadWarily) {
    ContentShelves shelves;
    ASSERT_EQ(shelves.Create("Mine"), "Mine");
    std::string error;
    EXPECT_FALSE(ReadSharedShelvesText("not json", shelves, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(ReadSharedShelvesText(R"({"version":2,"shelves":[]})", shelves, error));
    EXPECT_FALSE(ReadSharedShelvesText(R"({"version":1})", shelves, error));
    ASSERT_EQ(shelves.GetAll().size(), 1U) << "a refused file leaves the shelves as they were";

    // Paths that leave the content folder are skipped; the rest of the file still loads.
    ASSERT_TRUE(ReadSharedShelvesText(
        R"({"version":1,"shelves":[{"name":"S","items":["../../etc/passwd","/abs/x","a/../../b","ok/file.uvtex",7,""]},)"
        R"({"name":"S","items":["dupe"]},{"items":["nameless"]}]})",
        shelves, error))
        << error;
    ASSERT_NE(shelves.Find("S"), nullptr);
    EXPECT_EQ(shelves.Find("S")->items, (std::vector<std::filesystem::path>{"ok/file.uvtex"}));
    EXPECT_EQ(shelves.GetAll().size(), 2U); // S and Mine; the repeated S and the nameless one are dropped
}

TEST(AssetTableModelTest, TheTreeListingHoldsEverythingBelow) {
    const auto entries = Project();
    EXPECT_EQ(Names(entries, ListContentTree(entries, "Sea/Models", "")),
              (std::vector<std::string>{"Sea/Models/Characters", "Sea/Models/Environment",
                                        "Sea/Models/Characters/Diver.uventity"}));
    EXPECT_EQ(ListContentTree(entries, "", "").size(), entries.size());
    EXPECT_EQ(Names(entries, ListContentTree(entries, "Sea", "wav")),
              (std::vector<std::string>{"Sea/Wave.uvtex", "Sea/Audio/waves.wav"}));
}

TEST(AssetTableModelTest, DetailsSortKeepsFoldersFirstAndFallsBackToTheName) {
    const std::vector<ContentItemFacts> facts{
        {"b.png", "Texture", "Sea", 300U, 30, false}, {"a.png", "Texture", "Sea", 100U, 50, false},
        {"Zeta", "Folder", "Sea", 0U, 10, true},      {"c.wav", "Audio", "Sea/Audio", 300U, 20, false},
    };
    const auto sorted = [&](const ContentSortKey key, const bool ascending) {
        std::vector<std::size_t> order{0U, 1U, 2U, 3U};
        SortContentFacts(order, facts, key, ascending);
        std::vector<std::string> names;
        for (const std::size_t i : order) {
            names.push_back(facts[i].name);
        }
        return names;
    };
    EXPECT_EQ(sorted(ContentSortKey::Name, true), (std::vector<std::string>{"Zeta", "a.png", "b.png", "c.wav"}));
    EXPECT_EQ(sorted(ContentSortKey::Size, false), (std::vector<std::string>{"Zeta", "c.wav", "b.png", "a.png"}));
    EXPECT_EQ(sorted(ContentSortKey::Modified, false), (std::vector<std::string>{"Zeta", "a.png", "b.png", "c.wav"}));
    EXPECT_EQ(sorted(ContentSortKey::Type, true), (std::vector<std::string>{"Zeta", "c.wav", "a.png", "b.png"}));
    EXPECT_EQ(sorted(ContentSortKey::Folder, false), (std::vector<std::string>{"Zeta", "c.wav", "b.png", "a.png"}));
}

TEST(AssetTableModelTest, RecentBucketsCountFromMidnight) {
    constexpr std::int64_t kDay = 24 * 60 * 60;
    const std::int64_t midnight = 1'790'000'000;
    EXPECT_EQ(ClassifyContentAge(midnight + 5, midnight), ContentAge::Today);
    EXPECT_EQ(ClassifyContentAge(midnight + 3 * kDay, midnight), ContentAge::Today); // clock skew counts as now
    EXPECT_EQ(ClassifyContentAge(midnight - 1, midnight), ContentAge::Yesterday);
    EXPECT_EQ(ClassifyContentAge(midnight - kDay, midnight), ContentAge::Yesterday);
    EXPECT_EQ(ClassifyContentAge(midnight - kDay - 1, midnight), ContentAge::ThisWeek);
    EXPECT_EQ(ClassifyContentAge(midnight - 6 * kDay, midnight), ContentAge::ThisWeek);
    EXPECT_EQ(ClassifyContentAge(midnight - 6 * kDay - 1, midnight), ContentAge::Earlier);
    EXPECT_EQ(ClassifyContentAge(0, midnight), ContentAge::Earlier);
    EXPECT_STREQ(GetContentAgeLabel(ContentAge::Today), "Today");
}

TEST(AssetTableModelTest, BoardLanesGroupFilesByKind) {
    const std::vector<ContentItemFacts> facts{
        {"rock", "Mesh", "", 0U, 0, false}, {"Sea", "Folder", "", 0U, 0, true},  {"wave", "Texture", "", 0U, 0, false},
        {"reef", "Mesh", "", 0U, 0, false}, {"hit", "Audio", "", 0U, 0, false},
    };
    const auto lanes = GroupContentByType({0U, 1U, 2U, 3U, 4U}, facts);
    ASSERT_EQ(lanes.size(), 3U);
    EXPECT_EQ(lanes[0].first, "Audio");
    EXPECT_EQ(lanes[1].first, "Mesh");
    EXPECT_EQ(lanes[1].second, (std::vector<std::size_t>{0U, 3U}));
    EXPECT_EQ(lanes[2].first, "Texture");
}

const std::vector<std::string> kShown{"a.uvanim", "b.uvanim", "c.uvanim", "d.uvanim", "e.uvanim"};

TEST(ContentSelectionTest, APlainClickPicksOnlyThatItem) {
    ContentSelection selection;
    selection.Click(kShown, "b.uvanim", false, false);
    selection.Click(kShown, "d.uvanim", false, false);
    EXPECT_EQ(selection.Items(), (std::vector<std::string>{"d.uvanim"}));
}

TEST(ContentSelectionTest, ControlClickAddsAndRemovesOneItem) {
    ContentSelection selection;
    selection.Click(kShown, "a.uvanim", false, false);
    selection.Click(kShown, "c.uvanim", true, false);
    selection.Click(kShown, "e.uvanim", true, false);
    EXPECT_EQ(selection.Items(), (std::vector<std::string>{"a.uvanim", "c.uvanim", "e.uvanim"}));
    selection.Click(kShown, "c.uvanim", true, false);
    EXPECT_FALSE(selection.Contains("c.uvanim"));
    EXPECT_TRUE(selection.Contains("a.uvanim"));
}

TEST(ContentSelectionTest, ShiftClickPicksTheRunFromTheAnchorInEitherDirection) {
    ContentSelection selection;
    selection.Click(kShown, "b.uvanim", false, false);
    selection.Click(kShown, "d.uvanim", false, true);
    EXPECT_EQ(selection.Items(), (std::vector<std::string>{"b.uvanim", "c.uvanim", "d.uvanim"}));
    // The anchor stays at b: shift-clicking above it swaps the run rather than adding to it.
    selection.Click(kShown, "a.uvanim", false, true);
    EXPECT_EQ(selection.Items(), (std::vector<std::string>{"a.uvanim", "b.uvanim"}));
    // Shrinking works from the same end.
    selection.Click(kShown, "e.uvanim", false, true);
    EXPECT_EQ(selection.Items().size(), 4U);
    EXPECT_FALSE(selection.Contains("a.uvanim"));
}

TEST(ContentSelectionTest, ControlShiftAddsTheRunToWhatIsPicked) {
    ContentSelection selection;
    selection.Click(kShown, "a.uvanim", false, false);
    selection.Click(kShown, "c.uvanim", true, false);
    selection.Click(kShown, "e.uvanim", true, true);
    EXPECT_EQ(selection.Items(), (std::vector<std::string>{"a.uvanim", "c.uvanim", "d.uvanim", "e.uvanim"}));
}

TEST(ContentSelectionTest, ShiftWithoutAnAnchorOrForAnItemNotShownActsAsAPlainClick) {
    ContentSelection selection;
    selection.Click(kShown, "c.uvanim", false, true);
    EXPECT_EQ(selection.Items(), (std::vector<std::string>{"c.uvanim"}));
    selection.Click(kShown, "zzz.uvanim", false, true);
    EXPECT_EQ(selection.Items(), (std::vector<std::string>{"zzz.uvanim"}));
}

TEST(ContentSelectionTest, KeepOnlyDropsItemsThatAreGoneAndClearForgetsTheAnchor) {
    ContentSelection selection;
    selection.Click(kShown, "a.uvanim", false, false);
    selection.Click(kShown, "d.uvanim", false, true);
    ASSERT_EQ(selection.Items().size(), 4U);
    selection.KeepOnly(std::vector<std::string>{"a.uvanim", "d.uvanim"});
    EXPECT_EQ(selection.Items(), (std::vector<std::string>{"a.uvanim", "d.uvanim"}));
    selection.Clear();
    EXPECT_TRUE(selection.IsEmpty());
    selection.Click(kShown, "c.uvanim", false, true);
    EXPECT_EQ(selection.Items().size(), 1U) << "no anchor after clearing: a plain click";
}

} // namespace
} // namespace UVE::Editor
