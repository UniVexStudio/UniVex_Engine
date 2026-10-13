// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/utilities/binary_buffer_uve.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Utilities::Tests {
namespace {

TEST(BinaryBufferUVETest, LeIntegers_PinLittleEndianBytes) {
    std::vector<std::byte> buffer;
    AppendUint16LeUVE(buffer, std::uint16_t{0x0102});
    AppendUint32LeUVE(buffer, 0x01020304U);
    AppendUint64LeUVE(buffer, 0x0102030405060708ULL);
    const std::vector<std::byte> expected{std::byte{0x02}, std::byte{0x01}, std::byte{0x04},
                                          std::byte{0x03}, std::byte{0x02}, std::byte{0x01},
                                          std::byte{0x08}, std::byte{0x07}, std::byte{0x06},
                                          std::byte{0x05}, std::byte{0x04}, std::byte{0x03},
                                          std::byte{0x02}, std::byte{0x01}};
    EXPECT_EQ(buffer, expected);
}

TEST(BinaryBufferUVETest, BeIntegers_PinBigEndianBytes) {
    std::vector<std::byte> buffer;
    AppendUint16BeUVE(buffer, std::uint16_t{0x0102});
    AppendUint32BeUVE(buffer, 0x01020304U);
    AppendUint64BeUVE(buffer, 0x0102030405060708ULL);
    const std::vector<std::byte> expected{std::byte{0x01}, std::byte{0x02}, std::byte{0x01},
                                          std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
                                          std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
                                          std::byte{0x04}, std::byte{0x05}, std::byte{0x06},
                                          std::byte{0x07}, std::byte{0x08}};
    EXPECT_EQ(buffer, expected);
}

TEST(BinaryBufferUVETest, Float_PinsIeee754Bits) {
    std::vector<std::byte> little;
    AppendFloatLeUVE(little, 1.0F);
    AppendFloatLeUVE(little, -2.5F);
    EXPECT_EQ(little, std::vector<std::byte>({std::byte{0x00}, std::byte{0x00}, std::byte{0x80},
                                              std::byte{0x3F}, std::byte{0x00}, std::byte{0x00},
                                              std::byte{0x20}, std::byte{0xC0}}));

    std::vector<std::byte> big;
    AppendFloatBeUVE(big, 1.0F);
    EXPECT_EQ(big, std::vector<std::byte>(
                       {std::byte{0x3F}, std::byte{0x80}, std::byte{0x00}, std::byte{0x00}}));
}

TEST(BinaryBufferUVETest, RoundTrip_LeAndBe) {
    std::vector<std::byte> little;
    AppendUint16LeUVE(little, std::uint16_t{0xFFFF});
    AppendUint32LeUVE(little, 0xDEADBEEFU);
    AppendUint64LeUVE(little, 0xFFFFFFFFFFFFFFFFULL);
    AppendFloatLeUVE(little, 123.456F);
    std::size_t offset = 0U;
    std::uint16_t u16 = 0U;
    std::uint32_t u32 = 0U;
    std::uint64_t u64 = 0U;
    float f = 0.0F;
    ASSERT_TRUE(ReadUint16LeFromBufferUVE(little, offset, u16));
    ASSERT_TRUE(ReadUint32LeFromBufferUVE(little, offset, u32));
    ASSERT_TRUE(ReadUint64LeFromBufferUVE(little, offset, u64));
    ASSERT_TRUE(ReadFloatLeFromBufferUVE(little, offset, f));
    EXPECT_EQ(u16, std::uint16_t{0xFFFF});
    EXPECT_EQ(u32, 0xDEADBEEFU);
    EXPECT_EQ(u64, 0xFFFFFFFFFFFFFFFFULL);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(f), 0x42F6E979U);
    EXPECT_EQ(offset, little.size());

