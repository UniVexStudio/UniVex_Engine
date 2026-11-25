// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string_view>
#include <type_traits>

namespace UVE::Strings {

/// `StringId` is an interned name: a cheap-to-copy 32-bit id standing in for a string.
/// Comparison and hashing are integer operations; the string itself lives exactly once in a
/// process-wide table and can always be recovered with `ToString()`/`ToCString()`.
///
/// Constructing from text interns it (deduping against every id minted so far); copying an id
/// copies one `uint32_t` and never touches the table. The table never shrinks and interned
/// strings never move, so ids, views and C strings stay valid for the rest of the process.
///
/// The text constructors are deliberately implicit: call sites keep passing literals and
/// `std::string`s exactly as before (`FindType("component.transform")`), and read paths
/// compare integers instead of strings. That ergonomics comes with one hard rule — intern
/// only BOUNDED vocabularies (type names, property names, drawer ids). Every unique string is
/// kept forever, so interning unbounded input (user text, file contents, generated names)
/// is a permanent memory leak by construction. Three overloads (`string_view`, `const char*`,
/// `const std::string&`) exist so every common spelling converts in a single user-defined
/// step; there is intentionally no implicit conversion BACK to a string (use `ToString()`),
/// and no `operator<` (indices are arrival order, meaningless for sorting — sort by
/// `ToString()` when alphabetical order matters).
///
/// The default-constructed id is the empty string's id. There is no other invalid state:
/// every `StringId` names a real table entry.
///
/// Thread-safety: interning and string recovery lock a shared mutex (they run on cold paths:
/// registration, serialization, display). Comparison, hashing, copying and `IsEmpty()` are
/// lock-free.
class StringId final {
public:
    StringId() noexcept = default;

    StringId(std::string_view text);
    StringId(const char* text);
    StringId(const std::string& text);

    /// The interned text. Valid for the rest of the process (the table never shrinks).
    [[nodiscard]] std::string_view ToString() const;
    /// The interned text as a null-terminated string, for `%s` call sites. Valid forever.
    [[nodiscard]] const char* ToCString() const;

    /// The integer identity. Stable within a process; meaningless across processes (arrival
    /// order), so never persist it — persist `ToString()`.
    [[nodiscard]] std::uint32_t GetIndex() const noexcept { return m_index; }
    [[nodiscard]] bool IsEmpty() const noexcept { return m_index == 0U; }

private:
    std::uint32_t m_index = 0U;
};

static_assert(std::is_trivially_copyable_v<StringId>,
              "StringId must stay a cheap-to-copy integer handle.");

[[nodiscard]] inline bool operator==(const StringId& lhs, const StringId& rhs) noexcept {
    return lhs.GetIndex() == rhs.GetIndex();
}

[[nodiscard]] inline bool operator!=(const StringId& lhs, const StringId& rhs) noexcept {
    return !(lhs == rhs);
}

std::ostream& operator<<(std::ostream& stream, const StringId& id);

}  // namespace UVE::Strings

template <>
struct std::hash<UVE::Strings::StringId> {
    [[nodiscard]] std::size_t operator()(const UVE::Strings::StringId& id) const noexcept {
        return std::hash<std::uint32_t>{}(id.GetIndex());
    }
};
