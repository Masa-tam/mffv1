#include "codec/frame_parser.hpp"
#include "codec/slice_decoder.hpp"
#include "codec/slice_executor.hpp"
#include "codec/slice_footer_writer.hpp"
#include "util/crc32.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>

#include <gtest/gtest.h>

namespace {

class ScriptedUnsignedReader final : public mffv1::entropy::SymbolReader {
public:
    explicit ScriptedUnsignedReader(std::deque<std::uint64_t> values,
                                    std::uint64_t bytes_per_read,
                                    bool keyframe = true)
        : values_(std::move(values))
        , bytes_per_read_(bytes_per_read)
        , keyframe_(keyframe)
    {
    }

    mffv1::Status read_bool(bool& out_value) override
    {
        if (bool_read_) {
            return mffv1::make_error(mffv1::ErrorCode::InternalError, "unexpected second bool read");
        }
        out_value = keyframe_;
        bool_read_ = true;
        byte_position_ += bytes_per_read_;
        return mffv1::ok_status();
    }

    mffv1::Status read_unsigned(std::uint64_t& out_value) override
    {
        if (values_.empty()) {
            return mffv1::make_error(mffv1::ErrorCode::SyntaxError, "scripted reader underflow");
        }
        out_value = values_.front();
        values_.pop_front();
        byte_position_ += bytes_per_read_;
        return mffv1::ok_status();
    }

    mffv1::Status read_signed(std::int64_t&) override
    {
        return mffv1::make_error(mffv1::ErrorCode::InternalError, "unexpected signed read");
    }

    std::uint64_t byte_position() const noexcept override
    {
        return byte_position_;
    }

