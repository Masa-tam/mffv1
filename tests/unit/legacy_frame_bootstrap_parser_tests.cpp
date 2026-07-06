#include "codec/legacy_frame_bootstrap_parser.hpp"
#include "entropy/range_coder.hpp"
#include "entropy/range_encoder.hpp"
#include "mffv1/configuration_parser.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

std::vector<std::byte> make_legacy_frame_parameters(
    bool keyframe,
    mffv1::EntropyMode entropy_mode = mffv1::EntropyMode::Range,
    int colorspace_type = 0,
    bool chroma_planes = false,
    std::uint8_t log2_h_chroma_subsample = 0,
    std::uint8_t log2_v_chroma_subsample = 0,
    bool extra_plane = false,
    bool split_first_quant_table = false)
{
    mffv1::entropy::RangeEncoder writer;
    EXPECT_TRUE(writer.reset().ok());
    EXPECT_TRUE(writer.write_bool(keyframe).ok());
    if (keyframe) {
        const std::array<std::size_t, 1> parameter_context_counts{1};
        EXPECT_TRUE(writer.reconfigure_contexts(parameter_context_counts).ok());
        EXPECT_TRUE(writer.write_unsigned(0).ok()); // version
        EXPECT_TRUE(writer.write_unsigned(
            entropy_mode == mffv1::EntropyMode::GolombRice ? 0 : 1).ok());
        EXPECT_TRUE(writer.write_unsigned(
            static_cast<std::uint64_t>(colorspace_type)).ok());
        EXPECT_TRUE(writer.write_bool(chroma_planes).ok());
        EXPECT_TRUE(writer.write_unsigned(log2_h_chroma_subsample).ok());
        EXPECT_TRUE(writer.write_unsigned(log2_v_chroma_subsample).ok());
        EXPECT_TRUE(writer.write_bool(extra_plane).ok());
        for (int table = 0; table < 5; ++table) {
            EXPECT_TRUE(writer.begin_independent_scalar_contexts(1).ok());
            if (split_first_quant_table && table == 0) {
                EXPECT_TRUE(writer.write_unsigned(0).ok());
                EXPECT_TRUE(writer.write_unsigned(126).ok());
            } else {
                EXPECT_TRUE(writer.write_unsigned(127).ok());
            }
            EXPECT_TRUE(writer.end_independent_scalar_contexts().ok());
        }
    }

    std::vector<std::byte> payload;
    EXPECT_TRUE(writer.finalize(payload).ok());
    return payload;
}

TEST(LegacyFrameBootstrapParserTest, ParsesVersionZeroRangeKeyframeParameters)
{
    const auto payload = make_legacy_frame_parameters(
        true,
        mffv1::EntropyMode::Range,
        0,
        true,
        1,
        1,
        true);
    const mffv1::codec::LegacyFrameBootstrapParser parser;
    mffv1::codec::LegacyFrameBootstrap bootstrap;

    const auto status = parser.parse(payload, 320, 240, bootstrap);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_TRUE(bootstrap.keyframe);
    EXPECT_TRUE(bootstrap.has_embedded_parameters);
    EXPECT_GT(bootstrap.content_byte_offset, 0u);
    EXPECT_EQ(bootstrap.stream.version, 0);
    EXPECT_EQ(bootstrap.stream.entropy_mode, mffv1::EntropyMode::Range);
    EXPECT_EQ(bootstrap.stream.width, 320u);
    EXPECT_EQ(bootstrap.stream.height, 240u);
    EXPECT_TRUE(bootstrap.stream.chroma_planes);
    EXPECT_TRUE(bootstrap.stream.extra_plane);
    EXPECT_EQ(bootstrap.stream.log2_h_chroma_subsample, 1u);
    EXPECT_EQ(bootstrap.stream.log2_v_chroma_subsample, 1u);
    ASSERT_EQ(bootstrap.stream.quant_table_sets.size(), 1u);
    EXPECT_EQ(bootstrap.stream.quant_table_sets[0].context_count, 1u);
}

TEST(LegacyFrameBootstrapParserTest, ParsesVersionZeroGolombRiceKeyframeParameters)
{
    const auto payload =
        make_legacy_frame_parameters(true, mffv1::EntropyMode::GolombRice);
    const mffv1::codec::LegacyFrameBootstrapParser parser;
    mffv1::codec::LegacyFrameBootstrap bootstrap;

    const auto status = parser.parse(payload, 16, 8, bootstrap);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_TRUE(bootstrap.has_embedded_parameters);
    EXPECT_EQ(bootstrap.stream.version, 0);
    EXPECT_EQ(bootstrap.stream.entropy_mode, mffv1::EntropyMode::GolombRice);
    EXPECT_EQ(bootstrap.stream.width, 16u);
    EXPECT_EQ(bootstrap.stream.height, 8u);
}

