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

TEST(FrameValidatorTest, AcceptsValidOutputFrame)
{
    const auto stream = make_y_stream();
    std::array<std::uint8_t, 12> storage{};
    auto plane = make_output_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};

    const ffv1::codec::FrameValidator validator;
    EXPECT_TRUE(validator.validate_output(stream, frame).ok());
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