    std::vector<std::byte> big;
    AppendUint16BeUVE(big, std::uint16_t{0xFFFF});
    AppendUint32BeUVE(big, 0xDEADBEEFU);
    AppendUint64BeUVE(big, 0xFFFFFFFFFFFFFFFFULL);
    AppendFloatBeUVE(big, 123.456F);
    offset = 0U;
    ASSERT_TRUE(ReadUint16BeFromBufferUVE(big, offset, u16));
    ASSERT_TRUE(ReadUint32BeFromBufferUVE(big, offset, u32));
    ASSERT_TRUE(ReadUint64BeFromBufferUVE(big, offset, u64));
    ASSERT_TRUE(ReadFloatBeFromBufferUVE(big, offset, f));
    EXPECT_EQ(u16, std::uint16_t{0xFFFF});
    EXPECT_EQ(u32, 0xDEADBEEFU);
    EXPECT_EQ(u64, 0xFFFFFFFFFFFFFFFFULL);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(f), 0x42F6E979U);
    EXPECT_EQ(offset, big.size());
}

TEST(BinaryBufferUVETest, CrossRead_LeBytesAsBeSwapsValues) {
    // One value, both directions: reading little-endian bytes as big-endian is a byte-swap.
    std::vector<std::byte> buffer;
    AppendUint32LeUVE(buffer, 0x01020304U);
    std::size_t offset = 0U;
    std::uint32_t swapped = 0U;
    ASSERT_TRUE(ReadUint32BeFromBufferUVE(buffer, offset, swapped));
    EXPECT_EQ(swapped, 0x04030201U);
}

TEST(BinaryBufferUVETest, SimulatedByteSwap_PeerReemitsByteIdenticalOutput) {
    // The portability proof, on a single-endian machine: we write a record little-endian; a
    // big-endian peer reads those bytes (seeing byte-swapped values, as any naive reader of
    // foreign-order bytes would); it re-emits what it read through the explicit big-endian
    // API — and reproduces our exact bytes.
    constexpr std::uint16_t kChannels = 0x0102;
    constexpr std::uint32_t kRate = 0x01020304U;
    constexpr std::uint64_t kCount = 0x0102030405060708ULL;
    constexpr float kSample = 1.0F;

    std::vector<std::byte> ours;
    AppendUint16LeUVE(ours, kChannels);
    AppendUint32LeUVE(ours, kRate);
    AppendUint64LeUVE(ours, kCount);
    AppendFloatLeUVE(ours, kSample);

    std::size_t readOffset = 0U;
    std::uint16_t peerChannels = 0U;
    std::uint32_t peerRate = 0U;
    std::uint64_t peerCount = 0U;
    float peerSample = 0.0F;
    ASSERT_TRUE(ReadUint16BeFromBufferUVE(ours, readOffset, peerChannels));
    ASSERT_TRUE(ReadUint32BeFromBufferUVE(ours, readOffset, peerRate));
    ASSERT_TRUE(ReadUint64BeFromBufferUVE(ours, readOffset, peerCount));
    ASSERT_TRUE(ReadFloatBeFromBufferUVE(ours, readOffset, peerSample));
    EXPECT_EQ(peerChannels, std::byteswap(kChannels));
    EXPECT_EQ(peerRate, std::byteswap(kRate));
    EXPECT_EQ(peerCount, std::byteswap(kCount));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(peerSample),
              std::byteswap(std::bit_cast<std::uint32_t>(kSample)));

    std::vector<std::byte> reemitted;
    AppendUint16BeUVE(reemitted, peerChannels);
    AppendUint32BeUVE(reemitted, peerRate);
    AppendUint64BeUVE(reemitted, peerCount);
    AppendFloatBeUVE(reemitted, peerSample);
    EXPECT_EQ(reemitted, ours);
}

