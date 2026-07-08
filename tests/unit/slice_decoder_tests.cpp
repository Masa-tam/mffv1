#include "codec/slice_decoder.hpp"
#include "mffv1/configuration_parser.hpp"
#include "bitstream/bit_writer.hpp"
#include "entropy/golomb_rice_context.hpp"
#include "entropy/golomb_rice_run.hpp"
#include "entropy/golomb_rice_writer.hpp"
#include "entropy/range_encoder.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

mffv1::syntax::StreamParameters make_stream()
{
    mffv1::syntax::StreamParameters stream;
    stream.width = 4;
    stream.height = 2;
    stream.version = 0;
    stream.bits_per_raw_sample = 8;
    stream.chroma_planes = false;
    stream.quant_table_sets.push_back(mffv1::syntax::make_zero_quant_table_set());
    return stream;
}

mffv1::MutablePlaneView make_plane(std::array<std::uint8_t, 8>& storage)
{
    mffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = mffv1::PlaneRole::Y;
    plane.info.sample_format = mffv1::SampleFormat::UInt8;
    plane.info.width = 4;
    plane.info.height = 2;
    plane.info.stride_bytes = 4;
    return plane;
}

mffv1::MutablePlaneView make_u16_plane(std::array<std::uint16_t, 8>& storage)
{
    mffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = mffv1::PlaneRole::Y;
    plane.info.sample_format = mffv1::SampleFormat::UInt16;
    plane.info.width = 4;
    plane.info.height = 2;
    plane.info.stride_bytes = 8;
    return plane;
}

class RecordingSliceObserver final : public mffv1::codec::SliceDecodeObserver {
public:
    void on_golomb_rice_sample(
        const mffv1::codec::GolombRiceSampleTrace& trace) override
    {
        traces.push_back(trace);
    }

    std::vector<mffv1::codec::GolombRiceSampleTrace> traces;
};

TEST(LineStateTest, ResetsAndSwapsLines)
{
    mffv1::syntax::LineState line;
    ASSERT_TRUE(line.reset(3).ok());
    ASSERT_EQ(line.width(), 3u);

    line.mutable_current()[1] = 42;
    line.swap_lines();

    EXPECT_EQ(line.previous()[1], 42);
    EXPECT_EQ(line.current()[1], 0);
    line.mutable_current()[2] = 7;
    line.swap_lines();
    EXPECT_EQ(line.second_previous()[1], 42);
    EXPECT_EQ(line.previous()[2], 7);
}

TEST(LineStateTest, DerivesRfcSliceBorderNeighbors)
{
    mffv1::syntax::LineState line;
    ASSERT_TRUE(line.reset(3).ok());
    line.mutable_current() = {10, 20, 30};
    line.swap_lines();

    line.mutable_current()[0] = 40;
    auto neighbors = line.neighbors(0);
    EXPECT_EQ(neighbors.far_left, 0);
    EXPECT_EQ(neighbors.left, 10);
    EXPECT_EQ(neighbors.top, 10);
    EXPECT_EQ(neighbors.top_left, 0);
    EXPECT_EQ(neighbors.top_right, 20);
    EXPECT_EQ(neighbors.top_top, 0);

    neighbors = line.neighbors(1);
    EXPECT_EQ(neighbors.far_left, 10);
    EXPECT_EQ(neighbors.left, 40);
    EXPECT_EQ(neighbors.top, 20);
    EXPECT_EQ(neighbors.top_left, 10);
    EXPECT_EQ(neighbors.top_right, 30);
    EXPECT_EQ(neighbors.top_top, 0);

    line.mutable_current()[1] = 50;
    line.mutable_current()[2] = 60;
    line.swap_lines();
    neighbors = line.neighbors(0);
    EXPECT_EQ(neighbors.left, 40);
    EXPECT_EQ(neighbors.top_left, 10);
    EXPECT_EQ(neighbors.top_top, 10);
}

TEST(SliceStateTest, ResetsOneLineStatePerCodedPlane)
{
    const auto stream = make_stream();
    mffv1::codec::SliceState state;

    const auto status = state.reset(stream);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(state.plane_count(), 1u);
    EXPECT_EQ(state.line_state(0).width(), stream.width);
}

TEST(SliceStateTest, KeepsExtraPlaneFullWidthWhenChromaIsAbsent)
{
    auto stream = make_stream();
    stream.extra_plane = true;
    stream.log2_h_chroma_subsample = 1;
    mffv1::codec::SliceState state;

    const auto status = state.reset(stream);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(state.plane_count(), 2u);
    EXPECT_EQ(state.line_state(0).width(), stream.width);
    EXPECT_EQ(state.line_state(1).width(), stream.width);
}

TEST(SliceStateTest, UsesSliceOutputPlaneWidths)
{
    auto stream = make_stream();
    stream.chroma_planes = true;
    stream.log2_h_chroma_subsample = 1;

    std::array<std::uint8_t, 8> y{};
    std::array<std::uint8_t, 4> cb{};
    std::array<std::uint8_t, 4> cr{};
    std::array<mffv1::MutablePlaneView, 3> planes{};
    planes[0].data = y.data();
    planes[0].info = {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 4, 2, 4};
    planes[1].data = cb.data();
    planes[1].info = {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt8, 2, 2, 2};
    planes[2].data = cr.data();
    planes[2].info = {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt8, 2, 2, 2};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 2;
    slice.height = 2;
    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());

    mffv1::codec::SliceState state;
    const auto status = state.reset(window);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(state.plane_count(), 3u);
    EXPECT_EQ(state.line_state(0).width(), 2u);
    EXPECT_EQ(state.line_state(1).width(), 1u);
    EXPECT_EQ(state.line_state(2).width(), 1u);
}

TEST(SliceStateTest, PreservesCapturedRangeContextsAcrossLineReset)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    mffv1::entropy::RangeCoder reader;
    ASSERT_TRUE(reader.reset(payload).ok());
    std::int64_t value = 99;
    ASSERT_TRUE(reader.read_signed(value).ok());

    auto stream = make_stream();
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.capture_range_contexts(reader).ok());
    const auto captured = state.range_contexts();

    const auto status = state.reset(stream);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_TRUE(state.has_range_contexts());
    EXPECT_EQ(state.range_contexts(), captured);
}

TEST(SliceStateTest, ClearsCapturedRangeContextsForKeyframeReset)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    mffv1::entropy::RangeCoder reader;
    ASSERT_TRUE(reader.reset(payload).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.capture_range_contexts(reader).ok());

    state.clear_range_contexts();

    EXPECT_FALSE(state.has_range_contexts());
    EXPECT_TRUE(state.range_contexts().empty());
}

TEST(SliceStateTest, PreservesGolombRiceStateAcrossLineReset)
{
    auto stream = make_stream();
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());
    const std::array<std::size_t, 1> context_counts{1};
    ASSERT_TRUE(state.prepare_golomb_rice(context_counts).ok());
    state.golomb_rice_context(0, 0).count = 7;
    state.golomb_rice_run_state(0).run_index = 4;

    ASSERT_TRUE(state.reset(stream).ok());
    const auto status = state.prepare_golomb_rice(context_counts);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_TRUE(state.has_golomb_rice_state());
    EXPECT_EQ(state.golomb_rice_context(0, 0).count, 7);
    EXPECT_EQ(state.golomb_rice_run_state(0).run_index, 4u);
}

