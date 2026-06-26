#include "codec/slice_output_window.hpp"

#include <array>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

namespace {

mffv1::syntax::StreamParameters make_y_stream()
{
    mffv1::syntax::StreamParameters stream;
    stream.width = 8;
    stream.height = 4;
    stream.bits_per_raw_sample = 8;
    stream.chroma_planes = false;
    return stream;
}

mffv1::MutablePlaneView make_y_plane(std::array<std::uint8_t, 32>& storage)
{
    mffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = mffv1::PlaneRole::Y;
    plane.info.sample_format = mffv1::SampleFormat::UInt8;
    plane.info.width = 8;
    plane.info.height = 4;
    plane.info.stride_bytes = 8;
    return plane;
}

TEST(SliceOutputWindowTest, MapsSinglePlaneSliceRows)
{
    const auto stream = make_y_stream();
    std::array<std::uint8_t, 32> storage{};
    auto plane = make_y_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};

    mffv1::syntax::SliceDescriptor slice;
    slice.x = 2;
    slice.y = 1;
    slice.width = 4;
    slice.height = 2;

    mffv1::codec::SliceOutputWindow window;
    const auto status = window.validate(stream, frame, slice);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(window.plane_count(), 1u);
    EXPECT_EQ(window.plane_width(0), 4u);
    EXPECT_EQ(window.plane_height(0), 2u);
    EXPECT_EQ(window.row_u8(0, 0), storage.data() + 10);
    EXPECT_EQ(window.row_u8(0, 1), storage.data() + 18);
    EXPECT_EQ(window.row_u16(0, 0), nullptr);
}

TEST(SliceOutputWindowTest, RejectsOutOfFrameSlice)
{
    const auto stream = make_y_stream();
    std::array<std::uint8_t, 32> storage{};
    auto plane = make_y_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};

    mffv1::syntax::SliceDescriptor slice;
    slice.x = 7;
    slice.y = 0;
    slice.width = 2;
    slice.height = 1;

    mffv1::codec::SliceOutputWindow window;
    const auto status = window.validate(stream, frame, slice);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "slice rectangle is outside the frame");
}

TEST(SliceOutputWindowTest, RejectsUnrepresentableRowOffset)
{
    mffv1::syntax::StreamParameters stream;
    stream.width = 1;
    stream.height = 3;
    stream.chroma_planes = false;

    std::uint8_t storage = 0;
    mffv1::MutablePlaneView plane;
    plane.data = &storage;
    plane.info = {mffv1::PlaneRole::Y,
                  mffv1::SampleFormat::UInt8,
                  1,
                  3,
                  std::numeric_limits<std::ptrdiff_t>::max() / 2 + 1};
    mffv1::MutableFrameView frame{&plane, 1};

    mffv1::syntax::SliceDescriptor slice;
    slice.y = 2;
    slice.width = 1;
    slice.height = 1;

    mffv1::codec::SliceOutputWindow window;
    const auto status = window.validate(stream, frame, slice);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::ResourceExhausted);
    EXPECT_EQ(status.message, "output plane row offset exceeds ptrdiff_t");
}

TEST(SliceOutputWindowTest, RejectsWindowWhoseLastRowIsUnrepresentable)
{
    mffv1::syntax::StreamParameters stream;
    stream.width = 1;
    stream.height = 3;
    stream.chroma_planes = false;

    std::uint8_t storage = 0;
    mffv1::MutablePlaneView plane;
    plane.data = &storage;
    plane.info = {mffv1::PlaneRole::Y,
                  mffv1::SampleFormat::UInt8,
                  1,
                  3,
                  std::numeric_limits<std::ptrdiff_t>::max() / 2 + 1};
    mffv1::MutableFrameView frame{&plane, 1};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 3;

    mffv1::codec::SliceOutputWindow window;
    const auto status = window.validate(stream, frame, slice);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::ResourceExhausted);
    EXPECT_EQ(status.message, "output plane window rows exceed ptrdiff_t");
}

TEST(SliceOutputWindowTest, MapsChromaPlanesWithSubsampling)
{
    mffv1::syntax::StreamParameters stream;
    stream.width = 8;
    stream.height = 4;
    stream.bits_per_raw_sample = 8;
    stream.chroma_planes = true;
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = 1;

    std::array<std::uint8_t, 32> y{};
    std::array<std::uint8_t, 8> cb{};
    std::array<std::uint8_t, 8> cr{};
    std::array<mffv1::MutablePlaneView, 3> planes{};

    planes[0].data = y.data();
    planes[0].info = {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 8, 4, 8};
    planes[1].data = cb.data();
    planes[1].info = {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt8, 4, 2, 4};
    planes[2].data = cr.data();
    planes[2].info = {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt8, 4, 2, 4};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};

    mffv1::syntax::SliceDescriptor slice;
    slice.x = 2;
    slice.y = 0;
    slice.width = 4;
    slice.height = 4;

    mffv1::codec::SliceOutputWindow window;
    const auto status = window.validate(stream, frame, slice);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(window.plane_count(), 3u);
    EXPECT_EQ(window.plane_width(0), 4u);
    EXPECT_EQ(window.plane_height(0), 4u);
    EXPECT_EQ(window.plane_width(1), 2u);
    EXPECT_EQ(window.plane_height(1), 2u);
    EXPECT_EQ(window.row_u8(1, 0), cb.data() + 1);
    EXPECT_EQ(window.row_u8(2, 1), cr.data() + 5);
}

