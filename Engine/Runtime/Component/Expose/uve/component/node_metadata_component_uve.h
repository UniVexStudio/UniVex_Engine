// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Scene {

/// One authored key/value pair on an entity.
struct NodeMetadataEntryUVE final {
    std::string key;
    std::string value;

    [[nodiscard]] bool operator==(const NodeMetadataEntryUVE&) const = default;
};

/// Arbitrary authored key/value pairs attached to an entity, saved with the scene.
///
/// WHY AN ENGINE NEEDS THIS. A game always has per-instance data the engine has no type for: which
/// door a key opens, how much a chest holds, which wave a spawner belongs to. Without somewhere to
/// put it, every one of those becomes either a new component type that exists for one level, or a
/// value smuggled into the entity's name. Both are worse than a string map, and the second is how
/// scenes quietly become unmaintainable.
///
/// Values are strings rather than a variant. A variant would need a type tag per entry, a parser
/// per type and a serializer per type, and the caller still has to know which type it expects - so
/// it would add machinery without removing the one thing that actually matters, which is the
/// caller knowing what it asked for.
struct NodeMetadataComponentUVE final {
    /// A vector rather than a map: entries keep the order they were authored in, which is what an
    /// inspector shows and what makes a saved scene diff cleanly. Lookup is linear over a handful
    /// of entries, which is the size this is for.
    std::vector<NodeMetadataEntryUVE> entries;
};

/// Bounds, so a malformed or hostile scene file cannot make one entity carry unbounded data.
inline constexpr std::size_t kMaximumNodeMetadataEntriesUVE = 64U;
inline constexpr std::size_t kMaximumNodeMetadataKeyBytesUVE = 128U;
inline constexpr std::size_t kMaximumNodeMetadataValueBytesUVE = 1024U;

/// Rejects an empty or oversized key, an oversized value, a duplicate key - which would make a
/// lookup's answer depend on iteration order - or more entries than the bound above.
[[nodiscard]] bool IsNodeMetadataComponentValidUVE(const NodeMetadataComponentUVE& component) noexcept;

/// Returns the value for `key`, or null when the entity has no such entry. Null is distinct from
/// an entry whose value is the empty string, which is a legitimate authored value.
[[nodiscard]] const std::string* FindNodeMetadataUVE(const NodeMetadataComponentUVE& component,
                                                     std::string_view key) noexcept;

} // namespace UVE::Scene
