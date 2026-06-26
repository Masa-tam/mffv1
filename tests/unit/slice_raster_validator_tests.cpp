#include "codec/slice_raster_validator.hpp"

#include <array>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

namespace {

mffv1::syntax::StreamParameters make_stream()
{
    mffv1::syntax::StreamParameters stream;
    stream.width = 16;
    stream.height = 8;
    stream.num_h_slices = 2;
    stream.num_v_slices = 2;
    return stream;
}

mffv1::syntax::SliceDescriptor make_slice(std::uint32_t index,
                                         std::uint32_t x,
                                         std::uint32_t y,
                                         std::uint32_t width,
                                         std::uint32_t height)
{
    mffv1::syntax::SliceDescriptor slice;
    slice.index = index;
    slice.raster_x = x;
    slice.raster_y = y;
    slice.raster_width = width;
    slice.raster_height = height;
    return slice;
}

TEST(SliceRasterValidatorTest, AcceptsCompleteRasterCoverage)
{
    const auto stream = make_stream();
    const std::array slices{
        make_slice(0, 0, 0, 1, 1),
        make_slice(1, 1, 0, 1, 1),
        make_slice(2, 0, 1, 1, 1),
        make_slice(3, 1, 1, 1, 1),
    };

    const auto status = mffv1::codec::validate_slice_raster_coverage(stream, slices);

    EXPECT_TRUE(status.ok()) << status.message;
}

TEST(SliceRasterValidatorTest, AcceptsSingleSliceCoveringWholeRaster)
{
    const auto stream = make_stream();
    const std::array slices{make_slice(0, 0, 0, 2, 2)};

    const auto status = mffv1::codec::validate_slice_raster_coverage(stream, slices);

    EXPECT_TRUE(status.ok()) << status.message;
}

TEST(SliceRasterValidatorTest, AcceptsMaximumRasterWithoutCellAllocation)
{
    auto stream = make_stream();
    stream.version = 1;
    stream.num_h_slices = std::numeric_limits<std::uint32_t>::max();
    stream.num_v_slices = std::numeric_limits<std::uint32_t>::max();
    const std::array slices{
        make_slice(0,
                   0,
                   0,
                   stream.num_h_slices,
                   stream.num_v_slices),
    };

    const auto status = mffv1::codec::validate_slice_raster_coverage(stream, slices);

    EXPECT_TRUE(status.ok()) << status.message;
}

TEST(SliceRasterValidatorTest, AcceptsLargeFrameSlicesAtParallelAreaLimit)
{
    auto stream = make_stream();
    stream.width = 353;
    stream.height = 288;
    stream.num_h_slices = 4;
    stream.num_v_slices = 4;
    const std::array slices{
        make_slice(0, 0, 0, 2, 2),
        make_slice(1, 2, 0, 2, 2),
        make_slice(2, 0, 2, 2, 2),
        make_slice(3, 2, 2, 2, 2),
    };

    const auto status = mffv1::codec::validate_slice_raster_coverage(stream, slices);

    EXPECT_TRUE(status.ok()) << status.message;
}

TEST(SliceRasterValidatorTest, RejectsZeroSliceGrid)
{
    auto stream = make_stream();
    stream.num_h_slices = 0;
    const std::array slices{make_slice(0, 0, 0, 1, 1)};

    const auto status = mffv1::codec::validate_slice_raster_coverage(stream, slices);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "slice grid dimensions must be non-zero");
}

TEST(SliceRasterValidatorTest, RejectsLargeFrameSliceAboveParallelAreaLimit)
{
    auto stream = make_stream();
    stream.width = 353;
    stream.height = 288;
    stream.num_h_slices = 4;
    stream.num_v_slices = 4;
    const std::array slices{
        make_slice(7, 0, 0, 4, 2),
        make_slice(8, 0, 2, 2, 2),
        make_slice(9, 2, 2, 2, 2),
    };

    const auto status = mffv1::codec::validate_slice_raster_coverage(stream, slices);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice raster area exceeds the version 3 parallel decoding limit");
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 7u);
}

TEST(SliceRasterValidatorTest, DoesNotApplyParallelAreaLimitAtCifThreshold)
{
    auto stream = make_stream();
    stream.width = 352;
    stream.height = 288;
    const std::array slices{make_slice(0, 0, 0, 2, 2)};

    const auto status = mffv1::codec::validate_slice_raster_coverage(stream, slices);

    EXPECT_TRUE(status.ok()) << status.message;
}

TEST(SliceRasterValidatorTest, DoesNotApplyParallelAreaLimitBeforeVersionThree)
{
    auto stream = make_stream();
    stream.version = 1;
    stream.width = 353;
    stream.height = 288;
    const std::array slices{make_slice(0, 0, 0, 2, 2)};

    const auto status = mffv1::codec::validate_slice_raster_coverage(stream, slices);

    EXPECT_TRUE(status.ok()) << status.message;
}

TEST(SliceRasterValidatorTest, RejectsMissingRasterCell)
{
    const auto stream = make_stream();
    const std::array slices{
        make_slice(0, 0, 0, 1, 1),
        make_slice(1, 1, 0, 1, 1),
        make_slice(2, 0, 1, 1, 1),
    };

    const auto status = mffv1::codec::validate_slice_raster_coverage(stream, slices);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice raster coverage has missing cells");
}

TEST(SliceRasterValidatorTest, RejectsOverlappingRasterCells)
{
    const auto stream = make_stream();
    const std::array slices{
        make_slice(0, 0, 0, 2, 1),
        make_slice(1, 1, 0, 1, 1),
        make_slice(2, 0, 1, 2, 1),
    };

    const auto status = mffv1::codec::validate_slice_raster_coverage(stream, slices);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice raster rectangles overlap");
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 1u);
}

TEST(SliceRasterValidatorTest, RejectsOutOfRasterRectangle)
{
    const auto stream = make_stream();
    const std::array slices{make_slice(7, 1, 0, 2, 1)};

    const auto status = mffv1::codec::validate_slice_raster_coverage(stream, slices);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice raster rectangle is outside the frame raster");
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 7u);
}

TEST(SliceRasterValidatorTest, RejectsZeroSizedSlice)
{
    const auto stream = make_stream();
    const std::array slices{make_slice(3, 0, 0, 0, 1)};

    const auto status = mffv1::codec::validate_slice_raster_coverage(stream, slices);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice raster dimensions must be non-zero");
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 3u);
}

} // namespace
