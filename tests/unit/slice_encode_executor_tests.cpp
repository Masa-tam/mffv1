#include "codec/slice_encode_executor.hpp"

#include "codec/frame_decode_context.hpp"
#include "codec/frame_parser.hpp"
#include "codec/slice_executor.hpp"

#include <array>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

mffv1::syntax::StreamParameters make_stream(
    std::uint8_t bits_per_raw_sample = 8)
{
    mffv1::syntax::StreamParameters stream;
    stream.version = 3;
    stream.micro_version = 4;
    stream.width = 4;
    stream.height = 2;
    stream.bits_per_raw_sample = bits_per_raw_sample;
    stream.chroma_planes = false;
    stream.num_h_slices = 2;
    stream.num_v_slices = 2;
    stream.quant_table_sets.push_back(
        mffv1::syntax::make_zero_quant_table_set());
    stream.intra_only = true;
    return stream;
}

mffv1::syntax::StreamParameters make_yuv420_stream()
{
    auto stream = make_stream();
    stream.width = 8;
    stream.height = 4;
    stream.chroma_planes = true;
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = 1;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    stream.intra_only = false;
    return stream;
}

mffv1::PlaneView make_input_plane(const std::array<std::uint8_t, 8>& storage)
{
    return {
        storage.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 4, 2, 4},
    };
}

mffv1::MutablePlaneView make_output_plane(
    std::array<std::uint8_t, 8>& storage)
{
    return {
        storage.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 4, 2, 4},
    };
}

void expect_stateful_round_trip(mffv1::EntropyMode entropy_mode)
{
    auto stream = make_stream();
    stream.entropy_mode = entropy_mode;
    stream.intra_only = false;
    const std::array<std::uint8_t, 8> first{
        0, 17, 93, 255,
        71, 19, 201, 3,
    };
    const std::array<std::uint8_t, 8> second{
        255, 201, 19, 71,
        3, 93, 17, 0,
    };
    const auto first_plane = make_input_plane(first);
    const auto second_plane = make_input_plane(second);
    const mffv1::FrameView first_input{&first_plane, 1};
    const mffv1::FrameView second_input{&second_plane, 1};
    mffv1::codec::SliceEncodeExecutor encoder(stream, 3);
    std::vector<std::byte> first_frame;
    std::vector<std::byte> second_frame;

    ASSERT_FALSE(encoder.has_reference_state());
    ASSERT_TRUE(encoder.encode(first_input, true, first_frame).ok());
    ASSERT_TRUE(encoder.has_reference_state());
    ASSERT_TRUE(encoder.encode(second_input, false, second_frame).ok());

    const mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext parsed_first;
    mffv1::codec::FrameDecodeContext parsed_second;
    ASSERT_TRUE(parser.parse_with_range_header(
        first_frame, parsed_first).ok());
    ASSERT_TRUE(parser.parse_with_range_header(
        second_frame, parsed_second).ok());
    ASSERT_TRUE(parsed_first.keyframe);
    ASSERT_FALSE(parsed_second.keyframe);

    std::array<std::uint8_t, 8> decoded{};
    auto output_plane = make_output_plane(decoded);
    mffv1::MutableFrameView output{&output_plane, 1};
    mffv1::codec::SliceExecutor decoder(stream, 3);
    ASSERT_TRUE(decoder.decode(
        output, parsed_first.slices, parsed_first.keyframe).ok());
    EXPECT_EQ(decoded, first);

    decoded.fill(0);
    ASSERT_TRUE(decoder.decode(
        output, parsed_second.slices, parsed_second.keyframe).ok());
    EXPECT_EQ(decoded, second);
}

