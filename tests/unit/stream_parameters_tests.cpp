#include "ffv1/stream_parameters.hpp"

#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

namespace {

TEST(StreamParametersTest, SelectsSigned16BitPredictorOnlyForYcbcrRangeCoding)
{
    ffv1::syntax::StreamParameters stream;
    stream.colorspace_type = 0;
    stream.bits_per_raw_sample = 16;
    stream.entropy_mode = ffv1::EntropyMode::Range;
    EXPECT_TRUE(ffv1::syntax::uses_signed_16bit_predictor(stream));

    stream.bits_per_raw_sample = 15;
    EXPECT_FALSE(ffv1::syntax::uses_signed_16bit_predictor(stream));
    stream.bits_per_raw_sample = 16;
    stream.colorspace_type = 1;
    EXPECT_FALSE(ffv1::syntax::uses_signed_16bit_predictor(stream));
    stream.colorspace_type = 0;
    stream.entropy_mode = ffv1::EntropyMode::GolombRice;
    EXPECT_FALSE(ffv1::syntax::uses_signed_16bit_predictor(stream));
}

TEST(StreamParametersTest, ComputesPlaneGeometryForChromaOnly)
{
    ffv1::syntax::StreamParameters stream;
    stream.width = 17;
    stream.height = 9;
    stream.chroma_planes = true;
    stream.extra_plane = true;
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = 2;

    EXPECT_EQ(ffv1::syntax::plane_width(stream, 0), 17u);
    EXPECT_EQ(ffv1::syntax::plane_height(stream, 0), 9u);
    EXPECT_EQ(ffv1::syntax::plane_width(stream, 1), 9u);
    EXPECT_EQ(ffv1::syntax::plane_height(stream, 1), 3u);
    EXPECT_EQ(ffv1::syntax::plane_width(stream, 2), 9u);
    EXPECT_EQ(ffv1::syntax::plane_height(stream, 2), 3u);
    EXPECT_EQ(ffv1::syntax::plane_width(stream, 3), 17u);
    EXPECT_EQ(ffv1::syntax::plane_height(stream, 3), 9u);
}

TEST(StreamParametersTest, KeepsRgbPlanesFullResolutionAndOrdered)
{
    ffv1::syntax::StreamParameters stream;
    stream.colorspace_type = 1;
    stream.width = 17;
    stream.height = 9;
    stream.chroma_planes = true;
    stream.extra_plane = true;
    stream.log2_h_chroma_subsample = 2;
    stream.log2_v_chroma_subsample = 2;

    EXPECT_EQ(ffv1::syntax::expected_plane_role(stream, 0), ffv1::PlaneRole::R);
    EXPECT_EQ(ffv1::syntax::expected_plane_role(stream, 1), ffv1::PlaneRole::G);
    EXPECT_EQ(ffv1::syntax::expected_plane_role(stream, 2), ffv1::PlaneRole::B);
    EXPECT_EQ(ffv1::syntax::expected_plane_role(stream, 3), ffv1::PlaneRole::Alpha);
    for (std::size_t plane = 0; plane < 4; ++plane) {
        EXPECT_EQ(ffv1::syntax::plane_width(stream, plane), 17u);
        EXPECT_EQ(ffv1::syntax::plane_height(stream, plane), 9u);
    }
}

TEST(StreamParametersTest, SubsamplesMaximumExtentWithoutWrapping)
{
    const auto maximum = std::numeric_limits<std::uint32_t>::max();

    EXPECT_EQ(ffv1::syntax::subsampled_extent(maximum, 1), 0x80000000u);
    EXPECT_EQ(ffv1::syntax::subsampled_extent(maximum, 4), 0x10000000u);
    EXPECT_EQ(ffv1::syntax::subsampled_extent(maximum, 32), 1u);
    EXPECT_EQ(ffv1::syntax::subsampled_extent(0, 32), 0u);
}

TEST(StreamParametersTest, DerivesVersionThreeQuantTableIndexSlots)
{
    ffv1::syntax::StreamParameters stream;
    stream.version = 3;
    stream.chroma_planes = false;

    EXPECT_EQ(ffv1::syntax::quant_table_set_index_count(stream), 2u);
    EXPECT_EQ(ffv1::syntax::plane_quant_table_set_index_slot(stream, 0), 0u);

    stream.chroma_planes = true;
    stream.extra_plane = true;

    EXPECT_EQ(ffv1::syntax::quant_table_set_index_count(stream), 3u);
    EXPECT_EQ(ffv1::syntax::plane_quant_table_set_index_slot(stream, 0), 0u);
    EXPECT_EQ(ffv1::syntax::plane_quant_table_set_index_slot(stream, 1), 1u);
    EXPECT_EQ(ffv1::syntax::plane_quant_table_set_index_slot(stream, 2), 1u);
    EXPECT_EQ(ffv1::syntax::plane_quant_table_set_index_slot(stream, 3), 2u);
}

TEST(StreamParametersTest, OmitsLegacyChromaSlotAfterVersionThree)
{
    ffv1::syntax::StreamParameters stream;
    stream.version = 4;
    stream.chroma_planes = false;
    stream.extra_plane = true;

    EXPECT_EQ(ffv1::syntax::quant_table_set_index_count(stream), 2u);
    EXPECT_EQ(ffv1::syntax::plane_quant_table_set_index_slot(stream, 0), 0u);
    EXPECT_EQ(ffv1::syntax::plane_quant_table_set_index_slot(stream, 1), 1u);
}

TEST(StreamParametersTest, MapsSingleSliceRasterCellToFullFrame)
{
    ffv1::syntax::StreamParameters stream;
    stream.width = 16;
    stream.height = 8;
    stream.num_h_slices = 1;
    stream.num_v_slices = 1;

    EXPECT_EQ(ffv1::syntax::slice_pixel_x(stream, 0), 0u);
    EXPECT_EQ(ffv1::syntax::slice_pixel_y(stream, 0), 0u);
    EXPECT_EQ(ffv1::syntax::slice_pixel_width(stream, 0, 1), 16u);
    EXPECT_EQ(ffv1::syntax::slice_pixel_height(stream, 0, 1), 8u);
}

TEST(StreamParametersTest, MapsUnevenSliceRasterCellsWithFloorBoundaries)
{
    ffv1::syntax::StreamParameters stream;
    stream.width = 17;
    stream.height = 10;
    stream.num_h_slices = 4;
    stream.num_v_slices = 3;

    EXPECT_EQ(ffv1::syntax::slice_pixel_x(stream, 1), 4u);
    EXPECT_EQ(ffv1::syntax::slice_pixel_width(stream, 1, 2), 8u);
    EXPECT_EQ(ffv1::syntax::slice_pixel_x(stream, 3), 12u);
    EXPECT_EQ(ffv1::syntax::slice_pixel_width(stream, 3, 1), 5u);

    EXPECT_EQ(ffv1::syntax::slice_pixel_y(stream, 1), 3u);
    EXPECT_EQ(ffv1::syntax::slice_pixel_height(stream, 1, 2), 7u);
}

TEST(StreamParametersTest, UsesWideArithmeticForSliceRasterScaling)
{
    ffv1::syntax::StreamParameters stream;
    stream.width = std::numeric_limits<std::uint32_t>::max();
    stream.height = std::numeric_limits<std::uint32_t>::max();
    stream.num_h_slices = std::numeric_limits<std::uint32_t>::max();
    stream.num_v_slices = std::numeric_limits<std::uint32_t>::max();

    EXPECT_EQ(ffv1::syntax::slice_pixel_x(stream, stream.num_h_slices), stream.width);
    EXPECT_EQ(ffv1::syntax::slice_pixel_y(stream, stream.num_v_slices), stream.height);
}

} // namespace
