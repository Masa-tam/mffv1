#include "codec/plane_window_math.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

namespace {

TEST(PlaneWindowMathTest, ComputesRepresentableRowBytes)
{
    const mffv1::PlaneInfo info{
        mffv1::PlaneRole::Y,
        mffv1::SampleFormat::UInt16,
        5,
        3,
        10,
    };
    std::uint64_t row_bytes = 0;

    const auto status = mffv1::codec::checked_plane_row_bytes(
        info, row_bytes, "row is too large");

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(row_bytes, 10u);
}

TEST(PlaneWindowMathTest, ComputesMaximumSupportedRowBytes)
{
    const mffv1::PlaneInfo info{
        mffv1::PlaneRole::Y,
        mffv1::SampleFormat::UInt16,
        std::numeric_limits<std::uint32_t>::max(),
        1,
        std::numeric_limits<std::ptrdiff_t>::max(),
    };
    std::uint64_t row_bytes = 0;

    const auto status = mffv1::codec::checked_plane_row_bytes(
        info, row_bytes, "row is too large");

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(row_bytes,
              static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())
                  * 2u);
}

TEST(PlaneWindowMathTest, RejectsUnrepresentableRowOffset)
{
    const mffv1::PlaneInfo info{
        mffv1::PlaneRole::Y,
        mffv1::SampleFormat::UInt8,
        1,
        3,
        std::numeric_limits<std::ptrdiff_t>::max() / 2 + 1,
    };
    std::ptrdiff_t offset = 123;

    const auto status = mffv1::codec::checked_plane_window_offset(
        info,
        0,
        2,
        1,
        1,
        offset,
        "row offset",
        "sample offset",
        "rows",
        "extent");

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::ResourceExhausted);
    EXPECT_EQ(status.message, "row offset");
    EXPECT_EQ(offset, 123);
}

TEST(PlaneWindowMathTest, RejectsUnrepresentableSampleOffset)
{
    const mffv1::PlaneInfo info{
        mffv1::PlaneRole::Y,
        mffv1::SampleFormat::UInt16,
        2,
        2,
        std::numeric_limits<std::ptrdiff_t>::max() - 1,
    };
    std::ptrdiff_t offset = 123;

    const auto status = mffv1::codec::checked_plane_window_offset(
        info,
        1,
        1,
        1,
        1,
        offset,
        "row offset",
        "sample offset",
        "rows",
        "extent");

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::ResourceExhausted);
    EXPECT_EQ(status.message, "sample offset");
    EXPECT_EQ(offset, 123);
}

TEST(PlaneWindowMathTest, RejectsUnrepresentableWindowRows)
{
    const mffv1::PlaneInfo info{
        mffv1::PlaneRole::Y,
        mffv1::SampleFormat::UInt8,
        1,
        3,
        std::numeric_limits<std::ptrdiff_t>::max() / 2 + 1,
    };
    std::ptrdiff_t offset = 123;

    const auto status = mffv1::codec::checked_plane_window_offset(
        info,
        0,
        0,
        1,
        3,
        offset,
        "row offset",
        "sample offset",
        "rows",
        "extent");

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::ResourceExhausted);
    EXPECT_EQ(status.message, "rows");
    EXPECT_EQ(offset, 123);
}

TEST(PlaneWindowMathTest, ComputesWindowOffset)
{
    const mffv1::PlaneInfo info{
        mffv1::PlaneRole::Y,
        mffv1::SampleFormat::UInt16,
        8,
        4,
        16,
    };
    std::ptrdiff_t offset = 123;

    const auto status = mffv1::codec::checked_plane_window_offset(
        info,
        2,
        1,
        3,
        2,
        offset,
        "row offset",
        "sample offset",
        "rows",
        "extent");

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(offset, 20);
}

TEST(PlaneWindowMathTest, ComputesEmptyWindowOffsetWithoutExtentCheck)
{
    const mffv1::PlaneInfo info{
        mffv1::PlaneRole::Y,
        mffv1::SampleFormat::UInt16,
        8,
        4,
        16,
    };
    std::ptrdiff_t offset = 123;

    const auto status = mffv1::codec::checked_plane_window_offset(
        info,
        2,
        1,
        0,
        0,
        offset,
        "row offset",
        "sample offset",
        "rows",
        "extent");

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(offset, 20);
}

TEST(PlaneWindowMathTest, RejectsUnrepresentableLastSampleExtent)
{
    const mffv1::PlaneInfo info{
        mffv1::PlaneRole::Y,
        mffv1::SampleFormat::UInt8,
        1,
        2,
        std::numeric_limits<std::ptrdiff_t>::max(),
    };
    std::ptrdiff_t offset = 123;

    const auto status = mffv1::codec::checked_plane_window_offset(
        info,
        0,
        0,
        1,
        2,
        offset,
        "row offset",
        "sample offset",
        "rows",
        "extent");

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::ResourceExhausted);
    EXPECT_EQ(status.message, "extent");
    EXPECT_EQ(offset, 123);
}

} // namespace