TEST(SliceEncodeExecutorTest, StatefulGolombRiceYuv420MultiSliceRoundTripsNonKeyframe)
{
    const auto stream = make_yuv420_stream();
    const std::array<std::uint8_t, 32> first_y{
        0, 11, 22, 33, 44, 55, 66, 77,
        8, 19, 30, 41, 52, 63, 74, 85,
        16, 27, 38, 49, 60, 71, 82, 93,
        24, 35, 46, 57, 68, 79, 90, 101,
    };
    const std::array<std::uint8_t, 8> first_cb{
        128, 132, 136, 140,
        144, 148, 152, 156,
    };
    const std::array<std::uint8_t, 8> first_cr{
        64, 68, 72, 76,
        80, 84, 88, 92,
    };
    const std::array<std::uint8_t, 32> second_y{
        101, 90, 79, 68, 57, 46, 35, 24,
        93, 82, 71, 60, 49, 38, 27, 16,
        85, 74, 63, 52, 41, 30, 19, 8,
        77, 66, 55, 44, 33, 22, 11, 0,
    };
    const std::array<std::uint8_t, 8> second_cb{
        156, 152, 148, 144,
        140, 136, 132, 128,
    };
    const std::array<std::uint8_t, 8> second_cr{
        92, 88, 84, 80,
        76, 72, 68, 64,
    };
    const std::array<mffv1::PlaneView, 3> first_planes{{
        {first_y.data(), {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 8, 4, 8}},
        {first_cb.data(), {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt8, 4, 2, 4}},
        {first_cr.data(), {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt8, 4, 2, 4}},
    }};
    const std::array<mffv1::PlaneView, 3> second_planes{{
        {second_y.data(), {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 8, 4, 8}},
        {second_cb.data(), {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt8, 4, 2, 4}},
        {second_cr.data(), {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt8, 4, 2, 4}},
    }};
    const mffv1::FrameView first_input{first_planes.data(), first_planes.size()};
    const mffv1::FrameView second_input{second_planes.data(), second_planes.size()};
    mffv1::codec::SliceEncodeExecutor encoder(stream, 4);
    std::vector<std::byte> first_frame;
    std::vector<std::byte> second_frame;

    ASSERT_TRUE(encoder.encode(first_input, true, first_frame).ok());
    ASSERT_TRUE(encoder.encode(second_input, false, second_frame).ok());

    const mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext parsed_first;
    mffv1::codec::FrameDecodeContext parsed_second;
    ASSERT_TRUE(parser.parse_with_range_header(first_frame, parsed_first).ok());
    ASSERT_TRUE(parser.parse_with_range_header(second_frame, parsed_second).ok());
    ASSERT_TRUE(parsed_first.keyframe);
    ASSERT_FALSE(parsed_second.keyframe);

    std::array<std::uint8_t, 32> decoded_y{};
    std::array<std::uint8_t, 8> decoded_cb{};
    std::array<std::uint8_t, 8> decoded_cr{};
    std::array<mffv1::MutablePlaneView, 3> output_planes{{
        {decoded_y.data(), {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 8, 4, 8}},
        {decoded_cb.data(), {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt8, 4, 2, 4}},
        {decoded_cr.data(), {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt8, 4, 2, 4}},
    }};
    mffv1::MutableFrameView output{output_planes.data(), output_planes.size()};
    mffv1::codec::SliceExecutor decoder(stream, 4);
    ASSERT_TRUE(decoder.decode(output, parsed_first.slices, parsed_first.keyframe).ok());
    EXPECT_EQ(decoded_y, first_y);
    EXPECT_EQ(decoded_cb, first_cb);
    EXPECT_EQ(decoded_cr, first_cr);

    decoded_y.fill(0);
    decoded_cb.fill(0);
    decoded_cr.fill(0);
    ASSERT_TRUE(decoder.decode(output, parsed_second.slices, parsed_second.keyframe).ok());
    EXPECT_EQ(decoded_y, second_y);
    EXPECT_EQ(decoded_cb, second_cb);
    EXPECT_EQ(decoded_cr, second_cr);
}

TEST(SliceEncodeExecutorTest, ResolvesAndCapsWorkerCount)
{
    const auto stream = make_stream();
    const mffv1::codec::SliceEncodeExecutor automatic(stream, 0);
    const mffv1::codec::SliceEncodeExecutor serial(stream, 1);
    const mffv1::codec::SliceEncodeExecutor parallel(stream, 8);

    EXPECT_GE(automatic.thread_count(), 1u);
    EXPECT_EQ(serial.thread_count(), 1u);
    EXPECT_EQ(parallel.thread_count(), 8u);
    EXPECT_EQ(parallel.worker_count_for(0), 0u);
    EXPECT_EQ(parallel.worker_count_for(1), 1u);
    EXPECT_EQ(parallel.worker_count_for(4), 4u);
    EXPECT_EQ(parallel.worker_count_for(12), 8u);
}

