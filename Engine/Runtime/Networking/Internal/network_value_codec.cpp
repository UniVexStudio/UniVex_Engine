// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_value_codec.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "binary_buffer.h"

namespace UVE::Network {
namespace {

enum class WireValueTag : std::uint8_t {
    Nil = 0x00,
    Bool = 0x01,
    Int = 0x02,
    Float = 0x03,
    Str = 0x04,
    Vec3 = 0x05,
    List = 0x06,
    Map = 0x07,
    Tuple = 0x08
};

[[nodiscard]] std::byte TagByte(const WireValueTag tag) {
    return static_cast<std::byte>(static_cast<std::uint8_t>(tag));
}

} // namespace

// One value onto the wire: a tag byte plus the payload (counts and lengths u16,
// integers and doubles little-endian). False when the value cannot cross - an object
// ref, an over-deep nesting, an over-long string, or a map with an unpaired item.
bool AppendWireValue(std::vector<std::byte>& out, const Blueprint::Value& value,
                       const std::size_t depth) {
    return std::visit(
        [&](const auto& held) -> bool {
            using Held = std::decay_t<decltype(held)>;
            if constexpr (std::is_same_v<Held, std::monostate>) {
                out.push_back(TagByte(WireValueTag::Nil));
                return true;
            } else if constexpr (std::is_same_v<Held, bool>) {
                out.push_back(TagByte(WireValueTag::Bool));
                out.push_back(held ? static_cast<std::byte>(0x01) : static_cast<std::byte>(0x00));
                return true;
            } else if constexpr (std::is_same_v<Held, std::int64_t>) {
                out.push_back(TagByte(WireValueTag::Int));
                Utilities::AppendUint64Le(out, static_cast<std::uint64_t>(held));
                return true;
            } else if constexpr (std::is_same_v<Held, double>) {
                out.push_back(TagByte(WireValueTag::Float));
                Utilities::AppendUint64Le(out, std::bit_cast<std::uint64_t>(held));
                return true;
            } else if constexpr (std::is_same_v<Held, std::string>) {
                if (held.size() > 0xFFFFU) {
                    return false;
                }
                out.push_back(TagByte(WireValueTag::Str));
                Utilities::AppendUint16Le(out, static_cast<std::uint16_t>(held.size()));
                if (!held.empty()) {
                    Utilities::AppendBytes(out, held.data(), held.size());
                }
                return true;
            } else if constexpr (std::is_same_v<Held, Blueprint::Vec3Value>) {
                out.push_back(TagByte(WireValueTag::Vec3));
                Utilities::AppendUint64Le(out, std::bit_cast<std::uint64_t>(held.x));
                Utilities::AppendUint64Le(out, std::bit_cast<std::uint64_t>(held.y));
                Utilities::AppendUint64Le(out, std::bit_cast<std::uint64_t>(held.z));
                return true;
            } else if constexpr (std::is_same_v<Held, Blueprint::ObjectRef>) {
                // An object ref names a host-local object; the far side's ids differ, so the
                // value is meaningless there. Refuse to send rather than send confusion.
                return false;
            } else {
                static_assert(
                    std::is_same_v<Held, std::shared_ptr<const Blueprint::CollectionValue>>);
                if (held == nullptr || depth == kWireValueMaxDepth) {
                    return false;
                }
                const bool isMap = held->kind == Blueprint::CollectionValue::Kind::Map;
                const bool isTuple = held->kind == Blueprint::CollectionValue::Kind::Tuple;
                if (isMap && held->items.size() % 2U != 0U) {
                    return false;
                }
                const std::size_t count = isMap ? held->items.size() / 2U : held->items.size();
                if (count > 0xFFFFU) {
                    return false;
                }
                out.push_back(TagByte(isMap ? WireValueTag::Map
                                              : (isTuple ? WireValueTag::Tuple : WireValueTag::List)));
                Utilities::AppendUint16Le(out, static_cast<std::uint16_t>(count));
                for (const Blueprint::Value& item : held->items) {
                    if (!AppendWireValue(out, item, depth + 1U)) {
                        return false;
                    }
                }
                return true;
            }
        },
        value);
}

// A u16 length plus that many raw bytes as a string. Used for method names and str values;
// strings are opaque UTF-8 - never validated, only bounded.
[[nodiscard]] std::optional<std::string> ReadWireString(const std::vector<std::byte>& in,
                                                          std::size_t& offset) {
    std::uint16_t length = 0U;
    if (!Utilities::ReadUint16LeFromBuffer(in, offset, length) || length > in.size() - offset) {
        return std::nullopt;
    }
    std::string text;
    if (length > 0U) {
        text.assign(reinterpret_cast<const char*>(&in[offset]), length);
        offset += length;
    }
    return text;
}

// One value off the wire. Mirrors the encoder tag for tag; any truncation, unknown tag, or
// over-deep nesting fails the whole frame - values never decode partially.
[[nodiscard]] std::optional<Blueprint::Value> ReadWireValue(const std::vector<std::byte>& in,
                                                                std::size_t& offset,
                                                                const std::size_t depth) {
    if (offset >= in.size()) {
        return std::nullopt;
    }
    const auto tag = static_cast<WireValueTag>(std::to_integer<std::uint8_t>(in[offset++]));
    switch (tag) {
        case WireValueTag::Nil:
            return Blueprint::Value{};
        case WireValueTag::Bool: {
            if (offset >= in.size()) {
                return std::nullopt;
            }
            const bool value = in[offset++] != static_cast<std::byte>(0x00);
            return Blueprint::Value{value};
        }
        case WireValueTag::Int: {
            std::uint64_t bits = 0U;
            if (!Utilities::ReadUint64LeFromBuffer(in, offset, bits)) {
                return std::nullopt;
            }
            return Blueprint::Value{static_cast<std::int64_t>(bits)};
        }
        case WireValueTag::Float: {
            std::uint64_t bits = 0U;
            if (!Utilities::ReadUint64LeFromBuffer(in, offset, bits)) {
                return std::nullopt;
            }
            return Blueprint::Value{std::bit_cast<double>(bits)};
        }
        case WireValueTag::Str: {
            std::optional<std::string> text = ReadWireString(in, offset);
            if (!text.has_value()) {
                return std::nullopt;
            }
            return Blueprint::Value{std::move(*text)};
        }
        case WireValueTag::Vec3: {
            std::uint64_t x = 0U, y = 0U, z = 0U;
            if (!Utilities::ReadUint64LeFromBuffer(in, offset, x) ||
                !Utilities::ReadUint64LeFromBuffer(in, offset, y) ||
                !Utilities::ReadUint64LeFromBuffer(in, offset, z)) {
                return std::nullopt;
            }
            return Blueprint::Value{Blueprint::Vec3Value{
                std::bit_cast<double>(x), std::bit_cast<double>(y), std::bit_cast<double>(z)}};
        }
        case WireValueTag::List:
        case WireValueTag::Tuple: {
            if (depth == kWireValueMaxDepth) {
                return std::nullopt;
            }
            std::uint16_t count = 0U;
            if (!Utilities::ReadUint16LeFromBuffer(in, offset, count)) {
                return std::nullopt;
            }
            std::vector<Blueprint::Value> items;
            for (std::uint16_t i = 0U; i < count; ++i) {
                std::optional<Blueprint::Value> item = ReadWireValue(in, offset, depth + 1U);
                if (!item.has_value()) {
                    return std::nullopt;
                }
                items.push_back(std::move(*item));
            }
            if (tag == WireValueTag::List) {
                return Blueprint::MakeListValue(std::move(items));
            }
            return Blueprint::MakeTupleValue(std::move(items));
        }
        case WireValueTag::Map: {
            if (depth == kWireValueMaxDepth) {
                return std::nullopt;
            }
            std::uint16_t pairs = 0U;
            if (!Utilities::ReadUint16LeFromBuffer(in, offset, pairs)) {
                return std::nullopt;
            }
            std::vector<Blueprint::Value> items;
            for (std::uint16_t i = 0U; i < pairs; ++i) {
                std::optional<Blueprint::Value> key = ReadWireValue(in, offset, depth + 1U);
                std::optional<Blueprint::Value> item = ReadWireValue(in, offset, depth + 1U);
                if (!key.has_value() || !item.has_value()) {
                    return std::nullopt;
                }
                items.push_back(std::move(*key));
                items.push_back(std::move(*item));
            }
            return Blueprint::MakeMapValue(std::move(items));
        }
    }
    return std::nullopt;
}

} // namespace UVE::Network
