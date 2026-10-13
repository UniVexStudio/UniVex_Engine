// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string_view>
#include <type_traits>

namespace UVE::Strings {

/// `StringIdUVE` is an interned name: a cheap-to-copy 32-bit id standing in for a string.
/// Comparison and hashing are integer operations; the string itself lives exactly once in a
/// process-wide table and can always be recovered with `ToStringUVE()`/`ToCStringUVE()`.
///
/// Constructing from text interns it (deduping against every id minted so far); copying an id
/// copies one `uint32_t` and never touches the table. The table never shrinks and interned
/// strings never move, so ids, views and C strings stay valid for the rest of the process.
///
/// The text constructors are deliberately implicit: call sites keep passing literals and
/// `std::string`s exactly as before (`FindTypeUVE("component.transform")`), and read paths
/// compare integers instead of strings. That ergonomics comes with one hard rule — intern
/// only BOUNDED vocabularies (type names, property names, drawer ids). Every unique string is
/// kept forever, so interning unbounded input (user text, file contents, generated names)
/// is a permanent memory leak by construction. Three overloads (`string_view`, `const char*`,
/// `const std::string&`) exist so every common spelling converts in a single user-defined
/// step; there is intentionally no implicit conversion BACK to a string (use `ToStringUVE()`),
/// and no `operator<` (indices are arrival order, meaningless for sorting — sort by
/// `ToStringUVE()` when alphabetical order matters).
///
/// The default-constructed id is the empty string's id. There is no other invalid state:
/// every `StringIdUVE` names a real table entry.
///
/// Thread-safety: interning and string recovery lock a shared mutex (they run on cold paths:
/// registration, serialization, display). Comparison, hashing, copying and `IsEmptyUVE()` are
/// lock-free.
class StringIdUVE final {
public:
    StringIdUVE() noexcept = default;

    StringIdUVE(std::string_view text);
    StringIdUVE(const char* text);
    StringIdUVE(const std::string& text);

    /// The interned text. Valid for the rest of the process (the table never shrinks).
    [[nodiscard]] std::string_view ToStringUVE() const;
    /// The interned text as a null-terminated string, for `%s`/ImGui call sites. Valid forever.
    [[nodiscard]] const char* ToCStringUVE() const;

    /// The integer identity. Stable within a process; meaningless across processes (arrival
    /// order), so never persist it — persist `ToStringUVE()`.
    [[nodiscard]] std::uint32_t GetIndexUVE() const noexcept { return m_index; }
    [[nodiscard]] bool IsEmptyUVE() const noexcept { return m_index == 0U; }

private:
    std::uint32_t m_index = 0U;
};

static_assert(std::is_trivially_copyable_v<StringIdUVE>,
              "StringIdUVE must stay a cheap-to-copy integer handle.");

[[nodiscard]] inline bool operator==(const StringIdUVE& lhs, const StringIdUVE& rhs) noexcept {
    return lhs.GetIndexUVE() == rhs.GetIndexUVE();
}

[[nodiscard]] inline bool operator!=(const StringIdUVE& lhs, const StringIdUVE& rhs) noexcept {
    return !(lhs == rhs);
}

std::ostream& operator<<(std::ostream& stream, const StringIdUVE& id);

}  // namespace UVE::Strings

template <>
struct std::hash<UVE::Strings::StringIdUVE> {
    [[nodiscard]] std::size_t operator()(const UVE::Strings::StringIdUVE& id) const noexcept {
        return std::hash<std::uint32_t>{}(id.GetIndexUVE());
    }
};