TEST(SliceEncodeExecutorTest, ParallelEncodingMatchesSerialBitstream)
{
    const auto stream = make_stream();
    const std::array<std::uint8_t, 8> storage{
        0, 17, 93, 255,
        71, 19, 201, 3,
    };
    const mffv1::PlaneView plane{
        storage.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 4, 2, 4},
    };
    const mffv1::FrameView input{&plane, 1};
    std::vector<std::byte> serial_frame;
    std::vector<std::byte> parallel_frame;

    mffv1::CpuFeatures scalar_cpu;
    scalar_cpu.auto_detect = false;
    mffv1::codec::SliceEncodeExecutor serial(
        stream, 1, scalar_cpu);
    mffv1::codec::SliceEncodeExecutor parallel(stream, 3);
    ASSERT_TRUE(serial.encode(input, serial_frame).ok());
    ASSERT_TRUE(parallel.encode(input, parallel_frame).ok());

    EXPECT_EQ(parallel_frame, serial_frame);
}

TEST(SliceEncodeExecutorTest, ParallelFailureReportsFirstSliceInInputOrder)
{
    const auto stream = make_stream(10);
    const std::array<std::uint16_t, 8> storage{
        1024, 0, 1024, 0,
        0, 0, 0, 0,
    };
    const mffv1::PlaneView plane{
        storage.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt16, 4, 2, 8},
    };
    const mffv1::FrameView input{&plane, 1};
    std::vector<std::byte> frame{std::byte{0xaa}};
    mffv1::codec::SliceEncodeExecutor executor(stream, 4);

    const auto status = executor.encode(input, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "input sample exceeds configured bit depth");
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 0u);
    EXPECT_EQ(frame, (std::vector<std::byte>{std::byte{0xaa}}));
}

TEST(SliceEncodeExecutorTest, ParallelFailureAfterSuccessfulSlicesDoesNotPublishPartialFrame)
{
    const auto stream = make_stream(10);
    const std::array<std::uint16_t, 8> storage{
        0, 17, 93, 255,
        71, 19, 1024, 3,
    };
    const mffv1::PlaneView plane{
        storage.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt16, 4, 2, 8},
    };
    const mffv1::FrameView input{&plane, 1};
    std::vector<std::byte> frame{std::byte{0xaa}};
    mffv1::codec::SliceEncodeExecutor executor(stream, 4);

    const auto status = executor.encode(input, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "input sample exceeds configured bit depth");
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 3u);
    EXPECT_FALSE(executor.has_reference_state());
    EXPECT_EQ(frame, (std::vector<std::byte>{std::byte{0xaa}}));
}

TEST(SliceEncodeExecutorTest, RejectsNonKeyframeWithoutReferenceState)
{
    auto stream = make_stream();
    stream.intra_only = false;
    const std::array<std::uint8_t, 8> storage{};
    const auto plane = make_input_plane(storage);
    const mffv1::FrameView input{&plane, 1};
    std::vector<std::byte> frame{std::byte{0xaa}};
    mffv1::codec::SliceEncodeExecutor executor(stream, 2);

    const auto status = executor.encode(input, false, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message,
              "non-keyframe encode requires reference slice states");
    EXPECT_FALSE(executor.has_reference_state());
    EXPECT_EQ(frame, (std::vector<std::byte>{std::byte{0xaa}}));
}

