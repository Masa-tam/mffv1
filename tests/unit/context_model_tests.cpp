#include "ffv1/context_model.hpp"

#include <gtest/gtest.h>

namespace {

TEST(ContextModelTest, RejectsMissingContextCount)
{
    ffv1::syntax::QuantTableSet tables;
    const ffv1::syntax::ContextModel model(tables);
    std::uint32_t context = 99;

    const auto status = model.derive_context({}, context);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidState);
}

TEST(ContextModelTest, DerivesZeroContextForZeroTables)
{
    ffv1::syntax::QuantTableSet tables;
    tables.context_count = 4;
    const ffv1::syntax::ContextModel model(tables);
    std::uint32_t context = 99;

    const auto status = model.derive_context({10, 10, 10, 10}, context);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(context, 0u);
}

TEST(ContextModelTest, FoldsQuantizedGradientsIntoContextRange)
{
    ffv1::syntax::QuantTableSet tables;
    tables.context_count = 8;
    tables.tables[0][1] = 3;
    tables.tables[1][1] = 4;
    const ffv1::syntax::ContextModel model(tables);
    std::uint32_t context = 99;

    const auto status = model.derive_context({12, 10, 11, 10}, context);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(context, 7u);
}

TEST(ContextModelTest, NegativeFoldUsesMagnitude)
{
    ffv1::syntax::QuantTableSet tables;
    tables.context_count = 8;
    tables.tables[0][255] = -3;
    tables.tables[1][1] = -4;
    const ffv1::syntax::ContextModel model(tables);
    std::uint32_t context = 99;

    const auto status = model.derive_context({10, 10, 11, 11}, context);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(context, 7u);
}

} // namespace
