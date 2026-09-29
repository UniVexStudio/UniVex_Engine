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

TEST(ContentBrowserModelUVETest, TeamAndPersonalShelvesAreKeptApart) {
    ContentShelvesUVE shelves;
    ASSERT_EQ(shelves.CreateUVE("Props", true), "Props");
    ASSERT_EQ(shelves.CreateUVE("Mine"), "Mine");
    EXPECT_TRUE(shelves.FindUVE("Props")->shared);
    EXPECT_FALSE(shelves.FindUVE("Mine")->shared);
    EXPECT_TRUE(shelves.SetSharedUVE("Mine", true));
    EXPECT_FALSE(shelves.SetSharedUVE("Missing", true));
    ASSERT_TRUE(shelves.SetSharedUVE("Mine", false));
    shelves.RemoveAllUVE(true);
    ASSERT_EQ(shelves.GetAllUVE().size(), 1U);
    EXPECT_EQ(shelves.GetAllUVE()[0].name, "Mine");
}

TEST(ContentBrowserModelUVETest, TheTeamFileHoldsOnlyTeamShelvesAndReadsBack) {
    ContentShelvesUVE written;
    ASSERT_EQ(written.CreateUVE("Props", true), "Props");
    ASSERT_TRUE(written.AddItemUVE("Props", "Sea/rock.uvmodel"));
    ASSERT_TRUE(written.AddItemUVE("Props", "Sea/Models"));
    ASSERT_EQ(written.CreateUVE("Mine"), "Mine");
    const std::string text = WriteSharedShelvesTextUVE(written);
    EXPECT_EQ(text.find("Mine"), std::string::npos);

    ContentShelvesUVE read;
    ASSERT_EQ(read.CreateUVE("Scratch"), "Scratch"); // a personal shelf already there stays
    std::string error;
    ASSERT_TRUE(ReadSharedShelvesTextUVE(text, read, error)) << error;
    ASSERT_NE(read.FindUVE("Props"), nullptr);
    EXPECT_TRUE(read.FindUVE("Props")->shared);
    EXPECT_EQ(read.FindUVE("Props")->items, (std::vector<std::filesystem::path>{"Sea/rock.uvmodel", "Sea/Models"}));
    ASSERT_NE(read.FindUVE("Scratch"), nullptr);
    EXPECT_FALSE(read.FindUVE("Scratch")->shared);
    EXPECT_EQ(read.GetAllUVE().size(), 2U);
}

TEST(ContentBrowserModelUVETest, ATeamShelfWinsANameAndThePersonalOneIsRenamed) {
    ContentShelvesUVE shelves;
    ASSERT_EQ(shelves.CreateUVE("Props"), "Props");
    ASSERT_TRUE(shelves.AddItemUVE("Props", "mine.uvmodel"));
    std::string error;
    ASSERT_TRUE(ReadSharedShelvesTextUVE(R"({"version":1,"shelves":[{"name":"Props","items":["team.uvmodel"]}]})",
                                         shelves, error));
    ASSERT_NE(shelves.FindUVE("Props"), nullptr);
    EXPECT_TRUE(shelves.FindUVE("Props")->shared);
    EXPECT_TRUE(shelves.ContainsUVE("Props", "team.uvmodel"));
    ASSERT_NE(shelves.FindUVE("Props 2"), nullptr);
    EXPECT_FALSE(shelves.FindUVE("Props 2")->shared);
    EXPECT_TRUE(shelves.ContainsUVE("Props 2", "mine.uvmodel"));
}

