// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "uve/object/variant_uve.h"

namespace UVE::Scene {

/// One authored, typed key/value pair on an entity.
struct NodeMetadataEntryUVE final {
    std::string key;
    Core::VariantUVE value;

    [[nodiscard]] bool operator==(const NodeMetadataEntryUVE&) const = default;
};

/// Arbitrary authored key/value pairs attached to an entity, saved with the scene.
///
/// WHY AN ENGINE NEEDS THIS. A game always has per-instance data the engine has no type for: which
/// door a key opens, how much a chest holds, which wave a spawner belongs to. Without somewhere to
/// put it, every one of those becomes either a new component type that exists for one level, or a
/// value smuggled into the entity's name. Both are worse than a property bag.
///
/// WHY TYPED. Values are VariantUVE rather than strings, so "charges" is an int a script can do
/// arithmetic on, "spawn_offset" is a Vector3 the inspector edits with three fields, and a node
/// reference is a NodePath the editor can resolve - instead of text every reader has to parse and
/// every writer can get wrong.
struct NodeMetadataComponentUVE final {
    /// A vector rather than a map: entries keep the order they were authored in, which is what an
    /// inspector shows and what makes a saved scene diff cleanly. Lookup is linear over a handful
    /// of entries, which is the size this is for.
    std::vector<NodeMetadataEntryUVE> entries;
};

/// Bounds, so a malformed or hostile scene file cannot make one entity carry unbounded data.
inline constexpr std::size_t kMaximumNodeMetadataEntriesUVE = 256U;
inline constexpr std::size_t kMaximumNodeMetadataKeyBytesUVE = 128U;

/// Why a metadata key is not usable, so the editor can say so while the author is still typing
/// rather than refusing silently after they press Add.
enum class NodeMetadataKeyIssueUVE : std::uint8_t {
    None = 0,
    Empty,
    TooLong,
    /// Something other than a letter, a digit or an underscore. Keys are identifiers so a script
    /// can name them without quoting, the same rule that makes them safe in every file format.
    InvalidCharacter,
    StartsWithDigit,
    Duplicate,
};

/// Checks `key` against the identifier rules above and against `existing` entries (a key equal to
/// `ignoredKey` is not counted as a duplicate - that is how renaming an entry to itself is allowed).
[[nodiscard]] NodeMetadataKeyIssueUVE ValidateNodeMetadataKeyUVE(std::string_view key,
                                                                 const NodeMetadataComponentUVE& existing,
                                                                 std::string_view ignoredKey = {}) noexcept;

/// A short human-readable reason for `issue`, for the editor to show next to the name field.
[[nodiscard]] std::string_view DescribeNodeMetadataKeyIssueUVE(NodeMetadataKeyIssueUVE issue) noexcept;

/// Rejects an empty, oversized or duplicate key - a duplicate would make FindNodeMetadataUVE's answer
/// depend on iteration order - any value outside the Variant bounds, or more entries than the bound
/// above. Deliberately does not apply the identifier rule: that governs keys authored from now on,
/// and applying it to loaded data would make an older scene unloadable.
[[nodiscard]] bool IsNodeMetadataComponentValidUVE(const NodeMetadataComponentUVE& component) noexcept;

/// Returns the value for `key`, or null when the entity has no such entry. Null is distinct from an
/// entry whose value is empty, which is a legitimate authored value.
[[nodiscard]] const Core::VariantUVE* FindNodeMetadataUVE(const NodeMetadataComponentUVE& component,
                                                          std::string_view key) noexcept;

} // namespace UVE::Scene