    std::size_t remaining_value_count() const noexcept
    {
        return values_.size();
    }

private:
    std::deque<std::uint64_t> values_;
    std::uint64_t bytes_per_read_ = 0;
    std::uint64_t byte_position_ = 0;
    bool keyframe_ = true;
    bool bool_read_ = false;
};

mffv1::syntax::StreamParameters make_stream()
{
    mffv1::syntax::StreamParameters stream;
    stream.width = 16;
    stream.height = 8;
    stream.bits_per_raw_sample = 8;
    stream.chroma_planes = false;
    stream.num_h_slices = 1;
    stream.num_v_slices = 1;
    stream.intra_only = true;
    stream.quant_table_sets.push_back(mffv1::syntax::make_zero_quant_table_set());
    return stream;
}

mffv1::codec::FrameDecodeContext make_existing_frame()
{
    mffv1::codec::FrameDecodeContext frame;
    frame.keyframe = true;
    frame.frame_info.width = 99;
    frame.frame_info.height = 88;
    frame.frame_info.slice_count = 1;
    mffv1::syntax::SliceDescriptor slice;
    slice.index = 7;
    slice.payload_byte_offset = 123;
    slice.footer_byte_offset = 456;
    slice.slice_size = 789;
    slice.has_crc = true;
    frame.slices.push_back(slice);
    return frame;
}

void expect_existing_frame_preserved(const mffv1::codec::FrameDecodeContext& frame)
{
    EXPECT_TRUE(frame.keyframe);
    EXPECT_EQ(frame.frame_info.width, 99u);
    EXPECT_EQ(frame.frame_info.height, 88u);
    EXPECT_EQ(frame.frame_info.slice_count, 1u);
    ASSERT_EQ(frame.slices.size(), 1u);
    EXPECT_EQ(frame.slices[0].index, 7u);
    EXPECT_EQ(frame.slices[0].payload_byte_offset, 123u);
    EXPECT_EQ(frame.slices[0].footer_byte_offset, 456u);
    EXPECT_EQ(frame.slices[0].slice_size, 789u);
    EXPECT_TRUE(frame.slices[0].has_crc);
}

template <std::size_t Size>
void write_crc_parity(std::array<std::byte, Size>& payload)
{
    static_assert(Size >= 4);
    const auto crc = mffv1::util::crc32_ieee_msb(mffv1::ByteSpan(payload.data(), Size - 4));
    payload[Size - 4] = static_cast<std::byte>((crc >> 24) & 0xffu);
    payload[Size - 3] = static_cast<std::byte>((crc >> 16) & 0xffu);
    payload[Size - 2] = static_cast<std::byte>((crc >> 8) & 0xffu);
    payload[Size - 1] = static_cast<std::byte>(crc & 0xffu);
}

TEST(FrameParserTest, RejectsEmptyPayload)
{
    const auto stream = make_stream();
    mffv1::codec::FrameParser parser(stream);
    auto frame = make_existing_frame();

    const mffv1::ByteSpan empty;
    const auto status = parser.parse(empty, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "frame payload is empty");
    expect_existing_frame_preserved(frame);
}

TEST(FrameParserTest, CreatesSingleSliceDescriptor)
{
    const auto stream = make_stream();
    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext frame;
    const std::array<std::byte, 4> payload{
        std::byte{1},
        std::byte{2},
        std::byte{3},
        std::byte{4},
    };

    const auto status = parser.parse(payload, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(frame.slices.size(), 1u);
    EXPECT_EQ(frame.slices[0].index, 0u);
    EXPECT_EQ(frame.slices[0].x, 0u);
    EXPECT_EQ(frame.slices[0].y, 0u);
    EXPECT_EQ(frame.slices[0].width, stream.width);
    EXPECT_EQ(frame.slices[0].height, stream.height);
    EXPECT_EQ(frame.slices[0].raster_x, 0u);
    EXPECT_EQ(frame.slices[0].raster_y, 0u);
    EXPECT_EQ(frame.slices[0].raster_width, 1u);
    EXPECT_EQ(frame.slices[0].raster_height, 1u);
    EXPECT_EQ(frame.slices[0].payload.size(), payload.size());
    EXPECT_EQ(frame.slices[0].header_byte_offset, 0u);
    EXPECT_EQ(frame.slices[0].content_byte_offset, 0u);
    EXPECT_EQ(frame.slices[0].payload_byte_offset, 0u);
    ASSERT_EQ(frame.slices[0].quant_table_set_indexes.size(), 1u);
    EXPECT_EQ(frame.slices[0].quant_table_set_indexes[0], 0u);
    EXPECT_EQ(frame.frame_info.width, stream.width);
    EXPECT_EQ(frame.frame_info.height, stream.height);
    EXPECT_EQ(frame.frame_info.plane_count, 1u);
}

TEST(FrameParserTest, ParsesLegacyRangeKeyframeAndContentOffset)
{
    auto stream = make_stream();
    stream.version = 0;
    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext frame;
    const std::array payload{std::byte{0xff}, std::byte{0x00}};

    const auto status = parser.parse(payload, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_TRUE(frame.keyframe);
    ASSERT_EQ(frame.slices.size(), 1u);
    EXPECT_EQ(frame.slices[0].header_byte_offset, 0u);
    EXPECT_EQ(frame.slices[0].content_byte_offset, 2u);
    EXPECT_EQ(frame.slices[0].payload_byte_offset, 0u);
    EXPECT_TRUE(frame.slices[0].continues_frame_range_state);
}

TEST(FrameParserTest, RejectsLegacyRangeNonKeyframeForIntraOnlyStream)
{
    auto stream = make_stream();
    stream.version = 0;
    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext frame;
    const std::array payload{std::byte{0x00}, std::byte{0x00}};

    const auto status = parser.parse(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message,
              "non-keyframe is invalid for an intra-only stream");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
    EXPECT_TRUE(frame.slices.empty());
}

TEST(FrameParserTest, ParsesLegacyRangeNonKeyframe)
{
    auto stream = make_stream();
    stream.version = 0;
    stream.intra_only = false;
    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext frame;
    const std::array payload{std::byte{0x70}, std::byte{0x00}};

    const auto status = parser.parse(payload, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_FALSE(frame.keyframe);
    ASSERT_EQ(frame.slices.size(), 1u);
    EXPECT_EQ(frame.slices[0].content_byte_offset, 2u);
    EXPECT_TRUE(frame.slices[0].continues_frame_range_state);
}

TEST(FrameParserTest, ParsesLegacyGolombRiceKeyframeBit)
{
    auto stream = make_stream();
    stream.version = 0;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext frame;
    const std::array payload{std::byte{0xfe}};

    const auto status = parser.parse(payload, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_TRUE(frame.keyframe);
    ASSERT_EQ(frame.slices.size(), 1u);
    EXPECT_EQ(frame.slices[0].content_byte_offset, 0u);
    EXPECT_EQ(frame.slices[0].content_bit_offset, 1u);
    EXPECT_FALSE(frame.slices[0].continues_frame_range_state);
}

TEST(FrameParserTest, ParsesAndDecodesLegacyGolombRiceKeyframe)
{
    auto stream = make_stream();
    stream.version = 0;
    stream.width = 4;
    stream.height = 2;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext frame;
    const std::array payload{std::byte{0xfe}};
    ASSERT_TRUE(parser.parse(payload, frame).ok());
    ASSERT_EQ(frame.slices.size(), 1u);

    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    mffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = mffv1::PlaneRole::Y;
    plane.info.sample_format = mffv1::SampleFormat::UInt8;
    plane.info.width = 4;
    plane.info.height = 2;
    plane.info.stride_bytes = 4;
    mffv1::MutableFrameView output{&plane, 1};
    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, output, frame.slices[0]).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(frame.slices[0], window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    for (const auto sample : storage) {
        EXPECT_EQ(sample, 0u);
    }
}

TEST(FrameParserTest, RejectsLegacyGolombRiceNonKeyframeForIntraOnlyStream)
{
    auto stream = make_stream();
    stream.version = 0;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext frame;
    const std::array payload{std::byte{0x70}};

    const auto status = parser.parse(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message,
              "non-keyframe is invalid for an intra-only stream");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
    EXPECT_TRUE(frame.slices.empty());
}

TEST(FrameParserTest, ParsesLegacyGolombRiceNonKeyframe)
{
    auto stream = make_stream();
    stream.version = 0;
    stream.intra_only = false;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext frame;
    const std::array payload{std::byte{0x70}};

    const auto status = parser.parse(payload, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_FALSE(frame.keyframe);
    ASSERT_EQ(frame.slices.size(), 1u);
    EXPECT_EQ(frame.slices[0].content_byte_offset, 0u);
    EXPECT_EQ(frame.slices[0].content_bit_offset, 1u);
}

TEST(FrameParserTest, CreatesSingleSliceDescriptorFromHeaderReader)
{
    const auto stream = make_stream();
    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext frame;
    ScriptedUnsignedReader reader({0, 0, 0, 0, 0, 0, 3, 4, 3}, 1);
    const std::array<std::byte, 16> payload{
        std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3},
        std::byte{4}, std::byte{5}, std::byte{6}, std::byte{7},
        std::byte{8}, std::byte{9}, std::byte{10}, std::byte{11},
        std::byte{12}, std::byte{13}, std::byte{14}, std::byte{15},
    };

    const auto status = parser.parse_with_header_reader(payload, reader, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(frame.slices.size(), 1u);
    EXPECT_EQ(frame.slices[0].x, 0u);
    EXPECT_EQ(frame.slices[0].y, 0u);
    EXPECT_EQ(frame.slices[0].width, stream.width);
    EXPECT_EQ(frame.slices[0].height, stream.height);
    EXPECT_EQ(frame.slices[0].raster_x, 0u);
    EXPECT_EQ(frame.slices[0].raster_y, 0u);
    EXPECT_EQ(frame.slices[0].raster_width, 1u);
    EXPECT_EQ(frame.slices[0].raster_height, 1u);
    EXPECT_EQ(frame.slices[0].header_byte_offset, 1u);
    EXPECT_EQ(frame.slices[0].content_byte_offset, 10u);
    EXPECT_EQ(frame.slices[0].picture_structure, 3u);
    EXPECT_EQ(frame.slices[0].sar_num, 4u);
    EXPECT_EQ(frame.slices[0].sar_den, 3u);
    EXPECT_TRUE(frame.keyframe);
    EXPECT_EQ(frame.slices[0].payload.size(), payload.size());
    ASSERT_EQ(frame.slices[0].quant_table_set_indexes.size(), 2u);
    EXPECT_EQ(frame.slices[0].quant_table_set_indexes[0], 0u);
    EXPECT_EQ(frame.slices[0].quant_table_set_indexes[1], 0u);
}

TEST(FrameParserTest, CreatesMultiSliceDescriptorsFromHeaderReader)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext frame;
    ScriptedUnsignedReader reader({
        0, 0, 0, 0, 0, 0, 3, 4, 3,
        1, 0, 0, 0, 0, 0, 3, 4, 3,
    }, 1);
    const std::array<std::byte, 20> payload{};

    const auto status = parser.parse_with_header_reader(payload, reader, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_TRUE(frame.keyframe);
    ASSERT_EQ(frame.slices.size(), 2u);
    EXPECT_EQ(frame.slices[0].index, 0u);
    EXPECT_EQ(frame.slices[0].x, 0u);
    EXPECT_EQ(frame.slices[0].width, 8u);
    EXPECT_EQ(frame.slices[0].raster_x, 0u);
    EXPECT_EQ(frame.slices[0].raster_width, 1u);
    EXPECT_EQ(frame.slices[0].header_byte_offset, 1u);
    EXPECT_EQ(frame.slices[0].content_byte_offset, 10u);
    EXPECT_EQ(frame.slices[0].payload.size(), payload.size());
    EXPECT_EQ(frame.slices[1].index, 1u);
    EXPECT_EQ(frame.slices[1].x, 8u);
    EXPECT_EQ(frame.slices[1].width, 8u);
    EXPECT_EQ(frame.slices[1].raster_x, 1u);
    EXPECT_EQ(frame.slices[1].raster_width, 1u);
    EXPECT_EQ(frame.slices[1].header_byte_offset, 10u);
    EXPECT_EQ(frame.slices[1].content_byte_offset, 19u);
    EXPECT_EQ(frame.slices[1].payload.size(), payload.size());
    EXPECT_EQ(frame.frame_info.slice_count, 2u);
}

TEST(FrameParserTest, CreatesMultiSliceNonKeyframeFromHeaderReader)
{
    auto stream = make_stream();
    stream.intra_only = false;
    stream.num_h_slices = 2;
    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext frame;
    ScriptedUnsignedReader reader({
        0, 0, 0, 0, 0, 0, 3, 4, 3,
        1, 0, 0, 0, 0, 0, 3, 4, 3,
    }, 1, false);
    const std::array<std::byte, 20> payload{};

    const auto status = parser.parse_with_header_reader(payload, reader, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_FALSE(frame.keyframe);
    EXPECT_FALSE(frame.frame_info.keyframe);
    EXPECT_EQ(frame.frame_info.slice_count, 2u);
    ASSERT_EQ(frame.slices.size(), 2u);
    EXPECT_EQ(frame.slices[0].raster_x, 0u);
    EXPECT_EQ(frame.slices[1].raster_x, 1u);
}

TEST(FrameParserTest, FailedHeaderReaderMultiSliceDoesNotExposeParsedPrefix)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    mffv1::codec::FrameParser parser(stream);
    auto frame = make_existing_frame();
    ScriptedUnsignedReader reader({0, 0, 0, 0, 0, 0, 3, 4, 3}, 1);
    const std::array<std::byte, 20> payload{};

    const auto status = parser.parse_with_header_reader(payload, reader, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "scripted reader underflow");
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 1u);
    expect_existing_frame_preserved(frame);
}

TEST(FrameParserTest, RejectsOverlappingHeaderReaderSlicesWithoutChangingFrame)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    mffv1::codec::FrameParser parser(stream);
    auto frame = make_existing_frame();
    ScriptedUnsignedReader reader({
        0, 0, 0, 0, 0, 0, 3, 4, 3,
        0, 0, 0, 0, 0, 0, 3, 4, 3,
    }, 1);
    const std::array<std::byte, 20> payload{};

    const auto status = parser.parse_with_header_reader(payload, reader, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice raster rectangles overlap");
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 1u);
    expect_existing_frame_preserved(frame);
}

TEST(FrameParserTest, RejectsOutOfRasterHeaderReaderSliceWithoutChangingFrame)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    mffv1::codec::FrameParser parser(stream);
    auto frame = make_existing_frame();
    ScriptedUnsignedReader reader({
        0, 0, 0, 0, 0, 0, 3, 4, 3,
        3,
    }, 1);
    const std::array<std::byte, 20> payload{};

    const auto status = parser.parse_with_header_reader(payload, reader, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice_x is outside the slice raster");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 11u);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 1u);
    expect_existing_frame_preserved(frame);
}

TEST(FrameParserTest, RejectsSecondHeaderReaderSlicePastPayloadWithoutChangingFrame)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    mffv1::codec::FrameParser parser(stream);
    auto frame = make_existing_frame();
    ScriptedUnsignedReader reader({
        0, 0, 0, 0, 0, 0, 3, 4, 3,
        1, 0, 0, 0, 0, 0, 3, 4, 3,
    }, 1);
    const std::array<std::byte, 12> payload{};

    const auto status = parser.parse_with_header_reader(payload, reader, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message,
              "slice header consumes more bytes than the frame payload contains");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 19u);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 1u);
    expect_existing_frame_preserved(frame);
}

TEST(FrameParserTest, HeaderReaderStopsAfterRasterCoverageIsComplete)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext frame;
    ScriptedUnsignedReader reader({
        0, 0, 1, 0, 0, 0, 3, 4, 3,
        1, 0, 0, 0,
    }, 1);
    const std::array<std::byte, 20> payload{};

