#include "test_vector_data.hpp"

#include "mffv1/codec.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#if !defined(NO_DEFINE_TEST_VECTOR_DATA)
namespace {

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
        EXPECT_EQ(info.planes[index].stride_bytes, expected.info.stride_bytes);

        plane_storage.emplace_back(expected_size, std::byte{0xa5});
        output_planes.push_back(
            mffv1::MutablePlaneView{plane_storage.back().data(), expected.info});
    }

    mffv1::MutableFrameView output{output_planes.data(), output_planes.size()};
    const auto status = decoder.decode_frame(frame_payload, output);
    ASSERT_TRUE(status.ok()) << status.message;

    for (std::size_t index = 0; index < expected_planes.size(); ++index) {
        const auto& expected = expected_planes[index];
        std::size_t expected_size = 0;
        ASSERT_TRUE(compute_plane_size(expected, expected_size));
        const std::vector<std::byte> expected_bytes{
            expected.samples.begin(), expected.samples.begin() + expected_size};
        EXPECT_EQ(plane_storage[index], expected_bytes);
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
    ASSERT_TRUE(configure_status.ok()) << configure_status.message;

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
