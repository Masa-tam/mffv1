#include "test_vector_data.hpp"

#include "codec/configuration_record_parser.hpp"
#include "codec/frame_parser.hpp"
#include "mffv1/codec.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#if !defined(NO_DEFINE_TEST_VECTOR_DATA)
namespace {

std::string describe_status(const mffv1::Status& status)
{
    std::ostringstream out;
    out << status.message;
    if (status.location.has_byte_offset) {
        out << " byte=" << status.location.byte_offset;
    }
    if (status.location.has_slice_index) {
        out << " slice=" << status.location.slice_index;
    }
    return out.str();
}

std::string describe_frame_parse(
    const mffv1_testvectors::DecodeVector& vector,
    std::span<const std::byte> frame_payload)
{
    mffv1::syntax::StreamParameters stream;
    mffv1::codec::ConfigurationRecordParser config_parser;
    auto status = config_parser.parse(vector.configuration_record, stream);
    if (!status.ok()) {
        return std::string{"config parse: "} + describe_status(status);
    }
    stream.width = vector.frame_width;
    stream.height = vector.frame_height;

    mffv1::codec::FrameParser frame_parser(stream, true);
    mffv1::codec::FrameDecodeContext frame;
    status = frame_parser.parse(frame_payload, frame);
    if (!status.ok()) {
        return std::string{"frame parse: "} + describe_status(status);
    }

    std::ostringstream out;
    out << "stream entropy="
        << (stream.entropy_mode == mffv1::EntropyMode::GolombRice ? "gr" : "range")
        << " version=" << stream.version
        << "." << stream.micro_version
        << " colorspace=" << stream.colorspace_type
        << " bits=" << static_cast<int>(stream.bits_per_raw_sample)
        << " chroma=" << stream.chroma_planes
        << " subsample=" << static_cast<int>(stream.log2_h_chroma_subsample)
        << "," << static_cast<int>(stream.log2_v_chroma_subsample)
        << " extra=" << stream.extra_plane
        << " grid=" << stream.num_h_slices << "x" << stream.num_v_slices
        << " qsets=" << stream.quant_table_sets.size();
    for (std::size_t i = 0; i < stream.quant_table_sets.size(); ++i) {
        out << " q" << i << "=" << stream.quant_table_sets[i].context_count;
    }
    out
        << " slices=" << frame.slices.size();
    for (const auto& slice : frame.slices) {
        out << " [#" << slice.index
            << " x=" << slice.x
            << " y=" << slice.y
            << " w=" << slice.width
            << " h=" << slice.height
            << " raster=" << slice.raster_x << "," << slice.raster_y
            << "+" << slice.raster_width << "x" << slice.raster_height
            << " payload=" << slice.payload_byte_offset
            << " content=" << slice.content_byte_offset
            << " footer=" << slice.footer_byte_offset
            << " size=" << slice.slice_size
            << " qidx=";
        for (const auto index : slice.quant_table_set_indexes) {
            out << index << ",";
        }
        out << "]";
    }
    return out.str();
}

bool compute_plane_size(const mffv1_testvectors::PlaneVector& plane,
                        std::size_t& out_size)
{
    EXPECT_GT(plane.info.width, 0u);
    EXPECT_GT(plane.info.height, 0u);
    EXPECT_GT(plane.info.stride_bytes, 0);
    if (plane.info.width == 0 || plane.info.height == 0 ||
        plane.info.stride_bytes <= 0) {
        return false;
    }

    const auto stride = static_cast<std::size_t>(plane.info.stride_bytes);
    const auto height = static_cast<std::size_t>(plane.info.height);
    if (stride > std::numeric_limits<std::size_t>::max() / height) {
        ADD_FAILURE() << "plane byte size overflows size_t";
        return false;
    }

    out_size = stride * height;
    if (plane.samples.size() < out_size) {
        ADD_FAILURE() << "plane samples are shorter than stride * height";
        return false;
    }
    return true;
}

std::uint8_t byte_value(std::byte value) noexcept
{
    return std::to_integer<std::uint8_t>(value);
}

std::uint32_t read_sample(std::span<const std::byte> bytes,
                          std::size_t sample_offset,
                          mffv1::SampleFormat format)
{
    if (format == mffv1::SampleFormat::UInt8) {
        return byte_value(bytes[sample_offset]);
    }
    return static_cast<std::uint32_t>(byte_value(bytes[sample_offset]))
        | (static_cast<std::uint32_t>(byte_value(bytes[sample_offset + 1])) << 8);
}

void expect_plane_matches(std::span<const std::byte> actual,
                          std::span<const std::byte> expected,
                          const mffv1::PlaneInfo& plane)
{
    ASSERT_EQ(actual.size(), expected.size());
    const auto mismatch = std::mismatch(actual.begin(), actual.end(), expected.begin());
    if (mismatch.first == actual.end()) {
        return;
    }

    const auto byte_offset = static_cast<std::size_t>(mismatch.first - actual.begin());
    const auto bytes_per_sample = plane.sample_format == mffv1::SampleFormat::UInt16
        ? std::size_t{2}
        : std::size_t{1};
    const auto stride = static_cast<std::size_t>(plane.stride_bytes);
    const auto y = byte_offset / stride;
    const auto row_byte = byte_offset % stride;
    const auto x = row_byte / bytes_per_sample;
    const auto sample_offset = byte_offset - (row_byte % bytes_per_sample);
    const auto actual_sample = read_sample(actual, sample_offset, plane.sample_format);
    const auto expected_sample = read_sample(expected, sample_offset, plane.sample_format);

    ADD_FAILURE()
        << "plane mismatch at byte " << byte_offset
        << " (x=" << x << " y=" << y << " row_byte=" << row_byte << ")"
        << " actual_byte=0x" << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(byte_value(*mismatch.first))
        << " expected_byte=0x" << std::setw(2)
        << static_cast<int>(byte_value(*mismatch.second))
        << std::dec
        << " actual_sample=" << actual_sample
        << " expected_sample=" << expected_sample
        << " width=" << plane.width
        << " height=" << plane.height
        << " stride=" << plane.stride_bytes;
}

void expect_decodes_frame(
    mffv1::IDecoder& decoder,
    const mffv1_testvectors::DecodeVector& vector,
    std::size_t frame_index,
    std::span<const std::byte> frame_payload,
    std::span<const mffv1_testvectors::PlaneVector> expected_planes)
{
    SCOPED_TRACE(frame_index);
    ASSERT_FALSE(frame_payload.empty());
    ASSERT_FALSE(expected_planes.empty());

    mffv1::FrameInfo info;
    const auto inspect_status = decoder.inspect_frame(frame_payload, info);
    ASSERT_TRUE(inspect_status.ok()) << inspect_status.message;
    EXPECT_EQ(info.width, vector.frame_width);
    EXPECT_EQ(info.height, vector.frame_height);
    ASSERT_EQ(info.plane_count, expected_planes.size());

    std::vector<std::vector<std::byte>> plane_storage;
    std::vector<mffv1::MutablePlaneView> output_planes;
    plane_storage.reserve(expected_planes.size());
    output_planes.reserve(expected_planes.size());

    for (std::size_t index = 0; index < expected_planes.size(); ++index) {
        const auto& expected = expected_planes[index];
        SCOPED_TRACE(index);
        std::size_t expected_size = 0;
        ASSERT_TRUE(compute_plane_size(expected, expected_size));
        EXPECT_EQ(info.planes[index].role, expected.info.role);
        EXPECT_EQ(info.planes[index].sample_format, expected.info.sample_format);
        EXPECT_EQ(info.planes[index].width, expected.info.width);
        EXPECT_EQ(info.planes[index].height, expected.info.height);
        EXPECT_LE(info.planes[index].stride_bytes, expected.info.stride_bytes);

        plane_storage.emplace_back(expected_size, std::byte{0xa5});
        output_planes.push_back(
            mffv1::MutablePlaneView{plane_storage.back().data(), expected.info});
    }

    mffv1::MutableFrameView output{output_planes.data(), output_planes.size()};
    const auto status = decoder.decode_frame(frame_payload, output);
    ASSERT_TRUE(status.ok()) << describe_status(status) << "\n"
                             << describe_frame_parse(vector, frame_payload);

    for (std::size_t index = 0; index < expected_planes.size(); ++index) {
        const auto& expected = expected_planes[index];
        std::size_t expected_size = 0;
        ASSERT_TRUE(compute_plane_size(expected, expected_size));
        const std::vector<std::byte> expected_bytes{
            expected.samples.begin(), expected.samples.begin() + expected_size};
        expect_plane_matches(plane_storage[index], expected_bytes, expected.info);
    }
}

void expect_decodes_vector(const mffv1_testvectors::DecodeVector& vector)
{
    SCOPED_TRACE(vector.name);
    ASSERT_GT(vector.frame_width, 0u);
    ASSERT_GT(vector.frame_height, 0u);
    ASSERT_FALSE(vector.configuration_record.empty());
    ASSERT_FALSE(vector.frame_payloads.empty());
    ASSERT_FALSE(vector.expected_planes.empty());
    ASSERT_EQ(vector.frame_payloads.size(), vector.expected_planes.size());

    mffv1::DecoderOptions options;
    options.frame_width = vector.frame_width;
    options.frame_height = vector.frame_height;
    auto decoder = mffv1::create_decoder(options);
    ASSERT_TRUE(decoder.status.ok()) << decoder.status.message;
    ASSERT_NE(decoder.decoder, nullptr);
    const auto configure_status =
        decoder.decoder->configure(vector.configuration_record);
    ASSERT_TRUE(configure_status.ok()) << describe_status(configure_status);

    for (std::size_t frame_index = 0;
         frame_index < vector.frame_payloads.size();
         ++frame_index) {
        expect_decodes_frame(
            *decoder.decoder,
            vector,
            frame_index,
            vector.frame_payloads[frame_index],
            vector.expected_planes[frame_index]);
    }
}

} // namespace
#endif

TEST(TestVectorTest, GeneratedVectorsAreAvailable)
{
#if defined(NO_DEFINE_TEST_VECTOR_DATA)
    GTEST_SKIP() << "external FFV1 test vectors have not been generated";
#else
    ASSERT_FALSE(mffv1_testvectors::decode_vectors().empty());
#endif
}

TEST(TestVectorTest, GeneratedVectorsDecodeThroughPublicApi)
{
#if defined(NO_DEFINE_TEST_VECTOR_DATA)
    GTEST_SKIP() << "external FFV1 test vectors have not been generated";
#else
    for (const auto& vector : mffv1_testvectors::decode_vectors()) {
        expect_decodes_vector(vector);
    }
#endif
}