TEST(LegacyFrameBootstrapParserTest, ParsesVersionZeroEmbeddedQuantTableSet)
{
    const auto payload = make_legacy_frame_parameters(
        true,
        mffv1::EntropyMode::Range,
        0,
        false,
        0,
        0,
        false,
        true);
    const mffv1::codec::LegacyFrameBootstrapParser parser;
    mffv1::codec::LegacyFrameBootstrap bootstrap;

    const auto status = parser.parse(payload, 16, 8, bootstrap);

    ASSERT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(bootstrap.stream.quant_table_sets.size(), 1u);
    const auto& quant_table_set = bootstrap.stream.quant_table_sets[0];
    EXPECT_EQ(quant_table_set.context_count, 2u);
    EXPECT_EQ(quant_table_set.tables[0][0], 0);
    EXPECT_EQ(quant_table_set.tables[0][1], 1);
    EXPECT_EQ(quant_table_set.tables[0][127], 1);
    EXPECT_EQ(quant_table_set.tables[0][128], -1);
    EXPECT_EQ(quant_table_set.tables[0][255], -1);
}

TEST(LegacyFrameBootstrapParserTest, ExposesRangeStateBoundaries)
{
    const auto payload = make_legacy_frame_parameters(
        true,
        mffv1::EntropyMode::Range,
        0,
        true,
        1,
        1,
        false);
    const mffv1::codec::LegacyFrameBootstrapParser parser;
    mffv1::codec::LegacyFrameBootstrap bootstrap;

    const auto status = parser.parse(payload, 320, 240, bootstrap);

    ASSERT_TRUE(status.ok()) << status.message;
    mffv1::entropy::RangeCoder replay;
    ASSERT_TRUE(replay.reset(payload).ok());
    bool keyframe = false;
    ASSERT_TRUE(replay.read_bool(keyframe).ok());
    EXPECT_TRUE(keyframe);
    EXPECT_EQ(bootstrap.range_state_after_keyframe,
              replay.arithmetic_state());

    const std::array<std::size_t, 1> parameter_context_counts{1};
    ASSERT_TRUE(replay.reconfigure_contexts(parameter_context_counts).ok());
    mffv1::syntax::StreamParameters replay_stream;
    const mffv1::syntax::ConfigurationParser replay_parser;
    ASSERT_TRUE(replay_parser.parse(replay, replay_stream).ok());
    EXPECT_EQ(bootstrap.range_state_after_parameters,
              replay.arithmetic_state());
    EXPECT_EQ(bootstrap.content_byte_offset,
              bootstrap.range_state_after_parameters.byte_position);
    EXPECT_NE(bootstrap.range_state_after_parameters,
              bootstrap.range_state_after_keyframe);
}

TEST(LegacyFrameBootstrapParserTest, NonKeyframeHasNoEmbeddedParameters)
{
    const auto payload = make_legacy_frame_parameters(false);
    const mffv1::codec::LegacyFrameBootstrapParser parser;
    mffv1::codec::LegacyFrameBootstrap bootstrap;
    bootstrap.has_embedded_parameters = true;

    const auto status = parser.parse(payload, 320, 240, bootstrap);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_FALSE(bootstrap.keyframe);
    EXPECT_FALSE(bootstrap.has_embedded_parameters);
    EXPECT_GT(bootstrap.content_byte_offset, 0u);
    EXPECT_EQ(bootstrap.stream.width, 0u);
    EXPECT_EQ(bootstrap.stream.height, 0u);
    EXPECT_EQ(bootstrap.range_state_after_parameters,
              mffv1::entropy::RangeCoder::ArithmeticState{});
}

TEST(LegacyFrameBootstrapParserTest, FailedParsePreservesOutput)
{
    const std::vector<std::byte> payload{std::byte{0xff}};
    const mffv1::codec::LegacyFrameBootstrapParser parser;
    mffv1::codec::LegacyFrameBootstrap bootstrap;
    bootstrap.keyframe = true;
    bootstrap.has_embedded_parameters = true;
    bootstrap.content_byte_offset = 99;
    bootstrap.stream.width = 123;

    const auto status = parser.parse(payload, 320, 240, bootstrap);

    EXPECT_FALSE(status.ok());
    EXPECT_TRUE(bootstrap.keyframe);
    EXPECT_TRUE(bootstrap.has_embedded_parameters);
    EXPECT_EQ(bootstrap.content_byte_offset, 99u);
    EXPECT_EQ(bootstrap.stream.width, 123u);
}

} // namespace