    const auto status = parser.parse_with_header_reader(payload, reader, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(frame.slices.size(), 1u);
    EXPECT_EQ(frame.slices[0].raster_x, 0u);
    EXPECT_EQ(frame.slices[0].raster_width, 2u);
    EXPECT_EQ(frame.frame_info.slice_count, 1u);
    EXPECT_EQ(reader.remaining_value_count(), 4u);
}

TEST(FrameParserTest, RejectsHeaderReaderThatConsumesPastPayload)
{
    const auto stream = make_stream();
    mffv1::codec::FrameParser parser(stream);
    auto frame = make_existing_frame();
    ScriptedUnsignedReader reader({0, 0, 0, 0, 0, 0, 0, 0, 0}, 2);
    const std::array<std::byte, 8> payload{
        std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3},
        std::byte{4}, std::byte{5}, std::byte{6}, std::byte{7},
    };

    const auto status = parser.parse_with_header_reader(payload, reader, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message,
              "slice header consumes more bytes than the frame payload contains");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 20u);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 0u);
    expect_existing_frame_preserved(frame);
}

TEST(FrameParserTest, RejectsNonKeyframeForIntraOnlyStream)
{
    const auto stream = make_stream();
    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext frame;
    ScriptedUnsignedReader reader({0, 0, 0, 0, 0, 0, 0, 0, 0}, 1, false);
    const std::array<std::byte, 16> payload{};

    const auto status = parser.parse_with_header_reader(payload, reader, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message,
              "non-keyframe is invalid for an intra-only stream");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
    EXPECT_FALSE(frame.keyframe);
    EXPECT_TRUE(frame.slices.empty());
}

