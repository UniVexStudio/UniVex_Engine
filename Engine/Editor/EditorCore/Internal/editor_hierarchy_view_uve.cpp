// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_hierarchy_view_uve.h"

#include <algorithm>

namespace UVE::Editor {

bool IsDuplicateNameSuffixPatternValidUVE(const std::string_view pattern) noexcept {
    if (pattern.empty() || pattern.size() > kMaximumDuplicateNameSuffixPatternBytesUVE) {
        return false;
    }
    // One %n, and the only percent sign: a pattern holding any other % sequence would either
    // duplicate the number or surprise the author at the moment a name is taken.
    const std::size_t percent = pattern.find('%');
    if (percent == std::string_view::npos || percent + 1U >= pattern.size() || pattern[percent + 1U] != 'n') {
        return false;
    }
    return pattern.find('%', percent + 1U) == std::string_view::npos;
}

std::string FormatDuplicateNameUVE(const std::string_view baseName, const std::size_t index,
                                   const std::string_view pattern) {
    const std::string_view effective =
        IsDuplicateNameSuffixPatternValidUVE(pattern) ? pattern : kDefaultDuplicateNameSuffixPatternUVE;
    const std::size_t token = effective.find(kDuplicateNameNumberTokenUVE);
    std::string name{baseName};
    name.append(effective.substr(0U, token));
    name.append(std::to_string(index));
    name.append(effective.substr(token + kDuplicateNameNumberTokenUVE.size()));
    return name;
}

namespace {

[[nodiscard]] unsigned char FoldSortByteUVE(const unsigned char value) noexcept {
    if (value >= static_cast<unsigned char>('A') && value <= static_cast<unsigned char>('Z')) {
        return static_cast<unsigned char>(value + static_cast<unsigned char>('a' - 'A'));
    }
    return value;
}

[[nodiscard]] int CompareCaseInsensitiveUVE(const std::string_view left, const std::string_view right) noexcept {
    const std::size_t commonSize = std::min(left.size(), right.size());
    for (std::size_t index = 0U; index < commonSize; ++index) {
        const unsigned char leftByte = FoldSortByteUVE(static_cast<unsigned char>(left[index]));
        const unsigned char rightByte = FoldSortByteUVE(static_cast<unsigned char>(right[index]));
        if (leftByte != rightByte) {
            return leftByte < rightByte ? -1 : 1;
        }
    }
    if (left.size() == right.size()) {
        return 0;
    }
    return left.size() < right.size() ? -1 : 1;
}

} // namespace

int CompareHierarchySortKeysUVE(const HierarchySortModeUVE mode, const std::string_view leftName,
                                const std::string_view leftType, const std::string_view rightName,
                                const std::string_view rightType) noexcept {
    switch (mode) {
        case HierarchySortModeUVE::SceneOrder:
            return 0;
        case HierarchySortModeUVE::Alphabetical:
            return CompareCaseInsensitiveUVE(leftName, rightName);
        case HierarchySortModeUVE::ByType:
            if (const int typeOrder = CompareCaseInsensitiveUVE(leftType, rightType); typeOrder != 0) {
                return typeOrder;
            }
            return CompareCaseInsensitiveUVE(leftName, rightName);
    }
    return 0;
}

bool HasHierarchyErrorDiagnosticsUVE(const std::vector<HierarchyDiagnosticUVE>& diagnostics) noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const HierarchyDiagnosticUVE& diagnostic) {
        return diagnostic.severity == HierarchyDiagnosticSeverityUVE::Error;
    });
}


HierarchyIconAccentUVE GetHierarchyIconAccentUVE(const std::string_view typeCategory) noexcept {
    // These restrained, distinct colours sit behind the existing multicolour PNGs; the artwork is
    // never multiplied by a tint, so category remains an accent rather than a replacement palette.
    if (typeCategory == "Scene") {
        return {106U, 170U, 235U};
    }
    if (typeCategory == "Physics") {
        return {232U, 153U, 86U};
    }
    if (typeCategory == "Navigation") {
        return {197U, 177U, 89U};
    }
    if (typeCategory == "Animation") {
        return {168U, 132U, 216U};
    }
    if (typeCategory == "Camera") {
        return {99U, 178U, 220U};
    }
    if (typeCategory == "Combat") {
        return {218U, 105U, 112U};
    }
    if (typeCategory == "Gameplay") {
        return {129U, 188U, 112U};
    }
    if (typeCategory == "Rendering") {
        return {77U, 171U, 173U};
    }
    if (typeCategory == "Optimization") {
        return {140U, 164U, 178U};
    }
    if (typeCategory == "World") {
        return {121U, 151U, 203U};
    }
    if (typeCategory == "Audio") {
        return {190U, 119U, 190U};
    }
    if (typeCategory == "VFX") {
        return {205U, 127U, 148U};
    }
    if (typeCategory == "Logic") {
        return {123U, 154U, 213U};
    }
    if (typeCategory == "UI") {
        return {81U, 174U, 193U};
    }
    return {148U, 153U, 161U};
}

bool ShouldDrawHierarchyEyeUVE(const HierarchyVisibilityColumnUVE mode, const bool rowHovered,
                               const bool objectVisible) noexcept {
    switch (mode) {
        case HierarchyVisibilityColumnUVE::Always:
            return true;
        case HierarchyVisibilityColumnUVE::OnHover:
            return rowHovered || !objectVisible;
        case HierarchyVisibilityColumnUVE::Hidden:
            return false;
    }
    return true;
}

bool ShouldDrawHierarchyLockGlyphUVE(const bool locked, const bool rowHovered) noexcept {
    return locked || rowHovered;
}

HierarchyStructuralRowStyleUVE GetHierarchyStructuralRowStyleUVE(const bool structuralRoot,
                                                                const bool selected) noexcept {
    HierarchyStructuralRowStyleUVE style;
    if (!structuralRoot) {
        return style;
    }
    style.structuralRoot = true;
    // The same warm gold the locked-row padlock uses: "this is the document itself" reads as an
    // accent the panel already has, rather than as one more colour to learn.
    style.bar = HierarchyIconAccentUVE{224U, 174U, 82U};
    style.band = style.bar;
    // Low enough to sit behind the name without dimming it, and skipped entirely when selected,
    // where the header colour is already painting the row.
    style.bandAlpha = selected ? 0U : 30U;
    return style;
}

std::string_view GetHierarchyTypeHintUVE(const std::string_view name, const std::string_view type) noexcept {
    return type == name ? std::string_view{} : type;
}

} // namespace UVE::Editor
