#include "codec/frame_validator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

ffv1::syntax::StreamParameters make_y_stream()
{
    ffv1::syntax::StreamParameters stream;
    stream.width = 4;
    stream.height = 3;
    stream.bits_per_raw_sample = 8;
    stream.chroma_planes = false;
    return stream;
}

ffv1::MutablePlaneView make_output_plane(std::array<std::uint8_t, 12>& storage)
{
    ffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = ffv1::PlaneRole::Y;
    plane.info.sample_format = ffv1::SampleFormat::UInt8;
    plane.info.width = 4;
    plane.info.height = 3;
    plane.info.stride_bytes = 4;
    return plane;
}

ffv1::PlaneView make_input_plane(const std::array<std::uint8_t, 12>& storage)
{
    ffv1::PlaneView plane;
    plane.data = storage.data();
    plane.info.role = ffv1::PlaneRole::Y;
    plane.info.sample_format = ffv1::SampleFormat::UInt8;
    plane.info.width = 4;
    plane.info.height = 3;
    plane.info.stride_bytes = 4;
    return plane;
}

TEST(FrameValidatorTest, AcceptsValidOutputFrame)
{
    const auto stream = make_y_stream();
    std::array<std::uint8_t, 12> storage{};
    auto plane = make_output_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};

    const ffv1::codec::FrameValidator validator;
    EXPECT_TRUE(validator.validate_output(stream, frame).ok());
}

TEST(FrameValidatorTest, AcceptsValidInputFrame)
{
    const auto stream = make_y_stream();
    std::array<std::uint8_t, 12> storage{};
    auto plane = make_input_plane(storage);
    ffv1::FrameView frame{&plane, 1};

    const ffv1::codec::FrameValidator validator;
    EXPECT_TRUE(validator.validate_input(stream, frame).ok());
}

TEST(FrameValidatorTest, RejectsMissingOutputPlaneArray)
{
    const auto stream = make_y_stream();
    const ffv1::MutableFrameView frame{nullptr, 1};

    const ffv1::codec::FrameValidator validator;
    const auto status = validator.validate_output(stream, frame);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
}

TEST(FrameValidatorTest, RejectsWrongSampleFormat)
{
    const auto stream = make_y_stream();
    std::array<std::uint8_t, 12> storage{};
    auto plane = make_output_plane(storage);
    plane.info.sample_format = ffv1::SampleFormat::UInt16;
    ffv1::MutableFrameView frame{&plane, 1};

    const ffv1::codec::FrameValidator validator;
    const auto status = validator.validate_output(stream, frame);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
}

TEST(FrameValidatorTest, RejectsWrongPlaneRole)
{
    const auto stream = make_y_stream();
    std::array<std::uint8_t, 12> storage{};
    auto plane = make_output_plane(storage);
    plane.info.role = ffv1::PlaneRole::Cb;
    ffv1::MutableFrameView frame{&plane, 1};

    const ffv1::codec::FrameValidator validator;
    const auto status = validator.validate_output(stream, frame);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
}

TEST(FrameValidatorTest, RejectsWrongInputPlaneRole)
{
    const auto stream = make_y_stream();
    std::array<std::uint8_t, 12> storage{};
    auto plane = make_input_plane(storage);
    plane.info.role = ffv1::PlaneRole::Cr;
    ffv1::FrameView frame{&plane, 1};

    const ffv1::codec::FrameValidator validator;
    const auto status = validator.validate_input(stream, frame);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
}

TEST(FrameValidatorTest, RejectsShortStride)
{
    const auto stream = make_y_stream();
    std::array<std::uint8_t, 12> storage{};
    auto plane = make_output_plane(storage);
    plane.info.stride_bytes = 3;
    ffv1::MutableFrameView frame{&plane, 1};

    const ffv1::codec::FrameValidator validator;
    const auto status = validator.validate_output(stream, frame);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
}