TEST(FrameParserTest, AcceptsNonKeyframeForNonIntraStream)
{
    auto stream = make_stream();
    stream.intra_only = false;
    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext frame;
    ScriptedUnsignedReader reader({0, 0, 0, 0, 0, 0, 0, 0, 0}, 1, false);
    const std::array<std::byte, 16> payload{};

    const auto status = parser.parse_with_header_reader(payload, reader, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_FALSE(frame.keyframe);
    ASSERT_EQ(frame.slices.size(), 1u);
}

TEST(FrameParserTest, AcceptsKeyframeForNonIntraStream)
{
    auto stream = make_stream();
    stream.intra_only = false;
    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext frame;
    ScriptedUnsignedReader reader({0, 0, 0, 0, 0, 0, 0, 0, 0}, 1, true);
    const std::array<std::byte, 16> payload{};

    const auto status = parser.parse_with_header_reader(payload, reader, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_TRUE(frame.keyframe);
    ASSERT_EQ(frame.slices.size(), 1u);
}

TEST(FrameParserTest, RejectsTooShortRangeHeaderPayload)
{
    const auto stream = make_stream();
    mffv1::codec::FrameParser parser(stream);
    auto frame = make_existing_frame();
    const std::array<std::byte, 1> payload{std::byte{0}};

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 0u);
    expect_existing_frame_preserved(frame);
}

TEST(FrameParserTest, RejectsUnknownDimensionsWithoutChangingFrame)
{
    auto stream = make_stream();
    stream.width = 0;
    mffv1::codec::FrameParser parser(stream);
    auto frame = make_existing_frame();
    const std::array<std::byte, 5> payload{
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x05},
    };

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message,
              "stream dimensions must be known before parsing frames");
    expect_existing_frame_preserved(frame);
}

