// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_hierarchy_view_uve.h"

#include <gtest/gtest.h>

namespace UVE::Editor::Tests {
namespace {

TEST(EditorHierarchyViewUVETest, ErrorDiagnosticsTakePrecedenceOverWarningBadgeSeverity) {
    using Severity = HierarchyDiagnosticSeverityUVE;
    EXPECT_FALSE(HasHierarchyErrorDiagnosticsUVE(std::vector<HierarchyDiagnosticUVE>{}));
    EXPECT_FALSE(HasHierarchyErrorDiagnosticsUVE(
        std::vector<HierarchyDiagnosticUVE>{{Severity::Warning, "No mesh"}}));
    EXPECT_TRUE(HasHierarchyErrorDiagnosticsUVE(
        std::vector<HierarchyDiagnosticUVE>{{Severity::Error, "Broken asset"}}));
    EXPECT_TRUE(HasHierarchyErrorDiagnosticsUVE(
        std::vector<HierarchyDiagnosticUVE>{{Severity::Warning, "No mesh"},
                                             {Severity::Error, "Broken asset"}}));
}

TEST(EditorHierarchyViewUVETest, EyeIsDrawnPerModeAndAlwaysOnAHiddenObjectWhenOnHover) {
    using Mode = HierarchyVisibilityColumnUVE;
    for (const bool hovered : {false, true}) {
        for (const bool visible : {false, true}) {
            EXPECT_TRUE(ShouldDrawHierarchyEyeUVE(Mode::Always, hovered, visible));
            EXPECT_FALSE(ShouldDrawHierarchyEyeUVE(Mode::Hidden, hovered, visible));
        }
    }
    EXPECT_FALSE(ShouldDrawHierarchyEyeUVE(Mode::OnHover, false, true));
    EXPECT_TRUE(ShouldDrawHierarchyEyeUVE(Mode::OnHover, true, true));
    // A hidden object keeps its closed eye, so it never looks like a shown one.
    EXPECT_TRUE(ShouldDrawHierarchyEyeUVE(Mode::OnHover, false, false));
}

TEST(EditorHierarchyViewUVETest, LockGlyphStaysVisibleWhenLockedAndAppearsOnHoverWhenUnlocked) {
    EXPECT_TRUE(ShouldDrawHierarchyLockGlyphUVE(true, false));
    EXPECT_TRUE(ShouldDrawHierarchyLockGlyphUVE(true, true));
    EXPECT_FALSE(ShouldDrawHierarchyLockGlyphUVE(false, false));
    EXPECT_TRUE(ShouldDrawHierarchyLockGlyphUVE(false, true));
}

TEST(EditorHierarchyViewUVETest, StructuralRootRowsGetABarAndABandThatYieldsToSelection) {
    // An ordinary object row is marked by nothing at all, selected or not.
    for (const bool selected : {false, true}) {
        const HierarchyStructuralRowStyleUVE plain = GetHierarchyStructuralRowStyleUVE(false, selected);
        EXPECT_FALSE(plain.structuralRoot);
        EXPECT_EQ(plain.bandAlpha, 0U);
        EXPECT_EQ(plain.band, HierarchyIconAccentUVE{});
        EXPECT_EQ(plain.bar, HierarchyIconAccentUVE{});
    }

    const HierarchyStructuralRowStyleUVE root = GetHierarchyStructuralRowStyleUVE(true, false);
    EXPECT_TRUE(root.structuralRoot);
    EXPECT_NE(root.bar, HierarchyIconAccentUVE{});
    EXPECT_GT(root.bandAlpha, 0U);
    // One accent drawn twice: a solid bar and a faint band behind the row.
    EXPECT_EQ(root.bar, root.band);

    // The selection colour is already painting a selected row, so the band steps aside rather than
    // stacking a second highlight on it; the bar is what identifies the row and stays.
    const HierarchyStructuralRowStyleUVE selectedRoot = GetHierarchyStructuralRowStyleUVE(true, true);
    EXPECT_TRUE(selectedRoot.structuralRoot);
    EXPECT_EQ(selectedRoot.bandAlpha, 0U);
    EXPECT_EQ(selectedRoot.bar, root.bar);
}

TEST(EditorHierarchyViewUVETest, SortKeysAreCaseInsensitiveAndTypeSortUsesNameAsAStableSecondaryKey) {
    using Sort = HierarchySortModeUVE;
    EXPECT_EQ(CompareHierarchySortKeysUVE(Sort::SceneOrder, "Zulu", "A", "Alpha", "Z"), 0);

    EXPECT_LT(CompareHierarchySortKeysUVE(Sort::Alphabetical, "alpha", "Z", "Bravo", "A"), 0);
    EXPECT_GT(CompareHierarchySortKeysUVE(Sort::Alphabetical, "Zulu", "A", "Alpha", "Z"), 0);
    EXPECT_EQ(CompareHierarchySortKeysUVE(Sort::Alphabetical, "Alpha", "A", "aLPHa", "Z"), 0);

    // Type is the primary key; names order rows that share a type. A fully equal key leaves
    // the original scene order intact when the caller uses stable_sort.
    EXPECT_LT(CompareHierarchySortKeysUVE(Sort::ByType, "Zulu", "Camera", "Alpha", "Static"), 0);
    EXPECT_LT(CompareHierarchySortKeysUVE(Sort::ByType, "Alpha", "Camera", "Zulu", "Camera"), 0);
    EXPECT_EQ(CompareHierarchySortKeysUVE(Sort::ByType, "Alpha", "Camera", "alpha", "camera"), 0);
}

TEST(EditorHierarchyViewUVETest, TypeHintIsLeftOutWhenItWouldOnlyRepeatTheName) {
    EXPECT_EQ(GetHierarchyTypeHintUVE("Player", "Character3D"), "Character3D");
    EXPECT_EQ(GetHierarchyTypeHintUVE("BoxMesh3D", "BoxMesh3D"), "");
    EXPECT_EQ(GetHierarchyTypeHintUVE("Player", ""), "");
    // Only an exact repeat is left out; a name that differs only in case still shows the type.
    EXPECT_EQ(GetHierarchyTypeHintUVE("boxmesh3d", "BoxMesh3D"), "BoxMesh3D");
}

TEST(EditorHierarchyViewUVETest, IconAccentIsStableForEverySceneObjectCategoryAndNeutralForUnknownOnes) {
    struct CategoryAccent final {
        std::string_view category;
        HierarchyIconAccentUVE accent;
    };
    constexpr CategoryAccent cases[] = {
        {"Scene", {106U, 170U, 235U}},       {"Physics", {232U, 153U, 86U}},
        {"Navigation", {197U, 177U, 89U}},   {"Animation", {168U, 132U, 216U}},
        {"Camera", {99U, 178U, 220U}},       {"Combat", {218U, 105U, 112U}},
        {"Gameplay", {129U, 188U, 112U}},    {"Rendering", {77U, 171U, 173U}},
        {"Optimization", {140U, 164U, 178U}}, {"World", {121U, 151U, 203U}},
        {"Audio", {190U, 119U, 190U}},       {"VFX", {205U, 127U, 148U}},
        {"Logic", {123U, 154U, 213U}},       {"UI", {81U, 174U, 193U}},
    };
    constexpr HierarchyIconAccentUVE neutral{148U, 153U, 161U};
    for (const CategoryAccent& testCase : cases) {
        EXPECT_EQ(GetHierarchyIconAccentUVE(testCase.category), testCase.accent) << testCase.category;
        EXPECT_NE(testCase.accent, neutral) << testCase.category;
    }
    EXPECT_EQ(GetHierarchyIconAccentUVE("Unknown"), neutral);
    EXPECT_EQ(GetHierarchyIconAccentUVE(""), neutral);
}

TEST(EditorHierarchyViewUVETest, DefaultsAreThePanelsBehaviourBeforeItHadPreferences) {
    const HierarchyViewSettingsUVE view{};
    EXPECT_TRUE(view.revealSelection);
    EXPECT_FALSE(view.selectChildren);
    EXPECT_TRUE(view.showIcons);
    EXPECT_FALSE(view.colorCodeIcons); // opt-in; existing icon artwork stays unaltered by default
    EXPECT_TRUE(view.showComponentBadges);
    EXPECT_TRUE(view.showTypeName); // new with object types, and on
    EXPECT_EQ(view.visibilityColumn, HierarchyVisibilityColumnUVE::Always);
    EXPECT_EQ(view.doubleClick, HierarchyDoubleClickUVE::Rename);
    EXPECT_EQ(view.renameMode, HierarchyRenameModeUVE::Inline);
    EXPECT_EQ(view.filterMode, HierarchyFilterModeUVE::NameOnly);
    EXPECT_EQ(view.sortMode, HierarchySortModeUVE::SceneOrder);
    EXPECT_FALSE(view.filterCaseSensitive);
    EXPECT_TRUE(view.filterKeepAncestors);
    EXPECT_TRUE(view.dragToReparent);
    EXPECT_TRUE(view.confirmLargeSubtreeReparent);
    EXPECT_TRUE(view.confirmDeleteSubtree);
    EXPECT_EQ(view.treeLines, HierarchyTreeLinesUVE::None);
    EXPECT_EQ(view.duplicateNameSuffix, std::string{kDefaultDuplicateNameSuffixPatternUVE});
    EXPECT_FLOAT_EQ(view.rowHeight, kMinimumHierarchyRowHeightUVE);
    EXPECT_LE(view.rowHeight, kMaximumHierarchyRowHeightUVE);
    EXPECT_GE(view.indentWidth, kMinimumHierarchyIndentUVE);
    EXPECT_LE(view.indentWidth, kMaximumHierarchyIndentUVE);
}

TEST(EditorHierarchyViewUVETest, DuplicateNameSuffixPatternUVE_ValidatesAndFormats) {
    EXPECT_TRUE(IsDuplicateNameSuffixPatternValidUVE(" %n"));
    EXPECT_TRUE(IsDuplicateNameSuffixPatternValidUVE("_%n"));
    EXPECT_TRUE(IsDuplicateNameSuffixPatternValidUVE(" (%n)"));
    EXPECT_TRUE(IsDuplicateNameSuffixPatternValidUVE("%n"));
    // Invalid: nothing to number, a second percent sign, a different placeholder, an empty or an
    // over-long pattern.
    EXPECT_FALSE(IsDuplicateNameSuffixPatternValidUVE(""));
    EXPECT_FALSE(IsDuplicateNameSuffixPatternValidUVE("_"));
    EXPECT_FALSE(IsDuplicateNameSuffixPatternValidUVE("Lamp"));
    EXPECT_FALSE(IsDuplicateNameSuffixPatternValidUVE("%n%n"));
    EXPECT_FALSE(IsDuplicateNameSuffixPatternValidUVE("%d"));
    EXPECT_FALSE(IsDuplicateNameSuffixPatternValidUVE("%"));
    EXPECT_FALSE(IsDuplicateNameSuffixPatternValidUVE(
        std::string(kMaximumDuplicateNameSuffixPatternBytesUVE - 1U, 'n') + "%n"));

    EXPECT_EQ(FormatDuplicateNameUVE("Lamp", 2U, " %n"), "Lamp 2");
    EXPECT_EQ(FormatDuplicateNameUVE("Lamp", 3U, "_%n"), "Lamp_3");
    EXPECT_EQ(FormatDuplicateNameUVE("Lamp", 4U, " (%n)"), "Lamp (4)");
    EXPECT_EQ(FormatDuplicateNameUVE("Lamp", 12U, "%n"), "Lamp12");
    // A pattern that could not name anything falls back to the shipped one.
    EXPECT_EQ(FormatDuplicateNameUVE("Lamp", 2U, "garbage"), "Lamp 2");
    EXPECT_EQ(FormatDuplicateNameUVE("Lamp", 2U, ""), "Lamp 2");
}

} // namespace
} // namespace UVE::Editor::Tests
