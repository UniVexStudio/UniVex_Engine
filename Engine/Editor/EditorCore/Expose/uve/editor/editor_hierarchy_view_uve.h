// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Editor {

/// When the hierarchy draws a row's visibility eye.
enum class HierarchyVisibilityColumnUVE {
    Always,
    /// While the row is hovered, and always for a hidden object, so a hidden object never looks shown.
    OnHover,
    Hidden,
};

/// What a double-click on a hierarchy row does. F2 renames whichever is chosen.
enum class HierarchyDoubleClickUVE {
    Rename,
    FocusInViewport,
    ExpandCollapse,
};

/// Where a hierarchy rename is edited. F2 and the Rename context-menu item use this mode too.
enum class HierarchyRenameModeUVE {
    Inline,
    Dialog,
};

/// What plain text in the hierarchy filter searches in addition to object names.
enum class HierarchyFilterModeUVE {
    NameOnly,
    NameTypeAndComponents,
};

/// How sibling rows are ordered in the hierarchy view. This never changes scene sibling order.
enum class HierarchySortModeUVE : std::uint8_t {
    SceneOrder,
    Alphabetical,
    ByType,
};

/// Compare two display keys for the selected hierarchy sort mode. With a stable sort, a zero result
/// preserves the supplied sibling order (the default for SceneOrder and equal keys in other modes).
[[nodiscard]] int CompareHierarchySortKeysUVE(HierarchySortModeUVE mode, std::string_view leftName,
                                               std::string_view leftType, std::string_view rightName,
                                               std::string_view rightType) noexcept;

/// The lines that join a row to its parent.
enum class HierarchyTreeLinesUVE {
    None,
    /// Down to the last child, with a stub to each child.
    ToEachChild,
    /// Down the whole open branch. Cheaper on very large trees.
    FullHeight,
};

/// How severe a hierarchy setup diagnostic is. Errors take precedence for the row's badge glyph.
enum class HierarchyDiagnosticSeverityUVE : std::uint8_t {
    Warning,
    Error,
};

/// One hierarchy setup issue shown in the row's diagnostic tooltip.
struct HierarchyDiagnosticUVE final {
    HierarchyDiagnosticSeverityUVE severity = HierarchyDiagnosticSeverityUVE::Warning;
    std::string message;

    [[nodiscard]] bool operator==(const HierarchyDiagnosticUVE&) const = default;
};

/// Whether a row containing diagnostics should use its red error glyph rather than amber warning glyph.
[[nodiscard]] bool HasHierarchyErrorDiagnosticsUVE(const std::vector<HierarchyDiagnosticUVE>& diagnostics) noexcept;

/// A subtle category backplate behind a Scene object icon. The icon artwork stays untouched.
struct HierarchyIconAccentUVE final {
    std::uint8_t red = 0U;
    std::uint8_t green = 0U;
    std::uint8_t blue = 0U;

    [[nodiscard]] bool operator==(const HierarchyIconAccentUVE&) const = default;
};

/// The markers that set a structural root row - the Object, the document viewport and the
/// entity-editor root: the document's own rows rather than objects under them - apart from every
/// other row. Both are drawn before the row's label, so nothing is ever painted over text or glyphs.
struct HierarchyStructuralRowStyleUVE final {
    /// The bar along the row's left edge. This is the row's identity, so it is drawn whenever the
    /// row is a structural root.
    HierarchyIconAccentUVE bar{};
    /// A low-alpha band behind the whole row. Redundant with the selection colour, so it steps
    /// aside while the row is selected (see GetHierarchyStructuralRowStyleUVE()).
    HierarchyIconAccentUVE band{};
    std::uint8_t bandAlpha = 0U;
    /// False for every ordinary object row, and the only field a caller needs to test.
    bool structuralRoot = false;

    [[nodiscard]] bool operator==(const HierarchyStructuralRowStyleUVE&) const = default;
};

/// Width of the structural bar, in pixels. Narrow enough to read as an edge marker rather than as
/// an icon, wide enough not to vanish on a fractional-pixel row boundary.
inline constexpr float kHierarchyStructuralRowBarWidthUVE = 2.0F;

/// How the hierarchy panel looks and responds. Every field is an editor preference
/// (editor_settings_uve.cpp); apart from showTypeName, which came with object types, the defaults
/// are how the panel behaved before they existed.
/// The number placeholder inside a duplicate-name suffix pattern.
inline constexpr std::string_view kDuplicateNameNumberTokenUVE = "%n";
inline constexpr std::string_view kDefaultDuplicateNameSuffixPatternUVE = " %n";
/// Long enough for a separator, a token and short decoration; the preference refuses more.
inline constexpr std::size_t kMaximumDuplicateNameSuffixPatternBytesUVE = 32U;