TEST(FrameParserTest, RejectsZeroSliceGridWithoutChangingFrame)
{
    auto stream = make_stream();
    stream.num_h_slices = 0;
    mffv1::codec::FrameParser parser(stream);
    auto frame = make_existing_frame();
    const std::array<std::byte, 5> payload{
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x05},
    };

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "slice grid dimensions must be non-zero");
    expect_existing_frame_preserved(frame);
}

TEST(FrameParserTest, RejectsInvalidSliceHeaderThroughRangeCoder)
{
    const auto stream = make_stream();
    mffv1::codec::FrameParser parser(stream);
    auto frame = make_existing_frame();
    const std::array<std::byte, 4> payload{
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
    };

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    expect_existing_frame_preserved(frame);
}

TEST(FrameParserTest, RejectsRangeCodedNonKeyframeForIntraOnlyStream)
{
    const auto stream = make_stream();
    mffv1::codec::FrameParser parser(stream);
    auto frame = make_existing_frame();
    const std::array<std::byte, 5> payload{
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x05},
    };

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message,
              "non-keyframe is invalid for an intra-only stream");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 0u);
    expect_existing_frame_preserved(frame);
}

TEST(FrameParserTest, AcceptsRangeCodedNonKeyframeForNonIntraStream)
{
    auto stream = make_stream();
    stream.intra_only = false;
    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext frame;
    const std::array<std::byte, 5> payload{
        std::byte{0x7f},
        std::byte{0x7f},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x05},
    };

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_FALSE(frame.keyframe);
    EXPECT_FALSE(frame.frame_info.keyframe);
    EXPECT_EQ(frame.frame_info.slice_count, 1u);
    ASSERT_EQ(frame.slices.size(), 1u);
    EXPECT_EQ(frame.slices[0].raster_x, 0u);
    EXPECT_EQ(frame.slices[0].raster_y, 0u);
    EXPECT_EQ(frame.slices[0].raster_width, 1u);
    EXPECT_EQ(frame.slices[0].raster_height, 1u);
}