TEST(SliceStateTest, RejectsGolombRiceContextCountChangeWithoutResettingState)
{
    auto stream = make_stream();
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());
    const std::array<std::size_t, 1> initial_context_counts{1};
    ASSERT_TRUE(state.prepare_golomb_rice(initial_context_counts).ok());
    state.golomb_rice_context(0, 0).count = 7;
    state.golomb_rice_run_state(0).run_index = 4;
    const std::array<std::size_t, 1> changed_context_counts{2};

    const auto status = state.prepare_golomb_rice(changed_context_counts);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(state.golomb_rice_context(0, 0).count, 7);
    EXPECT_EQ(state.golomb_rice_run_state(0).run_index, 4u);
}

TEST(SliceDecoderTest, RejectsEmptyPayload)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.content_byte_offset = 0;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());
    const std::array<std::byte, 2> previous_payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    mffv1::entropy::RangeCoder previous_reader;
    ASSERT_TRUE(previous_reader.reset(previous_payload).ok());
    ASSERT_TRUE(state.capture_range_contexts(previous_reader).ok());
    const auto previous_contexts = state.range_contexts();

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
    EXPECT_EQ(state.range_contexts(), previous_contexts);
}

TEST(SliceDecoderTest, RejectsInitialStateCountThatDoesNotMatchQuantizationContexts)
{
    auto stream = make_stream();
    stream.initial_states.resize(1);
    stream.initial_states[0].contexts.resize(2);
    std::array<std::uint8_t, 8> storage{};
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
}

TEST(SliceDecoderTest, RestoresCapturedRangeContexts)
{
    auto stream = make_stream();
    stream.width = 1;
    stream.height = 1;
    std::array<std::uint8_t, 1> storage{0xee};
    mffv1::MutablePlaneView plane{
        storage.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 1, 1, 1},
    };
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0x80}, std::byte{0x00}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    mffv1::entropy::RangeCoder::ScalarContextStates context_states{};
    context_states.fill(1);
    const std::array<mffv1::entropy::RangeCoder::ScalarContextStates, 1> context_bank{
        context_states};
    const std::span<const mffv1::entropy::RangeCoder::ScalarContextStates> context_bank_span{
        context_bank.data(), context_bank.size()};
    const std::array<std::span<const mffv1::entropy::RangeCoder::ScalarContextStates>, 1>
        initial_state_banks{context_bank_span};
    const std::array<std::size_t, 1> context_counts{1};
    mffv1::entropy::RangeCoder previous_reader;
    ASSERT_TRUE(previous_reader.reset(payload, context_counts, initial_state_banks).ok());
    ASSERT_TRUE(state.capture_range_contexts(previous_reader).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_NE(storage[0], 0u);
}

TEST(SliceDecoderTest, RejectsCapturedRangeContextCountMismatch)
{
    auto stream = make_stream();
    stream.width = 1;
    stream.height = 1;
    std::array<std::uint8_t, 1> storage{0xee};
    mffv1::MutablePlaneView plane{
        storage.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 1, 1, 1},
    };
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());
    const std::array<std::size_t, 1> context_counts{2};
    mffv1::entropy::RangeCoder previous_reader;
    ASSERT_TRUE(previous_reader.reset(payload, context_counts).ok());
    ASSERT_TRUE(state.capture_range_contexts(previous_reader).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(storage[0], 0xee);
}

TEST(SliceDecoderTest, RejectsContentOffsetOutsidePayload)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.content_byte_offset = 3;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice content offset is outside payload");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, slice.content_byte_offset);
    EXPECT_EQ(storage, (std::array<std::uint8_t, 8>{
                           0xee, 0xee, 0xee, 0xee,
                           0xee, 0xee, 0xee, 0xee,
                       }));
}

TEST(SliceDecoderTest, RejectsContentOffsetBeforePayload)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.payload_byte_offset = 10;
    slice.content_byte_offset = 9;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice content offset is before payload");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, slice.content_byte_offset);
    EXPECT_EQ(storage, (std::array<std::uint8_t, 8>{
                           0xee, 0xee, 0xee, 0xee,
                           0xee, 0xee, 0xee, 0xee,
                       }));
}

TEST(SliceDecoderTest, DecodesWithAbsoluteContentOffset)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 4> payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0xff},
        std::byte{0x00},
    };

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.payload_byte_offset = 10;
    slice.content_byte_offset = 12;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 0u);
    EXPECT_EQ(storage[1], 0xee);
}

TEST(SliceDecoderTest, ReportsRangeCoderResetErrorAtAbsoluteContentOffset)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 3> payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0xff},
    };

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.payload_byte_offset = 10;
    slice.content_byte_offset = 12;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, slice.content_byte_offset);
    EXPECT_EQ(storage, (std::array<std::uint8_t, 8>{
                           0xee, 0xee, 0xee, 0xee,
                           0xee, 0xee, 0xee, 0xee,
                       }));
}

TEST(SliceDecoderTest, DecodesOnlyContentBeforeFooter)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 7> payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x07},
    };

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.payload_byte_offset = 10;
    slice.content_byte_offset = 12;
    slice.footer_byte_offset = 14;
    slice.slice_size = static_cast<std::uint32_t>(payload.size());
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 0u);
    EXPECT_EQ(storage[1], 0xee);
}

TEST(SliceDecoderTest, RejectsFooterOffsetBeforePayload)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 4> payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0xff},
        std::byte{0x00},
    };

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.payload_byte_offset = 10;
    slice.content_byte_offset = 12;
    slice.footer_byte_offset = 9;
    slice.slice_size = static_cast<std::uint32_t>(payload.size());
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice footer offset is before payload");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, slice.footer_byte_offset);
    EXPECT_EQ(storage, (std::array<std::uint8_t, 8>{
                           0xee, 0xee, 0xee, 0xee,
                           0xee, 0xee, 0xee, 0xee,
                       }));
}

TEST(SliceDecoderTest, RejectsFooterOffsetBeforeContent)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 4> payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0xff},
        std::byte{0x00},
    };

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.payload_byte_offset = 10;
    slice.content_byte_offset = 13;
    slice.footer_byte_offset = 12;
    slice.slice_size = static_cast<std::uint32_t>(payload.size());
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice footer offset is before content");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, slice.footer_byte_offset);
    EXPECT_EQ(storage, (std::array<std::uint8_t, 8>{
                           0xee, 0xee, 0xee, 0xee,
                           0xee, 0xee, 0xee, 0xee,
                       }));
}

TEST(SliceDecoderTest, DecodesZeroDifferencesForYOnly8BitSlice)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 16> payload{
        std::byte{0xff}, std::byte{0x00}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
    };

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.content_byte_offset = 0;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    for (const auto sample : storage) {
        EXPECT_EQ(sample, 0u);
    }
}

