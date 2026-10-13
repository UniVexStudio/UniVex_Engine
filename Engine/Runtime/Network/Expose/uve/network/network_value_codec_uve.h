// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
//
// Scope: the shared wire encoding for script values - one tag byte plus the payload,
// little-endian throughout. RPC arguments and results ride it, replication properties ride
// it, and the packet inspector mirrors it independently (a dissector verifies; sharing the
// decoder would blind it to codec bugs). Tags: Nil 0x00, Bool 0x01 (+1 byte), Int 0x02
// (+u64), Float 0x03 (+u64 double bits), Str 0x04 (+u16 length + bytes), Vec3 0x05 (+3 u64),
// List 0x06 / Tuple 0x08 (+u16 count + items), Map 0x07 (+u16 pair count + keys/values).
// Object refs never cross (host-local ids); nesting bottoms out at kWireValueMaxDepthUVE;
// over-long strings and over-count collections refuse - all-or-nothing, every direction.
//
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "uve/uvscript/uvscript_value_uve.h"

namespace UVE::Network {

/// Deepest value nesting the codec carries each way. Past it, encodes refuse and decodes
/// fail - all-or-nothing, every direction.
inline constexpr std::size_t kWireValueMaxDepthUVE = 64U;

/// Appends one value: a tag byte plus the payload. False when the value cannot cross - an
/// object ref, over-deep nesting, an over-long string, or a map with an unpaired item. On
/// false the buffer may hold a partial value: the caller must discard the frame, never
/// send it.
bool AppendWireValueUVE(std::vector<std::byte>& out, const UVScript::ValueUVE& value,
                        const std::size_t depth);
/// Reads a u16 length plus that many raw bytes as a string. Strings are opaque UTF-8 -
/// never validated, only bounded.
[[nodiscard]] std::optional<std::string> ReadWireStringUVE(const std::vector<std::byte>& in,
                                                           std::size_t& offset);
/// Reads one value, tag for tag what the encoder wrote. Any truncation, unknown tag, or
/// over-deep nesting fails the whole read - values never decode partially.
[[nodiscard]] std::optional<UVScript::ValueUVE> ReadWireValueUVE(const std::vector<std::byte>& in,
                                                                 std::size_t& offset,
                                                                 const std::size_t depth);

} // namespace UVE::Network