TEST(FrameParserTest, ParsesAndDecodesRangeCodedNonKeyframeAfterKeyframe)
{
    auto stream = make_stream();
    stream.width = 1;
    stream.height = 1;
    stream.intra_only = false;
    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::SliceExecutor executor(stream);
    std::array<std::uint8_t, 1> storage{0xee};
    mffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = mffv1::PlaneRole::Y;
    plane.info.sample_format = mffv1::SampleFormat::UInt8;
    plane.info.width = 1;
    plane.info.height = 1;
    plane.info.stride_bytes = 1;
    mffv1::MutableFrameView output{&plane, 1};

    const std::array<std::byte, 5> keyframe_payload{
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x05},
    };
    mffv1::codec::FrameDecodeContext keyframe;
    ASSERT_TRUE(parser.parse_with_range_header(keyframe_payload, keyframe).ok());
    ASSERT_TRUE(keyframe.keyframe);
    ASSERT_TRUE(executor.decode(output, keyframe.slices, keyframe.keyframe).ok());
    ASSERT_TRUE(executor.has_reference_state());

    const std::array<std::byte, 5> non_keyframe_payload{
        std::byte{0x7f},
        std::byte{0x7f},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x05},
    };
    mffv1::codec::FrameDecodeContext non_keyframe;
    ASSERT_TRUE(parser.parse_with_range_header(non_keyframe_payload, non_keyframe).ok());
    ASSERT_FALSE(non_keyframe.keyframe);

    const auto status = executor.decode(output, non_keyframe.slices, non_keyframe.keyframe);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_TRUE(executor.has_reference_state());
}

TEST(FrameParserTest, CreatesSingleSliceDescriptorFromRangeHeader)
{
    const auto stream = make_stream();
    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext frame;
    const std::array<std::byte, 7> payload{
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x07},
    };

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(frame.slices.size(), 1u);
    EXPECT_EQ(frame.slices[0].x, 0u);
    EXPECT_EQ(frame.slices[0].y, 0u);
    EXPECT_EQ(frame.slices[0].width, stream.width);
    EXPECT_EQ(frame.slices[0].height, stream.height);
    EXPECT_EQ(frame.slices[0].raster_x, 0u);
    EXPECT_EQ(frame.slices[0].raster_y, 0u);
    EXPECT_EQ(frame.slices[0].raster_width, 1u);
    EXPECT_EQ(frame.slices[0].raster_height, 1u);
    EXPECT_EQ(frame.slices[0].header_byte_offset, 2u);
    EXPECT_EQ(frame.slices[0].content_byte_offset, 2u);
    EXPECT_EQ(frame.slices[0].footer_byte_offset, 4u);
    EXPECT_EQ(frame.slices[0].slice_size, payload.size());
    EXPECT_EQ(frame.slices[0].payload.size(), payload.size());
    EXPECT_TRUE(frame.keyframe);
    ASSERT_EQ(frame.slices[0].quant_table_set_indexes.size(), 2u);
    EXPECT_EQ(frame.slices[0].quant_table_set_indexes[0], 0u);
    EXPECT_EQ(frame.slices[0].quant_table_set_indexes[1], 0u);
}

TEST(FrameParserTest, VerifiesSingleSliceCrc)
{
    auto stream = make_stream();
    stream.error_status_enabled = true;
    mffv1::codec::FrameParser parser(stream, true);
    mffv1::codec::FrameDecodeContext frame;
    std::array<std::byte, 12> payload{
        std::byte{0xff}, std::byte{0x00}, std::byte{0xff}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x0c}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00},
    };
    write_crc_parity(payload);

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(frame.slices.size(), 1u);
    EXPECT_TRUE(frame.slices[0].has_crc);
    EXPECT_EQ(frame.slices[0].footer_byte_offset, 4u);
    EXPECT_EQ(frame.slices[0].slice_size, payload.size());
}

TEST(FrameParserTest, RejectsSingleSliceCrcMismatch)
{
    auto stream = make_stream();
    stream.error_status_enabled = true;
    mffv1::codec::FrameParser parser(stream, true);
    auto frame = make_existing_frame();
    std::array<std::byte, 12> payload{
        std::byte{0xff}, std::byte{0x00}, std::byte{0xff}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x0c}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00},
    };
    write_crc_parity(payload);
    payload.back() ^= std::byte{0x01};

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::CrcMismatch);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 0u);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 8u);
    expect_existing_frame_preserved(frame);
}