TEST(SliceDecoderTest, DecodesZeroDifferencesForYOnly16BitSlice)
{
    auto stream = make_stream();
    stream.bits_per_raw_sample = 16;
    std::array<std::uint16_t, 8> storage{};
    storage.fill(0xffff);
    auto plane = make_u16_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 16> payload{
        std::byte{0xff}, std::byte{0x00}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
    };

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.content_byte_offset = 0;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    for (const auto sample : storage) {
        EXPECT_EQ(sample, 0u);
    }
}

TEST(SliceDecoderTest, DecodesPositiveDifferenceForYOnly16BitSlice)
{
    auto stream = make_stream();
    stream.bits_per_raw_sample = 16;
    std::array<std::uint16_t, 8> storage{};
    storage.fill(0xffff);
    auto plane = make_u16_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 2> payload{
        std::byte{0x14},
        std::byte{0x46},
    };

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.content_byte_offset = 0;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 1u);
    EXPECT_EQ(storage[1], 0xffffu);
}

TEST(SliceDecoderTest, DecodesWrappedNegativeDifferenceForYOnly16BitSlice)
{
    auto stream = make_stream();
    stream.bits_per_raw_sample = 16;
    std::array<std::uint16_t, 8> storage{};
    storage.fill(0);
    auto plane = make_u16_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 2> payload{
        std::byte{0x21},
        std::byte{0xcf},
    };

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.content_byte_offset = 0;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 65535u);
    EXPECT_EQ(storage[1], 0u);
}

TEST(SliceDecoderTest, DecodesZeroDifferencesFor8BitChromaSlice)
{
    auto stream = make_stream();
    stream.version = 3;
    stream.chroma_planes = true;
    stream.quant_table_sets.push_back(mffv1::syntax::make_zero_quant_table_set());
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = 1;

    std::array<std::uint8_t, 8> y{};
    std::array<std::uint8_t, 2> cb{};
    std::array<std::uint8_t, 2> cr{};
    std::array<mffv1::MutablePlaneView, 3> planes{};
    planes[0].data = y.data();
    planes[0].info = {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 4, 2, 4};
    planes[1].data = cb.data();
    planes[1].info = {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt8, 2, 1, 2};
    planes[2].data = cr.data();
    planes[2].info = {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt8, 2, 1, 2};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 16> payload{
        std::byte{0xff}, std::byte{0x00}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
    };

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.content_byte_offset = 0;
    slice.quant_table_set_indexes = {0, 1};

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    for (const auto sample : y) {
        EXPECT_EQ(sample, 0u);
    }
    for (const auto sample : cb) {
        EXPECT_EQ(sample, 0u);
    }
    for (const auto sample : cr) {
        EXPECT_EQ(sample, 0u);
    }
}

TEST(SliceDecoderTest, DecodesVersionThreeRangeChromaWithSharedSlotContext)
{
    auto stream = make_stream();
    stream.version = 3;
    stream.chroma_planes = true;
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = 1;
    stream.quant_table_sets.push_back(mffv1::syntax::make_zero_quant_table_set());

    const std::array<std::size_t, 2> context_counts{1, 1};
    mffv1::entropy::RangeEncoder writer;
    ASSERT_TRUE(writer.reset(context_counts).ok());
    for (std::size_t i = 0; i < 8; ++i) {
        ASSERT_TRUE(writer.write_signed(0, 0, 0).ok());
    }
    for (std::size_t i = 0; i < 2; ++i) {
        ASSERT_TRUE(writer.write_signed(1, 0, 0).ok());
    }
    for (std::size_t i = 0; i < 2; ++i) {
        ASSERT_TRUE(writer.write_signed(1, 0, 0).ok());
    }
    std::vector<std::byte> payload;
    ASSERT_TRUE(writer.finalize(payload).ok());

    std::array<std::uint8_t, 8> y{};
    std::array<std::uint8_t, 2> cb{};
    std::array<std::uint8_t, 2> cr{};
    y.fill(0xa5);
    cb.fill(0xa5);
    cr.fill(0xa5);
    std::array<mffv1::MutablePlaneView, 3> planes{};
    planes[0].data = y.data();
    planes[0].info = {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 4, 2, 4};
    planes[1].data = cb.data();
    planes[1].info = {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt8, 2, 1, 2};
    planes[2].data = cr.data();
    planes[2].info = {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt8, 2, 1, 2};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.content_byte_offset = 0;
    slice.quant_table_set_indexes = {0, 1};

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    ASSERT_TRUE(status.ok()) << status.message;
    for (const auto sample : y) {
        EXPECT_EQ(sample, 0u);
    }
    for (const auto sample : cb) {
        EXPECT_EQ(sample, 0u);
    }
    for (const auto sample : cr) {
        EXPECT_EQ(sample, 0u);
    }
    ASSERT_TRUE(state.has_range_contexts());
    EXPECT_EQ(state.range_contexts().size(), 2u);
}

TEST(SliceDecoderTest, DecodesZeroDifferencesFor16BitChromaSlice)
{
    auto stream = make_stream();
    stream.bits_per_raw_sample = 16;
    stream.chroma_planes = true;
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = 1;

    std::array<std::uint16_t, 8> y{};
    std::array<std::uint16_t, 2> cb{};
    std::array<std::uint16_t, 2> cr{};
    y.fill(0xffff);
    cb.fill(0xffff);
    cr.fill(0xffff);
    std::array<mffv1::MutablePlaneView, 3> planes{};
    planes[0].data = y.data();
    planes[0].info = {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt16, 4, 2, 8};
    planes[1].data = cb.data();
    planes[1].info = {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt16, 2, 1, 4};
    planes[2].data = cr.data();
    planes[2].info = {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt16, 2, 1, 4};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 16> payload{
        std::byte{0xff}, std::byte{0x00}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
    };

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.content_byte_offset = 0;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    for (const auto sample : y) {
        EXPECT_EQ(sample, 0u);
    }
    for (const auto sample : cb) {
        EXPECT_EQ(sample, 0u);
    }
    for (const auto sample : cr) {
        EXPECT_EQ(sample, 0u);
    }
}

TEST(SliceDecoderTest, DecodesZeroDifferencesForExtraPlaneSlice)
{
    auto stream = make_stream();
    stream.version = 3;
    stream.extra_plane = true;
    stream.quant_table_sets.push_back(mffv1::syntax::make_zero_quant_table_set());
    stream.quant_table_sets.push_back(mffv1::syntax::make_zero_quant_table_set());

    std::array<std::uint8_t, 8> y{};
    std::array<std::uint8_t, 8> alpha{};
    std::array<mffv1::MutablePlaneView, 2> planes{};
    planes[0].data = y.data();
    planes[0].info = {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 4, 2, 4};
    planes[1].data = alpha.data();
    planes[1].info = {mffv1::PlaneRole::Alpha, mffv1::SampleFormat::UInt8, 4, 2, 4};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 16> payload{
        std::byte{0xff}, std::byte{0x00}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
    };

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.content_byte_offset = 0;
    slice.quant_table_set_indexes = {0, 1, 2};

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    for (const auto sample : y) {
        EXPECT_EQ(sample, 0u);
    }
    for (const auto sample : alpha) {
        EXPECT_EQ(sample, 0u);
    }
}

