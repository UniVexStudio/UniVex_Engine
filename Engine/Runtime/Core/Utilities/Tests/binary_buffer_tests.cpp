// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "binary_buffer.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Utilities::Tests {
namespace {

TEST(BinaryBufferTest, LeIntegers_PinLittleEndianBytes) {
    std::vector<std::byte> buffer;
    AppendUint16Le(buffer, std::uint16_t{0x0102});
    AppendUint32Le(buffer, 0x01020304U);
    AppendUint64Le(buffer, 0x0102030405060708ULL);
    const std::vector<std::byte> expected{std::byte{0x02}, std::byte{0x01}, std::byte{0x04},
                                          std::byte{0x03}, std::byte{0x02}, std::byte{0x01},
                                          std::byte{0x08}, std::byte{0x07}, std::byte{0x06},
                                          std::byte{0x05}, std::byte{0x04}, std::byte{0x03},
                                          std::byte{0x02}, std::byte{0x01}};
    EXPECT_EQ(buffer, expected);
}

TEST(BinaryBufferTest, BeIntegers_PinBigEndianBytes) {
    std::vector<std::byte> buffer;
    AppendUint16Be(buffer, std::uint16_t{0x0102});
    AppendUint32Be(buffer, 0x01020304U);
    AppendUint64Be(buffer, 0x0102030405060708ULL);
    const std::vector<std::byte> expected{std::byte{0x01}, std::byte{0x02}, std::byte{0x01},
                                          std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
                                          std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
                                          std::byte{0x04}, std::byte{0x05}, std::byte{0x06},
                                          std::byte{0x07}, std::byte{0x08}};
    EXPECT_EQ(buffer, expected);
}

TEST(BinaryBufferTest, Float_PinsIeee754Bits) {
    std::vector<std::byte> little;
    AppendFloatLe(little, 1.0F);
    AppendFloatLe(little, -2.5F);
    EXPECT_EQ(little, std::vector<std::byte>({std::byte{0x00}, std::byte{0x00}, std::byte{0x80},
                                              std::byte{0x3F}, std::byte{0x00}, std::byte{0x00},
                                              std::byte{0x20}, std::byte{0xC0}}));

    std::vector<std::byte> big;
    AppendFloatBe(big, 1.0F);
    EXPECT_EQ(big, std::vector<std::byte>(
                       {std::byte{0x3F}, std::byte{0x80}, std::byte{0x00}, std::byte{0x00}}));
}

TEST(BinaryBufferTest, RoundTrip_LeAndBe) {
    std::vector<std::byte> little;
    AppendUint16Le(little, std::uint16_t{0xFFFF});
    AppendUint32Le(little, 0xDEADBEEFU);
    AppendUint64Le(little, 0xFFFFFFFFFFFFFFFFULL);
    AppendFloatLe(little, 123.456F);
    std::size_t offset = 0U;
    std::uint16_t u16 = 0U;
    std::uint32_t u32 = 0U;
    std::uint64_t u64 = 0U;
    float f = 0.0F;
    ASSERT_TRUE(ReadUint16LeFromBuffer(little, offset, u16));
    ASSERT_TRUE(ReadUint32LeFromBuffer(little, offset, u32));
    ASSERT_TRUE(ReadUint64LeFromBuffer(little, offset, u64));
    ASSERT_TRUE(ReadFloatLeFromBuffer(little, offset, f));
    EXPECT_EQ(u16, std::uint16_t{0xFFFF});
    EXPECT_EQ(u32, 0xDEADBEEFU);
    EXPECT_EQ(u64, 0xFFFFFFFFFFFFFFFFULL);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(f), 0x42F6E979U);
    EXPECT_EQ(offset, little.size());