TEST(ContentBrowserModelUVETest, TheTeamFileIsReadWarily) {
    ContentShelvesUVE shelves;
    ASSERT_EQ(shelves.CreateUVE("Mine"), "Mine");
    std::string error;
    EXPECT_FALSE(ReadSharedShelvesTextUVE("not json", shelves, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(ReadSharedShelvesTextUVE(R"({"version":2,"shelves":[]})", shelves, error));
    EXPECT_FALSE(ReadSharedShelvesTextUVE(R"({"version":1})", shelves, error));
    ASSERT_EQ(shelves.GetAllUVE().size(), 1U) << "a refused file leaves the shelves as they were";

    // Paths that leave the content folder are skipped; the rest of the file still loads.
    ASSERT_TRUE(ReadSharedShelvesTextUVE(
        R"({"version":1,"shelves":[{"name":"S","items":["../../etc/passwd","/abs/x","a/../../b","ok/file.uvtex",7,""]},)"
        R"({"name":"S","items":["dupe"]},{"items":["nameless"]}]})",
        shelves, error))
        << error;
    ASSERT_NE(shelves.FindUVE("S"), nullptr);
    EXPECT_EQ(shelves.FindUVE("S")->items, (std::vector<std::filesystem::path>{"ok/file.uvtex"}));
    EXPECT_EQ(shelves.GetAllUVE().size(), 2U); // S and Mine; the repeated S and the nameless one are dropped
}

TEST(ContentBrowserModelUVETest, TheTreeListingHoldsEverythingBelow) {
    const auto entries = ProjectUVE();
    EXPECT_EQ(NamesUVE(entries, ListContentTreeUVE(entries, "Sea/Models", "")),
              (std::vector<std::string>{"Sea/Models/Characters", "Sea/Models/Environment",
                                        "Sea/Models/Characters/Diver.uventity"}));
    EXPECT_EQ(ListContentTreeUVE(entries, "", "").size(), entries.size());
    EXPECT_EQ(NamesUVE(entries, ListContentTreeUVE(entries, "Sea", "wav")),
              (std::vector<std::string>{"Sea/Wave.uvtex", "Sea/Audio/waves.wav"}));
}

TEST(ContentBrowserModelUVETest, DetailsSortKeepsFoldersFirstAndFallsBackToTheName) {
    const std::vector<ContentItemFactsUVE> facts{
        {"b.png", "Texture", "Sea", 300U, 30, false}, {"a.png", "Texture", "Sea", 100U, 50, false},
        {"Zeta", "Folder", "Sea", 0U, 10, true},      {"c.wav", "Audio", "Sea/Audio", 300U, 20, false},
    };
    const auto sorted = [&](const ContentSortKeyUVE key, const bool ascending) {
        std::vector<std::size_t> order{0U, 1U, 2U, 3U};
        SortContentFactsUVE(order, facts, key, ascending);
        std::vector<std::string> names;
        for (const std::size_t i : order) {
            names.push_back(facts[i].name);
        }
        return names;
    };
    EXPECT_EQ(sorted(ContentSortKeyUVE::Name, true), (std::vector<std::string>{"Zeta", "a.png", "b.png", "c.wav"}));
    EXPECT_EQ(sorted(ContentSortKeyUVE::Size, false), (std::vector<std::string>{"Zeta", "c.wav", "b.png", "a.png"}));
    EXPECT_EQ(sorted(ContentSortKeyUVE::Modified, false), (std::vector<std::string>{"Zeta", "a.png", "b.png", "c.wav"}));
    EXPECT_EQ(sorted(ContentSortKeyUVE::Type, true), (std::vector<std::string>{"Zeta", "c.wav", "a.png", "b.png"}));
    EXPECT_EQ(sorted(ContentSortKeyUVE::Folder, false), (std::vector<std::string>{"Zeta", "c.wav", "b.png", "a.png"}));
}

TEST(ContentBrowserModelUVETest, RecentBucketsCountFromMidnight) {
    constexpr std::int64_t kDay = 24 * 60 * 60;
    const std::int64_t midnight = 1'790'000'000;
    EXPECT_EQ(ClassifyContentAgeUVE(midnight + 5, midnight), ContentAgeUVE::Today);
    EXPECT_EQ(ClassifyContentAgeUVE(midnight + 3 * kDay, midnight), ContentAgeUVE::Today); // clock skew counts as now
    EXPECT_EQ(ClassifyContentAgeUVE(midnight - 1, midnight), ContentAgeUVE::Yesterday);
    EXPECT_EQ(ClassifyContentAgeUVE(midnight - kDay, midnight), ContentAgeUVE::Yesterday);
    EXPECT_EQ(ClassifyContentAgeUVE(midnight - kDay - 1, midnight), ContentAgeUVE::ThisWeek);
    EXPECT_EQ(ClassifyContentAgeUVE(midnight - 6 * kDay, midnight), ContentAgeUVE::ThisWeek);
    EXPECT_EQ(ClassifyContentAgeUVE(midnight - 6 * kDay - 1, midnight), ContentAgeUVE::Earlier);
    EXPECT_EQ(ClassifyContentAgeUVE(0, midnight), ContentAgeUVE::Earlier);
    EXPECT_STREQ(GetContentAgeLabelUVE(ContentAgeUVE::Today), "Today");
}

TEST(ContentBrowserModelUVETest, BoardLanesGroupFilesByKind) {
    const std::vector<ContentItemFactsUVE> facts{
        {"rock", "Mesh", "", 0U, 0, false}, {"Sea", "Folder", "", 0U, 0, true},  {"wave", "Texture", "", 0U, 0, false},
        {"reef", "Mesh", "", 0U, 0, false}, {"hit", "Audio", "", 0U, 0, false},
    };
    const auto lanes = GroupContentByTypeUVE({0U, 1U, 2U, 3U, 4U}, facts);
    ASSERT_EQ(lanes.size(), 3U);
    EXPECT_EQ(lanes[0].first, "Audio");
    EXPECT_EQ(lanes[1].first, "Mesh");
    EXPECT_EQ(lanes[1].second, (std::vector<std::size_t>{0U, 3U}));
    EXPECT_EQ(lanes[2].first, "Texture");
}

const std::vector<std::string> kShownUVE{"a.uvanim", "b.uvanim", "c.uvanim", "d.uvanim", "e.uvanim"};

TEST(ContentSelectionUVETest, APlainClickPicksOnlyThatItem) {
    ContentSelectionUVE selection;
    selection.ClickUVE(kShownUVE, "b.uvanim", false, false);
    selection.ClickUVE(kShownUVE, "d.uvanim", false, false);
    EXPECT_EQ(selection.ItemsUVE(), (std::vector<std::string>{"d.uvanim"}));
}

TEST(ContentSelectionUVETest, ControlClickAddsAndRemovesOneItem) {
    ContentSelectionUVE selection;
    selection.ClickUVE(kShownUVE, "a.uvanim", false, false);
    selection.ClickUVE(kShownUVE, "c.uvanim", true, false);
    selection.ClickUVE(kShownUVE, "e.uvanim", true, false);
    EXPECT_EQ(selection.ItemsUVE(), (std::vector<std::string>{"a.uvanim", "c.uvanim", "e.uvanim"}));
    selection.ClickUVE(kShownUVE, "c.uvanim", true, false);
    EXPECT_FALSE(selection.ContainsUVE("c.uvanim"));
    EXPECT_TRUE(selection.ContainsUVE("a.uvanim"));
}

TEST(ContentSelectionUVETest, ShiftClickPicksTheRunFromTheAnchorInEitherDirection) {
    ContentSelectionUVE selection;
    selection.ClickUVE(kShownUVE, "b.uvanim", false, false);
    selection.ClickUVE(kShownUVE, "d.uvanim", false, true);
    EXPECT_EQ(selection.ItemsUVE(), (std::vector<std::string>{"b.uvanim", "c.uvanim", "d.uvanim"}));
    // The anchor stays at b: shift-clicking above it swaps the run rather than adding to it.
    selection.ClickUVE(kShownUVE, "a.uvanim", false, true);
    EXPECT_EQ(selection.ItemsUVE(), (std::vector<std::string>{"a.uvanim", "b.uvanim"}));
    // Shrinking works from the same end.
    selection.ClickUVE(kShownUVE, "e.uvanim", false, true);
    EXPECT_EQ(selection.ItemsUVE().size(), 4U);
    EXPECT_FALSE(selection.ContainsUVE("a.uvanim"));
}

TEST(ContentSelectionUVETest, ControlShiftAddsTheRunToWhatIsPicked) {
    ContentSelectionUVE selection;
    selection.ClickUVE(kShownUVE, "a.uvanim", false, false);
    selection.ClickUVE(kShownUVE, "c.uvanim", true, false);
    selection.ClickUVE(kShownUVE, "e.uvanim", true, true);
    EXPECT_EQ(selection.ItemsUVE(), (std::vector<std::string>{"a.uvanim", "c.uvanim", "d.uvanim", "e.uvanim"}));
}

TEST(ContentSelectionUVETest, ShiftWithoutAnAnchorOrForAnItemNotShownActsAsAPlainClick) {
    ContentSelectionUVE selection;
    selection.ClickUVE(kShownUVE, "c.uvanim", false, true);
    EXPECT_EQ(selection.ItemsUVE(), (std::vector<std::string>{"c.uvanim"}));
    selection.ClickUVE(kShownUVE, "zzz.uvanim", false, true);
    EXPECT_EQ(selection.ItemsUVE(), (std::vector<std::string>{"zzz.uvanim"}));
}

TEST(ContentSelectionUVETest, KeepOnlyDropsItemsThatAreGoneAndClearForgetsTheAnchor) {
    ContentSelectionUVE selection;
    selection.ClickUVE(kShownUVE, "a.uvanim", false, false);
    selection.ClickUVE(kShownUVE, "d.uvanim", false, true);
    ASSERT_EQ(selection.ItemsUVE().size(), 4U);
    selection.KeepOnlyUVE(std::vector<std::string>{"a.uvanim", "d.uvanim"});
    EXPECT_EQ(selection.ItemsUVE(), (std::vector<std::string>{"a.uvanim", "d.uvanim"}));
    selection.ClearUVE();
    EXPECT_TRUE(selection.IsEmptyUVE());
    selection.ClickUVE(kShownUVE, "c.uvanim", false, true);
    EXPECT_EQ(selection.ItemsUVE().size(), 1U) << "no anchor after clearing: a plain click";
}

} // namespace
} // namespace UVE::Editor
