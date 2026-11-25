// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "object_metadata_component.h"

#include <algorithm>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsAsciiLetter(const char character) noexcept {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

[[nodiscard]] bool IsAsciiDigit(const char character) noexcept {
    return character >= '0' && character <= '9';
}

} // namespace

ObjectMetadataKeyIssue ValidateObjectMetadataKey(const std::string_view key,
                                                   const ObjectMetadataComponent& existing,
                                                   const std::string_view ignoredKey) noexcept {
    if (key.empty()) {
        return ObjectMetadataKeyIssue::Empty;
    }
    if (key.size() > kMaximumObjectMetadataKeyBytes) {
        return ObjectMetadataKeyIssue::TooLong;
    }
    if (IsAsciiDigit(key.front())) {
        return ObjectMetadataKeyIssue::StartsWithDigit;
    }
    const bool allIdentifierCharacters = std::all_of(key.cbegin(), key.cend(), [](const char character) {
        return IsAsciiLetter(character) || IsAsciiDigit(character) || character == '_';
    });
    if (!allIdentifierCharacters) {
        return ObjectMetadataKeyIssue::InvalidCharacter;
    }
    const bool duplicate = std::any_of(existing.entries.cbegin(), existing.entries.cend(),
                                       [key, ignoredKey](const ObjectMetadataEntry& entry) {
                                           return entry.key == key && entry.key != ignoredKey;
                                       });
    return duplicate ? ObjectMetadataKeyIssue::Duplicate : ObjectMetadataKeyIssue::None;
}

std::string_view DescribeObjectMetadataKeyIssue(const ObjectMetadataKeyIssue issue) noexcept {
    switch (issue) {
        case ObjectMetadataKeyIssue::None:
            return {};
        case ObjectMetadataKeyIssue::Empty:
            return "Enter a name.";
        case ObjectMetadataKeyIssue::TooLong:
            return "Names are limited to 128 characters.";
        case ObjectMetadataKeyIssue::InvalidCharacter:
            return "Use only letters, digits and underscores.";
        case ObjectMetadataKeyIssue::StartsWithDigit:
            return "A name cannot start with a digit.";
        case ObjectMetadataKeyIssue::Duplicate:
            return "This object already has metadata with that name.";
    }
    return {};
}

bool IsObjectMetadataComponentValid(const ObjectMetadataComponent& component) noexcept {
    if (component.entries.size() > kMaximumObjectMetadataEntries) {
        return false;
    }
    for (const ObjectMetadataEntry& entry : component.entries) {
        // A key must occur exactly once across the whole list; counting occurrences catches every
        // duplicate, including one whose twin comes later in the list.
        const std::size_t occurrences = static_cast<std::size_t>(
            std::count_if(component.entries.cbegin(), component.entries.cend(),
                          [&entry](const ObjectMetadataEntry& other) { return other.key == entry.key; }));
        // Structural rules only - non-empty, bounded, unique - not the identifier rule new keys are
        // authored under. Metadata saved before that rule existed may hold a key like "door north";
        // rejecting it here would fail the component, and a failed component rolls back the whole
        // scene load. Such keys stay loadable and editable; only new ones must be identifiers.
        if (occurrences != 1U || entry.key.empty() || entry.key.size() > kMaximumObjectMetadataKeyBytes ||
            !Core::IsVariantWithinBounds(entry.value)) {
            return false;
        }
    }
    return true;
}

const Core::Variant* FindObjectMetadata(const ObjectMetadataComponent& component,
                                            const std::string_view key) noexcept {
    const auto iterator = std::find_if(component.entries.cbegin(), component.entries.cend(),
                                       [key](const ObjectMetadataEntry& entry) { return entry.key == key; });
    return iterator == component.entries.cend() ? nullptr : &iterator->value;
}

} // namespace UVE::Scene
