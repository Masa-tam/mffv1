#include "codec/frame_validator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

namespace {

mffv1::syntax::StreamParameters make_y_stream()
{
    mffv1::syntax::StreamParameters stream;
    stream.width = 4;
    stream.height = 3;
    stream.bits_per_raw_sample = 8;
    stream.chroma_planes = false;
    return stream;
}

mffv1::MutablePlaneView make_output_plane(std::array<std::uint8_t, 12>& storage)
{
    mffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = mffv1::PlaneRole::Y;
    plane.info.sample_format = mffv1::SampleFormat::UInt8;
    plane.info.width = 4;
    plane.info.height = 3;
    plane.info.stride_bytes = 4;
    return plane;
}

mffv1::PlaneView make_input_plane(const std::array<std::uint8_t, 12>& storage)
{
    mffv1::PlaneView plane;
    plane.data = storage.data();
    plane.info.role = mffv1::PlaneRole::Y;
    plane.info.sample_format = mffv1::SampleFormat::UInt8;
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
    mffv1::MutableFrameView frame{&plane, 1};

    const mffv1::codec::FrameValidator validator;
    EXPECT_TRUE(validator.validate_output(stream, frame).ok());
}

TEST(FrameValidatorTest, AcceptsValidInputFrame)
{
    const auto stream = make_y_stream();
    std::array<std::uint8_t, 12> storage{};
    auto plane = make_input_plane(storage);
    mffv1::FrameView frame{&plane, 1};

    const mffv1::codec::FrameValidator validator;
    EXPECT_TRUE(validator.validate_input(stream, frame).ok());
}

TEST(FrameValidatorTest, AcceptsPaddedInputStride)
{
    const auto stream = make_y_stream();
    std::array<std::uint8_t, 15> storage{};
    mffv1::PlaneView plane;
    plane.data = storage.data();
    plane.info = {
        mffv1::PlaneRole::Y,
        mffv1::SampleFormat::UInt8,
        4,
        3,
        5,
    };
    mffv1::FrameView frame{&plane, 1};

    const mffv1::codec::FrameValidator validator;
    EXPECT_TRUE(validator.validate_input(stream, frame).ok());
}

TEST(FrameValidatorTest, RejectsMissingOutputPlaneArray)
{
    const auto stream = make_y_stream();
    const mffv1::MutableFrameView frame{nullptr, 1};

    const mffv1::codec::FrameValidator validator;
    const auto status = validator.validate_output(stream, frame);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "output plane array is null");
}

