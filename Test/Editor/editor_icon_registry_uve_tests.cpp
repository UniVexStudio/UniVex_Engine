// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_icon_registry_uve.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>

#include <gtest/gtest.h>

namespace UVE::Editor::Tests {
namespace {

TEST(EditorIconRegistryUVETest, EveryIconKindIsReachableByName) {
    // The check that keeps the registry honest as icons are added. An enumerator with no name is
    // a glyph that exists, costs code, and cannot be asked for - which is exactly the state the
    // whole editor was in before this file, just spelled differently.
    //
    // Listed explicitly rather than iterated, because C++ gives no way to enumerate an enum: a
    // new enumerator added without a name would silently pass an iteration-based test.
    const std::array<HierarchyNodeIconKindUVE, 10U> everyKind{
        HierarchyNodeIconKindUVE::Node3D,       HierarchyNodeIconKindUVE::Mesh,
        HierarchyNodeIconKindUVE::Camera,      HierarchyNodeIconKindUVE::Light,
        HierarchyNodeIconKindUVE::Environment, HierarchyNodeIconKindUVE::Physics,
        HierarchyNodeIconKindUVE::Audio,       HierarchyNodeIconKindUVE::Particle,
        HierarchyNodeIconKindUVE::Script,      HierarchyNodeIconKindUVE::Animation};

    for (const HierarchyNodeIconKindUVE kind : everyKind) {
        const bool named = std::any_of(kEditorIconRegistryUVE.cbegin(), kEditorIconRegistryUVE.cend(),
                                       [kind](const EditorIconEntryUVE& entry) { return entry.kind == kind; });
        EXPECT_TRUE(named) << "icon kind " << static_cast<int>(kind)
                           << " has no name - it can be drawn but never requested";
    }
}

TEST(EditorIconRegistryUVETest, NamesAreUniqueAndLowercase) {
    // Two rows sharing a name means one is unreachable, and whichever loses depends on table
    // order - a bug that looks like "the icon just doesn't work" with nothing to point at.
    //
    // Lowercase is the documented convention. Enforced rather than trusted because the cost of a
    // stray capital is a caller that looks correct, compiles, and silently gets the fallback.
    std::set<std::string_view> seen;
    for (const EditorIconEntryUVE& entry : kEditorIconRegistryUVE) {
        EXPECT_TRUE(seen.insert(entry.name).second) << "duplicate icon name: " << entry.name;
        EXPECT_FALSE(entry.name.empty());
        const bool allLower = std::none_of(entry.name.cbegin(), entry.name.cend(), [](const char character) {
            return std::isupper(static_cast<unsigned char>(character)) != 0;
        });
        EXPECT_TRUE(allLower) << "icon name is not lowercase: " << entry.name;
    }
}

TEST(EditorIconRegistryUVETest, KnownNamesResolveToTheirGlyph) {
    EXPECT_EQ(ResolveEditorIconUVE("mesh"), HierarchyNodeIconKindUVE::Mesh);
    EXPECT_EQ(ResolveEditorIconUVE("camera"), HierarchyNodeIconKindUVE::Camera);
    EXPECT_EQ(ResolveEditorIconUVE("script"), HierarchyNodeIconKindUVE::Script);
    EXPECT_EQ(ResolveEditorIconUVE("animation"), HierarchyNodeIconKindUVE::Animation);
}

TEST(EditorIconRegistryUVETest, AliasesResolveToTheSameGlyphAsTheirPrimaryName) {
    // Aliases exist because the same picture legitimately answers to different words depending on
    // which panel is asking. They are only useful if they genuinely agree - an alias that drifted
    // to a different glyph would be worse than not having it.
    EXPECT_EQ(ResolveEditorIconUVE("sun"), ResolveEditorIconUVE("light"));
    EXPECT_EQ(ResolveEditorIconUVE("model"), ResolveEditorIconUVE("mesh"));
    EXPECT_EQ(ResolveEditorIconUVE("world"), ResolveEditorIconUVE("environment"));
    EXPECT_EQ(ResolveEditorIconUVE("collider"), ResolveEditorIconUVE("physics"));
    EXPECT_EQ(ResolveEditorIconUVE("sound"), ResolveEditorIconUVE("audio"));
    EXPECT_EQ(ResolveEditorIconUVE("node"), ResolveEditorIconUVE("empty"));
}

TEST(EditorIconRegistryUVETest, UnknownNameFallsBackToTheEmptyGlyphRatherThanFailing) {
    // An icon is decoration. A typo should leave the row readable with a neutral marker, not blank
    // the outliner and not take the editor down - so the miss path is a deliberate fallback, and
    // this pins it rather than leaving it to whoever next reads the loop.
    EXPECT_EQ(ResolveEditorIconUVE("definitely-not-an-icon"), HierarchyNodeIconKindUVE::Node3D);
    EXPECT_EQ(ResolveEditorIconUVE(""), HierarchyNodeIconKindUVE::Node3D);
    EXPECT_EQ(ResolveEditorIconUVE("Camera"), HierarchyNodeIconKindUVE::Node3D)
        << "lookup is case-sensitive by design; the convention is lowercase names";
}

TEST(EditorIconRegistryUVETest, RegistrationIsDistinguishableFromTheFallback) {
    // ResolveEditorIconUVE cannot tell "unknown name" from "the empty icon, requested by name" -
    // both are Empty. A caller validating author-supplied text needs to tell those apart, which is
    // the entire reason IsEditorIconRegisteredUVE exists.
    EXPECT_TRUE(IsEditorIconRegisteredUVE("empty"));
    // The kind's current spelling is registered too, and resolves to the same glyph the legacy
    // name maps to.
    EXPECT_TRUE(IsEditorIconRegisteredUVE("node_3d"));
    EXPECT_EQ(ResolveEditorIconUVE("node_3d"), ResolveEditorIconUVE("empty"));
    EXPECT_FALSE(IsEditorIconRegisteredUVE("definitely-not-an-icon"));
    EXPECT_EQ(ResolveEditorIconUVE("empty"), ResolveEditorIconUVE("definitely-not-an-icon"))
        << "both resolve to Empty - which is why the two functions are not redundant";
}

TEST(EditorIconRegistryUVETest, LookupIsUsableInAConstantExpression) {
    // Compile-time resolution is what lets a caller write a name with no runtime cost over the
    // enum it replaced. If this ever stops being constexpr, the naming becomes a per-frame string
    // comparison in a draw loop instead of a free convenience.
    static_assert(ResolveEditorIconUVE("camera") == HierarchyNodeIconKindUVE::Camera);
    static_assert(ResolveEditorIconUVE("unknown") == HierarchyNodeIconKindUVE::Node3D);
    static_assert(IsEditorIconRegisteredUVE("mesh"));
    static_assert(!IsEditorIconRegisteredUVE("mesh-typo"));
    SUCCEED();
}

} // namespace
} // namespace UVE::Editor::Tests
