// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace UVE::Utilities {

/// FNV-1a 64-bit offset basis (Fowler/Noll/Vo). The starting state every hasher below uses
/// unless a legacy format says otherwise.
inline constexpr std::uint64_t kFnv1a64OffsetBasisUVE = 14695981039346656037ULL;
/// FNV-1a 64-bit prime.
inline constexpr std::uint64_t kFnv1a64PrimeUVE = 1099511628211ULL;

/// The offset basis two older call sites actually shipped with: the trailing 7 of the true basis
/// was dropped when the constants were copied (`...5603` instead of `...56037`). One of those
/// sites is a persisted save-file checksum, so its values are a format and cannot change — both
/// sites pass this seed explicitly rather than the true basis. New code must use the default.
inline constexpr std::uint64_t kFnv1a64LegacyOffsetBasisUVE = 1469598103934665603ULL;

/// Mixes `value` into `seed`, in place. This is the boost `hash_combine` formula
/// (`seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2)`), matching the two hand-rolled
/// copies it replaced in `ArchetypeSignatureUVE` and the pose-graph cache key.
///
/// For in-memory hash tables only: `std::size_t` is platform-width, so combined values differ
/// between 32- and 64-bit builds. Anything persisted, sent across processes, or embedded in a
/// file format wants `Fnv1a64UVE` below, whose output is a fixed-width `std::uint64_t`.
constexpr void HashCombineUVE(std::size_t& seed, const std::size_t value) noexcept {
    seed ^= value + static_cast<std::size_t>(0x9e3779b9U) + (seed << 6U) + (seed >> 2U);
}

/// Incremental FNV-1a over a byte stream: xor each byte into the state, then multiply by the
/// prime. FNV-1a is stable across processes, builds, and standard-library implementations,
/// unlike `std::hash` — which is why the content fingerprint, the save checksum, and the cache
/// file-name hashes are all built on it.
///
/// The hasher is deliberately dumb: it hashes exactly the bytes it is given, with no framing.
/// Callers that hash structured data must delimit fields themselves (length-prefix variable
/// fields), or `"ab" + "c"` collides with `"a" + "bc"`.
///
/// Byte order is the caller's business too: hashing a `std::uint64_t` directly feeds native
/// endian bytes. Fine for same-machine caches; a portable format must serialize first (Tier 1.8).
class Fnv1a64UVE final {
  public:
    /// Starts from the true FNV-1a basis. Pass an explicit seed only to reproduce a legacy
    /// format's values (see `kFnv1a64LegacyOffsetBasisUVE`).
    constexpr explicit Fnv1a64UVE(const std::uint64_t seed = kFnv1a64OffsetBasisUVE) noexcept
        : m_state(seed) {}

    /// Feeds `size` bytes starting at `data`. A null `data` with a zero `size` is a no-op;
    /// anything else null is a caller bug. Runtime-only: traversing a `void*` is not a constant
    /// expression, so compile-time hashing goes through `AppendString` instead.
    void AppendBytes(const void* const data, const std::size_t size) noexcept {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t index = 0U; index < size; ++index) {
            MixByte(m_state, bytes[index]);
        }
    }

    /// Feeds `text`'s bytes, without any terminator. `constexpr`: string hashing never touches
    /// a `void*`, so known-answer vectors can be `static_assert`ed.
    constexpr void AppendString(const std::string_view text) noexcept {
        for (const char character : text) {
            MixByte(m_state, static_cast<unsigned char>(character));
        }
    }

    /// The running digest. Calling this does not reset the hasher — appending more bytes
    /// continues the same stream.
    [[nodiscard]] constexpr std::uint64_t Digest() const noexcept { return m_state; }

  private:
    /// One FNV-1a step. Both feed paths share it, so there is exactly one spelling of the
    /// xor-then-multiply order in the engine.
    static constexpr void MixByte(std::uint64_t& state, const unsigned char byte) noexcept {
        state ^= byte;
        state *= kFnv1a64PrimeUVE;
    }

    std::uint64_t m_state;
};

/// One-shot FNV-1a over `size` bytes. Same contract as `Fnv1a64UVE::AppendBytes`;
/// runtime-only for the same `void*` reason (`inline` because, unlike its `constexpr` sibling
/// below, it has no implicit inline).
[[nodiscard]] inline std::uint64_t HashBytesUVE(const void* const data, const std::size_t size,
                                                const std::uint64_t seed = kFnv1a64OffsetBasisUVE) noexcept {
    Fnv1a64UVE hasher{seed};
    hasher.AppendBytes(data, size);
    return hasher.Digest();
}

/// One-shot FNV-1a over `text`'s bytes, without any terminator. `constexpr` via the
/// `AppendString` path (not `HashBytesUVE`, which cannot be).
[[nodiscard]] constexpr std::uint64_t HashStringUVE(const std::string_view text,
                                                    const std::uint64_t seed = kFnv1a64OffsetBasisUVE) noexcept {
    Fnv1a64UVE hasher{seed};
    hasher.AppendString(text);
    return hasher.Digest();
}

} // namespace UVE::Utilities
