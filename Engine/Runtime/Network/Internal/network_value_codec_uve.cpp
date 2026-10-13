// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_value_codec_uve.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "uve/utilities/binary_buffer_uve.h"

namespace UVE::Network {
namespace {

enum class WireValueTagUVE : std::uint8_t {
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

[[nodiscard]] std::byte TagByteUVE(const WireValueTagUVE tag) {
    return static_cast<std::byte>(static_cast<std::uint8_t>(tag));
}

} // namespace

// One value onto the wire: a tag byte plus the payload (counts and lengths u16,
// integers and doubles little-endian). False when the value cannot cross - an object
// ref, an over-deep nesting, an over-long string, or a map with an unpaired item.
bool AppendWireValueUVE(std::vector<std::byte>& out, const UVScript::ValueUVE& value,
                       const std::size_t depth) {
    return std::visit(
        [&](const auto& held) -> bool {
            using HeldUVE = std::decay_t<decltype(held)>;
            if constexpr (std::is_same_v<HeldUVE, std::monostate>) {
                out.push_back(TagByteUVE(WireValueTagUVE::Nil));
                return true;
            } else if constexpr (std::is_same_v<HeldUVE, bool>) {
                out.push_back(TagByteUVE(WireValueTagUVE::Bool));
                out.push_back(held ? static_cast<std::byte>(0x01) : static_cast<std::byte>(0x00));
                return true;
            } else if constexpr (std::is_same_v<HeldUVE, std::int64_t>) {
                out.push_back(TagByteUVE(WireValueTagUVE::Int));
                Utilities::AppendUint64LeUVE(out, static_cast<std::uint64_t>(held));
                return true;
            } else if constexpr (std::is_same_v<HeldUVE, double>) {
                out.push_back(TagByteUVE(WireValueTagUVE::Float));
                Utilities::AppendUint64LeUVE(out, std::bit_cast<std::uint64_t>(held));
                return true;
            } else if constexpr (std::is_same_v<HeldUVE, std::string>) {
                if (held.size() > 0xFFFFU) {
                    return false;
                }
                out.push_back(TagByteUVE(WireValueTagUVE::Str));
                Utilities::AppendUint16LeUVE(out, static_cast<std::uint16_t>(held.size()));
                if (!held.empty()) {
                    Utilities::AppendBytesUVE(out, held.data(), held.size());
                }
                return true;
            } else if constexpr (std::is_same_v<HeldUVE, UVScript::Vec3ValueUVE>) {
                out.push_back(TagByteUVE(WireValueTagUVE::Vec3));
                Utilities::AppendUint64LeUVE(out, std::bit_cast<std::uint64_t>(held.x));
                Utilities::AppendUint64LeUVE(out, std::bit_cast<std::uint64_t>(held.y));
                Utilities::AppendUint64LeUVE(out, std::bit_cast<std::uint64_t>(held.z));
                return true;
            } else if constexpr (std::is_same_v<HeldUVE, UVScript::ObjectRefUVE>) {
                // An object ref names a host-local object; the far side's ids differ, so the
                // value is meaningless there. Refuse to send rather than send confusion.
                return false;
            } else {
                static_assert(
                    std::is_same_v<HeldUVE, std::shared_ptr<const UVScript::CollectionValueUVE>>);
                if (held == nullptr || depth == kWireValueMaxDepthUVE) {
                    return false;
                }
                const bool isMap = held->kind == UVScript::CollectionValueUVE::KindUVE::Map;
                const bool isTuple = held->kind == UVScript::CollectionValueUVE::KindUVE::Tuple;
                if (isMap && held->items.size() % 2U != 0U) {
                    return false;
                }
                const std::size_t count = isMap ? held->items.size() / 2U : held->items.size();
                if (count > 0xFFFFU) {
                    return false;
                }
                out.push_back(TagByteUVE(isMap ? WireValueTagUVE::Map
                                              : (isTuple ? WireValueTagUVE::Tuple : WireValueTagUVE::List)));
                Utilities::AppendUint16LeUVE(out, static_cast<std::uint16_t>(count));
                for (const UVScript::ValueUVE& item : held->items) {
                    if (!AppendWireValueUVE(out, item, depth + 1U)) {
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
[[nodiscard]] std::optional<std::string> ReadWireStringUVE(const std::vector<std::byte>& in,
                                                          std::size_t& offset) {
    std::uint16_t length = 0U;
    if (!Utilities::ReadUint16LeFromBufferUVE(in, offset, length) || length > in.size() - offset) {
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
[[nodiscard]] std::optional<UVScript::ValueUVE> ReadWireValueUVE(const std::vector<std::byte>& in,
                                                                std::size_t& offset,
                                                                const std::size_t depth) {
    if (offset >= in.size()) {
        return std::nullopt;
    }
    const auto tag = static_cast<WireValueTagUVE>(std::to_integer<std::uint8_t>(in[offset++]));
    switch (tag) {
        case WireValueTagUVE::Nil:
            return UVScript::ValueUVE{};
        case WireValueTagUVE::Bool: {
            if (offset >= in.size()) {
                return std::nullopt;
            }
            const bool value = in[offset++] != static_cast<std::byte>(0x00);
            return UVScript::ValueUVE{value};
        }
        case WireValueTagUVE::Int: {
            std::uint64_t bits = 0U;
            if (!Utilities::ReadUint64LeFromBufferUVE(in, offset, bits)) {
                return std::nullopt;
            }
            return UVScript::ValueUVE{static_cast<std::int64_t>(bits)};
        }
        case WireValueTagUVE::Float: {
            std::uint64_t bits = 0U;
            if (!Utilities::ReadUint64LeFromBufferUVE(in, offset, bits)) {
                return std::nullopt;
            }
            return UVScript::ValueUVE{std::bit_cast<double>(bits)};
        }
        case WireValueTagUVE::Str: {
            std::optional<std::string> text = ReadWireStringUVE(in, offset);
            if (!text.has_value()) {
                return std::nullopt;
            }
            return UVScript::ValueUVE{std::move(*text)};
        }
        case WireValueTagUVE::Vec3: {
            std::uint64_t x = 0U, y = 0U, z = 0U;
            if (!Utilities::ReadUint64LeFromBufferUVE(in, offset, x) ||
                !Utilities::ReadUint64LeFromBufferUVE(in, offset, y) ||
                !Utilities::ReadUint64LeFromBufferUVE(in, offset, z)) {
                return std::nullopt;
            }
            return UVScript::ValueUVE{UVScript::Vec3ValueUVE{
                std::bit_cast<double>(x), std::bit_cast<double>(y), std::bit_cast<double>(z)}};
        }
        case WireValueTagUVE::List:
        case WireValueTagUVE::Tuple: {
            if (depth == kWireValueMaxDepthUVE) {
                return std::nullopt;
            }
            std::uint16_t count = 0U;
            if (!Utilities::ReadUint16LeFromBufferUVE(in, offset, count)) {
                return std::nullopt;
            }
            std::vector<UVScript::ValueUVE> items;
            for (std::uint16_t i = 0U; i < count; ++i) {
                std::optional<UVScript::ValueUVE> item = ReadWireValueUVE(in, offset, depth + 1U);
                if (!item.has_value()) {
                    return std::nullopt;
                }
                items.push_back(std::move(*item));
            }
            if (tag == WireValueTagUVE::List) {
                return UVScript::MakeListValueUVE(std::move(items));
            }
            return UVScript::MakeTupleValueUVE(std::move(items));
        }
        case WireValueTagUVE::Map: {
            if (depth == kWireValueMaxDepthUVE) {
                return std::nullopt;
            }
            std::uint16_t pairs = 0U;
            if (!Utilities::ReadUint16LeFromBufferUVE(in, offset, pairs)) {
                return std::nullopt;
            }
            std::vector<UVScript::ValueUVE> items;
            for (std::uint16_t i = 0U; i < pairs; ++i) {
                std::optional<UVScript::ValueUVE> key = ReadWireValueUVE(in, offset, depth + 1U);
                std::optional<UVScript::ValueUVE> item = ReadWireValueUVE(in, offset, depth + 1U);
                if (!key.has_value() || !item.has_value()) {
                    return std::nullopt;
                }
                items.push_back(std::move(*key));
                items.push_back(std::move(*item));
            }
            return UVScript::MakeMapValueUVE(std::move(items));
        }
    }
    return std::nullopt;
}

} // namespace UVE::Network
