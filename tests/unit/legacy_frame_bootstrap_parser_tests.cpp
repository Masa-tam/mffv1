#include "codec/legacy_frame_bootstrap_parser.hpp"
#include "entropy/range_encoder.hpp"

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
    bool extra_plane = false)
{
    mffv1::entropy::RangeEncoder writer;
    EXPECT_TRUE(writer.reset().ok());
    EXPECT_TRUE(writer.write_bool(keyframe).ok());
    if (keyframe) {
        EXPECT_TRUE(writer.write_unsigned(0).ok()); // version
        EXPECT_TRUE(writer.write_unsigned(
            entropy_mode == mffv1::EntropyMode::GolombRice ? 0 : 1).ok());
        EXPECT_TRUE(writer.write_unsigned(
            static_cast<std::uint64_t>(colorspace_type)).ok());
        EXPECT_TRUE(writer.write_bool(chroma_planes).ok());
        EXPECT_TRUE(writer.write_unsigned(log2_h_chroma_subsample).ok());
        EXPECT_TRUE(writer.write_unsigned(log2_v_chroma_subsample).ok());
        EXPECT_TRUE(writer.write_bool(extra_plane).ok());
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
