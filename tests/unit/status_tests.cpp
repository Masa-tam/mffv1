#include "mffv1/result.hpp"
#include "util/status.hpp"

#include <gtest/gtest.h>

TEST(StatusTest, OkStatusHasDefaultSuccessState)
{
    const auto status = mffv1::ok_status();

    EXPECT_TRUE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::Ok);
    EXPECT_TRUE(status.message.empty());
    EXPECT_FALSE(status.location.has_byte_offset);
    EXPECT_FALSE(status.location.has_frame_index);
    EXPECT_FALSE(status.location.has_slice_index);
    EXPECT_EQ(status.location.byte_offset, 0u);
    EXPECT_EQ(status.location.frame_index, 0u);
    EXPECT_EQ(status.location.slice_index, 0u);
}

TEST(StatusTest, MakeErrorSetsCodeAndMessageOnly)
{
    const auto status =
        mffv1::make_error(mffv1::ErrorCode::SyntaxError, "bad syntax");

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
#if MFFV1_ENABLE_STATUS_MESSAGES
    EXPECT_EQ(status.message, "bad syntax");
#else
    EXPECT_TRUE(status.message.empty());
#endif
    EXPECT_FALSE(status.location.has_byte_offset);
    EXPECT_FALSE(status.location.has_frame_index);
    EXPECT_FALSE(status.location.has_slice_index);
}

TEST(StatusTest, MakeByteErrorSetsInitialByteLocation)
{
    const auto status =
        mffv1::make_byte_error(mffv1::ErrorCode::CrcMismatch, "crc", 42);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::CrcMismatch);
#if MFFV1_ENABLE_STATUS_MESSAGES
    EXPECT_EQ(status.message, "crc");
#else
    EXPECT_TRUE(status.message.empty());
#endif
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 42u);
    EXPECT_FALSE(status.location.has_slice_index);
}

TEST(StatusTest, LocationHelpersDoNotOverwriteExistingLocations)
{
    auto status = mffv1::make_byte_error(
        mffv1::ErrorCode::SyntaxError, "nested", 7);
    mffv1::set_byte_location_if_missing(status, 99);
    mffv1::set_slice_location_if_missing(status, 3);
    mffv1::set_slice_location_if_missing(status, 5);

    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 7u);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 3u);
}