TEST(FrameParserTest, RejectsMultiSliceCrcMismatchWithoutChangingFrame)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    stream.error_status_enabled = true;
    mffv1::codec::FrameParser parser(stream, true);
    auto frame = make_existing_frame();
    const mffv1::codec::SliceFooterWriter footer_writer;
    std::vector<std::byte> first_slice{std::byte{0xff}, std::byte{0x00}};
    ASSERT_TRUE(footer_writer.append(stream, 0, first_slice).ok());
    first_slice.back() ^= std::byte{0x01};
    std::vector<std::byte> second_slice{std::byte{0xff}, std::byte{0x00}};
    ASSERT_TRUE(footer_writer.append(stream, 0, second_slice).ok());
    std::vector<std::byte> payload = first_slice;
    payload.insert(payload.end(), second_slice.begin(), second_slice.end());

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::CrcMismatch);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 0u);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 6u);
    expect_existing_frame_preserved(frame);
}

TEST(FrameParserTest, ReportsLegacyMultiSliceAsNotImplemented)
{
    auto stream = make_stream();
    stream.version = 0;
    stream.num_h_slices = 2;
    mffv1::codec::FrameParser parser(stream);
    auto frame = make_existing_frame();
    const std::array<std::byte, 1> payload{std::byte{0}};

    const auto status = parser.parse(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::NotImplemented);
    EXPECT_EQ(status.message, "multi-slice frame parsing is not implemented yet");
    expect_existing_frame_preserved(frame);
}

TEST(FrameParserTest, ParsesVersionThreeMultiSliceThroughDefaultParse)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext frame;
    const std::array payload{
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x07},
        std::byte{0x3d},
        std::byte{0x34},
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x07},
    };

    const auto status = parser.parse(payload, frame);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_TRUE(frame.keyframe);
    ASSERT_EQ(frame.slices.size(), 2u);
    EXPECT_EQ(frame.slices[0].index, 0u);
    EXPECT_EQ(frame.slices[0].payload_byte_offset, 0u);
    EXPECT_EQ(frame.slices[0].slice_size, 7u);
    EXPECT_EQ(frame.slices[1].index, 1u);
    EXPECT_EQ(frame.slices[1].payload_byte_offset, 7u);
    EXPECT_EQ(frame.slices[1].slice_size, 7u);
    EXPECT_EQ(frame.frame_info.slice_count, 2u);
}

TEST(FrameParserTest, DefaultParseRejectsVersionThreeMultiSliceCrcMismatch)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    stream.error_status_enabled = true;
    mffv1::codec::FrameParser parser(stream, true);
    auto frame = make_existing_frame();
    const mffv1::codec::SliceFooterWriter footer_writer;
    std::vector<std::byte> first_slice{std::byte{0xff}, std::byte{0x00}};
    ASSERT_TRUE(footer_writer.append(stream, 0, first_slice).ok());
    first_slice.back() ^= std::byte{0x01};
    std::vector<std::byte> second_slice{std::byte{0xff}, std::byte{0x00}};
    ASSERT_TRUE(footer_writer.append(stream, 0, second_slice).ok());
    std::vector<std::byte> payload = first_slice;
    payload.insert(payload.end(), second_slice.begin(), second_slice.end());

    const auto status = parser.parse(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::CrcMismatch);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 0u);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 6u);
    expect_existing_frame_preserved(frame);
}

TEST(FrameParserTest, RejectsMultiSliceRangePayloadTooSmallForFooter)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    mffv1::codec::FrameParser parser(stream);
    auto frame = make_existing_frame();
    const std::array<std::byte, 2> payload{
        std::byte{0},
        std::byte{0},
    };

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
    expect_existing_frame_preserved(frame);
}

TEST(FrameParserTest, RejectsMalformedSingleSliceInMultiCellRaster)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    mffv1::codec::FrameParser parser(stream);
    auto frame = make_existing_frame();
    const std::array payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x05},
    };

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 2u);
    expect_existing_frame_preserved(frame);
}