TEST(SliceDecoderTest, RejectsUnsupportedBitDepth)
{
    auto stream = make_stream();
    stream.bits_per_raw_sample = 17;
    std::array<std::uint16_t, 8> storage{};
    auto plane = make_u16_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.content_byte_offset = 0;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::UnsupportedFeature);
}

TEST(SliceDecoderTest, DecodesZeroDifferenceRgbRangeSlice)
{
    auto stream = make_stream();
    stream.width = 1;
    stream.height = 1;
    stream.colorspace_type = 1;
    stream.chroma_planes = true;
    std::array<std::uint8_t, 1> r{0xee};
    std::array<std::uint8_t, 1> g{0xee};
    std::array<std::uint8_t, 1> b{0xee};
    std::array<mffv1::MutablePlaneView, 3> planes{};
    planes[0] = {r.data(), {mffv1::PlaneRole::R, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[1] = {g.data(), {mffv1::PlaneRole::G, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[2] = {b.data(), {mffv1::PlaneRole::B, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(r[0], 128u);
    EXPECT_EQ(g[0], 128u);
    EXPECT_EQ(b[0], 128u);
    EXPECT_TRUE(state.has_range_contexts());
    EXPECT_EQ(state.range_contexts().size(), 3u);
}

TEST(SliceDecoderTest, DecodesNonzeroRgbRangeSliceInLineOrder)
{
    auto stream = make_stream();
    stream.width = 1;
    stream.height = 1;
    stream.colorspace_type = 1;
    stream.chroma_planes = true;
    std::array<std::uint8_t, 1> r{};
    std::array<std::uint8_t, 1> g{};
    std::array<std::uint8_t, 1> b{};
    std::array<mffv1::MutablePlaneView, 3> planes{};
    planes[0] = {r.data(), {mffv1::PlaneRole::R, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[1] = {g.data(), {mffv1::PlaneRole::G, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[2] = {b.data(), {mffv1::PlaneRole::B, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 2> payload{std::byte{0x14}, std::byte{0x46}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(state.line_state(0).previous().size(), 1u);
    EXPECT_EQ(state.line_state(0).previous()[0], 1);
    EXPECT_EQ(state.line_state(1).previous()[0], 0);
    EXPECT_EQ(state.line_state(2).previous()[0], 2);
    EXPECT_EQ(r[0], 131u);
    EXPECT_EQ(g[0], 129u);
    EXPECT_EQ(b[0], 129u);
}

TEST(SliceDecoderTest, AppliesHighBitDepthRgbCompatibilityTransform)
{
    auto stream = make_stream();
    stream.width = 1;
    stream.height = 1;
    stream.bits_per_raw_sample = 10;
    stream.colorspace_type = 1;
    stream.chroma_planes = true;
    std::array<std::uint16_t, 1> r{};
    std::array<std::uint16_t, 1> g{};
    std::array<std::uint16_t, 1> b{};
    std::array<mffv1::MutablePlaneView, 3> planes{};
    planes[0] = {r.data(), {mffv1::PlaneRole::R, mffv1::SampleFormat::UInt16, 1, 1, 2}};
    planes[1] = {g.data(), {mffv1::PlaneRole::G, mffv1::SampleFormat::UInt16, 1, 1, 2}};
    planes[2] = {b.data(), {mffv1::PlaneRole::B, mffv1::SampleFormat::UInt16, 1, 1, 2}};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 2> payload{std::byte{0x00}, std::byte{0x80}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(state.line_state(0).previous()[0], 1);
    EXPECT_EQ(state.line_state(1).previous()[0], 1);
    EXPECT_EQ(state.line_state(2).previous()[0], 2047);
    EXPECT_EQ(r[0], 0u);
    EXPECT_EQ(g[0], 2u);
    EXPECT_EQ(b[0], 1u);
}

TEST(SliceDecoderTest, ReconstructsSixteenBitRgbInSeventeenBitDomain)
{
    auto stream = make_stream();
    stream.width = 1;
    stream.height = 1;
    stream.bits_per_raw_sample = 16;
    stream.colorspace_type = 1;
    stream.chroma_planes = true;
    std::array<std::uint16_t, 1> r{};
    std::array<std::uint16_t, 1> g{};
    std::array<std::uint16_t, 1> b{};
    std::array<mffv1::MutablePlaneView, 3> planes{};
    planes[0] = {r.data(), {mffv1::PlaneRole::R, mffv1::SampleFormat::UInt16, 1, 1, 2}};
    planes[1] = {g.data(), {mffv1::PlaneRole::G, mffv1::SampleFormat::UInt16, 1, 1, 2}};
    planes[2] = {b.data(), {mffv1::PlaneRole::B, mffv1::SampleFormat::UInt16, 1, 1, 2}};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 2> payload{std::byte{0x00}, std::byte{0x80}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(state.line_state(0).previous()[0], 1);
    EXPECT_EQ(state.line_state(1).previous()[0], 1);
    EXPECT_EQ(state.line_state(2).previous()[0], 131071);
    EXPECT_EQ(r[0], 0u);
    EXPECT_EQ(g[0], 1u);
    EXPECT_EQ(b[0], 2u);
}

TEST(SliceDecoderTest, DecodesRgbExtraPlaneAfterColorLines)
{
    auto stream = make_stream();
    stream.width = 1;
    stream.height = 1;
    stream.colorspace_type = 1;
    stream.chroma_planes = true;
    stream.extra_plane = true;
    std::array<std::uint8_t, 1> r{};
    std::array<std::uint8_t, 1> g{};
    std::array<std::uint8_t, 1> b{};
    std::array<std::uint8_t, 1> alpha{};
    std::array<mffv1::MutablePlaneView, 4> planes{};
    planes[0] = {r.data(), {mffv1::PlaneRole::R, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[1] = {g.data(), {mffv1::PlaneRole::G, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[2] = {b.data(), {mffv1::PlaneRole::B, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[3] = {alpha.data(), {mffv1::PlaneRole::Alpha, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 2> payload{std::byte{0x14}, std::byte{0x46}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(state.line_state(3).previous().size(), 1u);
    EXPECT_EQ(state.line_state(3).previous()[0], 0);
    EXPECT_EQ(r[0], 131u);
    EXPECT_EQ(g[0], 129u);
    EXPECT_EQ(b[0], 129u);
    EXPECT_EQ(alpha[0], 0u);
}

TEST(SliceDecoderTest, DecodesGolombRiceRgbZeroRunsInLineOrder)
{
    auto stream = make_stream();
    stream.width = 1;
    stream.height = 1;
    stream.colorspace_type = 1;
    stream.chroma_planes = true;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 1> r{0xee};
    std::array<std::uint8_t, 1> g{0xee};
    std::array<std::uint8_t, 1> b{0xee};
    std::array<mffv1::MutablePlaneView, 3> planes{};
    planes[0] = {r.data(), {mffv1::PlaneRole::R, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[1] = {g.data(), {mffv1::PlaneRole::G, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[2] = {b.data(), {mffv1::PlaneRole::B, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 1> payload{std::byte{0xe0}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(r[0], 128u);
    EXPECT_EQ(g[0], 128u);
    EXPECT_EQ(b[0], 128u);
}

TEST(SliceDecoderTest, SharesGolombRiceRgbRunIndexAcrossLinePlanes)
{
    auto stream = make_stream();
    stream.width = 8;
    stream.height = 1;
    stream.colorspace_type = 1;
    stream.chroma_planes = true;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 8> r{};
    std::array<std::uint8_t, 8> g{};
    std::array<std::uint8_t, 8> b{};
    r.fill(0xee);
    g.fill(0xee);
    b.fill(0xee);
    std::array<mffv1::MutablePlaneView, 3> planes{};
    planes[0] = {r.data(), {mffv1::PlaneRole::R, mffv1::SampleFormat::UInt8, 8, 1, 8}};
    planes[1] = {g.data(), {mffv1::PlaneRole::G, mffv1::SampleFormat::UInt8, 8, 1, 8}};
    planes[2] = {b.data(), {mffv1::PlaneRole::B, mffv1::SampleFormat::UInt8, 8, 1, 8}};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 7> payload{
        std::byte{0xfc},
        std::byte{0x00},
        std::byte{0x0f},
        std::byte{0xa7},
        std::byte{0x87},
        std::byte{0xf7},
        std::byte{0x80},
    };

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 8;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(r, (std::array<std::uint8_t, 8>{0, 0, 0, 0, 0, 0, 0, 0}));
    EXPECT_EQ(g, (std::array<std::uint8_t, 8>{0, 0, 0, 0, 0, 0, 0, 0}));
    EXPECT_EQ(b, (std::array<std::uint8_t, 8>{0, 0, 0, 0, 0, 0, 0, 0}));
}

TEST(SliceDecoderTest, ResetsGolombRiceRunIndexAtSliceStart)
{
    auto stream = make_stream();
    stream.width = 3;
    stream.height = 1;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 3> y{0xee, 0xee, 0xee};
    std::array<mffv1::MutablePlaneView, 1> planes{};
    planes[0] = {y.data(), {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 3, 1, 3}};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 1> payload{std::byte{0xe0}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 3;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());
    const std::array<std::size_t, 1> context_counts{1};
    ASSERT_TRUE(state.prepare_golomb_rice(context_counts, 1).ok());
    state.golomb_rice_run_state(0).run_index = 4;
    state.golomb_rice_run_state(0).pending_count = 3;

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(y, (std::array<std::uint8_t, 3>{0, 0, 0}));
    EXPECT_EQ(state.golomb_rice_run_state(0).pending_count, 0u);
}

TEST(SliceDecoderTest, DecodesGolombRiceRgbRunInterruptionsWithAlpha)
{
    auto stream = make_stream();
    stream.width = 1;
    stream.height = 1;
    stream.colorspace_type = 1;
    stream.chroma_planes = true;
    stream.extra_plane = true;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 1> r{0xee};
    std::array<std::uint8_t, 1> g{0xee};
    std::array<std::uint8_t, 1> b{0xee};
    std::array<std::uint8_t, 1> alpha{0xee};
    std::array<mffv1::MutablePlaneView, 4> planes{};
    planes[0] = {r.data(), {mffv1::PlaneRole::R, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[1] = {g.data(), {mffv1::PlaneRole::G, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[2] = {b.data(), {mffv1::PlaneRole::B, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[3] = {alpha.data(), {mffv1::PlaneRole::Alpha, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 2> payload{std::byte{0x44}, std::byte{0x44}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(state.line_state(0).previous()[0], 1);
    EXPECT_EQ(state.line_state(1).previous()[0], 1);
    EXPECT_EQ(state.line_state(2).previous()[0], 2);
    EXPECT_EQ(state.line_state(3).previous()[0], 255);
    EXPECT_EQ(r[0], 131u);
    EXPECT_EQ(g[0], 129u);
    EXPECT_EQ(b[0], 130u);
    EXPECT_EQ(alpha[0], 255u);
}

TEST(SliceDecoderTest, ReconstructsSixteenBitGolombRiceRgbInSeventeenBitDomain)
{
    auto stream = make_stream();
    stream.width = 1;
    stream.height = 1;
    stream.bits_per_raw_sample = 16;
    stream.colorspace_type = 1;
    stream.chroma_planes = true;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint16_t, 1> r{};
    std::array<std::uint16_t, 1> g{};
    std::array<std::uint16_t, 1> b{};
    std::array<mffv1::MutablePlaneView, 3> planes{};
    planes[0] = {r.data(), {mffv1::PlaneRole::R, mffv1::SampleFormat::UInt16, 1, 1, 2}};
    planes[1] = {g.data(), {mffv1::PlaneRole::G, mffv1::SampleFormat::UInt16, 1, 1, 2}};
    planes[2] = {b.data(), {mffv1::PlaneRole::B, mffv1::SampleFormat::UInt16, 1, 1, 2}};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 2> payload{std::byte{0x44}, std::byte{0x40}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(state.line_state(0).previous()[0], 1);
    EXPECT_EQ(state.line_state(1).previous()[0], 1);
    EXPECT_EQ(state.line_state(2).previous()[0], 2);
    EXPECT_EQ(r[0], 32771u);
    EXPECT_EQ(g[0], 32769u);
    EXPECT_EQ(b[0], 32770u);
}

TEST(SliceDecoderTest, AppliesHighBitDepthGolombRiceRgbCompatibilityTransform)
{
    auto stream = make_stream();
    stream.width = 1;
    stream.height = 1;
    stream.bits_per_raw_sample = 10;
    stream.colorspace_type = 1;
    stream.chroma_planes = true;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint16_t, 1> r{};
    std::array<std::uint16_t, 1> g{};
    std::array<std::uint16_t, 1> b{};
    std::array<mffv1::MutablePlaneView, 3> planes{};
    planes[0] = {r.data(), {mffv1::PlaneRole::R, mffv1::SampleFormat::UInt16, 1, 1, 2}};
    planes[1] = {g.data(), {mffv1::PlaneRole::G, mffv1::SampleFormat::UInt16, 1, 1, 2}};
    planes[2] = {b.data(), {mffv1::PlaneRole::B, mffv1::SampleFormat::UInt16, 1, 1, 2}};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 2> payload{std::byte{0x44}, std::byte{0x40}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(state.line_state(0).previous()[0], 1);
    EXPECT_EQ(state.line_state(1).previous()[0], 1);
    EXPECT_EQ(state.line_state(2).previous()[0], 2);
    EXPECT_EQ(r[0], 515u);
    EXPECT_EQ(g[0], 514u);
    EXPECT_EQ(b[0], 513u);
}

TEST(SliceDecoderTest, GolombRiceRgbAlphaDisablesCompatibilityTransform)
{
    auto stream = make_stream();
    stream.width = 1;
    stream.height = 1;
    stream.bits_per_raw_sample = 10;
    stream.colorspace_type = 1;
    stream.chroma_planes = true;
    stream.extra_plane = true;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint16_t, 1> r{};
    std::array<std::uint16_t, 1> g{};
    std::array<std::uint16_t, 1> b{};
    std::array<std::uint16_t, 1> alpha{};
    std::array<mffv1::MutablePlaneView, 4> planes{};
    planes[0] = {r.data(), {mffv1::PlaneRole::R, mffv1::SampleFormat::UInt16, 1, 1, 2}};
    planes[1] = {g.data(), {mffv1::PlaneRole::G, mffv1::SampleFormat::UInt16, 1, 1, 2}};
    planes[2] = {b.data(), {mffv1::PlaneRole::B, mffv1::SampleFormat::UInt16, 1, 1, 2}};
    planes[3] = {alpha.data(), {mffv1::PlaneRole::Alpha, mffv1::SampleFormat::UInt16, 1, 1, 2}};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 2> payload{std::byte{0x44}, std::byte{0x44}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(state.line_state(0).previous()[0], 1);
    EXPECT_EQ(state.line_state(1).previous()[0], 1);
    EXPECT_EQ(state.line_state(2).previous()[0], 2);
    EXPECT_EQ(state.line_state(3).previous()[0], 1023);
    EXPECT_EQ(r[0], 515u);
    EXPECT_EQ(g[0], 513u);
    EXPECT_EQ(b[0], 514u);
    EXPECT_EQ(alpha[0], 1023u);
}

TEST(SliceDecoderTest, KeepsGolombRiceRgbContextAndPredictionAcrossRows)
{
    auto stream = make_stream();
    stream.width = 1;
    stream.height = 2;
    stream.colorspace_type = 1;
    stream.chroma_planes = true;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 2> r{0xee, 0xee};
    std::array<std::uint8_t, 2> g{0xee, 0xee};
    std::array<std::uint8_t, 2> b{0xee, 0xee};
    std::array<mffv1::MutablePlaneView, 3> planes{};
    planes[0] = {r.data(), {mffv1::PlaneRole::R, mffv1::SampleFormat::UInt8, 1, 2, 1}};
    planes[1] = {g.data(), {mffv1::PlaneRole::G, mffv1::SampleFormat::UInt8, 1, 2, 1}};
    planes[2] = {b.data(), {mffv1::PlaneRole::B, mffv1::SampleFormat::UInt8, 1, 2, 1}};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 3> payload{
        std::byte{0x44}, std::byte{0x44}, std::byte{0x90},
    };

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 2;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(r, (std::array<std::uint8_t, 2>{131, 130}));
    EXPECT_EQ(g, (std::array<std::uint8_t, 2>{129, 127}));
    EXPECT_EQ(b, (std::array<std::uint8_t, 2>{130, 129}));
}

TEST(SliceDecoderTest, DecodesGolombRiceZeroRun)
{
    auto stream = make_stream();
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0xfc}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    for (const auto sample : storage) {
        EXPECT_EQ(sample, 0u);
    }
}

TEST(SliceDecoderTest, DecodesGolombRiceRunClippedAtPlaneEnd)
{
    auto stream = make_stream();
    stream.width = 5;
    stream.height = 1;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 5> storage{};
    storage.fill(0xee);
    mffv1::MutablePlaneView plane{
        storage.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 5, 1, 5}};
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0xf8}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 5;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    for (const auto sample : storage) {
        EXPECT_EQ(sample, 0u);
    }
}

TEST(SliceDecoderTest, DecodesGolombRiceChromaPlanesInOrder)
{
    auto stream = make_stream();
    stream.version = 3;
    stream.width = 1;
    stream.height = 1;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    stream.chroma_planes = true;
    stream.quant_table_sets.push_back(mffv1::syntax::make_zero_quant_table_set());

    std::array<std::uint8_t, 1> y{0xee};
    std::array<std::uint8_t, 1> cb{0xee};
    std::array<std::uint8_t, 1> cr{0xee};
    std::array<mffv1::MutablePlaneView, 3> planes{};
    planes[0].data = y.data();
    planes[0].info = {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 1, 1, 1};
    planes[1].data = cb.data();
    planes[1].info = {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt8, 1, 1, 1};
    planes[2].data = cr.data();
    planes[2].info = {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt8, 1, 1, 1};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 2> payload{std::byte{0x45}, std::byte{0x60}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes = {0, 1};

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(y[0], 1u);
    EXPECT_EQ(cb[0], 255u);
    EXPECT_EQ(cr[0], 2u);
}

TEST(SliceDecoderTest, KeepsGolombRiceContextsSlotLocalWhenQidxValueIsShared)
{
    auto stream = make_stream();
    stream.version = 3;
    stream.width = 1;
    stream.height = 1;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    stream.chroma_planes = true;

    mffv1::bitstream::BitWriter bit_writer;
    mffv1::entropy::GolombRiceRunState y_run_state;
    mffv1::entropy::GolombRiceContextState y_context;
    ASSERT_TRUE(mffv1::entropy::write_golomb_rice_run(
        bit_writer, y_run_state, 0, 1, 0).ok());
    mffv1::entropy::GolombRiceWriter rice_writer(bit_writer);
    ASSERT_TRUE(mffv1::entropy::write_golomb_rice_run_interruption(
        rice_writer, y_context, stream.bits_per_raw_sample, 126).ok());
    mffv1::entropy::GolombRiceRunState cb_run_state;
    mffv1::entropy::GolombRiceContextState cb_context;
    mffv1::entropy::GolombRiceRunState cr_run_state;
    ASSERT_TRUE(mffv1::entropy::write_golomb_rice_run(
        bit_writer, cb_run_state, 0, 1, 0).ok());
    ASSERT_TRUE(mffv1::entropy::write_golomb_rice_run_interruption(
        rice_writer, cb_context, stream.bits_per_raw_sample, 128).ok());
    ASSERT_TRUE(mffv1::entropy::write_golomb_rice_run(
        bit_writer, cr_run_state, 0, 1, 0).ok());
    ASSERT_TRUE(mffv1::entropy::write_golomb_rice_run_interruption(
        rice_writer, cb_context, stream.bits_per_raw_sample, 128).ok());
    ASSERT_TRUE(bit_writer.byte_align_zero().ok());
    std::vector<std::byte> payload;
    ASSERT_TRUE(bit_writer.finalize(payload).ok());

    std::array<std::uint8_t, 1> y{0xee};
    std::array<std::uint8_t, 1> cb{0xee};
    std::array<std::uint8_t, 1> cr{0xee};
    std::array<mffv1::MutablePlaneView, 3> planes{};
    planes[0].data = y.data();
    planes[0].info = {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 1, 1, 1};
    planes[1].data = cb.data();
    planes[1].info = {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt8, 1, 1, 1};
    planes[2].data = cr.data();
    planes[2].info = {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt8, 1, 1, 1};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes = {0, 0};

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream, window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(y[0], 126u);
    EXPECT_EQ(cb[0], 128u);
    EXPECT_EQ(cr[0], 128u);
}

TEST(SliceDecoderTest, KeepsGolombRiceRgbYContextSeparateFromSharedChromaSlot)
{
    auto stream = make_stream();
    stream.version = 3;
    stream.width = 1;
    stream.height = 1;
    stream.colorspace_type = 1;
    stream.chroma_planes = true;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;

    mffv1::bitstream::BitWriter bit_writer;
    mffv1::entropy::GolombRiceWriter rice_writer(bit_writer);
    mffv1::entropy::GolombRiceRunState run_state;
    mffv1::entropy::GolombRiceContextState y_context;
    mffv1::entropy::GolombRiceContextState chroma_context;
    constexpr std::uint8_t coded_bits = 9;
    ASSERT_TRUE(mffv1::entropy::write_golomb_rice_run(
        bit_writer, run_state, 0, 1, 0).ok());
    ASSERT_TRUE(mffv1::entropy::write_golomb_rice_run_interruption(
        rice_writer, y_context, coded_bits, 63).ok());
    ASSERT_TRUE(mffv1::entropy::write_golomb_rice_run(
        bit_writer, run_state, 0, 1, 0).ok());
    ASSERT_TRUE(mffv1::entropy::write_golomb_rice_run_interruption(
        rice_writer, chroma_context, coded_bits, -256).ok());
    ASSERT_TRUE(mffv1::entropy::write_golomb_rice_run(
        bit_writer, run_state, 0, 1, 0).ok());
    ASSERT_TRUE(mffv1::entropy::write_golomb_rice_run_interruption(
        rice_writer, chroma_context, coded_bits, -256).ok());
    ASSERT_TRUE(bit_writer.byte_align_zero().ok());
    std::vector<std::byte> payload;
    ASSERT_TRUE(bit_writer.finalize(payload).ok());

    std::array<std::uint8_t, 1> r{0xee};
    std::array<std::uint8_t, 1> g{0xee};
    std::array<std::uint8_t, 1> b{0xee};
    std::array<mffv1::MutablePlaneView, 3> planes{};
    planes[0].data = r.data();
    planes[0].info = {mffv1::PlaneRole::R, mffv1::SampleFormat::UInt8, 1, 1, 1};
    planes[1].data = g.data();
    planes[1].info = {mffv1::PlaneRole::G, mffv1::SampleFormat::UInt8, 1, 1, 1};
    planes[2].data = b.data();
    planes[2].info = {mffv1::PlaneRole::B, mffv1::SampleFormat::UInt8, 1, 1, 1};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes = {0, 0};

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream, window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(state.line_state(0).previous()[0], 63);
    EXPECT_EQ(state.line_state(1).previous()[0], 256);
    EXPECT_EQ(state.line_state(2).previous()[0], 256);
    EXPECT_EQ(r[0], 63u);
    EXPECT_EQ(g[0], 63u);
    EXPECT_EQ(b[0], 63u);
}

TEST(SliceDecoderTest, DecodesGolombRice16BitExtraPlane)
{
    auto stream = make_stream();
    stream.version = 3;
    stream.width = 1;
    stream.height = 1;
    stream.bits_per_raw_sample = 16;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    stream.extra_plane = true;
    stream.quant_table_sets.push_back(mffv1::syntax::make_zero_quant_table_set());
    stream.quant_table_sets.push_back(mffv1::syntax::make_zero_quant_table_set());

    std::array<std::uint16_t, 1> y{0xeeee};
    std::array<std::uint16_t, 1> alpha{0xeeee};
    std::array<mffv1::MutablePlaneView, 2> planes{};
    planes[0].data = y.data();
    planes[0].info = {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt16, 1, 1, 2};
    planes[1].data = alpha.data();
    planes[1].info = {mffv1::PlaneRole::Alpha, mffv1::SampleFormat::UInt16, 1, 1, 2};
    mffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 1> payload{std::byte{0x45}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes = {0, 1, 2};

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(y[0], 1u);
    EXPECT_EQ(alpha[0], 65535u);
}

TEST(SliceDecoderTest, DecodesPositiveGolombRiceRunInterruption)
{
    auto stream = make_stream();
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0x40}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 1u);
    EXPECT_EQ(storage[1], 0xee);
}

TEST(SliceDecoderTest, ContinuesGolombRiceContextAcrossLineReset)
{
    auto stream = make_stream();
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0x40}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());
    const mffv1::codec::SliceDecoder decoder(stream);
    ASSERT_TRUE(decoder.decode(slice, window, state).ok());
    const auto first_count = state.golomb_rice_context(0, 0).count;

    ASSERT_TRUE(state.reset(window).ok());
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_GT(state.golomb_rice_context(0, 0).count, first_count);
}

TEST(SliceDecoderTest, DecodesGolombRiceFromContentBitOffset)
{
    auto stream = make_stream();
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0xa0}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.content_bit_offset = 1;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 1u);
    EXPECT_EQ(storage[1], 0xee);
}

TEST(SliceDecoderTest, RejectsGolombRiceContentBitOffsetOutsideByte)
{
    auto stream = make_stream();
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 8> storage{};
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0x00}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.content_bit_offset = 8;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
}

TEST(SliceDecoderTest, DecodesNegativeGolombRiceRunInterruption)
{
    auto stream = make_stream();
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0x50}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 255u);
    EXPECT_EQ(storage[1], 0xee);
}

TEST(SliceDecoderTest, DecodesGolombRiceScalarContext)
{
    auto stream = make_stream();
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    stream.quant_table_sets[0].context_count = 2;
    stream.quant_table_sets[0].tables[0][1] = 1;
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0x48}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 2;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    RecordingSliceObserver observer;
    const auto status = decoder.decode(slice, window, state, &observer);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 1u);
    EXPECT_EQ(storage[1], 1u);
    EXPECT_EQ(storage[2], 0xee);
    ASSERT_GE(observer.traces.size(), 2u);
    const auto scalar_trace = std::find_if(
        observer.traces.begin(),
        observer.traces.end(),
        [](const mffv1::codec::GolombRiceSampleTrace& trace) {
            return trace.context.context == 1;
        });
    ASSERT_NE(scalar_trace, observer.traces.end());
    EXPECT_EQ(scalar_trace->plane, 0u);
    EXPECT_EQ(scalar_trace->x, 1u);
    EXPECT_EQ(scalar_trace->y, 0u);
    EXPECT_EQ(scalar_trace->prediction, 1);
    EXPECT_EQ(scalar_trace->difference, 0);
    EXPECT_EQ(scalar_trace->reconstructed_sample, 1);
    EXPECT_LT(scalar_trace->bit_position_before, scalar_trace->bit_position_after);
    EXPECT_EQ(scalar_trace->adaptive_state_before.count, 1);
    EXPECT_EQ(scalar_trace->adaptive_state_after.count, 2);
}

TEST(SliceDecoderTest, InvertsGolombRiceDifferenceForNegativeContext)
{
    auto stream = make_stream();
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    stream.quant_table_sets[0].context_count = 2;
    stream.quant_table_sets[0].tables[0][1] = -1;
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0x4c}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 2;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 1u);
    EXPECT_EQ(storage[1], 0u);
    EXPECT_EQ(storage[2], 0xee);
}

TEST(SliceDecoderTest, DecodesGolombRiceRunRemainder)
{
    auto stream = make_stream();
    stream.width = 6;
    stream.height = 1;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 6> storage{};
    storage.fill(0xee);
    mffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = mffv1::PlaneRole::Y;
    plane.info.sample_format = mffv1::SampleFormat::UInt8;
    plane.info.width = 6;
    plane.info.height = 1;
    plane.info.stride_bytes = 6;
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xf6}, std::byte{0x00}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 6;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage, (std::array<std::uint8_t, 6>{0, 0, 0, 0, 0, 1}));
}

TEST(SliceDecoderTest, KeepsGolombRiceRunModeAcrossDerivedContextChanges)
{
    auto stream = make_stream();
    stream.width = 4;
    stream.height = 1;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    stream.quant_table_sets[0].context_count = 2;
    stream.quant_table_sets[0].tables[1][255] = 1;

    mffv1::bitstream::BitWriter bit_writer;
    mffv1::entropy::GolombRiceRunState run_state;
    ASSERT_TRUE(mffv1::entropy::write_golomb_rice_run(
        bit_writer, run_state, 0, stream.width, stream.width).ok());
    ASSERT_TRUE(bit_writer.byte_align_zero().ok());
    std::vector<std::byte> payload;
    ASSERT_TRUE(bit_writer.finalize(payload).ok());

    std::array<std::uint8_t, 4> storage{};
    storage.fill(0xee);
    mffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = mffv1::PlaneRole::Y;
    plane.info.sample_format = mffv1::SampleFormat::UInt8;
    plane.info.width = stream.width;
    plane.info.height = stream.height;
    plane.info.stride_bytes = stream.width;
    mffv1::MutableFrameView frame{&plane, 1};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = stream.width;
    slice.height = stream.height;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());
    state.line_state(0).mutable_previous() = {5, 6, 6, 6};

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage, (std::array<std::uint8_t, 4>{5, 6, 6, 6}));
}

TEST(SliceDecoderTest, DecodesGolombRiceRunInterruptionWithDerivedContext)
{
    auto stream = make_stream();
    stream.width = 2;
    stream.height = 1;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    stream.quant_table_sets[0].context_count = 2;
    stream.quant_table_sets[0].tables[1][255] = 1;

    mffv1::bitstream::BitWriter bit_writer;
    mffv1::entropy::GolombRiceRunState run_state;
    ASSERT_TRUE(mffv1::entropy::write_golomb_rice_run(
        bit_writer, run_state, 0, stream.width, 1).ok());
    mffv1::entropy::GolombRiceWriter rice_writer(bit_writer);
    mffv1::entropy::GolombRiceContextState context_one;
    ASSERT_TRUE(mffv1::entropy::write_golomb_rice_run_interruption(
        rice_writer, context_one, stream.bits_per_raw_sample, 1).ok());
    ASSERT_TRUE(bit_writer.byte_align_zero().ok());
    std::vector<std::byte> payload;
    ASSERT_TRUE(bit_writer.finalize(payload).ok());

    std::array<std::uint8_t, 2> storage{};
    storage.fill(0xee);
    mffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = mffv1::PlaneRole::Y;
    plane.info.sample_format = mffv1::SampleFormat::UInt8;
    plane.info.width = stream.width;
    plane.info.height = stream.height;
    plane.info.stride_bytes = stream.width;
    mffv1::MutableFrameView frame{&plane, 1};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = stream.width;
    slice.height = stream.height;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());
    state.line_state(0).mutable_previous() = {5, 6};
    const std::array<std::size_t, 1> context_counts{2};
    ASSERT_TRUE(state.prepare_golomb_rice(context_counts, 1).ok());
    state.golomb_rice_context(0, 0) = {-5, 1274, -5, 6};

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage, (std::array<std::uint8_t, 2>{5, 7}));
    EXPECT_EQ(state.golomb_rice_context(0, 0).error_sum, 1274);
    EXPECT_EQ(state.golomb_rice_context(0, 1).count, 2);
}

TEST(SliceDecoderTest, AcceptsNonzeroGolombRiceBitPadding)
{
    auto stream = make_stream();
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 8> storage{};
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0xff}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage, (std::array<std::uint8_t, 8>{0, 0, 0, 0, 0, 0, 0, 0}));
}

TEST(SliceDecoderTest, RejectsTrailingGolombRiceByte)
{
    auto stream = make_stream();
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 8> storage{};
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 4> payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0xfc},
        std::byte{0x00},
    };

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.content_byte_offset = 2;
    slice.quant_table_set_indexes.push_back(0);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 3u);
    EXPECT_EQ(storage, (std::array<std::uint8_t, 8>{0, 0, 0, 0, 0, 0, 0, 0}));
}

TEST(SliceDecoderTest, AcceptsVersionThreeGolombRiceReadAheadTrailingBytes)
{
    auto stream = make_stream();
    stream.version = 3;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 8> storage{};
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 4> payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0xfc},
        std::byte{0x55},
    };

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.content_byte_offset = 2;
    slice.quant_table_set_indexes = {0, 0};

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream, true);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage, (std::array<std::uint8_t, 8>{0, 0, 0, 0, 0, 0, 0, 0}));
}

TEST(SliceDecoderTest, RejectsMissingQuantTableSetIndex)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.content_byte_offset = 0;

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice has no quantization table set indexes");
    EXPECT_EQ(storage, (std::array<std::uint8_t, 8>{
                           0xee, 0xee, 0xee, 0xee,
                           0xee, 0xee, 0xee, 0xee,
                       }));
}

TEST(SliceDecoderTest, AcceptsUnusedVersionThreeChromaCompatibilityIndex)
{
    auto stream = make_stream();
    stream.version = 3;
    stream.quant_table_sets.push_back(mffv1::syntax::make_zero_quant_table_set());
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.quant_table_set_indexes = {0, 1};

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    for (const auto sample : storage) {
        EXPECT_EQ(sample, 0u);
    }
}

TEST(SliceDecoderTest, RejectsOutOfRangeQuantTableSetIndex)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    mffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.content_byte_offset = 0;
    slice.quant_table_set_indexes.push_back(1);

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const mffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice quantization table set index is out of range");
    EXPECT_EQ(storage, (std::array<std::uint8_t, 8>{
                           0xee, 0xee, 0xee, 0xee,
                           0xee, 0xee, 0xee, 0xee,
                       }));
}

} // namespace