    std::vector<std::byte> big;
    AppendUint16Be(big, std::uint16_t{0xFFFF});
    AppendUint32Be(big, 0xDEADBEEFU);
    AppendUint64Be(big, 0xFFFFFFFFFFFFFFFFULL);
    AppendFloatBe(big, 123.456F);
    offset = 0U;
    ASSERT_TRUE(ReadUint16BeFromBuffer(big, offset, u16));
    ASSERT_TRUE(ReadUint32BeFromBuffer(big, offset, u32));
    ASSERT_TRUE(ReadUint64BeFromBuffer(big, offset, u64));
    ASSERT_TRUE(ReadFloatBeFromBuffer(big, offset, f));
    EXPECT_EQ(u16, std::uint16_t{0xFFFF});
    EXPECT_EQ(u32, 0xDEADBEEFU);
    EXPECT_EQ(u64, 0xFFFFFFFFFFFFFFFFULL);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(f), 0x42F6E979U);
    EXPECT_EQ(offset, big.size());
}

TEST(BinaryBufferTest, CrossRead_LeBytesAsBeSwapsValues) {
    // One value, both directions: reading little-endian bytes as big-endian is a byte-swap.
    std::vector<std::byte> buffer;
    AppendUint32Le(buffer, 0x01020304U);
    std::size_t offset = 0U;
    std::uint32_t swapped = 0U;
    ASSERT_TRUE(ReadUint32BeFromBuffer(buffer, offset, swapped));
    EXPECT_EQ(swapped, 0x04030201U);
}

TEST(BinaryBufferTest, SimulatedByteSwap_PeerReemitsByteIdenticalOutput) {
    // The portability proof, on a single-endian machine: we write a record little-endian; a
    // big-endian peer reads those bytes (seeing byte-swapped values, as any naive reader of
    // foreign-order bytes would); it re-emits what it read through the explicit big-endian
    // API — and reproduces our exact bytes.
    constexpr std::uint16_t kChannels = 0x0102;
    constexpr std::uint32_t kRate = 0x01020304U;
    constexpr std::uint64_t kCount = 0x0102030405060708ULL;
    constexpr float kSample = 1.0F;

    std::vector<std::byte> ours;
    AppendUint16Le(ours, kChannels);
    AppendUint32Le(ours, kRate);
    AppendUint64Le(ours, kCount);
    AppendFloatLe(ours, kSample);

    std::size_t readOffset = 0U;
    std::uint16_t peerChannels = 0U;
    std::uint32_t peerRate = 0U;
    std::uint64_t peerCount = 0U;
    float peerSample = 0.0F;
    ASSERT_TRUE(ReadUint16BeFromBuffer(ours, readOffset, peerChannels));
    ASSERT_TRUE(ReadUint32BeFromBuffer(ours, readOffset, peerRate));
    ASSERT_TRUE(ReadUint64BeFromBuffer(ours, readOffset, peerCount));
    ASSERT_TRUE(ReadFloatBeFromBuffer(ours, readOffset, peerSample));
    EXPECT_EQ(peerChannels, std::byteswap(kChannels));
    EXPECT_EQ(peerRate, std::byteswap(kRate));
    EXPECT_EQ(peerCount, std::byteswap(kCount));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(peerSample),
              std::byteswap(std::bit_cast<std::uint32_t>(kSample)));

    std::vector<std::byte> reemitted;
    AppendUint16Be(reemitted, peerChannels);
    AppendUint32Be(reemitted, peerRate);
    AppendUint64Be(reemitted, peerCount);
    AppendFloatBe(reemitted, peerSample);
    EXPECT_EQ(reemitted, ours);
}