TEST(FrameParserTest, CreatesMultiSliceDescriptorsFromLocatedRangeHeaders)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext frame;
    const std::array payload{
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x07},
        std::byte{0x3d},
        std::byte{0x34},
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x07},
    };

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(frame.slices.size(), 2u);
    EXPECT_TRUE(frame.keyframe);
    EXPECT_EQ(frame.slices[0].index, 0u);
    EXPECT_EQ(frame.slices[0].x, 0u);
    EXPECT_EQ(frame.slices[0].y, 0u);
    EXPECT_EQ(frame.slices[0].width, 8u);
    EXPECT_EQ(frame.slices[0].height, stream.height);
    EXPECT_EQ(frame.slices[0].raster_x, 0u);
    EXPECT_EQ(frame.slices[0].raster_y, 0u);
    EXPECT_EQ(frame.slices[0].raster_width, 1u);
    EXPECT_EQ(frame.slices[0].raster_height, 1u);
    EXPECT_EQ(frame.slices[0].payload_byte_offset, 0u);
    EXPECT_EQ(frame.slices[0].header_byte_offset, 2u);
    EXPECT_EQ(frame.slices[0].content_byte_offset, 2u);
    EXPECT_EQ(frame.slices[0].footer_byte_offset, 4u);
    EXPECT_EQ(frame.slices[0].slice_size, 7u);
    EXPECT_EQ(frame.slices[0].payload.size(), 7u);

    EXPECT_EQ(frame.slices[1].index, 1u);
    EXPECT_EQ(frame.slices[1].x, 8u);
    EXPECT_EQ(frame.slices[1].y, 0u);
    EXPECT_EQ(frame.slices[1].width, 8u);
    EXPECT_EQ(frame.slices[1].height, stream.height);
    EXPECT_EQ(frame.slices[1].raster_x, 1u);
    EXPECT_EQ(frame.slices[1].raster_y, 0u);
    EXPECT_EQ(frame.slices[1].raster_width, 1u);
    EXPECT_EQ(frame.slices[1].raster_height, 1u);
    EXPECT_EQ(frame.slices[1].payload_byte_offset, 7u);
    EXPECT_EQ(frame.slices[1].header_byte_offset, 9u);
    EXPECT_EQ(frame.slices[1].content_byte_offset, 10u);
    EXPECT_EQ(frame.slices[1].footer_byte_offset, 11u);
    EXPECT_EQ(frame.slices[1].slice_size, 7u);
    EXPECT_EQ(frame.slices[1].payload.size(), 7u);
}

TEST(FrameParserTest, PreservesMultiSliceErrorStatusFromLocatedRangeHeaders)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    stream.error_status_enabled = true;
    const mffv1::codec::SliceFooterWriter footer_writer;
    std::vector<std::byte> first_slice{
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0xff},
        std::byte{0x00},
    };
    ASSERT_TRUE(footer_writer.append(stream, 1, first_slice).ok());
    std::vector<std::byte> second_slice{
        std::byte{0x3d},
        std::byte{0x34},
        std::byte{0xff},
        std::byte{0x00},
    };
    ASSERT_TRUE(footer_writer.append(stream, 2, second_slice).ok());
    std::vector<std::byte> payload = first_slice;
    payload.insert(payload.end(), second_slice.begin(), second_slice.end());
    mffv1::codec::FrameParser parser(stream, true);
    mffv1::codec::FrameDecodeContext frame;

    const auto status = parser.parse_with_range_header(payload, frame);

    ASSERT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(frame.slices.size(), 2u);
    EXPECT_TRUE(frame.slices[0].has_crc);
    EXPECT_EQ(frame.slices[0].error_status, 1u);
    EXPECT_EQ(frame.slices[0].slice_size, first_slice.size());
    EXPECT_TRUE(frame.slices[1].has_crc);
    EXPECT_EQ(frame.slices[1].error_status, 2u);
    EXPECT_EQ(frame.slices[1].slice_size, second_slice.size());
}

TEST(FrameParserTest, AcceptsSingleSliceCoveringMultipleRasterCells)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext frame;
    const std::array payload{
        std::byte{0xe3},
        std::byte{0xfe},
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x07},
    };

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(frame.slices.size(), 1u);
    EXPECT_TRUE(frame.keyframe);
    EXPECT_EQ(frame.slices[0].raster_x, 0u);
    EXPECT_EQ(frame.slices[0].raster_y, 0u);
    EXPECT_EQ(frame.slices[0].raster_width, 2u);
    EXPECT_EQ(frame.slices[0].raster_height, 1u);
    EXPECT_EQ(frame.slices[0].x, 0u);
    EXPECT_EQ(frame.slices[0].width, stream.width);
}

TEST(FrameParserTest, RejectsMultiSliceRangeHeaderWithSliceLocation)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    mffv1::codec::FrameParser parser(stream);
    auto frame = make_existing_frame();
    const std::array payload{
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x03},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x03},
    };

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_NE(status.code, mffv1::ErrorCode::NotImplemented);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 0u);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
    expect_existing_frame_preserved(frame);
}

TEST(FrameParserTest, DoesNotExposeParsedPrefixWhenLaterSliceHeaderFails)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    mffv1::codec::FrameParser parser(stream);
    auto frame = make_existing_frame();
    const std::array payload{
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x07},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x03},
    };

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 1u);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 9u);
    expect_existing_frame_preserved(frame);
}

} // namespace