TEST(FrameValidatorTest, ComputesWideMinimumStrideWithoutWrapping)
{
    auto stream = make_y_stream();
    stream.width = 0xffffffffu;
    stream.height = 1;
    stream.bits_per_raw_sample = 16;

    std::uint16_t storage = 0;
    ffv1::MutablePlaneView plane;
    plane.data = &storage;
    plane.info = {ffv1::PlaneRole::Y,
                  ffv1::SampleFormat::UInt16,
                  stream.width,
                  stream.height,
                  static_cast<std::ptrdiff_t>(5'000'000'000ull)};
    ffv1::MutableFrameView frame{&plane, 1};

    const ffv1::codec::FrameValidator validator;
    const auto status = validator.validate_output(stream, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
}

TEST(FrameValidatorTest, AcceptsChromaPlaneRoles)
{
    auto stream = make_y_stream();
    stream.chroma_planes = true;
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = 1;

    std::array<std::uint8_t, 12> y{};
    std::array<std::uint8_t, 4> cb{};
    std::array<std::uint8_t, 4> cr{};
    std::array<ffv1::MutablePlaneView, 3> planes{};
    planes[0].data = y.data();
    planes[0].info = {ffv1::PlaneRole::Y, ffv1::SampleFormat::UInt8, 4, 3, 4};
    planes[1].data = cb.data();
    planes[1].info = {ffv1::PlaneRole::Cb, ffv1::SampleFormat::UInt8, 2, 2, 2};
    planes[2].data = cr.data();
    planes[2].info = {ffv1::PlaneRole::Cr, ffv1::SampleFormat::UInt8, 2, 2, 2};
    ffv1::MutableFrameView frame{planes.data(), planes.size()};

    const ffv1::codec::FrameValidator validator;
    EXPECT_TRUE(validator.validate_output(stream, frame).ok());
}

TEST(FrameValidatorTest, RejectsSwappedChromaPlaneRoles)
{
    auto stream = make_y_stream();
    stream.chroma_planes = true;
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = 1;

    std::array<std::uint8_t, 12> y{};
    std::array<std::uint8_t, 4> cb{};
    std::array<std::uint8_t, 4> cr{};
    std::array<ffv1::MutablePlaneView, 3> planes{};
    planes[0].data = y.data();
    planes[0].info = {ffv1::PlaneRole::Y, ffv1::SampleFormat::UInt8, 4, 3, 4};
    planes[1].data = cb.data();
    planes[1].info = {ffv1::PlaneRole::Cr, ffv1::SampleFormat::UInt8, 2, 2, 2};
    planes[2].data = cr.data();
    planes[2].info = {ffv1::PlaneRole::Cb, ffv1::SampleFormat::UInt8, 2, 2, 2};
    ffv1::MutableFrameView frame{planes.data(), planes.size()};

    const ffv1::codec::FrameValidator validator;
    const auto status = validator.validate_output(stream, frame);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
}

TEST(FrameValidatorTest, AcceptsExtraPlaneRole)
{
    auto stream = make_y_stream();
    stream.extra_plane = true;

    std::array<std::uint8_t, 12> y{};
    std::array<std::uint8_t, 12> alpha{};
    std::array<ffv1::MutablePlaneView, 2> planes{};
    planes[0].data = y.data();
    planes[0].info = {ffv1::PlaneRole::Y, ffv1::SampleFormat::UInt8, 4, 3, 4};
    planes[1].data = alpha.data();
    planes[1].info = {ffv1::PlaneRole::Alpha, ffv1::SampleFormat::UInt8, 4, 3, 4};
    ffv1::MutableFrameView frame{planes.data(), planes.size()};

    const ffv1::codec::FrameValidator validator;
    EXPECT_TRUE(validator.validate_output(stream, frame).ok());
}

TEST(FrameValidatorTest, KeepsExtraPlaneFullResolutionWhenChromaIsAbsent)
{
    auto stream = make_y_stream();
    stream.extra_plane = true;
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = 1;

    std::array<std::uint8_t, 12> y{};
    std::array<std::uint8_t, 12> alpha{};
    std::array<ffv1::MutablePlaneView, 2> planes{};
    planes[0].data = y.data();
    planes[0].info = {ffv1::PlaneRole::Y, ffv1::SampleFormat::UInt8, 4, 3, 4};
    planes[1].data = alpha.data();
    planes[1].info = {ffv1::PlaneRole::Alpha, ffv1::SampleFormat::UInt8, 4, 3, 4};
    ffv1::MutableFrameView frame{planes.data(), planes.size()};

    const ffv1::codec::FrameValidator validator;
    EXPECT_TRUE(validator.validate_output(stream, frame).ok());
}

TEST(FrameValidatorTest, RequiresChromaPlanesWhenStreamHasChroma)
{
    auto stream = make_y_stream();
    stream.chroma_planes = true;
    std::array<std::uint8_t, 12> storage{};
    auto plane = make_output_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};

    const ffv1::codec::FrameValidator validator;
    const auto status = validator.validate_output(stream, frame);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
}

} // namespace
