#include "codec/slice_input_window.hpp"

#include <array>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

namespace {

mffv1::syntax::StreamParameters make_stream()
{
    mffv1::syntax::StreamParameters stream;
    stream.width = 5;
    stream.height = 3;
    stream.bits_per_raw_sample = 8;
    stream.chroma_planes = true;
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = 1;
    stream.num_h_slices = 2;
    stream.num_v_slices = 2;
    return stream;
}

TEST(SliceInputWindowTest, MapsUnevenSubsampledRasterCell)
{
    const auto stream = make_stream();
    const std::array<std::uint8_t, 15> y{
        0, 1, 2, 3, 4,
        5, 6, 7, 8, 9,
        10, 11, 12, 13, 14,
    };
    const std::array<std::uint8_t, 6> cb{20, 21, 22, 23, 24, 25};
    const std::array<std::uint8_t, 6> cr{30, 31, 32, 33, 34, 35};
    const std::array<mffv1::PlaneView, 3> planes{{
        {
            y.data(),
            {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 5, 3, 5},
        },
        {
            cb.data(),
            {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt8, 3, 2, 3},
        },
        {
            cr.data(),
            {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt8, 3, 2, 3},
        },
    }};
    const mffv1::FrameView frame{planes.data(), planes.size()};
    mffv1::syntax::SliceDescriptor slice;
    slice.x = 2;
    slice.y = 1;
    slice.width = 3;
    slice.height = 2;
    slice.raster_x = 1;
    slice.raster_y = 1;
    slice.raster_width = 1;
    slice.raster_height = 1;
    mffv1::codec::SliceInputWindow window;

    const auto status = window.validate(stream, frame, slice);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(window.plane_width(0), 3u);
    EXPECT_EQ(window.plane_height(0), 2u);
    EXPECT_EQ(window.plane_width(1), 2u);
    EXPECT_EQ(window.plane_height(1), 1u);
    ASSERT_NE(window.row_u8(0, 0), nullptr);
    ASSERT_NE(window.row_u8(0, 1), nullptr);
    ASSERT_NE(window.row_u8(1, 0), nullptr);
    EXPECT_EQ(window.row_u8(0, 0)[0], 7u);
    EXPECT_EQ(window.row_u8(0, 1)[2], 14u);
    EXPECT_EQ(window.row_u8(1, 0)[0], 24u);
    EXPECT_EQ(window.row_u8(1, 0)[1], 25u);
}

TEST(SliceInputWindowTest, RejectsPlaneGeometryMismatch)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 15> storage{};
    const mffv1::PlaneView plane{
        storage.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 5, 3, 5},
    };
    const mffv1::FrameView frame{&plane, 1};
    mffv1::syntax::SliceDescriptor slice;
    slice.width = 5;
    slice.height = 3;
    slice.raster_width = 2;
    slice.raster_height = 2;
    mffv1::codec::SliceInputWindow window;

    const auto status = window.validate(stream, frame, slice);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "input frame plane count does not match the stream");
}

TEST(SliceInputWindowTest, RejectsUnrepresentableRowOffset)
{
    mffv1::syntax::StreamParameters stream;
    stream.width = 1;
    stream.height = 3;
    stream.bits_per_raw_sample = 8;
    stream.chroma_planes = false;

    std::uint8_t storage = 0;
    const mffv1::PlaneView plane{
        &storage,
        {
            mffv1::PlaneRole::Y,
            mffv1::SampleFormat::UInt8,
            1,
            3,
            std::numeric_limits<std::ptrdiff_t>::max() / 2 + 1,
        },
    };
    const mffv1::FrameView frame{&plane, 1};
    mffv1::syntax::SliceDescriptor slice;
    slice.y = 2;
    slice.width = 1;
    slice.height = 1;
    slice.raster_width = 1;
    slice.raster_height = 1;
    mffv1::codec::SliceInputWindow window;

    const auto status = window.validate(stream, frame, slice);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::ResourceExhausted);
    EXPECT_EQ(status.message, "input slice plane offset is not representable");
}

TEST(SliceInputWindowTest, RejectsUnrepresentableLastSampleExtent)
{
    mffv1::syntax::StreamParameters stream;
    stream.width = 1;
    stream.height = 2;
    stream.bits_per_raw_sample = 8;
    stream.chroma_planes = false;

    std::uint8_t storage = 0;
    const mffv1::PlaneView plane{
        &storage,
        {
            mffv1::PlaneRole::Y,
            mffv1::SampleFormat::UInt8,
            1,
            2,
            std::numeric_limits<std::ptrdiff_t>::max(),
        },
    };
    const mffv1::FrameView frame{&plane, 1};
    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 2;
    slice.raster_width = 1;
    slice.raster_height = 1;
    mffv1::codec::SliceInputWindow window;

    const auto status = window.validate(stream, frame, slice);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::ResourceExhausted);
    EXPECT_EQ(status.message, "input slice plane extent is not representable");
}

} // namespace