TEST(BinaryBufferUVETest, TruncatedRead_FailsWithoutAdvancing) {
    // The failure contract, pinned for every reader: false, offset and value untouched.
    const std::vector<std::byte> shortBuffer{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    std::size_t offset = 1U;
    std::uint16_t u16 = 0xBEEFU;
    EXPECT_TRUE(ReadUint16LeFromBufferUVE(shortBuffer, offset, u16));
    EXPECT_EQ(offset, 3U);

    offset = 2U;
    u16 = 0xBEEFU;
    EXPECT_FALSE(ReadUint16LeFromBufferUVE(shortBuffer, offset, u16));
    EXPECT_EQ(offset, 2U);
    EXPECT_EQ(u16, std::uint16_t{0xBEEF});

    std::uint32_t u32 = 0xDEADBEEFU;
    EXPECT_FALSE(ReadUint32LeFromBufferUVE(shortBuffer, offset, u32));
    EXPECT_FALSE(ReadUint32BeFromBufferUVE(shortBuffer, offset, u32));
    EXPECT_EQ(offset, 2U);
    EXPECT_EQ(u32, 0xDEADBEEFU);

    std::uint64_t u64 = 0xFFFFFFFFFFFFFFFFULL;
    EXPECT_FALSE(ReadUint64LeFromBufferUVE(shortBuffer, offset, u64));
    EXPECT_FALSE(ReadUint64BeFromBufferUVE(shortBuffer, offset, u64));
    EXPECT_EQ(offset, 2U);
    EXPECT_EQ(u64, 0xFFFFFFFFFFFFFFFFULL);

    float f = 123.456F;
    EXPECT_FALSE(ReadFloatLeFromBufferUVE(shortBuffer, offset, f));
    EXPECT_FALSE(ReadFloatBeFromBufferUVE(shortBuffer, offset, f));
    EXPECT_EQ(offset, 2U);
    EXPECT_EQ(f, 123.456F);

    std::vector<std::byte> out;
    EXPECT_FALSE(ReadBytesFromBufferUVE(shortBuffer, offset, 2U, out));
    EXPECT_EQ(offset, 2U);
    EXPECT_TRUE(out.empty());
}

TEST(BinaryBufferUVETest, MultiFieldRecord_RoundTripsWithRawBytes) {
    std::vector<std::byte> buffer;
    AppendUint16LeUVE(buffer, std::uint16_t{6});
    AppendUint32LeUVE(buffer, 48000U);
    AppendUint64LeUVE(buffer, 1024U);
    AppendFloatLeUVE(buffer, -2.5F);
    const std::vector<std::byte> payload{std::byte{0xAA}, std::byte{0xBB}};
    AppendBytesUVE(buffer, payload.data(), payload.size());

    std::size_t offset = 0U;
    std::uint16_t channels = 0U;
    std::uint32_t rate = 0U;
    std::uint64_t count = 0U;
    float sample = 0.0F;
    std::vector<std::byte> raw;
    ASSERT_TRUE(ReadUint16LeFromBufferUVE(buffer, offset, channels));
    ASSERT_TRUE(ReadUint32LeFromBufferUVE(buffer, offset, rate));
    ASSERT_TRUE(ReadUint64LeFromBufferUVE(buffer, offset, count));
    ASSERT_TRUE(ReadFloatLeFromBufferUVE(buffer, offset, sample));
    ASSERT_TRUE(ReadBytesFromBufferUVE(buffer, offset, 2U, raw));
    EXPECT_EQ(channels, std::uint16_t{6});
    EXPECT_EQ(rate, 48000U);
    EXPECT_EQ(count, 1024U);
    EXPECT_EQ(sample, -2.5F);
    EXPECT_EQ(raw, payload);
    EXPECT_EQ(offset, buffer.size());
}

TEST(BinaryBufferUVETest, LeMatchesHostOrderOnLittleEndianHosts) {
    // On little-endian hosts (every engine target), the explicit-LE bytes are exactly what the
    // old host-order memcpy produced — which is why every format migration in 1.8 keeps old
    // files loading bit-for-bit. Meaningless (and correctly absent) on big-endian hosts.
    if constexpr (std::endian::native == std::endian::little) {
        constexpr std::uint32_t kValue = 0x01020304U;
        std::vector<std::byte> explicitLe;
        AppendUint32LeUVE(explicitLe, kValue);
        std::vector<std::byte> hostOrder(sizeof(kValue));
        std::memcpy(hostOrder.data(), &kValue, sizeof(kValue));
        EXPECT_EQ(explicitLe, hostOrder);
    }
}

} // namespace
} // namespace UVE::Utilities::Tests