TEST(SliceOutputWindowTest, PartitionsChromaBySliceRasterWithoutOverlap)
{
    mffv1::syntax::StreamParameters stream;
    stream.width = 8;
    stream.height = 2;
    stream.chroma_planes = true;
    stream.log2_h_chroma_subsample = 1;
    stream.num_h_slices = 3;

    std::array<std::uint8_t, 16> y{};
    std::array<std::uint8_t, 8> cb{};
    std::array<std::uint8_t, 8> cr{};
    std::array<mffv1::MutablePlaneView, 3> planes{};
    planes[0].data = y.data();
    planes[0].info = {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 8, 2, 8};
    planes[1].data = cb.data();
    planes[1].info = {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt8, 4, 2, 4};
    planes[2].data = cr.data();
    planes[2].info = {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt8, 4, 2, 4};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};

    mffv1::syntax::SliceDescriptor middle;
    middle.x = 2;
    middle.width = 3;
    middle.height = 2;
    middle.raster_x = 1;
    middle.raster_width = 1;
    middle.raster_height = 1;
    mffv1::codec::SliceOutputWindow middle_window;
    ASSERT_TRUE(middle_window.validate(stream, frame, middle).ok());

    mffv1::syntax::SliceDescriptor right;
    right.x = 5;
    right.width = 3;
    right.height = 2;
    right.raster_x = 2;
    right.raster_width = 1;
    right.raster_height = 1;
    mffv1::codec::SliceOutputWindow right_window;
    ASSERT_TRUE(right_window.validate(stream, frame, right).ok());

    EXPECT_EQ(middle_window.row_u8(1, 0), cb.data() + 1);
    EXPECT_EQ(middle_window.plane_width(1), 1u);
    EXPECT_EQ(right_window.row_u8(1, 0), cb.data() + 2);
    EXPECT_EQ(right_window.plane_width(1), 2u);
}

TEST(SliceOutputWindowTest, RejectsSwappedChromaPlaneRoles)
{
    mffv1::syntax::StreamParameters stream;
    stream.width = 8;
    stream.height = 4;
    stream.bits_per_raw_sample = 8;
    stream.chroma_planes = true;
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = 1;

    std::array<std::uint8_t, 32> y{};
    std::array<std::uint8_t, 8> cb{};
    std::array<std::uint8_t, 8> cr{};
    std::array<mffv1::MutablePlaneView, 3> planes{};

    planes[0].data = y.data();
    planes[0].info = {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 8, 4, 8};
    planes[1].data = cb.data();
    planes[1].info = {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt8, 4, 2, 4};
    planes[2].data = cr.data();
    planes[2].info = {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt8, 4, 2, 4};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 8;
    slice.height = 4;

    mffv1::codec::SliceOutputWindow window;
    const auto status = window.validate(stream, frame, slice);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "output plane role does not match stream plane order");
}

TEST(SliceOutputWindowTest, MapsExtraPlaneAtFullResolution)
{
    mffv1::syntax::StreamParameters stream;
    stream.width = 8;
    stream.height = 4;
    stream.bits_per_raw_sample = 8;
    stream.chroma_planes = false;
    stream.extra_plane = true;

    std::array<std::uint8_t, 32> y{};
    std::array<std::uint8_t, 32> alpha{};
    std::array<mffv1::MutablePlaneView, 2> planes{};

    planes[0].data = y.data();
    planes[0].info = {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 8, 4, 8};
    planes[1].data = alpha.data();
    planes[1].info = {mffv1::PlaneRole::Alpha, mffv1::SampleFormat::UInt8, 8, 4, 8};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};

    mffv1::syntax::SliceDescriptor slice;
    slice.x = 2;
    slice.y = 1;
    slice.width = 4;
    slice.height = 2;

    mffv1::codec::SliceOutputWindow window;
    const auto status = window.validate(stream, frame, slice);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(window.plane_count(), 2u);
    EXPECT_EQ(window.plane_width(1), 4u);
    EXPECT_EQ(window.plane_height(1), 2u);
    EXPECT_EQ(window.row_u8(1, 0), alpha.data() + 10);
    EXPECT_EQ(window.row_u8(1, 1), alpha.data() + 18);
}

TEST(SliceOutputWindowTest, KeepsExtraPlaneFullResolutionWhenChromaIsAbsent)
{
    mffv1::syntax::StreamParameters stream;
    stream.width = 8;
    stream.height = 4;
    stream.bits_per_raw_sample = 8;
    stream.chroma_planes = false;
    stream.extra_plane = true;
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = 1;

    std::array<std::uint8_t, 32> y{};
    std::array<std::uint8_t, 32> alpha{};
    std::array<mffv1::MutablePlaneView, 2> planes{};

    planes[0].data = y.data();
    planes[0].info = {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 8, 4, 8};
    planes[1].data = alpha.data();
    planes[1].info = {mffv1::PlaneRole::Alpha, mffv1::SampleFormat::UInt8, 8, 4, 8};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};

    mffv1::syntax::SliceDescriptor slice;
    slice.x = 2;
    slice.y = 1;
    slice.width = 4;
    slice.height = 2;

    mffv1::codec::SliceOutputWindow window;
    const auto status = window.validate(stream, frame, slice);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(window.plane_width(1), 4u);
    EXPECT_EQ(window.plane_height(1), 2u);
    EXPECT_EQ(window.row_u8(1, 0), alpha.data() + 10);
    EXPECT_EQ(window.row_u8(1, 1), alpha.data() + 18);
}

} // namespace