TEST(SliceEncodeExecutorTest, RejectsNonKeyframeForIntraOnlyStream)
{
    const auto stream = make_stream();
    const std::array<std::uint8_t, 8> storage{};
    const auto plane = make_input_plane(storage);
    const mffv1::FrameView input{&plane, 1};
    std::vector<std::byte> frame{std::byte{0xaa}};
    mffv1::codec::SliceEncodeExecutor executor(stream, 2);

    const auto status = executor.encode(input, false, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "non-keyframe encode requires a non-intra stream");
    EXPECT_FALSE(executor.has_reference_state());
    EXPECT_EQ(frame, (std::vector<std::byte>{std::byte{0xaa}}));
}

TEST(SliceEncodeExecutorTest, RejectsInvalidStreamShapeWithoutChangingOutput)
{
    auto stream = make_stream();
    stream.num_h_slices = 0;
    const std::array<std::uint8_t, 8> storage{};
    const auto plane = make_input_plane(storage);
    const mffv1::FrameView input{&plane, 1};
    std::vector<std::byte> frame{std::byte{0xaa}};
    mffv1::codec::SliceEncodeExecutor executor(stream, 2);

    const auto status = executor.encode(input, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "slice grid dimensions must be non-zero");
    EXPECT_FALSE(executor.has_reference_state());
    EXPECT_EQ(frame, (std::vector<std::byte>{std::byte{0xaa}}));
}

TEST(SliceEncodeExecutorTest, RejectsChangedReferenceSliceCountWithoutChangingOutput)
{
    auto stream = make_stream();
    stream.intra_only = false;
    const std::array<std::uint8_t, 8> storage{
        0, 17, 93, 255,
        71, 19, 201, 3,
    };
    const auto plane = make_input_plane(storage);
    const mffv1::FrameView input{&plane, 1};
    mffv1::codec::SliceEncodeExecutor executor(stream, 2);
    std::vector<std::byte> frame;
    ASSERT_TRUE(executor.encode(input, true, frame).ok());
    ASSERT_TRUE(executor.has_reference_state());

    stream.num_h_slices = 1;
    frame.assign({std::byte{0xaa}});
    const auto status = executor.encode(input, false, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message,
              "encoder reference slice state does not match the stream");
    EXPECT_TRUE(executor.has_reference_state());
    EXPECT_EQ(frame, (std::vector<std::byte>{std::byte{0xaa}}));
}

TEST(SliceEncodeExecutorTest, StatefulRangeEncodingRoundTripsNonKeyframe)
{
    expect_stateful_round_trip(mffv1::EntropyMode::Range);
}

TEST(SliceEncodeExecutorTest, StatefulGolombRiceEncodingRoundTripsNonKeyframe)
{
    expect_stateful_round_trip(mffv1::EntropyMode::GolombRice);
}

TEST(SliceEncodeExecutorTest, FailedStatefulFramePreservesReferenceState)
{
    auto stream = make_stream(10);
    stream.intra_only = false;
    const std::array<std::uint16_t, 8> valid_storage{
        0, 17, 93, 255,
        71, 19, 201, 3,
    };
    const mffv1::PlaneView valid_plane{
        valid_storage.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt16, 4, 2, 8},
    };
    const mffv1::FrameView valid_input{&valid_plane, 1};
    mffv1::codec::SliceEncodeExecutor executor(stream, 4);
    std::vector<std::byte> frame;
    ASSERT_TRUE(executor.encode(valid_input, true, frame).ok());
    ASSERT_TRUE(executor.has_reference_state());
    const auto reference_frame = frame;

    const std::array<std::uint16_t, 8> invalid_storage{
        0, 17, 1024, 255,
        71, 19, 201, 3,
    };
    const mffv1::PlaneView invalid_plane{
        invalid_storage.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt16, 4, 2, 8},
    };
    const mffv1::FrameView invalid_input{&invalid_plane, 1};
    frame.assign({std::byte{0xaa}});

    const auto status = executor.encode(invalid_input, false, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "input sample exceeds configured bit depth");
    EXPECT_TRUE(executor.has_reference_state());
    EXPECT_EQ(frame, (std::vector<std::byte>{std::byte{0xaa}}));

    std::vector<std::byte> next_keyframe;
    ASSERT_TRUE(executor.encode(valid_input, true, next_keyframe).ok());
    EXPECT_EQ(next_keyframe, reference_frame);
}

} // namespace