TEST(FrameValidatorTest, RejectsMissingInputPlaneArray)
{
    const auto stream = make_y_stream();
    const mffv1::FrameView frame{nullptr, 1};

    const mffv1::codec::FrameValidator validator;
    const auto status = validator.validate_input(stream, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
}

TEST(FrameValidatorTest, RejectsNullInputPlaneData)
{
    const auto stream = make_y_stream();
    std::array<std::uint8_t, 12> storage{};
    auto plane = make_input_plane(storage);
    plane.data = nullptr;
    const mffv1::FrameView frame{&plane, 1};

    const mffv1::codec::FrameValidator validator;
    const auto status = validator.validate_input(stream, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
}

TEST(FrameValidatorTest, RejectsExtraInputPlane)
{
    const auto stream = make_y_stream();
    std::array<std::uint8_t, 12> storage{};
    std::array<mffv1::PlaneView, 2> planes{
        make_input_plane(storage),
        make_input_plane(storage),
    };
    const mffv1::FrameView frame{planes.data(), planes.size()};

    const mffv1::codec::FrameValidator validator;
    const auto status = validator.validate_input(stream, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
}

TEST(FrameValidatorTest, RejectsWrongInputSampleFormat)
{
    const auto stream = make_y_stream();
    std::array<std::uint8_t, 12> storage{};
    auto plane = make_input_plane(storage);
    plane.info.sample_format = mffv1::SampleFormat::UInt16;
    const mffv1::FrameView frame{&plane, 1};

    const mffv1::codec::FrameValidator validator;
    const auto status = validator.validate_input(stream, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
}

TEST(FrameValidatorTest, RejectsInputPlaneDimensionMismatch)
{
    const auto stream = make_y_stream();
    std::array<std::uint8_t, 12> storage{};
    auto plane = make_input_plane(storage);
    plane.info.width = 5;
    mffv1::FrameView frame{&plane, 1};

    const mffv1::codec::FrameValidator validator;
    const auto status = validator.validate_input(stream, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
}

TEST(FrameValidatorTest, RejectsShortInputStride)
{
    const auto stream = make_y_stream();
    std::array<std::uint8_t, 12> storage{};
    auto plane = make_input_plane(storage);
    plane.info.stride_bytes = 3;
    const mffv1::FrameView frame{&plane, 1};

    const mffv1::codec::FrameValidator validator;
    const auto status = validator.validate_input(stream, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
}

TEST(FrameValidatorTest, RejectsNegativeInputStrideAsUnsupported)
{
    const auto stream = make_y_stream();
    std::array<std::uint8_t, 12> storage{};
    auto plane = make_input_plane(storage);
    plane.info.stride_bytes = -4;
    mffv1::FrameView frame{&plane, 1};

    const mffv1::codec::FrameValidator validator;
    const auto status = validator.validate_input(stream, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::UnsupportedFeature);
}

TEST(FrameValidatorTest, RejectsUnrepresentableInputLastRowAddress)
{
    auto stream = make_y_stream();
    stream.width = 1;
    stream.height = 2;
    std::uint8_t storage = 0;
    mffv1::PlaneView plane;
    plane.data = &storage;
    plane.info = {
        mffv1::PlaneRole::Y,
        mffv1::SampleFormat::UInt8,
        stream.width,
        stream.height,
        std::numeric_limits<std::ptrdiff_t>::max(),
    };
    mffv1::FrameView frame{&plane, 1};

    const mffv1::codec::FrameValidator validator;
    const auto status = validator.validate_input(stream, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::ResourceExhausted);
}

TEST(FrameValidatorTest, RejectsWrongSampleFormat)
{
    const auto stream = make_y_stream();
    std::array<std::uint8_t, 12> storage{};
    auto plane = make_output_plane(storage);
    plane.info.sample_format = mffv1::SampleFormat::UInt16;
    mffv1::MutableFrameView frame{&plane, 1};

    const mffv1::codec::FrameValidator validator;
    const auto status = validator.validate_output(stream, frame);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "plane sample format does not match stream bit depth");
}

TEST(FrameValidatorTest, RejectsWrongPlaneRole)
{
    const auto stream = make_y_stream();
    std::array<std::uint8_t, 12> storage{};
    auto plane = make_output_plane(storage);
    plane.info.role = mffv1::PlaneRole::Cb;
    mffv1::MutableFrameView frame{&plane, 1};

    const mffv1::codec::FrameValidator validator;
    const auto status = validator.validate_output(stream, frame);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "plane role does not match stream plane order");
}

TEST(FrameValidatorTest, RejectsWrongInputPlaneRole)
{
    const auto stream = make_y_stream();
    std::array<std::uint8_t, 12> storage{};
    auto plane = make_input_plane(storage);
    plane.info.role = mffv1::PlaneRole::Cr;
    mffv1::FrameView frame{&plane, 1};

    const mffv1::codec::FrameValidator validator;
    const auto status = validator.validate_input(stream, frame);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
}

TEST(FrameValidatorTest, RejectsShortStride)
{
    const auto stream = make_y_stream();
    std::array<std::uint8_t, 12> storage{};
    auto plane = make_output_plane(storage);
    plane.info.stride_bytes = 3;
    mffv1::MutableFrameView frame{&plane, 1};

    const mffv1::codec::FrameValidator validator;
    const auto status = validator.validate_output(stream, frame);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "plane stride is smaller than the stream requires");
}

TEST(FrameValidatorTest, ComputesWideMinimumStrideWithoutWrapping)
{
    auto stream = make_y_stream();
    stream.width = 0xffffffffu;
    stream.height = 1;
    stream.bits_per_raw_sample = 16;

    std::uint16_t storage = 0;
    mffv1::MutablePlaneView plane;
    plane.data = &storage;
    plane.info = {mffv1::PlaneRole::Y,
                  mffv1::SampleFormat::UInt16,
                  stream.width,
                  stream.height,
                  static_cast<std::ptrdiff_t>(5'000'000'000ull)};
    mffv1::MutableFrameView frame{&plane, 1};

    const mffv1::codec::FrameValidator validator;
    const auto status = validator.validate_output(stream, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
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
    std::array<mffv1::MutablePlaneView, 3> planes{};
    planes[0].data = y.data();
    planes[0].info = {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 4, 3, 4};
    planes[1].data = cb.data();
    planes[1].info = {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt8, 2, 2, 2};
    planes[2].data = cr.data();
    planes[2].info = {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt8, 2, 2, 2};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};

    const mffv1::codec::FrameValidator validator;
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
    std::array<mffv1::MutablePlaneView, 3> planes{};
    planes[0].data = y.data();
    planes[0].info = {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 4, 3, 4};
    planes[1].data = cb.data();
    planes[1].info = {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt8, 2, 2, 2};
    planes[2].data = cr.data();
    planes[2].info = {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt8, 2, 2, 2};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};

    const mffv1::codec::FrameValidator validator;
    const auto status = validator.validate_output(stream, frame);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
}

TEST(FrameValidatorTest, AcceptsExtraPlaneRole)
{
    auto stream = make_y_stream();
    stream.extra_plane = true;

    std::array<std::uint8_t, 12> y{};
    std::array<std::uint8_t, 12> alpha{};
    std::array<mffv1::MutablePlaneView, 2> planes{};
    planes[0].data = y.data();
    planes[0].info = {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 4, 3, 4};
    planes[1].data = alpha.data();
    planes[1].info = {mffv1::PlaneRole::Alpha, mffv1::SampleFormat::UInt8, 4, 3, 4};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};

    const mffv1::codec::FrameValidator validator;
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
    std::array<mffv1::MutablePlaneView, 2> planes{};
    planes[0].data = y.data();
    planes[0].info = {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 4, 3, 4};
    planes[1].data = alpha.data();
    planes[1].info = {mffv1::PlaneRole::Alpha, mffv1::SampleFormat::UInt8, 4, 3, 4};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};

    const mffv1::codec::FrameValidator validator;
    EXPECT_TRUE(validator.validate_output(stream, frame).ok());
}

TEST(FrameValidatorTest, RequiresChromaPlanesWhenStreamHasChroma)
{
    auto stream = make_y_stream();
    stream.chroma_planes = true;
    std::array<std::uint8_t, 12> storage{};
    auto plane = make_output_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};

    const mffv1::codec::FrameValidator validator;
    const auto status = validator.validate_output(stream, frame);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
}

TEST(FrameValidatorTest, AcceptsFullResolutionRgbPlaneRoles)
{
    auto stream = make_y_stream();
    stream.colorspace_type = 1;
    stream.chroma_planes = true;

    std::array<std::uint8_t, 12> r{};
    std::array<std::uint8_t, 12> g{};
    std::array<std::uint8_t, 12> b{};
    std::array<mffv1::MutablePlaneView, 3> planes{};
    planes[0] = {r.data(), {mffv1::PlaneRole::R, mffv1::SampleFormat::UInt8, 4, 3, 4}};
    planes[1] = {g.data(), {mffv1::PlaneRole::G, mffv1::SampleFormat::UInt8, 4, 3, 4}};
    planes[2] = {b.data(), {mffv1::PlaneRole::B, mffv1::SampleFormat::UInt8, 4, 3, 4}};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};

    const mffv1::codec::FrameValidator validator;
    EXPECT_TRUE(validator.validate_output(stream, frame).ok());
}

TEST(FrameValidatorTest, RejectsSubsampledRgbStream)
{
    auto stream = make_y_stream();
    stream.colorspace_type = 1;
    stream.chroma_planes = true;
    stream.log2_h_chroma_subsample = 1;
    std::array<std::uint8_t, 12> storage{};
    auto plane = make_output_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};

    const mffv1::codec::FrameValidator validator;
    const auto status = validator.validate_output(stream, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
}

} // namespace