TEST(BinaryBufferTest, TruncatedRead_FailsWithoutAdvancing) {
    // The failure contract, pinned for every reader: false, offset and value untouched.
    const std::vector<std::byte> shortBuffer{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    std::size_t offset = 1U;
    std::uint16_t u16 = 0xBEEFU;
    EXPECT_TRUE(ReadUint16LeFromBuffer(shortBuffer, offset, u16));
    EXPECT_EQ(offset, 3U);

    offset = 2U;
    u16 = 0xBEEFU;
    EXPECT_FALSE(ReadUint16LeFromBuffer(shortBuffer, offset, u16));
    EXPECT_EQ(offset, 2U);
    EXPECT_EQ(u16, std::uint16_t{0xBEEF});

    std::uint32_t u32 = 0xDEADBEEFU;
    EXPECT_FALSE(ReadUint32LeFromBuffer(shortBuffer, offset, u32));
    EXPECT_FALSE(ReadUint32BeFromBuffer(shortBuffer, offset, u32));
    EXPECT_EQ(offset, 2U);
    EXPECT_EQ(u32, 0xDEADBEEFU);

    std::uint64_t u64 = 0xFFFFFFFFFFFFFFFFULL;
    EXPECT_FALSE(ReadUint64LeFromBuffer(shortBuffer, offset, u64));
    EXPECT_FALSE(ReadUint64BeFromBuffer(shortBuffer, offset, u64));
    EXPECT_EQ(offset, 2U);
    EXPECT_EQ(u64, 0xFFFFFFFFFFFFFFFFULL);

    float f = 123.456F;
    EXPECT_FALSE(ReadFloatLeFromBuffer(shortBuffer, offset, f));
    EXPECT_FALSE(ReadFloatBeFromBuffer(shortBuffer, offset, f));
    EXPECT_EQ(offset, 2U);
    EXPECT_EQ(f, 123.456F);

    std::vector<std::byte> out;
    EXPECT_FALSE(ReadBytesFromBuffer(shortBuffer, offset, 2U, out));
    EXPECT_EQ(offset, 2U);
    EXPECT_TRUE(out.empty());
}

TEST(BinaryBufferTest, MultiFieldRecord_RoundTripsWithRawBytes) {
    std::vector<std::byte> buffer;
    AppendUint16Le(buffer, std::uint16_t{6});
    AppendUint32Le(buffer, 48000U);
    AppendUint64Le(buffer, 1024U);
    AppendFloatLe(buffer, -2.5F);
    const std::vector<std::byte> payload{std::byte{0xAA}, std::byte{0xBB}};
    AppendBytes(buffer, payload.data(), payload.size());

    std::size_t offset = 0U;
    std::uint16_t channels = 0U;
    std::uint32_t rate = 0U;
    std::uint64_t count = 0U;
    float sample = 0.0F;
    std::vector<std::byte> raw;
    ASSERT_TRUE(ReadUint16LeFromBuffer(buffer, offset, channels));
    ASSERT_TRUE(ReadUint32LeFromBuffer(buffer, offset, rate));
    ASSERT_TRUE(ReadUint64LeFromBuffer(buffer, offset, count));
    ASSERT_TRUE(ReadFloatLeFromBuffer(buffer, offset, sample));
    ASSERT_TRUE(ReadBytesFromBuffer(buffer, offset, 2U, raw));
    EXPECT_EQ(channels, std::uint16_t{6});
    EXPECT_EQ(rate, 48000U);
    EXPECT_EQ(count, 1024U);
    EXPECT_EQ(sample, -2.5F);
    EXPECT_EQ(raw, payload);
    EXPECT_EQ(offset, buffer.size());
}

TEST(BinaryBufferTest, LeMatchesHostOrderOnLittleEndianHosts) {
    // On little-endian hosts (every engine target), the explicit-LE bytes are exactly what the
    // old host-order memcpy produced — which is why every format migration in 1.8 keeps old
    // files loading bit-for-bit. Meaningless (and correctly absent) on big-endian hosts.
    if constexpr (std::endian::native == std::endian::little) {
        constexpr std::uint32_t kValue = 0x01020304U;
        std::vector<std::byte> explicitLe;
        AppendUint32Le(explicitLe, kValue);
        std::vector<std::byte> hostOrder(sizeof(kValue));
        std::memcpy(hostOrder.data(), &kValue, sizeof(kValue));
        EXPECT_EQ(explicitLe, hostOrder);
    }
}

} // namespace
} // namespace UVE::Utilities::Tests
