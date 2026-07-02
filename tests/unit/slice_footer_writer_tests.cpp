#include "codec/slice_footer_writer.hpp"

#include "codec/slice_footer_parser.hpp"
#include "util/crc32.hpp"

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

namespace {

TEST(SliceFooterWriterTest, AppendsPayloadSliceSize)
{
    mffv1::syntax::StreamParameters stream;
    std::vector<std::byte> payload{
        std::byte{0xaa},
        std::byte{0xbb},
    };
    const mffv1::codec::SliceFooterWriter writer;

    const auto status = writer.append(stream, 0, payload);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(
        payload,
        (std::vector<std::byte>{
            std::byte{0xaa},
            std::byte{0xbb},
            std::byte{0x00},
            std::byte{0x00},
            std::byte{0x02},
        }));
    mffv1::syntax::SliceDescriptor descriptor;
    const mffv1::codec::SliceFooterParser parser;
    EXPECT_TRUE(parser.read_from_end(payload, stream, descriptor).ok());
}

TEST(SliceFooterWriterTest, AppendsErrorStatusAndCrcParity)
{
    mffv1::syntax::StreamParameters stream;
    stream.error_status_enabled = true;
    std::vector<std::byte> payload{
        std::byte{0xaa},
        std::byte{0xbb},
    };
    const mffv1::codec::SliceFooterWriter writer;

    const auto status = writer.append(stream, 2, payload);

    ASSERT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(payload.size(), 10u);
    EXPECT_EQ(payload[2], std::byte{0x00});
    EXPECT_EQ(payload[3], std::byte{0x00});
    EXPECT_EQ(payload[4], std::byte{0x02});
    EXPECT_EQ(payload[5], std::byte{0x02});
    EXPECT_EQ(mffv1::util::crc32_ieee_msb(payload), 0u);

    mffv1::syntax::SliceDescriptor descriptor;
    const mffv1::codec::SliceFooterParser parser;
    ASSERT_TRUE(parser.read_from_end(payload, stream, descriptor, true).ok());
    EXPECT_EQ(descriptor.error_status, 2u);
    EXPECT_TRUE(descriptor.has_crc);
}

TEST(SliceFooterWriterTest, AppendsMaximumRepresentableSliceSize)
{
    mffv1::syntax::StreamParameters stream;
    constexpr std::size_t maximum_slice_size = 0x00ffffffu;
    constexpr std::size_t footer_size = 3;
    std::vector<std::byte> payload(maximum_slice_size, std::byte{0xaa});
    const mffv1::codec::SliceFooterWriter writer;

    const auto status = writer.append(stream, 0, payload);

    ASSERT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(payload.size(), maximum_slice_size + footer_size);
    EXPECT_EQ(payload[payload.size() - 3], std::byte{0xff});
    EXPECT_EQ(payload[payload.size() - 2], std::byte{0xff});
    EXPECT_EQ(payload[payload.size() - 1], std::byte{0xff});

    mffv1::syntax::SliceDescriptor descriptor;
    const mffv1::codec::SliceFooterParser parser;
    ASSERT_TRUE(parser.read_from_end(payload, stream, descriptor).ok());
    EXPECT_EQ(descriptor.slice_size, maximum_slice_size);
}

TEST(SliceFooterWriterTest, RejectsReservedStatusWithoutChangingPayload)
{
    mffv1::syntax::StreamParameters stream;
    stream.error_status_enabled = true;
    std::vector<std::byte> payload{std::byte{0xaa}};
    const auto original = payload;
    const mffv1::codec::SliceFooterWriter writer;

    const auto status = writer.append(stream, 3, payload);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "slice error status uses a reserved value");
    EXPECT_EQ(payload, original);
}

TEST(SliceFooterWriterTest, RejectsStatusWhenEcIsDisabled)
{
    mffv1::syntax::StreamParameters stream;
    std::vector<std::byte> payload{std::byte{0xaa}};
    const auto original = payload;
    const mffv1::codec::SliceFooterWriter writer;

    const auto status = writer.append(stream, 1, payload);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "slice error status requires EC to be enabled");
    EXPECT_EQ(payload, original);
}

TEST(SliceFooterWriterTest, RejectsSliceSizeOverflowWithoutChangingPayload)
{
    mffv1::syntax::StreamParameters stream;
    constexpr std::size_t maximum_slice_size = 0x00ffffffu;
    std::vector<std::byte> payload(maximum_slice_size + 1, std::byte{0xaa});
    const auto original_size = payload.size();
    const auto first_byte = payload.front();
    const auto last_byte = payload.back();
    const mffv1::codec::SliceFooterWriter writer;

    const auto status = writer.append(stream, 0, payload);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::ResourceExhausted);
    EXPECT_EQ(status.message, "slice payload exceeds the 24-bit slice size limit");
    EXPECT_EQ(payload.size(), original_size);
    EXPECT_EQ(payload.front(), first_byte);
    EXPECT_EQ(payload.back(), last_byte);
}

} // namespace
