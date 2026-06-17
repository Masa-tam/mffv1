#include "util/crc32.hpp"

#include <array>
#include <cstddef>

#include <gtest/gtest.h>

namespace {

TEST(Crc32Test, UsesFfv1CrcParameters)
{
    constexpr std::array payload{
        std::byte{'1'},
        std::byte{'2'},
        std::byte{'3'},
        std::byte{'4'},
        std::byte{'5'},
        std::byte{'6'},
        std::byte{'7'},
        std::byte{'8'},
        std::byte{'9'},
    };

    EXPECT_EQ(ffv1::util::crc32_ieee_msb(payload), 0x89a1897fu);
}

TEST(Crc32Test, AppendingBigEndianRemainderLeavesZeroRemainder)
{
    constexpr std::array payload{
        std::byte{0x12},
        std::byte{0x34},
        std::byte{0x56},
    };
    const auto remainder = ffv1::util::crc32_ieee_msb(payload);
    const std::array with_parity{
        payload[0],
        payload[1],
        payload[2],
        static_cast<std::byte>((remainder >> 24) & 0xffu),
        static_cast<std::byte>((remainder >> 16) & 0xffu),
        static_cast<std::byte>((remainder >> 8) & 0xffu),
        static_cast<std::byte>(remainder & 0xffu),
    };

    EXPECT_EQ(remainder, 0x09fcfb57u);
    EXPECT_EQ(ffv1::util::crc32_ieee_msb(with_parity), 0u);
}

} // namespace