struct HierarchyViewSettingsUVE final {
    bool revealSelection = true;
    /// When a row is selected, include each unlocked descendant in the selection too.
    bool selectChildren = false;
    bool showIcons = true;
    /// Draw a low-alpha type-category backplate behind each icon.
    bool colorCodeIcons = false;
    /// Show attached-script component badges; diagnostics, visibility, and the lock column are separate.
    bool showComponentBadges = true;
    bool showTypeName = true;
    HierarchyVisibilityColumnUVE visibilityColumn = HierarchyVisibilityColumnUVE::Always;
    HierarchyDoubleClickUVE doubleClick = HierarchyDoubleClickUVE::Rename;
    HierarchyRenameModeUVE renameMode = HierarchyRenameModeUVE::Inline;
    HierarchyFilterModeUVE filterMode = HierarchyFilterModeUVE::NameOnly;
    /// Display-only row ordering; the saved sibling order remains authoritative.
    HierarchySortModeUVE sortMode = HierarchySortModeUVE::SceneOrder;
    bool filterCaseSensitive = false;
    bool filterKeepAncestors = true;
    bool dragToReparent = true;
    /// Ask before dropping a hierarchy subtree with at least the configured threshold of entities.
    bool confirmLargeSubtreeReparent = true;
    /// Ask before deleting a branch: a lone object deletes immediately (still undoable), a
    /// subtree at or above the delete threshold asks first, since children die silently with it.
    bool confirmDeleteSubtree = true;
    HierarchyTreeLinesUVE treeLines = HierarchyTreeLinesUVE::None;
    /// Height of each selectable hierarchy row, in pixels.
    float rowHeight = 16.0F;
    /// Pixels each level is indented by.
    float indentWidth = 12.0F;
    /// How a taken name gets its number: the pattern appended to the base, with `%n` standing in
    /// for the number (see FormatDuplicateNameUVE). The default reproduces "Lamp" -> "Lamp 2".
    std::string duplicateNameSuffix = std::string{kDefaultDuplicateNameSuffixPatternUVE};
};

inline constexpr std::size_t kHierarchyLargeSubtreeReparentThresholdUVE = 64U;
/// A delete affecting this many entities asks first: two means the object has at least one
/// descendant going down with it, which is exactly the danger the confirmation guards.
inline constexpr std::size_t kHierarchyDeleteSubtreeConfirmThresholdUVE = 2U;
inline constexpr float kMinimumHierarchyRowHeightUVE = 16.0F;
inline constexpr float kMaximumHierarchyRowHeightUVE = 48.0F;
inline constexpr float kMinimumHierarchyIndentUVE = 12.0F;
inline constexpr float kMaximumHierarchyIndentUVE = 40.0F;

/// Whether a duplicate-name suffix pattern can produce a name: exactly one `%n` marks where the
/// number goes. An invalid pattern is refused by the preference, so a stored one always formats.
[[nodiscard]] bool IsDuplicateNameSuffixPatternValidUVE(std::string_view pattern) noexcept;

/// The name the `index`th object to ask for `baseName` takes: the base with the pattern's `%n`
/// replaced by `index`, so the shipped pattern gives "Lamp 2", "Lamp 3", and `"_%n"` gives
/// "Lamp_2". An invalid pattern falls back to the shipped one, so this never returns a bare
/// number or an empty name.
[[nodiscard]] std::string FormatDuplicateNameUVE(std::string_view baseName, std::size_t index,
                                                 std::string_view pattern);

/// A stable accent for an object type's category, or a muted neutral for unknown categories.
[[nodiscard]] HierarchyIconAccentUVE GetHierarchyIconAccentUVE(std::string_view typeCategory) noexcept;

/// Whether a row's eye is drawn this frame. The column itself is kept whenever the mode is not
/// Hidden, so names do not shift as the pointer moves over rows.
[[nodiscard]] bool ShouldDrawHierarchyEyeUVE(HierarchyVisibilityColumnUVE mode, bool rowHovered,
                                             bool objectVisible) noexcept;

/// Keep a locked object's lock visible; reveal the unlocked control while the row is hovered.
[[nodiscard]] bool ShouldDrawHierarchyLockGlyphUVE(bool locked, bool rowHovered) noexcept;

/// The markers for one hierarchy row. The bar is the row's identity and never goes away; the band
/// is a second, redundant highlight, so it steps aside while the row is selected and the selection
/// colour is already painting it.
[[nodiscard]] HierarchyStructuralRowStyleUVE GetHierarchyStructuralRowStyleUVE(bool structuralRoot,
                                                                             bool selected) noexcept;

/// The type shown after a row's name, or empty when it would only repeat the name (an object still
/// called by its type's name).
[[nodiscard]] std::string_view GetHierarchyTypeHintUVE(std::string_view name, std::string_view type) noexcept;

} // namespace UVE::Editor
