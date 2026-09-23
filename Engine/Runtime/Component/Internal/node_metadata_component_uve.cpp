// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/node_metadata_component_uve.h"

#include <algorithm>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsAsciiLetterUVE(const char character) noexcept {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

[[nodiscard]] bool IsAsciiDigitUVE(const char character) noexcept {
    return character >= '0' && character <= '9';
}

} // namespace

NodeMetadataKeyIssueUVE ValidateNodeMetadataKeyUVE(const std::string_view key,
                                                   const NodeMetadataComponentUVE& existing,
                                                   const std::string_view ignoredKey) noexcept {
    if (key.empty()) {
        return NodeMetadataKeyIssueUVE::Empty;
    }
    if (key.size() > kMaximumNodeMetadataKeyBytesUVE) {
        return NodeMetadataKeyIssueUVE::TooLong;
    }
    if (IsAsciiDigitUVE(key.front())) {
        return NodeMetadataKeyIssueUVE::StartsWithDigit;
    }
    const bool allIdentifierCharacters = std::all_of(key.cbegin(), key.cend(), [](const char character) {
        return IsAsciiLetterUVE(character) || IsAsciiDigitUVE(character) || character == '_';
    });
    if (!allIdentifierCharacters) {
        return NodeMetadataKeyIssueUVE::InvalidCharacter;
    }
    const bool duplicate = std::any_of(existing.entries.cbegin(), existing.entries.cend(),
                                       [key, ignoredKey](const NodeMetadataEntryUVE& entry) {
                                           return entry.key == key && entry.key != ignoredKey;
                                       });
    return duplicate ? NodeMetadataKeyIssueUVE::Duplicate : NodeMetadataKeyIssueUVE::None;
}

std::string_view DescribeNodeMetadataKeyIssueUVE(const NodeMetadataKeyIssueUVE issue) noexcept {
    switch (issue) {
        case NodeMetadataKeyIssueUVE::None:
            return {};
        case NodeMetadataKeyIssueUVE::Empty:
            return "Enter a name.";
        case NodeMetadataKeyIssueUVE::TooLong:
            return "Names are limited to 128 characters.";
        case NodeMetadataKeyIssueUVE::InvalidCharacter:
            return "Use only letters, digits and underscores.";
        case NodeMetadataKeyIssueUVE::StartsWithDigit:
            return "A name cannot start with a digit.";
        case NodeMetadataKeyIssueUVE::Duplicate:
            return "This node already has metadata with that name.";
    }
    return {};
}

bool IsNodeMetadataComponentValidUVE(const NodeMetadataComponentUVE& component) noexcept {
    if (component.entries.size() > kMaximumNodeMetadataEntriesUVE) {
        return false;
    }
    for (const NodeMetadataEntryUVE& entry : component.entries) {
        // A key must occur exactly once across the whole list; counting occurrences catches every
        // duplicate, including one whose twin comes later in the list.
        const std::size_t occurrences = static_cast<std::size_t>(
            std::count_if(component.entries.cbegin(), component.entries.cend(),
                          [&entry](const NodeMetadataEntryUVE& other) { return other.key == entry.key; }));
        // Structural rules only - non-empty, bounded, unique - not the identifier rule new keys are
        // authored under. Metadata saved before that rule existed may hold a key like "door north";
        // rejecting it here would fail the component, and a failed component rolls back the whole
        // scene load. Such keys stay loadable and editable; only new ones must be identifiers.
        if (occurrences != 1U || entry.key.empty() || entry.key.size() > kMaximumNodeMetadataKeyBytesUVE ||
            !Core::IsVariantWithinBoundsUVE(entry.value)) {
            return false;
        }
    }
    return true;
}

const Core::VariantUVE* FindNodeMetadataUVE(const NodeMetadataComponentUVE& component,
                                            const std::string_view key) noexcept {
    const auto iterator = std::find_if(component.entries.cbegin(), component.entries.cend(),
                                       [key](const NodeMetadataEntryUVE& entry) { return entry.key == key; });
    return iterator == component.entries.cend() ? nullptr : &iterator->value;
}

} // namespace UVE::Scene
